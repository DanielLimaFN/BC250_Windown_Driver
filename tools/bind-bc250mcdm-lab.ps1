[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptBlackScreenRisk,
    [switch]$EnableShaderDispatch,
    # Fase P1: exige que o pacote tenha subido com a fila persistente ligada.
    [switch]$EnablePersistentSubmission,
    # Permite vincular sobre uma engine que ainda carrega estado da sessao
    # anterior. A telemetria resultante NAO vale como evidencia.
    [switch]$IAcceptDirtyEngine,
    # Fase V5: grava RenderNode=1, fazendo o driver registrar por
    # DxgkInitialize (WDDM display+render) em vez de
    # DxgkInitializeDisplayOnlyDriver. Lido em DriverEntry, portanto so vale a
    # partir do proximo BOOT. Alto risco de tela preta.
    [switch]$EnableRenderNode,
    [switch]$EnablePhysicalRenderOnly,
    # Permite o fluxo solicitado pelo operador sem rollback temporizado.
    # A recuperacao manual C:\BC250_RECOVERY.cmd continua obrigatoria.
    [switch]$DisableWatchdog,
    [switch]$IAcceptNoAutomaticRollback,
    [switch]$IAcceptAutomaticRecoveryReboot,
    [ValidateRange(30,600)][int]$WatchdogTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root 'driver\wdk\package\output_lab'
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'
$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packageCat = Join-Path $packageDir 'bc250mcdm.cat'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$watchdogScript = Join-Path $root 'tools\watch-bc250mcdm-lab.ps1'
# Gate proprio do laboratorio: o de producao exige SkipHwBringup=1 e por isso
# rejeita este binding sempre, por construcao (doc 95 secao B3).
$confirmScript = Join-Path $root 'tools\confirm-bc250mcdm-lab.ps1'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$cleanBootMarkerPath = 'C:\BC250_REQUIRE_CLEAN_BOOT.json'
$recoveryPath = 'C:\BC250_RECOVERY.cmd'
$hardwareId = 'PCI\VEN_1002&DEV_13FE'

function Assert-ArtifactHash([string]$Path, [string]$ExpectedHash, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Artefato ausente ($Label): $Path"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedHash) {
        throw "Hash divergente ($Label): observado=$actualHash esperado=$ExpectedHash"
    }
}

if (-not (Test-Path -LiteralPath $packageInf) -or -not (Test-Path -LiteralPath $manifestPath)) {
    throw "Pacote de laboratorio nao encontrado. Execute tools\package-bc250mcdm-lab.ps1 primeiro."
}
if (-not (Test-Path -LiteralPath $recoveryPath)) {
    throw "C:\BC250_RECOVERY.cmd nao encontrado."
}
if ($DisableWatchdog -and -not $IAcceptNoAutomaticRollback) {
    throw '-DisableWatchdog exige -IAcceptNoAutomaticRollback.'
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado (Administrador).'
}

