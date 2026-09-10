<#
.SYNOPSIS
    Constroi a imagem de LABORATORIO do bc250mcdm, com o bring-up de hardware compilado.

.DESCRIPTION
    A imagem de PRODUCAO compila o bring-up fora, por contrato de seguranca:

        bc250_active_miniport.c:2339
        #if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)
            /* ... a registry value must never be able to turn destructive
               laboratory bring-up on during PnP display ownership. */
            Bc250ProbeRecord(BC250_PROBE_SKIP_HW_BRINGUP, 1u);
            return STATUS_SUCCESS;
        #else
            ULONG skipHw = Bc250ActiveReadTunable(L"SkipHwBringup", 1);

    E tools\audit-bc250mcdm-safety.py PROIBE definir essa macro tanto em
    bc250_active_miniport.c quanto em driver\wdk\build-contract.bat.

    Este script NAO viola esse contrato - ele o respeita mantendo os dois
    binarios separados:

      - nao altera bc250_active_miniport.c
      - nao altera build-contract.bat
      - produz um artefato em diretorio proprio (driver\wdk\output_lab)

    Producao e laboratorio sao imagens DIFERENTES. Foi assim que as 167 capturas
    historicas foram feitas: com codigo anterior a 6acb6c8, quando o bring-up
    rodava incondicionalmente. Ver STATUS_E_ROADMAP_CONSOLIDADO.md secao 2.0.2.

    ESTRATEGIA: nao duplica as ~1400 linhas do build-contract.bat. Antes do
    link, executa obrigatoriamente o build contratual completo para que nenhum
    objeto antigo possa entrar numa imagem nova. Depois recompila SOMENTE
    bc250_active_miniport.c com as macros extras e refaz o link substituindo
    esse unico objeto. A lista de objetos e' extraida do proprio
    build-contract.bat, entao nao ha' divergencia silenciosa quando ele mudar.

.PARAMETER Execute
    Obrigatorio para compilar. Sem ele o script so' valida precondicoes.

.PARAMETER IUnderstandThisEnablesDestructiveBringup
    Obrigatorio junto com -Execute. A imagem resultante executa PFP direct load,
    CP, GCVM e HQD durante o StartDevice, enquanto o dispositivo detem o display.
    E' o cenario de tela preta descrito na secao 6 do STATUS.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1
