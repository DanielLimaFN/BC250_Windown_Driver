<#
.SYNOPSIS
    Ensaio F4d - caracteriza a falha intermitente do PFP (PC em loop 0x17F5 vs assentado 0x284).

.DESCRIPTION
    Executa N ciclos consecutivos de StartDevice SEM reboot, extraindo a cada ciclo a
    assinatura do PFP e o resultado do CP/HQD direto de
    HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe.

    HIPOTESE SOB TESTE (doc STATUS_E_ROADMAP_CONSOLIDADO.md secao 2):
      O PFP degrada por estado residual acumulado DENTRO do mesmo boot. Nas 167 capturas
      historicas o PC final previu o resultado do CP com 100% de acuracia:

        PC=0x00000284 transitions=2      -> CP OK    (110/110, sem excecao)
        PC=0x0000041E transitions=1      -> CP FAIL  (44, modo antigo 27/08)
        PC~0x000017F5 transitions=26..48 -> CP FAIL  (7, intermitencia 28/08 18h+)
        PC=0x00000000 transitions=0      -> CP FAIL  (3, PFP nunca saiu do reset)

    O ensaio responde: em qual iteracao o PC migra de 0x284 para 0x17F5?

.PARAMETER Iterations
    Numero de ciclos StartDevice a executar. Padrao 20.

.PARAMETER Execute
    Obrigatorio para agir. Sem este switch o script roda em modo PREFLIGHT: valida
    precondicoes e NAO toca no dispositivo.

.PARAMETER IAcceptTheRisk
    Obrigatorio junto com -Execute. Cada iteracao faz Disable/Enable do dispositivo PnP
    1002:13FE. Se o bc250mcdm estiver servindo o display, a tela apaga a cada ciclo.

.PARAMETER StopOnFirstFailure
    Encerra no primeiro ciclo cujo PC final nao for 0x284. Util para capturar o estado
    exato da transicao sem sobrescrever com ciclos posteriores.

.PARAMETER SettleSeconds
    Segundos de espera apos Enable-PnpDevice antes de ler a telemetria. Padrao 8.

.PARAMETER OutFile
    CSV de saida. Padrao: docs\evidence\ensaio-pfp-<timestamp>.csv

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\ensaio-pfp-intermitencia.ps1
    Roda o preflight, sem tocar em nada.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\ensaio-pfp-intermitencia.ps1 -Iterations 30 -Execute -IAcceptTheRisk
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 500)]
    [int]$Iterations = 20,

    [switch]$Execute,

    [switch]$IAcceptTheRisk,

    [switch]$StopOnFirstFailure,

    [switch]$AllowDriverMismatch,

    [ValidateRange(1, 120)]
    [int]$SettleSeconds = 8,

    [string]$OutFile
)

$ErrorActionPreference = 'Stop'

$root       = Split-Path -Parent $PSScriptRoot
$probePath  = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$paramPath  = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$deviceGlob = 'PCI\VEN_1002&DEV_13FE*'

# Assinaturas conhecidas do PC final do PFP (secao 2.1 do STATUS consolidado).
$PC_SETTLED   = 0x00000284
$PC_LOOP_A    = 0x000017F5
$PC_LOOP_B    = 0x000017F6
$PC_STALL_OLD = 0x0000041E

function To-Hex32 {
    param($Value)
    if ($null -eq $Value) { return 'n/a' }
    return ('{0:X8}' -f [uint32]$Value)
}

function Get-PfpVerdict {
    param([uint32]$PcLast, [uint32]$Transitions)
    if ($PcLast -eq $PC_SETTLED)   { return 'OK-SETTLED' }
    if ($PcLast -eq $PC_LOOP_A -or $PcLast -eq $PC_LOOP_B) { return 'FAIL-LOOP' }
    if ($PcLast -eq $PC_STALL_OLD) { return 'FAIL-STALL-OLD' }
    if ($PcLast -eq 0)             { return 'FAIL-NO-START' }
    return 'FAIL-UNKNOWN'
}

