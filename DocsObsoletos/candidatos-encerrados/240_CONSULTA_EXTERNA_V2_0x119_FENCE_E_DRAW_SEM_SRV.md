# 240 — Consulta Técnica Externa (V2): Corrida 18:34, `0x119` Arg1=0x1 e o protocolo de notificação de fence

> **Destinatários:** Claude, DeepSeek, Sparke e revisores de arquitetura WDDM.
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64 (19045).
> **Pacote:** candidato de laboratório `3.133.1.0` (INF fonte `3.133.0.0`, `BuildKind=MCDM_LAB_BRINGUP`).
> **Base:** medição direta no silício, corrida de 10/09/2026 18:34:36 → 18:37:51, mais leitura de código da árvore de trabalho.
> **Este documento SUBSTITUI o `docs/239` para envio externo.** A §0 é uma errata medida do 239 — por favor leiam antes de responder.

---

## 0. Errata do documento anterior (o que o 239 afirmou e a medição não sustenta)

Revisamos o 239 contra o log vivo e contra o código. Cinco afirmações mudaram de nível de confiança.
**Nada aqui invalida a hipótese central do 239 — a corrida condicional na notificação —, mas três números estavam inflados e a causa-raiz foi rebaixada de "identificada" para "hipótese com dois mecanismos compatíveis".**

| # | Afirmação do 239 | Medição (P0) | Nível correto |
|---|---|---|---|
| E1 | "9.010 submissões processadas e concluídas com perfeição" | A sequência de fence do VidSch chegou a **9010** (P0). Linhas de DDI no log: **3458**. Linhas `SUBMISSAO DESPACHADA`: **2826**. Linhas `IB EXECUTADO`: **15** (13 `IB_ENGINE_2LEGS`). `IB_TRIPE_VEREDITO`: **0**. `BYTE_PROOF`: **1**. | "9010 fences emitidas" = P0. "9010 execuções provadas" = **falso** (Lei 2: sem tripe, sem prova de dados) |
| E2 | "mais de 30 Presents" | `Present #1` (4 linhas) e `Present #2` (2 linhas) = **2 Presents**. `CommitVidPn`=1, `SetVidPnSourceAddress`=2, `SDMA=YES`=2. | Os **195/194 Presents** pertencem às corridas **18:20 e 18:26** (33 e 32 submissões cada), não à 18:34 |
| E3 | "zero falta de MMU (GFXHUB STATUS=00000000)" | Zero linhas `GFXHUB_FAULT_SNAPSHOT` na janela da corrida — mas esse snapshot só é tirado no caminho `IB NAO EXECUTADO`, que não disparou. | "nenhum snapshot de fault foi tirado" = P0; "STATUS=00000000" **não foi medido nesta corrida** |
| E4 | "timer de retaguarda one-shot (Period = 0)" | Na árvore: `KeSetTimerEx(..., Period=1, ...)` em `bc250_active_miniport.c:6719` (start) e `Period=0` só no re-arm de `:6656`. | Verificar no INF/manifesto do pacote; **na árvore o timer é periódico de 1 ms** |
| E5 | Causa-raiz: "Núcleo A notifica 9009 depois de o Núcleo B notificar 9010" | As duas últimas linhas do log são `[SubmitVirtual Node0 SDMA] FenceId=9009` seguida de `[McdmSubmitCommandVirtual] Node=0, FenceId=9010` — e **não há** `SUBMISSAO DESPACHADA` para a 9009. | **H (hipótese)**, com dois mecanismos compatíveis (§3). O log **não pode discriminar** (§5, evidência perdida) |

**Por que o log é lossy (isso explica E1):** `Bc250DisplayLog` só grava em disco quando
`KeGetCurrentIrql() == PASSIVE_LEVEL` (`bc250_display_ddi.c:575`). Toda linha emitida de DPC
(DISPATCH_LEVEL) — inclusive **toda a aposentadoria e notificação feita pela DPC** — vai apenas
para `DbgPrintEx` e **não chega ao arquivo**. Consequência: ~62 % das chamadas de DDI não têm
linha (3458 de ~9010; sem duplicatas de fence e com lacunas de 2 a 7, exatamente o padrão de
perda por IRQL, não de reuso de ID).

