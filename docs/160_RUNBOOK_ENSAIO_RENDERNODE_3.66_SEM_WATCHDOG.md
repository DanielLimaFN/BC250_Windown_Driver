# Runbook — Ensaio diagnóstico RenderNode 3.66 / schema 129 (SEM WATCHDOG)

> **Objetivo único desta corrida: capturar a telemetria causal do `StopDevice`.**
> Não é uma tentativa de manter o RenderNode em Código 0. Não altera UMD nem INF.
> Uma variável por corrida: só `RenderNode` 0 → 1.
>
> **Veredito de entrada e de saída até prova em contrário:**
> `NÃO VALIDADO EM HARDWARE PARA RENDER WDDM`.

## 0. Estado de partida verificado (01/09/2026, boot 10:15:25)

| Item | Valor |
|---|---|
| Dispositivo | `PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\4&19CAF403&0&0041` |
| Serviço / código | `bc250mcdm` / `ConfigManagerErrorCode=0` / `Status=OK` |
| SYS instalado | `A546F8BA2A26ECDB4BDE13FC23C4EAB401EEC7B7E49D76CF56A1A02D0E60140E` (== `PackageSysSha256` do 3.66) |
| INF empacotado | `B624F23E0E4664B9087E52D60A8AE8E72A23A2CFD05D724849C3B5DA0326DE01` (== manifesto) |
| CAT vigente | `CC8EAEBC38D9FE470F9AB2B2009C31506A56D810170197CAE3DF39A6EABE4C6C` (== manifesto) |
| `C:\BC250_RECOVERY.cmd` | `B61342F920B60C94C083D5B7FA14B56238A683245C1CD5D5BC264C8167A026D7` (== `RecoverySha256`) |
| Probe | `SchemaVersion=129`, `V5RegistrationMode=0`, `V5StartDeviceStatus=0`, `V5RenderNodeRequested=0` |
| Silício | `HqdActiveStatus=0`, `HqdActivePhysicalReady=1`, `ShaderPhysicalReady=1`, `P1EngineState=1`, `P1SelfTestPhysicalReady=1`, `P2PhysicalReady=1` |
| Params | `RenderNode=0`, `SkipHwBringup=0`, `ShaderDispatch=1`, `PersistentSubmission=1`, `GpuvaLabPromotion=0` |
| Reboot guard | ausente |
| `C:\BC250_REQUIRE_CLEAN_BOOT.json` | ausente |
| Auditorias offline | `audit-mcdm-ddi-matrix.py` PASS (structural only); `audit-bc250mcdm-safety.py` PASS |
| Dry-run da promoção | `BC250 RENDERNODE PROMOTION PREFLIGHT: PASS` |

Campos `StopDeviceStatus` e `V5StopFence*` estão **ABSENT** — correto, `StopDevice` não
rodou neste boot. É exatamente essa lacuna que a corrida vai preencher.

## 1. ANTES DO REBOOT — anotar em papel / celular

O ensaio roda **sem watchdog**. Não há rollback automático. Se a tela cair, a
recuperação é 100% manual e precisa ser conhecida de cor:

1. Reiniciar a máquina (botão físico se necessário).
2. Se o Windows subir com tela: abrir **Prompt de Comando como Administrador**.
3. Executar:
   ```
   C:\BC250_RECOVERY.cmd
   ```
4. O script desabilita a instância PCI exata, remove o pacote de identidade
   BC-250 estrita, exclui os serviços experimentais, apaga os binários, reabilita
   o dispositivo e exige `BasicDisplay` Código 0 por CIM.
5. Se ele pedir reboot, reiniciar e reexecutar.
6. Se o Windows **não** subir com tela: reiniciar e usar Modo de Segurança
   (F8 / três falhas de boot consecutivas → Recuperação → Inicialização →
   Modo de Segurança), depois executar o mesmo `C:\BC250_RECOVERY.cmd`.

O recovery elevado já foi testado e comprovou remoção de `.sys` órfão e retorno a
`BasicDisplay` Código 0. Mesmo assim: **não presumir acesso remoto.**

## 2. PASSO 1 — promover RenderNode (PowerShell ELEVADO)

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\prepare-bc250mcdm-rendernode.ps1" -Execute -IAcceptDisplayRenderBootRisk
```

Saída esperada:

```
BC250 RENDERNODE PROMOTION PREFLIGHT: PASS
BC250 RENDERNODE PROMOTION: PREPARED
  RenderNode=1 aplicado e relido; ainda nao consumido pelo DriverEntry.
  evidence: ...docs\evidence\<stamp>-DESKTOP-HQAG6T4-rendernode-promotion
NAO VALIDADO EM HARDWARE
```

O script grava `RenderNode=1`, **relê** e publica `rendernode-promotion.json`. Se o
readback divergir, ele restaura o valor anterior sozinho e falha fechado.

**Se este passo falhar, PARE.** Não reiniciar. Reportar a mensagem.

## 3. PASSO 2 — reboot normal, sem guard

Reiniciar o Windows normalmente. Não armar `BC250-RebootGuard`. Não usar
`arm-bc250mcdm-reboot-watchdog.ps1`.

`RenderNode=1` só é consumido pelo `DriverEntry` neste boot: é aqui que o driver
troca `DxgkInitializeDisplayOnlyDriver` por `DxgkInitialize` (WDDM 2.7 completo).

## 4. PASSO 3 — capturar IMEDIATAMENTE após o boot

Assim que houver sessão utilizável, **antes de qualquer outra coisa**:

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Daniel\Desktop\NamixWDDM\tools\inspect-bc250mcdm-rendernode.ps1"
```

