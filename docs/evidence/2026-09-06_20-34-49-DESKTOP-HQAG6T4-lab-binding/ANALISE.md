# Análise da corrida 2026-09-06 20:34

> **Nota de arquivamento (06/09 21:08).** Esta pasta foi fechada sem o log do KMD.
> O log só existia em `C:\bc250_display_log.txt`, que é **cumulativo e sobrescrito pelo
> próximo bind**. Ele foi copiado para cá antes de qualquer nova corrida. Sem essa cópia,
> os números do handoff 172 não seriam reproduzíveis a partir do corpus, e as linhas de
> `BASE_DE_AVANCO.md` que dependem deles não teriam prova arquivada.

## Artefato preservado

- `bc250_display_log-full.txt` (567.937 bytes)
- SHA-256: `BA524D826F170869049FD68DAB1EA0C33B01D2283E7E7723AE7983599EFF9D1A`
- **Ressalva de escopo:** o arquivo é cumulativo. Ele contém, no início, o conteúdo já
  arquivado da corrida 19:53 (`476.672` bytes, superconjunto) e, ao final, a corrida
  20:34. A leitura por corrida exige separar por offset — é exatamente o que a correção 3
  do handoff 172 (registrar o offset inicial do log no binder) passa a fornecer.

## Contagens conferidas contra o arquivo

| Medida | Valor lido |
|---|---|
| `IsSupportedVidPn` | último `Call #97` no bind 20:34, **zero** com `Supported=0` |
| `CommitVidPn` | **0** |
| `SetVidPnSourceAddress` | **0** |
| `GetStandardAllocationDriverData` | **0** |
| `SharedPrimary` | **0** |
| `McdmCreateContext Node=1` | **2** no bind 20:34 (6 no arquivo, 3 binds criaram contexto) |
| `SubmitCommandVirtual Node=1` | **0** |
| `Generation=` publicadas | 2 a **9** (arquivo cumulativo) |
| `PivotType=1..5` | **13 de cada** no bind 20:34 (78 de cada no arquivo, 6 binds) |

Confirma o bloqueio de display: nenhuma primária foi criada nem comitada, logo não houve
origem para o blit ao scanout GOP. **A causa continua NÃO VALIDADA EM HARDWARE.**

## Segmentação obrigatória: o arquivo cobre 6 binds

O log é cumulativo e **não** corresponde a uma corrida só. As fronteiras são explícitas:

```text
linha    1  [FUSE] STARTDEVICE AUTORIZADO (Boots=6)
linha 1411  [StartDevice] FUSIVEL ANTI-BOOT-LOOP DISPARADO: recusa StartDevice
linha 1412  [FUSE] STARTDEVICE AUTORIZADO (Boots=1)
linha 2896  [FUSE] STARTDEVICE AUTORIZADO (Boots=2)
linha 4306  [FUSE] STARTDEVICE AUTORIZADO (Boots=3)
linha 5869  [FUSE] STARTDEVICE AUTORIZADO (Boots=1)
linha 7238  [FUSE] STARTDEVICE AUTORIZADO (Boots=1)   <-- corrida 20:34
fim   8607
```

O contador `IsSupportedVidPn (Call #N)` reinicia em cada bind (linhas 503, 1914, 3398,
4808, 6275, 7644), o que confirma a mesma partição. **Toda contagem por corrida exige
recortar a faixa; medir o arquivo inteiro mistura seis binds.**

## Snapshot GPUVA — recortado na corrida 20:34 (linhas 7238-8607)

```text
IB_GPUVA_SNAPSHOT: Status=0 ObjStatus=0 VA=0x22F000  (1x)
IB_GPUVA_SNAPSHOT: Status=0 ObjStatus=0 VA=0x22F020  (1x)
PT_WALK IB RESOLVIDO: VA=0x22F000 -> 0x146602000
PT_WALK IB_PA_MATCH:  caminhada=0x146602000 cpu=0x146602000 -> CONFERE
```

As duas submissões de paginação desta corrida trazem `Status=0`. As entradas
`Status=9 ObjStatus=0 ... Offset=0x0` que aparecem antes da linha 7238 pertencem a
**binds anteriores** do arquivo cumulativo, não a esta corrida — não devem ser citadas
como fase inicial dela.

## Tripé físico (Node 0, paginação)

```text
SdmaPresent PAGING_FILL TRIPÉ PROVADO NO SILÍCIO: Dest=0xF400AAC000 (Off=0xAAC000), Size=81920,  Pattern=0x00000000 in 3 polls
SdmaPresent PAGING_FILL TRIPÉ PROVADO NO SILÍCIO: Dest=0xF400AC0000 (Off=0xAC0000), Size=4096,   Pattern=0x00000000 in 2 polls
```

**Ressalva obrigatória:** o log é explícito de que estes fills são
`PLANO A DIRECT FILL ... (substituindo INDIRECT)`. O tripé provado é de **fill direto**, e
não da execução do IB de paginação sob VMID8 — que permanece
**NÃO VALIDADA EM HARDWARE**, coerente com o handoff 172.

## Estado da máquina

`radv-harness-hang.dmp` com **0 bytes**; `harness-stage-journal.txt` para em
`before vkCreateDevice(physical, &device_info, NULL, &device)` sem retorno;
`harness-timeout-state.json` em `TIMEOUT_OBSERVED_BEFORE_TERMINATE` (20 s);
`watchdog-state.json` em `ROLLBACK_STARTED` por expiração da confirmação visual.
Fusível `C:\BC250_BOOT_FUSE.dat` permaneceu **ARMED** — boot frio obrigatório.
