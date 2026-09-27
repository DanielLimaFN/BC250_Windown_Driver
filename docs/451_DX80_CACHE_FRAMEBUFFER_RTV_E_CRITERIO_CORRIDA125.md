# Corrida 125 — Candidato DX.80: Cache de Framebuffers de RTV para Rotação de Swapchain Sem Flush

> **Data:** 25/09/2026  
> **Candidato:** DX.80 / `3.284.1.0` (INF fonte `3.284.0.0`)  
> **Objetivo:** Eliminar os 369 flushes síncronos forçados pela rotação de backbuffers da swapchain em `Bc250UmdVkRtvFollowResource`, implementando um cache por-RTV de até 8 framebuffers e image views (`FbCache`). Quando o DWM rotaciona entre os backbuffers da swapchain, o framebuffer correspondente é reutilizado instantaneamente com **zero flushes** e **zero esperas de fence**, aproximando o DWM da razão ideal de exatamente 1.0 flush por quadro apresentado e acelerando a taxa de composição rumo a 10–20+ FPS.

---

## 1. Contexto e Diagnóstico da Corrida 124

Na [Corrida 124](450_CORRIDA124_DX79_UPLOAD_COALESCENCIA_E_RESULTADO.md) (DX.79):
- A coalescência de uploads no anel de staging foi **validada com sucesso absoluto no silício**:
  - 666 de 672 uploads de textura executaram com `FlushSincrono=0` (taxa de coalescência de **99,1%**).
  - Flushes síncronos de upload despencaram de 504 para apenas **6** (redução de **98,8%**).
  - O DWM atingiu novo recorde histórico: **935 Presents** em 171,48 s (**5,45 Presents/s sustentados**, +30% em relação à Corrida 123 e +103% em relação à Corrida 118).
  - O KMD concluiu 1.990 blts de composição e 1.098 flips de buffer.
  - O conformance harness concluiu 60/60 quadros (`PRESENT_OK`, pico de 1,7 FPS).
  - Rollback online restaurou `BasicDisplay` Código 0 em 5/5 amostras (R1) e desktop/triângulo coloridos visualmente íntegros.

- **O Último Grande Gargalo de Flush Síncrono Revelado pela Mineração:**
  - Dos 1.462 flushes totais registrados no log do DWM na Corrida 124:
    - `PresentDestile` (`Bc250UmdPresentVulkanDetileAndScanout`): 935 flushes (1 por frame, ideal para apresentação física no scanout).
    - `Bc250UmdVkRtvFollowResource`: **369 flushes síncronos** (25,2% de todos os flushes do DWM).
    - `ExecuteVkDraw` (lote > 64 draws): 146 flushes.
    - Wrap de staging de upload: 6 flushes.
    - `ResourceMap` DISCARD: 6 flushes.
  - A análise de rastreamento confirmou que o DWM alterna periodicamente entre diferentes `VkImage` backbuffers da swapchain (`DXGI_SWAP_EFFECT_FLIP_DISCARD` / `SEQUENTIAL`).
  - No código anterior do DX.79, toda vez que a imagem do recurso mudava (`pRtv->VkImage != pResource->VkImage`), a função `Bc250UmdVkRtvFollowResource`:
    1. Forçava `Bc250UmdVkFlushCommands(pBridge)` com espera de fence na GPU.
    2. Destruía o `VkFramebuffer` e o `VkImageView` anteriores via `vkDestroyFramebuffer` / `vkDestroyImageView`.
    3. Criava um novo `VkImageView` e um novo `VkFramebuffer` para o novo backbuffer.
  - Essa destruição/recriação a cada rotação de buffer impôs **369 interrupções síncronas com espera de fence**, paralisando o render loop do DWM e limitando a composição a ~5,45 quadros/segundo.

---

## 2. Alterações Implementadas no Candidato DX.80

