[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$ForceBinding,
    [switch]$IAcceptBlackScreenRisk
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $root 'driver\wdk\package\output'
$packageInf = Join-Path $packageDir 'bc250readonly.inf'
$packageSys = Join-Path $packageDir 'bc250readonly.sys'
$packageCat = Join-Path $packageDir 'bc250readonly.cat'
$marker = 'C:\BC250_TEST_OEMINF.txt'
$recoveryRoot = 'C:\BC250_RECOVERY.cmd'
$recoverySource = Join-Path $root 'tools\BC250_RECOVERY.cmd'
$collector = Join-Path $root 'tools\capture-bc250readonly-activation.ps1'
$expectedThumbprint = '8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B'
$expectedInstancePrefix = 'PCI\VEN_1002&DEV_13FE'
$expectedHardwareId = 'PCI\VEN_1002&DEV_13FE'

function Test-Bc250ReadonlyInf([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $text = Get-Content -LiteralPath $Path -Raw
    return ($text -match '(?im)^Class=ComputeAccelerator\s*$' -and
        $text -match '(?im)^ClassGuid=\{F01A9D53-3FF6-48D2-9F97-C8A7004BE10C\}\s*$' -and
        $text -match '(?im)^DriverVer=08/25/2026,0\.5\.0\.0\s*$' -and
        $text -match '(?im)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250readonly,' -and
        $text -match '(?im)^ServiceBinary=%12%\\bc250readonly\.sys\s*$')
}

foreach ($path in @($packageInf,$packageSys,$packageCat,$collector,$recoveryRoot)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Artefato ausente: $path" }
}
if ((Get-FileHash $recoveryRoot).Hash -ne (Get-FileHash $recoverySource).Hash) {
    throw 'Recuperacao em C:\ diverge da fonte versionada.'
}
if (-not (Test-Bc250ReadonlyInf $packageInf)) {
    throw 'INF 0.5.0.0 do workspace nao passou na identidade estrita.'
}
foreach ($signedPath in @($packageSys,$packageCat)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signedPath
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Thumbprint -ne $expectedThumbprint) {
        throw "Assinatura invalida ou inesperada: $signedPath"
    }
}

$markerLines = @(Get-Content -LiteralPath $marker)
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem[0-9]+\.inf$') {
    throw 'Marcador OEM ausente ou invalido.'
}
$publishedName = $markerLines[0].ToLowerInvariant()
$installedInf = Join-Path $env:SystemRoot "INF\$publishedName"
if (-not (Test-Bc250ReadonlyInf $installedInf)) {
    throw "$publishedName nao e o pacote BC250 readonly 0.5.0.0 esperado."
}
$matchingPackages = @(
    Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
        Where-Object { Test-Bc250ReadonlyInf $_.FullName }
)
if ($matchingPackages.Count -ne 1 -or
    $matchingPackages[0].Name.ToLowerInvariant() -ne $publishedName) {
    throw 'Cardinalidade do pacote BC250 readonly no Driver Store nao e 1.'
}

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$expectedInstancePrefix*"
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'BC-250 nao esta exclusivamente em BasicDisplay saudavel.'
}
$device = $devices[0]
if (Get-Service bc250v4 -ErrorAction SilentlyContinue) {
    throw 'Servico legado bc250v4 existe.'
}
if (Get-Service bc250readonly -ErrorAction SilentlyContinue) {
    throw 'Servico bc250readonly ja existe antes da ativacao.'
}
$dbgService = Get-Service bc250dbg -ErrorAction SilentlyContinue
if ($dbgService -and $dbgService.Status -notin @('Running','Stopped')) {
    throw "bc250dbg esta em estado inesperado: $($dbgService.Status)."
}

$recoveryAudit = (& cmd.exe /d /c $recoveryRoot /DRY-RUN 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or
    $recoveryAudit -notmatch [regex]::Escape("Pacote OEM registrado para o teste: $publishedName")) {
    throw 'DRY-RUN da recuperacao nao reconheceu o pacote publicado.'
}

