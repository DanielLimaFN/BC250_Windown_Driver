<#
    Ensaio de robustez, repeticao, wraparound, retomada e casos negativos da
    SDMA0. Este script NAO substitui tools/test-sdma-minimal.ps1: ele assume
    que o gate minimo (uma submissao com fence fisica) ja passou no mesmo boot
    limpo e vai adiante nas provas exigidas por BC250_HARDWARE_RULES.md.

    O que ele tenta demonstrar, nesta ordem:

      A) repeticao   - varias submissoes seguidas, sem estado latchado;
      B) wraparound  - o anel da a volta e continua correto;
      C) retomada    - depois de ficar ocioso, o motor volta a executar;
      D) negativos   - sem kick, fence incorreta e opcode reservado NAO podem
                       produzir sucesso aparente;
      E) recuperacao - depois de cada negativo, uma submissao normal volta a
                       funcionar, e o rollback devolve registradores e VRAM.

    Nada aqui declara vitoria: o veredito impresso continua sendo
    NAO VALIDADO EM HARDWARE ate a analise dos artefatos.
#>
param(
    [switch]$Execute,
    [string]$OutputRoot,
    [ValidateRange(2,60)][int]$CommandTimeoutSeconds = 15,
    [ValidateRange(4,64)][int]$Submissions = 12,
    [ValidateSet(64,128,256,512,1024)][int]$RingDwords = 128,
    [ValidateRange(2,60)][int]$IdleSeconds = 6,
    [switch]$SkipBadOpcode,
    [switch]$SkipHealthyReset
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'
$workspaceDriver = Join-Path $root 'output\bc250dbg.sys'
$installedDriver = Join-Path $env:SystemRoot 'System32\drivers\bc250dbg.sys'
$firmware = Join-Path $root 'firmware\cyan_skillfish2_sdma.bin'
$firmwareManifest = Join-Path $root 'firmware\manifest.json'
$ringOffset = [uint32]0x0FF00000
$destination = [uint32]0x0FF04000     # 4 dwords por submissao, fora do anel
$dwordsPerOp = 4
$verdict = ('N{0}O VALIDADO EM HARDWARE' -f [char]0x00C3)
$startedAt = Get-Date

if (-not $Execute) {
    throw 'Ensaio bloqueado por padrao. Use -Execute sabendo que o firmware SDMA fica ativo ate o proximo reboot.'
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
$dir = Join-Path $OutputRoot "$(Get-Date -Format yyyy-MM-dd_HH-mm-ss)-$env:COMPUTERNAME-sdma-robustness"
New-Item -ItemType Directory -Path $dir -Force | Out-Null

$events = [System.Collections.Generic.List[object]]::new()
$phases = [System.Collections.Generic.List[object]]::new()
$telemetryProc = $null; $telemetryOutTask = $null; $telemetryErrTask = $null
$telemetryLines = @(); $telemetryFailure = $null

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
# ATENCAO: o primeiro argumento de 'telemetry watch' e CONTAGEM DE AMOSTRAS,
# nao duracao. O CLI recusa acima de 100000 e, quando recusa, nao emite CSV
# nenhum — foi assim que a primeira execucao ficou sem telemetria.
function Start-TelemetryWatch([int]$Samples) {
    if ($Samples -gt 100000) { $Samples = 100000 }
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $cli
    $psi.Arguments = "telemetry watch $Samples 1 250"
    $psi.WorkingDirectory = $root; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $script:telemetryProc = New-Object Diagnostics.Process
    $script:telemetryProc.StartInfo = $psi
    if (-not $script:telemetryProc.Start()) { throw 'Falha ao iniciar sampler de telemetria.' }
    $script:telemetryOutTask = $script:telemetryProc.StandardOutput.ReadToEndAsync()
    $script:telemetryErrTask = $script:telemetryProc.StandardError.ReadToEndAsync()
}
function Stop-TelemetryWatch {
    if (-not $script:telemetryProc) { return }
    if (-not $script:telemetryProc.HasExited) { $script:telemetryProc.Kill(); $script:telemetryProc.WaitForExit() }
    $script:telemetryLines = @($script:telemetryOutTask.Result -split "`r?`n" | Where-Object { $_ })
    $stderr = $script:telemetryErrTask.Result
    if ($stderr) { $script:telemetryFailure = $stderr.Trim() }
}
# Dump somente leitura do estado de falha da engine. Guardado em varios pontos
# do ensaio para que a comparacao 'saudavel x quebrada' seja mecanica, e nao
# uma leitura a olho de duas paginas de registradores.
$faultDumps = [ordered]@{}
function Get-Fault([string]$Label) {
    $r = Run "fault_$Label" @('sdma','fault') -AllowFailure
    $o = [ordered]@{}
    foreach ($line in $r.output) {
        if ($line -match '^\s{2}([A-Za-z0-9_/ ]+?)\s*:\s*(.+?)\s*$') {
            $o[$Matches[1].Trim()] = $Matches[2].Trim()
        }
    }
    $obj = [pscustomobject]$o
    $script:faultDumps[$Label] = $obj
    return $obj
}
function Compare-Fault([string]$A, [string]$B) {
    if (-not $script:faultDumps.Contains($A) -or -not $script:faultDumps.Contains($B)) { return $null }
    $x = $script:faultDumps[$A]; $y = $script:faultDumps[$B]
    $diff = @()
    foreach ($prop in $x.PSObject.Properties) {
        $other = $y.PSObject.Properties[$prop.Name]
        if ($other -and $other.Value -ne $prop.Value) {
            $diff += [pscustomobject]@{ campo = $prop.Name; $A = $prop.Value; $B = $other.Value }
        }
    }
    return $diff
}
function Add-Phase([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $phases.Add([pscustomobject]@{ phase = $Name; passed = $Passed; detail = $Detail; data = $Data; aborts_run = $true })
    if (-not $Passed) { throw "$Name : $Detail" }
}
# Fase que NAO aborta o ensaio (o resto ainda produz dado util), mas que conta
# para o veredito final. Uma fase reprovada nunca pode virar 'passou'.
function Add-PhaseSoft([string]$Name, [bool]$Passed, [string]$Detail, $Data) {
    $phases.Add([pscustomobject]@{ phase = $Name; passed = $Passed; detail = $Detail; data = $Data; aborts_run = $false })
}

# Uma submissao positiva: exige fence da GPU, RPTR alcancando o WPTR e os
# dwords conferidos pela CPU. Devolve os ponteiros lidos da saida.
function Invoke-Write([string]$Name, [uint32]$Offset, [uint32[]]$Data, [switch]$IgnoreSuspect) {
    # -ignoresuspect so nos degraus de recuperacao: a partir do caso de opcode
    # reservado o driver recusa submissao, e medir o dano exige contornar isso
    # explicitamente.
    $opts = if ($IgnoreSuspect) { @('-ignoresuspect') } else { @() }
    $cmd = @('sdma','write') + $opts + @(('0x{0:X8}' -f $Offset)) + @($Data | ForEach-Object { '0x{0:X8}' -f $_ })
    $r = Run $Name $cmd -AllowFailure
    $text = $r.output -join "`n"
    $sample = [ordered]@{
        name = $Name
        offset = ('0x{0:X8}' -f $Offset)
        exit_code = $r.exit_code
        completed = ($text -match 'Conclusao do pacote : SIM')
        fence_reached = ($text -match '\[ALCANCADO\]')
        data_ok = ($text -match 'Resultado completo\s+: VALIDADO NESTA OPERACAO')
        rptr_before = $null
        rptr_after = $null
        wptr = $null
        gfxoff_before = $null; gfxoff_kick = $null; gfxoff_after = $null
        grbm_before = $null; grbm_kick = $null; grbm_after = $null
        clk_before = $null; clk_kick = $null; clk_after = $null
        status_before = $null; status_kick = $null; status_after = $null
        f32pc_before = $null; f32pc_kick = $null; f32pc_after = $null
    }
    if ($text -match 'RPTR 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+) \(WPTR 0x([0-9A-Fa-f]+)\)') {
        $sample.rptr_before = [Convert]::ToUInt32($Matches[1],16)
        $sample.rptr_after  = [Convert]::ToUInt32($Matches[2],16)
        $sample.wptr        = [Convert]::ToUInt32($Matches[3],16)
    }
    # Telemetria medida DENTRO do kernel, no ponto do kick. Esta e a unica
    # fonte que cobre a janela de carga de um comando curto; o sampler em user
    # mode pode perde-la inteira.
    if ($text -match 'GFXOFF fisico\s+: (\d+)\[[^\]]*\] -> (\d+)\[[^\]]*\] -> (\d+)\[') {
        $sample.gfxoff_before = [int]$Matches[1]
        $sample.gfxoff_kick   = [int]$Matches[2]
        $sample.gfxoff_after  = [int]$Matches[3]
    }
    if ($text -match 'GRBM_STATUS\s+: 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+)') {
        $sample.grbm_before = '0x' + $Matches[1].ToUpperInvariant()
        $sample.grbm_kick   = '0x' + $Matches[2].ToUpperInvariant()
        $sample.grbm_after  = '0x' + $Matches[3].ToUpperInvariant()
    }
    if ($text -match 'SDMA CLK_CTRL\s+: 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+)') {
        $sample.clk_before = '0x' + $Matches[1].ToUpperInvariant()
        $sample.clk_kick   = '0x' + $Matches[2].ToUpperInvariant()
        $sample.clk_after  = '0x' + $Matches[3].ToUpperInvariant()
    }
    if ($text -match 'STATUS_REG\s+: 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+)') {
        $sample.status_before = '0x' + $Matches[1].ToUpperInvariant()
        $sample.status_after  = '0x' + $Matches[2].ToUpperInvariant()
    }
    if ($text -match 'STATUS no kick\s+: 0x([0-9A-Fa-f]+)') {
        $sample.status_kick = '0x' + $Matches[1].ToUpperInvariant()
    }
    if ($text -match 'F32 PC\s+: 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+)') {
        $sample.f32pc_before = '0x' + $Matches[1].ToUpperInvariant()
        $sample.f32pc_kick   = '0x' + $Matches[2].ToUpperInvariant()
        $sample.f32pc_after  = '0x' + $Matches[3].ToUpperInvariant()
    }
    return [pscustomobject]$sample
}

# Um caso negativo. Sucesso do ENSAIO = a operacao ter falhado exatamente como
# previsto; o proprio CLI ja aplica os criterios especificos de cada modo.
function Invoke-Negative([string]$Name, [string]$Flag, [uint32]$Offset, [uint32[]]$Data) {
    $cmd = @('sdma','write',$Flag,('0x{0:X8}' -f $Offset)) + @($Data | ForEach-Object { '0x{0:X8}' -f $_ })
    $r = Run $Name $cmd -AllowFailure
    $text = $r.output -join "`n"
    return [pscustomobject]@{
        name = $Name
        flag = $Flag
        exit_code = $r.exit_code
        refused_as_expected = ($r.exit_code -eq 0 -and $text -match 'Caso negativo\s+: REPROVADO COMO ESPERADO')
        completed = ($text -match 'Conclusao do pacote : SIM')
        fence_reached = ($text -match '\[ALCANCADO\]')
        output = $r.output
    }
}

$regs = [ordered]@{
    clk_ctrl='0x049EC'; cntl='0x049F0'; status='0x04A14'
    ucode_checksum='0x04A24'; f32_cntl='0x04A28'; status2='0x04A60'
}
function Read-RegSet([string]$Prefix) {
    $s = [ordered]@{}
    foreach ($e in $regs.GetEnumerator()) { $s[$e.Key] = Read-Reg "$Prefix`_$($e.Key)" $e.Value }
    return [pscustomobject]$s
}

$totalDwords = ($Submissions + 5) * $dwordsPerOp    # folga para retomada e negativos
$saved = @(); $restored = @(); $failure = $null
$pre = $null; $post = $null
$armed = $false; $pspCreated = $false; $sdmaCreated = $false; $firmwareAccepted = $false
$sequence = @(); $negatives = @(); $recovery = @(); $resume = $null; $wrapObserved = $false
$engineReset = $null; $healthyReset = $null

try {
    # ---------------- Preflight: so roda em boot limpo ----------------
    $infoPre = Run 'info_pre' @('info')
    if (($infoPre.output -join ' ') -notmatch 'bc250dbg v\d+\.\d+\s+ABI \d+\s+armamento=readonly') {
        throw 'Preflight recusado: info nao reportou ABI, ou armamento nao esta em readonly.'
    }
    $pspPre = Run 'psp_status_pre' @('psp','status')
    $pspText = $pspPre.output -join ' '
    if ($pspText -notmatch 'C2PMSG_67\s+:\s+0x([0-9A-Fa-f]{8})') { throw 'Nao foi possivel validar WPTR PSP.' }
    $prePspWptr = [Convert]::ToUInt32($Matches[1],16)
    if ($pspText -notmatch 'anel\s+:\s+lo 0x([0-9A-Fa-f]{8})\s+hi 0x([0-9A-Fa-f]{8})\s+tam 0x([0-9A-Fa-f]{8})') {
        throw 'Nao foi possivel validar base/tamanho do ring PSP.'
    }
    $prePspLo = [Convert]::ToUInt32($Matches[1],16)
    $prePspHi = [Convert]::ToUInt32($Matches[2],16)
    $prePspSize = [Convert]::ToUInt32($Matches[3],16)
    if ($prePspWptr -ne 0 -or $prePspLo -ne 0 -or $prePspHi -ne 0 -or $prePspSize -ne 0) {
        throw 'Preflight recusado: ring PSP externo/residual presente.'
    }
    $sdmaPre = Run 'sdma_status_pre' @('sdma','status')
    $sdmaText = $sdmaPre.output -join ' '
    if ($sdmaText -notmatch 'RB_CNTL\s+:\s+0x([0-9A-Fa-f]{8})') { throw 'Nao foi possivel validar RB_CNTL.' }
    $preRbCntl = [Convert]::ToUInt32($Matches[1],16)
    if ($sdmaText -notmatch 'RB_BASE\s+:\s+0x([0-9A-Fa-f]+)') { throw 'Nao foi possivel validar RB_BASE.' }
    $preRbBase = [Convert]::ToUInt64($Matches[1],16)
    if ($sdmaText -notmatch 'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})') { throw 'Nao foi possivel validar WPTR.' }
    $preWptr = [Convert]::ToUInt32($Matches[1],16)
    if (($preRbCntl -band 1) -ne 0 -or $preRbBase -ne 0 -or $preWptr -ne 0) {
        throw ('Preflight recusado: estado SDMA residual RB_CNTL=0x{0:X8} BASE=0x{1:X} WPTR=0x{2:X8}.' -f $preRbCntl,$preRbBase,$preWptr)
    }
    $pre = Read-RegSet 'pre'
    if ($pre.ucode_checksum -ne '0x00000000' -or (([Convert]::ToUInt32($pre.f32_cntl.Substring(2),16) -band 1) -eq 0)) {
        throw "Preflight recusado: firmware SDMA ja ativo (checksum=$($pre.ucode_checksum), F32_CNTL=$($pre.f32_cntl))."
    }
    for ($i = 0; $i -lt $totalDwords; $i++) { $saved += Read-Vram "destination_pre_$i" ($destination + 4*$i) }

    # ---------------- Bring-up do laboratorio ----------------
    Start-TelemetryWatch 100000
    Run 'arm_write' @('arm','write') | Out-Null; $armed = $true
    Run 'psp_init' @('psp','init') | Out-Null; $pspCreated = $true
    Run 'psp_loadfw_sdma0' @('psp','loadfw','sdma0',$firmware) | Out-Null; $firmwareAccepted = $true
    Run 'sdma_f32' @('sdma','f32') | Out-Null
    $init = Run 'sdma_init' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords")
    if (($init.output -join ' ') -notmatch 'Tamanho do anel\s+:\s+(\d+) dwords') { throw 'CLI nao reportou o tamanho do anel.' }
    if ([int]$Matches[1] -ne $RingDwords) { throw 'Tamanho do anel diferente do solicitado.' }
    $sdmaCreated = $true
    $ringBytes = $RingDwords * 4

    # ---------------- A e B: repeticao e wraparound ----------------
    for ($i = 0; $i -lt $Submissions; $i++) {
        $off = $destination + $i * $dwordsPerOp * 4
        # Sufixo L obrigatorio: sem ele o PowerShell le 0xBC250100 como Int32
        # negativo e o cast para uint32 estoura.
        $data = @(
            [uint32](0xBC250100L + $i),
            [uint32](0x13579BD0L + $i),
            [uint32](0x2468ACE0L + $i),
            [uint32](0x5AA55A00L + $i)
        )
        $s = Invoke-Write "seq_$i" $off $data
        if ($s.exit_code -ne 0 -or -not $s.completed -or -not $s.fence_reached -or -not $s.data_ok) {
            Add-Phase 'A_repeticao' $false "Submissao $i nao produziu fence e resultado completos." $s
        }
        if ($sequence.Count -gt 0) {
            $prev = $sequence[-1]
            if ($s.wptr -le $prev.wptr) {
                Add-Phase 'A_repeticao' $false "WPTR nao avancou entre submissoes ($($prev.wptr) -> $($s.wptr))." $s
            }
            if ([math]::Floor($prev.wptr / $ringBytes) -lt [math]::Floor($s.wptr / $ringBytes)) { $wrapObserved = $true }
        }
        $sequence += $s
    }
    Add-Phase 'A_repeticao' $true "$Submissions submissoes seguidas com fence fisica e dados conferidos." $sequence
    Add-Phase 'B_wraparound' $wrapObserved `
        ("Anel de $ringBytes bytes: WPTR final 0x{0:X}; volta do anel exercitada." -f $sequence[-1].wptr) `
        ([pscustomobject]@{ ring_bytes = $ringBytes; final_wptr = $sequence[-1].wptr; wrapped = $wrapObserved })

    # ---------------- C: ocioso e retomada ----------------
    Run 'sdma_status_idle_begin' @('sdma','status') | Out-Null
    Start-Sleep -Seconds $IdleSeconds
    Run 'telemetry_idle' @('telemetry','sample') | Out-Null
    Run 'sdma_status_idle_end' @('sdma','status') | Out-Null
    $resumeOff = $destination + $Submissions * $dwordsPerOp * 4
    $resume = Invoke-Write 'resume_after_idle' $resumeOff @([uint32]0xBC250FF1L,[uint32]0xBC250FF2L,[uint32]0xBC250FF3L,[uint32]0xBC250FF4L)
    Add-Phase 'C_retomada' ($resume.exit_code -eq 0 -and $resume.completed -and $resume.data_ok) `
        "Apos $IdleSeconds s ocioso a engine voltou a executar." $resume

    # Referencia de engine SAUDAVEL, tirada logo depois de uma submissao que
    # comprovadamente funcionou.
    Get-Fault 'saudavel' | Out-Null

    # ---------------- F: o reset de bloco funciona numa engine SAUDAVEL? ----
    #
    # Isto vem ANTES dos negativos de proposito. Testar um mecanismo de
    # recuperacao apenas depois de quebrar a engine confunde duas perguntas:
    # "o reset esta correto?" e "o dano e recuperavel?". Aqui a engine esta
    # comprovadamente boa, entao a resposta e sobre o reset e mais nada.
    if (-not $SkipHealthyReset) {
        $hr = Run 'enginereset_saudavel' @('sdma','enginereset') -AllowFailure
        Get-Fault 'apos_reset_saudavel' | Out-Null
        Run 'psp_loadfw_pos_reset_saudavel' @('psp','loadfw','sdma0',$firmware) -AllowFailure | Out-Null
        Run 'sdma_reset_pos_reset_saudavel' @('sdma','reset') -AllowFailure | Out-Null
        Get-Fault 'apos_rebringup_saudavel' | Out-Null
        $reinitH = Run 'sdma_init_pos_reset_saudavel' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") -AllowFailure
        if ($reinitH.exit_code -eq 0) { $sdmaCreated = $true }
        $offH = $destination + ($Submissions + 4) * $dwordsPerOp * 4
        $wH = Invoke-Write 'escrita_pos_reset_saudavel' $offH @([uint32]0xBC25E5E1L,[uint32]0xBC25E5E2L,[uint32]0xBC25E5E3L,[uint32]0xBC25E5E4L)
        $healthyReset = [pscustomobject]@{
            reset_exit = $hr.exit_code
            reset_saida = $hr.output
            reinit_exit = $reinitH.exit_code
            escrita = $wH
            recuperou = ($wH.exit_code -eq 0 -and $wH.completed -and $wH.data_ok)
        }
        Add-PhaseSoft 'F_reset_em_engine_saudavel' $healthyReset.recuperou `
            'Soft reset do bloco numa engine boa, seguido de firmware, F32 e anel: precisa voltar a escrever.' $healthyReset
    }

    # ---------------- D: casos negativos ----------------
    # Cada negativo usa dados que NAO estao no destino, para que "destino
    # inalterado" seja uma conclusao valida.
    $negOff = $destination + ($Submissions + 1) * $dwordsPerOp * 4
    $negData = @([uint32]0xDEAD0001L,[uint32]0xDEAD0002L,[uint32]0xDEAD0003L,[uint32]0xDEAD0004L)

    $n1 = Invoke-Negative 'negativo_sem_kick' '-nokick' $negOff $negData
    $negatives += $n1
    Add-Phase 'D1_sem_kick' $n1.refused_as_expected `
        'Sem publicar o WPTR nao pode haver consumo, fence nem escrita.' $n1
    $r1 = Invoke-Write 'recuperacao_apos_sem_kick' $negOff $negData
    $recovery += $r1
    Add-Phase 'E1_recuperacao' ($r1.exit_code -eq 0 -and $r1.completed -and $r1.data_ok) `
        'Submissao normal volta a funcionar depois do caso sem kick.' $r1

    $negOff2 = $negOff + $dwordsPerOp * 4
    $negData2 = @([uint32]0xDEAD0011L,[uint32]0xDEAD0012L,[uint32]0xDEAD0013L,[uint32]0xDEAD0014L)
    $n2 = Invoke-Negative 'negativo_fence_incorreta' '-badfence' $negOff2 $negData2
    $negatives += $n2
    Add-Phase 'D2_fence_incorreta' $n2.refused_as_expected `
        'A GPU escreve a fence, mas com valor diferente: o gate tem de reprovar.' $n2
    $r2 = Invoke-Write 'recuperacao_apos_fence_incorreta' $negOff2 $negData2
    $recovery += $r2
    Add-Phase 'E2_recuperacao' ($r2.exit_code -eq 0 -and $r2.completed -and $r2.data_ok) `
        'Submissao normal volta a funcionar depois da fence incorreta.' $r2

    if (-not $SkipBadOpcode) {
        # Ultimo de proposito: pode deixar o F32 parado. Depois dele so tentamos
        # status, reset, uma submissao de recuperacao e o rollback.
        $negOff3 = $negOff2 + $dwordsPerOp * 4
        $negData3 = @([uint32]0xDEAD0021L,[uint32]0xDEAD0022L,[uint32]0xDEAD0023L,[uint32]0xDEAD0024L)
        $n3 = Invoke-Negative 'negativo_opcode_reservado' '-badop' $negOff3 $negData3
        $negatives += $n3
        Add-Phase 'D3_opcode_reservado' $n3.refused_as_expected `
            'Opcode reservado, sem pacote FENCE: sucesso aparente e proibido.' $n3
        Get-Fault 'apos_badop' | Out-Null
        Run 'sdma_status_apos_badop' @('sdma','status') -AllowFailure | Out-Null
        Run 'sdma_f32_apos_badop' @('sdma','f32') -AllowFailure | Out-Null
        $r3 = Invoke-Write 'recuperacao_apos_opcode' $negOff3 $negData3 -IgnoreSuspect
        $recovery += $r3
        # Nao e falha do ensaio se a engine nao voltar sozinha: e um dado. O que
        # nao pode acontecer e sucesso no proprio caso negativo.
        # ESCADA DE RECUPERACAO.
        #
        # Medido em 2026-08-23: um unico dword com opcode reservado deixa a
        # engine drenando o anel sem executar nada — RPTR alcanca o WPTR, mas
        # nem WRITE nem FENCE escrevem memoria, e o writeback congela atras.
        # Reset do F32 sozinho ja foi testado e NAO recupera.
        #
        # A escada existe para responder uma pergunta objetiva: qual e a acao
        # minima que devolve a engine a um estado utilizavel? Cada degrau e
        # registrado como fase propria, e todos sao obrigatorios para o gate de
        # recuperacao — nenhum deles e decorativo.
        $spontaneous = ($r3.exit_code -eq 0 -and $r3.completed -and $r3.data_ok)
        Add-PhaseSoft 'E3a_retomada_espontanea' $spontaneous `
            'Retomada apos opcode reservado, sem nenhuma intervencao.' $r3

        $recovered = $spontaneous
        if (-not $recovered) {
            # Degrau 1: reset do microcontrolador.
            Run 'sdma_reset_f32' @('sdma','reset') -AllowFailure | Out-Null
            Run 'sdma_status_apos_reset' @('sdma','status') -AllowFailure | Out-Null
            Run 'sdma_f32_apos_reset' @('sdma','f32') -AllowFailure | Out-Null
            Get-Fault 'apos_reset_f32' | Out-Null
            $r3b = Invoke-Write 'recuperacao_apos_reset_f32' $negOff3 $negData3 -IgnoreSuspect
            $recovery += $r3b
            $recovered = ($r3b.exit_code -eq 0 -and $r3b.completed -and $r3b.data_ok)
            Add-PhaseSoft 'E3b_retomada_com_reset_f32' $recovered `
                'Retomada depois de resetar o F32.' $r3b
        }
        if (-not $recovered) {
            # Degrau 2: desfazer e recriar o anel, sem recarregar firmware.
            # O rollback halta o F32, entao o reset tem de vir antes do init.
            Run 'sdma_rollback_recuperacao' @('sdma','rollback') -AllowFailure | Out-Null
            Run 'sdma_reset_apos_rollback' @('sdma','reset') -AllowFailure | Out-Null
            $reinit = Run 'sdma_init_recuperacao' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") -AllowFailure
            if ($reinit.exit_code -eq 0) { $sdmaCreated = $true }
            Get-Fault 'apos_reinit' | Out-Null
            $r3c = Invoke-Write 'recuperacao_apos_reinit_do_anel' $negOff3 $negData3 -IgnoreSuspect
            $recovery += $r3c
            $recovered = ($r3c.exit_code -eq 0 -and $r3c.completed -and $r3c.data_ok)
            Add-PhaseSoft 'E3c_retomada_com_reinit_do_anel' $recovered `
                'Retomada depois de desfazer e recriar o anel.' $r3c
        }
        if (-not $recovered) {
            # Degrau 3: recarregar o firmware pelo PSP e recomecar do zero.
            Run 'sdma_rollback_recuperacao2' @('sdma','rollback') -AllowFailure | Out-Null
            Run 'psp_loadfw_recuperacao' @('psp','loadfw','sdma0',$firmware) -AllowFailure | Out-Null
            Run 'sdma_reset_recuperacao' @('sdma','reset') -AllowFailure | Out-Null
            $reinit2 = Run 'sdma_init_recuperacao2' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") -AllowFailure
            if ($reinit2.exit_code -eq 0) { $sdmaCreated = $true }
            Get-Fault 'apos_recarregar_firmware' | Out-Null
            $r3d = Invoke-Write 'recuperacao_apos_recarregar_firmware' $negOff3 $negData3 -IgnoreSuspect
            $recovery += $r3d
            $recovered = ($r3d.exit_code -eq 0 -and $r3d.completed -and $r3d.data_ok)
            Add-PhaseSoft 'E3d_retomada_com_firmware_recarregado' $recovered `
                'Retomada depois de recarregar o firmware SDMA pelo PSP.' $r3d
        }
        if (-not $recovered) {
            # Degrau 4: soft reset do bloco SDMA0 inteiro, sequencia do
            # upstream. E o unico caminho que reseta a maquina de estados que
            # latchou o opcode invalido em STATUS2_REG.CMD_OP. Perde o anel,
            # entao init e obrigatorio depois.
            Run 'sdma_rollback_recuperacao3' @('sdma','rollback') -AllowFailure | Out-Null
            $er = Run 'sdma_enginereset' @('sdma','enginereset') -AllowFailure
            $erText = $er.output -join "`n"
            if ($erText -match 'CMD_OP latchado : (\d+) -> (\d+)') {
                $cmdOpAntes = [int]$Matches[1]; $cmdOpDepois = [int]$Matches[2]
            } else { $cmdOpAntes = $null; $cmdOpDepois = $null }
            Get-Fault 'apos_enginereset' | Out-Null
            # O soft reset do bloco leva junto a instruction RAM do F32.
            # Medido em 2026-08-23 07:35: sem recarregar o firmware, o PC fica
            # congelado (0x006B nas tres amostras) e o anel volta a 'andar sem
            # executar'. Recarregar pelo PSP e parte obrigatoria da sequencia.
            Run 'psp_loadfw_apos_enginereset' @('psp','loadfw','sdma0',$firmware) -AllowFailure | Out-Null
            Run 'sdma_reset_apos_enginereset' @('sdma','reset') -AllowFailure | Out-Null
            Run 'sdma_f32_apos_enginereset' @('sdma','f32') -AllowFailure | Out-Null
            $reinit3 = Run 'sdma_init_apos_enginereset' @('sdma','init','vram',('0x{0:X8}' -f $ringOffset),"$RingDwords") -AllowFailure
            if ($reinit3.exit_code -eq 0) { $sdmaCreated = $true }
            $r3e = Invoke-Write 'recuperacao_apos_enginereset' $negOff3 $negData3 -IgnoreSuspect
            $recovery += $r3e
            $recovered = ($r3e.exit_code -eq 0 -and $r3e.completed -and $r3e.data_ok)
            $engineReset = [pscustomobject]@{
                exit_code = $er.exit_code
                cmd_op_antes = $cmdOpAntes
                cmd_op_depois = $cmdOpDepois
                recuperou = $recovered
                saida = $er.output
            }
            Add-PhaseSoft 'E3e_retomada_com_soft_reset_do_bloco' $recovered `
                'Soft reset do bloco SDMA0 via GRBM_SOFT_RESET, firmware recarregado e anel recriado.' $r3e
        }
        if (-not $recovered) {
            Add-PhaseSoft 'E3_recuperacao' $false `
                'Nenhuma acao testada devolveu a engine a um estado utilizavel: so resta reboot.' $null
        }
    }

    Run 'sdma_status_pos_negativos' @('sdma','status') | Out-Null
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($armed) {
        if ($sdmaCreated) {
            $rb = Run 'sdma_rollback' @('sdma','rollback') -AllowFailure
            if ($rb.exit_code -ne 0 -and -not $failure) { $failure = 'Rollback SDMA falhou.' }
        }
        for ($i = 0; $i -lt $saved.Count; $i++) {
            Run "destination_restore_$i" @('vram','w',('0x{0:X8}' -f ($destination + 4*$i)),('0x{0:X8}' -f $saved[$i])) -AllowFailure | Out-Null
        }
        if ($pspCreated) {
            $rb = Run 'psp_rollback' @('psp','rollback') -AllowFailure
            if ($rb.exit_code -ne 0 -and -not $failure) { $failure = 'Rollback PSP falhou.' }
        }
        Run 'arm_readonly' @('arm','readonly') -AllowFailure | Out-Null
    }
    Stop-TelemetryWatch
}

# ---------------- Verificacao final do estado ----------------
$postWptr = $null; $postRbCntl = $null; $postRbBase = $null
try {
    for ($i = 0; $i -lt $saved.Count; $i++) { $restored += Read-Vram "destination_post_$i" ($destination + 4*$i) }
    if ($saved.Count -ne $restored.Count -or (Compare-Object $saved $restored -SyncWindow 0)) {
        if (-not $failure) { $failure = 'Conteudo original do destino nao foi restaurado.' }
    }
    $post = Read-RegSet 'post'
    # O teardown tem de terminar com o F32 haltado. O firmware continua
    # carregado ate o reboot, mas o microcontrolador nao pode ficar rodando.
    if ($post -and (([Convert]::ToUInt32($post.f32_cntl.Substring(2),16) -band 1) -eq 0)) {
        if (-not $failure) { $failure = ('Teardown deixou o F32 fora de HALT (F32_CNTL={0}).' -f $post.f32_cntl) }
    }
    $sdmaPost = Run 'sdma_status_post' @('sdma','status')
    $postText = $sdmaPost.output -join ' '
    if ($postText -match 'RB_CNTL\s+:\s+0x([0-9A-Fa-f]{8})') { $postRbCntl = [Convert]::ToUInt32($Matches[1],16) }
    if ($postText -match 'RB_BASE\s+:\s+0x([0-9A-Fa-f]+)')   { $postRbBase = [Convert]::ToUInt64($Matches[1],16) }
    if ($postText -match 'WPTR \(reg\)\s+:\s+0x([0-9A-Fa-f]{8})') { $postWptr = [Convert]::ToUInt32($Matches[1],16) }
    if ($postWptr -ne 0 -or $postRbBase -ne 0 -or ($postRbCntl -band 1) -ne 0) {
        if (-not $failure) {
            $failure = ('Rollback deixou residuo: RB_CNTL=0x{0:X8} BASE=0x{1:X} WPTR=0x{2:X8}.' -f $postRbCntl,$postRbBase,$postWptr)
        }
    }
    Run 'psp_status_post' @('psp','status') | Out-Null
    Run 'info_post' @('info') | Out-Null
} catch {
    if (-not $failure) { $failure = $_.Exception.Message }
}

# Bugcheck durante o ensaio invalida tudo, mesmo que os comandos tenham passado.
$bugchecks = @()
try {
    $bugchecks = @(Get-WinEvent -FilterHashtable @{ LogName='System'; Id=1001; StartTime=$startedAt } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -like '*WER-SystemErrorReporting*' } |
        ForEach-Object { [pscustomobject]@{ time = $_.TimeCreated.ToString('o'); message = ($_.Message -split "`r?`n")[0] } })
} catch { }
if ($bugchecks.Count -and -not $failure) { $failure = 'Bugcheck registrado durante o ensaio.' }

$telemetrySamples = @()
if ($telemetryLines.Count -gt 1) {
    $telemetryLines | Set-Content (Join-Path $dir 'telemetry.csv') -Encoding ASCII
    try { $telemetrySamples = @(($telemetryLines -join "`n") | ConvertFrom-Csv) }
    catch { if (-not $telemetryFailure) { $telemetryFailure = $_.Exception.Message } }
}
$gfxoffHistogram = @()
$samplerWindowSeconds = $null
if ($telemetrySamples.Count) {
    $gfxoffHistogram = @($telemetrySamples | Group-Object gfxoff | ForEach-Object {
        [pscustomobject]@{ state = $_.Name; count = $_.Count }
    })
    # Cobertura real do sampler. Sem isto um CSV curto parece cobrir o ensaio
    # inteiro: na primeira execucao o sampler cobriu 0,665 s de um ensaio de
    # dezenas de segundos.
    $hz = [double]$telemetrySamples[0].qpc_hz
    if ($hz -gt 0) {
        $samplerWindowSeconds = [math]::Round(
            ([double]$telemetrySamples[-1].qpc - [double]$telemetrySamples[0].qpc) / $hz, 3)
    }
}
$runSeconds = [math]::Round(((Get-Date) - $startedAt).TotalSeconds, 3)

# Resumo da janela de carga, medida dentro do kernel em cada submissao. Esta e
# a fonte que vale para GFXOFF/GRBM/clock durante o comando.
$loadWindow = $null
$allOps = @(@($sequence) + @($resume) + @($recovery) | Where-Object { $_ -and $_.status_kick })
if ($allOps.Count) {
    $loadWindow = [pscustomobject]@{
        operations = $allOps.Count
        gfxoff_states_before = @($allOps.gfxoff_before | Sort-Object -Unique)
        gfxoff_states_kick   = @($allOps.gfxoff_kick | Sort-Object -Unique)
        gfxoff_states_after  = @($allOps.gfxoff_after | Sort-Object -Unique)
        grbm_values_kick     = @($allOps.grbm_kick | Sort-Object -Unique)
        clk_values_kick      = @($allOps.clk_kick | Sort-Object -Unique)
        status_values_kick   = @($allOps.status_kick | Sort-Object -Unique)
        status_values_before = @($allOps.status_before | Sort-Object -Unique)
        f32pc_values_kick    = @($allOps.f32pc_kick | Sort-Object -Unique)
        note = 'GFXOFF e do dominio GFX; carga de SDMA nao obriga transicao. Nunca use isto como prova da regra 9.'
    }
}

# INTEGRIDADE DO VEREDITO: uma fase reprovada nunca pode terminar em 'passou'.
# Na execucao de 2026-08-23 00:47 duas fases de recuperacao reprovaram e o
# ensaio ainda imprimiu sucesso, porque so $failure era consultado.
$failedPhases = @($phases | Where-Object { -not $_.passed } | ForEach-Object { $_.phase })
if ($failedPhases.Count -and -not $failure) {
    $failure = 'Fases reprovadas: ' + ($failedPhases -join ', ')
}

$record = [pscustomobject]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    objective = 'SDMA robustness: repetition, ring wraparound, idle/resume, negative cases and recovery'
    verdict = $verdict
    passed = (-not $failure)
    failure = $failure
    limitations = @(
        'A single passing run does not establish determinism; repeat across reboots',
        'Physical power telemetry remains unavailable and temperature is uncalibrated',
        'Firmware runtime state persists until reboot'
    )
    reboot_required = $firmwareAccepted
    environment = [pscustomobject]@{
        display_service = $device.Service
        legacy_bc250v4_absent = $true
        driver_sha256 = $driverHash.ToLowerInvariant()
        firmware_sha256 = $fwHash
        ring_offset = ('0x{0:X8}' -f $ringOffset)
        ring_dwords = $RingDwords
        submissions = $Submissions
        idle_seconds = $IdleSeconds
        bad_opcode_executed = (-not $SkipBadOpcode)
    }
    phases = $phases
    failed_phases = $failedPhases
    fault_dumps = $faultDumps
    fault_diff_saudavel_vs_badop = (Compare-Fault 'saudavel' 'apos_badop')
    fault_diff_badop_vs_reset = (Compare-Fault 'apos_badop' 'apos_reset_f32')
    fault_diff_saudavel_vs_firmware = (Compare-Fault 'saudavel' 'apos_recarregar_firmware')
    fault_diff_badop_vs_enginereset = (Compare-Fault 'apos_badop' 'apos_enginereset')
    engine_reset = $engineReset
    healthy_reset = $healthyReset
    sequence = $sequence
    wrap_observed = $wrapObserved
    resume = $resume
    negatives = $negatives
    recovery = $recovery
    pre_registers = $pre
    post_registers = $post
    post_state = [pscustomobject]@{ rb_cntl = $postRbCntl; rb_base = $postRbBase; wptr = $postWptr }
    destination = ('0x{0:X8}' -f $destination)
    destination_before = @($saved | ForEach-Object { '0x{0:X8}' -f $_ })
    destination_after_restore = @($restored | ForEach-Object { '0x{0:X8}' -f $_ })
    bugchecks = $bugchecks
    telemetry = [pscustomobject]@{
        sample_count = $telemetrySamples.Count
        sampler_window_seconds = $samplerWindowSeconds
        run_seconds = $runSeconds
        covers_full_run = ($samplerWindowSeconds -ne $null -and $samplerWindowSeconds -ge ($runSeconds * 0.8))
        gfxoff_state_histogram = $gfxoffHistogram
        sampler_error = $telemetryFailure
        power_available = $false
        file = if ($telemetrySamples.Count) { 'telemetry.csv' } else { $null }
    }
    load_window = $loadWindow
    commands = $events
}
$record | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $dir 'result.json') -Encoding UTF8
$events | ForEach-Object {
    "=== $($_.name) ==="; "`$ $($_.command)"; $_.output; "exit=$($_.exit_code) timeout=$($_.timed_out)"; ''
} | Set-Content (Join-Path $dir 'result.txt') -Encoding UTF8

# Decodificacao dos dumps com os nomes de bit do header autoritativo. Fica
# gravada junto com a evidencia para nao depender de rodar a ferramenta depois.
$decoder = Join-Path $root 'tools\decode-sdma-fault.py'
if ((Get-Command python -ErrorAction SilentlyContinue) -and (Test-Path $decoder)) {
    try {
        & python $decoder $dir 2>&1 | Set-Content (Join-Path $dir 'fault-decoded.txt') -Encoding UTF8
    } catch { }
}

Write-Host "Evidencia: $dir"
foreach ($ph in $phases) {
    Write-Host ("  [{0}] {1}" -f $(if ($ph.passed) { 'ok  ' } else { 'FALHA' }), $ph.phase)
}
if ($failure) { Write-Error $failure; exit 1 }
Write-Host 'Todas as fases passaram: repeticao, wraparound, retomada, negativos e recuperacao.'
Write-Host "Veredito global permanece: $verdict"
Write-Host 'REBOOT NECESSARIO para recuperar o estado de firmware do boot.'
exit 0
