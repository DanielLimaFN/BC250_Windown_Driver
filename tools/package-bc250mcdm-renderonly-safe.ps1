<#
.SYNOPSIS
    Empacota e assina o candidato RENDERONLY_SAFE para o Gate 1.5.

.DESCRIPTION
    Este pacote existe porque o 3.75 usou a imagem de LABORATORIO para um
    ensaio que, por definicao, nao pode tocar hardware. Aquela imagem e'
    compilada com BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP e o INF grava
    SkipHwBringup=0: o StartDevice executa PFP direct load, CP soft reset,
    GCVM, HQD e submissoes P1/P2 enquanto detem o display herdado do GOP.

    O SYS aqui vem de driver\wdk\output\bc250mcdm.sys, produzido por
    build-contract.bat SEM a macro insegura. Nao e' o pacote de laboratorio
    com outro INF: e' outro binario.

    O artefato sai em um diretorio proprio, com BuildKind proprio, de modo que
    nenhum script de laboratorio possa consumi-lo por engano e nenhum script
    seguro possa consumir o de laboratorio.

    NAO instala, NAO vincula, NAO reinicia.
#>
[CmdletBinding()]
param(
    [string]$SignerThumbprint = '8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B',
    # Grava RenderNode=1 e RenderOnlyLab=1 direto no INF, de modo que o
    # PRIMEIRO boot apos o bind ja' suba em render-only.
    #
    # Motivo, medido em 2026-09-01 com o 3.76 escalonado: o boot KMDOD
    # intermediario e' justamente o que apaga a tela. A telemetria mostrou
    # V5PostDisplayAttempted=1 e V5DisplayInitEntered=1 — o driver tomou o
    # scanout do GOP — seguidos de DmmIsSupportedCount=41 e DmmCommitCount=0:
    # o dxgkrnl nunca conseguiu comitar um VidPn, e ninguem ficou dono da tela.
    #
    # O perfil render-only sai do StartDevice ANTES desses dois marcadores.
    # Passar por KMDOD para so' entao promover era atravessar o unico ponto
    # perigoso do caminho sem necessidade nenhuma.
    [switch]$DirectRenderOnly
)

$ErrorActionPreference = 'Stop'

$root        = Split-Path -Parent $PSScriptRoot
$wdkDir      = Join-Path $root 'driver\wdk'
$sourceSys   = Join-Path $wdkDir 'output\bc250mcdm.sys'
$sourcePdb   = Join-Path $wdkDir 'output\bc250mcdm.pdb'
$sourceInf   = Join-Path $wdkDir 'package\bc250mcdm.inf'
$labSys      = Join-Path $wdkDir 'output_lab\bc250mcdm.sys'
$packageDir  = Join-Path $wdkDir 'package\output_renderonly_safe'
$manifestOut = Join-Path $wdkDir 'package\output_renderonly_safe.manifest.json'
$firmwareDir = Join-Path $root 'firmware'

$driverVer   = '09/01/2026,3.78.0.0'
$schema      = 134
$ledgerPath  = Join-Path $wdkDir 'package\renderonly-safe-candidate-ledger.json'

Write-Host ''
Write-Host '=== PACOTE RENDERONLY_SAFE (Gate 1.5) ===' -ForegroundColor Cyan
Write-Host ''

# ------------------------------------------------------------- PREFLIGHT --

if (-not (Test-Path -LiteralPath $sourceSys)) {
    throw "SYS seguro ausente: $sourceSys. Rode driver\wdk\build-contract.bat."
}
$sourceSysHash = (Get-FileHash -LiteralPath $sourceSys -Algorithm SHA256).Hash
$profileMode = $(if ($DirectRenderOnly) { 'DIRECT_RENDERONLY' } else { 'STAGED_KMDOD' })
$existingLedger = if (Test-Path -LiteralPath $ledgerPath) {
    @(Get-Content -LiteralPath $ledgerPath -Raw | ConvertFrom-Json)
} else { @() }
$sameVersion = @($existingLedger | Where-Object { [string]$_.DriverVer -eq $driverVer })
if ($sameVersion.Count -gt 0) {
    throw ("RECUSADO: DriverVer $driverVer ja foi emitido no ledger. " +
           'Nunca reutilize a mesma versao para outro SYS/INF/perfil; incremente DriverVer.')
}

