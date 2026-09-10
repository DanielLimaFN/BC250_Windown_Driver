# =============================================================================
# B3 - SdmaPlanoAFallback: armar / desarmar / inspecionar
#
# POLARIDADE:
#   0 ou ausente = somente INDIRECT (default fail-closed).
#   1            = restaura a precedencia dos caminhos PLANO A, que
#                  reimplementam a operacao no anel e NAO leem o DMA buffer.
#
# Use 1 somente para bisseccao. Em 16:19, os PLANO A absorveram 14/14
# submissoes e impediram qualquer medicao do INDIRECT.
#
# ORDEM CORRETA:
#
# A chave HKLM\...\Services\bc250mcdm\Parameters e' criada pelo AddReg do INF
# durante a instalacao, e o driver le o tunable no StartDevice. Portanto:
#
#   - antes da primeira instalacao a chave nao existe;
#   - alterar o valor depois do bind nao muda o bind atual; so vale no proximo
#     StartDevice (novo bind, desabilitar/reabilitar ou reboot);
#   - para uma corrida unica, use um pacote cujo INF ja declare o valor.
#
# Este script serve para desarmar sem reinstalar e para armar quando um NOVO
# StartDevice vai acontecer de qualquer forma. Nao crie a chave a mao.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm,
    [switch]$IAcceptHiddenFallbackRisk
)

$ErrorActionPreference = 'Stop'

$key  = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$name = 'SdmaPlanoAFallback'

function Show-State {
    if (Test-Path -LiteralPath $key) {
        $v = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
        if ($null -eq $v) {
            Write-Host 'PLANO_A_FALLBACK: DESARMADO (valor ausente) -- somente INDIRECT'
        } elseif ($v -eq 1) {
            Write-Host "PLANO_A_FALLBACK: ARMADO ($name = 1) -- PLANO A com precedencia" -ForegroundColor Yellow
        } else {
            Write-Host "PLANO_A_FALLBACK: DESARMADO/INVALIDO ($name = $v); o driver exige exatamente 1"
        }
    } else {
        Write-Host 'PLANO_A_FALLBACK: chave de servico ausente (driver nao instalado)'
    }
    Write-Host "  chave: $key\$name"
    Write-Host "  o driver so' liga o fallback com valor presente E igual a 1"
    Write-Host "  no log: '[StartDevice] SdmaPlanoAFallback=' e 'CAMINHO_ESCOLHIDO:'"
    Write-Host 'NAO VALIDADO EM HARDWARE'
}

if ($Arm -and $Disarm) { throw 'Escolha somente -Arm ou -Disarm.' }
if (-not $Arm -and -not $Disarm) {
    Show-State
    Write-Host ''
    Write-Host 'Uso: -Arm -IAcceptHiddenFallbackRisk   |   -Disarm'
    exit 0
}

$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em PowerShell elevado: a chave fica em HKLM.'
}

if (-not (Test-Path -LiteralPath $key)) {
    throw ("Chave de servico ausente: $key. O driver precisa estar instalado. " +
        'Nao crie a chave a mao: o valor tem de acompanhar o pacote ou ser ' +
        'alterado antes de um NOVO StartDevice.')
}

if ($Disarm) {
    Set-ItemProperty -LiteralPath $key -Name $name -Value 0 -Type DWord -Force
    Write-Host 'PLANO_A_FALLBACK desarmado (0): somente INDIRECT.' -ForegroundColor Green
    Show-State
    exit 0
}

if (-not $IAcceptHiddenFallbackRisk) {
    throw ('Armar exige -IAcceptHiddenFallbackRisk. Os PLANO A absorvem as ' +
        'submissoes, o INDIRECT deixa de ser exercitado e o log nao prova a ' +
        'leitura do DMA buffer do dxgkrnl.')
}

Set-ItemProperty -LiteralPath $key -Name $name -Value 1 -Type DWord -Force

Write-Host 'PLANO_A_FALLBACK ARMADO - comportamento pre-B3 restaurado.' -ForegroundColor Yellow
Write-Warning 'O valor so sera lido no PROXIMO StartDevice; o bind atual nao muda.'
Write-Host ''
Write-Host 'O que ler no log depois da corrida:' -ForegroundColor Cyan
Write-Host '  1. "[StartDevice] SdmaPlanoAFallback=1 (present=1)"'
Write-Host '  2. "CAMINHO_ESCOLHIDO: PLANO_A_DIRECT_FILL / PLANO_A_SYNC_NOP"'
Write-Host '  3. "PLANO A CONCLUIDO (...)" nao prova execucao do IB.'
Write-Host ''
Show-State
