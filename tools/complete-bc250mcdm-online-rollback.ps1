[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptDisplayInterruptionRisk
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'
$targetInf = 'oem19.inf'

$markerLines = @(Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue)
if ($markerLines.Count -ne 1 -or $markerLines[0] -cne $targetInf) {
    throw 'Continuacao recusada: marcador nao identifica exclusivamente oem19.inf.'
}
if (Test-Path -LiteralPath (Join-Path $env:SystemRoot "INF\$targetInf")) {
    throw 'Continuacao recusada: oem19.inf ainda existe; use o rollback principal.'
}
$matchingInf = @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    Where-Object {
        $text = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        $null -ne $text -and $text -match '(?i)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250mcdm,'
    })
if ($matchingInf.Count -ne 0) { throw 'Continuacao recusada: ainda existe INF MCDM publicado.' }

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or
    $devices[0].ConfigManagerErrorCode -notin @(0,43)) {
    throw 'Continuacao recusada: estado PnP nao e BasicDisplay Codigo 0/43.'
}
$service = Get-Service bc250mcdm -ErrorAction SilentlyContinue
if ($null -ne $service -and $service.Status -ne 'Stopped') {
    throw 'Continuacao recusada: bc250mcdm nao esta parado.'
}
if (Test-Path -LiteralPath $driverPath) {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne $manifest.PackageSysSha256) {
        throw 'Continuacao recusada: SYS residual diverge do manifesto.'
    }
}

Write-Host "Rollback parcial confirmado: $targetInf ausente; BasicDisplay Codigo $($devices[0].ConfigManagerErrorCode)."
if (-not $Execute) {
    if ($IAcceptDisplayInterruptionRisk) { throw '-IAcceptDisplayInterruptionRisk nao e valido sem -Execute.' }
    Write-Host 'DRY-RUN: zero escrita.'
    exit 0
}
if (-not $IAcceptDisplayInterruptionRisk) { throw 'Falta -IAcceptDisplayInterruptionRisk.' }
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Continuacao exige PowerShell elevado.'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-mcdm-online-rollback-resume"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null

if ($devices[0].ConfigManagerErrorCode -eq 43) {
    $restartOutput = (& pnputil.exe /restart-device $devices[0].PNPDeviceID 2>&1) -join "`n"
    $restartExit = $LASTEXITCODE
    $restartOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'pnputil-restart.txt') -Encoding UTF8
    $restartOutput | Write-Host
    if ($restartExit -ne 0) { throw "restart-device falhou (exit=$restartExit); tente reboot normal." }
}
(& pnputil.exe /scan-devices 2>&1) |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'pnputil-scan.txt') -Encoding UTF8

$deadline = (Get-Date).AddSeconds(20)
do {
    Start-Sleep -Milliseconds 500
    $devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
        $_.PNPDeviceID -like "$instancePrefix*"
    })
    $restored = ($devices.Count -eq 1 -and $devices[0].Service -eq 'BasicDisplay' -and
        $devices[0].ConfigManagerErrorCode -eq 0)
} until ($restored -or (Get-Date) -ge $deadline)
if (-not $restored) { throw 'BasicDisplay continua com erro; tente um reboot normal. Marcador preservado.' }

if (Get-Service bc250mcdm -ErrorAction SilentlyContinue) {
    (& sc.exe delete bc250mcdm 2>&1) |
        Set-Content -LiteralPath (Join-Path $evidenceDir 'sc-delete.txt') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw 'sc delete bc250mcdm falhou.' }
}
if (Test-Path -LiteralPath $driverPath) {
    $quarantineDir = 'C:\BC250_DRIVER_QUARANTINE'
    if (-not (Test-Path -LiteralPath $quarantineDir)) {
        New-Item -ItemType Directory -Path $quarantineDir | Out-Null
    }
    Move-Item -LiteralPath $driverPath -Destination (Join-Path $quarantineDir "bc250mcdm-$stamp.sys.disabled")
}
Remove-Item -LiteralPath $markerPath

Start-Sleep -Seconds 2
$recoveryOutput = (& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\audit-bc250-recovery-readiness.ps1') 2>&1) -join "`n"
$recoveryExit = $LASTEXITCODE
$recoveryOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'recovery-readiness-after.txt') -Encoding UTF8
$prebindingOutput = (& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\audit-bc250mcdm-prebinding.ps1') 2>&1) -join "`n"
$prebindingExit = $LASTEXITCODE
$prebindingOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'prebinding-after.txt') -Encoding UTF8
if ($recoveryExit -ne 0 -or $prebindingExit -ne 0) {
    throw 'Limpeza executada, mas os gates finais falharam; consulte a evidencia.'
}
Write-Host 'BC250 ONLINE ROLLBACK RESUME: PASS'
Write-Host "Evidencia: $evidenceDir"
Write-Host 'BasicDisplay Codigo 0 restaurado; NAO VALIDADO PARA ACELERACAO'
exit 0
