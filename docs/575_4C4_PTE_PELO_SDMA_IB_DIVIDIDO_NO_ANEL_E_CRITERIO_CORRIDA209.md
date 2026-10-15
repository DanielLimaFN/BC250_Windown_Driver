# 575 — 4c.4: PTE escrita pelo SDMA (GPU_VIRTUAL), IB de paginação dividido no anel, e o critério da Corrida 209

**Data:** 02/10/2026 · **Tipo:** implementação offline da etapa 4 da 4c ([doc 567](567_4C_COMPLETA_EM_5_ETAPAS_E_CRITERIO_CORRIDA205.md))
**Antecede:** [doc 574](574_CORRIDA208_4C3_JOURNAL_RESOLVEU_99_95_E_TRAVA_DURA_NO_ZERAMENTO.md) (Corrida 208: desfecho B; sobra a escrita de PTE pela CPU).

---

## 1. O problema de ordem que o contrato traz (P1)

No modo `GPU_VIRTUAL`, a sequência documentada de uma cópia ou de um fill (`examples.md` do WDDM) põe tudo **no mesmo paging buffer**, enviado de uma vez:

1. `UpdatePageTable` (origem);
2. `UpdatePageTable` (destino);
3. `FlushTLB`;
4. `TransferVirtual` ou `FillVirtual`.

A invalidação da TLB precisa ficar **entre** a escrita da PTE e a operação que a usa. Mas o SDMA **para no `SRBM_WRITE` dentro de IB** (P0, Corrida 205, doc 568). Por isso ela não pode ir no paging buffer.

## 2. O desenho (uma variável: quem escreve a PTE)

- **`GPUMMUCAPS.PageTableUpdateMode = GPU_VIRTUAL`.** Só arma com o tunable `PtUpdateGpuVirtual` (padrão 1) **e** com a invalidação pelo anel provada no boot. A decisão é tomada no `StartDevice`, antes do `GPUMMUCAPS` (a ordem foi medida na 208). Com 0 no registro, volta ao `CPU_VIRTUAL` da 208.
- **`UpdatePageTable` em modo GPU:**
  - O journal é encenado como antes.
  - As entradas viram `WRITE_LINEAR` **no paging buffer**, no endereço `PageTableAddress.GpuVirtual + StartIndex×8`. Esse é o mesmo formato de pacote do `sdma_v5_0_vm_write_pte` do Linux, até 32 PTEs por pacote, com multipasse.
  - **A CPU não toca a tabela.** O handler só usa o union como ponteiro quando o modo é `CPU_VIRTUAL`, porque o VidMm ainda força esse modo para as tabelas do próprio processo de paginação na inicialização.
  - **A convenção base + `StartIndex`** foi conferida no log da 208 (`FirstPteVA=0x280000`, `Start=128`, `CpuVa` = base da tabela de 0x200000). Ela vale para o modo CPU; para o GPU é **P1 por analogia** com o mesmo struct.
- **`FlushTlb` em modo GPU:**
  - Registra um **ponto de divisão**: a GPUVA do paging buffer onde ele caiu (`DmaBufferGpuVirtualAddress + DmaBufferWriteOffset`, o mesmo par que o X2b provou) e as VMIDs a invalidar.
  - Se o processo é o de paginação, só a VMID 8; senão, a máscara de sempre.
  - Nada vai antes do IB, e não há MMIO de invalidação.
- **Envio do Node 0 (só IB de paginação):**
  - Os pontos que caem dentro do IB viram `[pedaço 0][GCR][invalidação][pedaço 1][GCR]…[FENCE]` numa entrada do anel. Quem monta é o helper puro `Bc250SdmaEmitSplitIndirect`, testado offline.
  - A entrada pode **atravessar o fim do anel**, como o `amdgpu_ring_write`. O teto é de 960 dw.
  - Um ponto sem posição, ou vindo de um buffer já enviado, vai antes do IB, contado e logado (`stale`).
- **Teto por buffer:** com 12 pontos pendentes, o `UpdatePageTable` devolve `INSUFFICIENT_DMA_BUFFER` sem progresso e **antes** do journal. O VidMm envia o buffer e chama de novo (multipasse).
- **Journal falha fechado no `NO_SPACE`** (achado do doc 574 §5):
  - A faixa abortada fica marcada (`[PT_JOURNAL] TAINT`), e o gate pelo journal se recusa a resolvê-la, mandando para a caminhada.
  - A marca sai quando um `UpdatePageTable` posterior encena a faixa inteira.
  - Isto é correção de segurança do gate, não muda o que a GPU executa.
- O resto é igual à 208: L1, BPG1, 4c.3. A UMD é a mesma da 208.

**Riscos declarados:**
- É um caminho que **nunca rodou** com o nosso driver.
- O VidMm passa a mapear as tabelas dos processos no espaço do processo de paginação.
- Se a semântica de `GpuVirtual` não for a esperada, a PTE é escrita no lugar errado: fault, TDR ou trava.
- Se o VidMm não aceitar o multipasse sem progresso, pode vir `0x10E`.
- Tela preta no bind é desfecho possível.

