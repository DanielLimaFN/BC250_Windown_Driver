# 244 — Auditoria independente do `docs/243` (Marco 2, `0x10E` Arg1=0x4)

> **Escopo:** conferir cada afirmação do dossiê 243 contra `C:\BC250_DISPLAY_LOG.txt`
> (54.513.106 bytes; janela do bind: linhas 614.100–615.295), a pasta
> `docs/evidence/2026-09-11_10-35-34-DESKTOP-HQAG6T4-lab-binding/`, o código da árvore de trabalho
> e a documentação Microsoft.
> **Veredito:** §7.

---

## 1. O que está CONFIRMADO (P0/P1)

| # | Afirmação do 243 | Conferência |
|---|---|---|
| 1 | BugCheck `0x10E` Arg1=`0x4` = *"A rotation in a must-succeed path failed"* | ✔ **P1** (tabela Microsoft de Parameter 1 do `0x10E`) |
| 2 | `MEMORY.DMP` 821.799.548 bytes às 10:36:49 | ✔ **P0** (arquivo existe, mesmo tamanho/data) |
| 3 | Fusível disparado `State=2, Boots=12, LastStatus=c0000001` | ✔ **P0** (log 615294) |
| 4 | Submissão Node 1 publicada neste bind (192 bytes, `VA=0x200AC4000`, `RPTR=12 WPTR=12`, fence 18) | ✔ **P0** (log 615163–615195; descritor `C0023F00,00AC4000,00000002,09800030` = `PKT3_INDIRECT_BUFFER`, 48 dwords, `VALID`, VMID 9) |
| 5 | Zero linhas `[SdmaRetire]`, `IB EXECUTADO`, `IB_TRIPOD TIMEOUT` na janela do bind | ✔ **P0** (medido) |
| 6 | O código descrito em §4 existe: guarda `SdmaRetireBusy` (`bc250_active_miniport.c:6658`), DPC zerando `SdmaRetirementTimerRunning` (`:6774`), re-arm one-shot (`:6699-6700`) | ✔ **P1** (leitura da árvore) |
| 7 | Esse código estava **no binário que caiu** | ✔ **P0**: `output_lab\bc250mcdm.sys` de 11/09 09:15:37 contém a string `BusyYields` (contador da guarda), e o manifesto registra `BuiltAtUtc=2026-09-11T12:15:38Z` |
| 8 | Nome do adaptador `"AMD BC-250 Graphics"` | ✔ plausível e explicado: o INF declara `DeviceName="AMD BC-250 Graphics (Cyan Skillfish 2)"` (linha 110). **Isso resolve a ressalva #4 da auditoria 242** |
| 9 | O SDMA estava vivo | ✔ **P0**: `[Present #1/#2/#4] Blt 11059200 bytes (src 0xAB0000 -> scanout 0x0): SDMA=YES` |

---

## 2. O que está ERRADO no dossiê

| # | Afirmação | Medição / fonte |
|---|---|---|
| E1 | "Pacote `3.133.1.0`" | O manifesto registra **`DriverVer 09/10/2026,3.134.1.0`**, `BuiltAtUtc 2026-09-11T12:15:38Z`, `SourceSysSha256 A5C7D2EB…`. O pacote da corrida é **3.134.1.0**, construído às 09:15 de hoje |
| E2 | "Arg3 = `0xA`: código interno do VidMm indicando timeout de sincronização no motor de paginação" | **Invenção.** A Microsoft documenta **apenas o Parameter 1** do `0x10E`; não há tabela pública de Arg2/Arg3/Arg4. (Aliás, `0xA` na tabela de **Arg1** é "an invalid attempt to use a pinned allocation" — outra coisa.) Declarar como H, nunca como causa |
| E3 | "14 submissões: `FenceId=1,2,3,4,5,8,10,11,12,18,19,20,21,22`" | As 14 medidas são **`1,2,8,10,12,13,14,16,17,18,19,20,21,22`** (log). A lista do dossiê não confere |
| E4 | "O harness … **despachou compute no Node 1 (MEC)**" | **Falso para este processo.** `harness-stage-journal.txt` (pid 11056) termina em `after vkEnumeratePhysicalDevices` — sem `vkCreateDevice`, sem dispatch, sem present. A submissão Node 1 veio de **outro processo** (gate de pré-requisito), não do harness de present |
| E5 | Implícito: "Marco 1 revalidado nesta corrida" | **Não.** Não há `harness.stdout.txt`, nem `probe-after.json`, nem JSON de resultado: **não há byte proof nesta corrida**. O único vestígio do Node 1 é a publicação no anel (`RPTR=WPTR=12`) — a mesma perna que a auditoria 242 já classificou como insuficiente sozinha |

