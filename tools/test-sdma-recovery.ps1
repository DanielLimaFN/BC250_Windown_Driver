<#
    Experimento controlado sobre RECUPERACAO da SDMA0.

    Por que este script existe separado do ensaio de robustez:

    Todas as tentativas de recuperacao ate 2026-08-23 08:09 foram feitas depois
    de quebrar a engine com um pacote de opcode reservado. Isso mistura duas
    perguntas que precisam ser respondidas em ordem:

      Q1) a sequencia de re-bring-up funciona numa engine SAUDAVEL?
      Q2) se funciona, ela recupera uma engine DANIFICADA?

    Sem responder Q1 primeiro, um "nao recuperou" nao distingue dano
    irreversivel de sequencia de bring-up errada. E foi exatamente o que
    aconteceu: o degrau E3c/E3d assumia que recarregar firmware e recriar o anel
    funcionava, sem nunca ter sido verificado sozinho.

    Este ensaio testa tres variantes, em ordem crescente de intervencao, sempre
    numa engine que acabou de escrever corretamente:

      V1  rollback -> psp loadfw -> sdma init -> escrita
          (controle: re-bring-up sem tocar no bloco)
      V2  rollback -> sdma enginereset -> psp loadfw -> sdma init -> escrita
          (acrescenta o soft reset do bloco)
      V3  rollback -> sdma enginereset -> psp loadfw -> sdma reset -> sdma init
          -> escrita
          (acrescenta tambem o reset do microcontrolador)

    Cada variante termina com uma escrita que precisa passar. Se passar, a
    engine esta comprovadamente boa e a variante seguinte comeca de um estado
    conhecido. Se falhar, o ensaio PARA: continuar mediria variante sobre engine
    quebrada, que e o erro que este script existe para evitar.

    Com -WithInjury, depois de mapear as variantes, injeta um pacote de opcode
    reservado e aplica a primeira variante que funcionou. So aqui a pergunta Q2
    e respondida.

    Veredito impresso continua sendo NAO VALIDADO EM HARDWARE.
#>
param(
    [switch]$Execute,
    [switch]$WithInjury,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 20,
    [ValidateSet(64,128,256,512,1024)][int]$RingDwords = 128
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$firmware = Join-Path $root 'firmware\cyan_skillfish2_sdma.bin'
$firmwareManifest = Join-Path $root 'firmware\manifest.json'
$ringOffset = [uint32]0x0FF00000
$destination = [uint32]0x0FF04000
$dwordsPerOp = 4
$savedDwords = 24
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute; o firmware SDMA fica ativo ate o proximo reboot.'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$workspaceDriver,$installedDriver,$firmware,$firmwareManifest)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
$device = Get-PnpDevice -PresentOnly | Where-Object InstanceId -like 'PCI\VEN_1002&DEV_13FE*' | Select-Object -First 1
if (-not $device -or $device.Service -ne 'BasicDisplay') { throw "BC-250 nao esta em BasicDisplay: $($device.Service)" }
if (Get-Service bc250v4 -ErrorAction SilentlyContinue) { throw 'bc250v4 ainda existe.' }
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }
$manifest = Get-Content $firmwareManifest -Raw | ConvertFrom-Json
$fwEntry = $manifest.firmware | Where-Object name -eq 'cyan_skillfish2_sdma.bin' | Select-Object -First 1
$fwHash = (Get-FileHash $firmware -Algorithm SHA256).Hash.ToLowerInvariant()
if (-not $fwEntry -or -not $fwEntry.structurally_valid -or $fwHash -ne $fwEntry.sha256) {
    throw 'Hash/estrutura do firmware SDMA diverge do manifesto.'
}

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-sdma-recovery"
New-Item -ItemType Directory -Path $dir -Force | Out-Null

$events = [System.Collections.Generic.List[object]]::new()
$results = [System.Collections.Generic.List[object]]::new()

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
function Invoke-Write([string]$Name, [uint32]$Offset, [uint32[]]$Data, [string[]]$Flags) {
    $cmd = @('sdma','write') + @($Flags) + @(('0x{0:X8}' -f $Offset)) +
           @($Data | ForEach-Object { '0x{0:X8}' -f $_ })
    $r = Run $Name $cmd -AllowFailure
    $text = $r.output -join "`n"
    $o = [ordered]@{
        name = $Name
        exit_code = $r.exit_code
        completed = ($text -match 'Conclusao do pacote : SIM')
        fence_reached = ($text -match '\[ALCANCADO\]')
        data_ok = ($text -match 'Resultado completo\s+: VALIDADO NESTA OPERACAO')
        negative_refused = ($text -match 'Caso negativo\s+: REPROVADO COMO ESPERADO')
        f32_pc = $null
    }
    if ($text -match 'F32 PC\s+: 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+)') {
        $o.f32_pc = "0x$($Matches[1]) -> 0x$($Matches[2]) -> 0x$($Matches[3])"
    }
    return [pscustomobject]$o
}
# Bring-up exatamente igual ao caminho que ja passou em quatro boots limpos:
# firmware pelo PSP e depois 'sdma init'. Nenhum passo extra.
function Invoke-BringUp([string]$Tag, [switch]$WithEngineReset, [switch]$WithF32Reset) {
    $steps = [ordered]@{}
    $rb = Run "rollback_$Tag" @('sdma','rollback') -AllowFailure
    $steps['rollback'] = $rb.exit_code
    if ($WithEngineReset) {
        $er = Run "enginereset_$Tag" @('sdma','enginereset') -AllowFailure
        $steps['enginereset'] = $er.exit_code
    }
    $fw = Run "loadfw_$Tag" @('psp','loadfw','sdma0',$firmware) -AllowFailure
    $steps['psp_loadfw'] = $fw.exit_code
    if ($WithF32Reset) {
        $f32 = Run "f32reset_$Tag" @('sdma','reset') -AllowFailure
        $steps['f32_reset'] = $f32.exit_code
    }
    $init = Run "init_$Tag" @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") -AllowFailure
    $steps['sdma_init'] = $init.exit_code
    return [pscustomobject]$steps
}

