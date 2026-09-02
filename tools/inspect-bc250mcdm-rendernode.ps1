<#
.SYNOPSIS
    Le a telemetria pos-boot e julga o perfil WDDM que o driver registrou.

.DESCRIPTION
    Sem -RenderOnlyLab, exige o perfil display+render: RegistrationMode=1,
    uma VidPn source e um child.

    Com -RenderOnlyLab, exige o perfil do Gate 1.5: RegistrationMode=2, zero
    sources, zero children e nenhuma superficie de display anunciada.  Passar
    o switch errado transforma um resultado valido em falha ruidosa — e' de
    proposito: o julgamento tem de declarar antes qual hipotese esta' sendo
    testada.
#>
[CmdletBinding()]
param(
    [switch]$RenderOnlyLab
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$instanceId = 'PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\4&19CAF403&0&0041'
$probeKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-rendernode-inspect"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null

$os = Get-CimInstance Win32_OperatingSystem
$device = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -eq $instanceId
})
$probe = if (Test-Path -LiteralPath $probeKey) {
    Get-ItemProperty -LiteralPath $probeKey
} else { $null }

$fieldNames = @(
    'SchemaVersion','DriverEntryReached','InitializationDataReady',
    'DxgkInitializeStatus','AddDeviceStatus','StartDeviceEntered',
    'V5RenderNodeRequested','V5RenderNodeTunablePresent','V5RegistrationMode',
    'V5StartDeviceStatus','V5StartDeviceSources','V5StartDeviceChildren',
    'V5QaiCalls','V5QaiLastType','V5QaiLastStatus',
    'V5QaiLastUnsupportedType','V5QaiUnsupportedCount',
    'V5QiCalls','V5QiLastGuidD1','V5QiPrevGuidD1','V5QiPrev2GuidD1',
    'V5LastDdi','V5PrevDdi','V5Prev2Ddi','V5DdiMarks',
    'HqdActiveStatus','HqdActivePhysicalReady','ShaderPhysicalReady',
    'P1EngineState','P1SelfTestPhysicalReady','P2PhysicalReady',
    'DmmCommitStatus','DmmPresentStatus','DmmVsyncCount',
    'V5ChildRelationsCalls','V5ChildRelationsStatus','V5ChildRelationsCount',
    'V5PostStartStage','V5QiSlotsCaptured','V5ContractVersion',
    # Schema 132 — perfil de adaptador (Gate 1.5).
    'V5AdapterProfile','V5RenderOnlyLabRequested',
    'V5RenderOnlyLabTunablePresent','V5ProfileResolveStatus',
    'V5DisplaySurfaceAnnounced','V5ChildEnumerationEnabled',
    'StopDeviceEntered','StopDeviceStatus',
    'V5StopFenceQueryMode','V5StopFenceSyncStatus','V5StopFenceSyncResult',
    'V5StopFencePendingCount','V5StopFenceReservedCount',
    'V5StopReason','V5StopTimerDrained','V5StopDpcDrained',
    'V5StopSubmitInFlight','V5StopDrainIterations','V5StopSnapshotOk',
    'V5StopLockFailures','V5StopRejectedWhileStopping','V5StopFirstStatus',
    'V5LifecycleState',
    'RemoveDeviceEntered','RemoveDeviceStatus',
    # Schema 134 — as tres ABIs deixam de ser invisiveis.
    #
    # AbiReadyPublishCount responde "o publicador rodou?".  Valor 1 significa
    # que apenas o AddDevice publicou, e portanto o StartDevice nao alcancou
    # nenhum caminho de retorno.
    'AbiReadyContext','AbiReadyAllocation','AbiReadyRootPageTable',
    'AbiReadyPublishCount',
    # Schema 134 — bitmap de DXGKQAITYPE perguntados pelo dxgkrnl.
    # Bit 1 = DRIVERCAPS, bit 29 = WDDMDEVICECAPS. No 3.79 so' o 29 acendeu.
    'V5QaiTypeSeenLo','V5QaiTypeSeenHi',
    # Schema 134 — anel de rastro de DDI. Hist00 e' a marca mais recente.
    'V5DdiHist00','V5DdiHist01','V5DdiHist02','V5DdiHist03',
    'V5DdiHist04','V5DdiHist05','V5DdiHist06','V5DdiHist07',
    'V5DdiHist08','V5DdiHist09','V5DdiHist10','V5DdiHist11',
    'V5DdiHist12','V5DdiHist13','V5DdiHist14','V5DdiHist15'
)