---

## 3. O furo central do dossiê: "zero aposentadorias" não está provado

O 243 usa "linhas `[SdmaRetire]` = ZERO" como prova de que **nenhuma** submissão foi aposentada.
Isso não vale como prova:

- `Bc250DisplayLog` só grava em disco em `PASSIVE_LEVEL` (`bc250_display_ddi.c:575`). Toda
  aposentadoria feita dentro da **DPC** (DISPATCH_LEVEL) é invisível no arquivo — mesmo quando
  funciona perfeitamente.
- **Medição de controle:** no bind das **09:18** (Marco 1, que terminou com `StopDevice` limpo),
  houve **35 submissões despachadas e também ZERO** linhas de aposentadoria. Ou seja, "zero linhas"
  é o **comportamento normal deste build**, não um sinal de falha.

Consequência: **a única coisa provada é o `0x10E`**. Que as 14 fences nunca foram notificadas é
**inferência**, e a telemetria que responderia (`SdmaRetirementNotifiedTotal`,
`SdmaRetirementBusyYields`) morreu com o BugCheck (nunca houve `StopDevice`, e não há `probe-after`).

---

## 4. Sobre a causa-raiz proposta (§4): plausível, com um passo que não fecha

O mecanismo é **compatível** com o código (guarda + DPC que desarma o timer de 1 ms sem rearmar
quando perde o CAS). Mas o passo 7 do dossiê está incompleto: se a cabeça da fila ainda está
pendente, o próprio caminho de submit **rearma** o timer (`:6699-6700`). Para a cadeia morrer é
preciso que o dono da guarda saia por um caminho que **não** rearma — por exemplo o ramo de
**timeout** (`break` sem re-arm, `:6692-6695`) ou uma fila momentaneamente vazia — **ou** que a DPC
perca o CAS quando **nenhuma** thread de submit está rodando (perda de *wakeup* pura).

Hipóteses alternativas igualmente compatíveis (não testadas):
- **H2:** as 14 submissões simplesmente nunca completaram (anel parado / fence não escrita) — o
  `RPTR/WPTR` do SDMA no bind (`Sdma0RbRptr=Wptr=0x44`) é estado de boot anterior e não prova nada
  sobre o momento da queda;
- **H3:** a notificação em lote (`:6711`) com `highestSubmissionFenceId` somada à marca d'água
  monotônica nova (`:6252-6263`) suprimiu notificações — o contador
  `SdmaRetirementFencesSkippedWatermark` existe e **não foi medido**;
- **H4:** falha do lado do VidMm em `BuildPagingBuffer` — a janela do bind registra **nenhuma**
  chamada de `BuildPagingBuffer`, o que é no mínimo estranho numa queda por rotação.

**Veredito parcial:** a §4 é uma boa hipótese de trabalho (H), **não** uma causa-raiz demonstrada.

---

## 5. Respostas às três perguntas do dossiê

**Q1 — polling síncrono para paginação?** Aceitável **só como mitigação temporária e sob três
condições**: (a) teto em escala de **microssegundos** (algumas centenas de polls com flush de HDP),
nunca milissegundos; (b) **restrito ao contexto de paginação**; (c) **fail-closed** no estouro —
nunca "assumir sucesso". Alerta medido: espera síncrona em `DISPATCH_LEVEL` foi exatamente o que
produziu o `0x133` da corrida 12:16 (barra de status). O conserto canônico é o da Q2.

