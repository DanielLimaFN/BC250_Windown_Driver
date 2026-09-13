<#
.SYNOPSIS
    Gate fisico transacional RADV -> WDDM -> BC-250, na TRILHA DE LABORATORIO.

.DESCRIPTION
    Executa exatamente uma vez um shader Vulkan que troca o poison 0x43DAFFFE
    por 0xBC250C01. A corrida so e permitida dentro de uma janela de binding
    protegida pelo watchdog, com identidade, Codigo 0 e hashes conferidos.

    POR QUE A TRILHA E A DE LABORATORIO, E NAO A ATIVA:

    Ate o 3.81 este runner apontava para driver\wdk\package\output_active.
    Isso nunca poderia passar. O pacote ativo e compilado SEM
    BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP (proibido por
    tools\audit-bc250mcdm-safety.py) e grava SkipHwBringup=1, entao o
    StartDevice retorna cedo e o motor P1 nunca sobe: P1EngineState fica 0 e o
    preflight aborta sempre. Medido na corrida
    docs\evidence\2026-09-05_22-20-17-*: SkipHwBringup=1,
    V5UnsafeBringupCompiled=0, P1EngineState=0, GpuvaGateBlockMask=65
    (JOURNAL_PENDING | ENGINE_OFFLINE).

    A TRILHA TEM DOIS PASSOS ARMADOS, e este runner e apenas o terceiro tempo:

      1. pacote  : tools\build-bc250mcdm-lab-bringup.ps1 -Execute
                   -IUnderstandThisEnablesDestructiveBringup
                   tools\package-bc250mcdm-lab.ps1 -EnableRenderNode
                   -EnableShaderDispatch -EnablePersistentSubmission
                   -> DISPLAY_RENDER_LAB, SkipHwBringup=0, RenderNode=1,
                      GpuvaLabPromotion=0 gravado com NOCLOBBER (default, nao
                      mandato)
      2. bind    : tools\bind-bc250mcdm-lab.ps1 -EnableRenderNode
                   -EnableShaderDispatch -EnablePersistentSubmission
                   -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion
                   -Execute -IAcceptBlackScreenRisk, com watchdog ARMED
                   -> a politica pre-bind arma GpuvaLabPromotion=1 e o NOCLOBBER
                      do INF a preserva atraves do install, para o StartDevice
                      unico que a le (bc250_active_miniport.c:3443). Sem ela,
                      o submit no node 1 devolve STATUS_NOT_SUPPORTED
                      (bc250_active_miniport.c:6273)

    tools\test-gpuva-lab-promotion.ps1 continua servindo para -Action Inspect e
    -Action Rollback fora da janela de bind.

    O veredito exige simultaneamente:
      - prova byte a byte do buffer de resultado;
      - submissao/retirada vistas pelo motor P1;
      - WPTR MMIO == RPTR MMIO;
      - fence fisica observada e retirada;
      - gate GPUVA operacional ao fim da corrida;
      - push e notificacao V2 sem falhas, overflow ou fora de ordem.

    Compilar, carregar o ICD ou enumerar Vulkan nao constitui prova.
