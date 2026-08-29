[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'tools\BC250_RECOVERY.cmd'
$installedPath = 'C:\BC250_RECOVERY.cmd'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$errors = [System.Collections.Generic.List[string]]::new()

function Require-Text {
    param([string]$Text, [string]$Needle, [string]$Label)
    if (-not $Text.Contains($Needle)) {
        $errors.Add("ausente $Label`: $Needle")
    }
}

if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Fonte de recovery ausente: $sourcePath"
}
if (-not (Test-Path -LiteralPath $installedPath)) {
    $errors.Add("recovery instalado ausente: $installedPath")
}

$sourceText = Get-Content -LiteralPath $sourcePath -Raw
$installedText = if (Test-Path -LiteralPath $installedPath) {
    Get-Content -LiteralPath $installedPath -Raw
} else { '' }

foreach ($needle in @(
    'call :remove_service bc250mcdm',
    'call :quarantine_driver bc250mcdm.sys',
    'call :show_service bc250mcdm',
    'call :show_file bc250mcdm.sys',
    'findstr.exe /I /C:"AddService=bc250mcdm,"',
    'findstr.exe /I /C:"ServiceBinary=%%12%%\bc250mcdm.sys"'
)) {
    Require-Text $sourceText $needle 'cobertura bc250mcdm na fonte'
    Require-Text $installedText $needle 'cobertura bc250mcdm instalada'
}

$dryRunBranch = $sourceText.IndexOf(
    'if /I "%MODE%"=="DRY-RUN" goto :dry_run')
$firstMutator = $sourceText.IndexOf('fltmc.exe')
if ($dryRunBranch -lt 0 -or $firstMutator -lt 0 -or
    $dryRunBranch -gt $firstMutator -or
    -not $sourceText.Contains('if /I "%MODE%"=="DRY-RUN" set "LOG=NUL"')) {
    $errors.Add('fonte nao prova desvio DRY-RUN antes dos mutadores')
}

$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
$installedHash = if (Test-Path -LiteralPath $installedPath) {
    (Get-FileHash -LiteralPath $installedPath -Algorithm SHA256).Hash
} else { 'ABSENT' }
if ($sourceHash -ne $installedHash) {
    $errors.Add('hash instalado diverge da fonte versionada')
}

& sc.exe query bc250mcdm *> $null
$servicePresent = $LASTEXITCODE -eq 0
$driverPresent = Test-Path -LiteralPath $driverPath
$matchingPackages = [System.Collections.Generic.List[string]]::new()
Get-ChildItem -LiteralPath (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' |
    ForEach-Object {
        $text = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        if ($null -ne $text -and
            $text -match '(?im)^Class=(ComputeAccelerator|Display)\s*$' -and
            $text.Contains('PCI\VEN_1002&DEV_13FE') -and
            $text.Contains('AddService=bc250mcdm,') -and
            $text.Contains('ServiceBinary=%12%\bc250mcdm.sys')) {
            $matchingPackages.Add($_.Name)
        }
    }

Write-Host 'BC250 RECOVERY READINESS AUDIT'
Write-Host "  source SHA256:    $sourceHash"
Write-Host "  installed SHA256: $installedHash"
Write-Host "  bc250mcdm service present: $servicePresent"
Write-Host "  bc250mcdm.sys present:     $driverPresent"
Write-Host "  matching Driver Store INF: $($matchingPackages -join ', ')"
Write-Host '  mode: READ-ONLY; no recovery/install action executed'

if ($errors.Count -ne 0) {
    Write-Host 'BC250 RECOVERY READINESS AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    exit 1
}

Write-Host 'BC250 RECOVERY READINESS AUDIT: PASS'
exit 0
