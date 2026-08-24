<#
    Passo 2 do gate de memoria: provar o caminho de volta dos registradores de
    traducao ANTES de escrever qualquer valor novo neles.

    Por que este passo existe sozinho: ligar traducao substitui o bypass do qual
    todo o acesso a memoria da placa depende hoje, e a medicao de 2026-08-23
    mostrou que engine SDMA quebrada aqui nao volta por software. Um snapshot que
    nunca foi exercitado nao e rede de seguranca; e suposicao.

    O 'restore' reescreve exatamente os valores lidos. Isso e deliberado: se
    escrever valor identico tivesse efeito colateral em algum desses
    registradores, e melhor descobrir agora, com o bypass intacto, do que depois
    de a placa depender da restauracao.

    A prova de que nada foi perturbado nao e o readback: e uma submissao SDMA
    real, com fence fisica e dado conferido no kernel, feita DEPOIS do restore. O
    readback diz que o registrador aceitou o valor; a submissao diz que o
    caminho de memoria continua funcionando.

    Este ensaio carrega firmware SDMA e portanto exige boot limpo e deixa a
    sessao contaminada ate o proximo reboot.
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
$abiHeader = Join-Path $root 'inc\bc250dbg_abi.h'
$ringOffset = [uint32]0x0FF00000
$destination = [uint32]0x0FF04000
$savedDwords = 12
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute; ele carrega firmware SDMA (ativo ate o reboot).'
}
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute elevado.' }
foreach ($path in @($cli,$abiHeader,$workspaceDriver,$installedDriver,$firmware)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Arquivo ausente: $path" }
}
if ((Get-Service bc250dbg -ErrorAction Stop).Status -ne 'Running') { throw 'bc250dbg parado.' }
$driverHash = (Get-FileHash $workspaceDriver -Algorithm SHA256).Hash
if ($driverHash -ne (Get-FileHash $installedDriver -Algorithm SHA256).Hash) { throw 'Build instalado diverge do workspace.' }

# A contagem sai do header. Ela subiu de 24 para 33 quando o passo 3 passou a
# escrever de verdade, e numero fixo aqui reprovaria este ensaio por motivo
# cosmetico — o mesmo defeito que as versoes de ABI fixadas causaram.
$txnRegCount = $null
foreach ($linha in Get-Content -LiteralPath $abiHeader) {
    if ($linha -match 'BC250DBG_GCVM_TXN_REG_COUNT\s+(\d+)u') { $txnRegCount = [int]$Matches[1]; break }
}
if (-not $txnRegCount) { throw 'Nao foi possivel ler BC250DBG_GCVM_TXN_REG_COUNT do header da ABI.' }

if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'docs\evidence' }
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-gcvm-transaction"
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

