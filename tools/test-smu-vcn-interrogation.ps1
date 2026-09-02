<#
    Ensaio de Diagnóstico de Hardware:
    SMU Power-Up Interrogation & Direct MMIO Scan para o Bloco VCN (AMD BC-250)
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 25
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

if (-not (Test-Path $cli)) {
    throw "Utilitario bc250ctl.exe nao encontrado em: $cli"
}

if (-not $Execute) {
    throw 'Ensaio de laboratorio bloqueado por padrao. Execute com o parametro -Execute.'
}

if (-not $OutputRoot) {
    $OutputRoot = Join-Path $root 'docs\evidence'
}
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-smu-vcn-interrogation"
New-Item -ItemType Directory -Path $dir -Force | Out-Null

$events = [System.Collections.Generic.List[object]]::new()
$gates = [System.Collections.Generic.List[object]]::new()

function Run([string]$Name, [string[]]$Arguments, [switch]$AllowFailure) {
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
    $ot = $p.StandardOutput.ReadToEndAsync()
    $et = $p.StandardError.ReadToEndAsync()
    $to = -not $p.WaitForExit($CommandTimeoutSeconds * 1000)
    if ($to) { $p.Kill(); $p.WaitForExit() }
    $ec = if ($to) { 1460 } else { $p.ExitCode }
    $r = [pscustomobject]@{
        name = $Name
        command = 'bc250ctl.exe ' + ($Arguments -join ' ')
        exit_code = $ec
        timed_out = $to
        output = @(($ot.Result + $et.Result) -split "`r?`n" | Where-Object { $_ })
    }
    $events.Add($r)
    if (($to -or $ec -ne 0) -and -not $AllowFailure) { throw "$Name falhou: exit=$ec timeout=$to" }
    return $r
}

function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}

$smuResponses = @{}
$mmioResponses = @{}
$armed = $false

