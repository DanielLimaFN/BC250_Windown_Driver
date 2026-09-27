[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = 'F643EDC979EC0D59B338F2EC2B8F86D5C4E85345A656C960DFDF662E5617B7F9'
    (Join-Path $package 'bc250umd.dll') = '512880520EA9B12670702D4264C409F3D24025354EE12275016802CDBF53DD6E'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidato Corrida 100 ausente: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) {
        throw "Candidato Corrida 100 divergiu: $path esperado=$($expected[$path]) atual=$actual"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/24/2026,3.261.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1) {
    throw 'Manifesto KMD/UMD nao corresponde ao candidato controlado da Corrida 100.'
}
# Corrida 100: o wrapper saiu sem CRLF e a auditoria de seguranca, rodada so'
# dentro do script do harness, reprovou DEPOIS do bind -- o harness nao rodou.
# A mesma auditoria roda aqui, antes de qualquer bind.
& python (Join-Path $PSScriptRoot 'audit-bc250mcdm-safety.py') | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Auditoria de seguranca reprovou antes do bind: $LASTEXITCODE" }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'audit-bc250mcdm-lab-package.ps1')
if ($LASTEXITCODE -ne 0) { throw "Auditoria do pacote falhou: $LASTEXITCODE" }
Write-Host 'Corrida 100 DX.55 (janela 600 s): hashes KMD/UMD/RADV fixados e pacote auditado. O bind ainda nao ocorreu.'
