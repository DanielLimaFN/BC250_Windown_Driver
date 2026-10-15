# 730 — Análise profunda: D19 estava arquitetonicamente errado desde a Corrida 275; o resto do DCN básico confere com o Linux

**Pedido do Daniel:** antes de rodar qualquer coisa, analisar o código do DCN ponto a ponto, comparar com o Linux, e consultar o driver oficial da AMD em `C:\` se necessário. Sem corrida.

## 1. Resumo da conclusão

**Achei a causa real, e ela não é `V_TOTAL_MIN`/`MAX` sozinho (doc 728 estava incompleto) — é que escrever `OTG_V_TOTAL` diretamente, num pipe JÁ ativo (herdado do GOP, sem bloquear nada antes), não é como a AMD faz isso em NENHUM caminho do driver real.** Não existe um terceiro caminho "escreve e pronto" no código deles. Existem só dois:
1. **Modeset completo**: apaga a tela (`blank_pixel_data`), programa tudo (`program_timing`), religa (`enable_crtc`), espera confirmar o apagamento antes de prosseguir.
2. **DRR (Dynamic Refresh Rate) de verdade**: nunca toca o `V_TOTAL` normal — só `V_TOTAL_MIN`/`MAX` **com os bits de seleção ligados** e um gatilho de hardware (`TRIG_A`/`OTG_STATIC_SCREEN_CONTROL`) para sincronizar a troca.

**O D19 (desde a Corrida 275) fazia um terceiro caminho que a AMD nunca usa: escrever `V_TOTAL` direto, num pipe ativo, sem apagar a tela e sem ligar o mecanismo de DRR.** Funcionou "por sorte" na 275 (a escrita calhou de cair numa janela de retraço vertical inofensiva) e falhou nas quatro vezes seguintes (276-280).

## 2. Por que a correção da doc 728 não resolveu (dado que eu não tinha olhado antes)

Fui ver o log da própria Corrida 280, que eu nunca tinha lido linha a linha depois de implementar o diagnóstico:

```
[DCN2_VTOTAL_CONTROL] ANTES: V_TOTAL=1110 V_TOTAL_MIN=0 V_TOTAL_MAX=0 CONTROL=0x00000000 (MIN_SEL=0 MAX_SEL=0 ...)
[DCN2_VTOTAL] MIN/MAX ACOMPANHARAM: MIN 0x0->0x534, MAX 0x0->0x534 (readback OK)
[DCN2_VTOTAL_CONTROL] DEPOIS DO APLICADO: V_TOTAL=1332 V_TOTAL_MIN=1332 V_TOTAL_MAX=1332 CONTROL=0x00000000 (MIN_SEL=0 MAX_SEL=0 ...)
```

**`CONTROL` fica `0x00000000` do início ao fim — `MIN_SEL=0` e `MAX_SEL=0` sempre.** Escrevi os valores de `MIN`/`MAX` (e a escrita realmente aconteceu, confirmada por readback), mas **nunca liguei os bits que mandam o hardware USAR esses registradores**. Com `SEL=0`, o hardware ignora `MIN`/`MAX` completamente — minha "correção" da doc 728 foi, na prática, **inerte**: não mudou nada no comportamento real do silício, só adicionou leitura/escrita de registradores que o hardware não consultava. Por isso a 280 repetiu exatamente o mesmo sintoma da 276-279 — não porque a hipótese do `MIN`/`MAX` estivesse certa e a implementação falhou por outro motivo, mas porque eu implementei a metade errada do mecanismo.

## 3. O mecanismo real de DRR da AMD (`inc/linux_ref/dcn/dcn10_hwseq.c:3196`, `dcn10_optc.c:924`)

```c
// dcn10_hwseq.c, dcn10_set_drr() -- é isto que dc_stream_adjust_vmin_vmax chama em tempo real
params.vertical_total_max = adjust.v_total_max;
params.vertical_total_min = adjust.v_total_min;
tg->funcs->set_drr(tg, &params);                 // NUNCA toca OTG_V_TOTAL
if (adjust.v_total_max != 0 && adjust.v_total_min != 0)
    tg->funcs->set_static_screen_control(tg, 0x800 /* TRIG_A */, 2 /* num_frames */);

// dcn10_optc.c, optc1_set_drr() -- o que a chamada acima realmente escreve
optc->funcs->set_vtotal_min_max(optc, min-1, max-1);   // OTG_V_TOTAL_MIN e MAX
REG_UPDATE_5(OTG_V_TOTAL_CONTROL,
    OTG_V_TOTAL_MIN_SEL, 1,          // <- isto que faltou
    OTG_V_TOTAL_MAX_SEL, 1,          // <- isto que faltou
    OTG_FORCE_LOCK_ON_EVENT, 0,
    OTG_SET_V_TOTAL_MIN_MASK_EN, 0,
    OTG_SET_V_TOTAL_MIN_MASK, 0);
