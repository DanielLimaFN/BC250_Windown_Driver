# 725 — Corrida 276: D19 confirmado de novo, D19b mediu o intervalo real, e a tela ficou com artefatos o teste inteiro (pior que a 275) — D12 como suspeito principal

**Data/hora:** 13/10/2026, 09:03:39–09:07:41. **Candidato:** lab `3.426.1.0` (`DcnFlip=1`, `DcnExt=509`, `DcnExtAuxHwModeArmed=true`, `DcnExtVTotalRefreshTestArmed=true`). **Evidência:** `docs/evidence/2026-10-13_09-03-39-DESKTOP-HQAG6T4-lab-binding/`. Continuação dos docs [723](723_CORRIDA275_D19_MEDIDO_TELA_COM_ARTEFATOS_NO_BIND_SEM_TRAVA.md) e [724](724_ANALISE_DOCS_ANTIGRAVITY_711_712_713_E_PACOTE_3426_PARA_CORRIDA_276.md).

## 1. Houve avanço?

**Sim no que D19 mediu de novo; não no sintoma visual — piorou em relação à 275.** O operador relatou: **tela preta cheia de artefatos durante o teste inteiro** (180 s), não só no bind como na 275. A recuperação também precisou de reboot forçado (como na 274), diferente da recuperação limpa da 275.

## 2. Sucessos

- **D19 confirmado de novo, e com batimento ainda melhor que a 275**: `APLICADO` (1111→1333, pedido 50,000 Hz) → `MEDIDO` 50,007 Hz → `DEVOLVIDO` 59,999 Hz com `V_TOTAL`=1111 (original medido no boot: 60,000 Hz) — desta vez a devolução bateu quase exatamente com o original (Δ0,001 Hz, contra Δ0,27 Hz na 275), reforçando que a pequena discrepância da 275 era ruído de medição, não desvio real.
- **D19b (instrumentação nova) funcionou e respondeu à pergunta que a 275 deixou aberta**: o log agora grava `qpc_abs` no `DEVOLVIDO` do D19 (`1165635130`, freq `10000000`) e no primeiro `[DCN1_LATCH] #1` (`1171221744`, mesma freq). Diferença real: **(1171221744 − 1165635130) / 10000000 ≈ 0,559 s** — o primeiro flip do HUBP0 latchou **~559 ms depois** de o V_TOTAL já ter voltado ao valor do GOP. Isso é dado novo, não existia na 275.
- **Zero TDR, zero BugCheck, zero GFXHUB_FAULT, zero DEVICE_REMOVED** em toda a corrida (conferido por contagem no log de ~96 MB) — o problema não é um travamento de hardware.
- **D05 (flip por interrupção) continuou funcionando**: 34 de 35 flips latchados via IRQ aos 164 s.
- **GPU saudável o tempo todo**: `BC250_MONITOR.csv` mostra `tdr`/`fault` em zero em toda a janela, Tctl estável 75-80 °C, `ib_s`/`fills` crescendo — a GPU processou comandos o tempo todo, mesmo com a tela ruim.
- **D12 escreveu e leu de volta corretamente no único ponto medido**: `[DCN2_AUXPAD] ADQUIRIDO: DC_GPIO_DDC1_MASK 0x00000000 -> 0x00010000 (readback OK)`, exatamente a prova esperada (Lei 6).

## 3. Falhas

- **O sintoma visual piorou**: na 275 os artefatos apareceram no bind e **se resolveram sozinhos**; na 276 duraram **o teste inteiro**, com o cursor do mouse ainda respondendo (relato do operador em mensagens anteriores, mesmo padrão).
- **Achado P0 que correlaciona com o sintoma**: a perda de notificação de VSYNC, que na 275 **parava em 57 e ficava achatada** pelo resto dos 156 s (0% de perda nova depois do primeiro segundo), na 276 **cresceu continuamente o tempo todo**: 364 perdidos aos 44,8 s → 727 aos 98,9 s → 1.180 aos 164,3 s — uma perda sustentada de ~12% das notificações de VSYNC do começo ao fim, não um problema que se resolve. Essa é a diferença medida mais limpa entre as duas corridas.
- **Defeito meu encontrado e corrigido nesta análise**: `Bc250Dcn2AuxPadRelease` (a devolução do pad ao modo do GOP depois do D12) **escrevia o registrador mas não conferia por readback nem logava nada** — violação do próprio padrão que o projeto já usa em toda outra devolução (D19, cursor, blank). Pelo código, a devolução deveria ter rodado (o caminho que falha a leitura do DPCD chama `Bc250Dcn2AuxPadRelease` antes de retornar), mas isso era **P1 (lido no fonte), nunca P0 (medido)** — não havia como provar, só pelo log, que o pad realmente voltou. Corrigido: agora confere por readback e sempre loga `[DCN2_AUXPAD] DEVOLVIDO: ...` ou um aviso se não confirmar.
- **A pendência da 274 (recovery incompleta na 1ª tentativa) voltou**: `safe-bind-watchdog-journal.jsonl` mostra `BLOCKED` → `RECOVERY_REBOOT_REQUESTED` na tentativa 1, reboot automático, e só na tentativa 2 (depois do boot) `RECOVERY_OK_ONLINE`. O diagnóstico novo do `BC250_RECOVERY.cmd` (adicionado para a 274/275) não ajudou desta vez: ele grava em `C:\BC250_RECOVERY.log`, que a 2ª tentativa **sobrescreveu** antes de a evidência ser arquivada — o conteúdo da 1ª tentativa com o diagnóstico se perdeu. Defeito de coleta, não de driver; registrado para corrigir (arquivar o log a cada tentativa, não só no fim).
- **T3 não foi exercitada**: o `BC250_PROCESSO_NOVO.log` capturado é resíduo da Corrida 274 (timestamps de 12/10), não desta corrida.