# O rollback para BasicDisplay remove a chave de telemetria, mas nao garante que
# a HQD saiu fisicamente do estado anterior. Ele publica o boot onde ocorreu; o
# binder so pode prosseguir depois que o Windows observar outro boot.
if (Test-Path -LiteralPath $cleanBootMarkerPath) {
    $cleanBootMarker = Get-Content -LiteralPath $cleanBootMarkerPath -Raw |
        ConvertFrom-Json
    $currentBootTicks = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.
        ToUniversalTime().Ticks
    if ([int64]$cleanBootMarker.rollback_boot_ticks -eq [int64]$currentBootTicks) {
        throw (
            'Binding recusado: o rollback anterior exige BOOT FRIO com BasicDisplay ' +
            "antes de um novo ensaio. Marcador: $cleanBootMarkerPath")
    }
    Remove-Item -LiteralPath $cleanBootMarkerPath -Force
    Write-Host 'Boot posterior ao rollback confirmado; marcador de engine suja liberado.' -ForegroundColor Green
}

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" })
if ($devices.Count -ne 1) {
    throw "Dispositivo BC-250 nao encontrado ou multiplos encontrados."
}
$device = $devices[0]
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
# O schema vem do manifesto, nao de literal. Fixar o numero aqui ja custou um
# ciclo: o pacote 3.44/115 era recusado por um gate que ainda dizia 114. O piso
# continua sendo 114, o primeiro schema desta familia de laboratorio.
$expectedSchema = [uint32]$manifest.TelemetrySchemaVersion
if ($manifest.BuildKind -ne 'MCDM_LAB_BRINGUP' -or [bool]$manifest.HardwareValidated -or
    $expectedSchema -lt 114 -or [uint32]$manifest.SkipHwBringup -ne 0) {
    throw 'Contrato do manifesto de laboratorio invalido (kind/validacao/schema/bring-up).'
}
Assert-ArtifactHash $packageSys $manifest.PackageSysSha256 'SYS'
Assert-ArtifactHash $packageInf $manifest.PackageInfSha256 'INF'
Assert-ArtifactHash $packageCat $manifest.PackageCatSha256 'CAT'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_sdma.bin') $manifest.SdmaFirmwareSha256 'SDMA firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_rlc.bin') $manifest.RlcFirmwareSha256 'RLC firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_pfp.bin') $manifest.PfpFirmwareSha256 'PFP firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_me.bin') $manifest.MeFirmwareSha256 'ME firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_ce.bin') $manifest.CeFirmwareSha256 'CE firmware'
Assert-ArtifactHash (Join-Path $packageDir 'cyan_skillfish2_mec.bin') $manifest.MecFirmwareSha256 'MEC firmware'
Assert-ArtifactHash $recoveryPath $manifest.RecoverySha256 'recuperacao'
foreach ($signedArtifact in @($packageSys, $packageCat)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signedArtifact
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        $signature.SignerCertificate.Thumbprint -ne $manifest.SignerThumbprint) {
        throw "Assinatura invalida ou signer divergente: $signedArtifact"
    }
}

$infText = Get-Content -LiteralPath $packageInf -Raw
$expectedInfValues = [ordered]@{
    'Parameters\Probe,SchemaVersion' = $expectedSchema
    'Parameters,WddmContractVersion' = 27
    'Parameters\Driver,ActiveMode' = 1
    'Parameters,RenderNode' = $(if ($EnableRenderNode) { 1 } elseif ($EnablePhysicalRenderOnly) { 1 } else { 0 })
    'Parameters,RenderOnlyLab' = $(if ($EnablePhysicalRenderOnly) { 1 } else { 0 })
    'Parameters,SkipHwBringup' = 0
    'Parameters,ShaderDispatch' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,CpSoftReset' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,Vmid0PageTable' = 0
    'Parameters,SdmaAutoCtxSw' = $(if ($EnableShaderDispatch) { 0 } else { 1 })
    'Parameters,PersistentSubmission' =
        $(if ($EnablePersistentSubmission) { 1 } else { 0 })
    'Parameters,GpuvaLabPromotion' = 0
}
if ($EnablePhysicalRenderOnly -and
    (-not $EnableShaderDispatch -or -not $EnablePersistentSubmission)) {
    throw ('Physical render-only exige -EnableShaderDispatch e ' +
           '-EnablePersistentSubmission para provar o backend antes de StartDevice retornar sucesso.')
}
if ([bool]$manifest.RenderOnlyLab -ne [bool]$EnablePhysicalRenderOnly) {
    throw 'Perfil solicitado diverge do pacote: reempacote com -EnablePhysicalRenderOnly.'
}
foreach ($entry in $expectedInfValues.GetEnumerator()) {
    $escapedName = [regex]::Escape($entry.Key)
    if ([regex]::Matches(
            $infText,
            "(?m)^HKR,$escapedName,0x00010001,$($entry.Value)\s*$").Count -ne 1) {
        throw "INF diverge do contrato solicitado: $($entry.Key)=$($entry.Value)."
    }
}
if ($EnableShaderDispatch -and -not [bool]$manifest.ShaderDispatchEnabled) {
    throw 'Pacote nao habilita shader: reempacote com package-bc250mcdm-lab.ps1 -EnableShaderDispatch.'
}
if ($EnablePersistentSubmission -and -not [bool]$manifest.PersistentSubmissionEnabled) {
    throw 'Pacote nao habilita a fila persistente: reempacote com -EnablePersistentSubmission.'
}
if ([bool]$manifest.GpuvaLabPromotionEnabled) {
    throw 'Pacote inseguro: GpuvaLabPromotion deve estar desabilitado no manifesto.'
}
if ($EnableShaderDispatch -and
    (-not [bool]$manifest.CpSoftResetEnabled -or
     [bool]$manifest.Vmid0PageTableEnabled -or
     [bool]$manifest.SdmaAutoCtxSwEnabled)) {
    throw 'Receita F8b invalida no manifesto: exige CpSoftReset=1, Vmid0PageTable=0 e SdmaAutoCtxSw=0.'
}

