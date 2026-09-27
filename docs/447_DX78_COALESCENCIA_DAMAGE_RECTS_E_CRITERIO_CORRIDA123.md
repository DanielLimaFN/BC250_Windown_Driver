# Corrida 123 — Candidato DX.78: Coalescência de Damage Rects no Command Buffer e Eliminação de Flushes Síncronos

> **Data:** 25/09/2026  
> **Candidato:** DX.78 / `3.282.1.0` (INF fonte `3.282.0.0`)  
> **Objetivo:** Eliminar os flushes síncronos redundantes em `Bc250UmdVkCopyImageInternal` (chamado por `Bc250Umd10ResourceCopyRegion`), permitindo que a cópia de damage rects (regiões sujas do desktop copiadas pelo DWM) coalesça no mesmo `VkCommandBuffer` que os draws e o destile, reduzindo ~2.265 flushes síncronos (~9,74 s de espera pura de CPU em fences) para ~1 flush consolidado por Present, acelerando a taxa de composição do DWM rumo a 15–30+ FPS.

---

## 1. Contexto e Diagnóstico da Corrida 122

Na [Corrida 122](446_CORRIDA122_DX77_COALESCENCIA_MULTIDRAW_E_RESULTADO.md) (DX.77):
- A coalescência de multi-draws no caminho direto da UMD foi **validada com sucesso no silício**:
  - Drenos incondicionais caíram de 4.865 para **ZERO** (`0`).
  - Drenos totais do DWM caíram para apenas **27** (eliminação de 99,4% dos stalls de sincronização em draws).
  - Draws diretos do DWM foram agrupados em lotes de 2 (608x), 3 (440x), 4 (120x), 5 (162x) até 20 draws por command buffer.
  - Conformance harness concluiu 60/60 quadros em **41,64 s** (recorde histórico, pico de 1,8 FPS).
  - Total de Presents do DWM subiu de 561 para **840** (5,07 Presents/s sustentados; 4,83/s nos primeiros 120 s, +62,5% sobre a Corrida 121).
  - Rollback para `BasicDisplay` Código 0 em 5/5 amostras (R1) e triângulo colorido visualmente íntegro.

