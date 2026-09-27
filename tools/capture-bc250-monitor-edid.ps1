[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('PreBind','Bound','PostRecovery')][string]$Phase,
    [string]$EvidenceDir,
    [switch]$RequireValidBase,
    [switch]$SkipWmi
)

# Read-only Windows monitor evidence. This does not issue a fresh DP AUX transaction.
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $pointer = 'C:\BC250_HANG_ETW_LATEST.txt'
    if (-not (Test-Path -LiteralPath $pointer -PathType Leaf)) {
        throw 'Manifesto ETW ausente; informe -EvidenceDir.'
    }
    $manifest = (Get-Content -LiteralPath $pointer -Raw).Trim()
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Manifesto ETW nao encontrado: $manifest"
    }
    $EvidenceDir = Split-Path -Parent $manifest
}
$EvidenceDir = [System.IO.Path]::GetFullPath($EvidenceDir)
if (-not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw "EvidenceDir inexistente: $EvidenceDir"
}

function Test-EdidBlock([byte[]]$Bytes, [int]$Start) {
    if ($Bytes.Length -lt ($Start + 128)) { return $false }
    $sum = 0
    for ($i = $Start; $i -lt ($Start + 128); $i++) { $sum += [int]$Bytes[$i] }
    return ($sum % 256) -eq 0
}

function Get-Manufacturer([byte[]]$Bytes) {
    $word = (([int]$Bytes[8]) -shl 8) -bor [int]$Bytes[9]
    return -join @(10,5,0 | ForEach-Object {
        $part = ($word -shr $_) -band 31
        if ($part -ge 1 -and $part -le 26) { [char]([int][char]'A' + $part - 1) }
        else { [char]'?' }
    })
}

$result = [ordered]@{
    schema = 1
    phase = $Phase
    captured_at = (Get-Date).ToString('o')
    boot_ticks = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks
    source = $(if ($SkipWmi) {
        'Windows monitor PnP registry (may be cached or overridden)'
    } else {
        'Windows monitor PnP registry and WMI (may be cached or overridden)'
    })
    fresh_dp_aux_read = $false
    wmi_skipped = [bool]$SkipWmi
    monitor_count = 0
    monitors = @()
    errors = @()
    same_base_hash_as_prebind = $null
}

