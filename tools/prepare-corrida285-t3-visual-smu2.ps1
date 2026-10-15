# Apenas leitura: Corrida 285 = lab 3.434.1.0 (doc 736).
# Retomada dos testes de estabilidade com:
# - Carga T3 com render 3D em janela visivel (480x320) + 96 MB VRAM local (6 texturas x 16 MB) + 30 frames de clear/present ativos por processo.
# - Telemetria SMU.2: leitura periodica no KMD e no monitor-corrida de GFXCLK (MHz), tensao VDDGFX (mV) e versao de firmware do SMU.
# - DCN basico comprovado da 274 mantido intacto: DcnFlip = 1, DcnExt = 125, D12/D19 DESATIVADOS.
# - Watchdog aumentado para 300 s (5 minutos).
# UMD B73F9BDA e RADV B4AE2E14 iguais.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '957A4C3AC91D29BF71B6CFE2F05AF1C602E4F597FECB88864055C9626F037E3D'
    (Join-Path $package 'bc250mcdm.inf') = 'A80632A18DBEEBFD3A9315EA46FA93157D595AFAC5D48EFF24A90351801F5494'
    (Join-Path $package 'bc250mcdm.cat') = 'FBA1B2FB1DDBDA261F508A90BEE9BA4AD0DE33723C6B1B99212B011551B45D75'
    (Join-Path $package 'bc250umd.dll') = 'B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66'
    $radv = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    'C:\BC250_LIB\vulkan_radeon.dll' = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    (Join-Path $package 'cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $env:SystemRoot 'System32\drivers\cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $PSScriptRoot 'bc250-processo-novo.exe') = '05F3A84C283E6F63A836925EBA47D650C0E45F9B93808B2529AA7502B0AC3629'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 285 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 285 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/13/2026,3.434.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 0 -or
    [int]$manifest.DebugTrace -ne 1 -or
    [int]$manifest.DcnFlip -ne 1 -or
    [int]$manifest.DcnExt -ne 125 -or
    [bool]$manifest.DcnExtAuxHwModeArmed -ne $false -or
    [bool]$manifest.DcnExtVTotalRefreshTestArmed -ne $false -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato 3.434.1.0 controlado (DCN basico da Corrida 274: DcnExt=125, D12/D19 FORA, SMU.2 ativo).'
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
if ($cmosText -match 'RESET_REASON raw=0x([0-9A-Fa-f]{8})') {
    Write-Host "RESET_REASON (so registro; nao decide o boot frio): 0x$($Matches[1])"
}
try {
    $bootNow = (Get-WinEvent -FilterHashtable @{LogName='System'; Id=12} -MaxEvents 1).TimeCreated
    $pedidos = Get-WinEvent -FilterHashtable @{LogName='System'; Id=1074; EndTime=$bootNow.AddSeconds(-1)} -MaxEvents 200
    $desligar = $pedidos | Where-Object { [string]$_.Properties[4].Value -notmatch '(?i)reinic|restart' } | Select-Object -First 1
    $ultimoBind = Get-ChildItem -LiteralPath (Join-Path $root 'docs\evidence') -Directory -Filter '*-lab-binding' |
        ForEach-Object {
            if ($_.Name -match '^(\d{4}-\d{2}-\d{2})_(\d{2})-(\d{2})-(\d{2})-') {
                [datetime]::ParseExact("$($Matches[1]) $($Matches[2]):$($Matches[3]):$($Matches[4])", 'yyyy-MM-dd HH:mm:ss', $null)
            }
        } | Sort-Object | Select-Object -Last 1
    $frio = $false
    $motivo = 'nenhum "Desligar" no log'
    if ($desligar) {
        $sujos = Get-WinEvent -FilterHashtable @{LogName='System'; Id=41; StartTime=$desligar.TimeCreated} -MaxEvents 1 -ErrorAction SilentlyContinue
        $reinicios = @($pedidos | Where-Object { $_.TimeCreated -gt $desligar.TimeCreated }).Count
        if ($sujos) { $motivo = "desligamento sujo (41) em $($sujos.TimeCreated) depois do Desligar" }
        elseif ($ultimoBind -and $ultimoBind -gt $desligar.TimeCreated) { $motivo = "bind em $ultimoBind depois do ultimo Desligar ($($desligar.TimeCreated))" }
        else { $frio = $true; $motivo = "Desligar em $($desligar.TimeCreated), $reinicios reinicio(s) depois, nenhum bind nem 41 desde entao" }
    }
    Write-Host ("Boot atual {0}; ultimo bind {1}; boot frio: {2} ({3})" -f $bootNow, $ultimoBind, $frio, $motivo)
    if (-not $frio -and $env:BC250_ALLOW_WARM_BOOT -ne '1') {
        $blockers += "sem boot frio valido ($motivo): desligue (nao reinicie) e ligue de novo, ou BC250_ALLOW_WARM_BOOT=1"
    }
} catch {
    Write-Warning "Nao consegui ler o log do Windows para conferir o boot frio: $($_.Exception.Message)"
}
$apps = @('Claude', 'msedge', 'msedgewebview2', 'M365Copilot', 'SystemSettings', 'Taskmgr', 'chrome', 'firefox', 'Antigravity', 'RadeonSoftware') |
    ForEach-Object { Get-Process -Name $_ -ErrorAction SilentlyContinue } | Where-Object { $_ }
if ($apps) {
    $nomes = ($apps | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
    Write-Host "Programas abertos no preflight (registrado, nao bloqueia): $nomes"
} else {
    Write-Host 'Programas abertos no preflight: nenhum da lista'
}
$startOptions = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control').SystemStartOptions
Write-Host "SystemStartOptions (sem chave): $($startOptions -replace 'ENCRYPTION_KEY=\S+','ENCRYPTION_KEY=***')"
if ($startOptions -match '(^|\s)DEBUG(\s|$)' -and $env:BC250_ALLOW_KERNEL_DEBUG -ne '1') {
    $blockers += 'depurador de kernel habilitado no boot (bcdedit /debug off e reiniciar, ou BC250_ALLOW_KERNEL_DEBUG=1)'
}
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
foreach ($name in @('i8042prt', 'kbdhid')) {
    $params = "HKLM:\SYSTEM\CurrentControlSet\Services\$name\Parameters"
    if (Test-Path -LiteralPath $params) {
        $val = (Get-ItemProperty -LiteralPath $params -Name 'CrashOnCtrlScroll' -ErrorAction SilentlyContinue).CrashOnCtrlScroll
        if ($null -eq $val -or [int]$val -ne 1) {
            Write-Warning "CrashOnCtrlScroll ausente ou desligado em $name; atalho de teclado pode nao funcionar."
        }
    }
}
Write-Host 'Preflight 285 aprovado: candidato lab 3.434.1.0 (T3 com Janela 3D + VRAM 96 MB + Render Loop e SMU.2 GFXCLK/VDDGFX/VER).' -ForegroundColor Green
