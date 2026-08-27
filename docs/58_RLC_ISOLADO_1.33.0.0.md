# Gate RLC isolado - pacote 1.33.0.0

Data: 2026-08-26.

O 1.32 apagou a tela antes da telemetria, quando ativacao explicita do RLC e
direct-load do PFP estavam no mesmo StartDevice. O 1.33 conserva a ativacao
explicita do RLC, mas nao chama o PFP. O objetivo e atribuir a falha sem
ambiguidade.

Gate: PSP aceito, F32 observado, RLC fora da ROM e restore confirmado. PFP deve
permanecer com status inicial e zero amostras/escritas.

**PFP/PM4/COMPUTE NAO EXECUTADOS; NAO VALIDADO EM HARDWARE**.

## Resultado fisico

Gate aprovado sem tela preta. Evidencia:
`docs/evidence/2026-08-26_22-32-01-DESKTOP-HQAG6T4-mcdm-replace`.

- PSP RLC: tipo 8, 25088 bytes, fence `1/1`, 3 polls e resposta zero;
- `RLC_CNTL: 0x00000000 -> 0x00000001`;
- `RLC_GPM_STAT: 0x00F40016 -> 0x00B40016`;
- `EXEC_ROM_CODE: 1 -> 0` e `RLC_ENABLE_F32=1`;
- restore de `RLC_CNTL=1`;
- SDMA repetiu RPTR/WPTR, fence e quatro dwords corretos;
- PFP permaneceu intencionalmente sem staging, escrita ou amostra.

Conclusao: a ativacao explicita do RLC nao foi a causa da tela preta do 1.32.
O proximo diagnostico deve ficar restrito ao direct-load/unhalt do PFP.
