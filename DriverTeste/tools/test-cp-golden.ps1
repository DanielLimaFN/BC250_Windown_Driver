<#
.SYNOPSIS
    Teste de Silicio: Command Processor Ring 0 com Golden Settings Completos do Cyan Skillfish
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
Write-Host "   TESTE DO CP RING 0 COM GOLDEN SETTINGS DO CYAN SKILLFISH      " -ForegroundColor Cyan
Write-Host "=================================================================`n" -ForegroundColor Cyan

# 1. Armar driver e capturar snapshot
Run @('arm','write') | Out-Null
Run @('reg','w','0x000086D8','0x15000000') | Out-Null # HALT
$snap = Run @('cp','txn','snapshot')

# 2. Carregar firmware RLC via PSP e Microcodigos do CP via Direct Load
Write-Host "[1/6] Carregando Firmware RLC pelo PSP..." -ForegroundColor Yellow
Run @('psp','init') | Out-Null
$rlcOut = Run @('psp','loadfw','rlc','firmware\cyan_skillfish2_rlc.bin')
Write-Host "      RLC via PSP: $rlcOut"
Run @('psp','rollback') | Out-Null

Write-Host "[2/6] Carregando Microcodigos do CP (PFP, ME, CE), IC_BASE e Jump Tables..." -ForegroundColor Yellow
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

# 3. Aplicar Golden Settings Oficiais do Cyan Skillfish (Upstream gfx_v10_0.c) e Ativar RLC
Write-Host "[3/6] Aplicando Golden Settings, Ativando RLC e Desarmando SAFE_MODE..." -ForegroundColor Yellow
Run @('reg','w','0x00017980','0x00000001') | Out-Null # mmRLC_CNTL = 1 (RLC_ENABLE_F32)
Run @('reg','w','0x00017994','0x00000000') | Out-Null # mmRLC_SAFE_MODE = 0 (Unset SAFE_MODE)
Run @('reg','w','0x0000802C','0xE0000000') | Out-Null # mmGRBM_GFX_INDEX (Broadcast)
Run @('reg','w','0x00008088','0x00000000') | Out-Null # mmGRBM_GFX_CNTL (Pipe 0)
Run @('reg','w','0x00018C44','0xF8000100') | Out-Null # mmCGTT_CPF_CLK_CTRL (CPF Clock Un-gate)
Run @('reg','w','0x00018C48','0x3C000100') | Out-Null # mmCGTT_SPI_CLK_CTRL
Run @('reg','w','0x00018C4C','0x60000100') | Out-Null # mmCGTT_SQ_CLK_CTRL
Run @('reg','w','0x00018C50','0x40000100') | Out-Null # mmCGTT_SQG_CLK_CTRL
Run @('reg','w','0x00018C54','0xFFFF8100') | Out-Null # mmCGTT_VGT_CLK_CTRL
Run @('reg','w','0x00018C58','0xFEFF8100') | Out-Null # mmCGTT_WD_CLK_CTRL
Run @('reg','w','0x0000C6DC','0x000005FF') | Out-Null # mmCP_SD_CNTL
Run @('reg','w','0x00009F8C','0x00008500') | Out-Null # mmGCR_GENERAL_CNTL
Run @('reg','w','0x00009800','0x00000044') | Out-Null # mmGB_ADDR_CONFIG
Run @('reg','w','0x00008C34','0x00000101') | Out-Null # mmSH_MEM_CONFIG (64-bit unaligned)
Run @('reg','w','0x0001B9C0','0x00000008') | Out-Null # mmCPG_PSP_DEBUG (GPA override / Direct Physical)
Run @('reg','w','0x0001B9C4','0x00000008') | Out-Null # mmCPC_PSP_DEBUG (GPA override / Direct Physical)
Run @('reg','w','0x0000C12C','0x00000001') | Out-Null # mmCP_DEVICE_ID = 1
Run @('reg','w','0x0000C2B8','0x00000007') | Out-Null # mmCP_MAX_CONTEXT = 7
Run @('reg','w','0x0000C144','0x00000000') | Out-Null # mmCP_RB_VMID = 0
Run @('reg','w','0x0000C128','0x00000000') | Out-Null # mmCP_RB_WPTR_DELAY = 0
Run @('reg','w','0x0000C168','0x00000000') | Out-Null # mmCP_RB_DOORBELL_RANGE_LOWER
Run @('reg','w','0x0000C16C','0x000003FE') | Out-Null # mmCP_RB_DOORBELL_RANGE_UPPER

# 4. Inicializar Ring 0
Write-Host "[4/6] Inicializando Ring 0 (4 KB na VRAM 0x0E200000)..." -ForegroundColor Yellow
Run @('gfx','init','0x0E200000','4096') | Out-Null

# 5. Descongelar Microengines
Write-Host "[5/6] Desbloqueando Microengines (UNHALT)..." -ForegroundColor Yellow
Run @('reg','w','0x000086D8','0x00000000') | Out-Null

# 6. Submissao de Boot e NOPs
Write-Host "[6/6] Submetendo Sequencia Canônica de Boot GFX10..." -ForegroundColor Yellow
$bootOut = Run @('gfx','boot')
Write-Host "      $bootOut"

$rptr = Run @('reg','r','0x00008700')
$wptr = Run @('reg','r','0x0000C150')
$pfp  = Run @('reg','r','0x00008694')
$me   = Run @('reg','r','0x00008698')
$stat = Run @('reg','r','0x00008680')

Write-Host "`n=================================================================" -ForegroundColor Cyan
Write-Host "                  RESULTADO DO ENSAIO                            " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "  CP_RB0_RPTR    : $rptr"
Write-Host "  CP_RB0_WPTR    : $wptr"
Write-Host "  PFP_INSTR_PNTR : $pfp"
Write-Host "  ME_INSTR_PNTR  : $me"
Write-Host "  CP_STAT        : $stat"

if ($rptr -eq $wptr -and $wptr -ne '0x00000000') {
    Write-Host "`n>>> SUCESSO ABSOLUTO NO SILICIO! O CP CONSUMIU O ANEL! <<<" -ForegroundColor Green
} else {
    Write-Host "`nAmostragem concluida. Inspecione os registradores acima." -ForegroundColor Yellow
}

# 7. Limpeza e Rollback
Run @('reg','w','0x000086D8','0x15000000') | Out-Null
Run @('cp','rollback','all') | Out-Null
Run @('cp','txn','restore') | Out-Null
Write-Host "Rollback transacional concluido." -ForegroundColor Green
