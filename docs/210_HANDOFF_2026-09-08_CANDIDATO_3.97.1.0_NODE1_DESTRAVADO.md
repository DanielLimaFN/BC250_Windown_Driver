# 210 — Destravamento da Submissão Node 1 no VMID9 & Candidato Lab 3.97.1.0

> **Data:** 08/09/2026  
> **Candidato:** Lab `3.97.1.0` (SYS `A4029B6F...`, UMD `35FBD781...`, CAT `A2CD6CC1...`, INF `9B12FDF7...`)  
> **Status:** Compilado, assinado e auditado em `driver\wdk\package\output_lab\` · **NÃO INSTALADO**  
> **Precedente:** Corrida física de 17:18 (`docs/evidence/2026-09-08_17-18-28-DESKTOP-HQAG6T4-lab-binding/`), Handoffs 208 e 209.

---

## 1. Diagnóstico Forense da Corrida 17:18 (P0 Medido)

Na corrida física de 08/09 às 17:18 (`3.96.1.0`):
1. **O que funcionou (P0):**
   - O sistema subiu sob watchdog de 180s e executou o rollback limpo sem travamento ou bugcheck.
   - O harness UMD `bc250-umd-node1-tripod.exe` gerou e submeteu o pacote PM4 de 68 bytes em `VA=0x11000`.
   - O KMD recebeu a chamada `DxgkDdiSubmitCommandVirtual` no Node 1.
   - O portão abriu: `[SubmitVirtual Node1] GATE ABERTO: GpuvaMappingsOperational=1; publicando IB de 68 bytes em VA=0x11000 no anel persistente MEC (FenceId=1)`.
   - A raiz foi registrada: `[SetRootPageTable] Node1: raiz registrada; VMID9 instalado no submit`.

2. **O que falhou (P0):**
   - Linha 18011: `[SubmitVirtual Node1] RECUSA PrepareComputeVmid: SubmissionEnginePoll falhou (status=0xC000009C)`.
   - Linha 18012: `[SubmitVirtual Node1] RECUSA: PrepareComputeVmid/Submit falhou (submitStatus=0xC000009C)`.
   - A submissão física foi cancelada e `dxgkrnl` escreveu `UINT64_MAX` na monitored fence da UMD.
   - O cursor de MMIO do MEC (`P1LastWptrMmioReadback`) permaneceu em **204** (não avançou para 216).

3. **Causa Raiz Física:**
   - Em `Bc250ActivePrepareComputeVmid`, uma chamada incondicional a `Bc250SubmissionEnginePoll(Context, &retiredNow)` era executada antes do bind.
   - No momento pré-submissão, o anel MEC estava em repouso após os testes de bringup (`Submitted == Retired == 17`, logo `inFlight = 0`).
   - A rotina de aposentadoria `Bc250PersistentRingRetire` calcula `delta = ObservedFenceLow32 - lastRetiredLow`. Quando `inFlight == 0`, qualquer diferença transitória na leitura da memória da fence resulta em `delta > inFlight`.
   - Isso disparou `BC250_PRING_FENCE_REGRESSION`, fez `Bc250SubmissionEnginePoll` marcar o motor como `BC250_SUBMISSION_ENGINE_FAULTED` e devolver `STATUS_DEVICE_DATA_ERROR` (`0xC000009C`).

---

## 2. Alterações Implementadas

### A. Condicionamento Estrito do Poll em `Bc250ActivePrepareComputeVmid`
- Leitura atômica sob `SubmissionEngine.Lock` de `Submitted` e `Retired`.
- Se `Submitted == Retired` (anel ocioso): o poll é ignorado e o código prossegue diretamente para `Bc250GcvmComputeInstall`.
- Se `Submitted != Retired` (trabalho pendente): executa `Bc250SubmissionEnginePoll`. Se o anel persistir ocupado, retorna `STATUS_GRAPHICS_ALLOCATION_BUSY` (resposta WDDM canônica de retentativa), jamais corrompendo o estado do motor.

### B. Telemetria e Observabilidade Máxima (Conforme Instrução)
- `[SubmitVirtual Node1] PrepareComputeVmid: Root=0x%llX Sub=%llu Ret=%llu InFlight=%llu`
- `[SubmitVirtual Node1] PrepareComputeVmid: PosPoll Sub=%llu Ret=%llu RetiredNow=%u status=0x%08X`
- `[SubmitVirtual Node%u] INICIANDO PUBLICACAO: DmaVA=0x%llX SizeDW=%u FenceId=%llu`
- `[SubmitVirtual Node%u] SUBMIT SUCESSO: AssignedFence=%llu RingWptr=%u WptrMmio=%u`
- `[SubmitVirtual Node%u] FENCE COMMIT: Token=%llu RingFence=%llu committed=%u accepted=%u status=0x%08X`
- No `Bc250SubmissionEnginePoll`, log detalhado de `State`, `ObservedFence`, `RptrReport`, `RptrMmio`, `Sub`, `Ret`, `RingStatus`, e telemetria profunda em caso de regressão de fence.

### C. Robustez no Script de Empacotamento
- Corrigido `tools/package-bc250mcdm-lab.ps1` com regexes genéricos `\d+` para parâmetros no INF, evitando falhas de substituição quando o template do INF já contém flags habilitadas.

### D. Versionamento
- Fonte INF WDK: `09/08/2026,3.97.0.0`
- Pacote Lab: `09/08/2026,3.97.1.0`

---

## 3. Verificação Offline & Auditorias

| Verificação | Comando | Resultado |
|---|---|---|
| Suíte de Contratos WDK | `driver\wdk\build-contract.bat` | **EXIT=0** (Todos os testes e matriz DDI aprovados) |
| Auditoria de Segurança WDDM/MCDM | `python tools/audit-bc250mcdm-safety.py` | **PASS** (17 portões de segurança estritos cumpridos) |
| Auditoria de Mutações VMID9 | `python tools/test-compute-vmid-audit.py` | **PASS** (Baseline e 4 mutações rejeitadas com sucesso) |
| Auditoria de Submissão UMD Node 1 | `python tools/audit-umd-node1-submission.py` | **PASS** (Cadeia UMD validada) |
| Build da Imagem Lab Bringup | `tools\build-bc250mcdm-lab-bringup.ps1` | **EXIT=0** (Binário `output_lab\bc250mcdm.sys` com bringup compilado) |
| Empacotamento do Pacote de Laboratório | `tools\package-bc250mcdm-lab.ps1` | **EXIT=0** (Cat gerado e assinado via certificado de teste) |
| Auditoria do Pacote de Laboratório | `tools\audit-bc250mcdm-lab-package.ps1` | **PASS** (DriverVer 3.97.1.0, WDDM 2.7, DISPLAY_RENDER_LAB) |

---

## 4. Artefatos de Build

- **Diretório:** `driver\wdk\package\output_lab\`
- **Manifesto:** `driver\wdk\package\output_lab.manifest.json`
- **Hashes Criptográficos SHA-256:**
  - `bc250mcdm.sys`: `A4029B6FF2A2862D4E3F1E12EBA1A505BD63692AE051B7FAFF06C7C40ED79E1C`
  - `bc250umd.dll`: `35FBD78186D300D84F25DB76D1AF1C3025C7FECA3211CA422D35619B116AF214`
  - `bc250mcdm.inf`: `9B12FDF7EC6C54B7A811E7DB55CD5EF478D8FABE541481274EBAEE9F3835C875`
  - `bc250mcdm.cat`: `A2CD6CC11C52838679B007CFE0F97A298CDB4EDDEDF14AEA673CA1E09E75E46F`

---

## 5. Protocolo para o Próximo Ensaio Físico

> [!CAUTION]
> **Teto: 1 corrida física.** É mandatória a reinicialização a frio do host físico para satisfazer o gatilho de segurança `C:\BC250_REQUIRE_CLEAN_BOOT.json`.

### Passo 1: Reboot a frio
Executar no terminal elevado:
```cmd
tools\BC250_RECOVERY.cmd
shutdown /r /t 0
```

### Passo 2: Execução do Ensaio Físico
No terminal PowerShell com privilégios de administrador:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180 -Execute -IAcceptBlackScreenRisk
```

### O que observar no log físico pós-ensaio:
1. `[SubmitVirtual Node1] PrepareComputeVmid: Root=... Sub=17 Ret=17 InFlight=0`
2. `[SubmitVirtual Node1] VMID9_BIND: Root=... Status=0 Ready=1`
3. `[SubmitVirtual Node1] INICIANDO PUBLICACAO: DmaVA=0x11000 SizeDW=17 FenceId=1`
4. `[SubmitVirtual Node1] PUBLICADO NO ANEL MEC: AssignedFence=... WPTR=216`
5. `[SubmitVirtual Node1] SUBMIT SUCESSO: AssignedFence=... RingWptr=216 WptrMmio=216`
6. `[SubmitVirtual Node1] FENCE COMMIT: ... status=0x00000000`
7. E no harness `bc250-umd-node1-tripod.exe`: leitura da cerca monitorada e verificação dos 5 canários no buffer de prova `VA=0x21000`.