#>
[CmdletBinding()]
param(
    [string]$EvidenceDir,
    [string]$IcdManifest,
    [string]$Harness,
    [string]$Shader,
    [string]$MesaRoot = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm',
    [ValidateRange(10,60)][int]$TimeoutSeconds = 20,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$manifestPath = Join-Path $root 'driver\wdk\package\output_lab.manifest.json'
$kmdAudit = Join-Path $root 'tools\audit-bc250mcdm-safety.py'
$radvAudit = Join-Path $root 'tools\audit-radv-wddm-safety.py'
$driverPath = Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys'
$poison = [uint32]0x43DAFFFE

if ([string]::IsNullOrWhiteSpace($IcdManifest)) {
    $IcdManifest = Join-Path $root 'tools\bc250-radv-test-icd.json'
}
if ([string]::IsNullOrWhiteSpace($Harness)) {
    $Harness = Join-Path $root 'driver\tests\output_radv_gate\bc250-radv-compute-gate.exe'
}
if ([string]::IsNullOrWhiteSpace($Shader)) {
    $Shader = Join-Path $root 'driver\tests\output_radv_gate\bc250_radv_compute_gate.spv'
}

$watched = @(
    'P1EngineState',
    'P1SubmitCalls',
    'P1SubmitAccepted',
    'P1SubmitRejectedNode',
    'P1SubmitRejectedPlan',
    'P1SubmitRejectedEmit',
    'P1RetiredTotal',
    'P1RingFullCount',
    'P1FenceRegressionCount',
    'P1LastWptrPublished',
    'P1LastWptrMmioReadback',
    'P1LastRptrMmio',
    'P1LastRptrReportMem',
    'P1LastFenceObserved',
    'P1LastSubmittedFence',
    'P1LastRetiredFence',
    'V2TrackerPushedTotal',
    'V2NotifiedTotal',
    'V2NotifyBatches',
    'V2FencesInFlight',
    'V2TrackerOverflow',
    'V2TrackerOutOfOrder',
    'V2PushFailures',
    'V2PollFailures',
    # Sem o gate GPUVA aberto, SubmitCommandVirtual no node 1 devolve
    # STATUS_NOT_SUPPORTED antes de tocar o anel. Observar o gate e a promocao
    # e o que separa "o shader nao rodou" de "o driver recusou a submissao".
    'GpuvaGateEligible',
    'GpuvaGateOperational',
    'GpuvaGateBlockMask',
    'GpuvaLabPromotionRequested',
    'GpuvaLabPromotionPresent'
)

function Get-U32Value($Raw) {
    if ($null -eq $Raw) { return $null }
    return ([int64]$Raw -band 0xFFFFFFFFL)
}

function Get-ProbeSnapshot {
    if (-not (Test-Path -LiteralPath $probePath)) {
        throw "Telemetria ausente: $probePath"
    }
    $properties = Get-ItemProperty -LiteralPath $probePath
    $snapshot = [ordered]@{}
    foreach ($name in $watched) {
        if ($null -eq $properties.PSObject.Properties[$name]) {
            throw "Telemetria obrigatoria ausente: $name"
        }
        $snapshot[$name] = Get-U32Value $properties.$name
    }
    return $snapshot
}

function Get-U32Delta([int64]$Before, [int64]$After) {
    if ($After -ge $Before) { return $After - $Before }
    return (0x100000000L - $Before) + $After
}

function Invoke-SafetyAudit([string]$FilePath, [string[]]$Arguments) {
    $output = (& py -3 $FilePath @Arguments 2>&1) -join "`r`n"
    if ($LASTEXITCODE -ne 0) {
        throw "Auditoria fail-closed falhou: $FilePath`r`n$output"
    }
    return $output
}

# Estado do fusivel persistente EXIGIDO por este gate: qualquer coisa menos
# TRIPPED.
#
# MEDIDO na corrida 06/09 12:08: o driver ARMA o fusivel no proprio StartDevice
# ("[FUSE] STARTDEVICE AUTORIZADO: Fusivel armado para ensaio") e so o marca
# CLEAN no desligamento limpo. Portanto, durante uma janela de bind viva, ARMED
# (1) e o estado CORRETO -- exigir CLEAN (0) era uma condicao impossivel, e foi
# ela que recusou a corrida depois de todo o bring-up ter passado.
#
# TRIPPED (2) continua sendo recusa: significa que um boot anterior caiu.
function Get-BootFuseState {
    $path = 'C:\BC250_BOOT_FUSE.dat'
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $raw = [System.IO.File]::ReadAllBytes($path)
    if ($raw.Length -ne 40) { throw 'Fusivel persistente possui formato inesperado.' }
    $magic = [BitConverter]::ToUInt32($raw, 0)
    if ($magic -ne 0x45535546) { throw 'Fusivel persistente possui magic inesperado.' }
    return [int][BitConverter]::ToUInt32($raw, 8)
}

function Test-BootFuseNotTripped {
    $state = Get-BootFuseState
    if ($null -eq $state) { return $true }
    if ($state -eq 2) {
        throw ('Fusivel persistente em TRIPPED (2): um boot anterior caiu. ' +
            'Investigue antes de gastar a corrida; reset com ' +
            'tools\inspect-boot-fuse.ps1 -Reset.')
    }
    if ($state -ne 0 -and $state -ne 1) {
        throw "Fusivel persistente em estado desconhecido (state=$state)."
    }
    return $true
}

function Get-FileEvidence([string]$Path) {
    return [ordered]@{
        path = [System.IO.Path]::GetFullPath($Path)
        size = (Get-Item -LiteralPath $Path).Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
}

function Invoke-IcdIdentityProbe([string]$HarnessPath, [string]$IcdPath) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = [System.IO.Path]::GetFullPath($HarnessPath)
    $psi.Arguments = '--identity-only "' + [System.IO.Path]::GetFullPath($IcdPath) + '"'
    $psi.WorkingDirectory = [System.IO.Path]::GetDirectoryName(
        [System.IO.Path]::GetFullPath($HarnessPath))
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        if (-not $proc.Start()) { throw 'Process.Start do identity probe retornou false.' }
        $stdout = $proc.StandardOutput.ReadToEnd()
        $stderr = $proc.StandardError.ReadToEnd()
        $proc.WaitForExit()
        return [pscustomobject]@{
            exit_code = [int]$proc.ExitCode
            stdout = [string]$stdout
            stderr = [string]$stderr
        }
    } finally {
        $proc.Dispose()
    }
}

