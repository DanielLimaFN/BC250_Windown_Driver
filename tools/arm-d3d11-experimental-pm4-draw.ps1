# =============================================================================
# D3D11_EXPERIMENTAL_PM4_DRAW — armar / desarmar / inspecionar
#
# F5.2 (14/09). Permite a emissao experimental de pacotes PM4 para Draw 3D
# (CB_COLOR0, Scissor, DRAW_INDEX_AUTO) submetidos ao Node 1.
#
# ISTO NAO E' ACELERACAO 3D COMPLETA E NAO FECHA A PAREDE B.
# Motivos tecnicos:
#   1. Fila de submissao: emitida no Node 1 (MEC de compute), enquanto DRAW_INDEX_AUTO
#      e pacotes de contexto de render target pertencem ao anel grafico (ME/PFP).
#   2. Nao ha shaders compilados (DXBC->SPIR-V->ACO) carregados na VRAM.
#   3. Nao ha geometria de entrada (vertex buffer, input layout, descriptors).
#   4. Offsets de registradores GFX10 nao possuem proveniencia P1 completa.
#
# Consequencia direta se desarmado (default de fabrica):
#   - Qualquer chamada a pfnDraw/pfnDrawIndexed recusa honestamente com
#     Bc250UmdRejectUnbackedDraw ("Draw: RECUSADO: compilador/shader-state/draw PM4 ausente").
#   - Zero progresso simulado, estrita fidelidade a Lei 4.
#
# Default de fabrica: DESARMADO (fail-closed, valor ausente ou 0).
# Exige -Arm -IAcceptPm4DrawIsExperimentalWithoutShaders explicito.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm,
    [switch]$IAcceptPm4DrawIsExperimentalWithoutShaders
)

$ErrorActionPreference = 'Stop'

$key  = 'HKLM:\SOFTWARE\BC250\Umd'
$name = 'D3D11ExperimentalPm4Draw'

function Show-State {
    if (Test-Path -LiteralPath $key) {
        $v = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
        if ($null -eq $v) {
            Write-Host "D3D11_EXPERIMENTAL_PM4_DRAW: DESARMADO (valor ausente)"
        } elseif ($v -ne 0) {
            Write-Host "D3D11_EXPERIMENTAL_PM4_DRAW: ARMADO ($name = $v)" -ForegroundColor Yellow
        } else {
            Write-Host "D3D11_EXPERIMENTAL_PM4_DRAW: DESARMADO ($name = 0)"
        }
    } else {
        Write-Host "D3D11_EXPERIMENTAL_PM4_DRAW: DESARMADO (chave ausente)"
    }
    Write-Host "  chave: $key\$name"
    Write-Host "  env:   BC250_EXPERIMENTAL_PM4_DRAW=1 (override para testes)"
    Write-Host "  logs da corrida: C:\Windows\Temp\BC250_UMD_LOG_<pid>.txt"
    Write-Host "NAO VALIDADO EM HARDWARE"
}

if (-not $Arm -and -not $Disarm) {
    Show-State
    Write-Host ''
    Write-Host 'Uso: -Arm -IAcceptPm4DrawIsExperimentalWithoutShaders   |   -Disarm'
    exit 0
}

$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado: a chave fica em HKLM.'
}

if ($Disarm) {
    if (Test-Path -LiteralPath $key) {
        Remove-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue
    }
    Write-Host 'D3D11_EXPERIMENTAL_PM4_DRAW desarmado.' -ForegroundColor Green
    Show-State
    exit 0
}

if (-not $IAcceptPm4DrawIsExperimentalWithoutShaders) {
    throw ('Armar a emissao experimental de PM4 exige -IAcceptPm4DrawIsExperimentalWithoutShaders. ' +
        'Motivo: este tunable ativa a emissao de pacotes PM4 de draw no Node 1 sem anel grafico dedicado ' +
        'e sem pipeline de shaders compilados. Na ausencia deles, nao ha rasterizacao real de pixels. ' +
        'O default em producao DEVE permanecer desarmado para preservar a telemetria honesta RECUSADO (Lei 4).')
}

if (-not (Test-Path -LiteralPath $key)) {
    New-Item -Path $key -Force | Out-Null
}
New-ItemProperty -LiteralPath $key -Name $name -Value 1 -PropertyType DWord -Force | Out-Null

Write-Host 'D3D11_EXPERIMENTAL_PM4_DRAW ARMADO.' -ForegroundColor Yellow
Write-Host ''
Write-Host 'O que ler no log depois da corrida, em ordem:' -ForegroundColor Cyan
Write-Host '  1. "D3D11_EXPERIMENTAL_PM4_DRAW: ARMADO" -> o tunable foi lido pelo processo'
Write-Host '  2. "EmitPm4Draw: IndexCount=..."         -> pacotes PM4 emitidos e submetidos'
Write-Host '  3. "NativePm4DrawCount=N"                -> contador de draws nativos incrementado'
Write-Host ''
Write-Host 'AVISO: Emissao PM4 isolada NAO fecha a Parede B sem as etapas B.1 a B.5 completas.' -ForegroundColor DarkGray
Write-Host ''
Show-State
