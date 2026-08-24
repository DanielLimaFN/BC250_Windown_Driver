@echo off
setlocal
echo Compilando OpenCL 2.0/3.0 ICD para 32-bit (x86 / SysWOW64 / GPU-Z)
echo ==================================================================

set "SRCDIR=%~dp0"
set "BUILDDIR=%SRCDIR%..\build"
set "PKGDIR=%SRCDIR%..\package"

set VCVARS32=
for %%D in (C D E F G) do (
  if not defined VCVARS32 (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS32 (
        if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars32.bat" (
          set "VCVARS32=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars32.bat"
        )
      )
    )
  )
)

if not defined VCVARS32 (
  echo ERRO: vcvars32.bat nao encontrado.
  exit /b 1
)

call "%VCVARS32%" >nul

:: 1. Compila 32-bit bc250_opencl32.dll
echo [cl32] Compilando bc250_opencl32.dll (x86)...
cl /nologo /O2 /W3 /wd4996 /DCL_TARGET_OPENCL_VERSION=200 /I"%SRCDIR%include" /I"%SRCDIR%." /LD "%SRCDIR%bc250_opencl.c" /link /DEF:"%SRCDIR%bc250_opencl.def" /OUT:"%BUILDDIR%\bc250_opencl32.dll" gdi32.lib user32.lib
copy /Y "%BUILDDIR%\bc250_opencl32.dll" "%PKGDIR%\bc250_opencl32.dll" >nul
copy /Y "%BUILDDIR%\bc250_opencl32.dll" "%PKGDIR%\amdocl.dll" >nul
echo [+] bc250_opencl32.dll (x86) e amdocl.dll geradas com sucesso.

:: 2. Compila 32-bit OpenCL.dll (Universal ICD Loader x86)
echo [cl32] Compilando Khronos OpenCL.dll Universal ICD Loader (x86)...
cl /nologo /O2 /W3 /wd4996 /DCL_TARGET_OPENCL_VERSION=200 /I"%SRCDIR%include" /I"%SRCDIR%." /LD "%SRCDIR%opencl_loader.c" /link /DEF:"%SRCDIR%opencl_loader.def" /OUT:"%BUILDDIR%\OpenCL32.dll" advapi32.lib user32.lib
copy /Y "%BUILDDIR%\OpenCL32.dll" "%PKGDIR%\OpenCL32.dll" >nul
echo [+] OpenCL32.dll (Universal ICD Loader x86) gerada com sucesso.

echo.
echo ==================================================================
echo   BUILD 32-BIT OPENCL CONCLUIDO COM SUCESSO!
echo ==================================================================
exit /b 0
