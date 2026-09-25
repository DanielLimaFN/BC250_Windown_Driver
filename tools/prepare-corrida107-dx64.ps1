[CmdletBinding()]
param()

# Corrida 107: DX.64 -- a copia de imagem usa e grava o layout rastreado, para o
# Draw do DWM nao ser recusado e o device nao ser recriado (Corrida 106).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '7C4E6E1B406E77B89C6552019DBE549DE280E4B19DC6E18E8BDBAC8C4BCBDB93'
    (Join-Path $package 'bc250mcdm.inf') = '37EC3DF4685538728871BB025025D6FD3D9C7928C556A5D75D11FB93650D884A'
    (Join-Path $package 'bc250mcdm.cat') = '0F0615BEB3C72C2BA82A5B9472F0A9711C202424E46E7E7299E2A5EE9FA9AF55'
    (Join-Path $package 'bc250umd.dll') = 'B6AB6788ABBFD77FD6EE66D008E336BF4C4AA8B3B265013CC70B8E9EBD751E0B'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidato Corrida 107 ausente: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) {
        throw "Candidato Corrida 107 divergiu: $path esperado=$($expected[$path]) atual=$actual"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/24/2026,3.268.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto KMD/UMD nao corresponde ao candidato controlado da Corrida 107.'
}
foreach ($script in @('audit-bc250mcdm-safety.py', 'audit-bc250mcdm-lab-package.ps1')) {
    $path = Join-Path $PSScriptRoot $script
    if ($script.EndsWith('.py')) { & python $path | Out-Null }
    else { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $path | Out-Null }
    if ($LASTEXITCODE -ne 0) { throw "Preflight reprovou: $script exit=$LASTEXITCODE" }
}
Write-Host 'Corrida 107 DX.64: hashes KMD/UMD/RADV fixados, auditorias PASS. Nenhum bind ocorreu.'
