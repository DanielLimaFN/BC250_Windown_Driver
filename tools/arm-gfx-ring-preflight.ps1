# =============================================================================
# B.1 - GfxRingPreflight: armar / desarmar / inspecionar
#
# O QUE FAZ: manda o driver FOTOGRAFAR o anel grafico (CP_RB0) durante o
# bring-up de laboratorio, e so isso. Le CP_RB0_CNTL, BASE, BASE_HI, RPTR,
# WPTR, CP_RB_WPTR_POLL_CNTL e CP_ME_CNTL, decodifica o CNTL campo a campo e
# compara com o valor catalogado em docs/03.
#
# RISCO: baixo, e a afirmacao tem prova.
#
#   - O contrato NAO escreve registrador nenhum. O portao
#     audit_gfx_ring_contract_is_honest PROIBE qualquer Bc250GfxTxnWrite no
#     estagio de preflight, e a suite offline assere Writes == 0.
#   - O TRANSPORTE MMIO confirma por contador PROPRIO. Se o transporte
#     registrar UMA escrita, a medicao e DESCARTADA e o log diz isso. A
#     promessa de "somente leitura" nao depende so de quem a faz.
#   - O veredito do StartDevice NAO muda em desfecho nenhum: falha de mapa,
#     de leitura ou de unmap vira linha de log, nunca recusa de bind.
#
# POR QUE ISTO IMPORTA: o anel grafico e a UNICA engine desta placa sobre a
# qual o projeto nao tem medicao. SDMA e MEC tem. Tudo o que se sabe do CP_RB0
# e um CP_RB0_CNTL = 0x50000709 anotado uma vez no catalogo. Ele e pre-requisito
# da Parede B (rasterizacao), e escrever nele as cegas e como o unico prototipo
# existente ficou com RB_EXE = 0 sem ninguem perceber.
#
# ESCOPO: o probe fisico SO EXISTE na imagem de laboratorio (compilada com
# BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP). Na imagem de producao o driver
# imprime "[GfxRing] PREFLIGHT ARMADO mas AUSENTE desta imagem" - armado e
# ausente nao podem ser confundidos com armado e recusado.
#
# Default de fabrica: DESARMADO.
#
# ORDEM CORRETA (mesma licao do ensaio de TDR):
#
# A chave HKLM\...\Services\bc250mcdm\Parameters nasce do AddReg do INF na
# instalacao, e o driver le o tunable no StartDevice. Portanto:
#
#   - antes da primeira instalacao a chave nao existe;
#   - alterar o valor depois do bind nao muda o bind atual; so vale no proximo
#     StartDevice (novo bind, desabilitar/reabilitar ou reboot);
#   - nao crie a chave a mao.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm
)

$ErrorActionPreference = 'Stop'

$key  = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$name = 'GfxRingPreflight'

function Show-State {
    if (Test-Path -LiteralPath $key) {
        $v = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
        if ($null -eq $v) {
            Write-Host 'GFX_RING_PREFLIGHT: DESARMADO (valor ausente)'
        } elseif ($v -eq 1) {
            Write-Host "GFX_RING_PREFLIGHT: ARMADO ($name = 1) -- o anel grafico sera FOTOGRAFADO, sem escrita" -ForegroundColor Cyan
        } else {
            Write-Host "GFX_RING_PREFLIGHT: DESARMADO/INVALIDO ($name = $v); o driver exige exatamente 1"
        }
    } else {
        Write-Host 'GFX_RING_PREFLIGHT: chave de servico ausente (driver nao instalado)'
    }
    Write-Host "  chave: $key\$name"
    Write-Host "  o driver so arma com valor presente E igual a 1"
    Write-Host '  somente leitura, conferido por contador independente do transporte MMIO'
    Write-Host 'NAO VALIDADO EM HARDWARE'
}

if ($Arm -and $Disarm) { throw 'Escolha somente -Arm ou -Disarm.' }
if (-not $Arm -and -not $Disarm) {
    Show-State
    Write-Host ''
    Write-Host 'Uso: -Arm   |   -Disarm'
    exit 0
}

$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado: a chave fica em HKLM.'
}

if (-not (Test-Path -LiteralPath $key)) {
    throw ("Chave de servico ausente: $key. O driver precisa estar instalado. " +
        'Nao crie a chave a mao.')
}

if ($Disarm) {
    Set-ItemProperty -LiteralPath $key -Name $name -Value 0 -Type DWord -Force
    Write-Host 'GFX_RING_PREFLIGHT desarmado (0).' -ForegroundColor Green
    Show-State
    exit 0
}

Set-ItemProperty -LiteralPath $key -Name $name -Value 1 -Type DWord -Force

Write-Host 'GFX_RING_PREFLIGHT ARMADO.' -ForegroundColor Cyan
Write-Warning 'O valor so sera lido no PROXIMO StartDevice; o bind atual nao muda.'
Write-Host ''
Write-Host 'O que ler no log, em ordem:' -ForegroundColor Cyan
Write-Host '  1. "[StartDevice] GfxRingPreflight=1 (present=1 compiled=1)"'
Write-Host '     compiled=0 significa imagem de producao: o probe nao existe nela.'
Write-Host '  2. "[GfxRing] PREFLIGHT OK: Reads=7 Writes=0 (transporte Writes=0)"'
Write-Host '     Writes diferente de 0 em QUALQUER dos dois e defeito nosso, nao medicao.'
Write-Host '  3. "[GfxRing] CP_RB0 COMO ENCONTRADO: CNTL=... BASE=... RPTR=... WPTR=..."'
Write-Host '  4. "[GfxRing] CNTL DECODIFICADO: RB_BUFSZ=... RB_EXE=... CE_HQD_NEQ=..."'
Write-Host '  5. "[GfxRing] CONTRA O CATALOGO: lido=... catalogado=50000709 -> CONFERE|DIVERGE"'
Write-Host ''
Write-Host 'Os dois desfechos do item 5 informam:' -ForegroundColor Yellow
Write-Host '  CONFERE -> o 0x50000709 do catalogo e estado estavel do POST, e o plano'
Write-Host '             do anel pode ser construido em cima dele.'
Write-Host '  DIVERGE -> o estado do anel depende do caminho de boot, e ler antes de'
Write-Host '             escrever passa a ser obrigatorio em toda corrida.'
Write-Host ''
Write-Host 'Se a linha 2 nao aparecer, leia "[GfxRing] PREFLIGHT RECUSADO: status=..."'
Write-Host 'ou "[GfxRing] PREFLIGHT DESCARTADO: ..." -- as duas nomeiam a causa.'
Show-State
