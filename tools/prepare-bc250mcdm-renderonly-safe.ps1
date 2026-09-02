<#
.SYNOPSIS
    Promove o pacote RENDERONLY_SAFE ja' instalado para o perfil do Gate 1.5.

.DESCRIPTION
    Grava RenderNode=1 e RenderOnlyLab=1 para o proximo boot.

    Diferenca central em relacao a prepare-bc250mcdm-rendernode.ps1: aqui o
    preflight NAO exige prontidao fisica de P1, P2, shader ou HQD. Ele exige o
    OPOSTO — que estejam zerados.

    O porque: o perfil render-only sai do StartDevice antes de qualquer
    trabalho de hardware. Exigir prontidao fisica dele obrigava um boot de
    bring-up so' para satisfazer o portao, e foi exatamente essa exigencia que
    levou o ensaio 3.75 a rodar sobre a imagem de laboratorio, com o
    StartDevice executando CP soft reset enquanto detinha o display.

    Um portao que forca a corrida perigosa nao protege nada.

    NAO instala, NAO vincula, NAO reinicia.
#>
[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IAcceptProfileChangeRisk
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'bc250-safe-common.ps1')

$parameterKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$probeKey     = Join-Path $parameterKey 'Probe'
$instancePrefix = 'PCI\VEN_1002&DEV_13FE'

$manifest = Assert-Bc250SafePackage -Root $root
Assert-Bc250InstalledMatchesSafe -Manifest $manifest

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop | Where-Object {
    $_.PNPDeviceID -like "$instancePrefix*"
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Recusado: BC-250 nao esta em bc250mcdm Codigo 0.'
}
if (Get-ScheduledTask -TaskName 'BC250-RebootGuard' -ErrorAction SilentlyContinue) {
    throw 'Recusado: ja existe reboot guard ativo.'
}

$probe = Get-ItemProperty -LiteralPath $probeKey -ErrorAction Stop
$params = Get-ItemProperty -LiteralPath $parameterKey -ErrorAction Stop

# Preflight do perfil seguro. Note o que NAO esta aqui: Shader/HQD/P1/P2
# prontos. Este perfil nao os utiliza.
$required = [ordered]@{
    SchemaVersion            = [uint32]$manifest.TelemetrySchemaVersion
    V5RegistrationMode       = 0    # boot atual deve ser KMDOD
    V5AdapterProfile         = 0    # DISPLAY_ONLY
    V5UnsafeBringupCompiled  = 0    # binario carregado e' o seguro
    V5UnsafeBringupEntered   = 0    # e nao executou bring-up
    P1EngineState            = 0
    P1SelfTestPhysicalReady  = 0
    P2PhysicalReady          = 0
}
foreach ($e in $required.GetEnumerator()) {
    $prop = $probe.PSObject.Properties[$e.Key]
    $val = if ($null -eq $prop) { $null } else { [uint32]$prop.Value }
    if ($val -ne [uint32]$e.Value) {
        throw "Recusado: $($e.Key) esperado=$($e.Value) observado=$val."
    }
}

foreach ($e in @{ SkipHwBringup = 1; RenderNode = 0; RenderOnlyLab = 0 }.GetEnumerator()) {
    $prop = $params.PSObject.Properties[$e.Key]
    if ($null -eq $prop -or [uint32]$prop.Value -ne [uint32]$e.Value) {
        $seen = if ($prop) { $prop.Value } else { 'AUSENTE' }
        throw "Recusado: Parameters\$($e.Key) esperado=$($e.Value) observado=$seen."
    }
}

Write-Host 'BC250 RENDERONLY SAFE PREFLIGHT: PASS' -ForegroundColor Green
Write-Host "  pacote      : RENDERONLY_SAFE $($manifest.DriverVer)"
Write-Host "  SYS         : $($manifest.PackageSysSha256)"
Write-Host "  schema      : $($manifest.TelemetrySchemaVersion)"
Write-Host '  bring-up    : ausente do binario e nao executado neste boot'
Write-Host '  perfil alvo : RENDER_ONLY_LAB (Gate 1.5)'
Write-Host '  display     : NENHUMA superficie sera anunciada; GOP mantem o scanout'

if (-not $Execute) {
    Write-Host 'DRY-RUN: nenhuma escrita realizada.'
    exit 0
}
if (-not $IAcceptProfileChangeRisk) {
    throw 'Recusado: falta -IAcceptProfileChangeRisk.'
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Promocao exige PowerShell elevado.'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-renderonly-safe-promotion"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null

try {
    Set-ItemProperty -LiteralPath $parameterKey -Name RenderNode -Value 1 -Type DWord -Force
    Set-ItemProperty -LiteralPath $parameterKey -Name RenderOnlyLab -Value 1 -Type DWord -Force
    $rn = [uint32](Get-ItemPropertyValue -LiteralPath $parameterKey -Name RenderNode)
    $ro = [uint32](Get-ItemPropertyValue -LiteralPath $parameterKey -Name RenderOnlyLab)
    if ($rn -ne 1 -or $ro -ne 1) { throw "Readback divergiu: RenderNode=$rn RenderOnlyLab=$ro." }

    [ordered]@{
        state                = 'PREPARED_RENDERONLY_SAFE'
        captured_at          = (Get-Date).ToString('o')
        build_kind           = [string]$manifest.BuildKind
        driver_ver           = [string]$manifest.DriverVer
        schema_version       = [uint32]$manifest.TelemetrySchemaVersion
        package_sys_sha256   = [string]$manifest.PackageSysSha256
        instance_id          = [string]$devices[0].PNPDeviceID
        render_node_applied  = $rn
        render_only_applied  = $ro
        unsafe_bringup       = $false
        hardware_validated   = $false
    } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 `
        -LiteralPath (Join-Path $evidenceDir 'renderonly-safe-promotion.json')
} catch {
    Set-ItemProperty -LiteralPath $parameterKey -Name RenderNode -Value 0 -Type DWord -Force
    Set-ItemProperty -LiteralPath $parameterKey -Name RenderOnlyLab -Value 0 -Type DWord -Force
    throw
}

Write-Host ''
Write-Host 'BC250 RENDERONLY SAFE: PREPARADO' -ForegroundColor Green
Write-Host '  RenderNode=1 e RenderOnlyLab=1 aplicados e relidos.'
Write-Host '  Ainda nao consumidos: DriverEntry le no proximo boot.'
Write-Host "  evidencia: $evidenceDir"
Write-Host '  Apos o boot: tools\inspect-bc250mcdm-renderonly-safe.ps1'
Write-Host 'NAO VALIDADO EM HARDWARE'
