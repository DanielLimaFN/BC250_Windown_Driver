# Corrida 126 — Candidato DX.80b: Correção de Double Free no Teardown de RTV e Critério

> **Data:** 25/09/2026  
> **Candidato:** DX.80b / `3.285.1.0` (INF fonte `3.285.0.0`)  
> **Objetivo:** Eliminar o crash do DWM causado por Double Free em `Bc250UmdVkDestroyRtv`, garantindo destruição única (`Single-Destroy Guarantee`) de todos os handles de `VkFramebuffer` e `VkImageView` em cache, sustentando o ganho arquitetural comprovado no silício na Corrida 125 (6 Cache Hits consecutivos com zero flushes síncronos) ao longo de toda a janela de 180 s.

---

## 1. Contexto e Diagnóstico da Corrida 125

Na [Corrida 125](452_CORRIDA125_DX80_DOUBLE_FREE_TEARDOWN_E_RESULTADO.md) (DX.80):
- **O Cache Funcionou Perfeitamente:**
  - O DWM registrou 3 backbuffers em cache (`Count=3`) e atingiu **6 Cache Hits consecutivos** (`Hits=1..6 Misses=2`), executando rotações de swapchain com ZERO flushes e zero esperas de fence na GPU.
  - A janela do triângulo Direct3D 11 abriu e executou 16 quadros com sucesso (`PRESENT_OK`, pico de 1,8 FPS).
- **A Causa-Raiz do Crash:**
  - Às 15:06:05.831, o DWM chamou `Bc250UmdVkDestroyRtv`:
    ```text
    15:06:05.831 [pid=1208 dwm.exe] [VK_RES] Bc250UmdVkDestroyRtv: view=0000000005E166B0, passClear=0000000003ACA840, passLoad=0000000003ACAAE0, fb=00000000005404E0 (CachedFbs=1)
    ```
  - A função executava o loop por `pRtv->FbCache`, destruindo `FbCache[0].Fb` (`0x005404E0`) e `FbCache[0].View` (`0x05E166B0`).
  - Em seguida, fora do loop, a função tentava destruir novamente `pRtv->VkFb` e `pRtv->VkView`, que ainda continham os mesmos ponteiros `0x005404E0` e `0x05E166B0`.
  - Essa chamada redundante a `vkDestroyFramebuffer` / `vkDestroyImageView` causou violação de acesso imediata na DLL do driver Vulkan (RADV) e derrubou o `dwm.exe`.
  - A queda do DWM gerou `DXGI_ERROR_DEVICE_REMOVED` (`0x887A0005`) no harness no quadro 17, acionando o rollback de segurança.

---

## 2. Alterações Implementadas no Candidato DX.80b

