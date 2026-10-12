[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [switch]$Execute,
    [switch]$DeadlineEnforcer
)

$ErrorActionPreference = 'Stop'

# ==========================================================================
# Guardiao SYSTEM do safe-bind.
#
# Post-mortem do 3.79 (01/09/2026), que motivou esta revisao:
#   21:53:07  guardiao armado, OBSERVING
#   21:53:10  bc250mcdm instalado; PnP marca o dispositivo como STARTED
#   21:57:09  timeout de 240 s sem confirmacao; Codigo 43 observado
#   21:57:09  BC250_RECOVERY.cmd remove pacote, servico, SYS e o no PnP,
#             mas declara fracasso apos esperas fixas de 3 s e 5 s
#   21:57:18  shutdown.exe /r disparado por SYSTEM (evento 1074)
#   21:57:48  Kernel-Power 41: o desligamento nao terminou limpo
#   21:57:59  no boot seguinte o guardiao gravou NO_ACTION, "ja estava
#             restaurado no timeout", SOBRESCREVENDO o estado anterior
#
# Tres defeitos, corrigidos aqui:
#   1. NO_ACTION era ambiguo. Dizia ao mesmo tempo "nada estava errado" e "o
#      reboot da recovery deu certo". Virou NO_ROLLBACK_REQUIRED e
#      POST_REBOOT_VERIFIED, que sao fatos diferentes.
#   2. O state.json era sobrescrito a cada transicao, e a ultima escrita
#      apagava a historia. Agora existe um journal append-only: a evidencia
#      nao se autodestroi.
#   3. Uma leitura unica de PnP decidia o rollback. Agora sao cinco amostras,
#      e todas precisam concordar (fail-closed).
#
# Contrato externo preservado de proposito:
#   - o estado textual OBSERVING e' lido por bind-bc250mcdm-lab.ps1 e por
#     bind-bc250mcdm-renderonly-safe.ps1; nao renomear;
#   - os campos de armed.json so' podem ser acrescentados, nunca removidos.
# ==========================================================================

function Write-AtomicJson([string]$Path, [object]$Value) {
    $temporary = "$Path.$([guid]::NewGuid().ToString('N')).tmp"
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw 'Safe-bind watchdog recusado: configuracao ausente.'
}
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
foreach ($name in @('schema_version','task_name','token','instance_id','timeout_seconds',
        'deadline_task_name','evidence_dir','recovery_path','recovery_sha256',
        'armed_boot_ticks','armed_at_utc_ticks','deadline_utc_ticks',
        'automatic_recovery_reboot')) {
    if ($null -eq $config.PSObject.Properties[$name]) {
        throw "Safe-bind watchdog recusado: campo ausente: $name"
    }
}
# Corrida 269 (doc 710): o binder so' grava short_watchdog=1 com
# BC250_ALLOW_SHORT_WATCHDOG=1; so' entao o piso cai de 120 para 60 s.
$minTimeout = if ($null -ne $config.PSObject.Properties['short_watchdog'] -and [int]$config.short_watchdog -eq 1) { 60 } else { 120 }
if ([uint32]$config.schema_version -ne 1 -or
    [string]$config.instance_id -notmatch '(?i)^PCI\\VEN_1002&DEV_13FE(?:&|\\)' -or
    [int]$config.timeout_seconds -lt $minTimeout -or [int]$config.timeout_seconds -gt 600) {
    throw 'Safe-bind watchdog recusado: contrato invalido.'
}
$armedAtUtc = [datetime]::new([long]$config.armed_at_utc_ticks, [DateTimeKind]::Utc)
$absoluteDeadlineUtc = [datetime]::new(
    [long]$config.deadline_utc_ticks, [DateTimeKind]::Utc)
if ($absoluteDeadlineUtc -le $armedAtUtc -or
    [math]::Abs(($absoluteDeadlineUtc - $armedAtUtc).TotalSeconds -
        [double]$config.timeout_seconds) -gt 2.0) {
    throw 'Safe-bind watchdog recusado: prazo absoluto incoerente.'
}
$parsedToken = [guid]::Empty
if (-not [guid]::TryParseExact([string]$config.token, 'D', [ref]$parsedToken)) {
    throw 'Safe-bind watchdog recusado: token invalido.'
}

