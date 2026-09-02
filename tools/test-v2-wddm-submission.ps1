<#
.SYNOPSIS
    Fase V2/V1 — descobre se um UMD real faz o dxgkrnl chamar
    DxgkDdiSubmitCommandVirtual, e se a retirada de fence da V2 funciona.

.DESCRIPTION
    A P1 e a P2 provam o anel persistente, mas ambas chamam
    Bc250SubmissionEngineSubmit diretamente de dentro do motor: elas CONTORNAM
    Bc250McdmSubmitCommandVirtual, que e' onde o rastreador de fences WDDM da
    Fase V2 vive. Por isso os contadores V2* ficam zerados mesmo com a P2
    aprovada 16/16.

    Este ensaio fecha essa lacuna usando um UMD de verdade (llama.cpp sobre o
    RADV) como gerador de submissoes, e responde as duas perguntas abertas de
    uma vez:

      V1 - o RADV chega a submeter pelo WDDM?
      V2 - quando submete, a conclusao e' notificada ao dxgkrnl?

    O script captura a telemetria antes, roda o llama-bench, captura depois e
    grava o delta em docs\evidence. Ele NAO escreve no dispositivo: a unica
    coisa que toca a GPU e' o proprio llama-bench.

.PARAMETER Model
    Caminho do .gguf. Padrao: tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf.

.PARAMETER SkipBench
    So captura a telemetria, sem rodar o llama-bench. Util para conferir o
    estado antes de queimar uma corrida.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\test-v2-wddm-submission.ps1
#>
[CmdletBinding()]
param(
    [string]$Model = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\tools_ai\models\tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf',
    [string]$LlamaDir = 'C:\Users\Daniel\Desktop\bc250-win\bc250-win\tools_ai\llama-cpp',
    [int]$GpuLayers = 99,
    [string]$EvidenceDir,
    # Manifesto ICD a usar em vez do registrado no sistema. Permite exercitar
    # um build do Mesa direto da arvore, sem instalar nada em System32 e sem
    # privilegio de administrador.
    [string]$VulkanDriverFiles,
    [switch]$SkipBench
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'

# Campos que decidem o veredito. Todos observados, nenhum inferido.
$watched = @(
    'V2TrackerPushedTotal',
    'V2NotifiedTotal',
    'V2NotifyBatches',
    'V2RetirementDpcRuns',
    'V2FencesInFlight',
    'V2Backpressure',
    'V2PushFailures',
    'V2PollFailures',
    'V2TrackerOverflow',
    'V2TrackerOutOfOrder',
    'V2LastNotifiedFenceId',
    'V2InterruptsSeen',
    'P1EngineState',
    'P1SubmitCalls',
    'P1SubmitAccepted',
    'P1RetiredTotal',
    'P1LastFenceObserved',
    'P1SubmitRejectedNode',
    'P1SubmitRejectedPlan',
    'P1SubmitRejectedEmit'
)

function Get-ProbeSnapshot {
    if (-not (Test-Path -LiteralPath $probePath)) {
        throw "Telemetria ausente: $probePath. O driver de laboratorio esta vinculado?"
    }
    $p = Get-ItemProperty -LiteralPath $probePath
    $snap = [ordered]@{}
    foreach ($name in $watched) {
        $raw = $p.$name
        $snap[$name] = if ($null -eq $raw) { $null } else { [int64]$raw -band 0xFFFFFFFFL }
    }
    return $snap
}

# ---- preflight -----------------------------------------------------------

$device = Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.DeviceID -like 'PCI\VEN_1002&DEV_13FE*'
}
if ($null -eq $device -or $device.Service -ne 'bc250mcdm') {
    throw 'O BC-250 nao esta com o servico bc250mcdm. Vincule o driver de laboratorio primeiro.'
}
if ($device.ConfigManagerErrorCode -ne 0) {
    throw "Dispositivo com ConfigManagerErrorCode=$($device.ConfigManagerErrorCode)."
}

$bench = Join-Path $LlamaDir 'llama-bench.exe'
if (-not $SkipBench) {
    if (-not (Test-Path -LiteralPath $bench))  { throw "llama-bench.exe ausente: $bench" }
    if (-not (Test-Path -LiteralPath $Model))  { throw "Modelo ausente: $Model" }
}

if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $stamp = (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss')
    $EvidenceDir = Join-Path $root ("docs\evidence\{0}-{1}-v2-wddm-submission" -f $stamp, $env:COMPUTERNAME)
}
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null

