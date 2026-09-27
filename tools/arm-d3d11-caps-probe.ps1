# =============================================================================
# P7_D3D11_CAPS_PROBE — armar / desarmar / inspecionar
#
# O probe faz a bc250umd.dll publicar a tabela de adapter D3D10/D3D10_2 e
# responder pfnGetSupportedVersions e pfnGetCaps, REGISTRANDO tudo, enquanto
# pfnCreateDevice continua falhando fechado com E_NOTIMPL.
#
# O que ele NAO faz: nao desenha, nao cria device, nao habilita render, nao e'
# fallback de software. Ele so' faz o runtime D3D falar antes de desistir.
#
# Responde as tres perguntas abertas em docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md:
#   1. o dwmcore aceita device de feature level 10_1?
#   2. quais caps ele valida antes de pedir o device?
#   3. qual header corresponde ao Interface 0x000B0011 (minor 17) no 19041?
#
# Default de fabrica: DESARMADO. Exige -Arm explicito.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm,
    [switch]$IAcceptBlackScreenRisk
)

$ErrorActionPreference = 'Stop'

$key  = 'HKLM:\SOFTWARE\BC250\Umd'
$name = 'D3D11CapsProbe'

function Show-State {
    if (Test-Path -LiteralPath $key) {
        $v = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
        if ($null -eq $v) {
            Write-Host "P7_D3D11_CAPS_PROBE: DESARMADO (valor ausente)"
        } elseif ($v -ne 0) {
            Write-Host "P7_D3D11_CAPS_PROBE: ARMADO ($name = $v)" -ForegroundColor Yellow
        } else {
            Write-Host "P7_D3D11_CAPS_PROBE: DESARMADO ($name = 0)"
        }
    } else {
        Write-Host "P7_D3D11_CAPS_PROBE: DESARMADO (chave ausente)"
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
    Write-Host 'P7_D3D11_CAPS_PROBE desarmado.' -ForegroundColor Green
    Show-State
    exit 0
}

if (-not $IAcceptBlackScreenRisk) {
    throw ('Armar o probe exige -IAcceptBlackScreenRisk. Motivo: com a tabela de ' +
        'adapter publicada, o runtime D3D avanca mais do que hoje antes de falhar, ' +
        'e o comportamento do dwm.exe depois disso NAO e conhecido — e exatamente ' +
        'o que o probe existe para medir. A tela pode nao voltar sozinha; tenha o ' +
        'watchdog armado e o recovery a mao.')
}

if (-not (Test-Path -LiteralPath $key)) {
    New-Item -Path $key -Force | Out-Null
}
New-ItemProperty -LiteralPath $key -Name $name -Value 1 -PropertyType DWord -Force | Out-Null

Write-Host 'P7_D3D11_CAPS_PROBE ARMADO.' -ForegroundColor Yellow
Write-Host ''
Write-Host 'O que ler no log depois da corrida, em ordem:' -ForegroundColor Cyan
Write-Host '  1. "CAPS_PROBE: ARMADO"                  -> o probe foi lido pelo processo'
Write-Host '  2. "GetSupportedVersions: declarado[i]=" -> o que declaramos suportar'
Write-Host '  3. "GetCaps: Type=NNN (...)"             -> QUAIS caps o runtime consultou'
Write-Host '  4. "D3D10 CreateDevice: INTERFACE ACORDADA=0x........"'
Write-Host '     -> se aparecer, o runtime FECHOU numa interface. 0x000A0002 = D3D10_1,'
Write-Host '        e o piso de 105 entradas do doc 173 esta confirmado como alvo.'
Write-Host '     -> se NAO aparecer, ele desistiu nos caps, e o log do passo 3 diz onde.'
Write-Host ''
Show-State
