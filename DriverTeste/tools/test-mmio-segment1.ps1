<#
    Verificacao do segmento 1 do MMIO do GC.

    POR QUE ISTO VEM ANTES DE QUALQUER ESCRITA NO CP

    O endereco de um registrador SOC15 e

        byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4

    O segmento 0 usa base 0x1260 e esta amplamente verificado: 35 registradores
    de traducao escritos e conferidos por readback, GRBM_STATUS, SCRATCH legado,
    CC_GC_SHADER_ARRAY_CONFIG batendo com o valor visto no Linux, e a aritmetica
    de page table fechando com FB_OFFSET/FB_LOCATION.

    O segmento 1 usa base 0xA000 e esta no catalogo como classe P1 — DERIVADO,
    nunca conferido contra registrador vivo.

    E todo o caminho de direct load do CP mora no segmento 1:
    CP_PFP_IC_BASE_LO/HI/CNTL, CP_PFP_IC_OP_CNTL, CP_PFP_UCODE_ADDR/DATA. Escrever
    nesses enderecos com a base nao verificada seria escrever MMIO por suposicao,
    que e exatamente o que as regras deste projeto proibem.

    Em 2026-08-23 esse risco se materializou em forma leve: a primeira versao de
    test-cp-firmware.ps1 calculou RLC_CNTL e RLC_SAFE_MODE com a base do
    segmento 0 e relatou como RLC valores que eram de outro endereco qualquer.

    O ALVO, E POR QUE ELE E SEGURO

    SCRATCH_REG0..7, dwords 0x2040..0x2047 do segmento 1, bytes
    0x30100..0x3011C. Registrador de rascunho existe para ser escrito: nao
    configura nada, nao dispara nada. O upstream escreve 0xCAFEDEAD em
    SCRATCH_REG0 em gfx_v10_0_ring_test_ring() por essa exata razao.

    Ainda assim, o ensaio guarda os oito valores originais e os devolve.

    O CONTROLE QUE DISCRIMINA OS DOIS SEGMENTOS

    O dword 0x2040 existe nos dois segmentos com nomes diferentes:

        seg 0, byte 0x0CA80 -> TCP_WATCH0_ADDR_H
        seg 1, byte 0x30100 -> SCRATCH_REG0

    O ensaio le 0x0CA80 antes e depois, e NUNCA escreve nele. Se as escritas em
    0x30100 aparecerem em 0x0CA80, as duas derivacoes colidem e nenhuma esta
    provada. Se 0x0CA80 ficar intacto e 0x30100 guardar o que foi escrito, a base
    do segmento 1 esta localizada.

    O QUE ESTE ENSAIO NAO FAZ

    Nao carrega firmware. Nao toca no CP, na SDMA ou na traducao. NAO CONTAMINA A
    SESSAO: pode rodar em qualquer boot, e nao exige reinicializacao depois.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 20
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

# Bases de segmento, como o catalogo as registra.
$segBase = @{ 0 = 0x1260; 1 = 0xA000 }
function Get-Byte([int]$Seg, [uint32]$Dw) { return [uint32](($segBase[$Seg] + $Dw) * 4) }

# SCRATCH_REG0..7: dwords 0x2040..0x2047, segmento 1.
$scratchDw = 0x2040
$scratchCount = 8
$scratch = @()
for ($i = 0; $i -lt $scratchCount; $i++) {
    $scratch += [pscustomobject]@{
        nome = "SCRATCH_REG$i"
        dw   = [uint32]($scratchDw + $i)
        byte = Get-Byte 1 ([uint32]($scratchDw + $i))
    }
}
# Controle: mesmo dword, segmento 0. Outro registrador, outro endereco.
$controleByte = Get-Byte 0 ([uint32]$scratchDw)      # TCP_WATCH0_ADDR_H
$controleNome = 'TCP_WATCH0_ADDR_H'

# Dois padroes complementares. Um padrao so nao distingue armazenamento de
# valor preso: se o registrador estivesse travado no valor que ja tinha, uma
# unica escrita coincidente passaria.
$padraoA = @()
$padraoB = @()
for ($i = 0; $i -lt $scratchCount; $i++) {
    $padraoA += [uint32](0xBC250000L + $i)
    $padraoB += [uint32]((0xBC250000L + $i) -bxor 0xFFFFFFFFL)
}

