param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,30)][int]$CommandTimeoutSeconds = 10
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

if (-not $Execute) {
    throw 'Preflight bloqueado por padrao. Use -Execute; ele faz somente leitura de 36 registradores.'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute elevado.'
}
foreach ($path in @($cli, $abiHeader, $workspaceDriver, $installedDriver)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }

$header = Get-Content -LiteralPath $abiHeader -Raw
if ($header -notmatch '#define\s+BC250DBG_ABI_VERSION\s+29u') {
    throw 'Workspace nao esta em ABI 29.'
}
$workspaceHash = (Get-FileHash -LiteralPath $workspaceDriver -Algorithm SHA256).Hash
$installedHash = (Get-FileHash -LiteralPath $installedDriver -Algorithm SHA256).Hash
if ($workspaceHash -ne $installedHash) {
    throw "Build instalado diverge do workspace: workspace=$workspaceHash instalado=$installedHash"
}

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-cp-golden-preflight"
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

$failure = $null
$checks = [ordered]@{}
try {
    $info = Run-Cli 'info_pre' @('info')
    $infoText = $info.output -join "`n"
    $checks.abi_29 = $infoText -match 'bc250dbg v\d+\.\d+\s+ABI 29\s+armamento=readonly'
    if (-not $checks.abi_29) { throw 'Driver nao reportou ABI 29 em readonly.' }

    # Exit 1 e um resultado de hardware valido quando o driver encontra uma
    # leitura ambigua/indisponivel. Capture a saida inteira antes de decidir o
    # veredito; sem -AllowFailure o harness confundia RECUSADO com falha de
    # transporte e nem executava o info_after.
    $preflight = Run-Cli 'golden_preflight' @('cp','golden','preflight') -AllowFailure
    $text = $preflight.output -join "`n"
    $checks.command_completed = -not $preflight.timed_out -and $preflight.exit_code -in @(0,1)
    $checks.register_count_36 = $text -match 'registradores\s*:\s*36(?:\D|$)'
    $checks.write_count_zero = $text -match 'escritas\s*:\s*0(?:\D|$)'
    $checks.reported_values_36 = ([regex]::Matches($text, '(?m)^\s+0x[0-9A-Fa-f]{5}\s+:\s+0x[0-9A-Fa-f]{8}\s*$').Count -eq 36)

    $failureCount = if ($text -match 'falhas\s*:\s*(\d+)') { [int]$Matches[1] } else { -1 }
    $failedOffsets = @([regex]::Matches(
        $text,
        '(?m)^\s+(0x[0-9A-Fa-f]{5})\s+:\s+leituras\s+0x[0-9A-Fa-f]{8}\s+/\s+0x[0-9A-Fa-f]{8}\s*$') |
        ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() })
    $expectedBlocked = @('0X33884','0X3380C','0X3F040','0X3F044')
    $checks.failure_list_consistent = ($failureCount -ge 0 -and $failedOffsets.Count -eq $failureCount)
    $checks.known_abi29_classification = ($failureCount -eq 4 -and
        @($expectedBlocked | Where-Object { $_ -notin $failedOffsets }).Count -eq 0)
    $checks.golden_confirmed_32 = $checks.known_abi29_classification
    $checks.gl2_ambiguous_2 = $checks.known_abi29_classification
    $checks.gpa_unavailable_2 = $checks.known_abi29_classification
    $checks.preflight_approved = ($preflight.exit_code -eq 0 -and $failureCount -eq 0 -and
        $text -match 'preflight readonly aprovado; nenhuma escrita emitida')

    $infoAfter = Run-Cli 'info_after' @('info')
    $checks.readonly_after = (($infoAfter.output -join ' ') -match 'ABI 29\s+armamento=readonly')
    if (-not $checks.readonly_after) {
        throw 'Driver nao permaneceu em readonly depois do preflight.'
    }

    $captureChecks = @('command_completed','register_count_36','write_count_zero',
        'reported_values_36','failure_list_consistent','readonly_after')
    $captureFailed = @($captureChecks | Where-Object { -not $checks[$_] })
    if ($captureFailed.Count) {
        throw 'Captura incompleta: ' + ($captureFailed -join ', ')
    }
    if (-not $checks.preflight_approved) {
        if ($checks.known_abi29_classification) {
            throw 'Preflight recusado: 32 golden confirmados, 2 GL2 ambiguos e 2 GPA indisponiveis.'
        }
        throw "Preflight recusado com $failureCount falha(s); classificacao diferente da ABI 29 conhecida."
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

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'ABI 29 readonly preflight of 34 Cyan Skillfish golden registers plus two GPA registers'
    capture_completed = [bool]$checks.command_completed
    preflight_passed = [bool]$checks.preflight_approved
    failure = $failure
    verdict = $verdict
    writes_authorized = $false
    writes_reported = 0
    register_reads = 36
    classification = [pscustomobject]@{
        golden_confirmed = $(if ($checks.known_abi29_classification) { 32 } else { $null })
        golden_ambiguous = $(if ($checks.known_abi29_classification) { 2 } else { $null })
        gpa_unavailable = $(if ($checks.known_abi29_classification) { 2 } else { $null })
    }
    checks = [pscustomobject]$checks
    bugchecks = $bugchecks
    environment = [pscustomobject]@{
        abi = 29
        arm_level = 'readonly'
        driver_sha256 = $workspaceHash.ToLowerInvariant()
    }
    not_proved = @(
        'No golden setting was applied',
        'No GRBM selector was written',
        'No CP firmware, jump table, ring or PM4 packet was executed',
        'Observability does not validate CP, compute or WDDM'
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
Write-Host ('  [{0}] sem_bugcheck' -f $(if ($bugchecks.Count -eq 0) { 'ok  ' } else { 'FALHA' }))
Write-Host 'Zero escrita autorizada; zero escrita reportada. Apply golden nao foi chamado.'
Write-Host "Veredito global permanece: $verdict"
if ($failure) { Write-Error $failure; exit 1 }