---

## 1. O que a corrida efetivamente mediu (P0)

Janela: `C:\BC250_DISPLAY_LOG.txt`, 43.541.515 bytes / 493.369 linhas; bind das 18:34 começa no
offset 39.300.590 bytes (linha ~448.835); 44.554 linhas de corrida.

- Fence do VidSch: 1 → **9010**, monotonicamente crescente, sem duplicata.
- Submissões despachadas (log): **2826**; tamanho de DMA buffer: 32 B (5653), 96 B (355), 64 B (193), 224 B (10), 448 B (6).
- `IB_EFEITO_DECLARADO: AUSENTE` nas últimas submissões → **nenhum efeito declarado, nenhuma prova de dados**; são IBs minúsculos (NOP-like).
- `TIMEOUT`: **0**. `IB NAO EXECUTADO`: 0. `GFXHUB_FAULT_SNAPSHOT`: 0. `FENCE_VISIBILIDADE`: 0. `RECUSA`: 2.
- Display: `CommitVidPn`=1, `SetVidPnSourceAddress`=2, `Present`=2, `SDMA=YES`=2, `BYTE_PROOF`=1.
- UMD (`dwm.exe`, pid 1200): **1** `DrawIndexed`, recusado. Nenhum `Present`, `Flush` ou `Blt`. Nenhuma linha `DrawBltSubstitution REPROVOU`.
- Fusível anti-boot-loop disparado (`State=2`, `Boots=4`, `LastStatus=0xC0000001`); `StartDevice` recusado no reboot seguinte.

**Leitura honesta:** o que a 18:34 provou é que o anel SDMA sustentou **~9.000 fences consecutivas
sem timeout e sem fault** — um avanço real de robustez frente às corridas anteriores (12:16: 1819
submissões; 11:22: morte no FenceId 770). O que ela **não** provou é execução com efeito: zero
tripe, 2 Presents, e a tela preta permanece o resultado esperado (Draw recusado, substituição
desarmada por política — faixa C1, `docs/234`).

---

## 2. O que o código estabelece (P1, leitura de fonte — não é hipótese)

1. **A retirada e a notificação não são atômicas.**
   `Bc250SdmaPresentPollRetirement` (`bc250_sdma_present.c:1943`) adquire `RingLock` (:1963),
   consome a **cabeça FIFO** (:2041 `InFlightHead = (headSlot+1) % 32`), **libera o lock** (:2043)
   e devolve `Result` preenchido. A notificação acontece **depois**, fora do lock, no laço de
   `bc250_active_miniport.c:6609-6623`. Portanto: dois threads consomem em ordem (FIFO garante
   isso), mas **notificam fora de ordem**.
2. **Há dois chamadores concorrentes, e mais um gatilho.**
   `Bc250ActiveSdmaRetireAndNotify(context, FALSE)` no caminho de submissão (`:7572`);
   `Bc250ActiveSdmaRetireAndNotify(context, TRUE)` na DPC (`:6696`), enfileirada também ao fim de
   cada despacho (`:7717`).
3. **`DxgkCbSynchronizeExecution` serializa o corpo do callback, mas não a ordem de chegada.**
   Os dois threads disputam o lock de interrupção (DIRQL); quem chega primeiro notifica primeiro —
   independentemente de quem retirou o slot primeiro. É a race, vista do código.
4. **A marca d'água já existe, mas é write-only.**
   `LastNotifiedFenceIdByNode[]` é escrita em `:6228-6233` (caminho SDMA) e em
   `bc250_submission_engine.c:2128-2132` (dreno do rastreador). **Nenhum lugar a lê como guarda.**
   As duas escritas ocorrem dentro de `DxgkCbSynchronizeExecution`, logo já são serializadas entre si.