function Get-Bc250Device {
    $devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -like $deviceGlob })
    if ($devices.Count -ne 1) {
        throw "esperado exatamente 1 dispositivo $deviceGlob, encontrado $($devices.Count)"
    }
    return $devices[0]
}

# ---------------------------------------------------------------- PREFLIGHT --

Write-Host ''
Write-Host '=== ENSAIO F4d: INTERMITENCIA DO PFP ===' -ForegroundColor Cyan
Write-Host ''

$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin   = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

$blockers = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()

if (-not $isAdmin) { $blockers.Add('nao esta elevado - Disable/Enable-PnpDevice exige Administrador') }

$device = $null
try {
    $device = Get-Bc250Device
    Write-Host ("  Dispositivo    : {0}" -f $device.PNPDeviceID)
    Write-Host ("  Servico        : {0}" -f $device.Service)
    Write-Host ("  ConfigMgrError : {0}" -f $device.ConfigManagerErrorCode)
    if ($device.Service -ne 'bc250mcdm') {
        $blockers.Add("dispositivo ligado a '$($device.Service)', nao a bc250mcdm - nada a ensaiar")
    }
} catch {
    $blockers.Add("dispositivo nao localizado: $($_.Exception.Message)")
}

$probe = $null
if (Test-Path -LiteralPath $probePath) {
    $probe = Get-ItemProperty -LiteralPath $probePath
    $schema = $probe.SchemaVersion
    Write-Host ("  SchemaVersion  : {0}" -f $schema)

    <#
      SkipHwBringup existe em DOIS lugares e eles significam coisas diferentes:

        Parameters\SkipHwBringup        TUNABLE - lido pelo driver via
                                        RTL_REGISTRY_SERVICES em
                                        Bc250ActiveReadTunable(). Decide o
                                        PROXIMO StartDevice. E' o que importa
                                        para autorizar o ensaio.

        Parameters\Probe\SkipHwBringup  ECHO - gravado pelo driver como
                                        telemetria durante o StartDevice.
                                        Descreve o boot ATUAL e so' muda quando
                                        o dispositivo reinicia.

      Gatear pelo eco e' errado nos dois sentidos: bloqueia ensaio valido logo
      apos alterar o tunable, e libera ensaio que mediria nada se o tunable
      tiver sido posto de volta em 1 sem reiniciar o dispositivo.
    #>
    $skipTunable = 1
    $tunableProp = Get-ItemProperty -LiteralPath $paramPath -Name SkipHwBringup -ErrorAction SilentlyContinue
    if ($null -ne $tunableProp) { $skipTunable = [uint32]$tunableProp.SkipHwBringup }

    $skipEcho = -1
    if ($null -ne $probe.PSObject.Properties['SkipHwBringup']) { $skipEcho = [int]$probe.SkipHwBringup }

    Write-Host ("  SkipHwBringup  : tunable={0} (proximo StartDevice) | echo={1} (boot atual)" -f $skipTunable, $skipEcho)

    if ($skipTunable -ne 0) {
        $blockers.Add("SkipHwBringup=$skipTunable em Parameters - o bring-up de hardware ficara DESLIGADO no proximo StartDevice; o ensaio mediria nada. Grave 0 nesse valor antes de executar.")
    }
    if ($skipEcho -ne -1 -and $skipEcho -ne $skipTunable) {
        $warnings.Add("tunable ($skipTunable) difere do echo ($skipEcho): a alteracao so' vale a partir do proximo StartDevice, que este ensaio provoca na primeira iteracao")
    }

    if ($null -ne $probe.PSObject.Properties['PfpDirectPcLast']) {
        $pcNow = [uint32]$probe.PfpDirectPcLast
        $trNow = 0
        if ($null -ne $probe.PSObject.Properties['PfpDirectPcTransitions']) {
            $trNow = [uint32]$probe.PfpDirectPcTransitions
        }
        Write-Host ("  PFP atual      : PC={0} transitions={1} -> {2}" -f (To-Hex32 $pcNow), $trNow, (Get-PfpVerdict $pcNow $trNow))
    } else {
        $warnings.Add('telemetria do PFP ainda nao populada nesta sessao')
    }
} else {
    $blockers.Add("chave de telemetria ausente: $probePath")
}