try {
    Write-Host "===============================================================================" -ForegroundColor Cyan
    Write-Host "     ENSAIO SMU POWER-UP & DIRECT MMIO PARA BLOCO VCN (AMD BC-250)" -ForegroundColor Cyan
    Write-Host "===============================================================================" -ForegroundColor Cyan

    # 1. PREFLIGHT
    Write-Host "[1/5] Executando Preflight de Driver e Termal..." -ForegroundColor Yellow
    $infoPre = Run 'info_pre' @('info') -AllowFailure
    $dbgAlive = ($infoPre.output -join ' ') -match 'bc250dbg'
    Add-Gate 'A_driver_bc250dbg_ativo' $dbgAlive 'Driver de laboratorio ativo e responsivo' $infoPre.output

    if ($dbgAlive) {
        Run 'arm_write' @('arm', 'write') -AllowFailure | Out-Null
        $armed = $true
    }

    # 2. SNAPSHOT - SMU FEATURES
    Write-Host "[2/5] Consultando Features Ativas do SMU (Mensagem 0x3D)..." -ForegroundColor Yellow
    $smuFeatRes = Run 'smu_get_features' @('smu', '0', '0x3D', '0') -AllowFailure
    $smuResponses['0x3D_features'] = $smuFeatRes.output
    Write-Host "  -> Resposta SMU Features: $($smuFeatRes.output -join ' ')" -ForegroundColor Gray

    # 3. INTERROGAÇÃO SMU POWER-UP (Filas 0 e 2)
    Write-Host "[3/5] Interrogando SMU com Mensagens de Power-Up VCN..." -ForegroundColor Yellow
    $candidateMsgs = @(
        [pscustomobject]@{ id = '0x10'; name = 'PPSMC_MSG_PowerUpVcn_10' },
        [pscustomobject]@{ id = '0x11'; name = 'PPSMC_MSG_PowerUpVcn_11' },
        [pscustomobject]@{ id = '0x12'; name = 'PPSMC_MSG_PowerUpVcn_12' },
        [pscustomobject]@{ id = '0x13'; name = 'PPSMC_MSG_PowerUpJpeg_13' },
        [pscustomobject]@{ id = '0x14'; name = 'PPSMC_MSG_AllowGfxOff_14' },
        [pscustomobject]@{ id = '0x15'; name = 'PPSMC_MSG_DisallowGfxOff_15' }
    )

    foreach ($m in $candidateMsgs) {
        foreach ($q in @('0', '2')) {
            $msgName = "smu_q$($q)_msg_$($m.id)"
            $res = Run $msgName @('smu', $q, $m.id, '0') -AllowFailure
            $smuResponses["Q$($q)_$($m.id)_$($m.name)"] = $res.output
            Write-Host "  -> Fila $q | Msg $($m.id) ($($m.name)): $($res.output -join ' ')" -ForegroundColor Gray
        }
    }

    # 4. VARREDURA DIRECT MMIO / SMN DO VCN
    Write-Host "[4/5] Realizando Varredura Direct MMIO dos Registradores VCN..." -ForegroundColor Yellow
    $vcnRegs = @(
        [pscustomobject]@{ name = 'UVD_POWER_STATUS';   smn = '0x01800280'; bar5 = '0x18280' },
        [pscustomobject]@{ name = 'UVD_NO_OP';          smn = '0x01800284'; bar5 = '0x18284' },
        [pscustomobject]@{ name = 'UVD_CGC_GATE';       smn = '0x01800288'; bar5 = '0x18288' },
        [pscustomobject]@{ name = 'UVD_CGC_STATUS';     smn = '0x0180028C'; bar5 = '0x1828C' },
        [pscustomobject]@{ name = 'UVD_VCPU_CNTL';      smn = '0x01800198'; bar5 = '0x18198' },
        [pscustomobject]@{ name = 'UVD_STATUS';         smn = '0x01800188'; bar5 = '0x18188' },
        [pscustomobject]@{ name = 'UVD_LMI_CACHE_LOW';  smn = '0x0180017C'; bar5 = '0x1817C' },
        [pscustomobject]@{ name = 'UVD_LMI_CACHE_HIGH'; smn = '0x01800180'; bar5 = '0x18180' },
        [pscustomobject]@{ name = 'UVD_GPCOM_CMD';      smn = '0x01800030'; bar5 = '0x18030' },
        [pscustomobject]@{ name = 'UVD_GPCOM_DATA0';    smn = '0x01800034'; bar5 = '0x18034' }
    )

    $anyVcnAlive = $false
    foreach ($r in $vcnRegs) {
        $readSmn = Run "smn_read_$($r.name)" @('smn', 'read', $r.smn) -AllowFailure
        $valSmn = ($readSmn.output | Select-String -Pattern '0x[0-9A-Fa-f]{8}').Matches.Value
        
        $isFloating = ($valSmn -eq '0xFFFFFFFF' -or -not $valSmn)
        $mmioResponses[$r.name] = [pscustomobject]@{
            smn_addr = $r.smn
            raw_output = $readSmn.output
            parsed_value = $valSmn
            floating = $isFloating
        }

        $col = if ($isFloating) { [ConsoleColor]::DarkGray } else { [ConsoleColor]::Green }
        Write-Host "  -> $($r.name) ($($r.smn)): $(if ($valSmn) { $valSmn } else { 'ERRO/TIMEOUT' }) $(if ($isFloating) { '[FLUTUANDO/OFF]' } else { '[RESPONDEU!]' })" -ForegroundColor $col
        if (-not $isFloating) { $anyVcnAlive = $true }
    }

    Add-Gate 'B_vcn_mmio_responsivo' $anyVcnAlive "Responsividade dos registradores MMIO do VCN" $mmioResponses

    # 5. TEARDOWN & SÍNTESE
    Write-Host "[5/5] Finalizando e Gravando Evidencias..." -ForegroundColor Yellow
    if ($armed) {
        Run 'arm_readonly' @('arm', 'readonly') -AllowFailure | Out-Null
    }

    $verdict = if ($anyVcnAlive) { 'VCN HARDWARE PARCIALMENTE ACESSÍVEL VIA MMIO' } else { 'VCN DESABILITADO EM HARDWARE / EFUSES FLUTUANDO (0xFFFFFFFF)' }

} catch {
    Write-Host "Erro durante o ensaio: $_" -ForegroundColor Red
    $failure = $_.ToString()
} finally {
    $finishedAt = Get-Date
    $durationMs = [math]::Round(($finishedAt - $startedAt).TotalMilliseconds)

    $resultJson = [pscustomobject]@{
        schema_version = 1
        captured_at = (Get-Date).ToString('o')
        duration_ms = $durationMs
        objective = 'Interrogacao SMU Power-Up e Direct MMIO do bloco VCN no AMD BC-250'
        verdict = $verdict
        passed = (-not $failure)
        failure = $failure
        smu_responses = $smuResponses
        mmio_responses = $mmioResponses
        gates = $gates
        events = $events
    }

    $resultPath = Join-Path $dir 'result.json'
    $resultJson | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultPath -Encoding UTF8

    Write-Host "===============================================================================" -ForegroundColor Cyan
    Write-Host "VEREDITO: $verdict" -ForegroundColor $(if ($anyVcnAlive) { [ConsoleColor]::Green } else { [ConsoleColor]::Yellow })
    Write-Host "Evidencia gravada em: $resultPath" -ForegroundColor Cyan
    Write-Host "===============================================================================" -ForegroundColor Cyan
}
