[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$Token,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$werKey = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\dwm.exe'
$werKeyNative = 'HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\dwm.exe'
$statePath = Join-Path $EvidenceDir 'dwm-localdumps-state.json'
$snapshotPath = Join-Path $EvidenceDir 'dwm-localdumps-before.reg'
$parsedToken = [guid]::Empty

if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw 'Captura DWM recusada: diretorio de evidencia ausente.'
}
if (-not [guid]::TryParseExact($Token, 'D', [ref]$parsedToken)) {
    throw 'Captura DWM recusada: token invalido.'
}
$dumpFolder = Join-Path $env:SystemRoot ("Temp\BC250_DWM_DUMPS_{0}" -f $parsedToken.ToString('N'))

if (-not $Execute) {
    Write-Host "DWM LOCALDUMPS DRY-RUN: key=$werKeyNative folder=$dumpFolder"
    Write-Host 'Zero escrita no Registro e zero criacao de pasta.'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Captura DWM exige PowerShell elevado.'
}
if (Test-Path -LiteralPath $statePath) {
    throw 'Captura DWM recusada: state file ja existe.'
}
if (Test-Path -LiteralPath $dumpFolder) {
    throw 'Captura DWM recusada: pasta exclusiva ja existe.'
}

$priorExists = Test-Path -LiteralPath $werKey
if ($priorExists) {
    & reg.exe export $werKeyNative $snapshotPath /y | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $snapshotPath)) {
        throw 'Captura DWM recusada: nao foi possivel preservar a configuracao WER anterior.'
    }
}

$captureState = [ordered]@{
    state = 'PREPARING'
    token = $Token
    registry_key = $werKeyNative
    dump_folder = $dumpFolder
    prior_key_existed = $priorExists
    snapshot_file = if ($priorExists) { $snapshotPath } else { $null }
    dump_count = 2
    dump_type = 2
    preparing_at = [datetime]::Now.ToString('o')
    hardware_validated = $false
}
$captureState | ConvertTo-Json |
    Set-Content -LiteralPath $statePath -Encoding UTF8

try {
    New-Item -ItemType Directory -Path $dumpFolder | Out-Null
    $acl = Get-Acl -LiteralPath $dumpFolder
    $worldSid = New-Object Security.Principal.SecurityIdentifier(
        [Security.Principal.WellKnownSidType]::WorldSid, $null)
    $rule = New-Object Security.AccessControl.FileSystemAccessRule(
        $worldSid,
        [Security.AccessControl.FileSystemRights]::Modify,
        [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit',
        [Security.AccessControl.PropagationFlags]::None,
        [Security.AccessControl.AccessControlType]::Allow)
    $acl.AddAccessRule($rule)
    Set-Acl -LiteralPath $dumpFolder -AclObject $acl

    New-Item -Path $werKey -Force | Out-Null
    New-ItemProperty -LiteralPath $werKey -Name DumpFolder -PropertyType ExpandString `
        -Value $dumpFolder -Force | Out-Null
    New-ItemProperty -LiteralPath $werKey -Name DumpCount -PropertyType DWord `
        -Value 2 -Force | Out-Null
    New-ItemProperty -LiteralPath $werKey -Name DumpType -PropertyType DWord `
        -Value 2 -Force | Out-Null

    $readback = Get-ItemProperty -LiteralPath $werKey
    if ([string]$readback.DumpFolder -ne $dumpFolder -or
        [uint32]$readback.DumpCount -ne [uint32]2 -or
        [uint32]$readback.DumpType -ne [uint32]2) {
        throw 'Readback da configuracao LocalDumps divergiu.'
    }

    $captureState.state = 'ARMED'
    $captureState.armed_at = [datetime]::Now.ToString('o')
    $captureState | ConvertTo-Json |
        Set-Content -LiteralPath $statePath -Encoding UTF8
} catch {
    Remove-Item -LiteralPath $werKey -Recurse -Force -ErrorAction SilentlyContinue
    if ($priorExists -and (Test-Path -LiteralPath $snapshotPath)) {
        & reg.exe import $snapshotPath | Out-Null
    }
    if (Test-Path -LiteralPath $dumpFolder) {
        Remove-Item -LiteralPath $dumpFolder -Recurse -Force -ErrorAction SilentlyContinue
    }
    $captureState.state = 'FAILED_RESTORED'
    $captureState.failed_at = [datetime]::Now.ToString('o')
    $captureState | ConvertTo-Json |
        Set-Content -LiteralPath $statePath -Encoding UTF8
    throw
}

Write-Host "DWM LOCALDUMPS ARMED: $dumpFolder"
Write-Host 'NAO VALIDADO EM HARDWARE'
