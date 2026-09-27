[CmdletBinding()]
param()

# Corrida 105: DX.61 (guardiao: assentamento PnP e evidencia por tentativa),
# DX.62 (seguimento do CreateAllocation, so' instrumentacao) e DX.63 (present
# Blt copia para o destino, unica mudanca comportamental do driver).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '3C7ACDEF6332AFF5A0B65A4B83E1B9DA444DD7995842343766C93E1673C0B099'
    (Join-Path $package 'bc250mcdm.inf') = '710EDEE6E4EFBCE763C3C567EFF745C553262BAF53D7072D2C83048BB1CF05AB'
    (Join-Path $package 'bc250mcdm.cat') = 'DE83CCD0C03A13636C871E5E8BFCE718E0F35AB987919C4D1234B9970ECB86E3'
    (Join-Path $package 'bc250umd.dll') = '3B70D7F2A3DDB549BF64CFE573FB1BB9304651EFDB2C43B56DD411BC7B9FAE2D'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidato Corrida 105 ausente: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) {
        throw "Candidato Corrida 105 divergiu: $path esperado=$($expected[$path]) atual=$actual"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/24/2026,3.266.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto KMD/UMD nao corresponde ao candidato controlado da Corrida 105.'
}
foreach ($script in @('audit-bc250mcdm-safety.py', 'audit-bc250mcdm-lab-package.ps1')) {
    $path = Join-Path $PSScriptRoot $script
    if ($script.EndsWith('.py')) { & python $path | Out-Null }
    else { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $path | Out-Null }
    if ($LASTEXITCODE -ne 0) { throw "Preflight reprovou: $script exit=$LASTEXITCODE" }
}
Write-Host 'Corrida 105 DX.61-63: hashes KMD/UMD/RADV fixados, auditorias PASS. Nenhum bind ocorreu.'
