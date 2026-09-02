<#
.SYNOPSIS
    Gate compartilhado dos scripts RENDERONLY_SAFE.

.DESCRIPTION
    Existe para que "seguro" seja uma propriedade verificada, e nao uma
    convencao de nome de arquivo. Todo script safe carrega este arquivo e
    chama Assert-Bc250SafePackage antes de tocar em qualquer coisa.

    O 3.75 falhou porque o ensaio do Gate 1.5 rodou sobre a imagem de
    laboratorio, que executa bring-up fisico durante o StartDevice detendo o
    display. Nenhum script impedia aquela combinacao: os mesmos scripts
    serviam aos dois pacotes.
#>

$BC250_SAFE_BUILD_KIND = 'RENDERONLY_SAFE'
$BC250_LAB_BUILD_KIND  = 'MCDM_LAB_BRINGUP'

function Get-Bc250SafeManifestPath {
    param([Parameter(Mandatory)][string]$Root)
    return (Join-Path $Root 'driver\wdk\package\output_renderonly_safe.manifest.json')
}

function Get-Bc250SafePackageDir {
    param([Parameter(Mandatory)][string]$Root)
    return (Join-Path $Root 'driver\wdk\package\output_renderonly_safe')
}

<#
.SYNOPSIS
    Recusa qualquer coisa que nao seja o pacote seguro, e devolve o manifesto.
