[CmdletBinding()]
param(
    [switch]$LabPackage
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$errors = [System.Collections.Generic.List[string]]::new()
$expectedInstancePrefix = 'PCI\VEN_1002&DEV_13FE'
$marker = 'C:\BC250_TEST_OEMINF.txt'

$packageAudit = Join-Path $root $(if ($LabPackage) {
    'tools\audit-bc250mcdm-lab-package.ps1'
} else {
    'tools\audit-bc250mcdm-package.ps1'
})
foreach ($audit in @(
    (Join-Path $root 'tools\audit-bc250-recovery-readiness.ps1'),
    $packageAudit
)) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $audit
    if ($LASTEXITCODE -ne 0) { $errors.Add("gate dependente falhou: $audit") }
}

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.PNPDeviceID -like "$expectedInstancePrefix*" })
if ($devices.Count -ne 1) {
    $errors.Add("cardinalidade BC-250 inesperada: $($devices.Count)")
} else {
    $device = $devices[0]
    if ($device.Service -ne 'BasicDisplay' -or
        $device.ConfigManagerErrorCode -ne 0) {
        $errors.Add("BC-250 nao esta em BasicDisplay Codigo 0: service=$($device.Service) code=$($device.ConfigManagerErrorCode)")
    }
}

$serviceStates = [ordered]@{}
foreach ($serviceName in @('bc250readonly','bc250mcdm','bc250dbg','bc250v4')) {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    $serviceStates[$serviceName] = if ($service) { [string]$service.Status } else { 'Absent' }
    if ($service) { $errors.Add("servico experimental presente: $serviceName ($($service.Status))") }
}

$driverStates = [ordered]@{}
foreach ($driverName in @('bc250readonly.sys','bc250mcdm.sys','bc250dbg.sys','bc250v4.sys')) {
    $driverPath = Join-Path $env:SystemRoot "System32\drivers\$driverName"
    $present = Test-Path -LiteralPath $driverPath
    $driverStates[$driverName] = $present
    if ($present) { $errors.Add("binario experimental presente: $driverPath") }
}

if (Test-Path -LiteralPath $marker) {
    $errors.Add("marcador OEM preexistente: $marker")
}

$matchingPackages = [System.Collections.Generic.List[string]]::new()
Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    ForEach-Object {
        $infText = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        if ($null -eq $infText -or
            $infText -notmatch '(?im)^Class=(ComputeAccelerator|Display)\s*$' -or
            $infText -notmatch '(?i)PCI\\VEN_1002&DEV_13FE') { return }
        $isReadonly = $infText -match '(?im)^AddService=bc250readonly,' -and
            $infText -match '(?im)^ServiceBinary=%12%\\bc250readonly\.sys\s*$'
        $isMcdm = $infText -match '(?im)^AddService=bc250mcdm,' -and
            $infText -match '(?im)^ServiceBinary=%12%\\bc250mcdm\.sys\s*$'
        if ($isReadonly -or $isMcdm) { $matchingPackages.Add($_.Name) }
    }
if ($matchingPackages.Count -ne 0) {
    $errors.Add("pacotes experimentais no Driver Store: $($matchingPackages -join ', ')")
}

$pendingRebootReasons = [System.Collections.Generic.List[string]]::new()
foreach ($key in @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending',
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired',
    'HKLM:\SYSTEM\CurrentControlSet\Control\Pnp\PnpRebootRequired'
)) {
    if (Test-Path -LiteralPath $key) { $pendingRebootReasons.Add($key) }
}
$sessionManager = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -Name PendingFileRenameOperations -ErrorAction SilentlyContinue
if ($null -ne $sessionManager.PendingFileRenameOperations) {
    $pendingRebootReasons.Add('PendingFileRenameOperations')
}
if ($pendingRebootReasons.Count -ne 0) {
    $errors.Add("reboot pendente: $($pendingRebootReasons -join ', ')")
}

Write-Host 'BC250 MCDM PRE-BINDING AUDIT'
Write-Host "  device count: $($devices.Count)"
if ($devices.Count -eq 1) {
    Write-Host "  device service/code: $($devices[0].Service) / $($devices[0].ConfigManagerErrorCode)"
}
Write-Host "  services: $($serviceStates | ConvertTo-Json -Compress)"
Write-Host "  driver files: $($driverStates | ConvertTo-Json -Compress)"
Write-Host "  matching OEM packages: $($matchingPackages -join ', ')"
Write-Host "  pending reboot: $($pendingRebootReasons -join ', ')"
Write-Host "  package mode: $(if ($LabPackage) { 'LAB' } else { 'PRODUCTION' })"
Write-Host '  mode: READ-ONLY'

if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM PRE-BINDING AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}

Write-Host 'BC250 MCDM PRE-BINDING AUDIT: PASS'
Write-Host 'STAGE/BINDING NOT PERFORMED; NAO VALIDADO EM HARDWARE'
exit 0
