param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureDir,
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
function Convert-HexUInt64([string]$Value) {
    $normalized = $Value.Trim()
    if ($normalized.StartsWith('0x', [System.StringComparison]::OrdinalIgnoreCase)) {
        $normalized = $normalized.Substring(2)
    }
    if ($normalized -eq '') { throw 'valor hexadecimal vazio' }
    return [Convert]::ToUInt64($normalized, 16)
}

$root = (Resolve-Path -LiteralPath $CaptureDir).Path
$manifestPath = Join-Path $root 'manifest.txt'
$discoveryPath = Join-Path $root 'ip-discovery-sysfs.txt'

foreach ($path in @($manifestPath, $discoveryPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "captura incompleta: ausente $path"
    }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw
if ($manifest -notmatch '(?m)^read_only=true$') {
    throw 'captura nao declara read_only=true'
}
if ($manifest -notmatch '(?m)^pci_device=(?<bdf>\S+) vendor=0x1002 device=0x13fe$') {
    throw 'captura nao identifica uma BC-250 1002:13FE'
}
$pciBdf = $Matches['bdf']

$records = @{}
$linePattern = '/ip_discovery/die/(?<die>\d+)/30/(?<instance>\d+)/(?<field>hw_id|base_addr|major|minor|revision|num_instance|harvest|num_base_addresses)=(?<value>.*)$'
foreach ($line in Get-Content -LiteralPath $discoveryPath) {
    $match = [regex]::Match($line, $linePattern)
    if (-not $match.Success) { continue }
    $key = "$($match.Groups['die'].Value):$($match.Groups['instance'].Value)"
    if (-not $records.ContainsKey($key)) { $records[$key] = @{} }
    $records[$key][$match.Groups['field'].Value] =
        $match.Groups['value'].Value.Trim().TrimEnd(',')
}

if ($records.Count -ne 1) {
    throw "OSSSYS HWID 30 deve aparecer exatamente uma vez; encontrados=$($records.Count)"
}
$entry = $records.GetEnumerator() | Select-Object -First 1
$parts = $entry.Key.Split(':')
$fields = $entry.Value
foreach ($required in @('hw_id', 'base_addr', 'major', 'minor', 'revision',
                         'num_instance', 'harvest', 'num_base_addresses')) {
    if (-not $fields.ContainsKey($required)) {
        throw "OSSSYS incompleto: campo ausente $required"
    }
}
if ([int]$fields['hw_id'] -ne 30 -or [int]$parts[1] -ne 0 -or
    [int]$fields['num_instance'] -ne 0) {
    throw 'OSSSYS nao corresponde a HWID 30 instancia zero'
}
if ((Convert-HexUInt64 $fields['harvest']) -ne 0) {
    throw 'OSSSYS aparece harvested na captura'
}
if ([int]$fields['major'] -ne 5) {
    throw "versao OSSSYS inesperada: $($fields['major']).$($fields['minor']).$($fields['revision'])"
}

$bases = @($fields['base_addr'].Split(',') |
    Where-Object { $_ -ne '' } |
    ForEach-Object { Convert-HexUInt64 $_ })
if ($bases.Count -ne [int]$fields['num_base_addresses'] -or $bases.Count -eq 0) {
    throw 'quantidade de bases OSSSYS nao confere'
}

$layoutCandidates = @($bases | ForEach-Object {
    $base = [uint64]$_
    [ordered]@{
        osssys_base_dwords = '0x{0:X}' -f $base
        osssys_base_bytes = '0x{0:X}' -f ($base * 4)
        ih_rb_cntl_bytes = '0x{0:X}' -f (($base + 0x80) * 4)
        ih_rb_base_bytes = '0x{0:X}' -f (($base + 0x81) * 4)
        ih_rb_base_hi_bytes = '0x{0:X}' -f (($base + 0x82) * 4)
        ih_rb_rptr_bytes = '0x{0:X}' -f (($base + 0x83) * 4)
        ih_rb_wptr_bytes = '0x{0:X}' -f (($base + 0x84) * 4)
        ih_doorbell_rptr_bytes = '0x{0:X}' -f (($base + 0x87) * 4)
        ih_cntl_bytes = '0x{0:X}' -f (($base + 0xC0) * 4)
        ih_cntl2_bytes = '0x{0:X}' -f (($base + 0xC1) * 4)
        ih_status_bytes = '0x{0:X}' -f (($base + 0xC2) * 4)
    }
})

$result = [ordered]@{
    schema = 'bc250.ih-provenance-candidate.v1'
    captured_from = $root
    pci_bdf = $pciBdf
    pci_id = '1002:13FE'
    source = 'linux-amdgpu-ip-discovery-sysfs'
    source_sha256 = (Get-FileHash -LiteralPath $discoveryPath -Algorithm SHA256).Hash
    read_only_capture = $true
    osssys = [ordered]@{
        hardware_id = 30
        die = [int]$parts[0]
        instance = [int]$parts[1]
        version = "$($fields['major']).$($fields['minor']).$($fields['revision'])"
        base_addresses = @($bases | ForEach-Object { '0x{0:X}' -f $_ })
        harvested = $false
    }
    ih_vector_layout = [ordered]@{
        version = 4
        dwords = 8
        bytes = 32
    }
    ih_register_layout_candidates = $layoutCandidates
    register_offsets_authorized = $false
    client_source_map_authorized = $false
    hardware_validated = $false
    verdict = 'CANDIDATO SOMENTE; NAO AUTORIZA MMIO NEM HABILITACAO DE IH'
}

$json = $result | ConvertTo-Json -Depth 6
if ($OutputPath -ne '') {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -ne '' -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $OutputPath -Value $json -Encoding utf8
}
$json
