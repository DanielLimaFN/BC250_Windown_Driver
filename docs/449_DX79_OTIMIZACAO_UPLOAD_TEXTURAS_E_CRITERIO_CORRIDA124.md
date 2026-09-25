# Corrida 124 — Candidato DX.79: Coalescência de Uploads de Textura no Command Buffer e Anel de Staging

> **Data:** 25/09/2026  
> **Candidato:** DX.79 / `3.283.1.0` (INF fonte `3.283.0.0`)  
> **Objetivo:** Eliminar os flushes síncronos redundantes em `Bc250UmdVkUploadImageData` (chamado por `ResourceUpdateSubresourceUP`), implementando um anel de subalocação (staging ring buffer com alinhamento de 256 bytes) em `UploadStagingBuf`. Com isso, múltiplos uploads de superfícies e dirty boxes do DWM coalescem no command buffer ativo sem emitir flush síncrono nem travar a CPU em esperas de fences a cada imagem, reduzindo ~504 flushes síncronos (~3,5 s de espera pura de CPU) para ~1 flush consolidado por Present, impulsionando a taxa de composição do DWM rumo a 15–30+ FPS.

---

## 1. Contexto e Diagnóstico da Corrida 123

Na [Corrida 123](448_CORRIDA123_DX78_COALESCENCIA_DAMAGE_RECTS_E_RESULTADO.md) (DX.78):
- A coalescência de damage rects no command buffer direto foi **validada com sucesso no silício**:
  - Flushes síncronos de `ResourceCopyImage` caíram de 2.265 para **ZERO** (`0`).
  - Total de flushes do DWM despencou de 3.874 para **1.255** (redução drástica de 67,6% nos pontos de sincronização).
  - Conformance harness concluiu 60/60 quadros em **39,47 s** (~1,52 FPS sustentados, com picos de 1,8 FPS).
  - Total de Presents do DWM atingiu **719** em 178 s (~4,04 Presents/s sustentados).
  - Rollback online restaurou `BasicDisplay` Código 0 em 5/5 amostras (R1) e triângulo colorido visualmente íntegro.

- **O Gargalo Remanescente Revelado pela Mineração:**
  - Dos 1.255 flushes totais do DWM na Corrida 123:
    - `PresentDestile` (`Bc250UmdPresentVulkanDetileAndScanout`): 719 flushes (1 por frame, ideal para apresentação física).
    - `Bc250UmdVkUploadImageData`: **504 flushes síncronos**.
    - `ExecuteVkDraw` (estouro de lote de 64): 23 flushes.
    - `ResourceMap DISCARD`: 9 flushes.
  - A análise de telemetria revelou que o DWM realizou 505 chamadas de upload de textura. Dessas, **248 chamadas eram pequenos sub-retângulos sujos** (dirty boxes como 5x865, 10x727, 7x14, 16x19).
  - No entanto, devido à ausência de cursor de alocação em `UploadStagingBuf`, cada upload forçava um `Bc250UmdVkFlushCommands` síncrono incondicional logo após a emissão de `vkCmdCopyBufferToImage`.
  - Cada flush síncrono impunha `SubmitUs` ~4,5 ms + `FimCbUs` ~1,1 ms + espera de fence, totalizando **~3,5 segundos de CPU completamente paralisada** aguardando a GPU apenas para transferir texturas de suporte de composição.

---

## 2. Alterações Implementadas no Candidato DX.79

1. **Estrutura de Anel de Staging com Cursor e Alinhamento:**
   - Adicionados à estrutura `BC250_UMD_VK_BRIDGE` em [`driver/umd/bc250_umd_vulkan_bridge.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_vulkan_bridge.h):
     ```c
     VkDeviceSize UploadStagingHead;
     uint32_t UploadCoalescedCount;
     uint32_t UploadFlushCount;
     #define BC250_DX79_UPLOAD_ALIGN 256u
     ```
   - O alinhamento de 256 bytes respeita os requisitos de alinhamento de buffer-to-image copy da arquitetura RDNA/GFX10.

2. **Subalocação com Offset Dinâmico em `Bc250UmdVkUploadImageData`:**
   - Em [`driver/umd/bc250_umd_resource_vk.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_resource_vk.c):
     ```c
     const VkDeviceSize alignedHead = (pBridge->UploadStagingHead + (BC250_DX79_UPLOAD_ALIGN - 1u)) &
                                     ~(VkDeviceSize)(BC250_DX79_UPLOAD_ALIGN - 1u);
     BOOL wrapNeeded = (alignedHead + (VkDeviceSize)ByteSize > pBridge->UploadStagingSize);
     if (wrapNeeded) {
         res = Bc250UmdVkFlushCommands(pBridge);
         // StagingHead reseta para 0 no flush com fence
     }
     const VkDeviceSize uploadOffset = wrapNeeded ? 0 : alignedHead;
     pBridge->UploadStagingHead = uploadOffset + (VkDeviceSize)ByteSize;
     region.bufferOffset = uploadOffset;
     ```
   - O payload da imagem é copiado na CPU para `(uint8_t*)pBridge->UploadStagingCpu + uploadOffset`.
   - O comando de transferência `vkCmdCopyBufferToImage` é gravado apontando para `region.bufferOffset = uploadOffset`.

3. **Eliminação do Flush Síncrono Obrigatório (Coalescência Ativa):**
   - O comando de cópia permanece gravado no command buffer ativo junto aos draws precedentes.
   - O flush só é executado se houver wrap de buffer (`wrapNeeded`) ou quando o lote atingir a fronteira do quadro em `PresentDestile`.
   - Telemetria dedicada `[DX79_UPLOAD]` registra `Offset`, `Head`, `FlushSincrono`, `Coalescidos` e `FlushesUpload`.