# --------------------------------------------------------------------------
# Guard-rail 1: o pacote precisa ser do build atual.
#
# O empacotador COPIA o .sys, nao compila. Se driver\wdk\output_lab\bc250mcdm.sys
# for mais novo que a origem registrada no manifesto, este binding instalaria o
# candidato ANTERIOR e o ensaio inteiro seria sobre o binario errado — com
# aparencia de sucesso. Aconteceu em 2026-08-31 12:03.
# --------------------------------------------------------------------------
$builtSys = Join-Path $root 'driver\wdk\output_lab\bc250mcdm.sys'
if (Test-Path -LiteralPath $builtSys) {
    $builtHash = (Get-FileHash -LiteralPath $builtSys -Algorithm SHA256).Hash
    if ($builtHash -ne [string]$manifest.SourceSysSha256) {
        throw (
            "O pacote nao corresponde ao build atual.`n" +
            "  build   : $builtHash`n" +
            "  pacote  : $($manifest.SourceSysSha256)`n" +
            'Reempacote antes de vincular: tools\package-bc250mcdm-lab.ps1 ' +
            '(com as mesmas flags deste binding).')
    }
}

# --------------------------------------------------------------------------
# Guard-rail 2: a engine precisa estar limpa.
#
# Estado de fila nao cai com desbind; so o boot frio limpa. Vincular sobre uma
# fila ja ativa faz o motor persistente nao subir, e todos os contadores saem
# zerados — o que se confunde com "quebrado" quando na verdade e' "nao
# exercitado". Aconteceu em 2026-08-31 12:03 com WPTR remanescente 204.
# --------------------------------------------------------------------------
$probeKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
if (Test-Path -LiteralPath $probeKey) {
    $probeNow = Get-ItemProperty -LiteralPath $probeKey
    # REG_DWORD chega como Int32 e pode vir negativo; normalizar para 32 bits.
    $rawBefore = $probeNow.HqdActiveBefore
    $rawWptr   = $probeNow.HqdActiveBaselineWptrReadback
    $rawEngine = $probeNow.P1EngineState
    $rawP1Ready = $probeNow.P1SelfTestPhysicalReady
    $rawP2Ready = $probeNow.P2PhysicalReady
    $hqdBefore = if ($null -eq $rawBefore) { 0 } else { [int64]$rawBefore -band 0xFFFFFFFFL }
    $wptrLeft  = if ($null -eq $rawWptr)   { 0 } else { [int64]$rawWptr   -band 0xFFFFFFFFL }
    $engineState = if ($null -eq $rawEngine) { 0 } else { [int64]$rawEngine -band 0xFFFFFFFFL }
    $p1Ready = if ($null -eq $rawP1Ready) { 0 } else { [int64]$rawP1Ready -band 0xFFFFFFFFL }
    $p2Ready = if ($null -eq $rawP2Ready) { 0 } else { [int64]$rawP2Ready -band 0xFFFFFFFFL }
    if ($hqdBefore -ne 0 -or $wptrLeft -ne 0 -or $engineState -ne 0 -or
        $p1Ready -ne 0 -or $p2Ready -ne 0) {
        $msg = (
            "Engine suja: a fila de computacao ainda carrega estado da sessao anterior.`n" +
            "  HqdActiveBefore              = $hqdBefore`n" +
            "  HqdActiveBaselineWptrReadback = $wptrLeft`n" +
            "  P1EngineState                = $engineState`n" +
            "  P1SelfTestPhysicalReady       = $p1Ready`n" +
            "  P2PhysicalReady               = $p2Ready`n" +
            'Restaure BasicDisplay e so entao faca BOOT FRIO. Reiniciar com ' +
            'bc250mcdm ainda instalado religa a fila persistente.')
        if ($IAcceptDirtyEngine) {
            Write-Warning $msg
            Write-Warning 'Prosseguindo por -IAcceptDirtyEngine: a telemetria deste ciclo NAO e evidencia.'
        } else {
            throw $msg
        }
    }
}

