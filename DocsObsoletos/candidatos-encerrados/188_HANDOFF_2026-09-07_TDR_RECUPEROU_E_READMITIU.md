# 188 — TDR recuperou a SDMA0 e readmitiu 40 submissões

**Data:** 2026-09-07 21:49–21:51  
**Corridas gastas:** **uma** (`3.89.0.0`)  
**Estado:** recuperação da engine e readmissão **VALIDADAS EM HARDWARE**; imagem visível
continua bloqueada pelo B7/D3D11.  
**Item 25:** permanece **ABERTO somente na perna “display sobrevive visível”**.

## 1. Desfecho real

A tela ficou preta. Não houve BugCheck. O evento 1074 registra que o reboot das 21:51:14
foi planejado pelo watchdog (`BC250 Watchdog: reiniciando para restaurar video limpo`). O
rollback removeu `oem34.inf`, restaurou BasicDisplay Código 0 e terminou em
`BC250 ONLINE ROLLBACK: PASS`.

Antes do rollback, o watchdog preservou o log integral com 4.653 linhas:

```text
SHA-256 DDBA6A633633E4220625FE24799BBD99AA33F347B3FF54A4EB961FF20D2B16BF
```

## 2. Cadeia TDR medida no silício

```text
TDR_HANG_INDUZIDO ... RPTR=68 WPTR=92
TDR_ENSAIO ... completion deliberadamente não notificado
McdmResetFromTimeout
TDR_QUIESCE ... STATUS=46DC6052 STATUS1=000003FF ERR_LOG=0000000F
McdmRestartFromTimeout
SYNC_NOP TRIPOD PASS: WPTR=40 RPTR=40 Fence=00001002 in 2 polls
TDR_RESTART PROVADO: HardwareEnabled=1
```

O `SYNC_NOP` só é alcançado depois de:

1. pulso assert/deassert em `GRBM_SOFT_RESET.SDMA0`;
2. reconstrução integral do anel;
3. `Bc250TdrValidateSdmaRestartReadback == BC250_TDR_OK`;
4. todos os readbacks adicionais de bases, polling, IB, F32, UTCL1 e golden settings.

A linha com os valores exatos do readback foi chamada sob `RingLock`/DISPATCH_LEVEL e não
foi persistida por `Bc250DisplayLog`, que só escreve arquivo em PASSIVE_LEVEL. Isso é dívida
de observabilidade, não ausência do gate: qualquer falha teria retornado antes do NOP. A
telemetria foi movida offline para depois da prova física; essa mudança específica é
**NÃO VALIDADA EM HARDWARE** e não justifica repetir o ensaio.

`QueryEngineStatus` não foi chamada; o item 24 continua sem exercício físico nessa rota.

## 3. Readmissão depois do restart

Depois de `TDR_RESTART PROVADO`, ainda no mesmo bind:

| Medição | Resultado |
|---|---:|
| Submissões Node 0 recebidas | 41 (1 hang + 40 posteriores) |
| `IB EXECUTADO (INDIRECT)` após recuperação | **40** |
| `TRIPE_PROVADO` completo após recuperação | **4** |
| Recusas de submissão / resolução | **0** |
| Timeouts posteriores | **0** |
| `Present` posteriores | **36** |
| `Present` explicitamente logados com `SDMA=YES` | **10** |
| Estado antes do rollback | `bc250mcdm`, Código 0 |

As primeiras quatro submissões pós-TDR preencheram 11.075.584 bytes sob VMID8 e fecharam
RPTR==WPTR, fence sobre poison e cinco canários com `Divergentes=0`. Isso prova readmissão
real, não apenas a frase do callback.

## 4. Live dump do TDR

O Windows gerou `WATCHDOG-20260907-2149.dmp`, analisado com símbolos corretos:

```text
VIDEO_TDR_TIMEOUT_DETECTED (117)
MODULE_NAME: bc250mcdm
IMAGE_NAME: bc250mcdm.sys
FAILURE_BUCKET_ID: LKD_0x117_IMAGE_bc250mcdm.sys
PROCESS_NAME: System
```

Stack:

```text
watchdog!WdpDbgCaptureTriageDump
watchdog!WdDbgReportRecreate
dxgkrnl!TdrUpdateDbgReport
dxgkrnl!TdrCollectDbgInfoStage2
dxgkrnl!DXGADAPTER::Reset
dxgkrnl!TdrResetFromTimeout
dxgkrnl!TdrResetFromTimeoutWorkItem
```

| Artefato | SHA-256 |
|---|---|
| `WATCHDOG-20260907-2149.dmp` | `56C016FBA5B9C342FF6703034A9264B26F4BC484C427D26B3C1F60FA73CC0AAB` |
| `livekernel-watchdog-analysis.txt` | `480F53527E4E498168721C120E8CF1FF3A6B24E486AEE14A17798F8EADC4F411` |

## 5. Por que o item 25 não fecha inteiro

A engine recuperou e o adaptador permaneceu em Código 0, mas a tela já estava preta e
continuou preta. Houve novo `dwm.exe` fail-fast `0xE0464645` e dump de 197.726.764 bytes;
a primária/scanout permaneceram zeradas (`BYTE_PROOF INCONCLUSIVO`, zero contra zero).

Logo as pernas abaixo estão **P0**:

- hang físico detectado (`0x117`);
- engine quiescida e resetada;
- restart provado por fence física;
- submissões reais readmitidas e executadas;
- adaptador permaneceu em Código 0 até o rollback.

A perna **display visível sobrevive** continua **NÃO VALIDADA EM HARDWARE** e depende do
B7, porque a UMD ainda não fornece o backend D3D10/11 exigido pelo DWM. Não gastar outra
corrida TDR antes do B7.

## 6. Prova offline posterior

O log de readback foi movido para PASSIVE_LEVEL depois do `SYNC_NOP`; o gate de auditoria
agora fiscaliza a ordem real `ValidateReadback → SyncNop` separadamente da persistência da
linha. `build-contract.bat` terminou em **EXIT=0** e a safety audit em **PASS**.

Essa alteração de telemetria é **NÃO VALIDADA EM HARDWARE**.