#>
[CmdletBinding()]
param(
    [switch]$Execute,
    [switch]$IUnderstandThisEnablesDestructiveBringup
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$wdkDir    = Join-Path $root 'driver\wdk'
$buildBat  = Join-Path $wdkDir 'build-contract.bat'
$objDir    = Join-Path $wdkDir 'output'
$labDir    = Join-Path $wdkDir 'output_lab'
$source    = Join-Path $wdkDir 'bc250_active_miniport.c'
$labObj    = Join-Path $labDir 'bc250_active_miniport_lab.obj'
$labSys    = Join-Path $labDir 'bc250mcdm.sys'
$labPdb    = Join-Path $labDir 'bc250mcdm.pdb'
$prodObj   = 'bc250_active_miniport_driver.obj'

Write-Host ''
Write-Host '=== BUILD DE LABORATORIO - BRING-UP DE HARDWARE ATIVO ===' -ForegroundColor Magenta
Write-Host ''

$blockers = [System.Collections.Generic.List[string]]::new()

foreach ($p in @($buildBat, $source)) {
    if (-not (Test-Path -LiteralPath $p)) { $blockers.Add("artefato ausente: $p") }
}
if (-not (Test-Path -LiteralPath $objDir)) {
    $blockers.Add("objetos de producao ausentes em $objDir - rode driver\wdk\build-contract.bat primeiro")
}

# ---- extrai o comando de link do bc250mcdm.sys do proprio build-contract.bat ----

$objList = @()
$cflags  = $null
if (Test-Path -LiteralPath $buildBat) {
    $lines = Get-Content -LiteralPath $buildBat

    # CFLAGS: linha 'set CFLAGS=' e suas continuacoes com '^'
    $acc = $null
    foreach ($ln in $lines) {
        if ($null -eq $acc -and $ln -match '^\s*set\s+CFLAGS=(.*)$') {
            $acc = $Matches[1]
        } elseif ($null -ne $acc) {
            $acc = $acc.TrimEnd('^') + ' ' + $ln.Trim()
        }
        if ($null -ne $acc -and $acc -notmatch '\^\s*$' -and $ln -notmatch '\^\s*$') { break }
    }
    if ($null -ne $acc) { $cflags = ($acc -replace '\^',' ').Trim() }

    # Objetos: entre /OUT:"...bc250mcdm.sys" e a primeira lib
    $inLink = $false
    foreach ($ln in $lines) {
        if ($ln -match '/OUT:.*bc250mcdm\.sys') { $inLink = $true; continue }
        if ($inLink) {
            if ($ln -match '\.lib"') { break }
            if ($ln -match '"%ODIR%\\([^"]+\.obj)"') { $objList += $Matches[1] }
        }
    }
}

if ($objList.Count -eq 0) { $blockers.Add('nao foi possivel extrair a lista de objetos do link de bc250mcdm.sys') }
if ($null -eq $cflags)    { $blockers.Add('nao foi possivel extrair CFLAGS de build-contract.bat') }
if ($objList.Count -gt 0 -and $objList -notcontains $prodObj) {
    $blockers.Add("o link de bc250mcdm.sys nao usa $prodObj - o script precisa ser revisto")
}

foreach ($o in $objList) {
    if (-not (Test-Path -LiteralPath (Join-Path $objDir $o))) {
        $blockers.Add("objeto ausente: $o - rode build-contract.bat primeiro")
    }
}

Write-Host ("  build-contract.bat : {0}" -f $buildBat)
Write-Host ("  objetos no link    : {0}" -f $objList.Count)
Write-Host ("  objeto substituido : {0} -> bc250_active_miniport_lab.obj" -f $prodObj)
Write-Host ("  saida              : {0}" -f $labSys)
Write-Host ''

foreach ($b in $blockers) { Write-Host ("  BLOQUEIO: {0}" -f $b) -ForegroundColor Red }
if ($blockers.Count -gt 0) {
    Write-Host ''
    Write-Host 'PREFLIGHT REPROVADO - nada foi construido.' -ForegroundColor Red
    exit 1
}

Write-Host '  Preflight aprovado.' -ForegroundColor Green
Write-Host ''
Write-Host '  A imagem resultante executa PFP direct load, CP, GCVM e HQD durante o' -ForegroundColor Yellow
Write-Host '  StartDevice, com o dispositivo detendo o display. Cenario de tela preta.' -ForegroundColor Yellow
Write-Host ''

if (-not $Execute) {
    Write-Host 'Modo preflight. Para construir:'
    Write-Host '  powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup'
    exit 0
}
if (-not $IUnderstandThisEnablesDestructiveBringup) {
    Write-Host '-Execute exige tambem -IUnderstandThisEnablesDestructiveBringup.' -ForegroundColor Red
    exit 1
}

# ------------------------------------------------------------------- BUILD --

Write-Host '[base] reconstruindo todos os objetos e executando contratos...' -ForegroundColor Cyan
$baseBuildStartedUtc = [DateTime]::UtcNow
& cmd.exe /c ('"{0}"' -f $buildBat)
if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host ("BUILD CONTRATUAL BASE FALHOU (exit {0}); imagem de lab nao sera vinculada." -f
        $LASTEXITCODE) -ForegroundColor Red
    exit 1
}

# O build acima e' a barreira de proveniencia. Ainda assim, confira que cada
# objeto que entrara no link foi realmente renovado nesta invocacao.
foreach ($o in $objList) {
    $objectPath = Join-Path $objDir $o
    if ((Get-Item -LiteralPath $objectPath).LastWriteTimeUtc -lt $baseBuildStartedUtc) {
        Write-Host ("Objeto nao foi renovado pelo build atual: {0}" -f $o) -ForegroundColor Red
        exit 1
    }
}

if (-not (Test-Path -LiteralPath $labDir)) {
    New-Item -ItemType Directory -Force -Path $labDir | Out-Null
}

$defines = '/DBC250_ENABLE_DXGK_REGISTRATION=1 /DBC250_ENABLE_PERSISTENT_TELEMETRY=1 /DBC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP=1'

$objArgs = ($objList | ForEach-Object {
    if ($_ -eq $prodObj) { '"' + $labObj + '"' } else { '"' + (Join-Path $objDir $_) + '"' }
}) -join ' '

