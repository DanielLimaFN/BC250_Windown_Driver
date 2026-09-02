# Relatório de Engenharia — Micro-Gate 7 Fase F: Ativação de GCVM Context 0 (VMID 0) com Page Table de 512 PTEs para I-Cache e Ring PM4 (1.45.0.0)

Data: 2026-08-27.

Status: **CANDIDATO 1.45.0.0 PRONTO; TRADUÇÃO DE HARDWARE VMID 0 ATIVADA; AGUARDANDO PROVA EM SILÍCIO**.

## Causa-Raiz Física Comprovada

A investigação dos motivos pelos quais as microengines PFP, ME e CE permaneciam no endereço `0x0000041E` da Boot ROM revelou o elo final da arquitetura de hardware da APU BC-250 (GC 10.1.3):

1. **A Descoberta de `GPA_OVERRIDE`**:
   O registrador de segurança `0x0003F040` (`CPG_PSP_DEBUG`) lê `0xFFFFFFFF` e não permite ativação de `GPA_OVERRIDE` a partir do host no Windows (`tools/test-cp-golden-preflight.ps1`). Sem `GPA_OVERRIDE`, o Command Processor **não possui modo físico direto de DMA**: todo acesso à memória emitido pela Instruction Cache (IC) ou pelo Ring PM4 é obrigatoriamente submetido à Unidade de Tradução de Memória Gráfica (**GFXHUB MMU via VMID 0**).
2. **Contexto 0 (VMID 0) Desabilitado no Boot do Windows**:
   No Linux (onde o CP roda com sucesso), a auditoria do sweep confirmou que:
   - `GCVM_CONTEXT0_CNTL` (`0x0000A200`) está configurado como `0x007FFE01` (`ENABLE_CONTEXT = 1`);
   - `GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO` (`0x0000A3AC`) aponta para uma Page Table válida na VRAM;
   - `IC_BASE_LO/HI` aponta para uma GPU Virtual Address (GPUVA) mapeada no Contexto 0.
   No Windows, até o Candidato 1.44.0.0, o `GCVM_CONTEXT0_CNTL` permanecia desligado (`0`), e o miniport passava endereços físicos brutos (`0xF40FAEE000`) para `IC_BASE`. Como o Contexto 0 estava desligado, a GFXHUB MMU abortava qualquer tentativa da Instruction Cache de ler instruções na memória, forçando as microengines a girar em loop infinito no trap de erro da Boot ROM em `0x0000041E`.
3. **Solução Arquitetural do Candidato 1.45.0.0**:
   - Uma **Page Table de 512 PTEs** (4 KiB) é gravada na VRAM no offset `0x000C6000` da nossa alocação owned de 832 KiB;
   - As 512 páginas mapeiam o intervalo linear de GPUVA `0x00000000` a `0x00200000` (2 MiB) para os endereços físicos correspondentes na VRAM;
   - Cada PTE é marcada com as flags autoritativas `VALID (1) | READABLE (0x20) | WRITEABLE (0x40) | EXECUTABLE (0x80)` (`0xE1`);
   - Os registradores de Contexto 0 da GFXHUB MMU são programados sob HALT:
     - `A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO = ptMc | 1`;
     - `A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI = ptMc >> 32`;
     - `A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO = 0`;
     - `A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO = 0x00200000 >> 12`;
     - `A_GCVM_CONTEXT0_CNTL = 0x007FFE01` (Context 0 Habilitado);
   - TLB Flush de VMID 0 emitido e confirmado via ACK na engine GFX;
   - `IC_BASE` das três microengines e `RB0_BASE` configurados com os endereços virtuais traduzidos correspondentes no Contexto 0;
   - Sequência canônica PM4 (17 dwords) disparada;
   - Rollback LIFO e restauração de 100% dos registradores e VRAM garantida.

---

## Integridade do Pacote 1.45.0.0

- DriverVer: `08/27/2026,1.45.0.0`
- source SYS: `DEE70F2AB06843BD313984CD9B00166223025AD9840A6CD0D49868933C715A0D`
- signed SYS: `15808A25A8D07B7E67FDFD7CDD1F10EE7878BE33C3375FDC2CFC4DBD14C22BE4`
- catalog:    `45A4749D58AEF9947DD63DD4335F7ADB868BF81E441E4F664D41BD6DCD497352`
- SDMA FW:    `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`
- RLC FW:     `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`
- PFP FW:     `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- ME FW:      `D4ED4D968DE0D7720F7C8215D42FDAE727BF99CDF43CEE6F8F864DC2584B17E2`
- CE FW:      `5946EFBF7E46CCFE7E9F965DA56C35B1187469D1C1A49C8A9EFF433EB3AB4723`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

**PM4 RING 0 E COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