$saved = @(); $restored = @(); $failure = $null
$armed = $false; $pspCreated = $false; $sdmaCreated = $false; $firmwareAccepted = $false
# Sem isto, um preflight recusado ainda avaliaria os gates finais e imprimiria
# FALHA em algo que nunca teve chance de rodar. Gate que nao foi exercitado nao
# reprova: ele nao existe naquela execucao.
$snapshotTaken = $false

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
    Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true
    Run 'psp_loadfw' @('psp','loadfw','sdma0',$firmware) | Out-Null; $firmwareAccepted = $true
    Run 'sdma_f32' @('sdma','f32') | Out-Null
    Run 'sdma_init' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") | Out-Null
    $sdmaCreated = $true

    # Base: o bypass funciona. Sem isto, nada abaixo distingue efeito colateral
    # de caminho que nunca funcionou.
    $w1 = Invoke-Write 'escrita_antes_da_transacao' $destination @([uint32]0xBC25AA01L,[uint32]0xBC25AA02L,[uint32]0xBC25AA03L,[uint32]0xBC25AA04L)
    Add-Gate 'A_bypass_funciona_antes' ($w1.exit_code -eq 0 -and $w1.completed -and $w1.data_ok) `
        'Submissao com fence fisica e dado conferido antes de tocar a transacao.' $w1
    if (-not ($w1.exit_code -eq 0 -and $w1.completed -and $w1.data_ok)) {
        throw 'A engine nao escreveu antes da transacao: sem base para medir.'
    }

    $dumpAntes = Run 'gcvm_dump_antes' @('gcvm','dump')
    $mapaAntes = Get-DumpMap $dumpAntes

    $snap = Run 'gcvm_txn_snapshot' @('gcvm','txn','snapshot') -AllowFailure
    if ($snap.exit_code -eq 0) { $snapshotTaken = $true }
    Add-Gate 'B_snapshot_guardado' ($snap.exit_code -eq 0 -and (($snap.output -join ' ') -match ('registradores\s+:\s+' + $txnRegCount))) `
        "Os $txnRegCount registradores de configuracao de traducao ficam guardados no driver." `
        ([pscustomobject]@{ exit_code = $snap.exit_code; saida = $snap.output })

    $ver1 = Run 'gcvm_txn_verify_antes' @('gcvm','txn','verify') -AllowFailure
    $div1 = Get-Divergencias $ver1
    Add-Gate 'C_verify_sem_divergencia' ($ver1.exit_code -eq 0 -and $div1 -eq 0) `
        'Entre snapshot e verify nada mudou: o estado e estavel.' `
        ([pscustomobject]@{ divergencias = $div1; saida = $ver1.output })

    $rest = Run 'gcvm_txn_restore' @('gcvm','txn','restore') -AllowFailure
    $divR = Get-Divergencias $rest
    $restaurados = if (($rest.output -join ' ') -match 'restaurados\s+:\s+(\d+)') { [int]$Matches[1] } else { $null }
    Add-Gate 'D_restore_aceito_por_todos' ($rest.exit_code -eq 0 -and $divR -eq 0 -and $restaurados -eq $txnRegCount) `
        'Cada registrador aceita de volta o valor lido, com readback conferido.' `
        ([pscustomobject]@{ restaurados = $restaurados; divergencias = $divR; saida = $rest.output })

    $dumpDepois = Run 'gcvm_dump_depois' @('gcvm','dump')
    $mapaDepois = Get-DumpMap $dumpDepois
    $mudados = @()
    foreach ($campo in $mapaAntes.Keys) {
        if ($mapaDepois.ContainsKey($campo) -and $mapaDepois[$campo] -ne $mapaAntes[$campo]) {
            $mudados += "$campo : $($mapaAntes[$campo]) -> $($mapaDepois[$campo])"
        }
    }
    # Este gate reprovou em 2026-08-23 10:05 mostrando
    # INVALIDATE_ENG0_SEM 1 -> 0 e ACK 0 -> 0x00010000. Nao era efeito do
    # restore: era o proprio dump adquirindo o semaforo de invalidacao ao ler
    # SEM. O dump parou de ler SEM, e o estado do semaforo passou a ser
    # observado pelo bit SEMAPHORE do ACK, que nao tem efeito de leitura.
    Add-Gate 'E_dump_identico_apos_restore' ($mudados.Count -eq 0) `
        'O dump completo de traducao continua igual depois do restore.' `
        ([pscustomobject]@{ campos_mudados = $mudados })

    # A prova que importa: o caminho de memoria continua funcionando depois de
    # reescrever todos os registradores de traducao da transacao.
    $w2 = Invoke-Write 'escrita_depois_da_transacao' ($destination + 16) @([uint32]0xBC25BB01L,[uint32]0xBC25BB02L,[uint32]0xBC25BB03L,[uint32]0xBC25BB04L)
    Add-Gate 'F_bypass_funciona_depois' ($w2.exit_code -eq 0 -and $w2.completed -and $w2.data_ok) `
        'Reescrever os mesmos valores nao perturbou o caminho de memoria.' $w2
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
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
    # Cada gate final so e avaliado se a etapa correspondente aconteceu.
    if ($snapshotTaken) {
        $ver2 = Run 'gcvm_txn_verify_final' @('gcvm','txn','verify') -AllowFailure
        $div2 = Get-Divergencias $ver2
        Add-Gate 'G_verify_final_sem_divergencia' ($ver2.exit_code -eq 0 -and $div2 -eq 0) `
            "No fim do ensaio, os $txnRegCount registradores continuam iguais ao snapshot." `
            ([pscustomobject]@{ divergencias = $div2; saida = $ver2.output })
    }

    if ($armed) {
        for ($i = 0; $i -lt $saved.Count; $i++) { $restored += Read-Vram "dest_post_$i" ($destination + 4*$i) }
        $destOk = ($saved.Count -eq $restored.Count -and -not (Compare-Object $saved $restored -SyncWindow 0))
        $sdmaPost = Run 'sdma_status_post' @('sdma','status') -AllowFailure
        $tp = $sdmaPost.output -join ' '
        $wptrPost = if ($tp -match 'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})') { [Convert]::ToUInt32($Matches[1],16) } else { $null }
        Add-Gate 'H_estado_final_limpo' ($destOk -and $wptrPost -eq 0) `
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
Add-Gate 'I_sem_bugcheck' ($bugchecks.Count -eq 0) 'Nenhum bugcheck na janela do ensaio.' $bugchecks

$reprovados = @($gates | Where-Object { -not $_.passed } | ForEach-Object { $_.gate })
if ($reprovados.Count -and -not $failure) { $failure = 'Gates reprovados: ' + ($reprovados -join ', ') }

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'Step 2 of the memory gate: prove the rollback path of the translation registers before writing new values'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    not_covered = @(
        'No new value was written to any translation register: the restore rewrites what it read',
        'Whether the GPU accepts a real page table remains step 5'
    )
    reboot_required = $firmwareAccepted
    environment = [pscustomobject]@{
        driver_sha256 = $driverHash.ToLowerInvariant()
        ring_dwords = $RingDwords
        registers_in_transaction = $txnRegCount
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
Write-Host 'Passo 2 do gate de memoria: caminho de volta provado, com submissao SDMA antes e depois.'
Write-Host 'Nenhum valor NOVO foi escrito em registrador de traducao.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'REBOOT NECESSARIO: firmware SDMA fica ativo nesta sessao.'
exit 0
