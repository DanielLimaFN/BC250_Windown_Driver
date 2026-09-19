# Passagem de bastão — WDDM acelerado na BC-250 (Candidato 3.107.1.0)

**Data:** 09/09/2026. **Workspace:** `C:\Users\Daniel\Desktop\NamixWDDM`.
**Destinatária:** Daniel e próxima IA responsável pela continuidade.

## 1. Missão e Ponto Exato de Retomada

Candidato **3.107.1.0 (Lab Bring-up)** construído, auditado, assinado e **NÃO INSTALADO** nesta sessão.
Todas as verificações contratuais (`build-contract.bat`), auditorias de segurança de mutação (`test-mec-mmio-safety.py`), auditoria de safety MCDM (`audit-bc250mcdm-safety.py`), auditoria UMD (`audit-umd-node1-submission.py`) e auditoria de pacote (`audit-bc250mcdm-lab-package.ps1`) passaram com **PASS (STRUCTURAL ONLY) / 0 Falhas / 10 Mutações Rejeitadas**.

O candidato anterior (3.106.1.0) não chegou a ser instalado ou executado no silício físico (a última corrida em hardware foi às 08:26 com a 3.105.1.0).
Uma análise detalhada da cadeia de observabilidade revelou **4 pontos cegos críticos** que poderiam fazer com que uma corrida do 3.106.1.0 ainda não gerasse dados em disco caso o TDR ocorresse antes do StopDevice ou houvesse fault de MMU no Node 1.

O candidato **3.107.1.0** fecha esses 4 pontos cegos antes da execução física no hardware.

---

## 2. Os 4 Pontos Cegos Fechados no Candidato 3.107.1.0

1. **Snapshot Instantâneo dos Registradores MEC no Momento do TDR (`[TDR_MEC_SNAPSHOT]`):**
   - **Problema no 3.106:** A captura de registradores do MEC dependia do `Bc250SubmissionEngineArchiveDiagnostics` chamado apenas dentro de `StopDevice`. Porém, o evento Display/4101 (TDR) ocorre dezenas de segundos antes do watchdog chamar `StopDevice`. No momento do TDR, `Bc250ActiveTdrQuiesceLifecycle` roda em `PASSIVE_LEVEL`, mas não gravava o estado da fila do MEC nem dos ponteiros de anel.
   - **Solução no 3.107:** No início de `Bc250ActiveTdrQuiesceLifecycle`, capturamos diretamente via MMIO os registradores de hardware da fila MEC:
     - `CP_HQD_ACTIVE` (`0x0A000 + 0x200`)
     - `CP_HQD_DEQUEUE_REQUEST` (`0x0A000 + 0x208`)
     - `CP_HQD_PQ_RPTR` (`0x0A000 + 0x21C`)
     - `CP_HQD_PQ_WPTR` (`0x0A000 + 0x224`)
     - `CP_HQD_HQ_STATUS0` (`0x0A000 + 0x2B4`)
     - `CP_HQD_PQ_DOORBELL_CONTROL` (`0x0A000 + 0x238`)
     - `CP_HQD_IB_CONTROL` (`0x0A000 + 0x288`)
     - `CP_HQD_ERROR` (`0x0A000 + 0x2B8`)
   - Gravados de forma síncrona no log com a tag `[TDR_MEC_SNAPSHOT]`.

2. **Detecção de Faults de Tradução MMU / GCVM no Node 1 (`[NODE1_STOP_GCVM_FAULT]` e `[TDR_GCVM_FAULT]`):**
   - **Problema no 3.106:** Os registradores `BC250_ACTIVE_GCVM_FAULT_STATUS` (`0x0A130`), `ADDR_LO` (`0x0A134`) e `ADDR_HI` (`0x0A138`) só eram lidos no timeout do Node 0 (SDMA). Se o MEC/VMID 9 tomasse um page fault durante o fetch do IB (ex: se o endereçamento MC/PA ou permissões falhassem), o status de fault não era lido nem no TDR nem no `StopDevice`, permanecendo invisível.
   - **Solução no 3.107:** Leituras explícitas de `BC250_ACTIVE_GCVM_FAULT_STATUS`, `ADDR_LO` e `ADDR_HI` foram adicionadas no `TdrQuiesce` e no `StopDevice`, emitindo `[NODE1_STOP_GCVM_FAULT]`. Se houver fault no silício, veremos o endereço exato e a classe de fault.

