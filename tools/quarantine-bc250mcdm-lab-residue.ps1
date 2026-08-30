[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptExactResidueQuarantine
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$driverPath = [IO.Path]::GetFullPath(
    (Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'))
$expectedDriverPath = [IO.Path]::GetFullPath(
    'C:\Windows\System32\drivers\bc250mcdm.sys')
$quarantineRoot = [IO.Path]::GetFullPath('C:\BC250_DRIVER_QUARANTINE')
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$lastBindingPath = Join-Path $root 'tools\_last_lab_binding.json'

if ($driverPath -ne $expectedDriverPath) {
    throw "Target recusado: caminho resolvido inesperado: $driverPath"
}
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Manifesto de laboratorio ausente: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.BuildKind -ne 'MCDM_LAB_BRINGUP' -or
    [string]::IsNullOrWhiteSpace([string]$manifest.PackageSysSha256)) {
    throw 'Manifesto de laboratorio invalido.'
}
$authorizedHashes = [System.Collections.Generic.List[string]]::new()
$authorizedHashes.Add(([string]$manifest.PackageSysSha256).ToUpperInvariant())
if (Test-Path -LiteralPath $lastBindingPath -PathType Leaf) {
    $lastBinding = Get-Content -LiteralPath $lastBindingPath -Raw | ConvertFrom-Json
    # Fonte primaria e duravel: o hash gravado pelo proprio bind. O
    # fallback pelo relatorio de aceite abaixo so funciona quando o
    # acceptance recebeu -EvidenceDir apontando para o diretorio do
    # binding; sem isso ele cria um diretorio -shader-f8b proprio e o
    # arquivo nunca esta onde esta busca procura.
    $boundHash = ([string]$lastBinding.package_sys_sha256).ToUpperInvariant()
    if ($boundHash -match '^[0-9A-F]{64}$' -and
        -not $authorizedHashes.Contains($boundHash)) {
        $authorizedHashes.Add($boundHash)
    }
    $lastAcceptancePath = Join-Path ([string]$lastBinding.evidence_dir) `
        'shader-dispatch-acceptance.json'
    if (Test-Path -LiteralPath $lastAcceptancePath -PathType Leaf) {
        $lastAcceptance = Get-Content -LiteralPath $lastAcceptancePath -Raw |
            ConvertFrom-Json
        $lastHash = ([string]$lastAcceptance.package_sys_sha256).ToUpperInvariant()
        if ($lastHash -match '^[0-9A-F]{64}$' -and
            -not $authorizedHashes.Contains($lastHash)) {
            $authorizedHashes.Add($lastHash)
        }
    }
}

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -like "$instancePrefix*" })
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'BasicDisplay' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Quarentena recusada: BC-250 nao esta exclusivamente em BasicDisplay Codigo 0.'
}
if (Get-Service -Name bc250mcdm -ErrorAction SilentlyContinue) {
    throw 'Quarentena recusada: servico bc250mcdm ainda existe.'
}
if (Test-Path -LiteralPath $markerPath) {
    throw 'Quarentena recusada: marcador OEM ainda existe.'
}
if (-not (Test-Path -LiteralPath $driverPath -PathType Leaf)) {
    throw 'Quarentena recusada: SYS residual exato nao existe.'
}
$observedHash = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
if (-not $authorizedHashes.Contains($observedHash)) {
    throw "Quarentena recusada: hash residual desconhecido: $observedHash"
}

$matchingPackages = @(Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    Where-Object {
        $text = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        $null -ne $text -and $text -match '(?i)PCI\\VEN_1002&DEV_13FE' -and
        $text -match '(?im)^AddService=bc250mcdm,'
    })
if ($matchingPackages.Count -ne 0) {
    throw "Quarentena recusada: pacote MCDM ainda publicado: $($matchingPackages.Name -join ', ')."
}

Write-Host 'BC250 LAB RESIDUE QUARANTINE PREFLIGHT: PASS'
Write-Host "  target: $driverPath"
Write-Host "  sha256: $observedHash"
Write-Host '  device: BasicDisplay / Codigo 0'
Write-Host '  service/marker/OEM package: absent'
if (-not $Execute) {
    if ($IAcceptExactResidueQuarantine) {
        throw '-IAcceptExactResidueQuarantine nao e valido sem -Execute.'
    }
    Write-Host 'DRY-RUN: zero escrita.'
    exit 0
}
if (-not $IAcceptExactResidueQuarantine) {
    throw 'Quarentena recusada: falta -IAcceptExactResidueQuarantine.'
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Quarentena exige PowerShell elevado.'
}

# Segundo gate imediatamente antes da unica mutacao.
$secondDevices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -like "$instancePrefix*" })
if ($secondDevices.Count -ne 1 -or $secondDevices[0].Service -ne 'BasicDisplay' -or
    $secondDevices[0].ConfigManagerErrorCode -ne 0 -or
    (Get-Service -Name bc250mcdm -ErrorAction SilentlyContinue) -or
    (Test-Path -LiteralPath $markerPath) -or
    -not (Test-Path -LiteralPath $driverPath -PathType Leaf) -or
    -not $authorizedHashes.Contains(
        (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash)) {
    throw 'Quarentena abortada: estado mudou depois do preflight.'
}

if (-not (Test-Path -LiteralPath $quarantineRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $quarantineRoot | Out-Null
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$destination = [IO.Path]::GetFullPath(
    (Join-Path $quarantineRoot "bc250mcdm.pre-f8b2-$stamp.sys"))
if (-not $destination.StartsWith($quarantineRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $destination)) {
    throw "Destino de quarentena recusado: $destination"
}

try {
    Move-Item -LiteralPath $driverPath -Destination $destination -ErrorAction Stop
} catch {
    & takeown.exe /F $driverPath /A | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'takeown falhou no SYS residual exato.' }
    & icacls.exe $driverPath /grant '*S-1-5-32-544:F' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'icacls falhou no SYS residual exato.' }
    Move-Item -LiteralPath $driverPath -Destination $destination -ErrorAction Stop
}

if ((Test-Path -LiteralPath $driverPath) -or
    -not (Test-Path -LiteralPath $destination -PathType Leaf) -or
    (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne
        $observedHash) {
    throw 'Quarentena incompleta: readback do destino falhou.'
}

$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-f8b2-preflight"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
[ordered]@{
    captured_at = [DateTime]::Now.ToString('o')
    source = $driverPath
    destination = $destination
    sha256 = $observedHash
    authorized_hashes = @($authorizedHashes)
    device_service = $secondDevices[0].Service
    device_code = $secondDevices[0].ConfigManagerErrorCode
    action = 'MOVED_TO_RECOVERABLE_QUARANTINE'
    hardware_validated = $false
} | ConvertTo-Json | Set-Content -LiteralPath (
    Join-Path $evidenceDir 'residue-quarantine.json') -Encoding UTF8

Write-Host 'BC250 LAB RESIDUE QUARANTINE: PASS'
Write-Host "  destination: $destination"
Write-Host "  evidence: $evidenceDir"
Write-Host 'NAO VALIDADO EM HARDWARE'
