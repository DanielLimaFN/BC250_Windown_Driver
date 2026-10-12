# Apenas leitura: Corrida 261 = lab 3.407.1.0 (doc 686) = 260 + 5E (emissao incondicional de GCR GL2 rinse ao final de TransferVirtual, FillVirtual e FillByCopy no paging buffer, eliminando dessincronia GL2 e paridade no Data Fabric bit 30 durante eviccoes do VidMm sob estresse de 500 MB). UMD B73F9BDA e RADV B4AE2E14 iguais.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '521BFE9BED1B1D7B05E0CC77011175B683A0712B603CCA1675B17A177B728EBC'
    (Join-Path $package 'bc250mcdm.inf') = '0C6B8ED34B3B92448DE47F6EF0AABCE32B2BA8EEDCB30615C426F220ECB129B4'
    (Join-Path $package 'bc250mcdm.cat') = 'C355727192B9508CFF45ACE67FAD5950160F80610655555C221C222018C30D1B'
    (Join-Path $package 'bc250umd.dll') = 'B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66'
    $radv = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    'C:\BC250_LIB\vulkan_radeon.dll' = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    (Join-Path $package 'cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $env:SystemRoot 'System32\drivers\cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 261 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 261 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/09/2026,3.407.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato 3.407.1.0 controlado.'
}
# doc 551/553: a ultima leitura da CMOS (feita pelo EXECUTAR logo antes) tem de mostrar 256 MB APLICADOS pela ABL.
$cmosDir = Join-Path $root 'docs\evidence\vram-cmos'
$lastRead = Get-ChildItem -LiteralPath $cmosDir -Filter '*-ler.txt' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $lastRead -or $lastRead.LastWriteTime -lt (Get-Date).AddMinutes(-15)) {
    throw 'Leitura da CMOS ausente ou com mais de 15 min: rode tools\vram-cmos\bc250-vram.ps1 -Ler.'
}
$cmosText = Get-Content -LiteralPath $lastRead.FullName -Raw
if ($cmosText -notmatch 'UMA_SIZE=0256 MB' -or $cmosText -notmatch 'valor APLICADO pelo firmware') {
    throw "CMOS nao esta em 256 MB aplicados: $($lastRead.Name)"
}
if ($cmosText -notmatch 'RESET_REASON') {
    Write-Warning 'Leitura da CMOS sem RESET_REASON: bc250-vram.exe antigo? Rode tools\vram-cmos\build.bat.'
}
Write-Host "CMOS: 256 MB aplicados ($($lastRead.Name))"
$blockers = @()
# Corrida 238 (doc 627): o boot antes do bind foi reinicio, e apps reabriram sozinhos.
# Doc 625 sec. 1: depois de boot frio o RESET_REASON nao tem o bit 19 (0x00080000).
if ($cmosText -match 'RESET_REASON raw=0x([0-9A-Fa-f]{8})') {
    $reset = [Convert]::ToUInt32($Matches[1], 16)
    if (($reset -band 0x00080000) -ne 0 -and $env:BC250_ALLOW_WARM_BOOT -ne '1') {
        $blockers += "ultimo reset foi a quente (RESET_REASON=0x$($Matches[1]), bit 19): desligue (nao reinicie) e ligue de novo, ou BC250_ALLOW_WARM_BOOT=1"
    }
} else {
    Write-Warning 'RESET_REASON nao lido: nao da para conferir o boot frio.'
}
# So' DWM, Explorer e o menu podem usar a GPU nesta corrida (e o estresse controlado).
# M365Copilot relanca o msedgewebview2 sozinho (medido em 07/10): feche-o antes do WebView2.
$apps = @('Claude', 'msedge', 'msedgewebview2', 'M365Copilot', 'SystemSettings', 'Taskmgr', 'chrome', 'firefox') |
    ForEach-Object { Get-Process -Name $_ -ErrorAction SilentlyContinue } | Where-Object { $_ }
if ($apps -and $env:BC250_ALLOW_APPS -ne '1') {
    $nomes = ($apps | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
    $blockers += "apps abertos que carregam a GPU: $nomes (feche todos, ou BC250_ALLOW_APPS=1)"
}
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
Write-Host 'Preflight 261 aprovado: candidato lab 3.407.1.0 e ambiente confinados.' -ForegroundColor Green
