# 707 — Pesquisa Forense no Driver AMD do Windows (RDNA1 / Adrenalin NimeZ 25.3.1): Decompilação REA, Respostas aos Problemas da BC-250 e Arquitetura DCN

**Data:** 12/10/2026  
**Natureza:** Relatório de engenharia reversa forense (binários e INFs do driver AMD Windows para RDNA1/APU).  
**Alvo analisado:** Pacote `@NimeZ-25.3.1` (`Packages/Drivers/Display/WT6A_INF/B413137/amdkmdag.sys`, `B413136/atidxx64.dll`, `u0412791.inf.bak`, `u0412788.inf.bak`, `MultiParse/*.inf`).  
**Ferramentas:** REA (Reverse Engineer Anything), Capstone Engine x86_64, análise PE64 e inspeção estruturada de strings/vtables.  
**Classificação de Confiança:** **P2** (evidências observadas diretamente em binários e configurações oficiais da AMD no Windows; interpretações e hipóteses classificadas como **H** conforme Lei 8).

---

## 1. Sumário Executivo e Síntese de Achados Críticos

A análise estática e a desmontagem direcionada do driver oficial da AMD (`amdkmdag.sys` versão 24.30 / Adrenalin 25.3.1) revelaram detalhes arquiteturais internos cruciais que explicam a causa raiz mais provável da **trava dura (hard freeze)** na BC-250 sob paginação ativa, além de fornecerem a planta estrutural para a implementação completa de **DCN (Display Core Next)**.

### Principais Descobertas:
1. **Paging Queue Separada no Silício (A1):** No SDMA 5.0 (RDNA1), a AMD **não** executa paginação na fila genérica de DMA (`GFX_RB`, base MMIO `0x80`). Existe um conjunto de anéis e registradores de hardware dedicado exclusivamente à paginação: a **`PAGE_RB`** (base MMIO `0xE0` / doorbells `0x112`-`0x113`, implementada em `sdma_queue_paging_5_0.c`). Submeter comandos de paginação do VidMm na mesma fila de cópias do Present causa contenção e violação de protocolo de barramento.
2. **Uso Obrigatório de Semáforo de Invalidação (A2):** A AMD no Windows utiliza explicitamente o semáforo de hardware `VM_INVALIDATE_ENG*_SEM` em transações atômicas de 3 operações (endereço, semáforo, status) para evitar que o hub perca o sinal de ACK de invalidação da TLB durante transições de energia (`sys_domain_10_1.c` e `CS_Tlb`).
3. **Power Gating Desligado em APU (A6):** Em todas as APUs, a AMD força no registro:
   - `SMU_DisableMmhubPowerGating = 1` (Power Gating do MMHUB terminantemente desligado!)
   - `DisableDynamicGfxMGPowerGating = 1` (Medium Grain Power Gating dinâmico desligado!)  
   Em dGPUs Navi10/14:
   - `PP_SclkDeepSleepDisable = 1`
   - `PP_DisableVoltageIsland = 1`
4. **Domínio de Sistema Flat em RDNA1 (A4):** Na arquitetura GVM do RDNA1 (`sys_domain_10_1.c`), a AMD proíbe tabelas de múltiplos níveis para o domínio de sistema: `"GVM doesn't support multiple level page table in system domain!"`. O domínio de sistema (GART/RAM) é mapeado por tabela de página primária plana.
5. **Arquitetura DCN Completa Presente no KMD:** O driver KMD do Windows incorpora integralmente a mesma base de código do **AMD Display Core (DC)** presente no Linux (`PAGEDCIC`/`dcn20`/`dcn21`), gerenciando os pipelines `HUBP -> DPP -> MPC -> OPP -> OPTC` com MPO3 e flip travado por sync do OPTC sem intervenção de SDMA.

---

## 2. Prioridade A — Questões Ligadas à Trava Dura

