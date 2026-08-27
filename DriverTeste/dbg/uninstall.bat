@echo off
:: uninstall.bat - para e remove o bc250dbg.sys. Executar como Administrador.
setlocal
net session >nul 2>&1
if errorlevel 1 (
  echo ERRO: rode como Administrador.
  exit /b 1
)

sc stop bc250dbg >nul 2>&1
sc delete bc250dbg >nul 2>&1
del /f /q "%SystemRoot%\System32\drivers\bc250dbg.sys" >nul 2>&1
echo OK: bc250dbg removido.
endlocal
