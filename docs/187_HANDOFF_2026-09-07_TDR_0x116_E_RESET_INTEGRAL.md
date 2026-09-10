# 187 — TDR no silício: BugCheck 0x116 e candidato com reset integral

**Data:** 2026-09-07 20:41–21:08  
**Corridas gastas:** **uma** (`3.88.0.0`)  
**Estado:** a detecção e o par Reset/Restart foram chamados; o restart falhou. Correção
posterior inteiramente offline, pacote `3.89.0.0` **NÃO INSTALADO**.  
**Item 25:** continua **ABERTO**.

## 1. O que o silício mediu

O pacote armado `3.88.0.0` produziu tela preta e depois tela azul. O evento de sistema
registrou:

```text
BugCheck 0x00000116
Arg1=ffffe00f6575b010
Arg2=fffff8024163d5b0
Arg3=ffffffffc00000a3  (STATUS_DEVICE_NOT_READY)
Arg4=0000000000000004
MEMORY.DMP=C:\Windows\MEMORY.DMP
ReportId=6cc76956-42af-4ff1-b865-1c558c471f51
```

A cadeia relevante do log integral foi:

```text
TdrInduceHang=1 (present=1)
TDR_HANG_INDUZIDO ... WPTR=92, RPTR anterior=68
TDR_ENSAIO ... completion DELIBERADAMENTE nao notificado
McdmResetFromTimeout
TDR_QUIESCE: RPTR=68 WPTR=92 STATUS=46DC6052 STATUS1=000003FF ERR_LOG=0000000F
McdmRestartFromTimeout
SYNC_NOP TIMEOUT: WPTR=40 RPTR=68 FenceExp=00001002 FenceObs=43DAFFFE
TDR_RESTART FALHOU
STATUS_DEVICE_NOT_READY
```

Conclusões permitidas pela medição:

- o hang físico disparou o timeout do `dxgkrnl`;
- `ResetFromTimeout` e `RestartFromTimeout` foram chamados nessa ordem;
- o quiesce desligou o anel e travou o F32;
- o restart antigo não removeu o `POLL_REGMEM` preso: o RPTR permaneceu em 68 e a fence
  preservou o poison;
- devolver `STATUS_DEVICE_NOT_READY` foi fail-closed e aparece como Arg3 do BugCheck.

`QueryEngineStatus` não aparece no log; portanto o item 24 **não foi exercitado por esta
rota 0x116**.

### 1.1 Análise inicial do `MEMORY.DMP`

O dump foi aberto pelo CDB e confirmou:

```text
VIDEO_TDR_FAILURE (116)
Arg2 fffff8024163d5b0 = bc250mcdm!Bc250McdmRestartFromTimeout
Arg3 ffffffffc00000a3 = STATUS_DEVICE_NOT_READY
STACK: dxgkrnl!TdrUpdateDbgReport -> ... -> dxgkrnl!TdrResetFromTimeout
```

Portanto o `dxgkrnl` atribuiu a falha de recuperação ao callback de restart que recusou
declarar a engine pronta. Isso coincide byte por byte com o último veredito do log do KMD;
não há indicação de uma exceção aleatória dentro do miniport.

Ressalva histórica: a primeira linha de comando sofreu expansão de Markdown e passou ao
CDB um symbol store inválido (`[https://...](https://...)`). A análise foi repetida elevada
com `.symfix`; `WRONG_SYMBOLS` desapareceu e o stack válido passou a ser:

```text
nt!KeBugCheckEx
dxgkrnl!TdrBugcheckOnTimeout
dxgkrnl!ADAPTER_RENDER::Reset
dxgkrnl!DXGADAPTER::Reset
dxgkrnl!TdrResetFromTimeout
dxgkrnl!TdrResetFromTimeoutWorkItem
nt!ExpWorkerThread
```

O bucket final é `0x116_IMAGE_bc250mcdm.sys`, processo `System`, em worker thread do TDR.
O dump não mostra exceção executando dentro do miniport no instante do BugCheck; o endereço
do Arg2 é o owner tag `bc250mcdm+0x1D5B0`, previamente resolvido pelo PDB correspondente a
`Bc250McdmRestartFromTimeout`. A atribuição agora tem duas provas independentes: dump e log
do KMD.

Os artefatos preservados são:

| Artefato | SHA-256 |
|---|---|
| `C:\Windows\MEMORY.DMP` | `F45B3751A27651DC1DA002EE0587668628A15025A4EDB88B505B4F3D2BAB3764` |
| `C:\BC250_DISPLAY_LOG.txt` | `6A817E725399CED96465F10CD674FD7BBFC7D76F7B26162FACCEEF10F2557956` |
| `memory-dmp-analysis-symbols-fixed.txt` | `2E998208593FBE54A49075BB3936881CF6992C3C5D3548255743AAFBC118D041` |

