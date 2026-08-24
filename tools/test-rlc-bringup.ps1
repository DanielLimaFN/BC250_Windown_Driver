<#
    Bring-up do RLC.

    O QUE A MEDICAO MOSTROU, E POR QUE ISTO E O BLOQUEIO

    Em 2026-08-23 23:07, comparando com a referencia do Linux onde o CP esta
    comprovadamente executando:

        RLC_GPM_STAT.RLC_EXEC_ROM_CODE   Windows = 1     Linux = 0
        RLC_CNTL.RLC_ENABLE_F32          Windows = 0     Linux = 1
        RLC_CGCG_CGLS_CTRL               Windows = 0x0001003C   Linux = 0

    EXEC_ROM_CODE = 1 significa RLC executando o bootloader interno, sem
    microcodigo. E o analogo exato do F32 da SDMA em HALT, e e prova fisica de
    que o RLC nunca foi inicializado deste lado.

    A mesma medicao encerrou a caca a zona morta: CP_*_IC_* e
    RLC_GPM_UCODE_ADDR/DATA leem 0xFFFFFFFF nos DOIS sistemas, e o CP funciona no
    Linux. Sao portas de escrita, nao o bloqueio.

    A SEQUENCIA, DE gfx_v10_0_rlc_resume() NO CAMINHO SEM AUTOLOAD

        stop                RLC_ENABLE_F32 = 0
        cgcg                RLC_CGCG_CGLS_CTRL = 0
        pg                  RLC_PG_CNTL = 0
        load                ucode pelas portas ADDR/DATA
        start               RLC_ENABLE_F32 = 1

    O GATE

    A prova NAO e escrita aceita: e EXEC_ROM_CODE cair de 1 para 0. Este projeto
    ja mediu quanto vale uma escrita aceita sem prova de execucao — quatro passos
    do CP foram declarados aprovados sobre isso, e tres eram falso positivo.

    O ensaio testa DUAS rotas de carga, na ordem de risco crescente, porque nao
    esta medido qual delas esta placa usa:

        rota A: PSP    (o SOS aceita o RLC, RespStatus = 0 medido em 14:21)
        rota B: direct (portas ADDR/DATA, como gfx_v10_0_rlc_load_microcode)

    Se a rota A ja fizer EXEC_ROM_CODE cair, a B nem e tentada.

    O QUE ESTE ENSAIO NAO FAZ

    Nao faz init_csb nem update_spm_vmid: os dois exigem um buffer de clear state
    que este laboratorio nao monta. Se o RLC nao sair da ROM sem eles, a medicao
    diz, e a lacuna esta declarada aqui em vez de descoberta depois.

    Nao toca no CP. Nao cria anel. Nao emite PM4.

    Carrega firmware pelo PSP na rota A: exige boot limpo e queima o boot.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,120)][int]$CommandTimeoutSeconds = 60,
    [switch]$SkipPsp,
    [switch]$SkipDirect
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$rlcFirmware = Join-Path $root 'firmware\cyan_skillfish2_rlc.bin'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute; a rota A carrega firmware pelo PSP (ativo ate o reboot).'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$abiHeader,$workspaceDriver,$installedDriver,$rlcFirmware)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