# Testemunhas em segmento 0, ja verificado. Se alguma mudar, a escrita em
# scratch teve efeito colateral em lugar que nao devia.
$testemunhas = [ordered]@{
    'GRBM_STATUS'  = Get-Byte 0 0x0da4
    'GRBM_STATUS2' = Get-Byte 0 0x0da2
    'CP_STAT'      = Get-Byte 0 0x0f40
    'CP_ME_CNTL'   = Get-Byte 0 0x0f56
}

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute. Ele NAO contamina a sessao e nao exige reboot.'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$workspaceDriver,$installedDriver)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-mmio-segment1"
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
function Write-Reg([string]$Name, [uint32]$Offset, [uint32]$Value) {
    Run $Name @('reg','w',('0x{0:X5}' -f $Offset),('0x{0:X8}' -f $Value)) -AllowFailure | Out-Null
}
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}
function Format-Hex($V) {
    if ($null -eq $V) { return 'ILEGIVEL' }
    return ('0x{0:X8}' -f $V)
}
# Escreve um padrao inteiro e devolve o que foi lido de volta, registrador por
# registrador. A conferencia acontece fora, para que a saida traga sempre os
# valores crus.
function Invoke-Padrao([string]$Rotulo, [uint32[]]$Padrao) {
    for ($i = 0; $i -lt $scratchCount; $i++) {
        Write-Reg "${Rotulo}_w$i" $scratch[$i].byte $Padrao[$i]
    }
    $lido = @()
    for ($i = 0; $i -lt $scratchCount; $i++) {
        $lido += Read-Reg "${Rotulo}_r$i" $scratch[$i].byte
    }
    return $lido
}

$originais = @(); $restaurados = @(); $failure = $null
$armed = $false; $escreveu = $false
$controleAntes = $null; $controleDepois = $null
$testemunhaAntes = [ordered]@{}; $testemunhaDepois = [ordered]@{}

try {
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+\s+armamento=readonly') {
        throw 'Preflight recusado: info nao reportou ABI, ou armamento nao esta em readonly.'
    }

    # A: tudo legivel antes de escrever.
    for ($i = 0; $i -lt $scratchCount; $i++) {
        $originais += Read-Reg "orig_$i" $scratch[$i].byte
    }
    $controleAntes = Read-Reg 'controle_antes' $controleByte
    foreach ($k in $testemunhas.Keys) { $testemunhaAntes[$k] = Read-Reg "test_antes_$k" ([uint32]$testemunhas[$k]) }

    $ilegiveis = @()
    for ($i = 0; $i -lt $scratchCount; $i++) { if ($null -eq $originais[$i]) { $ilegiveis += $scratch[$i].nome } }
    if ($null -eq $controleAntes) { $ilegiveis += $controleNome }
    Add-Gate 'A_tudo_legivel_antes' ($ilegiveis.Count -eq 0) `
        'Os oito enderecos do segmento 1 e o controle do segmento 0 responderam a leitura.' `
        ([pscustomobject]@{
            originais = @(0..($scratchCount-1) | ForEach-Object { '{0} @ 0x{1:X5} = {2}' -f $scratch[$_].nome, $scratch[$_].byte, (Format-Hex $originais[$_]) })
            controle = '{0} @ 0x{1:X5} = {2}' -f $controleNome, $controleByte, (Format-Hex $controleAntes)
            ilegiveis = $ilegiveis
        })
    if ($ilegiveis.Count) { throw 'Endereco ilegivel: nao ha o que medir.' }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true

    # B: o padrao A volta, registrador por registrador. Oito valores DIFERENTES
    # em oito enderecos consecutivos: se fossem espelhos do mesmo registrador,
    # todos leriam o ultimo valor escrito.
    $lidoA = Invoke-Padrao 'padraoA' $padraoA
    $escreveu = $true
    $errosA = @()
    for ($i = 0; $i -lt $scratchCount; $i++) {
        if ($lidoA[$i] -ne $padraoA[$i]) {
            $errosA += '{0} @ 0x{1:X5}: escreveu {2}, leu {3}' -f `
                $scratch[$i].nome, $scratch[$i].byte, (Format-Hex $padraoA[$i]), (Format-Hex $lidoA[$i])
        }
    }
    Add-Gate 'B_padrao_A_volta_por_registrador' ($errosA.Count -eq 0) `
        'Cada um dos oito enderecos guardou o SEU valor: sao registradores independentes, nao espelhos.' `
        ([pscustomobject]@{
            escrito = @($padraoA | ForEach-Object { Format-Hex $_ })
            lido = @($lidoA | ForEach-Object { Format-Hex $_ })
            erros = $errosA
        })

    # C: segundo padrao, complementar. Um padrao so nao distingue armazenamento
    # de valor preso que por acaso coincidiu.
    $lidoB = Invoke-Padrao 'padraoB' $padraoB
    $errosB = @()
    for ($i = 0; $i -lt $scratchCount; $i++) {
        if ($lidoB[$i] -ne $padraoB[$i]) {
            $errosB += '{0} @ 0x{1:X5}: escreveu {2}, leu {3}' -f `
                $scratch[$i].nome, $scratch[$i].byte, (Format-Hex $padraoB[$i]), (Format-Hex $lidoB[$i])
        }
    }
    Add-Gate 'C_padrao_complementar_volta' ($errosB.Count -eq 0) `
        'O complemento tambem volta: e armazenamento de verdade, nao valor preso.' `
        ([pscustomobject]@{
            escrito = @($padraoB | ForEach-Object { Format-Hex $_ })
            lido = @($lidoB | ForEach-Object { Format-Hex $_ })
            erros = $errosB
        })

    # D: o controle. Mesmo dword, segmento 0, outro registrador. Nunca escrito.
    $controleDepois = Read-Reg 'controle_depois' $controleByte
    Add-Gate 'D_segmento_0_nao_recebeu_as_escritas' ($controleAntes -eq $controleDepois) `
        ('{0} em 0x{1:X5} ficou intacto: as escritas foram para o segmento 1, e as duas derivacoes nao colidem.' -f $controleNome, $controleByte) `
        ([pscustomobject]@{
            registrador = $controleNome
            endereco = ('0x{0:X5}' -f $controleByte)
            antes = Format-Hex $controleAntes
            depois = Format-Hex $controleDepois
        })

    # E: nada mais se mexeu onde sabemos olhar.
    foreach ($k in $testemunhas.Keys) { $testemunhaDepois[$k] = Read-Reg "test_depois_$k" ([uint32]$testemunhas[$k]) }
    $mudadas = @()
    foreach ($k in $testemunhas.Keys) {
        if ($testemunhaAntes[$k] -ne $testemunhaDepois[$k]) {
            $mudadas += '{0} : {1} -> {2}' -f $k, (Format-Hex $testemunhaAntes[$k]), (Format-Hex $testemunhaDepois[$k])
        }
    }
    Add-Gate 'E_sem_efeito_colateral_nas_testemunhas' ($mudadas.Count -eq 0) `
        'GRBM e CP, em segmento 0 ja verificado, nao mudaram por causa das escritas em scratch.' `
        ([pscustomobject]@{
            antes = @($testemunhas.Keys | ForEach-Object { '{0} = {1}' -f $_, (Format-Hex $testemunhaAntes[$_]) })
            depois = @($testemunhas.Keys | ForEach-Object { '{0} = {1}' -f $_, (Format-Hex $testemunhaDepois[$_]) })
            mudadas = $mudadas
        })
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
        if ($escreveu) {
            for ($i = 0; $i -lt $scratchCount -and $i -lt $originais.Count; $i++) {
                Write-Reg "restore_$i" $scratch[$i].byte $originais[$i]
            }
        }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}

