#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Remove o registro do Vulkan ICD da AMD BC-250 do Windows Registry.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Get-Item $scriptDir).Parent.FullName
$icdJsonPath = Join-Path $rootDir "driver\umd\output\bc250_icd.json"

$regPath = "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers"
if (Test-Path $regPath) {
    Remove-ItemProperty -Path $regPath -Name $icdJsonPath -ErrorAction SilentlyContinue
    Write-Host "Vulkan ICD da BC-250 removido do Windows Registry." -ForegroundColor Yellow
}
