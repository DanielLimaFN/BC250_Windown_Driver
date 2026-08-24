<#
    Frente do CP, passo 1: o PSP aceita os firmwares do CP?

    POR QUE ESTA E A PRIMEIRA PERGUNTA

    Todo o resto da frente do CP — anel GFX, PM4, MQD/HQD, compute — se constroi
    sobre firmware carregado. O catalogo de evidencias registra, como classe P2,
    que "a maioria dos tipos CP/SDMA retornou ITEM_NOT_FOUND" em medicoes de
    terceiros, enquanto a medicao propria deste projeto mostrou o SDMA sendo
    ACEITO com RespStatus=0. As duas coisas nao podem ser verdade ao mesmo tempo
    para o SDMA, e ninguem mediu os tipos do CP nesta placa.

    Se os firmwares do CP forem recusados, o plano da frente muda de forma. Vale
    resolver isso antes de escrever anel, pacote ou gate.

    O QUE ESTE ENSAIO NAO FAZ

    Nao cria anel GFX. Nao emite PM4. Nao tira o CP de reset. Nao escreve em
    NENHUM registrador do CP — a unica escrita e o proprio comando ao PSP.

    O QUE CONTA COMO PROVA, E O QUE NAO CONTA

    RespStatus = 0 prova que o PSP ACEITOU o payload. Nao prova que o CP executa,
    e por regra deste projeto resposta de mailbox nunca e prova de funcionamento.

    A corroboracao fisica disponivel de graca sao os ponteiros de instrucao das
    microengines: CP_PFP_INSTR_PNTR, CP_ME_INSTR_PNTR, CP_CE_INSTR_PNTR e
    CP_MEC1_INSTR_PNTR. Sao o equivalente do PC do F32 da SDMA, que foi o que
    provou o microcontrolador acordando. Este ensaio amostra os quatro antes e
    depois, varias vezes, e relata movimento — ou a ausencia dele — sem
    interpretar movimento como sucesso.

    Este ensaio carrega firmware pelo PSP: exige boot limpo e queima o boot.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 25,
    [ValidateRange(2,64)][int]$Samples = 8
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

# Registradores do CP, derivados de gc_10_1_0_offset.h pela regra
#   byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4
# com segment_base_dw = 0x1260 para BASE_IDX 0 e 0xA000 para BASE_IDX 1.
# Todos SOMENTE LEITURA neste ensaio.
#
# CORRECAO (2026-08-23, depois da execucao das 14:21): RLC_CNTL e RLC_SAFE_MODE
# sao BASE_IDX **1**, e a primeira versao deste ensaio calculou os dois com a
# base do segmento 0. Os valores relatados como RLC naquela execucao nao eram
# desses registradores — eram de outro endereco qualquer. O gate A "passou"
# lendo a coisa errada.
#
# Licao: o BASE_IDX faz parte do endereco. Anotar mmREG sem BASE_IDX e anotar
# metade do endereco.
$regs = [ordered]@{
    # BASE_IDX 0
    'CP_STAT'            = 0x08680   # dw 0x0f40
    'CP_ME_CNTL'         = 0x086D8   # dw 0x0f56
    'CP_PFP_INSTR_PNTR'  = 0x08694   # dw 0x0f45
    'CP_ME_INSTR_PNTR'   = 0x08698   # dw 0x0f46
    'CP_CE_INSTR_PNTR'   = 0x0869C   # dw 0x0f47
    'CP_MEC1_INSTR_PNTR' = 0x086A0   # dw 0x0f48
    'CP_CPF_STATUS'      = 0x0821C   # dw 0x0e27
    'CP_CPC_STATUS'      = 0x08210   # dw 0x0e24
    'CP_CPF_BUSY_STAT'   = 0x08220   # dw 0x0e28
    'GRBM_STATUS'        = 0x08010   # dw 0x0da4, ja verificado no catalogo
    'GRBM_STATUS2'       = 0x08008   # dw 0x0da2
    # BASE_IDX 1
    'RLC_CNTL'           = 0x3B000   # dw 0x4c00, seg 1
    'RLC_SAFE_MODE'      = 0x3B014   # dw 0x4c05, seg 1
    # Caminho de direct load do CP, todo no segmento 1. Lidos aqui apenas para
    # registrar o estado em que o POST os deixou; nenhum e escrito.
    'CP_PFP_IC_BASE_LO'   = 0x3E100  # dw 0x5840, seg 1
    'CP_PFP_IC_BASE_HI'   = 0x3E104  # dw 0x5841, seg 1
    'CP_PFP_IC_BASE_CNTL' = 0x3E108  # dw 0x5842, seg 1
    'CP_PFP_IC_OP_CNTL'   = 0x3E10C  # dw 0x5843, seg 1
    'CP_ME_IC_BASE_LO'    = 0x3E110  # dw 0x5844, seg 1
    'CP_ME_IC_BASE_CNTL'  = 0x3E118  # dw 0x5846, seg 1
    'CP_CE_IC_BASE_LO'    = 0x3E120  # dw 0x5848, seg 1
    'CP_CE_IC_BASE_CNTL'  = 0x3E128  # dw 0x584a, seg 1
}
# Os ponteiros de instrucao sao o que pode se mover. Amostrados a parte.
$pntrs = @('CP_PFP_INSTR_PNTR','CP_ME_INSTR_PNTR','CP_CE_INSTR_PNTR','CP_MEC1_INSTR_PNTR')