$statePath = Join-Path ([string]$config.evidence_dir) 'safe-bind-watchdog-state.json'
$journalPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-watchdog-journal.jsonl'
$finalPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-final.json'
$confirmationPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-confirmation.json'
$attemptPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-rollback-attempt.json'
$samplesPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-pnp-samples.json'
# DX.61 (Corrida 104): o arquivo acima e' sobrescrito a cada veredito, e a
# tentativa 2 apagou as amostras que reprovaram a tentativa 1. Este jsonl e'
# append-only, com tentativa, rodada e boot em cada amostra.
$samplesJournalPath = Join-Path ([string]$config.evidence_dir) 'safe-bind-pnp-samples.jsonl'
$script:verdictRound = 0

# Semeado a partir do state.json que sobreviveu ao boot anterior, quando houver.
$script:previousState = 'NONE'
if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    try {
        $script:previousState =
            [string](Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json).state
    } catch { $script:previousState = 'UNREADABLE' }
}
$script:attemptNumber = 1
$script:recoveryExitCode = $null
$script:shutdownExitCode = $null
$script:lastSamples = @()
$script:currentBootTicks = [long]$config.armed_boot_ticks

function Write-State([string]$State, [string]$Detail, [int]$ExitCode = 0,
    [switch]$JournalOnly) {
    $record = [ordered]@{
        state = $State
        previous_state = $script:previousState
        attempt_number = $script:attemptNumber
        detail = $Detail
        timestamp = (Get-Date).ToString('o')
        boot_ticks = $script:currentBootTicks
        exit_code = $ExitCode
        hardware_validated = $false
    }
    # -JournalOnly: uma segunda instancia que sai sem agir nao pode sobrescrever
    # o state.json da instancia que esta executando a recovery.
    if (-not $JournalOnly) { Write-AtomicJson $statePath $record }
    # Append-only: e' o unico artefato que nenhuma transicao posterior apaga.
    try {
        $line = ($record | ConvertTo-Json -Depth 8 -Compress)
        [IO.File]::AppendAllText(
            $journalPath, $line + [Environment]::NewLine,
            (New-Object Text.UTF8Encoding($false)))
    } catch { }
    $script:previousState = $State
}

