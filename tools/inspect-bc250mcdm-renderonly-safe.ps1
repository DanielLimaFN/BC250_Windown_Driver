<#
.SYNOPSIS
    Julga o boot do Gate 1.5 sobre o pacote RENDERONLY_SAFE.

.DESCRIPTION
    Le a telemetria e aplica o criterio PASS/FAIL do gate. Somente leitura:
    nao escreve registro, nao vincula, nao reinicia.

    O criterio e' de PROVA POSITIVA. Ate o schema 132 um campo em zero podia
    significar "nao executou" ou "executou e o valor sobreviveu do boot
    anterior". O schema 133 zera o enum inteiro em DriverEntry e acrescenta
    marcadores de entrada, entao zero passou a significar exatamente uma
    coisa.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'bc250-safe-common.ps1')

$probeKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$paramKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-renderonly-safe-inspect"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null

$manifest = Assert-Bc250SafePackage -Root $root

$device = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*" })
$probe  = if (Test-Path $probeKey) { Get-ItemProperty $probeKey } else { $null }
$params = if (Test-Path $paramKey) { Get-ItemProperty $paramKey } else { $null }

function Get-Field($Name) {
    if ($null -eq $probe) { return $null }
    $p = $probe.PSObject.Properties[$Name]
    if ($null -eq $p) { return $null }
    return [uint32]$p.Value
}

# ---- Portao de validade: se isto falhar, o resto e' ininterpretavel ----
$validity = [ordered]@{
    SchemaVersion           = [uint32]$manifest.TelemetrySchemaVersion
    V5UnsafeBringupCompiled = 0   # o binario carregado e' o seguro
    V5ProfileResolveStatus  = 0   # perfil resolvido sem erro
    V5AdapterProfile        = 2   # RENDER_ONLY_LAB
}

# ---- Criterio do gate ----
$expected = [ordered]@{
    V5RenderNodeRequested         = 1
    V5RenderNodeTunablePresent    = 1
    V5RenderOnlyLabRequested      = 1
    V5RenderOnlyLabTunablePresent = 1
    V5RegistrationMode            = 2
    V5StartDeviceStatus           = 0
    V5StartDeviceReturnStage      = 3   # BC250_V5_START_RETURN_RENDER_ONLY
    V5RenderOnlyEarlyReturn       = 1
    V5StartDeviceSources          = 0
    V5StartDeviceChildren         = 0
    V5DisplaySurfaceAnnounced     = 0
    V5PostDisplayAttempted        = 0
    V5DisplayInitEntered          = 0
    V5UnsafeBringupEntered        = 0
    P1EngineState                 = 0
    P1SelfTestPhysicalReady       = 0
    P2PhysicalReady               = 0
}

$invalid  = [System.Collections.Generic.List[string]]::new()
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($e in $validity.GetEnumerator()) {
    $v = Get-Field $e.Key
    if ($v -ne [uint32]$e.Value) { $invalid.Add("$($e.Key) esperado=$($e.Value) observado=$v") }
}
foreach ($e in $expected.GetEnumerator()) {
    $v = Get-Field $e.Key
    if ($v -ne [uint32]$e.Value) { $failures.Add("$($e.Key) esperado=$($e.Value) observado=$v") }
}

$code = if ($device.Count -eq 1) { [uint32]$device[0].ConfigManagerErrorCode } else { $null }
$svc  = if ($device.Count -eq 1) { [string]$device[0].Service } else { '<n/a>' }

