# Apenas leitura: Corrida 296 = lab 3.450.1.0 (doc 756), sobre a 295 (doc 754/755) = 3.449.1.0.
# NOVIDADES DA CORRIDA 296:
# 1. BLOCO 1 (ALIVIO DO JOURNAL GPUVA + TAINT):
#    - BC250_ACTIVE_GPUVA_JOURNAL_SLOTS expandido de 4 para 16 (4x capacidade por processo).
#    - BC250_ACTIVE_JOURNAL_TAINT_SLOTS expandido de 16 para 64 (4x capacidade de rastreio de taint).
#    - Cura da histerese de taint (JournalTaintAll e resetado para 0 quando as tabelas esvaziam).
#    - Descarte seguro de journals e taints orfaos no teardown do processo (Bc250ActiveDestroyProcessObject).
#    - Elimina completamente o falso NO_SPACE por saturacao de slots no dwm.exe / apps pesados.
# 2. VCN.1 (SONDA E ATIVACAO DO VCN 2.0 / PS5 LINUX):
#    - Integracao do microcodigo ps5_vcn.bin (VCN 2.0.3, proveniente dos patches do PS5 Linux).
#    - Mapeamento e ungate dos registradores do bloco UVD/VCN (Segmentos 0 e 1, MMIO 0x1E000..0x22000).
#    - Soft-reset canonico do PS5 (vcn_v2_0_ps5_reset) e preparacao dos caches BAR da VCPU.
#    - Polling de prontidao em mmUVD_STATUS (bit 1 / 0x2).
#    - Controlado pelo tunable VcnProbe=2 no INF (fail-closed, com retorno STATUS_SUCCESS sem quebrar StartDevice).
# 3. MANTIDO: APU.2/2b/2c, DCN.1 a DCN.3 (DcnFlip=1, DcnExt=125), ZC.1/1b (zero copia), PSO.1/1b, PAG.1, MENU.1, GART 32MB.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '0F74A48FDBB9FC9F2850016278D2707EFAC6F46B965A36604AB509CDF64D1245'
    (Join-Path $package 'bc250mcdm.inf') = '4F8A9CAD05BB8635F15621DCD9B10EEF093E8A727AEFAFB73B10837499CC0825'
    (Join-Path $package 'bc250mcdm.cat') = '72CD04CA46F2388D99DE09687A242771523D9FB0241967FB4357931141FB9278'
    (Join-Path $package 'bc250umd.dll')  = 'AF95BB016EB129E2596B07BAEF26CA9FD1260DFCEC68BCD59BC0499586AB27D8'
    (Join-Path $package 'ps5_vcn.bin')   = 'BBD15AB65E76178C2918E8224B6AB4AB1711F1B59D935EE131AB188A9712E596'
    $radv = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    'C:\BC250_LIB\vulkan_radeon.dll' = 'B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D'
    (Join-Path $package 'cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $env:SystemRoot 'System32\drivers\cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $PSScriptRoot 'bc250-processo-novo.exe') = '61168272E243B8D6C8329527D7A50F1B3BA50240A278B38CC415276691893907'
}

foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 296 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 296 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}

$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw -Encoding utf8 | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/14/2026,3.450.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [bool]$manifest.ShaderDispatchEnabled -ne $true -or
    [bool]$manifest.PersistentSubmissionEnabled -ne $true -or
    [int]$manifest.GfxWaveProbe -ne 0 -or
    [int]$manifest.DebugTrace -ne 1 -or
    [int]$manifest.DcnFlip -ne 1 -or
    [int]$manifest.DcnExt -ne 125 -or
    [int]$manifest.VcnProbe -ne 2 -or
    [bool]$manifest.DcnExtAuxHwModeArmed -ne $false -or
    [bool]$manifest.DcnExtVTotalRefreshTestArmed -ne $false -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')] -or
    $manifest.VcnFirmwareSha256 -ne $expected[(Join-Path $package 'ps5_vcn.bin')]) {
    throw 'Manifesto nao corresponde ao candidato 3.450.1.0 controlado (Journal Alivio + VCN Probe).'
}

# Conferencia da CMOS
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

Write-Host 'Preflight 296 aprovado: candidato lab 3.450.1.0 (Bloco 1 Alivio Journal GPUVA/Taint + VCN.1 Probe).' -ForegroundColor Green
