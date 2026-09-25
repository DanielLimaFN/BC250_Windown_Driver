[CmdletBinding()]
param()

# Corrida 109: mesmo comportamento da 107/108 + DX.65 (batimento do rastro duravel,
# so instrumentacao). Variavel: Radeon Software IMPEDIDA de abrir (Corrida 108).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '7EF9D54D90ABBA2DD30463D698DE253B30E76860EB635532B836BF2A0F5189FC'
    (Join-Path $package 'bc250mcdm.inf') = '6A304FA1B7DE793FC3986A29D9D41390D9EA83692B90ED9B486A2BFD718795ED'
    (Join-Path $package 'bc250mcdm.cat') = '9D72D9DB0AFEE58E8EA9CF12517A5B89252913C943441A59AB715BAB44024F48'
    (Join-Path $package 'bc250umd.dll') = 'B6AB6788ABBFD77FD6EE66D008E336BF4C4AA8B3B265013CC70B8E9EBD751E0B'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidato Corrida 109 ausente: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) {
        throw "Candidato Corrida 109 divergiu: $path esperado=$($expected[$path]) atual=$actual"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/24/2026,3.269.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto KMD/UMD nao corresponde ao candidato controlado da Corrida 109.'
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
    throw ('Corrida 109 exige a Radeon Software impedida de abrir: ' + ($radeonBlockers -join '; '))
}
foreach ($script in @('audit-bc250mcdm-safety.py', 'audit-bc250mcdm-lab-package.ps1')) {
    $path = Join-Path $PSScriptRoot $script
    if ($script.EndsWith('.py')) { & python $path | Out-Null }
    else { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $path | Out-Null }
    if ($LASTEXITCODE -ne 0) { throw "Preflight reprovou: $script exit=$LASTEXITCODE" }
}
Write-Host 'Corrida 109 DX.65 (Radeon Software fora): hashes KMD/UMD/RADV fixados, auditorias PASS. Nenhum bind ocorreu.'
