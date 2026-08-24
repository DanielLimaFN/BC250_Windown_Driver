param(
    [string]$OutputRoot,
    [ValidateRange(2, 30)]
    [int]$CommandTimeoutSeconds = 8
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $projectRoot 'studio\output\bc250ctl.exe'
$workspaceDriver = Join-Path $projectRoot 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute em um PowerShell elevado.'
}
if (-not (Test-Path -LiteralPath $cli)) { throw "CLI ausente: $cli" }
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') {
    throw 'bc250dbg não está em execução.'
}

$device = Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -like 'PCI\VEN_1002&DEV_13FE*'
} | Select-Object -First 1
if (-not $device -or $device.Service -ne 'BasicDisplay') {
    throw "Preflight recusado: serviço de display é '$($device.Service)', esperado BasicDisplay."
}
if (Get-Service bc250v4 -ErrorAction SilentlyContinue) {
    throw 'Preflight recusado: serviço legado bc250v4 ainda existe.'
}
if (-not (Test-Path -LiteralPath $installedDriver)) {
    throw "Driver instalado ausente: $installedDriver"
}
$workspaceHash = (Get-FileHash -LiteralPath $workspaceDriver -Algorithm SHA256).Hash
$installedHash = (Get-FileHash -LiteralPath $installedDriver -Algorithm SHA256).Hash
if ($workspaceHash -ne $installedHash) {
    throw "Binário instalado diverge do build: workspace=$workspaceHash instalado=$installedHash"
}
$hardwareVerdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)

if (-not $OutputRoot) { $OutputRoot = Join-Path $projectRoot 'docs\evidence' }
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$sessionDir = Join-Path $OutputRoot "$stamp-$env:COMPUTERNAME-sdma-rollback"
New-Item -ItemType Directory -Path $sessionDir -Force | Out-Null

$events = [System.Collections.Generic.List[object]]::new()
function Invoke-Lab {
    param([string]$Name, [string[]]$Arguments, [switch]$AllowFailure)

    $quoted = $Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $cli
    $psi.Arguments = $quoted -join ' '
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi
    if (-not $p.Start()) { throw "Falha ao iniciar $Name" }
    $stdoutTask = $p.StandardOutput.ReadToEndAsync()
    $stderrTask = $p.StandardError.ReadToEndAsync()
    $timedOut = -not $p.WaitForExit($CommandTimeoutSeconds * 1000)
    if ($timedOut) { $p.Kill(); $p.WaitForExit() }
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $exitCode = if ($timedOut) { 1460 } else { $p.ExitCode }
    $result = [pscustomobject]@{
        name = $Name
        command = 'bc250ctl.exe ' + ($Arguments -join ' ')
        exit_code = $exitCode
        timed_out = $timedOut
        output = @(($stdout + $stderr) -split "`r?`n" | Where-Object { $_ })
    }
    $events.Add($result)
    if (($timedOut -or $exitCode -ne 0) -and -not $AllowFailure) {
        throw "$Name falhou: exit=$exitCode timeout=$timedOut"
    }
    return $result
}

$registers = [ordered]@{
    sem_wait_fail_timer = '0x04A04'
    cntl = '0x049F0'
    chicken_bits = '0x049F4'
    gb_addr_config = '0x049F8'
    gb_addr_config_read = '0x049FC'
    f32_cntl = '0x04A28'
    utcl1_cntl = '0x04A70'
    utcl1_page = '0x04AA0'
    clk_ctrl = '0x049EC'
    rb_cntl = '0x04B80'
    rb_base = '0x04B84'
    rb_base_hi = '0x04B88'
    rb_rptr = '0x04B8C'
    rb_rptr_hi = '0x04B90'
    rb_wptr = '0x04B94'
    rb_wptr_hi = '0x04B98'
    wptr_poll_cntl = '0x04B9C'
    rptr_addr_hi = '0x04BA0'
    rptr_addr_lo = '0x04BA4'
    ib_cntl = '0x04BA8'
    doorbell = '0x04BC8'
    minor_ptr_update = '0x04C54'
    wptr_poll_addr_hi = '0x04C48'
    wptr_poll_addr_lo = '0x04C4C'
}

