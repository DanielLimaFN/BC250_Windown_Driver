# 727 — Análise detalhada: artefatos persistentes em 3 corridas seguidas (276, 277, 278) — a suspeita recai sobre o próprio D19, não sobre D12

**Pedido do Daniel:** parar de rodar e analisar a fundo o padrão de artefatos que já apareceu três vezes seguidas (276, 277, 278), depois de D12 ter sido isolado e refutado na 277. Esta é uma análise, sem corrida nova.

## 1. Os quatro números lado a lado

| Corrida | D12 | D19 | Artefato visual relatado | Perda de VSYNC (início→meio→fim) | % no fim |
|---|:---:|:---:|---|---|---:|
| 275 | não | sim | só no bind, autorrecuperado | 57→57→57 (achatado) | ~1% |
| 276 | **sim** | sim | **teste inteiro** | 364→727→1.180 | ~12,0% |
| 277 | não | sim | **teste inteiro** | 56→414→818 | ~13,4% |
| 278 | **sim** | sim | **teste inteiro** | 57→410→834→1.288 | ~12,8% |

**O padrão que salta aos olhos:** D12 variou (sim/não/sim) nas três corridas ruins e o resultado não mudou nada — já estava refutado (doc 726). **D19 é a única coisa que roda em TODAS as quatro corridas**, inclusive na única que saiu limpa (275). Isso parecia, a princípio, afastar D19 da lista de suspeitos. Mas reparando melhor nos números: a 275 foi a **primeira vez na história deste projeto** que o V_TOTAL foi alterado com o monitor sincronizado nele. As três corridas seguintes (276, 277, 278) são a 2ª, 3ª e 4ª vez — e todas as três vieram malsucedidas, com magnitude muito parecida entre si (12-13,4%), bem diferente do ~1% da primeira vez.

**Isso não parece dispersão aleatória.** Dispersão aleatória produziria números espalhados; aqui há um salto claro entre a 1ª tentativa e as três seguintes, e as três seguintes se parecem muito entre si.

## 2. O que já foi eliminado com prova

- **D12 não é a causa**: a 277 rodou sem D12 (zero linha `[DCN2_AUXPAD]` no log) e teve o mesmo problema. Confirmado de novo agora: a 278 rodou com D12 e teve a devolução do pad **provada por readback** (`[DCN2_AUXPAD] DEVOLVIDO: DC_GPIO_DDC1_MASK de volta a 0x00000000 (readback OK)`) — a primeira vez que isso foi medido, não só inferido do código. D12 funciona exatamente como projetado, entra e sai rápido, e não é a causa.
- **D19b (a instrumentação) não tem mecanismo plausível**: reli meu próprio diff linha a linha. As únicas adições são (a) uma função pura de aritmética (`Bc250DcnElapsedMs`, zero registrador), (b) duas leituras de contador de tempo mais duas linhas de log DENTRO da janela que o próprio D19 já ocupava (~400 ms, que já existia desde a 275), e (c) uma linha de log extra no primeiro flip que **reaproveita uma variável já calculada**, sem nenhuma leitura de hardware nova. Nenhuma dessas mudanças tem como produzir um efeito que dura 170+ segundos depois de executada uma vez.
- **Não é travamento de GPU**: zero TDR, zero BugCheck, zero fault nas três corridas (276, 277, 278), confirmado por contagem exata no log e no `BC250_MONITOR.csv`. A GPU processa comandos o tempo todo.
- **A janela de tempo real do D19 é extremamente estável** nas três corridas ruins: ~559 ms (276), ~557 ms (277), **~570 ms (278)** entre o `DEVOLVIDO` e o primeiro flip — isso é dado novo desta análise. Essa estabilidade reforça que o comportamento é determinístico, não aleatório.

## 3. Hipótese principal (H, não provada): o próprio monitor acumula o efeito do V_TOTAL

A pergunta que decide isso **eu não tenho como responder só pelos logs**: **o monitor físico foi desligado (ou o cabo DisplayPort foi desconectado) entre as corridas, ou só o PC foi reiniciado?**

