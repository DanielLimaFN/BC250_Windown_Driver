@echo off
setlocal
rem T3 (doc 691): abre um processo D3D11 novo a cada ~5 s na BC-250.
rem Padrao: 90 ciclos (~8 min, cabe na janela de 600 s), 4 texturas de 8 MB por processo (32 MB, dentro
rem do orcamento), sem desenho. Log: C:\BC250_PROCESSO_NOVO.log (cada linha com
rem data, hora, sessao e pid; gravado direto no disco para sobreviver a trava).
rem Argumentos extras vao direto para a ferramenta, ex.: --clear  --ciclos 60

net session >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERRO: rode este arquivo como Administrador ^(o log fica em C:\^).
    pause
    exit /b 1
)
if not exist "%~dp0bc250-processo-novo.exe" (
    echo ERRO: bc250-processo-novo.exe ausente; rode tools\build-processo-novo.bat.
    pause
    exit /b 1
)

echo ==========================================================
echo   T3: PROCESSO NOVO A CADA ~5 s NA BC-250 (doc 691)
echo   Log: C:\BC250_PROCESSO_NOVO.log
echo ==========================================================
"%~dp0bc250-processo-novo.exe" --ciclos 90 --intervalo-ms 5000 --texturas 4 --mb 8 --segurar-ms 500 %*
echo.
echo Saida=%ERRORLEVEL%  (0 ok, 1 falha D3D11, 2 adaptador nao e' a BC-250, 3 filho preso)
pause
