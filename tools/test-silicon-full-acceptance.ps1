[CmdletBinding()]
param(
    [switch]$RequireLiveEvidence
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$service = Join-Path $root 'driver\wdk\bc250_compute_service.c'
$rules = Join-Path $root 'BC250_HARDWARE_RULES.md'
$errors = [System.Collections.Generic.List[string]]::new()

function Require-Pattern([string]$Text, [string]$Pattern, [string]$Failure) {
    if ($Text -notmatch $Pattern) { $errors.Add($Failure) }
}

function Reject-Pattern([string]$Text, [string]$Pattern, [string]$Failure) {
    if ($Text -match $Pattern) { $errors.Add($Failure) }
}

if (-not (Test-Path -LiteralPath $service)) {
    throw "fonte do servico compute ausente: $service"
}

$source = Get-Content -LiteralPath $service -Raw
$hardwareRules = Get-Content -LiteralPath $rules -Raw

# This is deliberately a source-level gate.  It must pass before a live test is
# allowed to use this service as evidence, otherwise the same IOCTL can fabricate
# a perfectly plausible-looking result without the BC-250 executing anything.
Reject-Pattern $source 'RtlCopyMemory\s*\(\s*g_VramPool' `
    'SDMA COPY usa RtlCopyMemory para g_VramPool (copia por CPU, nao SDMA fisica).'
Reject-Pattern $source 'shaderStaging\s*\[\s*0\s*\]\s*=\s*0xBF810000' `
    'DISPATCH_COMPUTE apenas escreve s_endpgm na RAM do host; nenhum pacote MEC/HQD e submetido.'
Reject-Pattern $source 'dispReq->FenceObserved\s*=\s*dispReq->FenceValue' `
    'DISPATCH_COMPUTE ecoa o fence solicitado; o fence precisa ser lido da memoria escrita pela GPU.'
Reject-Pattern $source 'dispReq->WptrAfter\s*=\s*16' `
    'DISPATCH_COMPUTE publica WPTR constante; e necessario readback MMIO do ring fisico.'
Reject-Pattern $source 'dispReq->RptrAfter\s*=\s*16' `
    'DISPATCH_COMPUTE publica RPTR constante; e necessario polling MMIO ate RPTR == WPTR.'
Reject-Pattern $source 'hwInfo->GpuCuCount\s*=\s*24' `
    'GET_HARDWARE_INFO fixa 24 CUs; a topologia precisa ser detectada no silicio.'
Reject-Pattern $source 'hwInfo->GpuClockMhz\s*=\s*2000' `
    'GET_HARDWARE_INFO fixa o clock; o teste exige telemetria SMU real antes/durante/depois.'
Require-Pattern $source 'Bc250ActiveRunSdmaRingWork|Bc250ComputeRunActiveHqdSubmissionTransaction' `
    'backend fisico ainda nao esta ligado ao servico compute (SDMA/MEC-HQD indisponivel).'
Reject-Pattern $source 'status\s*=\s*STATUS_NOT_SUPPORTED' `
    'servico compute esta fail-closed, mas ainda sem backend fisico conectado.'

Require-Pattern $hardwareRules 'RPTR.*WPTR' 'BC250_HARDWARE_RULES nao exige drenagem RPTR/WPTR.'
Require-Pattern $hardwareRules 'Fence escrito pela.*GPU' 'BC250_HARDWARE_RULES nao exige fence escrita pela GPU.'
Require-Pattern $hardwareRules 'resultado produzido pela GPU' 'BC250_HARDWARE_RULES nao exige comparacao de resultado GPU/CPU.'

if ($RequireLiveEvidence) {
    $probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
    if (-not (Test-Path -LiteralPath $probePath)) {
        $errors.Add('telemetria live do miniport ausente.')
    } else {
        $probe = Get-ItemProperty -LiteralPath $probePath
        foreach ($name in @('HqdActiveTriadPassed', 'HqdActivePhysicalReady',
                             'SdmaRingPhysicalReady', 'SdmaRingMismatches')) {
            if ($null -eq $probe.PSObject.Properties[$name]) {
                $errors.Add("telemetria live ausente: $name")
            }
        }
        if ($null -ne $probe.PSObject.Properties['HqdActiveTriadPassed'] -and
            [uint32]$probe.HqdActiveTriadPassed -ne 1) {
            $errors.Add('MEC/HQD nao apresentou a triade fisica completa.')
        }
        if ($null -ne $probe.PSObject.Properties['HqdActivePhysicalReady'] -and
            [uint32]$probe.HqdActivePhysicalReady -ne 1) {
            $errors.Add('MEC/HQD nao esta PhysicalReady.')
        }
        if ($null -ne $probe.PSObject.Properties['SdmaRingPhysicalReady'] -and
            [uint32]$probe.SdmaRingPhysicalReady -ne 1) {
            $errors.Add('SDMA nao esta PhysicalReady.')
        }
        if ($null -ne $probe.PSObject.Properties['SdmaRingMismatches'] -and
            [uint32]$probe.SdmaRingMismatches -ne 0) {
            $errors.Add('SDMA apresentou divergencia de dados.')
        }
    }
}

Write-Host 'BC250 FULL SILICON ACCEPTANCE GATE'
if ($errors.Count -ne 0) {
    foreach ($message in $errors) { Write-Host "  BLOCKED: $message" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}

Write-Host 'PASS: backend sem caminhos sinteticos e evidencias live minimas presentes.'
Write-Host 'Ainda execute carga real e arquive RPTR/WPTR, fence, dados, clocks, potencia e GFXOFF.'
exit 0
