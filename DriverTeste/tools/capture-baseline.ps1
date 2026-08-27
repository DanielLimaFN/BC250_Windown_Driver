param(
    [switch]$IncludeSmuQueries,
    [string]$OutputRoot,
    [ValidateRange(2, 60)]
    [int]$CommandTimeoutSeconds = 8
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $projectRoot 'studio\output\bc250ctl.exe'
$driver = Join-Path $projectRoot 'output\bc250dbg.sys'
$rules = Join-Path $projectRoot 'BC250_HARDWARE_RULES.md'

if (-not (Test-Path -LiteralPath $cli)) {
    throw "CLI ausente: $cli"
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Execute este coletor em um PowerShell elevado.'
}

$labService = Get-Service -Name 'bc250dbg' -ErrorAction SilentlyContinue
if (-not $labService) {
    throw 'Servico bc250dbg nao instalado. Rode dbg\install.bat como Administrador.'
}
if ($labService.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Running) {
    throw 'Servico bc250dbg parado apos o boot. Rode dbg\start.bat como Administrador e repita a captura.'
}

if (-not $OutputRoot) {
    $OutputRoot = Join-Path $projectRoot 'docs\evidence'
}
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$hostNameSafe = $env:COMPUTERNAME -replace '[^A-Za-z0-9_.-]', '_'
$sessionDir = Join-Path $OutputRoot ("$stamp-$hostNameSafe-baseline")
New-Item -ItemType Directory -Path $sessionDir -Force | Out-Null

function Invoke-Capture {
    param(
        [string]$Name,
        [string[]]$Arguments,
        [string]$Class = 'host_readonly'
    )

    Write-Host ("[baseline] {0}..." -f $Name) -NoNewline

    $quotedArguments = $Arguments | ForEach-Object {
        '"' + ($_.Replace('"', '\"')) + '"'
    }
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $cli
    $startInfo.Arguments = $quotedArguments -join ' '
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Não foi possível iniciar: $Name"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($CommandTimeoutSeconds * 1000)
    if ($timedOut) {
        $process.Kill()
        $process.WaitForExit()
    }
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $exitCode = if ($timedOut) { 1460 } else { $process.ExitCode }
    $lines = @(($stdout + $stderr) -split "`r?`n" | Where-Object { $_ -ne '' })

    if ($timedOut) {
        Write-Host " TIMEOUT ($CommandTimeoutSeconds s)" -ForegroundColor Yellow
    } elseif ($exitCode -eq 0) {
        Write-Host ' ok' -ForegroundColor Green
    } else {
        Write-Host " exit=$exitCode" -ForegroundColor Yellow
    }

    $result = [pscustomobject]@{
        name = $Name
        evidence_class = $Class
        command = 'bc250ctl.exe ' + ($Arguments -join ' ')
        exit_code = $exitCode
        timed_out = $timedOut
        output = @($lines)
    }
    $result | ConvertTo-Json -Depth 4 -Compress |
        Add-Content -LiteralPath (Join-Path $sessionDir 'baseline.partial.jsonl') -Encoding UTF8
    return $result
}

$commands = @()
$commands += Invoke-Capture -Name 'info' -Arguments @('info')
if ($commands[-1].exit_code -ne 0) {
    $commands | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $sessionDir 'commands.json') -Encoding UTF8
    throw "bc250dbg.sys nao esta acessivel nesta sessao. Captura parcial: $sessionDir"
}

$commands += Invoke-Capture -Name 'psp_status' -Arguments @('psp', 'status')
$commands += Invoke-Capture -Name 'gcvm_status' -Arguments @('gcvm', 'status')
$commands += Invoke-Capture -Name 'sdma_status' -Arguments @('sdma', 'status')

