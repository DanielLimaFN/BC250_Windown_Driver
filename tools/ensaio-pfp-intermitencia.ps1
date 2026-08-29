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

    # Engine de invalidacao de GCVM usada pelo carregador do CP. -1 mantem o que
    # ja' estiver no registro. 0 e' o Display Engine: escolher 0 e' bisseccao
    # deliberada e derruba o scanout - so' com o watchdog armado.
    [ValidateRange(-1, 17)]
    [int]$GcvmInvalidateEngine = -1,

    # Protocolo de semaforo do upstream em volta da invalidacao. -1 mantem o que
    # ja' estiver no registro. O patch 0001 de referencias/bc250-gfx1013-fix
    # forca use_semaphore no caminho GFXHUB do gfx1013.
    [ValidateRange(-1, 1)]
    [int]$GcvmInvalidateSemaphore = -1,

    # Blocos que existiam em 4df3484 e foram removidos no trabalho de
    # preservacao do display. -1 mantem o valor que ja' estiver no registro.
    #
    # MEDIDO EM 18:42 de 29/08: so' o CpSoftReset e' necessario. Com
    # Vmid0PageTable=0 e CpSoftReset=1 o CP assenta em 0x284 e fecha com fence.
    # Vmid0PageTable=1 produz XNACK na UTCL1 do SDMA e timeout do anel, que
    # bloqueia o proprio carregador do CP. NAO LIGAR sem resolver a cobertura
    # do VMID 0.
    [ValidateRange(-1, 1)]
    [int]$CpSoftReset = -1,

    [ValidateRange(-1, 1)]
    [int]$Vmid0PageTable = -1,

    # AUTO_CTXSW_ENABLE do SDMA0_CNTL antes do kick do anel. 1 = comportamento
    # atual. O soak de 18:56 mostrou EX_IDLE_POLL_TIMER_EXPIRE aceso em 3/3 dos
    # ciclos que deram timeout e limpo em 37/37 dos bons.
    [ValidateRange(-1, 1)]
    [int]$SdmaAutoCtxSw = -1,

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
$PC_LOOP_A    = 0x000017F4
$PC_LOOP_B    = 0x000017F5
$PC_LOOP_C    = 0x000017F6
$PC_STALL_OLD = 0x0000041E
# Loop de fetch de instrucoes do PFP (doc 95). Distinto do loop alto 0x17Fx.
$PC_FETCH_LO  = 0x00000218
$PC_FETCH_HI  = 0x0000021B
# Estado herdado que 171/171 capturas validas do corpus tinham antes do direct
# load: PFP|ME|CE em HALT. Qualquer outro valor significa CP ja' em execucao.
$ME_CNTL_HALTED = 0x15000000

# Get-ItemProperty devolve REG_DWORD como Int32, entao qualquer valor com o bit
# 31 aceso chega negativo. [uint32] sobre negativo LANCA em PowerShell — e o
# literal 0xDEADC000 tambem e' Int32 negativo, o que derrubou a corrida de 19:59.
# Normaliza pelo Int64 antes de estreitar.
function To-U32 {
    param($Value)
    if ($null -eq $Value) { return [uint32]0 }
    return [uint32]([int64]$Value -band 0xFFFFFFFFL)
}

function To-Hex32 {
    param($Value)
    if ($null -eq $Value) { return 'n/a' }
    return ('{0:X8}' -f [uint32]$Value)
}