3. **Log Imediato na Submissão do Prefixo CPU do IB (`[SubmitVirtual Node1] IB_CPU_PREVIEW`):**
   - **Problema no 3.106:** O prefixo de 32 DWORDs do IB lido pela CPU era armazenado em memória (`Context->LastIbCpuDwords`) e só escrito em disco no `StopDevice`. Se ocorresse hang de máquina ou reboot não limpo antes do `StopDevice`, o conteúdo do buffer ficaria perdido.
   - **Solução no 3.107:** `Bc250ActiveSubmitCommandVirtualBody` roda em `PASSIVE_LEVEL`. Adicionamos a emissão imediata de até 17 DWORDs do IB (tamanho do pacote UMD de 68 bytes) diretamente no momento do submit: `[SubmitVirtual Node1] IB_CPU_PREVIEW: D0=0x... D1=...`.

4. **Log Imediato na Submissão do Descritor PM4 Publicado no Anel MEC (`[SubmitVirtual Node%u] DESCRIPTOR`):**
   - **Problema no 3.106:** O descritor de 4 DWORDs publicado no anel MEC também só era arquivado no `StopDevice`.
   - **Solução no 3.107:** Imediatamente após a publicação no anel MEC em `SubmitCommandVirtual`, emitimos `[SubmitVirtual Node%u] DESCRIPTOR: Raw=0x%08X 0x%08X 0x%08X 0x%08X`.

---

## 3. Identidade do Pacote Candidato 3.107.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Artefato | SHA-256 |
|---|---|
| SYS fonte (compilado) | `9DF22E71AABF2345D3B0AF36FA2891BE0B2F1580D86D21760A783B6B38C3A20E` |
| SYS assinado (pacote) | `7ECDAD4FCE205117F1CA04017FDE936F9A3F64673EC50779AF5C2E7EC3566D98` |
| UMD assinada | `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214` |
| INF assinado | `03EE98CD059F724162CB0E834FD352BC3416B19FC2394951746CC130D4BAD1BF` |
| CAT assinado | `1F970D024EF3396F2C3A6EB09D7CB8CB5FE30A1A88F24076CF5D339C5DA6734A` |

Propriedades:
- `DriverVer`: `09/09/2026,3.107.1.0`
- `ProfileMode`: `DISPLAY_RENDER_LAB` (RenderNode=1, RenderOnlyLab=0)
- `WddmContractVersion`: 27 (WDDM 2.7)
- `PersistentSubmissionEnabled`: true
- `ShaderDispatchEnabled`: true
- `CpSoftResetEnabled`: true
- `GpuvaLabPromotionEnabled`: false (requer flag `-EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion`)
- `HardwareValidated`: false (**NÃO VALIDADO EM HARDWARE**)

---

## 4. Instruções de Execução Física (Teto de 1 Corrida)

Conforme a **Regra 10 do AGENTS.md**, o teste em hardware é executado exclusivamente pelo Daniel, após boot frio do sistema.

### Pré-requisitos
1. Boot frio da máquina de teste.
2. Certificar-se de que o sistema inicializou com o display funcional (`BasicDisplay` / Código 0) e o fusível anti-boot-loop não está disparado.

### Comando Único de Execução (PowerShell Administrador na raiz do repo)

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180
```

### O Que Observar no Log Pós-Corrida
1. `[SubmitVirtual Node1] IB_CPU_PREVIEW`: Conterá os primeiros 17 DWORDs do buffer DMA gravados pela UMD. Devemos ver `0x20002000` (NOP ou header WRITE_DATA) e o token de sincronização.
2. `[SubmitVirtual Node1] DESCRIPTOR`: Conterá os 4 DWORDs emitidos para a fila MEC (`0xC0013F00`, `VA_LO`, `VA_HI`, `DW_COUNT | VMID9`).
3. Se houver hang / TDR:
   - `[TDR_MEC_SNAPSHOT]`: Mostrará se `PQ_RPTR` andou (`0x204` -> `0x216` ou além), se `ACTIVE=1`, `DEQUEUE_REQUEST=0` e se `ERROR` registrou algo.
   - `[TDR_GCVM_FAULT]`: Mostrará se houve page fault em `0x0A130`.
4. No StopDevice:
   - `[NODE1_STOP_GCVM_FAULT]`: Confirmação se a MMU interceptou transação inválida.
   - `[MEC_STOP_STATE]` e `[NODE1_IB_CPU_ARCHIVE]`.
