<#
.SYNOPSIS
    tools/probe-dcn-readonly.ps1 - Sonda somente-leitura do bloco DCN 2.0.1 sob bc250dbg.

.DESCRIPTION
    Realiza o inventario do bloco de display DCN 2.0.1 sem alterar nenhum registrador.
    Utiliza IOCTL_BC250DBG_REG_READ atraves do bc250ctl.exe para efetuar dois snapshots
    temporizados (S1 e S2).
    Compara o frame counter (OTG_STATUS_FRAME_COUNT) para comprovar geracao de VSync vivo
    a ~60 Hz e decodifica os parametros de scanout herdados do UEFI GOP (endereco fisico
    em VRAM, viewport, pitch e formato).

.PARAMETER Execute
    Obrigatorio para executar leituras fisicas no hardware. Sem ele, exibe apenas o plano.

.PARAMETER SampleIntervalMs
    Intervalo entre Snapshot 1 e Snapshot 2 em milissegundos (padrao: 500 ms).

.PARAMETER OutputRoot
    Diretorio base para arquivamento da evidencia (padrao: docs\evidence).
#>

[CmdletBinding()]
param(
    [switch]$Execute,
    [ValidateRange(100, 5000)]
    [int]$SampleIntervalMs = 500,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 1.0

$projectRoot = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $projectRoot 'studio\output\bc250ctl.exe'
$driver = Join-Path $projectRoot 'output\bc250dbg.sys'

# Tabela do Catalogo DCN 2.0.1 (proveniencia: include/cyan_skillfish_ip_offset.h + dcn_2_0_0_offset.h)
# Base DMU = { 0x00000012, 0x000000C0, 0x000034C0, 0x00009000 }
$dcnRegisters = @(
    # OTG0 / OPTC (BASE_IDX = 2, base dword 0x34C0)
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_CONTROL';              Offset = 0x14004; BaseIdx = 2; RegDw = 0x1B41; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_STATUS';               Offset = 0x14024; BaseIdx = 2; RegDw = 0x1B49; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_STATUS_POSITION';      Offset = 0x14028; BaseIdx = 2; RegDw = 0x1B4A; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_NOM_VERT_POSITION';   Offset = 0x1402C; BaseIdx = 2; RegDw = 0x1B4B; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_STATUS_FRAME_COUNT';   Offset = 0x14030; BaseIdx = 2; RegDw = 0x1B4C; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_H_TOTAL';              Offset = 0x13FA8; BaseIdx = 2; RegDw = 0x1B2A; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_H_BLANK_START_END';    Offset = 0x13FAC; BaseIdx = 2; RegDw = 0x1B2B; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_H_SYNC_A';             Offset = 0x13FB0; BaseIdx = 2; RegDw = 0x1B2C; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_V_TOTAL';              Offset = 0x13FBC; BaseIdx = 2; RegDw = 0x1B2F; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_V_BLANK_START_END';    Offset = 0x13FD8; BaseIdx = 2; RegDw = 0x1B36; Group = 'OTG' },
    [pscustomobject]@{ Symbol = 'mmOTG0_OTG_V_SYNC_A';             Offset = 0x13FDC; BaseIdx = 2; RegDw = 0x1B37; Group = 'OTG' },

    # HUBP0 & HUBPREQ0 (BASE_IDX = 2, base dword 0x34C0)
    [pscustomobject]@{ Symbol = 'mmHUBP0_DCSURF_SURFACE_CONFIG';       Offset = 0x0EA94; BaseIdx = 2; RegDw = 0x05E5; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBP0_DCSURF_ADDR_CONFIG';          Offset = 0x0EA98; BaseIdx = 2; RegDw = 0x05E6; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBP0_DCSURF_TILING_CONFIG';        Offset = 0x0EA9C; BaseIdx = 2; RegDw = 0x05E7; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBP0_DCSURF_PRI_VIEWPORT_START';   Offset = 0x0EAA4; BaseIdx = 2; RegDw = 0x05E9; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION'; Offset = 0x0EAA8; BaseIdx = 2; RegDw = 0x05EA; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBPREQ0_DCSURF_SURFACE_PITCH';     Offset = 0x0EB1C; BaseIdx = 2; RegDw = 0x0607; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS'; Offset = 0x0EB28; BaseIdx = 2; RegDw = 0x060A; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH'; Offset = 0x0EB2C; BaseIdx = 2; RegDw = 0x060B; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBPREQ0_DCSURF_SURFACE_INUSE';     Offset = 0x0EB84; BaseIdx = 2; RegDw = 0x0621; Group = 'HUBP' },
    [pscustomobject]@{ Symbol = 'mmHUBPREQ0_DCSURF_SURFACE_INUSE_HIGH'; Offset = 0x0EB88; BaseIdx = 2; RegDw = 0x0622; Group = 'HUBP' },

    # Global / VGA Legacy (BASE_IDX = 0 e 1)
    [pscustomobject]@{ Symbol = 'mmVGA_MEM_WRITE_PAGE_ADDR';       Offset = 0x00048; BaseIdx = 0; RegDw = 0x0000; Group = 'MISC' },
    [pscustomobject]@{ Symbol = 'mmVGA_RENDER_CONTROL';            Offset = 0x00300; BaseIdx = 1; RegDw = 0x0000; Group = 'MISC' },
    [pscustomobject]@{ Symbol = 'mmDC_MEM_GLOBAL_PWR_REQ_CNTL';    Offset = 0x0030C; BaseIdx = 1; RegDw = 0x0003; Group = 'MISC' }
)