$guardMode = if ($DisableWatchdog) { 'SEM WATCHDOG' } else { 'COM WATCHDOG' }
Write-Host "=== BINDING DE LABORATORIO (BC-250 MCDM) $guardMode ===" -ForegroundColor Magenta
Write-Host "  Dispositivo: $($device.PNPDeviceID)"
Write-Host "  Servico atual: $($device.Service)"
Write-Host "  Pacote: $packageInf"
if ($DisableWatchdog) {
    Write-Host '  Rollback automatico: DESABILITADO por decisao explicita.' -ForegroundColor Yellow
    Write-Host "  Recuperacao manual: $recoveryPath" -ForegroundColor Yellow
} else {
    Write-Host "  Watchdog Timeout: $WatchdogTimeoutSeconds segundos"
}
Write-Host ""

if (-not $Execute -or -not $IAcceptBlackScreenRisk) {
    Write-Host "Modo Dry-Run. Para executar:" -ForegroundColor Yellow
    # A dica precisa repetir -EnableShaderDispatch: sem ela o contrato do
    # INF exige ShaderDispatch=0 e o binding falha antes deste ponto.
    $shaderFlag = $(if ($EnableShaderDispatch) { ' -EnableShaderDispatch' } else { '' })
    $watchdogFlag = $(if ($DisableWatchdog) {
        ' -DisableWatchdog -IAcceptNoAutomaticRollback'
    } else { '' })
    Write-Host ("powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1" +
        " -Execute -IAcceptBlackScreenRisk" + $shaderFlag + $watchdogFlag)
    exit 0
}
if ($EnablePhysicalRenderOnly -and -not $DisableWatchdog -and
    -not $IAcceptAutomaticRecoveryReboot) {
    throw 'Physical render-only exige -IAcceptAutomaticRecoveryReboot.'
}
if ($EnablePhysicalRenderOnly -and -not $DisableWatchdog -and
    $WatchdogTimeoutSeconds -lt 120) {
    throw 'Physical render-only exige WatchdogTimeoutSeconds entre 120 e 600.'
}

