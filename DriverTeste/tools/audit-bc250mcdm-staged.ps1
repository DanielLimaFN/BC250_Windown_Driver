[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$infRoot = Join-Path $env:SystemRoot 'INF'
$repositoryRoot = Join-Path $env:SystemRoot 'System32\DriverStore\FileRepository'
$errors = [System.Collections.Generic.List[string]]::new()

foreach ($audit in @(
    (Join-Path $root 'tools\audit-bc250-recovery-readiness.ps1'),
    (Join-Path $root 'tools\audit-bc250mcdm-package.ps1')
)) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $audit
    if ($LASTEXITCODE -ne 0) { $errors.Add("gate dependente falhou: $audit") }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$markerLines = @(if (Test-Path -LiteralPath $markerPath) {
    Get-Content -LiteralPath $markerPath
})
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem\d+\.inf$') {
    $errors.Add('marcador OEM ausente ou invalido')
    $publishedName = $null
} else {
    $publishedName = $markerLines[0].ToLowerInvariant()
}

$matchingInfs = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
Get-ChildItem -LiteralPath $infRoot -Filter 'oem*.inf' | ForEach-Object {
    $text = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($null -ne $text -and
        $text -match '(?im)^Class=ComputeAccelerator\s*$' -and
        $text -match '(?i)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250mcdm,' -and
        $text -match '(?im)^ServiceBinary=%12%\\bc250mcdm\.sys\s*$') {
        $matchingInfs.Add($_)
    }
}
if ($matchingInfs.Count -ne 1) {
    $errors.Add("cardinalidade INF MCDM inesperada: $($matchingInfs.Count)")
} elseif ($null -eq $publishedName -or
    $matchingInfs[0].Name.ToLowerInvariant() -ne $publishedName) {
    $errors.Add('INF MCDM publicado diverge do marcador')
} else {
    $publishedHash = (Get-FileHash -LiteralPath $matchingInfs[0].FullName -Algorithm SHA256).Hash
    if ($publishedHash -ne $manifest.PackageInfSha256) {
        $errors.Add('hash do INF publicado diverge do manifesto')
    }
}

$repositoryDirs = @(Get-ChildItem -LiteralPath $repositoryRoot -Directory -Filter 'bc250mcdm.inf_amd64_*' -ErrorAction SilentlyContinue)
if ($repositoryDirs.Count -ne 1) {
    $errors.Add("cardinalidade FileRepository inesperada: $($repositoryDirs.Count)")
} else {
    foreach ($artifact in @(
        @{ Name = 'bc250mcdm.inf'; Hash = $manifest.PackageInfSha256 },
        @{ Name = 'bc250mcdm.sys'; Hash = $manifest.PackageSysSha256 },
        @{ Name = 'bc250mcdm.cat'; Hash = $manifest.PackageCatSha256 }
    )) {
        $path = Join-Path $repositoryDirs[0].FullName $artifact.Name
        if (-not (Test-Path -LiteralPath $path)) {
            $errors.Add("artefato ausente no FileRepository: $($artifact.Name)")
            continue
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $artifact.Hash) {
            $errors.Add("hash divergente no FileRepository: $($artifact.Name)")
        }
        if ($artifact.Name -in @('bc250mcdm.sys','bc250mcdm.cat')) {
            $signature = Get-AuthenticodeSignature -LiteralPath $path
            if ($signature.Status -ne 'Valid' -or
                $signature.SignerCertificate.Thumbprint -ne $manifest.SignerThumbprint) {
                $errors.Add("assinatura invalida no FileRepository: $($artifact.Name)")
            }
        }
    }
}

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.PNPDeviceID -like 'PCI\VEN_1002&DEV_13FE*' })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    $errors.Add('stage alterou ou nao comprovou BasicDisplay Codigo 0')
}
if (Get-Service bc250mcdm -ErrorAction SilentlyContinue) {
    $errors.Add('servico bc250mcdm apareceu durante stage')
}
if (Test-Path -LiteralPath (Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys')) {
    $errors.Add('bc250mcdm.sys apareceu em System32\drivers durante stage')
}

$recoveryOutput = (& cmd.exe /d /c C:\BC250_RECOVERY.cmd /DRY-RUN 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or $null -eq $publishedName -or
    $recoveryOutput -notmatch [regex]::Escape("Pacote OEM registrado para o teste: $publishedName") -or
    $recoveryOutput -notmatch [regex]::Escape("Pacote experimental BC250 detectado: $publishedName")) {
    $errors.Add('recovery DRY-RUN nao reconheceu INF e marcador do stage')
}

Write-Host 'BC250 MCDM STAGED PACKAGE AUDIT'
Write-Host "  published INF: $publishedName"
Write-Host "  matching INF count: $($matchingInfs.Count)"
Write-Host "  FileRepository count: $($repositoryDirs.Count)"
if ($devices.Count -eq 1) {
    Write-Host "  device service/code: $($devices[0].Service) / $($devices[0].ConfigManagerErrorCode)"
}
Write-Host '  service/System32 SYS created: NO'
Write-Host '  binding performed: NO'

if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM STAGED PACKAGE AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}

Write-Host 'BC250 MCDM STAGED PACKAGE AUDIT: PASS (STAGE ONLY)'
Write-Host 'NAO VALIDADO EM HARDWARE'
exit 0