**Q2 — padrão canônico de liveness?** Sim, e é um invariante único: **nunca deixar trabalho pendente
sem um wakeup agendado**. Implementar uma única função
`Bc250SdmaRetirementEnsureScheduled()` chamada em **toda** saída de
`Bc250ActiveSdmaRetireAndNotify` (bail-out da guarda, `break` de timeout, fila vazia, após notificar)
e na DPC: se `InFlightCount > 0`, rearmar o timer one-shot **ou** reenfileirar a DPC. Com isso,
quem quer que perca o CAS, a cadeia sobrevive — e a classe inteira do §4 deixa de existir,
independentemente de qual hipótese (H1–H4) seja a verdadeira. A DPC não deve zerar
`SdmaRetirementTimerRunning` **antes** de saber que vai reagendar.

**Q3 — o caminho para o Marco 2 está desimpedido?** **Não.** Mesmo com a fila SDMA viva, o próprio
log desta corrida mostra `BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)` e
`AVISO: Superficie de origem 0xAB0000 esta com todos os pixels em zero (NonZero=0/256)` nos Presents
#1, #2 e #4: **a superfície de origem está vazia**. Falta um produtor de pixel. Consertar paginação
não acende a tela — o Marco 2 depende de conteúdo desenhado na origem (rota B DXBC→SPIR-V→RADV/ACO
ou, no laboratório, do blt GPU declarado como não-rasterização).

---

## 6. Experimento discriminador barato (antes de qualquer reescrita grande)

1. Expor `SdmaRetirementBusyYields`, `SdmaRetirementNotifiedTotal` e
   `SdmaRetirementFencesSkippedWatermark` como **campos de probe lidos a cada bind** (hoje só saem
   no `StopDevice`, que um BugCheck destrói).
2. Rodar uma corrida curta só de Node 0 (sem harness Vulkan) com o build atual:
   - se `BusyYields > 0` e `NotifiedTotal == 0` → **hipótese do 243 confirmada**;
   - se `BusyYields == 0` e `NotifiedTotal == 0` → **refutada**; a causa é H2/H3/H4.
3. Só então escolher entre "consertar liveness" e "consertar o completamento".
4. Evidência: enquanto o log só gravar em `PASSIVE_LEVEL`, **a DPC continua invisível**. Acrescentar
   um buffer circular em memória (fence, nó, IRQL, thread) despejado no `StopDevice`/probe —
   mesma recomendação da auditoria da corrida 18:34.

---

## 7. Veredito

> **O incidente é real e está bem documentado (`0x10E` Arg1=0x4, dump, fusível). O diagnóstico do
> 243 é a melhor hipótese disponível, mas é H: (a) a "prova" de zero aposentadorias é inválida
> (o log perde tudo que acontece em DPC — o bind das 09:18 tem o mesmo "zero" e terminou limpo);
> (b) a lista de FenceIds e a versão do pacote estão erradas; (c) a leitura de Arg3 é inventada;
> (d) o harness de present **não** despachou compute — ele parou na enumeração, portanto **esta
> corrida não revalida o Marco 1**.**
>
> **O que fazer:** implementar o invariante de liveness da Q2 (uma função, todos os caminhos de
> saída) + telemetria dos três contadores por bind. Isso resolve a classe inteira do §4 sem
> dependência de qual hipótese seja a verdadeira, e transforma a próxima corrida em experimento
> discriminador.
>
> **Não afirmar**: "o caminho para o Marco 2 está desimpedido". A superfície de origem dos Presents
> continua zerada (`NonZero=0/256`); falta produtor de pixel, não falta fila.
>
> **Situação da máquina:** fusível disparado (`State=2`, `Boots=12`) — reset + boot frio antes de
> qualquer novo bind. Nada disto está validado em hardware: **`NÃO VALIDADO EM HARDWARE`**.
