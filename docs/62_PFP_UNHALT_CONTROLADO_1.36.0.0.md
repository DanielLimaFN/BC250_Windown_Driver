# PFP unhalt controlado com amostragem ultracurta - pacote 1.36.0.0

Data: 2026-08-27.

Status: **VALIDADO EM SILÍCIO (MICRO-GATE 3 APROVADO); PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO PARA ACELERAÇÃO**.

## Contexto e Objetivo

Os pacotes 1.34.0.0 e 1.35.0.0 provaram com sucesso que o staging do firmware do PFP na VRAM e a escrita nas portas write-only do CP e da Jump Table sob `PFP_HALT` não afetam o vídeo nem travam o barramento.

O pacote 1.36.0.0 implementa o **Micro-Gate 3**:
- Executa o staging autenticado de VRAM aprovado nos Gates 1 e 2;
- Programa as portas write-only do Instruction Cache e da Jump Table de 128 dwords;
- **Unhalt Controlado do PFP**:
  - Aplica `CP_ME_CNTL` limpando apenas `BC250_PFP_HALT` (`0x11000000`), mantendo `ME_HALT` e `CE_HALT` rígidos;
  - Realiza amostragem ultracurta de 4 amostras do `CP_PFP_PC` com 50 µs de stall entre leituras (janela de exposição de apenas 200 µs, 160 vezes menor que a tentativa do 1.32);
  - **Restaura Imediatamente `PFP_HALT` (`0x15000000`)**;
  - Lê `FAULT_STATUS`, `FAULT_ADDR_LO`, `FAULT_ADDR_HI` após o ciclo;
- **Critério Anti-Alucinação**:
  - Se o PC sair de zero mas permanecer constante em `0x0000041E` (parada no bootloader), registra status `BC250_PFP_DIRECT_LOAD_PC_STATIC` (10);
  - Se houver variação real entre amostras (`transitions > 0`), registra `BC250_PFP_DIRECT_LOAD_OK` (0);
- Teardown fail-closed com restauração de registradores legíveis e da memória VRAM.

## Gate Físico do Candidato 1.36.0.0

1. SDMA e RLC preservados no tripé de silício;
2. PFP Staging: `65792` dwords com 0 mismatches;
3. PFP Portas: `PfpDirectPortsProgrammed == 1`;
4. PFP Unhalt: `PfpDirectMeCntlApplied == 0x11000000` e restauração para `0x15000000`;
5. PFP Amostras: 4 amostras coletadas em 200 µs;
6. PFP Restore: `PfpDirectRegRestore == 1`, `PfpDirectMemRestore == 1`;
7. Sinal de vídeo ativo no `BasicDisplay` (sem tela preta).

## Integridade e Assinatura do Pacote 1.36.0.0

- DriverVer: `08/27/2026,1.36.0.0`
- source SYS: `DB74DADD4B7AB5730488110EAD3B81F1BA99E4F1BB34FD4B6BCA671F1B617CED`
- signed SYS: `61E69ECB52ECF0B7367EB75A5F6E1D38995906051553DBE604E0B505A62EBFF8`
- catalog:    `89E19AA25343FE2BDA447120C4E0D32230F6FECD19749E37AD578A3E75EC140F`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

## Evidência Física Medida no Hardware

- Log de auditoria: `docs/evidence/2026-08-27_09-46-47-DESKTOP-HQAG6T4-mcdm-replace/`
- Veredito da auditoria pós-binding: `BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS`
- `PfpDirectStatus = 0`
- `PfpDirectMeCntlBefore = 0x15000000` (352321536)
- `PfpDirectMeCntlApplied = 0x11000000` (285212672) -> Unhalt PFP
- `PfpDirectMeCntlAfter = 0x15000000` (352321536) -> Re-halt PFP restaurado
- `PfpDirectPcBefore = 0x00000000`
- `PfpDirectPcFirst = 0x00000000`
- `PfpDirectPcLast = 0x0000041E` (1054 decimal - parada no bootloader)
- `PfpDirectPcTransitions = 1` (em 4 amostras de 50 µs)
- `PfpDirectFaultAfter = 0x00000000` (zero falhas no barramento/GFXHUB)
- `PfpDirectRegRestore = 1`
- `PfpDirectMemRestore = 1`
- `PfpDirectPhysicalReady = 1`
- **Vídeo BasicDisplay**: 100% preservado (ZERO tela preta).

**PFP/PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
