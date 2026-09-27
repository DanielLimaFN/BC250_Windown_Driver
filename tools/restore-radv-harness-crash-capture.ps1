<#
.SYNOPSIS
    Desarma LocalDumps do harness e recolhe os dumps para a evidencia.

.DESCRIPTION
    Par de tools\arm-radv-harness-crash-capture.ps1. A ordem importa: primeiro
    devolve o Registro ao estado anterior, so depois copia os dumps. Um dump
    ainda sendo escrito nunca pode deixar a configuracao global do WER presa em
    ARMED.

    Se algum dump nao puder ser copiado (o WER ainda o mantem aberto), o estado
    fica em RESTORED_PENDING_DUMPS e o script pode ser reexecutado: o Registro
    ja restaurado nao e mexido de novo.

    Idempotente por construcao. Nao apaga nada fora da pasta exclusiva criada
    pelo arm.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir
)

$ErrorActionPreference = 'Stop'
$imageName = 'bc250-radv-compute-gate.exe'
$werKey = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\$imageName"
$statePath = Join-Path $EvidenceDir 'radv-harness-localdumps-state.json'
$snapshotPath = Join-Path $EvidenceDir 'radv-harness-localdumps-before.reg'

if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
    Write-Host 'RADV LOCALDUMPS: nada a restaurar (state file ausente).'
    exit 0
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if ([string]$state.state -eq 'RESTORED') {
    Write-Host 'RADV LOCALDUMPS: ja restaurado.'
    exit 0
}
if ([string]$state.state -eq 'FAILED_RESTORED') {
    Write-Host 'RADV LOCALDUMPS: o arm ja desfez a propria escrita.'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Restore RADV exige PowerShell elevado.'
}

$dumpFolder = [string]$state.dump_folder
$registryRestored = ([string]$state.state -eq 'RESTORED_PENDING_DUMPS')
if (-not $registryRestored) {
    Remove-Item -LiteralPath $werKey -Recurse -Force -ErrorAction SilentlyContinue
    if ([bool]$state.prior_key_existed) {
        if (-not (Test-Path -LiteralPath $snapshotPath -PathType Leaf)) {
            throw 'Restore RADV bloqueado: snapshot anterior ausente.'
        }
        & reg.exe import $snapshotPath | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'Restore RADV bloqueado: falha ao importar snapshot anterior.'
        }
    }
    $registryRestored = $true
    $state | Add-Member -NotePropertyName registry_restored_at `
        -NotePropertyValue ([datetime]::Now.ToString('o')) -Force
}

$dumpCount = 0
$copyFailures = @()
if (Test-Path -LiteralPath $dumpFolder -PathType Container) {
    $dumps = @(Get-ChildItem -LiteralPath $dumpFolder -Filter '*.dmp' -File `
        -ErrorAction SilentlyContinue | Sort-Object LastWriteTime)
    foreach ($dump in $dumps) {
        $targetName = 'radv-harness-crash-{0:D2}-{1}' -f ($dumpCount + 1),$dump.Name
        $targetPath = Join-Path $EvidenceDir $targetName
        $copied = $false
        for ($attempt = 1; $attempt -le 12 -and -not $copied; $attempt++) {
            try {
                Copy-Item -LiteralPath $dump.FullName -Destination $targetPath -Force
                $copied = $true
            } catch [System.IO.IOException] {
                if ($attempt -lt 12) {
                    Start-Sleep -Milliseconds 250
                }
            }
        }
        if ($copied) {
            $dumpCount++
        } else {
            $copyFailures += $dump.FullName
        }
    }
}

$cleanupPending = $false
if ($copyFailures.Count -eq 0 -and
    (Test-Path -LiteralPath $dumpFolder -PathType Container)) {
    try {
        Remove-Item -LiteralPath $dumpFolder -Recurse -Force
    } catch [System.IO.IOException] {
        $cleanupPending = $true
    }
}

$state.state = if ($copyFailures.Count -eq 0 -and -not $cleanupPending) {
    'RESTORED'
} else {
    'RESTORED_PENDING_DUMPS'
}
$state | Add-Member -NotePropertyName dump_files_collected -NotePropertyValue $dumpCount -Force
$state | Add-Member -NotePropertyName pending_dump_files -NotePropertyValue $copyFailures -Force
$state | Add-Member -NotePropertyName dump_folder_cleanup_pending -NotePropertyValue $cleanupPending -Force
$state | Add-Member -NotePropertyName restored_at `
    -NotePropertyValue ([datetime]::Now.ToString('o')) -Force
$state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
if ($copyFailures.Count -eq 0 -and -not $cleanupPending) {
    Write-Host "RADV LOCALDUMPS RESTORED: dumps=$dumpCount"
} else {
    Write-Warning ("RADV LOCALDUMPS REGISTRY RESTORED; dumps pendentes={0}; " +
        "limpeza pendente={1}. Execute novamente para concluir." -f
        $copyFailures.Count,$cleanupPending)
}
