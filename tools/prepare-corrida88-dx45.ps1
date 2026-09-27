[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '7F94EECB5097781E7E020D49959053EA8A5EA31FF8FAACD2E20F6F335EB5D4F7'
    (Join-Path $package 'bc250umd.dll') = '45BB28C3613F8E7D654B373E3E3D76669A58BD13C45876A301781AB5821396B9'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidato Corrida 88 ausente: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) {
        throw "Candidato Corrida 88 divergiu: $path esperado=$($expected[$path]) atual=$actual"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/23/2026,3.249.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1) {
    throw 'Manifesto KMD/UMD nao corresponde ao candidato controlado da Corrida 88.'
}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'audit-bc250mcdm-lab-package.ps1')
if ($LASTEXITCODE -ne 0) { throw "Auditoria do pacote falhou: $LASTEXITCODE" }
Write-Host 'Corrida 88 DX.45: hashes KMD/UMD/RADV fixados e pacote auditado. O bind ainda nao ocorreu.'
