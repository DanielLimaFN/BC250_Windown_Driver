<#
    Passo 3 do gate de memoria: programar valores NOVOS nos registradores de
    traducao, na ordem de gfxhub_v2_0.c, cada escrita com readback conferido.

    Este ensaio exercita APENAS os estagios que nao substituem o bypass do POST:

      gart      aponta CONTEXT0 para a page table do passo 1, sem habilitar nada
      system    aberturas AGP e de sistema, pagina default de falha
      identity  abertura de identidade do contexto 1, vazia como no upstream
      invrange  faixa de endereco das engines 0 e 17 de invalidacao. A 17 e a
                usada pela GART no upstream, e e a que o passo 4 dispara.

    Os estagios tlb, cache e context0 ficam FORA de proposito. Sao eles que
    trocam SYSTEM_ACCESS_MODE, VMC_TAP_*_REQUEST_PHYSICAL e ENABLE_CONTEXT, isto
    e, substituem o caminho de memoria do qual todo o sucesso medido depende.
    Exigem -danger no CLI e um ensaio proprio, depois de este passar.

    A prova de que os estagios seguros sao realmente seguros nao e o readback:
    e uma submissao SDMA com fence fisica e dado conferido no kernel, feita
    DEPOIS de 26 valores novos estarem escritos. Readback diz que o registrador
    aceitou; a submissao diz que a memoria continua alcancavel.

    Dois controles negativos, ambos exigindo recusa:
      1. 'gcvm program' ANTES do snapshot -> tem de ser recusado sem escrever;
      2. 'gcvm program tlb' sem -danger  -> tem de ser recusado sem escrever.

    Este ensaio carrega firmware SDMA e portanto exige boot limpo e deixa a
    sessao contaminada ate o proximo reboot.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 20,
    [ValidateSet(64,128,256,512,1024)][int]$RingDwords = 128,
    [uint32]$TableOffset = 0x0FF10000,
    [uint32]$TargetOffset = 0x0FD00000,
    [uint32]$DummyOffset = 0x0FF20000,
    [ValidateRange(1,512)][int]$PageCount = 512
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$firmware = Join-Path $root 'firmware\cyan_skillfish2_sdma.bin'
$ringOffset = [uint32]0x0FF00000
$destination = [uint32]0x0FF04000
$savedDwords = 12
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

# Faixa de VA da page table: PageCount paginas de 4 KB a partir de zero.
$ptStartVa = [uint64]0
$ptEndVa = [uint64](($PageCount * 4096) - 1)

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute; ele carrega firmware SDMA (ativo ate o reboot) e escreve valores NOVOS em registradores de traducao.'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$abiHeader,$workspaceDriver,$installedDriver,$firmware)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

