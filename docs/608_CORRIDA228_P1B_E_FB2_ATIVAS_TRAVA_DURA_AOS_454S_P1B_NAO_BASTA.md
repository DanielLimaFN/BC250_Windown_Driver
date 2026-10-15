# 608 — Corrida 228: P1b e FB.2 ativas, trava dura aos ~454 s; a P1b não basta

**Corrida 228:** 05/10/2026 22:47:09 · lab **3.375.1.0** (SYS `48281B18…`) · critério: [doc 607](607_FB1_MOTIVO_DA_FALHA_DO_BLT_SOBREVIVE_AO_BUGCHECK_E_CRITERIO_CORRIDA228.md) §4/§6
**Evidência:** `docs/evidence/2026-10-05_22-47-09-DESKTOP-HQAG6T4-lab-binding/` e o log vivo `C:\BC250_DISPLAY_LOG.txt` a partir do `start_offset` do baseline. A coleta automática do boot das 22:54 estava pendente quando este doc foi escrito.

## 1. O que aconteceu (P0)

**Pré-condição válida:**
- `[P1B_VRAM_DRAM] ATIVO`, StartDevice `status=0`, GFX online;
- **zero** `[HDP1_FLUSH]`/`[HDP_PT_FLUSH]` em regime.

**Regime:**
- 15.760 IBs executados;
- 3.401 Presents;
- 2.749 cópias MPO3 da primária terminadas (`PRIMARY_COPY ... END`).

**Falhas antes do fim:**
- zero `Blt Timeout`/`Blt RECUSADO`/`PRIMARY_COPY FAIL`;
- zero TDR, timeout, fault ou `DEVICE_REMOVED`;
- 11 `ResolveGpuVa RECUSADO` (superfície de outro processo, lacuna conhecida do doc 601).

**Fim, com a assinatura de sempre:**
- as últimas linhas são `IB EXECUTADO` com RPTR==WPTR e fence sobre poison, depois um `GFX_TRIPE_VEREDITO` e VSyncs;
- tudo aposentado, e depois silêncio.

**Duração:** do `InitialFrameCount=9047` até o último `OTG_Frame=34534` são 25.487 quadros a 56,13 Hz, ou **≈454 s**. É o limite inferior, porque o fim do log é a última drenagem (doc 582).

**Coleta automática (acrescentado depois):**
- teste de RAM 7.954 MB × 2, zero erros;
- o rastro de paginação termina no tique 3745, **≈374,5 s** depois do início do rastro, com uma **rajada de despejo de 46,8 MB** (608 flushes de TLB) no último segundo ativo;
- **divergência aberta:** o contador do monitor dá ≈454 s desde o `DisplayInit`. Os dois números não fecham; nenhum dos dois é escolhido aqui.

**Plataforma:**
- reboot com Kernel-Power 41 e EventLog 6008;
- **sem** evento 1001 e **sem** `MEMORY.DMP` novo: não houve bugcheck;
- o fusível recusou o StartDevice no boot seguinte (Código 43), como deve.

**Relato do Daniel:** "deu tela preta".

## 2. Veredito: desfecho B

**A P1b não é causa suficiente.** Com a CPU acessando a VRAM pela DRAM e nenhum flush de HDP em regime, a trava dura voltou, igual. A BAR/HDP sai da lista de causas suficientes, como antes saíram:
- D2 (anel e Work do SDMA0 na RAM);
- P1 (instrumentação na BAR);
- P7 (anel do GFX em GTT);
- P2 (WPTR do SDMA0 por doorbell).

A 226 (600 s) entra na mesma conta da 222: não atribuível.

**FB.2:** nenhuma ocorrência da corrida da fence em 2.749 cópias. Ausência num ensaio não prova a correção (P2), mas é coerente com ela.

## 3. Próximo item da paridade (doc 593 §4)

| # | Diferença | Situação |
|---|---|---|
| D2, P1, P7, P2, P1b | — | aplicadas; nenhuma suficiente |
| **P3** | conclusão por interrupção (IH), em vez de polling de 1 ms | **próxima** |
| P4 | SDMA1 | aberta |
| P5 | VM de 4 níveis | aberta |
| P6 | MTYPE UC nos BOs coerentes da RADV no segmento 2 | aberta |

**Pendências:**
- conferir a coleta automática: teste de RAM, `RESET_REASON` e o rastro de paginação dos últimos tiques;
- desenhar a P3 por leitura do `amdgpu` (IH ring, `IH_RB_CNTL`, interrupções de fence do SDMA0 e do GFX).

**NÃO VALIDADO EM HARDWARE:**
- a causa da trava dura;
- a FB.2 sob a corrida real que a disparou.
