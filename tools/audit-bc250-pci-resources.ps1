param(
    [Parameter(Mandatory=$true)][string]$Evidence
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$jsonPath = if (Test-Path -LiteralPath $Evidence -PathType Container) {
    Join-Path $Evidence 'result.json'
} else { $Evidence }
if (-not (Test-Path -LiteralPath $jsonPath)) { throw "Evidencia ausente: $jsonPath" }
$r = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
$failures = [System.Collections.Generic.List[string]]::new()

if ($r.schema_version -ne 1) { $failures.Add('schema_version diferente de 1.') }
if ($r.writes_authorized -ne $false -or $r.writes_reported -ne 0) { $failures.Add('Captura nao foi estritamente readonly.') }
if ($r.device.instance_id -notlike 'PCI\VEN_1002&DEV_13FE*') { $failures.Add('InstanceId nao e BC-250 1002:13FE.') }
foreach ($name in @('exactly_one_bc250','device_status_ok','memory_resources_present','ranges_valid','disjoint_segments_present','no_single_cpu_window_covers_linux_vram','no_single_cpu_window_reaches_linux_tmr_end')) {
    if ($r.checks.$name -ne $true) { $failures.Add("Gate reprovado: $name.") }
}
if (@($r.memory_descriptors).Count -lt 1 -or @($r.disjoint_segments).Count -lt 1) {
    $failures.Add('Descritores/segmentos ausentes.')
}
$largest = [uint64]$r.largest_disjoint_segment
if ($largest -ge [uint64]0x20000000) { $failures.Add('Maior janela alcanca 512 MiB; conclusao registrada e inconsistente.') }

$expected = [ordered]@{
    '0x00000000C0000000' = [uint64]0x10000000
    '0x00000000D0000000' = [uint64]0x00200000
    '0x00000000FE800000' = [uint64]0x00080000
}
if (@($r.disjoint_segments).Count -ne $expected.Count) {
    $failures.Add("Segmentos disjuntos: $(@($r.disjoint_segments).Count); esperado $($expected.Count).")
}
foreach ($start in $expected.Keys) {
    $segment = @($r.disjoint_segments | Where-Object { $_.start -eq $start })
    if ($segment.Count -ne 1 -or [uint64]$segment[0].length -ne $expected[$start]) {
        $failures.Add("Segmento $start ausente ou com tamanho diferente de 0x$($expected[$start].ToString('X')).")
    }
}

if ($failures.Count) {
    Write-Host 'AUDITORIA RECURSOS PCI: FALHA' -ForegroundColor Red
    foreach ($failure in $failures) { Write-Host "  - $failure" -ForegroundColor Red }
    exit 1
}
Write-Host 'AUDITORIA RECURSOS PCI: OK' -ForegroundColor Green
Write-Host "  Descritores: $(@($r.memory_descriptors).Count); segmentos: $(@($r.disjoint_segments).Count)."
Write-Host ('  Maior janela CPU disjunta: {0} bytes (0x{0:X}).' -f $largest)
Write-Host '  Nenhuma janela atribuida cobre os 512 MiB de VRAM ou o fim da TMR Linux.'
Write-Host '  Isso nao concede ownership de VRAM ao bc250dbg.'
Write-Host '  Veredito: NAO VALIDADO EM HARDWARE.'
