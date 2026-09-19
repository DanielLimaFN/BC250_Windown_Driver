# Passagem de bastão — WDDM acelerado na BC-250 (Candidato 3.109.1.0)

> **Data:** 2026-09-09 11:23 UTC-3  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Pacote:** Candidato `3.109.1.0` (Lab Bring-up com RenderNode=1 e Contrato WDDM 2.7)  
> **Status:** Compilado, auditado, assinado e **NÃO INSTALADO** nesta sessão.  
> **Regra 10:** A execução física é prerrogativa e decisão exclusiva do operador humano (Daniel).

---

## 1. Contexto e Diagnóstico do Candidato 3.108.1.0

Na corrida física do candidato 3.108.1.0, o silício confirmou:
1. `CP_HQD_VMID` manteve fisicamente o valor `0x00000900` (`IB_VMID = 9`), validado na telemetria síncrona `[SubmissionPublishWptr]` e no snapshot de TDR.
2. A tradução GCVM para o anel do UMD estava perfeita (`VA 0x11000 -> PA 0x451FCC000`, PTE `0x451FCC071`, `FaultStatus = 0`).
3. O descritor PM4 emitido pelo UMD (`0xC0023F00, 0x00011000, 0x00000000, 0x09800011`) é bit a bit idêntico ao padrão Linux upstream GFX10.
4. O silício consumiu 17 submissões consecutivas com `VMID = 0` (Host PA), avançando `RPTR` de 0 a 204 dwords no anel principal.
5. Na submissão #18 (WDDM submit real do UMD com `VMID = 9`), o `WPTR` avançou para 216 dwords, mas o Command Packet Fetcher (CPF) estagnou exatamente no dword 204 (`PACKET3_INDIRECT_BUFFER`, opcode `0x3F`), sem entrar no IB (`PROCESSING_IB = 0`, `RPTR = 204`), causando o timeout de 2 segundos.

Entretanto, no momento do TDR, os registradores internos do pipeline de prefetch/controle do microcódigo (`CP_CPF_STATUS`, `CP_CPF_STALLED_STAT1`, `CP_CPC_STATUS`, `CP_CPC_STALLED_STAT1`, `CP_MEC1_INSTR_PNTR`, `CP_HQD_IB_BASE_LO/HI`, `CP_HQD_IB_RPTR`) não estavam autorizados para leitura na allowlist do MMIO (`bc250_mmio_topology_transport.c`), ficando inacessíveis para o diagnóstico decisivo da causa do stall no dword 204.

---

## 2. O que foi Implementado no Candidato 3.109.1.0

### A. Exposição Cirúrgica na Allowlist de Leitura MMIO
No arquivo [`driver/wdk/bc250_mmio_topology_transport.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_mmio_topology_transport.c):
- Incluídos na allowlist de leitura:
  - `BC250_HQD_MMIO_CP_CPF_STATUS` (`0x0000821Cu`)
  - `BC250_HQD_MMIO_CP_CPF_STALLED_STAT1` (`0x00008224u`)
  - `BC250_HQD_MMIO_CP_MEC1_INSTR_PNTR` (`0x000086A0u`)
  - `BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR` (`0x000086A4u`)
  - `BC250_HQD_MMIO_IB_BASE_LO` (`0x0000C870u`)
  - `BC250_HQD_MMIO_IB_BASE_HI` (`0x0000C874u`)
  - `BC250_HQD_MMIO_IB_RPTR` (`0x0000C87Cu`)
- **Segurança estrita mantida**: Todos permanecem sumariamente rejeitados na allowlist de escrita (`Bc250MmioTopologyWriteOffsetAllowed`).

### B. Definições no Contrato e Suíte de Testes
- Constantes adicionadas em [`driver/core/bc250_compute_mqd_contract.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/core/bc250_compute_mqd_contract.h):
  - `BC250_HQD_MMIO_CP_MEC1_INSTR_PNTR 0x000086A0u`
  - `BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR 0x000086A4u`
