<#
.SYNOPSIS
    Ensaio F4c - soak de estabilidade do display WDDM (candidato 3.24, SkipHwBringup=1).

.DESCRIPTION
    Monitora, sem tocar em nada, se o caminho de display do bc250mcdm sofre regressao
    ao longo do tempo.

    LIMITACAO CONHECIDA E DELIBERADA - leia antes de interpretar o resultado:

      A telemetria em Parameters\Probe e' um RETRATO DE BRING-UP, nao um monitor de
      runtime. Verificado empiricamente em 2026-08-29: nenhum dos 100 campos da chave
      muda ao longo do tempo com o driver ativo.

      Em particular DmmVsyncCount NAO e' um contador vivo. Ele e' gravado uma unica vez,
      via latch VsyncProofRecorded em
      driver\wdk\bc250_display_ddi.c (Bc250RecordVsyncProofAfterSuccessfulPresent):

          InterlockedCompareExchange(&VsyncProofRecorded, 1, 0) == 0

      Depois do primeiro Present bem-sucedido o valor congela. Ler "delta 0" nesse campo
      e' o comportamento correto do driver, NAO uma regressao.

    PORTANTO este soak prova AUSENCIA DE REGRESSAO, nao liveness de VSync:

      PROVA        - o dispositivo permanece em Codigo 0
                   - o servico permanece bc250mcdm (nao houve rollback p/ BasicDisplay)
                   - nao surgiram erros de display/TDR no Event Log durante a janela
                   - os status de bring-up (Present/Commit) nao regrediram

      NAO PROVA    - que o VSync continua sendo entregue
                   - que Presents continuam concluindo
                   Para isso seria preciso um contador vivo no driver - codigo novo,
                   registrado como pendencia no STATUS_E_ROADMAP_CONSOLIDADO.md.

    100% READ-ONLY. Nao desabilita dispositivo, nao escreve registro, nao instala nada.
    Rode com a maquina em uso normal - carga real de desktop e' o cenario desejado.

.PARAMETER Minutes
    Duracao do soak em minutos. Padrao 240 (4 h, conforme a pendencia F4c).

.PARAMETER IntervalSeconds
    Intervalo entre amostras. Padrao 30.

.PARAMETER OutFile
    CSV de saida. Padrao: docs\evidence\soak-display-<timestamp>.csv

.PARAMETER StopOnRegression
    Encerra assim que detectar a primeira regressao.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\soak-bc250mcdm-display.ps1 -Minutes 5 -IntervalSeconds 15
    Soak curto, para validar o instrumento.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\soak-bc250mcdm-display.ps1
    Soak completo de 4 horas.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1440)]
    [int]$Minutes = 240,

    [ValidateRange(5, 600)]
    [int]$IntervalSeconds = 30,

    [string]$OutFile,

    [switch]$StopOnRegression
)

$ErrorActionPreference = 'Stop'

$root       = Split-Path -Parent $PSScriptRoot
$probePath  = 'HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe'
$deviceGlob = 'PCI\VEN_1002&DEV_13FE*'

function Get-Bc250Snapshot {
    $snap = [ordered]@{
        Timestamp        = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
        Service          = 'n/a'
        ConfigMgrError   = -1
        SkipHwBringup    = -1
        DmmVsyncCount    = -1
        DmmPresentStatus = -1
        DmmCommitStatus  = -1
        DmmPresentEntered= -1
        Note             = ''
    }

    $devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -like $deviceGlob })
    if ($devices.Count -eq 1) {
        $snap.Service        = $devices[0].Service
        $snap.ConfigMgrError = $devices[0].ConfigManagerErrorCode
    } else {
        $snap.Note = "dispositivos encontrados: $($devices.Count)"
    }

    if (Test-Path -LiteralPath $probePath) {
        $p = Get-ItemProperty -LiteralPath $probePath -ErrorAction SilentlyContinue
        if ($null -ne $p) {
            foreach ($name in @('SkipHwBringup','DmmVsyncCount','DmmPresentStatus',
                                'DmmCommitStatus','DmmPresentEntered')) {
                if ($null -ne $p.PSObject.Properties[$name]) {
                    $snap[$name] = [int64]$p.$name
                }
            }
        }
    } else {
        $snap.Note = 'chave Probe ausente'
    }

    return [pscustomobject]$snap
}

