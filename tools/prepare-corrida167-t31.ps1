[CmdletBinding()]
param()

# Apenas leitura: fixa o candidato T3.1 (doc 520) para a Corrida 167 (600 s).
# Muda SO' o KMD: a janela WC da VRAM para em 0xFA00000 e nenhuma pagina fica com
# mapeamento WC e UC ao mesmo tempo. Inclui a HDP.1. A UMD e' a mesma da 3.310.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '233795072B23A84B86AD056977C99717A4EFAD0D8C28F87551AA44D35164C72A'
    (Join-Path $package 'bc250mcdm.inf') = '2DEC75A3824912935F871F55C4EEB79B45318CF1690AD5659458D9C43FABF9DC'
    (Join-Path $package 'bc250mcdm.cat') = 'AFC40333174458FAA173397C70AE1D5B7ABB58BD2E5187872C499DF5723D8D8E'
    (Join-Path $package 'bc250umd.dll') = 'D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 167 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 167 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/28/2026,3.312.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato T3.1 controlado.'
}
$blockers = @()
# DX.86: depurador de kernel habilitado muda o tempo do experimento; so' com pedido explicito.
$startOptions = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control').SystemStartOptions
Write-Host "SystemStartOptions (sem chave): $($startOptions -replace 'ENCRYPTION_KEY=\S+','ENCRYPTION_KEY=***')"
if ($startOptions -match '(^|\s)DEBUG(\s|$)' -and $env:BC250_ALLOW_KERNEL_DEBUG -ne '1') {
    $blockers += 'depurador de kernel habilitado no boot (bcdedit /debug off e reiniciar, ou BC250_ALLOW_KERNEL_DEBUG=1)'
}
if (Get-Process -Name 'RadeonSoftware' -ErrorAction SilentlyContinue) { $blockers += 'RadeonSoftware.exe em execucao' }
$startCn = Get-ScheduledTask -TaskName 'StartCN' -ErrorAction SilentlyContinue
if ($startCn -and $startCn.State -ne 'Disabled') { $blockers += "StartCN $($startCn.State)" }
$events = Get-Service -Name 'AMD External Events Utility' -ErrorAction SilentlyContinue
if ($events -and ($events.Status -ne 'Stopped' -or $events.StartType -ne 'Disabled')) {
    $blockers += "AMD External Events Utility $($events.Status)/$($events.StartType)"
}
if ($blockers.Count -gt 0) { throw ('Preflight bloqueado: ' + ($blockers -join '; ')) }
foreach ($script in @('audit-bc250mcdm-safety.py', 'audit-bc250mcdm-lab-package.ps1')) {
    $path = Join-Path $PSScriptRoot $script
    if ($script.EndsWith('.py')) { & python $path | Out-Null }
    else { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $path | Out-Null }
    if ($LASTEXITCODE -ne 0) { throw "Preflight reprovou: $script exit=$LASTEXITCODE" }
}
$crash = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
if ([int]$crash.CrashDumpEnabled -ne 2 -or [int]$crash.AlwaysKeepMemoryDump -ne 1) {
    throw 'Dump de kernel nao configurado para eventual travamento.'
}
# Corrida 145: atalho alternativo de dump (qualquer Ctrl + Scroll Lock 2x).
foreach ($name in @('i8042prt', 'kbdhid')) {
    $params = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\Parameters"
    $dump = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\crashdump"
    $k1 = (Get-ItemProperty -LiteralPath $dump -Name Dump1Keys -ErrorAction SilentlyContinue).Dump1Keys
    $k2 = (Get-ItemProperty -LiteralPath $dump -Name Dump2Key -ErrorAction SilentlyContinue).Dump2Key
    $legacy = (Get-ItemProperty -LiteralPath $params -Name CrashOnCtrlScroll -ErrorAction SilentlyContinue).CrashOnCtrlScroll
    if ([int]$k1 -ne 0x22 -or [int]$k2 -ne 0x7D) {
        throw "Atalho de dump da Corrida 145 nao armado em $name (rode tools\arm-crash-keys-any-ctrl.ps1 elevado e reinicie)."
    }
    if ($null -ne $legacy -and [int]$legacy -ne 0) {
        throw "CrashOnCtrlScroll ainda ligado em $name; a doc exige 0 com o atalho alternativo."
    }
}
Write-Host 'Corrida 167 T3.1 (600 s, sem harness, atividade minima): hashes KMD/UMD/RADV fixados, auditorias PASS. Nenhum bind ocorreu.'
