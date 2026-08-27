# =============================================================================
#  FASE 2 - Performance 2D: blit vetorizado + dirty rects
#
#  A Fase 2 otimiza o caminho DWM -> PresentDisplayOnly -> BAR0. Este teste
#  mede esse caminho de tres angulos:
#
#    1. taxa de apresentacao sustentada (apres/s) sob carga
#    2. efeito dos dirty rects: frame cheio contra retangulo pequeno
#    3. custo de CPU no dwm.exe durante a carga
#
#  Referencia: uma copia de memoria do host no mesmo tamanho de frame da a
#  cota superior do lado da CPU, contra a qual comparar o caminho real.
#
#  ATENCAO SOBRE O QUE ESTA IMPLEMENTADO: o blit em wddm_driver.c usa
#  _mm_loadu_si128 / _mm_stream_si128, ou seja SSE2 de 128 bits com stores
#  nao-temporais. O roadmap descreve AVX2 de 256 bits
#  (_mm256_stream_si256 / registradores YMM), que NAO esta no codigo. O teste
#  reporta isso como INFO em vez de fingir que a otimizacao de 256 bits
#  existe. A parte nao-temporal do requisito esta cumprida; a largura de
#  registrador nao.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File Test-Fase2-Blit2D.ps1
# =============================================================================
[CmdletBinding()]
param(
    [double]$LoadSeconds = 5.0,
    [switch]$PassThru
)

. (Join-Path $PSScriptRoot 'lib\Bc250TestLib.ps1')

$ctx = New-Bc250TestContext -Phase 'FASE 2' -Title 'Performance 2D (blit vetorizado + dirty rects)'
Write-Bc250Header $ctx

$t = Get-Bc250Telemetry
if ($null -eq $t) {
    Add-Bc250Check $ctx 'SKIP' 'todas as verificacoes' 'driver nao esta em execucao'
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}

$w = [int](Get-TelemetryValue $t 'ScreenWidth' 0)
$h = [int](Get-TelemetryValue $t 'ScreenHeight' 0)
$pitch = [int](Get-TelemetryValue $t 'ScreenPitch' 0)
if ($w -le 0 -or $h -le 0) {
    Add-Bc250Check $ctx 'SKIP' 'todas as verificacoes' 'geometria de scanout invalida'
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}
$frameBytes = [int64]$pitch * $h

# --- 1. O caminho de apresentacao esta ativo ---------------------------------
$bpp = [int](Get-TelemetryValue $t 'Present_BytesPerPixel' 0)
Assert-Bc250 $ctx 'Present opera em 32 bits por pixel' ($bpp -eq 4) "Present_BytesPerPixel=$bpp"

$srcPitch = [int](Get-TelemetryValue $t 'Present_SrcPitch' 0)
Assert-Bc250 $ctx 'pitch de origem do Present cabe na largura' ($srcPitch -ge ($w * 4)) `
    "Present_SrcPitch=$srcPitch"

$entered = [int](Get-TelemetryValue $t 'Present_Entered' 0)
$exited  = [int](Get-TelemetryValue $t 'Present_Exited' 0)
Assert-Bc250 $ctx 'Present entrou e saiu sem abortar' (($entered -eq 1) -and ($exited -eq 1)) `
    "Entered=$entered Exited=$exited"

# --- 2. Nota sobre a largura de vetor realmente implementada ----------------
$srcFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'wddm_driver.c'
if (Test-Path -LiteralPath $srcFile) {
    $src = Get-Content -LiteralPath $srcFile -Raw
    $has256 = ($src -match '_mm256_stream_si256')
    $has128 = ($src -match '_mm_stream_si128')
    Assert-Bc250 $ctx 'blit usa stores nao-temporais (streaming)' ($has128 -or $has256) `
        $(if ($has256) { '_mm256_stream_si256 (AVX2, 256 bits)' } elseif ($has128) { '_mm_stream_si128 (SSE2, 128 bits)' } else { 'nenhum encontrado' })
    if (-not $has256) {
        Add-Bc250Check $ctx 'INFO' 'largura de vetor: roadmap pede AVX2 de 256 bits' `
            'implementado com SSE2 de 128 bits; item 2.1 do roadmap parcialmente cumprido'
    }
} else {
    Add-Bc250Check $ctx 'SKIP' 'inspecao da largura de vetor' 'wddm_driver.c nao encontrado ao lado de tests/'
}

# --- 3. Benchmark: referencia de copia no host ------------------------------
# Cota superior do lado da CPU para um frame deste tamanho. O caminho real
# escreve em memoria write-combine sobre PCIe, entao ficara bem abaixo disto.
$hostMBs = [Bc250Native]::HostCopyMBs([int]$frameBytes, 50)
Add-Bc250Bench $ctx 'Referencia: copia de memoria do host' ([math]::Round($hostMBs, 0)) 'MB/s' `
    ("frame de {0:N1} MB, 50 iteracoes" -f ($frameBytes / 1MB))

# --- 4. Benchmark: frame cheio ---------------------------------------------
$full = Measure-Bc250PresentRate -Width $w -Height $h -DirtyFraction 1.0
Assert-Bc250 $ctx 'apresentacoes avancaram com frame cheio' $full.Ok `
    ("delta=$($full.Delta) em $($full.Seconds)s")