# Preflight puramente observacional do mailbox Q0. RSP == 0 significa que a
# fila nao esta pronta (ou ha uma transacao presa); nesse caso nao escrevemos
# CMD/ARG/RSP e nao tentamos nenhuma mensagem.
$smuQ0Cmd = Invoke-Capture -Name 'smu_q0_cmd_raw' -Arguments @('smn', 'r', '0x03B10A08')
$commands += $smuQ0Cmd
$smuQ0Arg = Invoke-Capture -Name 'smu_q0_arg_raw' -Arguments @('smn', 'r', '0x03B10A48')
$commands += $smuQ0Arg
$smuQ0Rsp = Invoke-Capture -Name 'smu_q0_rsp_raw' -Arguments @('smn', 'r', '0x03B10A68')
$commands += $smuQ0Rsp

$registers = @(
    @('grbm_status2', '0x08008'),
    @('grbm_status', '0x08010'),
    @('cp_mec_cntl', '0x08234'),
    @('cp_stat', '0x08680'),
    @('cp_me_cntl', '0x086D8'),
    @('cc_gc_shader_array_config', '0x089BC'),
    @('spi_pg_enable_static_wgp_mask', '0x0935C'),
    @('scratch_reg0', '0x30100'),
    @('grbm_gfx_index', '0x30800'),
    @('rlc_pg_always_on_wgp_mask', '0x3B14C'),
    @('sdma0_clk_ctrl', '0x049EC'),
    @('sdma0_status_reg', '0x04A14'),
    @('sdma0_status2_reg', '0x04A60'),
    @('sdma0_gfx_rb_cntl', '0x04B80'),
    @('sdma0_gfx_rb_base', '0x04B84'),
    @('sdma0_gfx_rb_base_hi', '0x04B88'),
    @('sdma0_gfx_rb_rptr', '0x04B8C'),
    @('sdma0_gfx_rb_rptr_hi', '0x04B90'),
    @('sdma0_gfx_rb_wptr', '0x04B94'),
    @('sdma0_gfx_rb_wptr_hi', '0x04B98'),
    @('sdma0_gfx_rb_wptr_poll_cntl', '0x04B9C'),
    @('sdma0_gfx_rb_rptr_addr_hi', '0x04BA0'),
    @('sdma0_gfx_rb_rptr_addr_lo', '0x04BA4'),
    @('sdma0_gfx_ib_cntl', '0x04BA8'),
    @('sdma0_gfx_doorbell', '0x04BC8'),
    @('sdma_quarantined_offset_4bd0', '0x04BD0'),
    @('sdma0_gfx_doorbell_offset', '0x04C2C'),
    @('sdma0_gfx_wptr_poll_addr_hi', '0x04C48'),
    @('sdma0_gfx_wptr_poll_addr_lo', '0x04C4C'),
    @('sdma0_gfx_rb_aql_cntl', '0x04C50'),
    @('sdma0_gfx_minor_ptr_update', '0x04C54')
)
foreach ($reg in $registers) {
    $commands += Invoke-Capture -Name $reg[0] -Arguments @('reg', 'r', $reg[1])
}

if ($IncludeSmuQueries) {
    $smuQueries = @(
        @('smu_test', '0x01'),
        @('smu_version', '0x02'),
        @('smu_driver_if', '0x03'),
        @('smu_query_gfxclk', '0x0F'),
        @('smu_active_wgp', '0x1E'),
        @('smu_gfx_frequency', '0x37'),
        @('smu_gfx_vid', '0x38'),
        @('smu_enabled_features', '0x3D')
    )
    $rspReady = $false
    if (-not $smuQ0Rsp.timed_out -and $smuQ0Rsp.exit_code -eq 0) {
        $rspText = $smuQ0Rsp.output -join ' '
        if ($rspText -match '=\s*0x([0-9A-Fa-f]+)') {
            $rspReady = ([Convert]::ToUInt32($Matches[1], 16) -ne 0)
        }
    }

    if (-not $rspReady) {
        Write-Warning 'SMU Q0_RSP bruto esta zerado/indisponivel; consultas mailbox foram omitidas para nao agravar uma fila presa.'
    } else {
        foreach ($query in $smuQueries) {
            $result = Invoke-Capture -Name $query[0] -Arguments @('smu', '0', $query[1], '0') -Class 'smu_safe_mailbox_query'
            $commands += $result
            if ($result.timed_out) {
                Write-Warning 'Primeiro timeout SMU: consultas restantes foram omitidas porque compartilham o mesmo transporte.'
                break
            }
        }
    }
}

