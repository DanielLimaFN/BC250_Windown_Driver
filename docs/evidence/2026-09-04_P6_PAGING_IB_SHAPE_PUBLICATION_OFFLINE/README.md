# P6 — Instrumentação do IB de paginação e reporte legal de falta (candidato 3.81)

> **Data:** 2026-09-04 · **Classe:** offline (compilação + testes de contrato)
> **Origem:** análise forense da corrida de 22:27 (`C:\BC250_DISPLAY_LOG.txt`, 1.142 linhas)

---

## 1. O que a corrida de 22:27 mediu

```text
[FillVirtual] Completo: 11075584 bytes, pattern=0x00000000, em 16 dwords
[McdmSubmitCommandVirtual] Node=0, FenceId=1, DmaSize=64
[SubmitVirtual Node0 SDMA] FenceId=1, DmaSize=64, DmaVA=0x24F000
[SubmitVirtual Node0 SDMA] IB_GPUVA_SNAPSHOT: Status=9 ObjStatus=0 VA=0x24F000 Size=64
[SubmitVirtual Node0 SDMA] IB_TRIPOD TIMEOUT: TargetWPTR=132, WPTR=132, RPTR=96,
    STATUS=46DE6756, ERR_LOG=0000000F, FenceExp=00001001, FenceObs=43DAFFFE
[SubmitVirtual Node0 SDMA] GFXHUB_FAULT_SNAPSHOT: STATUS=00000000 ADDR=00000000:00000000
[SubmitVirtual Node0 SDMA] IB NAO EXECUTADO #1
[FUSE] STARTDEVICE RECUSADO: Fusivel disparado (State=2, Boots=2, LastStatus=c0000001)
```

### 1.1 Onde a engine parou — reconstrução aritmética

O boot tripod deixou `CurrentCursor` em 68 bytes = **DW 17**.

| Passo | Cálculo | DW |
|---|---|---|
| `padDwords` | `(2 - 17) & 7` | 1 → NOP em DW 17 |
| `INDIRECT` | 6 DW | DW 18–23 |
| Fim do `INDIRECT` | `17 + 1 + 6` | **DW 24**, `24 % 8 == 0` ✅ |
| `GCR` + `FENCE` | 9 DW | DW 24–32 |
| Cursor final | 33 DW | **132 bytes = `TargetWPTR`** ✅ |

O log mede `RPTR = 96` bytes = **DW 24**.

> A SDMA0 consumiu o NOP e o `INDIRECT` inteiro, saltou para o IB em GPUVA `0x24F000`
> sob VMID8 e **nunca voltou**. O travamento é **dentro do IB**, não no anel. O
> alinhamento do `INDIRECT` está correto e não é a causa.

### 1.2 Delta entre 22:16 (passou) e 22:27 (travou)

O anel é idêntico nas duas corridas. Só o corpo do IB mudou:

| | 22:16 PASS | 22:27 HANG |
|---|---|---|
| Corpo do IB | 15 DW (3× `CONST_FILL`) | 15 DW + 1 NOP de cauda |
| `IB_SIZE` no `INDIRECT` | 15 | 16 |

**Classificação: H (hipótese).** Não há dado para escolher entre "o NOP/`IB_SIZE`
de cauda mata a engine" e "o PASS de 22:16 foi frágil". Nenhuma das duas é causa.

### 1.3 Origem do reboot — P0, provado por dump

```text
BugCheck 0x119 VIDEO_SCHEDULER_INTERNAL_ERROR
  Arg1 = 0x2          "The driver failed upon the submission of a command."
  Arg2 = 0xC0000185   STATUS_IO_DEVICE_ERROR
  módulo carregado: bc250mcdm  fffff808`3f0a0000
