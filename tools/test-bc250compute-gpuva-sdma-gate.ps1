[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$header = Join-Path $root 'driver\wdk\bc250_compute_service.h'
$service = Join-Path $root 'driver\wdk\bc250_compute_service.c'
$client = Join-Path $root 'tools\bc250-gpuva-sdma-gate.c'
$broker = Join-Path $root 'driver\core\bc250_gpuva_sdma_broker_contract.c'
$brokerTest = Join-Path $root 'driver\tests\gpuva_sdma_broker_contract_test.c'
$executor = Join-Path $root 'driver\core\bc250_gpuva_sdma_executor_contract.c'
$miniport = Join-Path $root 'driver\wdk\bc250_active_miniport.c'
$errors = [System.Collections.Generic.List[string]]::new()

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { $errors.Add($message) }
}
function Reject([string]$text, [string]$pattern, [string]$message) {
    if ($text -match $pattern) { $errors.Add($message) }
}

$headerText = Get-Content -LiteralPath $header -Raw
$serviceText = Get-Content -LiteralPath $service -Raw
$clientText = Get-Content -LiteralPath $client -Raw
$brokerText = Get-Content -LiteralPath $broker -Raw
$brokerTestText = Get-Content -LiteralPath $brokerTest -Raw
$executorText = Get-Content -LiteralPath $executor -Raw
$miniportText = Get-Content -LiteralPath $miniport -Raw

Require $headerText 'IOCTL_BC250_GPUVA_SDMA_TWO_CONTEXT' 'IOCTL GPUVA+SDMA ausente.'
Require $headerText 'BC250_GPUVA_SDMA_TWO_CONTEXT_VERSION' 'ABI versionada ausente.'
Require $headerText 'SourceLeaseId' 'Leases opacos de alocacao ausentes.'
Require $serviceText 'Bc250ValidateGpuvaSdmaTwoContextRequest' 'Preflight transacional ausente.'
Require $serviceText 'Bc250GpuvaSdmaBackendNoAdapterOwner' 'Estado fail-closed do backend ausente.'
Require $serviceText 'STATUS_NOT_SUPPORTED' 'EXECUTE precisa falhar fechado sem backend.'
Require $serviceText 'No MMIO, CPU copy, allocation or GPU submission in dry-run mode' 'Dry-run precisa declarar ausencia de execucao.'
Reject $serviceText 'RtlCopyMemory\s*\(\s*.*GpuvaSdma' 'Copia por CPU encontrada no caminho GPUVA+SDMA.'
Reject $serviceText 'PhysicalFenceObserved\s*=\s*1u' 'Fence fisica foi sintetizada.'
Reject $serviceText 'HardwareValidated\s*=\s*1u' 'Validacao de hardware foi sintetizada.'
Reject $serviceText 'Bc250ActiveRunSdmaRingWork' 'Servico compute reutiliza ring SDMA legado de MC fixo.'
Require $clientText 'BC250_GPUVA_SDMA_FLAG_DRY_RUN' 'Cliente nao inicia pelo dry-run.'
Require $clientText 'HardwareValidated == 1u' 'Cliente precisa recusar falso positivo sem prova fisica.'
Require $brokerText 'Bc250GpuvaSdmaBrokerIssueLease' 'Broker de lease MCDM ausente.'
Require $brokerText 'BC250_MCDM_OBJECT_OPEN_ALLOCATION' 'Lease precisa derivar de OPEN_ALLOCATION.'
Require $brokerText 'RootPageTableSetCount' 'Broker nao exige page table do processo.'
Require $brokerText 'source->OwnerProcess == destination->OwnerProcess' 'Broker nao exige isolamento entre processos.'
Require $brokerTestText 'MAPPING_NOT_READY' 'Teste nao cobre recusa sem page table.'
Require $brokerTestText 'PAIR_NOT_ISOLATED' 'Teste nao cobre rejeicao de par nao isolado.'
Require $executorText 'Bc250GpuvaSdmaExecuteTransaction' 'Executor transacional ausente.'
Require $executorText 'Executor->Rollback' 'Executor nao exige rollback.'
Require $executorText 'Evidence->RptrAfter == Evidence->WptrAfter' 'Executor nao exige drenagem RPTR/WPTR.'
Require $executorText 'Evidence->FenceObserved == Evidence->FenceExpected' 'Executor nao exige fence observada.'
Require $executorText 'Evidence->DataMismatches == 0u' 'Executor nao exige comparacao integral.'
Reject $executorText 'BC250_SDMA_LEGACY_' 'Executor nao pode depender de offsets MC legados.'
Require $miniportText 'Bc250ActiveIssueGpuvaSdmaLease' 'Ponte de lease no miniport ausente.'
Require $miniportText 'Bc250GpuvaSdmaBrokerInitialize' 'Broker nao e inicializado pelo miniport.'
Require $miniportText 'Bc250GpuvaSdmaBrokerRevokeForObject' 'Teardown nao revoga leases do miniport.'
Reject $miniportText 'Bc250ActiveIssueGpuvaSdmaLease\s*\([^\)]*StartDevice' 'Bridge GPUVA nao pode ser disparada no StartDevice.'

Write-Host 'BC250 GPUVA+SDMA TWO-CONTEXT STATIC GATE'
if ($errors.Count -ne 0) {
    $errors | ForEach-Object { Write-Host "  BLOCKED: $_" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}
Write-Host 'PASS: ABI versionada, preflight de dois contextos, broker MCDM e EXECUTE fail-closed presentes.'
Write-Host 'Nenhuma submissao, copia por CPU ou prova de hardware foi fabricada.'
Write-Host 'NAO VALIDADO EM HARDWARE'
