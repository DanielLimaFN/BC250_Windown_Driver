<#
    Mapa de responsividade do MMIO do GC: quais registradores respondem e quais
    devolvem 0xFFFFFFFF.

    POR QUE ISTO EXISTE

    Medido em 2026-08-23: os registradores da cache de instrucoes do CP, nos
    enderecos CORRETOS do segmento 1, leem 0xFFFFFFFF e descartam escrita. No
    MESMO segmento, SCRATCH_REG0..7 respondem normalmente. No segmento 0,
    CP_ME_CNTL le 0x15000000 de forma estavel.

    Ler todos-os-bits-em-um nao e um valor: e a assinatura de um endereco que nao
    esta sendo decodificado. E isso produziu um falso positivo grave — a espera
    por IC_OP_CNTL.INVALIDATE_CACHE_COMPLETE (bit 1) terminava na primeira
    leitura, porque 0xFFFFFFFF tem esse bit ligado. Duas sessoes declararam
    "o hardware confirmou por COMPLETE" sem que o registrador tivesse respondido.

    A PERGUNTA QUE ESTE ENSAIO RESPONDE

    Que parte do GC esta respondendo? A hipotese em aberto e que o bloco de GFX
    esteja com energia ou clock desligados, e que o RLC seja o portao. Se
    RLC_CNTL responder, a rota do RLC tem por onde comecar. Se devolver
    0xFFFFFFFF, o problema e anterior ao RLC e a rota muda.

    O QUE ESTE ENSAIO NAO FAZ

    Nao escreve em NENHUM registrador. Nao carrega firmware. Nao arma o driver.
    E leitura pura, roda em qualquer sessao e nao exige reboot depois.

    Todos os enderecos abaixo saem de gc_10_1_0_offset.h pela regra
      byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4
    com 0x1260 para BASE_IDX 0 e 0xA000 para BASE_IDX 1, base do segmento 1
    conferida contra registrador vivo em 2026-08-23 17:54.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 30,
    [ValidateRange(2,16)][int]$Repeticoes = 3
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

# nome, dw, segmento, para que serve na investigacao
$alvos = @(
    # --- Segmento 0: o controle. Sabe-se que responde. ---
    [pscustomobject]@{ nome='GRBM_STATUS';        dw=0x0da4; seg=0; papel='controle, valor conhecido 0x00003028' }
    [pscustomobject]@{ nome='GRBM_STATUS2';       dw=0x0da2; seg=0; papel='controle' }
    [pscustomobject]@{ nome='CP_ME_CNTL';         dw=0x0f56; seg=0; papel='controle, HALT das tres engines' }
    [pscustomobject]@{ nome='CP_STAT';            dw=0x0f40; seg=0; papel='ocupacao do CP' }
    [pscustomobject]@{ nome='CP_PFP_INSTR_PNTR';  dw=0x0f45; seg=0; papel='PC do PFP' }
    [pscustomobject]@{ nome='CP_RB0_CNTL';        dw=0x1de1; seg=0; papel='anel GFX 0' }
    [pscustomobject]@{ nome='CP_RB0_RPTR';        dw=0x0f60; seg=0; papel='anel GFX 0' }
    # --- Segmento 1: o que responde e o que nao responde ---
    [pscustomobject]@{ nome='SCRATCH_REG0';       dw=0x2040; seg=1; papel='controle do segmento 1, comprovadamente responde' }
    [pscustomobject]@{ nome='SCRATCH_REG7';       dw=0x2047; seg=1; papel='controle do segmento 1' }
    [pscustomobject]@{ nome='GRBM_GFX_INDEX';     dw=0x2200; seg=1; papel='vizinho proximo do scratch' }
    [pscustomobject]@{ nome='CP_MES_DOORBELL_CTRL1'; dw=0x283c; seg=1; papel='sonda de fronteira, abaixo da zona morta' }
    [pscustomobject]@{ nome='RLC_CNTL';           dw=0x4c00; seg=1; papel='portao do GFX; mediu 0x0 em boot limpo' }
    [pscustomobject]@{ nome='RLC_SAFE_MODE';      dw=0x4c05; seg=1; papel='modo seguro do RLC' }
    [pscustomobject]@{ nome='RLC_PG_CNTL';        dw=0x4c43; seg=1; papel='power gating do GFX' }
    [pscustomobject]@{ nome='RLC_CGCG_CGLS_CTRL'; dw=0x4c49; seg=1; papel='clock gating de granularidade grossa' }
    [pscustomobject]@{ nome='RLC_SPARE_INT';      dw=0x4ccc; seg=1; papel='usado pela interface RLCG de acesso a registrador' }
    [pscustomobject]@{ nome='RLC_GPM_STAT';       dw=0x4e6e; seg=1; papel='estado do GPM do RLC' }
    [pscustomobject]@{ nome='CGTT_CPF_CLK_CTRL';  dw=0x50b1; seg=1; papel='SUSPEITO: clock gating do CPF' }
    [pscustomobject]@{ nome='CGTT_CPC_CLK_CTRL';  dw=0x50b2; seg=1; papel='SUSPEITO: clock gating do CPC' }
    [pscustomobject]@{ nome='CP_PFP_UCODE_ADDR';  dw=0x5814; seg=1; papel='porta de jump table do PFP' }
    [pscustomobject]@{ nome='CP_PFP_IC_BASE_LO';  dw=0x5840; seg=1; papel='medido: 0xFFFFFFFF' }
    [pscustomobject]@{ nome='CP_PFP_IC_BASE_CNTL';dw=0x5842; seg=1; papel='medido: 0xFFFFFFFF' }
    [pscustomobject]@{ nome='CP_PFP_IC_OP_CNTL';  dw=0x5843; seg=1; papel='medido: 0xFFFFFFFF, causou o falso positivo' }
    [pscustomobject]@{ nome='CP_ME_IC_BASE_LO';   dw=0x5844; seg=1; papel='mesma vizinhanca' }
    [pscustomobject]@{ nome='CP_CE_IC_BASE_LO';   dw=0x5848; seg=1; papel='mesma vizinhanca' }
    [pscustomobject]@{ nome='CP_MEC_ME1_UCODE_ADDR'; dw=0x581a; seg=1; papel='mesma vizinhanca, engine de compute' }
    # A sonda que decide se a zona morta e do CP ou se e limite de janela:
    # dw 0x5b61 fica ACIMA de toda a vizinhanca do CP.
    [pscustomobject]@{ nome='RLC_GPM_UCODE_ADDR';  dw=0x5b61; seg=1; papel='DECIDE: acima da zona morta. Se responder, o limite nao e de janela' }
)