$saved = @(); $restored = @(); $failure = $null
$armed = $false; $pspCreated = $false; $sdmaCreated = $false; $firmwareAccepted = $false
$offset = 0
function Next-Offset { $script:offset++; return [uint32]($destination + ($script:offset - 1) * $dwordsPerOp * 4) }
function New-Data { param([int]$Seed) @([uint32](0xBC250000L + $Seed),[uint32](0x13570000L + $Seed),
                                       [uint32](0x24680000L + $Seed),[uint32](0x5AA50000L + $Seed)) }

try {
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+\s+armamento=readonly') {
        throw 'Preflight recusado: info nao reportou ABI, ou armamento nao esta em readonly.'
    }
    $sdmaPre = Run 'sdma_status_pre' @('sdma','status')
    $t = $sdmaPre.output -join ' '
    if ($t -notmatch 'RB_CNTL\s+:\s+0x([0-9A-Fa-f]{8})') { throw 'Nao foi possivel validar RB_CNTL.' }
    $preRbCntl = [Convert]::ToUInt32($Matches[1],16)
    if ($t -notmatch 'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})') { throw 'Nao foi possivel validar WPTR.' }
    $preWptr = [Convert]::ToUInt32($Matches[1],16)
    if (($preRbCntl -band 1) -ne 0 -or $preWptr -ne 0) {
        throw ('Preflight recusado: SDMA residual RB_CNTL=0x{0:X8} WPTR=0x{1:X8}. Reinicie.' -f $preRbCntl,$preWptr)
    }
    $ucode = Run 'ucode_checksum_pre' @('reg','r','0x04A24')
    if (($ucode.output -join ' ') -notmatch '=\s*0x00000000') {
        throw 'Preflight recusado: firmware SDMA ja ativo nesta sessao. Reinicie.'
    }
    for ($i = 0; $i -lt $savedDwords; $i++) { $saved += Read-Vram "dest_pre_$i" ($destination + 4*$i) }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true
    Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true
    Run 'psp_loadfw_inicial' @('psp','loadfw','sdma0',$firmware) | Out-Null; $firmwareAccepted = $true
    Run 'sdma_f32_inicial' @('sdma','f32') | Out-Null
    Run 'sdma_init_inicial' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") | Out-Null
    $sdmaCreated = $true

    # Referencia: a engine precisa escrever ANTES de qualquer experimento.
    $w0 = Invoke-Write 'escrita_referencia' (Next-Offset) (New-Data 1) @()
    $results.Add([pscustomobject]@{ etapa = 'referencia'; ok = ($w0.exit_code -eq 0 -and $w0.completed -and $w0.data_ok); detalhe = $w0 })
    if (-not ($w0.exit_code -eq 0 -and $w0.completed -and $w0.data_ok)) {
        throw 'A engine nao escreveu no estado inicial: sem base para medir recuperacao.'
    }

    # --- V1: re-bring-up sem tocar no bloco (o controle que faltava) --------
    $v1steps = Invoke-BringUp 'v1'
    $w1 = Invoke-Write 'escrita_v1' (Next-Offset) (New-Data 2) @()
    $v1ok = ($w1.exit_code -eq 0 -and $w1.completed -and $w1.data_ok)
    $results.Add([pscustomobject]@{ etapa = 'V1_rebringup_sem_reset'; ok = $v1ok; passos = $v1steps; detalhe = $w1 })
    if (-not $v1ok) {
        $failure = 'V1 falhou: recarregar firmware e recriar o anel NAO devolve uma engine saudavel. ' +
                   'Toda conclusao anterior sobre recuperacao apoiada nesse caminho fica sem base.'
        throw $failure
    }

    # --- V2: acrescenta o soft reset do bloco -------------------------------
    $v2steps = Invoke-BringUp 'v2' -WithEngineReset
    $w2 = Invoke-Write 'escrita_v2' (Next-Offset) (New-Data 3) @()
    $v2ok = ($w2.exit_code -eq 0 -and $w2.completed -and $w2.data_ok)
    $results.Add([pscustomobject]@{ etapa = 'V2_com_soft_reset_do_bloco'; ok = $v2ok; passos = $v2steps; detalhe = $w2 })
    if (-not $v2ok) {
        $failure = 'V2 falhou: o soft reset do bloco quebra uma engine que estava saudavel. ' +
                   'O reset nao serve como mecanismo de recuperacao enquanto isso nao for resolvido.'
        throw $failure
    }

    # --- V3: acrescenta o reset do microcontrolador -------------------------
    $v3steps = Invoke-BringUp 'v3' -WithEngineReset -WithF32Reset
    $w3 = Invoke-Write 'escrita_v3' (Next-Offset) (New-Data 4) @()
    $v3ok = ($w3.exit_code -eq 0 -and $w3.completed -and $w3.data_ok)
    $results.Add([pscustomobject]@{ etapa = 'V3_com_reset_do_f32'; ok = $v3ok; passos = $v3steps; detalhe = $w3 })

    # --- Q2: so agora a injuria --------------------------------------------
    if ($WithInjury) {
        $injuryOffset = Next-Offset
        $inj = Invoke-Write 'injuria_opcode_reservado' $injuryOffset (New-Data 5) @('-badop')
        $results.Add([pscustomobject]@{
            etapa = 'injuria'; ok = $inj.negative_refused
            detalhe = $inj
            nota = 'O caso negativo tem de reprovar; isso confirma que a injuria aconteceu.'
        })

        $variante = if ($v2ok) { 'V2' } elseif ($v1ok) { 'V1' } else { $null }
        if ($variante) {
            $steps = if ($variante -eq 'V2') { Invoke-BringUp 'pos_injuria' -WithEngineReset }
                     else { Invoke-BringUp 'pos_injuria' }
            # Depois da injuria o driver marca a engine como suspeita e recusa
            # submissao. Aqui a recusa precisa ser contornada de proposito: o
            # objetivo do ensaio e medir o dano, nao confiar na engine.
            $wr = Invoke-Write 'escrita_pos_injuria' (Next-Offset) (New-Data 6) @('-ignoresuspect')
            $ok = ($wr.exit_code -eq 0 -and $wr.completed -and $wr.data_ok)
            $results.Add([pscustomobject]@{
                etapa = "recuperacao_pos_injuria_com_$variante"; ok = $ok; passos = $steps; detalhe = $wr
            })
            if (-not $ok) {
                $failure = "A injuria de opcode reservado sobrevive a ${variante}: nenhum caminho por software recupera."
            }
        }
    }
} catch {
    if (-not $failure) { $failure = $_.Exception.Message }
} finally {
    if ($armed) {
        if ($sdmaCreated) { Run 'rollback_final' @('sdma','rollback') -AllowFailure | Out-Null }
        for ($i = 0; $i -lt $saved.Count; $i++) {
            Run "dest_restore_$i" @('vram','w',('0x{0:X8}' -f ($destination + 4*$i)),('0x{0:X8}' -f $saved[$i])) -AllowFailure | Out-Null
        }
        if ($pspCreated) { Run 'psp_rollback' @('psp','rollback') -AllowFailure | Out-Null }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}
try {
    for ($i = 0; $i -lt $saved.Count; $i++) { $restored += Read-Vram "dest_post_$i" ($destination + 4*$i) }
    if ($saved.Count -ne $restored.Count -or (Compare-Object $saved $restored -SyncWindow 0)) {
        if (-not $failure) { $failure = 'Conteudo original do destino nao foi restaurado.' }
    }
    Run 'sdma_status_post' @('sdma','status') -AllowFailure | Out-Null
    Run 'info_post' @('info') -AllowFailure | Out-Null
} catch { if (-not $failure) { $failure = $_.Exception.Message } }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Controlled recovery experiment: validate re-bring-up on a healthy engine before judging damage'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    with_injury = [bool]$WithInjury
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        firmware_sha256 = $fwHash
        ring_dwords = $RingDwords
    }
    reboot_required = $firmwareAccepted
    results = $results
    destination_before = @($saved | ForEach-Object { '0x{0:X8}' -f $_ })
    destination_after_restore = @($restored | ForEach-Object { '0x{0:X8}' -f $_ })
    commands = $events
}
$record | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code) timeout=$($_.timed_out)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host "Evidencia: $dir"
foreach ($r in $results) {
    Write-Host ("  [{0}] {1}" -f $(if ($r.ok) { 'ok  ' } else { 'FALHA' }), $r.etapa)
}
if ($failure) { Write-Error $failure; exit 1 }
Write-Host 'Todas as etapas passaram.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'REBOOT NECESSARIO para recuperar o estado de firmware do boot.'
exit 0
