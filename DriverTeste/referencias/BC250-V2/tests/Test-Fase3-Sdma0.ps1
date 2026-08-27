# =============================================================================
#  FASE 3 - Aceleracao de copia por hardware: motor SDMA0
#
#  (Era Fase 4 no roadmap original. A Fase 3 antiga -- multiplas resolucoes --
#   foi movida para o fim, ver Test-Fase5-Resolucao.ps1.)
#
#  Verifica o bring-up do SDMA0 e mede a banda do motor de DMA.
#
#  PRE-REQUISITOS. Este teste degrada para SKIP com instrucao clara, em vez de
#  falhar, quando faltam:
#    - SkipHwBringup=0            -> reinstalar com -EnableHardwareBringup
#    - EnableDangerousEscapes=1   -> reinstalar com -EnableDangerousEscapes
#  A instalacao padrao deixa os dois desligados, e nesse estado o SDMA0 nem
#  sequer e inicializado.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File Test-Fase3-Sdma0.ps1
# =============================================================================
[CmdletBinding()]
param(
    [uint32]$BenchIterations = 64,
    # O benchmark submete cópias DMA de verdade. Com SkipHwBringup=0 e
    # EnableDangerousEscapes=1 ele TRAVOU A MÁQUINA em 20/08/2026 15:51 --
    # Kernel-Power 41 sem bugcheck e sem dump, ou seja, hang duro exigindo
    # desligamento na força. Por isso passou a exigir opt-in explícito em vez
    # de rodar por padrão dentro de run-all.ps1.
    [switch]$RunSdmaBenchmark,
    [switch]$PassThru
)

. (Join-Path $PSScriptRoot 'lib\Bc250TestLib.ps1')

$ctx = New-Bc250TestContext -Phase 'FASE 3' -Title 'Motor de copia por hardware SDMA0'
Write-Bc250Header $ctx

$t   = Get-Bc250Telemetry
$svc = Get-Bc250ServiceConfig

if ($null -eq $t -or $null -eq $svc) {
    Add-Bc250Check $ctx 'SKIP' 'todas as verificacoes' 'driver nao esta em execucao'
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}

# --- 1. Pre-requisitos de configuracao --------------------------------------
$skipBringup = [int](Get-TelemetryValue $svc 'SkipHwBringup' 1)
$dangerous   = [int](Get-TelemetryValue $svc 'EnableDangerousEscapes' 0)

Add-Bc250Check $ctx 'INFO' 'SkipHwBringup' "$skipBringup"
Add-Bc250Check $ctx 'INFO' 'EnableDangerousEscapes' "$dangerous"

if ($skipBringup -ne 0) {
    Add-Bc250Check $ctx 'SKIP' 'bring-up de hardware desabilitado' `
        'reinstale com: install-wddm.ps1 -EnableHardwareBringup'
}

# --- 2. Telemetria do bring-up do SDMA --------------------------------------
# Chaves gravadas por wddm_hw.c: Hw_Sdma_Status, Hw_Sdma_RbCntl,
# Hw_Sdma_Bandwidth_MBs, Hw_Sdma_Peak_MBs
$sdmaStatus = Get-TelemetryValue $t "Hw_Sdma_Status" $null
$sdmaStatusU = Get-TelemetryStatus $t "Hw_Sdma_Status"
if ($null -eq $sdmaStatus) {
    Add-Bc250Check $ctx 'SKIP' 'status do anel SDMA0' `
        'Hw_Sdma_Status ausente -- o bring-up do SDMA nao foi executado'
} else {
    Assert-Bc250 $ctx 'anel SDMA0 inicializado (Hw_Sdma_Status = 0)' ($sdmaStatusU -eq 0) `
        ("Hw_Sdma_Status=0x{0:X8}" -f $sdmaStatusU)
    $rbCntl = Get-TelemetryValue $t 'Hw_Sdma_RbCntl' 0
    Add-Bc250Check $ctx 'INFO' 'readback de SDMA0_RB_CNTL' ("0x{0:X8}" -f ([uint32]$rbCntl))
    # RB_ENABLE e o bit 0 de SDMA0_GFX_RB_CNTL.
    Assert-Bc250 $ctx 'SDMA0_RB_CNTL com RB_ENABLE ligado' ((([uint32]$rbCntl) -band 0x1) -ne 0) `
        ("bit0=" + (([uint32]$rbCntl) -band 0x1))
}

