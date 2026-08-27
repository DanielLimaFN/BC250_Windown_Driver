param(
    [string]$CaptureDir = 'referencias/linux-gpu-info-fedora-2026-08-24_14-14-07'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $CaptureDir).Path
$required = @(
    'manifest.txt',
    'dmesg-amdgpu-gpu-info.txt',
    'ip-discovery-sysfs.txt',
    'topology-from-dmesg.txt'
)

foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $name))) {
        throw "captura incompleta: ausente $name"
    }
}

$knownCapture = 'linux-gpu-info-fedora-2026-08-24_14-14-07'
$hashesVerified = $false
if ((Split-Path -Leaf $root) -eq $knownCapture) {
    $knownHashes = @{
        'dmesg-amdgpu-gpu-info.txt' = '8EE9BDBCC037987AD6BA68D123DF238CC0D006E112DF9E67E7B194DED4D37A4C'
        'ip-discovery-sysfs.txt' = '09DE20ABAE2E2560D1FB02678F3AE5F9AFA60F1534CA926716F2F35DA1267601'
        'manifest.txt' = '0B94D945E2E941B82233079EA508412BAC373D0CC889F44FBCB5752D9B13E95E'
        'modinfo-amdgpu.txt' = 'F46720A2FD64988B913CF812FC31F83B9CF6704A26D0CA721467C742B6834A32'
        'topology-from-dmesg.txt' = '5847406BD91110FEAFEE0CB4F327FEE7FBE4F4541BF7CCA84EDCEB0272B2CEAC'
    }
    foreach ($entry in $knownHashes.GetEnumerator()) {
        $actual = (Get-FileHash -LiteralPath (Join-Path $root $entry.Key) -Algorithm SHA256).Hash
        if ($actual -ne $entry.Value) {
            throw "SHA-256 divergente em $($entry.Key): $actual"
        }
    }
    $hashesVerified = $true
}

$manifest = Get-Content -LiteralPath (Join-Path $root 'manifest.txt') -Raw
$topologyText = Get-Content -LiteralPath (Join-Path $root 'topology-from-dmesg.txt') -Raw
$discovery = Get-Content -LiteralPath (Join-Path $root 'ip-discovery-sysfs.txt') -Raw
$dmesg = Get-Content -LiteralPath (Join-Path $root 'dmesg-amdgpu-gpu-info.txt') -Raw

if ($manifest -notmatch '(?m)^read_only=true$') { throw 'captura nao declara read_only=true' }
if ($manifest -notmatch '(?m)^pci_device=0000:01:00\.0 vendor=0x1002 device=0x13fe$') {
    throw 'PCI da captura nao e a BC-250 1002:13FE esperada'
}
if ($manifest -notmatch '(?m)^ip_discovery_sysfs=/.+$') {
    throw 'IP Discovery nao foi encontrada na captura'
}

$pattern = 'SE (?<se>\d+), SH per SE (?<sh>\d+), CU per SH (?<cu>\d+), active_cu_number (?<active>\d+)'
$match = [regex]::Match($topologyText, $pattern)
if (-not $match.Success) { throw 'linha canonica de topologia ausente' }

$se = [int]$match.Groups['se'].Value
$sh = [int]$match.Groups['sh'].Value
$cu = [int]$match.Groups['cu'].Value
$active = [int]$match.Groups['active'].Value
$capacity = $se * $sh * $cu

if ($se -ne 2 -or $sh -ne 2 -or $cu -ne 10 -or $active -ne 24) {
    throw "topologia inesperada: SE=$se SH/SE=$sh CU/SH=$cu ativos=$active"
}
if ($active -gt $capacity) { throw 'CUs ativos excedem a capacidade declarada' }

$gcPrefix = '/ip_discovery/die/0/11/0/'
foreach ($expected in @(
    "${gcPrefix}hw_id=11,",
    "${gcPrefix}base_addr=0x00001260,0x0000A000,0x02402C00,",
    "${gcPrefix}major=10,",
    "${gcPrefix}minor=1,",
    "${gcPrefix}revision=3,",
    "${gcPrefix}num_instance=0,"
)) {
    if (-not $discovery.Contains($expected)) {
        throw "entrada GC Discovery ausente: $expected"
    }
}

if ($dmesg -notmatch 'detected ip block number 6 <gfx_v10_0_0>') {
    throw 'GFX v10 nao aparece na inicializacao Linux'
}
if ($dmesg -notmatch 'Initialized amdgpu .* for 0000:01:00\.0') {
    throw 'amdgpu nao concluiu a inicializacao na captura'
}

Write-Output 'AUDITORIA DE TOPOLOGIA LINUX: OK'
if ($hashesVerified) {
    Write-Output '  Integridade: SHA-256 dos 5 arquivos conhecidos conferido.'
} else {
    Write-Output '  Integridade: captura sem conjunto de hashes conhecido; apenas estrutura conferida.'
}
Write-Output "  Capacidade: $se SE x $sh SH/SE x $cu CU/SH = $capacity CUs maximos."
Write-Output "  Ativos: $active; diferenca capacidade-ativos: $($capacity - $active)."
Write-Output '  GC IP 10.1.3, bases 0x1260/0xA000/0x02402C00, instancia IP ordinal 0.'
Write-Output '  ATENCAO: instancia IP ordinal 0 NAO dimensiona GRBM_GFX_INDEX.INSTANCE_INDEX.'
Write-Output '  Evidencia Linux nao valida CP, PM4, compute ou WDDM no driver Windows.'