function Get-DisplayEventCount {
    param([datetime]$Since)
    # Erros de display/TDR: Display (TDR), BugCheck, e falhas de PnP do proprio servico.
    $count = 0
    try {
        $ev = @(Get-WinEvent -FilterHashtable @{
            LogName   = 'System'
            Level     = 1,2
            StartTime = $Since
        } -ErrorAction Stop | Where-Object {
            $_.ProviderName -match 'Display|dxgkrnl|BugCheck|bc250mcdm' -or
            $_.Id -in @(4101, 4103, 1001)
        })
        $count = $ev.Count
    } catch {
        # Sem eventos na janela e' o caso normal e o Get-WinEvent lanca; tratar como 0.
        $count = 0
    }
    return $count
}

Write-Host ''
Write-Host '=== ENSAIO F4c: SOAK DO DISPLAY WDDM (READ-ONLY) ===' -ForegroundColor Cyan
Write-Host ''

$baseline = Get-Bc250Snapshot
$startTime = Get-Date

Write-Host ("  Servico          : {0}" -f $baseline.Service)
Write-Host ("  ConfigMgrError   : {0}" -f $baseline.ConfigMgrError)
Write-Host ("  SkipHwBringup    : {0}" -f $baseline.SkipHwBringup)
Write-Host ("  DmmPresentStatus : {0}" -f $baseline.DmmPresentStatus)
Write-Host ("  DmmCommitStatus  : {0}" -f $baseline.DmmCommitStatus)
Write-Host ("  DmmVsyncCount    : {0}  (latch de bring-up - nao avanca por design)" -f $baseline.DmmVsyncCount)
Write-Host ''

$blockers = [System.Collections.Generic.List[string]]::new()
if ($baseline.Service -ne 'bc250mcdm') {
    $blockers.Add("dispositivo ligado a '$($baseline.Service)' - o bc250mcdm nao esta servindo o display")
}
if ($baseline.ConfigMgrError -ne 0) {
    $blockers.Add("ConfigManagerErrorCode=$($baseline.ConfigMgrError) - dispositivo nao esta em Codigo 0")
}
if ($baseline.DmmPresentStatus -ne 0 -or $baseline.DmmCommitStatus -ne 0) {
    $blockers.Add('Present/Commit ja partem com status != 0 - nao ha baseline saudavel para soak')
}

foreach ($b in $blockers) { Write-Host ("  BLOQUEIO: {0}" -f $b) -ForegroundColor Red }
if ($blockers.Count -gt 0) {
    Write-Host ''
    Write-Host 'PREFLIGHT REPROVADO - soak nao iniciado.' -ForegroundColor Red
    exit 1
}

if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $stamp    = (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss')
    $evidence = Join-Path $root 'docs\evidence'
    if (-not (Test-Path -LiteralPath $evidence)) {
        New-Item -ItemType Directory -Force -Path $evidence | Out-Null
    }
    $OutFile = Join-Path $evidence ("soak-display-{0}.csv" -f $stamp)
}

$deadline = $startTime.AddMinutes($Minutes)
$samples  = [System.Collections.Generic.List[object]]::new()

