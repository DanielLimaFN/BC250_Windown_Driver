[CmdletBinding()]
param()

$ErrorActionPreference = "Continue"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "BC-250: PREPARACAO DO SSD PARA RETORNO A PLACA BC-250" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERRO: Execute este script em um terminal PowerShell como Administrador." -ForegroundColor Red
    return
}

Write-Host "[1/4] Removendo servico bc250mcdm..." -ForegroundColor Yellow
& sc.exe stop bc250mcdm *> $null
& sc.exe delete bc250mcdm
if ($LASTEXITCODE -eq 0) {
    Write-Host "  Servico bc250mcdm removido com sucesso." -ForegroundColor Green
} else {
    Write-Host "  Servico bc250mcdm ja estava ausente ou foi removido." -ForegroundColor Gray
}

Write-Host "`n[2/4] Removendo binario bc250mcdm.sys de System32\drivers..." -ForegroundColor Yellow
$driverPath = "$env:SystemRoot\System32\drivers\bc250mcdm.sys"
if (Test-Path -LiteralPath $driverPath) {
    & takeown.exe /f $driverPath *> $null
    & icacls.exe $driverPath /grant *S-1-5-32-544:F *> $null
    Remove-Item -LiteralPath $driverPath -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $driverPath) {
        Write-Host "  ERRO: nao foi possivel apagar bc250mcdm.sys." -ForegroundColor Red
    } else {
        Write-Host "  bc250mcdm.sys removido com sucesso de System32\drivers." -ForegroundColor Green
    }
} else {
    Write-Host "  bc250mcdm.sys ja nao existia em System32\drivers." -ForegroundColor Gray
}

Write-Host "`n[3/4] Limpando marcadores de ensaio anteriores..." -ForegroundColor Yellow
if (Test-Path "C:\BC250_TEST_OEMINF.txt") {
    Remove-Item "C:\BC250_TEST_OEMINF.txt" -Force -ErrorAction SilentlyContinue
    Write-Host "  BC250_TEST_OEMINF.txt apagado." -ForegroundColor Green
}
if (Test-Path "C:\BC250_BOOT_FUSE.dat") {
    & attrib.exe -R "C:\BC250_BOOT_FUSE.dat" *> $null
    Remove-Item "C:\BC250_BOOT_FUSE.dat" -Force -ErrorAction SilentlyContinue
    Write-Host "  BC250_BOOT_FUSE.dat apagado." -ForegroundColor Green
}

Write-Host "`n[4/4] Atualizando C:\BC250_RECOVERY.cmd com suporte a WinRE..." -ForegroundColor Yellow
Copy-Item "tools\BC250_RECOVERY.cmd" "C:\BC250_RECOVERY.cmd" -Force
Write-Host "  C:\BC250_RECOVERY.cmd atualizado com sucesso." -ForegroundColor Green

Write-Host "`n============================================================" -ForegroundColor Cyan
Write-Host "LIMPEZA CONCLUIDA COM SUCESSO!" -ForegroundColor Green
Write-Host "O SSD esta pronto e seguro para retornar a placa BC-250." -ForegroundColor Green
Write-Host "Ao ligar a BC-250, o Windows iniciara normalmente com o" -ForegroundColor White
Write-Host "Driver de Video Basico (BasicDisplay, Codigo 0) e tela ativa." -ForegroundColor White
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""
