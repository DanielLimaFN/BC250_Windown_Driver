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
cl.exe /nologo /O2 /MD /utf-8 /I"%~dp0..\..\mesa-radv-wddm\include" "%~dp0test_phase3_vram_allocator.c" /Fe:"%~dp0test_phase3_vram_allocator.exe" /link /OUT:"%~dp0test_phase3_vram_allocator.exe" gdi32.lib user32.lib
copy /y "%~dp0test_phase3_vram_allocator.exe" "%~dp0..\build\test_phase3_vram_allocator.exe" >nul
echo Teste Fase 3 compilado com sucesso!