# Schema 130: cada slot e' um REG_BINARY de 28 bytes com o GUID inteiro.
#   +0  Data1   ULONG
#   +4  Data2   USHORT
#   +6  Data3   USHORT
#   +8  Data4   UCHAR[8]
#   +16 Version ULONG
#   +20 Size    ULONG
#   +24 Status  ULONG
#   +28 Ordinal ULONG
# O 3.66 so guardava Data1 das tres ultimas de oito chamadas, e por isso a
# interface iniciada em 6BA41A33 ficou sem identificacao possivel.
$qiSlotCount = 16
function ConvertFrom-Bc250QiSlot {
    param([byte[]]$Bytes, [int]$Index)
    if ($null -eq $Bytes -or $Bytes.Length -lt 32) { return $null }
    $data1 = [BitConverter]::ToUInt32($Bytes, 0)
    $data2 = [BitConverter]::ToUInt16($Bytes, 4)
    $data3 = [BitConverter]::ToUInt16($Bytes, 6)
    $data4 = ($Bytes[8..15] | ForEach-Object { $_.ToString('x2') }) -join ''
    $ordinal = [BitConverter]::ToUInt32($Bytes, 28)
    if ($data1 -eq 0 -and $data2 -eq 0 -and $data3 -eq 0 -and $ordinal -eq 0) {
        return $null
    }
    $guid = '{0:x8}-{1:x4}-{2:x4}-{3}-{4}' -f $data1, $data2, $data3,
        $data4.Substring(0, 4), $data4.Substring(4)
    [ordered]@{
        slot = $Index
        ordinal = $ordinal
        guid = $guid
        version = [BitConverter]::ToUInt32($Bytes, 16)
        size = [BitConverter]::ToUInt32($Bytes, 20)
        status = '0x{0:X8}' -f [BitConverter]::ToUInt32($Bytes, 24)
    }
}
$fields = [ordered]@{}
foreach ($name in $fieldNames) {
    $property = if ($null -ne $probe) { $probe.PSObject.Properties[$name] } else { $null }
    $fields[$name] = if ($null -eq $property) { $null } else { [uint32]$property.Value }
}

$deviceSummary = if ($device.Count -eq 1) {
    [ordered]@{
        name = [string]$device[0].Name
        service = [string]$device[0].Service
        status = [string]$device[0].Status
        config_manager_error_code = [uint32]$device[0].ConfigManagerErrorCode
    }
} else {
    [ordered]@{ count = $device.Count }
}
$qiSlots = @()
for ($i = 0; $i -lt $qiSlotCount; $i++) {
    $name = 'V5QiSlot{0:d2}' -f $i
    $property = if ($null -ne $probe) { $probe.PSObject.Properties[$name] } else { $null }
    if ($null -eq $property) { continue }
    $parsed = ConvertFrom-Bc250QiSlot -Bytes ([byte[]]$property.Value) -Index $i
    if ($null -ne $parsed) { $qiSlots += $parsed }
}
$qiSlots = @($qiSlots | Sort-Object { $_.ordinal })