- **O Novo Gargalo Dominante Revelado pela Mineração:**
  - A análise detalhada de `bc250_umd_log_1212-FINAL.txt` revelou que, mesmo com os draws coalescidos, o DWM ainda executou **3.874 flushes com fence wait** em 165 segundos (~4,6 flushes por Present).
  - A decomposição de chamadores expôs:
    - `ResourceCopyRegion` / `Bc250UmdVkCopyImageInternal`: **2.265 flushes (58,5% de todos os flushes do DWM!)**.
    - `ResourceUpdateSubresourceUP` (`UploadImageData` de superfícies): 1.207 flushes.
    - `PresentDestile` (`Bc250UmdPresentVulkanDetileAndScanout`): 385 flushes.
    - `ExecuteVkDraw` (lotes atingindo teto de 64): 23 flushes.
    - `ResourceMap DISCARD`: 13 flushes.
  - Antes de cada chamada a `Present`, o DWM copia aproximadamente 2 a 4 damage rects (regiões sujas alteradas) de seu backbuffer para a superfície a ser apresentada. A cada retângulo copiado, a linha 2412 de [`driver/umd/bc250_d3d_umd.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.c#L2412) executava um `Bc250UmdVkFlushCommands` síncrono incondicional.
  - Multiplicando 2.265 flushes por ~4,3 ms de latência média de submit/wait, foram **~9,74 segundos de puro bloqueio de CPU aguardando fences da GPU** durante a sessão.

---

## 2. Alterações Implementadas no Candidato DX.78

1. **Encerramento Seguro de Render Pass em `Bc250UmdVkCopyImageInternal`:**
   - De acordo com a especificação Vulkan, `vkCmdCopyImage` e `vkCmdPipelineBarrier` para transferência só podem ser gravados fora de uma instância ativa de render pass.
   - Foi inserida a guarda:
     ```c
     if (device->InRenderPass) {
         device->VkBridge.vk.vkCmdEndRenderPass(cmdBuf);
         device->InRenderPass = FALSE;
     }
     ```
   - Garante que qualquer render pass aberto por draws ou fast-clears anteriores seja fechado antes da gravação das barreiras e cópias de imagem.

2. **Coalescência de Damage Rects sem Flush Síncrono:**
   - Substituição do `Bc250UmdVkFlushCommands` incondicional por acumulação no command buffer ativo:
     ```c
     device->VkBridge.PendingDraws++;
     const uint32_t batchPos = device->VkBridge.PendingDraws;
     const BOOL flushNow = (batchPos >= BC250_DX49_BATCH_MAX_DRAWS);
     if (flushNow) {
         if (Bc250UmdVkFlushCommands(&device->VkBridge) != VK_SUCCESS) {
             src->VkLayout = srcHasContent ? srcOld : VK_IMAGE_LAYOUT_UNDEFINED;
             dst->VkLayout = VK_IMAGE_LAYOUT_UNDEFINED;
             Bc250UmdTrace("[VK_BRIDGE] ResourceCopyImage FALHOU no flush: src=%p dst=%p; destino sem conteudo declarado",
                 (void*)src, (void*)dst);
             return FALSE;
         }
     }
     src->VkLayout = srcHasContent ? srcOld : VK_IMAGE_LAYOUT_UNDEFINED;
     dst->VkLayout = dstFinal;
     ```
   - As cópias de retângulos danificados agora permanecem gravadas sequencialmente no command buffer junto aos draws precedentes.
   - A submissão física ocorre de forma unificada no `Bc250DxgiPresent` / `Bc250UmdPresentVulkanDetileAndScanout`, submetendo Draws + Cópias + Destile em **uma única submissão e uma única espera de fence por quadro**.

3. **Preservação Estrita do Contrato de Rastreamento de Layouts Vulkan:**
   - As barreiras de transição (`srcOld -> TRANSFER_SRC -> srcFinal` e `dstOld -> TRANSFER_DST -> dstFinal`) mantêm `VkLayout` perfeitamente sincronizado com o fluxo de comandos da GPU.
   - Todos os 7 requisitos da auditoria de segurança [`audit_dx64_copy_tracks_vk_layout`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/audit-bc250mcdm-safety.py) permanecem 100% cumpridos.

---

## 3. Identificação do Pacote e Hashes de Controle

- **DriverVer Empacotado:** `09/25/2026,3.282.1.0` (INF fonte `3.282.0.0`)
- **Perfil:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `P1 PERSISTENT SUBMISSION`
- **Hashes SHA-256:**
  - `bc250mcdm.sys`: `7BDCA67C06FB3270A9F5D83E83F4EE21AE306128AA7D2756D2B74C1639F6DB40`
  - `bc250mcdm.inf`: `375E316F3794566D9502FC4B2CB9F1AB70611E6B4C874313E918ED46220CB055`
  - `bc250mcdm.cat`: `4F3408FAF4C21B98CE76FAC93557A173E0130A8DFD2F93BF1CBEFE91E15BB901`
  - `bc250umd.dll`: `DBCF1DF57A06B6C0589928EDB8A04C903ACA2F12C375ACA0C0D7D09707FA6F39`
  - RADV externo: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- **Validação Offline:**
  - `driver\wdk\build-contract.bat`: **EXIT 0** (40+ testes aprovados).
  - `driver\umd\build-umd.bat`: **EXIT 0** (DLLs e harnesses compilados e linkados).
  - `python tools\audit-bc250mcdm-safety.py`: **PASS**.
  - `powershell tools\audit-bc250mcdm-lab-package.ps1`: **PASS**.
  - `powershell tools\prepare-corrida123-dx78.ps1`: **PASS**.
  - `tools\EXECUTAR_CORRIDA123_DX78_180S.cmd`: **CRLF estrito validado**.

---

## 4. Critérios de Avaliação da Corrida 123

| Desfecho | Critério | Interpretação |
|---|---|---|
| **A (Avanço Pleno)** | (1) Janela de 180 s sem travamento, TDR ou BugCheck;<br>(2) Desktop legível e triângulo visível na janela do harness sem artefatos;<br>(3) Conformance harness com 60/60 `PRESENT_OK`;<br>(4) Log do DWM comprova eliminação dos flushes de `ResourceCopyImage` (flushes de cópia caem de 2.265 para ~0 fora do estouro de lote, `FlushAgora=0` na maioria das chamadas);<br>(5) Taxa de apresentação do DWM supera expressivamente os 5,07 Presents/s da Corrida 122 (alvo: > 8–15 Presents/s);<br>(6) Rollback online restaura `BasicDisplay` Código 0 em 5/5 amostras (R1). | Avanço funcional e de performance expressivo: eliminação de mais da metade dos bloqueios de sincronização do DWM validada em silício. |
| **B (Avanço Parcial / Coalescência com Outro Gargalo)** | Mesmos critérios 1, 2, 3, 4 e 6 do Desfecho A, mas a taxa de apresentação do DWM fica entre 5 e 8 Presents/s devido à latência do `pfnPresentCb` / VidMm scheduler. | Avanço arquitetural comprovado (coalescência de damage rects funcional), isolando o próximo gargalo na apresentação ou nos uploads de textura. |
| **C (Falha Visual / Conflito de Buffers)** | Execução sem crash, mas com corrupção de janelas, desktop piscando ou triângulo distorcido por inconsistência de layout nas cópias coalescidas. | Falha de barreira ou sincronização entre cópia de dano e destile; análise dos traces de layout. |
| **D (Falha Operacional / Crash / Hang)** | BugCheck, TDR, travamento duro antes dos 180 s ou falha no rollback R1. | Regressão de estabilidade; análise de dump e telemetria de fence. |

---

## 5. Procedimento de Execução Física

1. **Reinicialização a Frio Obrigatória:** Como a Corrida 122 realizou rollback, `C:\BC250_REQUIRE_CLEAN_BOOT.json` exige boot frio com corte de energia antes do novo bind.
2. Após o boot frio, executar como Administrador:
   ```cmd
   tools\EXECUTAR_CORRIDA123_DX78_180S.cmd
   ```
3. Observar visualmente o desktop e a janela do harness durante a janela de 180 s e aguardar a restauração automática do `BasicDisplay` pelo watchdog.