# 1. Limpeza de marcador OEM e pacotes anteriores
if (Test-Path -LiteralPath $markerPath) {
    $oldOem = (Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($oldOem -match '^oem\d+\.inf$') {
        & pnputil.exe /delete-driver $oldOem /uninstall /force 2>&1 | Out-Null
    }
    Remove-Item -LiteralPath $markerPath -Force -ErrorAction SilentlyContinue
}

# 2. Stage do pacote de laboratorio
Write-Host "[1/4] Estagiando pacote de laboratorio via pnputil..." -ForegroundColor Yellow
$output = (& pnputil.exe /add-driver $packageInf 2>&1) -join "`n"
$exitCode = $LASTEXITCODE
$output | Write-Host
if ($exitCode -ne 0) { throw "pnputil /add-driver falhou: exit=$exitCode" }

$publishedNames = @(
    [regex]::Matches($output, '(?i)\boem\d+\.inf\b') |
        ForEach-Object { $_.Value.ToLowerInvariant() } |
        Select-Object -Unique
)
if ($publishedNames.Count -ne 1) {
    throw 'Nome oemNN.inf nao identificado de forma unica.'
}
$publishedName = $publishedNames[0]
$publishedName | Set-Content -LiteralPath $markerPath -Encoding ascii
Write-Host "  Pacote publicado como: $publishedName" -ForegroundColor Green

# 3. Armar o Watchdog em segundo plano
#
# Dois watchdogs simultaneos brigam pelo mesmo dispositivo: o mais antigo dispara
# o rollback (que remove o pacote OEM) no meio do binding do mais novo, e o
# UpdateDriverForPlugAndPlayDevicesW falha com win32=2. Aconteceu em 29/08 18:14,
# com os PIDs 3092 e 908 armados ao mesmo tempo.
$liveWatchdogs = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*watch-bc250mcdm-lab.ps1*' })
if ($liveWatchdogs.Count -gt 0) {
    Write-Host ''
    Write-Host 'ERRO: ja existe watchdog de laboratorio armado:' -ForegroundColor Red
    foreach ($w in $liveWatchdogs) { Write-Host ("  PID {0}" -f $w.ProcessId) -ForegroundColor Red }
    Write-Host 'Confirme o binding anterior, deixe-o expirar, ou encerre o processo:' -ForegroundColor Yellow
    Write-Host ("  Stop-Process -Id {0} -Force" -f $liveWatchdogs[0].ProcessId) -ForegroundColor Yellow
    throw 'Binding abortado: watchdog concorrente ativo.'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-lab-binding"
New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null

$watchdogToken = $null
$watchdogProcess = $null
if ($DisableWatchdog) {
    Write-Host "`n[2/4] Watchdog omitido por aceite explicito; evidencia criada." -ForegroundColor Yellow
} else {
    Write-Host "`n[2/4] Armando Watchdog de Rollback ($WatchdogTimeoutSeconds s)..." -ForegroundColor Yellow
    $watchdogToken = [guid]::NewGuid().ToString('D')
    if ($EnablePhysicalRenderOnly) {
        $taskName = 'BC250-SafeBindGuard'
        $guardDir = 'C:\BC250_SAFE_GUARD'
        $guardScript = Join-Path $guardDir 'watch-bc250mcdm-safe-bind.ps1'
        $configPath = Join-Path $guardDir 'armed.json'
        New-Item -ItemType Directory -Path $guardDir -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'watch-bc250mcdm-safe-bind.ps1') `
            -Destination $guardScript -Force
        $guardConfig = [ordered]@{
            schema_version = 1; task_name = $taskName; token = $watchdogToken
            instance_id = [string]$device.PNPDeviceID
            timeout_seconds = $WatchdogTimeoutSeconds; evidence_dir = $evidenceDir
            recovery_path = $recoveryPath
            recovery_sha256 = (Get-FileHash $recoveryPath -Algorithm SHA256).Hash
            expected_sys_sha256 = [string]$manifest.PackageSysSha256
            expected_inf_sha256 = [string]$manifest.PackageInfSha256
            expected_driver_version = [string]$manifest.DriverVer
            armed_boot_ticks = [long](Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
            automatic_recovery_reboot = $true; hardware_validated = $false
        }
        $guardConfig | ConvertTo-Json -Depth 5 | Set-Content $configPath -Encoding UTF8
        $guardConfig | ConvertTo-Json -Depth 5 | Set-Content `
            (Join-Path $evidenceDir 'safe-bind-watchdog-config.json') -Encoding UTF8
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
        $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument `
            "-NoProfile -ExecutionPolicy Bypass -File `"$guardScript`" -ConfigPath `"$configPath`" -Execute"
        $trigger = New-ScheduledTaskTrigger -AtStartup
        $taskPrincipal = New-ScheduledTaskPrincipal -UserId SYSTEM -LogonType ServiceAccount -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 15) `
            -MultipleInstances IgnoreNew -StartWhenAvailable
        Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
            -Principal $taskPrincipal -Settings $settings -Force | Out-Null
        Start-ScheduledTask -TaskName $taskName
        Start-Sleep -Seconds 1
        $guardState = Join-Path $evidenceDir 'safe-bind-watchdog-state.json'
        if (-not (Test-Path $guardState) -or
            (Get-Content $guardState -Raw | ConvertFrom-Json).state -ne 'OBSERVING') {
            throw 'Watchdog SYSTEM nao entrou em OBSERVING.'
        }
        Write-Host "  Watchdog SYSTEM ativo; reboot automatico autorizado." -ForegroundColor Green
    } else {
        $watchdogArguments = "-NoProfile -ExecutionPolicy Bypass -File `"$watchdogScript`" " +
            "-EvidenceDir `"$evidenceDir`" -InstanceId `"$($device.PNPDeviceID)`" " +
            "-TargetInf `"$publishedName`" -Token `"$watchdogToken`" " +
            "-TimeoutSeconds $WatchdogTimeoutSeconds -Execute"
        $watchdogProcess = Start-Process -FilePath powershell.exe -ArgumentList $watchdogArguments `
            -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 500
        if ($watchdogProcess.HasExited) {
            throw "Watchdog falhou ao inicializar (exit=$($watchdogProcess.ExitCode))."
        }
        Write-Host "  Watchdog ativo (PID: $($watchdogProcess.Id))" -ForegroundColor Green
    }
}

# DriverEntry e StartDevice leem estes valores durante o proprio binding. A
# politica precisa estar aplicada e relida ANTES de carregar o novo SYS; gravar
# depois criava uma corrida e podia exercitar um modo diferente do solicitado.
$paramKey = "HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters"
if (-not (Test-Path -LiteralPath $paramKey)) {
    New-Item -Path $paramKey -Force | Out-Null
}
$requestedPolicy = [ordered]@{
    SkipHwBringup = 0
    ShaderDispatch = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    CpSoftReset = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    Vmid0PageTable = 0
    SdmaAutoCtxSw = $(if ($EnableShaderDispatch) { 0 } else { 1 })
    PersistentSubmission = $(if ($EnablePersistentSubmission) { 1 } else { 0 })
    GpuvaLabPromotion = 0
    RenderNode = $(if ($EnableRenderNode) { 1 } elseif ($EnablePhysicalRenderOnly) { 1 } else { 0 })
    RenderOnlyLab = $(if ($EnablePhysicalRenderOnly) { 1 } else { 0 })
}
$policyBefore = [ordered]@{}
$parametersBefore = Get-ItemProperty -LiteralPath $paramKey -ErrorAction Stop
foreach ($entry in $requestedPolicy.GetEnumerator()) {
    $oldProperty = $parametersBefore.PSObject.Properties[$entry.Key]
    $policyBefore[$entry.Key] = if ($null -eq $oldProperty) {
        $null
    } else {
        $oldProperty.Value
    }
    Set-ItemProperty -LiteralPath $paramKey -Name $entry.Key `
        -Value ([uint32]$entry.Value) -Type DWord -Force
    $readback = Get-ItemPropertyValue -LiteralPath $paramKey -Name $entry.Key `
        -ErrorAction Stop
    if ([uint32]$readback -ne [uint32]$entry.Value) {
        throw "Readback pre-bind falhou para $($entry.Key)."
    }
}
[ordered]@{
    captured_at = (Get-Date).ToString('o')
    phase = 'PRE_BIND_POLICY_APPLIED_READBACK_OK'
    before = $policyBefore
    requested = $requestedPolicy
    hardware_validated = $false
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'prebind-policy.json') -Encoding UTF8
Write-Host "  Politica pre-bind aplicada e verificada." -ForegroundColor Green
Write-Host ("  RenderNode={0}; PersistentSubmission={1}; GpuvaLabPromotion=0." -f
    $requestedPolicy.RenderNode, $requestedPolicy.PersistentSubmission)

# 4. Executar o binding via NewDev ou pnputil
Write-Host "`n[3/4] Vinculando driver ao hardware..." -ForegroundColor Yellow
if (-not ('Bc250LabNewDev.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Bc250LabNewDev
{
    public static class NativeMethods
    {
        [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool UpdateDriverForPlugAndPlayDevicesW(
            IntPtr hwndParent,
            string hardwareId,
            string fullInfPath,
            UInt32 installFlags,
            [MarshalAs(UnmanagedType.Bool)] ref bool rebootRequired);
    }
}
'@
}

$installFlagForce = [uint32]0x00000001
$rebootRequired = $false
$nativeOk = [Bc250LabNewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(
    [IntPtr]::Zero,
    $hardwareId,
    $packageInf,
    $installFlagForce,
    [ref]$rebootRequired)

$win32Error = if ($nativeOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
Write-Host "  UpdateDriverForPlugAndPlayDevicesW: success=$nativeOk win32=$win32Error reboot=$rebootRequired"

if (-not $nativeOk) {
    # 'pnputil /install /driver <oemN.inf>' nao existe: so' imprime a tela de
    # uso. Parecia fallback e nao era.
    Write-Warning "NewDev falhou (win32=$win32Error), tentando fallback valido..."
    $fallback = (& pnputil.exe /add-driver $packageInf /install 2>&1) -join "`n"
    $fallback | Write-Host
    if ($LASTEXITCODE -ne 0) { Write-Warning "Fallback pnputil tambem falhou: exit=$LASTEXITCODE" }
}

# 5. Confirmar que nenhum instalador sobrescreveu a politica pre-bind.
Write-Host "`n[4/4] Verificando parametros efetivos pos-binding..." -ForegroundColor Yellow
foreach ($entry in $requestedPolicy.GetEnumerator()) {
    $readback = Get-ItemPropertyValue -LiteralPath $paramKey -Name $entry.Key `
        -ErrorAction Stop
    if ([uint32]$readback -ne [uint32]$entry.Value) {
        throw "Politica pos-binding divergiu em $($entry.Key)."
    }
}
Write-Host "  Politica pos-binding coincide com a politica pre-bind." -ForegroundColor Green
Write-Host ("  RenderNode={0}; ShaderDispatch={1}; PersistentSubmission={2}; GPUVA=0." -f
    $requestedPolicy.RenderNode, $requestedPolicy.ShaderDispatch,
    $requestedPolicy.PersistentSubmission)

# Verificar estado do dispositivo
Start-Sleep -Seconds 3
$devAfter = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like "$hardwareId*" }
Write-Host "`nEstado do dispositivo:" -ForegroundColor Cyan
Write-Host "  Service: $($devAfter.Service)"
Write-Host "  Status: $($devAfter.Status)"
Write-Host "  ConfigManagerErrorCode: $($devAfter.ConfigManagerErrorCode)"

$driverInstalledPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$pkgSysPath = Join-Path $root 'driver\wdk\package\output_lab\bc250mcdm.sys'
if (Test-Path $driverInstalledPath) {
    $instHash = (Get-FileHash -LiteralPath $driverInstalledPath -Algorithm SHA256).Hash
    $pkgHash = (Get-FileHash -LiteralPath $pkgSysPath -Algorithm SHA256).Hash
    Write-Host "  Hash instalado: $($instHash.Substring(0,16))"
    Write-Host "  Hash do pacote: $($pkgHash.Substring(0,16))"
    if ($instHash -eq $pkgHash) {
        Write-Host "  Binario em System32\drivers ATUALIZADO com sucesso!" -ForegroundColor Green
    } else {
        Write-Host "  AVISO: Binario no System32 nao atualizou automaticamente; copiando..." -ForegroundColor Yellow
        Copy-Item -LiteralPath $pkgSysPath -Destination $driverInstalledPath -Force
    }
}

# Vincular NAO e' ter vinculado: o script ja' anunciou "INSTALADO!" com o
# dispositivo em BasicDisplay e, depois, sem servico nenhum. Conferir aqui, e
# desarmar o proprio watchdog se falhou - deixa-lo armado so' produz mais um
# rollback em cima de um estado que ja' esta quebrado.
if ($devAfter.Service -ne 'bc250mcdm') {
    Write-Host ''
    Write-Host "BINDING NAO PEGOU: dispositivo em '$($devAfter.Service)', esperado bc250mcdm." -ForegroundColor Red
    if ($win32Error -eq 1072) {
        Write-Host 'win32=1072 = ERROR_SERVICE_MARKED_FOR_DELETE.' -ForegroundColor Red
        Write-Host 'O servico bc250mcdm esta pendente de exclusao e nao pode ser recriado' -ForegroundColor Yellow
        Write-Host 'ate' + [char]39 + ' o sistema soltar a referencia. Isso exige REINICIAR.' -ForegroundColor Yellow
    }
    if (-not $DisableWatchdog) {
        Write-Host 'Desarmando o watchdog desta tentativa para nao empilhar rollbacks.' -ForegroundColor Yellow
    }
    if ($null -ne $watchdogProcess -and -not $watchdogProcess.HasExited) {
        Stop-Process -Id $watchdogProcess.Id -Force -ErrorAction SilentlyContinue
    }
    throw "Binding de laboratorio falhou (win32=$win32Error)."
}

# Captura imediata da telemetria, ANTES de o watchdog poder disparar.
#
# No ciclo 3.30 o watchdog expirou e fez rollback automatico antes de o
# confirm rodar: o servico bc250mcdm sumiu levando a chave Probe junto e a
# evidencia canonica do ensaio se perdeu. O ensaio e caro e so cabe uma
# submissao por boot; a captura nao pode depender de uma acao humana dentro
# da janela do watchdog.
$probeKeyPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
try {
    if (Test-Path -LiteralPath $probeKeyPath) {
        $probeSnapshot = Get-ItemProperty -LiteralPath $probeKeyPath -ErrorAction Stop
        $probeOrdered = [ordered]@{}
        foreach ($probeProperty in ($probeSnapshot.PSObject.Properties |
                Where-Object { $_.Name -notlike 'PS*' } |
                Sort-Object Name)) {
            $probeOrdered[$probeProperty.Name] = $probeProperty.Value
        }
        $probeOrdered | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $evidenceDir 'probe-registry-at-bind.json') -Encoding UTF8
        $probeRegPath = 'HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
        & reg.exe export $probeRegPath (Join-Path $evidenceDir 'probe-registry-at-bind.txt') /y | Out-Null
        Write-Host ("  Telemetria capturada no bind: {0} valores." -f $probeOrdered.Count) -ForegroundColor Cyan
    } else {
        Write-Host '  AVISO: chave de telemetria ausente logo apos o binding.' -ForegroundColor Yellow
    }
} catch {
    # Best-effort: a captura nunca pode derrubar o ensaio nem o watchdog.
    Write-Host ("  AVISO: falha ao capturar telemetria no bind: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
}

Write-Host "`n==========================================================================" -ForegroundColor Green
Write-Host "DRIVER DE LABORATORIO INSTALADO!" -ForegroundColor Green
if ($DisableWatchdog) {
    Write-Host 'Watchdog DESABILITADO; nao ha confirmacao temporizada a executar.' -ForegroundColor Yellow
    Write-Host "Em caso de falha use: $recoveryPath" -ForegroundColor Yellow
} else {
    Write-Host "O Watchdog continua armado por $WatchdogTimeoutSeconds segundos." -ForegroundColor Yellow
    if ($EnablePhysicalRenderOnly) {
        Write-Host 'Ensaio diagnostico: NAO desarme. O guardiao capturara a telemetria,' -ForegroundColor Yellow
        Write-Host 'removera o driver e reiniciara automaticamente se necessario.' -ForegroundColor Yellow
    } else {
        Write-Host "Se o video estiver visivel, confirme para desarmar o rollback:" -ForegroundColor Yellow
        Write-Host "powershell -NoProfile -ExecutionPolicy Bypass -File `"$confirmScript`" -EvidenceDir `"$evidenceDir`" -InstanceId `"$($device.PNPDeviceID)`" -Token `"$watchdogToken`" -IConfirmDisplayIsVisible"
    }
}
Write-Host "==========================================================================" -ForegroundColor Green

[ordered]@{
    evidence_dir = $evidenceDir
    watchdog_token = $watchdogToken
    watchdog_disabled = [bool]$DisableWatchdog
    instance_id = $device.PNPDeviceID
    published_inf = $publishedName
    # Registro duravel do que foi realmente instalado. A quarentena
    # depende disto para reconhecer o residuo mesmo depois de o
    # pacote ter sido reconstruido para um candidato posterior.
    package_sys_sha256 = (Get-FileHash -LiteralPath $pkgSysPath -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'tools\_last_lab_binding.json') -Encoding UTF8
