<#
.SYNOPSIS
    Varredura Automatica de Matriz de Configuracoes e Submissoes do CP Ring 0 (GFX10.1.3 / BC-250)
.DESCRIPTION
    Testa automaticamente multiplas permutacoes de:
    - Configuracoes de Abertura e Base do Ring (GPU PA vs Local VRAM)
    - Golden Settings da Cyan Skillfish (GB_ADDR_CONFIG, GRBM_GFX_INDEX, CP_MAX_CONTEXT)
    - Valores de CP_RB0_CNTL (BUFSZ/BLKSZ 9/0, 9/7, 9/9)
    - Tipos de Pacotes PM4 (NOP, SET_UCONFIG_REG em SCRATCH, WRITE_DATA MMIO, WRITE_DATA VRAM)
#>

[CmdletBinding()]
param([switch]$Execute)

$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent $PSScriptRoot
$script:Ctl  = Join-Path $script:Root 'studio\output\bc250ctl.exe'

function Run($ArgsList) {
    $p = Start-Process -FilePath $script:Ctl -ArgumentList $ArgsList -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$env:TEMP\bc_out.txt" -RedirectStandardError "$env:TEMP\bc_err.txt"
    $out = if (Test-Path "$env:TEMP\bc_out.txt") { (Get-Content "$env:TEMP\bc_out.txt" -Raw).Trim() } else { "" }
    return $out
}

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "   VARREDURA AUTOMATICA DA MATRIZ DO COMMAND PROCESSOR RING 0    " -ForegroundColor Cyan
Write-Host "=================================================================`n" -ForegroundColor Cyan

# 1. Armar driver e capturar snapshot transacional dos registradores do CP
Run @('arm','write') | Out-Null
Run @('reg','w','0x000086D8','0x15000000') | Out-Null # HALT
$snapOut = Run @('cp','txn','snapshot')
Write-Host "      Snapshot do CP: $snapOut"

# 2. Carregar ucodes e unhalt
Write-Host "[1/6] Gravando ucodes do CP (PFP, ME, CE), IC_BASE e Jump Tables..." -ForegroundColor Yellow
$engines = @(
    @{ nome = 'pfp'; offset = '0x0E000000'; arq = 'firmware\cyan_skillfish2_pfp.bin' },
    @{ nome = 'me';  offset = '0x0E080000'; arq = 'firmware\cyan_skillfish2_me.bin' },
    @{ nome = 'ce';  offset = '0x0E100000'; arq = 'firmware\cyan_skillfish2_ce.bin' }
)

foreach ($e in $engines) {
    Run @('cp','stage',$e.nome,$e.offset,$e.arq) | Out-Null
    Run @('cp','icinval',$e.nome) | Out-Null
    Run @('cp','icbase',$e.nome,$e.offset) | Out-Null
    Run @('cp','jt',$e.nome,$e.offset,$e.arq) | Out-Null
}
Write-Host "      Ucodes, IC_BASE e Jump Tables gravados com sucesso!" -ForegroundColor Green

# 3. Aplicar Golden Settings oficiais da Cyan Skillfish 2 (GFX 10.1.3)
Write-Host "[2/6] Aplicando Golden Settings do Cyan Skillfish 2..." -ForegroundColor Yellow
Run @('reg','w','0x00008088','0x00000000') | Out-Null # mmGRBM_GFX_CNTL: Pipe 0, ME 0
Run @('reg','w','0x0000802C','0xE0000000') | Out-Null # mmGRBM_GFX_INDEX: Broadcast all SEs/SHs
Run @('reg','w','0x00009800','0x00100044') | Out-Null # mmGB_ADDR_CONFIG: Cyan Skillfish golden
Run @('reg','w','0x0000C12C','0x00000001') | Out-Null # mmCP_DEVICE_ID = 1
Run @('reg','w','0x0000C2B8','0x00000007') | Out-Null # mmCP_MAX_CONTEXT = 7
Run @('reg','w','0x0000C144','0x00000000') | Out-Null # mmCP_RB_VMID = 0
Run @('reg','w','0x0000C128','0x00000000') | Out-Null # mmCP_RB_WPTR_DELAY = 0
Run @('reg','w','0x00008708','0x00000000') | Out-Null # mmCP_RB_WPTR_POLL_CNTL = 0 (MMIO direto)

