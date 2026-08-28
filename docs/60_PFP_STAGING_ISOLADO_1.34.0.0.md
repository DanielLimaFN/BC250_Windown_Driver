# PFP staging isolado sob HALT absoluto - pacote 1.34.0.0

Data: 2026-08-27.

Status: **VALIDADO EM SILÍCIO (MICRO-GATE 1 APROVADO); PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO PARA ACELERAÇÃO**.

## Contexto e Objetivo

O pacote 1.32.0.0 tentou o carregamento simultâneo do RLC e o unhalt do PFP (`CP_ME_CNTL &= ~BC250_PFP_HALT`), resultando em tela preta (apagamento do sinal de vídeo do `BasicDisplay` ativo em `0xC0000000`). O pacote 1.33.0.0 isolou o RLC e provou com sucesso físico que o firmware RLC sai da ROM (`EXEC_ROM_CODE: 1 -> 0`) sem causar impacto no display.

Conforme a estratégia de decomposição em micro-gates definida em `docs/59_HANDOFF_IA_2026-08-27.md`, o pacote 1.34.0.0 implementa o **Micro-Gate 1**:
- Isola a alocação de 0x41000 bytes de VRAM owned (CPU-visible);
- Realiza o staging do payload autenticado de `cyan_skillfish2_pfp.bin` (65.792 dwords);
- Compara dword a dword via readback da VRAM contra a CPU (`StagingMismatches == 0`);
- Captura snapshot de registradores não destrutivos de telemetria inicial (`CP_ME_CNTL`, `RLC_CNTL`, `CP_PFP_PC`, `FAULT_STATUS`);
- **Preservação de HALT estrito e ZERO escritas no CP**: nenhuma porta write-only (`PFP_IC_*`, `PFP_UCODE_*`) é tocada e `CP_ME_CNTL` permanece intocado com `PFP_HALT` ativo;
- Restaura o backup da VRAM e verifica integridade do restore dword a dword (`MemoryRestorePassed == 1`);
- Libera a VRAM owned e desmapeia a abertura.

## Contrato do Firmware PFP

- Arquivo: `cyan_skillfish2_pfp.bin` (263.424 bytes), SHA-256:
  `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- Payload em `+0x100`: 263.168 bytes, SHA-256:
  `5AA30F3B2E1C697AD2BFE02643777552D5351EC6813572271F01320F65CA7367`
- Jump Table: dword 65536, 128 dwords; versão `0x94`.

## Gate Físico do Candidato 1.34.0.0

1. SDMA repetida com sucesso: `RPTR MMIO == WPTR == 68`, fence física `0x43DAFFFE -> 0xBC250123`, 4 dwords corretos;
2. RLC repetido com sucesso: tipo 8 aceito, `RLC_CNTL 0 -> 1`, `EXEC_ROM_CODE 1 -> 0`;
3. PFP Staging: `PfpDirectStatus == 0`, `PfpDirectBytes == 263168`, `PfpDirectStagedDwords == 65792`, `PfpDirectStagingMismatches == 0`;
4. PFP Portas: `PfpDirectPortsProgrammed == 0` (portas do CP intocadas);
5. PFP Restore: `PfpDirectMemRestore == 1` e `PfpDirectPhysicalReady == 1`;
6. Ausência total de tela preta (vídeo permanece 100% responsivo).

## Integridade e Assinatura do Pacote 1.34.0.0

- DriverVer: `08/27/2026,1.34.0.0`
- source SYS: `57C0DEEC04426ED0B48AC0910E7D09A1824516177AFE2B1697DAA9667B3C7F13`
- signed SYS: `FB1A57F4BF95EF71AEDFCD3B5AE20FBEBFED81096A8617CD6A84A7EF5E277A73`
- catalog:    `0DB8EE1D77F6DDBBBC42C160C9603924AF79C6D5A0B180A26F1DEB06B889A55B`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

## Evidência Física Medida no Hardware

- Log de auditoria: `docs/evidence/2026-08-27_09-19-21-DESKTOP-HQAG6T4-mcdm-replace/`
- Veredito da auditoria pós-binding: `BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS`
- `PfpDirectStatus = 0`
- `PfpDirectBytes = 263168`
- `PfpDirectStagedDwords = 65792`
- `PfpDirectStagingMismatches = 0`
- `PfpDirectPortsProgrammed = 0` (portas do CP intocadas sob HALT)
- `PfpDirectMemRestore = 1`
- `PfpDirectPhysicalReady = 1`
- **Vídeo BasicDisplay**: 100% preservado (ZERO tela preta).

**PFP/PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