<#
  O ensaio exercita o driver INSTALADO em System32\drivers. Se o bind nao rodou
  - e o gate fail-closed bloqueia sempre que o host nao esta limpo - o build novo
  nao entrou, e o ensaio mediria o codigo antigo achando que mede o novo.

  COMPARAR CONTRA O QUE: nao contra driver\wdk\output\bc250mcdm.sys. Esse e' o
  binario ANTES da assinatura; build-bc250mcdm-package.ps1 assina o arquivo e o
  hash muda. O que e' instalado e' o pacote assinado, cujo SHA256 esta em
  output_active.manifest.json como PackageSysSha256 - a mesma fonte que
  audit-bc250mcdm-failclosed-binding.ps1 usa.

  Comparar com o build cru gera falso positivo: em 2026-08-29 o ensaio foi
  bloqueado logo apos um bind bem-sucedido, com o driver correto instalado.
#>
if (Test-Path -LiteralPath $driverPath) {
    $hInst = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
    Write-Host ("  SYS instalado  : {0}  {1}" -f $hInst.Substring(0,16), (Get-Item $driverPath).LastWriteTime)

    $hPackage = $null
    if (Test-Path -LiteralPath $manifestPath) {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if ($null -ne $manifest.PackageSysSha256) { $hPackage = $manifest.PackageSysSha256 }
    }

    if ($null -eq $hPackage) {
        $warnings.Add('manifesto do pacote ausente ou sem PackageSysSha256 - nao foi possivel confirmar qual driver sera ensaiado')
    } else {
        Write-Host ("  SYS do pacote  : {0}  (manifesto assinado)" -f $hPackage.Substring(0,16))
        if ($hInst -ne $hPackage) {
            if ($AllowDriverMismatch) {
                $warnings.Add('driver instalado DIVERGE do pacote assinado - prosseguindo por -AllowDriverMismatch; o ensaio mede o codigo INSTALADO')
            } else {
                $blockers.Add('driver instalado DIVERGE do pacote assinado: o ensaio mediria outro codigo. Rode stage + bind, ou passe -AllowDriverMismatch para ensaiar o instalado de proposito.')
            }
        }
    }
} else {
    $blockers.Add("bc250mcdm.sys ausente em $driverPath - nao ha driver instalado para ensaiar")
}

$recovery = Join-Path $root 'tools\BC250_RECOVERY.cmd'
if ((Test-Path -LiteralPath $recovery) -or (Test-Path -LiteralPath 'C:\BC250_RECOVERY.cmd')) {
    Write-Host '  Recovery       : disponivel'
} else {
    $warnings.Add('BC250_RECOVERY.cmd nao localizado - sem caminho de recuperacao rapida se a tela apagar')
}

Write-Host ''
foreach ($w in $warnings) { Write-Host ("  AVISO   : {0}" -f $w) -ForegroundColor Yellow }
foreach ($b in $blockers) { Write-Host ("  BLOQUEIO: {0}" -f $b) -ForegroundColor Red }

if ($blockers.Count -gt 0) {
    Write-Host ''
    Write-Host 'PREFLIGHT REPROVADO - nada foi tocado.' -ForegroundColor Red
    exit 1
}

if (-not $Execute) {
    Write-Host ''
    Write-Host 'PREFLIGHT APROVADO. Nada foi tocado (modo dry-run).' -ForegroundColor Green
    Write-Host ''
    Write-Host 'Para executar de fato:'
    Write-Host ("  powershell -ExecutionPolicy Bypass -File tools\ensaio-pfp-intermitencia.ps1 -Iterations {0} -Execute -IAcceptTheRisk" -f $Iterations)
    exit 0
}

