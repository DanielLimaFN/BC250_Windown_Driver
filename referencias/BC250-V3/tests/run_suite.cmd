@echo off
setlocal enabledelayedexpansion
rem ===========================================================================
rem  Suite de validacao BC250-V3 (Fase 0)
rem
rem  Ordem importa. test_regmap valida o mapa de registradores; se ele falhar,
rem  todo o resto esta medindo enderecos errados.
rem
rem  Codigo de saida:
rem    0 = nenhum teste em NAO VALIDADO EM HARDWARE
rem    1 = pelo menos um teste sem as provas exigidas pela regra BC-250
rem
rem  BLOQUEADO nao conta como falha: significa pre-requisito ausente
rem  (firmware nao aceito, driver antigo), nao defeito do teste.
rem ===========================================================================

set SRCDIR=%~dp0
set BINDIR=%SRCDIR%bin
set FALHAS=0
set BLOQUEADOS=0
set PROVADOS=0
set AUSENTES=0

echo =================================================================
echo  SUITE DE VALIDACAO BC250-V3 - FASE 0
echo =================================================================
echo.

rem --- T8: detector estatico de fallback, roda sem hardware ---
echo [T8] Detector de fallback por software...
where python >nul 2>&1
if %errorlevel%==0 (
  python "%SRCDIR%check_no_fallback.py"
  if errorlevel 1 (
    echo.
    echo ABORTADO: o codigo-fonte contem fallback de software ou offsets
    echo invalidos. Corrija antes de medir qualquer coisa no hardware.
    exit /b 1
  )
) else (
  echo   python nao encontrado - T8 pulado
)
echo.

set TESTES=test_regmap test_gfxoff test_hw_info test_fw_status test_telemetry test_vram_rw test_sdma test_pm4_ring test_cp_execution test_compute test_power_profile test_opencl

for %%T in (%TESTES%) do (
  if exist "%BINDIR%\%%T.exe" (
    echo.
    echo =================================================================
    pushd "%BINDIR%"
    rem test_power_profile e' interativo e ALTERA clock/tensao. Na suite ele
    rem roda somente-leitura: uma bateria automatizada nao pode mudar o estado
    rem do silicio para os testes seguintes.
    if /I "%%T"=="test_power_profile" (
      "%BINDIR%\%%T.exe" --readonly
    ) else (
      "%BINDIR%\%%T.exe"
    )
    set RC=!errorlevel!
    popd
    if !RC!==0 (
      set RESULTADO_%%T=PROVADO
      set /a PROVADOS+=1
    ) else if !RC!==2 (
      set RESULTADO_%%T=BLOQUEADO
      set /a BLOQUEADOS+=1
    ) else (
      set RESULTADO_%%T=NAO_VALIDADO
      set /a FALHAS+=1
    )
  ) else (
    echo   [ausente] %%T.exe - rode build_tests.bat
    set RESULTADO_%%T=AUSENTE
    set /a AUSENTES+=1
  )
)

echo.
echo =================================================================
echo  MATRIZ CONSOLIDADA
echo =================================================================
for %%T in (%TESTES%) do (
  call set R=%%RESULTADO_%%T%%
  call echo   %%T ............ !R!
)
echo.
echo   PROVADO ............ %PROVADOS%
echo   BLOQUEADO .......... %BLOQUEADOS%
echo   NAO VALIDADO ....... %FALHAS%
echo   AUSENTE ............ %AUSENTES%
echo =================================================================

if %FALHAS% GTR 0 (
  echo.
  echo VEREDITO GERAL: NAO VALIDADO EM HARDWARE
  echo %FALHAS% teste^(s^) nao apresentaram as provas fisicas exigidas.
  exit /b 1
)

rem FAIL-CLOSED (AI_INSTRUCTIONS.md, secao "Testes fail-closed"):
rem a suite falha globalmente quando um teste e' pulado ou bloqueado. Um
rem pre-requisito ausente nao e' aprovacao: e' etapa nao executada, e
rem declarar etapa nao executada como concluida e' proibido.
if %BLOQUEADOS% GTR 0 (
  echo.
  echo VEREDITO GERAL: NAO VALIDADO EM HARDWARE
  echo %BLOQUEADOS% teste^(s^) BLOQUEADO^(s^) por pre-requisito ausente.
  echo Etapa nao executada nao conta como aprovada.
  exit /b 1
)

rem Um teste ausente tambem e' falha global: a suite so' vale se completa.
if %AUSENTES% GTR 0 (
  echo.
  echo VEREDITO GERAL: NAO VALIDADO EM HARDWARE
  echo %AUSENTES% teste^(s^) ausente^(s^) - rode build_tests.bat.
  exit /b 1
)

echo.
echo VEREDITO GERAL: todas as provas apresentadas.
exit /b 0