$bat = @"
@echo off
setlocal EnableExtensions EnableDelayedExpansion
set ROOTDIR=$root
for %%D in (C D E F G) do (
  for %%E in (BuildTools Community Professional Enterprise) do (
    if not defined VCVARS if exist "%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCVARS if exist "%%D:\Program Files\Microsoft Visual Studio\2026\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2026\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not defined VCVARS ( echo ERRO: Visual Studio 2026 nao encontrado. ^& exit /b 1 )
call "!VCVARS!" >nul
if errorlevel 1 exit /b 1
set "WDK=C:\Program Files (x86)\Windows Kits\10"
if defined WindowsSdkDir set "WDK=%WindowsSdkDir:~0,-1%"
set "WDKVER=10.0.28000.0"
if not exist "%WDK%\Include\%WDKVER%\km\ntddk.h" ( echo ERRO: WDK %WDKVER% nao encontrado. ^& exit /b 1 )

echo [cc-lab] bc250_active_miniport.c ^(bring-up ATIVO^)
cl $cflags $defines /Fo"$labObj" "$source"
if errorlevel 1 exit /b 1

echo [link-lab] bc250mcdm.sys ^(laboratorio^)
link /NOLOGO /Brepro /NODEFAULTLIB /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /DEBUG /PDB:"$labPdb" /OUT:"$labSys" $objArgs "%WDK%\Lib\%WDKVER%\km\x64\ntoskrnl.lib" "%WDK%\Lib\%WDKVER%\km\x64\hal.lib" "%WDK%\Lib\%WDKVER%\km\x64\BufferOverflowK.lib" "%WDK%\Lib\%WDKVER%\km\x64\displib.lib"
if errorlevel 1 exit /b 1
endlocal
"@

$tmp = Join-Path $labDir '_lab_build.bat'
$bat | Set-Content -LiteralPath $tmp -Encoding ascii

& cmd.exe /c "$tmp"
$rc = $LASTEXITCODE
Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue

if ($rc -ne 0) {
    Write-Host ''
    Write-Host ("BUILD DE LABORATORIO FALHOU (exit {0})." -f $rc) -ForegroundColor Red
    exit 1
}

# ------------------------------------------------------- VERIFICACAO POSITIVA --
# Nao basta compilar sem erro: e' preciso PROVAR que as macros mudaram o binario.
# Em 2026-08-29 uma build "corrigida" saiu byte-identica a' anterior e a correcao
# so' nao passou por boa porque o binario foi inspecionado. Aqui isso e' gate.

function Test-BinaryContains {
    param([string]$Path, [string]$Text)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $bytes  = [IO.File]::ReadAllBytes($Path)
    $needle = [Text.Encoding]::Unicode.GetBytes($Text)
    for ($i = 0; $i -le $bytes.Length - $needle.Length; $i++) {
        $ok = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($bytes[$i + $j] -ne $needle[$j]) { $ok = $false; break }
        }
        if ($ok) { return $true }
    }
    return $false
}

Write-Host ''
Write-Host '=== VERIFICACAO ===' -ForegroundColor Cyan

$prodSys  = Join-Path $objDir 'bc250mcdm.sys'
$hashLab  = (Get-FileHash -LiteralPath $labSys -Algorithm SHA256).Hash
$hashProd = if (Test-Path -LiteralPath $prodSys) { (Get-FileHash -LiteralPath $prodSys -Algorithm SHA256).Hash } else { $null }

Write-Host ("  laboratorio : {0}  {1} bytes" -f $hashLab.Substring(0,16), (Get-Item $labSys).Length)
if ($null -ne $hashProd) {
    Write-Host ("  producao    : {0}  {1} bytes" -f $hashProd.Substring(0,16), (Get-Item $prodSys).Length)
}

$fails = [System.Collections.Generic.List[string]]::new()
if ($null -ne $hashProd -and $hashLab -eq $hashProd) {
    $fails.Add('imagem de laboratorio e' + "'" + ' byte-identica a' + "'" + ' de producao - as macros nao surtiram efeito')
}
if (-not (Test-BinaryContains $labSys 'bc250mcdm\Parameters')) {
    $fails.Add('string do caminho de registro ausente - Bc250ActiveReadTunable nao foi compilada')
}
foreach ($requiredTelemetry in @(
        'P1RearmQueueMaskApplied',
        'P1RearmQueueMaskReadback',
        'P1RearmQueueMaskPassed',
        'P1RingDword0',
        'P1HqdPqBaseLo')) {
    if (-not (Test-BinaryContains $labSys $requiredTelemetry)) {
        $fails.Add("telemetria 3.53 ausente do SYS: $requiredTelemetry - objeto antigo no link")
    }
}

foreach ($f in $fails) { Write-Host ("  FALHA: {0}" -f $f) -ForegroundColor Red }
if ($fails.Count -gt 0) {
    Write-Host ''
    Write-Host 'BUILD DE LABORATORIO REPROVADO NA VERIFICACAO.' -ForegroundColor Red
    exit 1
}

Write-Host '  bring-up compilado: CONFIRMADO no binario' -ForegroundColor Green
Write-Host ''
Write-Host ("IMAGEM DE LABORATORIO PRONTA: {0}" -f $labSys) -ForegroundColor Magenta
Write-Host ''
Write-Host 'Esta imagem NAO passa pelo pacote assinado de producao e NAO deve ser'
Write-Host 'instalada pela cadeia auditada. Instalar exige decisao explicita.'
Write-Host 'NAO VALIDADO EM HARDWARE'
exit 0