# O SYS seguro NAO pode ser o de laboratorio. Comparar hash e' a unica
# verificacao que nao depende de ninguem lembrar de qual diretorio copiou.
if (Test-Path -LiteralPath $labSys) {
    $labHash  = (Get-FileHash -LiteralPath $labSys   -Algorithm SHA256).Hash
    $safeHash = (Get-FileHash -LiteralPath $sourceSys -Algorithm SHA256).Hash
    if ($labHash -eq $safeHash) {
        throw 'RECUSADO: o SYS seguro tem o mesmo hash do de laboratorio. ' +
              'Rode build-contract.bat para regenerar a imagem de producao.'
    }
}

# Prova positiva de que a macro insegura NAO entrou no binario.
#
# NAO usar nomes de tunable ('SkipHwBringup', 'ShaderDispatch') para isso: eles
# aparecem no .rdata como nomes de campo de telemetria mesmo na imagem de
# producao, e a verificacao daria falso positivo — foi o primeiro erro desta
# checagem. O marcador abaixo e' declarado dentro do proprio
# #if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP).
function Test-BinaryContains([string]$Path, [string]$Text) {
    $bytes   = [System.IO.File]::ReadAllBytes($Path)
    $unicode = [System.Text.Encoding]::Unicode.GetBytes($Text)
    for ($i = 0; $i -le $bytes.Length - $unicode.Length; $i++) {
        $hit = $true
        for ($j = 0; $j -lt $unicode.Length; $j++) {
            if ($bytes[$i + $j] -ne $unicode[$j]) { $hit = $false; break }
        }
        if ($hit) { return $true }
    }
    return $false
}

$unsafeMarker = 'BC250-UNSAFE-STARTDEVICE-BRINGUP-COMPILED'
if (Test-BinaryContains $sourceSys $unsafeMarker) {
    throw 'RECUSADO: o SYS contem o marcador de bring-up inseguro, logo foi ' +
          'compilado com BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP. Este ' +
          'pacote exige a imagem de producao.'
}
# E a checagem inversa: o marcador PRECISA existir na imagem de laboratorio.
# Sem isso, um marcador renomeado ou removido faria a checagem acima passar
# sempre, transformando o gate em decoracao.
$markerSource = Join-Path $wdkDir 'bc250_active_miniport.c'
if (Test-Path -LiteralPath $labSys) {
    $labStale = (Get-Item -LiteralPath $labSys).LastWriteTimeUtc -lt
                (Get-Item -LiteralPath $markerSource).LastWriteTimeUtc
    if (Test-BinaryContains $labSys $unsafeMarker) {
        Write-Host '  deteccao verificada contra a imagem de laboratorio' -ForegroundColor Green
    } elseif ($labStale) {
        # Imagem de laboratorio anterior a introducao do marcador. Nao e'
        # falha: e' um artefato velho. O gate acima continua valido, mas fica
        # sem a contraprova ate o laboratorio ser relinkado.
        Write-Host ('  AVISO: imagem de laboratorio anterior ao marcador; ' +
                    'contraprova indisponivel nesta execucao.') -ForegroundColor Yellow
    } else {
        throw 'RECUSADO: a imagem de laboratorio e mais recente que o ' +
              'marcador e mesmo assim nao o contem. A deteccao esta quebrada; ' +
              'corrija antes de confiar neste gate.'
    }
}
Write-Host '  bring-up inseguro: AUSENTE do binario (verificado por conteudo)' -ForegroundColor Green

# --------------------------------------------------------------- PACOTE --

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'
$packageCat = Join-Path $packageDir 'bc250mcdm.cat'
$packagePdb = Join-Path $packageDir 'bc250mcdm.pdb'

Copy-Item -LiteralPath $sourceSys -Destination $packageSys -Force
if (Test-Path -LiteralPath $sourcePdb) {
    Copy-Item -LiteralPath $sourcePdb -Destination $packagePdb -Force
}

