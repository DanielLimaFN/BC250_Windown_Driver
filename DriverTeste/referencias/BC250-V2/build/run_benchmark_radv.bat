@echo off
set VK_ICD_FILENAMES=%~dp0..\package\vulkan_radeon.json
set RADV_DEBUG=startup
echo ========================================================
echo   Executando Benchmark Vulkan Suite com Mesa RADV
echo ========================================================
"%~dp0benchmark_vulkan_suite.exe" "%~dp0..\package\vulkan_radeon.dll"
