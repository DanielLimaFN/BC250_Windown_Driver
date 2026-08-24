<#
    Passos 3 (estagios de risco), 4, 5 e 6 do gate de memoria, num unico boot.

    Este e o ensaio mais perigoso do projeto ate aqui. Ele desliga o bypass que
    o POST deixou e liga a traducao de verdade no VMID 0. Se a programacao
    estiver errada, a SDMA para de alcancar memoria — e este projeto ja mediu,
    em seis boots, que engine SDMA quebrada nesta placa nao volta por software.
    O custo do erro e um reboot, no melhor caso.

    POR QUE OS QUATRO PASSOS JUNTOS

    Separar 3 de 5 exigiria dois boots com traducao ligada, e o segundo teria de
    reconstruir tudo. Pior: com traducao ligada e sem submissao traduzida, o
    ensaio gastaria o risco e voltaria sem resposta. A submissao e a unica coisa
    que responde "traduziu?".

    COMO A PROVA DISCRIMINA TRADUCAO DE APERTURE

    A page table mapeia VA 0x0000_0800_0000_0000 em diante para a VRAM a partir
    de 0x0FD00000. A submissao de prova carrega no pacote o endereco
    0x0000_0800_0000_1000 e o dado tem de aparecer na VRAM em 0x0FD01000.

    Esse offset NAO aparece em lugar nenhum do pacote. Nada alem da page table
    liga um endereco ao outro. Uma submissao que resolvesse pela abertura de
    framebuffer escreveria em outro lugar, ou em nenhum.

    O VA fica acima de 4 GB de proposito: abaixo disso todo endereco e memoria
    de sistema real, e um WRITE_LINEAR para la, com traducao desligada,
    escreveria na RAM do host. Em 8 TB o endereco e inofensivo caso o hardware o
    trate como fisico.

    O CONTROLE QUE SEPARA "TRADUZIU" DE "QUEBROU TUDO"

    Antes da submissao traduzida, uma submissao para a abertura de framebuffer
    (0x0FF04000, o destino de sempre). Se ela falhar, a traducao derrubou o
    caminho do anel e a submissao traduzida nem sera tentada.

    Este ensaio carrega firmware SDMA: exige boot limpo e queima o boot.
#>
param(
    [switch]$Execute,
    [switch]$IAcceptTheRisk,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 25,
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
$destination = [uint32]0x0FF04000       # abertura de framebuffer, o destino de sempre
$savedDwords = 20
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

# --- Enderecos da prova de traducao ---------------------------------------
# VA base em 8 TB: acima de 4 GB (nao colide com RAM do host se for tratado
# como fisico) e muito abaixo da abertura de framebuffer (0xF4_0000_0000).
$vaBase = [uint64]0x0000080000000000
$vaEnd = [uint64]($vaBase + [uint64]($PageCount * 4096) - 1)
$vaProof = [uint64]($vaBase + 0x1000)                 # PTE indice 1
$proofVram = [uint32]($TargetOffset + 0x1000)         # onde o dado tem de aparecer
# Endereco que seria escrito por engano se o modo cru fosse ignorado e o valor
# baixo do VA virasse offset de VRAM. Conferido para descartar erro de laboratorio.
$decoyVram = [uint32]0x00001000

$proofData = @([uint32]0xBC25F1A1L, [uint32]0xBC25F1A2L, [uint32]0xBC25F1A3L, [uint32]0xBC25F1A4L)
$proofPoison = @([uint32]0x5A5A0001L, [uint32]0x5A5A0002L, [uint32]0x5A5A0003L, [uint32]0x5A5A0004L)

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute -IAcceptTheRisk.'
}
if (-not $IAcceptTheRisk) {
    throw ('Este ensaio DESLIGA o bypass do POST e liga a traducao no VMID 0. ' +
           'Engine SDMA quebrada nesta placa nao volta por software: o custo do erro e um reboot. ' +
           'Repita com -IAcceptTheRisk se essa e a intencao.')
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$abiHeader,$workspaceDriver,$installedDriver,$firmware)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