# A contagem de registradores da transacao sai do header, nao de numero fixo no
# ensaio. Ela cresceu de 24 para 33 quando o passo 3 passou a escrever de
# verdade, e um numero fixo aqui reprovaria o ensaio por motivo cosmetico —
# exatamente o defeito que as versoes de ABI fixadas causaram antes.
$txnRegCount = $null
foreach ($linha in Get-Content -LiteralPath $abiHeader) {
    if ($linha -match 'BC250DBG_GCVM_TXN_REG_COUNT\s+(\d+)u') { $txnRegCount = [int]$Matches[1]; break }
}
if (-not $txnRegCount) { throw 'Nao foi possivel ler BC250DBG_GCVM_TXN_REG_COUNT do header da ABI.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-gcvm-program"
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
function Read-Vram([string]$Name, [uint32]$Offset) {
    $r = Run $Name @('vram','r',('0x{0:X8}' -f $Offset)); $t = $r.output -join ' '
    if ($t -notmatch '=\s*0x([0-9A-Fa-f]{8})') { throw "Parse de VRAM falhou: $Name" }
    return [Convert]::ToUInt32($Matches[1],16)
}
function Invoke-Write([string]$Name, [uint32]$Offset, [uint32[]]$Data) {
    $cmd = @('sdma','write',('0x{0:X8}' -f $Offset)) + @($Data | ForEach-Object { '0x{0:X8}' -f $_ })
    $r = Run $Name $cmd -AllowFailure
    $text = $r.output -join "`n"
    return [pscustomobject]@{
        name = $Name
        exit_code = $r.exit_code
        completed = ($text -match 'Conclusao do pacote : SIM')
        fence_reached = ($text -match '\[ALCANCADO\]')
        data_ok = ($text -match 'Resultado completo\s+: VALIDADO NESTA OPERACAO')
        dados = @($r.output | Where-Object { $_ -match 'Dados conferidos' }) -join ''
    }
}
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}
function Get-Divergencias([object]$Resultado) {
    foreach ($linha in $Resultado.output) {
        if ($linha -match 'divergencias\s+:\s+(\d+)') { return [int]$Matches[1] }
    }
    return $null
}
function Get-DumpMap([object]$Resultado) {
    $m = @{}
    foreach ($linha in $Resultado.output) {
        if ($linha -match '^\s{2}([A-Z0-9_]+)\s*:\s*(.+?)\s*$') { $m[$Matches[1]] = $Matches[2] }
    }
    return $m
}
function Compare-Dump($Antes, $Depois) {
    $mudados = @()
    foreach ($campo in $Antes.Keys) {
        if ($Depois.ContainsKey($campo) -and $Depois[$campo] -ne $Antes[$campo]) {
            $mudados += "$campo : $($Antes[$campo]) -> $($Depois[$campo])"
        }
    }
    return $mudados
}
# Um estagio so passa se o CLI aceitou, se o numero de escritas bate com o
# esperado e se NENHUMA delas divergiu no readback.
function Invoke-Stage([string]$Nome, [int]$EscritasEsperadas, [string[]]$Extra) {
    $cmdArgs = @('gcvm','program',$Nome,'-table',('0x{0:X8}' -f $TableOffset),'-dummy',('0x{0:X8}' -f $DummyOffset),
              '-start',('0x{0:X}' -f $ptStartVa),'-end',('0x{0:X}' -f $ptEndVa))
    if ($Extra) { $cmdArgs += $Extra }
    $r = Run "program_$Nome" $cmdArgs -AllowFailure
    $texto = $r.output -join "`n"
    $escritas = if ($texto -match 'escritas\s+:\s+(\d+),\s+(\d+) divergencias') { [int]$Matches[1] } else { $null }
    $div = if ($texto -match 'escritas\s+:\s+(\d+),\s+(\d+) divergencias') { [int]$Matches[2] } else { $null }
    return [pscustomobject]@{
        nome = $Nome
        exit_code = $r.exit_code
        escritas = $escritas
        escritas_esperadas = $EscritasEsperadas
        divergencias = $div
        ok = ($r.exit_code -eq 0 -and $escritas -eq $EscritasEsperadas -and $div -eq 0)
        saida = $r.output
    }
}

# Campos do dump que os estagios seguros TEM permissao de mudar. Qualquer campo
# fora desta lista que mude denuncia escrita nao prevista.
$camposPermitidos = @(
    'CTX0_PT_BASE','CTX0_PT_START','CTX0_PT_END',
    'AGP_BASE','AGP_BOT','AGP_TOP',
    'SYS_APERTURE_LOW','SYS_APERTURE_HIGH','SYS_APERTURE_DEFAULT',
    'FAULT_DEFAULT_ADDR','FAULT_CNTL2',
    'IDENTITY_APER_LOW','IDENTITY_APER_HIGH'
)

$saved = @(); $restored = @(); $failure = $null
$armed = $false; $pspCreated = $false; $sdmaCreated = $false; $firmwareAccepted = $false
$snapshotTaken = $false; $ptBuilt = $false
$estagios = @()

