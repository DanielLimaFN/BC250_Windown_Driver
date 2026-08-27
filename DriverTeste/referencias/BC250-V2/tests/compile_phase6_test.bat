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
cl.exe /nologo /O2 /MD /utf-8 /I"%~dp0..\..\mesa-radv-wddm\include" "%~dp0test_phase6_graphics_pipeline.c" /Fe:"%~dp0test_phase6_graphics_pipeline.exe" /link /OUT:"%~dp0test_phase6_graphics_pipeline.exe" gdi32.lib user32.lib
copy /y "%~dp0test_phase6_graphics_pipeline.exe" "%~dp0..\build\test_phase6_graphics_pipeline.exe" >nul
echo Teste Fase 6 compilado com sucesso!
