@echo off
setlocal enabledelayedexpansion
echo AMD BC-250 Native WDDM 2.0+ Driver (BC250-V3) Build
echo ====================================================

set SRCDIR=%~dp0
set ODIR=%~dp0build
set OBJDIR=%ODIR%\obj
set PKGDIR=%~dp0package

if not exist "%ODIR%" mkdir "%ODIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%PKGDIR%" mkdir "%PKGDIR%"

:: --------------------------------------------------------- toolchain -----
set HAVE_CL=
where cl.exe >nul 2>&1 && set HAVE_CL=1
if defined HAVE_CL goto :find_wdk

set VCVARS=
for %%D in (C D E F G) do (
  if not defined VCVARS (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS (
        if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
        if exist "%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
)
if not defined VCVARS (
  echo ERRO: vcvars64.bat do VS2022 nao encontrado.
  exit /b 1
)

:find_wdk
set WDK=
if defined WindowsSdkDir (
  if exist "%WindowsSdkDir%Include" set "WDK=%WindowsSdkDir:~0,-1%"
)
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
  exit /b 1
)

set WDKVER=
for %%V in (10.0.26100.0 10.0.22621.0) do (
  if not defined WDKVER (
    if exist "%WDK%\Include\%%V\km\ntddk.h" set "WDKVER=%%V"
  )
)
if not defined WDKVER (
  echo ERRO: WDK 10.0.22621 ou superior nao encontrado.
  exit /b 1
)

echo === WDK  : %WDK% ^(%WDKVER%^)
if not defined HAVE_CL (
  echo === VS   : %VCVARS%
  call "%VCVARS%" >nul
)

:: ------------------------------------------------------- carimbo build ---
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "[int](Get-Date -Format MMddHHmm)"`) do set "BUILDID=%%i"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format MM/dd/yyyy"`) do set "BUILDDATE=%%i"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format MMdd"`) do set "VERHI=%%i"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format HHmm"`) do set "VERLO=%%i"
if not defined BUILDID (
  echo ERRO: nao consegui gerar o carimbo de build via PowerShell.
  exit /b 1
)
set "DRIVERVER=%BUILDDATE%, 1.0.%VERHI%.%VERLO%"
echo === BUILD: id %BUILDID%  DriverVer %DRIVERVER%

:: Native KMDOD Display Miniport (DXGKDDI_INTERFACE_VERSION_WIN8 = 0x300E)
set DDIVER=0x300E
echo === DDI : DXGKDDI_INTERFACE_VERSION=%DDIVER% (Native KMDOD)

:: ------------------------------------------------------------ compila ----
set CFLAGS=/nologo /c /W3 /WX- /Od /Oi /Zi /kernel /GS- /Gy /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR- ^
 /D_AMD64_ /DAMD64 /D_WIN64 /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /DWINNT=1 /DNTDDI_VERSION=0x0A00000C /DDXGKDDI_INTERFACE_VERSION=%DDIVER% /DBC250_BUILD_ID=%BUILDID% /DNDEBUG ^
 /I "%SRCDIR%." ^
 /I "%WDK%\Include\%WDKVER%\km" ^
 /I "%WDK%\Include\%WDKVER%\km\crt" ^
 /I "%WDK%\Include\%WDKVER%\shared"

set SOURCES=wddm_driver.c wddm_hw.c
set OBJS=

for %%F in (%SOURCES%) do (
  echo [cc] %%F
  cl %CFLAGS% /Fo"%OBJDIR%\%%~nF.obj" /Fd"%ODIR%\vc143.pdb" "%SRCDIR%%%F"
  if errorlevel 1 (
    echo BUILD FALHOU em %%F
    exit /b 1
  )
  set OBJS=!OBJS! "%OBJDIR%\%%~nF.obj"
)

:: -------------------------------------------------------------- link -----
set BOLIB=BufferOverflowFastFailK.lib
if not exist "%WDK%\Lib\%WDKVER%\km\x64\%BOLIB%" set BOLIB=BufferOverflowK.lib

echo [link] bc250wddm.sys
link /ERRORREPORT:QUEUE /OUT:"%ODIR%\bc250wddm.sys" /VERSION:"1.0" /INCREMENTAL:NO /NOLOGO /WX /SECTION:"INIT,d" ^
  /LIBPATH:"%WDK%\Lib\%WDKVER%\km\x64" ^
  %BOLIB% ntoskrnl.lib hal.lib wmilib.lib displib.lib ntstrsafe.lib ^
  /NODEFAULTLIB /MANIFEST:NO /DEBUG /PDB:"%ODIR%\bc250wddm.pdb" /SUBSYSTEM:NATIVE,"10.00" /Driver /OPT:REF /OPT:ICF /ENTRY:"DriverEntry" /RELEASE /IMPLIB:"%ODIR%\bc250wddm.lib" /MACHINE:X64 /kernel /IGNORE:4198,4010,4037,4039,4065,4070,4078,4087,4089,4221,4108,4088,4218,4235 /osversion:10.0 /pdbcompress /debugtype:pdata ^
  %OBJS%
if errorlevel 1 (
  echo LINK FALHOU
  exit /b 1
)

:: ------------------------------------------------------------ assina -----
set SIGNTOOL=
for %%V in (%WDKVER% 10.0.22621.0 10.0.19041.0) do (
  if not defined SIGNTOOL (
    if exist "%WDK%\bin\%%V\x64\signtool.exe" set "SIGNTOOL=%WDK%\bin\%%V\x64\signtool.exe"
  )
)
if not defined SIGNTOOL (
  echo ERRO: signtool.exe nao encontrado; pacote de kernel nao sera gerado sem assinatura.
  exit /b 1
)
"%SIGNTOOL%" sign /fd SHA256 /n "BC250-WIN Test" /t http://timestamp.digicert.com "%ODIR%\bc250wddm.sys" >nul 2>&1
if errorlevel 1 (
  "%SIGNTOOL%" sign /fd SHA256 /n "BC250-WIN Test" "%ODIR%\bc250wddm.sys" >nul 2>&1
)
if errorlevel 1 (
  echo ERRO: falha ao assinar bc250wddm.sys com "BC250-WIN Test".
  exit /b 1
)
echo [sign] bc250wddm.sys assinado com sucesso.

:: --------------------------------------------------------- ADL Bridges ---
if exist "%SRCDIR%adl\compile_adl.bat" (
  echo [adl] Compilando bibliotecas de sensores ADL atiadlxx.dll e atiadlxy.dll...
  call "%SRCDIR%adl\compile_adl.bat" >nul 2>&1
)

:: --------------------------------------------------------- OpenCL ICD Driver ---
if exist "%SRCDIR%opencl\build_opencl.bat" (
  echo [opencl] Compilando driver OpenCL ICD bc250_opencl.dll...
  call "%SRCDIR%opencl\build_opencl.bat" >nul 2>&1
)

:: ---------------------------------------------------------- pacote -------
echo.
echo [pkg] Preparando pacote em %PKGDIR%...
copy /Y "%ODIR%\bc250wddm.sys" "%PKGDIR%\bc250wddm.sys" >nul
copy /Y "%SRCDIR%wddm.inf" "%PKGDIR%\wddm.inf" >nul
copy /Y "%SRCDIR%install-wddm.ps1" "%PKGDIR%\install-wddm.ps1" >nul
copy /Y "%SRCDIR%diag.ps1" "%PKGDIR%\diag.ps1" >nul

if exist "%SRCDIR%..\dbg\bc250-test.cer" (
  copy /Y "%SRCDIR%..\dbg\bc250-test.cer" "%PKGDIR%\bc250-test.cer" >nul
) else if exist "%SRCDIR%..\BC250-V2\package\bc250-test.cer" (
  copy /Y "%SRCDIR%..\BC250-V2\package\bc250-test.cer" "%PKGDIR%\bc250-test.cer" >nul
)
if not exist "%PKGDIR%\bc250-test.cer" (
  echo ERRO: certificado bc250-test.cer ausente.
  exit /b 1
)

powershell -NoProfile -Command "(Get-Content '%PKGDIR%\wddm.inf') -replace '^DriverVer=.*', 'DriverVer=%DRIVERVER%' | Set-Content -Encoding ASCII '%PKGDIR%\wddm.inf'"
echo [inf] DriverVer=%DRIVERVER%

echo [cat] Gerando catalogo de seguranca (wddm.cat)...
"%WDK%\bin\%WDKVER%\x86\Inf2Cat.exe" /driver:"%PKGDIR%" /os:10_X64,Server10_X64,10_NI_X64 >nul
if errorlevel 1 (
  echo ERRO: Inf2Cat rejeitou o pacote.
  exit /b 1
)

"%SIGNTOOL%" sign /fd SHA256 /n "BC250-WIN Test" "%PKGDIR%\wddm.cat" >nul 2>&1
if errorlevel 1 (
  echo ERRO: falha ao assinar wddm.cat.
  exit /b 1
)
echo [sign] wddm.cat assinado com sucesso.

(
echo @echo off
echo echo ========================================================
echo echo   Instalando Driver WDDM 2.0+ Nativo AMD BC-250 ^(V3^)...
echo echo ========================================================
echo powershell -NoProfile -ExecutionPolicy Bypass -File "%%~dp0install-wddm.ps1" %%*
echo set "INSTALL_RESULT=%%errorlevel%%"
echo echo.
echo echo Resultado do instalador: %%INSTALL_RESULT%% ^(BuildId esperado: %BUILDID%^)
echo pause
echo exit /b %%INSTALL_RESULT%%
) > "%PKGDIR%\install-wddm.bat"

echo.
echo ========================================================
echo   BUILD ^& PACOTE CONCLUIDOS COM SUCESSO!
echo   Pacote: %PKGDIR%
echo ========================================================
exit /b 0
