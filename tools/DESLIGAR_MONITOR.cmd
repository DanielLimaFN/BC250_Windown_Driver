@echo off
rem Corrida 272 (doc 713): desliga o monitor para testar o DPMS da DCN.2.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0desligar-monitor.ps1"
