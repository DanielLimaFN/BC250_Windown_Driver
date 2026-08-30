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

$paramsKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters'
$paramsObj = Get-ItemProperty -LiteralPath $paramsKey -ErrorAction SilentlyContinue
$skip = & $get 'SkipHwBringup'
if ($null -eq $skip -and $null -ne $paramsObj -and $null -ne $paramsObj.PSObject.Properties['SkipHwBringup']) {
    $skip = [uint32]$paramsObj.SkipHwBringup
}
Write-Host ("  SkipHwBringup    : {0}" -f $skip)
if ($null -eq $skip -or $skip -ne 0) {
    throw 'Confirmacao recusada: SkipHwBringup=0 nao foi comprovado - esta nao e a imagem de laboratorio.'
}
$requiredDisplayProbe = [ordered]@{
    SchemaVersion        = 114
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
    status                       = 'DISARMED'
    timestamp_utc                = [DateTime]::UtcNow.ToString('o')
    disarm_reason                = 'OPERATOR_VISUAL_CONFIRMATION'
    schema_version               = & $get 'SchemaVersion'
    dmm_is_supported_status      = & $get 'DmmIsSupportedStatus'
    dmm_is_supported_result      = & $get 'DmmIsSupportedResult'
    dmm_commit_status            = & $get 'DmmCommitStatus'
    dmm_commit_has_path          = & $get 'DmmCommitHasPath'
    dmm_commit_supported         = & $get 'DmmCommitSupported'
    hqd_active_status            = & $get 'HqdActiveStatus'
    hqd_active_after             = & $get 'HqdActiveAfter'
    hqd_mec_me2_before_active0   = & $get 'HqdActiveMecMe2BeforeActive0'
    hqd_mec_me2_before_active1   = & $get 'HqdActiveMecMe2BeforeActive1'
    hqd_mec_me2_before_active2   = & $get 'HqdActiveMecMe2BeforeActive2'
    hqd_mec_me2_before_active3   = & $get 'HqdActiveMecMe2BeforeActive3'
    hqd_cpc_status_before_kick   = & $get 'HqdActiveCpcStatusBeforeKick'
    hqd_cpf_busy_before_active   = & $get 'HqdActiveCpfBusyStatBeforeActive'
    hqd_cpf_busy_after_active    = & $get 'HqdActiveCpfBusyStatAfterActive'
    hqd_cpf_busy_before_wptr     = & $get 'HqdActiveCpfBusyStatBeforeWptr'
    hqd_cpf_busy_after_wptr      = & $get 'HqdActiveCpfBusyStatAfterWptr'
    hqd_cpf_busy_before_kick     = & $get 'HqdActiveCpfBusyStatBeforeKick'
    hqd_cpf_busy_after_kick      = & $get 'HqdActiveCpfBusyStatAfterKick'
    hqd_cpf_busy_last            = & $get 'HqdActiveCpfBusyStatLast'
    hqd_cpc_stalled_before_kick  = & $get 'HqdActiveCpcStalledStat1BeforeKick'
    hqd_cp_stat_before_kick      = & $get 'HqdActiveCpStatBeforeKick'
    hqd_cp_stalled_before_kick   = & $get 'HqdActiveCpStalledStat1BeforeKick'
    hqd_cpc_status_after_kick    = & $get 'HqdActiveCpcStatusAfterKick'
    hqd_cpc_stalled_after_kick   = & $get 'HqdActiveCpcStalledStat1AfterKick'
    hqd_cp_stat_after_kick       = & $get 'HqdActiveCpStatAfterKick'
    hqd_cp_stalled_after_kick    = & $get 'HqdActiveCpStalledStat1AfterKick'
    shader_ring_dword0           = & $get 'ShaderRingDword0'
    shader_ring_dword1           = & $get 'ShaderRingDword1'
    shader_ring_dword2           = & $get 'ShaderRingDword2'
    shader_ring_dword3           = & $get 'ShaderRingDword3'
    shader_ring_dword4           = & $get 'ShaderRingDword4'
    shader_ring_dword5           = & $get 'ShaderRingDword5'
    shader_ring_dword6           = & $get 'ShaderRingDword6'
    shader_ring_dword7           = & $get 'ShaderRingDword7'
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
    shader_dispatch_enabled      = & $get 'ShaderDispatchEnabled'
    shader_dispatch_dwords       = & $get 'ShaderDispatchDwords'
    pfp_cp_soft_reset             = & $get 'PfpDirectCpSoftReset'
    pfp_vmid0_page_table          = & $get 'PfpDirectVmid0PageTable'
    sdma_ring_cntl_applied        = & $get 'SdmaRingCntlApplied'
    shader_cc_array_pass1        = & $get 'ShaderCcArrayPass1'
    shader_cc_array_pass2        = & $get 'ShaderCcArrayPass2'
    shader_spi_wgp_mask_pass1    = & $get 'ShaderSpiWgpMaskPass1'
    shader_spi_wgp_mask_pass2    = & $get 'ShaderSpiWgpMaskPass2'
    shader_rlc_aon_wgp_pass1     = & $get 'ShaderRlcAonWgpPass1'
    shader_rlc_aon_wgp_pass2     = & $get 'ShaderRlcAonWgpPass2'
    shader_non_harvested_wgp     = & $get 'ShaderNonHarvestedWgpMask'
    shader_aon_non_harvested_wgp = & $get 'ShaderAonNonHarvestedWgpMask'
    shader_wgp_preflight         = & $get 'ShaderWgpPreflightPassed'
    shader_result_before         = & $get 'ShaderResultBefore'
    shader_result_after          = & $get 'ShaderResultAfter'
    shader_result_mismatches     = & $get 'ShaderResultMismatches'
    shader_fence_before          = & $get 'ShaderFenceBefore'
    shader_fence_after           = & $get 'ShaderFenceAfter'
    shader_rptr_after            = & $get 'ShaderRptrAfter'
    shader_wptr_after            = & $get 'ShaderWptrAfter'
    shader_register_restore      = & $get 'ShaderRegisterRestorePassed'
    shader_memory_restore        = & $get 'ShaderMemoryRestorePassed'
    shader_physical_ready        = & $get 'ShaderPhysicalReady'
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
