@echo off
rem =================================================================
rem   AMD BC-250 (BC250-V3) - Execucao da Suite Completa
rem   Redireciona para run_suite.cmd para garantir matriz consolidada,
rem   verificacao estatica T8 e todos os 11 testes.
rem =================================================================

call "%~dp0run_suite.cmd" %*
exit /b %errorlevel%