$firmwareNames = @(
    'cyan_skillfish2_sdma.bin','cyan_skillfish2_rlc.bin','cyan_skillfish2_pfp.bin',
    'cyan_skillfish2_me.bin','cyan_skillfish2_ce.bin','cyan_skillfish2_mec.bin')
foreach ($fw in $firmwareNames) {
    $src = Join-Path $firmwareDir $fw
    if (-not (Test-Path -LiteralPath $src)) { throw "firmware ausente: $src" }
    Copy-Item -LiteralPath $src -Destination (Join-Path $packageDir $fw) -Force
}

# O INF seguro: todo tunable de laboratorio desligado, sem excecao.
$safeValues = [ordered]@{
    'Parameters\Probe,SchemaVersion' = $schema
    'Parameters,WddmContractVersion' = 27
    'Parameters\Driver,ActiveMode'   = 1
    'Parameters,RenderNode'          = $(if ($DirectRenderOnly) { 1 } else { 0 })
    'Parameters,RenderOnlyLab'       = $(if ($DirectRenderOnly) { 1 } else { 0 })
    'Parameters,SkipHwBringup'       = 1
    'Parameters,ShaderDispatch'      = 0
    'Parameters,CpSoftReset'         = 0
    'Parameters,Vmid0PageTable'      = 0
    'Parameters,SdmaAutoCtxSw'       = 1
    'Parameters,PersistentSubmission'= 0
    'Parameters,GpuvaLabPromotion'   = 0
}

$infLines = Get-Content -LiteralPath $sourceInf | ForEach-Object {
    $line = $_
    if ($line -match '^DriverVer=') { return "DriverVer=$driverVer" }
    foreach ($entry in $safeValues.GetEnumerator()) {
        $escaped = [regex]::Escape($entry.Key)
        if ($line -match "^HKR,$escaped,0x00010001,\d+\s*$") {
            return "HKR,$($entry.Key),0x00010001,$($entry.Value)"
        }
    }
    return $line
}
$infLines | Set-Content -LiteralPath $packageInf -Encoding ascii

$renderedInf = Get-Content -LiteralPath $packageInf -Raw
foreach ($entry in $safeValues.GetEnumerator()) {
    $escaped = [regex]::Escape($entry.Key)
    $hits = [regex]::Matches($renderedInf,
        "(?m)^HKR,$escaped,0x00010001,$($entry.Value)\s*$")
    if ($hits.Count -ne 1) {
        throw "INF seguro nao materializou exatamente uma vez: $($entry.Key)=$($entry.Value)."
    }
}
if ($renderedInf -notmatch [regex]::Escape("DriverVer=$driverVer")) {
    throw "DriverVer nao materializou: $driverVer."
}

# ------------------------------------------------------------ ASSINATURA --

$cert = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
    Where-Object { $_.Thumbprint -eq $SignerThumbprint } | Select-Object -First 1
if (-not $cert) { throw "certificado $SignerThumbprint nao encontrado." }

# Mesmos caminhos usados pelo empacotador de laboratorio. Inf2Cat vive em
# \x86\, nao em \x64\ — procurar so' em x64 nao o encontra.
function Find-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($c in $Candidates) { if (Test-Path -LiteralPath $c) { return $c } }
    $kit = 'C:\Program Files (x86)\Windows Kits\10\bin'
    $found = Get-ChildItem -Path $kit -Filter $Name -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $found) { throw "$Name nao encontrado no WDK." }
    return $found.FullName
}
$inf2cat = Find-Tool 'Inf2Cat.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\Inf2Cat.exe'
)
$signtool = Find-Tool 'signtool.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe'
)

& $signtool sign /sha1 $SignerThumbprint /fd SHA256 $packageSys
if ($LASTEXITCODE -ne 0) { throw 'assinatura do SYS falhou.' }

& $inf2cat /driver:$packageDir /os:10_X64
if ($LASTEXITCODE -ne 0) { throw 'inf2cat falhou.' }

& $signtool sign /sha1 $SignerThumbprint /fd SHA256 $packageCat
if ($LASTEXITCODE -ne 0) { throw 'assinatura do CAT falhou.' }

& $signtool verify /pa /v $packageSys | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'verificacao da assinatura do SYS falhou.' }
& $signtool verify /pa /v $packageCat | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'verificacao da assinatura do CAT falhou.' }

