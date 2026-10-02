[CmdletBinding()]
param()

# Apenas leitura: fixa o candidato FRENTE B v2 + instrumentacao (3.337.1.0) para a Corrida 192.
# Corrida 191: desktop inteiro 51/51 Presents (v2 resolveu a tela preta), mas 1 TDR de GFX as 07:24
# num draw do menu (Fence 1580) e trava dura as ~07:28 num TRANSFER de paginacao de 11 MB.
# Unica mudanca: CreateResource registra Alias=0/1 para todo recurso (comportamento identico).

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '8000EE83C493F36AA562221F51FA1890D9DFA7DB7D796AEF2A7BC315C3A80416'
    (Join-Path $package 'bc250mcdm.inf') = '8DCF54174FEB971E7C0AEB75983977DD4A3BB7E9F877F37B741751FAC61F0A01'
    (Join-Path $package 'bc250mcdm.cat') = '91BD8A00463DF5E9B55F9CB233BE6E6713A41F7EB48C37571D7AADC1751C1A5F'
    (Join-Path $package 'bc250umd.dll') = 'A1300553CF7420DCCE1A83A282B5E0B8462F8CA0AAF7DF64D1DB5D665A7F6263'
    $radv = '5447A17D05FE55344ED766D049219CF7E14AB4552C17EAED789BB4656835DE82'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 192 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 192 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/01/2026,3.337.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato Frente B v2 + Alias no log controlado.'
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
    if (Test-Path -LiteralPath $params) {
        $val = (Get-ItemProperty -LiteralPath $params -Name 'CrashOnCtrlScroll' -ErrorAction SilentlyContinue).CrashOnCtrlScroll
        if ($null -eq $val -or [int]$val -ne 1) {
            Write-Warning "CrashOnCtrlScroll ausente ou desligado em $name; atalho de teclado pode nao funcionar."
        }
    }
}
Write-Host "Preflight Frente B v2 (Corrida 192) aprovado: pacote 3.337.1.0 selado." -ForegroundColor Green