1. **Garantia de Destruição Única em `Bc250UmdVkDestroyRtv`:**
   - Em [`driver/umd/bc250_umd_resource_vk.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_resource_vk.c):
     - Ao percorrer o array `pRtv->FbCache`, se o item destruir o handle que coincide com `pRtv->VkFb` ou `pRtv->VkView`, estes são imediatamente anulados (`VK_NULL_HANDLE`).
     - Quaisquer duplicações subsequentes no array têm seus handles desarmados antes da chamada de destruição.
     - Ao término do loop, `pRtv->VkFb = VK_NULL_HANDLE` e `pRtv->VkView = VK_NULL_HANDLE` são garantidamente nulos.
     - As destruições de `VkPassLoad` e `VkPass` também ganharam salvaguarda contra handles coincidentes.
     - O bloco de destruição redundante de `VkFb` e `VkView` ao final foi removido.

2. **Auditorias e Testes de Mutação:**
   - [`audit-bc250mcdm-safety.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/audit-bc250mcdm-safety.py) aprovado (**PASS**).
   - Suite de mutação [`test-dx80-rtv-cache-mutations.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/test-dx80-rtv-cache-mutations.py) aprovada (**8/8 mutações reprovadas**).
   - Compilações de KMD, UMD e lab bringup com **EXIT 0**.

---

## 3. Identificação do Pacote e Hashes de Controle

- **DriverVer Empacotado:** `09/25/2026,3.285.1.0` (INF fonte `3.285.0.0`)
- **Perfil:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `P1 PERSISTENT SUBMISSION`
- **Hashes SHA-256:**
  - `bc250mcdm.sys`: `C100449DC9A36E2F07E3C5739D0D5E684FDBD5A8C9A25299CB9BB3394CCD406D`
  - `bc250mcdm.inf`: `12F5B713D97B482361BD3379745B394BE1269B9B2B6FC25279389DC3510F43F9`
  - `bc250mcdm.cat`: `107CE5904E75D8E5FA353B0395FD4DAFE08DD57F23AD0697865F914A2A0F6D92`
  - `bc250umd.dll`: `E2DE8A312C8BAAF0487F3E10148B121CE8003EDDB3D80794ED949238992BF918`
  - RADV externo: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- **Validação Offline:**
  - `python tools\audit-bc250mcdm-safety.py`: **PASS**.
  - `powershell tools\audit-bc250mcdm-lab-package.ps1`: **PASS**.
  - `python tools\test-dx80-rtv-cache-mutations.py`: **PASS (8/8 mutações reprovadas)**.
  - `powershell tools\prepare-corrida126-dx80b.ps1`: **PASS**.

---

## 4. Critérios de Avaliação da Corrida 126

| Desfecho | Critério | Interpretação |
|---|---|---|
| **A (Avanço Pleno)** | (1) Janela de 180 s sem travamento, crash de DWM, TDR ou BugCheck;<br>(2) Desktop legível e triângulo visível na janela do harness sem artefatos visuais;<br>(3) Conformance harness com 60/60 `PRESENT_OK` (completando todos os quadros sem `DeviceRemovedReason`);<br>(4) Log do DWM comprova eliminação massiva dos flushes de rotação de RTV (`FollowHitCount > 0` dominante, flushes em `Bc250UmdVkRtvFollowResource` reduzidos a ~0–2);<br>(5) Teardown de RTV opera sem falhas (`Bc250UmdVkDestroyRtv` conclui limpo sem double free);<br>(6) Taxa de apresentação do DWM supera os 5,45 Presents/s da Corrida 124 (alvo: > 8–15+ Presents/s);<br>(7) Rollback online restaura `BasicDisplay` Código 0 em 5/5 amostras (R1). | Avanço funcional e de performance expressivo: eliminação completa das esperas síncronas de swapchain e ciclo de vida de RTV estabilizado no silício. |
| **B (Avanço Parcial / Taxa Controlada por Scanout)** | Mesmos critérios 1, 2, 3, 4, 5 e 7 do Desfecho A, mas a taxa de apresentação do DWM se mantém entre 5,5 e 8 Presents/s devido à latência de scanout/blt no KMD. | Avanço arquitetural comprovado (cache e ciclo de vida de RTV consolidados), isolando o próximo gargalo na camada de scanout. |
| **C (Falha Visual / Inconsistência de Imagem)** | Execução sem crash, mas com desktop congelando, piscando ou com sobreposição de quadros anteriores por inconsistência entre views/framebuffers em cache. | Falha de sincronização de barreiras de imagem no cache; análise dos logs `[DX80_CACHE]`. |
| **D (Falha Operacional / Crash / Hang)** | BugCheck, TDR, novo crash de DWM ou falha no rollback R1. | Regressão de estabilidade; análise de dump e telemetria. |

---

## 5. Procedimento Seguro de Execução

1. **Reinicialização a Frio com Corte de Energia:**
   - Desligar completamente o computador, aguardar 10 segundos para descarga capacitiva da VRAM GDDR6 e ligar novamente.
2. **Executar o Runner Preparado:**
   - Abrir prompt de comando elevado e executar:
     ```cmd
     tools\EXECUTAR_CORRIDA126_DX80B_180S.cmd
     ```
3. **Observação e Coleta:**
   - Observar o desktop, a janela do triângulo e aguardar os 180 s até o rollback do watchdog para `BasicDisplay` Código 0.
   - Em caso de congelamento, acionar `Ctrl Direito + Scroll Lock + Scroll Lock` para gravação do dump.
