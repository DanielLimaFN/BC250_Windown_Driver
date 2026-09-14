<#
.SYNOPSIS
    Empacota e assina a imagem de LABORATORIO do bc250mcdm.

.DESCRIPTION
    Copia driver\wdk\output_lab\bc250mcdm.sys, o INF e firmwares para
    driver\wdk\package\output_lab, gera o catalogo bc250mcdm.cat via Inf2Cat
    e assina ambos com o certificado de teste.
#>
[CmdletBinding()]
param(
    [switch]$EnableShaderDispatch,
    # Fase P1: sobe a fila de computacao persistente e liga o
    # DxgkDdiSubmitCommandVirtual real. Desligado por padrao.
    [switch]$EnablePersistentSubmission,
    # Fase V5: grava RenderNode=1 com RenderOnlyLab=0, o par que o resolvedor
    # de perfil do miniport (bc250_active_miniport.c:7124) traduz para
    # DISPLAY_RENDER. E' o perfil que registra por DxgkInitialize e expoe o no
    # de render, sem cair em RENDER_ONLY_LAB.
    #
    # Ate o 3.81 este switch nao existia e RenderNode so' subia junto com
    # -EnablePhysicalRenderOnly, o que forcava RenderOnlyLab=1. O binder
    # (bind-bc250mcdm-lab.ps1) sempre modelou os dois como independentes, entao
    # pedir -EnableRenderNode la' esbarrava num pacote que nunca sabia
    # produzi-lo. O acoplamento era politica do empacotador, nao limite do
    # miniport.
    [switch]$EnableRenderNode,
    [switch]$EnablePhysicalRenderOnly,
    # Versao de contrato WDDM declarada ao dxgkrnl. Lida como tunable no
    # DriverEntry (bc250_active_miniport.c:7365): 20 registra
    # DXGKDDI_INTERFACE_VERSION_WDDM2_0, 27 registra WDDM2_7. Nao exige rebuild
    # do SYS -- e' so' o valor no INF.
    #
    # POR QUE VIROU PARAMETRO: a corrida 06/09 12:08 (contrato 27) parou antes
    # de o SO pedir a alocacao padrao -- zero GetStandardAllocationDriverData,
    # zero CreateAllocation, zero CommitVidPn, com o SO em laco de 99
    # IsSupportedVidPn. A corrida 05/09 22:20 (contrato 20) convergiu em 18
    # IsSupportedVidPn, criou 155 alocacoes e comitou. Sob 27 o SO tambem passou
    # a consultar DXGKQAITYPE_GPUVERSION (27) e ADAPTERPERFDATA_CAPS (26), que
    # o KMD ainda recusa -- e GPUVERSION e' obrigatoria em WDDM 2.7.
    #
    # Isolar essa variavel exige poder empacotar 20 sem mudar mais nada.
    [ValidateSet(20, 27)][int]$WddmContractVersion = 27
)

$ErrorActionPreference = 'Stop'
if ($EnablePersistentSubmission -and -not $EnableShaderDispatch) {
    throw ('-EnablePersistentSubmission exige -EnableShaderDispatch; ' +
           'sem a receita F8b (CpSoftReset=1) a carga do PFP assenta no laco 0x218..0x21B.')
}
if ($EnablePhysicalRenderOnly -and
    (-not $EnableShaderDispatch -or -not $EnablePersistentSubmission)) {
    throw ('PHYSICAL_RENDER_ONLY exige -EnableShaderDispatch e ' +
           '-EnablePersistentSubmission; sem P1/P2 fisicos o runtime nao pode ser promovido.')
}

