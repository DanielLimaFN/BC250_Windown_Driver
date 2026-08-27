# =============================================================================
#  FASE 1 - Linha de base: KMDOD display-only, PnP, GOP, VSync, Present
#
#  Verifica que o adaptador esta online e que os tres mecanismos que sustentam
#  a Fase 1 estao de fato vivos:
#    - heranca do framebuffer UEFI GOP  (AcquirePostDisplayOwnership)
#    - motor de VSync sintetico a 60 Hz (DxgkCbNotifyInterrupt via timer DPC)
#    - caminho de apresentacao          (DxgkDdiPresentDisplayOnly)
#
#  Somente leitura, exceto pela janela de carga usada para provar o Present.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File Test-Fase1-LinhaDeBase.ps1
# =============================================================================
[CmdletBinding()]
param(
    [int]$VsyncSamples = 40,
    [double]$LoadSeconds = 3.0,
    [switch]$PassThru
)

. (Join-Path $PSScriptRoot 'lib\Bc250TestLib.ps1')

$ctx = New-Bc250TestContext -Phase 'FASE 1' -Title 'Linha de base (KMDOD + GOP + VSync + Present)'
Write-Bc250Header $ctx

# --- 1. PnP ------------------------------------------------------------------
$dev = Get-Bc250PnpDevice
Assert-Bc250 $ctx 'dispositivo PCI\VEN_1002&DEV_13FE presente' ($null -ne $dev) `
    $(if ($null -ne $dev) { $dev.InstanceId } else { 'nao encontrado' })

if ($null -eq $dev) {
    Add-Bc250Check $ctx 'SKIP' 'demais verificacoes' 'sem dispositivo, nada a testar'
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}

Assert-Bc250 $ctx 'status PnP = OK' ($dev.Status -eq 'OK') ("Status=" + $dev.Status)
Assert-Bc250 $ctx 'problema PnP = CM_PROB_NONE' ($dev.Problem -eq 'CM_PROB_NONE') ("Problem=" + $dev.Problem)

$ver = Get-Bc250DriverVersion
Assert-Bc250 $ctx 'driver bc250v2 selecionado para o dispositivo' ($null -ne $ver) ("versao=" + $ver)

# --- 2. Telemetria de inicializacao ------------------------------------------
$t = Get-Bc250Telemetry
Assert-Bc250 $ctx 'chave de telemetria do driver existe' ($null -ne $t) $script:Bc250ParamsKey

if ($null -eq $t) {
    Add-Bc250Check $ctx 'SKIP' 'verificacoes de telemetria' 'driver nunca chegou a rodar'
} else {
    $buildId = Get-TelemetryValue $t 'BuildId' '?'
    Add-Bc250Check $ctx 'INFO' 'BuildId do driver em execucao' "$buildId"

    $dodInit = Get-TelemetryValue $t 'DodInitStatus' -1
    Assert-Bc250 $ctx 'DxgkInitializeDisplayOnlyDriver retornou sucesso' ([int]$dodInit -eq 0) "DodInitStatus=$dodInit"

    $startStatus = Get-TelemetryValue $t 'Start_Status' -1
    Assert-Bc250 $ctx 'DdiStartDevice retornou sucesso' ([int]$startStatus -eq 0) "Start_Status=$startStatus"

    $caps = Get-TelemetryValue $t 'QAI_DriverCaps_Status' -1
    Assert-Bc250 $ctx 'DXGKQAITYPE_DRIVERCAPS negociado' ([int]$caps -eq 0) "QAI_DriverCaps_Status=$caps"

    # --- 3. Heranca do framebuffer GOP --------------------------------------
    $post   = Get-TelemetryValue $t 'PostDisplayAcquired' 0
    $fbMap  = Get-TelemetryValue $t 'FbMapped' 0
    $dispMap= Get-TelemetryValue $t 'DisplayMapped' 0
    $mmioMap= Get-TelemetryValue $t 'MmioMapped' 0
    Assert-Bc250 $ctx 'framebuffer do UEFI GOP herdado' ([int]$post -eq 1) "PostDisplayAcquired=$post"
    Assert-Bc250 $ctx 'BAR0 (VRAM) mapeado' ([int]$fbMap -eq 1) "FbMapped=$fbMap"
    Assert-Bc250 $ctx 'framebuffer de scanout mapeado' ([int]$dispMap -eq 1) "DisplayMapped=$dispMap"
    Assert-Bc250 $ctx 'BAR5 (MMIO) mapeado' ([int]$mmioMap -eq 1) "MmioMapped=$mmioMap"

    $w = [int](Get-TelemetryValue $t 'ScreenWidth' 0)
    $h = [int](Get-TelemetryValue $t 'ScreenHeight' 0)
    $p = [int](Get-TelemetryValue $t 'ScreenPitch' 0)
    Assert-Bc250 $ctx 'geometria do scanout valida' (($w -gt 0) -and ($h -gt 0)) "${w}x${h}"
    Assert-Bc250 $ctx 'pitch cabe a largura em 32bpp' ($p -ge ($w * 4)) "pitch=$p, minimo=$($w*4)"
    Add-Bc250Check $ctx 'INFO' 'framebuffer de scanout' ("{0}x{1}, pitch {2}, {3:N1} MB" -f $w, $h, $p, ($p * $h / 1MB))

    $commit = Get-TelemetryValue $t 'Commit_Status' -1
    Assert-Bc250 $ctx 'CommitVidPn aceitou o modo ativo' ([int]$commit -eq 0) "Commit_Status=$commit"
}

# --- 4. Adaptador GDI e resolucao corrente ----------------------------------
$gdi = Get-Bc250GdiAdapter
Assert-Bc250 $ctx 'BC-250 e o adaptador de display ativo' ($null -ne $gdi) `
    $(if ($null -ne $gdi) { "$($gdi.Name) / $($gdi.Description)" } else { 'nenhum adaptador anexado com VEN_1002&DEV_13FE' })

