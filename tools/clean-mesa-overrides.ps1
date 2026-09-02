<#
.SYNOPSIS
    Remove os overrides do Mesa que fazem o driver mentir sobre a versao de
    API suportada, e faz backup do estado anterior.

.DESCRIPTION
    MESA_VK_VERSION_OVERRIDE, MESA_GL_VERSION_OVERRIDE, MESA_GLSL_VERSION_OVERRIDE
    e MESA_EXTENSION_OVERRIDE forcam o Mesa/RADV a declarar suporte a versoes
    que ele pode nao ter. O llama.cpp escolhe caminhos de codigo com base
    nessas declaracoes; com override ativo qualquer diagnostico do backend
    Vulkan e invalido.

    A parte HKCU roda sem privilegio. A parte HKLM exige console elevado.

.PARAMETER Restore
    Reaplica os valores a partir do backup em docs\evidence\<data>_vulkan_rollback.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\clean-mesa-overrides.ps1
#>
[CmdletBinding()]
param(
    [switch]$Restore,
    [string]$BackupDir = "docs\evidence\2026-08-31_vulkan_rollback"
)

$ErrorActionPreference = 'Stop'

$names = @(
    'MESA_VK_VERSION_OVERRIDE',
    'MESA_GL_VERSION_OVERRIDE',
    'MESA_GLSL_VERSION_OVERRIDE',
    'MESA_EXTENSION_OVERRIDE'
)

$hklm = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
$hkcu = 'HKCU:\Environment'

$isAdmin = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($Restore) {
    $reg = Join-Path $BackupDir 'HKCU_Environment.reg'
    if (-not (Test-Path $reg)) {
        throw "Backup nao encontrado: $reg"
    }
    & reg.exe import $reg | Out-Null
    Write-Host "HKCU restaurado de $reg"
    if ($isAdmin) {
        $regm = Join-Path $BackupDir 'HKLM_Environment.reg'
        if (Test-Path $regm) {
            & reg.exe import $regm | Out-Null
            Write-Host "HKLM restaurado de $regm"
        }
    } else {
        Write-Warning 'HKLM nao restaurado: execute em console elevado.'
    }
    return
}

New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
& reg.exe export 'HKCU\Environment' (Join-Path $BackupDir 'HKCU_Environment.reg') /y | Out-Null
if ($isAdmin) {
    & reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' `
        (Join-Path $BackupDir 'HKLM_Environment.reg') /y | Out-Null
}

$pending = @()

foreach ($n in $names) {
    $v = (Get-ItemProperty -Path $hkcu -Name $n -ErrorAction SilentlyContinue).$n
    if ($null -ne $v) {
        Remove-ItemProperty -Path $hkcu -Name $n -Force
        Write-Host "HKCU removido: $n (era '$v')"
    }

    $v = (Get-ItemProperty -Path $hklm -Name $n -ErrorAction SilentlyContinue).$n
    if ($null -ne $v) {
        if ($isAdmin) {
            Remove-ItemProperty -Path $hklm -Name $n -Force
            Write-Host "HKLM removido: $n (era '$v')"
        } else {
            $pending += "$n = '$v'"
        }
    }
}

if ($pending.Count -gt 0) {
    Write-Warning 'Pendente em HKLM (exige console elevado):'
    $pending | ForEach-Object { Write-Warning "  $_" }
    Write-Warning 'Reexecute este script como Administrador para concluir.'
    exit 1
}

Write-Host 'Overrides do Mesa limpos. Abra um novo console para o ambiente valer.'
exit 0
