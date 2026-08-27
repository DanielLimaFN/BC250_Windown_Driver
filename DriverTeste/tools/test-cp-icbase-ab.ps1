<#
    ENSAIO A/B: AS ESCRITAS DE IC_BASE PRODUZEM QUALQUER EFEITO?

    O QUE ESTA EM JOGO

    Seis boots limpos, seis configuracoes, o mesmo PC 0x0000041E constante nas
    tres engines:

      00:19  ucode + IC_BASE fisico + jump table
      00:47  idem, reproducao
      00:54  CONTROLE: nada de ucode, IC_BASE ou jump table
      02:07  traducao LIGADA + IC_BASE em VA mapeada, zero falhas de traducao
      02:14  carga pelo PSP (recusada, 0x80000203 — ja era conhecido)
      08:32  ungate do CPF confirmado por readback (0x00000100 -> 0xF8000100)

    Toda a familia de hipoteses de ENDERECO caiu. A de CLOCK DO FETCHER caiu com
    readback de verdade. Sobra uma afirmacao que, do jeito que estava, era
    INDEMONSTRAVEL: 'IC_BASE_* e write-only, logo nenhum readback pode dizer se a
    escrita foi aceita'.

    COMO SE MEDE EFEITO SEM READBACK

    Escrevendo DOIS valores que o hardware deveria tratar de forma diferente e
    comparando o COMPORTAMENTO. EXE_DISABLE do IC_BASE_CNTL desabilita execucao a
    partir da cache de instrucoes; 0 e o valor do upstream.

    Este ensaio faz as duas passagens NO MESMO BOOT, com o mesmo RLC, o mesmo
    ucode e o mesmo ungate de CPF. So o bit muda:

      passagem A : EXE_DISABLE = 0   (configuracao do upstream)
      passagem B : EXE_DISABLE = 1   (execucao pela cache desabilitada)

    O QUE CADA RESULTADO SIGNIFICA

    - PC DIFERENTE entre A e B -> as escritas de IC_BASE_CNTL CHEGAM ao hardware.
      O registrador e aceito, e a investigacao volta para por que a busca nao
      resolve mesmo sendo aceita.

    - PC BIT A BIT IDENTICO -> as escritas nao produzem efeito observavel nenhum.
      A suspeita de que o registrador nao esta sendo aceito deixa de ser
      inverificavel e passa a ter evidencia positiva. O trabalho vira: descobrir
      o que falta para que o bloco aceite escrita (indexacao de GRBM, dominio de
      reset, handshake com o RLC).

    Nenhum dos dois e aprovacao do passo 3. Os dois sao informacao, e e a primeira
    vez que essa pergunta fica mediivel.

    NAO ACEITA COMO PROVA

    - PC diferente de zero. 0x0000041E apareceu em seis boots sem execucao.
    - Readback de IC_BASE_*. Continua impossivel, e e justamente o motivo do A/B.
    - Codigo de retorno da CLI.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,120)][int]$CommandTimeoutSeconds = 60,
    [ValidateRange(2,64)][int]$Samples = 8,
    [uint32]$PfpOffset = 0x0E000000,
    [uint32]$MeOffset  = 0x0E080000,
    [uint32]$CeOffset  = 0x0E100000,
    [switch]$ForcarSessaoSuja
    # Sem -ControleSemUcode aqui: este ensaio JA e um diferencial, e um controle
    # que nao escreve IC_BASE nenhum nao teria com o que comparar.
    #
    # A amostragem das duas passagens usa -Samples, o mesmo parametro: comparar
    # passagens com numero de amostras diferente seria comparar coisas diferentes.
)
$ControleSemUcode = $false

$ErrorActionPreference = 'Stop'
# Variavel nao inicializada passa a ser ERRO em vez de $null.
#
# Em 2026-08-24 00:47 o switch -ControleSemUcode foi passado na linha de comando,
# a variavel nao existia no param (defeito meu no patch), o PowerShell resolveu
# $ControleSemUcode para $null, e o ensaio rodou em MODO NORMAL sem uma palavra.
# Um boot gasto repetindo medicao conhecida. Com StrictMode isso passa a estourar
# na primeira leitura.
Set-StrictMode -Version 1.0
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date
$modoEnsaio = 'A/B DO EXE_DISABLE (18 gates)'
$rlcSnapshotFeito = $false
$pspCriado = $false
$firmwareCarregado = $false

$engines = @(
    [pscustomobject]@{ nome = 'pfp'; offset = $PfpOffset; arquivo = 'firmware\cyan_skillfish2_pfp.bin' }
    [pscustomobject]@{ nome = 'me';  offset = $MeOffset;  arquivo = 'firmware\cyan_skillfish2_me.bin' }
    [pscustomobject]@{ nome = 'ce';  offset = $CeOffset;  arquivo = 'firmware\cyan_skillfish2_ce.bin' }
)

# Ponteiros de instrucao, segmento 0. O instrumento honesto de execucao do CP.
$pntrs = [ordered]@{
    'CP_PFP_INSTR_PNTR'  = 0x08694
    'CP_ME_INSTR_PNTR'   = 0x08698
    'CP_CE_INSTR_PNTR'   = 0x0869C
    'CP_MEC1_INSTR_PNTR' = 0x086A0
}

Write-Host "Modo do ensaio: $modoEnsaio" -ForegroundColor Cyan
if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute. Ele executa unhalt sob protecao transacional e faz rollback completo.'
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