5. **Existe uma SEGUNDA via de notificação, sem guarda:** o dreno do rastreador
   (`:6259-6293`) notifica `DMA_COMPLETED` em lote (até 16) para entradas de **qualquer nó**.
   Hoje ela só é exercitada se houver `PUSH` no rastreador (opcode 0, `:6119`) — não há chamador na árvore.
   Se o PUSH voltar a ser usado para o nó 0, haverá **dois notificadores independentes da mesma fence**.
6. **O caminho de TIMEOUT engole a fence.** `:2103-2121`: a cabeça é consumida, `Result->Timeout=TRUE`,
   o laço faz `break` e **nada é notificado**. Uma submissão posterior que complete fará o VidSch
   considerar aquela fence concluída implicitamente (semântica "tudo até N").
7. **Tipos:** `SubmissionFenceId` é `UINT` (32 bits) em `d3dkmddi.h`; não há truncamento relevante em 9010.

---

## 3. Mecanismos compatíveis com `0x119` Arg1=0x1, Arg2=9009, Arg3=9010

**Decodificação (P1, Microsoft):** Arg1=0x1 = `DRIVER_REPORTED_INVALID_FENCE_ID` — "o driver
reportou um ID de fence inválido ao escalonador". A Microsoft **não publica** a semântica de
Arg2/Arg3; a leitura "Arg2 = fence reportada, Arg3 = marca d'água/limite esperado" é **inferência (H)**.

Dois mecanismos são compatíveis com os argumentos observados, e **ambos são fechados pela mesma guarda**:

- **M1 — inversão de ordem (hipótese do 239).** Thread A retira 9009, é atrasado; DPC retira 9010 e
  notifica; A notifica 9009 → reporte decrescente.
- **M2 — re-notificação/duplicata.** 9009 é notificada corretamente, 9010 também, e depois 9009
  reaparece (entrada devolvida duas vezes por caminhos distintos — por exemplo a via SDMA **e** a via
  de dreno do rastreador do item 2.5, ou um `Result` reprocessado após falha de notificação:
  `:6644-6648` registra `FALHA NA NOTIFICACAO` e **segue o laço**).

**O log não discrimina M1 de M2**, porque toda a evidência do lado da DPC é descartada (§0).
Nossa inclinação é M1 (há um chamador concorrente real e comprovado; M2 exige que o PUSH do
rastreador esteja ativo, o que não está na árvore), mas **isso é H, não P0**. O BugCheck é P0;
a atribuição da causa, não.

---

## 4. Perguntas

### Q1 — Guarda monotonônica por nó: suficiente?

```c
if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_NOTIFY_SDMA) {
    if (sync->NodeOrdinal <= BC250_SUBMISSION_NODE_COMPUTE &&
        sync->SubmissionFenceId <= WddmFences.LastNotifiedFenceIdByNode[sync->NodeOrdinal]) {
        return TRUE; /* ja ultrapassada ou repetida: descarta */
    }
    DxgkCbNotifyInterrupt(...);
    WddmFences.LastNotifiedFenceIdByNode[sync->NodeOrdinal] = sync->SubmissionFenceId;
}
```

Nossa leitura: **sim, para a classe M1/M2**, desde que (a) seja **por nó** (não global — uma fence
maior do nó 0 não implica conclusão de fence menor do nó 1); (b) viva **dentro** do callback
sincronizado, onde `DxgkCbSynchronizeExecution` já dá exclusão mútua (sem lock novo, sem risco de
deadlock com o ISR); (c) use `<=` (para matar também a duplicata exata). Perguntas para vocês:
existe cenário legítimo em WDDM em que o driver precise **re-notificar** a mesma fence
(ex.: após reset de engine, ou `LastCompletedFenceId` em `DMA_PREEMPTED`)? Há armadilha em descartar
silenciosamente em vez de contar o descarte?

### Q2 — Drenagem em lote com uma única notificação da maior fence?