$fullPps = $full.PresentsPerSec
Add-Bc250Bench $ctx 'Frame cheio: apresentacoes por segundo' $fullPps 'apres/s' `
    'contador quantizado em 256; limite inferior'
Add-Bc250Bench $ctx 'Frame cheio: repinturas do harness' $full.HarnessFps 'fps' 'lado do produtor'

if ($fullPps -gt 0) {
    $mbs = [math]::Round(($fullPps * $frameBytes) / 1MB, 1)
    Add-Bc250Bench $ctx 'Frame cheio: vazao estimada para o BAR0' $mbs 'MB/s' `
        'apres/s x bytes por frame; cota superior do que foi escrito'
    if ($hostMBs -gt 0) {
        $frac = [math]::Round(100.0 * $mbs / $hostMBs, 1)
        Add-Bc250Bench $ctx 'Frame cheio: fracao da copia do host' $frac '%' `
            'PCIe + write-combine contra RAM local'
    }
}

# --- 5. Benchmark: dirty rect pequeno --------------------------------------
# Mesma duracao, mas so 1/16 da area por repintura. Se os dirty rects estao
# funcionando, a taxa de apresentacao sobe porque cada Present copia menos.
$small = Measure-Bc250PresentRate -Width $w -Height $h -DirtyFraction 0.25
$smallPps = $small.PresentsPerSec
Add-Bc250Bench $ctx 'Dirty rect 1/16 da area: apresentacoes/s' $smallPps 'apres/s' `
    ("retangulo {0}x{1}" -f $small.DirtyWidth, $small.DirtyHeight)
Add-Bc250Bench $ctx 'Dirty rect 1/16: repinturas do harness' $small.HarnessFps 'fps' 'lado do produtor'

if (($fullPps -gt 0) -and ($smallPps -gt 0)) {
    $ratio = [math]::Round($smallPps / $fullPps, 2)
    Add-Bc250Bench $ctx 'Ganho do dirty rect (pequeno / cheio)' $ratio 'x' `
        'esperado > 1 se o blit respeita a area suja'
    # Nao afirmamos um limiar rigido: o DWM decide os retangulos e pode
    # fundi-los. Uma razao abaixo de 1 indica que a area suja nao esta
    # reduzindo o trabalho -- vale investigar, nao falhar a suite.
    if ($ratio -lt 1.0) {
        Add-Bc250Check $ctx 'INFO' 'dirty rect nao reduziu o trabalho por Present' `
            "razao=$ratio; o DWM pode ter fundido os retangulos em frame cheio"
    } else {
        Add-Bc250Check $ctx 'PASS' 'area suja menor eleva a taxa de apresentacao' "razao=$ratio"
    }
}

# --- 6. Custo de CPU no DWM ------------------------------------------------
# O objetivo declarado da Fase 2 e reduzir CPU no DWM. Medimos tempo de CPU
# do processo dwm.exe durante uma janela de carga.
$dwm = Get-Process -Name dwm -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $dwm) {
    Add-Bc250Check $ctx 'SKIP' 'custo de CPU do DWM' 'processo dwm nao acessivel'
} else {
    $cpuBefore = $dwm.TotalProcessorTime
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    [void](Invoke-Bc250DrawLoad -Width $w -Height $h -Seconds $LoadSeconds -DirtyFraction 1.0)
    $sw.Stop()
    $dwm.Refresh()
    $cpuAfter = $dwm.TotalProcessorTime
    $cpuMs = ($cpuAfter - $cpuBefore).TotalMilliseconds
    if ($cpuMs -le 0) {
        # Zero nao significa "DWM gratuito": normalmente e contador
        # inacessivel sem elevacao, ou a composicao acontecendo em outro
        # processo. Reportar 0 MB/s seria enganoso.
        Add-Bc250Check $ctx 'INFO' 'custo de CPU do DWM nao mensuravel' `
            'TotalProcessorTime nao avancou -- rode elevado para obter o numero'
    } else {
        $pct = 0
        if ($sw.Elapsed.TotalMilliseconds -gt 0) {
            $pct = [math]::Round(100.0 * $cpuMs / $sw.Elapsed.TotalMilliseconds, 1)
        }
        Add-Bc250Bench $ctx 'DWM: tempo de CPU sob carga' ([math]::Round($cpuMs, 0)) 'ms' `
            ("em {0:N1}s de parede" -f $sw.Elapsed.TotalSeconds)
        Add-Bc250Bench $ctx 'DWM: ocupacao de um nucleo' $pct '%' 'tempo de CPU / tempo de parede'
    }
}

# --- 7. Estabilidade apos a carga ------------------------------------------
$dev = Get-Bc250PnpDevice
Assert-Bc250 $ctx 'dispositivo continua OK depois da carga' `
    (($null -ne $dev) -and ($dev.Problem -eq 'CM_PROB_NONE')) `
    $(if ($null -ne $dev) { "Problem=" + $dev.Problem } else { 'dispositivo desapareceu' })

$since = (Get-Date).AddMinutes(-5)
$tdr = @()
try { $tdr = @(Get-WinEvent -FilterHashtable @{ LogName='System'; StartTime=$since; Id=4101,4103,4105 } -ErrorAction Stop) } catch { $tdr = @() }
Assert-Bc250 $ctx 'nenhum TDR durante o benchmark' ($tdr.Count -eq 0) ("eventos: " + $tdr.Count)

return (Complete-Bc250Test $ctx $PassThru.IsPresent)
