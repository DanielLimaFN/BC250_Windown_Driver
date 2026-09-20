# 302A — Emenda ao critério da Corrida 44

> **Data da emenda:** 20/09/2026.
> **Estado no momento da emenda:** Corrida 44 **NÃO EXECUTADA**; candidato lab `3.202.1.0`
> assinado, auditado e **NÃO INSTALADO**; fusível `TRIPPED`; `MEMORY.DMP` ainda é o da
> Corrida 43.
> **Emenda escrita ANTES do ensaio.** Não há resultado físico da Corrida 44 nesta data, logo
> esta emenda não pode ter sido influenciada por ele. Essa é a única razão pela qual mexer no
> critério é legítimo aqui.

O `docs/302` continua válido e **não foi alterado**: ele é o registro do que foi escrito
primeiro. Esta emenda substitui a seção "Sucesso obrigatório / Falha informativa" dele e o
§11 do `docs/303`. Onde houver divergência, **vale este documento**.

## 1. Por que emendar

Três defeitos foram encontrados no critério por auditoria offline, antes do ensaio:

1. **O 302 e o §11 do 303 já discordavam entre si.** O 302 lista 6 itens de sucesso
   obrigatório; o 303 lista 7, tendo acrescentado `[TDR_GFX_RESTART_BASE] Pass=1`. Dois
   critérios "congelados" em disco, discordando, é exatamente o arranjo que permite escolher
   o mais conveniente depois de ver o log.

2. **Faltava o desfecho `E — algo fora do mapa`,** exigido pela Lei 10, regra 2. A regra nasceu
   da Corrida 39, em que o maior resultado — 118 submissões gráficas sem hang — não estava em
   nenhuma das quatro letras previstas e por isso quase foi tratado como ruído.

3. **O "sucesso obrigatório" exigia coisa que o candidato não promete corrigir.** Os itens
   "submissão GFX posterior aposentada" e "nenhum segundo TDR" dependem do stall inicial em
   `RPTR=24/WPTR=32`, que o próprio 303 §5 declara dívida **separada** e o §13 lista como
   aberta. Do jeito que estava, H44 podia ser confirmada por inteiro e a corrida ainda ser
   graduada FALHA — e, pior, a falha seria lida como falha da correção de BASE, que não é.

## 2. O que a Corrida 44 mede, e o que ela não mede

**Mede:** se o CP_RB0, após soft-reset, com `BASE = GPUVA >> 8`, consegue buscar e executar o
pacote privado de 12 dwords (`NOP x4 + RELEASE_MEM`) e substituir o poison da fence.

**Não mede:** se o IB real do Node 1 executa. A receita PM4 que produziu o stall inicial
**não foi tocada** nesta faixa. Qualquer conclusão sobre ela nesta corrida é H, não P0.

## 3. Sucesso da faixa — os cinco itens que fecham H44

Todos precisam ocorrer, e todos estão sob controle da correção aplicada:

1. `[TDR_GFX_RESTART_BASE] ... Pass=1`, com `Esperado` idêntico ao `Readback`;
2. `[TDR_FENCE_RECONCILE] PASS` com `NotificadasComoConcluidas=0`;
3. `[TDR_GFX_RESTART_PROOF] PASS`, `RPTR=WPTR=12`, poison substituído pela fence esperada;
4. `[TDR_GFX_RESTART] CP_RB0 rearmado e comprovado por execucao fisica`;
5. o callback de restart retorna sucesso.

Os cinco juntos = **H44 confirmada**. Isso é o veredito da faixa, e é o que se anuncia.

## 4. Observação dependente de dívida aberta — fora do sucesso obrigatório

Os dois itens abaixo são **registrados e relatados**, e **não** rebaixam o veredito da faixa:

- **O6** — uma submissão GFX posterior ao restart é aceita **e aposentada** com fence real;
- **O7** — não ocorre segundo TDR equivalente nem BugCheck.

Leitura correta de cada combinação:

| Itens 1–5 | O6 / O7 | Veredito da faixa | O que isso significa |
|---|---|---|---|
| todos | ambos | **SUCESSO + avanço além do previsto** | restart recuperado e o stall inicial não reaparece — registrar como achado, investigar por quê |
| todos | falham | **SUCESSO da faixa** | a recuperação recupera; o stall inicial permanece dívida aberta e vira a próxima faixa |
| falta ≥1 | qualquer | **FALHA da faixa** | H44 não fecha; ver §5 |

O7 falhar é o desfecho **esperado**, não o surpreendente: se o restart devolver o adaptador e o
dxgkrnl reenviar o mesmo IB que já travava, um segundo TDR é a consequência natural da dívida
que esta faixa não tocou.

## 5. Falha informativa — assinatura e implicação em campos separados

Conforme Lei 10, regra 1, o que o log mostra e o que se conclui dele são campos distintos.

| # | Assinatura no log | Implicação |
|---|---|---|
| A | `[TDR_GFX_RESTART_BASE] Pass=0` | A escrita não foi retida, ou o domínio de leitura do registrador está errado. H44 não chega a ser testada. Próximo alvo: retenção/serialização da escrita, não a aritmética — ela está provada offline por teste. |
| B | `Pass=1` e prova com `RPTR=0` | H44 **refutada como causa suficiente**: a BASE está certa e o CP ainda não busca. Próximo alvo: a sequência pós-soft-reset comparada à inicialização ONLINE, sem acrescentar escrita não catalogada. |
| C | `Pass=1` e `RPTR` avança mas `< 12` | A BASE funcionou; outro estado ou outro pacote bloqueia o CP no meio do stream. Usar header dump, CP busy e IB restante para localizar o ponto. |
| D | Prova passa e a submissão seguinte trava | O restart privado funciona; a restauração do contexto real continua incompleta. Não é falha de BASE. |
| **E** | **Qualquer coisa fora de A–D** | **Descrever o observado ANTES de julgá-lo.** Anotar assinatura literal, horário, pacote e estado dos registradores, e só então classificar. Inclui BugCheck de código diferente de `0x116`, `GFXHUB_FAULT`, hang sem TDR, ausência total das linhas esperadas, e **qualquer resultado melhor que o previsto**. Não forçar em A–D. |

Em nenhum caso ampliar o soft-reset por tentativa. Preservar dump, cauda do KMD e readbacks
antes de editar qualquer linha.

## 6. Desfecho parcial não é desfecho

Lei 10, regra 3. Se um desfecho exige três sinais e aparecem dois, ele **não** ocorreu — em
especial quando o sinal ausente é o que o define. Não existe "A parcial" nem "sucesso parcial
da faixa": ou os cinco itens do §3 ocorrem, ou H44 não fechou.

## 7. Falha de contrato — inalterada

- declarar sucesso sem `[TDR_GFX_RESTART_PROOF] PASS`;
- notificar fence abortada como concluída;
- reabrir admissão depois de prova falha;
- perder a cauda do KMD;
- tratar BasicDisplay ou reboot como recuperação do **nosso** adaptador;
- **alterar este critério depois de ver o resultado da Corrida 44.**