if (-not $IAcceptTheRisk) {
    Write-Host ''
    Write-Host '-Execute exige tambem -IAcceptTheRisk.' -ForegroundColor Red
    Write-Host 'Cada iteracao faz Disable/Enable do dispositivo 1002:13FE.'
    Write-Host 'Se o bc250mcdm estiver servindo o display, a tela apaga a cada ciclo.'
    exit 1
}

# ------------------------------------------------------------------ ENSAIO --

if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $stamp   = (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss')
    $evidence = Join-Path $root 'docs\evidence'
    if (-not (Test-Path -LiteralPath $evidence)) {
        New-Item -ItemType Directory -Force -Path $evidence | Out-Null
    }
    $OutFile = Join-Path $evidence ("ensaio-pfp-{0}.csv" -f $stamp)
}

Write-Host ''
Write-Host ("Executando {0} ciclos de StartDevice sem reboot." -f $Iterations) -ForegroundColor Cyan
Write-Host ("Saida: {0}" -f $OutFile)
Write-Host ''
Write-Host 'iter  status  PC-final  trans  RPTR/WPTR   fence-CP            HQD  veredito'
Write-Host '----  ------  --------  -----  ----------  ------------------  ---  ------------------'

$rows       = [System.Collections.Generic.List[object]]::new()
$firstFail  = 0
$instanceId = $device.PNPDeviceID