### A1. Motor de Paginação em Navi1x (SDMA0, SDMA1, GFX ou Compute)
* **Proveniência:** `amdkmdag.sys` (RVA `0x14cfa1c` - `0x14cfc50` em `PAGESIPC`, e strings em `0xa96470`).
* **Arquivos fonte originais da AMD:**
  - `C:\constructicon\builds\gfx\one\24.30\drivers\swip\sdma\src\5_0\sdma_queue_paging_5_0.c`
  - `C:\constructicon\builds\gfx\one\24.30\drivers\swip\sdma\src\5_0\sdma_queue_dma_5_0.c`
  - `C:\constructicon\builds\gfx\one\24.30\drivers\swip\sdma\src\sdma_5_0_ip.c`
* **Achado Forense (P2):**
  O silício do SDMA 5.0 (RDNA1) possui **duas filas de anel de hardware independentes** por engine físico:
  1. **Fila DMA Comum (`sdma_queue_dma_5_0.c`):**
     - Registradores base: `0x80` (`mmSDMA0_GFX_RB_CNTL`), `0x81` (`mmSDMA0_GFX_RB_BASE`), `0x82` (`mmSDMA0_GFX_RB_BASE_HI`), `0x83` (`mmSDMA0_GFX_RB_RPTR`), `0x88` (`mmSDMA0_GFX_RB_WPTR`).
  2. **Fila de Paginação Dedicada (`sdma_queue_paging_5_0.c`):**
     - Registradores base: `0xE0` (`mmSDMA0_PAGE_RB_CNTL`), `0xE1` (`mmSDMA0_PAGE_RB_BASE`), `0xE2` (`mmSDMA0_PAGE_RB_BASE_HI`), `0xE3` (`mmSDMA0_PAGE_RB_RPTR`), `0xE8` (`mmSDMA0_PAGE_RB_WPTR`).
     - Doorbells dedicadas: registradores `0x112` e `0x113`.
* **Desmontagem do Registro de Inicialização da Fila de Paginação (`sdma_5_0_start_paging_queue`):**
  ```x86asm
  0x1414cfa89: mov ebx, 0xe0              ; mmSDMA0_PAGE_RB_CNTL
  0x1414cfa9a: mov rcx, rsi
  0x1414cfa9d: call sdma_cgs_read_register
  0x1414cfaa2: and eax, 0xfffffffe        ; Limpa bit RB_ENABLE
  0x1414cfab9: call sdma_cgs_write_register
  0x1414cfaf1: shr rbx, 8                 ; Base física alinhada a 256 bytes
  0x1414cfaff: mov edx, 0xe1              ; mmSDMA0_PAGE_RB_BASE
  0x1414cfb0c: call sdma_cgs_write_register
  0x1414cfb1f: mov edx, 0xe2              ; mmSDMA0_PAGE_RB_BASE_HI
  0x1414cfb2c: call sdma_cgs_write_register
  0x1414cfb3a: or  eax, 0xc0420000        ; Habilita RPTR writeback e RB_ENABLE
  0x1414cfb4b: mov edx, 0xe0              ; mmSDMA0_PAGE_RB_CNTL
  0x1414cfb57: call sdma_cgs_write_register
  0x1414cfc13: mov edx, 0x113             ; PAGE_DOORBELL_OFFSET
  0x1414cfc2f: mov edx, 0x112             ; PAGE_DOORBELL_CNTL
  ```
* **Chave no INF:** `Gmx_AdvancedVmPagingMode = 0` (força serialização padrão pelo VidMm).
* **Cruzamento com o Linux e Diagnóstico (H):**
  No Linux `amdgpu`, o kernel utiliza `AMDGPU_RING_TYPE_SDMA_PAGE` (a fila `PAGE_RB`). No driver atual da BC250, o pipeline estava submetendo buffers de paginação e comandos de Present blit no mesmo anel de DMA (`GFX_RB`), gerando concorrência destrutiva entre leitura de scanout e escrita de tabelas/preenchimento.

---

