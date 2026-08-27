# Ativacao explicita do RLC F32 - pacote 1.32.0.0

Data: 2026-08-26.

O 1.31 mostrou PSP LOAD_IP_FW concluido, mas `RLC_CNTL=0` e
`EXEC_ROM_CODE=1`. O 1.32 aplica explicitamente `RLC_ENABLE_F32=1` depois da
aceitacao PSP, aguarda 1 ms e somente entao mede `RLC_CNTL`/`RLC_GPM_STAT`.
O snapshot continua restaurado no teardown. O PFP permanece bloqueado se o RLC
nao sair da ROM.

**PFP/PM4/COMPUTE NAO VALIDADO EM HARDWARE**.

## Resultado fisico

O binding iniciado em 2026-08-26 22:26:40 apagou a tela antes da coleta
pos-binding. A pasta de evidencia contem apenas `device-before.json`,
`delete-oem19.txt` e um `add-driver.txt` interrompido; portanto nao existe
telemetria suficiente para atribuir a falha ao RLC ou ao PFP. Depois do reboot,
o dispositivo voltou automaticamente para `BasicDisplay`, codigo 0.

Veredito: **BLOQUEADO POR TELA PRETA; NAO VALIDADO EM HARDWARE**.