if ($null -ne $gdi) {
    Assert-Bc250 $ctx 'adaptador marcado como primario' $gdi.Primary ("StateFlags=0x{0:X}" -f $gdi.StateFlags)
    $cur = [Bc250Native]::CurrentMode($gdi.Name)
    if ($null -eq $cur) {
        Add-Bc250Check $ctx 'FAIL' 'modo corrente legivel' 'ENUM_CURRENT_SETTINGS falhou'
    } else {
        Add-Bc250Check $ctx 'INFO' 'modo corrente' $cur.ToString()
        Assert-Bc250 $ctx 'modo corrente 32 bits por pixel' ($cur.Bpp -eq 32) ("bpp=" + $cur.Bpp)
        if ($null -ne $t) {
            $sameAsGop = (($cur.Width -eq [int](Get-TelemetryValue $t 'ScreenWidth' 0)) -and
                          ($cur.Height -eq [int](Get-TelemetryValue $t 'ScreenHeight' 0)))
            Assert-Bc250 $ctx 'modo corrente igual ao scanout do GOP' $sameAsGop `
                ("GDI=" + $cur.Width + "x" + $cur.Height)
        }
    }

    $mons = [Bc250Native]::Monitors($gdi.Name)
    Assert-Bc250 $ctx 'monitor filho presente' ($mons.Count -gt 0) `
        $(if ($mons.Count -gt 0) { $mons[0].Description + " / " + $mons[0].DeviceId } else { 'nenhum' })
}

# --- 5. VSync: medido pela API, nao pelo contador do registro ---------------
# Bc250LogReg(L"VsyncCount", ...) so e gravado dentro de IsSupportedVidPn,
# entao o valor no registro fica estagnado. D3DKMTWaitForVerticalBlankEvent
# mede o motor de VSync de ponta a ponta.
$vs = Measure-Bc250VsyncRate -Samples $VsyncSamples
if (-not $vs.Ok) {
    Add-Bc250Check $ctx 'FAIL' 'VSync entregue ao SO' $vs.Reason
} else {
    Assert-Bc250 $ctx 'WaitForVerticalBlankEvent retorna (VSync vivo)' ($vs.Samples -gt 0) `
        ("$($vs.Samples) amostras")
    # O motor usa KeSetTimerEx com periodo de 17 ms (~58,8 Hz nominal).
    $inRange = (($vs.Hz -ge 45) -and ($vs.Hz -le 75))
    Assert-Bc250 $ctx 'taxa de VSync na faixa de 60 Hz (45-75)' $inRange ("$($vs.Hz) Hz")
    Add-Bc250Bench $ctx 'VSync: taxa observada' $vs.Hz 'Hz' 'timer DPC, periodo nominal 17 ms'
    Add-Bc250Bench $ctx 'VSync: intervalo medio' $vs.AvgMs 'ms' "min $($vs.MinMs) / max $($vs.MaxMs)"
    $jitter = [math]::Round($vs.MaxMs - $vs.MinMs, 3)
    Add-Bc250Bench $ctx 'VSync: jitter (max-min)' $jitter 'ms' 'VSync sintetico nao tem trava de hardware'
}

# --- 6. Present: prova que o caminho de apresentacao esta vivo --------------
# Present_Count e gravado no registro a cada 256 frames, entao o delta e
# quantizado em multiplos de 256. A carga precisa ser longa o bastante.
if ($null -eq $t) {
    Add-Bc250Check $ctx 'SKIP' 'caminho de apresentacao' 'sem telemetria'
} else {
    # A duracao minima aqui NAO segue -LoadSeconds: um passo do contador exige
    # ~4,3 s de carga a 60 Hz, e uma janela curta daria falso negativo.
    $pr = Measure-Bc250PresentRate -Width 800 -Height 600 -DirtyFraction 1.0
    Add-Bc250Check $ctx 'INFO' 'carga aplicada' `
        ("{0} repinturas em {1:N2}s ({2}/s no harness)" -f $pr.HarnessFrames, $pr.Seconds, $pr.HarnessFps)
    Assert-Bc250 $ctx 'Present_Count avancou sob carga' $pr.Ok `
        ("delta=$($pr.Delta) em $($pr.Seconds)s (contador grava a cada 256)")
    if ($pr.Ok) {
        Add-Bc250Bench $ctx 'Present: apresentacoes por segundo' $pr.PresentsPerSec 'apres/s' `
            'limite inferior; contador quantizado em 256'
    }
}

# --- 7. Ausencia de TDR / erros de display recentes -------------------------
$since = (Get-Date).AddMinutes(-10)
$tdr = @()
try {
    $tdr = @(Get-WinEvent -FilterHashtable @{ LogName='System'; StartTime=$since; Id=4101,4103,4105 } -ErrorAction Stop)
} catch { $tdr = @() }
Assert-Bc250 $ctx 'nenhum TDR/reset de display nos ultimos 10 min' ($tdr.Count -eq 0) `
    ("eventos 4101/4103/4105: " + $tdr.Count)

return (Complete-Bc250Test $ctx $PassThru.IsPresent)
