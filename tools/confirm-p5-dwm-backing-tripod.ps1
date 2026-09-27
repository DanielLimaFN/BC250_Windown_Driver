[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [UInt64]$ExpectedSourceOffset = 0xC10000,
    [switch]$IConfirmDesktopVisibleForTenMinutes
)

$ErrorActionPreference = 'Stop'
if (-not $IConfirmDesktopVisibleForTenMinutes) {
    throw 'P5 recusada: falta confirmacao visual explicita de dez minutos.'
}
$resolved = (Resolve-Path -LiteralPath $EvidenceDir -ErrorAction Stop).Path
$logPath = Join-Path $resolved 'bc250_display_log.txt'
$disarmPath = Join-Path $resolved 'watchdog-disarm.json'
if (-not (Test-Path -LiteralPath $logPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $disarmPath -PathType Leaf)) {
    throw 'P5 recusada: log do driver ou confirmacao do watchdog ausente.'
}
if (((Get-Date) - (Get-Item -LiteralPath $resolved).CreationTime).TotalMinutes -lt 10) {
    throw 'P5 recusada: janela observada ainda nao completou dez minutos.'
}

$log = Get-Content -LiteralPath $logPath -Raw
$sourceHex = ('0x{0:X}' -f $ExpectedSourceOffset)
$required = @(
    '\[FlushMappedGfxhubTlb\] Transaction status=0, PhysReady=1, AckObserved=1',
    ('Source=PRESENT_SLOT -> Physical VRAM {0} \(Seg=1\)' -f $sourceHex),
    '\[SdmaPresent\] BLT_TRIPOD PASS #1:',
    ('BYTE_PROOF PASS: Source={0} Dest=0x0 Bytes=11059200 \(byte-a-byte\)' -f $sourceHex)
)
foreach ($pattern in $required) {
    if ($log -notmatch $pattern) { throw "P5 recusada: prova ausente: $pattern" }
}
foreach ($pattern in @(
        'BYTE_PROOF FAIL', 'SDMA=FAILED_CLOSED',
        'STATUS_DEVICE_BUSY', 'STATUS_IO_DEVICE_ERROR')) {
    if ($log -match $pattern) { throw "P5 recusada: assinatura de falha: $pattern" }
}

$visual = Get-Content -LiteralPath $disarmPath -Raw | ConvertFrom-Json
if ($visual.display_visible -ne $true) {
    throw 'P5 recusada: desktop nao foi confirmado visualmente.'
}
$device = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
    Where-Object { $_.PNPDeviceID -like 'PCI\VEN_1002&DEV_13FE*' })
if ($device.Count -ne 1 -or $device[0].Service -ne 'bc250mcdm' -or
    $device[0].ConfigManagerErrorCode -ne 0) {
    throw 'P5 recusada: BC-250 nao permanece em bc250mcdm Codigo 0.'
}

$result = [ordered]@{
    test = 'P5_DWM_BACKING_TO_GOP_SDMA_TRIPOD'
    p5_hardware_validated = $true
    source_offset = $sourceHex
    byte_count = 11059200
    desktop_visible_minutes = 10
    confirmed_at = [datetime]::Now.ToString('o')
}
$resultPath = Join-Path $resolved 'p5-dwm-backing-tripod-result.json'
$result | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
Write-Host 'P5_DWM_BACKING_TO_GOP_SDMA_TRIPOD: PASS'