Write-Host "==========================================================================" -ForegroundColor Cyan
Write-Host " BC-250 SONDA SOMENTE-LEITURA DCN 2.0.1 (PRIORIDADE P1)" -ForegroundColor Cyan
Write-Host "==========================================================================" -ForegroundColor Cyan

if (-not $Execute) {
    Write-Host "[PREFLIGHT] Modo de demonstracao / plano. Use -Execute para leitura no hardware." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Registradores DCN 2.0.1 catalogados para leitura (Zero Escrita):"
    $dcnRegisters | Format-Table Symbol, @{Label='BAR5 Byte'; Expression={'0x{0:X5}' -f $_.Offset}}, BaseIdx, @{Label='mmREG dw'; Expression={'0x{0:X4}' -f $_.RegDw}}, Group
    exit 0
}

# 1. Checagem de privilegios e ambiente
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Execucao abortada: execute este script como Administrador."
}

if (-not (Test-Path -LiteralPath $cli)) {
    throw "Ferramenta ausente: $cli. Execute o build do studio primeiro."
}

$labService = Get-Service -Name 'bc250dbg' -ErrorAction SilentlyContinue
if (-not $labService -or $labService.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Running) {
    throw "Servico bc250dbg nao esta em execucao. Execute dbg\install.bat como Administrador."
}

# Funcao de leitura via bc250ctl.exe (estritamente somente-leitura)
function Read-Bar5Register([uint32]$Offset) {
    $argHex = '0x{0:X}' -f $Offset
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $cli
    $psi.Arguments = "reg r $argHex"
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $p = [System.Diagnostics.Process]::Start($psi)
    $stdout = $p.StandardOutput.ReadToEnd()
    $stderr = $p.StandardError.ReadToEnd()
    $p.WaitForExit(3000) | Out-Null

    if ($p.ExitCode -ne 0) {
        throw "Falha ao ler offset $argHex via bc250ctl: exit=$($p.ExitCode) $stderr"
    }

    # Saida esperada: 'bar 0x00014030 = 0x00001234'
    if ($stdout -match 'bar\s+0x[0-9A-Fa-f]+\s+=\s+(0x[0-9A-Fa-f]+)') {
        return [System.Convert]::ToUInt32($matches[1], 16)
    }
    throw "Formato inesperado ao ler offset $($argHex): $stdout"
}

# 2. Coleta de Snapshot 1
Write-Host "[P1-PROBE] Coletando Snapshot 1 (S1)..." -ForegroundColor Yellow
$snap1Time = Get-Date
$s1Data = @{}
foreach ($reg in $dcnRegisters) {
    $val = Read-Bar5Register -Offset $reg.Offset
    $s1Data[$reg.Symbol] = $val
}
Write-Host "           S1 concluido: $($dcnRegisters.Count) registradores lidos." -ForegroundColor Green

# 3. Intervalo medido
Write-Host "[P1-PROBE] Aguardando $SampleIntervalMs ms para medicao de frequencia do OTG..." -ForegroundColor Yellow
Start-Sleep -Milliseconds $SampleIntervalMs

# 4. Coleta de Snapshot 2
Write-Host "[P1-PROBE] Coletando Snapshot 2 (S2)..." -ForegroundColor Yellow
$snap2Time = Get-Date
$s2Data = @{}
foreach ($reg in $dcnRegisters) {
    $val = Read-Bar5Register -Offset $reg.Offset
    $s2Data[$reg.Symbol] = $val
}
Write-Host "           S2 concluido: $($dcnRegisters.Count) registradores lidos." -ForegroundColor Green

# 5. Analise dos Resultados
$elapsedSeconds = ($snap2Time - $snap1Time).TotalSeconds
$fc1 = $s1Data['mmOTG0_OTG_STATUS_FRAME_COUNT']
$fc2 = $s2Data['mmOTG0_OTG_STATUS_FRAME_COUNT']
$fcDelta = if ($fc2 -ge $fc1) { $fc2 - $fc1 } else { (0xFFFFFFFF - $fc1) + 1 + $fc2 }
$fpsEstimated = if ($elapsedSeconds -gt 0) { [math]::Round($fcDelta / $elapsedSeconds, 2) } else { 0 }

