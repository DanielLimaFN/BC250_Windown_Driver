# =============================================================================
# D3D11_DRAW_BLT_SUBSTITUTION — armar / desarmar / inspecionar
#
# C1 (10/09). Permite que pfnDraw/pfnDrawIndexed sejam atendidos por uma copia
# 1:1 de superficie executada pelo SDMA, em vez de recusados com E_NOTIMPL.
#
# ISTO NAO E' RASTERIZACAO. Shader, vertex buffer, index buffer, topologia e
# blend sao IGNORADOS. Sem compilador de shader nao se sabe se o pixel shader
# ligado e' um passe identidade; portanto a equivalencia com o draw e' HIPOTESE
# (H), nunca P0. A substituicao existe para medir o caminho de pixel, e nao
# para declarar aceleracao 3D.
#
# Consequencia direta se armado: um desktop que apareca na tela deixa de ser
# prova de rasterizacao. O log declara "Draw SUBSTITUIDO por copia 1:1 no SDMA
# -- NAO e' rasterizacao" e o contador SubstitutedDrawCount fica separado de
# DrawCount exatamente para que o veredito da corrida continue falsificavel.
#
# Historico: a versao original deste caminho atendia o draw com memcpy de CPU
# entre shadows e devolvia sucesso silencioso -- fallback de software
# disfarcado de renderizacao, proibido pela Lei 4. O memcpy foi removido; o
# unico produtor de pixel agora e' Bc250UmdBltGpu (SDMA). Ver docs/234.
#
# Default de fabrica: DESARMADO (fail-closed). Exige -Arm explicito.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm,
    [switch]$IAcceptSubstitutionIsNotRasterization
)

$ErrorActionPreference = 'Stop'

$key  = 'HKLM:\SOFTWARE\BC250\Umd'
$name = 'D3D11DrawBltSubstitution'

function Show-State {
    if (Test-Path -LiteralPath $key) {
        $v = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
        if ($null -eq $v) {
            Write-Host "D3D11_DRAW_BLT_SUBSTITUTION: DESARMADO (valor ausente)"
        } elseif ($v -ne 0) {
            Write-Host "D3D11_DRAW_BLT_SUBSTITUTION: ARMADO ($name = $v)" -ForegroundColor Yellow
        } else {
            Write-Host "D3D11_DRAW_BLT_SUBSTITUTION: DESARMADO ($name = 0)"
        }
    } else {
        Write-Host "D3D11_DRAW_BLT_SUBSTITUTION: DESARMADO (chave ausente)"
    }
    Write-Host "  chave: $key\$name"
    Write-Host "  logs da corrida: C:\Windows\Temp\BC250_UMD_LOG_<pid>.txt"
    Write-Host "NAO VALIDADO EM HARDWARE"
}

if (-not $Arm -and -not $Disarm) {
    Show-State
    Write-Host ''
    Write-Host 'Uso: -Arm -IAcceptSubstitutionIsNotRasterization   |   -Disarm'
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
    Write-Host 'D3D11_DRAW_BLT_SUBSTITUTION desarmado.' -ForegroundColor Green
    Show-State
    exit 0
}

if (-not $IAcceptSubstitutionIsNotRasterization) {
    throw ('Armar a substituicao de draw exige -IAcceptSubstitutionIsNotRasterization. ' +
        'Motivo: com ela armada, pixel na tela deixa de ser prova de rasterizacao 3D. ' +
        'A substituicao ignora shader, vertex/index buffer, topologia e blend, e vale como ' +
        'medicao do caminho de pixel -- nunca como PASS de etapa 3D.')
}

if (-not (Test-Path -LiteralPath $key)) {
    New-Item -Path $key -Force | Out-Null
}
New-ItemProperty -LiteralPath $key -Name $name -Value 1 -PropertyType DWord -Force | Out-Null

Write-Host 'D3D11_DRAW_BLT_SUBSTITUTION ARMADO.' -ForegroundColor Yellow
Write-Host ''
Write-Host 'O que ler no log depois da corrida, em ordem:' -ForegroundColor Cyan
Write-Host '  1. "DRAW_BLT_SUBSTITUTION: ARMADO"                     -> o tunable foi lido pelo processo'
Write-Host '  2. "Draw SUBSTITUIDO por copia 1:1 no SDMA"            -> a substituicao ocorreu, com Substituidos=N'
Write-Host '  3. "DrawIndexed: RECUSADO"                             -> caiu fora da janela licita e recusou fechado'
Write-Host '  4. No KMD, BYTE_PROOF PASS com NonZero alto            -> caminho de pixel medido'
Write-Host ''
Write-Host 'Substituicao NAO fecha etapa 3D. O item que fecha e compilador + PM4 de draw.' -ForegroundColor DarkGray
Write-Host ''
Show-State