Write-Host '=== FASE V2/V1 - SUBMISSAO WDDM POR UMD REAL ===' -ForegroundColor Magenta
Write-Host ("  Modelo    : {0}" -f $Model)
Write-Host ("  Camadas   : {0}" -f $GpuLayers)
Write-Host ("  Evidencia : {0}" -f $EvidenceDir)
if (-not [string]::IsNullOrWhiteSpace($VulkanDriverFiles)) {
    if (-not (Test-Path -LiteralPath $VulkanDriverFiles)) {
        throw "Manifesto ICD ausente: $VulkanDriverFiles"
    }
    $env:VK_DRIVER_FILES = $VulkanDriverFiles
    Write-Host ("  ICD       : {0}  (sobrepoe o registrado no sistema)" -f $VulkanDriverFiles) -ForegroundColor Yellow
}
Write-Host ''

$before = Get-ProbeSnapshot
Write-Host 'Telemetria ANTES:' -ForegroundColor Cyan
foreach ($k in $watched) { Write-Host ("  {0,-24} {1}" -f $k, $before[$k]) }
Write-Host ''

if ($before['P1EngineState'] -ne 1) {
    Write-Warning ("P1EngineState = {0} (esperado 1). O motor persistente nao esta online: " -f $before['P1EngineState'])
    Write-Warning 'faca boot frio e rode tools\test-p2-multi-submission.ps1 antes deste ensaio.'
}

# ---- corrida -------------------------------------------------------------

$benchLog = Join-Path $EvidenceDir 'llama-bench.log'
$benchExit = $null
$elapsed = $null

