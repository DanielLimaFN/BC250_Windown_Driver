@echo off
setlocal
echo Compilando Suite OpenCL 2.0/3.0 ICD da AMD BC-250
echo =================================================

set "SRCDIR=%~dp0"
set "BUILDDIR=%SRCDIR%..\build"
set "PKGDIR=%SRCDIR%..\package"
set "TOOLSDIR=%SRCDIR%..\tools"
set "TESTSDIR=%SRCDIR%..\tests\bin"

if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
if not exist "%PKGDIR%" mkdir "%PKGDIR%"

set VCVARS64=
for %%D in (C D E F G) do (
  if not defined VCVARS64 (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS64 (
        if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
          set "VCVARS64=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
        )
      )
    )
  )
)

if not defined VCVARS64 (
  echo ERRO: vcvars64.bat nao encontrado.
  exit /b 1
)

:: 1. Compila 64-bit Native Driver bc250_opencl.dll
call "%VCVARS64%" >nul
echo [cl64] Compilando bc250_opencl.dll (x64)...
cl /nologo /O2 /W3 /DCL_TARGET_OPENCL_VERSION=200 /I"%SRCDIR%include" /I"%SRCDIR%." /LD "%SRCDIR%bc250_opencl.c" /link /DEF:"%SRCDIR%bc250_opencl.def" /OUT:"%BUILDDIR%\bc250_opencl.dll" gdi32.lib user32.lib
if errorlevel 1 (
  echo ERRO ao compilar bc250_opencl.dll
  exit /b 1
)

copy /Y "%BUILDDIR%\bc250_opencl.dll" "%PKGDIR%\bc250_opencl.dll" >nul
copy /Y "%BUILDDIR%\bc250_opencl.dll" "%PKGDIR%\amdocl64.dll" >nul
if exist "%TESTSDIR%" copy /Y "%BUILDDIR%\bc250_opencl.dll" "%TESTSDIR%\bc250_opencl.dll" >nul
copy /Y "%BUILDDIR%\bc250_opencl.dll" "%SRCDIR%..\bc250_opencl.dll" >nul
echo [+] bc250_opencl.dll (x64) e amdocl64.dll geradas com sucesso.

:: 2. Compila 64-bit Universal Khronos Loader OpenCL.dll
echo [cl64] Compilando Khronos OpenCL.dll Universal ICD Loader (x64)...
cl /nologo /O2 /W3 /DCL_TARGET_OPENCL_VERSION=200 /I"%SRCDIR%include" /I"%SRCDIR%." /LD "%SRCDIR%opencl_loader.c" /link /DEF:"%SRCDIR%opencl_loader.def" /OUT:"%BUILDDIR%\OpenCL.dll" advapi32.lib user32.lib
if errorlevel 1 (
  echo ERRO ao compilar OpenCL.dll
  exit /b 1
)

copy /Y "%BUILDDIR%\OpenCL.dll" "%PKGDIR%\OpenCL.dll" >nul
copy /Y "%BUILDDIR%\OpenCL.dll" "%SRCDIR%OpenCL.dll" >nul
if exist "%TOOLSDIR%" copy /Y "%BUILDDIR%\OpenCL.dll" "%TOOLSDIR%\OpenCL.dll" >nul
if exist "%TESTSDIR%" copy /Y "%BUILDDIR%\OpenCL.dll" "%TESTSDIR%\OpenCL.dll" >nul
echo [+] OpenCL.dll (Universal ICD Loader x64) gerada com sucesso.

:: 3. Compila Suite 32-bit (x86)
call "%SRCDIR%build_opencl32.bat"

echo.
echo =============================================================
echo   BUILD OPENCL SUITE CONCLUIDO COM SUCESSO!
echo =============================================================
exit /b 0