# -----------------------------------------------------------------------------
# Preflight de boot limpo
#
# Este ensaio carrega firmware pelo PSP no bring-up do RLC, logo ele QUEIMA O
# BOOT, exatamente como test-rlc-bringup.ps1. A guarda faltava, e o preco
# apareceu: em 2026-08-23 23:58 ele rodou no mesmo boot das 23:26 que o
# bring-up das 23:32 ja havia gasto, e nao recusou. Gastou uma execucao para
# reprovar um pre-requisito que nao tinha como ser satisfeito.
#
# A deteccao nao e por suposicao: procura nas pastas de evidencia por um ensaio
# com reboot_required = true carimbado DEPOIS do ultimo boot.
#
# NAO filtra por nome de arquivo. A primeira versao procurava so 'evidence.json'
# e devolveu zero: os ensaios deste projeto gravam em 'result.json' (44 pastas),
# 'evidence.json' (8), 'baseline.json', 'rollback.json'. Filtrar por um nome era
# a licao 8 de novo — acoplar o leitor ao formato de um produtor so.
#
# O criterio passou a ser a PROPRIEDADE, nao o nome: qualquer .json que declare
# reboot_required = true conta. Um ensaio novo que grave com outro nome ja entra
# nesta conta de graca, desde que declare o campo.
# -----------------------------------------------------------------------------
$ultimoBoot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$queimaramEsteBoot = @()
$evidenceRoot = Join-Path $root 'docs\evidence'
if (Test-Path -LiteralPath $evidenceRoot) {
    foreach ($jsonPath in @(Get-ChildItem -LiteralPath $evidenceRoot -Recurse -Filter '*.json' -ErrorAction SilentlyContinue)) {
        if ($jsonPath.LastWriteTime -le $ultimoBoot) { continue }
        try {
            $j = Get-Content -LiteralPath $jsonPath.FullName -Raw | ConvertFrom-Json
        } catch { continue }
        if ($null -eq $j) { continue }
        if (-not ($j.PSObject.Properties.Name -contains 'reboot_required')) { continue }
        if ($j.reboot_required -eq $true) {
            $queimaramEsteBoot += [pscustomobject]@{
                ensaio = $jsonPath.Directory.Name
                arquivo = $jsonPath.Name
                em = $jsonPath.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
            }
        }
    }
}
if ($queimaramEsteBoot.Count -gt 0 -and -not $ForcarSessaoSuja) {
    $lista = ($queimaramEsteBoot | ForEach-Object { "  - $($_.ensaio) ($($_.arquivo)) em $($_.em)" }) -join "`n"
    throw ("Sessao suja: firmware ja foi carregado neste boot (ultimo boot em " +
           $ultimoBoot.ToString('yyyy-MM-dd HH:mm:ss') + ").`n$lista`n" +
           "Este ensaio carrega firmware pelo PSP e precisa de boot limpo. Reinicie e rode de novo.`n" +
           "Para medir de proposito em sessao suja, use -ForcarSessaoSuja: o resultado sai marcado como tal.")
}