1. **Estrutura de Cache de Framebuffers por RTV (`BC250_UMD_RTV`):**
   - Em [`driver/umd/bc250_d3d_umd.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.h):
     ```c
     #define BC250_RTV_MAX_CACHED_FBS 8u

     typedef struct _BC250_UMD_RTV_CACHED_FB {
         VkImage       Image;
         VkImageView   View;
         VkFramebuffer Fb;
     } BC250_UMD_RTV_CACHED_FB;

     // Na struct BC250_UMD_RTV:
     BC250_UMD_RTV_CACHED_FB FbCache[BC250_RTV_MAX_CACHED_FBS];
     uint32_t                FbCacheCount;
     uint32_t                FollowHitCount;
     uint32_t                FollowMissCount;
     ```

2. **Inicialização do Cache no `CreateRtv`:**
   - Em [`driver/umd/bc250_umd_resource_vk.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_resource_vk.c):
     - `pRtv->FbCache[0]` recebe a imagem inicial, sua view e seu framebuffer criados no bind primário.
     - `pRtv->FbCacheCount = 1`.

3. **Reutilização Instantânea Sem Flush em `Bc250UmdVkRtvFollowResource`:**
   - Ao detectar mudança de imagem do recurso:
     - Itera sobre `pRtv->FbCache[0..FbCacheCount-1]`.
     - **Caso Cache Hit:**
       - Se o render pass ativo estiver vinculado ao framebuffer antigo, fecha o render pass (`Bc250UmdVkEndActiveRenderPass`).
       - Aponta `pRtv->VkFb = pRtv->FbCache[i].Fb` e `pRtv->VkView = pRtv->FbCache[i].View`.
       - Atualiza `pRtv->VkImage = pResource->VkImage`.
       - Incrementa `FollowHitCount`.
       - **ZERO flushes, ZERO esperas de fence na GPU**.
     - **Caso Cache Miss:**
       - Cria nova view e novo framebuffer.
       - Se houver espaço (`FbCacheCount < BC250_RTV_MAX_CACHED_FBS`), adiciona ao cache sem destruir os anteriores.
       - Se o cache estiver cheio (8 entradas), drena com segurança o command buffer com fence, destrói a entrada mais antiga e insere a nova.
       - Incrementa `FollowMissCount`.
   - Telemetria dedicada `[DX80_CACHE]` emite amostras diagnósticas registrando `Hit`, `Miss`, `CacheCount` e se houve flush síncrono.

4. **Desalocação Completa e Limpa no `DestroyRtv`:**
   - Em [`driver/umd/bc250_umd_resource_vk.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_resource_vk.c):
     - Drena o command buffer ativo com fence antes da destruição.
     - Itera por todas as entradas `FbCache[i]` destruindo `VkFramebuffer` e `VkImageView` associados.
     - Zera contadores e ponteiros, evitando vazamento de recursos Vulkan ou falhas de objeto dangling.

5. **Auditorias e Testes de Mutação:**
   - [`audit-bc250mcdm-safety.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/audit-bc250mcdm-safety.py) recebeu o portão `audit_dx80_rtv_framebuffer_cache` validando constantes, estruturas, hit checks, telemetria e teardown.
   - Suite de mutação [`test-dx80-rtv-cache-mutations.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/test-dx80-rtv-cache-mutations.py) aprovada com **8/8 mutações reprovadas**.
   - Suite [`test-dx49-throughput-mutations.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/test-dx49-throughput-mutations.py) aprovada com **19/19 mutações reprovadas**.
   - Compilações de KMD (`build-contract.bat`), UMD (`build-umd.bat`) e imagem lab (`build-bc250mcdm-lab-bringup.ps1`) com **EXIT 0**.

---

## 3. Identificação do Pacote e Hashes de Controle

