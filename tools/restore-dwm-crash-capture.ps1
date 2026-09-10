[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$statePath = Join-Path $EvidenceDir 'dwm-localdumps-state.json'
$werKey = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\dwm.exe'
$expectedNativeKey = 'HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\dwm.exe'

if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw 'Restore DWM recusado: diretorio de evidencia ausente.'
}
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
    Write-Host 'Restore DWM: nenhuma captura armada neste ensaio.'
    exit 0
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if ([string]$state.state -in @('RESTORED','FAILED_RESTORED')) {
    Write-Host 'Restore DWM: captura ja restaurada.'
    exit 0
}
if ([string]$state.state -notin @('PREPARING','ARMED','RESTORED_PENDING_DUMPS') -or
    [string]$state.registry_key -ne $expectedNativeKey -or
    [string]$state.dump_folder -notmatch '^C:\\Windows\\Temp\\BC250_DWM_DUMPS_[0-9a-f]{32}$') {
    throw 'Restore DWM recusado: state file nao pertence ao alvo exato.'
}

$dumpFolder = [string]$state.dump_folder
$snapshotPath = Join-Path $EvidenceDir 'dwm-localdumps-before.reg'
if (-not $Execute) {
    Write-Host "DWM LOCALDUMPS RESTORE DRY-RUN: folder=$dumpFolder"
    Write-Host 'Zero copia, zero Registro e zero remocao.'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Restore DWM exige PowerShell elevado.'
}

if ([string]$state.state -ne 'RESTORED_PENDING_DUMPS' -and
    (Test-Path -LiteralPath $werKey)) {
    $readback = Get-ItemProperty -LiteralPath $werKey
    if ([string]$state.state -eq 'ARMED' -and
        ([string]$readback.DumpFolder -ne $dumpFolder -or
        [uint32]$readback.DumpCount -ne [uint32]2 -or
        [uint32]$readback.DumpType -ne [uint32]2)) {
        throw 'Restore DWM bloqueado: configuracao WER mudou desde o armamento.'
    }
}

$registryRestored = ([string]$state.state -eq 'RESTORED_PENDING_DUMPS')
if (-not $registryRestored) {
    # Desarmar o WER vem antes da coleta. Um dump ainda aberto nunca pode deixar
    # a configuracao global presa no estado ARMED.
    Remove-Item -LiteralPath $werKey -Recurse -Force -ErrorAction SilentlyContinue
    if ([bool]$state.prior_key_existed) {
        if (-not (Test-Path -LiteralPath $snapshotPath -PathType Leaf)) {
            throw 'Restore DWM bloqueado: snapshot anterior ausente.'
        }
        & reg.exe import $snapshotPath | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'Restore DWM bloqueado: falha ao importar snapshot anterior.'
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
        $targetName = 'dwm-crash-{0:D2}-{1}' -f ($dumpCount + 1),$dump.Name
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
    Write-Host "DWM LOCALDUMPS RESTORED: dumps=$dumpCount"
} else {
    Write-Warning ("DWM LOCALDUMPS REGISTRY RESTORED; dumps pendentes={0}; " +
        "limpeza pendente={1}. Execute novamente para concluir." -f
        $copyFailures.Count,$cleanupPending)
}
