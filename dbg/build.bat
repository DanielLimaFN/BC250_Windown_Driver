@echo off
setlocal enabledelayedexpansion
:: build.bat - compila bc250dbg.sys (driver de instrumentacao)
::
:: Requisitos na maquina de desenvolvimento:
::   - Visual Studio 2026 (Community/Professional/BuildTools) com toolset x64
::   - SDK/WDK 10.0.28000.0
::
:: Saida: output\bc250dbg.sys
::
:: O driver NAO e assinado por uma CA: a maquina alvo precisa de testsigning.
:: Ver install.bat.

set SRCDIR=%~dp0
set ODIR=%~dp0..\output
set OBJDIR=%ODIR%\obj
if not exist "%ODIR%" mkdir "%ODIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

:: --------------------------------------------------------- toolchain -----
set VCVARS=
for %%D in (C D E F G) do (
  if not defined VCVARS (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS (
        if exist "%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat"
        if exist "%%D:\Program Files\Microsoft Visual Studio\2026\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2026\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
)
if not defined VCVARS (
  echo ERRO: vcvars64.bat do VS2026 nao encontrado.
  exit /b 1
)

:find_wdk
:: --------------------------------------------------------------- WDK -----
:: 1) o ambiente ja aponta o kit (EWDK define WindowsSdkDir)
set WDK=
if defined WindowsSdkDir (
  if exist "%WindowsSdkDir%Include" set "WDK=%WindowsSdkDir:~0,-1%"
)
:: 2) senao, procura nas duas variantes de Program Files, em varios drives
if not defined WDK (
  for %%D in (C D E F G) do (
    for %%P in ("Program Files (x86)" "Program Files") do (
      if not defined WDK (
        if exist "%%D:\%%~P\Windows Kits\10\Include" set "WDK=%%D:\%%~P\Windows Kits\10"
      )
    )
  )
)
if not defined WDK (
  echo ERRO: Windows Kits\10 nao encontrado.
  echo        Instale o WDK ^(nao apenas o SDK^) — ver
  echo        docs\DESENVOLVIMENTO-REMOTO.md secao 1.4.
  exit /b 1
)

:: WDK 10.0.22621 e o minimo: usamos ExAllocatePool2 e MmMapIoSpaceEx.
set WDKVER=10.0.28000.0
if not exist "%WDK%\Include\%WDKVER%\km\ntddk.h" (
  echo ERRO: WDK 10.0.28000.0 nao encontrado.
  echo        Instale o "Windows Driver Kit", nao apenas o SDK.
  exit /b 1
)

echo === WDK  : %WDK% ^(%WDKVER%^)
echo === VS   : %VCVARS%
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

:: ------------------------------------------------------------ compila ----
set CFLAGS=/nologo /c /W4 /WX- /Od /Oi /Zi /kernel /GS- ^
 /D_AMD64_ /DAMD64 /D_WIN64 /DBC250_KERNEL_MODE ^
 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
 /I "%SRCDIR%." /I "%SRCDIR%..\inc" ^
 /I "%WDK%\Include\%WDKVER%\km" ^
 /I "%WDK%\Include\%WDKVER%\km\crt" ^
 /I "%WDK%\Include\%WDKVER%\shared"

set SOURCES=driver.c hw.c psp.c log.c script.c thermal.c
set OBJS=

for %%F in (%SOURCES%) do (
  echo [cc] %%F
  cl %CFLAGS% /Fo"%OBJDIR%\%%~nF.obj" /Fd"%OBJDIR%\bc250dbg.pdb" "%SRCDIR%%%F"
  if errorlevel 1 (
    echo BUILD FALHOU em %%F
    exit /b 1
  )
  set OBJS=!OBJS! "%OBJDIR%\%%~nF.obj"
)

:: -------------------------------------------------------------- link -----
set BOLIB=BufferOverflowFastFailK.lib
if not exist "%WDK%\Lib\%WDKVER%\km\x64\%BOLIB%" set BOLIB=BufferOverflowK.lib

echo [link] bc250dbg.sys
link /NOLOGO /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry ^
  /NODEFAULTLIB /INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF ^
  /OUT:"%ODIR%\bc250dbg.sys" /PDB:"%ODIR%\bc250dbg.pdb" ^
  /LIBPATH:"%WDK%\Lib\%WDKVER%\km\x64" ^
  %OBJS% ntoskrnl.lib hal.lib Wdmsec.lib %BOLIB%
if errorlevel 1 (
  echo LINK FALHOU
  exit /b 1
)

:: ------------------------------------------------------------ assina -----
:: Certificado de teste auto-assinado, criado uma vez:
::   powershell -c "New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=BC250-WIN Test' -CertStoreLocation Cert:\CurrentUser\My"
set SIGNTOOL=
for %%V in (%WDKVER% 10.0.22621.0 10.0.19041.0) do (
  if not defined SIGNTOOL (
    if exist "%WDK%\bin\%%V\x64\signtool.exe" set "SIGNTOOL=%WDK%\bin\%%V\x64\signtool.exe"
  )
)
if defined SIGNTOOL (
  :: com carimbo de tempo primeiro; se a maquina estiver sem internet, repete
  :: sem ele — para um driver de teste o carimbo nao faz falta
  "%SIGNTOOL%" sign /fd SHA256 /n "BC250-WIN Test" /t http://timestamp.digicert.com "%ODIR%\bc250dbg.sys" >nul 2>&1
  if errorlevel 1 (
    "%SIGNTOOL%" sign /fd SHA256 /n "BC250-WIN Test" "%ODIR%\bc250dbg.sys" >nul 2>&1
    if errorlevel 1 (
      echo AVISO: assinatura falhou — o driver NAO vai carregar.
      echo        Rode como admin:  powershell -ExecutionPolicy Bypass -File dbg\make-testcert.ps1
    ) else (
      echo [sign] ok ^(sem carimbo de tempo^)
    )
  ) else (
    echo [sign] ok
  )
) else (
  echo AVISO: signtool.exe nao encontrado; .sys ficou sem assinatura.
)

echo.
echo OK: %ODIR%\bc250dbg.sys
endlocal
