# RLC via PSP e transicao EXEC_ROM_CODE - pacote 1.29.0.0

Data: 2026-08-26.

Status: **GATE FISICO APROVADO; PM4/COMPUTE NAO VALIDADO**.

Depois da prova SDMA completa, o 1.29 carrega o payload autenticado do
`cyan_skillfish2_rlc.bin` pelo GPCOM PSP com tipo 8.

Contrato do arquivo:

- arquivo: 25344 bytes, SHA-256
  `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`;
- payload a partir de `0x100`: 25088 bytes, SHA-256
  `16794B4817CCB8E82E45FE3524AEC7A8A0E58D032E76AC1C3385C417A145BE48`.

Gate fisico:

- fence PSP `1/1` e resposta zero;
- `RLC_GPM_STAT.EXEC_ROM_CODE: 1 -> 0`;
- `RLC_CNTL.RLC_ENABLE_F32=1` depois da carga;
- destroy/restore do ring PSP;
- restauracao e readback de `RLC_CNTL` ao valor anterior.

O carregamento do microcodigo permanece no silicio ate reset; o bit controlavel
`RLC_ENABLE_F32` e restaurado. Se o RLC ja estiver fora da ROM no preflight, o
gate nao fabrica a transicao e permanece bloqueado.

Build completo, contratos offline, assinatura e auditoria: PASS.

## Resultado fisico

Evidencia:
`docs/evidence/2026-08-26_18-28-15-DESKTOP-HQAG6T4-mcdm-replace`.

- PSP: tipo 8, 25088 bytes, fence `1/1`, 3 polls, resposta zero;
- `RLC_CNTL: 0x00000000 -> 0x00000001`;
- `RLC_GPM_STAT: 0x00F40016 -> 0x00B40016`;
- `EXEC_ROM_CODE: 1 -> 0` e `RLC_ENABLE_F32=1`;
- destroy/restore PSP `1/1` e restore de `RLC_CNTL=1`;
- a prova SDMA completa foi repetida no mesmo ensaio.

Veredito restrito: firmware RLC aceito e RLC fisicamente fora da ROM. Isso
nao executou PFP, PM4 ou compute.

- source SYS: `64C99D4A8F84BCCBF7DEA417F871E1E4B06F8FD828F22937EC7E37E3D60FD85F`;
- signed SYS: `A1616ED4524AE3C949814D18D8C25F68C710689DE064E2767C3B3F473C73621B`;
- INF: `0EC9B32151860D2E8F7D651D31E1637858DF997D86A3BD67D239BE68E2DEE20A`;
- CAT: `CCF61B82EE0049931DC4CBFF9011CAAB63716C820F4CF6FD970B3012B1A76DF1`.

**PM4/COMPUTE NAO VALIDADO EM HARDWARE**
