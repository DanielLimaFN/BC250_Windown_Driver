@echo off
setlocal enabledelayedexpansion
:: build.bat - compila e testa a ferramenta de VRAM pela CMOS (bc250cmos.sys + bc250-vram.exe)
:: Saida: tools\vram-cmos\out\  (teste offline roda antes de tudo; falhou = nada e' gerado)

set SRCDIR=%~dp0
set ODIR=%~dp0out
set OBJDIR=%ODIR%\obj
if not exist "%ODIR%" mkdir "%ODIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

set VCVARS=
for %%D in (C D E F G) do (
  if not defined VCVARS (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS (
        if exist "%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\18\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
)
if not defined VCVARS ( echo ERRO: vcvars64.bat do VS2026 nao encontrado. & exit /b 1 )
set WDK=C:\Program Files (x86)\Windows Kits\10
set WDKVER=10.0.28000.0
if not exist "%WDK%\Include\%WDKVER%\km\ntddk.h" ( echo ERRO: WDK %WDKVER% nao encontrado. & exit /b 1 )
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

:: ---------------------------------------------------- teste offline -----
echo [test] bc250_apcb_memconf_test
cl /nologo /W4 /WX /Fe"%ODIR%\bc250_apcb_memconf_test.exe" /Fo"%OBJDIR%\\" "%SRCDIR%bc250_apcb_memconf_test.c" "%SRCDIR%bc250_apcb_memconf.c" >"%OBJDIR%\test_build.log"
if errorlevel 1 ( type "%OBJDIR%\test_build.log" & echo TESTE NAO COMPILOU & exit /b 1 )
"%ODIR%\bc250_apcb_memconf_test.exe"
if errorlevel 1 ( echo TESTE FALHOU & exit /b 1 )

:: ------------------------------------------------------------- CLI -----
echo [cc] bc250-vram.exe
cl /nologo /W4 /WX /Fe"%ODIR%\bc250-vram.exe" /Fo"%OBJDIR%\\" "%SRCDIR%bc250_vram_cli.c" "%SRCDIR%bc250_apcb_memconf.c" >"%OBJDIR%\cli_build.log"
if errorlevel 1 ( type "%OBJDIR%\cli_build.log" & echo CLI FALHOU & exit /b 1 )

:: ----------------------------------------------------------- driver -----
set KFLAGS=/nologo /c /W4 /WX /O2 /Zi /kernel /GS- ^
 /D_AMD64_ /DAMD64 /D_WIN64 /DBC250_KERNEL_MODE ^
 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
 /I "%SRCDIR%." ^
 /I "%WDK%\Include\%WDKVER%\km" /I "%WDK%\Include\%WDKVER%\km\crt" /I "%WDK%\Include\%WDKVER%\shared"
for %%F in (bc250cmos_driver.c bc250_apcb_memconf.c) do (
  echo [kcc] %%F
  cl %KFLAGS% /Fo"%OBJDIR%\k_%%~nF.obj" /Fd"%OBJDIR%\bc250cmos.pdb" "%SRCDIR%%%F"
  if errorlevel 1 ( echo DRIVER FALHOU em %%F & exit /b 1 )
)
set BOLIB=BufferOverflowFastFailK.lib
if not exist "%WDK%\Lib\%WDKVER%\km\x64\%BOLIB%" set BOLIB=BufferOverflowK.lib
echo [link] bc250cmos.sys
link /NOLOGO /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry /NODEFAULTLIB /INCREMENTAL:NO ^
  /DEBUG /OPT:REF /OPT:ICF /OUT:"%ODIR%\bc250cmos.sys" /PDB:"%ODIR%\bc250cmos.pdb" ^
  /LIBPATH:"%WDK%\Lib\%WDKVER%\km\x64" "%OBJDIR%\k_bc250cmos_driver.obj" "%OBJDIR%\k_bc250_apcb_memconf.obj" ^
  ntoskrnl.lib hal.lib wdmsec.lib %BOLIB%
if errorlevel 1 ( echo LINK FALHOU & exit /b 1 )

set SIGNTOOL=%WDK%\bin\%WDKVER%\x64\signtool.exe
"%SIGNTOOL%" sign /fd SHA256 /n "BC250-WIN Test" "%ODIR%\bc250cmos.sys" >nul 2>&1
if errorlevel 1 ( echo AVISO: assinatura do bc250cmos.sys falhou -- o driver nao vai carregar. & exit /b 1 )
echo [sign] ok
echo OK: %ODIR%\bc250cmos.sys e %ODIR%\bc250-vram.exe
endlocal
