@echo off
setlocal
cd /d "%~dp0"
echo BC-250 Corrida 206: 4c.2 com 3.353.1.0 = 3.352.1.0 + FlushTlb pelo SDMA no anel antes do INDIRECT, sem MMIO de invalidacao no build, doc 569. CMOS em 256 MB.
echo SEM harness. Repita o uso da 201: menu Iniciar, Pesquisa, Gerenciador de Tarefas e Configuracoes.
echo ROTEIRO (IMPORTANTE - abra o menu desde cedo):
echo   1. Quando o desktop aparecer, espere uns 20 s.
echo   2. Clique no menu Iniciar. Anote a hora e se icones e textos aparecem (e em quantos segundos).
echo   3. Feche o menu, espere 30 s, abra de novo. Repita umas 5 vezes ao longo da corrida.
echo   4. MESMO USO DA 201-205: Pesquisa algumas vezes, Gerenciador de Tarefas, Configuracoes. Anote a hora de cada tela preta.
echo SE TRAVAR: anote a hora e aperte o BOTAO DE RESET em 10 a 15 segundos.
echo Use o BOTAO de reset. NAO corte a energia nem segure o botao de ligar.
echo DEPOIS DE UMA TRAVA: no boot seguinte, ANTES de tudo, rode elevado: tools\coletar-pos-trava.ps1 (le o motivo do reset e copia os logs).
echo Recuperando captura ETW pendente de corrida anterior, se houver...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bc250-hang-etw.ps1" -Recover
echo Lendo a VRAM na CMOS e o MOTIVO DO ULTIMO RESET (le e limpa, como o Linux; doc 553). O preflight exige 256 MB aplicados...
if exist "%~dp0vram-cmos\out\bc250-vram.exe" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vram-cmos\bc250-vram.ps1" -Ler
    if errorlevel 1 echo [AVISO] Leitura da CMOS falhou; seguindo sem ela.
) else (
    echo [AVISO] tools\vram-cmos\out\bc250-vram.exe ausente; rode tools\vram-cmos\build.bat para ter a leitura.
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-corrida206-4c2-flush-tlb-no-anel.ps1"
if errorlevel 1 (
    echo [RECUSADO] Candidato, auditoria ou configuracao de boot divergiu. Nenhum bind foi iniciado.
    exit /b 1
)
set "BC250_WATCHDOG_SECONDS=600"
set "BC250_SKIP_D3D11_HARNESS=1"
set "BC250_ENABLE_HANG_ETW=1"
set "BC250_CAPTURE_MONITOR_EDID=1"
call "%~dp0EXECUTAR_D3D11_NATIVO.cmd"
exit /b %ERRORLEVEL%