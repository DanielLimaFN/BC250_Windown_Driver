[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptDisplayInterruptionRisk
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$recoveryAudit = Join-Path $root 'tools\audit-bc250-recovery-readiness.ps1'
$prebindingAudit = Join-Path $root 'tools\audit-bc250mcdm-prebinding.ps1'
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'

foreach ($path in @($recoveryAudit,$prebindingAudit,$manifestPath,$markerPath,'C:\BC250_RECOVERY.cmd')) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Artefato ausente: $path" }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (-not (Test-Path -LiteralPath $driverPath)) {
    throw 'Rollback recusado: bc250mcdm.sys instalado ausente.'
}
$installedDriverHash = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
$expectedDriverHash = [string]$manifest.PackageSysSha256
if ($installedDriverHash -ne $expectedDriverHash) {
    throw 'Rollback recusado: hash do bc250mcdm.sys instalado diverge do pacote autorizado.'
}
$installedSignature = Get-AuthenticodeSignature -LiteralPath $driverPath
if ($installedSignature.Status -ne 'Valid' -or $null -eq $installedSignature.SignerCertificate -or
    $installedSignature.SignerCertificate.Thumbprint -ne $manifest.SignerThumbprint) {
    throw 'Rollback recusado: assinatura do bc250mcdm.sys instalado nao corresponde ao signer autorizado.'
}

$preflightDevices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
if ($preflightDevices.Count -ne 1 -or $preflightDevices[0].Service -ne 'bc250mcdm' -or
    $preflightDevices[0].ConfigManagerErrorCode -notin @(0,43)) {
    throw 'Rollback recusado: binding atual nao e bc250mcdm Codigo 0/43.'
}

$markerLines = @(Get-Content -LiteralPath $markerPath)
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem\d+\.inf$') {
    throw 'Rollback recusado: marcador nao contem um unico nome OEM valido.'
}
$targetInf = $markerLines[0].ToLowerInvariant()
$installedInf = Join-Path $env:SystemRoot "INF\$targetInf"
if (-not (Test-Path -LiteralPath $installedInf)) {
    throw "Rollback recusado: $installedInf ausente."
}
$infText = Get-Content -LiteralPath $installedInf -Raw
$installedInfHash = (Get-FileHash -LiteralPath $installedInf -Algorithm SHA256).Hash
if ($installedInfHash -ne [string]$manifest.PackageInfSha256) {
    throw "Rollback recusado: hash de $targetInf diverge do pacote autorizado."
}
if ($infText -notmatch '(?im)^Class=(ComputeAccelerator|Display)\s*$') {
    throw "Rollback recusado: classe nao autorizada em $targetInf."
}
foreach ($identity in @(
    'PCI\VEN_1002&DEV_13FE',
    'AddService=bc250mcdm,',
    'ServiceBinary=%12%\bc250mcdm.sys'
)) {
    if ($infText -notmatch [regex]::Escape($identity)) {
        throw "Rollback recusado: identidade ausente em ${targetInf}: $identity"
    }
}

Write-Host "Preflight aprovado: remover somente $targetInf e restaurar BasicDisplay."
Write-Host "  installed SYS SHA256: $installedDriverHash"
Write-Host "  signer: $($installedSignature.SignerCertificate.Thumbprint)"
Write-Warning 'A tela pode piscar ou apagar durante a troca de driver.'
Write-Warning 'Fallback: Modo de Seguranca -> C:\BC250_RECOVERY.cmd -> reboot.'
if (-not $Execute) {
    if ($IAcceptDisplayInterruptionRisk) {
        throw '-IAcceptDisplayInterruptionRisk nao e valido sem -Execute.'
    }
    Write-Host 'DRY-RUN: zero escrita.'
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 0
}
if (-not $IAcceptDisplayInterruptionRisk) {
    throw 'Rollback recusado: falta -IAcceptDisplayInterruptionRisk.'
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Rollback online exige PowerShell elevado.'
}

$secondDevices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
$secondHash = if (Test-Path -LiteralPath $driverPath) {
    (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
} else { $null }
if ($secondDevices.Count -ne 1 -or $secondDevices[0].Service -ne 'bc250mcdm' -or
    $secondDevices[0].ConfigManagerErrorCode -notin @(0,43) -or
    $secondHash -ne $installedDriverHash) {
    throw 'Estado mudou antes da remocao; rollback abortado.'
}
$secondGate = @(
    'BC250 ONLINE ROLLBACK SECOND GATE: PASS',
    "device service/code: $($secondDevices[0].Service) / $($secondDevices[0].ConfigManagerErrorCode)",
    "installed SYS SHA256: $secondHash",
    "signer: $($installedSignature.SignerCertificate.Thumbprint)"
) -join "`n"

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-mcdm-online-rollback"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
$beforeDevice = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
} | Select-Object Name,PNPDeviceID,Service,Status,ConfigManagerErrorCode)
$beforeDevice | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'device-before.json') -Encoding UTF8
$secondGate | Set-Content -LiteralPath (Join-Path $evidenceDir 'binding-audit-before.txt') -Encoding UTF8
(& cmd.exe /d /c C:\BC250_RECOVERY.cmd /DRY-RUN 2>&1) |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'recovery-dry-run-before.txt') -Encoding UTF8

$deleteOutput = (& pnputil.exe /delete-driver $targetInf /uninstall /force 2>&1) -join "`n"
$deleteExit = $LASTEXITCODE
$deleteOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'pnputil-delete.txt') -Encoding UTF8
$deleteOutput | Write-Host
if ($deleteExit -ne 0) {
    throw "pnputil recusou a remocao de $targetInf (exit=$deleteExit). Use recovery em Modo de Seguranca."
}