$segBase = @{ 0 = 0x1260; 1 = 0xA000 }
foreach ($a in $alvos) {
    $a | Add-Member -NotePropertyName byte -NotePropertyValue ([uint32](($segBase[$a.seg] + $a.dw) * 4))
}

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute. Ele e SOMENTE LEITURA, nao arma o driver e nao contamina a sessao.'
}
foreach ($path in @($cli)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-mmio-responsividade"
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
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}
function Format-Hex($V) {
    if ($null -eq $V) { return 'ILEGIVEL' }
    return ('0x{0:X8}' -f $V)
}
# Bits de ocupacao, do header autoritativo do kernel (gc_10_1_0_sh_mask.h,
# SHA-256 cba20414ecf02e91bafdcfbff7d4ba686326f0bdaf236d1a883364f08410c167).
# Servem para dizer se a GPU esta em repouso ou com trabalho pendurado.
# Sufixo L obrigatorio acima de 0x7FFFFFFF: sem ele o PowerShell 5.1 tipa o
# literal como Int32 e 0x80000000 vira -2147483648. Isto ja custou uma execucao
# abortada no fim, e e o mesmo hazard que a telemetria enfrentou antes.
$bitsOcupacao = @(
    [pscustomobject]@{ reg='GRBM_STATUS';  mask=[uint32]0x80000000L; nome='GUI_ACTIVE' }
    [pscustomobject]@{ reg='GRBM_STATUS';  mask=[uint32]0x20000000L; nome='CP_BUSY' }
    [pscustomobject]@{ reg='GRBM_STATUS2'; mask=[uint32]0x40000000L; nome='RLC_BUSY' }
    [pscustomobject]@{ reg='GRBM_STATUS2'; mask=[uint32]0x10000000L; nome='CPF_BUSY' }
    [pscustomobject]@{ reg='GRBM_STATUS2'; mask=[uint32]0x01000000L; nome='CPG_BUSY' }
    [pscustomobject]@{ reg='CP_STAT';      mask=[uint32]0x80000000L; nome='CP_BUSY' }
    [pscustomobject]@{ reg='CP_STAT';      mask=[uint32]0x04000000L; nome='CE_BUSY' }
    [pscustomobject]@{ reg='CP_STAT';      mask=[uint32]0x00008000L; nome='PFP_BUSY' }
)
function Decode-Ocupacao([string]$Reg, $Valor) {
    if ($null -eq $Valor) { return @() }
    $acesos = @()
    foreach ($b in $bitsOcupacao) {
        if ($b.reg -ne $Reg) { continue }
        if (([uint32]$Valor -band $b.mask) -ne 0) { $acesos += $b.nome }
    }
    return @($acesos | Sort-Object)
}

