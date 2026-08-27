@echo off
cd /d "%~dp0"
echo =======================================================
echo Gerando Executavel Windows (.exe) - BC-250 Studio
echo =======================================================
call npm run package
pause
