# =============================================================================
#  FASE 4 - Compute / GPGPU: aneis MEC, caps e dispatch
#
#  (Era Fase 5 no roadmap original, promovida quando a antiga Fase 3 --
#   multiplas resolucoes -- foi movida para o fim.)
#
#  Exercita a superficie de compute que existe hoje no driver:
#    - escape QUERY_COMPUTE_CAPS (0xBD06) e coerencia dos caps reportados
#    - cadeia de microcodigo (RLC / CE / PFP / ME / MEC / MEC2) via telemetria
#    - estado dos aneis de comando (GRBM_STATUS, CP_STAT, rptr/wptr)
#    - contador de dispatches
#
#  Este teste NAO submete shaders. COMPUTE_DISPATCH exige um shader valido
#  residente em VRAM e um endereco GPU correto; disparar isso as cegas em
#  silicio real e uma boa receita para travar a GPU. O teste verifica que a
#  interface responde e que o hardware esta no estado esperado -- a validacao
#  de dispatch pertence a um teste dedicado, com shader conhecido.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File Test-Fase4-Compute.ps1
# =============================================================================
[CmdletBinding()]
param(
    [switch]$PassThru
)

. (Join-Path $PSScriptRoot 'lib\Bc250TestLib.ps1')

$ctx = New-Bc250TestContext -Phase 'FASE 4' -Title 'Compute / GPGPU (aneis MEC, caps, dispatch)'
Write-Bc250Header $ctx

$t   = Get-Bc250Telemetry
$svc = Get-Bc250ServiceConfig
if ($null -eq $t -or $null -eq $svc) {
    Add-Bc250Check $ctx 'SKIP' 'todas as verificacoes' 'driver nao esta em execucao'
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}

$skipBringup = [int](Get-TelemetryValue $svc 'SkipHwBringup' 1)
$dangerous   = [int](Get-TelemetryValue $svc 'EnableDangerousEscapes' 0)
Add-Bc250Check $ctx 'INFO' 'SkipHwBringup' "$skipBringup"
Add-Bc250Check $ctx 'INFO' 'EnableDangerousEscapes' "$dangerous"

if ($skipBringup -ne 0) {
    Add-Bc250Check $ctx 'SKIP' 'bring-up de hardware desabilitado' `
        'compute exige: install-wddm.ps1 -EnableHardwareBringup'
}

# --- 1. Cadeia de microcodigo ----------------------------------------------
# HwStatusFw[7] cobre SDMA0, RLC, CE, PFP, ME, MEC, MEC2. Os nomes exatos das
# chaves dependem de como wddm_hw.c as publica; varremos o que existir.
$fwKeys = @($t.Keys | Where-Object { $_ -like 'Hw_Fw_*' } | Sort-Object)
if ($fwKeys.Count -eq 0) {
    Add-Bc250Check $ctx 'SKIP' 'status do microcodigo' `
        'nenhuma chave Hw_Fw_* -- carregamento de firmware nao foi executado'
} else {
    foreach ($k in $fwKeys) {
        $v = [uint32](Get-TelemetryValue $t $k 0xFFFFFFFF)
        $name = $k -replace '^Hw_Fw_', ''
        Assert-Bc250 $ctx ("microcodigo autenticado: " + $name) ($v -eq 0) `
            ("status=0x{0:X8}" -f $v)
    }
    $mecLoaded = ($fwKeys | Where-Object { $_ -match 'Mec' }).Count
    if ($mecLoaded -eq 0) {
        Add-Bc250Check $ctx 'SKIP' 'aneis de compute MEC1/MEC2' `
            'sem chaves de firmware MEC -- item 5.1 do roadmap nao implementado ainda'
    }
}

# --- 2. Estado do hardware via QUERY_INFO ----------------------------------
$info = Get-Bc250EscapeInfo
if (-not $info.Ok) {
    Add-Bc250Check $ctx 'SKIP' 'escape QUERY_INFO' $info.Reason
} else {
    Assert-Bc250 $ctx 'escape QUERY_INFO respondeu' ([int]$info.DriverStatus -eq 0) `
        ("status=0x{0:X8}" -f $info.DriverStatus)
    Add-Bc250Check $ctx 'INFO' 'HardwareInitialized' "$($info.HardwareInitialized)"
    Add-Bc250Check $ctx 'INFO' 'GRBM_STATUS' ("0x{0:X8}" -f $info.GrbmStatus)
    Add-Bc250Check $ctx 'INFO' 'CP_STAT' ("0x{0:X8}" -f $info.CpStat)
    Add-Bc250Check $ctx 'INFO' 'anel de graficos wptr/rptr' "$($info.GfxWptr) / $($info.GfxRptr)"

    if ($info.HardwareInitialized -ne 0) {
        # GRBM_STATUS bit 31 = GUI_ACTIVE. Fora de carga deve estar baixo; se
        # ficar preso em 1 sem trabalho submetido, o motor esta travado.
        $guiActive = (($info.GrbmStatus -shr 31) -band 1)
        Assert-Bc250 $ctx 'GRBM GUI_ACTIVE baixo em repouso' ($guiActive -eq 0) `
            ("GUI_ACTIVE=$guiActive")
        # Ler 0xFFFFFFFF em ambos e sinal classico de MMIO morto ou GPU
        # em estado de energia baixo.
        $bothFF = (($info.GrbmStatus -eq 0xFFFFFFFF) -and ($info.CpStat -eq 0xFFFFFFFF))
        Assert-Bc250 $ctx 'leitura de MMIO plausivel (nao 0xFFFFFFFF)' (-not $bothFF) `
            ("GRBM=0x{0:X8} CP_STAT=0x{1:X8}" -f $info.GrbmStatus, $info.CpStat)
    } else {
        Add-Bc250Check $ctx 'SKIP' 'verificacoes de estado do motor grafico' `
            'HardwareInitialized=0 -- anel de graficos nao ligado'
    }
}