# Par (RenderNode, RenderOnlyLab) e o perfil que o miniport deriva dele:
#
#   0/0  DISPLAY_ONLY        -> ProfileMode DISPLAY_LAB
#   1/0  DISPLAY_RENDER      -> ProfileMode DISPLAY_RENDER_LAB
#   1/1  RENDER_ONLY_LAB     -> ProfileMode PHYSICAL_RENDER_ONLY
#
# O par 0/1 nao existe: o proprio driver o recusa com
# STATUS_DEVICE_CONFIGURATION_ERROR, e por isso RenderOnlyLab so' sobe junto
# com RenderNode.
$renderNodeValue = $(if ($EnableRenderNode -or $EnablePhysicalRenderOnly) { 1 } else { 0 })
$renderOnlyLabValue = $(if ($EnablePhysicalRenderOnly) { 1 } else { 0 })
if ($renderOnlyLabValue -eq 1 -and $renderNodeValue -ne 1) {
    throw 'Par invalido: RenderOnlyLab=1 exige RenderNode=1.'
}
$profileModeValue = $(if ($renderOnlyLabValue -eq 1) {
    'PHYSICAL_RENDER_ONLY'
} elseif ($renderNodeValue -eq 1) {
    'DISPLAY_RENDER_LAB'
} else {
    'DISPLAY_LAB'
})
$root = Split-Path -Parent $PSScriptRoot
$sourceSys = Join-Path $root 'driver\wdk\output_lab\bc250mcdm.sys'
$sourcePdb = Join-Path $root 'driver\wdk\output_lab\bc250mcdm.pdb'
$sourceInf = Join-Path $root 'driver\wdk\package\bc250mcdm.inf'
$sourceSdmaFirmware = Join-Path $root 'firmware\cyan_skillfish2_sdma.bin'
$sourceRlcFirmware = Join-Path $root 'firmware\cyan_skillfish2_rlc.bin'
$sourcePfpFirmware = Join-Path $root 'firmware\cyan_skillfish2_pfp.bin'
$sourceMeFirmware = Join-Path $root 'firmware\cyan_skillfish2_me.bin'
$sourceCeFirmware = Join-Path $root 'firmware\cyan_skillfish2_ce.bin'
$sourceMecFirmware = Join-Path $root 'firmware\cyan_skillfish2_mec.bin'
$sourceUmd = Join-Path $root 'driver\umd\output\bc250umd.dll'
$packageDir = Join-Path $root 'driver\wdk\package\output_lab'
$packageManifest = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$recoveryRoot = 'C:\BC250_RECOVERY.cmd'
$certificateThumbprint = '8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B'

function Find-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "$Name nao encontrado."
}

if (-not (Test-Path -LiteralPath $sourceSys)) {
    throw "Imagem de lab ausente: $sourceSys. Execute tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup"
}
if (-not (Test-Path -LiteralPath $sourceUmd -PathType Leaf)) {
    throw "UMD auditada ausente: $sourceUmd. Execute driver\umd\build-umd.bat."
}
if (-not (Test-Path -LiteralPath $sourceInf)) { throw "INF ausente: $sourceInf" }
if (-not (Test-Path -LiteralPath $sourceSdmaFirmware)) { throw "Firmware SDMA ausente: $sourceSdmaFirmware" }
if (-not (Test-Path -LiteralPath $sourceRlcFirmware)) { throw "Firmware RLC ausente: $sourceRlcFirmware" }
if (-not (Test-Path -LiteralPath $sourcePfpFirmware)) { throw "Firmware PFP ausente: $sourcePfpFirmware" }
if (-not (Test-Path -LiteralPath $sourceMeFirmware)) { throw "Firmware ME ausente: $sourceMeFirmware" }
if (-not (Test-Path -LiteralPath $sourceCeFirmware)) { throw "Firmware CE ausente: $sourceCeFirmware" }
if (-not (Test-Path -LiteralPath $sourceMecFirmware)) { throw "Firmware MEC ausente: $sourceMecFirmware" }

if (-not (Test-Path -LiteralPath $recoveryRoot)) {
    throw 'Recuperacao C:\BC250_RECOVERY.cmd ausente.'
}

$inf2cat = Find-Tool 'Inf2Cat.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x86\Inf2Cat.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\Inf2Cat.exe'
)
$signtool = Find-Tool 'signtool.exe' @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe'
)
$compilerPath = Find-Tool 'cl.exe' @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe'
)
if ($inf2cat -notmatch '\\10\.0\.28000\.0\\' -or
    $signtool -notmatch '\\10\.0\.28000\.0\\' -or
    $compilerPath -notmatch '\\Microsoft Visual Studio\\18\\') {
    throw 'Pacote lab bloqueado: toolchain efetiva nao e VS2026 + SDK/WDK 28000.'
}