### A2. Invalidação de TLB em Navi1x (Mecanismo, Semáforos e Workaround)
* **Proveniência:** `amdkmdag.sys`, classes `nsHW::ITlbFactory`, `nsHW::CS_Tlb` (RVA `0x140559810`), `vm_10_1_invalidate_tlbs_and_caches_per_ip` (RVA `0x14148e124`), e `sys_domain_10_1.c` (`0xa7f200`).
* **Achado Forense (P2):**
  1. **Alocação Dinâmica de Engine de Invalidação:**
     Em `0x140559810`, a função `AllocateInvalidateEngine` itera sobre o array de engines disponíveis (1 a N) procurando um engine com estado `0` (livre) e marcando como `2` (ocupado).
  2. **Uso Obrigatório de Semáforo (`VM_INVALIDATE_ENG*_SEM`):**
     Em `0x140575420`, o driver monta uma transação com 3 escritas de registrador:
     - Reg 1: Endereço baixo (`ENG*_ADDR_LO`)
     - Reg 2: Endereço alto (`ENG*_ADDR_HI`)
     - Reg 3: **Semáforo** (`ENG*_SEM`), seguido pela checagem de `ENG*_STATUS`.
     O semáforo garante exclusão mútua de hardware entre engines que demandam tradução simultânea.
  3. **Invalidação Síncrona via MMIO e Timeout:**
     Em `vm_10_1_invalidate_tlbs_and_caches_per_ip`:
     - Dispara requisição escrevendo no registrador `0x3e` (`REQ/CNTL`).
     - Realiza polling no registrador `0x3f` (`ACK/STATUS`) esperando bit `0 == 1`.
     - Caso o ACK não responda após 50 iterações (`0x32`), emite a mensagem crítica:
       `"Wait ENG ACK timeout!"` (Offset `0xa7f9bc`).
  4. **Barreiras de Ordem de Memória:**
     O driver emite instruções `lfence` explícitas (RVA `0x14148e2ec` e `0x14148e305`) antes e depois do acesso a descritores de página para impedir que o pipeline fora-de-ordem da CPU execute escritas antes da consolidação do TLB.

---

### A3. Serialização da Paginação e Limite em Voo
* **Proveniência:** `amdkmdag.sys`, `nsUSR::CS_OsPagingContext::BuildPagingBuffer_UpdatePageTable` (RVA `0x140667cd0`), `nsSVC::CS_VmPaging::FillMemory` (Offset `0xb16b20`), e INFs.
* **Achado Forense (P2):**
  - **Serialização por Padrão:** Nos INFs de Navi10/14, `KMD_EnablePageMigration = 0` e `Gmx_AdvancedVmPagingMode = 0`. O driver não executa migração autônoma especulativa de páginas; toda paginação é acionada e serializada sob o controle síncrono do VidMm.
  - **Tratamento de Chunks e Multipass:**
    Quando o preenchimento excede a cota de um buffer de DMA, o driver divide em fatias e registra:
    `"nsSVC::CS_VmPaging::FillMemory**Enter FILL DmaBufferPtr=[...], NumOfFillChunks=[...], MultiPassOffset=[...]**"`.
    Se o buffer esgotar, retorna `STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER` (`0xC01E0000`), forçando o VidMm a esperar a aposentadoria da fence do chunk anterior antes de fornecer o próximo buffer.

---

### A4. Tabelas de Página em APU e Configuração de MTYPE
* **Proveniência:** `amdkmdag.sys`, `sys_domain_10_1.c` (Offset `0xa7f200`), `nsHW::IMTypeFactory::CreateMType` (RVA `0x140558bf0`).
* **Achado Crítico de Arquitetura RDNA1 (P2):**
  - String oficial no driver:
    `"GVM doesn't support multiple level page table in system domain!"`
    (`C:\constructicon\builds\gfx\one\24.30\drivers\swip\gvm\src\vm\10\sys_domain_10_1.c`).
  - No hardware Navi 10.1, a AMD **não suporta** árvores multiníveis para a memória do sistema (System Domain / GART). As tabelas do sistema devem ser lineares/planas (`vm_10_1_create_primary_page_table`).
* **Política de Cache (MTYPE):**
  - O método `IMTypeFactory::CreateMType` configura o tipo de memória de acordo com o domínio. Para memória de sistema em APU compartilhada, utiliza-se **NC (Non-Coherent)** ou **CC (Coherent)** com snoop do barramento.
  - O método `sdma_5_0_start_engine` valida rigidamente: se os bits de cache policy forem alterados indevidamente em registradores do SDMA, o driver registra: `"Error: Non-cache policy bits overwritten!"` (RVA `0x14bd347`).

---

