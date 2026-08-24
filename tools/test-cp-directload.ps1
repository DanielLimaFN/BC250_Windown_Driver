<#
    Frente do CP, passo 2: direct load do ucode, sem tirar nada do HALT.

    POR QUE DIRECT LOAD

    Medido em 2026-08-23 14:21: o SOS desta placa ACEITA o RLC e RECUSA as quatro
    microengines do CP com 0x80000203. Coerente com o upstream — psp_v11_0_8 nao
    tem load_ip_fw para o CP. O GFX10 carrega o CP grafico por direct load.

    O MECANISMO, DE gfx_v10_0_cp_gfx_load_pfp_microcode()

    A microengine busca o proprio ucode da memoria, pela cache de instrucoes:

      1. ucode em memoria alcancavel pela GPU, alinhado a 4 KB;
      2. IC_OP_CNTL.INVALIDATE_CACHE = 1, esperar INVALIDATE_CACHE_COMPLETE;
      3. IC_BASE_CNTL com VMID = 0, CACHE_POLICY = 0, EXE_DISABLE = 0,
         ADDRESS_CLAMP = 1;
      4. IC_BASE_LO/HI apontando para o ucode;
      5. jump table por UCODE_ADDR/DATA, terminando com a versao no ADDR.

    O QUE ESTE ENSAIO NAO FAZ

    Nao tira NENHUMA microengine do HALT. Medido no POST desta placa,
    CP_ME_CNTL = 0x15000000: CE_HALT (24), PFP_HALT (26) e ME_HALT (28) ligados.
    Enquanto isso valer, tudo aqui e configuracao inerte — a cache aponta para um
    lugar e ninguem busca.

    Por isso o gate mais importante e o negativo: os ponteiros de instrucao das
    quatro microengines tem de continuar imoveis do inicio ao fim. Se algum se
    mover, alguma coisa comecou a executar sem ser liberada, e isso precisa de
    explicacao antes de qualquer proximo passo.

    ENDERECOS NO SEGMENTO 1

    Todo o caminho mora no segmento 1 do MMIO, cuja base foi conferida contra
    registrador vivo em 2026-08-23 17:54. Antes disso era derivacao, e escrever
    IC_BASE sobre derivacao seria escrever MMIO por suposicao.

    Este ensaio NAO carrega firmware pelo PSP e NAO contamina a sessao: o ucode
    volta da VRAM e os dez registradores voltam do snapshot.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,120)][int]$CommandTimeoutSeconds = 60,
    [ValidateRange(2,64)][int]$Samples = 6,
    [uint32]$UcodeOffset = 0x0E000000
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
    [pscustomobject]@{ nome = 'pfp'; arquivo = 'firmware\cyan_skillfish2_pfp.bin' }
    [pscustomobject]@{ nome = 'me';  arquivo = 'firmware\cyan_skillfish2_me.bin' }
    [pscustomobject]@{ nome = 'ce';  arquivo = 'firmware\cyan_skillfish2_ce.bin' }
)

