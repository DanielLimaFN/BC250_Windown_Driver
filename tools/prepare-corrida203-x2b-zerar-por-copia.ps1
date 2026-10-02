# Apenas leitura: Corrida 203 = X2b com 3.350.1.0 = 3.349.1.0 + zeramento de alocacao nova por COPY_LINEAR em vez de CONST_FILL, doc 561.
# Uma variavel: o pacote do zeramento. Destino, PTEs, teto de 256 KiB, CG.1, CMOS 256, UMD e RADV identicas.
# Apenas leitura: Corrida 201 = candidato 3.348.1.0 = 3.347.1.0 (Corrida 200) + CG.1 (SDMA0_CLK_CTRL volta aos overrides do POST depois do anel provado), doc 557.
# Unica variavel: o clock gating do SDMA0. Instrumentacao (BPS1/BSQ1/BCG1), CMOS 256, UMD e RADV identicas as da 200.
# Apenas leitura: Corrida 200 = candidato 3.347.1.0 = 3.346.1.0 (Corrida 199) + I.SDMA0 (estado do SDMA0 a cada 100 ms, BSQ1) + I.CG (clock gating a cada 10 s, BCG1), doc 556.
# Unica variavel: instrumentacao somente leitura. CMOS em 256 (exigida abaixo). UMD e RADV identicas as da 199.
# Apenas leitura: Corrida 199 = candidato 3.346.1.0 (= 3.345.1.0 reempacotado: o fonte da UMD auditada mudou com a M1b.1 dormente, doc 554; comportamento identico, interruptores em 0).
# Antes: candidato 3.345.1.0 (= 3.344.1.0 + instrumentacao I.PLAT, doc 553) com a CMOS em UMA_SIZE=256 (doc 551).
# I.PLAT so' MEDE: registro BPS1 (Tctl + estado do SDMA1) a cada 1 s no rastro, e motivo do ultimo reset (FCH) lido e limpo pelo -Ler.
# Antes: Corrida 199 = MESMO pacote 3.344.1.0 da 198 com a CMOS em UMA_SIZE=256 (decisao do Daniel, doc 551: funcionar com o minimo).
# Unica variavel: o tamanho da VRAM. Com 256 MB a VRAM inteira fica dentro do BAR e o segmento 3 deixa de existir sozinho (driver dinamico).
# Apenas leitura: fixa o candidato M2 completo (3.344.1.0) para a Corrida 198: imagens da RADV sem CPU podem morar no segmento 3 (unica variavel).
# Corrida 197: Lock2 fechado (zero falhas na corrida inteira), V-ok com 4096 MB, e BugCheck 0x10E/0x34 aos ~2 min num Lock da UMD na Pesquisa (doc 548 secao 10).
[CmdletBinding()]
param()

# Apenas leitura: fixa o candidato M2.1 (3.343.1.0) para a Corrida 197: segmento 3 DECLARADO, nenhuma alocacao nele,
# e alocacoes da RADV com CPU passam a aceitar a abertura (SegSet=0x3, preferido 1) -- regra de Lock2 do VidMm
# (allocation-usage-tracking.md). Corrida 196 (desfecho A): so' declarar o segmento 3 ja' quebrava o Lock2.
# Antes: candidato M2-diag (3.342.1.0) para a Corrida 196: segmento 3 DECLARADO, nenhuma alocacao nele.
# Corrida 195: Lock2 0xC000000D numa alocacao do segmento 1 depois de uma de 4 KB ir para o 3.
# Antes: candidato M2b (3.341.1.0) para a Corrida 195.
# Corrida 194: segmento 3 recusado (o calculo tratou o marcador opaco do proprio driver como uso).
# Antes: candidato M2 (3.340.1.0) para a Corrida 194 (600 s, ABRINDO O MENU).
# Corrida 193: sem alias nas texturas proprias a memoria nao coube (DWM em laco de 242 recriacoes).
# M2 (doc 548): segmento 3 = VRAM alem do BAR, derivado da topologia lida no boot (UMA da BIOS de
# 512 MB a 8 GB, valor escolhido pelo Daniel sem me dizer); imagens da RADV sem CPU vao para la'.
# Base PA da VRAM tambem lida da placa. UMD e RADV identicas as da 193.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '586E3E4F6C3F8CA7FE3DE1E3CF63AFD0F00012E5DDD2F3EA48B660E8B2CBB12E'
    (Join-Path $package 'bc250mcdm.inf') = 'E4F6A830054C7466FD595AAF59C0EB35AD7505A033F330E42F727F3B30053150'
    (Join-Path $package 'bc250mcdm.cat') = 'F679CA93D8FD8939A180D0DAE0F4F530CFDF0A5998F7B111B2D4E6E58C592A0F'
    (Join-Path $package 'bc250umd.dll') = 'ED025FAB4363AE7F452084B0D28F33F0E2A79AA2A85C5D7336EDEED68040408D'
    $radv = '5447A17D05FE55344ED766D049219CF7E14AB4552C17EAED789BB4656835DE82'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 203 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 203 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/02/2026,3.350.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato 3.350.1.0 controlado.'
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
Write-Host "Preflight Corrida 203 aprovado: pacote 3.350.1.0 (X2b: zeramento por copia em vez de CONST_FILL) selado." -ForegroundColor Green
