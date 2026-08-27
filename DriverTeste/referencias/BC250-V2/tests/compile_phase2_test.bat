@echo off
setlocal
for %%D in (C D E F G) do (
  for %%E in (BuildTools Community Professional Enterprise) do (
    if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
      call "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" >nul
      goto :compile
    )
  )
)
:compile
cl.exe /nologo /O2 /MD /utf-8 /I"%~dp0..\..\mesa-radv-wddm\include" "%~dp0test_phase2_radv_escape.c" /Fe:"%~dp0test_phase2_radv_escape.exe" /link /OUT:"%~dp0test_phase2_radv_escape.exe" gdi32.lib user32.lib
copy /y "%~dp0test_phase2_radv_escape.exe" "%~dp0..\build\test_phase2_radv_escape.exe" >nul
echo Teste Fase 2 compilado com sucesso!
