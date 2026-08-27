# =============================================================================
#  FASE 5 - Multiplas resolucoes e modos de display
#
#  (Era Fase 3 no roadmap. Movida para o fim depois de nao funcionar em
#   silicio real; ver docs/FASE3_DIAGNOSTICO.md para o historico completo.)
#
#  O teste separa deliberadamente duas coisas:
#
#    LADO DO DRIVER  -- assertivas normais (PASS/FAIL). Aqui tudo funciona:
#                       12 modos de origem publicados, mode set de monitor
#                       coerente, EDID sintetico aceito pelo Windows, Commit
#                       do VidPN bem sucedido.
#
#    LADO DO SO      -- assertivas marcadas -Expected FAIL, ou seja XFAIL.
#                       A lista de modos do GDI fica vazia e todo
#                       ChangeDisplaySettings responde BADMODE, inclusive na
#                       resolucao nativa em uso. Isso e um bloqueio conhecido
#                       e documentado, nao uma regressao.
#
#  Se um XFAIL virar XPASS, o bloqueio foi resolvido -- a suite avisa em
#  destaque. Esse e o ponto de codificar o bloqueio como expectativa em vez de
#  simplesmente nao testar.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File Test-Fase5-Resolucao.ps1
# =============================================================================
[CmdletBinding()]
param(
    [switch]$PassThru
)

. (Join-Path $PSScriptRoot 'lib\Bc250TestLib.ps1')

$ctx = New-Bc250TestContext -Phase 'FASE 5' -Title 'Multiplas resolucoes (bloqueio conhecido)'
Write-Bc250Header $ctx

$t = Get-Bc250Telemetry
if ($null -eq $t) {
    Add-Bc250Check $ctx 'SKIP' 'todas as verificacoes' 'driver nao esta em execucao'
    return (Complete-Bc250Test $ctx $PassThru.IsPresent)
}

$w = [int](Get-TelemetryValue $t 'ScreenWidth' 0)
$h = [int](Get-TelemetryValue $t 'ScreenHeight' 0)

# =============================================================================
#  LADO DO DRIVER - deve passar
# =============================================================================
Write-Host '  --- lado do driver ---' -ForegroundColor DarkGray

$srcCount = [int](Get-TelemetryValue $t 'SourceSet_ModeCount' 0)
$tgtCount = [int](Get-TelemetryValue $t 'TargetSet_ModeCount' 0)
$monCount = [int](Get-TelemetryValue $t 'MonitorSet_ModeCount' 0)

Assert-Bc250 $ctx 'driver publica mais de um modo de origem' ($srcCount -gt 1) `
    "SourceSet_ModeCount=$srcCount"
Assert-Bc250 $ctx 'driver publica mode set de monitor' ($monCount -gt 1) `
    "MonitorSet_ModeCount=$monCount"
# Por desenho, o conjunto de destino tem apenas o timing nativo: o CRTC herdado
# do GOP nunca muda de timing.
Assert-Bc250 $ctx 'conjunto de destino restrito ao timing nativo' ($tgtCount -eq 1) `
    "TargetSet_ModeCount=$tgtCount"
Assert-Bc250 $ctx 'origem e monitor com a mesma contagem de modos' ($srcCount -eq $monCount) `
    "origem=$srcCount monitor=$monCount"

$cofunc = [int](Get-TelemetryValue $t 'Cofunc_CallCount' 0)
Assert-Bc250 $ctx 'dxgkrnl negociou cofuncionalidade' ($cofunc -gt 0) "Cofunc_CallCount=$cofunc"
$unpinned = [int](Get-TelemetryValue $t 'Cofunc_SrcUnpinnedCount' 0)
Assert-Bc250 $ctx 'houve enumeracao com origem nao pinada' ($unpinned -gt 0) `
    "Cofunc_SrcUnpinnedCount=$unpinned"

foreach ($k in @('Cofunc_SourceSetStatus','Cofunc_TargetSetStatus','Cofunc_UpdatePathStatus')) {
    $v = Get-TelemetryValue $t $k $null
    Assert-Bc250 $ctx ("nenhuma falha de publicacao: " + $k) ($null -eq $v) `
        $(if ($null -eq $v) { 'chave ausente = nenhuma falha registrada' } else { ("0x{0:X8}" -f [uint32]$v) })
}

$commit = [int](Get-TelemetryValue $t 'Commit_Status' -1)
Assert-Bc250 $ctx 'CommitVidPn aceitou o modo ativo' ($commit -eq 0) "Commit_Status=$commit"

$activeW = [int](Get-TelemetryValue $t 'Mode_ActiveW' 0)
$activeH = [int](Get-TelemetryValue $t 'Mode_ActiveH' 0)
$offX    = [int](Get-TelemetryValue $t 'Mode_OffsetX' -1)
$offY    = [int](Get-TelemetryValue $t 'Mode_OffsetY' -1)
Assert-Bc250 $ctx 'geometria ativa coerente com o scanout' `
    (($activeW -eq $w) -and ($activeH -eq $h)) "ativo=${activeW}x${activeH} scanout=${w}x${h}"
Assert-Bc250 $ctx 'offset de centralizacao zerado na resolucao nativa' `
    (($offX -eq 0) -and ($offY -eq 0)) "offset=($offX,$offY)"