# Ordem do amdgpu: RLC primeiro (e o microcontrolador que gerencia energia e
# clock do GFX), depois as microengines do CP gráfico, depois o compute.
$firmwares = @(
    [pscustomobject]@{ nome = 'rlc';  arquivo = 'firmware\cyan_skillfish2_rlc.bin' }
    [pscustomobject]@{ nome = 'pfp';  arquivo = 'firmware\cyan_skillfish2_pfp.bin' }
    [pscustomobject]@{ nome = 'me';   arquivo = 'firmware\cyan_skillfish2_me.bin' }
    [pscustomobject]@{ nome = 'ce';   arquivo = 'firmware\cyan_skillfish2_ce.bin' }
    [pscustomobject]@{ nome = 'mec';  arquivo = 'firmware\cyan_skillfish2_mec.bin' }
)

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute; ele carrega firmware pelo PSP (ativo ate o reboot).'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$abiHeader,$workspaceDriver,$installedDriver)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
foreach ($fw in $firmwares) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $fw.arquivo))) {
        throw "Firmware ausente: $($fw.arquivo)"
    }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-cp-firmware"
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
function Read-Todos([string]$Rotulo) {
    $m = [ordered]@{}
    foreach ($k in $regs.Keys) {
        $m[$k] = Read-Reg "${Rotulo}_$k" ([uint32]$regs[$k])
    }
    return $m
}
# Amostra os ponteiros de instrucao varias vezes. Valor unico nao distingue
# "parado" de "em movimento" — foi a licao do UTCL1_PAGE.
function Read-Pntrs([string]$Rotulo) {
    $amostras = @()
    for ($s = 0; $s -lt $Samples; $s++) {
        $linha = [ordered]@{}
        foreach ($k in $pntrs) { $linha[$k] = Read-Reg "${Rotulo}_${k}_$s" ([uint32]$regs[$k]) }
        $amostras += [pscustomobject]$linha
    }
    $distintos = [ordered]@{}
    foreach ($k in $pntrs) {
        $vals = @($amostras | ForEach-Object { $_.$k } | Where-Object { $null -ne $_ })
        $distintos[$k] = ($vals | Sort-Object -Unique).Count
    }
    return [pscustomobject]@{
        amostras = @($amostras | ForEach-Object {
            $o = [ordered]@{}
            foreach ($k in $pntrs) { $o[$k] = Format-Hex $_.$k }
            [pscustomobject]$o
        })
        valores_distintos = [pscustomobject]$distintos
        algum_em_movimento = [bool](@($distintos.Keys | Where-Object { $distintos[$_] -gt 1 }).Count)
    }
}
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}
function Format-Hex($V) {
    if ($null -eq $V) { return 'ILEGIVEL' }
    return ('0x{0:X8}' -f $V)
}
function Format-Mapa($M) {
    $o = [ordered]@{}
    foreach ($k in $M.Keys) { $o[$k] = Format-Hex $M[$k] }
    return [pscustomobject]$o
}