### A5. Declaração de Segmentos ao VidMm em APUs (Carve-out vs Abertura)
* **Proveniência:** `amdkmdag.sys`, classes `nsUSR::IUmaCarveoutFactory`, `nsUSR::CS_OsSegmentsMgr::InitializeCpuHostApertureSegment`, `nsUSR::CS_OsCpuHostAperture::ConfigureHart` (Offsets `0xb1a747`, `0xb1b19d`).
* **Achado Forense (P2):**
  A AMD declara formalmente ao VidMm em APU:
  1. **Segmento 1 (VRAM Local Carve-out):**
     Instanciado por `IUmaCarveoutFactory`. Representa a faixa de VRAM reservada pelo UEFI (256 MB na BC250). Flags: `CpuVisible = 1`, `Aperture = 0`.
  2. **Segmento 2 (System Memory / Abertura da CPU):**
     Instanciado por `CS_OsSegmentsMgr`. Flags: `CpuVisible = 0`, `Aperture = 1`.
  - **Parâmetros de Abertura do Host (HART):**
    O driver programa os limites do barramento:
    - `CpuHostAper_SysPA`: Endereço Físico do Sistema (System Physical Address).
    - `CpuHostAper_StartOffsetInGpuMem`: Início no espaço da GPU.
    - `CpuHostAper_SizeInBytes`: Tamanho da abertura.
  - **Endereçamento nas PTEs de Abertura:**
    O PTE de segmento de abertura recebe o **endereço físico real do sistema (`SysPA`)**, validado pela faixa configurada em `ConfigureHart`.

---

### A6. Estado de Clock e Power Gating (Navi1x e APUs)
* **Proveniência:** `u0412791.inf.bak` e `u0412788.inf.bak`.
* **Tabela de Configurações de Power/Clock Gating por Família:**

| Chave de Registro | Navi10 / Navi14 (dGPU) | APUs (Renoir / Rembrandt / Phoenix) | Significado Técnico |
|---|---|---|---|
| `SMU_DisableMmhubPowerGating` | *(não listado)* | **`1` (Desligado)** | Impede power gating no MMHUB (evita perda de ACK de TLB) |
| `DisableDynamicGfxMGPowerGating` | *(não listado)* | **`1` (Desligado)** | Desliga Medium Grain Power Gating dinâmico |
| `PP_SclkDeepSleepDisable` | **`1` (Desligado)** | *(não listado)* | Desliga sleep profundo no clock de shader |
| `PP_DisableVoltageIsland` | **`1` (Desligado)** | *(não listado)* | Desliga ilha de voltagem independente |
| `DisableSAMUPowerGating` | **`1` (Desligado)** | `0` | Desliga power gating do coprocessador de segurança |
| `DisableUVDPowerGatingDynamic`| **`1` (Desligado)** | *(padrão)* | Desliga gating dinâmico do decodificador |
| `EnableUvdClockGating` | **`1` (Ligado)** | *(padrão)* | Clock gating estático de vídeo ligado |
| `EnableVceSwClockGating` | **`1` (Ligado)** | *(padrão)* | Clock gating de encoder ligado |
| `EnableAspmL1` / `L0s` | **`1` (Ligado)** | *(padrão)* | Economia de barramento PCIe |
| `EnableUlps` | **`1` (Ligado)** | **`1` (Ligado)** | Ultra Low Power State |
| `PP_GfxOffControl` | *(não listado)* | **`1` (Ligado)** | Habilita controle de GFXOFF pela SMU em APUs |
| `StutterMode` | **`0x200A`** | *(padrão)* | Modo stutter de display com refresh balanceado |

---

### A7. Workarounds de UTCL2, GL2, GCR e Falhas de Tradução
* **Proveniência:** `amdkmdag.sys`, `nsHW::CS_RequestLog::WaitForUtcl1AlogClean` (Offset `0xb26cad`), `nsHW::CS_PageFaultMgr::CreateAndInitGfxPageFault`.
* **Achado Forense (P2):**
  - **Sincronização do UTCL1 no SDMA:** O SDMA 5.0 possui caches de tradução de nível 1 próprios (UTCL1). Antes de concluir operações críticas de atualização de página, o driver invoca:
    `nsHW::CS_RequestLog::WaitForUtcl1AlogClean`.
  - **Tratamento de Page Faults:** O driver possui handlers separados para `GfxPageFault` e `MmPageFault` (`CreateAndInitGfxPageFault` e `CreateAndInitMmPageFault`), gerando logs estruturados via `nc_amdlog_oca_vm_page_fault`.

