#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Registra o manifesto do Vulkan ICD da AMD BC-250 no Windows Registry.

.DESCRIPTION
    Adiciona a chave HKLM\SOFTWARE\Khronos\Vulkan\Drivers apontando para o arquivo
    bc250_icd.json compilado em driver\umd\output\bc250_icd.json.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Get-Item $scriptDir).Parent.FullName
$icdJsonPath = Join-Path $rootDir "driver\umd\output\bc250_icd.json"

if (-not (Test-Path $icdJsonPath)) {
    Write-Error "Manifesto ICD nao encontrado: $icdJsonPath. Execute driver\umd\build-umd.bat primeiro."
}

$regPath = "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers"
if (-not (Test-Path $regPath)) {
    New-Item -Path $regPath -Force | Out-Null
}

New-ItemProperty -Path $regPath -Name $icdJsonPath -Value 0 -PropertyType DWord -Force | Out-Null

Write-Host "=======================================================================" -ForegroundColor Green
Write-Host "   AMD BC-250 VULKAN ICD REGISTRADO COM SUCESSO NO WINDOWS!" -ForegroundColor Green
Write-Host "   Manifesto: $icdJsonPath" -ForegroundColor Cyan
Write-Host "   Registro:  $regPath" -ForegroundColor Cyan
Write-Host "=======================================================================" -ForegroundColor Green