# Marca de teste no SCRATCH_REG0 (0x032D4 / mmSCRATCH_REG0)
Run @('reg','w','0x000032D4','0x11111111') | Out-Null
$scratchBefore = Run @('reg','r','0x000032D4')
Write-Host "      SCRATCH_REG0 (0x032D4) inicial = $scratchBefore"

# 4. Matriz de Testes de Inicializacao e Submissao
$rbCntlVariants = @(
    @{ Name = "CANONICAL_9_7 (BUFSZ=9, BLKSZ=7)"; Val = 0x00000709 },
    @{ Name = "CANONICAL_9_0 (BUFSZ=9, BLKSZ=0)"; Val = 0x00000009 },
    @{ Name = "CANONICAL_9_9 (BUFSZ=9, BLKSZ=9)"; Val = 0x00000909 }
)

$baseVariants = @(
    @{ Name = "GPU_PHYSICAL_BASE (0xF40E2000)"; Off = 0x0E200000; Base = 0xF40E2000 },
    @{ Name = "VRAM_LOCAL_OFFSET (0x000E2000)"; Off = 0x0E200000; Base = 0x000E2000 }
)

$successCount = 0
$totalTests   = 0

foreach ($b in $baseVariants) {
    foreach ($c in $rbCntlVariants) {
        $totalTests++
        Write-Host "`n-----------------------------------------------------------------" -ForegroundColor Gray
        Write-Host " [TESTE $totalTests] Base: $($b.Name) | CNTL: $($c.Name)" -ForegroundColor Cyan
        Write-Host "-----------------------------------------------------------------" -ForegroundColor Gray

        # Inicializa Ring
        $initOut = Run @('gfx','init',('0x{0:X8}' -f $b.Off),'4096')
        
        # Ajusta base e cntl especificos da permutacao
        Run @('reg','w','0x0000C100',('0x{0:X8}' -f $b.Base)) | Out-Null
        Run @('reg','w','0x0000C104',('0x{0:X8}' -f $c.Val)) | Out-Null
        Run @('reg','w','0x0000C680','0x00000001') | Out-Null
        Run @('reg','w','0x000086D8','0x00000000') | Out-Null # Unhalt

        # 0. Teste GFX10 Boot (SET_BASE + CONTEXT_CONTROL + CLEAR_STATE)
        Write-Host "   > Submissao 0: GFX10 BOOT (SET_BASE + CONTEXT_CONTROL + CLEAR_STATE)..." -NoNewline
        $sub0 = Run @('gfx','boot')
        $rptr0 = Run @('reg','r','0x00008700')
        $wptr0 = Run @('reg','r','0x0000C150')
        if ($rptr0 -eq $wptr0 -and $wptr0 -ne '0x00000000') {
            Write-Host " [SUCESSO! RPTR=$rptr0 WPTR=$wptr0]" -ForegroundColor Green
            $successCount++
            break
        } else {
            Write-Host " (RPTR=$rptr0 WPTR=$wptr0)" -ForegroundColor DarkGray
        }

        # 1. Teste SET_UCONFIG_REG em SCRATCH_REG0 (0x32D4)
        Write-Host "   > Submissao 1: SET_UCONFIG_REG (0xDEADBEEF em SCRATCH_REG0)..." -NoNewline
        $sub1 = Run @('gfx','set_uconfig','0x32D4','0xDEADBEEF')
        $scAfter1 = Run @('reg','r','0x000032D4')
        $rptr1 = Run @('reg','r','0x00008700')
        $wptr1 = Run @('reg','r','0x0000C150')
        if ($scAfter1 -match 'DEADBEEF' -or ($rptr1 -eq $wptr1 -and $wptr1 -ne '0x00000000')) {
            Write-Host " [SUCESSO! RPTR=$rptr1 WPTR=$wptr1 SCRATCH=$scAfter1]" -ForegroundColor Green
            $successCount++
            break
        } else {
            Write-Host " (RPTR=$rptr1 WPTR=$wptr1 SCRATCH=$scAfter1)" -ForegroundColor DarkGray
        }

        # 2. Teste WRITE_DATA MMIO em SCRATCH_REG0 (0x32D4)
        Write-Host "   > Submissao 2: WRITE_DATA MMIO (0x22222222 em SCRATCH_REG0)..." -NoNewline
        $sub2 = Run @('gfx','write_reg','0x32D4','0x22222222')
        $scAfter2 = Run @('reg','r','0x000032D4')
        $rptr2 = Run @('reg','r','0x00008700')
        $wptr2 = Run @('reg','r','0x0000C150')
        if ($scAfter2 -match '22222222' -or ($rptr2 -eq $wptr2 -and $wptr2 -ne '0x00000000')) {
            Write-Host " [SUCESSO! RPTR=$rptr2 WPTR=$wptr2 SCRATCH=$scAfter2]" -ForegroundColor Green
            $successCount++
            break
        } else {
            Write-Host " (RPTR=$rptr2 WPTR=$wptr2 SCRATCH=$scAfter2)" -ForegroundColor DarkGray
        }

        # 3. Teste WRITE_DATA VRAM
        Write-Host "   > Submissao 3: WRITE_DATA VRAM (0xCAFEBABE na fence)..." -NoNewline
        Run @('vram','w','0x0E201200','0x11111111') | Out-Null
        $sub3 = Run @('gfx','writedata','0x0E201200','0xCAFEBABE')
        $fenceVal = Run @('vram','r','0x0E201200')
        $rptr3 = Run @('reg','r','0x00008700')
        $wptr3 = Run @('reg','r','0x0000C150')
        if ($fenceVal -match 'CAFEBABE' -or ($rptr3 -eq $wptr3 -and $wptr3 -ne '0x00000000')) {
            Write-Host " [SUCESSO! RPTR=$rptr3 WPTR=$wptr3 FENCE=$fenceVal]" -ForegroundColor Green
            $successCount++
            break
        } else {
            Write-Host " (RPTR=$rptr3 WPTR=$wptr3 FENCE=$fenceVal)" -ForegroundColor DarkGray
        }

        # 4. Teste NOPs
        Write-Host "   > Submissao 4: 4x NOPs..." -NoNewline
        $sub4 = Run @('gfx','nop','4')
        $rptr4 = Run @('reg','r','0x00008700')
        $wptr4 = Run @('reg','r','0x0000C150')
        if ($rptr4 -eq $wptr4 -and $wptr4 -ne '0x00000000') {
            Write-Host " [SUCESSO! RPTR=$rptr4 WPTR=$wptr4]" -ForegroundColor Green
            $successCount++
            break
        } else {
            Write-Host " (RPTR=$rptr4 WPTR=$wptr4)" -ForegroundColor DarkGray
        }

        # Restaura HALT antes da proxima permutacao
        Run @('reg','w','0x000086D8','0x15000000') | Out-Null
    }
    if ($successCount -gt 0) { break }
}

# Restaura estado seguro e rollback ao finalizar
Run @('reg','w','0x000086D8','0x15000000') | Out-Null
Run @('reg','w','0x000032D4','0x00000000') | Out-Null
Run @('cp','rollback','all') | Out-Null
Run @('cp','txn','restore') | Out-Null

Write-Host "`n=================================================================" -ForegroundColor Cyan
Write-Host "               RELATORIO FINAL DA VARREDURA                      " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
if ($successCount -gt 0) {
    Write-Host ">>> SUCESSO ABSOLUTO! O Command Processor executou os pacotes PM4!" -ForegroundColor Green
} else {
    Write-Host "Varredura concluida. Inspecione as leituras de RPTR/WPTR de cada permutacao." -ForegroundColor Yellow
}