try {
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+\s+armamento=readonly') {
        throw 'Preflight recusado: info nao reportou ABI, ou armamento nao esta em readonly.'
    }
    $sdmaPre = Run 'sdma_status_pre' @('sdma','status')
    $t = $sdmaPre.output -join ' '
    if ($t -notmatch 'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})') { throw 'Nao foi possivel validar WPTR.' }
    if ([Convert]::ToUInt32($Matches[1],16) -ne 0) { throw 'Preflight recusado: WPTR residual. Reinicie.' }
    $ucode = Run 'ucode_checksum_pre' @('reg','r','0x04A24')
    if (($ucode.output -join ' ') -notmatch '=\s*0x00000000') {
        throw 'Preflight recusado: firmware SDMA ja ativo. Reinicie.'
    }
    for ($i = 0; $i -lt $savedDwords; $i++) { $saved += Read-Vram "dest_pre_$i" ($destination + 4*$i) }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true

    # Semaforo em estado conhecido antes de qualquer coisa. Capturas anteriores
    # a 2026-08-23 adquiriam o semaforo da engine de invalidacao ao ler SEM.
    Run 'gcvm_semrelease' @('gcvm','semrelease') -AllowFailure | Out-Null

    Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true
    Run 'psp_loadfw' @('psp','loadfw','sdma0',$firmware) | Out-Null; $firmwareAccepted = $true
    Run 'sdma_f32' @('sdma','f32') | Out-Null
    Run 'sdma_init' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") | Out-Null
    $sdmaCreated = $true

    # A: base. Sem isto nada abaixo distingue efeito colateral de caminho que
    # nunca funcionou.
    $w1 = Invoke-Write 'escrita_antes_do_passo3' $destination @([uint32]0xBC25CC01L,[uint32]0xBC25CC02L,[uint32]0xBC25CC03L,[uint32]0xBC25CC04L)
    Add-Gate 'A_bypass_funciona_antes' ($w1.exit_code -eq 0 -and $w1.completed -and $w1.data_ok) `
        'Submissao com fence fisica e dado conferido antes de escrever valor novo.' $w1
    if (-not ($w1.exit_code -eq 0 -and $w1.completed -and $w1.data_ok)) {
        throw 'A engine nao escreveu antes do passo 3: sem base para medir.'
    }

    $dumpInicial = Run 'gcvm_dump_inicial' @('gcvm','dump')
    $mapaInicial = Get-DumpMap $dumpInicial

    # B: controle negativo. Sem snapshot aberto, o driver tem de recusar. Se ele
    # escrever aqui, a rede de seguranca do passo 2 nao existe na pratica.
    $negSemTxn = Invoke-Stage 'gart' 0 $null
    $recusouSemTxn = ($negSemTxn.exit_code -ne 0 -and
                      ($negSemTxn.saida -join ' ') -match 'nenhum snapshot de transacao aberto' -and
                      ($negSemTxn.escritas -eq 0 -or $null -eq $negSemTxn.escritas))
    Add-Gate 'B_recusa_sem_snapshot' $recusouSemTxn `
        'Sem caminho de volta provado, o driver recusa programar e nao escreve nada.' $negSemTxn

    # C: e a recusa foi realmente sem efeito? Recusa que escreve e pior que
    # aceite honesto, porque a saida diz que nada aconteceu.
    $dumpPosRecusa = Run 'gcvm_dump_pos_recusa' @('gcvm','dump')
    $mudadosRecusa = Compare-Dump $mapaInicial (Get-DumpMap $dumpPosRecusa)
    Add-Gate 'C_recusa_sem_efeito' ($mudadosRecusa.Count -eq 0) `
        'A recusa nao deixou marca: dump de traducao identico.' `
        ([pscustomobject]@{ campos_mudados = $mudadosRecusa })

    # A page table precisa existir antes de CONTEXT0 apontar para ela. O passo 1
    # ja provou este caminho; aqui ele e pre-requisito, nao a prova.
    $pt = Run 'gcvm_ptbuild' @('gcvm','ptbuild',('0x{0:X8}' -f $TableOffset),('0x{0:X8}' -f $TargetOffset),"$PageCount") -AllowFailure
    # O ptbuild imprime "conferencia : N divergencias em M entradas": o numero
    # vem ANTES da palavra, e Get-Divergencias procura o oposto. Usar o helper
    # generico aqui devolvia $null e reprovava este gate com a saida crua
    # dizendo "0 divergencias em 512 entradas" (medido em 2026-08-23 12:59).
    $conf = if (($pt.output -join ' ') -match 'conferencia\s+:\s+(\d+) divergencias em (\d+) entradas') {
        [pscustomobject]@{ divergencias = [int]$Matches[1]; entradas = [int]$Matches[2] }
    } else { $null }
    if ($pt.exit_code -eq 0) { $ptBuilt = $true }
    Add-Gate 'D_page_table_construida' `
        ($pt.exit_code -eq 0 -and $null -ne $conf -and $conf.divergencias -eq 0 -and $conf.entradas -eq $PageCount) `
        'A tabela que CONTEXT0 vai apontar existe e foi conferida entrada por entrada.' `
        ([pscustomobject]@{ conferencia = $conf; saida = $pt.output })
    if ($pt.exit_code -ne 0) { throw 'Sem page table conferida, apontar CONTEXT0 para ela seria escrita por suposicao.' }

    $snap = Run 'gcvm_txn_snapshot' @('gcvm','txn','snapshot') -AllowFailure
    if ($snap.exit_code -eq 0) { $snapshotTaken = $true }
    Add-Gate 'E_snapshot_guardado' `
        ($snap.exit_code -eq 0 -and (($snap.output -join ' ') -match ('registradores\s+:\s+' + $txnRegCount))) `
        "Os $txnRegCount registradores de configuracao ficam guardados no driver." `
        ([pscustomobject]@{ exit_code = $snap.exit_code; esperados = $txnRegCount; saida = $snap.output })
    if (-not $snapshotTaken) { throw 'Sem snapshot nao ha caminho de volta: o ensaio para aqui.' }

    # F..I: os estagios seguros, na ordem do upstream.
    $estagios += Invoke-Stage 'gart' 6 $null
    $estagios += Invoke-Stage 'system' 10 $null
    $estagios += Invoke-Stage 'identity' 6 $null
    $estagios += Invoke-Stage 'invrange' 4 $null

    Add-Gate 'F_estagio_gart' ($estagios[0].ok) `
        'CONTEXT0 aponta para a page table: 6 escritas, todas confirmadas por readback.' $estagios[0]
    Add-Gate 'G_estagio_system' ($estagios[1].ok) `
        'Aberturas AGP e de sistema e pagina default de falha: 10 escritas confirmadas.' $estagios[1]
    Add-Gate 'H_estagio_identity' ($estagios[2].ok) `
        'Abertura de identidade vazia, valores literais do upstream: 6 escritas confirmadas.' $estagios[2]
    Add-Gate 'I_estagio_invrange' ($estagios[3].ok) `
        'Faixa das engines 0 e 17 de invalidacao: 4 escritas confirmadas. A 17 e a da GART.' $estagios[3]

    # J: segundo controle negativo. O estagio que substitui o bypass tem de ser
    # recusado sem o consentimento explicito, mesmo com snapshot aberto e armado.
    $negSemFlag = Invoke-Stage 'tlb' 0 $null
    $recusouSemFlag = ($negSemFlag.exit_code -ne 0 -and
                       ($negSemFlag.saida -join ' ') -match 'substitui o bypass do POST' -and
                       ($negSemFlag.escritas -eq 0 -or $null -eq $negSemFlag.escritas))
    Add-Gate 'J_estagio_perigoso_recusado' $recusouSemFlag `
        'tlb, cache e context0 exigem consentimento explicito; sem ele nao escrevem.' $negSemFlag

    # K: o dump mudou, e mudou SO onde podia. Este gate e o que separa "escrevi
    # o que queria" de "escrevi tambem o que nao vi".
    $dumpProgramado = Run 'gcvm_dump_programado' @('gcvm','dump')
    $mapaProgramado = Get-DumpMap $dumpProgramado
    $mudadosPrograma = Compare-Dump $mapaInicial $mapaProgramado
    $camposMudados = @($mudadosPrograma | ForEach-Object { ($_ -split ' :')[0].Trim() })
    $foraDoPrevisto = @($camposMudados | Where-Object { $camposPermitidos -notcontains $_ })
    $ptBaseMudou = ($mapaInicial['CTX0_PT_BASE'] -ne $mapaProgramado['CTX0_PT_BASE'])
    Add-Gate 'K_mudou_so_o_previsto' ($foraDoPrevisto.Count -eq 0 -and $ptBaseMudou) `
        'CTX0_PT_BASE realmente mudou, e nenhum campo fora da lista prevista mudou.' `
        ([pscustomobject]@{
            campos_mudados = $mudadosPrograma
            fora_do_previsto = $foraDoPrevisto
            pt_base_mudou = $ptBaseMudou
        })

    # L: a prova que importa. Com 26 valores novos escritos, a memoria continua
    # alcancavel? Readback nao responde isso; submissao com dado conferido sim.
    $w2 = Invoke-Write 'escrita_com_valores_novos' ($destination + 16) @([uint32]0xBC25DD01L,[uint32]0xBC25DD02L,[uint32]0xBC25DD03L,[uint32]0xBC25DD04L)
    Add-Gate 'L_memoria_alcancavel_com_valores_novos' ($w2.exit_code -eq 0 -and $w2.completed -and $w2.data_ok) `
        'Com os estagios seguros aplicados, a submissao SDMA continua escrevendo de verdade.' $w2

    # M: rollback dos registradores.
    $rest = Run 'gcvm_txn_restore' @('gcvm','txn','restore') -AllowFailure
    $divR = Get-Divergencias $rest
    $restaurados = if (($rest.output -join ' ') -match 'restaurados\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    Add-Gate 'M_rollback_devolve_tudo' ($rest.exit_code -eq 0 -and $divR -eq 0 -and $restaurados -eq $txnRegCount) `
        "Os $txnRegCount registradores voltam ao valor do snapshot, com readback conferido." `
        ([pscustomobject]@{ restaurados = $restaurados; esperados = $txnRegCount; divergencias = $divR; saida = $rest.output })

    # N: e o rollback devolveu de verdade, medido pelo dump e nao pelo contador.
    $dumpFinal = Run 'gcvm_dump_final' @('gcvm','dump')
    $mudadosFinal = Compare-Dump $mapaInicial (Get-DumpMap $dumpFinal)
    Add-Gate 'N_dump_volta_ao_inicial' ($mudadosFinal.Count -eq 0) `
        'Depois do rollback o dump de traducao e identico ao do inicio do ensaio.' `
        ([pscustomobject]@{ campos_mudados = $mudadosFinal })

    # O: e a memoria depois do rollback. Passo 6 do plano em miniatura.
    $w3 = Invoke-Write 'escrita_apos_rollback' ($destination + 32) @([uint32]0xBC25EE01L,[uint32]0xBC25EE02L,[uint32]0xBC25EE03L,[uint32]0xBC25EE04L)
    Add-Gate 'O_memoria_alcancavel_apos_rollback' ($w3.exit_code -eq 0 -and $w3.completed -and $w3.data_ok) `
        'Depois de desfazer tudo, a mesma submissao que funcionava volta a funcionar.' $w3
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
        # Ordem do desmonte: registradores de traducao primeiro. Deixar valor
        # novo em CONTEXT0_PT_BASE apontando para uma tabela que o rollback da
        # VRAM vai apagar seria a pior combinacao possivel.
        if ($snapshotTaken) {
            $rbTxn = Run 'gcvm_txn_restore_final' @('gcvm','txn','restore') -AllowFailure
            if ($rbTxn.exit_code -ne 0 -and -not $failure) { $failure = 'Rollback dos registradores de traducao falhou.' }
        }
        if ($ptBuilt) {
            $rbPt = Run 'gcvm_ptrollback' @('gcvm','ptrollback') -AllowFailure
            if ($rbPt.exit_code -ne 0 -and -not $failure) { $failure = 'Rollback da page table falhou.' }
        }
        if ($sdmaCreated) {
            $rb = Run 'sdma_rollback' @('sdma','rollback') -AllowFailure
            if ($rb.exit_code -ne 0 -and -not $failure) { $failure = 'Rollback SDMA falhou.' }
        }
        for ($i = 0; $i -lt $saved.Count; $i++) {
            Run "dest_restore_$i" @('vram','w',('0x{0:X8}' -f ($destination + 4*$i)),('0x{0:X8}' -f $saved[$i])) -AllowFailure | Out-Null
        }
        if ($pspCreated) { Run 'psp_rollback' @('psp','rollback') -AllowFailure | Out-Null }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}