- **DriverVer Empacotado:** `09/25/2026,3.284.1.0` (INF fonte `3.284.0.0`)
- **Perfil:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `P1 PERSISTENT SUBMISSION`
- **Hashes SHA-256:**
  - `bc250mcdm.sys`: `4F644E2A84501DC8207A18C57440F5A75DD283B4FB228FFCC76B6EEA7C86B66C`
  - `bc250mcdm.inf`: `0EF08056BBD330A6DA97BC713BB00809CFFA54C5BBA1766F0BC2D650DF2C8889`
  - `bc250mcdm.cat`: `CEAD130C9BB5B7679E3F6D1DFAAFCACCFF14699A8923415ADBD8F49B968ED9DF`
  - `bc250umd.dll`: `16A69E1A1F86CA0EA731446415F1A4F223A666A4584AC1934AF1B5EE046147B8`
  - RADV externo: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- **Validação Offline:**
  - `driver\wdk\build-contract.bat`: **EXIT 0**.
  - `driver\umd\build-umd.bat`: **EXIT 0**.
  - `python tools\audit-bc250mcdm-safety.py`: **PASS**.
  - `powershell tools\audit-bc250mcdm-lab-package.ps1`: **PASS**.
  - `python tools\test-dx80-rtv-cache-mutations.py`: **PASS (8/8 mutações reprovadas)**.
  - `powershell tools\prepare-corrida125-dx80.ps1`: **PASS**.

---

## 4. Critérios de Avaliação da Corrida 125

| Desfecho | Critério | Interpretação |
|---|---|---|
| **A (Avanço Pleno)** | (1) Janela de 180 s sem travamento, TDR ou BugCheck;<br>(2) Desktop legível e triângulo visível na janela do harness sem artefatos visuais;<br>(3) Conformance harness com 60/60 `PRESENT_OK`;<br>(4) Log do DWM comprova eliminação massiva dos flushes de rotação de RTV (`FollowHitCount > 0` dominante, flushes em `Bc250UmdVkRtvFollowResource` caem de 369 para ~0–2);<br>(5) Razão global de flushes do DWM converge para ~1.0 flush/frame;<br>(6) Taxa de apresentação do DWM supera os 5,45 Presents/s da Corrida 124 (alvo: > 8–15+ Presents/s);<br>(7) Rollback online restaura `BasicDisplay` Código 0 em 5/5 amostras (R1). | Avanço funcional e de performance expressivo: eliminação completa das esperas síncronas de rotação de swapchain validada no silício. |
| **B (Avanço Parcial / Cache Funcional com Latência de Scanout)** | Mesmos critérios 1, 2, 3, 4, 5 e 7 do Desfecho A, mas a taxa de apresentação do DWM se mantém entre 5,5 e 8 Presents/s devido à latência de swapchain/blt no KMD ou escalonamento de swapchain do dxgkrnl. | Avanço arquitetural comprovado (cache de RTV elimina flushes síncronos), isolando o próximo gargalo no caminho de scanout do KMD. |
| **C (Falha Visual / Framebuffer Corrompido)** | Execução sem crash, mas com desktop congelando, piscando ou com sobreposição de quadros anteriores por inconsistência entre views/framebuffers em cache. | Falha de sincronização de barreiras de imagem no cache; análise dos logs `[DX80_CACHE]`. |
| **D (Falha Operacional / Crash / Hang)** | BugCheck, TDR, travamento duro antes dos 180 s ou falha no rollback R1. | Regressão de estabilidade; análise de dump e telemetria de fence. |

---

## 5. Procedimento Seguro de Execução

1. **Reinicialização a Frio com Corte de Energia:**
   - Desligar completamente o host, aguardar 10 segundos para descarga capacitiva da VRAM GDDR6 e ligar novamente.
2. **Executar o Runner Preparado:**
   - Abrir prompt de comando elevado e executar:
     ```cmd
     tools\EXECUTAR_CORRIDA125_DX80_180S.cmd
     ```
3. **Observação e Coleta:**
   - Observar desktop, janela do triângulo e aguardar os 180 s até o rollback do watchdog para `BasicDisplay` Código 0.
   - Em caso de travamento duro, acionar `Ctrl Direito + Scroll Lock + Scroll Lock` para gravação do dump.
