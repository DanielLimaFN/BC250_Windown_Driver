# 671 — Lista de causas possíveis da trava dura e soluções, por paridade com o `amdgpu`, sem perder performance

**Data:** 09/10/2026. **Natureza:** offline. Pedido do Daniel: lista de causas e soluções; não perder performance, o ideal é ser igual ao Linux.

**Regras da lista:**
- Toda solução é o que o `amdgpu` faz (P1, `inc/linux_ref`).
- Nenhuma custa performance. A maioria **ganha**, porque o caminho do Linux é o rápido.
- Ordem: diferença real com o Linux, no caminho de memória/fabric (a assinatura é "GPU ociosa, máquina morta"), e custo.

## 1. Já fora das causas suficientes (medido)

**Display e memória:**
- DCN/scanout (L1 + 251b);
- TMR (TMR.1);
- forma da árvore de páginas (252, P5);
- tradução velha da TLB e filas de compute da comunidade (doc 670);
- CG/MGCG do SDMA0 (o Linux não mexe em CG neste chip: `nv.c` `cg_flags = 0`, e nós também não).

**Engines e paginação:**
- SDMA1 (P4);
- interrupções e conclusão por IH (P3);
- doorbell do SDMA0 (P2);
- anel e fences em GTT (D2/P7);
- CPU na VRAM pela DRAM no KMD (P1b);
- PTE pelo SDMA (4c);
- CONST_FILL;
- instrumentação (P1);
- WPTR polling do SDMA. O `sdma_v5_0.c:723` liga `F32_POLL_ENABLE` também, então estamos iguais.

## 2. Diferenças que sobram, com solução

| # | Diferença | Linux (P1) | Nós (P0/P1 do nosso código) | Solução (= Linux) | Performance | Custo |
|---|---|---|---|---|---|---|
| **C1** | **Como o anel GFX recebe o WPTR** | `gfx_v10_0_cp_gfx_set_doorbell`: doorbell no anel GFX, `CP_RB_WPTR_POLL_CNTL` no default (só é escrito no caminho de CG, desligado neste chip) | doorbell **fechada** (`DoorbellControl escrito 0`); WPTR por MMIO `0xC150`; `CP_RB_WPTR_POLL_CNTL = 0x00400004`, com `POLL_FREQUENCY=4` escolhido por nós (H, D0.4, "escolha nossa") e o shadow do WPTR em GTT (RAM pela GART) | doorbell do GFX como o `amdgpu` (a abertura do BIF já funciona desde a P2c: SDMA0 e MEC provados) e `POLL_CNTL` de volta ao default | **+** (doorbell é o caminho rápido, sem MMIO por submissão) | médio |
| **C2** | **Polling de WPTR da fila do MEC** | `gfx_v10_0.c:6630`: `CP_PQ_WPTR_POLL_CNTL.EN = 0`; WPTR só por doorbell | a fila do MEC tem o bit dela em `CP_PQ_WPTR_POLL_CNTL1` = 1 e o KMD escreve `EN=1` (`HqdActiveWptrPollCntlApplied=0x80000000`). `EnLatched=0` no registro: se o polling roda de fato é P2 | `EN=0` e bit da fila em 0; WPTR só pela doorbell, já provada no MEC (`HitObserved=1`) | = | baixo |
| **C3** | **CPU do Windows (VidMm/UMD) na VRAM pela BAR** | APU: a CPU acessa a VRAM pelo endereço físico da DRAM (`aper_base` = FB físico), sem passar pelo HDP (P1b, doc 602) | só o KMD usa a DRAM. O segmento 1 declara `CpuTranslatedAddress = CpuPhysicalStart` (BAR), então todo Lock da UMD e **toda PTE do processo de paginação escrita pela CPU** passam pela BAR/HDP. Na 252 são ~14 mil escritas, concentradas nas rajadas de paginação em que a máquina morre | `CpuTranslatedAddress` = PA da DRAM, como o `amdgpu` em APU | **+** (a BAR é o caminho lento) | médio. Risco: o VidMm recusar o mapeamento (a `DxgkCbMapMemory` recusou na 225) → Código 43, detectável no boot |
| **C4** | **Zeramento de alocação nova** | `amdgpu_fill_buffer`: `SDMA_OP_CONST_FILL`, fatias de até **4 MiB** (`fill_max_bytes = 0x400000`) | `FILL_POR_COPIA` (X2b): copia de um buffer de zeros, o que **dobra o tráfego de memória**, em fatias de 256 KiB. A 203 mostrou que voltar ao CONST_FILL **não cura** sozinho | CONST_FILL em fatias de 4 MiB | **+** (metade do tráfego, 16× menos pacotes) | baixo |
| **C5** | **PTE do processo de paginação escrita pela CPU no build** | todas as PTE vão no anel, em ordem com o uso | o VidMm força CPU_VIRTUAL para o processo de paginação. A troca da janela com IB em voo **não discrimina** (doc 670) | não dá para mudar (é o VidMm). Com a C3, essas escritas param de passar pela BAR/HDP | = | — (vem junto da C3) |

