# 238 — Resposta à consulta: `0x10E` Arg1=4 e aposentadoria de fila multi-slot

> **Base:** verificação independente em `driver/wdk/bc250_sdma_present.c`,
> recorte do bind das 16:56 (`start_offset=38.386.654`, 6.446 linhas) do log cumulativo, e o
> registro de evento do Windows. **Nenhuma corrida foi gasta para produzir esta resposta.**

---

## 0. O diagnóstico de vocês está CERTO. Confirmado por três vias independentes

Diferente da consulta anterior (docs/236), aqui não há o que corrigir na causa raiz. Conferi e
bate:

**Código.** `driver/wdk/bc250_sdma_present.c:1993` e `:1998`:

```c
if (fenceObserved == nextFence) {                       /* sinal correto */
    uint32_t rptrReg   = Bc250SdmaRegRead(...RB_RPTR...);
    uint32_t rptrWbRaw = g_Bc250SdmaPresent.Work[...];
    if (Bc250SdmaRptrMatches(rptrReg, rptrWbRaw, targetWptrBytes)) {   /* <-- gate fatal */
```

e `:51`:

```c
static BOOLEAN Bc250SdmaRptrMatches(uint32_t RptrReg, uint32_t RptrWb, uint32_t TargetWptr)
{
    uint32_t target = TargetWptr % BC250_SDMA_PRESENT_RING_BYTES;
    return ((RptrReg % BC250_SDMA_PRESENT_RING_BYTES) == target ||
            (RptrWb  % BC250_SDMA_PRESENT_RING_BYTES) == target) ? TRUE : FALSE;
}
```

A fence é condição **necessária mas não suficiente**; o RPTR com igualdade estrita é a segunda
condição, e é ela que reprova.

**Medição do bind das 16:56.** 296 `McdmSubmitCommandVirtual Node=0` contra **4**
`DMA_COMPLETED notificado`, nas fences **61, 196, 393 e 636** — exatamente o padrão de
coincidência aleatória que vocês descreveram. Três TDRs, todos com o silício íntegro:
`STATUS1=000003FF` (idle), `RPTR` reduzido idêntico ao `WPTR`, `GFXHUB STATUS=00000000`.

**Registro do Windows.** `0x0000010e (0x0000000000000004, 0x0, 0x000000000000000a, 0x0)` às
16:58:26. Confere.

Uma precisão só: eu contei **296** despachos no recorte deste bind, não 808 — se o 808 vier de
outro contador (ou de outro bind), vale reconciliar, porque a razão `notificadas/despachadas` é a
métrica de PASS da próxima corrida.

**A fila multi-slot e o despacho assíncrono estão certos. O defeito é o predicado de
aposentadoria.** Vocês trocaram a parede do `0x119`/`0x133` por uma parede de inanição — o que é
progresso, porque esta tem conserto local e barato.

---

## 1. Pergunta 1 — a fence basta? Sim. E o RPTR tem de SAIR do portão

### 1.1 A fence é o sinal canônico de completude

Sim, `fenceObserved == nextFence` **basta**, e não é opinião de estilo: é a semântica do pacote.
O SDMA executa os pacotes de um anel **em ordem**. Quando o `SDMA_OP_FENCE` escreve o valor, todo
pacote à frente dele naquele anel já foi executado. É exatamente para isso que o pacote existe.

E no caso de vocês é mais do que sinal, é **prova**: a área do slot é pré-carregada com poison
`0x43DAFFFE` e a fence é monotônica. Poison → valor exato esperado significa que a **GPU** escreveu.
Isso satisfaz a perna 2 da Lei 2 sozinha, sem depender do RPTR.

Ressalva que vale manter: a fence provar *execução* não faz a escrita ficar **visível à CPU**. Foi
a lição validada em 12:16 — o flush de HDP é obrigatório antes de ler o slot. Vocês já fazem isso;
não removam.

### 1.2 Subordinar a aposentadoria ao RPTR é erro de categoria

O RPTR não é um sinal de completude de submissão. É o ponteiro de **consumo do anel**, global à
fila e **compartilhado por todos os slots**. Ele não carrega identidade: não existe leitura de RPTR
que diga *qual* slot terminou.

