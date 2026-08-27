@echo off
:: Inicia o driver de laboratorio ja instalado, sem copiar, reinstalar ou
:: alterar sua configuracao. Executar como Administrador apos cada boot.

net session >nul 2>&1
if errorlevel 1 (
  echo ERRO: rode como Administrador.
  exit /b 1
)

sc query bc250dbg >nul 2>&1
if errorlevel 1 (
  echo ERRO: servico bc250dbg nao esta instalado.
  echo       Rode dbg\install.bat uma vez.
  exit /b 1
)

sc query bc250dbg | findstr /C:" 4  RUNNING" >nul
if not errorlevel 1 (
  echo OK: bc250dbg ja esta em execucao.
  exit /b 0
)

echo [*] iniciando bc250dbg
sc start bc250dbg
if errorlevel 1 (
  echo ERRO: nao foi possivel iniciar o driver.
  exit /b 1
)

echo OK. Dispositivo: \\.\bc250dbg ^(readonly^)
exit /b 0
