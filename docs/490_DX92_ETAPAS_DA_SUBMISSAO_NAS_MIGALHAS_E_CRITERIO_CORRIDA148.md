# 490 — DX.92: etapas da submissão do Node 0 nas migalhas BCR1, e o critério da Corrida 148

> 27/09/2026, offline. Origem: doc 489. Na trava da 147, a CPU 7 estava dentro de `SUBMIT_DDI` com **offset 0x000000 = Node 0 (SDMA)** e a CPU 9 dentro de um FLUSH_TLB (estágio 42), sem acesso MMIO em andamento em nenhuma das duas.

## 1. Leitura de código que motivou as marcas (P1)
Na troca de contexto, a submissão do Node 0 pode passar por duas chamadas custosas em DISPATCH:
- **`Bc250ActiveSdmaRetireAndNotify`**, quando há trabalho em voo;
- **`Bc250GcvmInstallVmid`**, que reescreve a raiz do VMID e faz flush de TLB pelo mesmo `Bc250ActiveFlushVmidTlb` (motor 17, `TlbLock`) que o FLUSH_TLB da paginação usa em outra CPU.

O código já registra quando troca de contexto com trabalho em voo (`AVISO CONTEXT SWITCH COM IN_FLIGHT`). Isso é **suspeito, não causa**.

## 2. O que mudou (somente instrumentação)
- **`Bc250CrumbStage`** (`bc250_mmio_breadcrumb.h`): reescreve só o sítio/offset do slot de submissão já aberto, com a mesma sequência e a fase ANTES mantida. É só escrita em RAM.
- **Offset do banco SUBMISSAO** = `(nó << 16) | etapa`. Etapas em torno de:
  - entrada (0x01);
  - `RetireAndNotify` (0x10/0x11);
  - `InstallVmid` (0x12/0x13);
  - resolução no snapshot (0x14);
  - caminhada da tabela de página do IB (0x16/0x17);
  - envio ao anel SDMA (0x18/0x19).
- **Decodificador:** publica `submit_node` e `submit_stage` para cada submissão em voo. Registros antigos saem como `ETAPA_0x0`.
- **Portão `audit_dx92_submit_stages_in_crumbs`**, com **quatro mutações e quatro reprovações**: etapa INSTALL_VMID removida, etapa RING_DONE removida, helper tocando MMIO, decodificador sem tabela.
- **Build:** `build-contract` EXIT 0, build de laboratório EXIT 0, auditorias de segurança e de pacote PASS.

| Artefato (3.298.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `0E5D14B4E99355A0575487B4D9BD021C1492CCCE4BE07BF3E75DD202596CB61A` |
| `bc250mcdm.sys` fonte lab | `4768B24763A200E0DA07D1542F1A28EA5859F6EC6E778E75CF7ABD13D6DD8EB1` |
| `bc250umd.dll` (inalterada) | `3EB5806557EF3DD9E41D5FD45CD5737D8A2D6FFC06038824946BF5C327F9CFB8` |
| `bc250mcdm.inf` | `DA45D6231DD18A1457E0C5BEF6EFCBD5FD791F750563429C75B62F276CC649E3` |
| `bc250mcdm.cat` | `58327EB35176B2E69AAFF2696AF8B9327B1B7AA615398A79FAD8E1B0E74AAD25` |

A DX.91 continua dentro deste pacote.

## 3. Critério da Corrida 148 (escrito antes)
**Runner:** `tools\EXECUTAR_CORRIDA148_DX92_600S.cmd`, **sem KD**; recusa se o boot estiver com DEBUG. Harness atrasado 150 s. Boot frio antes. Usar o desktop como nas 143/144/147.

Leitura: bloco `mmio_crumbs.in_flight_at_last_record` do decodificador.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Trava com submissão em voo em `INSTALL_VMID` ou `RETIRE_AND_NOTIFY`, e FLUSH_TLB em voo noutra CPU | Espera ligada à troca de VMID ou à aposentadoria concorrente com o flush. Próximo: ler essa chamada linha a linha. |
| **B** | Trava com submissão em voo em `PT_WALK_IB` ou `ANEL_SDMA` | O ponto é a caminhada da tabela (leitura de VRAM pela CPU) ou a escrita no anel. |
| **C** | Trava sem submissão em voo (só paginação) | O banco da 147 foi coincidência; o suspeito volta ao FLUSH_TLB sozinho. |
| **D** | Sem trava | Não reproduziu; repetir. |
| **E** | Algo fora do mapa, incluindo etapa `ENTRADA` parada, que aponta para o gate de admissão ou para antes da primeira marca | Descrever antes de julgar. |

**NADA DA DX.92 FOI VALIDADO EM HARDWARE.**
