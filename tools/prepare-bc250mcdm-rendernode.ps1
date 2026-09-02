<#
.SYNOPSIS
    Promove a BC-250 de KMDOD para um dos perfis WDDM registrados por
    DxgkInitialize, gravando os tunables e deixando o consumo para o proximo
    boot.

.DESCRIPTION
    Sem -RenderOnlyLab grava apenas RenderNode=1: perfil display+render, que
    anuncia uma VidPn source e um child DisplayPort.  Esse e' o perfil que
    produziu tela preta no 3.74.

    Com -RenderOnlyLab grava RenderNode=1 E RenderOnlyLab=1: perfil de
    diagnostico do Gate 1.5.  O driver devolve zero sources e zero children,
    nao adquire post-display ownership e nao inicializa display nenhum — o
    GOP continua dono do scanout.  A pergunta que esse boot responde e'
    unicamente se a tabela render/base sobrevive em Codigo 0.

    Os dois tunables sao gravados juntos e revertidos juntos: RenderOnlyLab=1
    com RenderNode=0 e' recusado pelo proprio driver
    (STATUS_DEVICE_CONFIGURATION_ERROR), entao deixar um sem o outro apenas
    produziria um boot inutil.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\prepare-bc250mcdm-rendernode.ps1 -RenderOnlyLab
