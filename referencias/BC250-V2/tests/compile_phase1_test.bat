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
cl.exe /nologo /O2 /MD /utf-8 "%~dp0test_phase1_hardware_bringup.c" /Fe:"%~dp0test_phase1_hardware_bringup.exe" /link /OUT:"%~dp0test_phase1_hardware_bringup.exe" gdi32.lib user32.lib
copy /y "%~dp0test_phase1_hardware_bringup.exe" "%~dp0..\build\test_phase1_hardware_bringup.exe" >nul
echo Teste Fase 1 compilado com sucesso!
