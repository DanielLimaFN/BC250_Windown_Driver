# Resultado físico do 3.32 — a fila deixou de estar vazia e ociosa

Data local: 30/08/2026
Ensaiado: **3.32.0.0 / SchemaVersion 103**
Veredito: **NÃO VALIDADO EM HARDWARE** — nenhuma wave, resultado e fence envenenados.

Continuação de [`107_RESULTADO_3.31_E_CANDIDATO_3.32.md`](107_RESULTADO_3.31_E_CANDIDATO_3.32.md).

---

## 1. Pela primeira vez a transação chegou ao fim

`HqdActiveStatus = 7` (`PM4_NOT_CONSUMED`) — não houve abort. O 3.30 parou no
`EN`, o 3.31 parou no WPTR pré-`ACTIVE`. O 3.32 percorreu a sequência inteira:
`CNTL1` → WPTR → `INIT_FETCHER` → `ACTIVE` → doorbell → 50000 iterações de
polling → teardown.

Boot frio confirmado: `WptrPollCntl1DirtyEntry = 0`, `WptrPollCntl1Before = 0`.
O reset limpou a `QUEUE_MASK`, como previsto.

## 2. O cursor passou a ser publicado pelo hardware

| Campo | Valor | Leitura |
|---|---:|---|
| `PreActiveWptrApplied` / `Readback` | `40` / `0` | pré-`ACTIVE` continua recusado (esperado) |
| **`PostActiveWptrObserved`** | **`40`** | **WPTR logo após `ACTIVE=1`, antes de doorbell ou escrita** |
| `DoorbellRung` / `WptrAfterKick` | `1` / `40` | permanece 40 |
| **`DirectWptrKickApplied`** | **`0`** | **a escrita direta de fallback NÃO foi acionada** |
| `WptrMmioLast` | `40` | permanece 40 ao longo de todo o polling |

Este é o ponto central. No 3.29 o cursor só aparecia por escrita direta da CPU
e decaía para `0` nas amostras seguintes. Agora ele é publicado **pela via de
hardware**, sem nenhuma escrita direta, e **se mantém**. A `QUEUE_MASK` do
`CP_PQ_WPTR_POLL_CNTL1` era exatamente a peça que faltava.

Nada aqui é simulado: `DirectWptrKickApplied = 0` prova que a CPU não forçou o
ponteiro.

## 3. O silício reconhece que há trabalho pendente

Decodificação a partir de `gc_10_1_0_sh_mask.h`, não de suposição:

```text
CP_HQD_PQ_CONTROL__PQ_EMPTY__SHIFT      0xf   -> bit 15 (0x00008000)
CP_HQD_HQ_STATUS0__QUEUE_IDLE__SHIFT    0x1e  -> bit 30 (0x40000000)
```

| Registrador | 3.29 | 3.32 | Mudança |
|---|---:|---:|---|
| `PQ_CONTROL` | `0xD030890D` | `0xD030090D` | **`PQ_EMPTY` 1 → 0** |
| `HQ_STATUS0` (final) | `0x40008000` | `0x00008000` | **`QUEUE_IDLE` 1 → 0** |

A fila deixou de estar vazia **e** deixou de estar ociosa. Os dois bits que
travavam o diagnóstico desde o 3.28 caíram no mesmo ciclo.

`PQ_CONTROL` decodificado confere com a configuração pedida: `QUEUE_SIZE = 13`
(ring de 64 KB → 16384 dwords), `RPTR_BLOCK_SIZE = 9`, `NO_UPDATE_RPTR = 0`,
`EXE_DISABLE = 0`.

## 4. Mas o RPTR não avança

| Campo | Valor |
|---|---:|
| `RptrAfterKick` / `RptrMmioLast` / `RptrAfter` | `0` / `0` / `0` |
| `PollIterations` | `50000` (esgotado) |
| `MecPcTransitions` / `Samples` | `38` / `196` |
| `HqdErrorPollLast` | `0` |
| `CpcUtcl1Error` / `CpcUtcl1Status` / `CpfUtcl1Status` | `0` / `0` / `0` |
| `CpcIntStatus`, `Me1Pipe0`, `Me2Pipe0`, `Me2Pipe1` | todos `0` |
| `ShaderResultAfter` / `ShaderFenceAfter` | `0x43DAFFFE` / `0xA55A0FF0` (veneno intacto) |

