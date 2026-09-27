# =============================================================================
# C3.9 - Node1JournalGatePerProcess: armar / desarmar / inspecionar
#
# O QUE MUDA:
#   0 ou ausente = o gate do Node 1 fecha se QUALQUER slot do journal do
#                  adaptador estiver pendente (default, fail-closed).
#   1            = o gate fecha apenas se o PROCESSO QUE ESTA FLUSHANDO tiver
#                  pendencia propria.
#
# POR QUE EXISTE, medido na corrida 12/09 13:29:
#   Com D3D11LabPromotion armado, o dwm.exe sobreviveu 3 min 13 s -- primeira
#   vez, contra 7 crashes em 18 s sem ele. Mas o DWM pagina sem parar, o journal
#   nunca drenou, o gate ficou em BlockMask=0x1 (JOURNAL) e a submissao de
#   compute do RADV foi RECUSADA. O shader nao executou e o byte proof leu
#   poison 0x43DAFFFE. Compute e desktop nao coexistiam por GRANULARIDADE do
#   gate, nao por limite do hardware.
#
# RISCO DECLARADO, NAO VALIDADO EM HARDWARE:
#   O argumento a favor e' que as raizes de page table sao POR PROCESSO, entao a
#   pendencia de outro processo nao deveria afetar a traducao deste. O argumento
#   NAO e' prova. Se estiver errado, uma submissao pode executar sobre page
#   tables em transito. Por isso o default e' 0.
#
# ORDEM CORRETA:
#   A chave HKLM\...\Services\bc250mcdm\Parameters nasce do AddReg do INF, e o
#   KMD le o valor UMA VEZ, no StartDevice. Armar depois do bind so' vale no
#   proximo start. O INF declara o campo com NOCLOBBER, entao o valor armado
#   ANTES do bind sobrevive a instalacao.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Arm,
    [switch]$Disarm
)

$ErrorActionPreference = 'Stop'
$key  = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$name = 'Node1JournalGatePerProcess'

function Show-State {
    if (-not (Test-Path -LiteralPath $key)) {
        Write-Host "  chave ausente: $key"
        Write-Host "  (nasce do AddReg do INF; instale o pacote antes de armar)"
        return
    }
    $v = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
    if ($null -eq $v) {
        Write-Host "  $name : AUSENTE -> gate GLOBAL (default fail-closed)"
    } elseif ([int]$v -eq 1) {
        Write-Host "  $name : 1 -> gate POR PROCESSO (relaxado, NAO VALIDADO EM HARDWARE)"
    } else {
        Write-Host "  $name : $v -> gate GLOBAL (default fail-closed)"
    }
}

Write-Host '=== C3.9 Node1JournalGatePerProcess ==='

if ($Arm -and $Disarm) {
    throw 'Escolha -Arm ou -Disarm, nao os dois.'
}

if (-not $Arm -and -not $Disarm) {
    Write-Host 'Estado atual:'
    Show-State
    Write-Host ''
    Write-Host 'Para armar  : -Arm     (gate por processo; relaxamento nao validado)'
    Write-Host 'Para desarmar: -Disarm (volta ao bloqueio global)'
    exit 0
}

if (-not (Test-Path -LiteralPath $key)) {
    throw "Chave ausente: $key. Instale o pacote antes (o AddReg do INF a cria)."
}

$value = if ($Arm) { 1 } else { 0 }
Set-ItemProperty -LiteralPath $key -Name $name -Value $value -Type DWord -Force
$readback = (Get-ItemPropertyValue -LiteralPath $key -Name $name)
if ([int]$readback -ne $value) {
    throw "Readback divergiu: esperado $value, lido $readback"
}

Write-Host "Gravado e conferido por readback: $name = $readback"
Show-State
Write-Host ''
Write-Host 'O KMD le este valor no StartDevice. Rebind (ou boot frio + bind) para valer.'
Write-Host 'NAO VALIDADO EM HARDWARE.'
