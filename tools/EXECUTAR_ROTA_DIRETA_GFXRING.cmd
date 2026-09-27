@echo off
setlocal EnableDelayedExpansion
echo ========================================================
echo   BC-250: ENSAIO DA ROTA DIRETA (ANEL GRAFICO CP_RB0)
echo ========================================================
echo.
echo 0. Verificando se o rollback anterior ja foi seguido por boot frio...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$marker='C:\BC250_REQUIRE_CLEAN_BOOT.json'; if (-not (Test-Path -LiteralPath $marker)) { exit 0 }; $m=Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json; $boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks; if ([int64]$m.rollback_boot_ticks -eq [int64]$boot) { exit 42 }; exit 0"
if %ERRORLEVEL% equ 42 (
    echo [RECUSADO] O rollback anterior exige BOOT FRIO antes de outro binding.
    echo Desligue completamente a maquina, ligue novamente e execute este arquivo outra vez.
    echo O fusivel nao foi alterado e o adaptador nao foi vinculado.
    pause
    exit /b 42
)
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Nao foi possivel verificar o marcador de boot limpo.
    pause
    exit /b 1
)
echo [OK] Preflight de boot frio aprovado.
echo.
echo 1. Resetando fusivel de boot C:\BC250_BOOT_FUSE.dat...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0inspect-boot-fuse.ps1" -Reset
if %ERRORLEVEL% neq 0 (
    echo [ERRO] Falha ao resetar o fusivel. Certifique-se de executar como Administrador!
    pause
    exit /b 1
)
echo.

echo Escolha o modo de ativacao do Anel Grafico (CP_RB0):
echo   [0] Modo Producao Direto: Ativacao persistente do CP_RB0 sem teste previo [RECOMENDADO]
echo   [1] Nivel 2: Bringup Test (Unhalt PFP/ME + CP_RB0) + Ativacao Persistente
echo   [2] Nivel 3: Submissao de Shaders VS+PS (SET_SH_REG 12 DW + Tripe) + Ativacao Persistente
echo   [3] Nivel 4: Isolamento Progressivo de Estado 3D (Sub-etapas 4.1 a 4.4)
echo   [4] Nivel 5: Pipeline 3D Completo com DRAW_INDEX_AUTO e Byte Proof
echo.
set "CHOICE=0"
set /p "CHOICE=Digite a opcao [0-4] (Padrao: 0): "

if "%CHOICE%"=="0" (
    set "GFX_LEVEL=0"
    set "GFX_DESC=Modo Producao Direto (CP_RB0 persistente ativo)"
) else if "%CHOICE%"=="1" (
    set "GFX_LEVEL=2"
    set "GFX_DESC=Nivel 2 (Partida e Desalt PFP/ME + Persistente)"
) else if "%CHOICE%"=="2" (
    set "GFX_LEVEL=3"
    set "GFX_DESC=Nivel 3 (Shaders VS+PS + Tripe)"
) else if "%CHOICE%"=="3" (
    set "GFX_LEVEL=4"
    set "GFX_DESC=Nivel 4 (Isolamento Progressivo 3D)"
) else if "%CHOICE%"=="4" (
    set "GFX_LEVEL=5"
    set "GFX_DESC=Nivel 5 (Pipeline 3D DRAW_INDEX_AUTO)"
) else (
    set "GFX_LEVEL=0"
    set "GFX_DESC=Modo Producao Direto (CP_RB0 persistente ativo)"
)

echo.
echo ========================================================
echo   EXECUTANDO: %GFX_DESC%
echo   GfxRingBringupLevel = %GFX_LEVEL%
echo ========================================================
echo.

echo 2. Iniciando vinculacao WDDM de laboratorio com ensaio do anel grafico...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bind-bc250mcdm-lab.ps1" -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -GfxRingBringupLevel %GFX_LEVEL% -GfxRingStage 4 -WatchdogTimeoutSeconds 180 -IAcceptAutomaticRecoveryReboot
set "BIND_EXIT=%ERRORLEVEL%"
echo.
echo 3. Executando rollback incondicional do ensaio automatizado...
call "%~dp0BC250_RECOVERY.cmd"
set "RECOVERY_EXIT=%ERRORLEVEL%"
if not "%RECOVERY_EXIT%"=="0" (
    echo [ERRO] Rollback incompleto, exit=%RECOVERY_EXIT%. Nao execute outro ensaio.
    pause
    exit /b %RECOVERY_EXIT%
)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m=[ordered]@{state='CLEAN_BOOT_REQUIRED';rollback_boot_ticks=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().Ticks;rollback_completed_at=(Get-Date).ToString('o');restored_service='BasicDisplay';restored_code=0;hardware_validated=$false}; $m | ConvertTo-Json | Set-Content -LiteralPath 'C:\BC250_REQUIRE_CLEAN_BOOT.json' -Encoding UTF8"
if %ERRORLEVEL% neq 0 (
    echo [ERRO] BasicDisplay voltou, mas o marcador de boot frio nao foi gravado.
    pause
    exit /b 1
)
echo [OK] BasicDisplay Codigo 0 restaurado. Boot frio obrigatorio antes de outro ensaio.
if not "%BIND_EXIT%"=="0" echo [AVISO] O ensaio terminou com exit=%BIND_EXIT%; a recuperacao foi concluida.

echo.
echo ========================================================
echo   RESUMO DO ENSAIO EXTRAIDO DA EVIDENCIA
echo ========================================================
powershell -NoProfile -ExecutionPolicy Bypass -Command "$last=Join-Path '%~dp0' '_last_lab_binding.json'; if (Test-Path $last) { $j=Get-Content $last -Raw | ConvertFrom-Json; $log=Join-Path $j.evidence_dir 'display-log-this-bind.txt'; if (Test-Path $log) { Select-String -Path $log -Pattern '\[GfxRing\]' -SimpleMatch | ForEach-Object { Write-Host ('  ' + $_.Line.Trim()) -ForegroundColor Cyan } } }"
echo ========================================================
echo.
echo Concluido. Pressione qualquer tecla para sair...
pause
exit /b %BIND_EXIT%
