# Relatório de Engenharia — Micro-Gate 7 Fase G: Broadcast GRBM_GFX_INDEX=0xE0000000 e Sincronização de Invalidação de I-Cache (1.46.0.0)

Data: 2026-08-27.

Status: **CANDIDATO 1.46.0.0 PRONTO; BROADCAST DE REGISTRADORES INDEXADOS APLICADO; AGUARDANDO PROVA EM SILÍCIO**.

## Causa-Raiz Física Comprovada

A auditoria física do comportamento do hardware nas versões 1.44 e 1.45 demonstrou que o CP permanecia no trap `0x0000041E` da Boot ROM porque `JT_LOADED` continuava em `0` (enquanto no Linux ativo lê `0x00000003`).

O motivo foi isolado através do código de referência do kernel Linux (`drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c`):

1. **Broadcast GRBM Obrigatório para Registradores Indexados**:
   A APU BC-250 possui múltiplos Shader Engines (SE) e Shader Arrays (SH). As portas MMIO de gravação da Jump Table (`mmCP_HYP_PFP_UCODE_ADDR = 0x3E050` e `mmCP_HYP_PFP_UCODE_DATA = 0x3E054`) são registradores indexados pela topologia gráfica.
   No upstream Linux, a instrução primordial executada antes de tocar em qualquer registrador do CP é:
   `WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, 0xE0000000u);`
   O valor `0xE0000000` ativa:
   - Bit 31: `BROADCAST_SE = 1`
   - Bit 30: `BROADCAST_SH = 1`
   - Bit 29: `BROADCAST_INSTANCE = 1`
   Sem o broadcast ativo, as gravações da Jump Table eram desconsideradas ou roteadas para um banco de instâncias desconectado do microprocessador primário.
2. **Sincronização Ativa de Invalidação da I-Cache**:
   No upstream (`gfx_v10_0.c` linhas 6146-6157), a invalidação de cada I-Cache (PFP, CE, ME) é disparada com `INVALIDATE_CACHE = 1` e o driver **aguarda ativamente** a confirmação do bit `INVALIDATE_CACHE_COMPLETE` (`& 0x100`) antes de programar a base de endereço ou escrever na Jump Table.
   O Candidato **1.46.0.0** implementa essa espera ativa sincronizada para cada microengine sob HALT.
3. **Mapeamento e Whitelist**:
   `GRBM_GFX_INDEX` (`0x00030800`) foi adicionado às whitelists de leitura e escrita com snapshot no preflight e restauração estrita no teardown fail-closed.

---

## Integridade do Pacote 1.46.0.0

- DriverVer: `08/27/2026,1.46.0.0`
- source SYS: `966DC3CE896AE89D4DF2AB09BF6E51536834F2C39A151DFE45508F056E4AC2A6`
- signed SYS: `BC043BC6B7D74DBBC3FE6F4C4F7BC26470064A7A1F38491299E38EDD67250C71`
- catalog:    `6EA0676EF76C0687296117EAF6844E3D47D28DDCD2003EEF8877E0B4B096A5AA`
- SDMA FW:    `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`
- RLC FW:     `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`
- PFP FW:     `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- ME FW:      `D4ED4D968DE0D7720F7C8215D42FDAE727BF99CDF43CEE6F8F864DC2584B17E2`
- CE FW:      `5946EFBF7E46CCFE7E9F965DA56C35B1187469D1C1A49C8A9EFF433EB3AB4723`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

**PM4 RING 0 E COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
