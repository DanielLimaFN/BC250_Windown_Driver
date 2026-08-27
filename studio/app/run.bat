@echo off
cd /d "%~dp0"
if not exist "%~dp0BC250Studio.exe" (
    echo Gerando executavel nativo...
    call npm run package
)
start "" "%~dp0BC250Studio.exe"