try {
    if ($snapshotTaken) {
        $ver = Run 'gcvm_txn_verify_final' @('gcvm','txn','verify') -AllowFailure
        $divV = Get-Divergencias $ver
        Add-Gate 'P_verify_final_sem_divergencia' ($ver.exit_code -eq 0 -and $divV -eq 0) `
            "No fim do ensaio os $txnRegCount registradores continuam iguais ao snapshot." `
            ([pscustomobject]@{ divergencias = $divV; saida = $ver.output })
    }
    if ($armed) {
        for ($i = 0; $i -lt $saved.Count; $i++) { $restored += Read-Vram "dest_post_$i" ($destination + 4*$i) }
        $destOk = ($saved.Count -eq $restored.Count -and -not (Compare-Object $saved $restored -SyncWindow 0))
        $sdmaPost = Run 'sdma_status_post' @('sdma','status') -AllowFailure
        $tp = $sdmaPost.output -join ' '
        $wptrPost = if ($tp -match 'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
        Add-Gate 'Q_estado_final_limpo' ($destOk -and $wptrPost -eq 0) `
            'Destino restaurado e WPTR de volta a zero.' `
            ([pscustomobject]@{ destino_restaurado = $destOk; wptr = $wptrPost })
    }
} catch { if (-not $failure) { $failure = $_.Exception.Message } }

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'R_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Step 3 of the memory gate: program new values into the safe translation registers, each write verified by readback'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    not_covered = @(
        'Stages tlb, cache and context0 were deliberately NOT exercised: they replace the POST bypass',
        'Translation itself is still disabled: CONTEXT0.ENABLE_CONTEXT remains 0 throughout',
        'Whether the GPU resolves an address through this page table remains step 5',
        'Upstream builds L2_CNTL3/4/5 from the reset default constant; this driver starts from the POST value'
    )
    reboot_required = $firmwareAccepted
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        ring_dwords = $RingDwords
        registers_in_transaction = $txnRegCount
        table_offset = ('0x{0:X8}' -f $TableOffset)
        target_offset = ('0x{0:X8}' -f $TargetOffset)
        dummy_offset = ('0x{0:X8}' -f $DummyOffset)
        page_count = $PageCount
        pt_va_range = ('0x{0:X} .. 0x{1:X}' -f $ptStartVa, $ptEndVa)
    }
    stages_exercised = $estagios
    allowed_dump_fields = $camposPermitidos
    gates = $gates
    failed_gates = $reprovados
    destination_before = @($saved | ForEach-Object { '0x{0:X8}' -f $_ })
    destination_after_restore = @($restored | ForEach-Object { '0x{0:X8}' -f $_ })
    bugchecks = $bugchecks
    commands = $events
}
$record | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host "Evidencia: $dir"
if ($gates.Count -eq 0) {
    Write-Host '  nenhum gate foi exercitado: o ensaio parou antes de comecar.'
}
foreach ($g in $gates) {
    Write-Host ("  [{0}] {1}" -f $(if ($g.passed) { 'ok  ' } else { 'FALHA' }), $g.gate)
}
if ($failure) { Write-Error $failure; exit 1 }
Write-Host 'Passo 3 do gate de memoria, estagios seguros: valores novos escritos, conferidos e desfeitos.'
Write-Host 'Traducao continua DESLIGADA: ENABLE_CONTEXT nunca saiu de zero neste ensaio.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'REBOOT NECESSARIO: firmware SDMA fica ativo nesta sessao.'
exit 0