function Get-PfpVerdict {
    param([uint32]$PcLast, [uint32]$Transitions, [uint32]$Samples)
    if ($PcLast -eq $PC_SETTLED)   { return 'OK-SETTLED' }
    # A assinatura do PC so' significa alguma coisa quando o laco de amostragem
    # roda ate' o fim. Com o anel sendo consumido, ele sai na 2a iteracao e o PC
    # e' apenas onde o PFP estava passando - nao um destino. As 110/110 do corpus
    # foram medidas com o laco esgotando 100 amostras, o que nao acontece mais.
    if ($Samples -gt 0 -and $Samples -lt 8) { return 'OK-CONSUMIU-CEDO' }
    if ($PcLast -eq $PC_LOOP_A -or $PcLast -eq $PC_LOOP_B -or $PcLast -eq $PC_LOOP_C) {
        # Mesmo PC, dois modos de falha distintos: com muitas amostras e no maximo
        # uma transicao, o microengine nao esta em loop -- esta parado.
        if ($Samples -ge 8 -and $Transitions -le 1) { return 'FAIL-FROZEN' }
        return 'FAIL-LOOP'
    }
    if ($PcLast -ge $PC_FETCH_LO -and $PcLast -le $PC_FETCH_HI) { return 'FAIL-FETCH-LOOP' }
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

# Um soak de 40 ciclos leva ~9 min e nao cabe numa janela de watchdog de 600 s.
# Em 29/08 18:30 um watchdog disparou no meio da corrida, desvinculou o driver e
# o ciclo 10 saiu com todos os campos zerados - o que se parece com regressao de
# silicio e nao era. Bloquear antes de comecar.
$liveWatchdogs = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*watch-bc250mcdm-lab.ps1*' })
if ($liveWatchdogs.Count -gt 0) {
    Write-Host ''
    Write-Host 'BLOQUEIO: existe watchdog de laboratorio armado.' -ForegroundColor Red
    foreach ($w in $liveWatchdogs) { Write-Host ("  PID {0}" -f $w.ProcessId) -ForegroundColor Red }
    Write-Host 'Ele reverte o binding no meio do ensaio e invalida a corrida inteira.' -ForegroundColor Yellow
    Write-Host 'Confirme o binding primeiro (confirm-bc250mcdm-lab.ps1) ou encerre o processo.' -ForegroundColor Yellow
    exit 1
}

Write-Host ''
Write-Host ("Executando {0} ciclos de StartDevice sem reboot." -f $Iterations) -ForegroundColor Cyan
Write-Host ("Saida: {0}" -f $OutFile)
Write-Host ''
Write-Host 'iter  status  PC-final  trans/amostras  RPTR/WPTR   fence-CP            HQD  IC inv/prime  ME_CNTL/PC antes    veredito'
Write-Host '----  ------  --------  --------------  ----------  ------------------  ---  ------------  ------------------  ------------------'

# Escreve os tunables e confere o eco. E' chamada duas vezes: uma antes do
# laco e outra dentro dele, entre o Disable e o Enable, para que o valor esteja
# no lugar quando o StartDevice o ler.
function Set-Bc250Tunables {
    param([switch]$Quiet)
    $tk = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
    if (-not (Test-Path -LiteralPath $tk)) { New-Item -Path $tk -Force | Out-Null }
    $applied = @()
    foreach ($tun in @(
            @{ Name = 'GcvmInvalidateEngine';    Value = $GcvmInvalidateEngine },
            @{ Name = 'GcvmInvalidateSemaphore'; Value = $GcvmInvalidateSemaphore },
            @{ Name = 'CpSoftReset';             Value = $CpSoftReset },
            @{ Name = 'Vmid0PageTable';          Value = $Vmid0PageTable },
            @{ Name = 'SdmaAutoCtxSw';           Value = $SdmaAutoCtxSw })) {
        if ($tun.Value -lt 0) { continue }
        Set-ItemProperty -Path $tk -Name $tun.Name -Value $tun.Value -Type DWord -Force
        $e = (Get-ItemProperty -LiteralPath $tk).PSObject.Properties[$tun.Name]
        if ($null -eq $e -or [int]$e.Value -ne $tun.Value) {
            throw "$($tun.Name) nao persistiu no registro. Sessao elevada?"
        }
        $applied += ("{0}={1}" -f $tun.Name, $tun.Value)
    }
    if ($applied.Count -gt 0 -and -not $Quiet) {
        Write-Host ("Tunables gravados e conferidos: {0}" -f ($applied -join '  ')) -ForegroundColor Cyan
        if ($CpSoftReset -eq 1 -or $Vmid0PageTable -eq 1 -or $GcvmInvalidateEngine -eq 0) {
            Write-Host 'Estes valores tocam o VMID 0 / resetam o CP: espere tela preta. Watchdog obrigatorio.' -ForegroundColor Red
        }
        Write-Host ''
    }
}

Set-Bc250Tunables

$rows       = [System.Collections.Generic.List[object]]::new()
$firstFail  = 0
$instanceId = $device.PNPDeviceID

for ($i = 1; $i -le $Iterations; $i++) {

    $cycleError = ''
    try {
        Disable-PnpDevice -InstanceId $instanceId -Confirm:$false -ErrorAction Stop
        Start-Sleep -Seconds 2
        # Reescreve com o dispositivo fora: e' a unica janela em que o valor
        # sobrevive ate' o StartDevice le-lo.
        Set-Bc250Tunables -Quiet
        Enable-PnpDevice  -InstanceId $instanceId -Confirm:$false -ErrorAction Stop
    } catch {
        $cycleError = $_.Exception.Message
    }

    Start-Sleep -Seconds $SettleSeconds

    # Se o dispositivo deixou de estar em bc250mcdm no meio da corrida, os campos
    # do probe somem e o ciclo sai todo zerado. Isso nao e' falha de silicio: e'
    # ausencia de instrumento. Abortar em vez de registrar uma linha falsa.
    $svcNow = (Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -eq $instanceId }).Service
    if ($svcNow -ne 'bc250mcdm' -or -not (Test-Path -LiteralPath $probePath)) {
        Write-Host ''
        Write-Host ("ENSAIO INVALIDADO na iteracao {0}: dispositivo em '{1}', telemetria {2}." -f `
            $i, $svcNow, $(if (Test-Path -LiteralPath $probePath) { 'presente' } else { 'ausente' })) -ForegroundColor Red
        Write-Host 'O binding caiu durante a corrida (watchdog, rollback ou PnP).' -ForegroundColor Yellow
        Write-Host ("Os {0} ciclos anteriores continuam validos; este e os seguintes nao existem." -f ($i - 1)) -ForegroundColor Yellow
        break
    }

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
    $samples   = 0
    $icInvEntry  = -1
    $icInvUnhalt = -1
    $icPrime     = -1
    $meCntlBefore = -1
    $pcBefore     = 0
    $jtPfp = -1; $jtCe = -1; $jtMec = -1
    $invEng = -1; $invAck1 = -1; $invPolls1 = -1; $invAck2 = -1; $invPolls2 = -1
    $invPre1 = -1; $invPre2 = -1
    $invSem = -1; $invRaw1 = -1; $invRaw2 = -1; $invAcq1 = -1; $invAcq2 = -1
    $srApplied = -1; $ptApplied = -1; $ptAck = -1
    $tunMask = -1
    $cmpDw = -1; $mism = -1; $destPolls = -1; $destObs = -1
    $sdmaStatus = -1; $sdmaPolls = -1; $sdmaWptr = -1; $sdmaRptr = -1
    $elB = -1; $elA = -1; $rxB = -1; $rxA = -1; $wxB = -1; $wxA = -1
    # Estado herdado pelo SDMA. Foi assim que a causa do CP apareceu: comparando
    # o que ele herdava nos ciclos bons contra os ruins. Gravado em TODOS os
    # ciclos, porque o registro so' guarda o ultimo e o diferencial exige os dois.
    $sdStatB = -1; $sdRptrB = -1; $sdF32Ctl = -1; $sdF32F32 = -1
    $sdExIdle = -1; $sdCntlB = -1; $sdCntlA = -1
    $sdF32Pc = -1; $sdF32Tr = -1; $sdRbCntl = -1; $sdHdr = -1
    $trace = @()

    if ($null -ne $p.PSObject.Properties['PfpDirectStatus'])         { $pfpStatus = [int]$p.PfpDirectStatus }
    if ($null -ne $p.PSObject.Properties['PfpDirectPcLast'])         { $pcLast    = [uint32]$p.PfpDirectPcLast }
    if ($null -ne $p.PSObject.Properties['PfpDirectPcTransitions'])  { $trans     = [uint32]$p.PfpDirectPcTransitions }
    if ($null -ne $p.PSObject.Properties['PfpDirectRb0RptrAfter'])   { $rptr      = [uint32]$p.PfpDirectRb0RptrAfter }
    if ($null -ne $p.PSObject.Properties['PfpDirectRb0WptrApplied']) { $wptr      = [uint32]$p.PfpDirectRb0WptrApplied }
    if ($null -ne $p.PSObject.Properties['PfpDirectRb0FenceAfter'])  { $fence     = To-U32 $p.PfpDirectRb0FenceAfter }
    if ($null -ne $p.PSObject.Properties['HqdActiveTriadPassed'])    { $hqdTriad  = [uint32]$p.HqdActiveTriadPassed }
    if ($null -ne $p.PSObject.Properties['HqdActiveStatus'])         { $hqdStatus = [int]$p.HqdActiveStatus }
    if ($null -ne $p.PSObject.Properties['PfpDirectPcSamples'])      { $samples   = [uint32]$p.PfpDirectPcSamples }
    if ($null -ne $p.PSObject.Properties['PfpDirectIcInvEntryMask']) { $icInvEntry  = [int]$p.PfpDirectIcInvEntryMask }
    if ($null -ne $p.PSObject.Properties['PfpDirectIcInvUnhaltMask']){ $icInvUnhalt = [int]$p.PfpDirectIcInvUnhaltMask }
    if ($null -ne $p.PSObject.Properties['PfpDirectIcPrimeUnhaltMask']) { $icPrime  = [int]$p.PfpDirectIcPrimeUnhaltMask }
    if ($null -ne $p.PSObject.Properties['PfpDirectMeCntlBefore'])   { $meCntlBefore = To-U32 $p.PfpDirectMeCntlBefore }
    if ($null -ne $p.PSObject.Properties['PfpDirectPcBefore'])       { $pcBefore     = [uint32]$p.PfpDirectPcBefore }
    if ($null -ne $p.PSObject.Properties['PfpDirectJtStatPfp'])      { $jtPfp = [int]$p.PfpDirectJtStatPfp }
    if ($null -ne $p.PSObject.Properties['PfpDirectJtStatCe'])       { $jtCe  = [int]$p.PfpDirectJtStatCe }
    if ($null -ne $p.PSObject.Properties['PfpDirectJtStatMec'])      { $jtMec = [int]$p.PfpDirectJtStatMec }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvEngine'])      { $invEng = [int]$p.PfpDirectInvEngine }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvAckStaging'])  { $invAck1 = [int]$p.PfpDirectInvAckStaging }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvAckBeforeStaging']) { $invPre1 = [int]$p.PfpDirectInvAckBeforeStaging }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvAckBeforePreWptr']) { $invPre2 = [int]$p.PfpDirectInvAckBeforePreWptr }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvUseSemaphore'])  { $invSem  = [int]$p.PfpDirectInvUseSemaphore }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvAckRawStaging']) { $invRaw1 = To-U32 $p.PfpDirectInvAckRawStaging }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvAckRawPreWptr']) { $invRaw2 = To-U32 $p.PfpDirectInvAckRawPreWptr }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvSemAcqStaging']) { $invAcq1 = [int]$p.PfpDirectInvSemAcqStaging }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvSemAcqPreWptr']) { $invAcq2 = [int]$p.PfpDirectInvSemAcqPreWptr }
    if ($null -ne $p.PSObject.Properties['PfpDirectCpSoftReset'])      { $srApplied = [int]$p.PfpDirectCpSoftReset }
    if ($null -ne $p.PSObject.Properties['PfpDirectVmid0PageTable'])   { $ptApplied = [int]$p.PfpDirectVmid0PageTable }
    if ($null -ne $p.PSObject.Properties['PfpDirectVmid0InvAck'])      { $ptAck = [int]$p.PfpDirectVmid0InvAck }
    if ($null -ne $p.PSObject.Properties['TunableReadOkMask'])         { $tunMask = [int]$p.TunableReadOkMask }
    if ($null -ne $p.PSObject.Properties['PfpDirectComparedDwords'])   { $cmpDw = [int]$p.PfpDirectComparedDwords }
    if ($null -ne $p.PSObject.Properties['PfpDirectMismatches'])       { $mism = [int]$p.PfpDirectMismatches }
    if ($null -ne $p.PSObject.Properties['PfpDirectDestPolls'])        { $destPolls = [int]$p.PfpDirectDestPolls }
    if ($null -ne $p.PSObject.Properties['PfpDirectDestObserved'])     { $destObs = To-U32 $p.PfpDirectDestObserved }
    if ($null -ne $p.PSObject.Properties['SdmaRingWorkStatus'])        { $sdmaStatus = [int]$p.SdmaRingWorkStatus }
    if ($null -ne $p.PSObject.Properties['SdmaRingPolls'])             { $sdmaPolls = [int]$p.SdmaRingPolls }
    if ($null -ne $p.PSObject.Properties['SdmaRingWptr'])              { $sdmaWptr = [int]$p.SdmaRingWptr }
    if ($null -ne $p.PSObject.Properties['SdmaRingRptrAfter'])         { $sdmaRptr = [int]$p.SdmaRingRptrAfter }
    if ($null -ne $p.PSObject.Properties['SdmaRingErrorLogBefore'])    { $elB = To-U32 $p.SdmaRingErrorLogBefore }
    if ($null -ne $p.PSObject.Properties['SdmaRingErrorLogAfter'])     { $elA = To-U32 $p.SdmaRingErrorLogAfter }
    if ($null -ne $p.PSObject.Properties['SdmaRingUtcl1ReadXnack0Before'])  { $rxB = To-U32 $p.SdmaRingUtcl1ReadXnack0Before }
    if ($null -ne $p.PSObject.Properties['SdmaRingUtcl1ReadXnack0After'])   { $rxA = To-U32 $p.SdmaRingUtcl1ReadXnack0After }
    if ($null -ne $p.PSObject.Properties['SdmaRingUtcl1WriteXnack0Before']) { $wxB = To-U32 $p.SdmaRingUtcl1WriteXnack0Before }
    if ($null -ne $p.PSObject.Properties['SdmaRingUtcl1WriteXnack0After'])  { $wxA = To-U32 $p.SdmaRingUtcl1WriteXnack0After }
    if ($null -ne $p.PSObject.Properties['SdmaRingStatusBefore'])       { $sdStatB = To-U32 $p.SdmaRingStatusBefore }
    if ($null -ne $p.PSObject.Properties['SdmaRingExIdlePollExpired'])  { $sdExIdle = [int]$p.SdmaRingExIdlePollExpired }
    if ($null -ne $p.PSObject.Properties['SdmaRingCntlBefore'])         { $sdCntlB = To-U32 $p.SdmaRingCntlBefore }
    if ($null -ne $p.PSObject.Properties['SdmaRingCntlApplied'])        { $sdCntlA = To-U32 $p.SdmaRingCntlApplied }
    if ($null -ne $p.PSObject.Properties['SdmaRingRptrBefore'])         { $sdRptrB = [uint32]$p.SdmaRingRptrBefore }
    if ($null -ne $p.PSObject.Properties['SdmaF32ControlBefore'])       { $sdF32Ctl = To-U32 $p.SdmaF32ControlBefore }
    if ($null -ne $p.PSObject.Properties['SdmaF32F32Before'])           { $sdF32F32 = To-U32 $p.SdmaF32F32Before }
    if ($null -ne $p.PSObject.Properties['SdmaF32PcLast'])              { $sdF32Pc = To-U32 $p.SdmaF32PcLast }
    if ($null -ne $p.PSObject.Properties['SdmaF32PcTransitions'])       { $sdF32Tr = [uint32]$p.SdmaF32PcTransitions }
    if ($null -ne $p.PSObject.Properties['SdmaRingRbCntl'])             { $sdRbCntl = To-U32 $p.SdmaRingRbCntl }
    if ($null -ne $p.PSObject.Properties['SdmaRingHeader'])             { $sdHdr = [uint32]$p.SdmaRingHeader }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvPollsStaging']){ $invPolls1 = [int]$p.PfpDirectInvPollsStaging }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvAckPreWptr'])  { $invAck2 = [int]$p.PfpDirectInvAckPreWptr }
    if ($null -ne $p.PSObject.Properties['PfpDirectInvPollsPreWptr']){ $invPolls2 = [int]$p.PfpDirectInvPollsPreWptr }
    for ($t = 0; $t -lt 16; $t++) {
        $tn = 'PfpDirectPcTrace{0:d2}' -f $t
        if ($null -ne $p.PSObject.Properties[$tn]) { $trace += [uint32]$p.$tn }
    }

    $verdict = Get-PfpVerdict $pcLast $trans $samples

    # Gravar o tunable, persistir e o driver aplicar sao tres coisas diferentes.
    # O historico do projeto ja tem um tunable que nunca controlou nada
    # (SkipHwBringup, doc 94): confrontar pedido contra o que o driver reportou.
    if ($GcvmInvalidateEngine -ge 0 -and $invEng -ge 0 -and $invEng -ne $GcvmInvalidateEngine) {
        $verdict = "TUNABLE-NAO-APLICADO(eng pedido=$GcvmInvalidateEngine lido=$invEng) $verdict"
    }
    if ($GcvmInvalidateSemaphore -ge 0 -and $invSem -ge 0 -and $invSem -ne $GcvmInvalidateSemaphore) {
        $verdict = "TUNABLE-NAO-APLICADO(sem pedido=$GcvmInvalidateSemaphore lido=$invSem) $verdict"
    }
    if ($CpSoftReset -ge 0 -and $srApplied -ge 0 -and $srApplied -ne $CpSoftReset) {
        $verdict = "TUNABLE-NAO-APLICADO(softreset pedido=$CpSoftReset lido=$srApplied) $verdict"
    }
    if ($Vmid0PageTable -ge 0 -and $ptApplied -ge 0 -and $ptApplied -ne $Vmid0PageTable) {
        $verdict = "TUNABLE-NAO-APLICADO(vmid0pt pedido=$Vmid0PageTable lido=$ptApplied) $verdict"
    }

    # O direct load pressupoe herdar o CP em HALT no PC 0. Sem isso o ensaio nao
    # esta medindo o carregador, e sim um CP que ja' estava rodando (doc 95).
    # Evidencia nao populada (PfpPcSamples=0) significa que o direct load nao
    # rodou - nao que o CP foi herdado ligado. Sem esta condicao, uma corrida
    # invalidada aparecia como FAIL-INHERITED-ACTIVE.
    if ($samples -gt 0 -and $meCntlBefore -ge 0 -and
        ($meCntlBefore -ne $ME_CNTL_HALTED -or $pcBefore -ne 0)) {
        $verdict = "FAIL-INHERITED-ACTIVE ($verdict)"
    }

    # Consistencia da hipotese: PC=0x284 <=> PfpDirectStatus=0. Divergir aqui e' um achado.
    $predicted = ($verdict -like 'OK-*')
    $actual    = ($pfpStatus -eq 0)
    if ($predicted -ne $actual) {
        $verdict = "$verdict !DIVERGE(status=$pfpStatus)"
    }

    $color = 'Green'
    if (-not $actual) { $color = 'Red' }

    $icText = ("{0}/{1}" -f $icInvUnhalt, $icPrime)
    if ($icInvUnhalt -lt 0) { $icText = 'AUSENTE' }

    # Caminho do PC na janela cega entre UNHALT e ativacao do anel: so' as
    # transicoes interessam, valores repetidos sao ruido de amostragem.
    $tracePath = ''
    if ($trace.Count -gt 0) {
        $prev = $null
        $steps = @()
        foreach ($v in $trace) {
            if ($null -eq $prev -or $v -ne $prev) { $steps += (To-Hex32 $v) }
            $prev = $v
        }
        $tracePath = ($steps -join '>')
    }

    $inhText = 'AUSENTE'
    if ($meCntlBefore -ge 0) {
        $inhText = ("{0}/{1}" -f (To-Hex32 ([uint32]$meCntlBefore)), (To-Hex32 $pcBefore))
    }

    Write-Host ("{0,4}  {1,6}  {2}  {3,6}/{4,-7}  {5,4}/{6,-5}  {7}  {8,3}  {9,-12}  {10,-18}  {11}" -f `
        $i, $pfpStatus, (To-Hex32 $pcLast), $trans, $samples, $rptr, $wptr, (To-Hex32 $fence), `
        $hqdTriad, $icText, $inhText, $verdict) -ForegroundColor $color

    $showDetail = ($i -eq 1) -or (-not ($verdict -like 'OK-*'))
    if ($tracePath -ne '' -and $showDetail) {
        Write-Host ("        traco PC pos-UNHALT : {0}" -f $tracePath) -ForegroundColor DarkGray
        Write-Host ("        JT_STAT PFP/CE/MEC  : 0x{0:X2}/0x{1:X2}/0x{2:X2}" -f $jtPfp, $jtCe, $jtMec) -ForegroundColor DarkGray
        # O estagio do CP roda atras de SdmaRingWorkStatus == OK. Quando o SDMA
        # falha, o carregador do CP nem e' chamado e a linha sai toda zerada.
        if ($sdmaStatus -gt 0) {
            Write-Host ("        SDMA status={0} polls={1} WPTR/RPTR={2}/{3}   <- o CP foi PULADO, nao falhou" -f `
                $sdmaStatus, $sdmaPolls, $sdmaWptr, $sdmaRptr) -ForegroundColor Red
            Write-Host ("        SDMA herdado : STATUS_before={0} RPTR_before={1} F32ctl={2} F32={3} rbCntl={4} hdr={5} F32pc={6} tr={7}" -f `
                (To-Hex32 ([uint32][math]::Max($sdStatB,0))), $sdRptrB, (To-Hex32 ([uint32][math]::Max($sdF32Ctl,0))), `
                (To-Hex32 ([uint32][math]::Max($sdF32F32,0))), (To-Hex32 ([uint32][math]::Max($sdRbCntl,0))), `
                $sdHdr, (To-Hex32 ([uint32][math]::Max($sdF32Pc,0))), $sdF32Tr) -ForegroundColor Yellow
            if ($elB -ge 0) {
                $fresh = ($elA -ne $elB) -or ($rxA -ne $rxB) -or ($wxA -ne $wxB)
                Write-Host ("        SDMA latches antes->depois : ErrorLog {0}->{1}  RdXnack0 {2}->{3}  WrXnack0 {4}->{5}" -f `
                    (To-Hex32 $elB), (To-Hex32 $elA), (To-Hex32 $rxB), (To-Hex32 $rxA), (To-Hex32 $wxB), (To-Hex32 $wxA)) -ForegroundColor Yellow
                if ($fresh) {
                    Write-Host '        FALTA DE TRADUCAO NOVA : algum latch mudou NESTE ciclo' -ForegroundColor Red
                } else {
                    Write-Host '        latches inalterados    : os XNACK sao residuais, nao deste ciclo' -ForegroundColor Yellow
                }
            }
        }
        if ($cmpDw -ge 0) {
            $cc = 'DarkGray'; if ($mism -ne 0) { $cc = 'Red' }
            $obsTxt = To-Hex32 (To-U32 $destObs)
            $diag = ''
            if ($mism -ne 0) {
                if ((To-U32 $destObs) -eq (To-U32 0xDEADC000L)) { $diag = '  (ainda o VENENO: a GPU nao escreveu)' }
                else { $diag = '  (valor inesperado: endereco ou pacote errado)' }
            }
            Write-Host ("        dados conferidos     : {0} dwords, {1} divergencia(s), {2} polls, dest[0]={3}{4}" -f `
                $cmpDw, $mism, $destPolls, $obsTxt, $diag) -ForegroundColor $cc
        }
        if ($tunMask -ge 0) {
            $tunColor = 'DarkGray'
            if ($tunMask -ne 15) { $tunColor = 'Red' }
            Write-Host ("        tunables lidos       : mascara=0x{0:X} (eng/sem/softreset/vmid0; 0xF = os quatro presentes)" -f $tunMask) -ForegroundColor $tunColor
        }
        if ($srApplied -ge 0 -or $ptApplied -ge 0) {
            Write-Host ("        blocos de 4df3484    : soft reset CP={0}  page table VMID0={1} (ack={2})" -f `
                $srApplied, $ptApplied, $ptAck) -ForegroundColor DarkGray
        }
        if ($invEng -ge 0) {
            $invColor = 'DarkGray'
            if ($invAck1 -ne 1 -or $invAck2 -ne 1) { $invColor = 'Red' }
            Write-Host ("        GCVM inval ENG{0,-2}     : ack staging={1} ({2} polls)  ack pre-WPTR={3} ({4} polls)" -f `
                $invEng, $invAck1, $invPolls1, $invAck2, $invPolls2) -ForegroundColor $invColor
            # ACK ja' em 1 antes do REQ significa bit residual: um ACK observado
            # depois com 0 polls nao prova que esta invalidacao respondeu.
            if ($invSem -ge 0) {
                # Bit 16 do ACK e o estado do semaforo, e e leitura pura.
                $semBit1 = if ($invRaw1 -ge 0) { ($invRaw1 -shr 16) -band 1 } else { '?' }
                $semBit2 = if ($invRaw2 -ge 0) { ($invRaw2 -shr 16) -band 1 } else { '?' }
                Write-Host ("        semaforo             : protocolo={0}  adquirido={1}/{2}  ACK.bit16={3}/{4}  ACK cru={5}/{6}" -f `
                    $invSem, $invAcq1, $invAcq2, $semBit1, $semBit2, (To-Hex32 ([uint32][math]::Max($invRaw1,0))), (To-Hex32 ([uint32][math]::Max($invRaw2,0)))) -ForegroundColor DarkGray
            }
            # InvAckBefore carrega o registrador CRU. O ACK e por VMID: bit N =
            # VMID N. Comparar o registrador inteiro com 1 dava falso positivo
            # quando o residuo era de outro VMID (ex.: 0x100 = VMID 8).
            $pre1bit = if ($invPre1 -ge 0) { $invPre1 -band 1 } else { -1 }
            $pre2bit = if ($invPre2 -ge 0) { $invPre2 -band 1 } else { -1 }
            if ($pre1bit -eq 1 -or $pre2bit -eq 1) {
                Write-Host ("        ACK RESIDUAL         : bit0 ja era 1 ANTES do REQ (staging={0} pre-WPTR={1}) - esse sitio e inconclusivo" -f `
                    (To-Hex32 ([uint32][math]::Max($invPre1,0))), (To-Hex32 ([uint32][math]::Max($invPre2,0)))) -ForegroundColor Yellow
            }
            if ($pre1bit -eq 0 -and $invAck1 -eq 1) {
                Write-Host '        INVALIDACAO PROVADA  : ACK.bit0 foi de 0 para 1 atravessando o REQ' -ForegroundColor Green
            }
        }
    } elseif ($tracePath -eq '' -and $showDetail) {
        Write-Host '        traco PC pos-UNHALT : AUSENTE (binario anterior a instrumentacao)' -ForegroundColor Yellow
    }

    $rows.Add([pscustomobject]@{
        Iteration      = $i
        PfpDirectStatus= $pfpStatus
        PfpPcLast      = (To-Hex32 $pcLast)
        PfpPcTransitions = $trans
        PfpPcSamples   = $samples
        IcInvEntryMask = $icInvEntry
        IcInvUnhaltMask= $icInvUnhalt
        IcPrimeUnhaltMask = $icPrime
        MeCntlBefore   = (To-Hex32 ([uint32][math]::Max($meCntlBefore,0)))
        PfpPcBefore    = (To-Hex32 $pcBefore)
        JtStatPfp      = $jtPfp
        JtStatCe       = $jtCe
        JtStatMec      = $jtMec
        PfpPcTrace     = $tracePath
        InvEngine      = $invEng
        InvAckStaging  = $invAck1
        InvAckBeforeStaging = $invPre1
        InvAckBeforePreWptr = $invPre2
        InvUseSemaphore = $invSem
        InvAckRawStaging = (To-Hex32 ([uint32][math]::Max($invRaw1,0)))
        InvAckRawPreWptr = (To-Hex32 ([uint32][math]::Max($invRaw2,0)))
        InvSemAcqStaging = $invAcq1
        InvSemAcqPreWptr = $invAcq2
        CpSoftResetApplied = $srApplied
        Vmid0PageTableApplied = $ptApplied
        Vmid0InvAck = $ptAck
        TunableReadOkMask = $tunMask
        ComparedDwords = $cmpDw
        Mismatches     = $mism
        DestPolls      = $destPolls
        DestObserved   = (To-Hex32 (To-U32 $destObs))
        SdmaWorkStatus = $sdmaStatus
        SdmaPolls      = $sdmaPolls
        SdmaErrorLogBefore = (To-Hex32 ([uint32][math]::Max($elB,0)))
        SdmaErrorLogAfter  = (To-Hex32 ([uint32][math]::Max($elA,0)))
        SdmaStatusBefore   = (To-Hex32 ([uint32][math]::Max($sdStatB,0)))
        SdmaRptrBefore     = $sdRptrB
        SdmaF32ControlBefore = (To-Hex32 ([uint32][math]::Max($sdF32Ctl,0)))
        SdmaF32Before      = (To-Hex32 ([uint32][math]::Max($sdF32F32,0)))
        SdmaF32PcLast      = (To-Hex32 ([uint32][math]::Max($sdF32Pc,0)))
        SdmaF32PcTransitions = $sdF32Tr
        SdmaRbCntl         = (To-Hex32 ([uint32][math]::Max($sdRbCntl,0)))
        SdmaRingHeader     = $sdHdr
        SdmaExIdlePollExpired = $sdExIdle
        SdmaCntlBefore     = (To-Hex32 ([uint32][math]::Max($sdCntlB,0)))
        SdmaCntlApplied    = (To-Hex32 ([uint32][math]::Max($sdCntlA,0)))
        InvPollsStaging= $invPolls1
        InvAckPreWptr  = $invAck2
        InvPollsPreWptr= $invPolls2
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

# Gate de instrumentacao: um ensaio que nao mede nada se parece com hardware
# falhando. Se os campos de I-Cache nao chegaram, o binario carregado e' antigo
# e nenhuma conclusao sobre a cache pode ser tirada desta corrida.
$semIc = @($rows | Where-Object { $_.IcInvUnhaltMask -lt 0 }).Count
if ($semIc -gt 0) {
    Write-Host ''
    Write-Host ("AVISO: {0}/{1} ciclos sem PfpDirectIcInvUnhaltMask." -f $semIc, $rows.Count) -ForegroundColor Yellow
    Write-Host '       O driver carregado e anterior a instrumentacao de I-Cache.' -ForegroundColor Yellow
    Write-Host '       Reconstrua e reinstale a imagem de laboratorio antes de concluir.' -ForegroundColor Yellow
}

# ------------------------------------------------------------------ RESUMO --

$total    = $rows.Count
$okCount  = @($rows | Where-Object { $_.PfpDirectStatus -eq 0 }).Count
$loop     = @($rows | Where-Object { $_.Verdict -like 'FAIL-LOOP*' }).Count
$frozen   = @($rows | Where-Object { $_.Verdict -like 'FAIL-FROZEN*' }).Count
$inherit  = @($rows | Where-Object { $_.Verdict -like 'FAIL-INHERITED-ACTIVE*' }).Count
$diverge  = @($rows | Where-Object { $_.Verdict -like '*!DIVERGE*' }).Count

Write-Host ''
Write-Host '=== RESUMO ===' -ForegroundColor Cyan
Write-Host ("  Ciclos executados     : {0}" -f $total)
Write-Host ("  Sucessos (status=0)   : {0}" -f $okCount)
$rptrEq = @($rows | Where-Object { $_.Rb0RptrAfter -eq $_.Rb0WptrApplied }).Count
$rptrOff = @($rows | Where-Object { $_.PfpDirectStatus -eq 0 -and $_.Rb0RptrAfter -ne $_.Rb0WptrApplied }).Count
Write-Host ("  RPTR == WPTR estrito  : {0}" -f $rptrEq)
$triade = @($rows | Where-Object {
    $_.PfpDirectStatus -eq 0 -and
    $_.Rb0RptrAfter -eq $_.Rb0WptrApplied -and
    $_.Rb0FenceAfter -eq 'BC250123' }).Count
$tripe = @($rows | Where-Object {
    $_.PfpDirectStatus -eq 0 -and
    $_.Rb0RptrAfter -eq $_.Rb0WptrApplied -and
    $_.Rb0FenceAfter -eq 'BC250123' -and
    $_.ComparedDwords -gt 0 -and $_.Mismatches -eq 0 }).Count
Write-Host ("  CP: status+RPTR==WPTR+fence : {0}" -f $triade) -ForegroundColor Green
Write-Host ("  CP TRIPE COMPLETO           : {0}   (RPTR==WPTR + fence + dados byte a byte)" -f $tripe) -ForegroundColor Green
Write-Host ("  OK porem RPTR != WPTR : {0}   <- nao fecham o tripe do CLAUDE.md" -f $rptrOff)
$hqdOk = @($rows | Where-Object { $_.HqdTriadPassed -eq 1 }).Count
Write-Host ("  HQD tripe fechado     : {0}" -f $hqdOk)
$sdmaFail = @($rows | Where-Object { $_.SdmaWorkStatus -gt 0 }).Count
$exIdleBad = @($rows | Where-Object { $_.SdmaExIdlePollExpired -eq 1 }).Count
if ($exIdleBad -ge 0) {
    Write-Host ("  EX_IDLE_POLL expirado : {0}   <- estado herdado pelo SDMA" -f $exIdleBad)
}
if ($sdmaFail -gt 0) {
    Write-Host ("  Falhas do SDMA        : {0}   <- bloqueiam o estagio do CP" -f $sdmaFail) -ForegroundColor Yellow
}
Write-Host ("  Falhas modo LOOP      : {0}" -f $loop)
Write-Host ("  Falhas modo CONGELADO : {0}" -f $frozen)
Write-Host ("  CP herdado EM EXECUCAO: {0}" -f $inherit)
if ($inherit -gt 0) {
    Write-Host ''
    Write-Host 'ATENCAO: o CP nao chegou em HALT no PC 0. O direct load foi aplicado' -ForegroundColor Yellow
    Write-Host '         sobre um CP ja em execucao, condicao ausente das 177 capturas' -ForegroundColor Yellow
    Write-Host '         historicas. Reinicie a maquina antes de tirar conclusoes.' -ForegroundColor Yellow
}
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