$txnRegCount = $null
foreach ($linha in Get-Content -LiteralPath $abiHeader) {
    if ($linha -match 'BC250DBG_GCVM_TXN_REG_COUNT\s+(\d+)u') { $txnRegCount = [int]$Matches[1]; break }
}
if (-not $txnRegCount) { throw 'Nao foi possivel ler BC250DBG_GCVM_TXN_REG_COUNT do header da ABI.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-gcvm-translate"
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
function Write-Vram([string]$Name, [uint32]$Offset, [uint32]$Value) {
    Run $Name @('vram','w',('0x{0:X8}' -f $Offset),('0x{0:X8}' -f $Value)) -AllowFailure | Out-Null
}
function Invoke-Write([string]$Name, [uint32]$Offset, [uint32[]]$Data) {
    $cmd = @('sdma','write',('0x{0:X8}' -f $Offset)) + @($Data | ForEach-Object { '0x{0:X8}' -f $_ })
    $r = Run $Name $cmd -AllowFailure
    $text = $r.output -join "`n"
    return [pscustomobject]@{
        name = $Name
        exit_code = $r.exit_code
        completed = ($text -match 'Conclusao do pacote : SIM')
        data_ok = ($text -match 'Resultado completo\s+: VALIDADO NESTA OPERACAO')
        dados = @($r.output | Where-Object { $_ -match 'Dados conferidos' }) -join ''
        saida = $r.output
    }
}
# A submissao de prova: endereco de 64 bits no pacote, conferencia redirecionada
# para onde a page table manda o dado aparecer.
function Invoke-TranslatedWrite([string]$Name, [uint64]$Va, [uint32]$VerifyAt, [uint32[]]$Data) {
    $cmd = @('sdma','write','-rawaddr',('0x{0:X}' -f $Va),'-verifyat',('0x{0:X8}' -f $VerifyAt),'0x00000000') +
           @($Data | ForEach-Object { '0x{0:X8}' -f $_ })
    $r = Run $Name $cmd -AllowFailure
    $text = $r.output -join "`n"
    return [pscustomobject]@{
        name = $Name
        exit_code = $r.exit_code
        completed = ($text -match 'Conclusao do pacote : SIM')
        data_ok = ($text -match 'Resultado completo\s+: VALIDADO NESTA OPERACAO')
        dados = @($r.output | Where-Object { $_ -match 'Dados conferidos' }) -join ''
        saida = $r.output
    }
}
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
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
function Invoke-Stage([string]$Nome, [int]$EscritasEsperadas, [switch]$Danger) {
    $cmdArgs = @('gcvm','program',$Nome,
                 '-table',('0x{0:X8}' -f $TableOffset),
                 '-dummy',('0x{0:X8}' -f $DummyOffset),
                 '-start',('0x{0:X}' -f $vaBase),
                 '-end',('0x{0:X}' -f $vaEnd))
    if ($Danger) { $cmdArgs += '-danger' }
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

$saved = @(); $restored = @(); $savedProof = @(); $savedDecoy = @(); $failure = $null
$armed = $false; $pspCreated = $false; $sdmaCreated = $false; $firmwareAccepted = $false
$snapshotTaken = $false; $ptBuilt = $false; $translationOn = $false
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
    for ($i = 0; $i -lt 4; $i++) { $savedProof += Read-Vram "proof_pre_$i" ($proofVram + 4*$i) }
    for ($i = 0; $i -lt 4; $i++) { $savedDecoy += Read-Vram "decoy_pre_$i" ($decoyVram + 4*$i) }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true
    Run 'gcvm_semrelease' @('gcvm','semrelease') -AllowFailure | Out-Null
    Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true
    Run 'psp_loadfw' @('psp','loadfw','sdma0',$firmware) | Out-Null; $firmwareAccepted = $true
    Run 'sdma_f32' @('sdma','f32') | Out-Null
    Run 'sdma_init' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") | Out-Null
    $sdmaCreated = $true

    $dumpInicial = Run 'gcvm_dump_inicial' @('gcvm','dump')
    $mapaInicial = Get-DumpMap $dumpInicial

    # A: base com o bypass do POST.
    $w1 = Invoke-Write 'escrita_bypass_antes' $destination @([uint32]0xBC25AA01L,[uint32]0xBC25AA02L,[uint32]0xBC25AA03L,[uint32]0xBC25AA04L)
    Add-Gate 'A_bypass_funciona_antes' ($w1.exit_code -eq 0 -and $w1.completed -and $w1.data_ok) `
        'Submissao com fence fisica e dado conferido antes de mexer na traducao.' $w1
    if (-not ($w1.exit_code -eq 0 -and $w1.completed -and $w1.data_ok)) {
        throw 'A engine nao escreveu antes de comecar: sem base para medir.'
    }

    # B: a page table. VA base em 8 TB, 512 paginas, alvo na VRAM.
    $pt = Run 'gcvm_ptbuild' @('gcvm','ptbuild',('0x{0:X8}' -f $TableOffset),('0x{0:X8}' -f $TargetOffset),"$PageCount") -AllowFailure
    $conf = if (($pt.output -join ' ') -match 'conferencia\s+:\s+(\d+) divergencias em (\d+) entradas') {
        [pscustomobject]@{ divergencias = [int]$Matches[1]; entradas = [int]$Matches[2] }
    } else { $null }
    if ($pt.exit_code -eq 0) { $ptBuilt = $true }
    Add-Gate 'B_page_table_construida' `
        ($pt.exit_code -eq 0 -and $null -ne $conf -and $conf.divergencias -eq 0 -and $conf.entradas -eq $PageCount) `
        'A tabela existe e foi conferida entrada por entrada.' `
        ([pscustomobject]@{ conferencia = $conf; saida = $pt.output })
    if (-not $ptBuilt) { throw 'Sem page table conferida, ligar traducao seria escrita por suposicao.' }

    # C: veneno no destino traduzido. Sem isso, dado encontrado la depois nao
    # prova nada: poderia ser lixo antigo que coincidiu.
    for ($i = 0; $i -lt 4; $i++) { Write-Vram "proof_poison_$i" ($proofVram + 4*$i) $proofPoison[$i] }
    $poisonLido = @()
    for ($i = 0; $i -lt 4; $i++) { $poisonLido += Read-Vram "proof_poison_rd_$i" ($proofVram + 4*$i) }
    $poisonOk = ($null -eq (Compare-Object $proofPoison $poisonLido -SyncWindow 0))
    Add-Gate 'C_destino_traduzido_envenenado' $poisonOk `
        'O destino que a page table aponta contem veneno conhecido antes da prova.' `
        ([pscustomobject]@{
            offset = ('0x{0:X8}' -f $proofVram)
            veneno = @($proofPoison | ForEach-Object { '0x{0:X8}' -f $_ })
            lido = @($poisonLido | ForEach-Object { '0x{0:X8}' -f $_ })
        })
    if (-not $poisonOk) { throw 'Nao foi possivel envenenar o destino traduzido: a prova nao teria valor.' }

    # D: snapshot. Sem ele o driver recusa programar, e com razao.
    $snap = Run 'gcvm_txn_snapshot' @('gcvm','txn','snapshot') -AllowFailure
    if ($snap.exit_code -eq 0) { $snapshotTaken = $true }
    Add-Gate 'D_snapshot_guardado' `
        ($snap.exit_code -eq 0 -and (($snap.output -join ' ') -match ('registradores\s+:\s+' + $txnRegCount))) `
        "Os $txnRegCount registradores de configuracao ficam guardados no driver." `
        ([pscustomobject]@{ exit_code = $snap.exit_code; esperados = $txnRegCount; saida = $snap.output })
    if (-not $snapshotTaken) { throw 'Sem snapshot nao ha caminho de volta: o ensaio para aqui.' }

    # E: estagios inertes, ja aprovados em 2026-08-23 12:59. Aqui sao
    # pre-requisito, e a faixa de VA e outra.
    $estagios += Invoke-Stage 'gart' 6
    $estagios += Invoke-Stage 'system' 10
    $estagios += Invoke-Stage 'identity' 6
    $estagios += Invoke-Stage 'invrange' 4
    $inertesOk = (@($estagios | Where-Object { -not $_.ok }).Count -eq 0)
    Add-Gate 'E_estagios_inertes' $inertesOk `
        'gart, system, identity e invrange: 26 escritas, todas confirmadas por readback.' $estagios
    if (-not $inertesOk) { throw 'Estagio inerte falhou: nao faz sentido seguir para os de risco.' }

    $w2 = Invoke-Write 'escrita_bypass_apos_inertes' ($destination + 16) @([uint32]0xBC25BB01L,[uint32]0xBC25BB02L,[uint32]0xBC25BB03L,[uint32]0xBC25BB04L)
    Add-Gate 'F_bypass_funciona_apos_inertes' ($w2.exit_code -eq 0 -and $w2.completed -and $w2.data_ok) `
        'Os estagios inertes nao perturbaram o caminho de memoria.' $w2
    if (-not ($w2.exit_code -eq 0 -and $w2.completed -and $w2.data_ok)) {
        throw 'Caminho de memoria quebrou nos estagios inertes: nao ligar traducao sobre isso.'
    }

    # ------------------------------------------------------------------
    # A partir daqui o bypass do POST deixa de existir.
    # ------------------------------------------------------------------
    # Cada estagio de risco aborta na hora se falhar. Em 2026-08-23 13:40 os
    # tres foram aplicados e so depois o resultado foi conferido — o 'cache'
    # falhou no meio e o 'context0' ligou traducao sobre configuracao
    # incompleta. Deu para desfazer, mas foi sorte, nao desenho.
    $tlb = Invoke-Stage 'tlb' 1 -Danger
    $estagios += $tlb
    Add-Gate 'G_estagio_tlb' ($tlb.ok) `
        'SYSTEM_ACCESS_MODE sai de 0 e vai a 3: o bypass fisico do POST acaba aqui.' $tlb
    if (-not $tlb.ok) { throw 'Estagio tlb nao aplicou: rollback imediato, sem seguir para cache.' }

    $cache = Invoke-Stage 'cache' 5 -Danger
    $estagios += $cache
    Add-Gate 'H_estagio_cache' ($cache.ok) `
        'L2 habilitada e VMC_TAP_PDE/PTE_REQUEST_PHYSICAL a zero.' $cache
    if (-not $cache.ok) { throw 'Estagio cache nao aplicou: rollback imediato. NAO ligar CONTEXT0 sobre cache incompleta.' }

    $ctx = Invoke-Stage 'context0' 1 -Danger
    $estagios += $ctx
    if ($ctx.ok) { $translationOn = $true }
    Add-Gate 'I_estagio_context0' ($ctx.ok) `
        'ENABLE_CONTEXT a 1: o VMID 0 passa a traduzir.' $ctx
    if (-not $ctx.ok) { throw 'Estagio context0 nao aplicou: rollback imediato, sem tentar submissao.' }

    # J: o estado ligado, medido e nao presumido.
    $dumpLigado = Run 'gcvm_dump_ligado' @('gcvm','dump')
    $mapaLigado = Get-DumpMap $dumpLigado
    $ctx0 = if ($mapaLigado['CONTEXT0_CNTL'] -match '0x([0-9A-Fa-f]{8})') { [Convert]::ToUInt32($Matches[1],16) } else { 0 }
    $tlbReg = if ($mapaLigado['MX_L1_TLB_CNTL'] -match '0x([0-9A-Fa-f]{8})') { [Convert]::ToUInt32($Matches[1],16) } else { 0 }
    $l2 = if ($mapaLigado['L2_CNTL'] -match '0x([0-9A-Fa-f]{8})') { [Convert]::ToUInt32($Matches[1],16) } else { 0 }
    $ctxEnabled = (($ctx0 -band 0x1) -eq 0x1)
    $samMode = (($tlbReg -band 0x18) -shr 3)
    $l2Enabled = (($l2 -band 0x1) -eq 0x1)
    Add-Gate 'J_traducao_esta_ligada' ($ctxEnabled -and $samMode -eq 3 -and $l2Enabled) `
        'ENABLE_CONTEXT=1, SYSTEM_ACCESS_MODE=3 e L2 habilitada, lidos do silicio.' `
        ([pscustomobject]@{
            context0_cntl = ('0x{0:X8}' -f $ctx0); enable_context = $ctxEnabled
            mx_l1_tlb_cntl = ('0x{0:X8}' -f $tlbReg); system_access_mode = $samMode
            l2_cntl = ('0x{0:X8}' -f $l2); enable_l2_cache = $l2Enabled
        })

    # K: passo 4. Engine 17, sem semaforo, com a leitura muda de REQ.
    $inv = Run 'gcvm_invalidate' @('gcvm','invalidate','0','0') -AllowFailure
    $invTexto = $inv.output -join "`n"
    $invOk = ($inv.exit_code -eq 0 -and $invTexto -match 'ACK confirmado pelo hardware')
    Add-Gate 'K_invalidacao_confirmada' $invOk `
        'A TLB foi invalidada pela engine 17 e o hardware confirmou pelo ACK.' `
        ([pscustomobject]@{ exit_code = $inv.exit_code; saida = $inv.output })

    # L: controle. O anel e a abertura de framebuffer sobreviveram a traducao?
    # Se este falhar, a submissao traduzida nem e tentada: sem este controle,
    # falha na prova nao distinguiria "nao traduziu" de "quebrou tudo".
    $w3 = Invoke-Write 'escrita_aperture_com_traducao' ($destination + 32) @([uint32]0xBC25CC01L,[uint32]0xBC25CC02L,[uint32]0xBC25CC03L,[uint32]0xBC25CC04L)
    Add-Gate 'L_aperture_funciona_com_traducao' ($w3.exit_code -eq 0 -and $w3.completed -and $w3.data_ok) `
        'Com traducao ligada, o anel e a abertura de framebuffer continuam funcionando.' $w3
    if (-not ($w3.exit_code -eq 0 -and $w3.completed -and $w3.data_ok)) {
        throw 'Com traducao ligada o caminho do anel parou: rollback imediato. A prova de traducao nao pode ser interpretada sem este controle.'
    }

    # M: A PROVA. O pacote carrega 0x0000_0800_0000_1000; o dado tem de aparecer
    # na VRAM em 0x0FD01000. Esse offset nao aparece no pacote.
    $prova = Invoke-TranslatedWrite 'submissao_traduzida' $vaProof $proofVram $proofData
    Add-Gate 'M_TRADUCAO_PROVADA' ($prova.exit_code -eq 0 -and $prova.completed -and $prova.data_ok) `
        ('O pacote carrega VA 0x{0:X} e o dado apareceu na VRAM em 0x{1:X8}. Nada alem da page table liga um ao outro.' -f $vaProof, $proofVram) `
        $prova

    # N: descarta erro de laboratorio. Se o modo cru tivesse sido ignorado e a
    # parte baixa do VA virasse offset de VRAM, o dado estaria aqui.
    $decoyLido = @()
    for ($i = 0; $i -lt 4; $i++) { $decoyLido += Read-Vram "decoy_post_$i" ($decoyVram + 4*$i) }
    $decoyIntacto = ($null -eq (Compare-Object $savedDecoy $decoyLido -SyncWindow 0))
    Add-Gate 'N_dado_nao_foi_para_o_offset_baixo' $decoyIntacto `
        ('VRAM 0x{0:X8} intacta: o dado nao foi escrito como se o VA fosse offset.' -f $decoyVram) `
        ([pscustomobject]@{
            antes = @($savedDecoy | ForEach-Object { '0x{0:X8}' -f $_ })
            depois = @($decoyLido | ForEach-Object { '0x{0:X8}' -f $_ })
        })

    # O: falha de protecao. Se a traducao gerou falha, e aqui que aparece.
    $dumpProva = Run 'gcvm_dump_pos_prova' @('gcvm','dump')
    $mapaProva = Get-DumpMap $dumpProva
    $faultStatus = $mapaProva['FAULT_STATUS']
    Add-Gate 'O_sem_falha_de_protecao' ($faultStatus -match '0x00000000') `
        'FAULT_STATUS limpo depois da submissao traduzida.' `
        ([pscustomobject]@{ fault_status = $faultStatus; fault_addr = $mapaProva['FAULT_ADDR'] })
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
        # Traducao sai primeiro. Deixar CONTEXT0 apontando para uma tabela que o
        # rollback da VRAM vai apagar seria a pior combinacao possivel.
        if ($snapshotTaken) {
            $rbTxn = Run 'gcvm_txn_restore' @('gcvm','txn','restore') -AllowFailure
            $divRb = if (($rbTxn.output -join ' ') -match 'divergencias\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
            $restRb = if (($rbTxn.output -join ' ') -match 'restaurados\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
            Add-Gate 'P_rollback_devolve_tudo' ($rbTxn.exit_code -eq 0 -and $divRb -eq 0 -and $restRb -eq $txnRegCount) `
                "Os $txnRegCount registradores voltam ao snapshot, com readback conferido." `
                ([pscustomobject]@{ restaurados = $restRb; divergencias = $divRb; saida = $rbTxn.output })
        }
        # Passo 6: o bypass voltou? A mesma submissao que funcionava antes tem
        # de voltar a funcionar. So da para medir aqui: depois do restore dos
        # registradores e antes do rollback da SDMA, que destroi o anel.
        if ($snapshotTaken -and $sdmaCreated) {
            $w4 = Invoke-Write 'escrita_bypass_apos_rollback' ($destination + 48) @([uint32]0xBC25DD01L,[uint32]0xBC25DD02L,[uint32]0xBC25DD03L,[uint32]0xBC25DD04L)
            Add-Gate 'Q_bypass_volta_a_funcionar' ($w4.exit_code -eq 0 -and $w4.completed -and $w4.data_ok) `
                'Desfeita a traducao, a mesma submissao de antes volta a funcionar.' $w4
        }
        if ($ptBuilt) {
            $rbPt = Run 'gcvm_ptrollback' @('gcvm','ptrollback') -AllowFailure
            if ($rbPt.exit_code -ne 0 -and -not $failure) { $failure = 'Rollback da page table falhou.' }
        }
        for ($i = 0; $i -lt $savedProof.Count; $i++) {
            Write-Vram "proof_restore_$i" ($proofVram + 4*$i) $savedProof[$i]
        }
        if ($sdmaCreated) {
            $rb = Run 'sdma_rollback' @('sdma','rollback') -AllowFailure
            if ($rb.exit_code -ne 0 -and -not $failure) { $failure = 'Rollback SDMA falhou.' }
        }
        for ($i = 0; $i -lt $saved.Count; $i++) {
            Write-Vram "dest_restore_$i" ($destination + 4*$i) $saved[$i]
        }
        if ($pspCreated) { Run 'psp_rollback' @('psp','rollback') -AllowFailure | Out-Null }
    }
}

