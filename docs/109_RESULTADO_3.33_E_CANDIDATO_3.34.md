# Resultado físico do 3.33 e candidato 3.34

Data local: 30/08/2026
Ensaiado: **3.33.0.0 / SchemaVersion 104** — **NÃO VALIDADO EM HARDWARE**
Preparado: **3.34.0.0 / SchemaVersion 105** (offline)

Continuação de [`108_RESULTADO_FISICO_3.32.md`](108_RESULTADO_FISICO_3.32.md).

---

## 1. O 3.32 é reprodutível

Ciclo completo, boot frio (`WptrPollCntl1DirtyEntry = 0`), `confirm` executado
dentro da janela, 607 valores capturados no bind.

| Campo | 3.32 | 3.33 |
|---|---:|---:|
| `HqdActiveStatus` | `7` | `7` |
| `PostActiveWptrObserved` | `40` | `40` |
| `DirectWptrKickApplied` | `0` | `0` |
| `WptrMmioLast` | `40` | `40` |
| `PqControlObserved` | `0xD030090D` | `0xD030090D` |
| `HqStatus0PollLast` | `0x00008000` | `0x00008000` |
| `RptrAfter` | `0` | `0` |

Idêntico. O cursor publicado por hardware, `PQ_EMPTY = 0` e `QUEUE_IDLE = 0`
não foram acidente de um ciclo.

## 2. Duas hipóteses eliminadas

### 2.1 Não é o writeback de RPTR

| Campo | Valor |
|---|---:|
| `RptrNonZeroSamples` | `0` (em 196 amostras) |
| `RptrReportMemFirst` / `RptrReportMemLast` | `0` / `0` |
| `RptrMmioLast` | `0` |

Registrador **e** memória em zero. Se o CP estivesse retirando pacotes sem
refletir no registrador, o writeback em memória teria andado. Não andou. O CP
genuinamente não consome — não é problema de reporte.

### 2.2 Não é o VMID

`VmidPollLast = 0`, igual ao Contexto 0 que programamos. E
`DequeueRequestPollLast = 0`: nada pediu dequeue por conta própria durante a
execução.

## 3. O estado nasce congelado

| Campo | Primeira amostra | Última amostra |
|---|---:|---:|
| `PQ_CONTROL` | `0xD030090D` | `0xD030090D` |
| `HQ_STATUS0` | `0x00008000` | `0x00008000` |

`PQ_EMPTY = 0` e `QUEUE_IDLE = 0` já valem na primeira amostra e não mudam em
50000 iterações. A fila entra no estado "tenho trabalho e não estou ociosa"
imediatamente e fica ali.

## 4. O MEC está em laço fechado

```text
MecPcFirst        0x000017FB
MecPcLast         0x000017FB
MecPcTransitions  49
MecPcSamples      196
```

O PC se move (49 transições) mas volta sempre a `0x17FB`. O microengine está
vivo e girando num laço curto — comportamento de quem espera algo, não de quem
processa pacotes.

## 5. O CP tocou o HQD por conta própria

O rollback reprovou no **estágio 9** (`HQD_PLAN`), offset `0x0000C834` =
`CP_HQD_PERSISTENT_STATE`:

```text
esperado (snapshot)  0x0BE05301
observado            0x0BEC5301
diferenca            0x000C0000  -> bits 18 e 19
```

Bits 18–19 caem dentro de `PRELOAD_SIZE[19:8]`
(`CP_HQD_PERSISTENT_STATE__PRELOAD_SIZE__SHIFT 0x8`), que foi de `0x053` para
`0xC53`. `PRELOAD_REQ[0]` estava em 1 na nossa configuração.

Isto é a primeira vez que se observa **o próprio CP alterando um registrador do
HQD**. Ele não está ignorando a fila: ele mexeu nela. A telemetria de rollback
capturou a mudança em vez de deixá-la passar.

## 6. Onde isso deixa o diagnóstico

Das quatro hipóteses do doc 108 §7:

| Hipótese | Estado |
|---|---|
| Writeback de RPTR recusado | **eliminada** (§2.1) |
| `CP_HQD_VMID` divergente | **eliminada** (§2.2) |
| MEC não escalonando este HQD | enfraquecida — o CP alterou `PERSISTENT_STATE` |
| CP busca mas não retira o pacote | **principal** |

