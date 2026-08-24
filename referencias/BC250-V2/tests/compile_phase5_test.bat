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
cl.exe /nologo /O2 /MD /utf-8 /I"%~dp0..\..\mesa-radv-wddm\include" "%~dp0test_phase5_llm_inference.c" /Fe:"%~dp0test_phase5_llm_inference.exe" /link /OUT:"%~dp0test_phase5_llm_inference.exe" gdi32.lib user32.lib
copy /y "%~dp0test_phase5_llm_inference.exe" "%~dp0..\build\test_phase5_llm_inference.exe" >nul
echo Teste Fase 5 compilado com sucesso!