$failure = $null
$armed = $false; $pspCreated = $false; $algumAceito = $false
$resultados = @()

try {
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+\s+armamento=readonly') {
        throw 'Preflight recusado: info nao reportou ABI, ou armamento nao esta em readonly.'
    }
    # Firmware SDMA ativo indica sessao suja. Nao e requisito deste ensaio, mas
    # medir estado do CP numa sessao onde outro firmware ja rodou confunde a
    # linha de base.
    $ucode = Run 'sdma_ucode_pre' @('reg','r','0x04A24')
    if (($ucode.output -join ' ') -notmatch '=\s*0x00000000') {
        throw 'Preflight recusado: firmware SDMA ja ativo nesta sessao. Reinicie.'
    }

    # A: linha de base. Tudo lido antes de qualquer escrita.
    $antes = Read-Todos 'antes'
    $pntrAntes = Read-Pntrs 'pntr_antes'
    $ilegiveis = @($antes.Keys | Where-Object { $null -eq $antes[$_] })
    Add-Gate 'A_estado_inicial_legivel' ($ilegiveis.Count -eq 0) `
        'Todos os registradores do CP responderam a leitura antes de qualquer escrita.' `
        ([pscustomobject]@{ valores = Format-Mapa $antes; ilegiveis = $ilegiveis })

    # B: o CP esta parado antes? Se algum ponteiro de instrucao ja se mover aqui,
    # a placa tem CP rodando por conta do POST, e isso muda a leitura de tudo
    # que vier depois.
    Add-Gate 'B_cp_parado_na_linha_de_base' (-not $pntrAntes.algum_em_movimento) `
        'Nenhum ponteiro de instrucao do CP se move antes de carregar firmware.' $pntrAntes

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true
    Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true

    # C..G: um firmware por vez, na ordem do amdgpu. Recusa e RESULTADO, nao
    # falha do ensaio: o objetivo e descobrir o que esta placa aceita.
    foreach ($fw in $firmwares) {
        $r = Run "loadfw_$($fw.nome)" @('psp','loadfw',$fw.nome,$fw.arquivo) -AllowFailure
        $texto = $r.output -join "`n"
        $resp = if ($texto -match 'RespStatus\s+:\s+0x([0-9A-Fa-f]{8})') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
        $fence = ($texto -match 'fence\s+:\s+alcancado')
        $versao = if ($texto -match 'versao ucode :\s+0x([0-9A-Fa-f]+)') { '0x' + $Matches[1] } else { $null }
        $dwords = if ($texto -match 'payload\s+:\s+(\d+) dwords') { [int]$Matches[1] } else { $null }
        $aceito = ($fence -and $resp -eq 0)
        if ($aceito) { $algumAceito = $true }

        $resultados += [pscustomobject]@{
            nome = $fw.nome
            arquivo = $fw.arquivo
            versao_ucode = $versao
            payload_dwords = $dwords
            fence_alcancada = $fence
            resp_status = if ($null -ne $resp) { '0x{0:X8}' -f $resp } else { $null }
            aceito = $aceito
            exit_code = $r.exit_code
            saida = $r.output
        }
    }

    # O gate NAO exige aceitacao: exige que cada tentativa tenha produzido uma
    # resposta legivel do PSP. Um ensaio que reprova por o hardware ter dito
    # "nao" nao esta medindo, esta torcendo.
    $semResposta = @($resultados | Where-Object { -not $_.fence_alcancada } | ForEach-Object { $_.nome })
    Add-Gate 'C_psp_respondeu_a_todas_as_tentativas' ($semResposta.Count -eq 0) `
        'O PSP consumiu o frame e devolveu RespStatus para cada firmware tentado.' `
        ([pscustomobject]@{ sem_resposta = $semResposta; resultados = $resultados })

    # D: estado depois. O que mudou, se mudou algo.
    $depois = Read-Todos 'depois'
    $pntrDepois = Read-Pntrs 'pntr_depois'
    $mudados = @()
    foreach ($k in $antes.Keys) {
        if ($antes[$k] -ne $depois[$k]) {
            $mudados += ('{0} : {1} -> {2}' -f $k,
                (Format-Hex $antes[$k]), (Format-Hex $depois[$k]))
        }
    }
    # Este gate registra, nao julga. Mudanca de registrador depois de carregar
    # firmware e informacao; ausencia de mudanca tambem.
    Add-Gate 'D_estado_registrado_apos_carga' $true `
        'Estado do CP relido depois das cargas. Mudanca e informacao, nao veredito.' `
        ([pscustomobject]@{
            valores = Format-Mapa $depois
            campos_mudados = $mudados
            ponteiros_antes = $pntrAntes
            ponteiros_depois = $pntrDepois
        })

    # E: o CP nao pode ter comecado a executar sozinho. Nada aqui tirou o CP de
    # reset nem criou anel; se um ponteiro de instrucao passar a se mover, isso
    # e um achado que precisa de explicacao antes de qualquer proximo passo.
    Add-Gate 'E_cp_nao_executa_sem_ser_liberado' (-not $pntrDepois.algum_em_movimento) `
        'Carregar firmware nao poe o CP a executar: nada aqui o tirou de reset.' $pntrDepois
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
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
Add-Gate 'F_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$aceitos = @($resultados | Where-Object { $_.aceito } | ForEach-Object { $_.nome })
$recusados = @($resultados | Where-Object { -not $_.aceito } | ForEach-Object { $_.nome })

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'CP front, step 1: measure which CP firmwares the PSP on this board accepts, and whether loading them changes any CP register'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    firmware_accepted = $aceitos
    firmware_refused = $recusados
    not_covered = @(
        'PSP acceptance is NOT CP execution: a mailbox response is never proof of function in this project',
        'No CP register was written; the CP was not taken out of reset',
        'No GFX ring was created and no PM4 packet was submitted',
        'Instruction pointers were sampled, not driven: no movement was expected or required here'
    )
    reboot_required = $armed
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        samples_per_pointer = $Samples
        registers_read = @($regs.Keys | ForEach-Object { '{0} = 0x{1:X5}' -f $_, $regs[$_] })
        register_derivation = 'byte = dw * 4 + 0x4980, from gc_10_1_0_offset.h'
    }
    firmwares = $resultados
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
if ($gates.Count -eq 0) {
    Write-Host '  nenhum gate foi exercitado: o ensaio parou antes de comecar.'
}
foreach ($g in $gates) {
    Write-Host ("  [{0}] {1}" -f $(if ($g.passed) { 'ok  ' } else { 'FALHA' }), $g.gate)
}
Write-Host ''
Write-Host 'Firmwares do CP, resposta do PSP nesta placa:'
foreach ($r in $resultados) {
    Write-Host ("  {0,-5} versao {1,-10} {2,6} dwords  RespStatus {3}  {4}" -f `
        $r.nome, $r.versao_ucode, $r.payload_dwords, $r.resp_status,
        $(if ($r.aceito) { 'ACEITO' } else { 'recusado' }))
}
if ($failure) { Write-Error $failure; exit 1 }
Write-Host ''
Write-Host ('Aceitos : {0}' -f $(if ($aceitos.Count) { $aceitos -join ', ' } else { 'nenhum' }))
Write-Host ('Recusados: {0}' -f $(if ($recusados.Count) { $recusados -join ', ' } else { 'nenhum' }))
Write-Host 'ACEITACAO DO PSP NAO E EXECUCAO DO CP. Nenhum registrador do CP foi escrito,'
Write-Host 'o CP nao saiu de reset e nenhum pacote PM4 foi emitido.'
Write-Host "Veredito global permanece: $verdict"
if ($armed) { Write-Host 'REBOOT NECESSARIO: firmware carregado fica ativo nesta sessao.' }
exit 0