$failure = $null
$leituras = @()

try {
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+') {
        throw 'Preflight recusado: info nao reportou versao/ABI do driver.'
    }

    foreach ($a in $alvos) {
        $vals = @()
        for ($i = 0; $i -lt $Repeticoes; $i++) {
            $vals += Read-Reg ("rd_{0}_{1}" -f $a.nome, $i) $a.byte
        }
        $distintos = @($vals | Where-Object { $null -ne $_ } | Sort-Object -Unique)
        $todosUm = [bool](@($vals | Where-Object { $_ -eq [uint32]::MaxValue }).Count -eq $Repeticoes)
        $ilegivel = [bool](@($vals | Where-Object { $null -eq $_ }).Count -gt 0)
        $leituras += [pscustomobject]@{
            nome = $a.nome
            dw = ('0x{0:X4}' -f $a.dw)
            segmento = $a.seg
            byte = ('0x{0:X5}' -f $a.byte)
            papel = $a.papel
            leituras = @($vals | ForEach-Object { Format-Hex $_ })
            valores_distintos = $distintos.Count
            responde = (-not $todosUm -and -not $ilegivel)
            todos_bits_em_um = $todosUm
        }
    }

    # A: o segmento 0 responde? Se nao, o problema e do laboratorio e nada
    # abaixo pode ser interpretado.
    $seg0 = @($leituras | Where-Object { $_.segmento -eq 0 })
    $seg0Muda = @($seg0 | Where-Object { -not $_.responde })
    Add-Gate 'A_segmento_0_responde' ($seg0Muda.Count -eq 0) `
        'Todos os registradores do segmento 0 responderam: a leitura de MMIO funciona.' `
        ([pscustomobject]@{ nao_responderam = @($seg0Muda | ForEach-Object { $_.nome }); leituras = $seg0 })

    # B: a GPU esta em repouso, ou tem trabalho pendurado de outra sessao?
    #
    # Isto NAO e um requisito do mapa: e o contexto sem o qual o mapa nao pode
    # ser interpretado. Um bloco que nao responde numa GPU com trabalho
    # pendurado e um sintoma possivel do proprio travamento; numa GPU em repouso
    # e uma propriedade do bloco. Sao conclusoes diferentes.
    $ocupacao = @()
    foreach ($nome in @('GRBM_STATUS','GRBM_STATUS2','CP_STAT')) {
        $l = @($leituras | Where-Object { $_.nome -eq $nome })
        if (-not $l.Count) { continue }
        $v = $null
        if ($l[0].leituras[0] -match '^0x([0-9A-Fa-f]{8})$') { $v = [Convert]::ToUInt32($Matches[1],16) }
        $bits = Decode-Ocupacao $nome $v
        if ($bits.Count) {
            $ocupacao += [pscustomobject]@{ registrador = $nome; valor = $l[0].leituras[0]; bits = $bits }
        }
    }
    $emRepouso = ($ocupacao.Count -eq 0)
    Add-Gate 'B_gpu_em_repouso' $emRepouso `
        'Nenhum bit de ocupacao aceso: a GPU esta em repouso e o mapa reflete o bloco, nao um travamento herdado.' `
        ([pscustomobject]@{
            em_repouso = $emRepouso
            ocupacao = $ocupacao
            consequencia_se_reprovar = 'A GPU tem trabalho pendurado de outra sessao. O mapa continua valido como MEDICAO, mas nao serve de linha de base: reinicie e repita para separar "bloco nao responde" de "bloco travado".'
        })

    # C: o scratch do segmento 1 responde? E o controle positivo do segmento.
    $scratch = @($leituras | Where-Object { $_.nome -like 'SCRATCH_REG*' })
    $scratchOk = [bool](@($scratch | Where-Object { $_.responde }).Count -eq $scratch.Count)
    Add-Gate 'C_scratch_do_segmento_1_responde' $scratchOk `
        'O banco de rascunho do segmento 1 responde: a base 0xA000 alcanca hardware vivo.' $scratch

    # D: registra, nao julga. O mapa e o resultado.
    $naoResponde = @($leituras | Where-Object { $_.todos_bits_em_um } | ForEach-Object { $_.nome })
    Add-Gate 'D_mapa_registrado' $true `
        'Mapa de responsividade registrado. Quem devolve todos-os-bits-em-um nao esta decodificando.' `
        ([pscustomobject]@{
            nao_respondem = $naoResponde
            respondem = @($leituras | Where-Object { $_.responde } | ForEach-Object { $_.nome })
            tabela = $leituras
        })

    # E0: a zona morta e do CP, ou e limite da janela de MMIO?
    #
    # RLC_GPM_UCODE_ADDR (dw 0x5b61) fica ACIMA de toda a vizinhanca do CP
    # (dw 0x5814..0x5848). Se ele responder, a janela alcanca esse endereco e a
    # nao-resposta e propriedade do bloco do CP. Se nao responder, a hipotese
    # passa a ser limite de janela ou de aperture, e o alvo da investigacao muda.
    $acima = @($leituras | Where-Object { $_.nome -eq 'RLC_GPM_UCODE_ADDR' })
    if ($acima.Count) {
        Add-Gate 'E0_zona_morta_e_do_cp' ($acima[0].responde) `
            'Um registrador ACIMA da vizinhanca do CP responde: a janela alcanca, e a nao-resposta e do bloco do CP.' `
            ([pscustomobject]@{
                sonda = $acima[0]
                consequencia_se_reprovar = 'Se nem acima da zona morta responde, o limite pode ser de janela ou aperture, e nao propriedade do CP. O alvo da investigacao muda.'
            })
    }

    # E: a pergunta central. RLC_CNTL responde?
    $rlc = @($leituras | Where-Object { $_.nome -eq 'RLC_CNTL' })[0]
    Add-Gate 'E_rlc_cntl_responde' ($rlc.responde) `
        'RLC_CNTL responde, portanto a rota do RLC tem por onde comecar.' `
        ([pscustomobject]@{
            leitura = $rlc
            consequencia_se_reprovar = 'Se RLC_CNTL devolve 0xFFFFFFFF, o problema e anterior ao RLC: nao da para ligar o portao pelo proprio portao. A rota muda.'
        })
} catch {
    $failure = $_.Exception.Message
}

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'F_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

# O gate E pode reprovar legitimamente: e uma pergunta, nao um requisito. O
# veredito do ensaio nao depende dele.
# Os gates B e E sao PERGUNTAS, nao requisitos: "a GPU esta em repouso?" e
# "o RLC responde?". Reprovar o ensaio por causa deles esconderia o resultado
# atras de um erro. Defeito real de leitura (gates A, C, F) continua reprovando.
# Os gates B, E e E0 sao PERGUNTAS, nao requisitos: "a GPU esta em repouso?",
# "o RLC responde?" e "a zona morta e do bloco ou da janela?". A ultima foi
# respondida pela referencia do Linux em 2026-08-23: a zona morta e IDENTICA nos
# dois sistemas e o CP funciona la, entao ela nao e o bloqueio. Reprovar o ensaio
# por causa dela esconderia o resultado atras de um erro.
$reprovadosCriticos = @($gates | Where-Object { -not $_.passed -and $_.gate -notlike 'E_*' -and $_.gate -notlike 'E0_*' -and $_.gate -notlike 'B_*' } | ForEach-Object { $_.gate })
if ($reprovadosCriticos.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovadosCriticos -join ', ') }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Map which GC MMIO registers respond and which return all-ones, to decide whether the CP instruction-cache block is reachable at all and whether the RLC route can even start'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    not_covered = @(
        'Read only: no register was written, no firmware loaded, the driver was not armed',
        'All-ones is read as "not decoding"; this ensaio does not prove WHY a block is not decoding',
        'Says nothing about the CP executing'
    )
    reboot_required = $false
    contaminates_session = $false
    environment = [pscustomobject]@{
        segment_base_dw = [pscustomobject]@{ seg0 = '0x1260'; seg1 = '0xA000 (medida em 2026-08-23 17:54)' }
        derivation = 'byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4'
        repeticoes_por_registrador = $Repeticoes
    }
    leituras = $leituras
    gates = $gates
    failed_gates = $reprovadosCriticos
    bugchecks = $bugchecks
    commands = $events
}
$record | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host "Evidencia: $dir"
foreach ($g in $gates) {
    Write-Host ("  [{0}] {1}" -f $(if ($g.passed) { 'ok  ' } else { 'FALHA' }), $g.gate)
}
Write-Host ''
Write-Host 'Mapa de responsividade do MMIO do GC:'
Write-Host ('  {0,-22} {1,-6} {2,-4} {3,-12} {4}' -f 'registrador','dw','seg','leitura','veredito')
foreach ($l in $leituras) {
    $v = if ($l.todos_bits_em_um) { 'NAO RESPONDE' } elseif ($l.responde) { 'responde' } else { 'ilegivel' }
    Write-Host ('  {0,-22} {1,-6} {2,-4} {3,-12} {4}' -f $l.nome, $l.dw, $l.segmento, $l.leituras[0], $v)
}
Write-Host ''
$rlcLinha = @($leituras | Where-Object { $_.nome -eq 'RLC_CNTL' })
if ($rlcLinha.Count -and $rlcLinha[0].responde) {
    Write-Host 'RLC_CNTL RESPONDE: a rota do RLC tem por onde comecar.'
} elseif ($rlcLinha.Count) {
    Write-Host 'RLC_CNTL NAO RESPONDE: nao da para ligar o portao pelo proprio portao.'
    Write-Host 'O problema e anterior ao RLC, e a rota do CP muda de forma.'
}
Write-Host ''
$ocupLinhas = @($gates | Where-Object { $_.gate -eq 'B_gpu_em_repouso' })
if ($ocupLinhas.Count -and -not $ocupLinhas[0].passed) {
    Write-Host 'ATENCAO: a GPU NAO esta em repouso. Bits de ocupacao acesos:'
    foreach ($o in $ocupLinhas[0].data.ocupacao) {
        Write-Host ('  {0,-14} {1}  ->  {2}' -f $o.registrador, $o.valor, ($o.bits -join ' | '))
    }
    Write-Host ''
    Write-Host 'Isso vem de sessao anterior, nao deste ensaio (ele so le).'
    Write-Host 'O mapa vale como medicao, mas NAO como linha de base: um bloco que'
    Write-Host 'nao responde numa GPU travada pode estar apenas travado. Reinicie e'
    Write-Host 'repita para separar as duas leituras.'
    Write-Host ''
}
$mortos = @($leituras | Where-Object { $_.todos_bits_em_um } | Sort-Object { $_.byte })
$vivos  = @($leituras | Where-Object { $_.responde } | Sort-Object { $_.byte })
if ($mortos.Count -and $vivos.Count) {
    $ultimoVivoAbaixo = @($vivos | Where-Object { $_.byte -lt $mortos[0].byte } | Select-Object -Last 1)
    $primeiroVivoAcima = @($vivos | Where-Object { $_.byte -gt $mortos[-1].byte } | Select-Object -First 1)
    Write-Host 'Fronteira da zona que nao responde:'
    if ($ultimoVivoAbaixo.Count) {
        Write-Host ('  ultimo que responde ABAIXO : {0,-24} {1} (dw {2})' -f $ultimoVivoAbaixo[0].nome, $ultimoVivoAbaixo[0].byte, $ultimoVivoAbaixo[0].dw)
    }
    Write-Host ('  zona morta                 : {0} .. {1}' -f $mortos[0].byte, $mortos[-1].byte)
    if ($primeiroVivoAcima.Count) {
        Write-Host ('  primeiro que responde ACIMA: {0,-24} {1} (dw {2})' -f $primeiroVivoAcima[0].nome, $primeiroVivoAcima[0].byte, $primeiroVivoAcima[0].dw)
        Write-Host '  -> a janela alcanca acima da zona morta: o limite e do BLOCO, nao da janela.'
    } else {
        Write-Host '  -> nada acima da zona morta respondeu: pode ser limite de janela.'
    }
    Write-Host ''
}
Write-Host 'Todos-os-bits-em-um NAO e um valor: e endereco que nao esta decodificando.'
Write-Host "Veredito global permanece: $verdict"
if ($failure) { Write-Error $failure; exit 1 }
exit 0
