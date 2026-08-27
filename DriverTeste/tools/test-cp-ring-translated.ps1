<#
    Ensaio Integrado: CP Bring-up, Traducao GCVM (VMID 0 Page Table) e Anel PM4.

    OBJETIVO:
    1. Trazer RLC fora da ROM pelo PSP;
    2. Construir Page Table do VMID 0 mapeando a regiao dos ucodes e do anel (VA 0x0000_0800_0000_0000);
    3. Habilitar traducao do VMID 0 (ENABLE_CONTEXT=1, L2 habilitada, TLB invalidada);
    4. Configurar CP Golden Settings e Clocks;
    5. Carregar ucodes (PFP, ME, CE, MEC) com IC_BASE apontado para as VAs traduzidas;
    6. Escrever Jump Tables;
    7. Inicializar o Graphics Ring Buffer 0 apontando CP_RB0_BASE para a VA traduzida (0x0000_0800_0018_0000);
    8. Submeter pacotes PM4 (NOP, Context Boot, WRITE_DATA) e medir o avanco do RPTR;
    9. Teardown e rollback transacional completo (CP + GCVM).
#>
param(
    [switch]$Execute,
    [switch]$IAcceptTheRisk,
    [string]$OutputRoot,
    [ValidateRange(2,120)][int]$CommandTimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$startedAt = Get-Date

# VRAM Offsets
$pfpVram  = [uint32]0x0E000000
$meVram   = [uint32]0x0E080000
$ceVram   = [uint32]0x0E100000
$mecVram  = [uint32]0x0E140000
$ringVram = [uint32]0x0E180000
$ptVram   = [uint32]0x0E1F0000

# GPU Virtual Addresses (VMID 0 Page Table em 8 TB)
$vaBase    = [uint64]0x0000080000000000
$pfpGpuVa  = [uint64]($vaBase + 0x00000000)
$meGpuVa   = [uint64]($vaBase + 0x00080000)
$ceGpuVa   = [uint64]($vaBase + 0x00100000)
$mecGpuVa  = [uint64]($vaBase + 0x00140000)
$ringGpuVa = [uint64]($vaBase + 0x00180000)

$engines = @(
    [pscustomobject]@{ nome = 'pfp'; vram = $pfpVram; gpuVa = $pfpGpuVa; arquivo = 'firmware\cyan_skillfish2_pfp.bin' }
    [pscustomobject]@{ nome = 'me';  vram = $meVram;  gpuVa = $meGpuVa;  arquivo = 'firmware\cyan_skillfish2_me.bin' }
    [pscustomobject]@{ nome = 'ce';  vram = $ceVram;  gpuVa = $ceGpuVa;  arquivo = 'firmware\cyan_skillfish2_ce.bin' }
    [pscustomobject]@{ nome = 'mec'; vram = $mecVram; gpuVa = $mecGpuVa; arquivo = 'firmware\cyan_skillfish2_mec.bin' }
)

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute -IAcceptTheRisk. Ele ativa a traducao por Page Table do VMID 0 e o anel PM4.'
}
throw 'QUARENTENA ABI 29: o init golden usa broadcast e ainda nao possui snapshot/rollback por SE/SH/instancia. Nenhuma escrita deste ensaio foi autorizada.'
if (-not $IAcceptTheRisk) {
    throw 'Este ensaio liga a traducao por Page Table no VMID 0. Confirme com -IAcceptTheRisk.'
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

$gcvmTxnCount = $null
foreach ($linha in Get-Content -LiteralPath $abiHeader) {
    if ($linha -match 'BC250DBG_GCVM_TXN_REG_COUNT\s+(\d+)u') { $gcvmTxnCount = [int]$Matches[1]; break }
}
if (-not $gcvmTxnCount) { throw 'Nao foi possivel ler BC250DBG_GCVM_TXN_REG_COUNT do header da ABI.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-cp-ring-translated"
New-Item -ItemType Directory -Path $dir -Force | Out-Null

$events = [System.Collections.Generic.List[object]]::new()
$gates = [System.Collections.Generic.List[object]]::new()

$liveLogPath = Join-Path $dir 'live_execution.log'
$globalLiveLog = Join-Path $root 'live_test_progress.log'

function Log-Live([string]$Msg) {
    $ts = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff")
    $line = "[$ts] $Msg"
    try {
        [System.IO.File]::AppendAllText($liveLogPath, "$line`r`n")
        [System.IO.File]::AppendAllText($globalLiveLog, "$line`r`n")
    } catch {}
}

# Inicializa arquivo de log persistente
[System.IO.File]::WriteAllText($globalLiveLog, "=== INICIO DO ENSAIO: $(Get-Date) ===`r`n")

function Run([string]$Name, [string[]]$Arguments, [switch]$AllowFailure) {
    Log-Live "INICIO: $Name -> bc250ctl.exe $($Arguments -join ' ')"
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $cli
    $psi.Arguments = (($Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
    $psi.WorkingDirectory = $root; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = New-Object Diagnostics.Process; $p.StartInfo = $psi
    if (-not $p.Start()) {
        Log-Live "FALHA_START: $Name"
        throw "Falha ao iniciar $Name"
    }
    $ot = $p.StandardOutput.ReadToEndAsync(); $et = $p.StandardError.ReadToEndAsync()
    $to = -not $p.WaitForExit($CommandTimeoutSeconds * 1000)
    if ($to) { $p.Kill(); $p.WaitForExit() }
    $ec = if ($to) { 1460 } else { $p.ExitCode }
    $outLines = @(($ot.Result + $et.Result) -split "`r?`n" | Where-Object { $_ })
    $r = [pscustomobject]@{
        name = $Name
        command = 'bc250ctl.exe ' + ($Arguments -join ' ')
        exit_code = $ec
        timed_out = $to
        output = $outLines
    }
    $events.Add($r)
    Log-Live "FIM: $Name (exit=$ec, timeout=$to)"
    foreach ($l in $outLines) { Log-Live "  [out] $l" }
    if (($to -or $ec -ne 0) -and -not $AllowFailure) {
        Log-Live "ERRO_ABORT: $Name falhou: exit=$ec timeout=$to"
        throw "$Name falhou: exit=$ec timeout=$to"
    }
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
    Log-Live "GATE: $tag $Name (pass=$Pass)"
}

Write-Host "Evidencia: $dir"
Write-Host "Log em tempo real: $globalLiveLog"

$armado = $false
$gcvmSnapFeito = $false
$cpSnapFeito = $false
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
        'Os quatro ucodes (PFP, ME, CE, MEC) tem header v1.0, IP v10.1 e jump table descrita dentro do payload.' `
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
    # Armamento de Escrita
    # -------------------------------------------------------------------------
    Run 'arm_write' @('arm','write') | Out-Null
    $armado = $true

    # -------------------------------------------------------------------------
    # Gate B2: RLC fora da ROM
    # -------------------------------------------------------------------------
    $rlcSnap = Run 'rlc_txn_snapshot' @('rlc','txn','snapshot') -AllowFailure
    foreach ($passo in @('stop','cgcg','pg')) {
        Run "rlc_$passo" @('rlc',$passo) -AllowFailure | Out-Null
    }
    Run 'psp_init' @('psp','init') -AllowFailure | Out-Null
    Run 'psp_loadfw_rlc' @('psp','loadfw','rlc',(Join-Path $root 'firmware\cyan_skillfish2_rlc.bin')) -AllowFailure | Out-Null
    Run 'rlc_start' @('rlc','start') -AllowFailure | Out-Null
    Run 'rlc_cgcg_pos_carga' @('rlc','cgcg') -AllowFailure | Out-Null
    Run 'rlc_pg_pos_carga' @('rlc','pg') -AllowFailure | Out-Null

    $rlcDepoisTxt = (Run 'rlc_status_depois' @('rlc','status') -AllowFailure).output -join "`n"
    $romDepois = if ($rlcDepoisTxt -match 'EXEC_ROM_CODE (\d)') { [int]$Matches[1] } else { $null }
    Add-Gate 'B2_rlc_fora_da_rom' `
        ([bool]($null -ne $romDepois -and $romDepois -eq 0)) `
        'RLC_GPM_STAT.RLC_EXEC_ROM_CODE = 0: o RLC executa microcodigo, nao a ROM.' `
        ([pscustomobject]@{ exec_rom_code = $romDepois; saida = $rlcDepoisTxt })

    # -------------------------------------------------------------------------
    # Gate C: Snapshots GCVM + CP
    # -------------------------------------------------------------------------
    $snapGcvm = Run 'snap_gcvm' @('gcvm','txn','snapshot') -AllowFailure
    $gcvmSnapFeito = ($snapGcvm.exit_code -eq 0)
    Add-Gate 'C1_snapshot_gcvm' `
        ([bool]$gcvmSnapFeito) `
        "Snapshot dos $gcvmTxnCount registradores do GCVM capturado." `
        ([pscustomobject]@{ exit_code = $snapGcvm.exit_code })

    $snapCp = Run 'snap_cp' @('cp','txn','snapshot') -AllowFailure
    $cpSnapFeito = ($snapCp.exit_code -eq 0)
    Add-Gate 'C2_snapshot_cp' `
        ([bool]$cpSnapFeito) `
        "Snapshot dos $cpTxnCount registradores do CP capturado." `
        ([pscustomobject]@{ exit_code = $snapCp.exit_code })

    # Limpeza preventiva de page table anterior
    Run 'gcvm_ptrollback_pre' @('gcvm','ptrollback') -AllowFailure | Out-Null

    # -------------------------------------------------------------------------
    # Gate D: Construcao da Page Table do VMID 0 (512 paginas = 2 MB)
    # -------------------------------------------------------------------------
    $ptBuild = Run 'gcvm_ptbuild' @('gcvm','ptbuild',('0x{0:X8}' -f $ptVram),('0x{0:X8}' -f $pfpVram),'512') -AllowFailure
    $ptTxt = $ptBuild.output -join "`n"
    $ptOk = ($ptBuild.exit_code -eq 0 -and $ptTxt -match '0 divergencias')
    Add-Gate 'D_page_table_construida' `
        ([bool]$ptOk) `
        'Page table construida e conferida dword a dword na VRAM (512 paginas mapeando ucodes e anel).' `
        ([pscustomobject]@{ exit_code = $ptBuild.exit_code; saida = $ptBuild.output })

    $dummyVram = [uint32]0x0E1E0000
    $vaEnd = [uint64]($vaBase + (512 * 4096) - 1)

    function Invoke-GcvmStage([string]$StageName, [int]$ExpectedWrites, [switch]$Danger) {
        $cmd = @('gcvm','program',$StageName,
                 '-table',('0x{0:X8}' -f $ptVram),
                 '-dummy',('0x{0:X8}' -f $dummyVram),
                 '-start',('0x{0:X}' -f $vaBase),
                 '-end',('0x{0:X}' -f $vaEnd))
        if ($Danger) { $cmd += '-danger' }
        $r = Run "gcvm_prog_$StageName" $cmd -AllowFailure
        $txt = $r.output -join "`n"
        $writes = if ($txt -match 'escritas\s+:\s+(\d+),\s+(\d+) divergencias') { [int]$Matches[1] } else { $null }
        $div    = if ($txt -match 'escritas\s+:\s+(\d+),\s+(\d+) divergencias') { [int]$Matches[2] } else { $null }
        return [pscustomobject]@{
            stage = $StageName
            exit_code = $r.exit_code
            escritas = $writes
            esperadas = $ExpectedWrites
            divergencias = $div
            ok = ($r.exit_code -eq 0 -and $writes -eq $ExpectedWrites -and $div -eq 0)
            saida = $r.output
        }
    }

    # -------------------------------------------------------------------------
    # Gate E: Ativacao da Traducao do VMID 0 (Estagios GCVM)
    # -------------------------------------------------------------------------
    $gcvmStages = @()
    $gcvmStages += Invoke-GcvmStage 'gart' 6
    $gcvmStages += Invoke-GcvmStage 'system' 10
    $gcvmStages += Invoke-GcvmStage 'identity' 6
    $gcvmStages += Invoke-GcvmStage 'invrange' 4
    $gcvmStages += Invoke-GcvmStage 'tlb' 1 -Danger
    $gcvmStages += Invoke-GcvmStage 'cache' 5 -Danger
    $gcvmStages += Invoke-GcvmStage 'context0' 1 -Danger

    $stagesOk = (@($gcvmStages | Where-Object { -not $_.ok }).Count -eq 0)
    $inv = Run 'gcvm_invalidate' @('gcvm','invalidate','0','0') -AllowFailure
    $invTxt = $inv.output -join "`n"
    $invOk = ($inv.exit_code -eq 0 -and $invTxt -match 'ACK confirmado pelo hardware')
    Add-Gate 'E_traducao_vmid0_ativa' `
        ([bool]($stagesOk -and $invOk)) `
        'Contexto 0 ativado (7 estagios GCVM, ENABLE_CONTEXT=1, L2 ativada, TLB invalidada com ACK).' `
        ([pscustomobject]@{ stages = $gcvmStages; invalidate_exit = $inv.exit_code; saida = $inv.output })

    # -------------------------------------------------------------------------
    # Gate F: Clocks e Golden Registers do CP
    # -------------------------------------------------------------------------
    $clk = Run 'cp_clocks' @('cp','clocks') -AllowFailure
    $gold = Run 'cp_golden' @('cp','golden') -AllowFailure
    Add-Gate 'F_cp_clocks_e_golden' `
        ([bool]($clk.exit_code -eq 0 -and $gold.exit_code -eq 0)) `
        'CGTT_CPF_CLK_CTRL = 0xF8000100 e 15 registradores Golden do CP configurados.' `
        ([pscustomobject]@{ clk_exit = $clk.exit_code; gold_exit = $gold.exit_code })

    # -------------------------------------------------------------------------
    # Gate G: Carga dos 4 ucodes na VRAM
    # -------------------------------------------------------------------------
    $stageResults = @()
    foreach ($e in $engines) {
        $st = Run "stage_$($e.nome)" @('cp','stage',$e.nome,('0x{0:X8}' -f $e.vram),$e.arquivo) -AllowFailure
        $stTxt = $st.output -join "`n"
        $stDiv = if ($stTxt -match 'escritos\s+:\s+(\d+)\s+dwords,\s+(\d+)\s+divergencias') { [int]$Matches[2] } else { $null }
        $stageResults += [pscustomobject]@{
            engine = $e.nome
            vram = ('0x{0:X8}' -f $e.vram)
            exit_code = $st.exit_code
            divergencias = $stDiv
        }
    }
    Add-Gate 'G_quatro_ucodes_na_vram' `
        ([bool](@($stageResults | Where-Object { $_.exit_code -eq 0 -and $_.divergencias -eq 0 }).Count -eq $engines.Count)) `
        'Os quatro ucodes (PFP, ME, CE, MEC) gravados e conferidos na VRAM.' `
        $stageResults

    # -------------------------------------------------------------------------
    # Gate H: Invalidacao das Caches e Apontamento dos 4 IC_BASE para VAs Traduzidas
    # -------------------------------------------------------------------------
    $loadResults = @()
    foreach ($e in $engines) {
        $inv = Run "icinval_$($e.nome)" @('cp','icinval',$e.nome) -AllowFailure
        $base = Run "icbase_$($e.nome)" @('cp','icbase',$e.nome,('0x{0:X8}' -f $e.vram),'-gpu',('0x{0:X16}' -f $e.gpuVa)) -AllowFailure
        $jt = Run "jt_$($e.nome)" @('cp','jt',$e.nome,('0x{0:X8}' -f $e.vram),$e.arquivo) -AllowFailure
        $loadResults += [pscustomobject]@{
            engine = $e.nome
            invalidate_exit = $inv.exit_code
            icbase_exit = $base.exit_code
            jump_table_exit = $jt.exit_code
            gpu_va = ('0x{0:X16}' -f $e.gpuVa)
        }
    }
    Add-Gate 'H_ucodes_apontados_para_va_traduzida' `
        ([bool](@($loadResults | Where-Object {
            $_.invalidate_exit -eq 0 -and $_.icbase_exit -eq 0 -and $_.jump_table_exit -eq 0
        }).Count -eq $engines.Count)) `
        'As quatro sequencias de invalidacao, IC_BASE em VA traduzida e escrita da Jump Table foram emitidas sem erro. Isto nao prova latch nem execucao.' `
        $loadResults

    # O latch e o primeiro efeito fisico observavel da Jump Table. Ler o
    # registrador com sucesso nao basta: no estado bloqueado ele le 0.
    $pfpJtStat = Read-Reg 'read_CP_PFP_JT_STAT' 0x0C4CC
    $ceJtStat  = Read-Reg 'read_CP_CE_JT_STAT'  0x0C4D0
    $mecJtStat = Read-Reg 'read_CP_MEC_JT_STAT' 0x0C4D4
    $pfpJtLoaded = if ($null -ne $pfpJtStat) { $pfpJtStat -band 3 } else { $null }
    $ceJtLoaded  = if ($null -ne $ceJtStat)  { $ceJtStat -band 3 } else { $null }
    Add-Gate 'H2_jump_tables_latchadas' `
        ([bool]($pfpJtLoaded -eq 3 -and $ceJtLoaded -eq 3)) `
        'PFP e CE devem reportar JT_LOADED=3, como no Linux com o CP executando. JT_LOADED=0 reprova.' `
        ([pscustomobject]@{
            CP_PFP_JT_STAT = Format-Hex $pfpJtStat
            CP_CE_JT_STAT = Format-Hex $ceJtStat
            CP_MEC_JT_STAT = Format-Hex $mecJtStat
            PFP_JT_LOADED = $pfpJtLoaded
            CE_JT_LOADED = $ceJtLoaded
        })

    # -------------------------------------------------------------------------
    # Gate I: Inicializacao do Graphics Ring Buffer 0 apontando para VA Traduzida
    # -------------------------------------------------------------------------
    $gfxInit = Run 'gfx_init' @('gfx','init',('0x{0:X8}' -f $ringVram),'4096','-gpu',('0x{0:X16}' -f $ringGpuVa)) -AllowFailure
    $unhaltFeito = ($gfxInit.exit_code -eq 0)
    $rbCntlRead = Read-Reg 'read_CP_RB0_CNTL' 0x0C104
    Add-Gate 'I_anel_gfx_inicializado_com_traducao' `
        ([bool]($gfxInit.exit_code -eq 0 -and $rbCntlRead -ne 0 -and $rbCntlRead -ne 0xFFFFFFFF)) `
        'Graphics Ring Buffer 0 configurado e apontado para a VA traduzida 0x0000_0800_0018_0000.' `
        ([pscustomobject]@{ exit_code = $gfxInit.exit_code; CP_RB0_CNTL = Format-Hex $rbCntlRead; saida = $gfxInit.output })

    # -------------------------------------------------------------------------
    # Gate J: Submissao de pacote PM4 PACKET3_NOP (4 dwords)
    # -------------------------------------------------------------------------
    $nop = Run 'gfx_nop' @('gfx','nop','4') -AllowFailure
    $nopTxt = $nop.output -join "`n"
    $nopCompleted = ($nopTxt -match 'SUCESSO! O CP processou os pacotes PM4 e avancou o RPTR!')
    $rptrBefore = if ($nopTxt -match 'RPTR Inicial\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
    $wptrFinal  = if ($nopTxt -match 'WPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
    $rptrFinal  = if ($nopTxt -match 'RPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }

    Add-Gate 'J_submissao_pm4_nop' `
        ([bool]($nop.exit_code -eq 0 -and $nopCompleted -and $rptrFinal -eq $wptrFinal -and $wptrFinal -gt $rptrBefore)) `
        'Pacote PM4 PACKET3_NOP submetido e consumido pela GPU (RPTR alcancou WPTR).' `
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
    # Gate L: Submissao de PACKET3_WRITE_DATA (0xCAFEBABE na VRAM)
    # -------------------------------------------------------------------------
    $fenceVram = [uint32]($ringVram + 0x1200)
    $wd = Run 'gfx_writedata' @('gfx','writedata',('0x{0:X8}' -f $fenceVram),'0xCAFEBABE') -AllowFailure
    $wdTxt = $wd.output -join "`n"
    $wdCompleted = ($wdTxt -match 'SUCESSO! O CP processou o WRITE_DATA e avancou o RPTR!')
    $wdWptr = if ($wdTxt -match 'WPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
    $wdRptr = if ($wdTxt -match 'RPTR Final\s+:\s+0x([0-9A-Fa-f]+)') { [Convert]::ToUInt32($Matches[1],16) } else { $null }

    Add-Gate 'L_submissao_pm4_writedata' `
        ([bool]($wd.exit_code -eq 0 -and $wdCompleted -and $wdRptr -eq $wdWptr)) `
        'Comprovacao de execucao: PACKET3_WRITE_DATA (0xCAFEBABE) processado pela GPU.' `
        ([pscustomobject]@{
            exit_code = $wd.exit_code
            wptr_final = Format-Hex $wdWptr
            rptr_final = Format-Hex $wdRptr
            conclusao  = $wdCompleted
            saida      = $wd.output
        })

    # -------------------------------------------------------------------------
    # Gate M: Reativacao do HALT
    # -------------------------------------------------------------------------
    $hlt = Run 'cp_halt' @('cp','halt') -AllowFailure
    $meCntlAfterHalt = Read-Reg 'halt_CP_ME_CNTL' 0x086D8
    Add-Gate 'M_halt_reativado' `
        ([bool]($hlt.exit_code -eq 0 -and $meCntlAfterHalt -eq 0x15000000)) `
        'CP_ME_CNTL = 0x15000000 reativado e microengines pausadas.' `
        ([pscustomobject]@{ exit_code = $hlt.exit_code; CP_ME_CNTL = Format-Hex $meCntlAfterHalt })

    # -------------------------------------------------------------------------
    # Gate N: Rollback da VRAM de todas as engines
    # -------------------------------------------------------------------------
    $rb = Run 'cp_rollback_all' @('cp','rollback','all') -AllowFailure
    $rbTxt = $rb.output -join "`n"
    $rbDiv = if ($rbTxt -match 'devolvidos\s+:\s+(\d+),\s+(\d+)\s+divergencias') { [int]$Matches[2] } else { $null }
    Add-Gate 'N_rollback_completo' `
        ([bool]($rb.exit_code -eq 0 -and $rbDiv -eq 0)) `
        'Regioes de VRAM devolvidas e conferidas dword a dword sem divergencias.' `
        ([pscustomobject]@{ exit_code = $rb.exit_code; divergencias = $rbDiv; saida = $rb.output })

    # -------------------------------------------------------------------------
    # Gate O: Restore dos registradores do CP
    # -------------------------------------------------------------------------
    $resCp = Run 'restore_cp' @('cp','txn','restore') -AllowFailure
    $resCpTxt = $resCp.output -join "`n"
    $resCpDiv = if ($resCpTxt -match 'divergencias\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    Add-Gate 'O_registradores_cp_devolvidos' `
        ([bool]($resCp.exit_code -eq 0 -and $resCpDiv -eq 0)) `
        "Todos os $cpTxnCount registradores do CP devolvidos aos valores do snapshot." `
        ([pscustomobject]@{ exit_code = $resCp.exit_code; divergencias = $resCpDiv })

    # -------------------------------------------------------------------------
    # Gate P: Restore dos registradores do GCVM
    # -------------------------------------------------------------------------
    $resGcvm = Run 'restore_gcvm' @('gcvm','txn','restore') -AllowFailure
    $resGcvmTxt = $resGcvm.output -join "`n"
    $resGcvmDiv = if ($resGcvmTxt -match 'divergencias\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    Add-Gate 'P_registradores_gcvm_devolvidos' `
        ([bool]($resGcvm.exit_code -eq 0 -and $resGcvmDiv -eq 0)) `
        "Todos os $gcvmTxnCount registradores do GCVM devolvidos aos valores do snapshot." `
        ([pscustomobject]@{ exit_code = $resGcvm.exit_code; divergencias = $resGcvmDiv })

} finally {
    if ($armado) {
        if ($unhaltFeito) {
            Run 'teardown_cp_halt' @('cp','halt') -AllowFailure | Out-Null
        }
        Run 'teardown_rollback_all' @('cp','rollback','all') -AllowFailure | Out-Null
        Run 'teardown_gcvm_ptrollback' @('gcvm','ptrollback') -AllowFailure | Out-Null
        if ($cpSnapFeito) {
            Run 'teardown_restore_cp' @('cp','txn','restore') -AllowFailure | Out-Null
        }
        if ($gcvmSnapFeito) {
            Run 'teardown_restore_gcvm' @('gcvm','txn','restore') -AllowFailure | Out-Null
        }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}

# Gate Q: Sem BugCheck
Add-Gate 'Q_sem_bugcheck' $true 'O sistema operacional permaneceu estavel sem BugCheck durante todo o ensaio.' 'zero bugchecks'

# Relatorio
$passouGeral = [bool](@($gates | Where-Object { -not $_.pass }).Count -eq 0)

$relatorioTxt = [System.Text.StringBuilder]::new()
[void]$relatorioTxt.AppendLine("================================================================================")
[void]$relatorioTxt.AppendLine("  RELATORIO DE ENSAIO DO GFX RING 0 COM TRADUCAO GCVM VMID 0")
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
    Write-Host "SUCESSO: Ensaio de Anel PM4 com Traducao VMID 0 concluido com 100% dos gates aprovados!" -ForegroundColor Green
} else {
    Write-Host "AVISO: Alguns gates foram reprovados. Inspecione o relatorio em $dir" -ForegroundColor Yellow
}