$scanOutput = (& pnputil.exe /scan-devices 2>&1) -join "`n"
$scanExit = $LASTEXITCODE
$scanOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'pnputil-scan.txt') -Encoding UTF8
if ($scanExit -ne 0) { throw "scan PnP falhou (exit=$scanExit)." }

$deadline = (Get-Date).AddSeconds(20)
do {
    Start-Sleep -Milliseconds 500
    $devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
        $_.PNPDeviceID -like "$instancePrefix*"
    })
    $restored = ($devices.Count -eq 1 -and $devices[0].Service -eq 'BasicDisplay' -and
        $devices[0].ConfigManagerErrorCode -eq 0)
} until ($restored -or (Get-Date) -ge $deadline)

if (-not $restored) {
    $restartOutput = (& pnputil.exe /restart-device $devices[0].PNPDeviceID 2>&1) -join "`n"
    $restartOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'pnputil-restart.txt') -Encoding UTF8
    if ($LASTEXITCODE -eq 0) {
        $deadline = (Get-Date).AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 500
            $devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
                $_.PNPDeviceID -like "$instancePrefix*"
            })
            $restored = ($devices.Count -eq 1 -and $devices[0].Service -eq 'BasicDisplay' -and
                $devices[0].ConfigManagerErrorCode -eq 0)
        } until ($restored -or (Get-Date) -ge $deadline)
    }
}

$devices | Select-Object Name,PNPDeviceID,Service,Status,ConfigManagerErrorCode |
    ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'device-after-delete.json') -Encoding UTF8
if (-not $restored) {
    throw 'BasicDisplay Codigo 0 nao voltou apos scan/restart. Marcador preservado; tente reboot normal antes do recovery.'
}

$service = Get-Service bc250mcdm -ErrorAction SilentlyContinue
if ($null -ne $service) {
    if ($service.Status -ne 'Stopped') { throw 'Servico bc250mcdm nao esta parado; cleanup online recusado.' }
    & sc.exe delete bc250mcdm | Set-Content -LiteralPath (Join-Path $evidenceDir 'sc-delete.txt') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw 'sc delete bc250mcdm falhou.' }
}

if (Test-Path -LiteralPath $driverPath) {
    $driverHash = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
    if ($driverHash -ne $installedDriverHash) {
        throw 'SYS residual diverge do binario assinado capturado no preflight; arquivo preservado para analise.'
    }
    $quarantineDir = 'C:\BC250_DRIVER_QUARANTINE'
    if (-not (Test-Path -LiteralPath $quarantineDir)) {
        New-Item -ItemType Directory -Path $quarantineDir | Out-Null
    }
    $quarantinePath = Join-Path $quarantineDir "bc250mcdm-$stamp.sys.disabled"
    try {
        Move-Item -LiteralPath $driverPath -Destination $quarantinePath -ErrorAction Stop
    } catch [System.UnauthorizedAccessException] {
        & takeown.exe /F $driverPath /A | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'takeown falhou no binario experimental exato.' }
        & icacls.exe $driverPath /grant '*S-1-5-32-544:F' | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'icacls falhou no binario experimental exato.' }
        Move-Item -LiteralPath $driverPath -Destination $quarantinePath -ErrorAction Stop
    }
}

Remove-Item -LiteralPath $markerPath
if (Test-Path -LiteralPath $markerPath) { throw 'Marcador OEM nao foi removido.' }

Start-Sleep -Seconds 2
$recoveryOutput = (& powershell -NoProfile -ExecutionPolicy Bypass -File $recoveryAudit 2>&1) -join "`n"
$recoveryExit = $LASTEXITCODE
$recoveryOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'recovery-readiness-after.txt') -Encoding UTF8
$prebindingOutput = (& powershell -NoProfile -ExecutionPolicy Bypass -File $prebindingAudit 2>&1) -join "`n"
$prebindingExit = $LASTEXITCODE
$prebindingOutput | Set-Content -LiteralPath (Join-Path $evidenceDir 'prebinding-after.txt') -Encoding UTF8

$finalDevice = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
$matchingInf = @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    Where-Object {
        $text = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        $null -ne $text -and $text -match '(?i)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250mcdm,'
    })
$success = ($recoveryExit -eq 0 -and $prebindingExit -eq 0 -and
    $finalDevice.Count -eq 1 -and $finalDevice[0].Service -eq 'BasicDisplay' -and
    $finalDevice[0].ConfigManagerErrorCode -eq 0 -and $matchingInf.Count -eq 0 -and
    -not (Get-Service bc250mcdm -ErrorAction SilentlyContinue) -and
    -not (Test-Path -LiteralPath $driverPath))

$result = [ordered]@{
    target_inf = $targetInf
    pnputil_exit = $deleteExit
    service_after = if ($finalDevice.Count -eq 1) { $finalDevice[0].Service } else { $null }
    problem_code_after = if ($finalDevice.Count -eq 1) { $finalDevice[0].ConfigManagerErrorCode } else { $null }
    matching_mcdm_inf_after = $matchingInf.Count
    recovery_readiness_exit = $recoveryExit
    prebinding_exit = $prebindingExit
    success = $success
    hardware_acceleration_validated = $false
}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidenceDir 'rollback-result.json') -Encoding UTF8
$result | Format-List | Out-String | Write-Host
Write-Host "Evidencia: $evidenceDir"
if (-not $success) { throw 'Rollback online incompleto; use recovery em Modo de Seguranca.' }
Write-Host 'BC250 ONLINE ROLLBACK: PASS'
Write-Host 'NAO VALIDADO PARA ACELERACAO'
exit 0