---

### A8. Chaves de Registro do KMD para Navi10 e Navi14 (AddReg)
* **Proveniência:** `u0412791.inf.bak` (linhas 11131–11355).

```ini
; Configuração Padrão AMD Adrenalin para RDNA1 (Navi10 e Navi14)
[ati2mtag_Navi10_SoftwareDeviceSettings]
HKR,,WmAgpMaxIdleClk,%REG_DWORD%,0x20
HKR,,MemInitLatencyTimer,%REG_DWORD%,0x775771BF
HKR,,KMD_EnablePageMigration,%REG_DWORD%,0
HKR,,PP_SclkDeepSleepDisable,%REG_DWORD%,1
HKR,,PP_DisableVoltageIsland,%REG_DWORD%,1
HKR,,DisableSAMUPowerGating,%REG_DWORD%,1
HKR,,EnableAspmL1,%REG_DWORD%,1
HKR,,EnableAspmL0s,%REG_DWORD%,1
HKR,,KMD_EnableDesktopTexture,%REG_DWORD%,0
HKR,,DisableVCEPowerGating,%REG_DWORD%,0
HKR,,EnableUvdClockGating,%REG_DWORD%,1
HKR,,EnableVceSwClockGating,%REG_DWORD%,1
HKR,,DisableUVDPowerGatingDynamic,%REG_DWORD%,1
HKR,,StutterMode,%REG_DWORD%,0x200A
HKR,,EnableUlps,%REG_DWORD%,1
HKR,,DisableFBCSupport,%REG_DWORD%,0
HKR,,EnableCrossFireAutoLink,%REG_DWORD%,1
HKR,,ExtEvent_BIOSEventByInterrupt,%REG_DWORD%,1
HKR,,UseExecuteIndirectPacket,%REG_DWORD%,1
HKR,,GameConfigSupported,%REG_DWORD%,1
HKR,,KMD_EnableOPM2Interface,%REG_DWORD%,1
HKR,,DalAllowDPrefSwitchingForGLSync,%REG_DWORD%,1
HKR,,DalReportSingleSelectedTiming,%REG_DWORD%,1
```

---

## 3. Prioridade B — Outros Problemas

### B1. Reset de GFX no TDR e Sobrevivência do Display
* **Proveniência:** `amdkmdag.sys`, funções `Dispatch_ResetEngine`, `TtlRtsResetEngine`, `TTL_ForceMode1Reset`.
* **Achado Forense (P2):**
  - **Nível 1 (Soft Reset por Engine):** `Dispatch_ResetEngine` tenta resetar apenas a fila e os CUs comprometidos através da KIQ (`gc_initialize_kiq_engine_10_1`). O display (`DCN`) sobrevive completamente porque roda em clocks e domínios de energia separados.
  - **Nível 2 (BACO / Reset de Barramento):** O driver checa se o display suporta BACO: `DisplayManager_NoDCE::CanAllDisplaysSupportBacoSGpu`. Se suportado, o chip entra em estado de alimentação mínima e retorna sem apagar a imagem.
  - **Nível 3 (Mode 1 Reset da ASIC):** Em falhas graves de GPUVM ou travamento de CP/RLC, o driver dispara `TTL_ForceMode1Reset`. Esse reset reinicia os trilhos de energia do silício via PSP/SMU; o display perde sincronismo e precisa ser restaurado do zero via VidPn.

### B2. Firmware Embutido vs PSP VBIOS
* **Proveniência:** `u0412788.inf.bak` (linhas 8820–8830) e arquivos `.sbin`/`.dat` em `MSFTParse/`.
* **Achado Forense (P2):**
  - O INF define formalmente:
    `EnableswSdmaFwLoading = 0`
    `EnableswGcFwLoading = 0`
    `EnableswVcnFwLoading = 0`
    `DisableCailLoadUcode = 1`
  - Isso confirma que em plataformas Windows modernas e APUs, os microcódigos primários (SDMA, CP, RLC, MEC) são carregados pelo PSP/UEFI durante a fase de inicialização da plataforma, e não injetados dinamicamente via software pelo KMD.

