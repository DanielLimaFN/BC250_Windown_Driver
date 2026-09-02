# PFP portas write-only e Jump Table sob HALT - pacote 1.35.0.0

Data: 2026-08-27.

Status: **VALIDADO EM SILÍCIO (MICRO-GATE 2 APROVADO); PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO PARA ACELERAÇÃO**.

## Contexto e Objetivo

O pacote 1.34.0.0 (Micro-Gate 1) provou no silício que o staging de 65.792 dwords do payload de `cyan_skillfish2_pfp.bin` em VRAM owned e sua restauração fail-closed ocorrem perfeitamente sem afetar o sinal de vídeo do `BasicDisplay` (ativo em `0xC0000000`).

O pacote 1.35.0.0 implementa o **Micro-Gate 2**:
- Executa o staging autenticado de VRAM aprovado no 1.34;
- Escreve as portas write-only do CP (`RLC_CNTL`, `PFP_IC_OP_CNTL`, `PFP_IC_BASE_CNTL`, `PFP_IC_BASE_LO`, `PFP_IC_BASE_HI`);
- Descarrega a **Jump Table** de 128 dwords via `PFP_UCODE_ADDR` e `PFP_UCODE_DATA`;
- Escreve a versão do ucode (`0x94`) em `PFP_UCODE_ADDR`;
- **PRESERVA RIGOROSAMENTE O `PFP_HALT` EM `CP_ME_CNTL` (`0x15000000`)**: a engine não é deshaltada;
- Captura `FAULT_STATUS`, `FAULT_ADDR_LO`, `FAULT_ADDR_HI` após a escrita;
- Restaura registradores e a memória VRAM no teardown fail-closed;
- Observa se o vídeo do display permanece íntegro.

## Contrato do Firmware PFP

- Arquivo: `cyan_skillfish2_pfp.bin` (263.424 bytes), SHA-256:
  `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- Payload em `+0x100`: 263.168 bytes, SHA-256:
  `5AA30F3B2E1C697AD2BFE02643777552D5351EC6813572271F01320F65CA7367`
- Jump Table: dword 65536, 128 dwords; versão `0x94`.

## Gate Físico do Candidato 1.35.0.0

1. SDMA repetida com sucesso: `RPTR MMIO == WPTR == 68`, fence física `0x43DAFFFE -> 0xBC250123`, 4 dwords corretos;
2. RLC repetido com sucesso: tipo 8 aceito, `RLC_CNTL 0 -> 1`, `EXEC_ROM_CODE 1 -> 0`;
3. PFP Staging: `PfpDirectStatus == 0`, `PfpDirectBytes == 263168`, `PfpDirectStagedDwords == 65792`, `PfpDirectStagingMismatches == 0`;
4. PFP Portas: `PfpDirectPortsProgrammed == 1` (portas do CP programadas);
5. PFP HALT: `PfpDirectMeCntlApplied == 0x15000000` (HALT preservado);
6. PFP Restore: `PfpDirectRegRestore == 1`, `PfpDirectMemRestore == 1` e `PfpDirectPhysicalReady == 1`;
7. Ausência total de tela preta (vídeo permanece 100% responsivo).

## Integridade e Assinatura do Pacote 1.35.0.0

- DriverVer: `08/27/2026,1.35.0.0`
- source SYS: `CE24C9397E95757BF28E9A77DB262F31C417C4194DC8FA653B9EA7CE00321B4B`
- signed SYS: `D3EF8357E390A193A9542C0DD01DF590E0F4903A4940009C2169D3314244B261`
- catalog:    `77D19669F5A181774894A971DD0237622BF6EBE493F733CC68501382D00255F7`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

## Evidência Física Medida no Hardware

- Log de auditoria: `docs/evidence/2026-08-27_09-36-29-DESKTOP-HQAG6T4-mcdm-replace/`
- Veredito da auditoria pós-binding: `BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS`
- `PfpDirectStatus = 0`
- `PfpDirectBytes = 263168`
- `PfpDirectStagedDwords = 65792`
- `PfpDirectStagingMismatches = 0`
- `PfpDirectPortsProgrammed = 1` (todas as portas do CP programadas sob HALT)
- `PfpDirectMeCntlApplied = 0x15000000` (HALT estrito preservado)
- `PfpDirectFaultAfter = 0x00000000` (zero falhas no barramento/GFXHUB)
- `PfpDirectRegRestore = 1`
- `PfpDirectMemRestore = 1`
- `PfpDirectPhysicalReady = 1`
- **Vídeo BasicDisplay**: 100% preservado (ZERO tela preta).

**PFP/PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
