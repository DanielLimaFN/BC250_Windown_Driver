@echo off
setlocal

set "ROOT=%~dp0.."
set "MESA=C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm"
set "OUT=%ROOT%\driver\tests\output_radv_gate"

if not exist "%MESA%\include\vulkan\vulkan.h" exit /b 2
if not exist "%MESA%\tools\bin\glslangValidator.exe" exit /b 4
if not exist "%OUT%" mkdir "%OUT%"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
"%MESA%\tools\bin\glslangValidator.exe" -V -S comp ^
  -o "%OUT%\bc250_radv_compute_gate.spv" ^
  "%ROOT%\driver\tests\bc250_radv_compute_gate.comp"
if errorlevel 1 exit /b 5

cl /nologo /W4 /WX /O2 /MD /I "%MESA%\include" ^
  /Fe"%OUT%\bc250-radv-compute-gate.exe" ^
  "%ROOT%\driver\tests\bc250_radv_compute_gate.c"
if errorlevel 1 exit /b 6

echo RADV compute gate built: %OUT%\bc250-radv-compute-gate.exe
echo Hardware was not touched.
