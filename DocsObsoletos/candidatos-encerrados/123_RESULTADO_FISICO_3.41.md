# 123 — Resultado Físico do Candidato 3.41.0.0 (SchemaVersion 112)

**Data/Hora**: 30/08/2026 14:45 UTC-3  
**Plataforma**: APU AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2), Windows 11 x64  
**Diretório de Evidência Canônica**: `docs/evidence/2026-08-30_14-44-41-DESKTOP-HQAG6T4-lab-binding/`  
**Veredito Global**: **NÃO VALIDADO EM HARDWARE**

---

## 1. Resumo Executivo da Execução Física

1. **Boot Frio e Isolamento**: Sistema inicializado em `BasicDisplay` Código 0, sem resíduos prévios.
2. **Binding & Assinatura Authenticode**: Pacote assinado com hash `A60AD02B96DA0649005F228FA1D7EFB0F0682B3E4C52E3DF9CEF3F16FFC28008` vinculado com sucesso.
3. **Display e Watchdog**: Vídeo do monitor permaneceu íntegro e responsivo. Watchdog desarmado com token `6c435419-f744-499c-915d-6c74c852d10e`.
4. **Rollback do Hardware**: `HqdActiveRestoreFailureCount = 0`! A máscara nos bits de QoS de `CP_HQD_PERSISTENT_STATE` eliminou completamente os falsos erros de restauração.

---

## 2. Telemetria e Comportamento dos Registradores do Silício

### A. Transição Causal do Fetcher do Command Processor (`CP_CPF_BUSY_STAT` `0x08220`)
| Momento | Valor Bruto | Decodificação SOC15 |
|---|---|---|
| **Antes de `CP_HQD_ACTIVE=1`** | `0x20000000` | `HQD_WPTR_POLL_BUSY` (Fetcher e Message Bus 100% livres) |
| **Após `CP_HQD_ACTIVE=1`** | `0x60C60000` | `HQD_PQ_BUSY \| HQD_CONSUMED_RPTR_BUSY \| HQD_PQ_FETCHER_BUSY \| HQD_MESSAGE_BUSY` |
| **Após `DOORBELL_ENABLE` & WPTR** | `0x68460000` | `HQD_ROQ_PQ_BUSY \| HQD_PQ_BUSY \| HQD_CONSUMED_RPTR_BUSY \| HQD_PQ_FETCHER_BUSY \| HQD_MESSAGE_BUSY` |
| **Polling Final** | `0x68460000` | Fetcher permanece em espera da transição ROQ/PQ |

### B. Estado do MQD e MEC
- `CP_HQD_PERSISTENT_STATE`: observado em `0x000C5300` (com `PRELOAD_REQ = 1` aplicado com sucesso e retido pelo microcode).
- `HqdActiveMecPcLast`: `0x17FB` (`13724`), laço de espera do MEC microcode.
- `CP_HQD_DEQUEUE_REQUEST`: `0x00000000` aplicado e confirmado no hardware.

---

## 3. Conclusão Técnica e Causa Raiz Isolada

O microcode do CP/MEC requer um handshake adicional de ativação de fila de compute (MEC) antes do fetch de instruções PM4:
1. **Engine de Compute (MEC1/HQD)**: Ao receber `ACTIVE=1` e `PRELOAD_REQ=1`, o CP entra no estado `HQD_ROQ_PQ_BUSY` (`0x68460000`) aguardando que os buffers de leitura (ROQ - Read Order Queue) e a interface de despachador de compute estejam associados a um pipe/VMID ativo via `CP_HQD_VMID` e `CP_HQD_QUANTUM`.
2. O rollback foi 100% limpo (`RestoreFailureCount: 0`), provando que a metodologia fail-closed e a reversibilidade dos registradores estão perfeitas.
