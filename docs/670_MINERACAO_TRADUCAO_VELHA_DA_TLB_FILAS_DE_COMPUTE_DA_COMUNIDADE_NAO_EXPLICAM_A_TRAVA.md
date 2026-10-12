# 670 — Mineração: tradução velha na TLB (o achado das filas de compute da comunidade) não explica a trava

**Data:** 09/10/2026. **Natureza:** offline, sem corrida. Pergunta do Daniel: a trava pode vir das filas de compute "quebradas" nesta placa?
**Scripts:** `remap.py` e `inflight.py` (scratchpad da sessão). Lógica descrita abaixo, reprodutível a partir dos logs.

## 0. O achado da comunidade

Fonte: `referencias/AMD-BC-250-Windows-Driver/docs/BC250-LINUX-RESEARCH-FINDINGS.md` §6.
- **Defeito:** no Linux com ROCm, sob o agendador de hardware (KFD/HWS), a invalidação da TLB por PASID não acha a VMID, porque `ATC_VMID*_PASID_MAPPING` nunca é escrito. A GPU continua usando a tradução da geração anterior depois de um `hipFree`.
- **Sintoma:** dados corrompidos (13/18 execuções), não trava.
- **Contorno:** reconstruir a runlist.
- **Achado à parte:** forçar a invalidação por KIQ ou por MMIO no ENG0 trava a placa.

## 1. Nossa invalidação chega à VMID certa? Sim (P1, leitura de código)

- **Quem chama:** `Bc250ActiveFlushTlb` (`bc250_active_miniport.c`, ramo 4c.4).
- **Processo de paginação:** VMID 8 (a VMID dele).
- **Qualquer outro processo:** `Bc250ActiveTlbFlushVmidMasks`, ou seja, as fixas 7/8/9 mais todas as VMIDs do pool instaladas. Com isso as VMIDs dinâmicas do GFX (01–06, 10 na 252) entram.
- **Como invalida:** pela VMID direto, no anel do SDMA0, engine 1, com espera do ACK (`sdma_v5_0_ring_emit_reg_write_reg_wait`). Não usa PASID nem KIQ, e não temos HWS.

## 2. As invalidações concluem? Sim (P0)

- **252:** 14.360 FLUSH_TLB e 5.656 IBs do SDMA0, **todos aposentados** (0 sem retire). Um ACK que não voltasse prenderia o anel.
- **Zero FlushTlb órfão.**
- **As 3.552 faixas "parciais" da P4.2:** ficam no IB do SDMA0 em vez de ir ao SDMA1. Não são escritas perdidas.

## 3. "Mesma VA, memória nova" nas superfícies de composição (P0, 40 corridas de outubro)

`[ResolveGpuVa]` por (processo, VA):
- Remapeamentos por corrida: 0 a 34.
- **Nas últimas 2.000 linhas antes do fim do log: zero, em todas as 40.**
- Muitas travas não tiveram nenhum remapeamento no bind inteiro: 244, 245, 246, 248, 248b, 251b.

**Limite:** só cobre as superfícies que passam pela composição (4–56 VAs por corrida).

## 4. A janela de rascunho do processo de paginação (o caso de risco real)

**O padrão:** `FILL_POR_COPIA` (848 na 252) escreve sempre em **VA 0x240000/0x250000** do processo de paginação. A cada operação o VidMm aponta essa VA para outra alocação. As PTEs desse processo são escritas **pela CPU, no build** (o VidMm força CPU_VIRTUAL para ele). É o padrão "mesma VA, memória nova" em alta frequência, com o risco do doc 566 (a CPU remapeia debaixo de um IB em voo).

**Medido** (FLUSH_TLB com IB do SDMA0 submetido e não aposentado, por QPC):

| Corrida | Trava? | FLUSH_TLB com IB em voo |
|---|---|---|
| 252 (09/10 08:51) | sim, ~124 s | 203 / 14.608 (1,4%) |
| 251b (08/10 22:47) | sim, ~53 s | 85 / 6.999 (1,2%) |
| 250 (08/10 15:39) | sim | 310 / 36.001 (0,9%) |
| 249 (08/10 14:42) | sim | 835 / 94.133 (0,9%) |
| 245 (07/10 22:25) | sim | 525 / 31.897 (1,6%) |
| 237 (06/10 18:52) | **não**, 600 s | 146 / 14.557 (1,0%) |
| 238 (06/10 21:54) | **não**, ~600 s | 160 / 21.312 (0,8%) |
| 226 (05/10 16:15) | **não**, 600 s | **18.737 / 60.484 (31,0%)** |
| 222 (05/10 14:09) | **não**, 600 s | **1.972 / 10.649 (18,5%)** |

As duas corridas com **mais** sobreposição (226 e 222, conclusão ainda por timer, latência ~1,3 ms) não travaram. A sobreposição não separa trava de não trava. **Como causa suficiente, está refutada pela medição** (P0 para a métrica; o mecanismo exato da sobreposição continua H).

## 5. Veredito

- **Achado da comunidade:** o mecanismo dele (invalidação que não chega à VMID) não existe no nosso caminho.
- **Invalidação forçada que trava:** não acontece nas nossas ~14 mil invalidações por corrida pelo anel.
- **Nos dados:** nenhum remapeamento de VA precede as travas. A troca da janela de rascunho com IB em voo é mais frequente em corridas que **não** travaram.

As filas de compute e a tradução velha da TLB saem das causas suficientes da trava dura (P2 no conjunto, P0 nas métricas).

**Fica em aberto:** a fila de fences do MEC fica ativa o tempo todo, e nunca houve corrida sem ela. É o único elemento ligado às filas de compute que segue sem teste.

**NÃO VALIDADO EM HARDWARE:** a causa da trava.