for ($i = 1; $i -le $Iterations; $i++) {

    $cycleError = ''
    try {
        Disable-PnpDevice -InstanceId $instanceId -Confirm:$false -ErrorAction Stop
        Start-Sleep -Seconds 2
        Enable-PnpDevice  -InstanceId $instanceId -Confirm:$false -ErrorAction Stop
    } catch {
        $cycleError = $_.Exception.Message
    }

    Start-Sleep -Seconds $SettleSeconds

    $p = $null
    if (Test-Path -LiteralPath $probePath) { $p = Get-ItemProperty -LiteralPath $probePath }

    if ($null -eq $p) {
        Write-Host ("{0,4}  --      --        --     --          --                  --   TELEMETRIA AUSENTE" -f $i) -ForegroundColor Red
        continue
    }

    $pfpStatus = -1
    $pcLast    = 0
    $trans     = 0
    $rptr      = 0
    $wptr      = 0
    $fence     = 0
    $hqdTriad  = 0
    $hqdStatus = -1

    if ($null -ne $p.PSObject.Properties['PfpDirectStatus'])         { $pfpStatus = [int]$p.PfpDirectStatus }
    if ($null -ne $p.PSObject.Properties['PfpDirectPcLast'])         { $pcLast    = [uint32]$p.PfpDirectPcLast }
    if ($null -ne $p.PSObject.Properties['PfpDirectPcTransitions'])  { $trans     = [uint32]$p.PfpDirectPcTransitions }
    if ($null -ne $p.PSObject.Properties['PfpDirectRb0RptrAfter'])   { $rptr      = [uint32]$p.PfpDirectRb0RptrAfter }
    if ($null -ne $p.PSObject.Properties['PfpDirectRb0WptrApplied']) { $wptr      = [uint32]$p.PfpDirectRb0WptrApplied }
    if ($null -ne $p.PSObject.Properties['PfpDirectRb0FenceAfter'])  { $fence     = [uint32]$p.PfpDirectRb0FenceAfter }
    if ($null -ne $p.PSObject.Properties['HqdActiveTriadPassed'])    { $hqdTriad  = [uint32]$p.HqdActiveTriadPassed }
    if ($null -ne $p.PSObject.Properties['HqdActiveStatus'])         { $hqdStatus = [int]$p.HqdActiveStatus }

    $verdict = Get-PfpVerdict $pcLast $trans

    # Consistencia da hipotese: PC=0x284 <=> PfpDirectStatus=0. Divergir aqui e' um achado.
    $predicted = ($verdict -eq 'OK-SETTLED')
    $actual    = ($pfpStatus -eq 0)
    if ($predicted -ne $actual) {
        $verdict = "$verdict !DIVERGE(status=$pfpStatus)"
    }

    $color = 'Green'
    if (-not $actual) { $color = 'Red' }

    Write-Host ("{0,4}  {1,6}  {2}  {3,5}  {4,4}/{5,-5}  {6}  {7,3}  {8}" -f `
        $i, $pfpStatus, (To-Hex32 $pcLast), $trans, $rptr, $wptr, (To-Hex32 $fence), $hqdTriad, $verdict) -ForegroundColor $color

    $rows.Add([pscustomobject]@{
        Iteration      = $i
        PfpDirectStatus= $pfpStatus
        PfpPcLast      = (To-Hex32 $pcLast)
        PfpPcTransitions = $trans
        Rb0RptrAfter   = $rptr
        Rb0WptrApplied = $wptr
        Rb0FenceAfter  = (To-Hex32 $fence)
        HqdActiveStatus= $hqdStatus
        HqdTriadPassed = $hqdTriad
        Verdict        = $verdict
        CycleError     = $cycleError
    })

    if ((-not $actual) -and $firstFail -eq 0) { $firstFail = $i }
    if ($StopOnFirstFailure -and (-not $actual)) {
        Write-Host ''
        Write-Host ("Parado na primeira falha (iteracao {0}), conforme -StopOnFirstFailure." -f $i) -ForegroundColor Yellow
        break
    }
}

$rows | Export-Csv -LiteralPath $OutFile -NoTypeInformation -Encoding UTF8

# ------------------------------------------------------------------ RESUMO --

$total    = $rows.Count
$okCount  = @($rows | Where-Object { $_.PfpDirectStatus -eq 0 }).Count
$loop     = @($rows | Where-Object { $_.Verdict -like 'FAIL-LOOP*' }).Count
$diverge  = @($rows | Where-Object { $_.Verdict -like '*!DIVERGE*' }).Count

Write-Host ''
Write-Host '=== RESUMO ===' -ForegroundColor Cyan
Write-Host ("  Ciclos executados     : {0}" -f $total)
Write-Host ("  Sucessos (status=0)   : {0}" -f $okCount)
Write-Host ("  Falhas modo LOOP      : {0}" -f $loop)
if ($firstFail -gt 0) {
    Write-Host ("  Primeira falha em     : iteracao {0}" -f $firstFail) -ForegroundColor Yellow
} else {
    Write-Host '  Primeira falha em     : nenhuma' -ForegroundColor Green
}
Write-Host ("  CSV                   : {0}" -f $OutFile)

Write-Host ''
if ($diverge -gt 0) {
    Write-Host ("ACHADO: {0} ciclo(s) em que o PC final NAO previu o status." -f $diverge) -ForegroundColor Magenta
    Write-Host 'A regra 110/110 do corpus historico foi quebrada - vale investigar antes de seguir.'
} elseif ($firstFail -gt 0 -and $loop -gt 0) {
    Write-Host 'HIPOTESE SUSTENTADA: o modo LOOP (PC~0x17F5) reproduziu sem reboot.' -ForegroundColor Green
    Write-Host ("Proximo passo: reboot e repetir para confirmar que o reboot recupera (iteracao de falha: {0})." -f $firstFail)
} elseif ($firstFail -eq 0) {
    Write-Host 'HIPOTESE NAO REPRODUZIDA neste numero de ciclos.' -ForegroundColor Yellow
    Write-Host 'Aumente -Iterations, ou a degradacao depende de tempo/carga e nao de contagem de ciclos.'
} else {
    Write-Host 'Falhas observadas, mas nao no modo LOOP. Ver coluna Verdict no CSV.' -ForegroundColor Yellow
}
Write-Host ''
