# Apenas leitura: Corrida 256 = lab 3.402.1.0 (doc 679) = 255 + 4C (supressao de MMIO de HDP no caminho critico de FLUSH_TLB em APU/DRAM; _mm_mfence preservado; zero MMIO 0x3FC4 em rajadas de paginacao). UMD B73F9BDA e RADV B4AE2E14 iguais.
# Apenas leitura: Corrida 255 = lab 3.401.1.0 (doc 676) = 254 + C4 (zeramento de alocacao nova por SDMA CONST_FILL em fatias de 4 MiB, como amdgpu_fill_buffer com fill_max_bytes 0x400000; a copia de zeros da X2b fica dormente). UMD B73F9BDA e RADV B4AE2E14 iguais.
# Apenas leitura: Corrida 254 = lab 3.400.1.0 (doc 674) = 253 + C3 (todo mapeamento da VRAM pela DRAM no KMD sai write-combined, como o ioremap_wc do amdgpu em APU e o mapeamento do VidMm; um so' tipo por pagina fisica). Origem 253: lab 3.399.1.0 (doc 672) = 252 + C1 (WPTR do anel GFX por doorbell como o amdgpu, CP_RB_WPTR_POLL_CNTL no valor da placa, sem GRBM/MMIO de WPTR por submissao) + MON.1 (temperatura Tctl do KMD de volta, 1/s). C2 sem codigo: o MEC ja' esta' sem polling de WPTR (doc 672). Origem 252: lab 3.398.1.0 (doc 668) = 251b com a P5 (tabela de paginas de 4 niveis x 9 bits, VA de 48 bits, GCVM_CONTEXTn_CNTL DEPTH=3 e END=2^36-1, como o amdgpu gfx10) e a SOMBRA.1 DESLIGADA (o Present volta ao scanout real: tela visivel). Foto CG segue desligada. UMD B73F9BDA e RADV B4AE2E14 iguais. Origem 251b: lab 3.397.1.0 (doc 665) = 251 com a sombra alocada depois do allocator do bootstrap existir, a foto CG DESLIGADA e o refresh sem espera. Origem 251: lab 3.396.1.0 (doc 664) = 3.395.1.0 (250) + SOMBRA.1 (Present/flip copiam para uma sombra em VRAM, nao para o scanout que o DCN varre) + foto do clock gating por bloco e refresh real (so' leitura, no boot). Origem 250: repeticao da 249 com a sonda DCN compilada com o log real. Origem 249: lab 3.394.1.0 = 3.393.1.0 (G.1 + RLC rodando + LOGMIN.1) + TMR.1 (TMR do PSP persistente, PA da DRAM, sem fallback) + split do SDMA sem static + D0 + IhRing + VRD.1 + sonda DCN (A; B/C so' leitura), doc 658. Historico: 3.391.1.0 (G.1 + RLC.1) + RLC.1b (RLC_PG_CNTL bit 23 = 1, handshake RLC/SMU desligado, ramo sem GFXOFF de gfx_v10_0_rlc_smu_handshake_cntl) + LOGMIN.1 (log minimo da UMD), doc 650. P3.3=0; a MESMA RADV B4AE2E14.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '1C01B05201F85C5266076DBC99BD244695FC0EB55B965C2DD34A64A27116A817'
    (Join-Path $package 'bc250mcdm.inf') = 'E8EF6697C6B0F8A7185C7CFAA0E2196AB402915EF29AED6AB87F89C2B278CA73'
    (Join-Path $package 'bc250mcdm.cat') = 'FFFC77F14EFE93672C404DC468F7CCF69F04442D9D279756B52F3D19805B9BC4'
    (Join-Path $package 'bc250umd.dll') = 'B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66'
    $radv = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    'C:\BC250_LIB\vulkan_radeon.dll' = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    (Join-Path $package 'cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $env:SystemRoot 'System32\drivers\cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 256 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 256 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/09/2026,3.402.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato 3.402.1.0 controlado.'
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
# So' DWM, Explorer e o menu podem usar a GPU nesta corrida.
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
# Corrida 237 (doc 625): nada pode desligar a tela nem suspender dentro dos 600 s, senao a carga muda sozinha.
foreach ($q in @(@('SUB_VIDEO', 'VIDEOIDLE', 'desligar o monitor'), @('SUB_SLEEP', 'STANDBYIDLE', 'suspensao'), @('SUB_SLEEP', 'HIBERNATEIDLE', 'hibernacao'))) {
    $txt = (powercfg /query SCHEME_CURRENT $q[0] $q[1]) -join "`n"
    if ($txt -match '(?i)(Alternadas Atuais|Current AC Power Setting Index):\s*0x([0-9a-f]+)') {
        $sec = [Convert]::ToInt32($Matches[2], 16)
        if ($sec -ne 0 -and $sec -le 900) { throw "Energia: $($q[2]) em $sec s (na tomada) cai dentro da corrida; ponha em Nunca." }
        Write-Host "Energia: $($q[2]) = $(if ($sec -eq 0) { 'nunca' } else { "$sec s" })"
    } else { Write-Warning "Nao li $($q[2]) do powercfg; confira a mao que nao dispara em 15 min." }
}
$ss = Get-ItemProperty -LiteralPath 'HKCU:\Control Panel\Desktop' -ErrorAction SilentlyContinue
if ($ss -and $ss.'SCRNSAVE.EXE' -and [int]$ss.ScreenSaveActive -eq 1) {
    $to = 0; [void][int]::TryParse("$($ss.ScreenSaveTimeOut)", [ref]$to)
    if ($to -gt 0 -and $to -le 900) { throw "Protecao de tela ($($ss.'SCRNSAVE.EXE')) em $to s cai dentro da corrida; desligue." }
}
Write-Host "Preflight Corrida 256 aprovado: pacote 3.402.1.0 (4C supressao de MMIO de HDP em FLUSH_TLB + C4 + C3 + C1 + MON.1, sobre a P5; P3.3=0) e RADV B4AE2E14 selados; carga = SO o menu Iniciar." -ForegroundColor Green