O quadro: fila com trabalho reconhecido, MEC vivo em laço, CP mexendo no HQD,
zero faltas de tradução, zero `HQD_ERROR`, e nenhum pacote retirado.

## 7. O que o 3.34 mede

Outro ciclo **sem nova escrita**. Seis registradores de diagnóstico do CP/CPC,
todos entrando **somente na allowlist de leitura**:

| Registrador | dw | BAR5 | Para quê |
|---|---|---|---|
| `CP_CPC_STATUS` | `0x0E24` | `0x08210` | estado do CPC |
| `CP_CPC_BUSY_STAT` | `0x0E25` | `0x08214` | que unidades estão ocupadas |
| `CP_CPC_STALLED_STAT1` | `0x0E26` | `0x08218` | **em que o CPC está parado** |
| `CP_MEC_ME1_HEADER_DUMP` | `0x0E2E` | `0x08238` | **cabeçalhos PM4 que o ME1 processou** |
| `CP_STALLED_STAT1` | `0x0F3D` | `0x08674` | stall do CP gráficos |
| `CP_STAT` | `0x0F40` | `0x08680` | estado agregado |

### 7.1 Offsets ancorados, não deduzidos no escuro

A base `+0x1260` dwords agora fecha em **cinco** registradores independentes, e
dois deles são escritos com sucesso no silício em todo ensaio:

| Registrador | dw | calculado | usado |
|---|---|---|---|
| `CP_MEC_CNTL` | `0x0E2D` | `0x08234` | `0x08234` ✓ (escrito) |
| `CP_ME_CNTL` | `0x0F56` | `0x086D8` | `0x086D8` ✓ (escrito) |
| `CP_RB_WPTR_POLL_CNTL` | `0x0F62` | `0x08708` | `0x08708` ✓ |
| `CP_PQ_WPTR_POLL_CNTL` | `0x1E23` | `0x0C20C` | `0x0C20C` ✓ |
| `CPC_INT_STATUS` | `0x1E55` | `0x0C2D4` | `0x0C2D4` ✓ |

`CP_MEC_ME1_HEADER_DUMP` (`0x08238`) é **adjacente** ao `CP_MEC_CNTL`
(`0x08234`), que já provou o endereço no hardware.

### 7.2 O header dump é uma FIFO — tratado como tal

Cada leitura de `CP_MEC_ME1_HEADER_DUMP` avança o ponteiro interno. Por isso
ele é drenado em **uma rajada de quatro leituras, uma única vez, depois do laço
de polling** — nunca a cada amostra. O efeito colateral está documentado no
código e no catálogo.

### 7.3 Escrita proibida

Um teste dedicado no `mmio_topology_transport_test.c` exige que os seis sejam
legíveis e que **toda escrita seja recusada**. Escrever no estado do
microengine seria alterar exatamente o que estamos tentando observar.

## 8. Artefatos do pacote 3.34

```text
DriverVer              08/30/2026,3.34.0.0
Schema                 105
recipe cp/vmid0/sdma   1 / 0 / 0
Package SYS SHA-256    6C5CFD3253A13C46D9048917FF719541131A01D4B0D624107D97D59F5BD74C1B
Package INF SHA-256    B8D147FD86113C23F6653C7A9F5DD8DF1B0D025D65D211D911D74C3D10444194
audit-bc250mcdm-lab-package.ps1   PASS
```

Suíte inteira em `EXIT=0`, sem warning no código de kernel.

## 9. Como ler o resultado

| Observação | Leitura |
|---|---|
| `CP_MEC_ME1_HEADER_DUMP` traz o header do nosso PM4 | O CP buscou o pacote; o problema é a interpretação ou execução |
| header dump em zero ou lixo | O CP nunca chegou a buscar o pacote; o bloqueio é no fetch |
| `CP_CPC_STALLED_STAT1` ≠ 0 | Diz diretamente em que o CPC está parado |
| `CP_CPC_BUSY_STAT` ≠ 0 com `STALLED_STAT1` = 0 | O CPC está ocupado sem estar bloqueado — investigar o laço do ME |
| `CP_STAT` muda entre primeira e última amostra | Há progresso interno, ainda que o RPTR não ande |

**NÃO VALIDADO EM HARDWARE.**
