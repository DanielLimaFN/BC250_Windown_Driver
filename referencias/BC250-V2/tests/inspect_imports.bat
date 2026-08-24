@echo off
setlocal

:: Localiza Visual Studio vcvars64.bat
set VCVARS=
for %%D in (C D E F G) do (
  if not defined VCVARS (
    for %%E in (BuildTools Community Professional Enterprise) do (
      if not defined VCVARS (
        if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
        if exist "%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
)

call "%VCVARS%" >nul

echo ========================================================
echo EXPORTS DE amdbc250vulkan.dll:
echo ========================================================
dumpbin /exports "%~dp0..\build\amdbc250vulkan.dll" | findstr /i "vk"

echo ========================================================
echo IMPORTS DE VulkanCube.exe:
echo ========================================================
dumpbin /imports "%~dp0..\..\Vulkan-Cube\VulkanCube.exe" | findstr /i "vulkan vk"
