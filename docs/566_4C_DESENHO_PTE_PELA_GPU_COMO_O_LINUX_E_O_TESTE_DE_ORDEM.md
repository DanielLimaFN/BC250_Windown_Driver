# 566 — 4c: desenho da atualização das tabelas de página pela GPU (como o Linux) e um teste de ordem mais barato

**Data:** 02/10/2026 · **Tipo:** offline, desenho, sem código ainda
**Antecede:** [doc 565](565_CORRIDA204_4B_VALIDO_TRAVOU_LEITURA_DA_CPU_NAO_BASTA.md) (Corrida 204, desfecho B).

---

## 1. O que o nosso driver faz hoje (P0, leitura do fonte)

- **Modo declarado:** `DXGK_GPUMMUCAPS.PageTableUpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL` (`bc250_mcdm_callbacks.c`, `GPUMMUCAPS`).
- **`UpdatePageTable`** (`DXGK_OPERATION_UPDATE_PAGE_TABLE`): o handler escreve as PTEs **pela CPU, pela BAR, no momento do build** (`[ActiveUpdatePT] … Hardware PTEs written`) e faz o flush do HDP.
- **`FlushTlb`** (`DXGK_OPERATION_FLUSH_TLB`): `Bc250ActiveFlushTlb` invalida a TLB do GFXHUB **por MMIO, na hora do build** (`Bc250ActiveFlushMappedGfxhubTlb`). Nada vai para o paging buffer.
- **A cada submit de paginação**, o KMD ainda **caminha** as tabelas pela CPU (`[PT_WALK]`).

## 2. O que o Linux e o próprio WDDM preveem (P1)

- **Linux** (`amdgpu_vm.c`, doc 563 §3): VM gráfica atualiza as tabelas **pelo SDMA**, num IB do anel do kernel, com `WRITE_LINEAR`/`PTEPDE` (`sdma_v5_0_vm_write_pte`, `sdma_v5_0_vm_set_pte_pde`). A invalidação de TLB também vai **em ordem no anel** (`emit_vm_flush`: escrita de registrador + `POLL_REGMEM`).
- **WDDM** (`referencias/windows-driver-docs/.../system-paging-process.md`, `examples.md`, `d3dkmddi.h`): existem os modos `DXGK_PAGETABLEUPDATE_GPU_VIRTUAL` e `GPU_PHYSICAL`.
  - Nesses modos o `UpdatePageTable` traz `PageTableAddress.GpuVirtual`: a tabela é mapeada pelo VidMm no espaço do processo de paginação. O driver emite os pacotes **no paging buffer**, executados em ordem.
  - Só as tabelas do **próprio processo de paginação**, na inicialização e em transição de energia, são forçadas a `CPU_VIRTUAL`.
- **A sequência documentada de um FILL** (`examples.md`):
  1. `UpdatePageTable`: mapeia o destino na área de rascunho do processo de paginação;
  2. `FlushTLB`;
  3. `FillVirtual`.
  - Os logs confirmam que a área de rascunho é **reaproveitada**: `Dst=0x240000`, `0x23E000` e `0x250000` se repetem operação após operação.

## 3. A hipótese que isso levanta (H)

No nosso modo, a reescrita da PTE de rascunho e a invalidação da TLB acontecem **quando o VidMm monta** a próxima operação, e não **quando a GPU chega nela**.

Se o VidMm montar a operação N+1 enquanto o IB da operação N ainda executa no SDMA, a CPU remapeia **embaixo da GPU** a VA que ela está escrevendo. O resultado seria escrita de DMA em memória física errada, que é a trava clássica de plataforma. Isso casaria com o que se repete em todas as travas:
- uma operação de paginação sobre uma alocação nova, em voo ou recém-concluída;
- a VRAM "perdendo conteúdo" durante a trava (migalhas que decaem, doc 526);
- as travas concentradas nas rajadas de alocação (partida, menu, Pesquisa).

**O que eu não sei:** se, no modo `CPU_VIRTUAL`, o VidMm espera a conclusão do paging buffer anterior antes de pedir o `UpdatePageTable` seguinte. Não há texto local que diga. A tentativa de medir pelos logs falhou: `[UpdatePT]` é amostrado (27 linhas em 134 s) e as aposentadorias vêm em lote.

## 4. Duas formas de testar (uma variável cada)

| | O que muda | Tamanho | Responde |
|---|---|---|---|
| **4c-ordem** (barato) | Antes de escrever PTE ou invalidar a TLB pela CPU, o KMD **drena** o SDMA0: espera os IBs de paginação em voo aposentarem, com teto e log. Continua tudo pela CPU, só que nunca debaixo de trabalho em voo | Pequeno: um ponto de espera no `UpdatePageTable` e no `FlushTlb`, com contador e log | Se a trava é **ordem** (CPU remapeando debaixo da GPU). Se parar de travar, é isso. Se travar igual, a ordem não basta e vale a 4c completa |
| **4c completa** (como o Linux) | `PageTableUpdateMode = GPU_VIRTUAL`. `UpdatePageTable` vira `WRITE_LINEAR` no paging buffer. `FlushTlb` vira escrita do `GCVM_INVALIDATE_ENG*_REQ` + `POLL_REGMEM` do ACK no paging buffer. A caminhada `[PT_WALK]` e o "gate físico" do GFX saem, e a validação passa ao journal de software | Grande: muda o contrato de memória virtual com o VidMm. Mexe no handler de `UpdatePageTable`, no `FlushTlb`, nos dois gates de submit (SDMA e GFX) e no journal. Precisa de procedência dos pacotes SRBM/POLL_REGMEM do SDMA 5.0 e do protocolo do `INVALIDATE_ENG` | A trava some quando a CPU **não toca** tabela de página nenhuma, que é exatamente o estado do Linux |

**Riscos da 4c completa (declarados):**
- O VidMm passa a mapear as tabelas de cada processo no espaço do processo de paginação. É um caminho que nunca rodou com o nosso driver.
- Erro de encoding de pacote ou de protocolo do `INVALIDATE_ENG` vira fault de GPU ou TDR em vez de trava.
- Várias corridas de bring-up até estabilizar.

**Recomendação:** começar pela **4c-ordem**.
- Mede a hipótese de §3 com uma variável, em poucas horas de código.
- Se a trava acabar, o produto ganha a correção mínima. A 4c completa continua sendo o alvo de qualidade (o estado do Linux), sem urgência.
- Se a trava continuar, a ordem sai de suspeita e a 4c completa fica justificada pela medição, não só pela diferença com o Linux.

**NÃO VALIDADO EM HARDWARE.**