## 2. Correção offline

`Bc250SdmaPresentRestartAfterTdr` agora executa, nesta ordem:

1. desliga polling/semáforo, trava F32 e desliga UTC L1;
2. pulsa somente `GRBM_SOFT_RESET.SOFT_RESET_SDMA0` por RMW, com readback assertado e
   desassertado;
3. zera RPTR/WPTR altos e baixos e reconstrói bases, writeback, polling, golden settings,
   UTCL1, doorbell, anel e IB;
4. exige readback integral por `Bc250TdrValidateSdmaRestartReadback`;
5. somente depois submete o `SYNC_NOP` com fence física sobre poison.

A janela MMIO passou a ter o contrato explícito `0x03000..0x08FFF`: `GRBM_SOFT_RESET`
fica em `0x08020`. Uma janela terminando em `0x04FFF` faria o helper rejeitar a escrita e
transformaria o reset em no-op.

Falha em qualquer readback mantém `HardwareEnabled=FALSE` e faz a DDI devolver
`STATUS_DEVICE_NOT_READY`; não há sucesso otimista.

## 3. Provas offline

- `driver\wdk\build-contract.bat`: **EXIT=0**;
- `tdr_recovery_contract_test`: cobre plano de reset integral, readback válido e negativos
  para RPTR residual 68, anel desligado, F32 haltado e reset ainda assertado;
- `audit_tdr_pair_has_backend`: **PASS** e agora exige janela MMIO, pulso assert/deassert,
  reconstrução, readback e ordem readback → prova;
- mutação da janela para `0x02000`: auditoria **FAIL**;
- mutação que remove o veredito de readback: auditoria produz **dois FAILs**;
- package audit: **PASS (INTEGRITY ONLY)**.

Tudo nesta seção é **NÃO VALIDADO EM HARDWARE**.

## 4. Pacote de ensaio `3.89.0.0` — NÃO INSTALADO

| Item | Valor |
|---|---|
| `BuildKind` | `MCDM_TDR_INDUCED_HANG` |
| `DriverVer` | `09/07/2026,3.89.0.0` |
| SYS fonte | `CC8B16154D03212A75907953F67DDB2BADC2E986C7C8E6B7917818420ECC85AE` |
| SYS assinado | `9573685EA5CFABA5C0E76A5F78199C46E7BE4E3DD18DA3E89D8AF5C84E1BB13C` |
| INF | `53F5900D6316F7ABE82A5B8B52BF3B08D5D5B8FF332F742DF41CF3D94B6D6815` |
| CAT | `0A765FFBF71E610E70F26DA24F0DA9C3BE5D300583A64E60D8FD10A66F4DF4F7` |
| INF empacotado | `TdrInduceHang=1` |

## 5. Análise do MEMORY.DMP

Executar em **PowerShell elevado**. O comando não apaga nem move o dump:

```powershell
$evidence = 'C:\Users\Daniel\Desktop\NamixWDDM\docs\evidence\2026-09-07_20-41-12-DESKTOP-HQAG6T4-mcdm-display-binding'
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'

& $cdb `
  -z 'C:\Windows\MEMORY.DMP' `
  -y 'srv*C:\Symbols*https://msdl.microsoft.com/download/symbols' `
  -c '.reload /f; !analyze -v; .bugcheck; lmvm bc250mcdm; ln fffff8024163d5b0; kv; q' |
  Tee-Object -FilePath (Join-Path $evidence 'memory-dmp-analysis-symbols-fixed.txt')

Copy-Item -LiteralPath 'C:\BC250_DISPLAY_LOG.txt' `
  -Destination (Join-Path $evidence 'kmd-live-after-bugcheck.txt') -Force

Get-FileHash -LiteralPath 'C:\Windows\MEMORY.DMP','C:\BC250_DISPLAY_LOG.txt' `
  -Algorithm SHA256 |
  Format-Table -AutoSize |
  Out-File (Join-Path $evidence 'post-bugcheck-sha256.txt') -Encoding utf8
```

## 6. Próxima corrida e critério

**Teto:** uma corrida com `3.89.0.0`, após analisar o dump e fazer boot frio/recuperação do
fusível. Não repetir o mesmo pacote.

O resultado útil exige a cadeia:

```text
TDR_HANG_INDUZIDO
TDR_QUIESCE
TDR_FULL_RESET E REPROGRAMACAO: READBACK PASS
TDR_RESTART PROVADO
nova submissão aceita
```

Qualquer `TDR_FULL_RESET FALHOU`, `TDR_REPROGRAM_READBACK FALHOU`, novo timeout, BugCheck
ou ausência da cadeia encerra a corrida para mineração. O item 25 e o comportamento do novo
reset permanecem **NÃO VALIDADOS EM HARDWARE**.
