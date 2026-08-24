<#
    Ensaio do gate de recuperacao REESCRITO.

    Seis boots e quatro configuracoes de recuperacao estabeleceram que a injuria
    de opcode reservado nao tem caminho de volta por software. O gate original
    exigia "recuperacao deterministica apos falha", o que a medicao mostrou nao
    existir para essa classe de injuria. Insistir seria perseguir algo
    inexistente; esconder seria pior.

    O gate foi reescrito em requisitos verificaveis, e este ensaio mede os tres
    primeiros no silicio:

      G1  O driver nunca emite pacote malformado.
          Prova: toda entrada invalida do IOCTL de operacao e recusada antes de
          virar pacote, e o WPTR nao se move. Depois da bateria, uma submissao
          normal ainda precisa funcionar — a fronteira nao pode ter efeito
          colateral.

      G2  Deteccao deterministica do estado mentiroso.
          Prova: apos a injuria, o driver marca a engine como suspeita, RECUSA
          submissao normal por conta propria, e quando forcado a medir reporta
          conclusao negativa mesmo que ponteiros e fence estejam bons.

      G3  Falha limpa.
          Prova: com a engine condenada, o rollback ainda devolve 24
          registradores e a VRAM sem divergencia, o anel fica desabilitado, o
          WPTR volta a zero, o F32 fica haltado, o destino e restaurado, o
          driver continua respondendo e nenhum bugcheck ocorre.

    O que este ensaio NAO faz: alegar que a engine foi recuperada. O item 4 do
    gate — recuperacao em nivel de dispositivo — segue em aberto e exige
    auditoria propria, porque o display esta na mesma GPU.

    Veredito impresso continua sendo NAO VALIDADO EM HARDWARE.
