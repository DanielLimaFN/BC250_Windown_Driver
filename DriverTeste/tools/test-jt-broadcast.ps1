# Teste de Latch da Jump Table com Pipeline Completo (RLC + GCVM/VRAM + IC_BASE + JT)
param([switch]$Execute)

$root = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $root 'studio\output\bc250ctl.exe'

function Run-Cmd([string[]]$CmdArgs) {
    & $cli $CmdArgs
}

Write-Host "1. Arm write"
Run-Cmd @('arm','write')

Write-Host "`n2. RLC Bring-up (Stop -> CGCG/PG Clean -> PSP Load -> Start)"
Run-Cmd @('rlc','txn','snapshot')
Run-Cmd @('rlc','stop')
Run-Cmd @('rlc','cgcg')
Run-Cmd @('rlc','pg')
Run-Cmd @('psp','init')
Run-Cmd @('psp','loadfw','rlc',(Join-Path $root 'firmware\cyan_skillfish2_rlc.bin'))
Run-Cmd @('rlc','start')
Run-Cmd @('rlc','status')

Write-Host "`n3. CP Snapshot + Clocks + Golden Registers"
Run-Cmd @('cp','txn','snapshot')
Run-Cmd @('cp','clocks')
Run-Cmd @('cp','golden')

Write-Host "`n4. Carga dos 4 ucodes na VRAM (Stage)"
Run-Cmd @('cp','stage','pfp','0x0E000000','firmware\cyan_skillfish2_pfp.bin')
Run-Cmd @('cp','stage','me','0x0E080000','firmware\cyan_skillfish2_me.bin')
Run-Cmd @('cp','stage','ce','0x0E100000','firmware\cyan_skillfish2_ce.bin')
Run-Cmd @('cp','stage','mec','0x0E140000','firmware\cyan_skillfish2_mec.bin')

Write-Host "`n5. Apontamento de IC_BASE + I-Cache Prime (0x11)"
Run-Cmd @('cp','icinval','pfp')
Run-Cmd @('cp','icbase','pfp','0x0E000000')
Run-Cmd @('cp','icinval','me')
Run-Cmd @('cp','icbase','me','0x0E080000')
Run-Cmd @('cp','icinval','ce')
Run-Cmd @('cp','icbase','ce','0x0E100000')
Run-Cmd @('cp','icinval','mec')
Run-Cmd @('cp','icbase','mec','0x0E140000')

Write-Host "`n6. Carga da Jump Table (0x3E050..0x3E068)"
Run-Cmd @('cp','jt','pfp','0x0E000000','firmware\cyan_skillfish2_pfp.bin')
Run-Cmd @('cp','jt','me','0x0E080000','firmware\cyan_skillfish2_me.bin')
Run-Cmd @('cp','jt','ce','0x0E100000','firmware\cyan_skillfish2_ce.bin')
Run-Cmd @('cp','jt','mec','0x0E140000','firmware\cyan_skillfish2_mec.bin')

Write-Host "`n7. Readback do Status da Jump Table (antes do unhalt)"
Write-Host "CP_PFP_JT_STAT (0x0C4CC):"
Run-Cmd @('reg','r','0x0C4CC')
Write-Host "CP_CE_JT_STAT  (0x0C4D0):"
Run-Cmd @('reg','r','0x0C4D0')
Write-Host "CP_MEC_JT_STAT (0x0C4D4):"
Run-Cmd @('reg','r','0x0C4D4')

Write-Host "`n8. Unhalt das Microengines (CP_ME_CNTL = 0) e Amostragem"
Run-Cmd @('cp','unhalt')
Start-Sleep -Milliseconds 100
Run-Cmd @('cp','sample')

Write-Host "`n9. Readback do Status da Jump Table (apos o unhalt)"
Write-Host "CP_PFP_JT_STAT (0x0C4CC):"
Run-Cmd @('reg','r','0x0C4CC')
Write-Host "CP_CE_JT_STAT  (0x0C4D0):"
Run-Cmd @('reg','r','0x0C4D0')
Write-Host "CP_MEC_JT_STAT (0x0C4D4):"
Run-Cmd @('reg','r','0x0C4D4')

Write-Host "`n10. Teardown Seguro (Halt + Restore)"
Run-Cmd @('cp','halt')
Run-Cmd @('cp','txn','restore')
Run-Cmd @('rlc','txn','restore')
Run-Cmd @('arm','readonly')