# --------------------------------------------------------------------------
# Veredito de PnP por AMOSTRAGEM.
#
# Uma leitura unica pega o dispositivo no meio de uma transicao e decide
# errado. Sao cinco amostras com 1 s de intervalo, e "saudavel" exige que
# TODAS concordem: fail-closed, porque o custo de um rollback desnecessario e'
# muito menor que o de deixar um adaptador quebrado ligado.
# --------------------------------------------------------------------------
function Get-Bc250PnpVerdict {
    $samples = @()
    $script:verdictRound++
    for ($i = 1; $i -le 5; $i++) {
        $sampleError = $null
        try {
            $devices = @(Get-CimInstance Win32_PnPEntity -OperationTimeoutSec 2 `
                -ErrorAction Stop | Where-Object {
                    $_.PNPDeviceID -eq [string]$config.instance_id
                })
        } catch {
            $devices = @()
            $sampleError = $_.Exception.Message
        }
        # device_count, e nao "count": num [ordered]@{} o nome count colide
        # com a propriedade Count do proprio dicionario. Medido no PS 5.1
        # desta maquina, o adaptador prioriza a chave e devolve o valor certo
        # — mas a leitura fica ambigua para quem revisa, e o desempate depende
        # do adaptador. O nome explicito remove a duvida.
        $sample = [ordered]@{
            index = $i
            at = (Get-Date).ToString('o')
            device_count = $devices.Count
            service = $(if ($devices.Count -eq 1) { [string]$devices[0].Service } else { $null })
            code = $(if ($devices.Count -eq 1) { [uint32]$devices[0].ConfigManagerErrorCode } else { $null })
            status = $(if ($devices.Count -eq 1) { [string]$devices[0].Status } else { $null })
            error = $sampleError
        }
        $samples += $sample
        try {
            $journalSample = [ordered]@{
                attempt_number = $script:attemptNumber
                round = $script:verdictRound
                boot_ticks = $script:currentBootTicks
            }
            foreach ($key in $sample.Keys) { $journalSample[$key] = $sample[$key] }
            [IO.File]::AppendAllText($samplesJournalPath,
                ($journalSample | ConvertTo-Json -Compress) + [Environment]::NewLine,
                (New-Object Text.UTF8Encoding($false)))
        } catch { }
        if ($i -lt 5) { Start-Sleep -Seconds 1 }
    }
    $healthy = ($samples.Count -eq 5)
    foreach ($s in $samples) {
        if ($s.device_count -ne 1 -or $s.service -ne 'BasicDisplay' -or $s.code -ne 0) {
            $healthy = $false
        }
    }
    $script:lastSamples = $samples
    try { Write-AtomicJson $samplesPath $samples } catch { }
    return [pscustomobject]@{ healthy = $healthy; samples = $samples }
}

# --------------------------------------------------------------------------
# A4 — arquivamento e limpeza do diretorio do guardiao.
#
# Chamar SEMPRE depois do Unregister-ScheduledTask. Na ordem inversa, a tarefa
# AtStartup fica apontando para um script que nao existe mais, e o proximo
# boot dispara um powershell que morre sem gravar estado nenhum.
#
# Falha de remocao nunca derruba o guardiao: e' registrada em cleanup_ok.
# --------------------------------------------------------------------------
function Invoke-Bc250SafeGuardCleanup {
    $guardDir = 'C:\BC250_SAFE_GUARD'
    try {
        $archive = Join-Path ([string]$config.evidence_dir) 'safe-guard-archive'
        if (-not (Test-Path -LiteralPath $archive)) {
            New-Item -ItemType Directory -Path $archive -Force | Out-Null
        }
        foreach ($name in @('armed.json','watch-bc250mcdm-safe-bind.ps1','restore-dwm-crash-capture.ps1')) {
            $source = Join-Path $guardDir $name
            if (Test-Path -LiteralPath $source -PathType Leaf) {
                Copy-Item -LiteralPath $source -Destination $archive -Force
            }
        }
        Remove-Item -LiteralPath $guardDir -Recurse -Force -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

function Restore-DwmCaptureIfArmed {
    if ($null -eq $config.PSObject.Properties['dwm_capture_enabled'] -or
        -not [bool]$config.dwm_capture_enabled) {
        return $true
    }
    $restore = [string]$config.dwm_restore_script
    if (-not (Test-Path -LiteralPath $restore -PathType Leaf) -or
        (Get-FileHash -LiteralPath $restore -Algorithm SHA256).Hash -ne
            [string]$config.dwm_restore_sha256) {
        try { Write-State 'DWM_CAPTURE_RESTORE_FAILED' 'script de restore ausente ou alterado' 13 } catch { }
        return $false
    }
    try {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $restore `
            -EvidenceDir ([string]$config.evidence_dir) -Execute | Out-Null
        return ($LASTEXITCODE -eq 0)
    } catch {
        try { Write-State 'DWM_CAPTURE_RESTORE_FAILED' $_.Exception.Message 13 } catch { }
        return $false
    }
}

# Arquivar a cauda real da UMD depois da recovery. O binder copia uma amostra
# intermediaria e pode nao voltar de um travamento duro. Esta funcao so' roda
# DEPOIS de executar BC250_RECOVERY.cmd; falha de coleta nunca impede rollback.
function Archive-Bc250UmdLogsAfterRecovery(
    [string]$SourceDirectory = 'C:\Windows\Temp') {
    $destination = Join-Path ([string]$config.evidence_dir) 'umd-logs-pos-travamento'
    $manifestPath = Join-Path ([string]$config.evidence_dir) 'umd-post-recovery-manifest.json'
    $entries = @()
    $errors = @()
    $aclResetOk = $false
    try {
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        $logs = @(Get-ChildItem -LiteralPath $SourceDirectory `
            -Filter 'BC250_UMD_LOG_*.txt' -File -ErrorAction Stop | Sort-Object Name)
        foreach ($log in $logs) {
            try {
                $copyPath = Join-Path $destination $log.Name
                Copy-Item -LiteralPath $log.FullName -Destination $copyPath -Force -ErrorAction Stop
                $entries += [ordered]@{
                    name = $log.Name
                    source_last_write_utc = $log.LastWriteTimeUtc.ToString('o')
                    copied_bytes = (Get-Item -LiteralPath $copyPath).Length
                    sha256 = (Get-FileHash -LiteralPath $copyPath -Algorithm SHA256).Hash
                }
            } catch {
                $errors += "$($log.Name): $($_.Exception.Message)"
            }
        }
        # O guardiao roda como SYSTEM. Herdar a ACL da pasta de evidencia
        # permite que a sessao de analise sem elevacao leia as copias.
        & icacls.exe $destination /reset /T /Q | Out-Null
        $aclResetOk = ($LASTEXITCODE -eq 0)
        if (-not $aclResetOk) { $errors += 'icacls /reset falhou para as copias UMD' }
    } catch {
        $errors += $_.Exception.Message
    }
    try {
        Write-AtomicJson $manifestPath ([ordered]@{
            kind = 'POST_RECOVERY_UMD_TAIL'
            captured_at = (Get-Date).ToString('o')
            armed_boot_ticks = [long]$config.armed_boot_ticks
            recovery_boot_ticks = [long]$script:currentBootTicks
            source = (Join-Path $SourceDirectory 'BC250_UMD_LOG_*.txt')
            files = @($entries)
            acl_reset_ok = $aclResetOk
            errors = @($errors)
        })
    } catch { }
}

# --------------------------------------------------------------------------
# DX.34 -- MARCADOR DE BOOT FRIO GRAVADO PELO PROPRIO WATCHDOG.
#
# Corrida 79 (P0): a 78 terminou em RECOVERY_OK_ONLINE no boot seguinte ao
# reboot QUENTE que este watchdog pediu, e nenhum caminho aqui gravava
# C:\BC250_REQUIRE_CLEAN_BOOT.json. O marcador que existia era o do binder, com
# os ticks do boot ANTERIOR ao reboot; o binder da 79 viu ticks diferentes,
# concluiu "boot posterior confirmado" e ligou o driver no MESMO boot do
# rollback. Agora todo rollback (inicio e fim) grava o marcador com os ticks do
# boot CORRENTE, no formato de rollback-bc250mcdm-lab.ps1. Falha de escrita nao
# impede a recovery: seguranca vem antes de evidencia.
# --------------------------------------------------------------------------
$cleanBootMarkerPath = 'C:\BC250_REQUIRE_CLEAN_BOOT.json'
function Write-Bc250CleanBootMarker([string]$Why) {
    try {
        Write-AtomicJson $cleanBootMarkerPath ([ordered]@{
            state = 'CLEAN_BOOT_REQUIRED'
            rollback_boot_ticks = [long]$script:currentBootTicks
            rollback_completed_at = (Get-Date).ToString('o')
            restored_service = 'BasicDisplay'
            restored_code = 0
            written_by = "watch-bc250mcdm-safe-bind.ps1 ($Why)"
            hardware_validated = $false
        })
    } catch { }
}

function Complete-Bc250SafeBind([string]$FinalState, [string]$Detail, [int]$ExitCode = 0) {
    # Capturado ANTES do Write-State: aquele avanca $script:previousState para
    # o estado final, e o final.json passaria a registrar previous_state igual
    # a final_state -- perdendo justamente a transicao que interessa ler.
    $stateBefore = $script:previousState
    $dwmRestoreOk = Restore-DwmCaptureIfArmed
    Write-State $FinalState $Detail $ExitCode
    Unregister-ScheduledTask -TaskName ([string]$config.task_name) `
        -Confirm:$false -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName ([string]$config.deadline_task_name) `
        -Confirm:$false -ErrorAction SilentlyContinue
    $cleanupOk = Invoke-Bc250SafeGuardCleanup
    Write-AtomicJson $finalPath ([ordered]@{
        final_state = $FinalState
        previous_state = $stateBefore
        detail = $Detail
        attempt_number = $script:attemptNumber
        timestamp = (Get-Date).ToString('o')
        boot_ticks = $script:currentBootTicks
        armed_boot_ticks = [long]$config.armed_boot_ticks
        recovery_exit_code = $script:recoveryExitCode
        shutdown_exit_code = $script:shutdownExitCode
        pnp_samples = $script:lastSamples
        journal_path = $journalPath
        cleanup_ok = $cleanupOk
        dwm_capture_restore_ok = $dwmRestoreOk
        hardware_validated = $false
    })
}

if (-not $Execute) {
    Write-Host "SAFE-BIND WATCHDOG DRY-RUN: timeout=$($config.timeout_seconds)s"
    exit 0
}
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Safe-bind watchdog exige Administrador/SYSTEM.'
}
if (-not (Test-Path -LiteralPath ([string]$config.recovery_path) -PathType Leaf) -or
    (Get-FileHash -LiteralPath ([string]$config.recovery_path) -Algorithm SHA256).Hash -ne
        [string]$config.recovery_sha256) {
    # Nao limpa nem desregistra: sem recovery integra o guardiao nao pode
    # prometer rollback, e o operador precisa encontrar o estado como esta'.
    Write-State 'BLOCKED' 'recovery ausente ou alterada' 12
    exit 12
}
$currentBootTicks = [long](Get-CimInstance Win32_OperatingSystem `
    -OperationTimeoutSec 2 -ErrorAction Stop).LastBootUpTime.ToUniversalTime().Ticks
$script:currentBootTicks = $currentBootTicks
$retryAfterBoot = $false
if (Test-Path -LiteralPath $attemptPath) {
    $previousAttempt = Get-Content -LiteralPath $attemptPath -Raw | ConvertFrom-Json
    if ([long]$previousAttempt.boot_ticks -eq $currentBootTicks -or
        [uint32]$previousAttempt.attempt_number -ge 2) {
        # Corrida 104: a tarefa de prazo chegou aqui 0,24 s depois da tarefa
        # principal publicar ROLLBACK_STARTED e sobrescreveu o state.json com
        # BLOCKED enquanto a recovery rodava. So' o journal registra a saida.
        Write-State 'BLOCKED' 'rollback ja foi tentado neste boot ou atingiu o limite de 2 boots' 11 -JournalOnly
        exit 11
    }
    $script:attemptNumber = [uint32]$previousAttempt.attempt_number + 1
    $retryAfterBoot = $true
}

if (-not $DeadlineEnforcer) {
    Write-State 'OBSERVING' $(if ($retryAfterBoot) {
        'boot posterior ao rollback incompleto; prazo absoluto preservado'
    } else {
        'guardiao ativo antes do bind; aguardando confirmacao explicita'
    })
    do {
        if (Test-Path -LiteralPath $confirmationPath -PathType Leaf) {
            try {
                $confirmation = Get-Content -LiteralPath $confirmationPath -Raw | ConvertFrom-Json
                if ([string]$confirmation.token -eq [string]$config.token -and
                    [string]$confirmation.instance_id -eq [string]$config.instance_id -and
                    [long]$confirmation.boot_ticks -eq $currentBootTicks -and
                    [bool]$confirmation.display_visible) {
                    Complete-Bc250SafeBind 'DISARMED' `
                        'confirmacao visual e estado PnP estavel aceitos'
                    exit 0
                }
            } catch { }
        }
        Start-Sleep -Milliseconds 250
    } until ([datetime]::UtcNow -ge $absoluteDeadlineUtc)
} elseif ([datetime]::UtcNow -lt $absoluteDeadlineUtc) {
    # A tarefa de prazo pode ser disparada adiantada por ajuste de relogio.
    # Nunca recupera antes do instante absoluto declarado.
    do { Start-Sleep -Milliseconds 250 } until (
        [datetime]::UtcNow -ge $absoluteDeadlineUtc)
}

# Duas tarefas independentes chegam aqui. Uma trava global torna o rollback
# single-owner; a outra sai sem tocar no dispositivo.
$recoveryMutex = New-Object Threading.Mutex($false, 'Global\BC250SafeBindRecovery')
$ownsRecovery = $false
try {
    $ownsRecovery = $recoveryMutex.WaitOne(0)
} catch [Threading.AbandonedMutexException] {
    # Se o processo morreu/rebootou segurando o mutex, WaitOne transfere a
    # posse ao chamador E lanca esta excecao. Tratar como "nao adquiriu"
    # repetiria exatamente a falha que este segundo boot precisa recuperar.
    $ownsRecovery = $true
} catch {
    $ownsRecovery = $false
}
if (-not $ownsRecovery) { exit 0 }
if (Test-Path -LiteralPath $finalPath -PathType Leaf) {
    $recoveryMutex.ReleaseMutex(); $recoveryMutex.Dispose(); exit 0
}

# Fecha a corrida entre a ultima sondagem do observador e a aquisicao do
# mutex. Depois do prazo esta leitura e' SOMENTE de arquivo: nenhuma chamada
# a WMI/PnP pode impedir a recovery. O confirmador ja validou Codigo 0, hash,
# boot e 60 s de estabilidade antes de publicar este registro atomico.
if (Test-Path -LiteralPath $confirmationPath -PathType Leaf) {
    try {
        $confirmation = Get-Content -LiteralPath $confirmationPath -Raw | ConvertFrom-Json
        if ([string]$confirmation.token -eq [string]$config.token -and
            [string]$confirmation.instance_id -eq [string]$config.instance_id -and
            [long]$confirmation.boot_ticks -eq $currentBootTicks -and
            [bool]$confirmation.display_visible) {
            Complete-Bc250SafeBind 'DISARMED' `
                'confirmacao atomica aceita na barreira final antes do rollback'
            $recoveryMutex.ReleaseMutex(); $recoveryMutex.Dispose()
            exit 0
        }
    } catch { }
}

# AUSENCIA DE CONFIRMACAO EXIGE ROLLBACK, independentemente do estado PnP.
# A corrida 12:50 travou na consulta WMI que existia aqui, antes desta linha.
# Segurança vem antes de diagnóstico: nenhuma consulta WMI/evento antecede o
# recovery depois do prazo.
try {
    Write-State 'ROLLBACK_STARTED' `
        'prazo absoluto expirou sem confirmacao; recovery iniciada sem preconsulta WMI'
} catch {
    # Evidencia e' importante, mas indisponibilidade do disco nao tem direito
    # de impedir a acao de seguranca. A recovery continua fail-closed.
}
Write-Bc250CleanBootMarker 'ROLLBACK_STARTED'

# LocalDumps e' estado global do Windows: desarmar antes da recovery/reboot,
# mesmo se algum dump ainda estiver bloqueado. O script preserva dumps
# pendentes e restaura o registro independentemente da copia.
[void](Restore-DwmCaptureIfArmed)

# SNAPSHOT antes do rollback: preserva a causa mesmo quando a tela esta preta
# e o operador nao consegue executar o inspetor manualmente.
try {
    Write-AtomicJson $attemptPath ([ordered]@{
        attempted_at = (Get-Date).ToString('o')
        attempt_number = $script:attemptNumber
        boot_ticks = $currentBootTicks
        observed_service = '<nao consultado antes da recovery>'
        observed_code = $null
        pnp_samples = @()
        reason = 'ausencia de confirmacao explicita; qualquer Codigo, inclusive 39/43, exige rollback'
    })
} catch { }
# --------------------------------------------------------------------------
# Execucao blindada da recovery.
#
# ARMADILHA REAL do PowerShell 5.1: com $ErrorActionPreference = 'Stop', a
# forma "& nativo ... 2>&1" transforma CADA linha de stderr em um
# NativeCommandError TERMINANTE. Se a recovery escrever qualquer coisa em
# stderr, o script morre exatamente entre executar o rollback e pedir o
# reboot — o pior lugar possivel para morrer, porque a maquina fica com a
# tela apagada e ninguem pede o reinicio.
#
# Por isso: preferencia rebaixada para Continue durante a chamada, try/finally
# para restaurar, e sentinela 1000 para "nem chegou a rodar", distinguivel dos
# codigos reais do .cmd (0, 1, 2, 5).
# --------------------------------------------------------------------------
$previousEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$exitCode = 1000
$output = ''
try {
    $output = (& cmd.exe /d /c ([string]$config.recovery_path) 2>&1 |
        ForEach-Object { [string]$_ }) -join "`n"
    if ($null -ne $LASTEXITCODE) { $exitCode = [int]$LASTEXITCODE }
} catch {
    $output = "EXCECAO AO EXECUTAR RECOVERY: $($_.Exception.Message)"
} finally {
    $ErrorActionPreference = $previousEap
}
$script:recoveryExitCode = $exitCode
$output | Set-Content -LiteralPath (Join-Path ([string]$config.evidence_dir) 'safe-bind-recovery.txt') -Encoding UTF8
# DX.61: a tentativa 2 sobrescreveu safe-bind-recovery.txt e C:\BC250_RECOVERY.log
# da tentativa 1. Copias por tentativa nunca sao reescritas.
try {
    $output | Set-Content -LiteralPath (Join-Path ([string]$config.evidence_dir) `
        ("safe-bind-recovery-attempt{0}.txt" -f $script:attemptNumber)) -Encoding UTF8
    Copy-Item -LiteralPath 'C:\BC250_RECOVERY.log' -Destination (Join-Path `
        ([string]$config.evidence_dir) ("bc250-recovery-log-attempt{0}.txt" -f $script:attemptNumber)) `
        -ErrorAction Stop
} catch { }
$recoveryFinishedUtc = [datetime]::UtcNow
Archive-Bc250UmdLogsAfterRecovery

if ($exitCode -eq 0) {
    # A recovery diz que fechou. Confirmar por amostragem propria antes de
    # acreditar: exit 0 e' afirmacao do script, nao medicao do PnP.
    #
    # DX.61 (Corrida 104): Kernel-PnP 410 registrou BasicDisplay iniciado as
    # 15:38:30 e o veredito unico reprovou ate 15:38:35,97 -- o dispositivo ja
    # estava no driver certo e o guardiao reiniciou a maquina. Janela de
    # assentamento: novas rodadas ate 45 s depois da recovery. O criterio NAO
    # afrouxa -- uma rodada inteira de 5 amostras consecutivas em
    # BasicDisplay/Codigo 0; amostra ruim anula a rodada inteira.
    $settleDeadlineUtc = $recoveryFinishedUtc.AddSeconds(45)
    do {
        $afterVerdict = Get-Bc250PnpVerdict
        if ($afterVerdict.healthy) { break }
        Start-Sleep -Seconds 2
    } while ([datetime]::UtcNow -lt $settleDeadlineUtc)
    if ($afterVerdict.healthy) {
        Write-Bc250CleanBootMarker 'RECOVERY_OK_ONLINE'
        Complete-Bc250SafeBind 'RECOVERY_OK_ONLINE' `
            ("recovery fechou sem reboot e BasicDisplay Codigo 0 foi confirmado em 5 amostras " +
             "consecutivas (rodada $($script:verdictRound))")
        $recoveryMutex.ReleaseMutex(); $recoveryMutex.Dispose()
        exit 0
    }
    Write-State 'ROLLED_BACK' `
        ("recovery devolveu 0 mas o PnP nao confirmou BasicDisplay em $($script:verdictRound) " +
         'rodadas / 45 s; escalando para reboot') 0
}

if ([bool]$config.automatic_recovery_reboot -and $script:attemptNumber -eq 1) {
    Write-State 'RECOVERY_REBOOT_REQUESTED' `
        'recovery online incompleta; reboot automatico em 10 s' $exitCode
    # Sem 2>&1 aqui, pela mesma armadilha documentada acima.
    & shutdown.exe /r /t 10 /f /d p:2:4 /c "BC250 safe-bind recovery"
    $script:shutdownExitCode = $LASTEXITCODE
    if ($script:shutdownExitCode -ne 0) {
        Write-State 'RECOVERY_REBOOT_REQUESTED' `
            ("shutdown.exe falhou (exit=$($script:shutdownExitCode)); escalando para Restart-Computer") `
            $exitCode
        try {
            Restart-Computer -Force
        } catch {
            Write-State 'ROLLBACK_FAILED' `
                ("shutdown.exe e Restart-Computer falharam: $($_.Exception.Message); " +
                 'reinicie manualmente e execute C:\BC250_RECOVERY.cmd') $exitCode
        }
    }
    # NAO desregistrar nem limpar: o proximo boot precisa de armed.json e do
    # script para confirmar POST_REBOOT_VERIFIED.
    exit $exitCode
}

Write-State 'ROLLBACK_FAILED' 'recovery falhou apos dois boots; execute C:\BC250_RECOVERY.cmd' $exitCode
$recoveryMutex.ReleaseMutex(); $recoveryMutex.Dispose()
exit $exitCode
