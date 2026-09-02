# Gate de Programação Reversível MEC/HQD Inativo (Candidato 1.68.0.0)

## 1. Contexto e Objetivos do Gate

- **Estado Físico Comprovado**:
  - SDMA (DMA engine), PSP GPCOM/TMR, carga de firmware via PSP e RLC possuem evidência física comprovada no silício da APU **AMD BC-250** (`1002:13FE`).
  - PM4 Compute Ring 0 permaneceu com `RPTR=0`, `WPTR=12` e fence inalterada (`0x43DAFFFE`). O compute PM4 físico **NÃO** executou.
  - A prova física 1.66 confirmou que `GRBM_GFX_CNTL=0x4` seleciona MEC1/P0/Q0 de forma estável e independente (`MQD_BASE=0`, `PQ_BASE=0`, `RPTR=WPTR=0`, `doorbell=0`, `PQ_CONTROL=0x00308509`).
- **Objetivo do Gate 1.68**:
  - Implementar e integrar a **transação reversível de programação MQD/HQD** em modo estritamente inativo (`CP_HQD_ACTIVE=0`, `DOORBELL=0`, cursores `RPTR=WPTR=0`), consumindo o plano de 16 escritas gerado por `Bc250ComputeBuildInactiveHqdPlan`.
  - Executar verificação prévia dos buffers de memória MC byte a byte, aplicação dos registradores HQD configuráveis, dupla leitura de estabilidade e restauração reversa (LIFO) de registradores, memória MC e seletor `GRBM_GFX_CNTL`.

> [!IMPORTANT]
> O registrador `CP_HQD_ACTIVE` **NÃO** é ativado, nenhum `WPTR` é publicado e nenhum doorbell é disparado neste gate. O veredito de compute permanece **`NÃO VALIDADO EM HARDWARE`**.

---

## 2. Arquitetura da Transação Reversível

A transação segue o ciclo de vida transacional rigoroso:

```text
[PREFLIGHT] -> [MC MEMORY APPLY & VERIFY] -> [SNAPSHOT] -> [APPLY HQD PLAN] -> [DOUBLE READBACK] -> [ROLLBACK & RESTORE] -> [TEARDOWN]
```

### A. Buffers de Memória MC Alocados
1. **MQD Buffer**: 512 bytes, alinhamento de 256 bytes (`Props->MqdGpuAddress`).
2. **Ring Buffer**: 64 KiB (16384 dwords), alinhamento de 256 bytes (`Props->HqdBaseGpuAddress`).
3. **EOP Buffer**: 4096 bytes, alinhamento de 256 bytes (`Props->EopGpuAddress`).
4. **RPTR Report Buffer**: 8 bytes, alinhamento de 4 bytes (`Props->RptrGpuAddress`).
5. **WPTR Poll Buffer**: 8 bytes, alinhamento de 8 bytes (`Props->WptrGpuAddress`).
6. **Fence Buffer**: 8 bytes, poison inicial `0x43DAFFFEu`.

### B. Plano de 16 Registradores HQD (Modo Inativo)
1. `MQD_BASE_LO` (0xC824)
2. `MQD_BASE_HI` (0xC828)
3. `MQD_CONTROL` (0xC8AC)
4. `EOP_BASE_LO` (0xC8B8)
5. `EOP_BASE_HI` (0xC8BC)
6. `EOP_CONTROL` (0xC8C0)
7. `VMID` (0xC830)
8. `PERSISTENT_STATE` (0xC834)
9. `PIPE_PRIORITY` (0xC838)
10. `QUEUE_PRIORITY` (0xC83C)
11. `QUANTUM` (0xC840)
12. `PQ_BASE_LO` (0xC844)
13. `PQ_BASE_HI` (0xC848)
14. `PQ_RPTR` (0xC84C)
15. `PQ_CONTROL` (0xC868)
16. `DOORBELL_CONTROL` (0xC860)

### C. Allowlists Restritas de MMIO
- **Leitura**: Inclui os 16 registradores de configuração, `GRBM_GFX_CNTL`, `ACTIVE`, `PQ_WPTR_LO` e `PQ_WPTR_HI`.
- **Escrita**: Permite exclusivamente os 16 registradores de configuração e `GRBM_GFX_CNTL`.
- **Proibição Estrita de Escrita**: `CP_HQD_ACTIVE` (0xC82C), `PQ_WPTR_LO` (0xC8FC) e `PQ_WPTR_HI` (0xC900) permanecem estritamente **fora** da allowlist de escrita.

---

## 3. Telemetria e Probes de Diagnóstico

A telemetria persistente no registro do Windows (`HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe`) registra os seguintes novos campos (SchemaVersion `74`):

| Probe Field | Chave de Registro | Descrição |
|:---|:---|:---|
| `BC250_PROBE_HQD_PROGRAM_STATUS` | `HqdProgramStatus` | Status da transação (`0 = BC250_HQD_PROGRAM_OK`) |
| `BC250_PROBE_HQD_PROGRAM_ORIGINAL_SELECTOR` | `HqdProgramOriginalSelector` | Seletor original antes da transação (`0x00000000`) |
| `BC250_PROBE_HQD_PROGRAM_SELECTED_SELECTOR` | `HqdProgramSelectedSelector` | Seletor aplicado para MEC1/P0/Q0 (`0x00000004`) |
| `BC250_PROBE_HQD_PROGRAM_RESTORED_SELECTOR` | `HqdProgramRestoredSelector` | Seletor readback após restauração (`0x00000000`) |
| `BC250_PROBE_HQD_PROGRAM_READ_CALLS` | `HqdProgramReadCalls` | Contagem total de leituras MMIO na transação |
| `BC250_PROBE_HQD_PROGRAM_WRITE_CALLS` | `HqdProgramWriteCalls` | Contagem total de escritas MMIO na transação |
| `BC250_PROBE_HQD_PROGRAM_MEMORY_PASSED` | `HqdProgramMemoryPassed` | Flag de validação prévia de memória MC (1 = Sucesso) |
| `BC250_PROBE_HQD_PROGRAM_READBACK_PASSED` | `HqdProgramReadbackPassed` | Flag de auditoria da dupla leitura (1 = Sucesso) |
| `BC250_PROBE_HQD_PROGRAM_ACTIVE_ZERO` | `HqdProgramActiveZero` | Confirmação de que `ACTIVE` permaneceu 0 (1 = Sucesso) |
| `BC250_PROBE_HQD_PROGRAM_RESTORE_PASSED` | `HqdProgramRestorePassed` | Rollback de registradores e seletor validado (1 = Sucesso) |
| `BC250_PROBE_HQD_PROGRAM_MEM_RESTORE_PASSED` | `HqdProgramMemRestorePassed` | Limpeza e restauração de buffers MC validada (1 = Sucesso) |
| `BC250_PROBE_HQD_PROGRAM_PHYSICAL_READY` | `HqdProgramPhysicalReady` | Prontidão física do gate inativo (1 = Sucesso) |

---

## 4. Veredito Formal

```text
=============================================================================================
Veredito Global: NÃO VALIDADO EM HARDWARE (Aguardando avanço do RPTR do PM4)
=============================================================================================
```
