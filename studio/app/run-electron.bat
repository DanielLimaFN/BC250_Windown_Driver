@echo off
cd /d "%~dp0"
if not exist "%~dp0BC250Studio.exe" (
    call npm run package
)
start "" "%~dp0BC250Studio.exe"
