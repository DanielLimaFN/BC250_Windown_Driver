# Resultado Físico 3.60 — Gate One-Shot Provado e Causa-Raiz no IB da Submissão Persistente

> **Data**: 31/08/2026 09:01 (UTC-3)  
> **Pacote**: `3.60.0.0` / Schema `127`  
> **SYS SHA-256**: `E81521B65343B56EB46BE99486DC70F009961B3522FF76D5C6F95807E4050BA1`  
> **Evidência Binding**: [`docs/evidence/2026-08-31_08-59-15-DESKTOP-HQAG6T4-lab-binding`](evidence/2026-08-31_08-59-15-DESKTOP-HQAG6T4-lab-binding)  
> **Evidência P2**: [`docs/evidence/2026-08-31_09-01-52-DESKTOP-HQAG6T4-p2-multi-submission/p2-multi-submission-acceptance.json`](evidence/2026-08-31_09-01-52-DESKTOP-HQAG6T4-p2-multi-submission/p2-multi-submission-acceptance.json)  
> **Veredito Oficial**: **`NÃO VALIDADO EM HARDWARE`**

---

## 1. Resumo Executivo e Conquistas do Ensaio 3.60

O ensaio físico do Candidato **3.60.0.0** comprovou no silício da APU AMD BC-250:

1. **Gate One-Shot Pré-Persistente (100% PROVADO NO SILÍCIO)**:
   - `ShaderPhysicalReady = 1`
   - `ShaderResultAfter = 0xBC250C01` (saiu de `0x43DAFFFE`, zero mismatch)
   - `ShaderFenceAfter = 0xBC250D15` (saiu de `0xA55A0FF0`, gravada pela GPU)
   - `ShaderRptrAfter = ShaderWptrAfter = 45` (`RPTR == WPTR == 45`)
   - `ShaderTriadPassed = 1`, `ShaderRegisterRestorePassed = 1`, `ShaderMemoryRestorePassed = 1`
   - A reintegração da transação one-shot de computação antes do motor persistente foi totalmente aprovada no hardware real.

2. **Ativação e Estado do Motor Persistente (ONLINE)**:
   - `P1EngineState = 1` (`BC250_SUBMISSION_ENGINE_ONLINE`)
   - `P1ActivateStatus = 0` (`BC250_HQD_ACTIVE_OK`)
   - `P1SnapActActive = 1`, `P1SnapActDoorbellControl = 0x40000110`, `P1SnapActCpPqStatus = 0x00000002`
   - Primary Ring recebeu os 12 DWORDs canônicos: `C0023F00 00A96000 000000F4 0080002D ...` (zero mismatches no anel primário).

3. **Comportamento Fail-Closed do Gate P2**:
   - Como o Self-Test P1 parou dentro do IB, `P2StreamAttempted = 0`.
   - Nenhuma das 16 submissões da P2 foi disparada, preservando a fidelidade da evidência e impedindo o acúmulo de trabalho em fila congelada.

---

## 2. Isolamento da Causa-Raiz no Self-Test P1 e P2Stream

A telemetria do ensaio revelou:
- `P1SelfTestRingWptrAfter = 12`
- `P1SelfTestRptrMmioAfter = 4` (o MEC consumiu os 4 DWORDs do descritor `PACKET3_INDIRECT_BUFFER`)
- `P1SelfTestRptrReportAfter = 0`
- `P1SelfTestResultAfter = 0x43DAFFFE` (permaneceu no poison)
- `CP_HQD_IB_CONTROL = 0x84300000` (`PROCESSING_IB = 1`)
- `CP_MEC_ME2_HEADER_DUMP = 0xC0037602` (`SET_SH_REG`)

### Diagnóstico Causal Detalhado:
No modo de operação Host PA (`Vmid0PageTable=0`), a GPU opera diretamente sobre endereços físicos da VRAM (`0xF400...`), sem tradução de GPUVA pelo VMID 0.

Em `driver/wdk/bc250_submission_engine.c`:
- **Código com Falha no 3.60** (Linhas 1043–1045 e 1412–1414):
  ```c
  descriptor.ShaderGpuAddress = BC250_HQD_SHADER_GPUVA; /* 0x00004000 */
  descriptor.ResultGpuAddress = BC250_HQD_RESULT_GPUVA; /* 0x00005000 */
  descriptor.FenceGpuAddress  = BC250_SUBMISSION_DISPATCH_FENCE_OFFSET; /* 0x00002300 */
  ```
- **Consequência no Hardware**:
  O IB gravou `COMPUTE_PGM_LO = 0x4000 >> 8 = 0x40`, `COMPUTE_USER_DATA_0 = 0x5000` e `RELEASE_MEM Fence = 0x2300`. Ao disparar `DISPATCH_DIRECT`, a Wave do shader tentou buscar instruções no endereço físico de baixa memória `0x4000` em vez da VRAM física `0xF400A94000`. O Wave Manager travou aguardando o fetch impossível, paralisando a execução no início do IB.

- **Correção Necessária para o Candidato 3.61**:
  Usar os endereços físicos calculados a partir de `engine->BasePhysical`:
  ```c
  descriptor.ShaderGpuAddress = engine->Buffers.ShaderGpuAddress; /* basePhysical + 0x4000 */
  descriptor.ResultGpuAddress = engine->Buffers.ResultGpuAddress; /* basePhysical + 0x5000 */
  descriptor.FenceGpuAddress  = engine->BasePhysical + BC250_SUBMISSION_DISPATCH_FENCE_OFFSET; /* basePhysical + 0x2300 */
  ```
  E de forma idêntica no laço de 16 lotes de `Bc250SubmissionEngineP2StreamTest`.

---

## 3. Estado Pós-Ensaio

1. Rollback executado com sucesso através de `tools\rollback-bc250mcdm-lab.ps1`.
2. Dispositivo restaurado para `BasicDisplay / Código 0` (`CM_PROB_NONE`).
3. Árvore de trabalho Git preservada sem modificações destrutivas.