optc->funcs->setup_manual_trigger(optc);                // arma TRIG_A
```

`OTG_V_TOTAL` (o registrador "normal") **nunca é escrito** neste caminho. A troca de refresh ao vivo na AMD é inteiramente feita por `MIN`/`MAX`/`SEL`/gatilho — nunca pelo valor direto.

## 4. O outro caminho, o de modeset completo (`dcn20_hwseq.c:897-957`)

```c
tg->funcs->program_timing(tg, &stream->timing, ...);   // escreve OTG_V_TOTAL direto
...
hws->funcs.blank_pixel_data(dc, pipe_ctx, true);        // APAGA a tela primeiro
tg->funcs->enable_crtc(tg);                             // (re)liga o CRTC
hws->funcs.wait_for_blank_complete(opp);                // espera confirmar
```

`program_timing` (que escreve `V_TOTAL` direto, como o nosso D19 fazia) **só é chamado com a tela apagada**, nunca num pipe ativo exibindo imagem. É exatamente o padrão que falta no nosso código: nós nunca apagamos nada antes de escrever `V_TOTAL`.

## 5. Isso explica tudo, retroativamente

- **Por que a 275 "funcionou"**: sorte de tempo — a escrita caiu numa janela em que o retraço vertical já estava na área de blank, sem corromper a área ativa visível.
- **Por que 276-280 falharam igual, independente de D12, do monitor, do cabo**: a causa nunca esteve no D12 nem no monitor — estava em escrever um registrador de temporização "normal" (não double-buffered da forma que presumimos) num pipe ativo, sem nenhum dos dois mecanismos de segurança que a própria AMD usa. A correção de `MIN`/`MAX` (doc 728) não ativou o mecanismo de DRR porque faltava `SEL=1` — ficou sem efeito.

## 6. O resto do DCN básico (D02 flip, D10 cursor) confere com o Linux — sem achado novo

Fui verificar se o mesmo tipo de problema (escrita sem sincronização) poderia afetar o que já está provado:

- **D02 (flip)**: `HUBPREQ0_DCSURF_FLIP_CONTROL`/`PRIMARY_SURFACE_ADDRESS`/`EARLIEST_IN_USE` são, por desenho de hardware, **duplamente bufferizados** — o HUBP troca o endereço sozinho no próximo VSYNC, sem precisar de `OTG_MASTER_UPDATE_LOCK`. Confirmado em `dcn20_hwseq.c:dcn20_pipe_control_lock`: a AMD só chama `tg->funcs->lock()` (o `OTG_MASTER_UPDATE_LOCK`) quando **não** está em modo "flip imediato"; no modo "flip imediato" (o nosso caso), não há lock porque o hardware já cuida disso. **Isso explica por que o nosso flip nunca precisou de lock e nunca deu problema em 12 corridas.**
- **D10 (cursor)**: `CNVC_CUR0_CURSOR0_CONTROL` tem um bit `CUR0_UPDATE_PENDING` — também double-buffered por natureza, mesmo padrão do flip. Consistente com a estabilidade medida (138 formas, 14.390 movimentos na 273, zero problema).
- **Conclusão**: D02 e D10 são corretos por desenho, não por sorte — ao contrário do D19, que violava a única regra que o resto do DCN sempre respeitou (nunca escrever um registrador "ao vivo" sem o mecanismo de sincronização certo).

## 7. Consulta ao driver oficial em `C:\`

Em `C:\AMD` existe o **NimeZ-25.3.1**, um pacote de instalação de driver AMD genérico para GPU de mesa (Adrenalin), não específico da Cyan Skillfish 2/BC-250 (que não tem driver oficial da AMD — é por isso que este projeto existe). Como o `inc/linux_ref/` já deu uma resposta completa, exata e proveniente do código-fonte real da AMD para o cenário exato (`dcn10_set_drr`, a função que a API pública de DRR chama), não foi necessário desmontar o binário do driver de mesa — a fonte Linux é a mesma arquitetura (`dc`/`DCN`) que o driver Windows da AMD usa, só que nós temos o código, não o binário.

## 8. Caminhos possíveis (nenhum implementado ainda — análise, não código)

**Caminho A — DRR de verdade.** Implementar exatamente `dcn10_set_drr`/`optc1_set_drr`: `V_TOTAL_MIN`/`MAX` com `SEL=1` nos dois, mais o gatilho `TRIG_A`/`OTG_STATIC_SCREEN_CONTROL`. Os registradores de gatilho (`mmOTG0_OTG_TRIGA_CNTL`=0x1b39, `mmOTG0_OTG_STATIC_SCREEN_CONTROL`=0x1b82) já estão catalogados para este chip. Mais fiel ao que a AMD faz de verdade; mais registradores novos para provar certo.

**Caminho B — Modeset com blank.** Reusar o mecanismo de DPMS (`OTG_BLANK_CONTROL`, já provado desde a 272) para apagar antes de escrever `V_TOTAL`/`MIN`/`MAX` e religar depois. Mais simples, reaproveita código já confiável; mais invasivo (a tela apaga de propósito por um instante, ao contrário da troca "sem ninguém notar" que a DRR de verdade permite).

**Nenhum dos dois foi implementado.** O Daniel decide se quer perseguir isso agora ou manter o plano já em andamento (Corrida 281, DCN básico sem D19/D12, voltando à investigação da trava dura) — a Corrida 281 **não é afetada por este achado**, porque o pacote já preparado (`3.430.1.0`) não arma a chave de D19 (`V_TOTAL` nunca é escrito nela).

## 9. Pendências

- **NÃO VALIDADO EM HARDWARE** (análise pura, zero corrida).
- `BASE_DE_AVANCO.md` atualizado: a linha D19 passa a citar esta causa (mais completa e correta que a doc 728) em vez da hipótese incompleta de `MIN`/`MAX`.