function Save-RadvHangDump([int]$ProcessId, [string]$RunDirectory,
    [string]$MesaSymbols) {
    $cdbCandidates = @(
        'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe',
        'C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe'
    )
    $cdb = $null
    foreach ($candidate in $cdbCandidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $cdb = $candidate
            break
        }
    }
    $captureStatePath = Join-Path $RunDirectory 'radv-harness-hang-capture-state.json'
    if ($null -eq $cdb) {
        [ordered]@{
            method = 'CDB_ATTACH_DUMP_MA'
            state = 'CDB_NOT_FOUND'
            process_id = $ProcessId
            captured_at = (Get-Date).ToString('o')
            success = $false
        } | ConvertTo-Json | Set-Content -LiteralPath $captureStatePath -Encoding UTF8
        return $false
    }

    $dumpPath = Join-Path $RunDirectory 'radv-harness-hang.dmp'
    Remove-Item -LiteralPath $dumpPath -Force -ErrorAction SilentlyContinue
    # Captura e' uma operacao com deadline: nao use symbol server aqui. Um
    # download pendente consumiu a janela inteira e deixou um arquivo de zero
    # bytes na corrida 20:34. A simbolizacao pode ser feita offline depois.
    $symbolPath = $MesaSymbols
    $commands = ".dump /ma `"$dumpPath`"; ~*k; q"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $cdb
    $psi.Arguments = "-p $ProcessId -y `"$symbolPath`" -c `"$commands`""
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $debugger = New-Object System.Diagnostics.Process
    $debugger.StartInfo = $psi
    $timedOut = $false
    $exitCode = $null
    try {
        if (-not $debugger.Start()) { return $false }
        $outTask = $debugger.StandardOutput.ReadToEndAsync()
        $errTask = $debugger.StandardError.ReadToEndAsync()
        if (-not $debugger.WaitForExit(30000)) {
            $timedOut = $true
            try { $debugger.Kill() } catch { }
            [void]$debugger.WaitForExit(3000)
        }
        if ($debugger.HasExited) {
            $exitCode = [int]$debugger.ExitCode
            $outTask.Result | Set-Content -LiteralPath `
                (Join-Path $RunDirectory 'radv-harness-hang-cdb.stdout.txt') -Encoding UTF8
            $errTask.Result | Set-Content -LiteralPath `
                (Join-Path $RunDirectory 'radv-harness-hang-cdb.stderr.txt') -Encoding UTF8
        } else {
            '<cdb permaneceu vivo apos timeout e Kill>' | Set-Content -LiteralPath `
                (Join-Path $RunDirectory 'radv-harness-hang-cdb.stdout.txt') -Encoding UTF8
            '<streams indisponiveis porque o cdb nao encerrou>' | Set-Content -LiteralPath `
                (Join-Path $RunDirectory 'radv-harness-hang-cdb.stderr.txt') -Encoding UTF8
        }
        $dumpBytes = if (Test-Path -LiteralPath $dumpPath -PathType Leaf) {
            [int64](Get-Item -LiteralPath $dumpPath).Length
        } else { [int64]0 }
        # Um full dump de processo e' muito maior que 1 MiB. O piso recusa
        # explicitamente arquivo criado mas vazio/truncado como o de 20:34.
        $success = (-not $timedOut) -and ($exitCode -eq 0) -and
            ($dumpBytes -ge 1MB)
        [ordered]@{
            method = 'CDB_ATTACH_DUMP_MA'
            state = $(if ($success) { 'CAPTURED' } elseif ($timedOut) { 'CDB_TIMEOUT' } `
                elseif ($dumpBytes -lt 1MB) { 'DUMP_TOO_SMALL' } else { 'CDB_FAILED' })
            process_id = $ProcessId
            cdb_path = $cdb
            symbols = $symbolPath
            cdb_exit_code = $exitCode
            timed_out = $timedOut
            dump_path = $dumpPath
            dump_bytes = $dumpBytes
            minimum_bytes = 1MB
            captured_at = (Get-Date).ToString('o')
            success = $success
        } | ConvertTo-Json | Set-Content -LiteralPath $captureStatePath -Encoding UTF8
        return $success
    } finally {
        $debugger.Dispose()
    }
}

foreach ($path in @($manifestPath,$kmdAudit,$radvAudit,$IcdManifest,$Harness,$Shader)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Preflight recusado: artefato ausente: $path"
    }
}
if (-not (Test-Path -LiteralPath $MesaRoot -PathType Container)) {
    throw "Preflight recusado: arvore Mesa ausente: $MesaRoot"
}

$icd = Get-Content -LiteralPath $IcdManifest -Raw | ConvertFrom-Json
$radvDll = [string]$icd.ICD.library_path
if ([string]::IsNullOrWhiteSpace($radvDll) -or
    -not (Test-Path -LiteralPath $radvDll -PathType Leaf)) {
    throw "Preflight recusado: DLL do ICD ausente: $radvDll"
}
if ([string]$icd.ICD.library_arch -ne '64') {
    throw 'Preflight recusado: ICD nao declara library_arch=64.'
}

$kmdAuditOutput = Invoke-SafetyAudit $kmdAudit @()
$radvAuditOutput = Invoke-SafetyAudit $radvAudit @('--mesa-root', $MesaRoot)
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

# Gates de pacote. Todos avaliados tambem em dry-run: apontar para o pacote
# errado tem de custar zero corrida fisica, e nao uma corrida abortada no
# preflight com o hardware ja vinculado.
if ([string]$manifest.BuildKind -ne 'MCDM_LAB_BRINGUP') {
    throw ("Preflight recusado: BuildKind='$($manifest.BuildKind)'. Este gate exige " +
        'o pacote MCDM_LAB_BRINGUP; o pacote ativo e compilado sem bring-up e ' +
        'jamais publica P1EngineState=1.')
}
if (-not [bool]$manifest.UnsafeStartDeviceBringup) {
    throw 'Preflight recusado: manifesto nao declara bring-up compilado no StartDevice.'
}
if ([uint32]$manifest.SkipHwBringup -ne 0) {
    throw "Preflight recusado: SkipHwBringup=$($manifest.SkipHwBringup); sem 0 o motor P1 nao sobe."
}
if (-not [bool]$manifest.PersistentSubmissionEnabled) {
    throw 'Preflight recusado: pacote sem fila persistente; nao ha node 1 para submeter.'
}
if ([uint32]$manifest.RenderNode -ne 1 -or [uint32]$manifest.RenderOnlyLab -ne 0 -or
    [string]$manifest.ProfileMode -ne 'DISPLAY_RENDER_LAB') {
    throw ('Preflight recusado: perfil do pacote e ' +
        "'$($manifest.ProfileMode)' ($($manifest.RenderNode)/$($manifest.RenderOnlyLab)); " +
        'o Vulkan exige DISPLAY_RENDER (RenderNode=1, RenderOnlyLab=0). ' +
        'RENDER_ONLY_LAB nao e fallback: uma recusa do dxgkrnl vira evidencia.')
}
if ([bool]$manifest.GpuvaLabPromotionEnabled) {
    throw ('Preflight recusado: o pacote nao pode habilitar GpuvaLabPromotion. ' +
        'A promocao e armada em passo proprio por tools\test-gpuva-lab-promotion.ps1.')
}

$artifacts = [ordered]@{
    harness = Get-FileEvidence $Harness
    shader = Get-FileEvidence $Shader
    icd_manifest = Get-FileEvidence $IcdManifest
    radv_dll = Get-FileEvidence $radvDll
}

# O binder roda elevado. O Vulkan Loader ignora VK_DRIVER_FILES e
# VK_ICD_FILENAMES em processos elevados, portanto a antiga estrategia podia
# registrar o hash do candidato e ainda carregar C:\Windows\System32\vulkan_radeon.dll.
# Medido no dump de 06/09 17:11. O harness agora nao importa vulkan-1.dll:
# carrega o ICD absoluto diretamente, confere GetModuleFileName e negocia a
# ABI loader/ICD antes de qualquer chamada que possa tocar a GPU.
$identityProbe = Invoke-IcdIdentityProbe $Harness $radvDll
$expectedIcdPath = [System.IO.Path]::GetFullPath($radvDll)
$loadedIcdPath = $null
foreach ($line in ($identityProbe.stdout -split "`r?`n")) {
    if ($line -match '^icd_module=(.+)$') { $loadedIcdPath = $Matches[1].Trim() }
}
if ($identityProbe.exit_code -ne 0 -or [string]::IsNullOrWhiteSpace($loadedIcdPath) -or
    -not [string]::Equals($loadedIcdPath, $expectedIcdPath,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw ("Preflight recusado: identidade do ICD explicito nao foi provada. " +
        "esperado='$expectedIcdPath' observado='$loadedIcdPath' " +
        "exit=$($identityProbe.exit_code) stderr='$($identityProbe.stderr.Trim())'")
}
$artifacts.icd_identity_probe = [ordered]@{
    mode = 'EXPLICIT_DIRECT_LOAD_NO_VULKAN_LOADER'
    requested_path = $expectedIcdPath
    loaded_path = $loadedIcdPath
    exit_code = $identityProbe.exit_code
    matched = $true
}

Write-Host '=== RADV_WDDM_COMPUTE_TRIPOD (trilha de laboratorio) ===' -ForegroundColor Magenta
Write-Host "  Harness : $Harness"
Write-Host "  Shader  : $Shader"
Write-Host "  ICD     : $IcdManifest"
Write-Host "  RADV    : $radvDll"
Write-Host "  Loader  : explicit direct load; identidade conferida" -ForegroundColor Green
Write-Host ("  Pacote  : {0} ({1}/{2}), SYS {3}" -f
    $manifest.ProfileMode, $manifest.RenderNode, $manifest.RenderOnlyLab,
    ([string]$manifest.PackageSysSha256).Substring(0,16))
Write-Host "  Timeout : $TimeoutSeconds segundos"
Write-Host '  Teto    : uma unica execucao fisica por build'
Write-Host '  Abandono: primeiro erro de preflight/contexto/alocacao/GPUVA/submit/timeout'
Write-Host '  Armado  : 1) pacote lab DISPLAY_RENDER_LAB  2) bind lab com -EnableGpuvaLabPromotion e watchdog'

if (-not $Execute) {
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDir)) {
        Write-Host "  Evidencia planejada: $EvidenceDir"
    }
    Write-Host 'DRY-RUN APROVADO: hashes e auditorias estaticas conferidos; GPU nao tocada.' -ForegroundColor Green
    Write-Host 'Para executar, passe -Execute e o EvidenceDir de um binding com watchdog ARMED.'
    Write-Host 'NAO VALIDADO EM HARDWARE'
    exit 0
}

if ([string]::IsNullOrWhiteSpace($EvidenceDir) -or
    -not (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
    throw '-Execute exige EvidenceDir existente da corrida de binding.'
}
$safeGuardAuditScript = Join-Path $root 'tools\assert-bc250-safe-bind-guard.ps1'
$safeGuardRaw = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    $safeGuardAuditScript -EvidenceDir $EvidenceDir `
    -RequiredRemainingSeconds ($TimeoutSeconds + 10) 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Corrida recusada: guardiao SYSTEM nao comprovado:`n$safeGuardRaw"
}
$watchdogState = $safeGuardRaw | ConvertFrom-Json
$watchdogPidValue = [int]$watchdogState.process_id
$watchdogRemaining = [double]$watchdogState.remaining_seconds

$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like 'PCI\VEN_1002&DEV_13FE*'
})
if ($devices.Count -ne 1 -or $devices[0].Service -ne 'bc250mcdm' -or
    $devices[0].ConfigManagerErrorCode -ne 0) {
    throw 'Corrida recusada: BC-250 exato nao esta em bc250mcdm, Codigo 0.'
}
if ([string]$watchdogState.instance_id -ne [string]$devices[0].PNPDeviceID) {
    throw 'Corrida recusada: watchdog pertence a outra instancia de dispositivo.'
}
[void](Test-BootFuseNotTripped)
$bootFuseState = Get-BootFuseState
if (-not (Test-Path -LiteralPath $driverPath -PathType Leaf)) {
    throw "Corrida recusada: SYS instalado ausente: $driverPath"
}
$installedSysHash = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash
if ($installedSysHash -ne [string]$manifest.PackageSysSha256) {
    throw "Corrida recusada: SYS instalado nao corresponde ao manifesto ativo ($installedSysHash)."
}

$before = Get-ProbeSnapshot
if ([int64]$before.P1EngineState -ne 1) {
    throw "Corrida recusada: P1EngineState=$($before.P1EngineState), esperado 1."
}
# A promocao GPUVA e lida uma unica vez, no StartDevice
# (bc250_active_miniport.c:3443). Se ela nao estiver armada, o submit no node 1
# devolve STATUS_NOT_SUPPORTED e a corrida morre antes de tocar o anel.
#
# A politica pre-bind de bind-bc250mcdm-lab.ps1 grava esse valor: 0 por padrao,
# 1 somente com -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion. Este gate
# recusa a corrida quando o bind nao foi armado, em vez de gasta-la para
# descobrir isso no primeiro submit.
if ([int64]$before.GpuvaLabPromotionPresent -ne 1 -or
    [int64]$before.GpuvaLabPromotionRequested -ne 1) {
    throw ('Corrida recusada: GpuvaLabPromotion nao esta armado ' +
        "(present=$($before.GpuvaLabPromotionPresent), " +
        "requested=$($before.GpuvaLabPromotionRequested)). Sem ele " +
        'GpuvaMappingsOperational fica 0 e SubmitCommandVirtual no node 1 ' +
        'devolve STATUS_NOT_SUPPORTED. Refaca o bind com ' +
        'tools\bind-bc250mcdm-lab.ps1 -EnableGpuvaLabPromotion ' +
        '-IAcceptGpuvaLabPromotion: a promocao precisa estar na chave ANTES do ' +
        'StartDevice que a le.')
}

$runStamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss-fff'
$runDir = Join-Path $EvidenceDir "radv-compute-tripod-$runStamp"
New-Item -ItemType Directory -Path $runDir | Out-Null
$kmdAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'kmd-safety-audit.txt') -Encoding UTF8
$radvAuditOutput | Set-Content -LiteralPath (Join-Path $runDir 'radv-safety-audit.txt') -Encoding UTF8
$before | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'probe-before.json') -Encoding UTF8

# Captura de crash do harness.
#
# Tres corridas (06/09 13:19, 13:41 e 13:54) morreram em 0xC0000005 por volta
# de 1,5 s, com tres DLLs diferentes da RADV e sempre o mesmo stderr de duas
# linhas. Nenhum fprintf sobreviveu ao crash, e cada hipotese custou uma
# corrida fisica. Com o dump, a pilha da falha vem junto do veredito.
#
# Best-effort deliberado: se armar falhar, a corrida continua sem dump. Uma
# ferramenta de diagnostico nunca pode ser o motivo de o ensaio nao acontecer.
$crashArm = Join-Path $root 'tools\arm-radv-harness-crash-capture.ps1'
$crashRestore = Join-Path $root 'tools\restore-radv-harness-crash-capture.ps1'
$crashToken = [guid]::NewGuid().ToString('D')
$crashArmed = $false
if ((Test-Path -LiteralPath $crashArm -PathType Leaf) -and
    (Test-Path -LiteralPath $crashRestore -PathType Leaf)) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $armOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $crashArm -EvidenceDir $runDir -Token $crashToken -Execute 2>&1 |
            Out-String)
        if ($LASTEXITCODE -eq 0) {
            $crashArmed = $true
            Write-Host '  Captura de crash do harness ARMADA.'
        } else {
            Write-Warning ("Captura de crash nao armada; a corrida segue sem dump.`r`n" + $armOutput)
        }
    } finally {
        $ErrorActionPreference = $previousEap
    }
}

$start = Get-Date
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = [System.IO.Path]::GetFullPath($Harness)
$startInfo.Arguments = ('"' + [System.IO.Path]::GetFullPath($Shader) + '" "' +
    [System.IO.Path]::GetFullPath($radvDll) + '"')
$startInfo.WorkingDirectory = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($Harness))
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$stageJournalPath = Join-Path $runDir 'harness-stage-journal.txt'
$startInfo.EnvironmentVariables['BC250_GATE_STAGE_FILE'] = $stageJournalPath
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo

$timedOut = $false
$exitCode = $null
$stdout = ''
$stderr = ''
try {
    if (-not $process.Start()) { throw 'Process.Start retornou false.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $timedOut = $true
        [ordered]@{
            state = 'TIMEOUT_OBSERVED_BEFORE_TERMINATE'
            process_id = $process.Id
            timeout_seconds = $TimeoutSeconds
            captured_at = (Get-Date).ToString('o')
            hardware_validated = $false
        } | ConvertTo-Json | Set-Content -LiteralPath `
            (Join-Path $runDir 'harness-timeout-state.json') -Encoding UTF8
        # LocalDumps cobre excecao, nao processo vivo preso em D3DKMT. Antes
        # de TerminateProcess, anexe o cdb por uma janela limitada e preserve
        # a pilha real de vkCreateDevice/Win32U. A corrida 19:53 parou dentro
        # de vkCreateDevice e foi terminada sem dump, deixando apenas o nome
        # amplo da chamada Vulkan.
        $mesaSymbols = Join-Path $MesaRoot 'build\src\amd\vulkan'
        $hangDumpCaptured = Save-RadvHangDump $process.Id $runDir $mesaSymbols
        [ordered]@{
            attempted = $true
            captured = $hangDumpCaptured
            process_id = $process.Id
            captured_at = (Get-Date).ToString('o')
            hardware_validated = $false
        } | ConvertTo-Json | Set-Content -LiteralPath `
            (Join-Path $runDir 'harness-hang-dump-state.json') -Encoding UTF8
        try { $process.Kill() } catch {
            $_.Exception.Message | Set-Content -LiteralPath `
                (Join-Path $runDir 'harness-kill-error.txt') -Encoding UTF8
        }
        # Um processo preso dentro de uma chamada do kernel pode nao terminar
        # mesmo depois de TerminateProcess. Nunca bloquear o coletor ate o
        # watchdog de 180 s destruir a evidencia que ja temos.
        [void]$process.WaitForExit(5000)
    }
    if ($process.HasExited) {
        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result
        if (-not $timedOut) { $exitCode = $process.ExitCode }
    } else {
        $stdout = '<processo permaneceu vivo apos timeout e tentativa de termino; consulte harness-stage-journal.txt>'
        $stderr = '<streams ainda abertos; coleta fail-closed sem espera indefinida>'
    }
} finally {
    $process.Dispose()
}
$elapsedMs = [math]::Round(((Get-Date) - $start).TotalMilliseconds)
$stdout | Set-Content -LiteralPath (Join-Path $runDir 'harness.stdout.txt') -Encoding UTF8
$stderr | Set-Content -LiteralPath (Join-Path $runDir 'harness.stderr.txt') -Encoding UTF8

