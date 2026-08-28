# Relatório de Engenharia — Micro-Gate 7 Fase E: Un-gate de Clocks do CPF/Graphic Engine, Ordem PFP->CE->ME e Preâmbulo Canônico PM4 (1.44.0.0)

Data: 2026-08-27.

Status: **CANDIDATO 1.44.0.0 PRONTO; SEQUÊNCIA AUTORITATIVA UPSTREAM APLICADA; AGUARDANDO PROVA EM SILÍCIO**.

## Causa-Raiz e Alinhamento com a Arquitetura Upstream

A análise da persistência no vetor de espera `0x0000041E` sob a ótica dos documentos de auditoria física (`docs/10_AUDITORIA_INIT_GFX_CYAN_SKILLFISH.md`) e do código de referência do Linux (`drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c`) revelou três bloqueios que impediam o motor do CP de buscar microcódigo e processar pacotes PM4:

1. **Clock Gating do Command Processor Fetcher (`CGTT_CPF_CLK_CTRL`)**:
   Por padrão de hardware, o clock do CPF (unidade que busca as instruções da memória) fica desligado (clock gated). O upstream Linux escreve `0xF8000100u` em `CGTT_CPF_CLK_CTRL` (`0x0003C2C4`), desativando o gating dos 5 blocos de fetch. O Candidato **1.44.0.0** aplica a máscara SOC15 `(lido & ~0xFCFF8FFF) | (0xF8000100 & 0xFCFF8FFF)` sob HALT.
2. **Clocks Rápidos do Graphic Engine (`GE_FAST_CLKS`) e Streaming (`CP_SD_CNTL`)**:
   - `GE_FAST_CLKS` (`0x00008920`) configurado com `0x0000493Eu`;
   - `CP_SD_CNTL` (`0x0000C6DC`) configurado com `0x000005FFu`;
   - `GRBM_GFX_CNTL` (`0x00030804`) comutado explicitamente para a Pipe 0.
3. **Ordem Autoritativa do Direct Load Gráfico (PFP -> CE -> ME)**:
   No GFX10, a MicroEngine (ME) é o controlador mestre que coordena o Prefetch Parser (PFP) e a Constant Engine (CE). O upstream programa rigorosamente **PFP primeiro, CE em segundo e ME por último**. Programar ME antes de CE deixava a ME tentando sincronizar com uma microengine sem microcódigo carregado. O Candidato **1.44.0.0** restaura a ordem correta `PFP -> CE -> ME`.
4. **Preâmbulo Canônico Obrigatório PM4 (`PREAMBLE_CNTL` + `CONTEXT_CONTROL`)**:
   O microcódigo da ME no GFX10 recusa comandos genéricos enquanto a máquina de estados de contexto não for inicializada. O anel agora inicia com a sequência canônica de preâmbulo:
   - Pacote 1 (2 dw): `PACKET3_PREAMBLE_CNTL` (`0xC0004A00`), valor `2` (`PACKET3_PREAMBLE_BEGIN_CLEAR_STATE`);
   - Pacote 2 (3 dw): `PACKET3_CONTEXT_CONTROL` (`0xC0012800`), `0x80000000`, `0x80000000` (ativação de contexto de hardware);
   - Pacote 3 (2 dw): `PACKET3_NOP` (`0xC0001000`, marker `0xBC250144`);
   - Pacote 4 (5 dw): `PACKET3_WRITE_DATA` para registrador `CP_SCRATCH` (`0x22222222`);
   - Pacote 5 (5 dw): `PACKET3_WRITE_DATA` gravando a fence na VRAM (`0xBC250123`);
   - Cursor total: **17 dwords**.
5. **Rollback e Teardown Fail-Closed**:
   Todos os novos registradores (`CGTT_CPF_CLK_CTRL`, `GE_FAST_CLKS`, `CP_SD_CNTL`, `GRBM_GFX_CNTL`) são restaurados no LIFO e verificados.

---

## Integridade do Pacote 1.44.0.0

- DriverVer: `08/27/2026,1.44.0.0`
- source SYS: `1359C6B7B5E062E2DB5CE21D1229B038365ECAB19D666FECBB0E040ED6EC35D6`
- signed SYS: `766304399E63773273827A0C5A0F7006A0B5A3B818F9E4A8C75230ED00CB0920`
- catalog:    `D7C941CC00FD6FC02F16831E6463A9CF9EA97A99059AB0E1B27475838A830016`
- SDMA FW:    `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`
- RLC FW:     `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`
- PFP FW:     `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- ME FW:      `D4ED4D968DE0D7720F7C8215D42FDAE727BF99CDF43CEE6F8F864DC2584B17E2`
- CE FW:      `5946EFBF7E46CCFE7E9F965DA56C35B1187469D1C1A49C8A9EFF433EB3AB4723`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

**PM4 RING 0 E COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