# --- EDID sintetico ---------------------------------------------------------
$edidValid = [int](Get-TelemetryValue $t 'Edid_Valid' 0)
Assert-Bc250 $ctx 'EDID sintetico montado' ($edidValid -eq 1) "Edid_Valid=$edidValid"
$served = [int](Get-TelemetryValue $t 'Edid_ServedBytes' 0)
Assert-Bc250 $ctx 'dxgkrnl leu o bloco base completo de 128 bytes' ($served -eq 128) `
    "Edid_ServedBytes=$served"
$clock = [int](Get-TelemetryValue $t 'Edid_ClockMhz10kHz' (Get-TelemetryValue $t 'Edid_Clock10kHz' 0))
if ($clock -gt 0) {
    $hT = [int](Get-TelemetryValue $t 'Edid_HTotal' 0)
    $vT = [int](Get-TelemetryValue $t 'Edid_VTotal' 0)
    # O clock do descritor detalhado deve reconstruir 60 Hz exatos a partir do
    # total horizontal e vertical anunciados.
    $recon = 0
    if (($hT -gt 0) -and ($vT -gt 0)) { $recon = ($clock * 10000.0) / ($hT * $vT) }
    Assert-Bc250 $ctx 'timing do EDID reconstroi 60 Hz' `
        ([math]::Abs($recon - 60.0) -lt 0.5) `
        ("{0}x{1} @ {2:N4} Hz (clock {3} x 10kHz)" -f $hT, $vT, $recon, $clock)
}

# --- Monitor visto pelo SO --------------------------------------------------
$gdi = Get-Bc250GdiAdapter
if ($null -eq $gdi) {
    Add-Bc250Check $ctx 'SKIP' 'verificacoes de monitor' 'adaptador GDI nao encontrado'
} else {
    $mons = [Bc250Native]::Monitors($gdi.Name)
    if ($mons.Count -eq 0) {
        Add-Bc250Check $ctx 'FAIL' 'monitor filho presente' 'nenhum'
    } else {
        $m = $mons[0]
        # Com EDID aceito o Windows registra PnP; sem EDID cai em
        # Default_Monitor com BAD_EDID.
        Assert-Bc250 $ctx 'monitor registrado como PnP (EDID aceito)' `
            ($m.DeviceId -notlike '*Default_Monitor*') ($m.Description + ' / ' + $m.DeviceId)
        Assert-Bc250 $ctx 'ID do monitor derivado do nosso EDID (BCX13FE)' `
            ($m.DeviceId -like '*BCX13FE*') $m.DeviceId
    }

    $wmiModes = Get-Bc250MonitorModes
    if ($null -eq $wmiModes) {
        Add-Bc250Check $ctx 'FAIL' 'mode set do monitor consultavel por WMI' `
            'WmiMonitorListedSupportedSourceModes indisponivel'
    } else {
        Assert-Bc250 $ctx 'monitor reporta os modos do EDID' ($wmiModes.Count -gt 1) `
            ("$($wmiModes.Count) modos")
        Assert-Bc250 $ctx 'contagem do monitor bate com a publicada pelo driver' `
            ($wmiModes.Count -eq $monCount) ("WMI=$($wmiModes.Count) driver=$monCount")
        $hasNative = @($wmiModes | Where-Object { $_.Width -eq $w -and $_.Height -eq $h }).Count -gt 0
        Assert-Bc250 $ctx 'modo nativo presente no mode set do monitor' $hasNative "${w}x${h}"
    }
}

# =============================================================================
#  LADO DO SO - bloqueio conhecido, esperado falhar (XFAIL)
# =============================================================================
Write-Host '  --- lado do SO (bloqueio conhecido, XFAIL esperado) ---' -ForegroundColor DarkGray

if ($null -eq $gdi) {
    Add-Bc250Check $ctx 'SKIP' 'verificacoes do lado do SO' 'adaptador GDI nao encontrado'
} else {
    $filtered = [Bc250Native]::Modes($gdi.Name, 0)
    $raw      = [Bc250Native]::Modes($gdi.Name, 2)
    Add-Bc250Check $ctx 'INFO' 'modos do GDI com filtro de monitor' "$($filtered.Count)"
    Add-Bc250Check $ctx 'INFO' 'modos do GDI em EDS_RAWMODE' "$($raw.Count)"
    if ($raw.Count -gt 0) {
        Add-Bc250Check $ctx 'INFO' 'conteudo da lista raw' (($raw | ForEach-Object { $_.ToString() }) -join ', ')
    }

    Assert-Bc250 $ctx 'EnumDisplaySettings expoe mais de um modo' ($filtered.Count -gt 1) `
        "$($filtered.Count) modos -- ver docs/FASE3_DIAGNOSTICO.md secoes 9-11" -Expected FAIL

    # CDS_TEST apenas valida; nunca aplica o modo nem muda a tela.
    $cands = @(
        @{W=$w; H=$h; Native=$true},
        @{W=1920; H=1080; Native=$false},
        @{W=1600; H=900;  Native=$false},
        @{W=1280; H=720;  Native=$false},
        @{W=1024; H=768;  Native=$false}
    )
    foreach ($c in $cands) {
        if ($c.W -gt $w -or $c.H -gt $h) { continue }
        $rc = [Bc250Native]::TestMode($gdi.Name, $c.W, $c.H, 60)
        $nm = [Bc250Native]::DispChangeName($rc)
        $label = "CDS_TEST aceita $($c.W)x$($c.H)"
        if ($c.Native) { $label = $label + ' (resolucao ATIVA)' }
        Assert-Bc250 $ctx $label ($rc -eq 0) $nm -Expected FAIL
    }
}

# --- Ponteiro para a documentacao ------------------------------------------
Add-Bc250Check $ctx 'INFO' 'diagnostico completo do bloqueio' `
    'docs/FASE3_DIAGNOSTICO.md -- 6 hipoteses testadas, 4 correcoes aplicadas, causa ainda aberta'

return (Complete-Bc250Test $ctx $PassThru.IsPresent)
