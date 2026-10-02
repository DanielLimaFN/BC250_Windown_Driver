[CmdletBinding()]
param()

# Apenas leitura: fixa o candidato DX.97 para a Corrida 159 (600 s) e o atalho de dump.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '3CB6FAC7F95D1AFAAD74486597CB9C5AECB2F4A3CDE688CA8FEBA3F0E6E7089B'
    (Join-Path $package 'bc250mcdm.inf') = '83D049E3CAEEB0270C2EBBACF4B3A653963C34BE6F4D172E2FA7E0E2799979C7'
    (Join-Path $package 'bc250mcdm.cat') = '5B20E0F326BB6E68C9E60862EB50D3BA2B78FA40F3F29735084BAE1C71E0B41C'
    (Join-Path $package 'bc250umd.dll') = 'FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911'
    $radv = 'FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 159 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 159 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '09/28/2026,3.308.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato DX.97 controlado.'
}
$blockers = @()
# DX.86: a Corrida 132 rodou com DEBUG/DEBUGPORT=NET no boot. Depurador de kernel
# habilitado muda o tempo do experimento; a 139 so' roda sem ele, salvo pedido explicito.
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
# Corrida 145: atalho alternativo documentado pela Microsoft ("Forcing a System
# Crash from the Keyboard"): qualquer Ctrl (Dump1Keys=0x22) + Scroll Lock duas
# vezes (Dump2Key=0x7D, indice 125 da keyToScanTbl = scan 0x46). A doc exige
# CrashOnCtrlScroll=0 ou ausente quando crashdump\Dump1Keys/Dump2Key existem.
foreach ($name in @('i8042prt', 'kbdhid')) {
    $params = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\Parameters"
    $dump = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\crashdump"
    $k1 = (Get-ItemProperty -LiteralPath $dump -Name Dump1Keys -ErrorAction SilentlyContinue).Dump1Keys
    $k2 = (Get-ItemProperty -LiteralPath $dump -Name Dump2Key -ErrorAction SilentlyContinue).Dump2Key
    $legacy = (Get-ItemProperty -LiteralPath $params -Name CrashOnCtrlScroll -ErrorAction SilentlyContinue).CrashOnCtrlScroll
    if ([int]$k1 -ne 0x22 -or [int]$k2 -ne 0x7D) {
        throw "Atalho de dump da Corrida 145 nao armado em $name (rode toolsrm-crash-keys-any-ctrl.ps1 elevado e reinicie)."
    }
    if ($null -ne $legacy -and [int]$legacy -ne 0) {
        throw "CrashOnCtrlScroll ainda ligado em $name; a doc exige 0 com o atalho alternativo."
    }
}
Write-Host 'Corrida 159 DX.97 (600 s, harness atrasado, atalho de dump qualquer Ctrl + Scroll Lock 2x): hashes KMD/UMD/RADV fixados, auditorias PASS. Nenhum bind ocorreu.'
