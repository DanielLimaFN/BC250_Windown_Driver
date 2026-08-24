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
cl.exe /nologo /O2 /MD /utf-8 /I"%~dp0..\mesa-radv-wddm\include" /I"%~dp0tests" "%~dp0tests\benchmark_vulkan_suite.c" /Fe:"%~dp0build\benchmark_vulkan_suite.exe" /link /OUT:"%~dp0build\benchmark_vulkan_suite.exe"
copy /y "%~dp0build\benchmark_vulkan_suite.exe" "%~dp0package\benchmark_vulkan_suite.exe" >nul
echo Benchmark compilado com sucesso!
