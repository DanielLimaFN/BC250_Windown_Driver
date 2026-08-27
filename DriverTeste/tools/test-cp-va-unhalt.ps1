<#
    ENSAIO: IC_BASE POR ENDERECO TRADUZIDO (VMID 0) + UNHALT DO CP

    A PERGUNTA

    Tres boots limpos mostraram os tres ponteiros de instrucao parando no MESMO
    0x0000041E, com e SEM carga de ucode. A busca de instrucao nunca acontece.

    O amdgpu, em gfx_v10_0_cp_gfx_load_pfp_microcode(), aloca o ucode em
    AMDGPU_GEM_DOMAIN_GTT e escreve no IC_BASE um endereco de GPU TRADUZIDO, com
    IC_BASE_CNTL.VMID = 0. Este laboratorio escrevia um fisico cru.

    O gcvm dump de 2026-08-24 REFUTOU a explicacao mais simples: 0xF4_0E00_0000
    esta dentro de SYS_APERTURE e de FB_LOCATION, e MX_L1_TLB_CNTL esta em bypass
    fisico. O endereco era alcancavel. A diferenca real com o Linux e outra:
    aqui CONTEXT0_CNTL.ENABLE_CONTEXT = 0 e CTX0_PT_BASE = 0 — o VMID 0 nao tem
    page table nenhuma.

    Sobra uma pergunta que somente leitura nao resolve: a busca de instrucao da
    microengine honra o bypass do MX_L1_TLB_CNTL, como a SDMA, ou vai pelo
    UTCL1/UTCL2 do GC e exige contexto de traducao ativo?

    ESTE ENSAIO RESPONDE ISSO

    Liga a traducao pelo caminho JA PROVADO NO SILICIO (gate de memoria, 20/20,
    VA 0x800000001000 -> VRAM 0x0FD01000), mapeia a VRAM onde o ucode do CP mora,
    aponta o IC_BASE de cada engine para a VA correspondente (ABI 21, 'cp icbase
    -va') e faz o unhalt.

    O QUE ESTE ENSAIO NAO ACEITA COMO PROVA

    - PC diferente de zero. 0x0000041E e nao-zero e foi medido tres vezes sem
      execucao nenhuma. Prova de execucao e VARIACAO ENTRE AMOSTRAS.
    - Readback de IC_BASE_*, UCODE_ADDR ou IC_OP_CNTL. Sao write-only: leem
      0xFFFFFFFF tambem no Linux, onde o CP executa.
    - Codigo de retorno da CLI. 'sem recusa' nao e 'aceito pelo hardware'.

    O DISCRIMINADOR NOVO

    Com traducao ligada, uma busca de instrucao que TENTE traduzir e falhe deixa
    rastro: GCVM_L2_PROTECTION_FAULT_STATUS / _ADDR. Este ensaio le os dois antes
    e depois do unhalt. Falha de pagina apos o unhalt seria a primeira evidencia
    positiva de que a microengine chegou a emitir a busca — resultado valioso
    mesmo que o PC nao ande.

    RISCO

    'gcvm program context0' e PONTO SEM VOLTA declarado. O rollback e conhecido
    (transacao de 35 registradores + ptrollback) e roda no finally.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,120)][int]$CommandTimeoutSeconds = 60,
    [ValidateRange(2,64)][int]$Samples = 8,
    [uint32]$PfpOffset = 0x0E000000,
    [uint32]$MeOffset  = 0x0E080000,
    [uint32]$CeOffset  = 0x0E100000,
    [switch]$ForcarSessaoSuja,
    [switch]$IAcceptTheRisk,
    # Page table: mesma maquinaria do gate de memoria (20/20).
    [uint32]$TableOffset = 0x0FF10000,
    [uint32]$DummyOffset = 0x0FF20000,
    # 336 paginas de 4 KB = 0x150000 a partir de PfpOffset. O ucode da ultima
    # engine termina em 0x0E140500 (263424 bytes por engine), logo 322 paginas
    # seriam o minimo; 336 e a folga arredondada. Maximo do driver: 512.
    [ValidateRange(1,512)][int]$PageCount = 336,
    # VA base em 8 TB: acima de 4 GB e muito abaixo da abertura de framebuffer.
    # Se o valor baixo do VA fosse tratado como offset de VRAM por engano, o
    # endereco resultante nao existiria — e o erro apareceria, em vez de coincidir.
    [uint64]$VaBase = 0x0000080000000000,
    # Controle negativo: bring-up do RLC e unhalt SEM carregar ucode, sem icbase
    # e sem jump table. Responde se 0x0000041E e ponto de parada comum das
    # engines (bootloader interno) ou consequencia do que carregamos.
    [switch]$ControleSemUcode
)

