# RLC ja ativo no preflight - pacote 1.31.0.0

Data: 2026-08-26.

O ensaio 1.30 encontrou `EXEC_ROM_CODE=0` antes da carga. Recarregar o mesmo
RLC inverteu o estado para ROM e impediu corretamente o PFP de iniciar. O 1.31
trata dois estados validos sem fabricar evidencia:

- RLC na ROM: executa PSP LOAD_IP_FW e exige `ROM 1 -> 0`;
- RLC ja ativo: nao envia LOAD_IP_FW, ativa F32, exige `ROM 0 -> 0` e registra
  `RlcAlreadyActive=1`.

Nos dois casos `RLC_CNTL` e restaurado. O PFP somente prossegue quando o RLC
esta fisicamente fora da ROM e F32 foi observado ativo.

**PFP/PM4/COMPUTE NAO VALIDADO EM HARDWARE**.
