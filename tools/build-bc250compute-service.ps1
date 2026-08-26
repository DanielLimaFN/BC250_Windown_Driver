# Build script for bc250compute.sys and bc250-compute-cli.exe
$ErrorActionPreference = "Stop"

$PSScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $PSScriptRoot
$WdkDir = "$RepoRoot\driver\wdk"
$OutputDir = "$RepoRoot\driver\wdk\output_compute"

if (!(Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

Write-Host "Compilando bc250compute.sys e bc250-compute-cli.exe..."

cmd.exe /c "driver\wdk\build-contract.bat"

# Compilar bc250-compute-cli.exe com cl.exe
$clPath = (where.exe cl.exe | Select-Object -First 1)
if ($clPath) {
    & cl.exe /nologo /W4 /O2 /Fe"$OutputDir\bc250-compute-cli.exe" "$RepoRoot\tools\bc250-compute-cli.c" /link kernel32.lib advapi32.lib
}

# Assinar bc250compute.sys
$signTool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
$thumb = "8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B"
foreach ($targetSys in @("$OutputDir\bc250compute.sys", "$RepoRoot\driver\wdk\output\bc250compute.sys", "$RepoRoot\driver\wdk\output\bc250compute_pkg.sys")) {
    if (Test-Path $targetSys) {
        & $signTool sign /sha1 $thumb /fd SHA256 $targetSys
    }
}

Write-Host "Compilacao e assinatura de bc250compute concluidas em $OutputDir!"
