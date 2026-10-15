# 731 — D19 Caminho B implementado: apaga antes, escreve V_TOTAL, acende depois (reaproveitando o D08)

**Data:** 13/10/2026, offline (sem corrida). Continuação do [doc 730](730_ANALISE_PROFUNDA_D19_ESTAVA_ERRADO_DESDE_O_INICIO_REAL_DRR_DA_AMD_E_PONTO_A_PONTO_DO_RESTO_DO_DCN.md). Pedido do Daniel: implementar o Caminho B.

## 1. O que foi implementado

A AMD real (`dcn20_hwseq.c`) só escreve `OTG_V_TOTAL` com a tela apagada (`blank_pixel_data` antes de `program_timing`). `Bc250Dcn2VTotalProve` agora faz o mesmo, usando o **mesmo registrador do D08** (`OTG0_BLANK_CONTROL`), mas com salvamento/devolução **local** (sem tocar o estado estático do DPMS, para não interferir se o DPMS for acionado por outro caminho ao mesmo tempo):

1. **`Bc250Dcn2VTotalBlank`**: lê e salva `OTG0_BLANK_CONTROL`, liga `OTG_BLANK_DATA_EN`, e **espera até 20 ms**, conferindo `OTG_CURRENT_BLANK_STATE` (o bit de confirmação real do hardware, não só o pedido) antes de prosseguir. **Falha fechada**: se não confirmar em 20 ms, devolve o blank ao valor original e recusa — `V_TOTAL` nunca é tocado.
2. A sequência de `V_TOTAL`/`MIN`/`MAX` (igual à faixa anterior) só roda depois do apagamento confirmado.
3. **`Bc250Dcn2VTotalUnblank`**: só é chamada depois que `V_TOTAL`/`MIN`/`MAX` já foram devolvidos ao valor do GOP **e** o refresh já foi medido de volta ao original — ou seja, a tela nunca é religada mostrando um estado de transição. Também confirma por `OTG_CURRENT_BLANK_STATE`, com aviso (não aborto, pois não há mais para onde devolver) se não confirmar.
4. **Toda saída da função depois do apagamento confirmado acende de volta** — inclusive os dois caminhos de falha fechada (readback de `V_TOTAL` falhou, readback de `MIN`/`MAX` falhou) — para nunca deixar a tela presa apagada.

## 2. O que isso muda na experiência visível

Ao contrário da promessa original do D19 ("troca sem ninguém notar"), **agora vai aparecer um flash de tela preta breve e intencional** durante a troca — o mesmo tipo de blackout que o próprio D08 (DPMS) já produz e que o Daniel já confirmou funcionar (272/273). A troca de "imperceptível" para "flash controlado e seguro" é a consequência direta de seguir o único caminho que a AMD de fato usa para isto.

## 3. Testes e auditoria

- Dois portões novos em `audit_dcn19_vtotal_refresh_proof`: o apagamento tem de vir **antes** da primeira escrita em `V_TOTAL`; toda saída depois do apagamento confirmado tem de acender de volta (3 ocorrências: readback de `V_TOTAL`, readback de `MIN`/`MAX`, caminho final); `Bc250Dcn2VTotalBlank`/`Unblank` têm de conferir por `OTG_CURRENT_BLANK_STATE`.
- **Duas mutações provadas**: (a) removido o aborto por falha de confirmação do apagamento — auditoria reprovou; (b) removido o aceso final — auditoria reprovou com a mensagem certa. As duas revertidas, auditoria voltou a `PASS`.
- `build-contract.bat` completo: `DCN FLIP CONTRACT: OK`, `DCN EXT CONTRACT: OK`, `BC250 MCDM SAFETY AUDIT: PASS`.

## 4. Pacote: lab 3.431.1.0

`DcnFlip=1`, `DcnExt=381` (0x17D — D19 armado via `-ArmVTotalRefreshTest`; **D12 deliberadamente fora** desta corrida, para isolar só a mudança do Caminho B sem misturar variáveis — D12 já está provado inocente e pode voltar depois). As 4 auditorias obrigatórias: todas **PASS**.

## 5. Critério da próxima corrida (Lei 10)

**A — Limpo.** A tela mostra um flash preto breve e controlado durante o bind (esperado, não é defeito) e depois fica normal pelo resto do teste, sem artefatos. → Caminho B resolve; D19 fica provado com a ressalva do flash visível.

**B — Artefato mesmo assim.** O flash acontece, mas depois do "aceso" a tela continua com artefatos como em 276-280. → O apagamento sozinho não basta; sobra investigar se o próprio `OTG_BLANK_CONTROL` tem a mesma falta de confirmação em outro nível, ou se o problema nunca foi de timing de escrita.

**C — Sem flash, sem artefato.** Se o apagamento não chegar a ser visualmente perceptível (hardware rápido o suficiente) e a tela ficar limpa. → Melhor dos mundos; registrar como achado.

**D — Recusa (`[DCN2_VTOTAL_BLANK] RECUSADO`).** O apagamento não confirmou em 20 ms; `V_TOTAL` nunca foi tocado (falha fechada). → Informa que `OTG_CURRENT_BLANK_STATE` não se comporta como esperado neste chip; D19 fica sem medir, mas sem risco.

**E — Fora do mapa.**

## 6. Pendências

- **NÃO VALIDADO EM HARDWARE.**
- D12 fica fora desta corrida de propósito (isolamento); pode voltar numa corrida seguinte se a 282 confirmar o Caminho B.