O roteiro que dei em todas as corridas diz "desligue a máquina (não reinicie) e ligue de novo" — isso garante um boot frio do **PC**, mas um monitor de mesa normalmente continua ligado (ou em standby) o tempo todo, independente de quantas vezes o PC é desligado. Se o monitor mantém algum estado interno (PLL de sincronismo, lógica de detecção de modo) que não é resetado por um boot frio do PC, é fisicamente plausível que:

1. A 1ª vez que o V_TOTAL foi alterado (275), o monitor tolerou bem e voltou ao normal rápido.
2. Da 2ª vez em diante (276, 277, 278), alguma coisa no receptor do monitor ficou num estado mais sensível/degradado, que só um desligamento do **monitor em si** (não do PC) resolveria.

Isso explicaria tudo que foi medido: por que D12 não importa (nunca mexeu em nada que afete isso), por que a 275 foi a única limpa, e por que as três seguintes são tão parecidas entre si (o monitor está "preso" num mesmo estado ruim, não sorteando um estado novo a cada corrida).

**Hipótese concorrente, mais fraca**: D19, mesmo sendo "seguro" em teoria (só altera o front porch, nunca a área ativa nem o sync), tem uma taxa de falha real e alta neste monitor específico, e a 275 foi a exceção, não a regra — sem precisar de efeito cumulativo, só de uma taxa de sucesso baixa.

Não dá para distinguir as duas só pelos dados que tenho. A primeira é testável sem gastar uma corrida de verdade (é só um procedimento físico diferente antes da próxima); a segunda só se resolveria rodando D19 várias vezes mais, o que não vale o custo agora.

**Confirmado pelo Daniel (13/10/2026): só o PC foi reiniciado entre as corridas 275-278; o monitor nunca foi desligado.** Isso satisfaz a condição necessária da hipótese principal.

**Teste físico feito pelo Daniel, mesmo dia: desligou o monitor na tecla dele — voltou limpo. Tirou e recolocou o cabo DisplayPort — voltou normal também.** As duas ações, independentes, resolveram o estado visual. Isso **confirma o mecanismo da hipótese principal**: o estado ruim não está em nada que o driver ou o Windows guardam (um boot frio do PC nunca resolvia), está no próprio receptor do monitor (ou na negociação do link ao reconectar), e sai sozinho com qualquer uma das duas ações físicas. Continua sendo H quanto a "foi o D19 que colocou o monitor nesse estado" (não foi isolado por uma corrida controlada dedicada a isso), mas a parte "o PC não causa, o monitor guarda, e um reset do lado do monitor resolve" passa de H para medição direta.

**Consequência prática, independente de mais investigação:** se D19 (ou qualquer escrita de temporização do OTG) for usado de novo, a recuperação de um eventual estado ruim de tela **não depende de reiniciar o PC** — já temos duas ações físicas simples e confirmadas que resolvem: desligar o monitor, ou desconectar/reconectar o cabo DisplayPort.

## 4. Recomendação

**Parar de rodar D19 por enquanto**, como você pediu. Antes de testar de novo:

1. **Teste físico, sem gastar uma corrida de driver**: desligue o monitor na própria tecla dele (ou desconecte e reconecte o cabo DisplayPort), não só o PC, espere uns segundos, e then religue tudo e veja se o desktop normal do Windows (sem nosso driver, só o GPU padrão) aparece limpo. Isso não exige rodar nada deste projeto — é só confirmar que o monitor "zera" sozinho.
2. Se isso resolver visualmente o estado do monitor, a próxima corrida com D19 pode vir logo depois de um desligamento físico do monitor (não só do PC), para testar se volta a ficar limpa como a 275.
3. Se quiser seguir sem gastar mais tempo nisso: **deixar D19 desarmado** nas próximas corridas (ele já cumpriu o papel de provar que o registrador funciona, Lei 3) e focar em outras frentes (D06, ou aguardar uma via nova para D18) até decidir se vale a pena voltar a essa investigação.

## 5. O que não mudou

- Nenhum código novo foi escrito nesta análise — é só leitura e comparação dos logs já coletados.
- `BASE_DE_AVANCO.md` será atualizado para registrar esta suspeita revisada (D19, não D12) e a pergunta pendente sobre o monitor.