**Testes e portões:**
- `build-contract` EXIT=0, auditoria de segurança PASS, lab package audit PASS, matriz de DDI PASS e noop PASS.
- **Gabarito novo, montado à mão** (`sdma_packet_builder_test.c`):
  - IB de 64 dw dividido no dw 32 com a invalidação da VMID 8: o segundo `INDIRECT` cai em `0x22C080`, com 6 NOPs de pré-pad;
  - divisão no início e no fim;
  - fusão de offsets iguais;
  - 5 recusas.
- **Portão novo `audit_4c4_pte_by_sdma_and_split_ib`, com 5 mutações, todas reprovadas:**
  1. o handler usa a GPUVA como ponteiro;
  2. o `FlushTlb` GPU volta a adiar a invalidação para antes do IB;
  3. pontos consumidos por IB que não é de paginação;
  4. `NO_SPACE` sem marca;
  5. o caminho sem handler escreve pela CPU no modo GPU.
- **Dois portões antigos reapontados**, com a mesma invariante:
  - X2a: toda cópia para o anel tem migalha, e agora conta também a cópia com índice módulo o anel;
  - X2b: a fonte dos zeros tem de estar **dentro** do IB enviado, não mais no início dele.

**Pacote (assinado, auditado, NÃO INSTALADO):** lab **3.356.1.0**.
- SYS `CE261EEF…DF66`, INF `5FEB4ABE…B2D0`, CAT `1F2CC6BA…AB69`; UMD `A1BD1657…` igual à da 208.
- Strings conferidas no SYS: `[PT_GPU] %s (tunable PtUpdateGpuVirtual`, `PT_GPU_SPLIT` (6×), `GPU_VIRTUAL (PTE por WRITE_LINEAR`, `TAINT:` e o tunable em UTF-16.
- Preflight `prepare-corrida209-4c4-pte-pelo-sdma.ps1`: hashes e manifesto conferem. Ele só recusou a CMOS antiga, que o roteiro lê logo antes.

## 3. Critério da Corrida 209 (escrito antes, Lei 10)

**Roteiro:** recuperação, boot frio, CMOS em 256 MB, `tools\EXECUTAR_CORRIDA209_4C4_PTE_PELO_SDMA_600S.cmd`, mesmo uso das 201–208. Se travar: botão de reset e **`tools\coletar-pos-trava.ps1` elevado ANTES da recuperação**.

**Sinais:**
- `[PT_GPU] ARMADO`;
- `GPUMMUCAPS … PageTableUpdateMode=GPU_VIRTUAL`;
- contagem de `[PT_GPU] UpdatePT #` (atualizações no paging buffer);
- `[PT_GPU_SPLIT] #` (IBs divididos) e os `stale`;
- `[PT_GPU] RECUSA` e `[PT_GPU_SPLIT] RECUSA`;
- `[ActiveUpdatePT] … Hardware PTEs written` (só deveria vir das atualizações `CPU_VIRTUAL` forçadas);
- `TAINT`;
- `GFXHUB_FAULT`, `TDR_QUIESCE`, `IB_TRIPOD TIMEOUT`;
- trava e BPG1.

| Desfecho | Assinatura (o que o log mostra) | Implicação (para onde vai) |
|---|---|---|
| **A** | Modo GPU armado e **em uso** (centenas de `UpdatePT` no paging buffer e de IBs divididos), zero fault, zero timeout, desktop funcional e **~600 s sem trava dura** | Compatível com a CPU escrevendo PTE como causa (n=1). A 4c.5 repete e limpa o MMIO de TLB restante |
| **B** | Modo GPU armado e em uso, sem fault novo, desktop funcional até a **trava dura** com a assinatura de sempre | A escrita de PTE pela CPU não é causa suficiente, e a 4c inteira sai da lista. Voltar às diferenças com o Linux que restam (SMU, fences por interrupção, SDMA1) |
| **C** | `GFXHUB_FAULT`, TDR que não é o gráfico conhecido, `IB_TRIPOD TIMEOUT`, `RECUSA` do caminho novo, `0x10E`/`0x119` ou Código 43 no bind | O caminho GPU errou (encoding, semântica de `GpuVirtual`, ordem da divisão ou multipasse). Voltar offline com o log; `PtUpdateGpuVirtual=0` reverte sem reinstalar |
| **D** | `[PT_GPU] DESARMADO`, `GPUMMUCAPS` em `CPU_VIRTUAL`, ou **zero** `UpdatePT` no paging buffer | A corrida não testou a 4c.4 |
| **E** | Algo fora do mapa | Descrever antes de julgar |

**Regras:**
- Desfecho parcial não conta. A e B exigem o modo GPU **em uso**: as atualizações dos processos chegando como `GPU_VIRTUAL`, não só as forçadas.
- Uma trava antes de o desktop subir, sem fault, conta como E e não como B.

**NÃO VALIDADO EM HARDWARE.**