if ($SkipBench) {
    Write-Host 'SkipBench: nenhuma corrida executada.' -ForegroundColor Yellow
} else {
    Write-Host 'Rodando llama-bench (pode travar a maquina: watchdog do WDDM decide)...' -ForegroundColor Yellow

    <#
      Start-Process com redirecionamento em arquivo, de proposito.

      O llama.cpp escreve o log normal em stderr. No Windows PowerShell 5.1,
      stderr de executavel nativo dentro do pipeline vira NativeCommandError e,
      com $ErrorActionPreference = 'Stop', mata o processo na PRIMEIRA linha de
      saida. Foi exatamente o que invalidou o ensaio de 2026-08-31 12:13: o
      bench morreu em 0s e o delta zerado foi lido como "o RADV nao submete".
      Aqui nada passa pelo pipeline, entao nao ha o que reinterpretar.
    #>
    $errLog = Join-Path $EvidenceDir 'llama-bench.stderr.log'
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $bench `
        -ArgumentList @('-m', $Model, '-ngl', "$GpuLayers") `
        -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $benchLog `
        -RedirectStandardError $errLog
    $benchExit = $proc.ExitCode
    $sw.Stop()

    foreach ($logFile in @($benchLog, $errLog)) {
        if (Test-Path -LiteralPath $logFile) {
            Get-Content -LiteralPath $logFile | Select-Object -Last 25 |
                ForEach-Object { Write-Host ("  | {0}" -f $_) }
        }
    }
    $elapsed = [math]::Round($sw.Elapsed.TotalSeconds, 1)
    Write-Host ("llama-bench terminou em {0}s (exit={1})" -f $elapsed, $benchExit)
    Write-Host ''
}

$after = Get-ProbeSnapshot

# ---- delta e veredito ----------------------------------------------------

$delta = [ordered]@{}
foreach ($k in $watched) {
    $b = $before[$k]; $a = $after[$k]
    $delta[$k] = if ($null -eq $a -or $null -eq $b) { $null } else { $a - $b }
}

Write-Host 'Telemetria DEPOIS (delta):' -ForegroundColor Cyan
foreach ($k in $watched) {
    $mark = if ($delta[$k] -gt 0) { '  <--' } else { '' }
    Write-Host ("  {0,-24} {1,-12} (delta {2}){3}" -f $k, $after[$k], $delta[$k], $mark)
}
Write-Host ''

$pushed = [int64]$delta['V2TrackerPushedTotal']
$notified = [int64]$delta['V2NotifiedTotal']

Write-Host '=========================================================================' -ForegroundColor Magenta
if ($SkipBench) {
    $verdict = 'SEM CORRIDA - APENAS LEITURA'
    Write-Host $verdict -ForegroundColor Yellow
    Write-Host 'Nenhum UMD foi exercitado, portanto nao ha veredito sobre V1 nem V2.'
} elseif ($benchExit -ne 0) {
    <#
      Sem corrida valida nao ha veredito. Delta zerado com o bench morto nao
      prova nada sobre o RADV — declarar "nao submete" nesse caso seria
      inventar conclusao a partir de ausencia de dado.
    #>
    $verdict = 'ENSAIO INVALIDO - O BENCH NAO CONCLUIU'
    Write-Host $verdict -ForegroundColor Red
    Write-Host ("llama-bench saiu com codigo {0} em {1}s." -f $benchExit, $elapsed)
    Write-Host ("Veja {0} e {1}." -f $benchLog, (Join-Path $EvidenceDir 'llama-bench.stderr.log'))
    Write-Host 'Nenhuma conclusao sobre VAZAO pode ser tirada desta corrida.'

    <#
      Uma corrida que falha nao serve para medir throughput, mas pode carregar
      o diagnostico. Se o RADV instrumentado imprimiu o NTSTATUS da submissao,
      isso e' dado duro e merece subir para a tela em vez de ficar enterrado no
      log — foi assim que a ausencia do no de render ficou provada por escrito
      em 2026-08-31.
    #>
    $diag = @()
    foreach ($logFile in @($benchLog, (Join-Path $EvidenceDir 'llama-bench.stderr.log'))) {
        if (Test-Path -LiteralPath $logFile) {
            $diag += (Select-String -LiteralPath $logFile -Pattern 'radv/wddm2:' -SimpleMatch |
                ForEach-Object { $_.Line.Trim() })
        }
    }
    if ($diag.Count -gt 0) {
        Write-Host ''
        Write-Host 'DIAGNOSTICO CAPTURADO (RADV instrumentado):' -ForegroundColor Cyan
        $diag | Select-Object -Unique | ForEach-Object { Write-Host ("  {0}" -f $_) }
        Write-Host ''
        Write-Host 'Referencia de NTSTATUS comuns neste ponto:' -ForegroundColor Cyan
        Write-Host '  0xC00000E8  STATUS_INVALID_USER_BUFFER   argumentos recusados pelo dxgkrnl'
        Write-Host '  0xC00000BB  STATUS_NOT_SUPPORTED         DDI ausente na tabela do miniport'
        Write-Host '  0xC0000008  STATUS_INVALID_HANDLE        contexto/dispositivo invalido'
    }
} elseif ($pushed -le 0) {
    $verdict = 'RADV NAO SUBMETE PELO WDDM'
    Write-Host $verdict -ForegroundColor Red
    Write-Host 'Nenhuma submissao chegou a DxgkDdiSubmitCommandVirtual.'
    Write-Host 'A saida corrompida do llama.cpp e memoria nao inicializada, nao numerica.'
    Write-Host 'Proximo: Fase V3 (winsys + alvo gfx1013). A V2 segue nao exercitada.'
} elseif ($notified -le 0) {
    $verdict = 'SUBMETE MAS A RETIRADA DA V2 NAO NOTIFICA'
    Write-Host $verdict -ForegroundColor Red
    Write-Host ("Empurradas {0}, notificadas 0. Investigar poll/fence." -f $pushed)
} elseif ($notified -lt $pushed) {
    $verdict = 'RETIRADA PARCIAL'
    Write-Host $verdict -ForegroundColor Yellow
    Write-Host ("Empurradas {0}, notificadas {1}, em voo {2}." -f $pushed, $notified, $after['V2FencesInFlight'])
} else {
    $verdict = 'CICLO DE SUBMISSAO WDDM FECHADO'
    Write-Host $verdict -ForegroundColor Green
    Write-Host ("Empurradas {0}, notificadas {1}." -f $pushed, $notified)
}
if ([int64]$delta['V2TrackerOverflow'] -gt 0 -or
    [int64]$delta['V2TrackerOutOfOrder'] -gt 0 -or
    [int64]$delta['V2PushFailures'] -gt 0 -or
    [int64]$delta['V2PollFailures'] -gt 0) {
    Write-Host 'ATENCAO: contadores de falha do rastreador se moveram.' -ForegroundColor Red
}
Write-Host 'Aceleracao de LLM NAO esta validada por este ensaio.'
Write-Host 'NAO VALIDADO EM HARDWARE.'
Write-Host '=========================================================================' -ForegroundColor Magenta

$report = [ordered]@{
    captured_at        = (Get-Date).ToString('o')
    model              = $Model
    gpu_layers         = $GpuLayers
    bench_skipped      = [bool]$SkipBench
    bench_exit_code    = $benchExit
    bench_seconds      = $elapsed
    installed_sys_sha256 = (Get-FileHash -LiteralPath (Join-Path $env:SystemRoot 'System32\drivers\bc250mcdm.sys') -Algorithm SHA256).Hash
    before             = $before
    after              = $after
    delta              = $delta
    verdict            = $verdict
    hardware_validated = $false
}
$reportPath = Join-Path $EvidenceDir 'v2-wddm-submission.json'
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host ("Relatorio: {0}" -f $reportPath)