Write-Host "Preflight aprovado: $publishedName, BC-250 em BasicDisplay, recovery pronta."
Write-Warning 'Com -Execute, a tela pode apagar imediatamente e permanecer preta.'
Write-Warning 'Rollback: Modo de Seguranca -> C:\BC250_RECOVERY.cmd -> reboot.'
if ($ForceBinding) {
    Write-Warning 'Modo FORCE: o ranking do display.inf sera deliberadamente ignorado.'
}
if (-not $Execute) {
    if ($ForceBinding) {
        Write-Host 'Zero escrita: para executar, acrescente -Execute -IAcceptBlackScreenRisk.'
    } else {
        Write-Host 'Zero escrita: repita com -Execute somente quando estiver pronto.'
    }
    Write-Host 'Veredito: NAO VALIDADO EM HARDWARE'
    exit 0
}

if ($ForceBinding -and -not $IAcceptBlackScreenRisk) {
    throw 'Binding forcado exige -IAcceptBlackScreenRisk.'
}
if ($IAcceptBlackScreenRisk -and -not $ForceBinding) {
    throw '-IAcceptBlackScreenRisk so e valido junto de -ForceBinding.'
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado.'
}

if ($ForceBinding) {
    if (-not [Environment]::Is64BitProcess) {
        throw 'Binding forcado exige PowerShell 64-bit.'
    }
    if (-not ('Bc250NewDev.NativeMethods' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Bc250NewDev
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
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$hostName = $env:COMPUTERNAME
$evidenceSuffix = if ($ForceBinding) { 'mcdm-force-activation' } else { 'mcdm-activation' }
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$hostName-$evidenceSuffix"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
$startTime = Get-Date

$manifest = [ordered]@{
    started = $startTime.ToString('o')
    published_inf = $publishedName
    instance_id = $device.PNPDeviceID
    service_before = $device.Service
    bc250dbg_before = if ($dbgService) { [string]$dbgService.Status } else { 'Absent' }
    problem_code_before = $device.ConfigManagerErrorCode
    activation_mode = if ($ForceBinding) { 'UpdateDriverForPlugAndPlayDevicesW_INSTALLFLAG_FORCE' } else { 'PnPUtil_ranked_selection' }
    package_inf_sha256 = (Get-FileHash $packageInf).Hash
    package_sys_sha256 = (Get-FileHash $packageSys).Hash
    package_cat_sha256 = (Get-FileHash $packageCat).Hash
    recovery_sha256 = (Get-FileHash $recoveryRoot).Hash
    verdict = 'NAO VALIDADO EM HARDWARE'
}
$manifest | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'manifest-before.json') -Encoding UTF8
$recoveryAudit |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'recovery-dry-run-before.txt') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $env:SystemRoot 'INF\setupapi.dev.log') -Tail 1200 |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'setupapi-before-tail.txt') -Encoding UTF8
@'
SE A TELA ESTIVER PRETA:
1. Inicie o Windows em Modo de Seguranca com Prompt de Comando.
2. Execute C:\BC250_RECOVERY.cmd
3. Reinicie o Windows.
'@ | Set-Content -LiteralPath (Join-Path $evidenceDir 'RECOVERY.txt') -Encoding ASCII

if ($dbgService -and (Get-Service bc250dbg).Status -ne 'Stopped') {
    Stop-Service bc250dbg -Force
    (Get-Service bc250dbg).WaitForStatus('Stopped',[timespan]::FromSeconds(10))
}
if ($dbgService -and (Get-Service bc250dbg).Status -ne 'Stopped') {
    throw 'bc250dbg nao parou.'
}

$collectorArguments =
    "-NoProfile -ExecutionPolicy Bypass -File `"$collector`" " +
    "-EvidenceDir `"$evidenceDir`" " +
    "-InstanceId `"$($device.PNPDeviceID)`" " +
    "-StartTimeIso `"$($startTime.ToString('o'))`""
