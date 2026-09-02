<#
.SYNOPSIS
    Prepara, inspeciona e reverte o gate GPUVA opt-in do BC-250.

.DESCRIPTION
    Harness transacional. Nao instala driver, nao troca binding, nao reinicia o
    dispositivo e nao submete trabalho. Prepare altera somente o DWORD
    GpuvaLabPromotion para o proximo StartDevice/boot; Inspect coleta evidencia;
    Rollback restaura exatamente o estado anterior salvo em transaction.json.

    Ciclo obrigatório:
      [PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK]
      -> [PROVA NO SILÍCIO por ferramenta separada] -> [ROLLBACK/TEARDOWN]

    Este script, sozinho, nunca constitui prova no silício.
#>
[CmdletBinding()]
param(
    [ValidateSet('Prepare', 'Inspect', 'Rollback')]
    [string]$Action = 'Inspect',
    [string]$EvidenceDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$parameterPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$probePath = Join-Path $parameterPath 'Probe'
$valueName = 'GpuvaLabPromotion'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Esta ação exige PowerShell executado como Administrador.'
    }
}

function Get-Bc250Device {
    $devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop | Where-Object {
        $_.PNPDeviceID -like 'PCI\VEN_1002&DEV_13FE*'
    })
    if ($devices.Count -ne 1) {
        throw "Preflight recusado: esperada exatamente uma BC-250; encontradas $($devices.Count)."
    }
    return $devices[0]
}

function Get-OptionalDword([string]$Path, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return [ordered]@{ key_exists = $false; value_exists = $false; value = $null }
    }
    $item = Get-ItemProperty -LiteralPath $Path
    $property = $item.PSObject.Properties[$Name]
    return [ordered]@{
        key_exists = $true
        value_exists = ($null -ne $property)
        value = if ($null -eq $property) { $null } else { [uint32]$property.Value }
    }
}

function Get-Probe {
    $names = @(
        'GpuvaLabPromotionRequested', 'GpuvaLabPromotionPresent',
        'GpuvaGateEligible', 'GpuvaGateOperational', 'GpuvaGateBlockMask',
        'GpuvaSnapshotGeneration', 'GpuvaSnapshotEpoch',
        'GpuvaSnapshotLiveCount', 'P1EngineState', 'V2TrackerPushedTotal',
        'V2NotifiedTotal', 'V2FencesInFlight', 'V2TrackerOverflow',
        'V2TrackerOutOfOrder', 'V2PushFailures', 'V2PollFailures'
    )
    $result = [ordered]@{}
    if (-not (Test-Path -LiteralPath $probePath)) {
        foreach ($name in $names) { $result[$name] = $null }
        return $result
    }
    $item = Get-ItemProperty -LiteralPath $probePath
    foreach ($name in $names) {
        $property = $item.PSObject.Properties[$name]
        $result[$name] = if ($null -eq $property) {
            $null
        } else {
            [uint32]$property.Value
        }
    }
    return $result
}

if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    if ($Action -ne 'Prepare') {
        throw '-EvidenceDir é obrigatório para Inspect e Rollback.'
    }
    $stamp = (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss')
    $EvidenceDir = Join-Path $root (
        "docs\evidence\{0}-{1}-gpuva-lab-promotion" -f $stamp, $env:COMPUTERNAME)
}
$EvidenceDir = [IO.Path]::GetFullPath($EvidenceDir)
$statePath = Join-Path $EvidenceDir 'transaction.json'

Write-Host '=== BC-250 GPUVA LAB PROMOTION ===' -ForegroundColor Magenta
Write-Host ("  Ação       : {0}" -f $Action)
Write-Host ("  Evidência  : {0}" -f $EvidenceDir)
Write-Host '  Estado     : NÃO VALIDADO EM HARDWARE' -ForegroundColor Yellow

if ($Action -eq 'Prepare') {
    Assert-Administrator
    $device = Get-Bc250Device
    if (-not (Test-Path -LiteralPath 'C:\BC250_RECOVERY.cmd')) {
        throw 'Preflight recusado: C:\BC250_RECOVERY.cmd não foi encontrado.'
    }
    if (Test-Path -LiteralPath $EvidenceDir) {
        throw "Snapshot recusado: diretório de evidência já existe: $EvidenceDir"
    }
    New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
    $before = Get-OptionalDword $parameterPath $valueName
    $installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
    $state = [ordered]@{
        schema = 1
        phase = 'SNAPSHOT'
        created_at = (Get-Date).ToString('o')
        computer = $env:COMPUTERNAME
        instance_id = $device.PNPDeviceID
        service_before = $device.Service
        config_manager_error_before = $device.ConfigManagerErrorCode
        recovery_script = 'C:\BC250_RECOVERY.cmd'
        parameter_path = $parameterPath
        value_name = $valueName
        original = $before
        installed_driver_sha256 = if (Test-Path -LiteralPath $installedDriver) {
            (Get-FileHash -LiteralPath $installedDriver -Algorithm SHA256).Hash
        } else { $null }
        hardware_validated = $false
    }
    $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statePath -Encoding UTF8

    if (-not (Test-Path -LiteralPath $parameterPath)) {
        New-Item -Path $parameterPath -Force | Out-Null
    }
    New-ItemProperty -LiteralPath $parameterPath -Name $valueName `
        -PropertyType DWord -Value 1 -Force | Out-Null
    $readback = Get-OptionalDword $parameterPath $valueName
    if (-not $readback.value_exists -or $readback.value -ne 1) {
        throw 'Readback falhou: GpuvaLabPromotion não ficou igual a 1.'
    }
    $state.phase = 'APPLIED_READBACK_OK'
    $state.applied_at = (Get-Date).ToString('o')
    $state.readback = $readback
    $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statePath -Encoding UTF8
    Write-Host 'Prepare concluído. A política só será lida no próximo StartDevice/boot.' -ForegroundColor Green
    Write-Host 'Nenhum driver foi instalado, reiniciado ou vinculado.'
    Write-Host ("Após o boot: -Action Inspect -EvidenceDir `"{0}`"" -f $EvidenceDir)
    exit 0
}

