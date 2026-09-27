# =============================================================================
# item 25 - TdrInduceHang: armar / desarmar / inspecionar
#
# O QUE FAZ: manda o driver travar a SDMA0 DE PROPOSITO, uma vez por bind, na
# primeira submissao do Node 0. O hang e' real: um POLL_REGMEM esperando
# 0xDEADDEAD num slot que o driver nunca escreve, com retry e intervalo no
# teto. A submissao e' ACEITA e o completion NAO e' notificado: e' a fence
# pendente que faz o timeout de 2 s do dxgkrnl disparar o TDR.
#
# RISCO - LEIA: se o TDR do Windows nao conseguir recuperar o adaptador, o
# desfecho e' BugCheck (0x117/0x141/0x142) e reboot. Tenha o watchdog armado,
# boot frio antes e tools\BC250_RECOVERY.cmd a mao. O fusivel anti-boot-loop
# cobre a repeticao no proximo boot.
#
# Default de fabrica: DESARMADO. Dispara UMA vez por bind.
#
# ORDEM CORRETA:
#
# A chave HKLM\...\Services\bc250mcdm\Parameters e' criada pelo AddReg do INF
# durante a instalacao, e o driver le o tunable no StartDevice. Portanto:
#
#   - antes da primeira instalacao a chave nao existe;
#   - alterar o valor depois do bind nao muda o bind atual; so vale no proximo
#     StartDevice (novo bind, desabilitar/reabilitar ou reboot);
#   - para uma corrida unica, instale um pacote cujo INF ja declare o valor.
#
# Construcao do pacote de ensaio:
#   tools\build-bc250mcdm-package.ps1 -PackageDriverVersion X.Y.Z.W
#     -ArmTdrInducedHang -IAcceptDeliberateHangAndPossibleBugCheck
#
# Este script serve para desarmar sem reinstalar e para armar quando um NOVO
# StartDevice vai acontecer de qualquer forma. Nao crie a chave a mao.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm,
    [switch]$IAcceptDeliberateHangAndPossibleBugCheck
)

$ErrorActionPreference = 'Stop'

$key  = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$name = 'TdrInduceHang'

function Show-State {
    if (Test-Path -LiteralPath $key) {
        $v = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
        if ($null -eq $v) {
            Write-Host 'TDR_INDUCE_HANG: DESARMADO (valor ausente)'
        } elseif ($v -eq 1) {
            Write-Host "TDR_INDUCE_HANG: ARMADO ($name = 1) -- a proxima submissao do Node 0 TRAVA de proposito" -ForegroundColor Red
        } else {
            Write-Host "TDR_INDUCE_HANG: DESARMADO/INVALIDO ($name = $v); o driver exige exatamente 1"
        }
    } else {
        Write-Host 'TDR_INDUCE_HANG: chave de servico ausente (driver nao instalado)'
    }
    Write-Host "  chave: $key\$name"
    Write-Host "  o driver so' arma com valor presente E igual a 1, e dispara UMA vez por bind"
    Write-Host 'NAO VALIDADO EM HARDWARE'
}

if ($Arm -and $Disarm) { throw 'Escolha somente -Arm ou -Disarm.' }
if (-not $Arm -and -not $Disarm) {
    Show-State
    Write-Host ''
    Write-Host 'Uso: -Arm -IAcceptDeliberateHangAndPossibleBugCheck   |   -Disarm'
    exit 0
}

$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado: a chave fica em HKLM.'
}

if (-not (Test-Path -LiteralPath $key)) {
    throw ("Chave de servico ausente: $key. O driver precisa estar instalado. " +
        'Nao crie a chave a mao. Para a primeira corrida, construa/instale o pacote ' +
        'de ensaio cujo INF traz TdrInduceHang=1.')
}

if ($Disarm) {
    Set-ItemProperty -LiteralPath $key -Name $name -Value 0 -Type DWord -Force
    Write-Host 'TDR_INDUCE_HANG desarmado (0).' -ForegroundColor Green
    Show-State
    exit 0
}

if (-not $IAcceptDeliberateHangAndPossibleBugCheck) {
    throw ('Armar exige -IAcceptDeliberateHangAndPossibleBugCheck. O driver vai ' +
        'travar a SDMA0 de proposito e NAO notificar completion. Se o TDR nao ' +
        'recuperar, o desfecho e BugCheck/reboot.')
}

Set-ItemProperty -LiteralPath $key -Name $name -Value 1 -Type DWord -Force

Write-Host 'TDR_INDUCE_HANG ARMADO.' -ForegroundColor Red
Write-Warning 'O valor so sera lido no PROXIMO StartDevice; o bind atual nao muda.'
Write-Host ''
Write-Host 'O que ler no log, em ordem:' -ForegroundColor Cyan
Write-Host '  1. "[StartDevice] TdrInduceHang=1 (present=1)"'
Write-Host '  2. "TDR_HANG_INDUZIDO ARMADO NO SILICIO: POLL_REGMEM ..."'
Write-Host '  3. "TDR_ENSAIO: hang induzido ... completion DELIBERADAMENTE nao notificado"'
Write-Host '  4. "[McdmResetFromTimeout] TDR: quiesce fisico do SDMA0 iniciado"'
Write-Host '  5. "TDR_QUIESCE: ... STATUS=... ERR_LOG=..."'
Write-Host '  6. "[SdmaPresent] ENGINE_PROBE: Responsive=0"'
Write-Host '  7. "TDR_RESTART PROVADO" ou "TDR_RESTART FALHOU"'
Write-Host ''
Show-State