Base (P1, Microsoft, `DXGK_INTERRUPT_TYPE`): `DMA_COMPLETED` — "The driver must supply the DMA
buffer fence identifier … assigned … **for the latest completed DMA buffer**".
Nossa leitura: **sim, e é a forma documentada**. Mas **não substitui a Q1**: dois threads ainda
podem inverter (A drena [9009], B drena [9010]). Q1 e Q2 são complementares — Q1 dá corretude,
Q2 dá eficiência (menos `DxgkCbSynchronizeExecution`, menos DIRQL, menos chance de 0x133).
Ressalva que queremos ver avaliada: notificar "máx" é válido **só se todas as fences menores
daquele nó realmente concluíram** — o que hoje é furado pelo item 2.6 (timeout engolido).
Pergunta: concordam que, com timeout presente, o correto é **não** consumir o slot e deixar o TDR
agir, em vez de notificar máx e mentir por implicação?

### Q3 — Manter a retirada dentro de `McdmSubmitCommandVirtual`?

Nossa posição: **manter**. Foi o que eliminou a inanição de cabeça de fila (`0x10E` Arg1=4) e há
recomendação externa anterior nesse sentido (`docs/238` §4.3: "o poll oportunista no
SubmitCommandVirtual está OK — mantenham a forma"). Remover devolve o risco de inanição.
O custo real é latência em DISPATCH_LEVEL — e **já pagamos esse custo**: `0x133`
DPC_WATCHDOG_VIOLATION na corrida 12:16, atribuído a espera síncrona sob spinlock.
Pergunta: com drenagem-em-lote (Q2) e notificação única, mantenha-se a retirada no submit — ou
vocês veem risco de reentrância que Q1+Q2 não cubram?

### Q4 — `DrawIndexed` sem SRV: solid fill via SDMA é aceitável?

**Nossa posição é NÃO**, e queremos que vocês nos contradigam se houver fundamento:
um solid fill **inventa cor que não veio do shader** — é pixel fabricado (Lei 4: proibido fallback
silencioso/falso sucesso). Tornaria **infalsificável** qualquer desktop futuro: não haveria como
distinguir "a GPU rasterizou" de "nós pintamos um retângulo". Foi exatamente por esse argumento
que removemos o `memcpy` de CPU do `Draw` na faixa C1 (`docs/234`).
A rota sancionada para rasterização real está decidida na `BASE_DE_AVANCO.md` §"Backend de shader":
**rota B — DXBC → SPIR-V → RADV/ACO** (reaproveitar ACO/GFX10 e o winsys da branch `wddm2`).
Pergunta: existe variante honesta de preenchimento sólido que **não** seja fabricação
(ex.: preencher com um padrão de poison + contador e declarar "não é rasterização")?
Ou o correto é continuar recusando até a rota B existir?

### Q5 (nova) — Como discriminar M1 de M2 com log que só grava em PASSIVE_LEVEL?

Hoje ~62 % das DDIs e **100 % das aposentadorias em DPC** são invisíveis no arquivo.
Proposta interna: buffer circular pequeno (fence, marca d'água, nó, thread, IRQL) preenchido
dentro do callback sincronizado e despejado no `StopDevice`/registry — zero I/O em DIRQL.
Pergunta: há forma melhor e barata? Um `ETW`/`WMI` já pronto no WDK para isso?

### Q6 (nova) — O caminho de TIMEOUT deve notificar `DMA_PAGE_FAULTED`?

`DXGK_INTERRUPT_DMA_FAULTED` é "reserved for system use. Do not use" (P1, Microsoft) e
**já medimos** que usá-lo produz `0x119` Arg1=0xD (`bc250_active_miniport.c:7689-7709`).
`DMA_PAGE_FAULTED` (9) é o caminho suportado, mas dispara recuperação do SO.
Temos TDR validado no silício (08/09 21:49). Pergunta: qual é o protocolo correto aqui —
deixar o slot preso e o dxgkrnl estourar o timeout de 2 s (TDR), ou notificar page fault?

### Q7 (nova) — Há risco em chamar `DxgkCbSynchronizeExecution` simultaneamente do caminho de
submissão (passivo/dispatch) e da DPC? Nossa leitura é que não (é o uso canônico), mas queremos
confirmação — e se há restrição de IRQL quando o driver declara `Sched(LowIrql=1)`.

### Q8 (nova) — ~9.000 IBs de 32 bytes sem efeito declarado em 193 s: o que são, e devemos tratá-los?
São ~47 submissões/s de DMA buffers de 32 bytes com `IB_EFEITO_DECLARADO: AUSENTE`.
Suspeita: laço do DWM sem rasterizador (o log da UMD mostra 1 draw, recusa, e depois silêncio).
Pergunta: esse volume é esperado num adaptador sem backend D3D11 funcional, ou indica laço
patológico que devemos estrangular (ex.: recusar submissão sem efeito declarado)?

---

## 5. O que pedimos de vocês

1. Confirmar/refutar Q1 e Q2 como conjunto mínimo de corretude (guarda por nó + notificação de máx).
2. Dizer se veem um **terceiro mecanismo** para `Arg1=0x1` que nossa análise não cobriu — em
   particular se a semântica real de Arg2/Arg3 é diferente da que inferimos.
3. Q4: solid fill é admissível em perfil de laboratório **declarado como não-rasterização**, ou é
   fabricação inaceitável? (Nossa posição: inaceitável.)
4. Q6: protocolo correto para submissão que estoura o timeout, dado que TDR já está validado.

### Critérios de aceite internos (Lei 3 — toda mudança fecha com teste nomeado)

- Teste offline `Bc250FenceNotifyOrderTest`: duas threads drenando [N, N+1] com notificação
  invertida → exatamente **uma** chamada a `DxgkCbNotifyInterrupt`, com a fence **N+1**;
  a fence N descartada e **contada** em `NotifySuppressedCount`. Mutação: remover a guarda → reprovar.
- Gate de auditoria: proibir `DxgkCbNotifyInterrupt(DMA_COMPLETED)` fora do callback sincronizado,
  e proibir escrita de `LastNotifiedFenceIdByNode[]` fora das duas fontes conhecidas.
- Critério da próxima corrida (escrito antes dela): com a guarda ativa, **zero `0x119`** em ≥ 9.000
  fences; se `0x119` Arg1=0x1 reaparecer, a hipótese M1/M2 estará **refutada** e o campo volta a abrir.
- Nenhuma dessas mudanças está validada em hardware: **`NÃO VALIDADO EM HARDWARE`**.

---

## 6. Anexo — como reproduzir as contagens deste documento

```powershell
# janelas por bind (offsets de display-log-baseline.json de cada pasta de evidência)
# 18:20 -> 38962283 ; 18:26 -> 39132082 ; 18:34 -> 39300590 ; EOF -> tamanho do arquivo
$src='C:\BC250_DISPLAY_LOG.txt'
# contar por janela: [McdmSubmitCommandVirtual] Node=X, FenceId=Y | SUBMISSAO DESPACHADA |
# IB EXECUTADO | IB_TRIPE_VEREDITO | TIMEOUT | GFXHUB_FAULT_SNAPSHOT | [Present # |
# CommitVidPn | SetVidPnSourceAddress | BYTE_PROOF
```
Trechos de código citados: `bc250_sdma_present.c:1943-2094` (retirada), `:2103-2121` (timeout);
`bc250_active_miniport.c:6119-6126` (opcodes), `:6218-6238` (notificação + marca d'água),
`:6259-6293` (dreno do rastreador), `:6609-6623` (laço de retirada/notificação), `:6656` e `:6719`
(timers), `:7572` e `:7717` (caminho de submissão), `:7689-7710` (lição medida do `DMA_FAULTED`);
`bc250_display_ddi.c:553-589` (log só em PASSIVE_LEVEL);
`bc250_submission_engine.c:2100-2136` (dreno e marca d'água por nó).
Fontes externas: Microsoft, *Bug Check 0x119* (tabela de Arg1) e *DXGK_INTERRUPT_TYPE*
(`DMA_COMPLETED` = fence do **último** DMA buffer concluído; `DMA_FAULTED` reservado ao sistema).