# Ponteiros de instrucao, segmento 0. O instrumento honesto de execucao do CP.
$pntrs = [ordered]@{
    'CP_PFP_INSTR_PNTR'  = 0x08694
    'CP_ME_INSTR_PNTR'   = 0x08698
    'CP_CE_INSTR_PNTR'   = 0x0869C
    'CP_MEC1_INSTR_PNTR' = 0x086A0
}

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute. Ele NAO carrega firmware pelo PSP e NAO contamina a sessao.'
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
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-cp-directload"
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
# Valor unico nao distingue parado de em movimento. Amostrar e obrigatorio.
function Read-Pntrs([string]$Rotulo) {
    $amostras = @()
    for ($s = 0; $s -lt $Samples; $s++) {
        $linha = [ordered]@{}
        foreach ($k in $pntrs.Keys) { $linha[$k] = Read-Reg "${Rotulo}_${k}_$s" ([uint32]$pntrs[$k]) }
        $amostras += [pscustomobject]$linha
    }
    $distintos = [ordered]@{}
    foreach ($k in $pntrs.Keys) {
        $vals = @($amostras | ForEach-Object { $_.$k } | Where-Object { $null -ne $_ })
        $distintos[$k] = ($vals | Sort-Object -Unique).Count
    }
    return [pscustomobject]@{
        primeira = [pscustomobject](& { $o = [ordered]@{}; foreach ($k in $pntrs.Keys) { $o[$k] = Format-Hex $amostras[0].$k }; $o })
        valores_distintos = [pscustomobject]$distintos
        algum_em_movimento = [bool](@($distintos.Keys | Where-Object { $distintos[$_] -gt 1 }).Count)
    }
}
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}
function Get-MeCntl([object]$Resultado) {
    foreach ($linha in $Resultado.output) {
        if ($linha -match 'CP_ME_CNTL\s+:\s+0x([0-9A-Fa-f]{8})') { return [Convert]::ToUInt32($Matches[1],16) }
    }
    return $null
}

$failure = $null
$armed = $false; $snapshotTaken = $false
$staged = @{}; $resultados = @()
$meCntlInicial = $null; $pntrInicial = $null; $pntrFinal = $null

