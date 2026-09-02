#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet(20,27)]
    [int]$ContractVersion,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$key = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$instance = 'PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\4&19CAF403&0&0041'
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidence = Join-Path $root "docs\evidence\$stamp-$($env:COMPUTERNAME)-wddm-contract-$ContractVersion"

$device = Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -eq $instance
}
if ($null -eq $device -or $device.Service -ne 'bc250mcdm' -or
    $device.ConfigManagerErrorCode -ne 0) {
    throw 'Preflight recusado: requer bc250mcdm em Codigo 0.'
}
if (-not (Test-Path -LiteralPath $key)) {
    throw "Chave ausente: $key"
}
$renderNode = Get-ItemPropertyValue -LiteralPath $key -Name RenderNode -ErrorAction SilentlyContinue
if ($null -eq $renderNode) { $renderNode = 0 }
if ([int]$renderNode -ne 0) {
    throw 'Preflight recusado: selecione o contrato ainda em RenderNode=0.'
}

Write-Host 'BC250 WDDM CONTRACT A/B PREFLIGHT: PASS'
Write-Host "  contrato solicitado: WDDM $([math]::Floor($ContractVersion / 10)).$($ContractVersion % 10)"
Write-Host '  estado atual: bc250mcdm / Codigo 0 / RenderNode=0'
if (-not $Execute) {
    Write-Host 'DRY-RUN; nenhuma alteracao.'
    Write-Host 'Use -Execute para gravar e reler a politica.'
    exit 0
}

New-Item -ItemType Directory -Path $evidence | Out-Null
$before = Get-ItemProperty -LiteralPath $key
$before | Select-Object WddmContractVersion,RenderNode |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'before.json') -Encoding UTF8
New-ItemProperty -LiteralPath $key -Name WddmContractVersion -PropertyType DWord     -Value $ContractVersion -Force | Out-Null
$readback = [int](Get-ItemPropertyValue -LiteralPath $key -Name WddmContractVersion)
if ($readback -ne $ContractVersion) {
    throw "Readback divergente: $readback"
}
[ordered]@{
    ContractVersion = $readback
    RenderNode = [int]$renderNode
    AppliedAt = (Get-Date).ToString('o')
    RequiresDriverReload = $true
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'applied.json') -Encoding UTF8

Write-Host 'BC250 WDDM CONTRACT A/B: PREPARED'
Write-Host "  WddmContractVersion=$readback gravado e relido."
Write-Host "  evidencia: $evidence"
Write-Host '  O valor so sera consumido no proximo DriverEntry/boot.'
Write-Host 'NAO VALIDADO EM HARDWARE'