#>
function Assert-Bc250SafePackage {
    param([Parameter(Mandatory)][string]$Root)

    $manifestPath = Get-Bc250SafeManifestPath -Root $Root
    $packageDir   = Get-Bc250SafePackageDir -Root $Root

    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw ('Recusado: manifesto RENDERONLY_SAFE ausente. Rode ' +
               'tools\package-bc250mcdm-renderonly-safe.ps1.')
    }
    $m = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

    if ($m.BuildKind -ne $BC250_SAFE_BUILD_KIND) {
        throw "Recusado: BuildKind='$($m.BuildKind)'. Este caminho aceita apenas $BC250_SAFE_BUILD_KIND."
    }
    if ($m.BuildKind -eq $BC250_LAB_BUILD_KIND) {
        throw 'Recusado: pacote de laboratorio nao pode ser usado no fluxo seguro.'
    }
    if ([bool]$m.UnsafeStartDeviceBringup) {
        throw 'Recusado: manifesto declara bring-up inseguro compilado.'
    }
    if ([uint32]$m.SkipHwBringup -ne 1) {
        throw "Recusado: SkipHwBringup=$($m.SkipHwBringup); o fluxo seguro exige 1."
    }
    foreach ($flag in @('ShaderDispatchEnabled','CpSoftResetEnabled',
                        'PersistentSubmissionEnabled','GpuvaLabPromotionEnabled',
                        'Vmid0PageTableEnabled')) {
        if ([bool]$m.$flag) { throw "Recusado: $flag habilitado no pacote seguro." }
    }
    # Dois modos de perfil, ambos legitimos, cada um com o par coerente:
    #
    #   STAGED_KMDOD      0/0 — sobe em KMDOD, promocao em passo separado
    #   DIRECT_RENDERONLY 1/1 — primeiro boot ja' em render-only
    #
    # DIRECT existe porque o boot KMDOD intermediario e' o unico ponto do
    # caminho que toma o scanout do GOP, e foi ele que apagou a tela em
    # 2026-09-01. O perfil render-only sai do StartDevice antes disso.
    #
    # Qualquer outro par e' recusado: RenderOnlyLab=1 com RenderNode=0 o
    # proprio driver ja' rejeita com STATUS_DEVICE_CONFIGURATION_ERROR, e um
    # pacote que o declarasse so' geraria um boot desperdicado.
    $profileMode = [string]$m.ProfileMode
    $rn = [uint32]$m.RenderNode
    $ro = [uint32]$m.RenderOnlyLab
    if ($profileMode -eq 'DIRECT_RENDERONLY') {
        if ($rn -ne 1 -or $ro -ne 1) {
            throw "Recusado: ProfileMode=DIRECT_RENDERONLY exige RenderNode=1 e RenderOnlyLab=1; achei $rn/$ro."
        }
    } elseif ($profileMode -eq 'STAGED_KMDOD' -or [string]::IsNullOrEmpty($profileMode)) {
        if ($rn -ne 0 -or $ro -ne 0) {
            throw "Recusado: ProfileMode staged exige RenderNode=0 e RenderOnlyLab=0; achei $rn/$ro."
        }
    } else {
        throw "Recusado: ProfileMode desconhecido: '$profileMode'."
    }
    if ([bool]$m.HardwareValidated) {
        throw 'Recusado: nenhum pacote deste projeto pode declarar HardwareValidated.'
    }
    if ([string]$m.CandidateId -notmatch '^[0-9A-F]{64}$') {
        throw 'Recusado: CandidateId ausente ou invalido.'
    }
    $ledgerPath = Join-Path $Root 'driver\wdk\package\renderonly-safe-candidate-ledger.json'
    if (-not (Test-Path -LiteralPath $ledgerPath -PathType Leaf)) {
        throw 'Recusado: ledger imutavel de candidatos ausente.'
    }
    $ledgerRaw = Get-Content -LiteralPath $ledgerPath -Raw | ConvertFrom-Json
    $ledger = @($ledgerRaw | ForEach-Object { $_ })
    $ledgerMatch = @($ledger | Where-Object {
        [string]$_.CandidateId -eq [string]$m.CandidateId -and
        [string]$_.DriverVer -eq [string]$m.DriverVer -and
        [string]$_.ProfileMode -eq [string]$m.ProfileMode -and
        [string]$_.SourceSysSha256 -eq [string]$m.SourceSysSha256 -and
        [string]$_.PackageSysSha256 -eq [string]$m.PackageSysSha256 -and
        [string]$_.PackageInfSha256 -eq [string]$m.PackageInfSha256 -and
        [string]$_.PackageCatSha256 -eq [string]$m.PackageCatSha256
    })
    if ($ledgerMatch.Count -ne 1) {
        throw 'Recusado: manifesto nao possui exatamente uma identidade correspondente no ledger.'
    }

    # Hashes: o manifesto so' vale se descrever os arquivos que estao la'.
    foreach ($pair in @(
        @{ File = 'bc250mcdm.sys'; Hash = $m.PackageSysSha256; Label = 'SYS' },
        @{ File = 'bc250mcdm.inf'; Hash = $m.PackageInfSha256; Label = 'INF' },
        @{ File = 'bc250mcdm.cat'; Hash = $m.PackageCatSha256; Label = 'CAT' })) {
        $path = Join-Path $packageDir $pair.File
        if (-not (Test-Path -LiteralPath $path)) { throw "Recusado: $($pair.Label) ausente em $packageDir." }
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actual -ne $pair.Hash) {
            throw "Recusado: hash do $($pair.Label) diverge do manifesto."
        }
    }

    foreach ($signed in @('bc250mcdm.sys','bc250mcdm.cat')) {
        $sig = Get-AuthenticodeSignature -LiteralPath (Join-Path $packageDir $signed)
        if ($sig.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
            $sig.SignerCertificate.Thumbprint -ne $m.SignerThumbprint) {
            throw "Recusado: assinatura invalida ou signer divergente em $signed."
        }
    }

    # Prova por conteudo: o marcador do bring-up inseguro nao pode existir.
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $packageDir 'bc250mcdm.sys'))
    $needle = [System.Text.Encoding]::Unicode.GetBytes('BC250-UNSAFE-STARTDEVICE-BRINGUP-COMPILED')
    for ($i = 0; $i -le $bytes.Length - $needle.Length; $i++) {
        $hit = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($bytes[$i + $j] -ne $needle[$j]) { $hit = $false; break }
        }
        if ($hit) {
            throw 'Recusado: o SYS do pacote seguro contem o marcador de bring-up inseguro.'
        }
    }

    return $m
}

<#
.SYNOPSIS
    Confere que o SYS instalado no sistema e' o do pacote seguro.
#>
function Assert-Bc250InstalledMatchesSafe {
    param([Parameter(Mandatory)]$Manifest)

    $installed = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
    if (-not (Test-Path -LiteralPath $installed)) {
        throw 'Recusado: bc250mcdm.sys nao esta instalado.'
    }
    $hash = (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash
    if ($hash -ne $Manifest.PackageSysSha256) {
        throw ("Recusado: SYS instalado ($hash) diverge do pacote seguro " +
               "($($Manifest.PackageSysSha256)).")
    }
}