Numa fila de 32 slots, quando a CPU olha, o RPTR já passou por vários alvos. Exigir igualdade com o
alvo de **um** slot é uma corrida que se perde quase sempre — e `4/296` é a medição dessa
probabilidade. Pior: como o teste está na **cabeça** da FIFO, uma reprovação bloqueia todos os
outros (head-of-line), que nem chegam a ser avaliados. Foi o que aconteceu.

**Para o que o RPTR serve, e só para isso:** detectar que a engine **parou** (hang) — não que uma
submissão terminou. Mantenham-no no caminho de **timeout**, que vocês já têm, e tirem-no do caminho
de sucesso.

### 1.3 Nota sobre a Lei 2, para não trocar um falso sucesso por outro

A perna 1 do tripé (`RPTR == WPTR`) foi desenhada para uma prova **síncrona de submissão única**.
Numa fila assíncrona multi-slot ela deixa de ser o instrumento certo, e insistir nela foi o que
quebrou esta corrida. A reformulação honesta da perna 1 para o caso assíncrono é:

> o RPTR **avançou até ou além** do fim absoluto daquela submissão

— não "é igual a". As pernas 2 (fence sobre poison) e 3 (dados byte a byte) continuam intocadas, e
elas são as que carregam o peso da prova. Sugiro registrar essa reformulação por escrito no
`rules.md`, senão a próxima IA vai reintroduzir o `==` "para respeitar a Lei 2".

---

## 2. Pergunta 1.3 — a fórmula correta, e uma descoberta nos dados de vocês

A proposta `((uint32_t)(Rptr - StartWptr) >= (uint32_t)(TargetWptr - StartWptr))` está na direção
certa (distância modular a partir de uma base), mas tem dois problemas:

1. **Mistura grandeza envolvida e não envolvida.** `TargetWptrBytes` no slot é `cursor * 4`, ou seja
   **já reduzido** ao anel (0..4095). O `RptrReg` **não é reduzido**. Comparar distâncias entre um
   contador monotônico e um valor circular é indefinido.
2. **Ambiguidade de janela.** Com anel de 4096 bytes, a distância modular não distingue "alvo uma
   volta atrás" de "alvo uma volta à frente". Com valores reduzidos, essa informação **não existe
   mais** — nenhuma fórmula a recupera.

### 2.1 E vocês não precisam de aritmética modular nenhuma

As medições de vocês provam que o `RB_RPTR` desta engine é um **contador cumulativo de bytes**, não
um ponteiro reduzido ao anel:

| Corrida | `RPTR` cru | Anel | `RPTR % 4096` | `WPTR` |
|---|---|---|---|---|
| 07/09 17:43 | 4.612 | 4096 | 516 | 516 |
| 10/09 11:22 | 50.244 | 4096 | 1092 | 1092 |
| 10/09 16:56 TDR #1 | 29.188 | 4096 | 516 | 516 |
| 10/09 16:56 TDR #2 | 8.324 | 4096 | 132 | 132 |
| 10/09 16:56 TDR #3 | 8.452 | 4096 | 260 | 260 |

Cinco medições independentes, todas coerentes. O driver **tem** a informação monotônica e o
`% BC250_SDMA_PRESENT_RING_BYTES` a joga no lixo.

A correção da A15 (reduzir à volta do anel) estava certa **para o problema dela** — uma comparação
síncrona única em que o RPTR podia estar uma volta à frente. Generalizá-la para a fila assíncrona é
o que criou este defeito. Vale registrar: a A15 resolveu um `0x119` e plantou este.

### 2.2 Desenho correto

```c
/* Acumulador ABSOLUTO de bytes publicados, mantido sob o mesmo RingLock. */
g_Bc250SdmaPresent.WptrAbsolute += bytesPublishedThisSubmission;
slot->TargetAbsolute = g_Bc250SdmaPresent.WptrAbsolute;   /* 64 bits */

/*
 * Comparacao wrap-safe por diferenca SINALIZADA. Nunca '==', nunca '%'.
 * Vale para qualquer distancia < 2^31 bytes de anel consumido.
 */
static BOOLEAN Bc250SdmaRptrReached(uint32_t RptrRaw, uint32_t TargetAbsoluteLow32)
{
    return ((int32_t)(RptrRaw - TargetAbsoluteLow32) >= 0) ? TRUE : FALSE;
}
```

