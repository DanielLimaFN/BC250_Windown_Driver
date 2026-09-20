<#
.SYNOPSIS
    Teste de Conformidade Direct3D 11 Nativo (bc250umd.dll -> Vulkan Bridge -> BC-250).

.DESCRIPTION
    Executa o pipeline Direct3D 11 canonico via Windows Runtime padrao (sem DXVK na pasta),
    roteando atraves da UMD nativa do projeto (bc250umd.dll), Vulkan Graphics Bridge,
    Mesa RADV e silicio fisico da AMD BC-250 (1002:13FE).

    Valida:
    - Negociacao de interface D3D10_1/D3D11 com o runtime do Windows.
    - Criacao de recursos (Textures, Buffers, RTV, DSV) e compilacao de shaders via D3DCompile.
    - Pipeline Cache e execucao de draws via vkCmdDraw e apresentacao WDDM.
    - Sustentacao de 600 quadros a 60 FPS no silicio real.
#>
[CmdletBinding()]
param(
    [string]$EvidenceDir,
    [string]$Harness,
    [ValidateRange(10,300)][int]$TimeoutSeconds = 120,
    [ValidateRange(1,3600)][int]$Frames = 600,
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$Vsync = 0,
    [switch]$Headless,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$kmdAudit = Join-Path $root 'tools\audit-bc250mcdm-safety.py'
$safeGuardAuditScript = Join-Path $root 'tools\assert-bc250-safe-bind-guard.ps1'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'

if ([string]::IsNullOrWhiteSpace($Harness)) {
    $Harness = Join-Path $root 'driver\tests\output_d3d11_native\bc250-d3d11-conformance.exe'
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $root ("docs\evidence\" + $stamp + "_d3d11_native_conformance")
}

Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host " BC-250 DIRECT3D 11 NATIVE CONFORMANCE GATE (bc250umd.dll)" -ForegroundColor Cyan
Write-Host " Target: AMD BC-250 APU (1002:13FE) | Windows 10 22H2 x64" -ForegroundColor Cyan
Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host "  Harness:      $Harness"
Write-Host "  Frames:       $Frames"
Write-Host "  Headless:     $Headless"
Write-Host "  Evidence Dir: $EvidenceDir"

if (-not (Test-Path -LiteralPath $Harness)) {
    throw "Harness D3D11 nativo nao encontrado em: $Harness. Execute tools\build-d3d11-native-conformance.bat primeiro."
}

if (-not $Execute) {
    Write-Host "`n[MODO SIMULACAO/PREFLIGHT] Para executar no silicio, execute com -Execute." -ForegroundColor Yellow
    exit 0
}

if (-not (Test-Path -LiteralPath $EvidenceDir)) {
    New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
}

$evidenceLog = Join-Path $EvidenceDir 'd3d11_native_conformance.log'

Write-Host "`n[PREFLIGHT 1/3] Auditoria de seguranca KMD..." -ForegroundColor Cyan
if (Test-Path -LiteralPath $kmdAudit) {
    & python $kmdAudit
    if ($LASTEXITCODE -ne 0) {
        throw "Auditoria de seguranca do KMD reprovou com exit $LASTEXITCODE."
    }
    Write-Host "  -> KMD Safety Audit: PASS" -ForegroundColor Green
}

Write-Host "`n[PREFLIGHT 2/3] Verificando integridade do boot fuse..." -ForegroundColor Cyan
$inspectScript = Join-Path $root 'tools\inspect-boot-fuse.ps1'
if (Test-Path -LiteralPath $inspectScript) {
    & $inspectScript
}

Write-Host "`n[PREFLIGHT 3/3] Checando biblioteca auxiliar C:\BC250_LIB..." -ForegroundColor Cyan
if (-not (Test-Path -LiteralPath 'C:\BC250_LIB\vulkan_radeon.dll')) {
    Write-Host "  [AVISO] vulkan_radeon.dll nao encontrada em C:\BC250_LIB. Verifique variaveis de ambiente." -ForegroundColor Yellow
} else {
    Write-Host "  -> C:\BC250_LIB: OK" -ForegroundColor Green
}

Write-Host "`n[EXECUCAO NO SILICIO] Disparando bc250-d3d11-conformance.exe..." -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# DESPEJO DO IB GRAFICO -- 19/09/2026.
#
# Na Corrida 34 o CP_RB0 parou dentro de um IB de 1008 DW e nao guardamos UM
# dword dele; o diagnostico publicado saiu com tres hipoteses e nenhuma medicao
# que as separasse. O RADV agora despeja o stream quando BC250_RADV_IB_DUMP
# aponta um diretorio (fail-closed: sem a variavel, nada e' aberto).
#
# O destino e' a PASTA DE EVIDENCIA, nao %TEMP%. Essa escolha e' a licao da C2,
# que custou tres criterios de corrida: o que nao cai na pasta de evidencia
# desaparece quando a maquina reinicia, e o despejo so' serve se sobreviver.
# ---------------------------------------------------------------------------
$ibDumpDir = Join-Path $EvidenceDir 'gfx-ib-dump'
if (-not (Test-Path -LiteralPath $ibDumpDir)) {
    New-Item -ItemType Directory -Path $ibDumpDir -Force | Out-Null
}
$env:BC250_RADV_IB_DUMP = $ibDumpDir
# MEDIDO na Corrida 35: a variavel de ambiente NAO alcanca o dwm.exe, e foi o
# dwm.exe que submeteu o IB que travou o CP. O despejo saiu vazio por isso.
# A chave de registro alcanca TODO processo que carrega o vulkan_radeon.dll.
try {
    $umdRegistryPath = 'HKLM:\SOFTWARE\BC250\Umd'
    # PowerShell 5.1 trata New-Item -Force sobre chave existente como
    # recriacao: os valores preexistentes (inclusive D3D11LabPromotion) somem.
    # Crie somente quando ausente; a instrumentacao nao pode alterar gates.
    if (-not (Test-Path -LiteralPath $umdRegistryPath)) {
        New-Item -Path $umdRegistryPath -ErrorAction Stop | Out-Null
    }
    Set-ItemProperty -Path $umdRegistryPath -Name 'GfxIbDumpDir' `
        -Value $ibDumpDir -Type String -ErrorAction Stop
    Write-Host "  -> GfxIbDumpDir (HKLM) e BC250_RADV_IB_DUMP = $ibDumpDir" -ForegroundColor Cyan
} catch {
    Write-Warning ("Nao foi possivel armar HKLM GfxIbDumpDir: " + $_.Exception.Message +
                   " -- o despejo so valera para o harness, nao para o dwm.exe")
}

$argsList = @("--frames", "$Frames", "--width", "$Width", "--height", "$Height", "--vsync", "$Vsync")
if ($Headless) {
    $argsList += "--headless"
}

$stdoutFile = Join-Path $EvidenceDir 'conformance-stdout.tmp'
$stderrFile = Join-Path $EvidenceDir 'conformance-stderr.tmp'
$timedOut = $false
$exitCode = -1
$stdout = ''
$stderr = ''

$proc = Start-Process -FilePath $Harness -ArgumentList ($argsList -join ' ') `
    -WorkingDirectory ([System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($Harness))) `
    -NoNewWindow -PassThru `
    -RedirectStandardOutput $stdoutFile `
    -RedirectStandardError $stderrFile

if ($null -eq $proc) {
    throw "Start-Process de $Harness falhou."
}

$finished = $proc.WaitForExit($TimeoutSeconds * 1000)
if (-not $finished) {
    $timedOut = $true
    try { $proc.Kill() } catch { }
} else {
    $proc.Refresh()
    $exitCode = [int]$proc.ExitCode
}

if (Test-Path -LiteralPath $stdoutFile) {
    $stdout = Get-Content -LiteralPath $stdoutFile -Raw -ErrorAction SilentlyContinue
    if ($stdout) { Write-Host $stdout }
}
if (Test-Path -LiteralPath $stderrFile) {
    $stderr = Get-Content -LiteralPath $stderrFile -Raw -ErrorAction SilentlyContinue
    if ($stderr) { Write-Host $stderr -ForegroundColor Red }
}

$fullLog = "STDOUT:`r`n" + $stdout + "`r`nSTDERR:`r`n" + $stderr
Set-Content -LiteralPath $evidenceLog -Value $fullLog -Encoding UTF8

if ($timedOut) {
    Write-Host "`n[TIMEOUT] Teste excedeu $TimeoutSeconds segundos e foi cancelado." -ForegroundColor Red
    $exitCode = -1
}

# Verificacao de evidencia fisica de rasterizacao no log da UMD
$pixelVerdict = $false
$candidateSources = @()
$candidateSources += @(Get-ChildItem -LiteralPath 'C:\Windows\Temp' -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction SilentlyContinue)
if ($env:TEMP -and (Test-Path -LiteralPath $env:TEMP)) {
    $candidateSources += @(Get-ChildItem -LiteralPath $env:TEMP -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction SilentlyContinue)
}
if (Test-Path -LiteralPath $stderrFile) {
    $candidateSources += Get-Item -LiteralPath $stderrFile
}
foreach ($src in $candidateSources) {
    $lines = Get-Content -LiteralPath $src.FullName -ErrorAction SilentlyContinue | Select-String -Pattern 'INSPECAO LINEAR_BUF'
    if ($lines) {
        foreach ($l in $lines) {
            Write-Host ("  " + $l.Line.Trim()) -ForegroundColor Magenta
            if ($l.Line -match 'NonZeroSamples=([1-5])/5') {
                $pixelVerdict = $true
            }
        }
    }
}

Write-Host "`n===================================================================" -ForegroundColor Cyan
Write-Host " RESULTADO DO TESTE D3D11 NATIVO (EXIT CODE: $exitCode)" -ForegroundColor Cyan
Write-Host "===================================================================" -ForegroundColor Cyan
# ---------------------------------------------------------------------------
# PROVA DE QUADRO -- 19/09, depois da Corrida 35.
#
# Naquela corrida o harness NAO imprimiu uma unica linha de quadro (nem
# PRESENT_OK nem PRESENT_FALHOU), nao houve timeout, e ainda assim o runner
# anunciou "EXIT CODE: 0". Exit code 0 sem prova de quadro nao e sucesso: e
# ausencia de medicao. O mecanismo daquele 0 permanece INDETERMINADO, e este
# bloco nao depende de descobri-lo -- ele exige a evidencia positiva.
# ---------------------------------------------------------------------------
$frameEvidence = 0
$frameFailure = 0
if (Test-Path -LiteralPath $stdoutFile) {
    $stdoutText = Get-Content -LiteralPath $stdoutFile -Raw -ErrorAction SilentlyContinue
    if ($stdoutText) {
        $frameEvidence = ([regex]::Matches($stdoutText, 'PRESENT_OK')).Count
        $frameFailure = ([regex]::Matches($stdoutText, 'PRESENT_FALHOU')).Count
    }
}
if ($frameEvidence -eq 0 -and $frameFailure -eq 0) {
    Write-Host ("`n[SEM MEDICAO] O harness nao imprimiu NENHUMA linha de quadro " +
                "(PRESENT_OK/PRESENT_FALHOU). Exit code reportado: $exitCode. " +
                "Esta corrida nao mediu apresentacao -- nao a conte como sucesso.") -ForegroundColor Red
    if ($exitCode -eq 0) { $exitCode = 3 }
}

if ($exitCode -eq 0) {
    Write-Host " Quadros com Present aceito: $frameEvidence | Presents falhos: $frameFailure" -ForegroundColor Cyan
    if ($pixelVerdict) {
        Write-Host " VEREDITO: SUCESSO COMPROVADO - D3D11 Nativo com rasterizacao fisica de pixels confirmada!" -ForegroundColor Green
    } else {
        Write-Host " VEREDITO: PARCIAL - Processo finalizou com Exit 0, mas inspecao de pixels pendente de confirmacao." -ForegroundColor Yellow
    }
} else {
    Write-Host " VEREDITO: FALHA - Exit code $exitCode. Consulte log em $evidenceLog" -ForegroundColor Red
}

exit $exitCode