#>
[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptDisplayRenderBootRisk,
    # Gate 1.5: perfil render-only, sem qualquer superficie de display.
    [switch]$RenderOnlyLab
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$parameterKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$probeKey = Join-Path $parameterKey 'Probe'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'Promocao recusada: manifesto de laboratorio ausente.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Promocao recusada: BC-250 nao esta em bc250mcdm Codigo 0.'
}
if (-not (Test-Path -LiteralPath $driverPath -PathType Leaf) -or
    (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne
        [string]$manifest.PackageSysSha256) {
    throw 'Promocao recusada: SYS ativo diverge do candidato autorizado.'
}
if (Get-ScheduledTask -TaskName 'BC250-RebootGuard' -ErrorAction SilentlyContinue) {
    throw 'Promocao recusada: ja existe reboot guard ativo.'
}

$probe = Get-ItemProperty -LiteralPath $probeKey -ErrorAction Stop
$required = [ordered]@{
    SchemaVersion = [uint32]$manifest.TelemetrySchemaVersion
    V5RegistrationMode = 0
    HqdActiveStatus = 0
    HqdActivePhysicalReady = 1
    ShaderPhysicalReady = 1
    P1EngineState = 1
    P1SelfTestPhysicalReady = 1
    P2PhysicalReady = 1
}
foreach ($entry in $required.GetEnumerator()) {
    $property = $probe.PSObject.Properties[$entry.Key]
    if ($null -eq $property -or [uint32]$property.Value -ne [uint32]$entry.Value) {
        throw "Promocao recusada: $($entry.Key)=$($entry.Value) nao comprovado."
    }
}

$parameters = Get-ItemProperty -LiteralPath $parameterKey -ErrorAction Stop
$renderProperty = $parameters.PSObject.Properties['RenderNode']
$renderPresent = ($null -ne $renderProperty)
$renderBefore = if ($renderPresent) { [uint32]$renderProperty.Value } else { $null }
if ($renderPresent -and $renderBefore -ne 0) {
    throw "Promocao recusada: RenderNode inicial inesperado: $renderBefore."
}

$labProperty = $parameters.PSObject.Properties['RenderOnlyLab']
$labPresent = ($null -ne $labProperty)
$labBefore = if ($labPresent) { [uint32]$labProperty.Value } else { $null }
if ($labPresent -and $labBefore -ne 0) {
    throw "Promocao recusada: RenderOnlyLab inicial inesperado: $labBefore."
}

$targetProfile = if ($RenderOnlyLab) { 'RENDER_ONLY_LAB (Gate 1.5)' }
                 else { 'DISPLAY_RENDER' }

Write-Host 'BC250 RENDERNODE PROMOTION PREFLIGHT: PASS'
Write-Host "  device: bc250mcdm / Codigo 0"
Write-Host "  current registration: KMDOD (V5RegistrationMode=0)"
Write-Host '  one-shot/P1/P2: fisicamente prontos'
Write-Host "  RenderNode snapshot: $(if ($renderPresent) { $renderBefore } else { 'ABSENT' })"
Write-Host "  RenderOnlyLab snapshot: $(if ($labPresent) { $labBefore } else { 'ABSENT' })"
Write-Host "  perfil alvo: $targetProfile"
if ($RenderOnlyLab) {
    Write-Host '  display: NENHUMA superficie anunciada; GOP mantem o scanout.'
} else {
    Write-Host '  display: 1 source + 1 child anunciados; risco de tela preta.'
}
if (-not $Execute) {
    if ($IAcceptDisplayRenderBootRisk) {
        throw '-IAcceptDisplayRenderBootRisk nao e valido sem -Execute.'
    }
    Write-Host 'DRY-RUN: nenhuma escrita realizada.'
    exit 0
}
if (-not $IAcceptDisplayRenderBootRisk) {
    throw 'Promocao recusada: falta -IAcceptDisplayRenderBootRisk.'
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Promocao exige PowerShell elevado.'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-rendernode-promotion"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
try {
    Set-ItemProperty -LiteralPath $parameterKey -Name RenderNode -Value 1 -Type DWord -Force
    $readback = Get-ItemPropertyValue -LiteralPath $parameterKey -Name RenderNode -ErrorAction Stop
    if ([uint32]$readback -ne 1) {
        throw "Readback RenderNode divergiu: $readback."
    }

    # RenderOnlyLab e' sempre gravado explicitamente, inclusive como 0. O
    # driver distingue "ausente" de "presente e zero" por dupla leitura com
    # defaults opostos; deixar a chave ausente e um valor herdado no registro
    # produziria um perfil diferente do pedido aqui.
    $labValue = if ($RenderOnlyLab) { 1 } else { 0 }
    Set-ItemProperty -LiteralPath $parameterKey -Name RenderOnlyLab `
        -Value $labValue -Type DWord -Force
    $labReadback = Get-ItemPropertyValue -LiteralPath $parameterKey `
        -Name RenderOnlyLab -ErrorAction Stop
    if ([uint32]$labReadback -ne [uint32]$labValue) {
        throw "Readback RenderOnlyLab divergiu: $labReadback."
    }

    [ordered]@{
        state = 'PREPARED_FOR_PROTECTED_REBOOT'
        captured_at = (Get-Date).ToString('o')
        boot_ticks = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
        instance_id = [string]$devices[0].PNPDeviceID
        sys_sha256 = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
        schema_version = [uint32]$probe.SchemaVersion
        render_node_present_before = $renderPresent
        render_node_before = $renderBefore
        render_node_applied = 1
        render_node_readback = [uint32]$readback
        target_profile = $targetProfile
        render_only_lab_present_before = $labPresent
        render_only_lab_before = $labBefore
        render_only_lab_applied = [uint32]$labValue
        render_only_lab_readback = [uint32]$labReadback
        v5_registration_mode_before = [uint32]$probe.V5RegistrationMode
        p1_physical_ready = [uint32]$probe.P1SelfTestPhysicalReady
        p2_physical_ready = [uint32]$probe.P2PhysicalReady
        hardware_validated = $false
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
        (Join-Path $evidenceDir 'rendernode-promotion.json') -Encoding UTF8
} catch {
    # Os dois tunables voltam juntos ao estado anterior: um perfil meio
    # aplicado e' pior do que nenhum.
    if ($renderPresent) {
        Set-ItemProperty -LiteralPath $parameterKey -Name RenderNode `
            -Value $renderBefore -Type DWord -Force
    } else {
        Remove-ItemProperty -LiteralPath $parameterKey -Name RenderNode `
            -ErrorAction SilentlyContinue
    }
    if ($labPresent) {
        Set-ItemProperty -LiteralPath $parameterKey -Name RenderOnlyLab `
            -Value $labBefore -Type DWord -Force
    } else {
        Remove-ItemProperty -LiteralPath $parameterKey -Name RenderOnlyLab `
            -ErrorAction SilentlyContinue
    }
    throw
}

Write-Host 'BC250 RENDERNODE PROMOTION: PREPARED'
Write-Host "  RenderNode=1 e RenderOnlyLab=$labValue aplicados e relidos;"
Write-Host '  ainda nao consumidos pelo DriverEntry.'
Write-Host "  perfil que o proximo boot testara: $targetProfile"
Write-Host "  evidence: $evidenceDir"
Write-Host '  Reboot guard recomendado; o operador pode dispensa-lo explicitamente.'
Write-Host 'NAO VALIDADO EM HARDWARE'