## 3. Por que C1–C3 encabeçam

- **A assinatura:** em todas as travas a GPU está ociosa e a máquina morre entre duas observações, durante ou logo depois de rajadas de paginação. Na W1, sem o nosso driver, foram 45 min sem trava.
- **C1 e C2 (H):** com a GPU "ociosa", o CP pode continuar consultando por conta própria o WPTR em memória de sistema pela GART, numa cadência que **nós** escolhemos. O Linux não faz isso. Se o polling for real, é o único tráfego que existe justamente quando a GPU parece parada.
- **C3 (H):** é o único acesso de CPU à VRAM que segue um caminho que o Linux nunca usa num APU (BAR + HDP), e ele cresce nas rajadas.

## 4. Plano, uma diferença por corrida

| Corrida | Mudança | Teste que fecha (pré-condição no log) |
|---|---|---|
| 253 | **C1** (doorbell do GFX + `POLL_CNTL` default) | `WPTR_CANAIS` com doorbell aberta e acerto visto pelo CP; `POLL_CNTL` lido = default; 600 s, só o menu |
| 254 | **C2** (polling do MEC desligado) | `CP_PQ_WPTR_POLL_CNTL` lido com EN=0 e bit da fila em 0; fences do MEC seguem fechando |
| 255 | **C3** (segmento 1 pela DRAM) | `QUERYSEGMENT4` com `CpuTranslated` = PA da DRAM; Lock da UMD OK; zero HDP |
| 256 | **C4** (CONST_FILL 4 MiB) | `X2B_FILL` com CONST_FILL e fatias de 4 MiB; FILLs com fence |

Cada uma fica mesmo sem curar a trava, porque é paridade e não custa performance. A régua continua sendo 600 s sem trava, com repetição.

## 5. Estado (atualizado em 09/10, depois da Corrida 255)

| # | Estado | Evidência |
|---|---|---|
| C1 | **Feita e provada (P0); não era a causa** | 253: doorbell com `HIT=1`, 769/769 submissões do GFX sem MMIO; trava igual aos ~40 s ([doc 673](673_CORRIDA253_C1_DOORBELL_DO_GFX_ATIVA_E_PROVADA_TRAVA_DURA_AO_ABRIR_O_MENU_DESFECHO_B.md)) |
| C2 | **Já estava igual ao Linux (P0); não era a causa** | probes da 252 e da 253: `CP_PQ_WPTR_POLL_CNTL` e máscara de filas em 0 em regime ([doc 672](672_C1_DOORBELL_DO_GFX_COMO_O_AMDGPU_C2_JA_EM_PARIDADE_MON1_TEMPERATURA_E_CRITERIO_CORRIDA253.md) §1) |
| C3 | **Feita e provada (P0); não era a causa.** Correção: o Windows já via a VRAM pela DRAM desde a P1b ([doc 674](674_C3_CORRIGIDA_A_VRAM_DO_WINDOWS_JA_ERA_PELA_DRAM_UM_SO_TIPO_WC_E_CRITERIO_CORRIDA254.md)); a diferença era o tipo de memória, e agora todo mapeamento da DRAM pelo KMD é WC, como o `ioremap_wc` | 254: 8 mapeamentos WC, zero UC, zero TDR/fault; trava igual ~1,8 s depois de o menu carregar ([doc 675](675_CORRIDA254_C3_VRAM_WC_ATIVA_TRAVA_DURA_AO_ABRIR_O_MENU_DESFECHO_B.md)) |
| C4 | **Feita e provada (P0); não era a causa.** CONST_FILL de 4 MiB, cópia de zeros dormente ([doc 676](676_C4_ZERAMENTO_POR_CONST_FILL_DE_4MIB_COMO_O_AMDGPU_E_CRITERIO_CORRIDA255.md)) | 255: 585 FILLs por CONST_FILL, zero cópia, zero TDR/fault; trava igual ~17 s depois de o menu carregar ([doc 677](677_CORRIDA255_C4_CONST_FILL_ATIVO_TRAVA_DURA_17S_DEPOIS_DO_MENU_DESFECHO_B.md)) |

**Lista esgotada:** C1–C4 estão iguais ao Linux e nenhuma era a causa. **NÃO VALIDADO EM HARDWARE:** a causa da trava. A causa da trava segue desconhecida.