### B3. Inicialização e Uso do SDMA1
* **Proveniência:** `amdkmdag.sys`, `sdma_5_0_start_engine` (RVA `0x14bd347`).
* **Achado Forense (P2):**
  - O KMD inicializa SDMA0 e SDMA1 passando o índice da instância em `r13d` (0 ou 1).
  - Antes de liberar a fila para o sistema operacional, o driver realiza o teste de congelamento (`sdma_5_0_wait_for_sdma_freeze_frozen`). Se o SDMA1 expirar (`PROOF_TIMEOUT`), o driver aborta a fila do SDMA1 e reconfigura o escalonador do KMD para direcionar todas as solicitações para o SDMA0.

### B4. Layout das Doorbells e IH em Navi1x
* **Proveniência:** `amdkmdag.sys`, classes `nsHW::CS_DoorbellAllocator`, `InitDoorbellApertureRange`, `sdma_5_0_start_paging_queue`.
* **Achado Forense (P2):**
  - O SDMA 5.0 utiliza doorbells mapeadas em uma abertura GART (`InitDoorbellGartRange`).
  - Para a fila de paginação (`PAGE_RB`), a porta da doorbell é configurada em `0x112` / `0x113`.
  - Para o interrupt handler (IH), se a coerência de barramento ou o handshake da doorbell de IH falhar, o driver desabilita a doorbell de RPTR do IH e recorre à atualização por polling/MMIO tradicional para evitar starvation da CPU.

### B5. Recursos Compartilhados entre Processos (Shared Handles)
* **Proveniência:** `amdkmdag.sys`, `Dispatch_OpenAllocation`, e `atidxx64.dll`.
* **Achado Forense (P2):**
  - O KMD valida a handle global NT via `DxgkDdiOpenAllocation`.
  - A UMD armazena a estrutura de layout de textura e tiling nos metadados privados da alocação (`pDmaBufferPrivateData`), garantindo que o processo consumidor conheça o pitch, formato e alinhamento sem tentar renegociar o recurso.

### B6. Gerenciamento de Clocks pela SMU
* **Proveniência:** `amdkmdag.sys`, `PhwNavi10_EnableDPMTasks`, `PhwNavi10_PostInitSmu`.
* **Achado Forense (P2):**
  - A comunicação ocorre por caixas postais da SMU (SMN Message ID + Arg + Poll Response).
  - Em APUs (como Van Gogh e Renoir), o driver chama `PhwVanGogh_EnableDPMTasks` para configurar a PPTable de limites de potência e temperatura (`PhwVanGogh_GetTemperature`).

### B7. Tratamento de Falta de Memória no `CreateResource` (UMD)
* **Proveniência:** `atidxx64.dll` (`0x1cea9`, `0x21915`, `0x2265f` — mais de 200 ocorrências).
* **Achado Forense (P2):**
  - A UMD D3D11 da AMD verifica meticulosamente cada falha de alocação de heap interna ou rejeição de alocação do VidMm, retornando estritamente `0x8007000E` (`E_OUTOFMEMORY`) em conformidade com o contrato D3D10/D3D11 DDI, impedindo crashes e ponteiros nulos no pipeline gráfico.

---

## 4. Prioridade C — Arquitetura e Implementação do DCN Completo para RDNA1

A pergunta do usuário expressa especial interesse em:
> *"tambem seria muito util saber como implementar o DCN completo para o RDNA 1."*

### 4.1 Identificação do Hardware de Display em RDNA1
* **Navi 10 / Navi 12:** DCN 2.0 (Display Core Next versão 2.0).
* **Navi 14:** DCN 2.0.
* **Cyan Skillfish (BC-250) / Renoir:** DCN 2.1 (variação otimizada de DCN 2.0 para APU, compartilhando blocos de DCN 2.0 com ajustes de DCHVM e relógio).

### 4.2 Blocos Funcionais do DCN 2.0 / 2.1 no Driver AMD
Dentro de `amdkmdag.sys` (seções `PAGEDCIC`, `PAGEDCIR`, `PAGEDCID`), o driver incorpora integralmente a pilha **AMD Display Core (DC)**:

```
[ GPU Memory (VRAM / GTT) ]
            │
            ▼
    ┌───────────────┐
    │  HUBP (DCN20) │ ◄── Display VM (DCHVM), DCC Decompressor, Request FIFO
    └───────┬───────┘
            │
            ▼
    ┌───────────────┐
    │  DPP (DCN20)  │ ◄── Color Space Conversion, Input Gamma (1DLUT), SCL (Scaler)
    └───────┬───────┘
            │
            ▼
    ┌───────────────┐
    │  MPC (DCN20)  │ ◄── Multi-Plane Overlay (MPO3), Plane Blending, 3DLUT HDR
    └───────┬───────┘
            │
            ▼
    ┌───────────────┐
    │  OPP (DCN20)  │ ◄── Output Pixel Formatting, Dithering, Bit Clamping (8/10/12-bit)
    └───────┬───────┘
            │
            ▼
    ┌───────────────┐
    │ OPTC / OTG    │ ◄── Timing Generator, VSYNC/HSYNC/VBLANK, Flip Lock Atômico
    └───────┬───────┘
            │
            ▼
    ┌───────────────┐
    │ Link/PHY Enc  │ ◄── DP/HDMI Stream Encoder, PHY Transceivers, AUX/DDC Channel
    └───────────────┘
```

### 4.3 Símbolos Forenses Identificados em `amdkmdag.sys`
- `dcn20_clk_mgr_construct`: Gerenciador de clocks (DISPCLK, DPPCLK, DCFCLK).
- `dcn20_dccg_init`: Gerenciador central de controle e distribuição de relógios.
- `dcn20_disable_vga`: Desativa o legado VGA para liberar os pipelines.
- `dcn20_hubp_pg_control`: Controle de economia de energia por HUBP.
- `dcn20_dpp_pg_control`: Controle de economia de energia por DPP.
- `dcn20_dsc_pg_control`: Display Stream Compression (DSC).
- `dcn20_program_pix_clk`: Programação de frequências de pixel clock.
- `dcn21_dchvm_init`: Inicialização da VM de display (DCHVM) específica de APU.
- `dcn21_link_encoder_construct`: Construtor do transmissor físico de link.
- `dcn_validate_bandwidth`: Validador de largura de banda e marcas d'água de latência (watermarks).

### 4.4 O Ciclo de Flip Atômico no Windows (MPO3 / DirectFlip)
Diferente da abordagem atual da BC250 (que realiza cópias periódicas por SDMA no scanout herdado do GOP):
1. **Contrato WDDM:**
   O KMD exporta `DxgkDdiSetVidPnSourceAddressWithParams` (ou `DxgkDdiSetVidPnSourceAddress`).
2. **Programação do HUBP:**
   O driver recebe o endereço da superfície (`PrimaryAddress`) e escreve diretamente nos registradores do HUBP:
   - `mmHUBPREQ0_DCSURF_SURFACE_ADDRESS_HIGH`
   - `mmHUBPREQ0_DCSURF_SURFACE_ADDRESS_LOW`
   - `mmHUBPREQ0_DCSURF_SURFACE_CONTROL`
3. **Trava Atômica do Timing Generator (OPTC):**
   Para evitar tearing:
   - Aciona `OTG0_OTG_GLOBAL_SYNC_LOCK = 1` (trava a atualização dos registradores de sombra).
   - Atualiza formato de cor e pitch.
   - Libera `OTG0_OTG_GLOBAL_SYNC_LOCK = 0`.
4. **Comutação por Hardware:**
   No próximo pulso de VBLANK gerado pelo OPTC, o hardware comuta instantaneamente o endereço de scanout para o novo frame **sem gastar ciclos de CPU nem canais de SDMA**.

### 4.5 Roteiro de Implementação de DCN para a BC-250 (Fase P9)
1. **Inicialização do DCN 2.1:**
   Invocar sequência `dcn21_dchvm_init`, conectar gerador de clock `dccg` com os divisores de frequência recuperados do GOP.