Duas condições que **não podem** ser esquecidas:

1. **Re-basear na reinicialização do anel.** O `RPTR` zera quando o anel é reconstruído — o próprio
   log prova: 29.188 no TDR #1 e **8.324** no TDR #2, ou seja, diminuiu. Em
   `RestartFromTimeout` e em qualquer init de anel, zerem o acumulador e **descartem ou completem**
   o que ainda estiver na fila. Sem isso, todo alvo antigo fica inalcançável para sempre e vocês
   reproduzem a inanição de forma permanente após o primeiro TDR.
2. **Atualizar o portão de auditoria.** `audit_ring_wrap_arithmetic` hoje proíbe o `==` cru. Ele
   precisa passar a **proibir `% BC250_SDMA_PRESENT_RING_BYTES` no caminho de aposentadoria** e a
   **exigir** o helper de diferença sinalizada. E provem por mutação — um portão que não reprova a
   mutação é falso negativo silencioso, e isso já aconteceu neste repo (A16).

---

## 3. Pergunta 2 — ordem de notificação: marca d'água é legal e é melhor

`DXGK_INTERRUPT_DMA_COMPLETED` carrega um `SubmissionFenceId`, e o scheduler interpreta esse valor
como *"tudo até e inclusive esta fence, neste nó, completou"*. Portanto:

- **Podem** avançar a marca d'água para a maior fence confirmada. Numa fila de hardware única e
  em-ordem, isso é literalmente correto, não é atalho.
- **Não podem** notificar fence decrescente. Nunca reportem uma fence antiga depois de uma mais
  nova.
- Consequência prática para o conserto: aposentem **todos** os slots cuja fence já apareceu em uma
  única passada e notifiquem **uma vez** com a maior. Hoje vocês aposentam só a cabeça, um por
  chamada — o que, mesmo com o predicado corrigido, mantém a fila lenta e vulnerável a bloqueio de
  cabeça.

### 3.1 Simplificação que eu recomendo

Em vez de 32 endereços de fence, use **um único** local monotônico (um *seqno*) para a fila inteira.
Uma leitura de um dword responde tudo: todo slot com `HardwareFence <= observado` está completo.
Mantenham o poison por slot **apenas** onde há prova de dados declarada (perna 3 da Lei 2) — que é
a minoria das submissões.

**Cuidado com a Lei 4:** a marca d'água tem de vir de valor **escrito pela GPU**. Nunca a avancem
por CPU sob o argumento de que "já deve ter terminado". Isso seria fence incrementada por CPU, que é
proibição nominal.

---

## 4. Pergunta 3 — a arquitetura: funciona, com dois riscos concretos

### 4.1 `KeSetTimerEx` de 1 ms não entrega 1 ms

O tick padrão do Windows 10 é ~15,6 ms e temporizadores de kernel arredondam para o tick, a menos
que a resolução da plataforma seja elevada. Ou seja, o piso real de drenagem é **~15 ms**, não 1 ms:
~64 drenagens por segundo contra um DWM a 56 Hz submetendo várias por quadro. Com aposentadoria
só-da-cabeça, isso sozinho já produz acúmulo. Elevar a resolução global do timer num driver de
display é remédio pior que a doença.

### 4.2 A resposta certa é a interrupção, e o hardware já oferece

O SDMA tem `SDMA_OP_TRAP`, que levanta interrupção na conclusão. O caminho canônico de completude em
WDDM é:

```
SDMA_OP_TRAP  ->  DxgkDdiInterruptRoutine (DIRQL)
                    -> DxgkCbNotifyInterrupt(DXGK_INTERRUPT_DMA_COMPLETED, fence)
                    -> DxgkCbQueueDpc
                       -> DxgkDdiDpcRoutine -> DxgkCbNotifyDpc
```

Notificar do DPC de timer funciona, mas é *polling*; o trap + ISR é o que torna a latência
determinística e elimina o problema de resolução de timer.

**Confiram um detalhe que costuma passar:** vocês chamam `DxgkCbNotifyDpc`? Sem ele o scheduler pode
não rodar prontamente mesmo depois de `DxgkCbNotifyInterrupt`, e o sintoma é indistinguível de
"fence não chegou" — exatamente o quadro que levou ao TDR aqui.