if (-not (Test-Path -LiteralPath $statePath)) {
    throw "Transação ausente: $statePath"
}
$saved = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if ($saved.computer -ne $env:COMPUTERNAME -or
    $saved.parameter_path -ne $parameterPath -or $saved.value_name -ne $valueName) {
    throw 'Transação não pertence a esta máquina/chave; operação recusada.'
}

if ($Action -eq 'Rollback') {
    Assert-Administrator
    if ($saved.original.value_exists) {
        New-ItemProperty -LiteralPath $parameterPath -Name $valueName `
            -PropertyType DWord -Value ([uint32]$saved.original.value) -Force | Out-Null
    } else {
        Remove-ItemProperty -LiteralPath $parameterPath -Name $valueName `
            -ErrorAction SilentlyContinue
    }
    $restored = Get-OptionalDword $parameterPath $valueName
    if ([bool]$restored.value_exists -ne [bool]$saved.original.value_exists -or
        ($saved.original.value_exists -and
         [uint32]$restored.value -ne [uint32]$saved.original.value)) {
        throw 'Rollback falhou no readback da configuração original.'
    }
    $rollback = [ordered]@{
        rolled_back_at = (Get-Date).ToString('o')
        restored = $restored
        restart_required = $true
        hardware_validated = $false
    }
    $rollback | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'rollback.json') -Encoding UTF8
    Write-Host 'Rollback da configuração concluído e verificado.' -ForegroundColor Green
    Write-Host 'Reinicialização ainda é necessária para o driver reler a política.'
    exit 0
}

$device = Get-Bc250Device
$configured = Get-OptionalDword $parameterPath $valueName
$probe = Get-Probe
$failures = [Collections.Generic.List[string]]::new()
if ($device.Service -ne 'bc250mcdm') { $failures.Add('serviço não é bc250mcdm') }
if ($device.ConfigManagerErrorCode -ne 0) { $failures.Add('dispositivo não está Código 0') }
if (-not $configured.value_exists -or $configured.value -ne 1) {
    $failures.Add('GpuvaLabPromotion não está configurado como 1')
}
if ($probe.GpuvaLabPromotionPresent -ne 1) { $failures.Add('driver não confirmou presença do tunable') }
if ($probe.GpuvaLabPromotionRequested -ne 1) { $failures.Add('driver não leu promoção igual a 1') }
if ($probe.GpuvaGateEligible -ne 1) { $failures.Add('gate ainda não está elegível') }
if ($probe.GpuvaGateBlockMask -ne 0) { $failures.Add('máscara de bloqueio não está zerada') }
if ($probe.GpuvaGateOperational -ne 1) { $failures.Add('gate operacional não foi promovido') }
if ($probe.GpuvaSnapshotGeneration -lt 1) { $failures.Add('snapshot sem geração publicada') }
if ($probe.GpuvaSnapshotEpoch -lt 1) { $failures.Add('snapshot sem epoch publicada') }
if ($probe.GpuvaSnapshotLiveCount -lt 1) { $failures.Add('snapshot sem faixa viva') }
if ($probe.P1EngineState -ne 1) { $failures.Add('submission engine não está online') }
foreach ($counter in @('V2TrackerOverflow','V2TrackerOutOfOrder','V2PushFailures','V2PollFailures')) {
    if ($null -ne $probe[$counter] -and $probe[$counter] -ne 0) {
        $failures.Add("contador de falha $counter não está zerado")
    }
}
$report = [ordered]@{
    inspected_at = (Get-Date).ToString('o')
    device = [ordered]@{
        instance_id = $device.PNPDeviceID
        service = $device.Service
        config_manager_error = $device.ConfigManagerErrorCode
    }
    configured = $configured
    probe = $probe
    failures = @($failures)
    ready_for_controlled_submission = ($failures.Count -eq 0)
    hardware_validated = $false
    verdict = if ($failures.Count -eq 0) {
        'GATE PRONTO PARA SUBMISSAO CONTROLADA; NAO VALIDADO EM HARDWARE'
    } else {
        'FAIL-CLOSED; NAO EXECUTAR SUBMISSAO'
    }
}
$report | ConvertTo-Json -Depth 7 |
    Set-Content -LiteralPath (Join-Path $EvidenceDir 'inspect.json') -Encoding UTF8
foreach ($failure in $failures) { Write-Host ("  BLOQUEIO: {0}" -f $failure) -ForegroundColor Red }
Write-Host $report.verdict -ForegroundColor $(if ($failures.Count -eq 0) { 'Yellow' } else { 'Red' })
Write-Host 'Este resultado não é prova de execução na GPU.'
if ($failures.Count -ne 0) { exit 2 }
exit 0