$report = [ordered]@{
    captured_at = (Get-Date).ToString('o')
    boot_time = $os.LastBootUpTime.ToString('o')
    boot_ticks = $os.LastBootUpTime.ToUniversalTime().Ticks
    instance_id = $instanceId
    device = $deviceSummary
    telemetry = $fields
    query_interfaces = $qiSlots
    hardware_validated = $false
}
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath `
    (Join-Path $evidenceDir 'rendernode-inspect.json') -Encoding UTF8

Write-Host '=== BC250 RENDERNODE POST-REBOOT INSPECT ==='
Write-Host "Evidence: $evidenceDir"
Write-Host "Device: $($deviceSummary.service) / Codigo $($deviceSummary.config_manager_error_code)"
foreach ($name in $fieldNames) {
    $value = $fields[$name]
    if ($null -ne $value) { Write-Host ("  {0} = 0x{1:X8} ({2})" -f $name,$value,$value) }
}

Write-Host ''
Write-Host 'QueryInterface (GUID completo, ordem de chamada):'
if ($qiSlots.Count -eq 0) {
    Write-Host '  <nenhum slot capturado>'
} else {
    foreach ($slot in $qiSlots) {
        Write-Host ("  #{0}  {{{1}}}  v={2} size={3} status={4}" -f
            $slot.ordinal, $slot.guid, $slot.version, $slot.size, $slot.status)
    }
}
Write-Host ''

# O schema exigido vem do manifesto do candidato, como em bind/confirm/prepare.
# Fixar o numero aqui faria o inspetor aprovar telemetria de um binario antigo
# depois do proximo bump.
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'Inspecao recusada: manifesto de laboratorio ausente.'
}
$expectedSchema = [uint32](
    (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json).TelemetrySchemaVersion)

$required = [ordered]@{
    SchemaVersion = $expectedSchema
    V5RenderNodeRequested = 1
    V5RenderNodeTunablePresent = 1
    # 0 = STATUS_SUCCESS: os tunables foram lidos e o perfil foi resolvido.
    V5ProfileResolveStatus = 0
    V5RenderOnlyLabRequested = $(if ($RenderOnlyLab) { 1 } else { 0 })
    V5RenderOnlyLabTunablePresent = 1
    # 1 = DISPLAY_RENDER, 2 = RENDER_ONLY_LAB (BC250_ADAPTER_PROFILE).
    V5AdapterProfile = $(if ($RenderOnlyLab) { 2 } else { 1 })
    V5RegistrationMode = $(if ($RenderOnlyLab) { 2 } else { 1 })
    V5DisplaySurfaceAnnounced = $(if ($RenderOnlyLab) { 0 } else { 1 })
    V5StartDeviceStatus = 0
    V5StartDeviceSources = $(if ($RenderOnlyLab) { 0 } else { 1 })
    V5StartDeviceChildren = $(if ($RenderOnlyLab) { 0 } else { 1 })
}

if ($RenderOnlyLab) {
    # No perfil render-only o StartDevice retorna ANTES do bring-up de
    # hardware, por projeto: nao adquire post-display ownership, nao chama
    # Bc250DisplayInit e nao executa PFP/CP/GCVM/HQD. DriverEntry zera o
    # bloco P1/P2 a cada carregamento e nada o reproduz.
    #
    # Exigir P1/P2 fisicamente prontos aqui reprovaria uma corrida
    # perfeitamente valida, e o operador leria "render-only falhou" quando o
    # driver fez exatamente o que deveria. Exigir que estejam ZERADOS e' a
    # prova positiva de que o early-return executou — e' mais forte do que
    # simplesmente omitir os campos.
    #
    # So' entram campos que Bc250ProbeResetSubmissionEvidence realmente zera
    # a cada DriverEntry, isto e', dentro do intervalo
    # [BC250_PROBE_P1_ENGINE_STATE .. BC250_PROBE_P2_DOORBELL_HIT_OBSERVED].
    # ShaderPhysicalReady e HqdActivePhysicalReady estao FORA desse intervalo
    # e sobrevivem ao boot anterior; afirmar zero neles reprovaria por
    # contaminacao cruzada, nao por comportamento do driver.
    $required['P1EngineState'] = 0
    $required['P1SelfTestPhysicalReady'] = 0
    $required['P2PhysicalReady'] = 0
} else {
    $required['HqdActiveStatus'] = 0
    $required['HqdActivePhysicalReady'] = 1
    $required['ShaderPhysicalReady'] = 1
    $required['P1EngineState'] = 1
    $required['P1SelfTestPhysicalReady'] = 1
    $required['P2PhysicalReady'] = 1
}
$failures = [System.Collections.Generic.List[string]]::new()
if ($device.Count -ne 1 -or $device[0].Service -ne 'bc250mcdm' -or
    $device[0].ConfigManagerErrorCode -ne 0) {
    $failures.Add('BC-250 nao esta em bc250mcdm Codigo 0')
}
foreach ($entry in $required.GetEnumerator()) {
    if ($fields[$entry.Key] -ne [uint32]$entry.Value) {
        $failures.Add("$($entry.Key) esperado=$($entry.Value) observado=$($fields[$entry.Key])")
    }
}
if ($failures.Count -ne 0) {
    $failures | Set-Content -LiteralPath (Join-Path $evidenceDir 'failures.txt') -Encoding UTF8
    Write-Host 'RENDERNODE: FAIL-CLOSED' -ForegroundColor Red
    foreach ($failure in $failures) { Write-Host "  BLOQUEIO: $failure" }
    Write-Host 'Nenhum rollback automatico foi solicitado.' -ForegroundColor Yellow
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 2
}

Write-Host 'RENDERNODE V5/START/P1/P2: VALIDADO EM HARDWARE' -ForegroundColor Green
Write-Host 'Isso ainda nao valida submissao de uma aplicacao WDDM/Vulkan.'
exit 0
