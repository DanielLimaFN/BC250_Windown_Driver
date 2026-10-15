# 597 — Corrida 220: P7 válida, GFX rodando pela GTT, trava dura igual aos ~137 s

> **Correção (05/10, mesmo dia):** a primeira versão do §4 dizia que o HDP por MMIO não rodou em regime e cortava a P1b. **Era falso.** O meu filtro mostrou só as últimas linhas `[HDP_PT_FLUSH]` e escondeu as 4 `[HDP1_FLUSH]`, que existem. O código chama `Bc250SdmaPresentHdp` em toda submissão do SDMA0 (`bc250_sdma_present.c`, caminho assíncrono), e o log só registra as quatro primeiras. A P1b continua no inventário.

**Data:** 05/10/2026 11:38:50 · **Candidato:** lab **3.368.1.0** (SYS `2B75E7D5…`) · **Critério:** [doc 596](596_P7_ANEL_E_FENCES_DO_GFX_EM_GTT_COMO_O_AMDGPU_E_CRITERIO_CORRIDA220.md) §4
**Evidência:** `docs/evidence/2026-10-05_11-38-50-DESKTOP-HQAG6T4-lab-binding/` (cauda recuperada de 14,3 MB, rastro de 2,97 MB, coleta automática e teste de RAM).

## 1. Pré-condição: válida (P0)

- `[P7_GFX_GTT] ATIVO`: 69.632 bytes na RAM PA `0x44DE00000` → GART VA `0x40000`, `PTE[64]=0x300044DE00077` (MTYPE_UC), divergentes=0, flush VMID0 ok.
- `BOOT_PREAMBLE WPTR=960 RPTR=960 Fence=1 DrenoOk=1`: o CP leu o CSB e o anel pela GART e escreveu a fence na RAM.
- `[GfxEngine] ONLINE SUCESSO ... Anel=RAM_GART(P7) CsbVram=0xF40FF81000`.
- Base da 219 mantida: D2 com a GART ligada, MEC nas PTE[0..31] (V0.1), P1 com migalhas só em RAM.

## 2. O que aconteceu

- **Trava dura aos ~136,8 s** (último tique do rastro: 1368 × 100 ms). Na 219 foi aos ~139 s; na 218, ~123 s.
- Os últimos eventos são `SUBMIT_GFX`/`RETIRE_GFX` (fences 1306–1308, 5,6–6,6 KB) e o SDMA0 aposentando, tudo retirado. GPU ociosa no último registro, como nas travas anteriores.
- Até a trava: 5.981 IBs executados, 1.304 Presents, 1.308 submissões GFX pela GTT. Zero TDR, timeout, fault, `INSIDE_IB`, `NO_SPACE` ou `DEVICE_REMOVED`.
- Os 23 `DIRTY_RECTS_RECUSADOS` são a bissecção DX.N desligada, comportamento esperado.
- Reboot com Kernel-Power 41 e EventLog 6008. `RESET_REASON=0x40080400`, igual às anteriores.
- **Teste de RAM após a trava:** 8.852 MB × 2 passadas, zero erros.
- **Rollback:** fusível disparado no boot (StartDevice recusado, como deve), e depois o watchdog levou a `RECOVERY_OK_ONLINE` com BasicDisplay em Código 0 (5 amostras).

## 3. Veredito: desfecho B

A P7 funcionou (o GFX rodou ~137 s com anel e fences na RAM, sem uma falha), e a trava ficou igual. **Anel e fences do GFX na VRAM pela BAR saem das causas suficientes.** A janela de teardown da P7 não chegou a ser exercitada.

## 4. O que cada submissão do SDMA0 faz pela BAR (leitura de código, P1)

Em regime, cada submissão assíncrona do SDMA0 faz:
- HDP: escreve `0x385C`, lê `0x378C`, escreve e lê o `HDP_INVALIDATE` do SDMA0;
- publicação: escreve `MINOR_PTR=1`, `RB_WPTR`, `RB_WPTR_HI` e `MINOR_PTR=0`.

São 6 escritas e 2 leituras de registrador por submissão (~6.000 nesta corrida). O amdgpu (`sdma_v5_0_ring_set_wptr`) faz uma escrita na shadow do WPTR e uma doorbell de 64 bits, sem registrador.

## 5. Inventário restante (doc 593 §4)

| # | Diferença | Situação |
|---|---|---|
| P1b | HDP por MMIO em cada submissão | aberta (§4) |
| P2 | WPTR por doorbell (SDMA0; o GFX no gfx10 também usa doorbell) | próxima recomendada |
| P3 | conclusão por interrupção (IH), em vez de polling de 1 ms | aberta |
| P4 | SDMA1 | aberta |
| P5 | VM de 4 níveis | aberta |
| P6 | MTYPE UC nos BOs coerentes da RADV no segmento 2 | aberta |

**Por que a P2:** a publicação do WPTR é o acesso de registrador mais frequente em regime (4 escritas por submissão do SDMA0, ~6.000 nesta corrida), e o amdgpu a faz por doorbell. A P1b vem logo depois, uma diferença por corrida.

**NÃO VALIDADO EM HARDWARE:** 600 s sem trava com 256 MB; teardown da P7.
