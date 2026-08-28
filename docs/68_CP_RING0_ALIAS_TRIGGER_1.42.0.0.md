# Relatório de Engenharia — Micro-Gate 7 Fase C: Disparo Físico via CP_RING0_WPTR (0xDA78), Leitura de RPTR (0xDA6C) e Prova Dupla com Scratch (0x32D4) (1.42.0.0)

Data: 2026-08-27.

Status: **CANDIDATO 1.42.0.0 PRONTO; DISPARO EM 0xDA78 & SCRATCH 0x32D4 ARMED; AGUARDANDO PROVA EM SILÍCIO**.

## Contexto e Evolução Técnica

Nos ensaios anteriores (`1.40.0.0` e `1.41.0.0`):
- A tríade do CP (**PFP**, **ME**, **CE**) foi carregada com sucesso absoluto na VRAM owned (`832 KiB`, 0 mismatches);
- Todas as três microengines saíram do HALT e saltaram de `0x00000000` para `0x0000041E` (instrução de espera de comandos);
- O anel não consumia porque o registrador `CP_RB0_WPTR` (`0x0000C150`) é **READ-ONLY para o Host** no silício da BC-250 (`referencias/BC250-V3/CentralDeTestes/src/t3_write_stick.c`).

O **Candidato 1.42.0.0** corrige e arma o trigger físico:
1. **Trigger de WPTR Físico**:
   - Aciona o registrador comprovadamente gravável pelo Host: **`0x0000DA78`** (`CP_RING0_WPTR`);
   - Re-afirma também `0xC150` e o buffer de polling em VRAM (`*pWptrPoll = cursor`);
2. **Readback de RPTR Multiponto**:
   - Lê simultaneamente `0x0000DA6C` (`CP_RING0_RPTR`), `0x00008700` (`CP_RB0_RPTR`) e o writeback na VRAM (`*pRptrWb`);
3. **Prova Dupla de Execução**:
   - Pacote 1 (4 dw): `NOP` com identificador `0xBC250142`;
   - Pacote 2 (5 dw): `WRITE_DATA` escrevendo `0x22222222` no registrador MMIO `CP_SCRATCH` (`0x000032D4`);
   - Pacote 3 (5 dw): `WRITE_DATA` escrevendo `0xBC250123` na VRAM física (fence);
   - `cursor = 14 dwords`.
4. **Ciclo Transacional e Rollback Fail-Closed**:
   - Re-halt total imediato pós-amostragem e restauração rigorosa de 100% dos registradores e da memória.

## Integridade do Pacote 1.42.0.0

- DriverVer: `08/27/2026,1.42.0.0`
- source SYS: `60DF6BB9CB54081F513CD90C114D018269A60531013FD054C337D037DCE544CD`
- signed SYS: `000E20734E3D6EC7642B4C4CD298A967ACF681A080B0D3E5D6B8A3D02E4B2AE1`
- catalog:    `33039A2EE7C32D485F3C2AAAB1B3C4F9C7EC557CD16D0A23A345692D1FC938E1`
- SDMA FW:    `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`
- RLC FW:     `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`
- PFP FW:     `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- ME FW:      `D4ED4D968DE0D7720F7C8215D42FDAE727BF99CDF43CEE6F8F864DC2584B17E2`
- CE FW:      `5946EFBF7E46CCFE7E9F965DA56C35B1187469D1C1A49C8A9EFF433EB3AB4723`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

**PM4 RING 0 E COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