- [`driver/tests/mmio_topology_transport_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/mmio_topology_transport_test.c) atualizado com asserções `CHECK` de leitura permitida e escrita rejeitada para cada novo registrador. `ReadCount` ajustado de 135 para 142. Suíte aprovada (`EXIT=0`).

### C. Telemetria Ativa no TDR e no StopDevice
1. **No TDR Quiesce (`Bc250ActiveTdrQuiesceLifecycle` em `bc250_active_miniport.c`)**:
   - `[TDR_MEC_CPF_STAT]`: lê e emite `CpfStat`, `CpfBusy`, `CpfStalled`, `CpcStat`, `CpcBusy`, `CpcStalled`, `CpStalled1`, `CpStat`.
   - `[TDR_MEC_EXEC_STAT]`: lê e emite `Mec1Hdr`, `Mec2Hdr`, `Mec1Pntr`, `Mec2Pntr`, `IbBase`, `IbRptr`, `PqCtrl`, `Dequeue`.
2. **No StopDevice (`Bc250SubmissionEngineArchiveDiagnostics` em `bc250_submission_engine.c`)**:
   - `[MEC_STOP_CPF]`: emite o estado final do CPF e CPC no desligamento do adaptador.
   - `[MEC_STOP_EXEC]`: emite ponteiros de instrução e registradores da fila de IB.
3. **No Polling Ativo (`Bc250SubmissionEnginePoll` em `bc250_submission_engine.c`)**:
   - `[MEC_PENDING_STALL]`: emite `CpfStalled` e `CpcStalled` na primeira observação pendente.

---

## 3. Identidade Criptográfica do Pacote 3.109.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Arquivo | SHA-256 |
|---|---|
| SYS fonte (compilado lab) | `67539AE94102BC37BFA2BB75979421DAB8D922B9DAC0CF633B0B8A20AE1DE59A` |
| SYS assinado (pacote) | `200D88D50E7CEDED1A17DE8DB366DF192878CB84CEB5E6A387A3139B2AA7F3DA` |
| UMD assinada | `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214` |
| INF assinado | `6FC74AC15AE71E193876A36820CAE8AD9214D1528D570A963D61E552B362D954` |
| CAT assinado | `118A1A95801677A48FC6EF6D3F61F1A12F1110A3453610BF71EE5A6CCB48F3E3` |

Propriedades:
- `DriverVer`: `09/09/2026,3.109.1.0`
- `Profile`: `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`)
- `WDDM Contract`: `27` (`WDDM 2.7`)
- `Bring-up ativo`: `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP` compilado.

Auditorias de segurança executadas e aprovadas:
- `driver\wdk\build-contract.bat`: **EXIT=0** (todas as 40+ suítes passaram)
- `tools/audit-bc250mcdm-safety.py`: **PASS**
- `tools/audit-umd-node1-submission.py`: **PASS**
- `tools/test-compute-vmid-audit.py`: **PASS**
- `tools/test-mec-mmio-safety.py`: **PASS**
- `tools/audit-bc250mcdm-lab-package.ps1`: **PASS**

---

## 4. O que a Corrida Física Irá Medir (Decodificação Imediata)

Ao analisar o log da corrida física, as novas linhas responderão de imediato:
1. `CpfStalled`:
   - Se bit 10 (`CMP_UTCL1_WAITING_ON_TRANS`) estiver setado: o CPF tentou buscar o IB na GPUVA `0x11000`, mas a transação do GFXHUB/UTCL1 para o VMID 9 não respondeu (indica necessidade de sincronização TLB ou invalidation na inicialização do contexto).
2. `CpcStalled`:
   - Se bit 4 (`RCIU_PRIV_VIOLATION`) estiver setado: violação de privilégio ao chavear do anel primário privilegiado para o IB do usuário no VMID 9.
3. `CpfBusy` e `CpfStat`:
   - Se bit 19 (`HQD_IB_FETCHER_BUSY`) estiver setado: o fetcher de IB está ativo no hardware tentando processar o cabeçalho.
4. `Mec1Pntr`:
   - Ponteiro de instrução exato do microcódigo do MEC1 onde ocorreu o stall.
5. `IbBase` e `IbRptr`:
   - Se `IbBase` carregou `0x11000` e quanto do buffer foi lido.

---

## 5. Instruções Cirúrgicas para o Ensaio Físico no Hardware (Daniel)

Em cumprimento irrestrito à **Regra 10**, a execução em hardware é conduzida **exclusivamente por Daniel**.

### Passo 1: Boot Frio
Desligar a máquina e religar (cold boot) para reset elétrico completo dos blocos PCIe, GFXHUB e MEC da APU.

### Passo 2: Binding e Disparo do Teste
Em PowerShell elevado:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission
```

### Passo 3: Mineração do Log Pós-Corrida
Após a corrida (ou recuperação pelo watchdog), extrair o bloco de diagnóstico do MEC:
```powershell
Select-String -Path "docs\evidence\*\bc250_display_log-full.txt" -Pattern "TDR_MEC_|MEC_STOP_|MEC_PENDING"
```
Com esses dados, saberemos o motivo exato pelo qual o hardware parou no dword 204.