# --- 3. Caps de compute via QUERY_COMPUTE_CAPS (0xBD06) --------------------
$caps = Get-Bc250ComputeCaps
if (-not $caps.Ok) {
    Add-Bc250Check $ctx 'SKIP' 'escape QUERY_COMPUTE_CAPS' $caps.Reason
} elseif ($caps.DriverStatus -ne 0) {
    # STATUS_NOT_SUPPORTED (0xC00000BB) significa que o escape ainda nao esta
    # implementado no build instalado; ACCESS_DENIED significa que falta
    # EnableDangerousEscapes.
    $s = "0x{0:X8}" -f $caps.DriverStatus
    if ($caps.DriverStatus -eq 0xC0000022) {
        Add-Bc250Check $ctx 'SKIP' 'QUERY_COMPUTE_CAPS' `
            "ACCESS_DENIED ($s) -- reinstale com -EnableDangerousEscapes"
    } elseif ($caps.DriverStatus -eq 0xC00000BB) {
        Add-Bc250Check $ctx 'SKIP' 'QUERY_COMPUTE_CAPS' `
            "NOT_SUPPORTED ($s) -- escape ausente no build instalado"
    } else {
        Add-Bc250Check $ctx 'FAIL' 'QUERY_COMPUTE_CAPS' ("driver retornou " + $s)
    }
} else {
    Add-Bc250Check $ctx 'PASS' 'escape QUERY_COMPUTE_CAPS respondeu' ''

    # Valores esperados para a BC-250 (Cyan Skillfish / Navi12 Lite):
    #   24 CUs, 12 WGPs, 2 SIMD32 por CU, 1536 stream processors,
    #   GFX10.1 (1010), wave32, workgroup maximo 1024.
    Assert-Bc250 $ctx 'CUs reportadas = 24' ($caps.NumCu -eq 24) "NumCu=$($caps.NumCu)"
    Assert-Bc250 $ctx 'WGPs reportados = 12' ($caps.NumWgp -eq 12) "NumWgp=$($caps.NumWgp)"
    Assert-Bc250 $ctx 'stream processors = 1536' ($caps.NumTotalShaders -eq 1536) `
        "NumTotalShaders=$($caps.NumTotalShaders)"
    # 24 CUs x 2 SIMD x 32 lanes = 1536. Checa a coerencia interna dos caps.
    $derived = $caps.NumCu * $caps.NumSimdPerCu * $caps.WaveSize
    Assert-Bc250 $ctx 'caps internamente coerentes (CU x SIMD x wave = shaders)' `
        ($derived -eq $caps.NumTotalShaders) `
        ("$($caps.NumCu) x $($caps.NumSimdPerCu) x $($caps.WaveSize) = $derived")
    Assert-Bc250 $ctx 'arquitetura GFX10.1 (1010)' ($caps.GfxArch -eq 1010) "GfxArch=$($caps.GfxArch)"
    Assert-Bc250 $ctx 'wave size = 32 (RDNA)' ($caps.WaveSize -eq 32) "WaveSize=$($caps.WaveSize)"
    Assert-Bc250 $ctx 'workgroup maximo = 1024' ($caps.MaxWorkgroupSize -eq 1024) `
        "MaxWorkgroupSize=$($caps.MaxWorkgroupSize)"
    Assert-Bc250 $ctx 'base de VRAM de compute dentro da abertura' `
        ($caps.ComputeVramBase -ge 0xF400000000) ("0x{0:X}" -f $caps.ComputeVramBase)
    Assert-Bc250 $ctx 'area de staging de compute nao vazia' ($caps.ComputeVramSize -gt 0) `
        ("{0:N1} MB" -f ($caps.ComputeVramSize / 1MB))

    Add-Bc250Bench $ctx 'Compute: clock da GPU' $caps.GpuClockMhz 'MHz' ''
    Add-Bc250Bench $ctx 'Compute: stream processors' $caps.NumTotalShaders 'lanes' `
        "$($caps.NumCu) CUs"
    Add-Bc250Bench $ctx 'Compute: LDS por WGP' ([math]::Round($caps.LdsSizeBytes / 1KB, 0)) 'KB' ''
    Add-Bc250Bench $ctx 'Compute: dispatches acumulados' $caps.DispatchCount 'dispatches' ''

    # FP32 teorico = lanes x 2 (FMA) x clock. E um numero de placa, nao
    # medido -- rotulado como teorico para nao ser confundido com benchmark.
    if (($caps.GpuClockMhz -gt 0) -and ($caps.NumTotalShaders -gt 0)) {
        $tflops = [math]::Round(($caps.NumTotalShaders * 2.0 * $caps.GpuClockMhz) / 1000000.0, 3)
        Add-Bc250Bench $ctx 'Compute: FP32 teorico de pico' $tflops 'TFLOPS' `
            'lanes x 2 (FMA) x clock; teorico, nao medido'
    }
}