$ErrorActionPreference = 'Stop'
if ($Execute -and -not $IAcceptTheRisk) {
    throw ('Este ensaio liga a traducao do VMID 0 (gcvm program context0, PONTO SEM ' +
           'VOLTA declarado). Passe -IAcceptTheRisk junto com -Execute.')
}
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
$modoEnsaio = $(if ($ControleSemUcode) { 'CONTROLE-SEM-UCODE COM TRADUCAO (16 gates)' } else { 'IC_BASE POR VA TRADUZIDA (20 gates)' })
$vaEnd = [uint64]($VaBase + [uint64]($PageCount * 4096) - 1)
$gcvmSnapshotFeito = $false
$ptConstruida = $false
$traducaoLigada = $false
# Registradores de falha do GCVM (segmento 0). Sao a evidencia nova deste ensaio.
$A_FAULT_STATUS   = 0x0A130
$A_FAULT_ADDR_LO  = 0x0A134
$A_FAULT_ADDR_HI  = 0x0A138
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

function Read-Falha([string]$Rotulo) {
    # GCVM_L2_PROTECTION_FAULT_STATUS/_ADDR reportam falha de traducao. Sao a
    # evidencia NOVA deste ensaio: se a microengine emitir a busca e a traducao
    # falhar, o rastro aparece aqui. Nao configuram nada, so relatam.
    $st = Read-Reg "${Rotulo}_FAULT_STATUS" $A_FAULT_STATUS
    $lo = Read-Reg "${Rotulo}_FAULT_ADDR_LO" $A_FAULT_ADDR_LO
    $hi = Read-Reg "${Rotulo}_FAULT_ADDR_HI" $A_FAULT_ADDR_HI
    return [pscustomobject]@{
        status = Format-Hex $st
        status_bruto = $st
        addr = $(if ($null -ne $hi -and $null -ne $lo) { '0x{0:X8}{1:X8}' -f $hi, $lo } else { $null })
        addr_hi = Format-Hex $hi
        addr_lo = Format-Hex $lo
    }
}