### 4.3 O poll oportunista no `SubmitCommandVirtual` está OK — mantenham a forma

É aceitável **desde que** seja uma passada única e limitada, sem giro. É o que vocês têm agora.
O laço de 2000 polls com `YieldProcessor()` era o produtor do `0x133`; não voltem a ele em nenhuma
variante, nem com teto menor.

### 4.4 Disciplina de lock e IRQL

Não chamem `DxgkCbNotifyInterrupt` segurando o `RingLock` se o ISR também o toma — junte as
completudes sob o lock, solte, e só então notifique. A função de aposentadoria de vocês já libera o
spinlock antes de preencher `Result`; preservem essa forma ao passar a aposentar vários por
passada.

---

## 5. O `TILE_PROBE` rodou e não informou nada — e isso se conserta de graça

```
[Present #1 TILE_PROBE] Off=0: 00000000 00000000 00000000 00000000 | Off+64: 00000000 00000000 | Off+10240: 00000000 00000000
[Present #3 TILE_PROBE] Off=0: 00000000 00000000 00000000 00000000 | Off+64: 00000000 00000000 | Off+10240: 00000000 00000000
```

O discriminador disparou sobre superfície **em branco** (a corrida foi 15 s de tela preta), e zero é
zero em qualquer layout. **Não discrimina nada.** A instrumentação está certa; o gatilho está errado.

Conserto: condicionem a emissão a **conteúdo**, não ao número do quadro. Só emitam quando a
amostragem `Spread` daquela superfície mostrar, digamos, `NonZero >= 64/256`, e emitam para os 3
primeiros quadros **qualificados**. Custo zero e garante que a próxima corrida responda a pergunta
do tiling em vez de gastar outra.

---

## 6. Ordem recomendada

1. **Predicado de aposentadoria** (§1 e §2): fence como sinal canônico; RPTR só como segurança
   monotônica com diferença sinalizada; re-base no init do anel. **É o defeito inteiro.**
2. **Aposentar todos os slots prontos por passada** e notificar por marca d'água (§3).
3. **Condicionar o `TILE_PROBE` a conteúdo** (§5). Custo zero, pega carona na mesma corrida.
4. Depois: `SDMA_OP_TRAP` + ISR substituindo o timer de 1 ms (§4.2), e confirmar `DxgkCbNotifyDpc`.
5. Só então voltar ao tiling / de-tiling.

**Critério de PASS a escrever ANTES de armar**, com o DWM ativo:

- `notificadas / despachadas >= 0,99` (contra 4/296 = 1,4% desta corrida);
- **zero** TDR, **zero** BugCheck;
- `InFlightCount` voltando a 0 entre quadros;
- e, se houver conteúdo, `TILE_PROBE` com dwords não-nulos nos três offsets.

Qualquer um desses falhando, a corrida ainda informa — desde que a razão de notificação seja
registrada. **Não** aumentem o número de slots como tentativa de conserto: 32 slots com predicado
errado esvaziam na mesma taxa de 1,4%.

**Ao empacotar:** 3.132.1.0 **já foi instalado**. Subam o `DriverVer` fonte antes de gerar outro
candidato, senão dois artefatos diferentes carregam o mesmo `DriverVer` — erro já cometido com o
3.92.1.0.

---

## 7. Sobre o `0x10E` Arg1=4 em si

A leitura de vocês é **plausível e coerente com o log**: três TDRs consecutivos, VidMm entrando no
caminho de recuperação, e o motor de paginação sem completar fence nenhuma porque 284 submissões
estavam presas. Não tenho fonte local para o texto exato de `Arg1=0x4`, então trato a definição
citada como **P1 (derivada da documentação)**, não P0.

O que dá para afirmar sem o dump: o `0x10E` é **consequência**, não causa. A causa é a inanição da
fila, e ela está provada por código e por contagem. Se quiserem fechar a atribuição em P0, o stack
do `MEMORY.DMP` de 1,10 GB responde:

```
powershell -Command "& 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe' -z C:\Windows\MEMORY.DMP -y 'srv*C:\Symbols*https://msdl.microsoft.com/download/symbols' -c '!analyze -v; lm m bc250*; q'"
```

Mas eu **não** gastaria tempo nisso agora: a causa raiz já está isolada e o conserto não depende do
dump. Prioridade é o §6.
