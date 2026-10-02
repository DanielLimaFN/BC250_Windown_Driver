# 512 — Referências macOS (RDNA1) e Linux: a AMD atualiza tabelas de página pela GPU; nós, pela CPU

> 28/09/2026, offline. Base: o retrato da trava da Corrida 158 (doc 511). No instante da trava, a CPU00 estava dentro de `UPDATE_PAGE_TABLE` (CPU escrevendo PTEs na VRAM pela BAR) e a CPU09 caminhava tabelas de página pela BAR.

## 1. Linux (P1, `drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c`, upstream)
- `vm->use_cpu_for_update = !!(adev->vm_manager.vm_update_mode & AMDGPU_VM_USE_CPU_FOR_GFX);`
- Padrão: *"by default, only in large BAR systems Compute VM tables will be updated by CPU"*. `vm_update_mode = AMDGPU_VM_USE_CPU_FOR_COMPUTE` só em x86_64 **com VRAM inteira visível**; caso contrário vale 0, e **tudo é atualizado pelo SDMA**.
- Aviso do próprio driver: *"CPU update of VM recommended only for large BAR system"*.
- **BC-250:** BAR de 256 MB, VRAM não inteira visível, então no Linux **nenhuma** tabela de página seria atualizada pela CPU.

## 2. macOS RDNA1 (P1 por nomes de símbolos e strings, sem desmontagem)
**Arquivos:**
- o driver Navi (GFX10) é o conjunto **X6000** (`AMDRadeonX6000.kext` e `AMDRadeonX6700HWLibs.kext`);
- o X5000 é Vega;
- **não há Cyan Skillfish** (13FE, gfx1013, GC_10_1_3): só GC_10_1_0/1/2 e 10_3.

**Tabelas de página:**
- Existem os dois caminhos. O de GPU usa uma **fila de paginação dedicada do SDMA** (`SDMA0_PAGE`, `_sdma_5_0_start_paging_queue`, `_sdma_5_0_submit_frame_paging_queue`) com o gerador de PTE/PDE (`SDMA_OP_PTEPDE`, `SDMA_SUBOP_PTEPDE_GEN`/`_COPY`/`_RMW`, `writeWritePTEPDECommand`, `updateContiguousPTEsWithDMAUsingAddr`).
- A ordem é garantida por barreiras (`beginVMPTUpdate`/`endVMPTUpdate`, `addToPageTableUpdateBarrier`, `checkVMUpdated`).
- O acesso da CPU às tabelas passa por `prepareVmBlockForCPUAccess`/`completeVmBlockForCPUAccess` explícitos.

**Invalidação de TLB:**
- também **no próprio anel SDMA** (`writeVMInvalidateCommand`, `SDMA_SUBOP_INVALIDATION`), depois das escritas;
- serialização por VMID (`waitForVMID`, `lockContext`);
- **não** há string de semáforo `VM_INVALIDATE_ENG*_SEM`.

**Outros:**
- HDP: `flushHDPCache`, `_hdp_5_0_invalidate_flush` e `flushAndInvalidateCaches` depois de atualizar o GART.
- `Info.plist` Navi10: **`PP_GfxOffControl=0`** (GFXOFF desligado no macOS).
- Errata de memória em Navi10: *"SMU 11.0 failed to enable UMC hybrid CDR workaround for Navi10!"* (`_smu_11_0_umc_hybrid_cdr_workaround`). É uma correção de GDDR6/UMC **feita pelo SMU**. Não sabemos se a BC-250 precisa dela, e o nosso driver não fala com o SMU.

## 3. O que isso significa
**P0 + P1:** o nosso driver declara `DXGK_PAGETABLEUPDATE_CPU_VIRTUAL` (`bc250_mcdm_callbacks.c:2728`). O VidMm entrega a tabela mapeada e o driver escreve as PTEs **com a CPU, pela BAR**, e invalida a TLB **pela CPU, na hora**. Os dois drivers da AMD, numa placa com BAR pequena, fazem o contrário: escrevem as PTEs **com o SDMA**, numa fila própria, e invalidam **no mesmo fluxo**, depois das escritas. A documentação do WDDM permite escolher (`system-paging-process.md`: *"Updates of the page table entries for all other processes are done using the PageTableUpdateMode specified by the driver"*; `DXGK_PAGETABLEUPDATE_GPU_VIRTUAL` existe no `d3dkmddi.h`).

Isso **não prova** que a escrita pela CPU trave a placa (H). Mas somado ao retrato da 158, o nosso modelo de atualização é o que a AMD evita neste tipo de placa, e ele gera tráfego da CPU sobre a VRAM que cresce com a paginação:
- ~1 milhão de páginas lidas pela validação em 128 s;
- as escritas de PTE do `UpdatePageTable`.

## 4. Duas opções, em ordem de custo

| | DX.97 — caminhada de páginas só nas pontas | DX.98 — tabelas de página pela GPU (`GPU_VIRTUAL`) |
|---|---|---|
| O que muda | A validação do submit lê 2 páginas por faixa em vez de todas (−99% das leituras de tabela pela CPU) | As PTEs passam a ser escritas por pacotes SDMA no buffer de paginação, e a invalidação de TLB vai para o mesmo fluxo; a CPU para de escrever tabelas pela BAR |
| Tamanho | Pequeno (um laço) | Grande: montar os pacotes, ordenar a invalidação depois das escritas, manter o journal de GPUVA coerente, testes offline novos |
| Testa | "A CPU **lendo** as tabelas pela BAR ao mesmo tempo que o VidMm escreve é o gatilho" | "A CPU **escrevendo** as tabelas pela BAR é o gatilho", no modelo da AMD |
| Risco | Baixo: só deixa de validar o miolo das faixas (os faults já vão para a página dummy e são observáveis) | Médio: muda o caminho mais crítico da paginação |

**Recomendação:** DX.97 primeiro, porque é uma corrida barata e responde metade da pergunta. Se a trava continuar, a DX.98 é o passo seguinte, **alinhado aos dois drivers da AMD**. O desenho da DX.98 pode começar offline em paralelo, sem gastar corrida.
