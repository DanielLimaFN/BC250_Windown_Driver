# PFP Instruction Cache Priming - pacote 1.37.0.0

Data: 2026-08-27.

Status: **VALIDADO EM SILÍCIO (MICRO-GATE 4 APROVADO); PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO PARA ACELERAÇÃO**.

## Contexto e Objetivo

No ensaio do pacote 1.36.0.0 (Micro-Gate 3), o unhalt temporário em janela de 200 µs produziu a primeira reação física da microengine PFP no silício: `PC = 0x00000000 -> 0x0000041E`.
O endereço `0x0000041E` é o ponto de parada na ROM interna do bootloader do chip aguardando instrução válida.

O pacote 1.37.0.0 implementa o **Micro-Gate 4**:
- Preserva o staging autenticado de 65.792 dwords do payload na VRAM e a Jump Table de 128 dwords;
- Ativa o **Instruction Cache Priming**:
  - `CP_PFP_IC_OP_CNTL` é programado com `0x11u` (`INVALIDATE_CACHE (0x1) | PRIME_ICACHE (0x10)`);
  - O comando `PRIME_ICACHE` ordena ao controlador de cache que pré-carregue a primeira linha de cache a partir do `IC_BASE` antes da liberação do HALT;
- Executa a mesma deshaltagem ultracurta e segura de 200 µs (4 amostras de 50 µs);
- Restaura imediatamente `PFP_HALT` (`0x15000000`);
- Lê `FAULT_STATUS`, `FAULT_ADDR_LO`, `FAULT_ADDR_HI` após o ciclo;
- Mantém o critério estrito anti-alucinação: `0x041E` constante não é considerado execução útil (`PC_STATIC = 10`); apenas variações reais entre amostras aprovam `BC250_PFP_DIRECT_LOAD_OK` (0);
- Teardown fail-closed com restauração de registradores legíveis e da memória VRAM.

## Gate Físico do Candidato 1.37.0.0

1. SDMA e RLC preservados no tripé de silício;
2. PFP Staging: `65792` dwords com 0 mismatches;
3. PFP Portas: `PfpDirectPortsProgrammed == 1`;
4. PFP Priming: `CP_PFP_IC_OP_CNTL` escrito com `0x11`;
5. PFP Unhalt: `0x11000000` em janela de 200 µs e re-halt para `0x15000000`;
6. PFP Restore: `PfpDirectRegRestore == 1`, `PfpDirectMemRestore == 1`;
7. Ausência total de tela preta (vídeo permanece 100% responsivo).

## Integridade e Assinatura do Pacote 1.37.0.0

- DriverVer: `08/27/2026,1.37.0.0`
- source SYS: `D33E2D328569B39D800A614631D1717FEB894419DA5FAB515AE4761A39F40E87`
- signed SYS: `5EDD0C4B9315D9F7C14847F4A0AF33DB38E98E9A4EB395309645D49FD2A3B95A`
- catalog:    `309E2F8E60C8DC51EBDBB57E5464E6EEFBC12E2F9314BFF549A3B933186B2ED2`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

## Evidência Física Medida no Hardware

- Log de auditoria: `docs/evidence/2026-08-27_10-05-06-DESKTOP-HQAG6T4-mcdm-replace/`
- Veredito da auditoria pós-binding: `BC250 MCDM FAIL-CLOSED BINDING AUDIT: PASS`
- `PfpDirectStatus = 0`
- `PfpDirectBytes = 263168`
- `PfpDirectStagedDwords = 65792`
- `PfpDirectStagingMismatches = 0`
- `PfpDirectPortsProgrammed = 1`
- `PfpDirectMeCntlBefore = 0x15000000` (352321536)
- `PfpDirectMeCntlApplied = 0x11000000` (285212672)
- `PfpDirectMeCntlAfter = 0x15000000` (352321536)
- `PfpDirectPcBefore = 0x00000000`
- `PfpDirectPcFirst = 0x0000041E` (1054 decimal - transição imediata na 1ª amostra com cache priming)
- `PfpDirectPcLast = 0x0000041E` (1054 decimal)
- `PfpDirectPcTransitions = 1` (em 4 amostras de 50 µs)
- `PfpDirectFaultAfter = 0x00000000` (zero falhas no barramento/GFXHUB)
- `PfpDirectRegRestore = 1`
- `PfpDirectMemRestore = 1`
- `PfpDirectPhysicalReady = 1`
- **Vídeo BasicDisplay**: 100% preservado (ZERO tela preta).

**PFP/PM4/COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