$rlcTxnCount = $null
foreach ($linha in Get-Content -LiteralPath $abiHeader) {
    if ($linha -match 'BC250DBG_RLC_TXN_REG_COUNT\s+(\d+)u') { $rlcTxnCount = [int]$Matches[1]; break }
}
if (-not $rlcTxnCount) { throw 'Nao foi possivel ler BC250DBG_RLC_TXN_REG_COUNT do header da ABI.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-rlc-bringup"
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
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}
# Le o estado do RLC pelo proprio CLI e devolve os bits que decidem.
function Get-RlcState([string]$Rotulo) {
    $r = Run "rlc_status_$Rotulo" @('rlc','status') -AllowFailure
    $t = $r.output -join "`n"
    $cntl = if ($t -match 'RLC_CNTL\s+:\s+0x([0-9A-Fa-f]{8})') { $Matches[1] } else { $null }
    $stat = if ($t -match 'RLC_GPM_STAT\s+:\s+0x([0-9A-Fa-f]{8})') { $Matches[1] } else { $null }
    $cgcg = if ($t -match 'RLC_CGCG_CGLS_CTRL\s+:\s+0x([0-9A-Fa-f]{8})') { $Matches[1] } else { $null }
    $pg   = if ($t -match 'RLC_PG_CNTL\s+:\s+0x([0-9A-Fa-f]{8})') { $Matches[1] } else { $null }
    $cgtt = if ($t -match 'CGTT_CPF_CLK_CTRL\s+:\s+0x([0-9A-Fa-f]{8})') { $Matches[1] } else { $null }
    $rom  = if ($t -match 'EXEC_ROM_CODE (\d)') { [int]$Matches[1] } else { $null }
    $f32  = if ($t -match 'ENABLE_F32 (\d)') { [int]$Matches[1] } else { $null }
    return [pscustomobject]@{
        rotulo = $Rotulo
        exit_code = $r.exit_code
        rlc_cntl = $cntl
        rlc_gpm_stat = $stat
        cgcg_cgls_ctrl = $cgcg
        pg_cntl = $pg
        cgtt_cpf_clk_ctrl = $cgtt
        exec_rom_code = $rom
        enable_f32 = $f32
        saida = $r.output
    }
}
function Invoke-RlcStep([string]$Sub) {
    $r = Run "rlc_$Sub" @('rlc',$Sub) -AllowFailure
    $t = $r.output -join "`n"
    $div = if ($t -match 'escritas\s+:\s+(\d+), (\d+) divergencias') { [int]$Matches[2] } else { $null }
    $esc = if ($t -match 'escritas\s+:\s+(\d+),') { [int]$Matches[1] } else { $null }
    return [pscustomobject]@{
        passo = $Sub
        exit_code = $r.exit_code
        escritas = $esc
        divergencias = $div
        saida = $r.output
    }
}

$failure = $null
$armed = $false; $snapshotTaken = $false; $pspCreated = $false; $firmwareAccepted = $false
$estadoInicial = $null; $estadoFinal = $null
$rotaA = $null; $rotaB = $null; $passos = @()
$saiuDaRom = $false; $rotaQueFuncionou = $null