```

`0xC0000185` é exatamente o status que `Bc250ActiveSubmitCommandVirtual` devolvia.
**Não existe NTSTATUS de erro aceitável em `DxgkDdiSubmitCommandVirtual`** — o video
scheduler derruba a máquina em qualquer um.

### 1.4 Falha de telemetria — P0, provado por leitura de fonte

`Bc250DisplayLog` (`driver/wdk/bc250_display_ddi.c:536`) só escreve em disco em
`PASSIVE_LEVEL`. O `IB_ALIGN` era emitido com `RingLock` adquirido, isto é, em
`DISPATCH_LEVEL` — **descartado silenciosamente**. A corrida perdeu justamente o dado
que descreve o layout do anel. O SYS carregado era o correto (`2AECF5BE…`); não houve
divergência de binário.

---

## 2. O que o candidato 3.81 muda

| # | Mudança | Arquivo |
|---|---|---|
| 1 | `IB_ALIGN` movido para fora do spinlock, nos dois caminhos de saída, agora com `IbSizeDW` e `PublishedDW` | `bc250_sdma_present.c` |
| 2 | Snapshot UTCL1/SDMA0 **antes** do disparo + linhas `UTCL1_BASELINE` e `UTCL1_DELTA` | `bc250_sdma_present.c` |
| 3 | `IB_DUMP` dos dwords que a engine vai buscar (teto de 32 DW) e `IB_SHAPE` | `bc250_mcdm_callbacks.c` |
| 4 | `IB_SIZE` do `INDIRECT` explícito: exclui os NOPs de cauda, que existem só para alinhar a GPUVA do próximo buffer | `bc250_sdma_present.{c,h}` |
| 5 | Falta reportada por `DXGK_INTERRUPT_DMA_FAULTED` em vez de `STATUS_IO_DEVICE_ERROR` | `bc250_active_miniport.c` |

### 2.1 Por que (5) não viola a Lei 4

Não afirmamos conclusão. Afirmamos **falta**, com o `FenceId` real e o `NTSTATUS` real,
pelo canal que o WDDM define para isso, e entregamos a engine ao TDR — `ResetEngine`,
`ResetFromTimeout`, `RestartFromTimeout` e `QueryEngineStatus` já estão registrados.
A submissão continua contada em `SubmissionsNotExecutedCount` e logada como
`IB NAO EXECUTADO`. Se o canal de notificação não estiver disponível, o driver volta a
devolver o erro e aceita o `0x119` — falha fechada, nunca sucesso silencioso.

### 2.2 Canal da forma do IB

O canal sancionado seria `pDmaBufferPrivateData`, mas declaramos
`PagingBufferPrivateDataSize = 0` em `DRIVERCAPS` e o VidMm entrega o ponteiro vazio.
Mudar esse cap altera o contrato já aceito pelo dxgkrnl no bind; pela Lei 9 isso **não**
foi empacotado na mesma corrida. A publicação encoder→submit é serial, **autovalidada**
contra o `DmaBufferSize` entregue pelo VidMm e **destrutiva**: divergência ou segunda
leitura devolvem 0 e o executor deriva o `IB_SIZE` do tamanho publicado.

---

## 3. Testes que fecham este item

| Teste | Resultado |
|---|---|
| `P6_PAGING_IB_SHAPE_PUBLICATION_OFFLINE` (`driver/tests/mcdm_callbacks_test.c`) | **PASS** |
| `P6_SDMA_INDIRECT_END_ALIGNMENT_OFFLINE` | PASS (regressão) |
| `P6_PAGING_IB_SIZE_ALIGNMENT_OFFLINE` | PASS (regressão) |
| `driver\wdk\build-contract.bat` — suíte completa | **TODOS OS TESTES APROVADOS (0 falhas)** |
| `audit-mcdm-ddi-matrix.py` | PASS (STRUCTURAL ONLY) |
| `audit-bc250mcdm-safety.py` | PASS |
| `audit-ddi-noop-callbacks.py` | APROVADO (0 falhas, 1 aviso pré-existente) |
| `audit-bc250mcdm-package.ps1` | PASS (INTEGRITY ONLY) |

O `P6_PAGING_IB_SHAPE_PUBLICATION_OFFLINE` cobre três casos: `FillVirtual` publica
15 úteis / 16 totais; `TransferVirtual` publica 21 / 24; e o consumo com total
divergente devolve 0 (fail-closed).

---

## 4. Pacote produzido

```text
DriverVer:          09/02/2026, 3.81.0.0
Source SYS SHA256:  FE6AFAEBB48F91E9E249C87C6456829BB628D34B205271496A940DFD2B923619
Signed SYS SHA256:  A458443279BD4A73AAD166F8C6D5070AC72C9A0ED9A0E6F82544EB2091FF5D51
CAT SHA256:         C856CEAE80435996637271BC0BCEE32093002E89CC8296D7C75EDD58C7A6946A
Signer:             8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B
```

---

## 5. Critério de leitura da próxima corrida — teto de 1

O discriminador é decidido por `IB_TRIPOD`:

- **PASS** → a hipótese 1.2 está confirmada: o NOP de cauda / `IB_SIZE=16` era o que
  matava a engine. Esperar então uma segunda IB em `DmaVA=0x24F040`, o que também
  responde à pergunta pendente do handoff anterior.
- **TIMEOUT de novo** → o PASS de 22:16 era frágil. Nesse caso o `IB_DUMP` e o
  `UTCL1_DELTA` passam a ser a base: delta zerado significa que a engine parou **sem**
  levantar fault de tradução, e a investigação vira caminhada root/PDE/PTE a partir do
  `RootAddr` e do `SegId` que o `ActiveUpdatePT` reporta.

Em qualquer dos dois casos o `DMA_FAULTED` deve evitar o `0x119`. **Se o BugCheck 0x119
voltar a aparecer, isso é por si só um achado** — significa que o scheduler recusou a
notificação de falta, e a linha `DMA_FAULTED NAO NOTIFICADO` dirá por quê.

**Abandono:** se a máquina reiniciar antes de qualquer linha `IB_DUMP` chegar ao log,
parar e analisar o dump — não repetir o bind.

---

## 6. Estado honesto

Provado no silício: SDMA0 física, blt de apresentação com byte proof, GOP/VidPn,
instalação e readback do VMID8, `UpdatePageTable` e `FlushTlb` com `AckObserved=1`.

**NÃO VALIDADO EM HARDWARE:** retirada de IB de paginação sob VMID8, byte proof do
`FillVirtual`, IBs consecutivas, composição real do desktop, UMD 3D
(`CreateDevice = E_NOTIMPL`), jogos acelerados.