$collectorProcess = Start-Process -FilePath powershell.exe `
    -ArgumentList $collectorArguments -WindowStyle Hidden -PassThru

if ($ForceBinding) {
    $installFlagForce = [uint32]0x00000001
    $rebootRequired = $false
    $nativeOk = [Bc250NewDev.NativeMethods]::UpdateDriverForPlugAndPlayDevicesW(
        [IntPtr]::Zero,
        $expectedHardwareId,
        $packageInf,
        $installFlagForce,
        [ref]$rebootRequired)
    $installExit = if ($nativeOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
    $installResult = [ordered]@{
        api = 'UpdateDriverForPlugAndPlayDevicesW'
        hardware_id = $expectedHardwareId
        inf = $packageInf
        install_flags = ('0x{0:X8}' -f $installFlagForce)
        success = $nativeOk
        win32_error = $installExit
        reboot_required = $rebootRequired
    }
    $installResult | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'force-binding-result.json') -Encoding UTF8
    $installResult | Format-List | Out-String | Write-Host
} else {
    $installOutput = (& pnputil.exe /add-driver $packageInf /install 2>&1) -join "`n"
    $installExit = $LASTEXITCODE
    $installOutput |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'pnputil-install.txt') -Encoding UTF8
    $installOutput | Write-Host
    "pnputil_exit=$installExit" |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'pnputil-exit.txt') -Encoding ASCII
}

try {
    Wait-Process -Id $collectorProcess.Id -Timeout 45 -ErrorAction Stop
} catch {
    Write-Warning 'Coletor ainda esta em execucao; ele continuara oculto.'
}

$deviceAfterPath = Join-Path $evidenceDir 'device-after.json'
$probeAfterPath = Join-Path $evidenceDir 'probe-registry.json'
if (Test-Path -LiteralPath $deviceAfterPath) {
    $deviceAfter = Get-Content -LiteralPath $deviceAfterPath -Raw | ConvertFrom-Json
    $bindingState = if ($deviceAfter.Service -eq 'bc250readonly') {
        if ($deviceAfter.ConfigManagerErrorCode -eq 0) {
            'bc250readonly_active'
        } else {
            'bc250readonly_selected_with_pnp_error'
        }
    } elseif ($deviceAfter.Service -eq 'BasicDisplay') {
        'basicdisplay_retained_or_restored'
    } else {
        'unexpected_service'
    }
    $activationVerdict = [ordered]@{
        binding_state = $bindingState
        service_after = $deviceAfter.Service
        problem_code_after = $deviceAfter.ConfigManagerErrorCode
        telemetry_present = (Test-Path -LiteralPath $probeAfterPath)
        hardware_validation = 'NAO VALIDADO EM HARDWARE'
    }
    $activationVerdict | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'activation-verdict.json') -Encoding UTF8
    Write-Host "Binding observado: $bindingState; servico=$($deviceAfter.Service); codigo=$($deviceAfter.ConfigManagerErrorCode)"
} else {
    Write-Warning 'Coletor ainda nao produziu device-after.json; classificar pela pasta de evidencia.'
}

Write-Host "Evidencia: $evidenceDir"
if ($ForceBinding) {
    Write-Host "UpdateDriverForPlugAndPlayDevicesW: success=$nativeOk win32=$installExit reboot=$rebootRequired"
} elseif ($installExit -eq 259) {
    Write-Warning 'Binding NAO ocorreu: ERROR_NO_MORE_ITEMS (259), pacote superado pelo ranking.'
} else {
    Write-Host "pnputil exit: $installExit"
}
Write-Host 'Nao reinicie bc250dbg antes de analisar o binding e a telemetria.'
Write-Host 'Veredito: NAO VALIDADO EM HARDWARE'
if ($ForceBinding -and -not $nativeOk) { exit 1 }
if (-not $ForceBinding -and $installExit -notin @(0,259)) { exit 1 }
exit 0
