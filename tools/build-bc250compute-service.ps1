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
if (Test-Path "$OutputDir\bc250compute.sys") {
    & $signTool sign /a /v /s PrivateCertStore /n "BC250-TestCert" /t "http://timestamp.digicert.com" "$OutputDir\bc250compute.sys" 2>$null
    if ($LASTEXITCODE -ne 0) {
        & $signTool sign /a /v /s PrivateCertStore /n "BC250-TestCert" "$OutputDir\bc250compute.sys"
    }
}

Write-Host "Compilacao e assinatura de bc250compute concluidas em $OutputDir!"
