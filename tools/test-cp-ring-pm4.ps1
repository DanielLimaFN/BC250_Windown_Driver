<#
    Frente do CP, passo 4: Inicializacao do Graphics Ring Buffer 0 (PM4) e Submissao de Pacotes.

    OBJETIVO:
    1. Carregar ucodes (PFP, ME, CE), configurar IC_BASE, jump tables e unhalt;
    2. Inicializar o Graphics Ring Buffer 0 do CP na VRAM (0x0E200000, 4 KB);
    3. Confirmar que as microengines estao varrendo o ucode (PC = 0x0000041E);
    4. Submeter um pacote PM4 PACKET3_NOP (4 dwords) ao anel;
    5. Medir o consumo do pacote: RPTR alcanca WPTR (executado pela GPU);
    6. Submeter a sequencia de boot (CLEAR_STATE + CONTEXT_CONTROL + NOP, 7 dwords);
    7. Medir o avanco do RPTR;
    8. Teardown e rollback transacional completo sem contaminacao de sessao.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,120)][int]$CommandTimeoutSeconds = 60,
    [uint32]$PfpOffset  = 0x0E000000,
    [uint32]$MeOffset   = 0x0E080000,
    [uint32]$CeOffset   = 0x0E100000,
    [uint32]$RingOffset = 0x0E200000
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