try {
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+\s+armamento=readonly') {
        throw 'Preflight recusado: info nao reportou ABI, ou armamento nao esta em readonly.'
    }

    # A: headers coerentes. Puro arquivo, nenhum hardware.
    $headers = @()
    $headersOk = $true
    foreach ($e in $engines) {
        $r = Run "cp_info_$($e.nome)" @('cp','info',$e.nome,$e.arquivo) -AllowFailure
        $texto = $r.output -join "`n"
        $jt = if ($texto -match 'jump table\s+:\s+dw (\d+) \.\. (\d+) \((\d+) dwords') {
            [pscustomobject]@{ inicio = [int]$Matches[1]; fim = [int]$Matches[2]; dwords = [int]$Matches[3] }
        } else { $null }
        $payload = if ($texto -match 'payload\s+:\s+(\d+) bytes \((\d+) dwords\)') { [int]$Matches[2] } else { $null }
        $ver = if ($texto -match 'ucode_version\s+:\s+0x([0-9A-Fa-f]+)') { '0x' + $Matches[1] } else { $null }
        $ok = ($r.exit_code -eq 0 -and $null -ne $jt -and $null -ne $payload -and $jt.fim -le $payload)
        if (-not $ok) { $headersOk = $false }
        $headers += [pscustomobject]@{
            engine = $e.nome; versao = $ver; payload_dwords = $payload; jump_table = $jt; ok = $ok
        }
    }
    Add-Gate 'A_headers_coerentes' $headersOk `
        'Header v1.0, IP v10.1 e jump table dentro do payload nas tres engines.' $headers
    if (-not $headersOk) { throw 'Header incoerente: nao mandar isso ao hardware.' }

    # B: linha de base. O CP esta parado e haltado?
    $st0 = Run 'cp_status_inicial' @('cp','status','pfp') -AllowFailure
    $meCntlInicial = Get-MeCntl $st0
    $pntrInicial = Read-Pntrs 'pntr_inicial'
    $tresHalts = ($null -ne $meCntlInicial -and
                  ($meCntlInicial -band 0x04000000L) -and
                  ($meCntlInicial -band 0x10000000L) -and
                  ($meCntlInicial -band 0x01000000L))
    Add-Gate 'B_tres_engines_haltadas_e_paradas' ($tresHalts -and -not $pntrInicial.algum_em_movimento) `
        'PFP_HALT, ME_HALT e CE_HALT ligados, e nenhum ponteiro de instrucao se move.' `
        ([pscustomobject]@{
            cp_me_cntl = Format-Hex $meCntlInicial
            pfp_halt = [bool]($meCntlInicial -band 0x04000000L)
            me_halt  = [bool]($meCntlInicial -band 0x10000000L)
            ce_halt  = [bool]($meCntlInicial -band 0x01000000L)
            ponteiros = $pntrInicial
        })
    if (-not $tresHalts) { throw 'As tres engines nao estao haltadas: este ensaio pressupoe CP em repouso.' }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true

    # C: controle negativo. Sem snapshot, escrever em registrador do CP tem de
    # ser recusado. Se o driver escrever aqui, a rede de seguranca nao existe.
    $neg = Run 'icbase_sem_snapshot' @('cp','icbase','pfp',('0x{0:X8}' -f $UcodeOffset)) -AllowFailure
    $recusou = ($neg.exit_code -ne 0 -and ($neg.output -join ' ') -match 'nenhum snapshot de transacao aberto|RECUSADO')
    Add-Gate 'C_recusa_sem_snapshot' $recusou `
        'Sem caminho de volta provado, o driver recusa escrever em registrador do CP.' `
        ([pscustomobject]@{ exit_code = $neg.exit_code; saida = $neg.output })

    # D: snapshot dos dez registradores de configuracao.
    $snap = Run 'cp_txn_snapshot' @('cp','txn','snapshot') -AllowFailure
    if ($snap.exit_code -eq 0) { $snapshotTaken = $true }
    Add-Gate 'D_snapshot_guardado' `
        ($snap.exit_code -eq 0 -and (($snap.output -join ' ') -match ('registradores\s+:\s+' + $cpTxnCount))) `
        "Os $cpTxnCount registradores de configuracao do CP ficam guardados no driver." `
        ([pscustomobject]@{ exit_code = $snap.exit_code; esperados = $cpTxnCount; saida = $snap.output })
    if (-not $snapshotTaken) { throw 'Sem snapshot nao ha caminho de volta: o ensaio para aqui.' }

    # E..: uma engine por vez. O snapshot de ucode do driver e unico, entao
    # stage/rollback acontecem em pares. Consequencia conhecida: este ensaio NAO
    # deixa as tres com ucode simultaneo — isso e requisito do passo seguinte,
    # o que tira do HALT, e vai exigir tres regioes vivas ao mesmo tempo.
    foreach ($e in $engines) {
        $r = [pscustomobject]@{
            engine = $e.nome
            stage = $null; icinval = $null; icbase = $null; jt = $null; rollback = $null
            ok = $false
        }
        $off = '0x{0:X8}' -f $UcodeOffset

        $st = Run "stage_$($e.nome)" @('cp','stage',$e.nome,$off,$e.arquivo) -AllowFailure
        $stTexto = $st.output -join "`n"
        $stDiv = if ($stTexto -match 'escritos\s+:\s+(\d+) dwords, (\d+) divergencias') { [int]$Matches[2] } else { $null }
        $stDw = if ($stTexto -match 'escritos\s+:\s+(\d+) dwords') { [int]$Matches[1] } else { $null }
        $r.stage = [pscustomobject]@{ exit_code = $st.exit_code; dwords = $stDw; divergencias = $stDiv; saida = $st.output }
        if ($st.exit_code -eq 0) { $staged[$e.nome] = $true }

        $inv = Run "icinval_$($e.nome)" @('cp','icinval',$e.nome) -AllowFailure
        $invTexto = $inv.output -join "`n"
        $r.icinval = [pscustomobject]@{
            exit_code = $inv.exit_code
            confirmado = ($invTexto -match 'o hardware confirmou por COMPLETE')
            saida = $inv.output
        }

        $icb = Run "icbase_$($e.nome)" @('cp','icbase',$e.nome,$off) -AllowFailure
        $icbTexto = $icb.output -join "`n"
        $r.icbase = [pscustomobject]@{
            exit_code = $icb.exit_code
            escritas = $(if ($icbTexto -match 'escritas\s+:\s+(\d+)') { [int]$Matches[1] } else { $null })
            confirmado = ($icbTexto -match 'ok, IC_BASE apontado para o ucode|confirmado')
            saida = $icb.output
        }

        $jt = Run "jt_$($e.nome)" @('cp','jt',$e.nome,$off,$e.arquivo) -AllowFailure
        $jtTexto = $jt.output -join "`n"
        $r.jt = [pscustomobject]@{
            exit_code = $jt.exit_code
            dwords = $(if ($jtTexto -match 'dwords escritos\s+:\s+(\d+)') { [int]$Matches[1] } else { $null })
            saida = $jt.output
        }

        $rb = Run "rollback_$($e.nome)" @('cp','rollback') -AllowFailure
        $rbTexto = $rb.output -join "`n"
        $rbDiv = if ($rbTexto -match 'devolvidos\s+:\s+(\d+), (\d+) divergencias') { [int]$Matches[2] } else { $null }
        $r.rollback = [pscustomobject]@{ exit_code = $rb.exit_code; divergencias = $rbDiv; saida = $rb.output }
        if ($rb.exit_code -eq 0) { $staged.Remove($e.nome) }

        $r.ok = ($st.exit_code -eq 0 -and $stDiv -eq 0 -and
                 $inv.exit_code -eq 0 -and $r.icinval.confirmado -and
                 $icb.exit_code -eq 0 -and $r.icbase.confirmado -and $r.icbase.escritas -eq 3 -and
                 $jt.exit_code -eq 0 -and
                 $rb.exit_code -eq 0 -and $rbDiv -eq 0)
        $resultados += $r
    }

    Add-Gate 'E_ucode_na_vram_conferido' `
        ([bool](@($resultados | Where-Object { $_.stage.exit_code -eq 0 -and $_.stage.divergencias -eq 0 }).Count -eq $engines.Count)) `
        'O ucode das tres engines foi escrito na VRAM e conferido dword a dword.' `
        ($resultados | ForEach-Object { [pscustomobject]@{ engine = $_.engine; stage = $_.stage } })

    Add-Gate 'F_cache_invalidada_com_confirmacao' `
        ([bool](@($resultados | Where-Object { $_.icinval.confirmado }).Count -eq $engines.Count)) `
        'INVALIDATE_CACHE_COMPLETE aceso pelo hardware nas tres engines.' `
        ($resultados | ForEach-Object { [pscustomobject]@{ engine = $_.engine; icinval = $_.icinval } })

    Add-Gate 'G_icbase_aponta_para_o_ucode' `
        ([bool](@($resultados | Where-Object { $_.icbase.confirmado -and $_.icbase.escritas -eq 3 }).Count -eq $engines.Count)) `
        'IC_BASE_CNTL, LO e HI configurados para as tres engines.' `
        ($resultados | ForEach-Object { [pscustomobject]@{ engine = $_.engine; icbase = $_.icbase } })

    Add-Gate 'H_jump_table_escrita' `
        ([bool](@($resultados | Where-Object { $_.jt.exit_code -eq 0 }).Count -eq $engines.Count)) `
        'A jump table foi escrita pela porta DATA nas tres engines, com o ADDR conferido.' `
        ($resultados | ForEach-Object { [pscustomobject]@{ engine = $_.engine; jt = $_.jt } })

    Add-Gate 'I_ucode_devolvido' `
        ([bool](@($resultados | Where-Object { $_.rollback.exit_code -eq 0 -and $_.rollback.divergencias -eq 0 }).Count -eq $engines.Count)) `
        'A regiao de VRAM voltou ao conteudo anterior nas tres passagens.' `
        ($resultados | ForEach-Object { [pscustomobject]@{ engine = $_.engine; rollback = $_.rollback } })

    # J: O GATE QUE MAIS IMPORTA. Nada disto podia por o CP a executar.
    $pntrFinal = Read-Pntrs 'pntr_final'
    $st1 = Run 'cp_status_final' @('cp','status','pfp') -AllowFailure
    $meCntlFinal = Get-MeCntl $st1
    Add-Gate 'J_cp_continua_parado_e_haltado' `
        ((-not $pntrFinal.algum_em_movimento) -and $meCntlFinal -eq $meCntlInicial) `
        'Nenhum ponteiro de instrucao se moveu e CP_ME_CNTL nao mudou: nada saiu do HALT.' `
        ([pscustomobject]@{
            cp_me_cntl_antes = Format-Hex $meCntlInicial
            cp_me_cntl_depois = Format-Hex $meCntlFinal
            ponteiros_antes = $pntrInicial
            ponteiros_depois = $pntrFinal
        })
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
        foreach ($nome in @($staged.Keys)) {
            Run "rollback_final_$nome" @('cp','rollback') -AllowFailure | Out-Null
        }
        if ($snapshotTaken) {
            $rbTxn = Run 'cp_txn_restore' @('cp','txn','restore') -AllowFailure
            $rest = if (($rbTxn.output -join ' ') -match 'restaurados\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
            $div = if (($rbTxn.output -join ' ') -match 'divergencias\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
            Add-Gate 'K_registradores_devolvidos' ($rbTxn.exit_code -eq 0 -and $div -eq 0 -and $rest -eq $cpTxnCount) `
                "Os $cpTxnCount registradores do CP voltaram ao snapshot, com readback conferido." `
                ([pscustomobject]@{ restaurados = $rest; esperados = $cpTxnCount; divergencias = $div; saida = $rbTxn.output })
        }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'L_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'CP front, step 2: direct load of PFP/ME/CE ucode into VRAM and point each instruction cache at it, without releasing any engine from HALT'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    not_covered = @(
        'No engine was released from HALT: nothing here proves the CP executes',
        'The driver keeps a single ucode snapshot, so the three engines were staged one at a time; simultaneous staging is a requirement of the next step',
        'The jump table port (UCODE_DATA) writes the microengine internal RAM and has no readback; only ADDR is verified',
        'The instruction cache fetch was configured with VMID 0 but translation was NOT enabled: the POST bypass was active throughout',
        'No GFX ring, no PM4 packet, no compute'
    )
    reboot_required = $false
    contaminates_session = $false
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        cp_txn_registers = $cpTxnCount
        ucode_vram_offset = ('0x{0:X8}' -f $UcodeOffset)
        segment1_base_dw = '0xA000, conferida contra registrador vivo em 2026-08-23 17:54'
        halt_bits = 'CE_HALT bit 24, PFP_HALT bit 26, ME_HALT bit 28, do header autoritativo'
        pointer_samples = $Samples
    }
    firmware_headers = $headers
    engines = $resultados
    gates = $gates
    failed_gates = $reprovados
    bugchecks = $bugchecks
    commands = $events
}
$record | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host "Evidencia: $dir"
if ($gates.Count -eq 0) { Write-Host '  nenhum gate foi exercitado: o ensaio parou antes de comecar.' }
foreach ($g in $gates) {
    Write-Host ("  [{0}] {1}" -f $(if ($g.passed) { 'ok  ' } else { 'FALHA' }), $g.gate)
}
if ($failure) { Write-Error $failure; exit 1 }
Write-Host ''
Write-Host 'Direct load do CP exercitado nas tres microengines: ucode na VRAM conferido'
Write-Host 'dword a dword, cache de instrucoes invalidada com confirmacao do hardware,'
Write-Host 'IC_BASE apontando para o ucode com readback, e jump table escrita.'
Write-Host ''
Write-Host 'NENHUMA ENGINE SAIU DO HALT. Nada aqui prova que o CP executa: os quatro'
Write-Host 'ponteiros de instrucao continuaram imoveis do inicio ao fim.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'Sessao NAO contaminada: ucode devolvido, registradores devolvidos.'
exit 0