# Desarmar e recolher SEMPRE que armamos, inclusive quando a corrida passou:
# a configuracao do WER e global e nao pode ficar presa em ARMED por causa
# desta ferramenta. O WER ainda pode estar escrevendo o dump, dai a espera
# curta antes da coleta.
$crashDumpsCollected = 0
if ($crashArmed) {
    Start-Sleep -Seconds 3
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $restoreOutput = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $crashRestore -EvidenceDir $runDir 2>&1 | Out-String)
        Write-Host ($restoreOutput.TrimEnd())
        $crashDumpsCollected =
            @(Get-ChildItem -LiteralPath $runDir -Filter 'radv-harness-crash-*.dmp' `
                -File -ErrorAction SilentlyContinue).Count
    } finally {
        $ErrorActionPreference = $previousEap
    }
}

$pollDeadline = (Get-Date).AddSeconds(5)
$after = Get-ProbeSnapshot
while ((Get-Date) -lt $pollDeadline) {
    if ([int64]$after.P1RetiredTotal -gt [int64]$before.P1RetiredTotal -and
        [int64]$after.V2NotifiedTotal -gt [int64]$before.V2NotifiedTotal) {
        break
    }
    Start-Sleep -Milliseconds 100
    $after = Get-ProbeSnapshot
}
$after | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runDir 'probe-after.json') -Encoding UTF8
$delta = [ordered]@{}
foreach ($name in $watched) {
    $delta[$name] = Get-U32Delta ([int64]$before[$name]) ([int64]$after[$name])
}

$checks = [ordered]@{
    process_completed = (-not $timedOut)
    loaded_icd_identity = ($stdout -split "`r?`n" | Where-Object {
        [string]::Equals($_.Trim(), "icd_module=$expectedIcdPath",
            [System.StringComparison]::OrdinalIgnoreCase)
    }).Count -eq 1
    process_exit_zero = (-not $timedOut -and $exitCode -eq 0)
    byte_proof = (-not $timedOut -and $exitCode -eq 0 -and
        $stdout -match '(?m)^BC250 RADV COMPUTE BYTE PROOF: PASS\s*$')
    submit_accepted = ([int64]$delta.P1SubmitAccepted -gt 0)
    retired = ([int64]$delta.P1RetiredTotal -gt 0)
    ring_drained = ([int64]$after.P1LastWptrMmioReadback -eq [int64]$after.P1LastRptrMmio)
    ring_moved = (
        [int64]$after.P1LastWptrMmioReadback -ne [int64]$before.P1LastWptrMmioReadback -or
        [int64]$delta.P1SubmitAccepted -gt 0)
    physical_fence_replaced_poison = (
        [int64]$after.P1LastFenceObserved -gt [int64]$before.P1LastFenceObserved -and
        [int64]$after.P1LastFenceObserved -eq [int64]$after.P1LastSubmittedFence -and
        [int64]$after.P1LastFenceObserved -eq [int64]$after.P1LastRetiredFence -and
        [int64]$delta.P1RetiredTotal -gt 0)
    gpuva_gate_operational = ([int64]$after.GpuvaGateOperational -eq 1)
    v2_pushed = ([int64]$delta.V2TrackerPushedTotal -gt 0)
    v2_notified = ([int64]$delta.V2NotifiedTotal -gt 0)
    v2_drained = ([int64]$after.V2FencesInFlight -eq 0)
    no_submit_reject = (
        [int64]$delta.P1SubmitRejectedNode -eq 0 -and
        [int64]$delta.P1SubmitRejectedPlan -eq 0 -and
        [int64]$delta.P1SubmitRejectedEmit -eq 0)
    no_tracker_fault = (
        [int64]$delta.V2TrackerOverflow -eq 0 -and
        [int64]$delta.V2TrackerOutOfOrder -eq 0 -and
        [int64]$delta.V2PushFailures -eq 0 -and
        [int64]$delta.V2PollFailures -eq 0)
}
$hardwareValidated = $true
foreach ($value in $checks.Values) {
    if (-not [bool]$value) { $hardwareValidated = $false; break }
}
$verdict = if ($hardwareValidated) {
    'RADV_WDDM_COMPUTE_TRIPOD PASSOU NO SILICIO'
} elseif ($timedOut) {
    'TIMEOUT FAIL-CLOSED - NAO VALIDADO EM HARDWARE'
} else {
    'TRIPE INCOMPLETO - NAO VALIDADO EM HARDWARE'
}

$report = [ordered]@{
    schema_version = 2
    test = 'RADV_WDDM_COMPUTE_TRIPOD'
    track = 'MCDM_LAB_BRINGUP'
    package_build_kind = [string]$manifest.BuildKind
    package_profile_mode = [string]$manifest.ProfileMode
    package_render_node = [uint32]$manifest.RenderNode
    package_render_only_lab = [uint32]$manifest.RenderOnlyLab
    package_skip_hw_bringup = [uint32]$manifest.SkipHwBringup
    gpuva_gate_block_mask_before = [int64]$before.GpuvaGateBlockMask
    gpuva_gate_block_mask_after = [int64]$after.GpuvaGateBlockMask
    boot_fuse_state = $bootFuseState
    crash_capture_armed = $crashArmed
    crash_dumps_collected = $crashDumpsCollected
    started = $start.ToString('o')
    elapsed_ms = $elapsedMs
    ceiling = 'one physical invocation per build'
    abandonment = 'first preflight/context/allocation/GPUVA/submit/timeout failure; do not repeat this build'
    device_instance_id = $devices[0].PNPDeviceID
    driver_service = $devices[0].Service
    driver_problem_code = $devices[0].ConfigManagerErrorCode
    package_sys_sha256 = [string]$manifest.PackageSysSha256
    installed_sys_sha256 = $installedSysHash
    artifacts = $artifacts
    watchdog = $watchdogState
    watchdog_pid = $watchdogPidValue
    watchdog_remaining_seconds_at_preflight = [math]::Round($watchdogRemaining, 1)
    timed_out = $timedOut
    hang_dump_captured = $(if ($timedOut) { [bool]$hangDumpCaptured } else { $false })
    process_exit_code = $exitCode
    before = $before
    after = $after
    delta = $delta
    checks = $checks
    expected_poison = ('0x{0:X8}' -f $poison)
    expected_result = '0xBC250C01'
    verdict = $verdict
    hardware_validated = $hardwareValidated
}
$reportPath = Join-Path $runDir 'radv-wddm-compute-tripod.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8

Write-Host ''
foreach ($name in $checks.Keys) {
    $color = if ($checks[$name]) { 'Green' } else { 'Red' }
    Write-Host ('  {0,-34} {1}' -f $name, $checks[$name]) -ForegroundColor $color
}
Write-Host "Veredito: $verdict" -ForegroundColor $(if ($hardwareValidated) { 'Green' } else { 'Red' })
Write-Host "Evidencia: $reportPath"
if ($crashDumpsCollected -gt 0) {
    Write-Host ("Dump(s) do harness: {0}. Analise com:" -f $crashDumpsCollected) -ForegroundColor Cyan
    Write-Host ("  powershell -ExecutionPolicy Bypass -File tools\analyze-radv-harness-dump.ps1 -EvidenceDir `"$runDir`"")
} elseif ($crashArmed -and -not $hardwareValidated) {
    Write-Warning 'Captura estava armada e nenhum dump apareceu: a saida nao foi por excecao nao tratada.'
}
if (-not $hardwareValidated) { Write-Host 'NAO VALIDADO EM HARDWARE' }

if ($hardwareValidated) { exit 0 }
exit 1
