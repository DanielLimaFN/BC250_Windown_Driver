# Relatório de Engenharia — Micro-Gate 7 Fase B: Fechamento do PM4 Ring 0 via Polling em VRAM, RPTR Writeback, Invalidação L2 e Controle de Ring 0x50000709 (1.41.0.0)

Data: 2026-08-27.

Status: **CANDIDATO 1.41.0.0 REFINADO; RB_CNTL=0x50000709 & TLB ENG0 FLUSH ARMED; AGUARDANDO PROVA EM SILÍCIO**.

## Contexto e Evolução Técnica

O Candidato **1.40.0.0** alcançou **`BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS`** com zero violação de integridade ou estabilidade. No entanto, o `RPTR` físico permaneceu em `0`, e a fence permaneceu em `0x43DAFFFE`.

Investigando o procedimento comprovado em hardware no driver de laboratório (`dbg/hw.c:5650-5670`) e nas referências arquiteturais (`referencias/BC250-V2/wddm_hw.c` e `referencias/BC250-V3/CentralDeTestes`):
1. **Configuração Exata de `CP_RB0_CNTL = 0x50000709`**:
   - `(9u & 0x3F) | ((7u & 0x3F) << 8) | (1u << 28) | (1u << 30)`:
     - Bit 28 (`0x10000000`): Habilita o circuito de atomicidade/swapping do buffer do anel.
     - Bit 30 (`0x40000000`): Habilita a decodificação primária do Ring Buffer no frontend do CP.
     - Sem os bits 28 e 30, o hardware do CP ignora comandos de anel.
2. **Ciclo de Reset com `RB_NO_UPDATE`**:
   - Durante o reset, o driver escreve `0x80000000 | (1u << 27) | rbCntl` (`RB_RPTR_WR_ENA` + `RB_NO_UPDATE`), zera `RPTR`, `WPTR`, `WPTR_HI`, e então grava `rbCntl` (`0x50000709`), liberando a atualização autônoma do hardware.
3. **Golden Registers de Rasterização Obrigatórios**:
   - `R_VGT_NUM_INSTANCES` (`0x30934`) = `1`;
   - `R_PA_SC_LINE_STIPPLE_STATE` (`0x30A04`) = `0`.
4. **Invalidação TLB Engine 0 (CP)**:
   - `R_GCVM_INVALIDATE_ENG0_REQ` (`0x0A28C`) = `0x00000801` (invalida Engine 0 / VMID 0) com espera de ACK em `0x0A2D4`.
5. **Re-afirmação de Controle pós-Unhalt**:
   - Após o unhalt conjunto de `CP_MEC_CNTL = 0` e `CP_ME_CNTL = 0`, o hardware re-afirma `CP_RB0_CNTL` e `CP_RB_WPTR_POLL_CNTL = 0x00400100` antes de emitir o cursor.

## Integridade do Pacote 1.41.0.0

- DriverVer: `08/27/2026,1.41.0.0`
- source SYS: `55E68096977DBFA0BBFDD12577066935D4ABB6FA5F27AE9007230B36E786F8C7`
- signed SYS: `2DB7865F5DCECEAA4FA05BFB2E2053A34098BADF47D31DCE765A46023FE60335`
- catalog:    `F874C952E9882BF335AB61D62CF1F5212E67F733386DA4C8F61B577956B6A17B`
- SDMA FW:    `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`
- RLC FW:     `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`
- PFP FW:     `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- ME FW:      `D4ED4D968DE0D7720F7C8215D42FDAE727BF99CDF43CEE6F8F864DC2584B17E2`
- CE FW:      `5946EFBF7E46CCFE7E9F965DA56C35B1187469D1C1A49C8A9EFF433EB3AB4723`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

**PM4 RING 0 E COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