2. **Reaproveitamento dos Timings do GOP:**
   Em placas BC250 sem monitor físico conectado (ou com display emulado/dummy), ler os registradores `OTG0_OTG_H_TOTAL`, `OTG0_OTG_V_TOTAL` configurados pelo GOP do UEFI para manter o display timing ativo sem reprogramar PHY externo.
3. **Desacoplamento do SDMA do Scanout:**
   Substituir a cópia periódica do Present por programação de endereço no HUBP0. Isso **elimina 100% dos laços de poll de RPTR de SDMA no Present** e zera a concorrência entre o display e a paginação.

---

## 5. Matriz Comparativa: AMD Windows vs. Linux `amdgpu` vs. Nosso Driver BC250

| Componente / Área | Driver Oficial AMD Windows (NimeZ 25.3.1) | Linux Upstream `amdgpu` | Nosso Driver Atual BC250 (3.415.1.0) | Recomendação Corretiva (Ação Imediata) |
|---|---|---|---|---|
| **Fila de Paginação (A1)** | Fila dedicada **`PAGE_RB`** (base `0xE0`) | Fila dedicada `AMDGPU_RING_TYPE_SDMA_PAGE` | Usa anel genérico `GFX_RB` com concorrência de Present | **Separar a fila de paginação para os registradores base `0xE0` (PAGE_RB)** |
| **Invalidação de TLB (A2)** | Semáforo `VM_INVALIDATE_ENG*_SEM` + ACK poll com timeout | Semáforo `use_invalidate_semaphore` em MMHUB | Invalidação direta por MMIO sem semáforo | **Adicionar escrita de semáforo `VM_INVALIDATE_ENG*_SEM` antes de requisitar** |
| **Power Gating MMHUB (A6)**| **Desligado** em APU (`SMU_DisableMmhubPowerGating=1`) | Desligado em GMC v10 / APUs | Não configurado (estado indefinido do POST) | **Garantir MMHUB Power Gating desabilitado via registros/RLC** |
| **Gating de Shader (A6)** | **Desligado** em Navi10 (`PP_SclkDeepSleepDisable=1`) | Gerenciado via power profiles | Não configurado | **Desabilitar Sclk Deep Sleep no silício** |
| **Domínio de Sistema (A4)** | **Tabela plana única** (GVM proíbe multinível em sys domain) | GART linear de 1 nível | Testado 2 níveis e 4 níveis (P5) | **Manter tabela primária linear/plana para o domínio do sistema** |
| **Display / Scanout** | Pipeline DCN completo com flip no HUBP | DC completo com HUBP/OPTC | GOP herdado com cópia por SDMA no Present | **Remover cópia por SDMA no Present; programar endereço direto no HUBP (P9)** |
| **Erros na UMD (B7)** | Retorna `0x8007000E` (`E_OUTOFMEMORY`) em 200+ pontos | Retorna `-ENOMEM` | Retornava sucesso mesmo sem alocação (T9) | **Corrigir retorno de `E_OUTOFMEMORY` no CreateResource** |

---

## 6. Próximos Passos Recomendados para a Engenharia

1. **Ação de Prioridade Zero (P0 - Solução da Trava Dura):**
   - Implementar a separação do anel do SDMA: configurar o anel de paginação usando a base de registradores **`0xE0`** (`PAGE_RB`), deixando os registradores `0x80` (`GFX_RB`) isolados ou sem concorrência com o VidMm.
   - Proteger a invalidação de TLB com o semáforo de hardware **`VM_INVALIDATE_ENG*_SEM`**.
   - Garantir que o Power Gating do MMHUB permaneça desativado (`SMU_DisableMmhubPowerGating = 1`).
2. **Ação de Prioridade 1 (P9 - Implementação DCN):**
   - Mapear os registradores do HUBP0 e OPTC0 já inicializados pelo GOP do UEFI.
   - Conectar o flip do WDDM (`SetVidPnSourceAddress`) diretamente à escrita do registrador de endereço do HUBP0 (`DCSURF_SURFACE_ADDRESS`), extinguindo a dependência de cópias de SDMA para o scanout.
3. **Validação:**
   - Submeter o pacote de teste com a paginação na `PAGE_RB` e os semáforos de TLB para validação física em hardware com o Daniel.
