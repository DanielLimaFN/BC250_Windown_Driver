param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,30)][int]$CommandTimeoutSeconds = 10
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 1.0
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

if (-not $Execute) {
    throw 'Preflight bloqueado por padrao. Use -Execute; ele executa somente leituras.'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute elevado.'
}
foreach ($path in @($cli, $workspaceDriver, $installedDriver)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-cp-psp-preflight"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$events = [System.Collections.Generic.List[object]]::new()

function Run-Cli([string]$Name, [string[]]$Arguments, [switch]$AllowFailure) {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $cli
    $psi.Arguments = (($Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
    $psi.WorkingDirectory = $root
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = New-Object Diagnostics.Process
    $p.StartInfo = $psi
    if (-not $p.Start()) { throw "Falha ao iniciar $Name" }
    $stdout = $p.StandardOutput.ReadToEndAsync()
    $stderr = $p.StandardError.ReadToEndAsync()
    $timedOut = -not $p.WaitForExit($CommandTimeoutSeconds * 1000)
    if ($timedOut) { $p.Kill(); $p.WaitForExit() }
    $exitCode = if ($timedOut) { 1460 } else { $p.ExitCode }
    $result = [pscustomobject]@{
        name = $Name
        command = 'bc250ctl.exe ' + ($Arguments -join ' ')
        exit_code = $exitCode
        timed_out = $timedOut
        output = @(($stdout.Result + $stderr.Result) -split "`r?`n" | Where-Object { $_ })
    }
    $events.Add($result)
    if (($timedOut -or $exitCode -ne 0) -and -not $AllowFailure) {
        throw "$Name falhou: exit=$exitCode timeout=$timedOut"
    }
    return $result
}

function Read-Reg([string]$Name, [uint32]$Offset) {
    $r = Run-Cli $Name @('reg','r',('0x{0:X5}' -f $Offset)) -AllowFailure
    $text = $r.output -join ' '
    if ($r.exit_code -ne 0 -or $text -notmatch '=\s*0x([0-9A-Fa-f]{8})') { return $null }
    return [Convert]::ToUInt32($Matches[1], 16)
}

function Hex($Value) {
    if ($null -eq $Value) { return $null }
    return ('0x{0:X8}' -f [uint32]$Value)
}

$failure = $null
$checks = [ordered]@{}
$registers = [ordered]@{}
try {
    $infoBefore = Run-Cli 'info_before' @('info')
    $infoText = $infoBefore.output -join ' '
    $checks.abi_29 = $infoText -match 'ABI 29'
    $checks.readonly_before = $infoText -match 'armamento=readonly'
    if (-not $checks.abi_29 -or -not $checks.readonly_before) {
        throw 'Driver ativo nao reportou ABI 29 em readonly.'
    }

    $psp = Run-Cli 'psp_status' @('psp','status') -AllowFailure
    $checks.psp_status_read = ($psp.exit_code -eq 0 -and -not $psp.timed_out)

    # Offsets SOC15 GC 10.1.3. Nenhum comando de armamento ou escrita aparece
    # neste instrumento.
    $targets = [ordered]@{
        RLC_RLCS_BOOTLOAD_STATUS = 0x3BA34
        RLC_RLCS_UTCL2_CNTL      = 0x3BB98
        RLC_GPM_STAT             = 0x3B9B8
        CP_STAT                  = 0x08680
        CP_PFP_INSTR_PNTR        = 0x08694
        CP_ME_INSTR_PNTR         = 0x08698
        CP_CE_INSTR_PNTR         = 0x0869C
        CP_MEC1_INSTR_PNTR       = 0x086A0
        CP_ME_CNTL               = 0x086D8
        CP_MEC_CNTL              = 0x08234
    }
    foreach ($name in $targets.Keys) {
        $a = Read-Reg "${name}_a" ([uint32]$targets[$name])
        $b = Read-Reg "${name}_b" ([uint32]$targets[$name])
        $registers[$name] = [pscustomobject]@{
            offset = ('0x{0:X5}' -f [uint32]$targets[$name])
            first = Hex $a
            second = Hex $b
            readable = ($null -ne $a -and $null -ne $b)
            stable = ($null -ne $a -and $null -ne $b -and $a -eq $b)
        }
    }
    $checks.register_reads_20 = (@($registers.Values | Where-Object { $_.readable }).Count -eq $targets.Count)
    $checks.utcl2_matches_linux = ($registers.RLC_RLCS_UTCL2_CNTL.first -eq '0x00000018' -and
                                   $registers.RLC_RLCS_UTCL2_CNTL.second -eq '0x00000018')
    $checks.bootload_bits_clear = ($registers.RLC_RLCS_BOOTLOAD_STATUS.first -eq '0x00000000' -and
                                   $registers.RLC_RLCS_BOOTLOAD_STATUS.second -eq '0x00000000')
    $checks.rlc_executes_rom = ($registers.RLC_GPM_STAT.first -eq '0x00F40016' -and
                                $registers.RLC_GPM_STAT.second -eq '0x00F40016')

    $infoAfter = Run-Cli 'info_after' @('info')
    $checks.readonly_after = (($infoAfter.output -join ' ') -match 'ABI 29\s+armamento=readonly')
    if (-not $checks.register_reads_20 -or -not $checks.readonly_after) {
        throw 'Captura readonly incompleta.'
    }
} catch {
    $failure = $_.Exception.Message
}

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time=$_.TimeCreated.ToString('o'); message=($_.Message -split "`r?`n")[0] } })
} catch { }
if ($bugchecks.Count -and -not $failure) { $failure = 'Bugcheck detectado na janela do preflight.' }

$workspaceHash = (Get-FileHash -LiteralPath $workspaceDriver -Algorithm SHA256).Hash
$installedHash = (Get-FileHash -LiteralPath $installedDriver -Algorithm SHA256).Hash
$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Readonly PSP/RLC/CP state capture before designing a corrected PSP load sequence'
    capture_completed = ($null -eq $failure)
    failure = $failure
    verdict = $verdict
    writes_authorized = $false
    writes_reported = 0
    workspace_driver_sha256 = $workspaceHash.ToLowerInvariant()
    installed_driver_sha256 = $installedHash.ToLowerInvariant()
    hashes_match = ($workspaceHash -eq $installedHash)
    checks = [pscustomobject]$checks
    registers = [pscustomobject]$registers
    psp_status = @($events | Where-Object { $_.name -eq 'psp_status' })
    bugchecks = $bugchecks
    not_proved = @(
        'No firmware was loaded',
        'No TMR was set up or modified',
        'No golden register was applied',
        'No CP engine, ring or PM4 packet was executed'
    )
    commands = $events
}
$record | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code)"; ''
} | Set-Content -LiteralPath (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host "Evidencia: $dir"
foreach ($check in $checks.GetEnumerator()) {
    Write-Host ('  [{0}] {1}' -f $(if ($check.Value) { 'ok  ' } else { 'FALHA' }), $check.Key)
}
foreach ($name in $registers.Keys) {
    Write-Host ('  {0,-28} {1} / {2}' -f $name, $registers[$name].first, $registers[$name].second)
}
Write-Host 'Zero escrita autorizada; zero escrita reportada.'
Write-Host "Hashes iguais: $(if ($workspaceHash -eq $installedHash) { 'sim' } else { 'nao (permitido neste preflight readonly)' })"
Write-Host "Veredito global permanece: $verdict"
if ($failure) { Write-Error $failure; exit 1 }