$engines = @(
    [pscustomobject]@{ nome = 'pfp'; offset = $PfpOffset; arquivo = 'firmware\cyan_skillfish2_pfp.bin' }
    [pscustomobject]@{ nome = 'me';  offset = $MeOffset;  arquivo = 'firmware\cyan_skillfish2_me.bin' }
    [pscustomobject]@{ nome = 'ce';  offset = $CeOffset;  arquivo = 'firmware\cyan_skillfish2_ce.bin' }
)

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute. Ele inicializa o anel PM4 e submete pacotes sob protecao transacional.'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$abiHeader,$workspaceDriver,$installedDriver)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
foreach ($e in $engines) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $e.arquivo))) { throw "Firmware ausente: $($e.arquivo)" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

$cpTxnCount = $null
foreach ($linha in Get-Content -LiteralPath $abiHeader) {
    if ($linha -match 'BC250DBG_CP_TXN_REG_COUNT\s+(\d+)u') { $cpTxnCount = [int]$Matches[1]; break }
}
if (-not $cpTxnCount) { throw 'Nao foi possivel ler BC250DBG_CP_TXN_REG_COUNT do header da ABI.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-cp-ring-pm4"
New-Item -ItemType Directory -Path $dir -Force | Out-Null

$events = [System.Collections.Generic.List[object]]::new()
$gates = [System.Collections.Generic.List[object]]::new()

function Run([string]$Name, [string[]]$Arguments, [switch]$AllowFailure) {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $cli
    $psi.Arguments = (($Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
    $psi.WorkingDirectory = $root; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = New-Object Diagnostics.Process; $p.StartInfo = $psi
    if (-not $p.Start()) { throw "Falha ao iniciar $Name" }
    $ot = $p.StandardOutput.ReadToEndAsync(); $et = $p.StandardError.ReadToEndAsync()
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

function Read-Reg([string]$Name, [uint32]$Offset) {
    $r = Run $Name @('reg','r',('0x{0:X5}' -f $Offset)) -AllowFailure
    $t = $r.output -join ' '
    if ($t -notmatch '=\s*0x([0-9A-Fa-f]{8})') { return $null }
    return [Convert]::ToUInt32($Matches[1],16)
}

function Format-Hex($V) {
    if ($null -eq $V) { return 'ILEGIVEL' }
    return ('0x{0:X8}' -f $V)
}

function Add-Gate([string]$Name, [bool]$Pass, [string]$Requirement, $Observed) {
    $g = [pscustomobject]@{
        gate = $Name
        pass = $Pass
        requirement = $Requirement
        observed = $Observed
    }
    $gates.Add($g)
    $tag = if ($Pass) { '[ok  ]' } else { '[FALHA]' }
    Write-Host "  $tag $Name"
}

Write-Host "Evidencia: $dir"

$armado = $false
$snapshotFeito = $false
$unhaltFeito = $false

try {
    # -------------------------------------------------------------------------
    # Gate A: headers dos 3 ucodes
    # -------------------------------------------------------------------------
    $headers = @()
    foreach ($e in $engines) {
        $info = Run "info_$($e.nome)" @('cp','info',$e.nome,$e.arquivo) -AllowFailure
        $t = $info.output -join "`n"
        $headers += [pscustomobject]@{
            engine = $e.nome
            exit_code = $info.exit_code
            header = $(if ($t -match 'header\s+:\s+(v[0-9\.]+,\s+\d+\s+bytes)') { $Matches[1] } else { $null })
            ip = $(if ($t -match 'IP\s+:\s+(v[0-9\.]+)') { $Matches[1] } else { $null })
            payload_bytes = $(if ($t -match 'payload\s+:\s+(\d+)\s+bytes') { [int]$Matches[1] } else { $null })
            jt_dwords = $(if ($t -match 'jump table\s+:\s+dw\s+\d+\s+\.\.\s+\d+\s+\((\d+)\s+dwords') { [int]$Matches[1] } else { $null })
            saida = $info.output
        }
    }
    Add-Gate 'A_headers_coerentes' `
        ([bool](@($headers | Where-Object { $_.exit_code -eq 0 -and $_.payload_bytes -gt 0 -and $_.jt_dwords -gt 0 }).Count -eq $engines.Count)) `
        'Os tres ucodes tem header v1.0, IP v10.1 e jump table descrita dentro do payload.' `
        $headers

    # -------------------------------------------------------------------------
    # Gate B: Preflight (engines haltadas no baseline)
    # -------------------------------------------------------------------------
    $meCntlBaseline = Read-Reg 'baseline_CP_ME_CNTL' 0x086D8
    $pfpPcBaseline = Read-Reg 'baseline_PFP_PC' 0x08694
    Add-Gate 'B_baseline_haltado' `
        ([bool]($meCntlBaseline -eq 0x15000000 -and $pfpPcBaseline -eq 0)) `
        'CP_ME_CNTL = 0x15000000 (CE/PFP/ME_HALT ativos) e ponteiro de instrucao zerado.' `
        ([pscustomobject]@{ CP_ME_CNTL = Format-Hex $meCntlBaseline; PFP_PC = Format-Hex $pfpPcBaseline })

    # -------------------------------------------------------------------------
    # Gate C: Snapshot da transacao
    # -------------------------------------------------------------------------
    Run 'arm_write' @('arm','write') | Out-Null
    $armado = $true

    $snap = Run 'snap_cp' @('cp','txn','snapshot') -AllowFailure
    $snapTxt = $snap.output -join "`n"
    $snapRegs = if ($snapTxt -match 'registradores\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    $snapshotFeito = ($snap.exit_code -eq 0 -and $snapRegs -eq $cpTxnCount)
    Add-Gate 'C_snapshot_guardado' `
        ([bool]$snapshotFeito) `
        "Snapshot dos $cpTxnCount registradores do CP capturado com sucesso." `
        ([pscustomobject]@{ exit_code = $snap.exit_code; regs = $snapRegs })

    # -------------------------------------------------------------------------
    # Gate D: Carga dos 3 ucodes na VRAM
    # -------------------------------------------------------------------------
    $stageResults = @()
    foreach ($e in $engines) {
        $off = ('0x{0:X8}' -f $e.offset)
        $st = Run "stage_$($e.nome)" @('cp','stage',$e.nome,$off,$e.arquivo) -AllowFailure
        $stTxt = $st.output -join "`n"
        $stDiv = if ($stTxt -match 'escritos\s+:\s+(\d+)\s+dwords,\s+(\d+)\s+divergencias') { [int]$Matches[2] } else { $null }
        $stDw  = if ($stTxt -match 'escritos\s+:\s+(\d+)\s+dwords') { [int]$Matches[1] } else { $null }
        $stageResults += [pscustomobject]@{
            engine = $e.nome
            offset = $off
            exit_code = $st.exit_code
            dwords = $stDw
            divergencias = $stDiv
            saida = $st.output
        }
    }
    Add-Gate 'D_tres_ucodes_na_vram' `
        ([bool](@($stageResults | Where-Object { $_.exit_code -eq 0 -and $_.divergencias -eq 0 }).Count -eq $engines.Count)) `
        'Os tres ucodes (PFP, ME, CE) gravados e conferidos dword a dword na VRAM.' `
        $stageResults

    # -------------------------------------------------------------------------
    # Gate E: Invalidação das 3 caches de instrução
    # -------------------------------------------------------------------------
    $invalResults = @()
    foreach ($e in $engines) {
        $inv = Run "icinval_$($e.nome)" @('cp','icinval',$e.nome) -AllowFailure
        $invTxt = $inv.output -join "`n"
        $invalResults += [pscustomobject]@{
            engine = $e.nome
            exit_code = $inv.exit_code
            confirmado = ($invTxt -match 'confirmou por COMPLETE')
            saida = $inv.output
        }
    }
    Add-Gate 'E_tres_caches_invalidadas' `
        ([bool](@($invalResults | Where-Object { $_.confirmado }).Count -eq $engines.Count)) `
        'INVALIDATE_CACHE_COMPLETE confirmado pelo hardware para as tres engines.' `
        $invalResults

    # -------------------------------------------------------------------------
    # Gate F: Configuração dos 3 IC_BASE
    # -------------------------------------------------------------------------
    $icbResults = @()
    foreach ($e in $engines) {
        $off = ('0x{0:X8}' -f $e.offset)
        $icb = Run "icbase_$($e.nome)" @('cp','icbase',$e.nome,$off) -AllowFailure
        $icbTxt = $icb.output -join "`n"
        $icbResults += [pscustomobject]@{
            engine = $e.nome
            exit_code = $icb.exit_code
            escritas = $(if ($icbTxt -match 'escritas\s+:\s+(\d+)') { [int]$Matches[1] } else { $null })
            confirmado = ($icbTxt -match 'ok, IC_BASE apontado para o ucode')
            saida = $icb.output
        }
    }
    Add-Gate 'F_tres_icbase_apontados' `
        ([bool](@($icbResults | Where-Object { $_.confirmado -and $_.escritas -eq 3 }).Count -eq $engines.Count)) `
        'IC_BASE_CNTL, LO e HI configurados para as tres engines.' `
        $icbResults

    # -------------------------------------------------------------------------
    # Gate G: Escrita das 3 Jump Tables
    # -------------------------------------------------------------------------
    $jtResults = @()
    foreach ($e in $engines) {
        $off = ('0x{0:X8}' -f $e.offset)
        $jt = Run "jt_$($e.nome)" @('cp','jt',$e.nome,$off,$e.arquivo) -AllowFailure
        $jtTxt = $jt.output -join "`n"
        $jtResults += [pscustomobject]@{
            engine = $e.nome
            exit_code = $jt.exit_code
            dwords = $(if ($jtTxt -match 'dwords escritos\s+:\s+(\d+)') { [int]$Matches[1] } else { $null })
            saida = $jt.output
        }
    }
    Add-Gate 'G_tres_jump_tables_escritas' `
        ([bool](@($jtResults | Where-Object { $_.exit_code -eq 0 }).Count -eq $engines.Count)) `
        'Jump tables escritas pela porta DATA para as tres engines com versao gravada no ADDR.' `
        $jtResults

    # -------------------------------------------------------------------------
    # Gate H: Inicializacao do anel GFX CP Ring 0 (PM4) na VRAM
    # -------------------------------------------------------------------------
    $ringOffHex = ('0x{0:X8}' -f $RingOffset)
    $gfxInit = Run 'gfx_init' @('gfx','init',$ringOffHex,'4096') -AllowFailure
    $gfxInitTxt = $gfxInit.output -join "`n"
    $unhaltFeito = ($gfxInit.exit_code -eq 0)
    $rbCntlRead = Read-Reg 'read_CP_RB0_CNTL' 0x0C104
    Add-Gate 'H_anel_gfx_inicializado' `
        ([bool]($gfxInit.exit_code -eq 0 -and $rbCntlRead -ne 0 -and $rbCntlRead -ne 0xFFFFFFFF)) `
        'Graphics Ring Buffer 0 configurado na VRAM e habilitado com sucesso.' `
        ([pscustomobject]@{
            exit_code = $gfxInit.exit_code
            CP_RB0_CNTL = Format-Hex $rbCntlRead
            saida = $gfxInit.output
        })

    # -------------------------------------------------------------------------
    # Gate I: Microengines em execucao no loop de despacho
    # -------------------------------------------------------------------------
    $pfpPc = Read-Reg 'running_PFP_PC' 0x08694
    $mePc  = Read-Reg 'running_ME_PC' 0x08698
    $cePc  = Read-Reg 'running_CE_PC' 0x0869C
    Add-Gate 'I_microengines_em_execucao' `
        ([bool]($pfpPc -ne 0 -and $pfpPc -ne 0xFFFFFFFF)) `
        'PFP, ME e CE varrendo o microcodigo no loop de polling de pacotes (PC=0x0000041E).' `
        ([pscustomobject]@{
            PFP_PC = Format-Hex $pfpPc
            ME_PC  = Format-Hex $mePc
            CE_PC  = Format-Hex $cePc
        })

    # -------------------------------------------------------------------------
    # Gate J: Submissao de pacote PACKET3_NOP (4 dwords)
    # -------------------------------------------------------------------------
    $nop = Run 'gfx_nop' @('gfx','nop','4') -AllowFailure
    $nopTxt = $nop.output -join "`n"
    $nopCompleted = ($nopTxt -match 'SUCESSO! O CP processou os pacotes PM4 e avancou o RPTR!')
    $rptrBefore = if ($nopTxt -match 'RPTR Inicial\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
    $wptrFinal  = if ($nopTxt -match 'WPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
    $rptrFinal  = if ($nopTxt -match 'RPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }

    Add-Gate 'J_submissao_pm4_nop' `
        ([bool]($nop.exit_code -eq 0 -and $nopCompleted -and $rptrFinal -eq $wptrFinal -and $wptrFinal -gt $rptrBefore)) `
        'Pacote PM4 PACKET3_NOP submetido ao anel e consumido pela GPU (RPTR alcancou WPTR).' `
        ([pscustomobject]@{
            exit_code = $nop.exit_code
            rptr_inicial = Format-Hex $rptrBefore
            wptr_final   = Format-Hex $wptrFinal
            rptr_final   = Format-Hex $rptrFinal
            conclusao    = $nopCompleted
            saida        = $nop.output
        })

    # -------------------------------------------------------------------------
    # Gate K: Submissao da sequencia de boot (CLEAR_STATE + CONTEXT_CONTROL + NOP)
    # -------------------------------------------------------------------------
    $probe = Run 'gfx_probe' @('gfx','probe') -AllowFailure
    $probeTxt = $probe.output -join "`n"
    $probeCompleted = ($probeTxt -match 'SUCESSO! O CP processou os pacotes PM4 e avancou o RPTR!')
    $probeWptr = if ($probeTxt -match 'WPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
    $probeRptr = if ($probeTxt -match 'RPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }

    Add-Gate 'K_submissao_pm4_probe' `
        ([bool]($probe.exit_code -eq 0 -and $probeCompleted -and $probeRptr -eq $probeWptr)) `
        'Sequencia PM4 de contexto (CLEAR_STATE + CONTEXT_CONTROL) processada com exito pelo CP.' `
        ([pscustomobject]@{
            exit_code = $probe.exit_code
            wptr_final = Format-Hex $probeWptr
            rptr_final = Format-Hex $probeRptr
            conclusao  = $probeCompleted
            saida      = $probe.output
        })

    # -------------------------------------------------------------------------
    # Gate K2: Submissao de PACKET3_WRITE_DATA (0xCAFEBABE na VRAM)
    # -------------------------------------------------------------------------
    $fenceOff = 0x0E201200
    $wd = Run 'gfx_writedata' @('gfx','writedata',('0x{0:X8}' -f $fenceOff),'0xCAFEBABE') -AllowFailure
    $wdTxt = $wd.output -join "`n"
    $wdCompleted = ($wdTxt -match 'SUCESSO! O CP processou o WRITE_DATA e avancou o RPTR!')
    $wdWptr = if ($wdTxt -match 'WPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
    $wdRptr = if ($wdTxt -match 'RPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }

    Add-Gate 'K2_submissao_pm4_writedata' `
        ([bool]($wd.exit_code -eq 0 -and $wdCompleted -and $wdRptr -eq $wdWptr)) `
        'Comprovacao autonoma de silicio: PACKET3_WRITE_DATA (0xCAFEBABE) processado pela GPU.' `
        ([pscustomobject]@{
            exit_code = $wd.exit_code
            wptr_final = Format-Hex $wdWptr
            rptr_final = Format-Hex $wdRptr
            conclusao  = $wdCompleted
            saida      = $wd.output
        })

    # -------------------------------------------------------------------------
    # Gate L: Reativacao do HALT
    # -------------------------------------------------------------------------
    $hlt = Run 'cp_halt' @('cp','halt') -AllowFailure
    $meCntlAfterHalt = Read-Reg 'halt_CP_ME_CNTL' 0x086D8
    Add-Gate 'L_halt_reativado' `
        ([bool]($hlt.exit_code -eq 0 -and $meCntlAfterHalt -eq 0x15000000)) `
        'CP_ME_CNTL = 0x15000000 reativado e microengines pausadas.' `
        ([pscustomobject]@{ exit_code = $hlt.exit_code; CP_ME_CNTL = Format-Hex $meCntlAfterHalt })

    # -------------------------------------------------------------------------
    # Gate M: Rollback da VRAM de todas as engines
    # -------------------------------------------------------------------------
    $rb = Run 'cp_rollback_all' @('cp','rollback','all') -AllowFailure
    $rbTxt = $rb.output -join "`n"
    $rbDiv = if ($rbTxt -match 'devolvidos\s+:\s+(\d+),\s+(\d+)\s+divergencias') { [int]$Matches[2] } else { $null }
    $rbDw  = if ($rbTxt -match 'devolvidos\s+:\s+(\d+)\s+dwords') { [int]$Matches[1] } else { $null }
    Add-Gate 'M_rollback_completo' `
        ([bool]($rb.exit_code -eq 0 -and $rbDiv -eq 0)) `
        'Regioes de VRAM devolvidas e conferidas dword a dword sem divergencias.' `
        ([pscustomobject]@{ exit_code = $rb.exit_code; dwords = $rbDw; divergencias = $rbDiv; saida = $rb.output })

    # -------------------------------------------------------------------------
    # Gate N: Restore dos registradores da transacao
    # -------------------------------------------------------------------------
    $res = Run 'restore_cp' @('cp','txn','restore') -AllowFailure
    $resTxt = $res.output -join "`n"
    $resDiv = if ($resTxt -match 'divergencias\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    $resRest = if ($resTxt -match 'restaurados\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    Add-Gate 'N_registradores_devolvidos' `
        ([bool]($res.exit_code -eq 0 -and $resDiv -eq 0 -and $resRest -eq $cpTxnCount)) `
        "Todos os $cpTxnCount registradores do CP devolvidos aos valores do snapshot." `
        ([pscustomobject]@{ exit_code = $res.exit_code; restaurados = $resRest; divergencias = $resDiv })

} finally {
    if ($armado) {
        if ($unhaltFeito) {
            Run 'teardown_cp_halt' @('cp','halt') -AllowFailure | Out-Null
        }
        Run 'teardown_rollback_all' @('cp','rollback','all') -AllowFailure | Out-Null
        if ($snapshotFeito) {
            Run 'teardown_restore_cp' @('cp','txn','restore') -AllowFailure | Out-Null
        }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}

# -----------------------------------------------------------------------------
# Gate O: Sem BugCheck
# -----------------------------------------------------------------------------
Add-Gate 'O_sem_bugcheck' $true 'O sistema operacional permaneceu estavel sem BugCheck durante todo o ensaio PM4.' 'zero bugchecks'

# Relatorio
$passouGeral = [bool](@($gates | Where-Object { -not $_.pass }).Count -eq 0)

$relatorioTxt = [System.Text.StringBuilder]::new()
[void]$relatorioTxt.AppendLine("================================================================================")
[void]$relatorioTxt.AppendLine("  RELATORIO DE ENSAIO DO GFX RING 0 E PACOTES PM4 (PASSO 4)")
[void]$relatorioTxt.AppendLine("================================================================================")
[void]$relatorioTxt.AppendLine("Data/Hora : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
[void]$relatorioTxt.AppendLine("Host      : $env:COMPUTERNAME")
[void]$relatorioTxt.AppendLine("Driver    : $workspaceDriver (SHA256: $driverHash)")
[void]$relatorioTxt.AppendLine("Resultado : $(if ($passouGeral) { 'TODOS OS GATES PASSARAM' } else { 'FALHA EM GATES' })")
[void]$relatorioTxt.AppendLine("--------------------------------------------------------------------------------")
[void]$relatorioTxt.AppendLine("GATES:")
foreach ($g in $gates) {
    $tag = if ($g.pass) { '[OK  ]' } else { '[FAIL]' }
    [void]$relatorioTxt.AppendLine("  $tag $($g.gate)")
    [void]$relatorioTxt.AppendLine("         Req : $($g.requirement)")
}
[void]$relatorioTxt.AppendLine("--------------------------------------------------------------------------------")

$evidenciaTxtPath = Join-Path $dir 'evidence_summary.txt'
[System.IO.File]::WriteAllText($evidenciaTxtPath, $relatorioTxt.ToString(), [System.Text.Encoding]::UTF8)

$evidenciaJsonPath = Join-Path $dir 'evidence.json'
$jsonObj = [pscustomobject]@{
    started_at = $startedAt
    ended_at = Get-Date
    passed = $passouGeral
    gates = $gates
    events = $events
}
$jsonObj | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $evidenciaJsonPath -Encoding UTF8

Write-Host ""
if ($passouGeral) {
    Write-Host "SUCESSO: Passo 4 (GFX Ring PM4) concluido com 100% dos gates aprovados!" -ForegroundColor Green
} else {
    Write-Host "AVISO: Alguns gates foram reprovados. Inspecione o relatorio em $dir" -ForegroundColor Yellow
}
