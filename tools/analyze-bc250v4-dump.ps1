param(
    [string]$DumpPath = 'C:\Windows\MEMORY.DMP'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$installedSys = Join-Path $env:SystemRoot 'System32\drivers\bc250wddm.sys'
$v4Root = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\BC250-V4'
$candidatePdb = Join-Path $v4Root 'build\bc250wddm.pdb'

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute este script em um PowerShell como Administrador para ler MEMORY.DMP.'
}
if (-not (Test-Path -LiteralPath $DumpPath)) {
    throw "Dump nao encontrado: $DumpPath"
}
if (-not (Test-Path -LiteralPath $cdb)) {
    throw "CDB nao encontrado: $cdb"
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $projectRoot "docs\evidence\$stamp-bugcheck-c4"
New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null
$analysisPath = Join-Path $evidenceDir 'analysis.txt'
$metadataPath = Join-Path $evidenceDir 'metadata.json'
$verifierPath = Join-Path $evidenceDir 'verifier-querysettings.txt'

& verifier.exe /querysettings 2>&1 | Out-File -LiteralPath $verifierPath -Encoding utf8

$metadata = [ordered]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    dump_path = $DumpPath
    dump_size = (Get-Item -LiteralPath $DumpPath).Length
    dump_last_write_time = (Get-Item -LiteralPath $DumpPath).LastWriteTime.ToString('o')
    installed_sys = $installedSys
    installed_sys_sha256 = if (Test-Path -LiteralPath $installedSys) {
        (Get-FileHash -LiteralPath $installedSys -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { $null }
    candidate_pdb = if (Test-Path -LiteralPath $candidatePdb) { $candidatePdb } else { $null }
    note = 'O PDB local pode ter idade diferente do binario no dump; CDB registrara a incompatibilidade.'
}
$metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metadataPath -Encoding utf8

$symbolPath = "srv*;C:\Windows\System32\drivers;$v4Root\build"
$commands = @(
    '.symfix'
    ".sympath+ $symbolPath"
    '.reload /f'
    '.bugcheck'
    '!analyze -v'
    'kv'
    'lmvm bc250wddm'
    '!verifier 3 bc250wddm.sys'
    '!verifier 0x80 bc250wddm.sys'
    '!poolused 2'
    'q'
) -join '; '

Write-Host "[*] analisando $DumpPath"
Write-Host "[*] saida: $analysisPath"
& $cdb -z $DumpPath -y $symbolPath -logo $analysisPath -c $commands
$exitCode = $LASTEXITCODE

if (-not (Test-Path -LiteralPath $analysisPath)) {
    throw "CDB nao produziu a analise (exit $exitCode)."
}

Write-Host "Analise concluida (CDB exit $exitCode): $analysisPath" -ForegroundColor Green
Write-Host 'Envie o conteudo de analysis.txt para identificarmos as tags e pilhas pendentes.'
