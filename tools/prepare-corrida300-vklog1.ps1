# Apenas leitura: Corrida 300 = lab 3.454.1.0 (doc 761), sobre a 299 (3.453.1.0).
# NOVIDADE: VKLOG.1 -- a RADV grava o proprio log (BC250_UMD_LOG_<pid>-VULKAN.txt) em
# QUALQUER app Vulkan, inclusive os que nunca carregam a UMD (GravityMark, memtest):
# instancia e device pedidos, placas aceitas/recusadas, todo erro Vulkan com arquivo:linha
# e toda funcao pedida que a RADV nao expoe. SYS e UMD iguais aos da 299.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'driver\wdk\package\output_lab'
$radv = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm\build\src\amd\vulkan\vulkan_radeon.dll'
$expected = [ordered]@{
    (Join-Path $package 'bc250mcdm.sys') = '6E61F032122828871D17A9D3497A79C6F4DA900FEBAA2B6D6FC00A4877E8C8F1'
    (Join-Path $package 'bc250mcdm.inf') = '70037D661CCA623246FACE5FA758BE33EABEB3ABB6294C61B88775D790E80B34'
    (Join-Path $package 'bc250mcdm.cat') = '7EA352AD77BC3DEF7E4DE072112EE679F6C06D132595F4AC55050618B6C6C007'
    (Join-Path $package 'bc250umd.dll')  = 'B1AAFF1234573C717C2B1A0D355E8AEF56799B354C6B00D466E5D58A67412414'
    (Join-Path $package 'vulkan_radeon.dll') = '296F3F8F0502F7AF2FD36096083A8AEE9387B9B8526FAD109AABA496DD99526C'
    (Join-Path $package 'z-1.dll') = '1AFBD67C6806FBB2E754222146962FA13F78B1C0A1BBDFAFBEC73CD1CD4AEB34'
    (Join-Path $package 'bc250_vk_icd.json') = '9D01FB48C88DD820A79432B3E974C27A7282FF05E990BE38B75E574231D8E214'
    $radv = 'C6C9E7F3F2D0477AA84438E3D526EB1CFECD295BCE4D6293A57F8DA91A4985EC'
    'C:\BC250_LIB\vulkan_radeon.dll' = 'C6C9E7F3F2D0477AA84438E3D526EB1CFECD295BCE4D6293A57F8DA91A4985EC'
    (Join-Path $package 'cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $env:SystemRoot 'System32\drivers\cyan_skillfish2_rlc.bin') = '20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB'
    (Join-Path $PSScriptRoot 'bc250-processo-novo.exe') = '61168272E243B8D6C8329527D7A50F1B3BA50240A278B38CC415276691893907'
}

foreach ($path in $expected.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Candidato 300 ausente: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$path]) { throw "Candidato 300 divergiu: $path esperado=$($expected[$path]) atual=$actual" }
}

$manifest = Get-Content -LiteralPath (Join-Path $root 'driver\wdk\package\output_lab.manifest.json') -Raw -Encoding utf8 | ConvertFrom-Json
if ($manifest.DriverVer -ne '10/14/2026,3.454.1.0' -or
    $manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB' -or
    [bool]$manifest.ShaderDispatchEnabled -ne $true -or
    [bool]$manifest.PersistentSubmissionEnabled -ne $true -or
    [int]$manifest.GfxWaveProbe -ne 0 -or
    [int]$manifest.DebugTrace -ne 1 -or
    [int]$manifest.DcnFlip -ne 1 -or
    [int]$manifest.DcnExt -ne 125 -or
        [bool]$manifest.DcnExtAuxHwModeArmed -ne $false -or
    [bool]$manifest.DcnExtVTotalRefreshTestArmed -ne $false -or
    $manifest.PackageSysSha256 -ne $expected[(Join-Path $package 'bc250mcdm.sys')] -or
    $manifest.PackageInfSha256 -ne $expected[(Join-Path $package 'bc250mcdm.inf')] -or
    $manifest.PackageCatSha256 -ne $expected[(Join-Path $package 'bc250mcdm.cat')] -or
    $manifest.PackageUmdSha256 -ne $expected[(Join-Path $package 'bc250umd.dll')] -or
    $manifest.PackageRadvSha256 -ne $expected[(Join-Path $package 'vulkan_radeon.dll')] -or
    $manifest.SourceRadvSha256 -ne $expected[$radv]) {
    throw 'Manifesto nao corresponde ao candidato 3.454.1.0 controlado (VKLOG.1).'
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


# ICD.1: o runtime do Vulkan (loader) tem de existir e o ICD antigo quebrado nao pode sobrar.
$vkLoader = Join-Path $env:SystemRoot 'System32\vulkan-1.dll'
if (-not (Test-Path -LiteralPath $vkLoader -PathType Leaf)) {
    $blockers += 'runtime do Vulkan ausente (System32\vulkan-1.dll): rode tools\instalar-vulkan-runtime.ps1 elevado'
} else {
    Write-Host ("Runtime do Vulkan: {0}" -f (Get-Item -LiteralPath $vkLoader).VersionInfo.FileVersion)
}
$vkKey = 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers'
if (Test-Path -LiteralPath $vkKey) {
    $vkStale = @((Get-Item -LiteralPath $vkKey).Property | Where-Object { $_ -match '(?i)bc250_icd\.json$' })
    if ($vkStale.Count -gt 0) { $blockers += "ICD antigo ainda registrado: $($vkStale -join '; ') (rode tools\instalar-vulkan-runtime.ps1)" }
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

Write-Host 'Preflight 300 aprovado: candidato lab 3.454.1.0 (VKLOG.1).' -ForegroundColor Green