try {
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+\s+armamento=readonly') {
        throw 'Preflight recusado: info nao reportou ABI, ou armamento nao esta em readonly.'
    }
    $ucode = Run 'sdma_ucode_pre' @('reg','r','0x04A24')
    if (($ucode.output -join ' ') -notmatch '=\s*0x00000000') {
        throw 'Preflight recusado: firmware SDMA ja ativo. Reinicie.'
    }

    # A: linha de base. O RLC esta na ROM, como medido?
    $estadoInicial = Get-RlcState 'inicial'
    Add-Gate 'A_rlc_na_rom_na_linha_de_base' `
        ($estadoInicial.exec_rom_code -eq 1 -and $estadoInicial.enable_f32 -eq 0) `
        'EXEC_ROM_CODE = 1 e ENABLE_F32 = 0: o RLC esta rodando o bootloader interno.' `
        $estadoInicial
    if ($estadoInicial.exec_rom_code -ne 1) {
        throw "EXEC_ROM_CODE nao e 1 na linha de base (leu $($estadoInicial.exec_rom_code)). O pressuposto do ensaio nao vale nesta sessao."
    }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true

    # B: controle negativo, DEPOIS do armamento e ANTES do snapshot.
    #
    # Na execucao de 23:32 este gate rodava antes do 'arm write', e o driver
    # recusou por ARMAMENTO (erro 5), nao por falta de snapshot. Recusou pelo
    # motivo errado, e a guarda de snapshot nunca foi exercitada. Um gate que
    # recusa pelo motivo errado e pior que um gate ausente: parece ter testado.
    #
    # Agora o armamento esta em ordem, entao a unica guarda em jogo e a do
    # snapshot — e a mensagem exigida distingue as duas.
    $neg = Run 'stop_sem_snapshot' @('rlc','stop') -AllowFailure
    $negTexto = $neg.output -join ' '
    $recusouPorSnapshot = ($neg.exit_code -ne 0 -and $negTexto -match 'nenhum snapshot')
    $recusouPorArmamento = ($negTexto -match "erro 5|arm write")
    Add-Gate 'B_recusa_sem_snapshot' $recusouPorSnapshot `
        'Armado, mas sem snapshot: o driver recusa escrever, e recusa por ESSE motivo.' `
        ([pscustomobject]@{
            exit_code = $neg.exit_code
            recusou_por_snapshot = $recusouPorSnapshot
            recusou_por_armamento = $recusouPorArmamento
            nota = 'Se recusou por armamento, o gate mediu a guarda errada e nao vale.'
            saida = $neg.output
        })

    $snap = Run 'rlc_txn_snapshot' @('rlc','txn','snapshot') -AllowFailure
    if ($snap.exit_code -eq 0) { $snapshotTaken = $true }
    Add-Gate 'C_snapshot_guardado' `
        ($snap.exit_code -eq 0 -and (($snap.output -join ' ') -match ('registradores\s+:\s+' + $rlcTxnCount))) `
        "Os $rlcTxnCount registradores de configuracao do RLC ficam guardados no driver." `
        ([pscustomobject]@{ exit_code = $snap.exit_code; esperados = $rlcTxnCount; saida = $snap.output })
    if (-not $snapshotTaken) { throw 'Sem snapshot nao ha caminho de volta: o ensaio para aqui.' }

    # D..F: a sequencia do upstream antes da carga.
    foreach ($p in @('stop','cgcg','pg')) {
        $passos += Invoke-RlcStep $p
    }
    $prepOk = [bool](@($passos | Where-Object { $_.exit_code -eq 0 -and $_.divergencias -eq 0 }).Count -eq 3)
    Add-Gate 'D_stop_cgcg_pg_aceitos' $prepOk `
        'ENABLE_F32 a 0, CGCG_CGLS_CTRL a 0 e PG_CNTL a 0, cada um com readback conferido.' $passos
    if (-not $prepOk) { throw 'A preparacao do RLC nao aplicou: nao faz sentido seguir para a carga.' }

    $estadoPrep = Get-RlcState 'pos_preparacao'
    Add-Gate 'E_cgcg_zerado_como_no_linux' `
        ($estadoPrep.cgcg_cgls_ctrl -eq '00000000') `
        'RLC_CGCG_CGLS_CTRL = 0, igual ao Linux com o CP vivo (era 0x0001003C).' $estadoPrep

    # ---------------- rota A: PSP ----------------
    if (-not $SkipPsp) {
        Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true
        $lf = Run 'psp_loadfw_rlc' @('psp','loadfw','rlc',$rlcFirmware) -AllowFailure
        $lfTexto = $lf.output -join "`n"
        $resp = if ($lfTexto -match 'RespStatus\s+:\s+0x([0-9A-Fa-f]{8})') { $Matches[1] } else { $null }
        if ($resp -eq '00000000') { $firmwareAccepted = $true }

        # Estado logo APOS a carga do PSP e ANTES do start. Sem isto nao da para
        # saber quem habilitou o F32: o PSP ou o nosso start.
        $estadoPosPsp = Get-RlcState 'pos_psp_antes_do_start'

        $st = Invoke-RlcStep 'start'
        $estadoA = Get-RlcState 'pos_rota_A'
        # Se o F32 ja estava 1 antes do start, o start foi no-op e o credito e do
        # PSP. Medido assim em 23:32: 0x3B000 leu 0x00000001 antes de escrever.
        $f32AntesDoStart = $estadoPosPsp.enable_f32
        $romAntesDoStart = $estadoPosPsp.exec_rom_code
        $rotaA = [pscustomobject]@{
            rota = 'A: PSP'
            resp_status = $resp
            estado_pos_psp_antes_do_start = $estadoPosPsp
            f32_ja_estava_ligado_antes_do_start = ($f32AntesDoStart -eq 1)
            rom_ja_havia_caido_antes_do_start = ($romAntesDoStart -eq 0)
            quem_habilitou = $(if ($romAntesDoStart -eq 0) { 'o PSP, ao carregar o ucode' } else { 'o nosso start' })
            start = $st
            estado = $estadoA
            saiu_da_rom = ($estadoA.exec_rom_code -eq 0)
        }
        if ($rotaA.saiu_da_rom) { $saiuDaRom = $true; $rotaQueFuncionou = 'A: PSP' }
        Add-Gate 'F_rota_A_psp' ($rotaA.saiu_da_rom) `
            'Com o RLC carregado pelo PSP e ENABLE_F32 a 1, EXEC_ROM_CODE caiu para 0.' $rotaA
    }

    # ---------------- rota B: direct load ----------------
    # Só se a A nao resolveu. Sem isso, a B mediria sobre um RLC ja rodando.
    if (-not $saiuDaRom -and -not $SkipDirect) {
        $passosB = @()
        $passosB += Invoke-RlcStep 'stop'
        $ld = Run 'rlc_load_direct' @('rlc','load',$rlcFirmware) -AllowFailure
        $ldTexto = $ld.output -join "`n"
        $dw = if ($ldTexto -match 'dwords escritos\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
        $passosB += Invoke-RlcStep 'start'
        $estadoB = Get-RlcState 'pos_rota_B'
        $rotaB = [pscustomobject]@{
            rota = 'B: direct'
            dwords = $dw
            load_exit = $ld.exit_code
            load_saida = $ld.output
            passos = $passosB
            estado = $estadoB
            saiu_da_rom = ($estadoB.exec_rom_code -eq 0)
        }
        if ($rotaB.saiu_da_rom) { $saiuDaRom = $true; $rotaQueFuncionou = 'B: direct' }
        Add-Gate 'G_rota_B_direct' ($rotaB.saiu_da_rom) `
            'Com o ucode escrito pelas portas ADDR/DATA e ENABLE_F32 a 1, EXEC_ROM_CODE caiu para 0.' $rotaB
    }

    # H: O GATE. Uma das rotas tirou o RLC da ROM?
    $estadoFinal = Get-RlcState 'final'
    Add-Gate 'H_RLC_SAIU_DA_ROM' $saiuDaRom `
        'EXEC_ROM_CODE caiu de 1 para 0: o RLC esta executando microcodigo, nao a ROM.' `
        ([pscustomobject]@{
            rota_que_funcionou = $rotaQueFuncionou
            estado_inicial = $estadoInicial
            estado_final = $estadoFinal
            referencia_linux = 'EXEC_ROM_CODE = 0, ENABLE_F32 = 1, CGCG_CGLS_CTRL = 0'
        })

    # I: se saiu da ROM, os SOFT_OVERRIDE do CGTT devem aparecer sozinhos.
    if ($saiuDaRom) {
        # Medido em 23:32: NAO muda. Com o RLC fora da ROM, CGTT_CPF_CLK_CTRL
        # continuou 0x00000100 contra 0xF8000100 no Linux. A hipotese de que os
        # SOFT_OVERRIDE apareceriam sozinhos esta errada — o amdgpu os escreve
        # em algum outro lugar. Isto e REGISTRO, nao requisito.
        $cgttMudou = ($estadoFinal.cgtt_cpf_clk_ctrl -ne $estadoInicial.cgtt_cpf_clk_ctrl)
        Add-Gate 'I_cgtt_e_consequencia_do_rlc' $cgttMudou `
            'PERGUNTA, nao requisito: os SOFT_OVERRIDE do CGTT_CPF aparecem sozinhos quando o RLC sai da ROM?' `
            ([pscustomobject]@{
                antes = $estadoInicial.cgtt_cpf_clk_ctrl
                depois = $estadoFinal.cgtt_cpf_clk_ctrl
                linux = 'F8000100'
                resposta_medida_em_23_32 = 'NAO: ficou 0x00000100 com o RLC fora da ROM. Nao e consequencia automatica; falta achar onde o amdgpu escreve isso.'
            })
    }
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
        if ($snapshotTaken) {
            $rb = Run 'rlc_txn_restore' @('rlc','txn','restore') -AllowFailure
            $rest = if (($rb.output -join ' ') -match 'restaurados\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
            $div = if (($rb.output -join ' ') -match 'divergencias\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
            Add-Gate 'J_registradores_devolvidos' ($rb.exit_code -eq 0 -and $div -eq 0 -and $rest -eq $rlcTxnCount) `
                "Os $rlcTxnCount registradores do RLC voltaram ao snapshot, com readback conferido." `
                ([pscustomobject]@{ restaurados = $rest; esperados = $rlcTxnCount; divergencias = $div; saida = $rb.output })
        }
        if ($pspCreated) { Run 'psp_rollback' @('psp','rollback') -AllowFailure | Out-Null }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'K_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

# As rotas A e B sao TENTATIVAS: reprovar uma delas quando a outra funcionou
# esconderia o resultado. O que decide o ensaio e o gate H.
$reprovados = @($gates | Where-Object {
    -not $_.passed -and $_.gate -notlike 'F_rota_A*' -and $_.gate -notlike 'G_rota_B*' -and $_.gate -notlike 'I_*'
} | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Bring up the RLC out of ROM execution, with RLC_GPM_STAT.RLC_EXEC_ROM_CODE going 1 to 0 as the only accepted proof'
    verdict = $verdict
    passed = (-not $failure)
    rlc_saiu_da_rom = $saiuDaRom
    rota_que_funcionou = $rotaQueFuncionou
    failure = $failure
    not_covered = @(
        'init_csb and update_spm_vmid were NOT done: both need a clear state buffer this lab does not build',
        'RLC_GPM_UCODE_ADDR/DATA are write-only (0xFFFFFFFF on both Windows and Linux); the ADDR readback is an indication, not proof',
        'Nothing about the CP was touched: no unhalt, no ring, no PM4',
        'A running RLC is not a running CP: that is the next step, not this one'
    )
    reboot_required = $firmwareAccepted
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        rlc_txn_registers = $rlcTxnCount
        segment1_base_dw = '0xA000, medida contra registrador vivo e cross-validada com Linux'
        gate = 'RLC_GPM_STAT.RLC_EXEC_ROM_CODE (bit 22) cair de 1 para 0'
        referencia_linux = [pscustomobject]@{
            RLC_CNTL = '0x00000001'
            RLC_GPM_STAT = '0x00B40016 (EXEC_ROM_CODE = 0)'
            RLC_CGCG_CGLS_CTRL = '0x00000000'
            CGTT_CPF_CLK_CTRL = '0xF8000100'
        }
    }
    estado_inicial = $estadoInicial
    estado_final = $estadoFinal
    passos_preparacao = $passos
    rota_A_psp = $rotaA
    rota_B_direct = $rotaB
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
Write-Host ''
if ($estadoInicial -and $estadoFinal) {
    Write-Host 'EXEC_ROM_CODE, o gate:'
    Write-Host ("  inicial : {0}" -f $estadoInicial.exec_rom_code)
    Write-Host ("  final   : {0}" -f $estadoFinal.exec_rom_code)
    Write-Host ("  Linux   : 0  (referencia, com o CP executando)")
    Write-Host ''
    Write-Host ("CGTT_CPF_CLK_CTRL : 0x{0} -> 0x{1}   (Linux: 0xF8000100)" -f `
        $estadoInicial.cgtt_cpf_clk_ctrl, $estadoFinal.cgtt_cpf_clk_ctrl)
    Write-Host ''
}
if ($saiuDaRom) {
    Write-Host ("O RLC SAIU DA ROM pela rota {0}." -f $rotaQueFuncionou)
    Write-Host 'Isso NAO e o CP funcionando: e o pre-requisito dele.'
} else {
    Write-Host 'O RLC NAO saiu da ROM em nenhuma das rotas tentadas.'
    Write-Host 'Escrita aceita nao e execucao. Candidatos ao que falta:'
    Write-Host '  - init_csb e update_spm_vmid, declaradamente nao feitos aqui;'
    Write-Host '  - ucode entregue por caminho que esta placa nao usa;'
    Write-Host '  - algum passo anterior ao start que o upstream faz e nao mapeamos.'
}
if ($failure) { Write-Error $failure; exit 1 }
Write-Host ''
Write-Host "Veredito global permanece: $verdict"
if ($firmwareAccepted) { Write-Host 'REBOOT NECESSARIO: firmware carregado pelo PSP fica ativo nesta sessao.' }
exit 0
