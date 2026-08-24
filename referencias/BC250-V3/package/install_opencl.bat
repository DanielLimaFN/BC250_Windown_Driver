@echo off
setlocal
cd /d "%~dp0"
echo =================================================================
echo  AMD BC-250 - Instalador Global OpenCL 2.0 (Khronos e AMD)
echo =================================================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_opencl.ps1"
pause
