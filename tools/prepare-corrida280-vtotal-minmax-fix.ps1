# Apenas leitura: Corrida 280 = lab 3.429.1.0 (doc 728). Depois de 4 corridas seguidas (276-279)
# com o mesmo sintoma de tela com artefatos, mesmo resetando o MONITOR e o cabo DisplayPort
# (refuta a hipotese de estado acumulado no monitor), fui ao codigo real da AMD
# (dcn10_optc.c:optc1_program_timing) e achei que o driver real NUNCA escreve OTG_V_TOTAL sem
# TAMBEM escrever OTG_V_TOTAL_MIN e OTG_V_TOTAL_MAX com o mesmo valor -- "in case V_TOTAL_CONTROL
# is on". O D19 (desde a 275) so' escrevia V_TOTAL. Este candidato: (1) lê e loga V_TOTAL/MIN/
# MAX/CONTROL antes e depois de cada troca (diagnostico, zero risco); (2) agora escreve MIN/MAX
# junto com V_TOTAL, exatamente como optc1_program_timing, com readback e devolucao ao valor do
# GOP (Lei 5/6). D12 mantido armado (ja provado inocente). DcnExt=509. UMD B73F9BDA e RADV
# B4AE2E14 iguais.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '162F244B97CB6FA16F6043E757838D49A25B8E2E543F661D883C3F75C5B4EBAF'
    (Join-Path $package 'bc250mcdm.inf') = '0FF632B5DDB6222CE9F14C292FBA51DC3986E90C1BE03AAEBAB7C356C42EB8EB'
    (Join-Path $package 'bc250mcdm.cat') = 'DC0F6CCF508F6B149CDB67991759DA7B69F3EED4ED692B8402250A25471D1F40'
    (Join-Path $package 'bc250umd.dll') = 'B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66'
    $radv = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    'C:\BC250_LIB\vulkan_radeon.dll' = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    (Join-Path $package 'cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $env:SystemRoot 'System32\drivers\cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $PSScriptRoot 'bc250-processo-novo.exe') = '768C82F74EFB48B1AE765113E15879899A493B4843A98B8296F166C53C9D10DF'
}
foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 280 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 280 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/13/2026,3.429.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [int]$manifest.GfxWaveProbe -ne 0 -or
    [int]$manifest.DebugTrace -ne 1 -or
    [int]$manifest.DcnFlip -ne 1 -or
    [int]$manifest.DcnExt -ne 509 -or
    [bool]$manifest.DcnExtAuxHwModeArmed -ne $true -or
    [bool]$manifest.DcnExtVTotalRefreshTestArmed -ne $true -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')]) {
    throw 'Manifesto nao corresponde ao candidato 3.429.1.0 controlado.'
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
Write-Host 'Preflight 280 aprovado: candidato lab 3.429.1.0 (V_TOTAL_MIN/MAX acompanham o V_TOTAL, como optc1_program_timing), ferramenta T3 conferida.' -ForegroundColor Green
