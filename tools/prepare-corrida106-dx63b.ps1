[CmdletBinding()]
param()

# Corrida 106: DX.63b -- alocacoes padrao staging/GDI publicam geometria para
# o present Blt alcancar o destino (Corrida 105: 60/60 GEOMETRIA_DO_DESTINO_DESCONHECIDA).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '2E76ACEB71C08B7C91717386DB8D041BA851FDB9CB81CD710D397BB6436D7F5A'
    (Join-Path $package 'bc250mcdm.inf') = '8E9DBCBEB0CDE8BFA325812F9888B38BBEEAD8886A0383F8A045244702F42652'
    (Join-Path $package 'bc250mcdm.cat') = 'BB89DAF4C18F35D17CE73FF88DF4A1EAC65FF0758F94C163F20B1F1C98741A44'
    (Join-Path $package 'bc250umd.dll') = '3B70D7F2A3DDB549BF64CFE573FB1BB9304651EFDB2C43B56DD411BC7B9FAE2D'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidato Corrida 106 ausente: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) {
        throw "Candidato Corrida 106 divergiu: $path esperado=$($expected[$path]) atual=$actual"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/24/2026,3.267.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto KMD/UMD nao corresponde ao candidato controlado da Corrida 106.'
}
foreach ($script in @('audit-bc250mcdm-safety.py', 'audit-bc250mcdm-lab-package.ps1')) {
    $path = Join-Path $PSScriptRoot $script
    if ($script.EndsWith('.py')) { & python $path | Out-Null }
    else { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $path | Out-Null }
    if ($LASTEXITCODE -ne 0) { throw "Preflight reprovou: $script exit=$LASTEXITCODE" }
}
Write-Host 'Corrida 106 DX.63b: hashes KMD/UMD/RADV fixados, auditorias PASS. Nenhum bind ocorreu.'
