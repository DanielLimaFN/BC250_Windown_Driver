<#
.SYNOPSIS
    Arma LocalDumps do WER para bc250-radv-compute-gate.exe.

.DESCRIPTION
    O harness do RADV_WDDM_COMPUTE_TRIPOD morreu com 0xC0000005 em tres
    corridas seguidas (06/09 13:19, 13:41 e 13:54), sempre por volta de 1,5 s,
    com tres DLLs diferentes da RADV e sempre com o mesmo stderr de duas
    linhas. Nenhum diagnostico por fprintf sobreviveu ao crash, e cada palpite
    custou uma corrida fisica inteira.

    Este script troca palpite por fato: configura o Windows Error Reporting
    para escrever um minidump COMPLETO do processo exato, numa pasta
    exclusiva, para que tools\analyze-radv-harness-dump.ps1 extraia a pilha da
    falha com o cdb.

    Escreve em HKLM. Por isso: preserva a configuracao anterior num .reg antes
    de tocar em qualquer coisa, faz readback do que gravou, e desfaz tudo
    sozinho se qualquer etapa falhar. Quem restaura no fluxo normal e
    tools\restore-radv-harness-crash-capture.ps1.

.PARAMETER EvidenceDir
    Diretorio onde o estado da captura e os dumps coletados sao arquivados.

.PARAMETER Token
    GUID da corrida; nomeia a pasta exclusiva de dumps.

.PARAMETER Execute
    Obrigatorio para escrever. Sem ele o script so descreve o que faria.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$Token,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$imageName = 'bc250-radv-compute-gate.exe'
$werKey = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\$imageName"
$werKeyNative = "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\$imageName"
$statePath = Join-Path $EvidenceDir 'radv-harness-localdumps-state.json'
$snapshotPath = Join-Path $EvidenceDir 'radv-harness-localdumps-before.reg'
$parsedToken = [guid]::Empty

if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw 'Captura RADV recusada: diretorio de evidencia ausente.'
}
if (-not [guid]::TryParseExact($Token, 'D', [ref]$parsedToken)) {
    throw 'Captura RADV recusada: token invalido.'
}
$dumpFolder = Join-Path $env:SystemRoot ("Temp\BC250_RADV_DUMPS_{0}" -f $parsedToken.ToString('N'))

if (-not $Execute) {
    Write-Host "RADV LOCALDUMPS DRY-RUN: key=$werKeyNative folder=$dumpFolder"
    Write-Host 'Zero escrita no Registro e zero criacao de pasta.'
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Captura RADV exige PowerShell elevado.'
}
if (Test-Path -LiteralPath $statePath) {
    throw 'Captura RADV recusada: state file ja existe.'
}
if (Test-Path -LiteralPath $dumpFolder) {
    throw 'Captura RADV recusada: pasta exclusiva ja existe.'
}

$priorExists = Test-Path -LiteralPath $werKey
if ($priorExists) {
    & reg.exe export $werKeyNative $snapshotPath /y | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $snapshotPath)) {
        throw 'Captura RADV recusada: nao foi possivel preservar a configuracao WER anterior.'
    }
}

$captureState = [ordered]@{
    state = 'PREPARING'
    token = $Token
    image_name = $imageName
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
    # O harness roda no contexto do binder elevado, mas o WER escreve o dump
    # pelo servico. Sem Modify para todos, o dump simplesmente nao aparece e a
    # corrida se perde em silencio.
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
    # DumpType 2 = dump completo. Um minidump normal nao traz os dados locais
    # necessarios para decidir em qual estrutura o acesso invalido ocorreu.
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

Write-Host "RADV LOCALDUMPS ARMED: $dumpFolder"
Write-Host 'NAO VALIDADO EM HARDWARE'
