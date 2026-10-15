# 728 — Achado: `OTG0_V_TOTAL_MIN`/`MAX` nunca acompanhavam o `V_TOTAL`; D19 corrigido como `optc1_program_timing`; pacote 3.429 para a Corrida 280

**Data:** 13/10/2026, offline (sem corrida). Pedido do Daniel depois de 4 corridas seguidas (276-279) com o mesmo sintoma de tela com artefatos: "pare e realmente analise o que foi feito... se não sabe, tente colocar logs para identificar."

## 1. O que mudou de hipótese

Doc 727 suspeitava de efeito cumulativo no **monitor**. O Daniel testou: desligou o monitor (voltou limpo) e também trocou o cabo DisplayPort (voltou normal). Com o monitor comprovadamente "limpo", repetimos a receita da 278 — **e o mesmo sintoma apareceu de novo**. Isso refuta de vez a hipótese do monitor: se resetar o monitor não resolve, o estado ruim não está nele. Só pode estar do **nosso lado**, numa parte do hardware que um boot frio do PC também não está limpando — ou seja, algo que nosso próprio código deixa inconsistente a cada vez que ESCREVE, não algo que se acumula entre corridas.

## 2. Fui ao código real da AMD, não só ao catálogo de endereços

`inc/linux_ref/dcn/dcn10_optc.c`, função `optc1_program_timing` (a rotina real que a AMD usa para programar temporização do OTG), linhas 221-229:

```c
v_total = patched_crtc_timing.v_total - 1;
REG_SET(OTG_V_TOTAL, 0, OTG_V_TOTAL, v_total);

/* In case of V_TOTAL_CONTROL is on, make sure OTG_V_TOTAL_MAX and
 * OTG_V_TOTAL_MIN are equal to V_TOTAL.
 */
optc->funcs->set_vtotal_min_max(optc, v_total, v_total);
```

**O driver real NUNCA escreve `OTG_V_TOTAL` sozinho.** Toda vez, ele também escreve `OTG_V_TOTAL_MIN` e `OTG_V_TOTAL_MAX` com o mesmo valor. O comentário do próprio código da AMD explica o porquê: existe um modo chamado `V_TOTAL_CONTROL` (campo `OTG0_OTG_V_TOTAL_CONTROL`, usado para VRR/Adaptive-Sync — "Dynamic Refresh Rate") em que, se ligado, o hardware pode usar `V_TOTAL_MIN`/`MAX` para variar a duração do quadro **em vez de** obedecer cegamente ao `V_TOTAL` simples.

Conferido contra `inc/linux_ref/dcn/dcn_2_0_3_offset.h`/`dcn_2_0_3_sh_mask.h` (mesma família de endereços que já usamos, `BASE_IDX=2`, igual ao `V_TOTAL`): os três registradores existem de verdade neste chip — `mmOTG0_OTG_V_TOTAL_MIN` (0x1b30), `mmOTG0_OTG_V_TOTAL_MAX` (0x1b31), `mmOTG0_OTG_V_TOTAL_CONTROL` (0x1b33), com os bits `V_TOTAL_MIN_SEL`/`V_TOTAL_MAX_SEL`/`FORCE_LOCK_ON_EVENT` etc.

**O D19, desde a Corrida 275, só escrevia `OTG_V_TOTAL` — nunca tocou nem leu `MIN`, `MAX` ou `CONTROL`.** Se o GOP (ou a VBIOS no boot) deixou `MIN_SEL`/`MAX_SEL` ligado, o valor "normal" que escrevíamos pode não ser o que realmente governa a geração de linhas/quadros — e a geração de sincronismo pode ficar numa combinação que nunca existiu em nenhum modo testado pela própria AMD (eles NUNCA escrevem um sem o outro). Isso bate exatamente com o padrão observado: medíamos Hz corretos pela nossa própria varredura (porque o `FRAME_COUNT`/posição que líamos é outro mecanismo), mas o que o monitor via na prática podia estar diferente — e **esse estado não é limpo por nada que fizemos até agora** (nem reboot do PC, nem resetar o monitor), porque **o problema está no nosso próprio registrador do lado da GPU**, não em nada que o monitor guarda.

## 3. O que foi feito (offline, sem corrida)

### 3.1 Diagnóstico (só leitura, zero risco)

