[CmdletBinding()]
param()

# Apenas leitura: fixa o candidato X1 (doc 522) para a Corrida 169 (600 s).
# Muda SO' o KMD: os lacos de espera do SDMA decidem so' pelo registrador RPTR, sem HDP
# e sem ler VRAM enquanto a engine trabalha. Inclui HDP.1, T3.1 e EVT.1. UMD da 3.310.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = 'AAF5D521215A0F51C6B3F9734F6FA27204A23450207CA19E8A4F3962B5CC5458'
    (Join-Path $package 'bc250mcdm.inf') = 'B12807C78BD26226CDB1B406D385636D90C6E25BEB41008A82DF96E1FD4E9A83'
    (Join-Path $package 'bc250mcdm.cat') = '829AA934F898FBD73EA54F0AFE0035CD1820B65F8C244C75D05C5335327A7AF5'
    (Join-Path $package 'bc250umd.dll') = 'D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 169 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 169 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/29/2026,3.314.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato X1 controlado.'
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
Write-Host 'Corrida 169 X1 (600 s, sem harness, atividade minima): hashes KMD/UMD/RADV fixados, auditorias PASS. Nenhum bind ocorreu.'