O MEC está executando (38 transições de PC em 196 amostras), não há falta de
tradução, não há erro de HQD, não há interrupção pendente — e mesmo assim
nenhum pacote é retirado da fila.

A memória e a tradução estão provadas neste mesmo ciclo: ring em GPUVA
`0x10000`, `PTE = 0xF400AA00E1`, `ShaderRingTranslationPassed = 1`,
`ShaderRingReadbackMismatches = 0`, `ShaderReadbackMismatches = 0`,
`ShaderWgpPreflightPassed = 1`.

## 5. O teardown mudou de comportamento — e isso é coerente

| Campo | Valor |
|---|---:|
| `FirstRestoreFailureStage` | `1` (`ACTIVE_DEACTIVATE`) |
| `FirstRestoreFailureOffset` | `0x0000C82C` (`CP_HQD_ACTIVE`) |
| `FirstRestoreFailureExpected` / `Observed` | `0` / `1` |
| `FirstRestoreFailureTransport` | `0` (divergência de valor) |
| `ActiveApplied` / `ActiveAfter` | `1` / `0` |

`DEQUEUE_REQUEST` não desativou a fila dentro de 1000 amostras; foi preciso o
teardown de emergência com `ACTIVE = 0`, que funcionou. Uma fila que se recusa
a fazer dequeue é uma fila **ocupada** — consistente com `QUEUE_IDLE = 0`. Nos
ciclos anteriores, com a fila ociosa, o dequeue era limpo.

O gate de rollback reprova, e deve reprovar: houve estado que não voltou pela
via normal.

## 6. Correção de uma conclusão do 3.31

O doc 107 §2.4 afirmou que a `QUEUE_MASK` "acende e não apaga". Estava
incompleto. Neste ciclo ela limpou **na primeira tentativa**
(`WptrPollCntl1ClearAttempts = 1`, `WptrPollCntl1Restored = 0`).

A diferença é que no 3.31 a transação abortou antes de `ACTIVE` e a fila nunca
passou pelo ciclo de vida completo. A máscara não é sticky — ela exige que a
fila tenha sido ativada e desativada antes de aceitar a limpeza. A guarda
`WptrPollCntl1DirtyEntry` continua valendo e fez seu papel confirmando o boot
frio.

## 7. Onde o bloqueio está agora

O quadro mudou de lugar. Antes: fila vazia, ociosa, cursor não publicado.
Agora: **cursor publicado pelo hardware, fila não-vazia, fila não-ociosa, MEC
rodando, zero faltas — e nenhum pacote consumido.**

O bloqueio deixou de ser a publicação do WPTR e passou a ser o consumo do ring
pelo CP. Hipóteses que o próximo ciclo precisa separar, sem empilhar
especulação:

- o MEC não está escalonando este HQD (topologia de pipe/queue em
  `RLC_CP_SCHEDULERS`, `CP_MEC_CNTL`, prioridade/quantum);
- o CP busca mas não consegue interpretar o primeiro pacote PM4;
- o writeback de RPTR (`RPTR_REPORT_ADDR`) não está sendo aceito;
- `CP_HQD_VMID` divergente do contexto GCVM programado.

O próximo candidato deve ser **de observação**, não de nova hipótese:
amostrar `DEQUEUE_REQUEST`, `PQ_RPTR`, `PQ_CONTROL`, `HQ_STATUS0` e o PC do MEC
ao longo do polling, e catalogar formalmente `PQ_EMPTY` e `QUEUE_IDLE` agora
que existe evidência diferencial dos dois.

## 8. O que este documento NÃO afirma

Nenhuma wave executou. `TriadPassed = 0`, `PhysicalReady = 0`, resultado e
fence permanecem no veneno. O tripé não fechou.

**NÃO VALIDADO EM HARDWARE.**
