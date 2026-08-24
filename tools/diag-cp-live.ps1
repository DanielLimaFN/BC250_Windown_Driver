<#
.SYNOPSIS
    Diagnostico em Tempo Real dos Ponteiros de Instrucao e Ring do CP
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
Write-Host "   DIAGNOSTICO EM TEMPO REAL: COMMAND PROCESSOR GFX10            " -ForegroundColor Cyan
Write-Host "=================================================================`n" -ForegroundColor Cyan

# 1. Armar driver e capturar snapshot
Run @('arm','write') | Out-Null
Run @('reg','w','0x000086D8','0x15000000') | Out-Null # HALT
$snap = Run @('cp','txn','snapshot')

# 2. Carregar ucodes e unhalt
Write-Host "[1/4] Carregando Microcodigos, IC_BASE e Jump Tables..." -ForegroundColor Yellow
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

# 3. Inicializar Ring 0
Write-Host "[2/4] Inicializando Ring 0 e Golden Settings..." -ForegroundColor Yellow
Run @('gfx','init','0x0E200000','4096') | Out-Null
Run @('reg','w','0x00008088','0x00000000') | Out-Null # Pipe 0
Run @('reg','w','0x0000802C','0xE0000000') | Out-Null # Broadcast
Run @('reg','w','0x00009800','0x00100044') | Out-Null # Golden GB_ADDR_CONFIG
Run @('reg','w','0x0000C12C','0x00000001') | Out-Null # mmCP_DEVICE_ID = 1
Run @('reg','w','0x0000C2B8','0x00000007') | Out-Null # mmCP_MAX_CONTEXT = 7
Run @('reg','w','0x0000C144','0x00000000') | Out-Null # mmCP_RB_VMID = 0
Run @('reg','w','0x0000C128','0x00000000') | Out-Null # mmCP_RB_WPTR_DELAY = 0
Run @('reg','w','0x00008708','0x00000000') | Out-Null # mmCP_RB_WPTR_POLL_CNTL = 0 (MMIO)
Run @('reg','w','0x0000C104','0x00000709') | Out-Null # BUFSZ=9, BLKSZ=7
Run @('reg','w','0x0000C680','0x00000001') | Out-Null # RB0_ACTIVE = 1

# 4. Amostrar registradores em UNHALT
Write-Host "[3/4] Desbloqueando Microengines (UNHALT)..." -ForegroundColor Yellow
Run @('reg','w','0x000086D8','0x00000000') | Out-Null

$pfpPntr = Run @('reg','r','0x00008694')
$mePntr  = Run @('reg','r','0x00008698')
$cePntr  = Run @('reg','r','0x0000869C')
$cpStat  = Run @('reg','r','0x00008680')
$grbm    = Run @('reg','r','0x00008010')
$rptr    = Run @('reg','r','0x00008700')
$wptr    = Run @('reg','r','0x0000C150')

Write-Host "      Estado das Microengines Apos Unhalt:" -ForegroundColor Cyan
Write-Host "        PFP_INSTR_PNTR : $pfpPntr"
Write-Host "        ME_INSTR_PNTR  : $mePntr"
Write-Host "        CE_INSTR_PNTR  : $cePntr"
Write-Host "        CP_STAT        : $cpStat"
Write-Host "        GRBM_STATUS    : $grbm"
Write-Host "        CP_RB0_RPTR    : $rptr"
Write-Host "        CP_RB0_WPTR    : $wptr"

# 5. Enviar Sequencia de Boot e Teste
Write-Host "`n[4/4] Submetendo Pacote PM4 Boot + NOP no Ring 0..." -ForegroundColor Yellow
$bootOut = Run @('gfx','boot')
Write-Host "      $bootOut"

$rptrAfter = Run @('reg','r','0x00008700')
$wptrAfter = Run @('reg','r','0x0000C150')
$pfpAfter  = Run @('reg','r','0x00008694')
$meAfter   = Run @('reg','r','0x00008698')
$cpStatAft = Run @('reg','r','0x00008680')
$faultStat = Run @('reg','r','0x0000A130')
$faultLo   = Run @('reg','r','0x0000A134')
$faultHi   = Run @('reg','r','0x0000A138')

Write-Host "`n      Estado Apos Submissao:" -ForegroundColor Cyan
Write-Host "        CP_RB0_RPTR    : $rptrAfter"
Write-Host "        CP_RB0_WPTR    : $wptrAfter"
Write-Host "        PFP_INSTR_PNTR : $pfpAfter"
Write-Host "        ME_INSTR_PNTR  : $meAfter"
Write-Host "        CP_STAT        : $cpStatAft"
Write-Host "        MMU_FAULT_STAT : $faultStat"
Write-Host "        MMU_FAULT_ADDR : $faultHi $faultLo"

# 6. Limpeza e Rollback
Run @('reg','w','0x000086D8','0x15000000') | Out-Null
Run @('cp','rollback','all') | Out-Null
Run @('cp','txn','restore') | Out-Null
Write-Host "`nRollback transacional concluido com sucesso." -ForegroundColor Green