$certificate = Get-ChildItem Cert:\CurrentUser\My | Where-Object {
    $_.Thumbprint -eq $certificateThumbprint -and $_.HasPrivateKey
}
if (-not $certificate) {
    throw "Certificado de teste com chave privada ausente: $certificateThumbprint"
}

# Guarda de frescor: o empacotador apenas COPIA driver\wdk\output_lab\bc250mcdm.sys.
# Quem o compila e tools\build-bc250mcdm-lab-bringup.ps1 -Execute. Sem esta
# checagem, um SYS obsoleto era assinado e empacotado em silencio, gastando um
# ensaio fisico inteiro com o binario do candidato anterior.
if (-not (Test-Path -LiteralPath $sourceSys -PathType Leaf)) {
    throw ("Imagem de laboratorio ausente: $sourceSys. " +
        'Rode tools\build-bc250mcdm-lab-bringup.ps1 -Execute ' +
        '-IUnderstandThisEnablesDestructiveBringup antes de empacotar.')
}
$sourceSysStamp = (Get-Item -LiteralPath $sourceSys).LastWriteTimeUtc
$staleSources = @(
    Get-ChildItem -LiteralPath (Join-Path $root 'driver\core'),
                                (Join-Path $root 'driver\wdk') `
        -Recurse -File -Include '*.c', '*.h' -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notlike '*\package\*' -and
        $_.FullName -notlike '*\output*\*' -and
        $_.LastWriteTimeUtc -gt $sourceSysStamp
    }
)
if ($staleSources.Count -ne 0) {
    Write-Host 'FONTES MAIS NOVAS QUE A IMAGEM DE LABORATORIO:'
    foreach ($stale in ($staleSources | Select-Object -First 10)) {
        Write-Host ("  - {0} ({1:u})" -f
            $stale.FullName.Substring($root.Length + 1), $stale.LastWriteTimeUtc)
    }
    if ($staleSources.Count -gt 10) {
        Write-Host ("  ... e mais {0} arquivo(s)" -f ($staleSources.Count - 10))
    }
    throw ('SYS de laboratorio esta obsoleto em relacao as fontes. ' +
        'Rode tools\build-bc250mcdm-lab-bringup.ps1 -Execute ' +
        '-IUnderstandThisEnablesDestructiveBringup e empacote de novo.')
}

New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
$packageSys = Join-Path $packageDir 'bc250mcdm.sys'
$packagePdb = Join-Path $packageDir 'bc250mcdm.pdb'
$packageInf = Join-Path $packageDir 'bc250mcdm.inf'
$packageCat = Join-Path $packageDir 'bc250mcdm.cat'
$packageSdmaFirmware = Join-Path $packageDir 'cyan_skillfish2_sdma.bin'
$packageRlcFirmware = Join-Path $packageDir 'cyan_skillfish2_rlc.bin'
$packagePfpFirmware = Join-Path $packageDir 'cyan_skillfish2_pfp.bin'
$packageMeFirmware = Join-Path $packageDir 'cyan_skillfish2_me.bin'
$packageCeFirmware = Join-Path $packageDir 'cyan_skillfish2_ce.bin'
$packageMecFirmware = Join-Path $packageDir 'cyan_skillfish2_mec.bin'
$packageUmd = Join-Path $packageDir 'bc250umd.dll'

Remove-Item -LiteralPath $packageCat -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $sourceSys -Destination $packageSys -Force
if (Test-Path -LiteralPath $sourcePdb) {
    Copy-Item -LiteralPath $sourcePdb -Destination $packagePdb -Force
}

# No INF de laboratorio, colocamos DriverVer mais novo e os gates explicitos.
#
# O schema vive numa unica variavel usada TANTO pela reescrita QUANTO pela
# assercao logo abaixo. Ate o 3.74 eram dois literais independentes, e bastou
# atualizar um deles para o empacotamento quebrar com "nao materializou
# exatamente uma vez" — uma mensagem que descreve o sintoma, nao a causa.
# 135: a faixa A16 acrescentou BC250_PROBE_SECOND_PREFLIGHT_STATUS ao fim do
# enum de probes. O campo foi APENDADO, entao nenhum indice existente mudou e os
# consumidores (que leem por NOME) continuam validos -- mas declarar 134 para um
# conjunto de 135 campos e' duas fontes de verdade para a mesma coisa, e este
# projeto ja pagou caro por isso.
$labSchemaVersion = 135

$infContent = Get-Content -LiteralPath $sourceInf
$sourceDriverVerLine = $infContent | Where-Object { $_ -match '^DriverVer=' } |
    Select-Object -First 1
if ($sourceDriverVerLine -notmatch '^DriverVer=([^,]+),(\d+\.\d+\.\d+\.\d+)$') {
    throw "DriverVer fonte invalido: $sourceDriverVerLine"
}
$sourceDriverDate = $Matches[1]
$sourceDriverVersion = [version]$Matches[2]
# O pacote lab precisa ranquear acima do ativo que compartilha o mesmo HWID.
# O literal antigo 3.80.0.0 ficou abaixo do ativo 3.81.0.0 e permitia ao PnP
# preferir o pacote errado. Derivar o build evita repetir a dessincronizacao.
$labDriverVersion = [version]::new(
    $sourceDriverVersion.Major,
    $sourceDriverVersion.Minor,
    ($sourceDriverVersion.Build + 1),
    $sourceDriverVersion.Revision)
$labDriverVerLine = 'DriverVer={0},{1}' -f $sourceDriverDate, $labDriverVersion
$infContent = $infContent | ForEach-Object {
    if ($_ -match '^DriverVer=') {
        $labDriverVerLine
    } elseif ($_ -match '^HKR,Parameters\\Probe,SchemaVersion,0x00010001,\d+$') {
        'HKR,Parameters\Probe,SchemaVersion,0x00010001,{0}' -f $labSchemaVersion
    } elseif ($_ -match 'HKR,Parameters,SkipHwBringup,0x00010001,\d+') {
        'HKR,Parameters,SkipHwBringup,0x00010001,0'
    } elseif ($_ -match 'HKR,Parameters,ShaderDispatch,0x00010001,\d+') {
        'HKR,Parameters,ShaderDispatch,0x00010001,{0}' -f
            ($(if ($EnableShaderDispatch) { 1 } else { 0 }))
    } elseif ($_ -match 'HKR,Parameters,CpSoftReset,0x00010001,\d+') {
        'HKR,Parameters,CpSoftReset,0x00010001,{0}' -f
            ($(if ($EnableShaderDispatch) { 1 } else { 0 }))
    } elseif ($_ -match 'HKR,Parameters,Vmid0PageTable,0x00010001,\d+') {
        'HKR,Parameters,Vmid0PageTable,0x00010001,0'
    } elseif ($_ -match 'HKR,Parameters,PersistentSubmission,0x00010001,\d+') {
        'HKR,Parameters,PersistentSubmission,0x00010001,{0}' -f
            ($(if ($EnablePersistentSubmission) { 1 } else { 0 }))
    } elseif ($_ -match 'HKR,Parameters,WddmContractVersion,0x00010001,\d+') {
        'HKR,Parameters,WddmContractVersion,0x00010001,{0}' -f $WddmContractVersion
    } elseif ($_ -match 'HKR,Parameters,RenderNode,0x00010001,\d+') {
        'HKR,Parameters,RenderNode,0x00010001,{0}' -f $renderNodeValue
    } elseif ($_ -match 'HKR,Parameters,RenderOnlyLab,0x00010001,\d+') {
        'HKR,Parameters,RenderOnlyLab,0x00010001,{0}' -f $renderOnlyLabValue
    } elseif ($_ -match 'HKR,Parameters,SdmaAutoCtxSw,0x00010001,\d+') {
        'HKR,Parameters,SdmaAutoCtxSw,0x00010001,{0}' -f
            ($(if ($EnableShaderDispatch) { 0 } else { 1 }))
    } elseif ($_ -match 'HKR,Parameters,GpuvaLabPromotion,0x000100(01|03),\d+') {
        # 0x00010003 = FLG_ADDREG_TYPE_DWORD | FLG_ADDREG_BINVALUETYPE |
        # FLG_ADDREG_NOCLOBBER. Aqui o INF declara um DEFAULT, nao um mandato.
        #
        # Sem NOCLOBBER o AddReg do instalador reescreve a chave a cada
        # install, e a promocao que o operador armou antes do bind era apagada
        # em silencio no meio do proprio binding -- o valor e lido uma unica
        # vez, no StartDevice (bc250_active_miniport.c:3443), e ja chegava
        # zerado. Com NOCLOBBER o valor gravado pela politica pre-bind de
        # bind-bc250mcdm-lab.ps1 sobrevive a instalacao, e o readback
        # pos-binding que ja existe la prova que sobreviveu.
        #
        # O default continua 0: um pacote recem-instalado, sem ninguem ter
        # armado nada, nao habilita a promocao.
        'HKR,Parameters,GpuvaLabPromotion,0x00010003,0'
    } elseif ($_ -match 'HKR,Parameters,SdmaPlanoAFallback,0x000100(01|03),\d+') {
        'HKR,Parameters,SdmaPlanoAFallback,0x00010003,0'
    } elseif ($_ -match 'HKR,Parameters,GfxRingBringup,0x000100(01|03),\d+') {
        # NOCLOBBER pela mesma razao do preflight: lido UMA vez no StartDevice.
        'HKR,Parameters,GfxRingBringup,0x00010003,0'
    } elseif ($_ -match 'HKR,Parameters,GfxRingPreflight,0x000100(01|03),\d+') {
        # B.1: DEFAULT, nao mandato. NOCLOBBER pela MESMA razao que custou a
        # corrida do ensaio de TDR (doc 186): o valor e lido UMA vez, no
        # StartDevice, e um AddReg mandatorio apagaria em silencio o que o
        # operador armou antes do bind. Pacote recem-instalado nao mede nada.
        'HKR,Parameters,GfxRingPreflight,0x00010003,0'
    } elseif ($_ -match 'HKR,Parameters,Node1JournalGatePerProcess,0x000100(01|03),\d+') {
        # C3.9: DEFAULT, nao mandato. NOCLOBBER para que o operador possa armar
        # antes do bind sem que o AddReg do install apague em silencio -- foi
        # essa classe de defeito que custou a corrida do ensaio de TDR (doc 186).
        'HKR,Parameters,Node1JournalGatePerProcess,0x00010003,0'
    } else {
        $_
    }
}
$infContent | Set-Content -LiteralPath $packageInf -Encoding ascii

$expectedInfValues = [ordered]@{
    'Parameters\Probe,SchemaVersion' = $labSchemaVersion
    'Parameters,WddmContractVersion' = $WddmContractVersion
    'Parameters\Driver,ActiveMode' = 1
    'Parameters,RenderNode' = $renderNodeValue
    'Parameters,RenderOnlyLab' = $renderOnlyLabValue
    'Parameters,SkipHwBringup' = 0
    'Parameters,ShaderDispatch' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,CpSoftReset' = $(if ($EnableShaderDispatch) { 1 } else { 0 })
    'Parameters,Vmid0PageTable' = 0
    'Parameters,PersistentSubmission' = $(if ($EnablePersistentSubmission) { 1 } else { 0 })
    'Parameters,GpuvaLabPromotion' = 0
    'Parameters,SdmaPlanoAFallback' = 0
    'Parameters,GfxRingPreflight' = 0
    'Parameters,GfxRingBringup' = 0
    'Parameters,Node1JournalGatePerProcess' = 0
    'Parameters,SdmaAutoCtxSw' = $(if ($EnableShaderDispatch) { 0 } else { 1 })
}
# GpuvaLabPromotion e SdmaPlanoAFallback sao gravados como DEFAULT (NOCLOBBER);
# todos os demais sao mandato do pacote e reescrevem a chave a cada install.
$infValueFlags = @{
    'Parameters,GpuvaLabPromotion' = '0x00010003'
    'Parameters,SdmaPlanoAFallback' = '0x00010003'
    'Parameters,GfxRingPreflight' = '0x00010003'
    'Parameters,GfxRingBringup' = '0x00010003'
    'Parameters,Node1JournalGatePerProcess' = '0x00010003'
}
$renderedInf = Get-Content -LiteralPath $packageInf -Raw
foreach ($entry in $expectedInfValues.GetEnumerator()) {
    $escapedName = [regex]::Escape($entry.Key)
    $flag = $(if ($infValueFlags.ContainsKey($entry.Key)) {
        $infValueFlags[$entry.Key]
    } else { '0x00010001' })
    $matches = [regex]::Matches(
        $renderedInf,
        "(?m)^HKR,$escapedName,$flag,$($entry.Value)\s*$")
    if ($matches.Count -ne 1) {
        throw "INF de laboratorio nao materializou exatamente uma vez: $($entry.Key)=$($entry.Value) (flag $flag)."
    }
}

Copy-Item -LiteralPath $sourceSdmaFirmware -Destination $packageSdmaFirmware -Force
Copy-Item -LiteralPath $sourceRlcFirmware -Destination $packageRlcFirmware -Force
Copy-Item -LiteralPath $sourcePfpFirmware -Destination $packagePfpFirmware -Force
Copy-Item -LiteralPath $sourceMeFirmware -Destination $packageMeFirmware -Force
Copy-Item -LiteralPath $sourceCeFirmware -Destination $packageCeFirmware -Force
Copy-Item -LiteralPath $sourceMecFirmware -Destination $packageMecFirmware -Force
if (Test-Path -LiteralPath $sourceUmd) {
    Copy-Item -LiteralPath $sourceUmd -Destination $packageUmd -Force
    & $signtool sign /sha1 $certificateThumbprint /fd SHA256 $packageUmd
    if ($LASTEXITCODE -ne 0) { throw 'Assinatura da DLL UMD de laboratorio falhou.' }
    & $signtool verify /pa /v $packageUmd | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Verificacao Authenticode da DLL UMD de laboratorio falhou.' }
}

& $signtool sign /sha1 $certificateThumbprint /fd SHA256 $packageSys
if ($LASTEXITCODE -ne 0) { throw 'Assinatura do .sys de laboratorio falhou.' }

& $inf2cat /driver:$packageDir /os:10_X64
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $packageCat)) {
    throw 'Inf2Cat recusou o pacote de laboratorio.'
}

& $signtool sign /sha1 $certificateThumbprint /fd SHA256 $packageCat
if ($LASTEXITCODE -ne 0) { throw 'Assinatura do catalogo de laboratorio falhou.' }

& $signtool verify /pa /v $packageSys | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Verificacao Authenticode do .sys de laboratorio falhou.' }
& $signtool verify /pa /v $packageCat | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Verificacao Authenticode do catalogo de laboratorio falhou.' }

$driverVersionLine = Get-Content -LiteralPath $packageInf |
    Where-Object { $_ -match '^DriverVer=' } |
    Select-Object -First 1

# B.1: o valor sai do INF JA EMPACOTADO, nao do INF fonte -- o empacotador
# reescreve a linha para NOCLOBBER, e o manifesto tem de descrever o artefato
# que sera instalado, nao o que estava na arvore.
$gfxRingPreflightLine = Get-Content -LiteralPath $packageInf |
    Where-Object { $_ -match '^HKR,Parameters,GfxRingPreflight,0x000100(?:01|03),(\d+)$' } |
    Select-Object -First 1
if (-not $gfxRingPreflightLine) {
    throw 'Pacote de laboratorio bloqueado: INF empacotado sem HKR,Parameters,GfxRingPreflight -- o manifesto nao pode declarar a medicao do anel grafico.'
}
$gfxRingPreflightLine -match '^HKR,Parameters,GfxRingPreflight,0x000100(?:01|03),(\d+)$' | Out-Null
$gfxRingPreflightValue = [int]$Matches[1]

$gfxRingBringupLine = Get-Content -LiteralPath $packageInf |
    Where-Object { $_ -match '^HKR,Parameters,GfxRingBringup,0x000100(?:01|03),(\d+)$' } |
    Select-Object -First 1
if (-not $gfxRingBringupLine) {
    throw 'Pacote de laboratorio bloqueado: INF empacotado sem HKR,Parameters,GfxRingBringup.'
}
$gfxRingBringupLine -match '^HKR,Parameters,GfxRingBringup,0x000100(?:01|03),(\d+)$' | Out-Null
$gfxRingBringupValue = [int]$Matches[1]

$manifest = [ordered]@{
    SchemaVersion = 1
    BuildKind = 'MCDM_LAB_BRINGUP'
    # Declarado explicitamente para que o fluxo seguro possa recusar este
    # pacote por um campo, e nao por convencao de nome de diretorio.
    UnsafeStartDeviceBringup = $true
    BuiltAtUtc = [DateTime]::UtcNow.ToString('o')
    DriverVer = $driverVersionLine -replace '^DriverVer=', ''
    HardwareValidated = $false
    TelemetrySchemaVersion = $labSchemaVersion
    RenderNode = $renderNodeValue
    RenderOnlyLab = $renderOnlyLabValue
    ProfileMode = $profileModeValue
    WddmContractVersion = $WddmContractVersion
    SkipHwBringup = 0
    ShaderDispatchEnabled = [bool]$EnableShaderDispatch
    CpSoftResetEnabled = [bool]$EnableShaderDispatch
    Vmid0PageTableEnabled = $false
    SdmaAutoCtxSwEnabled = -not [bool]$EnableShaderDispatch
    PersistentSubmissionEnabled = [bool]$EnablePersistentSubmission
    GpuvaLabPromotionEnabled = $false
    # B.1: DERIVADO do INF empacotado, nunca literal. Declarar por literal aqui
    # criaria a segunda fonte de verdade que ja mordeu este projeto na faixa
    # A11 (geometria de GPUVA). O operador tem direito de saber pelo manifesto
    # o que o pacote pede ao silicio -- mesmo sendo somente leitura.
    GfxRingPreflight = $gfxRingPreflightValue
    GfxRingPreflightArmed = ($gfxRingPreflightValue -ne 0)
    GfxRingBringup = $gfxRingBringupValue
    GfxRingBringupArmed = ($gfxRingBringupValue -ne 0)
    Node1TripodDefaultArmed = $false
    D3D10_11NativeReady = $false
    VisualStudio = '2026'
    CompilerPath = $compilerPath
    CompilerFileVersion = (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion
    WindowsKitVersion = '10.0.28000.0'
    Inf2CatPath = $inf2cat
    SignToolPath = $signtool
    SourceSysSha256 = (Get-FileHash -LiteralPath $sourceSys -Algorithm SHA256).Hash
    SourceUmdSha256 = (Get-FileHash -LiteralPath $sourceUmd -Algorithm SHA256).Hash
    PackageSysSha256 = (Get-FileHash -LiteralPath $packageSys -Algorithm SHA256).Hash
    PackageUmdSha256 = (Get-FileHash -LiteralPath $packageUmd -Algorithm SHA256).Hash
    PackageInfSha256 = (Get-FileHash -LiteralPath $packageInf -Algorithm SHA256).Hash
    PackageCatSha256 = (Get-FileHash -LiteralPath $packageCat -Algorithm SHA256).Hash
    SdmaFirmwareSha256 = (Get-FileHash -LiteralPath $packageSdmaFirmware -Algorithm SHA256).Hash
    RlcFirmwareSha256 = (Get-FileHash -LiteralPath $packageRlcFirmware -Algorithm SHA256).Hash
    PfpFirmwareSha256 = (Get-FileHash -LiteralPath $packagePfpFirmware -Algorithm SHA256).Hash
    MeFirmwareSha256 = (Get-FileHash -LiteralPath $packageMeFirmware -Algorithm SHA256).Hash
    CeFirmwareSha256 = (Get-FileHash -LiteralPath $packageCeFirmware -Algorithm SHA256).Hash
    MecFirmwareSha256 = (Get-FileHash -LiteralPath $packageMecFirmware -Algorithm SHA256).Hash
    RecoverySha256 = (Get-FileHash -LiteralPath $recoveryRoot -Algorithm SHA256).Hash
    SignerThumbprint = $certificateThumbprint
    Verdict = 'NAO VALIDADO EM HARDWARE'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath $packageManifest -Encoding utf8

Write-Host "Pacote MCDM de LABORATORIO construido: $packageDir" -ForegroundColor Green
Write-Host "Manifesto de build: $packageManifest"
Write-Host "NAO VALIDADO EM HARDWARE; imagem de laboratorio com bring-up ativo."