4. **Ciclo de Vida e Resets Seguros:**
   - Em [`driver/umd/bc250_umd_vulkan_bridge.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_vulkan_bridge.c):
     - `pBridge->UploadStagingHead = 0` no retorno com fence de `Bc250UmdVkFlushCommands`.
     - `pBridge->UploadStagingHead = 0` no redimensionamento de `Bc250UmdVkEnsureUploadStaging`.
     - `pBridge->UploadStagingHead = 0` no teardown de `Bc250UmdVkBridgeShutdown`.

5. **Auditorias e Testes de Mutação:**
   - [`audit-bc250mcdm-safety.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/audit-bc250mcdm-safety.py) ganhou o portão `audit_dx79_texture_upload_coalescing` cobrindo todos os requisitos estruturais.
   - Suite de mutação [`test-dx79-upload-coalescing-mutations.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/test-dx79-upload-coalescing-mutations.py) aprovada com 8/8 mutações reprovadas.
   - Suite de contrato offline [`umd_dx49_batch_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/umd_dx49_batch_test.c) atualizada com 20/20 testes unitários aprovados.

---

## 3. Identificação do Pacote e Hashes de Controle

- **DriverVer Empacotado:** `09/25/2026,3.283.1.0` (INF fonte `3.283.0.0`)
- **Perfil:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `P1 PERSISTENT SUBMISSION`
- **Hashes SHA-256:**
  - `bc250mcdm.sys`: `04C8F356F66485090ACA1185100035A0BEE496CE76E66836AF215985FA67AE57`
  - `bc250mcdm.inf`: `3A2B6CFDB42DE7E700C0EFEA9F8989ACC90343CEA9134584BF2DD02B177CCFEC`
  - `bc250mcdm.cat`: `612865891C8D139EE9AFC40E6873949CCC32E4A1077EB7834B3B3FED92113D6D`
  - `bc250umd.dll`: `AB272CF81E8C6D91A4A94D61A54DBD51212284C33FF97F23849848A5B26795B0`
  - RADV externo: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- **Validação Offline:**
  - `driver\wdk\build-contract.bat`: **EXIT 0** (40+ testes aprovados).
  - `driver\umd\build-umd.bat`: **EXIT 0** (DLLs e harnesses compilados e linkados).
  - `python tools\audit-bc250mcdm-safety.py`: **PASS**.
  - `powershell tools\audit-bc250mcdm-lab-package.ps1`: **PASS**.
  - `python tools\test-dx79-upload-coalescing-mutations.py`: **PASS (8/8 mutações reprovadas)**.

---

## 4. Critérios de Avaliação da Corrida 124

| Desfecho | Critério | Interpretação |
|---|---|---|
| **A (Avanço Pleno)** | (1) Janela de 180 s sem travamento, TDR ou BugCheck;<br>(2) Desktop legível e triângulo visível na janela do harness sem artefatos;<br>(3) Conformance harness com 60/60 `PRESENT_OK`;<br>(4) Log do DWM comprova eliminação massiva dos flushes de upload (`FlushSincrono=0` e `Coalescidos > 0` na esmagadora maioria das chamadas `[DX79_UPLOAD]`, flushes caem de 504 para ~1–10);<br>(5) Taxa de apresentação do DWM supera expressivamente os 4–5 Presents/s das Corridas 122/123 (alvo: > 8–15+ Presents/s);<br>(6) Rollback online restaura `BasicDisplay` Código 0 em 5/5 amostras (R1). | Avanço funcional e de performance expressivo: eliminação dos stalls de upload de textura validada em silício. |
| **B (Avanço Parcial / Coalescência com Outro Gargalo)** | Mesmos critérios 1, 2, 3, 4 e 6 do Desfecho A, mas a taxa de apresentação do DWM se mantém entre 5 e 8 Presents/s devido à latência intrínseca do `pfnPresentCb` / VidMm scheduler do Windows. | Avanço arquitetural comprovado (coalescência de uploads funcional), isolando o próximo gargalo na apresentação do kernel. |
| **C (Falha Visual / Corrupção de Textura)** | Execução sem crash, mas com ícones ou textos corrompidos no desktop, desktop piscando ou triângulo distorcido por sobreposição indevida no anel de staging. | Falha no cálculo de offset ou wrap do buffer de staging; análise dos logs de `[DX79_UPLOAD]`. |
| **D (Falha Operacional / Crash / Hang)** | BugCheck, TDR, travamento duro antes dos 180 s ou falha no rollback R1. | Regressão de estabilidade; análise de dump e telemetria de fence. |

---

## 5. Procedimento Seguro de Execução

1. **Reinicialização a Frio com Corte de Energia:**
   - Desligar completamente o host, aguardar 10 segundos para descarga capacitiva da VRAM GDDR6 e ligar novamente.
2. **Executar o Runner Preparado:**
   - Abrir prompt de comando elevado e executar:
     ```cmd
     tools\EXECUTAR_CORRIDA124_DX79_180S.cmd
     ```
3. **Observação e Coleta:**
   - Observar desktop, janela do triângulo e aguardar os 180 s até o rollback do watchdog para `BasicDisplay` Código 0.
   - Em caso de travamento duro, acionar `Ctrl Direito + Scroll Lock + Scroll Lock` para gravação do dump.
