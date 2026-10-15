@echo off
setlocal
rem T3 (doc 691): abre um processo D3D11 novo a cada ~5 s na BC-250.
rem Padrao: 90 ciclos (~8 min, cabe na janela de 600 s), 6 texturas de 16 MB por processo (96 MB, ~50%
rem da VRAM dedicada), com desenho ativo na GPU (--clear, loop de renderizacao 3D a cada frame).
rem Log: C:\BC250_PROCESSO_NOVO.log (cada linha com data, hora, sessao e pid; gravado direto no disco).
rem Argumentos extras vao direto para a ferramenta.

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
echo   T3: PROCESSO NOVO 3D NA BC-250 (doc 737)
echo   Carga: 2 texturas x 16 MB (32 MB VRAM) + Janela 3D Ativa (3s / 180 quadros)
echo   Log: C:\BC250_PROCESSO_NOVO.log
echo ==========================================================
"%~dp0bc250-processo-novo.exe" --ciclos 90 --intervalo-ms 1000 --texturas 2 --mb 16 --segurar-ms 3000 --clear %*
echo.
echo Saida=%ERRORLEVEL%  (0 ok, 1 falha D3D11, 2 adaptador nao e' a BC-250, 3 filho preso)
pause
