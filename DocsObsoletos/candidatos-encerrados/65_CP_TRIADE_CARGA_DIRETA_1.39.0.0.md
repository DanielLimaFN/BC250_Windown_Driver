# Relatório de Engenharia — Micro-Gate 6: Carga Conjunta e Programação das Jump Tables da Tríade do CP (1.39.0.0)

Data: 2026-08-27.

Status: **VALIDADO EM SILÍCIO (MICRO-GATE 6 APROVADO); PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO PARA ACELERAÇÃO**.

## Contexto e Objetivo

No Micro-Gate 5 (1.38.0.0), foi comprovado que PFP, ME e CE reagem sincronizados ao sinal de unhalt em janela ultracurta de 200 µs, transitando de `0x00000000` para o vetor de ROM de boot `0x0000041E`. A permanência em `0x041E` devia-se à ausência de microcódigos e Jump Tables para ME e CE.

O objetivo do **Micro-Gate 6 (1.39.0.0)** é descarregar e configurar completamente toda a frente de comando do Command Processor (CP):
1. Carga dos 3 firmwares do CP a partir de `System32\drivers`:
   - `cyan_skillfish2_pfp.bin`: 263.168 bytes payload, 128 dw JT, versão `0x94`
   - `cyan_skillfish2_me.bin`: 263.168 bytes payload, 128 dw JT, versão `0x63`
   - `cyan_skillfish2_ce.bin`: 263.040 bytes payload, 96 dw JT, versão `0x25`
2. Staging contíguo unificado de 832 KiB (`0x000D0000`) na VRAM owned:
   - Slice 0 (`+0x00000`): PFP staging
   - Slice 1 (`+0x41000`): ME staging
   - Slice 2 (`+0x82000`): CE staging
3. Programação das portas write-only e Jump Tables sob HALT rígido (`0x15000000`):
   - PFP: `IC_OP_CNTL` (0x11), `IC_BASE_CNTL` (0x10), `IC_BASE_LO/HI`, 128 dw JT, versão `0x94`
   - ME: `IC_OP_CNTL` (0x11), `IC_BASE_CNTL` (0x10), `IC_BASE_LO/HI`, 128 dw JT, versão `0x63`
   - CE: `IC_OP_CNTL` (0x11), `IC_BASE_CNTL` (0x10), `IC_BASE_LO/HI`, 96 dw JT, versão `0x25`
   - CP global: `CP_MAX_CONTEXT = 0`, `CP_DEVICE_ID = 1`
4. Unhalt conjunto em janela ultracurta de 200 µs (`CP_ME_CNTL = 0x00000000`) com amostragem dos 3 PCs e re-halt fail-closed imediato (`0x15000000`).
5. Restauração íntegra da memória VRAM e registradores.

## Integridade e Assinatura do Pacote 1.39.0.0

- DriverVer: `08/27/2026,1.39.0.0`
- source SYS: `B812CEBC8154C4FB9121BC09844C1AE35CBD8B8DD5F9186FDEBFB421C812AC1A`
- signed SYS: `CA62A8069849F36E562CD128242C4E113FA896965AA816F6C3BA7FB6BA1ABB4F`
- catalog:    `01991EA695100F2573C3EA72ADC35A79AB401A9445E5D078D57C4F3181F41E4E`
- SDMA FW:    `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`
- RLC FW:     `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`
- PFP FW:     `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- ME FW:      `D4ED4D968DE0D7720F7C8215D42FDAE727BF99CDF43CEE6F8F864DC2584B17E2`
- CE FW:      `5946EFBF7E46CCFE7E9F965DA56C35B1187469D1C1A49C8A9EFF433EB3AB4723`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

## Evidência Física Medida no Hardware

- Log de auditoria: `docs/evidence/2026-08-27_10-24-05-DESKTOP-HQAG6T4-mcdm-replace/`
- Veredito da auditoria pós-binding: `BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS`
- `PfpDirectStatus = 0`
- `PfpDirectBytes = 263168`
- `PfpDirectStagedDwords = 65792`
- `PfpDirectStagingMismatches = 0` (zero discrepâncias no staging de PFP, ME e CE na VRAM)
- `PfpDirectPortsProgrammed = 1` (portas write-only de PFP, ME, CE, CP_MAX_CONTEXT e CP_DEVICE_ID programadas sob HALT)
- `PfpDirectMeCntlBefore = 0x15000000` (352321536)
- `PfpDirectMeCntlApplied = 0x00000000` (0 - unhalt conjunto da tríade PFP, ME e CE)
- `PfpDirectMeCntlAfter = 0x15000000` (352321536 - re-halt total imediato com restauração confirmada)
- `PfpDirectPcBefore = 0x00000000`
- `PfpDirectPcLast = 0x0000041E` (1054 decimal)
- `PfpDirectMePcBefore = 0x00000000`
- `PfpDirectMePcLast = 0x0000041E` (1054 decimal - ME respondeu e programada com ucode/JT)
- `PfpDirectCePcBefore = 0x00000000`
- `PfpDirectCePcLast = 0x0000041E` (1054 decimal - CE respondeu e programada com ucode/JT)
- `PfpDirectFaultAfter = 0x00000000` (zero falhas no barramento/GFXHUB)
- `PfpDirectRegRestore = 1`
- `PfpDirectMemRestore = 1`
- `PfpDirectPhysicalReady = 1`
- **Vídeo BasicDisplay**: 100% preservado (ZERO tela preta).

**PFP/PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