try {
    if ($snapshotTaken -and $armed) {
        $dumpFinal = Run 'gcvm_dump_final' @('gcvm','dump') -AllowFailure
        $mudadosFinal = Compare-Dump $mapaInicial (Get-DumpMap $dumpFinal)
        Add-Gate 'R_dump_volta_ao_inicial' ($mudadosFinal.Count -eq 0) `
            'Depois do rollback o dump de traducao e identico ao do inicio.' `
            ([pscustomobject]@{ campos_mudados = $mudadosFinal })
    }
    if ($armed) {
        for ($i = 0; $i -lt $saved.Count; $i++) { $restored += Read-Vram "dest_post_$i" ($destination + 4*$i) }
        $destOk = ($saved.Count -eq $restored.Count -and -not (Compare-Object $saved $restored -SyncWindow 0))
        $sdmaPost = Run 'sdma_status_post' @('sdma','status') -AllowFailure
        $tp = $sdmaPost.output -join ' '
        $wptrPost = if ($tp -match 'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
        Add-Gate 'S_estado_final_limpo' ($destOk -and $wptrPost -eq 0) `
            'Destino restaurado e WPTR de volta a zero.' `
            ([pscustomobject]@{ destino_restaurado = $destOk; wptr = $wptrPost })
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
} catch { if (-not $failure) { $failure = $_.Exception.Message } }

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'T_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }
$provaPassou = [bool](@($gates | Where-Object { $_.gate -eq 'M_TRADUCAO_PROVADA' -and $_.passed }).Count)

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Memory gate steps 3 (risk stages), 4, 5 and 6: enable VMID 0 translation, invalidate the TLB, prove a translated submission, roll back'
    verdict = $verdict
    passed = (-not $failure)
    translation_proved = $provaPassou
    failure = $failure
    not_covered = @(
        'Only VMID 0 was configured; contexts 1..15 were deliberately left as the POST left them',
        'Only invalidation engines 0 and 17 had their address range programmed',
        'A single 4 KB page was exercised as a translated destination, not the whole 2 MB range',
        'Upstream builds L2_CNTL3/4/5 from the reset default constant; this driver starts from the POST value',
        'No claim is made about CP/PM4 or compute: this is SDMA through the GFXHUB only'
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
        va_base = ('0x{0:X}' -f $vaBase)
        va_end = ('0x{0:X}' -f $vaEnd)
        va_proof = ('0x{0:X}' -f $vaProof)
        proof_vram_offset = ('0x{0:X8}' -f $proofVram)
        decoy_vram_offset = ('0x{0:X8}' -f $decoyVram)
        sdma_vm_hub = 'GFXHUB(0), per sdma_v5_0.c: ring->vm_hub = AMDGPU_GFXHUB(0)'
        invalidation_engine = 17
    }
    stages_exercised = $estagios
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
if ($failure) {
    Write-Host ''
    if ($provaPassou) {
        Write-Host 'ATENCAO: a submissao traduzida PASSOU, mas outro gate reprovou.'
        Write-Host 'Leia a evidencia antes de concluir qualquer coisa.'
    }
    Write-Error $failure
    exit 1
}
Write-Host ''
Write-Host 'Traducao de endereco EXERCITADA no silicio: submissao cujo endereco'
Write-Host ('so resolve pela page table (VA 0x{0:X} -> VRAM 0x{1:X8}).' -f $vaProof, $proofVram)
Write-Host 'Rollback completo: o bypass do POST voltou e o dump de traducao esta igual.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'REBOOT NECESSARIO: firmware SDMA fica ativo nesta sessao.'
exit 0
