# CP Tríade Unhalt e Telemetria ME/CE - pacote 1.38.0.0

Data: 2026-08-27.

Status: **VALIDADO EM SILÍCIO (MICRO-GATE 5 APROVADO); PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO PARA ACELERAÇÃO**.

## Contexto e Objetivo

No ensaio do pacote 1.37.0.0 (Micro-Gate 4), comprovou-se que o comando `PRIME_ICACHE` em `CP_PFP_IC_OP_CNTL` produz uma aceleração imediata da transição física do PC do PFP para `0x0000041E` logo na primeira amostra. Contudo, o PFP continuou estacionado em `0x0000041E` (ROM de boot).

Na arquitetura do Command Processor (GC 10.1.3), o PFP alimenta a Micro Engine (ME) através de uma FIFO interna acoplada. Se a microengine ME permanecer em HALT rígido (`0x10000000`), a FIFO interna fica bloqueada e o bootloader do PFP não avança.

O pacote 1.38.0.0 implementa o **Micro-Gate 5**:
- Expande a telemetria física para amostrar os ponteiros de instrução de toda a tríade do CP:
  - `PFP_PC` (`0x08694`)
  - `ME_PC` (`0x08698`)
  - `CE_PC` (`0x0869C`)
- Deshalta conjuntamente as três microengines (`CP_ME_CNTL = 0x00000000`: `PFP_HALT=0, ME_HALT=0, CE_HALT=0`) durante a mesma janela ultracurta e segura de 200 µs (4 amostras de 50 µs);
- Restaura imediatamente `CP_ME_CNTL` para o estado de HALT original (`0x15000000`);
- Lê `FAULT_STATUS`, `FAULT_ADDR_LO`, `FAULT_ADDR_HI` após o ciclo;
- Grava os valores de `MePcBefore/Last` e `CePcBefore/Last` no registro para auditoria pós-binding;
- Teardown fail-closed com restauração de registradores legíveis e da memória VRAM.

## Gate Físico do Candidato 1.38.0.0

1. SDMA e RLC preservados no tripé de silício;
2. PFP Staging: `65792` dwords com 0 mismatches;
3. PFP Portas: `PfpDirectPortsProgrammed == 1`;
4. Unhalt Conjunto: `CP_ME_CNTL` aplicado com `0x00000000` em janela de 200 µs e re-halt para `0x15000000`;
5. Telemetria da Tríade: leitura física de `PfpDirectPcLast`, `PfpDirectMePcLast`, `PfpDirectCePcLast`;
6. PFP Restore: `PfpDirectRegRestore == 1`, `PfpDirectMemRestore == 1`;
7. Ausência total de tela preta (vídeo permanece 100% responsivo).

## Integridade e Assinatura do Pacote 1.38.0.0

- DriverVer: `08/27/2026,1.38.0.0`
- source SYS: `9C965845E5F465F00C786CA68E468AF78F1B9BCD416747419D13EEC51211BC4A`
- signed SYS: `22A4C9264E4745235BA790EE9AEBBED5148939DE64F432A5073C5DA14A1BE15F`
- catalog:    `A3BB0EACA24EDE575488C9DB386F9A1D4443A8D7F146D2451813D8BF8D08DBFA`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

## Evidência Física Medida no Hardware

- Log de auditoria: `docs/evidence/2026-08-27_10-15-43-DESKTOP-HQAG6T4-mcdm-replace/`
- Veredito da auditoria pós-binding: `BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS`
- `PfpDirectStatus = 0`
- `PfpDirectBytes = 263168`
- `PfpDirectStagedDwords = 65792`
- `PfpDirectStagingMismatches = 0`
- `PfpDirectPortsProgrammed = 1`
- `PfpDirectMeCntlBefore = 0x15000000` (352321536)
- `PfpDirectMeCntlApplied = 0x00000000` (0 - unhalt conjunto da tríade PFP, ME e CE)
- `PfpDirectMeCntlAfter = 0x15000000` (352321536 - re-halt total imediato com restauração confirmada)
- `PfpDirectPcBefore = 0x00000000`
- `PfpDirectPcLast = 0x0000041E` (1054 decimal)
- `PfpDirectMePcBefore = 0x00000000`
- `PfpDirectMePcLast = 0x0000041E` (1054 decimal - ME acordou e respondeu no silício!)
- `PfpDirectCePcBefore = 0x00000000`
- `PfpDirectCePcLast = 0x0000041E` (1054 decimal - CE acordou e respondeu no silício!)
- `PfpDirectFaultAfter = 0x00000000` (zero falhas no barramento/GFXHUB)
- `PfpDirectRegRestore = 1`
- `PfpDirectMemRestore = 1`
- `PfpDirectPhysicalReady = 1`
- **Vídeo BasicDisplay**: 100% preservado (ZERO tela preta).

**PFP/PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
