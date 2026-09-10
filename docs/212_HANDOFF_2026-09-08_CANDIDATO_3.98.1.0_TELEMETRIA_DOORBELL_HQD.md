# docs/212_HANDOFF_2026-09-08_CANDIDATO_3.98.1.0_TELEMETRIA_DOORBELL_HQD.md

## 1. Situação Física Atual (Medição P0 na Corrida 18:21)

Na execução de `tools\bind-bc250mcdm-lab.ps1 -RunUmdNode1Tripod -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion` (Evidência: `docs/evidence/2026-09-08_18-21-39-DESKTOP-HQAG6T4-lab-binding/`):

### O que Funcionou no Silício (P0 Fato)
1. **Cura definitiva de `0xC000009C`:** O anel em repouso não disparou mais falsa regressão de fence:
   `[SubmitVirtual Node1] PrepareComputeVmid: Root=0x45FFB0000 Sub=17 Ret=17 InFlight=0`
2. **Instalação do VMID9:** Tabela de páginas própria e invalidação de TLB com ACK no GFXHUB confirmada:
   `[SubmitVirtual Node1] VMID9_BIND: Root=0x45FFB0000 Submitted=17 Retired=17 Status=0 Ready=1`
3. **Escrita Direta MMIO do WPTR Comprovada no Silício (Segunda Vez na História):**
   - Em `b7-umd-node1-tripod.json`:
     - `before.P1LastWptrMmioReadback = 204`
     - `after.P1LastWptrMmioReadback = 216`!
   - O registrador de hardware `BC250_HQD_MMIO_PQ_WPTR_LO` avançou fisicamente 12 dwords.

### O que Falhou no Silício (P0 Fato)
- Linha 21040 do log: `[SubmitVirtual Node1] RECUSA: PrepareComputeVmid/Submit falhou (submitStatus=0xC0000182)`.
- O status `0xC0000182` (`STATUS_DEVICE_CONFIGURATION_ERROR`) marcou o motor persistente como `FAULTED`.
- O `dxgkrnl` abortou a submissão e preencheu `UINT64_MAX` na monitored fence da UMD.
- A UMD aguardou a fence, viu `UINT64_MAX`, e o harness declarou o tripé como não validado.

---

## 2. Diagnóstico da Causa Raiz de `0xC0000182`

No arquivo `driver/wdk/bc250_submission_engine.c`, dentro de `Bc250SubmissionPublishWptr`, existem exatamente dois pontos que retornam `STATUS_DEVICE_CONFIGURATION_ERROR`:
1. **Check A (Linha 615 original):**
   ```c
   if (Engine->DoorbellEnabled == 0u || Engine->DoorbellVirtual == NULL ||
       (Engine->DoorbellOffsetBytes & (sizeof(uint64_t) - 1u)) != 0u ||
       Engine->Buffers.DoorbellSizeBytes <
           Engine->DoorbellOffsetBytes + sizeof(uint64_t))
   ```
2. **Check B (Linha 637 original):**
   ```c
   if ((doorbellControl & BC250_SUBMISSION_DB_ENABLE) == 0u ||
       (doorbellControl & BC250_SUBMISSION_DB_OFFSET_MASK) !=
           Engine->DoorbellOffsetBytes)
   ```

Nenhum dos dois tinha logging. A falha ocorreu em silêncio absoluto.

---

## 3. O que Foi Implementado no Candidato 3.98.1.0

### Arquivo Modificado: `driver/wdk/bc250_submission_engine.c`
1. **Entrada de `Bc250SubmissionPublishWptr`:**
   `Bc250DisplayLog("[SubmissionPublishWptr] ENTER: NewWptr=%u EngineState=%d RingWptr=%u ShadowVirt=%p MmioWPTR=%u\n", ...)`
2. **Step 1 (Shadow):**
   Logs explícitos para ponteiro nulo e divergência de readback.
3. **Step 2 (Coerência):**
   Log explícito caso `Bc250SubmissionApplyCoherency` falhe.
4. **Step 3 (MMIO WPTR direto):**
   `Bc250DisplayLog("[SubmissionPublishWptr] Step 3 MMIO WPTR: Write=%u Readback=%u (DirWrites=%u)\n", ...)`
5. **Step 4 Check A (Pré-checagem da Doorbell):**
   - Log completo de todas as 4 variáveis: `Enabled`, `DoorbellVirtual`, `OffsetBytes`, `DoorbellSizeBytes`.
   - Se falhar: log discriminado de recusa antes do `return STATUS_DEVICE_CONFIGURATION_ERROR`.
6. **Step 4 Leitura do Banco de Registradores da Fila:**
   - Sob seleção de banco (`Engine->SelectedSelector`), leitura de:
     - `BC250_HQD_MMIO_ACTIVE` (estado da fila no silício)
     - `BC250_HQD_MMIO_PQ_RPTR` (posição de leitura do hardware)
     - `BC250_HQD_MMIO_DOORBELL_CONTROL` (configuração da doorbell)
     - `BC250_HQD_MMIO_HQD_ERROR` (código de erro do microcode MEC)
   - Log detalhado: `[SubmissionPublishWptr] Step 4 QueueBank: Active=%u Rptr=%u DbCtrl=0x%08X Error=0x%08X (Selector=0x%08X)`
7. **Step 4 Check B (Consistência da Doorbell):**
   - Log dos campos decodificados: `EnableBit`, `Offset`, `ExpOffset`.
   - Se falhar: log discriminado antes do `return STATUS_DEVICE_CONFIGURATION_ERROR`.
8. **Step 4 Disparo e Pós-Disparo:**
   - Log da escrita de 64 bits na abertura BAR2 da doorbell.
   - Leitura e log do registrador pós-disparo (`DoorbellControlAfter`, `DoorbellHitObserved`, `Kicks`).
9. **Em `Bc250SubmissionEngineSubmit`:**
   - Log em caso de falha de `Bc250SubmissionPublishWptr` e log de sucesso com WPTR e fence.

### Versionamento e Pacote
- `driver/wdk/package/bc250mcdm.inf`: `DriverVer=09/08/2026,3.98.0.0`
- Pacote assinado de laboratório: `3.98.1.0`
  - SYS SHA-256: `8A33F68D95078968309DC7A41CB57CA68BFFF6644F3373E094AF7CFFDE6B013A`
  - CAT SHA-256: `963078B7B78A84840EB1B01E89C6BEE2863D9AC6E4943E4861F7832C966B243E`
  - INF SHA-256: `11528A19385F7EAE2192BA60CB3DA1B7D10842A97E33559094F5576C39E99A29`

---

## 4. Auditorias e Verificação Offline

- `cmd /c driver\wdk\build-contract.bat`: **EXIT=0** (36 testes passaram)
- `py -3 tools\audit-bc250mcdm-safety.py`: **PASS** (17 portões estritos)
- `py -3 tools\test-compute-vmid-audit.py`: **PASS** (mutantes VMID9 rejeitados)
- `py -3 tools\audit-umd-node1-submission.py`: **PASS** (contrato UMD)
- `tools\audit-bc250mcdm-lab-package.ps1`: **PASS**

---

## 5. Comando para Ensaio Físico

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -RunUmdNode1Tripod -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion
```
