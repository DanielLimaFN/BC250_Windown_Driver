@echo off
:: install.bat - instala e inicia o bc250dbg.sys na maquina ALVO.
:: Executar como Administrador.
::
:: Pre-requisitos da maquina alvo (checklist do roadmap, secao 9):
::   bcdedit /set testsigning on
::   bcdedit /set hypervisorlaunchtype off       (VBS desligado)
::   reboot depois de mexer em qualquer um dos itens acima.
::
:: Nao altere IOMMU/BIOS por presuncao. O laboratorio deve registrar a
:: configuracao real e parar se o BAR nao responder, sem atribuir a causa.

setlocal
net session >nul 2>&1
if errorlevel 1 (
  echo ERRO: rode como Administrador.
  exit /b 1
)

set SRC=%~dp0..\output\bc250dbg.sys
if not exist "%SRC%" (
  echo ERRO: %SRC% nao existe. Rode dbg\build.bat primeiro.
  exit /b 1
)

set DST=%SystemRoot%\System32\drivers\bc250dbg.sys

:: O certificado de teste precisa estar confiavel NA MAQUINA, senao o driver
:: assina, copia, e falha ao carregar com um erro que nao explica o motivo.
certutil -store Root 2>nul | findstr /i "BC250-WIN" >nul
if errorlevel 1 (
  echo.
  echo AVISO: o certificado "BC250-WIN Test" nao esta em LocalMachine\Root.
  echo        O driver provavelmente nao vai carregar. Rode antes, como admin:
  echo          powershell -ExecutionPolicy Bypass -File dbg\make-testcert.ps1
  echo.
)

:: testsigning ligado?
bcdedit /enum {current} | findstr /i "testsigning" | findstr /i "Yes On Sim" >nul
if errorlevel 1 (
  echo AVISO: testsigning parece desligado. Rode:
  echo          bcdedit /set testsigning on     ^(e reinicie^)
  echo.
)

sc query bc250dbg >nul 2>&1
if not errorlevel 1 (
  echo [*] servico existente: parando
  sc stop bc250dbg >nul 2>&1
  sc delete bc250dbg >nul 2>&1
  timeout /t 2 /nobreak >nul
)

echo [*] copiando driver
copy /Y "%SRC%" "%DST%" >nul
if errorlevel 1 (
  echo ERRO: copia falhou.
  exit /b 1
)

echo [*] criando servico
sc create bc250dbg type= kernel start= demand binPath= "%DST%" DisplayName= "BC-250 instrumentation" >nul
if errorlevel 1 (
  echo ERRO: sc create falhou.
  exit /b 1
)

echo [*] iniciando
sc start bc250dbg
if errorlevel 1 (
  echo.
  echo ERRO ao iniciar. Causas mais comuns:
  echo   - testsigning desligado    ^(bcdedit /set testsigning on + reboot^)
  echo   - driver sem assinatura    ^(ver dbg\build.bat^)
  echo   - HVCI/VBS ligado          ^(bcdedit /set hypervisorlaunchtype off^)
  exit /b 1
)

echo.
echo OK. Dispositivo: \\.\bc250dbg   ^(nivel de armamento: readonly^)
echo Teste:  studio\output\bc250ctl.exe info
endlocal