function Invoke-Stage([string]$Nome, [int]$EscritasEsperadas, [switch]$Danger) {
    $cmdArgs = @('gcvm','program',$Nome,
                 '-table',('0x{0:X8}' -f $TableOffset),
                 '-dummy',('0x{0:X8}' -f $DummyOffset),
                 '-start',('0x{0:X}' -f $VaBase),
                 '-end',('0x{0:X}' -f $vaEnd))
    if ($Danger) { $cmdArgs += '-danger' }
    $r = Run "program_$Nome" $cmdArgs -AllowFailure
    $texto = $r.output -join "`n"
    $escritas = $null; $div = $null
    if ($texto -match 'escritas\s+:\s+(\d+),\s+(\d+) divergencias') {
        $escritas = [int]$Matches[1]; $div = [int]$Matches[2]
    }
    return [pscustomobject]@{
        nome = $Nome
        exit_code = $r.exit_code
        escritas = $escritas
        escritas_esperadas = $EscritasEsperadas
        divergencias = $div
        ok = ($r.exit_code -eq 0 -and $escritas -eq $EscritasEsperadas -and
              $null -ne $div -and $div -eq 0)
        saida = $r.output
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
    # Gates C2..C5: LIGAR A TRADUCAO DO VMID 0
    #
    # Toda esta fase usa a maquinaria que fechou o gate de memoria (20/20). O que
    # muda e o ALVO: em vez de 0x0FD00000, a page table mapeia a VRAM onde o
    # ucode do CP mora, para que o IC_BASE possa apontar para uma VA.
    # -------------------------------------------------------------------------
    Run 'gcvm_semrelease' @('gcvm','semrelease') -AllowFailure | Out-Null
    $dumpAntes = Run 'gcvm_dump_antes' @('gcvm','dump') -AllowFailure

    $pt = Run 'gcvm_ptbuild' @('gcvm','ptbuild',
                               ('0x{0:X8}' -f $TableOffset),
                               ('0x{0:X8}' -f $PfpOffset),
                               "$PageCount") -AllowFailure
    $ptTxt = $pt.output -join "`n"
    $ptDiv = $null; $ptEnt = $null
    if ($ptTxt -match 'conferencia\s+:\s+(\d+) divergencias em (\d+) entradas') {
        $ptDiv = [int]$Matches[1]; $ptEnt = [int]$Matches[2]
    }
    $ptConstruida = ($pt.exit_code -eq 0 -and $ptDiv -eq 0 -and $ptEnt -eq $PageCount)
    Add-Gate 'C2_page_table_do_ucode_construida' `
        ([bool]$ptConstruida) `
        ("As $PageCount PTEs cobrem a VRAM do ucode e foram conferidas uma por uma.") `
        ([pscustomobject]@{
            tabela = ('0x{0:X8}' -f $TableOffset)
            alvo = ('0x{0:X8}' -f $PfpOffset)
            paginas = $PageCount
            va_base = ('0x{0:X}' -f $VaBase)
            va_fim = ('0x{0:X}' -f $vaEnd)
            divergencias = $ptDiv
            entradas = $ptEnt
            saida = $pt.output
        })
    if (-not $ptConstruida) {
        throw 'Sem page table conferida, ligar traducao seria escrita por suposicao.'
    }

    $gsnap = Run 'gcvm_txn_snapshot' @('gcvm','txn','snapshot') -AllowFailure
    $gsnapTxt = $gsnap.output -join "`n"
    $gsnapRegs = if ($gsnapTxt -match 'registradores\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    $gcvmSnapshotFeito = ($gsnap.exit_code -eq 0 -and $null -ne $gsnapRegs -and $gsnapRegs -gt 0)
    Add-Gate 'C3_snapshot_do_gcvm' `
        ([bool]$gcvmSnapshotFeito) `
        'Transacao do GCVM aberta: existe caminho de volta provado antes de escrever.' `
        ([pscustomobject]@{ exit_code = $gsnap.exit_code; registradores = $gsnapRegs; saida = $gsnap.output })
    if (-not $gcvmSnapshotFeito) {
        throw 'Sem snapshot do GCVM nao ha rollback. Nao ligo traducao assim.'
    }

    # Ordem e contagem de escritas identicas ao ensaio que fechou o gate de
    # memoria: 'nao PERIGOSO' primeiro, 'PERIGOSO' depois, context0 por ultimo.
    # -danger somente onde o driver exige, e nao por atacado.
    $estagios = @()
    $planoSeguro = @(
        [pscustomobject]@{ nome = 'gart';     escritas = 6 },
        [pscustomobject]@{ nome = 'system';   escritas = 10 },
        [pscustomobject]@{ nome = 'identity'; escritas = 6 },
        [pscustomobject]@{ nome = 'invrange'; escritas = 4 }
    )
    $planoPerigoso = @(
        [pscustomobject]@{ nome = 'tlb';   escritas = 1 },
        [pscustomobject]@{ nome = 'cache'; escritas = 5 }
    )
    foreach ($etapa in $planoSeguro) {
        $r = Invoke-Stage $etapa.nome $etapa.escritas
        $estagios += $r
        if (-not $r.ok) {
            Add-Gate 'C4_estagios_de_traducao' $false `
                ("Todos os estagios do GCVM aplicados com a contagem de escritas " +
                 "medida no gate de memoria (parou em $($etapa.nome)).") $estagios
            throw ("Estagio $($etapa.nome) nao aplicou. Rollback imediato, sem ligar context0.")
        }
    }
    foreach ($etapa in $planoPerigoso) {
        $r = Invoke-Stage $etapa.nome $etapa.escritas -Danger
        $estagios += $r
        if (-not $r.ok) {
            Add-Gate 'C4_estagios_de_traducao' $false `
                ("Todos os estagios do GCVM aplicados com a contagem de escritas " +
                 "medida no gate de memoria (parou em $($etapa.nome)).") $estagios
            throw ("Estagio $($etapa.nome) nao aplicou. Rollback imediato, sem ligar context0.")
        }
    }
    $ctx = Invoke-Stage 'context0' 1 -Danger
    $estagios += $ctx
    if ($ctx.ok) { $traducaoLigada = $true }
    Add-Gate 'C4_estagios_de_traducao' `
        ([bool]$traducaoLigada) `
        ('Os sete estagios do GCVM aplicados com a MESMA contagem de escritas ' +
         'medida no gate de memoria (6/10/6/4/1/5/1), zero divergencias, ' +
         'inclusive context0.') `
        $estagios
    if (-not $traducaoLigada) {
        throw 'context0 nao aplicou: sem traducao ligada, apontar o IC_BASE para VA seria sem sentido.'
    }

    # Prova de que a traducao esta LIGADA, lida do silicio e nao do codigo de
    # retorno: ENABLE_CONTEXT tem de ter virado 1 e o PT_BASE tem de ser nao-zero.
    $ctx0Cntl = Read-Reg 'CONTEXT0_CNTL_depois' 0x0A200
    $ptBaseLo = Read-Reg 'CTX0_PT_BASE_LO_depois' 0x0A3AC
    $ctxLigado = ($null -ne $ctx0Cntl -and ($ctx0Cntl -band 0x1) -eq 1)
    Add-Gate 'C5_contexto0_ligado_no_silicio' `
        ([bool]($ctxLigado -and $null -ne $ptBaseLo -and $ptBaseLo -ne 0)) `
        'CONTEXT0_CNTL.ENABLE_CONTEXT = 1 e PAGE_TABLE_BASE nao-zero, lidos do hardware.' `
        ([pscustomobject]@{
            CONTEXT0_CNTL = Format-Hex $ctx0Cntl
            enable_context = $ctxLigado
            CTX0_PT_BASE_LO = Format-Hex $ptBaseLo
            antes_deste_ensaio = '0x007FFE80 com ENABLE_CONTEXT = 0 e PT_BASE = 0'
        })

    $inv = Run 'gcvm_invalidate' @('gcvm','invalidate','0','0') -AllowFailure
    $invTxt = $inv.output -join "`n"
    $invOk = ($inv.exit_code -eq 0 -and $invTxt -match 'ok, ACK confirmado pelo hardware')
    Add-Gate 'C6_tlb_invalidada_com_ack' `
        ([bool]$invOk) `
        'ACK do hardware confirmou a invalidacao da TLB do VMID 0.' `
        ([pscustomobject]@{ exit_code = $inv.exit_code; ack_confirmado = $invOk; saida = $inv.output })

    # Falha de traducao ANTES do unhalt. Precisa estar limpa para que qualquer
    # falha depois seja atribuivel a busca de instrucao, e nao a lixo anterior.
    $falhaAntes = Read-Falha 'antes_unhalt'

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
            # A VA de cada engine e a base mais o deslocamento DENTRO da regiao
            # mapeada. PFP fica no indice 0; ME e CE nos indices correspondentes
            # ao offset de VRAM deles, porque a page table e linear a partir de
            # PfpOffset.
            $vaEngine = [uint64]($VaBase + [uint64]($e.offset - $PfpOffset))
            $icb = Run "icbase_$($e.nome)" @('cp','icbase',$e.nome,$off,
                                             '-va',('0x{0:X}' -f $vaEngine)) -AllowFailure
            $icbTxt = $icb.output -join "`n"
            $icbResults += [pscustomobject]@{
                engine = $e.nome
                va = ('0x{0:X}' -f $vaEngine)
                vram = $off
                exit_code = $icb.exit_code
                escritas = $(if ($icbTxt -match 'escritas\s+:\s+(\d+)') { [int]$Matches[1] } else { $null })
                confirmado = ($icbTxt -match 'ok, IC_BASE apontado para o ucode')
                saida = $icb.output
            }
        }
        Add-Gate 'F_tres_icbase_apontados_por_va' `
            ([bool](@($icbResults | Where-Object { $_.confirmado -and $_.escritas -eq 3 }).Count -eq $engines.Count)) `
            ('IC_BASE_CNTL, LO e HI escritos para as tres engines com VA traduzida. ' +
             'ATENCAO: sao registradores WRITE-ONLY. Isto atesta que as escritas ' +
             'foram emitidas sem recusa, NAO que o hardware as aceitou.') `
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
    # H2: FALHA DE TRADUCAO DEPOIS DO UNHALT — o discriminador novo
    #
    # Isto NAO e um gate de aprovacao, e sim uma medicao com duas leituras uteis
    # e opostas, e por isso e registrado como observacao e nao como pass/fail:
    #
    #   - falha NOVA apontando para a faixa de VA do ucode: a microengine EMITIU
    #     a busca de instrucao e a traducao a rejeitou. Primeira evidencia
    #     positiva de que a busca acontece. Diz onde consertar.
    #   - nenhuma falha, e o PC parado no mesmo lugar: a busca nao chega nem a
    #     tentar traduzir. O bloqueio esta antes do caminho de memoria.
    # -------------------------------------------------------------------------
    $falhaDepois = Read-Falha 'depois_unhalt'
    $falhaNova = ($null -ne $falhaDepois.status_bruto -and
                  $falhaDepois.status_bruto -ne 0 -and
                  $falhaDepois.status_bruto -ne $falhaAntes.status_bruto)
    Add-Gate 'H2_falha_de_traducao_observada' `
        $true `
        ('OBSERVACAO, nao aprovacao: FAULT_STATUS/_ADDR lidos antes e depois do ' +
         'unhalt. Qualquer um dos dois resultados e informativo, por isso este ' +
         'item nao reprova.') `
        ([pscustomobject]@{
            antes = $falhaAntes
            depois = $falhaDepois
            falha_nova = $falhaNova
            leitura = $(if ($falhaNova) {
                'FALHA NOVA: a microengine emitiu a busca de instrucao e a traducao a rejeitou. Compare FAULT_ADDR com a faixa de VA do ucode.'
            } else {
                'Sem falha nova: nada indica que a busca de instrucao tenha chegado ao caminho de memoria.'
            })
            faixa_va_do_ucode = ('0x{0:X} .. 0x{1:X}' -f $VaBase, $vaEnd)
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
        # Desligar a traducao ANTES de devolver a page table: a ordem inversa
        # deixaria o contexto ligado apontando para uma tabela ja desfeita.
        if ($gcvmSnapshotFeito) {
            Run 'teardown_gcvm_restore' @('gcvm','txn','restore') -AllowFailure | Out-Null
            Run 'teardown_gcvm_invalidate' @('gcvm','invalidate','0','0') -AllowFailure | Out-Null
        }
        if ($ptConstruida) {
            Run 'teardown_ptrollback' @('gcvm','ptrollback') -AllowFailure | Out-Null
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
$totalGatesPrevistos = $(if ($ControleSemUcode) { 16 } else { 20 })
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
if ($ControleSemUcode) {
    # No controle, todos os gates passando significa que o CONTROLE funcionou.
    # O controle foi feito para NAO executar nada, e neste caso ele acabou de
    # demonstrar o oposto de aprovacao. Anunciar "passo 3 concluido" aqui seria a
    # licao 2 outra vez: ensaio imprimindo sucesso sobre coisa que nao mediu.
    #
    # A saida diz O QUE FOI MEDIDO, nao quantos gates passaram.
    $pcControle = ($gates | Where-Object { $_.gate -eq 'I_controle_ponto_de_parada' }).observed
    Write-Host "CONTROLE NEGATIVO CONCLUIDO. Isto NAO e aprovacao do passo 3." -ForegroundColor Cyan
    Write-Host "  Sem ucode, sem icbase, sem jump table, o PC parou em: $($pcControle.PFP_PC)" -ForegroundColor Cyan
    if ($pcControle.PFP_PC -eq '0x0000041E') {
        Write-Host "  IGUAL ao valor medido COM ucode. A carga nao teve efeito sobre as" -ForegroundColor Yellow
        Write-Host "  engines: a busca de instrucao nunca aconteceu. O CP NAO EXECUTA." -ForegroundColor Yellow
    } else {
        Write-Host "  DIFERENTE do valor com ucode: a carga mudou o comportamento." -ForegroundColor Yellow
    }
    Write-Host "  Veredito do passo 3: NAO VALIDADO EM HARDWARE." -ForegroundColor Yellow
} else {
    # O veredito deste ensaio NAO deriva da contagem de gates. Deriva de duas
    # medicoes: o PC variou entre amostras, e houve falha de traducao nova.
    # Todos os gates podem passar com o CP parado — foi o que aconteceu nos tres
    # boots anteriores. Licao 25.
    $obsPc = ($gates | Where-Object { $_.gate -eq 'I_ponteiros_de_instrucao_ativos' }).observed
    $obsFalha = ($gates | Where-Object { $_.gate -eq 'H2_falha_de_traducao_observada' }).observed
    $variou = $false
    if ($null -ne $obsPc -and $null -ne $obsPc.variou_entre_amostras) {
        $variou = [bool]$obsPc.variou_entre_amostras
    }
    if ($variou) {
        Write-Host "EXECUCAO MEDIDA: os ponteiros de instrucao VARIARAM entre amostras." -ForegroundColor Green
        Write-Host "  Isto e prova de execucao, nao apenas de PC nao-zero." -ForegroundColor Green
    } else {
        Write-Host "O CP CONTINUA SEM EXECUTAR: o PC nao variou entre amostras." -ForegroundColor Yellow
        Write-Host "  Apontar o IC_BASE para VA traduzida NAO destravou a busca de instrucao." -ForegroundColor Yellow
    }
    if ($null -ne $obsFalha) {
        if ($obsFalha.falha_nova) {
            Write-Host "  ACHADO: falha de traducao NOVA apos o unhalt." -ForegroundColor Cyan
            Write-Host "    FAULT_STATUS $($obsFalha.depois.status)  FAULT_ADDR $($obsFalha.depois.addr)" -ForegroundColor Cyan
            Write-Host "    A microengine EMITIU a busca e a traducao a rejeitou. Compare o" -ForegroundColor Cyan
            Write-Host "    endereco com a faixa $($obsFalha.faixa_va_do_ucode)." -ForegroundColor Cyan
        } else {
            Write-Host "  Sem falha de traducao nova: nada indica que a busca tenha chegado" -ForegroundColor Yellow
            Write-Host "  ao caminho de memoria. O bloqueio esta antes dele." -ForegroundColor Yellow
        }
    }
    if (-not $passouGeral) {
        Write-Host "  Gates reprovados: inspecione o relatorio em $dir" -ForegroundColor Yellow
    }
    if (-not $variou) {
        Write-Host "  Veredito do passo 3: NAO VALIDADO EM HARDWARE." -ForegroundColor Yellow
    }
}
