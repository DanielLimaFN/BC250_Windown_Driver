$boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$marker = Get-Content 'C:\BC250_REQUIRE_CLEAN_BOOT.json' | ConvertFrom-Json
[PSCustomObject]@{
    BootTime = $boot
    BootTicks = $boot.ToUniversalTime().Ticks
    MarkerTicks = $marker.rollback_boot_ticks
    RebootVerified = ($boot.ToUniversalTime().Ticks -ne $marker.rollback_boot_ticks)
} | Format-List