$os = Get-CimInstance Win32_OperatingSystem
$lastBoot = $os.LastBootUpTime
$uptime = (Get-Date) - $lastBoot
$device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -like 'PCI\VEN_1002&DEV_13FE*' } |
    Select-Object -First 1 Status, Class, FriendlyName, InstanceId, Service
$signedDriver = Get-CimInstance Win32_PnPSignedDriver -ErrorAction SilentlyContinue |
    Where-Object { $_.DeviceID -like 'PCI\VEN_1002&DEV_13FE*' } |
    Select-Object -First 1 DeviceName, DriverProviderName, DriverVersion,
        DriverDate, InfName, IsSigned, Signer
$boundService = if ($device) { $device.Service } else { $null }
$environmentContaminated = $boundService -and $boundService -ne 'BasicDisplay'

if ($environmentContaminated) {
    Write-Warning ("A BC-250 esta ligada ao driver {0}; o estado observado pode ter sido programado por ele e nao representa POST puro." -f $boundService)
}

$artifacts = @()
foreach ($path in @($driver, $cli, $rules)) {
    if (Test-Path -LiteralPath $path) {
        $item = Get-Item -LiteralPath $path
        $artifacts += [pscustomobject]@{
            path = $item.FullName
            size_bytes = $item.Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
}

$signature = if (Test-Path -LiteralPath $driver) {
    $sig = Get-AuthenticodeSignature -LiteralPath $driver
    [pscustomobject]@{
        status = $sig.Status.ToString()
        signer = if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { $null }
        thumbprint = if ($sig.SignerCertificate) { $sig.SignerCertificate.Thumbprint } else { $null }
    }
} else { $null }

$mailboxAttempted = @($commands | Where-Object { $_.evidence_class -eq 'smu_safe_mailbox_query' }).Count -gt 0
$hardwareVerdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)

$manifest = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    verdict = $hardwareVerdict
    mutation_policy = if ($mailboxAttempted) {
        'MMIO/SMN read plus explicitly whitelisted SMU query mailboxes; no arm/write/engine init'
    } else {
        'MMIO/SMN read only; no mailbox query, arm, write or engine init'
    }
    computer = $env:COMPUTERNAME
    user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    os = [pscustomobject]@{
        caption = $os.Caption
        version = $os.Version
        build = $os.BuildNumber
        last_boot_at = $lastBoot.ToString('o')
        uptime_seconds = [math]::Floor($uptime.TotalSeconds)
    }
    pnp_device = $device
    bound_display_driver = $signedDriver
    environment = [pscustomobject]@{
        bound_service = $boundService
        legacy_kmd_absent = (-not $environmentContaminated)
        note = if ($environmentContaminated) {
            'Another display KMD is bound and may mutate GPU state before this capture.'
        } else {
            'No non-basic display KMD was detected by the collector.'
        }
    }
    driver_signature = $signature
    artifacts = $artifacts
    commands = $commands
}

$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $sessionDir 'baseline.json') -Encoding UTF8
$commands | ForEach-Object {
    "=== $($_.name) [$($_.evidence_class)] ==="
    "`$ $($_.command)"
    $_.output
    "exit=$($_.exit_code)"
    ''
} | Set-Content -LiteralPath (Join-Path $sessionDir 'baseline.txt') -Encoding UTF8

Write-Host "Captura concluida: $sessionDir"
Write-Host "Veredito permanece: $hardwareVerdict"
Write-Host ("Ultimo boot do Windows: {0:o} (uptime {1:N0} s)" -f $lastBoot, $uptime.TotalSeconds)
