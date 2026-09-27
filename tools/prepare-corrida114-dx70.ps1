[CmdletBinding()]
param()

# Corrida 114: DX.70 -- amostragem de logs rotineiros da UMD.
# Radeon Software segue impedida, como na 109, para nao reintroduzir variavel.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = 'A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A'
    (Join-Path $package 'bc250mcdm.inf') = '8507683D45F0E6B9DDEA352D5C44C30804910AE03E3977AAA742E57F0BB1E65B'
    (Join-Path $package 'bc250mcdm.cat') = '03D26BC9B04167511DDC3801FF64E5B29568E09D1628FF2ABD614CFC20535F73'
    (Join-Path $package 'bc250umd.dll') = '80668E4E91EDD0117B66BFDA2992E15F25168566177AC1BEEA739F6C9541A9A3'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidato Corrida 114 ausente: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) {
        throw "Candidato Corrida 114 divergiu: $path esperado=$($expected[$path]) atual=$actual"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/24/2026,3.274.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto KMD/UMD nao corresponde ao candidato controlado da Corrida 114.'
}
# Corrida 108: Radeonsoftware.exe abriu a UMD ~5 min 10 s apos o bind em 100,
# 101, 102 e 108, e o travamento veio 42 s a 3,5 min depois (H). Esta corrida
# so' mede alguma coisa se a Radeon Software NAO puder abrir. Somente leitura:
# quem desliga e' o operador.
$radeonBlockers = @()
if (Get-Process -Name 'RadeonSoftware' -ErrorAction SilentlyContinue) {
    $radeonBlockers += 'processo RadeonSoftware.exe em execucao'
}
$startCn = Get-ScheduledTask -TaskName 'StartCN' -ErrorAction SilentlyContinue
if ($startCn -and $startCn.State -ne 'Disabled') {
    $radeonBlockers += "tarefa agendada StartCN habilitada (State=$($startCn.State))"
}
$events = Get-Service -Name 'AMD External Events Utility' -ErrorAction SilentlyContinue
if ($events -and ($events.Status -ne 'Stopped' -or $events.StartType -ne 'Disabled')) {
    $radeonBlockers += "servico 'AMD External Events Utility' Status=$($events.Status) StartType=$($events.StartType)"
}
if ($radeonBlockers.Count -gt 0) {
    throw ('Corrida 114 exige a Radeon Software impedida de abrir: ' + ($radeonBlockers -join '; '))
}
foreach ($script in @('audit-bc250mcdm-safety.py', 'audit-bc250mcdm-lab-package.ps1')) {
    $path = Join-Path $PSScriptRoot $script
    if ($script.EndsWith('.py')) { & python $path | Out-Null }
    else { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $path | Out-Null }
    if ($LASTEXITCODE -ne 0) { throw "Preflight reprovou: $script exit=$LASTEXITCODE" }
}
Write-Host 'Corrida 114 DX.70 (amostragem UMD): hashes KMD/UMD/RADV fixados, auditorias PASS. Nenhum bind ocorreu.'