# --- 4. Coerencia dos IDs de escape entre driver e teste nativo ------------
#
# Existe um test_phase5_compute.c nesta pasta com sua propria copia dos
# defines de escape. Se ela divergir de wddm.h, o teste chama um comando
# diferente do que pretende -- e no caso de 0xBD04 isso significa cair no
# caminho de COPIA DMA do driver, que le SrcGpuAddress/DstGpuAddress de cima
# de um struct de outro formato e submete um DMA com enderecos arbitrarios.
# Por isso este teste COMPARA os IDs e nao executa o binario quando ha
# divergencia: detectar o defeito e seguro, dispara-lo nao e.
$hdrFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'wddm.h'
$natFile = Join-Path $PSScriptRoot 'test_phase5_compute.c'

function Get-EscapeDefines {
    param([string]$Path)
    $map = @{}
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        if ($line -match '^\s*#define\s+(BC250_ESCAPE\w*)\s+(0x[0-9A-Fa-f]+)u?') {
            $map[$Matches[1]] = [Convert]::ToUInt32($Matches[2], 16)
        }
    }
    return $map
}

$hdrMap = Get-EscapeDefines $hdrFile
$natMap = Get-EscapeDefines $natFile

if ($null -eq $hdrMap -or $null -eq $natMap) {
    Add-Bc250Check $ctx 'SKIP' 'coerencia de IDs de escape' 'wddm.h ou test_phase5_compute.c ausente'
} else {
    $mismatch = New-Object System.Collections.ArrayList
    foreach ($k in $natMap.Keys) {
        if (-not $hdrMap.ContainsKey($k)) { continue }
        if ($natMap[$k] -ne $hdrMap[$k]) {
            [void]$mismatch.Add(("{0}: teste=0x{1:X} driver=0x{2:X}" -f $k, $natMap[$k], $hdrMap[$k]))
        }
    }
    # Colisao perigosa: um ID do teste nativo que, no driver, e OUTRO comando.
    $collisions = New-Object System.Collections.ArrayList
    foreach ($k in $natMap.Keys) {
        $v = $natMap[$k]
        foreach ($hk in $hdrMap.Keys) {
            if (($hdrMap[$hk] -eq $v) -and ($hk -ne $k)) {
                [void]$collisions.Add(("{0} do teste (0x{1:X}) e {2} no driver" -f $k, $v, $hk))
            }
        }
    }

    Assert-Bc250 $ctx 'IDs de escape do teste nativo batem com wddm.h' ($mismatch.Count -eq 0) `
        $(if ($mismatch.Count -eq 0) { 'todos coincidem' } else { ($mismatch -join ' | ') })

    if ($collisions.Count -gt 0) {
        Add-Bc250Check $ctx 'FAIL' 'colisao de ID de escape (RISCO EM SILICIO REAL)' `
            (($collisions -join ' | ') + ' -- corrija test_phase5_compute.c antes de rodar com -EnableHardwareBringup -EnableDangerousEscapes')
        Add-Bc250Check $ctx 'SKIP' 'execucao de test_phase5_compute.exe' `
            'nao executado de proposito: com os IDs divergentes ele dispara o caminho errado do driver'
    } else {
        $exe = Join-Path $PSScriptRoot 'test_phase5_compute.exe'
        if (Test-Path -LiteralPath $exe) {
            Add-Bc250Check $ctx 'INFO' 'teste nativo disponivel' `
                'test_phase5_compute.exe -- rode-o separadamente para dispatch de shader'
        }
    }
}

# --- 5. Dispatch: deliberadamente nao exercitado --------------------------
Add-Bc250Check $ctx 'SKIP' 'COMPUTE_DISPATCH (0xBD07)' `
    'exige shader valido em VRAM e endereco GPU correto; dispatch as cegas trava a GPU'

# --- 5. Estabilidade ------------------------------------------------------
$dev = Get-Bc250PnpDevice
Assert-Bc250 $ctx 'dispositivo OK depois das consultas de compute' `
    (($null -ne $dev) -and ($dev.Problem -eq 'CM_PROB_NONE')) `
    $(if ($null -ne $dev) { "Problem=" + $dev.Problem } else { 'dispositivo desapareceu' })

return (Complete-Bc250Test $ctx $PassThru.IsPresent)
