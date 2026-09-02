<#
.SYNOPSIS
    Auditoria offline do pacote RENDERONLY_SAFE e da separacao entre fluxos.

.DESCRIPTION
    Duas perguntas, ambas respondidas sem tocar o hardware:

      1. o pacote seguro e' coerente, assinado e realmente livre do bring-up?
      2. os scripts seguros RECUSAM o pacote de laboratorio, e vice-versa?

    A pergunta (2) existe porque o 3.75 falhou justamente por nao haver
    separacao: os mesmos scripts serviam aos dois pacotes, e nada impedia o
    ensaio do Gate 1.5 de rodar sobre a imagem que executa CP soft reset
    durante o StartDevice.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'bc250-safe-common.ps1')

$errors = [System.Collections.Generic.List[string]]::new()

Write-Host ''
Write-Host 'BC250 RENDERONLY_SAFE PACKAGE AUDIT' -ForegroundColor Cyan

# ---- 1. Coerencia do pacote seguro ----
try {
    $manifest = Assert-Bc250SafePackage -Root $root
    Write-Host "  BuildKind        : $($manifest.BuildKind)"
    Write-Host "  DriverVer        : $($manifest.DriverVer)"
    Write-Host "  schema           : $($manifest.TelemetrySchemaVersion)"
    Write-Host "  SkipHwBringup    : $($manifest.SkipHwBringup)"
    Write-Host "  UnsafeBringup    : $($manifest.UnsafeStartDeviceBringup)"
    Write-Host "  SYS              : $($manifest.PackageSysSha256)"
    Write-Host "  INF              : $($manifest.PackageInfSha256)"
    Write-Host "  CAT              : $($manifest.PackageCatSha256)"
    Write-Host '  hashes, assinatura e ausencia do marcador: OK' -ForegroundColor Green
} catch {
    $errors.Add("pacote seguro invalido: $($_.Exception.Message)")
    $manifest = $null
}

# PDB coerente com o SYS (correspondencia por presenca, nao por GUID: a
# checagem de GUID/age exige ferramenta de simbolos e vive no runbook KD).
$packageDir = Get-Bc250SafePackageDir -Root $root
foreach ($f in @('bc250mcdm.sys','bc250mcdm.inf','bc250mcdm.cat')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir $f))) {
        $errors.Add("artefato ausente: $f")
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $packageDir 'bc250mcdm.pdb'))) {
    Write-Host '  AVISO: PDB ausente do pacote; depuracao KD ficara sem simbolos privados.' -ForegroundColor Yellow
}

# INF: todo tunable de laboratorio desligado.
$infPath = Join-Path $packageDir 'bc250mcdm.inf'
if (Test-Path -LiteralPath $infPath) {
    $inf = Get-Content -LiteralPath $infPath -Raw
    # RenderNode/RenderOnlyLab seguem o ProfileMode do manifesto, e a
    # conferencia e' cruzada de proposito: se o INF e o manifesto discordarem,
    # o boot testaria um perfil diferente do declarado e a corrida seria
    # ininterpretavel.
    $direct = ($manifest -ne $null -and $manifest.ProfileMode -eq 'DIRECT_RENDERONLY')
    $mustBe = [ordered]@{
        'Parameters,SkipHwBringup'        = 1
        'Parameters,WddmContractVersion'  = 27
        'Parameters,RenderNode'           = $(if ($direct) { 1 } else { 0 })
        'Parameters,RenderOnlyLab'        = $(if ($direct) { 1 } else { 0 })
        'Parameters,ShaderDispatch'       = 0
        'Parameters,CpSoftReset'          = 0
        'Parameters,PersistentSubmission' = 0
        'Parameters,GpuvaLabPromotion'    = 0
        'Parameters,Vmid0PageTable'       = 0
    }
    foreach ($e in $mustBe.GetEnumerator()) {
        $esc = [regex]::Escape($e.Key)
        if ([regex]::Matches($inf, "(?m)^HKR,$esc,0x00010001,$($e.Value)\s*$").Count -ne 1) {
            $errors.Add("INF seguro: $($e.Key) deveria ser $($e.Value)")
        }
    }
}

# ---- 2. Separacao entre os dois fluxos ----
Write-Host ''
Write-Host '  separacao de fluxos:'

$labManifest = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
if (Test-Path -LiteralPath $labManifest) {
    $lab = Get-Content -LiteralPath $labManifest -Raw | ConvertFrom-Json
    if ($lab.BuildKind -eq 'RENDERONLY_SAFE') {
        $errors.Add('o manifesto de laboratorio declara BuildKind seguro')
    } else {
        Write-Host "    pacote lab continua marcado como $($lab.BuildKind)" -ForegroundColor Green
    }
    if (-not [bool]$lab.PSObject.Properties['UnsafeStartDeviceBringup']) {
        Write-Host '    AVISO: manifesto lab nao declara UnsafeStartDeviceBringup.' -ForegroundColor Yellow
    }
}

# Todo script seguro tem de carregar o gate comum. Um script safe sem o gate
# e' um script safe apenas no nome.
$safeScripts = @(
    'prepare-bc250mcdm-renderonly-safe.ps1',
    'inspect-bc250mcdm-renderonly-safe.ps1',
    'bind-bc250mcdm-renderonly-safe.ps1'
)
foreach ($s in $safeScripts) {
    $p = Join-Path $root "tools\$s"
    if (-not (Test-Path -LiteralPath $p)) { $errors.Add("script seguro ausente: $s"); continue }
    $text = Get-Content -LiteralPath $p -Raw
    if ($text -notmatch 'bc250-safe-common\.ps1') {
        $errors.Add("$s nao carrega bc250-safe-common.ps1")
    }
    if ($text -notmatch 'Assert-Bc250SafePackage') {
        $errors.Add("$s nao chama Assert-Bc250SafePackage")
    }
    if ($text -match 'output_lab') {
        $errors.Add("$s referencia o pacote de laboratorio")
    }
}
if ($errors.Count -eq 0) {
    Write-Host '    todos os scripts seguros exigem o gate e ignoram o pacote lab' -ForegroundColor Green
}

# O gate precisa REALMENTE recusar: prova negativa executando-o contra um
# manifesto de laboratorio sintetico.
$tmp = Join-Path $env:TEMP ("bc250safeaudit_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $tmp 'driver\wdk\package') -Force | Out-Null
@{ BuildKind = 'MCDM_LAB_BRINGUP'; SkipHwBringup = 0; UnsafeStartDeviceBringup = $true } |
    ConvertTo-Json | Set-Content -Encoding UTF8 `
    -LiteralPath (Join-Path $tmp 'driver\wdk\package\output_renderonly_safe.manifest.json')
$refused = $false
try { Assert-Bc250SafePackage -Root $tmp | Out-Null } catch { $refused = $true }
Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
if ($refused) {
    Write-Host '    gate recusa manifesto MCDM_LAB_BRINGUP (prova negativa)' -ForegroundColor Green
} else {
    $errors.Add('o gate ACEITOU um manifesto de laboratorio — protecao inoperante')
}

Write-Host ''
if ($errors.Count -ne 0) {
    foreach ($e in $errors) { Write-Host "  ERRO: $e" -ForegroundColor Red }
    Write-Host 'BC250 RENDERONLY_SAFE PACKAGE AUDIT: FAILED' -ForegroundColor Red
    exit 1
}
Write-Host 'BC250 RENDERONLY_SAFE PACKAGE AUDIT: PASS' -ForegroundColor Green
Write-Host 'INSTALL/BINDING NOT PERFORMED; NAO VALIDADO EM HARDWARE'
exit 0