Write-Host ("Soak de {0} min, amostra a cada {1}s. Termina em {2}." -f `
    $Minutes, $IntervalSeconds, $deadline.ToString('HH:mm:ss')) -ForegroundColor Cyan
Write-Host ("Saida: {0}" -f $OutFile)
Write-Host ''
Write-Host 'hora      svc         cme  present  commit  evt  situacao'
Write-Host '--------  ----------  ---  -------  ------  ---  --------------------------'

$regressions = 0
$sampleNo    = 0

while ((Get-Date) -lt $deadline) {

    $s = Get-Bc250Snapshot
    $sampleNo++
    $evtCount = Get-DisplayEventCount -Since $startTime

    $situacao = 'estavel'
    $color    = 'Green'

    if ($s.Service -ne 'bc250mcdm') {
        $situacao = 'ROLLBACK p/ ' + $s.Service
        $color    = 'Red'
        $regressions++
    } elseif ($s.ConfigMgrError -ne 0) {
        $situacao = 'ERRO PnP ' + $s.ConfigMgrError
        $color    = 'Red'
        $regressions++
    } elseif ($s.DmmPresentStatus -ne 0 -or $s.DmmCommitStatus -ne 0) {
        $situacao = 'STATUS != 0'
        $color    = 'Red'
        $regressions++
    } elseif ($evtCount -gt 0) {
        $situacao = "EVENTO de display/TDR ($evtCount)"
        $color    = 'Yellow'
    }

    Write-Host ("{0}  {1,-10}  {2,3}  {3,7}  {4,6}  {5,3}  {6}" -f `
        (Get-Date).ToString('HH:mm:ss'), $s.Service, $s.ConfigMgrError,
        $s.DmmPresentStatus, $s.DmmCommitStatus, $evtCount, $situacao) -ForegroundColor $color

    $row = $s | Select-Object *
    $row | Add-Member -NotePropertyName Sample      -NotePropertyValue $sampleNo -Force
    $row | Add-Member -NotePropertyName DisplayEvts -NotePropertyValue $evtCount -Force
    $row | Add-Member -NotePropertyName Situacao    -NotePropertyValue $situacao -Force
    $samples.Add($row)

    # Grava incrementalmente: um travamento da maquina nao pode custar o log inteiro.
    $samples | Export-Csv -LiteralPath $OutFile -NoTypeInformation -Encoding UTF8

    if ($StopOnRegression -and $regressions -gt 0) {
        Write-Host ''
        Write-Host 'Parado na primeira regressao, conforme -StopOnRegression.' -ForegroundColor Yellow
        break
    }

    Start-Sleep -Seconds $IntervalSeconds
}

# ------------------------------------------------------------------ RESUMO --

$elapsed  = ((Get-Date) - $startTime).TotalMinutes
$finalEvt = Get-DisplayEventCount -Since $startTime

Write-Host ''
Write-Host '=== RESUMO ===' -ForegroundColor Cyan
Write-Host ("  Duracao real        : {0:N1} min" -f $elapsed)
Write-Host ("  Amostras            : {0}" -f $samples.Count)
Write-Host ("  Regressoes          : {0}" -f $regressions)
Write-Host ("  Eventos display/TDR : {0}" -f $finalEvt)
Write-Host ("  CSV                 : {0}" -f $OutFile)
Write-Host ''

if ($regressions -gt 0) {
    Write-Host 'SOAK REPROVADO - houve rollback, erro PnP ou status != 0.' -ForegroundColor Red
    exit 1
}
if ($finalEvt -gt 0) {
    Write-Host ("SOAK COM RESSALVA - sem regressao de binding, mas {0} evento(s) de display/TDR na janela." -f $finalEvt) -ForegroundColor Yellow
    Write-Host 'Inspecione o Event Log antes de considerar a janela limpa.'
    exit 0
}

Write-Host ("SOAK SEM REGRESSAO em {0:N1} min." -f $elapsed) -ForegroundColor Green
Write-Host ''
Write-Host 'ESCOPO DO QUE FOI PROVADO:'
Write-Host '  - Codigo 0 mantido, sem rollback para BasicDisplay, sem evento de display/TDR.'
Write-Host 'NAO PROVADO (falta instrumento de runtime no driver):'
Write-Host '  - que o VSync continua sendo entregue e que Presents continuam concluindo.'
Write-Host ''
Write-Host 'Ainda faltam para fechar F4c: reboot do proprio 3.24, suspend/resume e mudanca de modo.'
exit 0