# Ate onde o dxgkrnl levou o adaptador — informativo, nao criterio.
$progress = [ordered]@{
    V5PostStartStage     = Get-Field 'V5PostStartStage'
    V5QaiCalls           = Get-Field 'V5QaiCalls'
    V5QaiLastType        = Get-Field 'V5QaiLastType'
    V5QaiLastStatus      = Get-Field 'V5QaiLastStatus'
    V5QiCalls            = Get-Field 'V5QiCalls'
    V5LastDdi            = Get-Field 'V5LastDdi'
    V5DdiMarks           = Get-Field 'V5DdiMarks'
    V5LifecycleState     = Get-Field 'V5LifecycleState'
    StopDeviceEntered    = Get-Field 'StopDeviceEntered'
    RemoveDeviceEntered  = Get-Field 'RemoveDeviceEntered'
    # Schema 134 — informativos, NUNCA criterio de gate.
    #
    # Este inspetor audita a imagem SEGURA, cujo contrato e' justamente nao
    # subir hardware; promover qualquer um destes a criterio transformaria o
    # gate em outra coisa.
    AbiReadyContext       = Get-Field 'AbiReadyContext'
    AbiReadyAllocation    = Get-Field 'AbiReadyAllocation'
    AbiReadyRootPageTable = Get-Field 'AbiReadyRootPageTable'
    AbiReadyPublishCount  = Get-Field 'AbiReadyPublishCount'
    V5QaiTypeSeenLo       = Get-Field 'V5QaiTypeSeenLo'
    V5QaiTypeSeenHi       = Get-Field 'V5QaiTypeSeenHi'
    V5DdiHist00           = Get-Field 'V5DdiHist00'
    V5DdiHist01           = Get-Field 'V5DdiHist01'
    V5DdiHist02           = Get-Field 'V5DdiHist02'
    V5DdiHist03           = Get-Field 'V5DdiHist03'
}

$report = [ordered]@{
    captured_at        = (Get-Date).ToString('o')
    build_kind         = [string]$manifest.BuildKind
    driver_ver         = [string]$manifest.DriverVer
    schema_version     = [uint32]$manifest.TelemetrySchemaVersion
    package_sys_sha256 = [string]$manifest.PackageSysSha256
    device_service     = $svc
    device_code        = $code
    render_node        = if ($params) { $params.RenderNode } else { $null }
    render_only_lab    = if ($params) { $params.RenderOnlyLab } else { $null }
    validity_failures  = @($invalid)
    gate_failures      = @($failures)
    progress           = $progress
    hardware_validated = $false
}
$report | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $evidenceDir 'renderonly-safe-inspect.json') -Encoding UTF8

Write-Host ''
Write-Host 'BC250 GATE 1.5 - RENDERONLY SAFE' -ForegroundColor Cyan
Write-Host "  pacote : $($manifest.BuildKind) $($manifest.DriverVer) schema $($manifest.TelemetrySchemaVersion)"
Write-Host "  device : $svc / Codigo $code"
Write-Host ''
Write-Host '  progresso do dxgkrnl apos StartDevice:'
foreach ($e in $progress.GetEnumerator()) { Write-Host ("    {0,-22} = {1}" -f $e.Key, $e.Value) }
Write-Host ''
Write-Host "  evidencia: $evidenceDir"
Write-Host ''

if ($invalid.Count -ne 0) {
    Write-Host 'CORRIDA INVALIDADA - o perfil que subiu nao e o pedido.' -ForegroundColor Red
    foreach ($f in $invalid) { Write-Host "  INVALIDO: $f" }
    Write-Host 'Nao interprete o restante. NAO VALIDADO EM HARDWARE'
    exit 3
}

if ($failures.Count -ne 0) {
    Write-Host 'GATE 1.5: FAIL' -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  FALHA: $f" }
    Write-Host ''
    Write-Host 'Ainda assim e resultado util: o perfil pedido subiu e nenhuma'
    Write-Host 'operacao de display ou compute foi executada.'
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 2
}

if ($code -ne 0 -or $svc -ne 'bc250mcdm') {
    Write-Host 'GATE 1.5: PARCIAL' -ForegroundColor Yellow
    Write-Host "  telemetria coerente, mas o dispositivo esta em $svc / Codigo $code."
    Write-Host '  Registre o codigo: e informacao util sobre onde o dxgkrnl parou.'
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 2
}

Write-Host 'GATE 1.5: PASS' -ForegroundColor Green
Write-Host '  Adaptador render-only em Codigo 0, sem qualquer superficie de display,'
Write-Host '  sem post-display ownership e sem bring-up fisico.'
Write-Host '  A tabela render/base sobrevive ao registro pelo dxgkrnl.'
Write-Host 'NAO VALIDADO EM HARDWARE (o gate seguinte e submissao real)'
exit 0
