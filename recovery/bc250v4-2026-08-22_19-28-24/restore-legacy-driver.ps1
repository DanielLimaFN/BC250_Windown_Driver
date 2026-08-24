param(
    [string]$BackupDirectory,
    [switch]$Reboot
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute como Administrador.'
}

if (-not $BackupDirectory) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'manifest.json')) {
        $BackupDirectory = $PSScriptRoot
    } else {
        $latest = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'recovery') `
            -Directory -Filter 'bc250v4-*' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $latest) {
            throw 'Backup bc250v4 nao encontrado em recovery\.'
        }
        $BackupDirectory = $latest.FullName
    }
}

$BackupDirectory = (Resolve-Path -LiteralPath $BackupDirectory).Path
$manifestPath = Join-Path $BackupDirectory 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Manifesto de recuperacao ausente: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.service -ne 'bc250v4' -or $manifest.hardware_id -ne 'PCI\VEN_1002&DEV_13FE') {
    throw 'Manifesto nao pertence ao bc250v4/BC-250 esperado.'
}

$inf = Get-ChildItem -LiteralPath (Join-Path $BackupDirectory 'driver-package') `
    -Recurse -File -Filter '*.inf' | Where-Object {
        (Get-Content -LiteralPath $_.FullName -Raw) -match 'DEV_13FE' -and
        (Get-Content -LiteralPath $_.FullName -Raw) -match 'bc250v4'
    } | Select-Object -First 1
if (-not $inf) {
    throw 'INF exportado do bc250v4 nao encontrado ou nao confere.'
}

Write-Host "[*] restaurando pacote: $($inf.FullName)"
& pnputil.exe /add-driver $inf.FullName /install
if ($LASTEXITCODE -ne 0) {
    throw "PnPUtil falhou ao restaurar o driver (exit $LASTEXITCODE)."
}
& pnputil.exe /scan-devices | Out-Host

Write-Host 'OK: pacote legado restaurado.' -ForegroundColor Green
if ($Reboot) {
    shutdown.exe /r /t 15 /c 'Restauracao do driver BC-250 concluida'
} else {
    Write-Host 'Reinicie o Windows para concluir: shutdown /r /t 0'
}
