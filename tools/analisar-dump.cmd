@echo off
setlocal
echo Ajustando permissoes em C:\Windows\MEMORY.DMP...
takeown /f C:\Windows\MEMORY.DMP
icacls C:\Windows\MEMORY.DMP /grant Todos:F
echo Analisando C:\Windows\MEMORY.DMP com kd.exe...
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe" -z C:\Windows\MEMORY.DMP -c "!analyze -v; .thread; kv; lmvm bc250mcdm; q" > "%~dp0..\docs\evidence\dump-analysis.txt"
echo Analise concluida! Arquivo gerado em docs\evidence\dump-analysis.txt
