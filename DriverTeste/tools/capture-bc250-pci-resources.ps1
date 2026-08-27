param(
    [switch]$Execute,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$root = Split-Path -Parent $PSScriptRoot
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)

if (-not $Execute) {
    throw 'Captura bloqueada por padrao. Use -Execute; ela consulta somente PnP/CIM.'
}
if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }

$devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
    Where-Object { $_.InstanceId -like 'PCI\VEN_1002&DEV_13FE*' })
if ($devices.Count -ne 1) {
    throw "Esperada exatamente uma BC-250 presente; encontradas: $($devices.Count)."
}
$device = $devices[0]
$escapedId = $device.InstanceId.Replace('\', '\\').Replace("'", "''")
$pnp = Get-CimInstance Win32_PnPEntity -Filter "DeviceID='$escapedId'" -ErrorAction Stop
if ($null -eq $pnp) { throw 'Win32_PnPEntity da BC-250 nao encontrado.' }

$allocated = @($pnp | Get-CimAssociatedInstance -Association Win32_PNPAllocatedResource -ErrorAction Stop)
$memoryRaw = @($allocated | Where-Object {
    $_.CimClass.CimClassName -eq 'Win32_DeviceMemoryAddress'
} | ForEach-Object {
    $start = [uint64]$_.StartingAddress
    $end = [uint64]$_.EndingAddress
    [pscustomobject]@{
        start = $start
        end = $end
        length = if ($end -ge $start) { $end - $start + 1 } else { [uint64]0 }
        name = [string]$_.Name
        memory_type = [string]$_.MemoryType
    }
} | Sort-Object start, length)

if ($memoryRaw.Count -eq 0) { throw 'Nenhum recurso de memoria associado a BC-250.' }

# O provedor CIM pode publicar um descritor agregado e, simultaneamente, um
# filho contido nele. Decompor pelas fronteiras produz janelas fisicas
# disjuntas sem tentar adivinhar numeros de BAR.
$boundaries = [System.Collections.Generic.SortedSet[uint64]]::new()
foreach ($m in $memoryRaw) {
    [void]$boundaries.Add([uint64]$m.start)
    if ($m.end -ne [uint64]::MaxValue) { [void]$boundaries.Add([uint64]$m.end + 1) }
}
$points = @($boundaries)
$segments = [System.Collections.Generic.List[object]]::new()
for ($i = 0; $i + 1 -lt $points.Count; $i++) {
    $start = [uint64]$points[$i]
    $endExclusive = [uint64]$points[$i + 1]
    if ($endExclusive -le $start) { continue }
    $cover = @($memoryRaw | Where-Object { $_.start -le $start -and $_.end -ge ($endExclusive - 1) })
    if ($cover.Count -eq 0) { continue }
    $segments.Add([pscustomobject]@{
        start = ('0x{0:X16}' -f $start)
        end = ('0x{0:X16}' -f ($endExclusive - 1))
        length = [uint64]($endExclusive - $start)
        length_hex = ('0x{0:X}' -f ($endExclusive - $start))
        descriptor_depth = $cover.Count
    })
}

$rangesValid = @($memoryRaw | Where-Object { $_.length -eq 0 }).Count -eq 0
$segmentBytes = @($segments | ForEach-Object { [uint64]$_.length })
$largestSegment = if ($segmentBytes.Count) { [uint64](($segmentBytes | Measure-Object -Maximum).Maximum) } else { [uint64]0 }
$linuxVramBytes = [uint64]0x20000000
$linuxTmrOffset = [uint64]0x1F800000
$linuxTmrBytes = [uint64]0x00400000

$checks = [ordered]@{
    exactly_one_bc250 = ($devices.Count -eq 1)
    device_status_ok = ($device.Status -eq 'OK')
    memory_resources_present = ($memoryRaw.Count -gt 0)
    ranges_valid = $rangesValid
    disjoint_segments_present = ($segments.Count -gt 0)
    no_single_cpu_window_covers_linux_vram = ($largestSegment -lt $linuxVramBytes)
    no_single_cpu_window_reaches_linux_tmr_end = ($largestSegment -lt ($linuxTmrOffset + $linuxTmrBytes))
}

$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-pci-resources"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Readonly inventory of Windows-assigned BC-250 PCI resources'
    verdict = $verdict
    writes_authorized = $false
    writes_reported = 0
    source = 'Get-PnpDevice + Win32_PnPEntity/Win32_PNPAllocatedResource CIM associations'
    device = [pscustomobject]@{
        instance_id = $device.InstanceId
        friendly_name = $device.FriendlyName
        service = $pnp.Service
        status = $device.Status
        bus_number = (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_BusNumber' -ErrorAction SilentlyContinue).Data
        address = (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_Address' -ErrorAction SilentlyContinue).Data
    }
    checks = [pscustomobject]$checks
    memory_descriptors = @($memoryRaw | ForEach-Object {
        [pscustomobject]@{
            start = ('0x{0:X16}' -f $_.start)
            end = ('0x{0:X16}' -f $_.end)
            length = $_.length
            length_hex = ('0x{0:X}' -f $_.length)
            name = $_.name
            memory_type = $_.memory_type
        }
    })
    disjoint_segments = @($segments)
    largest_disjoint_segment = $largestSegment
    linux_comparison = [pscustomobject]@{
        vram_bytes = $linuxVramBytes
        tmr_offset = $linuxTmrOffset
        tmr_bytes = $linuxTmrBytes
        note = 'Comparison only; Linux addresses do not authorize Windows writes.'
    }
    not_proved = @(
        'CIM descriptors are allocations, not ownership granted to bc250dbg',
        'Descriptor-to-BAR numbering is not inferred',
        'No VRAM range is reserved by this capture',
        'No MMIO, PCI configuration, PSP command or driver IOCTL was issued'
    )
}
$jsonPath = Join-Path $dir 'result.json'
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("BC-250: $($device.InstanceId)")
$lines.Add("Nome: $($device.FriendlyName); servico: $($pnp.Service); status: $($device.Status)")
$lines.Add('Descritores de memoria atribuidos pelo Windows:')
foreach ($m in $record.memory_descriptors) {
    $lines.Add(('  {0}..{1}  {2,12} bytes ({3})' -f $m.start, $m.end, $m.length, $m.length_hex))
}
$lines.Add('Segmentos disjuntos derivados somente das fronteiras CIM:')
foreach ($s in $segments) {
    $lines.Add(('  {0}..{1}  {2,12} bytes ({3}) depth={4}' -f $s.start, $s.end, $s.length, $s.length_hex, $s.descriptor_depth))
}
$lines.Add(('Maior segmento disjunto: {0} bytes (0x{0:X})' -f $largestSegment))
$lines.Add('Zero escrita autorizada; zero escrita reportada.')
$lines.Add("Veredito: $verdict")
$txtPath = Join-Path $dir 'result.txt'
$lines | Set-Content -LiteralPath $txtPath -Encoding UTF8

Write-Host "Evidencia: $dir"
foreach ($line in $lines) { Write-Host $line }
foreach ($check in $checks.GetEnumerator()) {
    Write-Host ('  [{0}] {1}' -f $(if ($check.Value) { 'ok  ' } else { 'FALHA' }), $check.Key)
}
if (@($checks.Values | Where-Object { -not $_ }).Count) { exit 1 }
