[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = 'EA3F02B328D590DD6FC734D29E99733CA01BA8495F525882AFC39A08C45A281E'
    (Join-Path $package 'bc250umd.dll') = 'B786E3608743979D1E5B7C4D0C24C9ABDF84FD58A29BD75177523F2DAF36B6FF'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidato Corrida 94 ausente: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) {
        throw "Candidato Corrida 94 divergiu: $path esperado=$($expected[$path]) atual=$actual"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/23/2026,3.254.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1) {
    throw 'Manifesto KMD/UMD nao corresponde ao candidato controlado da Corrida 94.'
}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'audit-bc250mcdm-lab-package.ps1')
if ($LASTEXITCODE -ne 0) { throw "Auditoria do pacote falhou: $LASTEXITCODE" }
Write-Host 'Corrida 94 DX.49 (janela 300 s): hashes KMD/UMD/RADV fixados e pacote auditado. O bind ainda nao ocorreu.'