$fwStatus = Get-TelemetryValue $t "Hw_Fw_Sdma0" $null
if ($null -ne $fwStatus) {
    Assert-Bc250 $ctx 'microcodigo do SDMA0 autenticado pelo PSP' ((Get-TelemetryStatus $t "Hw_Fw_Sdma0") -eq 0) `
        ("Hw_Fw_Sdma0=0x{0:X8}" -f (Get-TelemetryStatus $t "Hw_Fw_Sdma0"))
}

# --- 3. Estado ao vivo via escape QUERY_INFO --------------------------------
# QUERY_INFO e o unico escape liberado sem EnableDangerousEscapes.
$info = Get-Bc250EscapeInfo
if (-not $info.Ok) {
    Add-Bc250Check $ctx 'SKIP' 'escape QUERY_INFO' $info.Reason
} else {
    Assert-Bc250 $ctx 'escape QUERY_INFO respondeu' ([int]$info.DriverStatus -eq 0) `
        ("status=0x{0:X8}" -f $info.DriverStatus)
    Assert-Bc250 $ctx 'device ID reportado pelo driver = 0x13FE' ($info.DeviceId -eq 0x13FE) `
        ("0x{0:X4}" -f $info.DeviceId)
    Add-Bc250Check $ctx 'INFO' 'HardwareInitialized' "$($info.HardwareInitialized)"
    Add-Bc250Check $ctx 'INFO' 'SdmaInitialized' "$($info.SdmaInitialized)"
    Add-Bc250Check $ctx 'INFO' 'base de VRAM na GPU' ("0x{0:X}" -f $info.VramGpuBase)
    Add-Bc250Check $ctx 'INFO' 'base do anel na GPU' ("0x{0:X}" -f $info.RingGpuBase)

    if ($info.SdmaInitialized -ne 0) {
        Add-Bc250Check $ctx 'PASS' 'SDMA0 reportado como inicializado' `
            ("wptr=$($info.SdmaWptr) rptr=$($info.SdmaRptr) copias=$($info.SdmaCopyCount)")
        # rptr alcancando wptr significa que o motor consumiu o que foi
        # submetido -- e a prova de que o SDMA executa, nao so aceita.
        Assert-Bc250 $ctx 'SDMA0 consumiu o que foi submetido (rptr == wptr)' `
            ($info.SdmaRptr -eq $info.SdmaWptr) `
            ("rptr=$($info.SdmaRptr) wptr=$($info.SdmaWptr)")
        if ($info.SdmaBandwidthMBs -gt 0) {
            Add-Bc250Bench $ctx 'SDMA0: banda da ultima copia' $info.SdmaBandwidthMBs 'MB/s' `
                'medida pelo driver'
        }
    } else {
        Add-Bc250Check $ctx 'SKIP' 'verificacoes do anel SDMA0' `
            'SdmaInitialized=0 -- motor nao esta ligado neste build/configuracao'
    }
}

# --- 4. Benchmark de banda via escape SDMA_BENCHMARK -----------------------
$canBench = ($dangerous -eq 1) -and ($info.Ok) -and ($info.SdmaInitialized -ne 0) -and $RunSdmaBenchmark.IsPresent
if (-not $canBench) {
    $why = 'pre-requisito ausente'
    if (-not $RunSdmaBenchmark.IsPresent) {
        $why = 'exige -RunSdmaBenchmark: este benchmark travou a maquina em 20/08 15:51 (Kernel-Power 41, sem bugcheck nem dump)'
    }
    elseif ($dangerous -ne 1) { $why = 'EnableDangerousEscapes=0 -- reinstale com -EnableDangerousEscapes' }
    elseif (-not $info.Ok) { $why = 'escape indisponivel: ' + $info.Reason }
    elseif ($info.SdmaInitialized -eq 0) { $why = 'SDMA0 nao inicializado' }
    Add-Bc250Check $ctx 'SKIP' 'benchmark de banda do SDMA0' $why
} else {
    Add-Bc250Check $ctx 'INFO' 'benchmark de SDMA0 habilitado explicitamente' `
        'submete copias DMA reais; ja travou a maquina uma vez'
    $blocks = @(
        @{ Name='64 KB';  Bytes=[uint32](64 * 1024) },
        @{ Name='256 KB'; Bytes=[uint32](256 * 1024) },
        @{ Name='1 MB';   Bytes=[uint32](1024 * 1024) },
        @{ Name='4 MB';   Bytes=[uint32](4 * 1024 * 1024) }
    )
    $any = $false
    foreach ($b in $blocks) {
        $r = Invoke-Bc250SdmaBenchmark -Iterations $BenchIterations -BlockSizeBytes $b.Bytes
        if (-not $r.Ok) {
            Add-Bc250Check $ctx 'FAIL' ("benchmark SDMA0 bloco de " + $b.Name) $r.Reason
            continue
        }
        if ($r.DriverStatus -ne 0) {
            Add-Bc250Check $ctx 'FAIL' ("benchmark SDMA0 bloco de " + $b.Name) `
                ("driver retornou 0x{0:X8}" -f $r.DriverStatus)
            continue
        }
        $any = $true
        Add-Bc250Bench $ctx ("SDMA0 " + $b.Name + ": banda media") $r.AvgMBs 'MB/s' `
            ("$BenchIterations iteracoes")
        Add-Bc250Bench $ctx ("SDMA0 " + $b.Name + ": banda de pico") $r.PeakMBs 'MB/s' ''
    }
    Assert-Bc250 $ctx 'benchmark do SDMA0 produziu numeros' $any ''

    # O objetivo 4.2 do roadmap e superar 10 GB/s. Registramos a comparacao
    # sem falhar a suite: a meta e de projeto, nao contrato do teste.
    if ($any) {
        $peak = ($ctx.Bench | Where-Object { $_.Metric -like 'SDMA0*banda de pico' } |
                 Measure-Object -Property Value -Maximum).Maximum
        if ($null -ne $peak) {
            $gbs = [math]::Round($peak / 1024.0, 2)
            Add-Bc250Bench $ctx 'SDMA0: melhor pico observado' $gbs 'GB/s' 'meta do roadmap 4.2: > 10 GB/s'
            if ($gbs -lt 10) {
                Add-Bc250Check $ctx 'INFO' 'meta de 10 GB/s do roadmap ainda nao alcancada' "$gbs GB/s"
            } else {
                Add-Bc250Check $ctx 'PASS' 'meta de 10 GB/s do roadmap alcancada' "$gbs GB/s"
            }
        }
    }
}

# --- 5. Estabilidade -------------------------------------------------------
$dev = Get-Bc250PnpDevice
Assert-Bc250 $ctx 'dispositivo OK depois dos testes de SDMA' `
    (($null -ne $dev) -and ($dev.Problem -eq 'CM_PROB_NONE')) `
    $(if ($null -ne $dev) { "Problem=" + $dev.Problem } else { 'dispositivo desapareceu' })

return (Complete-Bc250Test $ctx $PassThru.IsPresent)