Nova função `Bc250Dcn2LogVTotalControl`, que lê e loga `V_TOTAL`, `V_TOTAL_MIN`, `V_TOTAL_MAX` e os bits de `V_TOTAL_CONTROL`, chamada em três pontos de `Bc250Dcn2VTotalProve`: **antes** de qualquer escrita (mostra exatamente como o GOP deixou), **depois do `APLICADO`**, e **depois do `DEVOLVIDO`**. Isso vai responder, na próxima corrida, se `MIN_SEL`/`MAX_SEL` realmente estavam ligados.

### 3.2 Correção (escreve MIN/MAX junto com V_TOTAL, exatamente como `optc1_program_timing`)

`Bc250Dcn2VTotalProve` agora, depois de confirmar `V_TOTAL` aplicado:
- Lê e salva `V_TOTAL_MIN`/`MAX` atuais (os do GOP).
- Escreve os dois com o **mesmo valor** do `V_TOTAL` novo (Lei 6, RMW preservando o resto do registrador — o campo é de 15 bits, igual ao `V_TOTAL`).
- Confere por readback; se não bater, desfaz **tudo** (`V_TOTAL`, `MIN` e `MAX`) e recusa — falha fechada, sem seguir com o pad num estado incerto.
- Na devolução (Lei 5), escreve `MIN`/`MAX` de volta aos valores salvos do GOP, com a mesma conferência por readback.

Isso não é uma tentativa especulativa: é **replicar exatamente o que a própria AMD faz em toda troca de `V_TOTAL`**, independente de `MIN_SEL`/`MAX_SEL` estarem ligados ou não — se estiverem desligados, a escrita é inofensiva (o hardware ignora `MIN`/`MAX`); se estiverem ligados, é a peça que faltava.

### 3.3 Testes e auditoria

- Portão `audit_dcn19_vtotal_refresh_proof` estendido: exige 2 escritas em `V_TOTAL_MIN` e 2 em `V_TOTAL_MAX` (uma no caminho de falha fechada do `APLICADO`, outra na devolução final), cada uma devolvendo exatamente o valor salvo do GOP.
- **Mutação provada**: removi a linha de devolução de `MIN`/`MAX` de propósito — a auditoria reprovou com a mensagem nova (`DCN.19: a devolucao de MIN/MAX (fail-closed e final) tem de escrever exatamente o valor salvo do GOP`). Revertido, auditoria voltou a `PASS`.
- `build-contract.bat` completo: `DCN FLIP CONTRACT: OK`, `DCN EXT CONTRACT: OK`, `BC250 MCDM SAFETY AUDIT: PASS`.

## 4. Pacote: lab 3.429.1.0

Mesma base da 278/279 (D12 armado, D19 com a correção, D19b mantido). As 4 auditorias obrigatórias: todas **PASS**.

## 5. Critério da próxima corrida, escrito antes (Lei 10)

**A — `MIN_SEL`/`MAX_SEL` estavam ligados, e a correção resolve.** `[DCN2_VTOTAL_CONTROL] ANTES` mostra `MIN_SEL=1` ou `MAX_SEL=1`, e a tela fica limpa como a 275 (ou pelo menos volta a ser transitória, não o teste inteiro). → Confirma o mecanismo e fecha D19 de verdade.

**B — `MIN_SEL`/`MAX_SEL` estavam ligados, mas o sintoma persiste.** Os bits aparecem ligados, mas a tela continua com artefatos. → O mecanismo existe mas não é (ou não é só) a causa; precisa olhar outro campo do mesmo registrador (`FORCE_LOCK_ON_EVENT`, `VTOTAL_MID_REPLACING_*`).

**C — `MIN_SEL`/`MAX_SEL` já estavam desligados (0/0).** A correção não deveria ter efeito nenhum (escrever MIN/MAX com os bits de seleção desligados é inofensivo), e se a tela ficar limpa mesmo assim, foi coincidência/dispersão — se continuar com artefato, pelo menos a hipótese desta faixa está descartada com uma causa nomeada e podemos voltar a procurar em outro lugar, não mais adivinhando.

**D — Regressão.** Qualquer coisa que já funcionava (flip por IRQ, cursor, DPMS) parar de funcionar, ou BugCheck novo.

**E — Fora do mapa.** Qualquer coisa não prevista aqui: descrever antes de julgar (Lei 10).

## 6. Pendências

- **NÃO VALIDADO EM HARDWARE.**
- Se o desfecho A ou B confirmar `MIN_SEL`/`MAX_SEL` ligados, isso também é informação nova e relevante para qualquer trabalho futuro de VRR (D27) — o hardware já sinaliza que o modo de refresh dinâmico está presente/configurado desde o boot pela VBIOS.
