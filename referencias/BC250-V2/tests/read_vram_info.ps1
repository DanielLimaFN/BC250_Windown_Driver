$serviceParams = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250v2\Parameters' -ErrorAction SilentlyContinue
Write-Host "=== Parameters Registry ==="
$serviceParams | Format-List *

$driverClass = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000' -ErrorAction SilentlyContinue
Write-Host "=== Class 0000 Registry ==="
$driverClass | Format-List *

$os = Get-CimInstance Win32_OperatingSystem
$cs = Get-CimInstance Win32_ComputerSystem
$installedTotal = 17179869184 # 16 GB
$totalPhysical = $cs.TotalPhysicalMemory
$carveoutApprox = $installedTotal - $totalPhysical
Write-Host "=== Memory Calculation ==="
Write-Host "Total Installed GDDR6: 16384 MB (16.00 GB)"
Write-Host "Total OS Physical: $($totalPhysical / 1MB) MB ($($totalPhysical / 1GB) GB)"
Write-Host "Visible to OS: $($os.TotalVisibleMemorySize / 1KB) MB"
Write-Host "Estimated Hardware Carveout/Reserved: $($carveoutApprox / 1MB) MB ($($carveoutApprox / 1GB) GB)"