try {
    if ($escreveu) {
        for ($i = 0; $i -lt $scratchCount; $i++) { $restaurados += Read-Reg "post_$i" $scratch[$i].byte }
        $errosR = @()
        for ($i = 0; $i -lt $scratchCount -and $i -lt $originais.Count; $i++) {
            if ($restaurados[$i] -ne $originais[$i]) {
                $errosR += '{0}: original {1}, agora {2}' -f `
                    $scratch[$i].nome, (Format-Hex $originais[$i]), (Format-Hex $restaurados[$i])
            }
        }
        Add-Gate 'F_valores_originais_devolvidos' ($errosR.Count -eq 0) `
            'Os oito registradores voltaram ao valor que tinham antes do ensaio.' `
            ([pscustomobject]@{
                originais = @($originais | ForEach-Object { Format-Hex $_ })
                restaurados = @($restaurados | ForEach-Object { Format-Hex $_ })
                erros = $errosR
            })
    }
} catch { if (-not $failure) { $failure = $_.Exception.Message } }

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'G_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Verify the GC MMIO segment 1 base (0xA000 dwords) against live registers, before any CP register is written'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    not_covered = @(
        'Only the scratch bank was exercised; it verifies the base, not any other segment 1 register semantics',
        'No CP register was written, no firmware loaded, no ring created',
        'Segment 2 and beyond remain unverified'
    )
    reboot_required = $false
    contaminates_session = $false
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        segment_base_dw = [pscustomobject]@{ seg0 = ('0x{0:X}' -f $segBase[0]); seg1 = ('0x{0:X}' -f $segBase[1]) }
        derivation = 'byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4'
        target = 'SCRATCH_REG0..7, dw 0x2040..0x2047, BASE_IDX 1'
        target_bytes = @($scratch | ForEach-Object { '{0} = 0x{1:X5}' -f $_.nome, $_.byte })
        control = ('{0} (seg 0, same dw 0x2040) = 0x{1:X5}, read only' -f $controleNome, $controleByte)
        upstream_precedent = 'gfx_v10_0_ring_test_ring() writes 0xCAFEDEAD to mmSCRATCH_REG0'
    }
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
Write-Host 'Base do segmento 1 do GC LOCALIZADA em 0xA000 dwords:'
Write-Host '  oito enderecos consecutivos guardaram oito valores distintos, e o'
Write-Host '  complemento tambem voltou — e armazenamento, nao espelho nem valor preso.'
Write-Host ("  {0} em 0x{1:X5}, mesmo dword no segmento 0, ficou intacto." -f $controleNome, $controleByte)
Write-Host ''
Write-Host 'Isto habilita o caminho de direct load do CP, que mora todo no segmento 1.'
Write-Host 'NAO habilita nenhuma afirmacao sobre o CP funcionar.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'Sessao NAO contaminada: nenhum firmware carregado, valores devolvidos.'
exit 0