#>
param(
    [switch]$Execute,
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
$savedDwords = 32
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute; a engine SDMA fica condenada ate o proximo reboot.'
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
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-sdma-failclosed"
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
function Read-Reg([string]$Name, [string]$Offset) {
    $r = Run $Name @('reg','r',$Offset); $t = $r.output -join ' '
    if ($t -notmatch '=\s*0x([0-9A-Fa-f]{8})') { throw "Parse de registrador falhou: $Name" }
    return '0x' + $Matches[1].ToUpperInvariant()
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
    return [pscustomobject]@{
        name = $Name
        exit_code = $r.exit_code
        completed = ($text -match 'Conclusao do pacote : SIM')
        fence_reached = ($text -match '\[ALCANCADO\]')
        data_ok = ($text -match 'Resultado completo\s+: VALIDADO NESTA OPERACAO')
        negative_refused = ($text -match 'Caso negativo\s+: REPROVADO COMO ESPERADO')
        suspect_reported = ($text -match 'ENGINE SUSPEITA')
        data_line = @($r.output | Where-Object { $_ -match 'Dados conferidos' }) -join ''
        output = $r.output
    }
}
function Add-Gate([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $gates.Add([pscustomobject]@{ gate = $Name; passed = $Passed; detail = $Detail; data = $Data })
}

$saved = @(); $restored = @(); $failure = $null
$armed = $false; $pspCreated = $false; $sdmaCreated = $false; $firmwareAccepted = $false
$offset = 0
function Next-Offset { $script:offset++; return [uint32]($destination + ($script:offset - 1) * 16) }
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
    if ((Read-Reg 'ucode_checksum_pre' '0x04A24') -ne '0x00000000') {
        throw 'Preflight recusado: firmware SDMA ja ativo nesta sessao. Reinicie.'
    }
    $faultPre = Run 'fault_pre' @('sdma','fault')
    if (($faultPre.output -join ' ') -match 'ENGINE SUSPEITA:\s+SIM') {
        throw 'Preflight recusado: engine ja marcada como suspeita. Reinicie.'
    }
    for ($i = 0; $i -lt $savedDwords; $i++) { $saved += Read-Vram "dest_pre_$i" ($destination + 4*$i) }

    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true
    Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true
    Run 'psp_loadfw' @('psp','loadfw','sdma0',$firmware) | Out-Null; $firmwareAccepted = $true
    Run 'sdma_f32' @('sdma','f32') | Out-Null
    Run 'sdma_init' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") | Out-Null
    $sdmaCreated = $true

    # Base: sem uma escrita boa, nada abaixo significa nada.
    $w0 = Invoke-Write 'escrita_referencia' (Next-Offset) (New-Data 1) @()
    Add-Gate 'referencia' ($w0.exit_code -eq 0 -and $w0.completed -and $w0.data_ok) `
        'Escrita com fence fisica e dado conferido no kernel.' $w0
    if (-not ($w0.exit_code -eq 0 -and $w0.completed -and $w0.data_ok)) {
        throw 'A engine nao escreveu no estado inicial: sem base para medir os gates.'
    }

    # ---------------- G1: fronteira fail-closed --------------------------
    $og = Run 'opgate' @('sdma','opgate') -AllowFailure
    $ogText = $og.output -join "`n"
    $ogAceitos = if ($ogText -match 'Resultado: (\d+) recusados, (\d+) aceitos') { [int]$Matches[2] } else { $null }
    $ogWptrOk = ($ogText -match 'WPTR depois da bateria: 0x[0-9A-Fa-f]{8} \(inalterado\)')
    $g1a = ($og.exit_code -eq 0 -and $ogAceitos -eq 0 -and $ogWptrOk)
    Add-Gate 'G1a_entradas_invalidas_recusadas' $g1a `
        'Nenhuma entrada invalida virou pacote e o WPTR nao se moveu.' `
        ([pscustomobject]@{ exit_code = $og.exit_code; aceitos = $ogAceitos; wptr_inalterado = $ogWptrOk; saida = $og.output })

    # A bateria nao pode ter efeito colateral: a engine tem de continuar boa.
    $w1 = Invoke-Write 'escrita_apos_opgate' (Next-Offset) (New-Data 2) @()
    $g1b = ($w1.exit_code -eq 0 -and $w1.completed -and $w1.data_ok)
    Add-Gate 'G1b_sem_efeito_colateral' $g1b `
        'Submissao normal continua funcionando depois da bateria de entradas invalidas.' $w1

    # ---------------- G2: deteccao do estado mentiroso -------------------
    $inj = Invoke-Write 'injuria_opcode_reservado' (Next-Offset) (New-Data 3) @('-badop')
    Add-Gate 'G2a_injuria_reprovada' $inj.negative_refused `
        'O caso negativo tem de reprovar; e o que confirma que a injuria aconteceu.' $inj

    $faultPos = Run 'fault_pos_injuria' @('sdma','fault') -AllowFailure
    $suspeita = (($faultPos.output -join ' ') -match 'ENGINE SUSPEITA:\s+SIM')
    Add-Gate 'G2b_engine_marcada_suspeita' $suspeita `
        'O driver tem de marcar a engine, sem depender de quem chama.' `
        ([pscustomobject]@{ saida = $faultPos.output })

    # O ponto central do gate: o driver recusa por conta propria.
    $wRef = Invoke-Write 'submissao_normal_deve_ser_recusada' (Next-Offset) (New-Data 4) @()
    $recusou = ($wRef.exit_code -ne 0 -and -not $wRef.completed)
    Add-Gate 'G2c_driver_recusa_submissao' $recusou `
        'Com a engine condenada, submissao normal e recusada pelo proprio driver.' $wRef

    # Forcando a medicao: mesmo com ponteiros e fence bons, nao pode concluir.
    $wForced = Invoke-Write 'medicao_forcada_ignoresuspect' (Next-Offset) (New-Data 5) @('-ignoresuspect')
    $naoConcluiu = (-not $wForced.completed)
    Add-Gate 'G2d_conclusao_negada_mesmo_com_fence' $naoConcluiu `
        'Fence e ponteiros bons nao produzem conclusao sem o dado conferido.' $wForced
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
        if ($sdmaCreated) {
            $rb = Run 'sdma_rollback' @('sdma','rollback') -AllowFailure
            $rbText = $rb.output -join "`n"
            $rbRegs = ($rbText -match 'registradores\s+:\s+24 restaurados, 0 divergencias')
            $rbVram = ($rbText -match 'VRAM\s+:\s+\d+ dwords restaurados, 0 divergencias')
            Add-Gate 'G3a_rollback_sem_divergencia' ($rb.exit_code -eq 0 -and $rbRegs -and $rbVram) `
                'Com a engine condenada, o rollback ainda devolve registradores e VRAM.' `
                ([pscustomobject]@{ exit_code = $rb.exit_code; regs_ok = $rbRegs; vram_ok = $rbVram; saida = $rb.output })
        }
        for ($i = 0; $i -lt $saved.Count; $i++) {
            Run "dest_restore_$i" @('vram','w',('0x{0:X8}' -f ($destination + 4*$i)),('0x{0:X8}' -f $saved[$i])) -AllowFailure | Out-Null
        }
        if ($pspCreated) { Run 'psp_rollback' @('psp','rollback') -AllowFailure | Out-Null }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
}

# ---------------- G3: estado final e sobrevivencia do driver -------------
$postWptr = $null; $postRbCntl = $null; $postF32 = $null
try {
    for ($i = 0; $i -lt $saved.Count; $i++) { $restored += Read-Vram "dest_post_$i" ($destination + 4*$i) }
    $destOk = ($saved.Count -eq $restored.Count -and -not (Compare-Object $saved $restored -SyncWindow 0))

    $sdmaPost = Run 'sdma_status_post' @('sdma','status') -AllowFailure
    $t = $sdmaPost.output -join ' '
    if ($t -match 'RB_CNTL\s+:\s+0x([0-9A-Fa-f]{8})') { $postRbCntl = [Convert]::ToUInt32($Matches[1],16) }
    if ($t -match 'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})') { $postWptr = [Convert]::ToUInt32($Matches[1],16) }
    $postF32 = Read-Reg 'f32_cntl_post' '0x04A28'
    $infoPost = Run 'info_post' @('info') -AllowFailure

    $estadoOk = ($destOk -and $postWptr -eq 0 -and ($postRbCntl -band 1) -eq 0 -and
                 (([Convert]::ToUInt32($postF32.Substring(2),16) -band 1) -eq 1) -and
                 $infoPost.exit_code -eq 0)
    Add-Gate 'G3b_estado_final_seguro' $estadoOk `
        'Anel desabilitado, WPTR zero, F32 haltado, destino restaurado e driver respondendo.' `
        ([pscustomobject]@{
            destino_restaurado = $destOk; wptr = $postWptr; rb_cntl = $postRbCntl
            f32_cntl = $postF32; info_exit = $infoPost.exit_code
        })
} catch { if (-not $failure) { $failure = $_.Exception.Message } }

$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
Add-Gate 'G3c_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Rewritten recovery gate: never emit malformed packets, detect the lying state, fail clean'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    not_covered = @(
        'Device-level recovery (gate item 4) remains open and unaudited: the display shares this GPU',
        'The reserved-opcode injury has no software recovery path; that is documented behaviour, not a fixed defect'
    )
    reboot_required = $firmwareAccepted
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        firmware_sha256 = $fwHash
        ring_dwords = $RingDwords
    }
    gates = $gates
    failed_gates = $reprovados
    destination_before = @($saved | ForEach-Object { '0x{0:X8}' -f $_ })
    destination_after_restore = @($restored | ForEach-Object { '0x{0:X8}' -f $_ })
    bugchecks = $bugchecks
    commands = $events
}
$record | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code) timeout=$($_.timed_out)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

Write-Host "Evidencia: $dir"
foreach ($g in $gates) {
    Write-Host ("  [{0}] {1}" -f $(if ($g.passed) { 'ok  ' } else { 'FALHA' }), $g.gate)
}
if ($failure) { Write-Error $failure; exit 1 }
Write-Host 'Gate reescrito: itens 1 a 3 medidos e aprovados.'
Write-Host 'Item 4 (recuperacao em nivel de dispositivo) permanece em aberto.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'REBOOT NECESSARIO: a engine SDMA fica condenada nesta sessao.'
exit 0
