<#
.SYNOPSIS
    Confirma o binding de LABORATORIO e desarma o watchdog de rollback.

.DESCRIPTION
    tools\confirm-bc250mcdm-display.ps1 e' o gate da imagem de PRODUCAO e exige
    SkipHwBringup=1 - o modo display-only. A imagem de laboratorio existe
    justamente para rodar com SkipHwBringup=0, entao aquele gate a rejeita
    sempre, por construcao. Foi o que derrubou o binding das 15:01 em 29/08.

    Este script NAO afrouxa o gate de producao: ele e' um gate proprio, com a
    asserção INVERTIDA no unico ponto em que os dois cenarios divergem, e mais
    rigoroso onde o laboratorio permite ser - registra a evidencia do bring-up
    no proprio arquivo de desarme.

    Todo gate imprime o valor observado antes de falhar, para que uma recusa
    custe leitura e nao outro ciclo de instalacao.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$InstanceId,
    [Parameter(Mandatory = $true)][string]$Token,
    [switch]$IConfirmDisplayIsVisible
)

$ErrorActionPreference = 'Stop'

if (-not $IConfirmDisplayIsVisible) {
    throw 'Confirmacao recusada: falta -IConfirmDisplayIsVisible.'
}
if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw "Confirmacao recusada: diretorio de evidencia ausente: $EvidenceDir"
}
$parsedToken = [guid]::Empty
if (-not [guid]::TryParseExact($Token, 'D', [ref]$parsedToken)) {
    throw 'Confirmacao recusada: token invalido.'
}

$devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -eq $InstanceId })
if ($devices.Count -ne 1) {
    throw "Confirmacao recusada: $($devices.Count) dispositivos casam com o InstanceId."
}
$device = $devices[0]
Write-Host ("  Servico          : {0}" -f $device.Service)
Write-Host ("  ConfigMgrError   : {0}" -f $device.ConfigManagerErrorCode)
if ($device.Service -ne 'bc250mcdm' -or $device.ConfigManagerErrorCode -ne 0) {
    throw 'Confirmacao recusada: BC-250 nao esta em bc250mcdm Codigo 0.'
}

$probeKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$vsyncDeadline = (Get-Date).AddSeconds(20)
do {
    $probe = Get-ItemProperty -LiteralPath $probeKey -ErrorAction Stop
    $vsyncProperty = $probe.PSObject.Properties['DmmVsyncCount']
    $vsyncProved = ($null -ne $vsyncProperty -and [uint32]$vsyncProperty.Value -ge 1)
    if (-not $vsyncProved) { Start-Sleep -Milliseconds 250 }
} until ($vsyncProved -or (Get-Date) -ge $vsyncDeadline)

$get = {
    param($name)
    $p = $probe.PSObject.Properties[$name]
    if ($null -eq $p) { return $null }
    return [uint32]$p.Value
}

Write-Host ("  DmmVsyncCount    : {0}" -f (& $get 'DmmVsyncCount'))
if (-not $vsyncProved) {
    throw 'Confirmacao recusada: VSync real seguido por Present nao foi comprovado.'
}

# Ponto unico de divergencia com o gate de producao, e ele e' invertido de
# proposito: a imagem de laboratorio SO tem valor se o bring-up rodou.
$skip = & $get 'SkipHwBringup'
Write-Host ("  SkipHwBringup    : {0}" -f $skip)
if ($null -eq $skip -or $skip -ne 0) {
    throw 'Confirmacao recusada: SkipHwBringup=0 nao foi comprovado - esta nao e a imagem de laboratorio.'
}

$requiredDisplayProbe = [ordered]@{
    SchemaVersion        = 96
    DmmIsSupportedStatus = 0
    DmmIsSupportedResult = 1
    DmmCommitStatus      = 0
    DmmCommitHasPath     = 1
    DmmCommitSupported   = 1
    DmmPresentEntered    = 1
    DmmPresentStatus     = 0
}
foreach ($entry in $requiredDisplayProbe.GetEnumerator()) {
    $observed = & $get $entry.Key
    Write-Host ("  {0,-16} : {1}  (esperado {2})" -f $entry.Key, $observed, $entry.Value)
    if ($null -eq $observed -or $observed -ne [uint32]$entry.Value) {
        throw "Confirmacao recusada: telemetria $($entry.Key)=$($entry.Value) nao foi comprovada."
    }
}
foreach ($counterName in @('DmmIsSupportedCount','DmmCommitCount','DmmControlInterruptCount')) {
    $observed = & $get $counterName
    Write-Host ("  {0,-16} : {1}" -f $counterName, $observed)
    if ($null -eq $observed -or $observed -lt 1) {
        throw "Confirmacao recusada: contador $counterName nao avancou."
    }
}

$finalPath = Join-Path $EvidenceDir 'watchdog-disarm.json'
$temporaryPath = Join-Path $EvidenceDir ("watchdog-disarm-{0}.tmp" -f [guid]::NewGuid().ToString('N'))
[ordered]@{
    token                        = $Token
    instance_id                  = $InstanceId
    display_visible              = $true
    confirmed_at                 = [datetime]::Now.ToString('o')
    image                        = 'laboratorio'
    skip_hw_bringup              = 0
    schema_version               = 96
    is_supported_count           = & $get 'DmmIsSupportedCount'
    commit_count                 = & $get 'DmmCommitCount'
    control_interrupt_count      = & $get 'DmmControlInterruptCount'
    present_entered              = & $get 'DmmPresentEntered'
    present_status               = & $get 'DmmPresentStatus'
    vsync_count_at_first_present = & $get 'DmmVsyncCount'
    # Evidencia do bring-up, exclusiva do cenario de laboratorio.
    pfp_direct_status            = & $get 'PfpDirectStatus'
    pfp_pc_last                  = & $get 'PfpDirectPcLast'
    pfp_pc_transitions           = & $get 'PfpDirectPcTransitions'
    pfp_pc_samples               = & $get 'PfpDirectPcSamples'
    pfp_me_cntl_before           = & $get 'PfpDirectMeCntlBefore'
    pfp_pc_before                = & $get 'PfpDirectPcBefore'
    ic_inv_unhalt_mask           = & $get 'PfpDirectIcInvUnhaltMask'
    ic_prime_unhalt_mask         = & $get 'PfpDirectIcPrimeUnhaltMask'
    hardware_validated           = $false
} | ConvertTo-Json | Set-Content -LiteralPath $temporaryPath -Encoding UTF8
Move-Item -LiteralPath $temporaryPath -Destination $finalPath

Write-Host ''
Write-Host 'Watchdog desarmado por confirmacao visual explicita.' -ForegroundColor Green
Write-Host 'Display presente NAO valida compute nem aceleracao.' -ForegroundColor Yellow
Write-Host 'NAO VALIDADO EM HARDWARE.' -ForegroundColor Yellow
