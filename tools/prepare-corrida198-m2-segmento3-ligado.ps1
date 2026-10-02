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
    (Join-Path $package 'bc250mcdm.sys') = 'FAF7BD3EAFF6D2968899948F0AE8286CAD34DEF9B6B256CAD376B369DB1C64AB'
    (Join-Path $package 'bc250mcdm.inf') = 'E31053EECF40B8D5D91557ACBA9E95BEA3378F6C129BCCBD723211D4A9D7F976'
    (Join-Path $package 'bc250mcdm.cat') = 'E76567D6A5E6DFA938D8530D48A0872EDCE9489D4162135F236AB1D38B16D0F4'
    (Join-Path $package 'bc250umd.dll') = '5CEC87A9BF4260BE1DCF452D93F409B9FF3FF2103B7A78EB0BD63F2C75A2ED59'
    $radv = '5447A17D05FE55344ED766D049219CF7E14AB4552C17EAED789BB4656835DE82'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 198 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 198 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/01/2026,3.344.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 1 -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato M2 controlado.'
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
Write-Host "Preflight M2 completo (Corrida 198) aprovado: pacote 3.344.1.0 selado." -ForegroundColor Green
