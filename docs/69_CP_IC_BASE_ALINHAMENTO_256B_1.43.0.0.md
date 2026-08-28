# Relatório de Engenharia — Micro-Gate 7 Fase D: Alinhamento 256B (>> 8 / >> 40), IC_BASE_CNTL (0x100) e Antecipação de GPA_OVERRIDE (1.43.0.0)

Data: 2026-08-27.

Status: **CANDIDATO 1.43.0.0 PRONTO; ARQUITETURA DE I-CACHE CORRIGIDA; AGUARDANDO PROVA EM SILÍCIO**.

## Causa-Raiz e Correções Fundamentais

No ensaio do Candidato `1.42.0.0`, a tríade de microcódigo do CP estava devidamente staged e verificada na VRAM (832 KiB, 0 mismatches), mas os microprocessadores permaneciam travados no vetor `0x0000041E` (vetor de espera na Boot ROM interna).

O cruzamento com o upstream Linux `amdgpu/gfx_v10_0.c` e com a implementação de referência `wddm_hw.c:748-763` revelou as causas físicas exatas:

1. **Deslocamento Base de I-Cache (`>> 8` e `>> 40`)**:
   Assim como `CP_RB0_BASE`, as portas `CP_*_IC_BASE_LO` no GFX10 esperam o endereço físico de memória em escala de blocos de 256 bytes (`gpuAddr >> 8`), e `CP_*_IC_BASE_HI` espera `gpuAddr >> 40`. O código anterior enviava o byte address cru (`gpuAddr & 0xFFFFF000`), fazendo a hardware multiplicá-lo por 256 e tentar ler de um offset inexistente a 3.8 TB de distância.
2. **Controle de I-Cache `CP_*_IC_BASE_CNTL = 0x00000100u`**:
   O bit 8 (`IC_CNTL__ENABLE` / `ADDRESS_CLAMP`) deve ser ativado com valor `0x00000100u` gravado **estritamente após** `LO` e `HI`. O código anterior gravava `0x10u` antes de `LO` e `HI`.
3. **Antecipação Imediata de `GPA_OVERRIDE`**:
   `CPG_PSP_DEBUG` (`0x3F040`) = `0x8` e `CPC_PSP_DEBUG` (`0x3F044`) = `0x8` foram movidos para o início da rotina sob HALT, antes de qualquer operação de gravação nas portas de IC, garantindo que o controlador de busca ignore tabelas de páginas não inicializadas e acesse a VRAM física diretamente.
4. **Filtro de Status do RPTR (`0xDA6C & 0xFFFF`)**:
   O registrador de status `0xDA6C` reserva os bits [31:16] para flags internas de estado do motor (`0x00200000` / `0x01200000`). O ponteiro de leitura dword real reside nos 16 bits inferiores (`rptr & 0xFFFFu`).
5. **Prova Dupla**:
   Pacote PM4 com marcador `0xBC250143`, mutação de `CP_SCRATCH` (`0x32D4`) para `0x22222222` e fence de silício na VRAM (`0xBC250123`).

---

## Integridade do Pacote 1.43.0.0

- DriverVer: `08/27/2026,1.43.0.0`
- source SYS: `BE35432EE8AE5BC60A214E8F9A6FD98FF5C55659F8BF24A031BB903DE6575A20`
- signed SYS: `17BC6D1E4EBC8F8FFB21AEB11C83DADDAEEA8BCC0CD6D91A8A8ED835299EE250`
- catalog:    `455B19C1689D786685120B4F68AF5516D67A50840F16F65B29B6BA7BC6AC9E58`
- SDMA FW:    `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`
- RLC FW:     `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB`
- PFP FW:     `50E56DC1913571FF589425D2B059BDDFC5E8A2B4BC22D3EB78CAC75D1E0479A1`
- ME FW:      `D4ED4D968DE0D7720F7C8215D42FDAE727BF99CDF43CEE6F8F864DC2584B17E2`
- CE FW:      `5946EFBF7E46CCFE7E9F965DA56C35B1187469D1C1A49C8A9EFF433EB3AB4723`
- signer:     `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`

**PM4 RING 0 E COMPUTE NÃO EXECUTADOS; NÃO VALIDADO EM HARDWARE**.
