# Passagem de bastão — WDDM acelerado na BC-250 (Candidato 3.114.1.0)

> **Data:** 2026-09-09 15:45 UTC-3  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Pacote:** Candidato `3.114.1.0` (Lab Bring-up com RenderNode=1 e Contrato WDDM 2.7)  
> **Status:** Compilado, auditado, assinado e **NÃO INSTALADO** nesta sessão.  
> **Regra 10:** A execução física é prerrogativa e decisão exclusiva do operador humano (Daniel).

---

## 1. Análise da Corrida Física do Candidato 3.113.1.0

Na corrida física executada por Daniel (`docs/evidence/2026-09-09_15-19-03-DESKTOP-HQAG6T4-lab-binding`):
1. **Comportamento do Sistema e Rollback Limpo**:
   - O watchdog realizou o rollback online limpo com sucesso após a detecção de ausência de backend D3D10/11 nativo na UMD (`0x887A0004` / fail-fast do DWM).
   - O adaptador retornou para BasicDisplay em **Código 0**.
   - O fusível de boot persistente (`C:\BC250_BOOT_FUSE.dat`) permaneceu em **`CLEAN (0)`**. Zero BugChecks / telas azuis.

2. **Submissões 1..17 (Boot / VMID 0)**:
   - 17 submissões concluídas com 100% de sucesso sob VMID 0 (`P1RetireObservedFence = 17`).

3. **Submissão #18 (Harness UMD Node 1 Tripod sob VMID 9)**:
   - O cursor avançou no buffer circular (`P1LastWptrMmioReadback = 216`).
   - Contudo, `P1LastRptrMmio` estagnou em **204** e o microcódigo do MEC2 acusou:
     - `CP_CPC_STALLED_STAT1 = 0x00040000` (`MEC2_WAIT_ON_RCIU_READ`).
     - `Step 4 QueueBank: ... (Selector=0x00000008 Readback=0x00000000)`.

### Causa-Raiz Diagnosticada
1. **Stall no RCIU (`0x00040000`)**: No candidato 3.112.1.0, `CP_CPC_STALLED_STAT1` estava em `0x00000000`. A introdução de toggles no bit 0 de `HDP_MISC` (`0x03FCC`) no candidato 3.113.1.0 causou perturbação no barramento RCIU com o motor MEC2 ativo. Conforme documentado no upstream Linux kernel (`hdp_v5_0.c`), o flush do host e invalidação da GPU tocam exclusivamente `HDP_FLUSH` (`0x04A80`) e `HDP_READ_CACHE_INVALIDATE` (`0x03FC4`), **nunca** alternando `HDP_MISC` dinamicamente no caminho crítico.
2. **Isolamento de Banco MMIO (`GRBM_GFX_INDEX`)**: O readback de `GRBM_GFX_CNTL` retornava `0x00000000` em vez do seletor `0x00000008`. No silício GFX10/SOC15, qualquer seleção de Pipe/Queue em `GRBM_GFX_CNTL` exige que `GRBM_GFX_INDEX` (`0x00030800u`) esteja configurado para broadcast canônico (`0xE0000000u`). Sem isso, as operações MMIO em `CP_HQD_*` caem no canal default em vez da fila mapeada do MEC2.

---

## 2. O que foi Implementado no Candidato 3.114.1.0

### A. Alinhamento Estrito do HDP com o Upstream Linux (`bc250_submission_engine.c`)
- Removidos os toggles de `HDP_MISC` em `Bc250SubmissionApplyCoherencyLocked`.
- Mantida a sequência canônica upstream:
  1. `HDP_FLUSH (0x04A80) = 0` + readback imediato (drena completamente escritas da CPU pela abertura PCIe na VRAM física).
  2. `HDP_READ_CACHE_INVALIDATE (0x03FC4) = 1` + readback imediato (invalida o cache de leitura do HDP para a GPU).

### B. Broadcast Canônico de `GRBM_GFX_INDEX` (`0xE0000000u`)
- Definidos `BC250_GRBM_GFX_INDEX_BYTE_OFFSET` (`0x00030800u`) e `BC250_GRBM_GFX_INDEX_BROADCAST` (`0xE0000000u`) no contrato `bc250_compute_mqd_contract.h`.
- Injetada a escrita de broadcast canônico antes de toda seleção (`SelectedSelector`) e antes de toda restauração (`OriginalSelector`) de `GRBM_GFX_CNTL` nos módulos:
  - `driver/wdk/bc250_active_miniport.c`: em `Bc250ActivePrepareComputeVmid`, `Bc250ActiveCaptureMecTelemetry` e no bloco diagnóstico de render.
  - `driver/wdk/bc250_submission_engine.c`: em `Bc250SubmissionSnapshotQueueLocked`, nos Passos 3, 4 (QueueBank) e 4 (DbHit) de `Bc250SubmissionPublishWptr`, em `Bc250SubmissionEngineRetire`, em `Bc250SubmissionEngineArchiveDiagnostics` e no bloco de rearmamento de `Bc250SubmissionEngineRunEvidenceLocked`.

### C. Salvaguarda Absoluta do Harvest de 24 CUs
- `CC_GC_SHADER_ARRAY_CONFIG` (`0x000089BCu`) permanece **estritamente somente leitura**, garantindo a integridade dos fusíveis físicos de fábrica do chip.

---

## 3. Identidade Criptográfica do Pacote 3.114.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Componente | SHA-256 |
| :--- | :--- |
| **SYS de laboratório** (`bc250mcdm.sys`) | `9862D6C08C6BB88B80E583822527C86DDB8E825EF9BAEF5ED595C1D7D289635B` |
| **UMD de laboratório** (`bc250umd.dll`) | `2A25BE2C6D807917BDCA2D8A03BDDEA94FB3DD018B312EC1FDAF1AD5D460969B` |
| **INF de laboratório** (`bc250mcdm.inf`) | `0E32027931F7D75B15FE177323C64A61989CB97D1597D5321F1990F59005077E` |
| **Catálogo assinado** (`bc250mcdm.cat`) | `01BFE3FC248E4CFD8A5D33A7B8BC598A2C3715EA8028D9BFFBBA46BE11334C66` |
| **DriverVer** | `09/09/2026,3.114.1.0` |
| **Perfil** | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`) |
| **WddmContractVersion** | `27` (`WDDM 2.7`) |

### Auditorias e Testes de Contrato
- `build-contract.bat`: **EXIT=0** (todos os 45 contratos e testes unitários offline aprovados).
- `build-umd.bat`: **EXIT=0** (todos os harnesses D3D, Vulkan e tripé do Nó 1 aprovados).
- `audit-bc250mcdm-safety.py`: **PASS**.
- `audit-bc250mcdm-lab-package.ps1`: **PASS**.
- `audit-umd-node1-submission.py`: **PASS**.

---

## 4. Instruções para o Operador Humano (Daniel)

Para ensaiar o candidato **3.114.1.0** no silício físico da BC-250:

```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod
```

### O que observar no ensaio:
1. Em `Step 4 QueueBank`, se `Readback` reflete o seletor `0x00000008` (comprovando que o broadcast de `GRBM_GFX_INDEX` operou a indexação correta).
2. Se a supressão do toggle em `HDP_MISC` eliminou o stall `0x00040000` em `CP_CPC_STALLED_STAT1`.
3. Se `P1LastRptrMmio` avança de **204** para **216**, completando a execução das 12 dwords e validando o tripé físico do Nó 1.