try {
    $activeMonitors = @(Get-PnpDevice -Class Monitor -ErrorAction Stop |
        Where-Object { $_.Status -eq 'OK' })
} catch {
    $activeMonitors = @()
    $result.errors += "Get-PnpDevice: $($_.Exception.Message)"
}
if ($SkipWmi) { $wmiMonitors = @() }
else {
    try {
        $wmiMonitors = @(Get-CimInstance -Namespace 'root\wmi' `
            -ClassName WmiMonitorDescriptorMethods -ErrorAction Stop |
            Where-Object { $_.Active })
    } catch {
        $wmiMonitors = @()
        $result.errors += "WMI enumeration: $($_.Exception.Message)"
    }
}

$index = 0
foreach ($monitor in $activeMonitors) {
    $index++
    $item = [ordered]@{
        instance_id = [string]$monitor.InstanceId
        friendly_name = [string]$monitor.FriendlyName
        parent = $null
        registry_path = $null
        registry_bytes = 0
        registry_sha256 = $null
        header_valid = $false
        base_checksum_valid = $false
        all_available_checksums_valid = $false
        manufacturer = $null
        product_code = $null
        serial_number = $null
        extensions_declared = $null
        extensions_available = 0
        pixel_clock_khz_dtd1 = $null
        h_active_dtd1 = $null
        v_active_dtd1 = $null
        edid_override_present = $false
        wmi_blocks = @()
        wmi_base_matches_registry = $null
        errors = @()
    }
    try {
        $parentProperty = Get-PnpDeviceProperty -InstanceId $monitor.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent' -ErrorAction Stop
        $item.parent = [string]$parentProperty.Data
    } catch { $item.errors += "parent: $($_.Exception.Message)" }

    $regPath = 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\' +
        $monitor.InstanceId + '\Device Parameters'
    $item.registry_path = $regPath
    $item.edid_override_present = Test-Path -LiteralPath ($regPath + '\EDID_OVERRIDE')
    $edid = $null
    try {
        $edid = [byte[]](Get-ItemProperty -LiteralPath $regPath -Name EDID -ErrorAction Stop).EDID
        $item.registry_bytes = $edid.Length
        if ($edid.Length -ge 128) {
            $rawPath = Join-Path $EvidenceDir ("edid-{0}-monitor-{1:D2}-registry.bin" -f $Phase,$index)
            [System.IO.File]::WriteAllBytes($rawPath, $edid)
            $item.registry_sha256 = (Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash
            $item.header_valid = [Convert]::ToBase64String($edid[0..7]) -eq
                [Convert]::ToBase64String([byte[]](0,255,255,255,255,255,255,0))
            $item.base_checksum_valid = Test-EdidBlock $edid 0
            $item.extensions_declared = [int]$edid[126]
            $item.extensions_available = [Math]::Floor($edid.Length / 128) - 1
            $valid = $true
            for ($offset = 0; $offset + 128 -le $edid.Length; $offset += 128) {
                if (-not (Test-EdidBlock $edid $offset)) { $valid = $false }
            }
            $item.all_available_checksums_valid = $valid
            $item.manufacturer = Get-Manufacturer $edid
            $item.product_code = '0x{0:X4}' -f ((([int]$edid[11]) -shl 8) -bor [int]$edid[10])
            $item.serial_number = [BitConverter]::ToUInt32($edid, 12)
            $item.pixel_clock_khz_dtd1 = 10 * [BitConverter]::ToUInt16($edid, 54)
            $item.h_active_dtd1 = [int]$edid[56] -bor (([int]$edid[58] -band 0xF0) -shl 4)
            $item.v_active_dtd1 = [int]$edid[59] -bor (([int]$edid[61] -band 0xF0) -shl 4)
        }
    } catch { $item.errors += "registry EDID: $($_.Exception.Message)" }

    $wmi = $wmiMonitors | Where-Object {
        $_.InstanceName.StartsWith($monitor.InstanceId + '_',
            [StringComparison]::OrdinalIgnoreCase)
    } | Select-Object -First 1
    if ($null -ne $wmi) {
        $blockCount = if ($null -ne $item.extensions_declared) {
            [Math]::Min(1 + [int]$item.extensions_declared, 4)
        } else { 1 }
        for ($block = 0; $block -lt $blockCount; $block++) {
            try {
                $read = Invoke-CimMethod -InputObject $wmi `
                    -MethodName WmiGetMonitorRawEEdidV1Block `
                    -Arguments @{ BlockId = [byte]$block } -ErrorAction Stop
                $bytes = [byte[]]$read.BlockContent
                $blockResult = [ordered]@{
                    block = $block
                    returned = [bool]$read.ReturnValue
                    type = [int]$read.BlockType
                    bytes = $bytes.Length
                    checksum_valid = $(if ($bytes.Length -eq 128) { Test-EdidBlock $bytes 0 } else { $false })
                }
                if ($bytes.Length -eq 128) {
                    $wmiPath = Join-Path $EvidenceDir ("edid-{0}-monitor-{1:D2}-wmi-block-{2}.bin" -f $Phase,$index,$block)
                    [System.IO.File]::WriteAllBytes($wmiPath, $bytes)
                    $blockResult.sha256 = (Get-FileHash -LiteralPath $wmiPath -Algorithm SHA256).Hash
                    if ($block -eq 0 -and $null -ne $edid -and $edid.Length -ge 128) {
                        $item.wmi_base_matches_registry =
                            [Convert]::ToBase64String($bytes) -eq
                            [Convert]::ToBase64String($edid[0..127])
                    }
                }
                $item.wmi_blocks += $blockResult
            } catch {
                $item.wmi_blocks += [ordered]@{ block = $block; error = $_.Exception.Message }
            }
        }
    } elseif (-not $SkipWmi) { $item.errors += 'WMI active instance not found' }
    $result.monitors += $item
}
$result.monitor_count = $result.monitors.Count
if ($Phase -eq 'PostRecovery') {
    $baselinePath = Join-Path $EvidenceDir 'edid-PreBind.json'
    if (Test-Path -LiteralPath $baselinePath -PathType Leaf) {
        $baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
        $before = @($baseline.monitors | ForEach-Object registry_sha256 | Where-Object { $_ })
        $after = @($result.monitors | ForEach-Object registry_sha256 | Where-Object { $_ })
        $result.same_base_hash_as_prebind = @($after | Where-Object { $before -contains $_ }).Count -gt 0
    }
}
$output = Join-Path $EvidenceDir ("edid-{0}.json" -f $Phase)
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $output -Encoding UTF8
$validCount = @($result.monitors | Where-Object {
    $_.header_valid -and $_.base_checksum_valid -and
    $_.parent -like 'PCI\VEN_1002&DEV_13FE*'
}).Count
Write-Host ("EDID {0}: monitores ativos={1}; bases validas da BC-250={2}; evidencia={3}" -f
    $Phase,$result.monitor_count,$validCount,$output)
Write-Host 'Origem: Windows PnP/registro (WMI se habilitado), possivelmente cache; nenhuma transacao AUX nova foi feita.'
if ($RequireValidBase -and $validCount -eq 0) { exit 2 }