## 4. Veredito

**Parcial, com regressão de sintoma visual não explicada por hardware.** D19 fecha de novo por medição (critério A do doc 722, repetido). D19b respondeu exatamente ao que foi desenhado para responder (tempo real entre o V_TOTAL voltar e o primeiro flip: ~559 ms — nem os dois eventos são simultâneos, nem a janela de ~400 ms do D19 por si só explica sozinha um sintoma que durou os 180 s inteiros). O sintoma em si (artefatos pelo teste inteiro) é **pior que a 275** e **não tem explicação por P0** nos dados disponíveis — nenhum TDR, fault ou BugCheck acompanha. **H principal (não provada): D12** é a única escrita de registrador nova nesta corrida em relação à 275 (D19b é só log); a perda sustentada de VSYNC começa a ser medida logo depois da sequência de GPIO/AUX do D12 e nunca se resolve, diferente da 275. **H concorrente, mais fraca**: a combinação D12+D19 tão próximas no tempo (duas mexidas de temporização/pad em menos de 1 segundo) pode ter sido mais perturbadora que D19 isolado.

## 5. Recuperação e pendências

- **Driver ativo ao final**: não — watchdog disparou aos ~180 s como programado.
- **Recuperação**: precisou de reboot automático (tentativa 1 falhou com "ainda existe depois da exclusão forçada", igual à 274); tentativa 2 fechou limpa, BasicDisplay Código 0 confirmado.
- **NÃO VALIDADO EM HARDWARE**: a causa do sintoma visual. D12 é suspeito principal por eliminação (única escrita nova), não por mecanismo comprovado.
- **Corrigido offline (zero risco, só instrumentação)**: `Bc250Dcn2AuxPadRelease` agora confere por readback e loga sempre — a próxima vez que D12 rodar, teremos prova P0 de que o pad voltou (ou não).
- **Próximo passo recomendado**: isolar a variável. Corrida 277 = exatamente a receita da 275 (D19 + D19b) **com D12 desarmado de novo**, para confirmar se os artefatos voltam a ser transitórios (resolveria a favor de D12 como causa) ou persistem por todo o teste mesmo sem D12 (refutaria essa hipótese e abriria outra, talvez a proximidade temporal entre duas mudanças de temporização/pad).

## 6. Tabela-checklist da corrida 276

| Item | Resultado | Prova |
|---|:---:|---|
| D19 aplicado/medido/devolvido antes do desktop | ✅ | linhas 157-161, antes de `StartDevice LEAVE` |
| D19 devolve o Hz ao original | ✅ | 59,999 Hz medido vs 60,000 Hz original (Δ0,001 Hz) — melhor que a 275 |
| D19b: tempo real decorrido e qpc_abs logados | ✅ | `[DCN2_VTOTAL] DEVOLVIDO ... qpc_abs=1165635130`, `[DCN1_LATCH] #1 qpc_abs=1171221744` |
| D12 adquire e escreve o pad corretamente | ✅ | `[DCN2_AUXPAD] ADQUIRIDO` com readback OK |
| D12 lê EDID real | ❌ | AUX0/AUX1 sem resposta no DPCD, igual a todas as corridas anteriores |
| D12 devolve o pad ao GOP (provado) | ⬜ | sem log na 276 (defeito meu); corrigido offline para a 277 |
| Zero TDR/BugCheck/fault | ✅ | contagem exata no log e no CSV: 0/0/0 |
| D05 (flip por interrupção) continua funcionando | ✅ | 34/35 via IRQ aos 164 s |
| Tela sem artefatos durante o teste | ❌ | relato do operador: artefatos o teste inteiro (pior que a 275) |
| VSYNC sem perda crescente | ❌ | 364→727→1.180 perdidos ao longo do teste (contra achatado em 57 na 275) |
| Recovery sem reboot forçado | ❌ | tentativa 1 falhou, reboot automático, tentativa 2 OK |
| T3 exercitada | ⬜ | log residual da 274, não desta corrida |

## 7. Tabela viva do DCN — deltas desta corrida

Ver atualização em [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) (seção P9). Resumo:
- **D12**: volta de 🧪 para ❌ — a leitura real do EDID continua falhando, e a escrita nova é suspeita principal (não provada) de um sintoma visual pior. Desarmado de novo até isolar.
- **D19**: mantém 🟡, com segunda medição reforçando a confiança no registrador; a pendência agora é entender o sintoma visual, não o V_TOTAL em si.
- **D32** (retorno ao GOP / recovery): a pendência da 274 voltou na 276; segue 🟡.
