# 492 — M1: recursos "só GPU" na memória do sistema (segmento 2), e o critério da Corrida 149

> 28/09/2026, offline. Desenho no doc 491. A decisão de seguir pela memória unificada é do Daniel, depois da Corrida 148.

## 1. O que mudou
- **Contrato (`bc250_allocation_abi_contract.*`):**
  - flag nova `BC250_ALLOCATION_FLAG_GPU_VA_ONLY` (0x4). Ela significa que nenhum caminho do KMD endereça a alocação por MC, que só é tocada pela GPU via GPUVA e pela CPU via Lock;
  - **só vale para a classe RESOURCE**, e é recusada na formatação e no parse em qualquer outra classe;
  - `Bc250AllocationMayUseSystemSegment` é a fonte única da decisão.
- **UMD (`bc250_d3d_umd.c`):** `binding.GpuVaOnly` é ligado para recursos não primários e não staging/dynamic, sem RENDER_TARGET, DEPTH_STENCIL, UAV ou DECODER, e sem MISC de compartilhamento ou present (0x2, 0x8, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000). São as texturas só de leitura e os buffers.
- **Winsys (`bc250_wddm_winsys.c`):** manda a flag só com o vínculo não primário, não compartilhado e de classe RESOURCE. Log `Allocate M1 GPU_VA_ONLY #N` nas 8 primeiras e depois 1 em 256.
- **KMD (`DxgkDdiCreateAllocation`):** esses recursos saem com `SupportedRead/WriteSegmentSet = 0x3` e `PreferredSegment = 2, depois 1`. Os demais continuam com `0x1`. Log `[M1_SYSTEM_SEGMENT] #N`, com teto próprio e sem amostragem global.
- **Segurança:** se algum caminho do KMD resolver uma dessas alocações, ela sai com `Seg=2`. A DX.91 e `Bc250SegmentOffsetToMc` recusam com log nomeado; não copiam.

## 2. Testes
- `allocation_abi_contract_test`, caso novo `TestAbiGpuVaOnlySystemSegment`: RESOURCE aceito; PRIMARY e COMMAND recusados na formatação; blob forjado recusado no parse; decisão verdadeira só para RESOURCE com a flag.
- `active_miniport_test`: recurso GPU_VA_ONLY sai com SegSet 0x3 e preferência 2,1. O contraponto sem a flag fica em 0x1/preferência 1, e o compartilhado fica em 0x1.
- `bc250_wddm_winsys_test`:
  - recurso comum sem vínculo GpuVaOnly não recebe a flag;
  - compartilhado com GpuVaOnly pedido não recebe;
  - recurso comum com GpuVaOnly recebe CPU_VISIBLE e GPU_VA_ONLY;
  - **defeito latente do teste corrigido:** as estruturas de alocação não eram zeradas e passavam por sorte do layout da pilha.
- **Portão `audit_m1_system_segment_only_for_gpu_va_only`**, com **cinco mutações e cinco reprovações**: guarda de primária no KMD, render target na UMD, compartilhado na UMD, guarda de compartilhado na winsys, parse aceitando fora de RESOURCE.
- `build-contract` EXIT 0, `build-umd` EXIT 0, build de laboratório EXIT 0, auditorias de segurança e de pacote PASS.

| Artefato (3.299.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `457C9CFC9E0938CDC3CA64226B5B5F52374297D3A236177451D0782F4C0AAEC7` |
| `bc250mcdm.sys` fonte lab | `15785038F69F7AA63A22DD2C1235979A5C8BF8F3E0B218CD25238A66DAF7988E` |
| `bc250umd.dll` | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `90DDB7AC5392DC07A234B32C63649CD796EAA21EEC6A3CDBE016ECB6E9F39A56` |
| `bc250mcdm.cat` | `D0C21CBCFC8B946FC2F39A7894A5E18A7992F72F7D674CF610900E59FFFA6918` |

O SYS assinado contém `M1_SYSTEM_SEGMENT` e `RANGE_SCAN RECUSADO`; a UMD contém `M1 GPU_VA_ONLY`.

## 3. Critério da Corrida 149 (escrito antes)
**Runner:** `tools\EXECUTAR_CORRIDA149_M1_600S.cmd`, sem KD, harness atrasado 150 s. **Roteiro da 148:** desktop, Gerenciador de Tarefas e depois o navegador. Boot frio antes.

**Linha de base:** 7.340 transferências `Dir=0` (VRAM→sistema) em 600 s na 146, a única corrida recente com as linhas `DX91_PAGING_MOVE` e sem trava. As 147 e 148 travaram antes dos 600 s.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `[M1_SYSTEM_SEGMENT]` no KMD e `Allocate M1 GPU_VA_ONLY` na UMD; `DX91_PAGING_MOVE op=TRANSFER Dir=0` **abaixo de 3.670** (metade da base) nos 600 s; desktop correto; sem trava com o roteiro | A M1 alivia a VRAM e a trava não veio no roteiro que travava. Próximo: M2. |
| **B** | Marcas M1 presentes, sem trava, mas `Dir=0` não cai | O VidMm não honrou a preferência; ler as PTEs `Seg=2` dos recursos no `UpdatePT`. |
| **C** | Marcas M1 presentes e trava | Aliviar o despejo não basta; comparar N e o despejo antes da trava com a 148. |
| **D** | Regressão visual (janelas pretas ou lixo) ou recusa nomeando `Seg=2` na composição/Present | Um recurso GPU_VA_ONLY era endereçado pelo KMD; estreitar a classe na UMD. |
| **E** | Algo fora do mapa, incluindo marcas M1 ausentes, que indicam defeito de pacote/instrumentação | Descrever antes de julgar. |

**NADA DA M1 FOI VALIDADO EM HARDWARE.**