# Decodificacao de Scanout
$surfAddrLo = $s1Data['mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS']
$surfAddrHi = $s1Data['mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH']
$surfPhysAddr64 = ([uint64]$surfAddrHi -shl 32) -bor [uint64]$surfAddrLo

$inUseAddrLo = $s1Data['mmHUBPREQ0_DCSURF_SURFACE_INUSE']
$inUseAddrHi = $s1Data['mmHUBPREQ0_DCSURF_SURFACE_INUSE_HIGH']
$inUsePhysAddr64 = ([uint64]$inUseAddrHi -shl 32) -bor [uint64]$inUseAddrLo

$vpDim = $s1Data['mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION']
$vpWidth = $vpDim -band 0xFFFF
$vpHeight = ($vpDim -shr 16) -band 0xFFFF

$hTotal = $s1Data['mmOTG0_OTG_H_TOTAL'] -band 0x7FFF
$vTotal = $s1Data['mmOTG0_OTG_V_TOTAL'] -band 0x7FFF

Write-Host ""
Write-Host "==========================================================================" -ForegroundColor Green
Write-Host " RELATORIO DE INVENTARIO DCN 2.0.1 (UEFI GOP HERDADO)" -ForegroundColor Green
Write-Host "==========================================================================" -ForegroundColor Green
Write-Host (" OTG Frame Counter (S1): 0x{0:X8} ({0})" -f $fc1)
Write-Host (" OTG Frame Counter (S2): 0x{0:X8} ({0})" -f $fc2)
Write-Host (" Delta de Frames:        {0} frames em {1:F3} s" -f $fcDelta, $elapsedSeconds)
Write-Host (" Frequencia Estimada:    {0} Hz" -f $fpsEstimated) -ForegroundColor $(if ($fpsEstimated -ge 50 -and $fpsEstimated -le 70) { 'Green' } else { 'Yellow' })
Write-Host (" Resolucao Viewport:     {0} x {1}" -f $vpWidth, $vpHeight)
Write-Host (" Timing Total (H x V):   {0} x {1}" -f $hTotal, $vTotal)
Write-Host (" Endereco Framebuffer:   0x{0:X12}" -f $surfPhysAddr64) -ForegroundColor Cyan
Write-Host (" Endereco Scanout InUse: 0x{0:X12}" -f $inUsePhysAddr64) -ForegroundColor Cyan

# 6. Geracao de Evidencia JSON
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $projectRoot 'docs\evidence'
}
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$evidenceDir = Join-Path $OutputRoot ("$stamp-$env:COMPUTERNAME-dcn-inventory")
New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null

$evidenceReport = [ordered]@{
    schema_version = 1
    session_type = "dcn_readonly_inventory"
    timestamp_start = $snap1Time.ToString("o")
    timestamp_end = $snap2Time.ToString("o")
    elapsed_seconds = $elapsedSeconds
    zero_write_audit = $true
    otg_metrics = [ordered]@{
        frame_count_s1 = $fc1
        frame_count_s2 = $fc2
        delta_frames = $fcDelta
        fps_estimated = $fpsEstimated
        otg_alive = ($fcDelta -gt 0)
        h_total = $hTotal
        v_total = $vTotal
    }
    scanout_metrics = [ordered]@{
        primary_surface_address_64 = ('0x{0:X12}' -f $surfPhysAddr64)
        surface_inuse_address_64 = ('0x{0:X12}' -f $inUsePhysAddr64)
        viewport_width = $vpWidth
        viewport_height = $vpHeight
        surface_config_raw = ('0x{0:X8}' -f $s1Data['mmHUBP0_DCSURF_SURFACE_CONFIG'])
        surface_pitch_raw = ('0x{0:X8}' -f $s1Data['mmHUBPREQ0_DCSURF_SURFACE_PITCH'])
    }
    snapshot_1 = $s1Data
    snapshot_2 = $s2Data
    verdict = if ($fcDelta -gt 0 -and $surfPhysAddr64 -gt 0) { "PROVADO EM SILICIO" } else { "NAO VALIDADO EM HARDWARE" }
}

$jsonPath = Join-Path $evidenceDir 'dcn_inventory.json'
$evidenceReport | ConvertTo-Json -Depth 6 | Out-File -FilePath $jsonPath -Encoding utf8
Write-Host ""
Write-Host "[EVIDENCIA] Arquivada com sucesso em:" -ForegroundColor Cyan
Write-Host "            $jsonPath" -ForegroundColor White
Write-Host " Veredito: $($evidenceReport.verdict)" -ForegroundColor $(if ($evidenceReport.verdict -eq 'PROVADO EM SILICIO') { 'Green' } else { 'Yellow' })
Write-Host "==========================================================================" -ForegroundColor Green