# -------------------------------------------------------------- MANIFESTO --

$manifest = [ordered]@{
    SchemaVersion               = 1
    BuildKind                   = 'RENDERONLY_SAFE'
    BuiltAtUtc                  = [DateTime]::UtcNow.ToString('o')
    DriverVer                   = $driverVer
    HardwareValidated           = $false
    UnsafeStartDeviceBringup    = $false
    TelemetrySchemaVersion      = $schema
    SkipHwBringup               = 1
    ProfileMode                 = $profileMode
    RenderNode                  = $(if ($DirectRenderOnly) { 1 } else { 0 })
    RenderOnlyLab               = $(if ($DirectRenderOnly) { 1 } else { 0 })
    ShaderDispatchEnabled       = $false
    CpSoftResetEnabled          = $false
    Vmid0PageTableEnabled       = $false
    PersistentSubmissionEnabled = $false
    GpuvaLabPromotionEnabled    = $false
    SourceSysSha256             = $sourceSysHash
    PackageSysSha256            = (Get-FileHash -LiteralPath $packageSys -Algorithm SHA256).Hash
    PackageInfSha256            = (Get-FileHash -LiteralPath $packageInf -Algorithm SHA256).Hash
    PackageCatSha256            = (Get-FileHash -LiteralPath $packageCat -Algorithm SHA256).Hash
    SignerThumbprint            = $SignerThumbprint
    # Hash da recuperacao FONTE. O bind seguro exige que a copia implantada em
    # C:\ bata com este valor: a versao anterior do script nao sabia remover o
    # no PnP orfao e deixava o operador em laco de reboot no Codigo 19.
    RecoverySha256              = (Get-FileHash -LiteralPath (Join-Path $root 'tools\BC250_RECOVERY.cmd') -Algorithm SHA256).Hash
    Verdict                     = 'NAO VALIDADO EM HARDWARE'
    Purpose                     = 'FUNCTIONAL_QUERY_CONNECTION_CHANGE_WDDM27'
}
foreach ($fw in $firmwareNames) {
    $manifest["$($fw -replace '[^A-Za-z0-9]','')Sha256"] =
        (Get-FileHash -LiteralPath (Join-Path $packageDir $fw) -Algorithm SHA256).Hash
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestOut -Encoding UTF8

$candidateIdMaterial = "$driverVer|$profileMode|$($manifest.SourceSysSha256)|$($manifest.PackageInfSha256)"
$candidateIdBytes = [System.Text.Encoding]::UTF8.GetBytes($candidateIdMaterial)
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $candidateId = ([BitConverter]::ToString($sha256.ComputeHash($candidateIdBytes))).Replace('-','')
} finally {
    $sha256.Dispose()
}
$manifest['CandidateId'] = $candidateId
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestOut -Encoding UTF8
$ledgerEntry = [ordered]@{
    CandidateId = $candidateId
    DriverVer = $driverVer
    ProfileMode = $profileMode
    SourceSysSha256 = [string]$manifest.SourceSysSha256
    PackageSysSha256 = [string]$manifest.PackageSysSha256
    PackageInfSha256 = [string]$manifest.PackageInfSha256
    PackageCatSha256 = [string]$manifest.PackageCatSha256
    BuiltAtUtc = [string]$manifest.BuiltAtUtc
    HardwareValidated = $false
    Purpose = [string]$manifest.Purpose
}
@($existingLedger) + @($ledgerEntry) | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $ledgerPath -Encoding UTF8

Write-Host ''
Write-Host "Pacote RENDERONLY_SAFE: $packageDir"
Write-Host "Manifesto:              $manifestOut"
Write-Host "  DriverVer $driverVer | schema $schema | SkipHwBringup=1"
Write-Host ("  perfil do primeiro boot: {0}" -f
    $(if ($DirectRenderOnly) { 'RENDER_ONLY_LAB (sem boot KMDOD)' } else { 'KMDOD (promocao em passo separado)' }))
Write-Host "  SYS $($manifest.PackageSysSha256)"
Write-Host "  CandidateId $candidateId"
Write-Host 'INSTALL/BINDING NAO REALIZADOS; NAO VALIDADO EM HARDWARE'
