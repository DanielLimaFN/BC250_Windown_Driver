[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'driver\wdk\package\output_active.manifest.json'
$markerPath = 'C:\BC250_TEST_OEMINF.txt'
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$errors = [System.Collections.Generic.List[string]]::new()

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\audit-bc250mcdm-package.ps1')
if ($LASTEXITCODE -ne 0) { $errors.Add('pacote local falhou na auditoria de integridade') }

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$markerLines = @(Get-Content -LiteralPath $markerPath -ErrorAction SilentlyContinue)
if ($markerLines.Count -ne 1 -or $markerLines[0] -notmatch '^oem\d+\.inf$') {
    $errors.Add('marcador OEM ausente ou invalido')
    $publishedName = $null
} else { $publishedName = $markerLines[0].ToLowerInvariant() }

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like 'PCI\VEN_1002&DEV_13FE*'
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -ne 43) {
    $errors.Add('resultado PnP nao e bc250mcdm Codigo 43 fail-closed')
}

$service = Get-Service bc250mcdm -ErrorAction SilentlyContinue
if ($null -eq $service -or $service.Status -ne 'Stopped') {
    $errors.Add('servico bc250mcdm nao existe parado apos falha de StartDevice')
}
if (-not (Test-Path -LiteralPath $driverPath)) {
    $errors.Add('bc250mcdm.sys ativo ausente')
} elseif ((Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash -ne
    $manifest.PackageSysSha256) {
    $errors.Add('bc250mcdm.sys ativo diverge do pacote auditado')
}

if (-not (Test-Path -LiteralPath $probePath)) {
    $errors.Add('telemetria Parameters\Probe ausente')
    $probe = $null
} else {
    $probe = Get-ItemProperty -LiteralPath $probePath
    foreach ($expected in @(
        @{ Name='SchemaVersion'; Value=24 },
        @{ Name='DriverEntryReached'; Value=1 },
        @{ Name='InitializationDataReady'; Value=1 },
        @{ Name='AddDeviceEntered'; Value=1 },
        @{ Name='AddDeviceStatus'; Value=0 },
        @{ Name='StartDeviceEntered'; Value=1 },
        @{ Name='InterfaceAccepted'; Value=1 },
        @{ Name='DeviceInfoStatus'; Value=0 },
        @{ Name='PciStatus'; Value=0 },
        @{ Name='PciBytesRead'; Value=64 },
        @{ Name='BarCount'; Value=3 },
        @{ Name='ResolvedBarCount'; Value=3 },
        @{ Name='MmioStatus'; Value=0 },
        @{ Name='TopologyStatus'; Value=0 },
        @{ Name='TopologyStablePasses'; Value=2 },
        @{ Name='DiscoveryTmrStatus'; Value=5 },
        @{ Name='DiscoveryTmrBytes'; Value=0 },
        @{ Name='RuntimeMissing'; Value=1 },
        @{ Name='AllocationSegmentSet'; Value=1 },
        @{ Name='AllocationMaxLow32'; Value=252911616 },
        @{ Name='GpuVaDescriptorReady'; Value=0 },
        @{ Name='RootPageTableAbiReady'; Value=0 },
        @{ Name='RootPageTableMaxEntries'; Value=0 },
        @{ Name='PagingCpuUpdateReady'; Value=0 },
        @{ Name='TlbFlushPhysicalReady'; Value=1 },
        @{ Name='TlbTransactionStatus'; Value=0 },
        @{ Name='TlbVmid'; Value=8 },
        @{ Name='TlbEngineId'; Value=17 },
        @{ Name='TlbRequestValue'; Value=0x00F80100 },
        @{ Name='TlbRangeLowProgrammed'; Value=[uint32]::MaxValue },
        @{ Name='TlbRangeHighProgrammed'; Value=0x0000001F },
        @{ Name='TlbRequestWritten'; Value=1 },
        @{ Name='TlbDummyReadCompleted'; Value=1 },
        @{ Name='TlbAckObserved'; Value=1 },
        @{ Name='TlbRangeRestorePassed'; Value=1 },
        @{ Name='GcvmBackingStatus'; Value=0 },
        @{ Name='GcvmBackingOwned'; Value=1 },
        @{ Name='GcvmBackingReadback'; Value=1 },
        @{ Name='GcvmBackingRestore'; Value=1 },
        @{ Name='GcvmVmid8Status'; Value=0 },
        @{ Name='GcvmVmid8Readback'; Value=1 },
        @{ Name='GcvmVmid8TlbPassed'; Value=1 },
        @{ Name='GcvmVmid8Restore'; Value=1 },
        @{ Name='GcvmInertPhysicalReady'; Value=1 }
    )) {
        $property = $probe.PSObject.Properties[$expected.Name]
        if ($null -eq $property -or [uint32]$property.Value -ne [uint32]$expected.Value) {
            $errors.Add("telemetria inesperada: $($expected.Name)")
        }
    }
    $ackAfter = $probe.PSObject.Properties['TlbAckAfter']
    if ($null -eq $ackAfter -or
        [uint32]$ackAfter.Value -eq [uint32]::MaxValue -or
        ([uint32]$ackAfter.Value -band [uint32]0x100) -eq 0) {
        $errors.Add('ACK TLB fisico ausente, all-ones ou sem o bit do VMID 8')
    }
    $pollCount = $probe.PSObject.Properties['TlbPollCount']
    if ($null -eq $pollCount -or [uint32]$pollCount.Value -eq 0 -or
        [uint32]$pollCount.Value -gt 1000) {
        $errors.Add('contagem de polls TLB fora de 1..1000')
    }
    if (([uint32]$probe.GcvmVmid8CntlBefore -band 1) -ne 0) {
        $errors.Add('VMID8 estava habilitado durante a transacao que deveria ser inerte')
    }
    $pte = ([uint64][uint32]$probe.GcvmFirstPteHigh32 -shl 32) -bor
        [uint64][uint32]$probe.GcvmFirstPteLow32
    $expectedTarget = ([uint64][uint32]$probe.FbOffset16M -shl 24) +
        [uint64][uint32]$probe.GcvmTargetOffsetLow32
    if (($pte -band [uint64]0xFF) -ne [uint64]0x61 -or
        ($pte -band [uint64]0x0000FFFFFFFFF000) -ne $expectedTarget) {
        $errors.Add('PTE GCVM nao corresponde ao buffer owned e flags VALID|READ|WRITE')
    }
}

Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT'
Write-Host "  published INF: $publishedName"
if ($devices.Count -eq 1) {
    Write-Host "  device service/code: $($devices[0].Service) / $($devices[0].ConfigManagerErrorCode)"
}
Write-Host "  driver hash matches package: $($errors -notcontains 'bc250mcdm.sys ativo diverge do pacote auditado')"
if ($null -ne $probe) {
    Write-Host "  lifecycle: DriverEntry=$($probe.DriverEntryReached) Add=$($probe.AddDeviceEntered) Start=$($probe.StartDeviceEntered) Stop=$($probe.StopDeviceEntered) Remove=$($probe.RemoveDeviceEntered)"
    Write-Host "  PCI/BAR/topology: $($probe.PciBytesRead) bytes / $($probe.ResolvedBarCount) BARs / status $($probe.TopologyStatus)"
    Write-Host "  VRAM/visible/ReBAR MiB: $($probe.VramTotalMiB) / $($probe.CpuVisibleMiB) / $($probe.RebarAssignedMiB)"
    Write-Host "  discovery TMR: status=$($probe.DiscoveryTmrStatus) bytes=$($probe.DiscoveryTmrBytes) (5=NOT_REPORTED)"
    Write-Host "  runtime missing: $($probe.RuntimeMissing)"
    Write-Host "  TLB: status=$($probe.TlbTransactionStatus) vmid=$($probe.TlbVmid) engine=$($probe.TlbEngineId) ackBefore=$('{0:X8}' -f [uint32]$probe.TlbAckBefore) ackAfter=$('{0:X8}' -f [uint32]$probe.TlbAckAfter) polls=$($probe.TlbPollCount) restore=$($probe.TlbRangeRestorePassed)"
    Write-Host "  GCVM inert: backing=$($probe.GcvmBackingStatus) vmid8=$($probe.GcvmVmid8Status) pt=$('{0:X8}{1:X8}' -f [uint32]$probe.GcvmPtPhysicalHigh32,[uint32]$probe.GcvmPtPhysicalLow32) readback=$($probe.GcvmBackingReadback)/$($probe.GcvmVmid8Readback) restore=$($probe.GcvmBackingRestore)/$($probe.GcvmVmid8Restore)"
}

if ($errors.Count -ne 0) {
    Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT: BLOCKED'
    foreach ($errorMessage in $errors) { Write-Host "  - $errorMessage" }
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 1
}
Write-Host 'BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS'
Write-Host 'PM4/COMPUTE NAO EXECUTADOS; NAO VALIDADO EM HARDWARE'
exit 0