> **Cuidado crítico de evidência:** o carregamento zera o rastro V5 antes do
> registro. Se o Windows repetir `StartDevice`, ou se o dispositivo for
> desabilitado/reabilitado, ou se o recovery rodar antes da captura, o rastro da
> **primeira** falha é sobrescrito e a corrida é perdida.
> **Não desabilitar o dispositivo, não rodar o recovery e não reiniciar antes de
> executar o inspect.** Capturar primeiro, recuperar depois.

O inspect publica `rendernode-inspect.json` em
`docs\evidence\<stamp>-DESKTOP-HQAG6T4-rendernode-inspect` **em qualquer
resultado**, inclusive Código 43 (nesse caso sai com código 2 e grava também
`failures.txt`). A evidência é preservada nos dois desfechos.

Se o Código 43 impedir o desktop, subir em Modo de Segurança e rodar o inspect de
lá — a chave `Parameters\Probe` sobrevive ao teardown, então a telemetria continua
legível.

## 5. Campos a interpretar (e só depois disso, corrigir)

Ordem de leitura no `rendernode-inspect.json`:

**a) O registro V5 aconteceu?**
`V5RenderNodeRequested`, `V5RenderNodeTunablePresent`, `V5RegistrationMode`,
`DxgkInitializeStatus`, `AddDeviceStatus`, `StartDeviceEntered`,
`V5StartDeviceStatus`, `V5StartDeviceSources`, `V5StartDeviceChildren`.

**b) Quem pediu o teardown e depois de quê?**
`V5LastDdi`, `V5PrevDdi`, `V5Prev2Ddi`, `V5DdiMarks`,
`V5QiCalls`, `V5QiLastGuidD1`, `V5QiPrevGuidD1`, `V5QiPrev2GuidD1`,
`V5QaiCalls`, `V5QaiLastType`, `V5QaiLastStatus`, `V5QaiLastUnsupportedType`,
`V5QaiUnsupportedCount`.

**c) O `DEVICE_BUSY` é contagem real ou falha de consulta?** — o alvo do schema 129:

| `V5StopFenceQueryMode` | Significado |
|---|---|
| `1` | `DxgkCbSynchronizeExecution` **indisponível** → leitura direta; `SyncStatus` = `STATUS_NOT_SUPPORTED` |
| `2` | consulta serializada tentada |

Com `QueryMode=2`, o veredito sai de:

| Observação | Caso |
|---|---|
| `SyncStatus=0`, `SyncResult=1`, `PendingCount>0` ou `ReservedCount>0` | **contagem de fences realmente pendente** |
| `SyncStatus=0`, `SyncResult=1`, ambas as contagens `0` | fences limpas — o `BUSY` veio de outro lugar; reexaminar `StopDeviceStatus` |
| `SyncStatus != 0` | **falha em `DxgkCbSynchronizeExecution`** → fail-closed correto, causa fora do tracker |
| `SyncStatus=0`, `SyncResult=0` | **callback executado com retorno falso** |
| `ReservedCount>0` com `PendingCount=0` | **reserva WDDM não cancelada** |

**d) Desfecho do PnP:** `StopDeviceEntered`, `StopDeviceStatus`,
`RemoveDeviceEntered`, `RemoveDeviceStatus`.

**e) Silício preservado?** `HqdActiveStatus`, `HqdActivePhysicalReady`,
`ShaderPhysicalReady`, `P1EngineState`, `P1SelfTestPhysicalReady`,
`P2PhysicalReady`, `DmmCommitStatus`, `DmmPresentStatus`, `DmmVsyncCount`.

## 6. Depois da captura — PARAR

Regra da corrida:

- **Não** relaxar o fail-closed de `Bc250ActiveFenceTrackerHasPending`
  (`driver/wdk/bc250_active_miniport.c:4437`) antes de interpretar os campos.
- **Não** implementar stub de UMD D3D, nem qualquer DDI que retorne sucesso
  fictício para manter o adaptador vivo.
- **Não** editar `driver/wdk/package/bc250mcdm.inf` esperando efeito: o artefato
  efetivo é `driver/wdk/package/output_lab/bc250mcdm.inf`, gerado por
  `tools/package-bc250mcdm-lab.ps1`.
- A ausência de UMD D3D permanece classificada como **bloqueio de aplicações**,
  não como causa comprovada do Código 43.

Recuperar (`C:\BC250_RECOVERY.cmd`), confirmar `BasicDisplay` Código 0, boot frio,
e só então propor o candidato 3.67 com a correção mínima da causa comprovada mais
o teste offline que a reproduza.

## 7. Desfechos possíveis

| Desfecho | Ação |
|---|---|
| `RENDERNODE V5/START/P1/P2: VALIDADO EM HARDWARE` (exit 0) | RenderNode sobreviveu. Seguir para o harness `D3DKMT` — sem UMD D3D, registrando qual thunk foi aceito e qual callback KMD foi alcançado. |
| `RENDERNODE: FAIL-CLOSED` (exit 2) com evidência completa | Corrida bem-sucedida como **diagnóstico**. Interpretar pela seção 5, recuperar, boot frio, propor 3.67. |
| Sem tela / sem boot | Recuperação manual da seção 1. Capturar o inspect em Modo de Segurança **antes** do recovery, se possível. |

Em nenhum desses desfechos o veredito muda sem fence física originada por
submissão WDDM: **NÃO VALIDADO EM HARDWARE PARA RENDER WDDM**.
