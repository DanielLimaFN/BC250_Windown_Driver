@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
cl.exe /nologo /O2 /W3 /LD /D_CRT_SECURE_NO_WARNINGS /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt" /I"%~dp0." /I"%~dp0.." "%~dp0bc250_vulkan_icd.c" /Fe:"%~dp0..\build\amdbc250vulkan32.dll" /link /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x86" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x86" gdi32.lib user32.lib
if not errorlevel 1 (
    copy /Y "%~dp0..\build\amdbc250vulkan32.dll" "%~dp0..\package\amdbc250vulkan32.dll"
)