function Read-RegisterSet([string]$Prefix) {
    $set = [ordered]@{}
    foreach ($entry in $registers.GetEnumerator()) {
        $r = Invoke-Lab -Name "$Prefix`_$($entry.Key)" -Arguments @('reg','r',$entry.Value)
        $text = $r.output -join ' '
        if ($text -notmatch '=\s*0x([0-9A-Fa-f]{8})') {
            throw "Não foi possível extrair $($entry.Key): $text"
        }
        $set[$entry.Key] = '0x' + $Matches[1].ToUpperInvariant()
    }
    return [pscustomobject]$set
}

$pre = $null
$post = $null
$rollback = $null
$failure = $null
$armed = $false
try {
    Invoke-Lab -Name 'info_pre' -Arguments @('info') | Out-Null
    Invoke-Lab -Name 'sdma_status_pre' -Arguments @('sdma','status') | Out-Null
    $pre = Read-RegisterSet 'pre'
    if (([Convert]::ToUInt32($pre.rb_cntl.Substring(2), 16) -band 1) -ne 0) {
        throw "Preflight recusado: RB_ENABLE já está ativo ($($pre.rb_cntl))."
    }

    Invoke-Lab -Name 'arm_write' -Arguments @('arm','write') | Out-Null
    $armed = $true
    Invoke-Lab -Name 'sdma_init' -Arguments @('sdma','init','vram','0x0FF00000') | Out-Null
    Invoke-Lab -Name 'sdma_status_armed' -Arguments @('sdma','status') | Out-Null
}
catch {
    $failure = $_.Exception.Message
}
finally {
    if ($armed) {
        $rollback = Invoke-Lab -Name 'sdma_rollback' -Arguments @('sdma','rollback') -AllowFailure
        Invoke-Lab -Name 'arm_readonly' -Arguments @('arm','readonly') -AllowFailure | Out-Null
    }
}

try {
    $post = Read-RegisterSet 'post'
    Invoke-Lab -Name 'sdma_status_post' -Arguments @('sdma','status') | Out-Null
} catch {
    if (-not $failure) { $failure = $_.Exception.Message }
}

$differences = @()
if ($pre -and $post) {
    foreach ($name in $registers.Keys) {
        if ($pre.$name -ne $post.$name) {
            $differences += [pscustomobject]@{ register=$name; before=$pre.$name; after=$post.$name }
        }
    }
}
if ($rollback -and ($rollback.exit_code -ne 0 -or $rollback.timed_out)) {
    if (-not $failure) { $failure = 'O comando de rollback reportou falha.' }
}
if ($differences.Count -gt 0 -and -not $failure) {
    $failure = "$($differences.Count) registradores divergiram após rollback."
}

$manifest = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Validate SDMA ring init rollback only; no command submission and no firmware load'
    verdict = $hardwareVerdict
    passed = (-not $failure)
    failure = $failure
    environment = [pscustomobject]@{
        display_service = $device.Service
        legacy_bc250v4_absent = $true
        installed_driver_sha256 = $installedHash.ToLowerInvariant()
    }
    pre_registers = $pre
    post_registers = $post
    differences = $differences
    commands = $events
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $sessionDir 'rollback.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="
    "`$ $($_.command)"
    $_.output
    "exit=$($_.exit_code) timeout=$($_.timed_out)"
    ''
} | Set-Content -LiteralPath (Join-Path $sessionDir 'rollback.txt') -Encoding UTF8

Write-Host "Evidência: $sessionDir"
if ($failure) {
    Write-Error "FALHA: $failure"
    exit 1
}
Write-Host 'PASSOU: init sem submissão e rollback sem divergências.'
Write-Host "Veredito permanece: $hardwareVerdict"
exit 0
