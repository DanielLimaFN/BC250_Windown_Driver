# =============================================================================
# D3D11_LAB_PROMOTION — armar / desarmar / inspecionar
#
# Promove a criacao do dispositivo D3D10/D3D11 no ambiente de laboratorio.
# Quando armado, a bc250umd.dll publica o suporte a D3D10.0/10.1 no GetCaps(Type=130),
# conecta a submissao de filas do No 1 e popula todas as 105 entradas de D3D10.1
# alem dos 7 pontos de entrada da DXGI_DDI_BASE_FUNCTIONS.
#
# Default de fabrica: DESARMADO (fail-closed). Exige -Arm explicito.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm,
    [switch]$IAcceptBlackScreenRisk
)

$ErrorActionPreference = 'Stop'

$key  = 'HKLM:\SOFTWARE\BC250\Umd'
$name = 'D3D11LabPromotion'

function Show-State {
    if (Test-Path -LiteralPath $key) {
        $v = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
        if ($null -eq $v) {
            Write-Host "D3D11_LAB_PROMOTION: DESARMADO (valor ausente)"
        } elseif ($v -ne 0) {
            Write-Host "D3D11_LAB_PROMOTION: ARMADO ($name = $v)" -ForegroundColor Yellow
        } else {
            Write-Host "D3D11_LAB_PROMOTION: DESARMADO ($name = 0)"
        }
    } else {
        Write-Host "D3D11_LAB_PROMOTION: DESARMADO (chave ausente)"
    }
    Write-Host "  chave: $key\$name"
    Write-Host "  logs da corrida: C:\Windows\Temp\BC250_UMD_LOG_<pid>.txt"
    Write-Host "NAO VALIDADO EM HARDWARE"
}

if (-not $Arm -and -not $Disarm) {
    Show-State
    Write-Host ''
    Write-Host 'Uso: -Arm -IAcceptBlackScreenRisk   |   -Disarm'
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
    Write-Host 'D3D11_LAB_PROMOTION desarmado.' -ForegroundColor Green
    Show-State
    exit 0
}

if (-not $IAcceptBlackScreenRisk) {
    throw ('Armar a promocao D3D11 exige -IAcceptBlackScreenRisk. Motivo: com o device D3D10/11 ' +
        'e DXGI base ativos, o Desktop Window Manager (dwm.exe) avanca para composicao completa ' +
        'e apresentacao. A tela pode demorar a convergir; tenha o watchdog armado e o recovery a mao.')
}

if (-not (Test-Path -LiteralPath $key)) {
    New-Item -Path $key -Force | Out-Null
}
New-ItemProperty -LiteralPath $key -Name $name -Value 1 -PropertyType DWord -Force | Out-Null

Write-Host 'D3D11_LAB_PROMOTION ARMADO.' -ForegroundColor Yellow
Write-Host ''
Write-Host 'O que ler no log depois da corrida, em ordem:' -ForegroundColor Cyan
Write-Host '  1. "D3D11_LAB_PROMOTION: ARMADO"               -> a promocao foi lida pelo processo'
Write-Host '  2. "GetCaps: Type=130 ... -> Level 10_0 e 10_1"  -> resposta de caps 3D entregue'
Write-Host '  3. "D3D10 CreateDevice: INTERFACE ACORDADA=..."  -> dispositivo criado'
Write-Host '  4. "DXGI Present: ..."                           -> chamadas de apresentacao DXGI'
Write-Host ''
Show-State