$cpTxnCount = $null
foreach ($linha in Get-Content -LiteralPath $abiHeader) {
    if ($linha -match 'BC250DBG_CP_TXN_REG_COUNT\s+(\d+)u') { $cpTxnCount = [int]$Matches[1]; break }
}
if (-not $cpTxnCount) { throw 'Nao foi possivel ler BC250DBG_CP_TXN_REG_COUNT do header da ABI.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-cp-unhalt"
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

function Read-Pntrs([string]$Rotulo) {
    $amostras = @()
    for ($s = 0; $s -lt $Samples; $s++) {
        $linha = [ordered]@{}
        foreach ($k in $pntrs.Keys) { $linha[$k] = Read-Reg "${Rotulo}_${k}_$s" ([uint32]$pntrs[$k]) }
        $linha['CP_STAT'] = Read-Reg "${Rotulo}_CP_STAT_$s" 0x08680
        $linha['GRBM_STATUS'] = Read-Reg "${Rotulo}_GRBM_STATUS_$s" 0x08010
        $amostras += [pscustomobject]$linha
        Start-Sleep -Milliseconds 10
    }
    $distintos = [ordered]@{}
    foreach ($k in $pntrs.Keys) {
        $vals = @($amostras | ForEach-Object { $_.$k } | Where-Object { $null -ne $_ })
        $distintos[$k] = ($vals | Sort-Object -Unique).Count
    }
    return [pscustomobject]@{
        primeira = [pscustomobject](& { $o = [ordered]@{}; foreach ($k in $pntrs.Keys) { $o[$k] = Format-Hex $amostras[0].$k }; $o })
        amostras = $amostras
        valores_distintos = [pscustomobject]$distintos
        algum_em_movimento = [bool](@($distintos.Keys | Where-Object { $distintos[$_] -gt 1 }).Count)
    }
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
$stagedEngines = [System.Collections.Generic.List[string]]::new()
$unhaltFeito = $false

# O try/finally do ensaio fica DENTRO de um try/catch, para que um throw de
# pre-requisito nao destrua a evidencia. Em 2026-08-23 23:58 o throw do gate B2
# matou o script antes do relatorio: a pasta de evidencia ficou vazia, a execucao
# foi gasta e nao sobrou nem o valor que reprovou o gate.
#
# Regra: abortar e uma decisao do ensaio; perder a medicao nao e.
$abortadoPor = $null
try {
try {
    # -------------------------------------------------------------------------
    # Gate A: headers dos 3 ucodes
    # -------------------------------------------------------------------------
    $headers = @()
    foreach ($e in $engines) {
        $info = Run "info_$($e.nome)" @('cp','info',$e.nome,$e.arquivo) -AllowFailure
        $t = $info.output -join "`n"
        $h = [pscustomobject]@{
            engine = $e.nome
            exit_code = $info.exit_code
            header = $(if ($t -match 'header\s+:\s+(v[0-9\.]+,\s+\d+\s+bytes)') { $Matches[1] } else { $null })
            ip = $(if ($t -match 'IP\s+:\s+(v[0-9\.]+)') { $Matches[1] } else { $null })
            payload_bytes = $(if ($t -match 'payload\s+:\s+(\d+)\s+bytes') { [int]$Matches[1] } else { $null })
            jt_dwords = $(if ($t -match 'jump table\s+:\s+dw\s+\d+\s+\.\.\s+\d+\s+\((\d+)\s+dwords') { [int]$Matches[1] } else { $null })
            saida = $info.output
        }
        $headers += $h
    }
    Add-Gate 'A_headers_coerentes' `
        ([bool](@($headers | Where-Object { $_.exit_code -eq 0 -and $_.payload_bytes -gt 0 -and $_.jt_dwords -gt 0 }).Count -eq $engines.Count)) `
        'Os tres ucodes tem header v1.0, IP v10.1 e jump table descrita dentro do payload.' `
        $headers

    # -------------------------------------------------------------------------
    # Gate B: Preflight (engines haltadas no baseline)
    # -------------------------------------------------------------------------
    $pntrsBaseline = Read-Pntrs 'baseline'
    $meCntlBaseline = Read-Reg 'baseline_CP_ME_CNTL' 0x086D8
    Add-Gate 'B_tres_engines_haltadas_e_paradas' `
        ([bool]($meCntlBaseline -eq 0x15000000 -and -not $pntrsBaseline.algum_em_movimento)) `
        'CP_ME_CNTL = 0x15000000 (CE/PFP/ME_HALT ativos) e os quatro ponteiros de instrucao zerados.' `
        ([pscustomobject]@{
            CP_ME_CNTL = Format-Hex $meCntlBaseline
            ponteiros = $pntrsBaseline.primeira
        })

    # -------------------------------------------------------------------------
    # Gate C: Snapshot da transacao
    # -------------------------------------------------------------------------
    Run 'arm_write' @('arm','write') | Out-Null
    $armado = $true

    # -------------------------------------------------------------------------
    # Gate B2: o RLC precisa estar FORA DA ROM antes de tocar no CP
    #
    # Medido em 2026-08-23 23:32: deste lado o RLC executa o bootloader interno
    # (RLC_GPM_STAT.RLC_EXEC_ROM_CODE = 1) e sai da ROM quando o PSP carrega o
    # ucode. No Linux, onde o CP funciona, ele esta fora da ROM. Tentar o unhalt
    # do CP com o RLC na ROM e construir sobre pre-requisito ausente.
    #
    # Sequencia identica a que funcionou no ensaio do RLC: snapshot, stop, cgcg,
    # pg, PSP. SEM 'rlc start': naquele ensaio o start leu 0x00000001 antes de
    # escrever, ou seja, quem habilita o F32 e o PSP, e o start nao fez nada.
    # -------------------------------------------------------------------------
    $rlcAntes = Run 'rlc_status_antes' @('rlc','status') -AllowFailure
    $romAntes = if (($rlcAntes.output -join "`n") -match 'EXEC_ROM_CODE (\d)') { [int]$Matches[1] } else { $null }

    $rlcSnap = Run 'rlc_txn_snapshot' @('rlc','txn','snapshot') -AllowFailure
    if ($rlcSnap.exit_code -eq 0) { $rlcSnapshotFeito = $true }
    if (-not $rlcSnapshotFeito) { throw 'Sem snapshot do RLC nao ha caminho de volta: o ensaio para aqui.' }

    $rlcPrep = @()
    foreach ($passo in @('stop','cgcg','pg')) {
        $r = Run "rlc_$passo" @('rlc',$passo) -AllowFailure
        $t = $r.output -join "`n"
        $rlcPrep += [pscustomobject]@{
            passo = $passo
            exit_code = $r.exit_code
            divergencias = $(if ($t -match 'escritas\s+:\s+(\d+), (\d+) divergencias') { [int]$Matches[2] } else { $null })
            saida = $r.output
        }
    }
    $rlcPrepOk = [bool](@($rlcPrep | Where-Object { $_.exit_code -eq 0 -and $_.divergencias -eq 0 }).Count -eq 3)
    if (-not $rlcPrepOk) { throw 'A preparacao do RLC (stop/cgcg/pg) nao aplicou: nao faz sentido seguir para a carga.' }

    Run 'psp_init' @('psp','init') -AllowFailure | Out-Null
    $pspCriado = $true
    # Marcado ANTES da carga: se o PSP foi acionado, o boot conta como gasto,
    # independente do que a carga devolver.
    $firmwareCarregado = $true
    $rlcLoad = Run 'psp_loadfw_rlc' @('psp','loadfw','rlc',(Join-Path $root 'firmware\cyan_skillfish2_rlc.bin')) -AllowFailure
    $rlcResp = if (($rlcLoad.output -join "`n") -match 'RespStatus\s+:\s+0x([0-9A-Fa-f]{8})') { $Matches[1] } else { $null }

    $rlcDepoisTxt = (Run 'rlc_status_depois' @('rlc','status') -AllowFailure).output -join "`n"
    $romDepois = if ($rlcDepoisTxt -match 'EXEC_ROM_CODE (\d)') { [int]$Matches[1] } else { $null }
    $f32Depois = if ($rlcDepoisTxt -match 'ENABLE_F32 (\d)') { [int]$Matches[1] } else { $null }
    $statDepois = if ($rlcDepoisTxt -match 'RLC_GPM_STAT\s+:\s+0x([0-9A-Fa-f]{8})') { $Matches[1] } else { $null }

    # O gate distingue tres estados, porque reprovar por nao ter medido e reprovar
    # por ter medido 1 sao coisas diferentes — e a segunda e um fato de hardware
    # enquanto a primeira e falha de instrumento (irma da licao 17).
    $b2Medido = ($null -ne $romDepois)
    Add-Gate 'B2_rlc_fora_da_rom' `
        ([bool]($b2Medido -and $romDepois -eq 0)) `
        'RLC_GPM_STAT.RLC_EXEC_ROM_CODE = 0: o RLC executa microcodigo, nao a ROM. Pre-requisito do CP.' `
        ([pscustomobject]@{
            exec_rom_code_antes = $romAntes
            exec_rom_code_depois = $romDepois
            enable_f32_depois = $f32Depois
            rlc_gpm_stat_depois = $statDepois
            psp_resp_status = $rlcResp
            preparacao = $rlcPrep
            referencia_linux = 'RLC_GPM_STAT = 0x00B40016, EXEC_ROM_CODE = 0'
            medido = $b2Medido
            nota = $(if ($b2Medido) {
                'Quem habilita o F32 e o PSP ao carregar o ucode, nao um start nosso.'
            } else {
                'NAO MEDIDO: rlc status nao devolveu EXEC_ROM_CODE. Isto e falha de instrumento, nao fato de hardware.'
            })
            sessao_suja_forcada = [bool]$ForcarSessaoSuja
            firmware_ja_carregado_neste_boot = $queimaramEsteBoot
            saida_rlc_status_depois = $rlcDepoisTxt
        })
    if (-not $b2Medido) {
        throw ('Nao consegui LER EXEC_ROM_CODE apos a carga. Nao afirmo nada sobre o RLC: ' +
               'o instrumento falhou, nao o hardware. Saida crua no evidence.json.')
    }
    if ($romDepois -ne 0) {
        throw 'O RLC nao saiu da ROM. Sem esse pre-requisito, o unhalt do CP mediria sobre base ausente.'
    }

    $snap = Run 'snap_cp' @('cp','txn','snapshot') -AllowFailure
    $snapTxt = $snap.output -join "`n"
    $snapRegs = if ($snapTxt -match 'registradores\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    $snapshotFeito = ($snap.exit_code -eq 0 -and $snapRegs -eq $cpTxnCount)
    Add-Gate 'C_snapshot_guardado' `
        ([bool]$snapshotFeito) `
        "Snapshot dos $cpTxnCount registradores do CP capturado com sucesso." `
        ([pscustomobject]@{ exit_code = $snap.exit_code; regs = $snapRegs; saida = $snap.output })

    # -------------------------------------------------------------------------
    # Gate C7: UNGATE DO CLOCK DO CPF
    #
    # CPF e o Command Processor FETCHER: o bloco que executa a busca de instrucao.
    #
    # Windows le CGTT_CPF_CLK_CTRL = 0x00000100; o Linux com o CP executando le
    # 0xF8000100 — os cinco SOFT_OVERRIDE_* ligados. A diferenca estava registrada
    # como pendencia SEM EXPLICACAO desde 2026-08-23, com a nota de que 'devem
    # aparecer sozinhos como consequencia do RLC'. Nao apareceram.
    #
    # A explicacao veio de auditoria de enderecos, nao de hardware: BcGfxInit()
    # escrevia o registrador em 0x18C44, que e o dw 0x50B1 somado a base do
    # SEGMENTO 0. O header autoritativo diz BASE_IDX 1, logo o byte e 0x3C2C4. O
    # ungate nunca chegou ao registrador. Licao 12, outra vez.
    #
    # Diferente dos IC_BASE, este registrador RESPONDE: aqui o readback vale como
    # confirmacao de verdade, e o gate pode exigir o valor exato.
    #
    # ORDEM: vem ANTES do staging, do icinval e do icbase, e nao apenas antes do
    # unhalt. IC_OP_CNTL e IC_BASE sao do dominio do CPF — se o clock dele estiver
    # gated, um clock parado explicaria TAMBEM por que aquelas escritas parecem nao
    # ter efeito nenhum. Ungate primeiro, resto depois.
    # -------------------------------------------------------------------------
    $clk = Run 'cp_clocks' @('cp','clocks') -AllowFailure
    $clkTxt = $clk.output -join "`n"
    $cpfDepois = Read-Reg 'CGTT_CPF_CLK_CTRL_depois' 0x3C2C4
    Add-Gate 'C7_clock_do_cpf_ungated' `
        ([bool]($clk.exit_code -eq 0 -and $null -ne $cpfDepois -and $cpfDepois -eq 0xF8000100L)) `
        ('CGTT_CPF_CLK_CTRL = 0xF8000100 CONFERIDO POR READBACK (registrador legivel, ' +
         'ao contrario dos IC_BASE). Valor medido no Linux com o CP executando.') `
        ([pscustomobject]@{
            exit_code = $clk.exit_code
            CGTT_CPF_CLK_CTRL = Format-Hex $cpfDepois
            esperado = '0xF8000100'
            antes_deste_ensaio = '0x00000100 (os cinco SOFT_OVERRIDE_* em 0)'
            endereco = '0x3C2C4  (dw 0x50B1, BASE_IDX 1)'
            saida = $clk.output
        })

    if ($ControleSemUcode) {
        # Controle negativo: nada de ucode, icbase ou jump table. As engines
        # vao ser liberadas com a cache de instrucao como o boot deixou.
        Add-Gate 'DEFG_pulados_no_controle' `
            $true `
            'Controle negativo: staging, invalidacao, icbase e jump table NAO foram executados de proposito.' `
            ([pscustomobject]@{
                modo = 'ControleSemUcode'
                pergunta = 'O PC 0x0000041E depois do unhalt e ponto de parada comum das engines, ou consequencia do ucode que carregamos?'
                interpretacao = 'Se o PC parar em 0x0000041E tambem aqui, a carga nao teve efeito sobre as engines e a busca de instrucao nunca aconteceu.'
            })
    } else {
        # -------------------------------------------------------------------------
        # Gate D: Carga dos 3 ucodes na VRAM (Staging simultaneo)
        # -------------------------------------------------------------------------
        $stageResults = @()
        foreach ($e in $engines) {
            $off = ('0x{0:X8}' -f $e.offset)
            $st = Run "stage_$($e.nome)" @('cp','stage',$e.nome,$off,$e.arquivo) -AllowFailure
            $stTxt = $st.output -join "`n"
            $stDiv = if ($stTxt -match 'escritos\s+:\s+(\d+)\s+dwords,\s+(\d+)\s+divergencias') { [int]$Matches[2] } else { $null }
            $stDw  = if ($stTxt -match 'escritos\s+:\s+(\d+)\s+dwords') { [int]$Matches[1] } else { $null }
            if ($st.exit_code -eq 0 -and $stDiv -eq 0) { $stagedEngines.Add($e.nome) }
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
            'Os tres ucodes (PFP, ME, CE) gravados e conferidos dword a dword em regioes distintas da VRAM.' `
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
                pedida = ($inv.exit_code -eq 0)
                confirmado = ($invTxt -match 'confirmou por COMPLETE')
                nao_confirmavel = ($invTxt -match 'NAO CONFIRMAVEL')
                saida = $inv.output
            }
        }
        # CORRECAO DE 2026-08-24: este gate exigia 'confirmou por COMPLETE', uma
        # confirmacao que NAO EXISTE neste silicio. IC_OP_CNTL le 0xFFFFFFFF, e
        # 0xFFFFFFFF satisfaz o bit COMPLETE sozinho — quem "confirmava" estava
        # lendo o proprio 0xFFFFFFFF. Exigir prova impossivel nao endurece o ensaio,
        # so garante que ele nunca passe, ou pior, que passe por acidente de mascara.
        #
        # O gate passou a exigir o que da para exigir: a invalidacao foi PEDIDA sem
        # recusa nas tres engines. Se alguma engine confirmar de verdade, isso fica
        # registrado como observacao — e seria novidade, nao requisito.
        $invalPedidas = @($invalResults | Where-Object { $_.pedida }).Count
        Add-Gate 'E_tres_caches_invalidacao_pedida' `
            ([bool]($invalPedidas -eq $engines.Count)) `
            'Invalidacao de cache emitida sem recusa para as tres engines. NAO e confirmacao: IC_OP_CNTL e write-only.' `
            ([pscustomobject]@{
                pedidas = $invalPedidas
                de = $engines.Count
                alguma_confirmou_por_complete = [bool](@($invalResults | Where-Object { $_.confirmado }).Count -gt 0)
                nota = 'COMPLETE e o bit 1 e 0xFFFFFFFF o satisfaz. Confirmacao por COMPLETE aqui seria leitura do proprio 0xFFFFFFFF.'
                por_engine = $invalResults
            })

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

    }
    # -------------------------------------------------------------------------
    # Gate H: Execucao do UNHALT
    # -------------------------------------------------------------------------
    $unh = Run 'cp_unhalt' @('cp','unhalt') -AllowFailure
    $unhTxt = $unh.output -join "`n"
    $unhaltFeito = ($unh.exit_code -eq 0)
    $meCntlAfterUnhalt = Read-Reg 'unhalt_CP_ME_CNTL' 0x086D8
    Add-Gate 'H_unhalt_executado' `
        ([bool]($unhaltFeito -and ($meCntlAfterUnhalt -band 0x15000000) -eq 0)) `
        'CP_ME_CNTL configurado com ME_HALT=0, PFP_HALT=0, CE_HALT=0 (0x00000000).' `
        ([pscustomobject]@{
            exit_code = $unh.exit_code
            CP_ME_CNTL = Format-Hex $meCntlAfterUnhalt
            saida = $unh.output
        })

    # -------------------------------------------------------------------------
    # Gate I: Verificacao do movimento dos ponteiros de instrucao (EXECUCAO REAL)
    # -------------------------------------------------------------------------
    $pntrsRunning = Read-Pntrs 'running'
    # Os ponteiros de instrucao de engines ativas saem de 0x00000000 e entram no loop do microcodigo.
    $pfpPc = $pntrsRunning.primeira.CP_PFP_INSTR_PNTR
    $mePc  = $pntrsRunning.primeira.CP_ME_INSTR_PNTR
    $cePc  = $pntrsRunning.primeira.CP_CE_INSTR_PNTR
    # CORRECAO DE 2026-08-24: este gate aceitava PC apenas DIFERENTE DE ZERO,
    # mesmo constante. E exatamente o falso positivo do 0x0000041E: uma
    # microengine sem ucode valido para num valor fixo do bootloader, e PC parado
    # nao e execucao.
    #
    # A prova passou a ser VARIACAO entre amostras. Sair de zero continua sendo
    # registrado, como observacao mais fraca, mas nao aprova nada por si.
    $saiuDeZero = ($pfpPc -ne '0x00000000' -or $mePc -ne '0x00000000' -or $cePc -ne '0x00000000')
    $variouEntreAmostras = [bool]$pntrsRunning.algum_em_movimento

    if ($ControleSemUcode) {
        # No controle, execucao NAO e esperada: o gate registra o ponto de parada
        # e passa, porque reprovar aqui seria reprovar o controle por cumprir o
        # proprio papel. A comparacao com a execucao normal e que informa.
        Add-Gate 'I_controle_ponto_de_parada' `
            $true `
            'Controle negativo: registra em que PC as engines param SEM ucode, para comparar com a execucao normal.' `
            ([pscustomobject]@{
                PFP_PC = $pfpPc
                ME_PC  = $mePc
                CE_PC  = $cePc
                variou_entre_amostras = $variouEntreAmostras
                os_tres_iguais = [bool]($pfpPc -eq $mePc -and $mePc -eq $cePc)
                medido_com_ucode_em_00_19 = '0x0000041E nos tres, constante em 8 amostras'
                leitura = $(if ($pfpPc -eq '0x0000041E') {
                    'IGUAL ao valor com ucode: a carga nao teve efeito sobre as engines. A busca de instrucao nunca aconteceu.'
                } else {
                    'DIFERENTE do valor com ucode: a carga mudou o comportamento das engines.'
                })
                amostras = $pntrsRunning.amostras
            })
    } else {
    Add-Gate 'I_ponteiros_de_instrucao_ativos' `
        $variouEntreAmostras `
        'Ao menos um ponteiro de instrucao VARIOU entre amostras. PC constante nao e execucao, mesmo diferente de zero.' `
        ([pscustomobject]@{
            PFP_PC = $pfpPc
            ME_PC  = $mePc
            CE_PC  = $cePc
            variou_entre_amostras = $variouEntreAmostras
            saiu_de_zero = $saiuDeZero
            nota = 'saiu_de_zero sozinho NAO aprova: uma engine sem ucode para num valor fixo do bootloader (medido: 0x0000041E).'
            os_tres_iguais = [bool]($pfpPc -eq $mePc -and $mePc -eq $cePc)
            nota_tres_iguais = 'Tres microengines com tres ucodes diferentes nao param no mesmo endereco se estiverem executando o proprio ucode. No Linux com o CP vivo os valores sao 0x172E / 0x0E19 / 0x05CF.'
            valores_distintos_por_ponteiro = $pntrsRunning.valores_distintos
            amostras = $pntrsRunning.amostras
        })
    }

    # -------------------------------------------------------------------------
    # Gate J: Reativacao do HALT
    # -------------------------------------------------------------------------
    $hlt = Run 'cp_halt' @('cp','halt') -AllowFailure
    $meCntlAfterHalt = Read-Reg 'halt_CP_ME_CNTL' 0x086D8
    $pntrsAfterHalt = Read-Pntrs 'after_halt'
    Add-Gate 'J_halt_restaurado_e_parado' `
        ([bool]($hlt.exit_code -eq 0 -and $meCntlAfterHalt -eq 0x15000000 -and -not $pntrsAfterHalt.algum_em_movimento)) `
        'CP_ME_CNTL = 0x15000000 reativado e ponteiros de instrucao estabilizados.' `
        ([pscustomobject]@{
            CP_ME_CNTL = Format-Hex $meCntlAfterHalt
            ponteiros = $pntrsAfterHalt.primeira
        })

    # -------------------------------------------------------------------------
    # PASSAGEM B: o mesmo caminho, com EXE_DISABLE = 1
    #
    # O CP acabou de voltar ao HALT no gate J, e o ucode continua na VRAM: nada
    # precisa ser recarregado. A unica coisa que muda em relacao a passagem A e
    # UM BIT do IC_BASE_CNTL. Isso e o que torna a comparacao valida.
    # -------------------------------------------------------------------------
    $icbB = @()
    foreach ($e in $engines) {
        $off = ('0x{0:X8}' -f $e.offset)
        $r = Run "icbase_B_$($e.nome)" @('cp','icbase',$e.nome,$off,'-exedisable') -AllowFailure
        $icbB += [pscustomobject]@{
            engine = $e.nome
            exit_code = $r.exit_code
            confirmado = (($r.output -join "`n") -match 'ok, IC_BASE apontado para o ucode')
            saida = $r.output
        }
    }
    Add-Gate 'N_icbase_reescrito_com_exe_disable' `
        ([bool](@($icbB | Where-Object { $_.confirmado }).Count -eq $engines.Count)) `
        ('As tres engines receberam IC_BASE_CNTL com EXE_DISABLE = 1. Continua ' +
         'WRITE-ONLY: isto atesta emissao sem recusa, nao aceitacao.') `
        $icbB

    foreach ($e in $engines) {
        $off = ('0x{0:X8}' -f $e.offset)
        Run "icinval_B_$($e.nome)" @('cp','icinval',$e.nome) -AllowFailure | Out-Null
        Run "jt_B_$($e.nome)" @('cp','jt',$e.nome,$off,$e.arquivo) -AllowFailure | Out-Null
    }

    $unhB = Run 'cp_unhalt_B' @('cp','unhalt') -AllowFailure
    $meCntlB = Read-Reg 'unhalt_B_CP_ME_CNTL' 0x086D8
    Add-Gate 'O_unhalt_da_passagem_B' `
        ([bool]($unhB.exit_code -eq 0 -and $null -ne $meCntlB -and ($meCntlB -band 0x15000000) -eq 0)) `
        'Segundo unhalt executado: CP_ME_CNTL de volta a 0x00000000.' `
        ([pscustomobject]@{ exit_code = $unhB.exit_code; CP_ME_CNTL = Format-Hex $meCntlB; saida = $unhB.output })

    $pntrsB = Read-Pntrs 'B'
    $variouB = [bool]$pntrsB.algum_em_movimento

    # A COMPARACAO. Este e o unico gate deste ensaio que responde a pergunta.
    $pcA = ($gates | Where-Object { $_.gate -eq 'I_ponteiros_de_instrucao_ativos' }).observed
    # Sem a passagem A nao existe diferencial. StrictMode 1.0 NAO erra em
    # propriedade inexistente: sem esta guarda, $pcA nulo daria $null nos tres
    # PCs, a comparacao diria 'sem diferenca' e o ensaio concluiria o oposto do
    # que mediu. Licao 2, no lugar mais caro possivel.
    if ($null -eq $pcA -or $null -eq $pcA.amostras -or $pcA.amostras.Count -lt 1) {
        throw 'A passagem A nao produziu amostras. Sem elas o diferencial nao existe, e nao afirmo nada.'
    }
    $pfpA = $pcA.amostras[0].CP_PFP_INSTR_PNTR
    $meA  = $pcA.amostras[0].CP_ME_INSTR_PNTR
    $ceA  = $pcA.amostras[0].CP_CE_INSTR_PNTR
    $pfpB = $pntrsB.amostras[0].CP_PFP_INSTR_PNTR
    $meB  = $pntrsB.amostras[0].CP_ME_INSTR_PNTR
    $ceB  = $pntrsB.amostras[0].CP_CE_INSTR_PNTR
    # Guarda pela MESMA razao que existe do lado de A: StrictMode 1.0 nao erra em
    # propriedade inexistente, e foi assim que a primeira versao deste ensaio
    # comparou 0x041E com $null e anunciou 'o bit mudou o comportamento' na
    # execucao que media o contrario. Sem os tres valores, nao ha diferencial.
    if ($null -eq $pfpB -or $null -eq $meB -or $null -eq $ceB) {
        throw ('A passagem B nao devolveu os tres ponteiros. Sem eles nao existe ' +
               'diferencial, e comparar com $null anunciaria diferenca onde nao ha.')
    }
    $houveDiferenca = (($pfpA -ne $pfpB) -or ($meA -ne $meB) -or ($ceA -ne $ceB))

    # Gate deliberadamente SEMPRE verdadeiro: os dois resultados sao informativos
    # e opostos, e reprovar um deles esconderia metade da informacao atras de um
    # vermelho. Licao 25 — a saida diz o que foi MEDIDO.
    Add-Gate 'P_diferencial_do_exe_disable' `
        $true `
        ('OBSERVACAO, nao aprovacao: compara o PC entre EXE_DISABLE = 0 e = 1, no ' +
         'mesmo boot, com o mesmo ucode e o mesmo ungate de CPF. Diferenca prova ' +
         'que a escrita de IC_BASE_CNTL chega ao hardware; igualdade bit a bit e ' +
         'evidencia positiva de que ela nao produz efeito nenhum.') `
        ([pscustomobject]@{
            A_exe_disable_0 = [pscustomobject]@{
                PFP = ('0x{0:X8}' -f $pfpA); ME = ('0x{0:X8}' -f $meA); CE = ('0x{0:X8}' -f $ceA)
            }
            B_exe_disable_1 = [pscustomobject]@{
                PFP = ('0x{0:X8}' -f $pfpB); ME = ('0x{0:X8}' -f $meB); CE = ('0x{0:X8}' -f $ceB)
            }
            houve_diferenca = $houveDiferenca
            variou_entre_amostras_em_B = $variouB
            amostras_B = $pntrsB.amostras
            valores_distintos_em_B = $pntrsB.valores_distintos
            leitura = $(if ($houveDiferenca) {
                'O bit mudou o comportamento: as escritas de IC_BASE_CNTL CHEGAM ao hardware.'
            } else {
                'Nenhuma diferenca: as escritas de IC_BASE_CNTL nao produzem efeito observavel.'
            })
        })

    Run 'halt_B' @('cp','halt') -AllowFailure | Out-Null

    # -------------------------------------------------------------------------
    # Gate K: Rollback da VRAM de todas as engines
    # -------------------------------------------------------------------------
    $rb = Run 'cp_rollback_all' @('cp','rollback','all') -AllowFailure
    $rbTxt = $rb.output -join "`n"
    $rbDiv = if ($rbTxt -match 'devolvidos\s+:\s+(\d+),\s+(\d+)\s+divergencias') { [int]$Matches[2] } else { $null }
    $rbDw  = if ($rbTxt -match 'devolvidos\s+:\s+(\d+)\s+dwords') { [int]$Matches[1] } else { $null }
    if (-not $ControleSemUcode) {
    Add-Gate 'K_ucodes_devolvidos_sem_divergencia' `
        ([bool]($rb.exit_code -eq 0 -and $rbDiv -eq 0)) `
        'Regioes de VRAM das tres engines devolvidas e conferidas dword a dword sem divergencia.' `
        ([pscustomobject]@{ exit_code = $rb.exit_code; dwords = $rbDw; divergencias = $rbDiv; saida = $rb.output })
    }

    # -------------------------------------------------------------------------
    # Gate L: Restore dos registradores da transacao
    # -------------------------------------------------------------------------
    $res = Run 'restore_cp' @('cp','txn','restore') -AllowFailure
    $resTxt = $res.output -join "`n"
    $resDiv = if ($resTxt -match 'divergencias\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    $resRest = if ($resTxt -match 'restaurados\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    Add-Gate 'L_registradores_devolvidos' `
        ([bool]($res.exit_code -eq 0 -and $resDiv -eq 0 -and $resRest -eq $cpTxnCount)) `
        "Todos os $cpTxnCount registradores do CP devolvidos aos valores do snapshot." `
        ([pscustomobject]@{ exit_code = $res.exit_code; restaurados = $resRest; divergencias = $resDiv; saida = $res.output })

} finally {
    # Teardown de seguranca caso ocorra interrupcao
    if ($armado) {
        if ($unhaltFeito) {
            Run 'teardown_cp_halt' @('cp','halt') -AllowFailure | Out-Null
        }
        Run 'teardown_rollback_all' @('cp','rollback','all') -AllowFailure | Out-Null
        if ($snapshotFeito) {
            Run 'teardown_restore_cp' @('cp','txn','restore') -AllowFailure | Out-Null
        }
        if ($rlcSnapshotFeito) {
            Run 'teardown_rlc_restore' @('rlc','txn','restore') -AllowFailure | Out-Null
        }
        if ($pspCriado) {
            Run 'teardown_psp_rollback' @('psp','rollback') -AllowFailure | Out-Null
        }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}
} catch {
    $abortadoPor = $_.Exception.Message
    Write-Host ""
    Write-Host "ENSAIO ABORTADO: $abortadoPor" -ForegroundColor Yellow
    Write-Host "A evidencia dos gates ja medidos vai ser gravada." -ForegroundColor Yellow
}

# -----------------------------------------------------------------------------
# Gate M: Sem BugCheck
# -----------------------------------------------------------------------------
# CORRECAO DE 2026-08-24: este gate estava fixado em $true e nunca consultou o
# log de eventos. Gate que sempre passa e pior que gate ausente — ele afirma.
$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'M_sem_bugcheck' ($bugchecks.Count -eq 0) `
    'Nenhum bugcheck registrado no log do sistema na janela do ensaio.' `
    ([pscustomobject]@{ quantidade = $bugchecks.Count; eventos = $bugchecks })

# Gerar relatorios de evidencia
#
# Aborto nao e aprovacao. E tambem nao transforma gate nao alcancado em reprovado:
# gate que nao rodou nao existe naquela execucao (licao 7). O relatorio diz
# quantos foram medidos, de quantos, e por que parou.
$totalGatesPrevistos = 18
$passouGeral = [bool]($null -eq $abortadoPor -and
                      @($gates | Where-Object { -not $_.pass }).Count -eq 0 -and
                      $gates.Count -eq $totalGatesPrevistos)

$relatorioTxt = [System.Text.StringBuilder]::new()
[void]$relatorioTxt.AppendLine("================================================================================")
[void]$relatorioTxt.AppendLine("  RELATORIO DE ENSAIO DE UNHALT E EXECUCAO DO CP (PASSO 3)")
[void]$relatorioTxt.AppendLine("================================================================================")
[void]$relatorioTxt.AppendLine("Data/Hora : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
[void]$relatorioTxt.AppendLine("Host      : $env:COMPUTERNAME")
[void]$relatorioTxt.AppendLine("Driver    : $workspaceDriver (SHA256: $driverHash)")
[void]$relatorioTxt.AppendLine("Resultado : $(if ($passouGeral) { 'TODOS OS GATES PASSARAM' } elseif ($abortadoPor) { 'ABORTADO' } else { 'FALHA EM GATES' })")
[void]$relatorioTxt.AppendLine("Gates     : $($gates.Count) medidos de $totalGatesPrevistos previstos")
[void]$relatorioTxt.AppendLine("Boot      : $($ultimoBoot.ToString('yyyy-MM-dd HH:mm:ss'))$(if ($ForcarSessaoSuja) { '  (SESSAO SUJA FORCADA)' })")
if ($abortadoPor) {
    [void]$relatorioTxt.AppendLine("Abortou   : $abortadoPor")
    [void]$relatorioTxt.AppendLine("            Os gates nao alcancados NAO reprovaram: nao existem nesta execucao.")
}
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
    # Declarado de forma conservadora: basta a TENTATIVA de carga pelo PSP, sem
    # olhar o RespStatus. Se o PSP foi acionado, o estado pode ter mudado, e o
    # proximo ensaio tem de saber disso pelo arquivo, nao pela memoria de quem
    # rodou. Sem este campo, o preflight de boot limpo nao funciona.
    reboot_required = [bool]$firmwareCarregado
    aborted = ($null -ne $abortadoPor)
    aborted_reason = $abortadoPor
    gates_medidos = $gates.Count
    gates_previstos = $totalGatesPrevistos
    last_boot = $ultimoBoot
    sessao_suja_forcada = [bool]$ForcarSessaoSuja
    firmware_ja_carregado_neste_boot = $queimaramEsteBoot
    controle_sem_ucode = [bool]$ControleSemUcode
    modo = $modoEnsaio
    gates = $gates
    events = $events
}
$jsonObj | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $evidenciaJsonPath -Encoding UTF8

Write-Host ""
$obsAB = ($gates | Where-Object { $_.gate -eq 'P_diferencial_do_exe_disable' }).observed
if ($null -eq $obsAB) {
    Write-Host "O diferencial NAO foi alcancado: o ensaio abortou antes da passagem B." -ForegroundColor Yellow
    Write-Host "  Nenhuma conclusao sobre o IC_BASE. Inspecione $dir" -ForegroundColor Yellow
} else {
    Write-Host "DIFERENCIAL DO EXE_DISABLE, mesmo boot, so um bit de diferenca:" -ForegroundColor Cyan
    Write-Host ("  A (EXE_DISABLE=0): {0} / {1} / {2}" -f $obsAB.A_exe_disable_0.PFP, $obsAB.A_exe_disable_0.ME, $obsAB.A_exe_disable_0.CE) -ForegroundColor Cyan
    Write-Host ("  B (EXE_DISABLE=1): {0} / {1} / {2}" -f $obsAB.B_exe_disable_1.PFP, $obsAB.B_exe_disable_1.ME, $obsAB.B_exe_disable_1.CE) -ForegroundColor Cyan
    if ($obsAB.houve_diferenca) {
        Write-Host "  ACHADO: o bit MUDOU o comportamento." -ForegroundColor Green
        Write-Host "  As escritas de IC_BASE_CNTL CHEGAM ao hardware. O registrador e" -ForegroundColor Green
        Write-Host "  aceito, e a pergunta passa a ser por que a busca nao resolve." -ForegroundColor Green
    } else {
        Write-Host "  ACHADO: identico bit a bit." -ForegroundColor Yellow
        Write-Host "  As escritas de IC_BASE_CNTL nao produzem efeito observavel. A" -ForegroundColor Yellow
        Write-Host "  suspeita de que o bloco nao aceita escrita deixa de ser" -ForegroundColor Yellow
        Write-Host "  inverificavel: agora tem evidencia." -ForegroundColor Yellow
    }
    if ($obsAB.variou_entre_amostras_em_B) {
        Write-Host "  E o PC VARIOU entre amostras na passagem B." -ForegroundColor Green
    }
}
if (-not $passouGeral) {
    Write-Host "  Gates reprovados: inspecione o relatorio em $dir" -ForegroundColor Yellow
}
Write-Host "  Veredito do passo 3: NAO VALIDADO EM HARDWARE." -ForegroundColor Yellow
