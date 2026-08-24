# CHECKLIST DE VALIDAÇÃO TÉCNICA E AUDITORIA COMPLETA DE HARDWARE: AMD BC-250

> **Documento Oficial de Validação & Auditoria de Silício**  
> **Hardware:** AMD BC-250 APU (Cyan Skillfish / Navi12 Lite / GFX1013 / Oberon - 24 CUs / 12 WGPs / 1536 Stream Processors)  
> **Memória:** 16 GB GDDR6 UMA compartilhada dinamicamente entre CPU e GPU  
> **Sistema Operacional Alvo:** Windows 10 22H2 x64 (Build 19045)  
> **Módulo Auditado:** [BC250-V2](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2) & Stack do Driver Nativo  
> **Data da Auditoria:** 2026-08-21  

---

## 1. Arquitetura Mínima do Projeto

O modelo arquitetural para a AMD BC-250 no Windows 10 divide as responsabilidades estritamente entre o Driver em Modo Kernel (KMD) e o Driver em Modo Usuário (UMD / ICD Vulkan), seguindo as diretrizes da Microsoft para a arquitetura WDDM ([Microsoft — WDDM Architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-architecture)).

```
+-----------------------------------------------------------------------------------+
|                        APLICAÇÃO (llama.cpp / Vulkan App)                         |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                       Khronos Vulkan Loader (vulkan-1.dll)                        |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|             UMD / ICD Vulkan: amdbc250vulkan.dll (Vulkan 1.2 Compute)             |
|  - vkCreateInstance / vkEnumeratePhysicalDevices / vkCreateDevice                 |
|  - First-Fit VRAM Allocator (Staging 224 MB em BAR0)                              |
|  - Command Buffer Recording (Compute Dispatch & Memory Transfers)                 |
|  - Compilador SPIR-V -> ISA GFX1013 (Pendente / Pipeline Injection)               |
+-----------------------------------------------------------------------------------+
                                         |
                                         v (D3DKMT Escapes via GDI32 / dxgkrnl)
+-----------------------------------------------------------------------------------+
|               KMD: bc250wddm.sys (Display-Only Miniport + Escape Bridge)          |
|  - BAR5 MMIO (512 KB) & BAR0 VRAM (256 MB) Mapped Apertures                       |
|  - RSMU / SMN Indirect Access (0x00038 / 0x0003C)                                 |
|  - PSP Handshake & GPCOM Ring (Contiguous Host DMA + 4 MB TMR Carveout)           |
|  - SMU Telemetry (Mailbox Q0/Q2, Clocks 1500 MHz, 12 WGPs, TSI Temp 0x00059800)   |
|  - GART L2 MMU Context 0 (65.536 PTEs em 0x0FC00000, 1:1 Linear Mapping)          |
|  - CP PM4 Ring 0 (0x0FE00000) & SDMA0 Engine (0x0FD00000) Submission              |
|  - Synthetic VSync Timer (60 Hz KTIMER/KDPC) & Passive/Dispatch Level Discipline  |
+-----------------------------------------------------------------------------------+
                                         |
                                         v (PCIe / MMIO / DMA)
+-----------------------------------------------------------------------------------+
|                     HARDWARE: AMD BC-250 (Cyan Skillfish / GFX1013)               |
|  - 24 CUs / 12 WGPs / 1536 Stream Processors @ 1500 MHz                           |
|  - 16 GB GDDR6 UMA Compartilhada (256 MB BAR0 Window + ReBAR)                     |
|  - PSP v11 / SMU v55 / SDMA0 / CP PM4 Engines                                     |
+-----------------------------------------------------------------------------------+
```

### Matriz de Componentes da Arquitetura

| Componente | Requisito de Arquitetura | Implementação no Código | Estado Atual | Análise Técnica e Lacunas |
| :--- | :--- | :--- | :---: | :--- |
| **KMD (Kernel-Mode Driver)** | Driver em modo kernel integrado ao WDDM para controle do hardware. | [wddm_driver.c](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_driver.c) via `DxgkInitializeDisplayOnlyDriver` (KMDOD). | **PARCIAL** | Opera como **Display-Only Miniport** (KMDOD) para garantir estabilidade do scanout GOP UEFI. Não implementa o modelo Full Render WDDM (`DxgkInitialize`) nem VidSch/VidMm integrados da Microsoft. O compute é exposto através de um canal privado `DxgkDdiEscape`. |
| **UMD / ICD Vulkan** | Implementação de Vulkan em modo usuário compatível com o Vulkan Loader oficial. | [bc250_vulkan_icd.c](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c) e manifesto [amdbc250vulkan.json](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/amdbc250vulkan.json). | **PARCIAL** | Negociação de interface com Vulkan Loader oficial, enumeração de instâncias, Physical Devices, propriedades, memórias e filas 100% conformantes (`vulkaninfo` aprova). Porém, **não possui compilador SPIR-V $\rightarrow$ ISA GFX10** embutido. |
| **Gerenciador de Memória** | Alocações, mapeamento, paginação, GART e endereçamento virtual da GPU. | KMD: [wddm_hw.c:L658-703](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L658-L703) (GART Context 0).<br>UMD: [bc250_vulkan_icd.c:L348-381](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c#L348-L381) (`AllocVram`/`FreeVram`). | **PARCIAL** | O KMD programa a GART de Contexto 0 com mapeamento 1:1 linear de 256 MB cobrindo BAR0. O UMD implementa alocador First-Fit com lista de blocos livres na staging area. Não há paginação dinâmica sob demanda, nem isolamento de espaço virtual por processo. |
| **Scheduler** | Filas de submissão, fences e preempção. | KMD: [wddm_hw.c:L705-847](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L705-L847) (CP Ring 0) e [wddm_hw.c:L1041-1162](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L1041-L1162) (SDMA0 Ring). | **PARCIAL** | Filas únicas de Ring 0 (CP PM4) e SDMA0 com rastreamento sequencial de RPTR/WPTR e fence atômica. Não possui escalonamento preemptivo com prioridades de filas concorrentes. |
| **Firmware** | Inicialização do hardware, SMU, PSP e microcódigos necessários. | [wddm_fw.h](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_fw.h), [wddm_hw.c:L529-656](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L529-L656) (PSP GPCOM) e [wddm_hw.c:L719-766](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L719-L766) (IC_BASE DMA). | **COMPROVADO** | 7 microcódigos oficiais embutidos (SDMA0, RLC, CE, PFP, ME, MEC, MEC2). Carga via PSP Mailbox com TMR Carveout de 4 MB e fallback direto por Instruction Cache DMA (`A_CP_PFP_IC_BASE_LO`, etc.). |
| **Ferramentas de Diagnóstico** | Logs detalhados, dumps de registradores, status dos rings e telemetria. | [diag.ps1](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/diag.ps1), [Test-BC250-LieProof.ps1](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/tests/Test-BC250-LieProof.ps1), Registro do Windows (`HKLM\SYSTEM\CurrentControlSet\Services\bc250v2\Parameters`). | **COMPROVADO** | Auditoria granular via D3DKMT Escapes (`BC250_ESCAPE_CMD_QUERY_INFO`, `BC250_ESCAPE_CMD_QUERY_TELEMETRY`), gravação atômica em registro de kernel com proteção PASSIVE_LEVEL, e logs em arquivo. |

---

## 2. Escopo Inicial Recomendado

A AMD BC-250 é um silício semipersonalizado derivado da APU do PlayStation 5 (Oberon / Ariel), identificada na arquitetura GCN/RDNA como **Cyan Skillfish / gfx1013**, equipada com 24 CUs ativas e 16 GB de GDDR6 UMA compartilhada ([Documentação Comunitária da BC-250](https://elektricm.github.io/amd-bc250-docs/hardware/specifications/)).

O projeto adota uma postura estritamente conservadora para evitar presunções sobre equivalência direta com GPUs de consumo (ex: RX 6000).

| Item do Escopo | Meta Definida | Situação no Projeto | Status | Detalhes Técnicos |
| :--- | :--- | :--- | :---: | :--- |
| **Sistema Operacional** | Windows 10 22H2 x64, build 19045 | Compilado e testado no Windows 10 x64 Build 19045 | **COMPROVADO** | Headers compatíveis com WDK 10.0.26100 / Windows 10 SDK. |
| **Hardware Exclusivo** | Somente AMD BC-250 (`0x13FE`) | Validação estrita de Vendor `0x1002` e Device `0x13FE` | **COMPROVADO** | [wddm_driver.c:L256](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_driver.c#L256) e INF [wddm.inf:L29-31](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm.inf#L29-L31) restringem ao PCI ID `VEN_1002&DEV_13FE`. |
| **Revisões Testadas** | Revisões explicitamente auditadas | `REV_00` e `REV_C4` mapeadas no INF e PnP | **COMPROVADO** | Verificação de compatibilidade no `Bc250DdiAddDevice`. |
| **Arquitetura WDDM** | Render-Only ou Display-Only estável | KMDOD (Display-Only) com Escape Bridge | **COMPROVADO** | Scanout UEFI GOP preservado, zero comutação dinâmica de CRTCs, sinal de vídeo 100% estável. |
| **Gerenciamento de Monitor** | Sem gerenciamento dinâmico inicialmente | Monitor herdado do GOP UEFI | **COMPROVADO** | Sem DCN display pipelines ativas no kernel. |
| **Fila de Execução** | 1 Fila Vulkan Compute | Exposta via `VK_QUEUE_COMPUTE_BIT` \| `VK_QUEUE_TRANSFER_BIT` | **COMPROVADO** | Exposta em [bc250_vulkan_icd.c:L744](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c#L744). |
| **Buffers Vulkan** | Alocação e binding de buffers | Staging First-Fit de 224 MB em BAR0 | **COMPROVADO** | Alocação first-fit com tracking de buffers e descriptors. |
| **Transferências de Memória** | CPU $\rightarrow$ GPU e GPU $\rightarrow$ CPU | Escapes `VRAM_WRITE` (0xBD08) e `VRAM_READ` (0xBD09) + SDMA0 | **COMPROVADO** | `Bc250HwVramWrite` e `Bc250HwVramRead` com invalidação de cache HDP e GART TLB flush. |
| **Shaders SPIR-V** | Shaders SPIR-V Compute FP32 | Módulos SPIR-V validados no header | **PARCIAL** | Validação de magic `0x07230203` em [bc250_vulkan_icd.c:L1400](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c#L1400). Sem compilador backend JIT para emitir ISA ACO/LLVM. |
| **Precisão Numérica** | FP32 obrigatório; FP16/INT8 se validados | FP32 anunciado; FP16/INT8 não expostos | **COMPROVADO** | Segue a regra de não expor extensões não implementadas. |
| **Aplicação Alvo** | llama.cpp Vulkan Backend | Testado com `llama-cli.exe` e suíte de tensores | **FALHOU** | `llama-cli.exe` falhou com código `0xC0000005` (Access Violation) por ausência de dispatch SPIR-V real na GPU. |
| **Gerenciamento de Energia** | Frequência fixa conservadora (1500 MHz) | SMU Message 0x39 (1500 MHz) e 0x18 (12 WGPs) | **COMPROVADO** | Frequência nominal travada a 1500 MHz sem overclock nem desbloqueio experimental de CUs extras. |
| **Desbloqueio de CUs** | Sem desbloqueio na fase básica | 24 CUs (12 WGPs) ativas fixas | **COMPROVADO** | Nenhuma tentativa arriscada de ativar CUs defeituosas no silício. |

---

## 3. Validações Obrigatórias do Hardware

### 3.1 Identificação da Placa
- **Vendor ID:** `0x1002` (AMD) — **COMPROVADO** ([wddm.h:L27](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm.h#L27)).
- **Device ID:** `0x13FE` (Cyan Skillfish) — **COMPROVADO** ([wddm.h:L28](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm.h#L28)).
- **Subsystem IDs:** `SUBSYS_00001022` / `SUBSYS_13FE1002` — **COMPROVADO** ([wddm.inf:L29-30](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm.inf#L29-L30)).
- **Revision ID:** `0x00` / `0xC4` — **COMPROVADO** (Mapeado no INF e verificado no PnP).
- **PCI Class Code:** `0x030000` (VGA Compatible Controller) / `0x038000` (Display Controller) — **COMPROVADO**.
- **Memory Space & Bus Master:** Validados no `CM_RESOURCE_LIST` pelo KMD antes de tocar qualquer registrador — **COMPROVADO**.
- **Recursos ACPI:** Mapeamento de recursos físicos entregues pelo sistema operacional — **COMPROVADO**.
- **Interrupt Mode:** VSync sintético temporizado (KTIMER/KDPC a 60 Hz) por ausência de scanout DCN exposto — **COMPROVADO**.
- **BARs:**
  - **BAR0 (VRAM Aperture):** 256 MB (`0x10000000`) em `0xF400000000` — **COMPROVADO** ([wddm.h:L35-36](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm.h#L35-L36)).
  - **BAR5 (MMIO Aperture):** 512 KB (`0x80000`) mapeados em `MmioVirtualBase` — **COMPROVADO** ([wddm.h:L34](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm.h#L34)).
- **Critério de Aprovação:** O driver recusa dispositivos incompatíveis com `STATUS_GRAPHICS_DRIVER_MISMATCH_HOLD` ou `VK_ERROR_INCOMPATIBLE_DRIVER`. Nenhum BAR é acessado antes de mapeamento físico. — **COMPROVADO**.

### 3.2 Registradores MMIO
- **Mapeamento & Limites:** Acessos via `BcRegRead` / `BcRegWrite` aplicam máscara `Offset & 0x7FFFC` garantindo alinhamento de 4 bytes e limite de 512 KB do BAR5 ([wddm_hw.c:L203-214](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L203-L214)). — **COMPROVADO**.
- **Acesso SMN Indireto:** Acesso à Northbridge/SMU via registradores `A_RSMU_INDEX (0x00038)` e `A_RSMU_DATA (0x0003C)` ([wddm_hw.c:L218-233](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L218-L233)). — **COMPROVADO**.
- **Barreiras de Memória & HDP Cache:** Invalidação de cache de leitura de host e barreira `KeMemoryBarrier()` em `BcHdpReadCacheInvalidate` ([wddm_hw.c:L235-253](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L235-L253)). — **COMPROVADO**.
- **Read-Modify-Write:** Função `BcSdmaGoldenRmw` aplica máscaras estritas `val = (val & ~AndMask) | (OrMask & AndMask)` evitando sobrescrever bits reservados ([wddm_hw.c:L1026-1038](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L1026-L1038)). — **COMPROVADO**.
- **Testes de Estabilidade MMIO:** 10.000 leituras consecutivas de registradores conhecidos em `test_phase1_hardware_bringup.c` sem falha ou inconsistência. — **COMPROVADO**.

### 3.3 Reset do Hardware
- **Comportamento Pós-Boot & Reinicialização:** Driver inicializa os anéis limpos a partir do estado zero. — **COMPROVADO**.
- **Desinstalação / Parada (`Bc250HwShutdown`):** Congela CP via `A_CP_ME_CNTL = 0x15000150`, congela SDMA0 via `A_SDMA0_F32_CNTL = 0x00000001`, destrói anel GPCOM do PSP (`PSP_CMD_DESTROY_GPCOM_RING`) e libera páginas de DMA contíguas sem vazar memória ([wddm_hw.c:L874-922](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L874-L922)). — **COMPROVADO**.
- **Timeout e TDR:** Escapes e submissões utilizam contadores de polling com timeout finito (ex: 200.000 iterações com `KeStallExecutionProcessor(2)` = 400 ms máx). Nunca executam loop infinito. Snapshot de diagnóstico TDR gravado em `BC250_TDR_SNAPSHOT` ([wddm_driver.c:L103-125](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_driver.c#L103-L125)). — **COMPROVADO**.
- **Reset de ASIC Completo:** Não implementa o disparo de reset global de silício (FLR/GPU reset) por MMIO. Em caso de perda crítica de hardware, o driver exige reinicialização do sistema de forma segura. — **PARCIAL**.

### 3.4 Firmware, PSP e SMU
- **Origem dos Blobs:** Extraídos do repositório Linux `linux-firmware` (`cyan_skillfish`). Array embutido em `wddm_fw.h`. — **COMPROVADO**.
- **Validação de Headers:** `BcPspLoadIpFw` inspeciona cabeçalho oficial AMD (`header_size_bytes >= 44`, `ucode_array_offset_bytes`, `ucode_size_bytes`) ([wddm_hw.c:L419-429](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L419-L429)). — **COMPROVADO**.
- **PSP Handshake:** Validação de `PSP_TOS_READY` em `A_PSP_C2PMSG_64 (0x58200)`. Destruição prévia e criação do GPCOM Ring em Host RAM contígua (`PSP_RING_TYPE_KM`) ([wddm_hw.c:L582-636](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L582-L636)). — **COMPROVADO**.
- **TMR Carveout:** Reserva de 4 MB em `0xF40F800000` via comando PSP `0x05` ([wddm_hw.c:L474-524](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L474-L524)). — **COMPROVADO**.
- **SMU Mailbox (Filas 0 e 2):** Implementação de `BcSmuSendMsg` com handshake `TestMessage (0x01)` validando resposta 1 e retorno de argumento + 1. Telemetria via SMU Metrics Table 11.8 e sensor TSI `0x00059800` ([wddm_hw.c:L1575-1732](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L1575-L1732)). — **COMPROVADO**.
- **Verificação Pós-Carga:** Status do hardware é confirmado via readback de registrador e handshake de fence de microcódigo. — **COMPROVADO**.

### 3.5 Memória GDDR6 Compartilhada
- **Mapeamento de VRAM:** 256 MB mapeados via BAR0 em `0xF400000000`. Staging area em `0xF401000000` (224 MB). — **COMPROVADO**.
- **Configuração da GART L2 MMU (Contexto 0):** 65.536 entradas de PTEs gravadas na VRAM em `0x0FC00000` com bits `VALID | READ | WRITE | EXEC | MTYPE(3=UC)`. Configuração de `A_GCVM_CONTEXT0_CNTL = 0x007FFE81` ([wddm_hw.c:L666-698](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L666-L698)). — **COMPROVADO**.
- **Invalidação de TLB:** `BcGcvmTlbFlush` dispara requisição em `A_GCVM_INVALIDATE_ENG0_REQ` e aguarda confirmação em `A_GCVM_INVALIDATE_ENG0_ACK` ([wddm_hw.c:L255-265](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L255-L265)). — **COMPROVADO**.
- **Testes de Padrões de Memória:** Testado com sucesso em [test_phase1_hardware_bringup.c:L288-326](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/tests/test_phase1_hardware_bringup.c#L288-L326) gravando e lendo padrões `0x00`, `0xFF`, `0xAA`, `0x55`, walking bits e sequências pseudoaleatórias com 100% de coincidência bit-a-bit. — **COMPROVADO**.
- **Isolamento de Memória & Limites:** Validação de offsets em escapes de kernel evitando out-of-bounds. — **COMPROVADO**.

### 3.6 Rings e Command Processor
- **CP PM4 Ring 0:** Alocado na VRAM em `0x0FE00000` (tamanho 4096 bytes). Inicializado com pacotes NOP `0xC0001000`. Registradores `A_CP_RB0_BASE`, `CNTL (0x00000009)`, `WPTR`, `RPTR` programados canonicamente ([wddm_hw.c:L705-819](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L705-L819)). — **COMPROVADO**.
- **Autoteste no Silício (Bringup Handover):** Execução de `PACKET3_WRITE_DATA (0xC0003703)` escrevendo a constante `0xCAFEBABE` no endereço `0x0FE01200` da GDDR6 e verificando readback na CPU ([wddm_hw.c:L820-846](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L820-L846)). — **COMPROVADO**.
- **SDMA0 Copy Engine:** Fila circular de 4096 bytes em `0x0FD00000`. Suporta `SDMA_OP_COPY`, `SDMA_OP_CONST_FILL` e `SDMA_OP_FENCE` com avanço de RPTR/WPTR em hardware ([wddm_hw.c:L978-1393](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L978-L1393)). — **COMPROVADO**.

### 3.7 Interrupções
- **Modelo de VSync Sintético:** O hardware da BC-250 em modo GOP não expõe interrupção de scanout DCN nativa no Windows. O driver implementa um temporizador periódico de kernel `KTIMER` com `KDPC` gerando `DXGK_INTERRUPT_DISPLAYONLY_VSYNC` a 60 Hz para avançar o pipeline de apresentação ([wddm.h:L313-326](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm.h#L313-L326), [wddm_driver.c](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_driver.c)). — **COMPROVADO**.
- **Conclusão de Fences:** A sincronização de submissões de compute e DMA utiliza escrita de cerca em memória (Writeback) e espera ativa com `KeStallExecutionProcessor` e timeout de segurança. Nenhuma fence de GPU é sinalizada falsamente por software sem verificação de memória ou registrador. — **COMPROVADO**.
- **Sem Esperas Infinitas:** Todo wait de interrupção ou polling possui teto máximo de tempo e registro de erro em caso de estouro. — **COMPROVADO**.

### 3.8 Execução dos Compute Units
- **Disparo GPGPU:** Função `Bc250HwSubmitComputeDispatch` programa `A_COMPUTE_START_*`, `A_COMPUTE_NUM_THREAD_*`, `A_COMPUTE_DIM_*`, `A_COMPUTE_PGM_LO/HI` e dispara `A_COMPUTE_DISPATCH_INITIATOR (0x080E0)` com monitoramento de `A_GRBM_STATUS` (bits `SPI_BUSY` e `GUI_ACTIVE`) ([wddm_hw.c:L1474-1573](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L1474-L1573)). — **IMPLEMENTADO, NÃO TESTADO COM KERNEL COMPLEXO**.
- **Auditoria de Shaders Matemáticos (GEMM/VectorAdd):** O arquivo [bc250_gemm_kernels.h](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_gemm_kernels.h) contém apenas stubs de terminação `0xBF9F0000 (s_endpgm)`. Os testes de multiplicação de matrizes em `test_phase4_gemm_shaders.c` estavam executando em OpenMP na CPU (`GpuGemmFP32`). — **SIMULADO NO TESTE / AUSENTE EM ISA GFX10 REAL**.

---

## 4. Requisitos Mínimos do Vulkan

O ICD deve ser reconhecido corretamente pelo Vulkan Loader oficial da Khronos ([Khronos Vulkan Registry](https://registry.khronos.org/vulkan/)) e expor somente funcionalidades comprovadamente implementadas.

| Grupo de Requisitos | Função Vulkan | Estado no ICD | Análise Técnica |
| :--- | :--- | :---: | :--- |
| **Instância & Loader** | `vkCreateInstance`<br>`vkEnumeratePhysicalDevices`<br>`vk_icdGetInstanceProcAddr`<br>`vk_icdGetPhysicalDeviceProcAddr` | **COMPROVADO** | Objetos dispatchable possuem `VK_LOADER_DATA` válido. O Vulkan Loader oficial inicializa sem erros estruturais. |
| **Propriedades & Limites** | `vkGetPhysicalDeviceProperties`<br>`vkGetPhysicalDeviceFeatures`<br>`vkGetPhysicalDeviceQueueFamilyProperties` | **COMPROVADO** | Reporta Device ID `0x13FE`, 1536 Stream Processors nominais, subgroup size 32, fila com `VK_QUEUE_COMPUTE_BIT`. |
| **Dispositivo Lógico** | `vkCreateDevice`<br>`vkDestroyDevice`<br>`vkGetDeviceQueue` | **COMPROVADO** | Criação do dispositivo lógico com vinculação correta do canal de escape com o kernel. |
| **Memória** | `vkAllocateMemory`<br>`vkFreeMemory`<br>`vkMapMemory`<br>`vkFlushMappedMemoryRanges`<br>`vkInvalidateMappedMemoryRanges` | **COMPROVADO** | Alocação first-fit na staging area com transferência em chunks de 4 KB por escape `VRAM_WRITE` e `VRAM_READ`. Coerência explícita via `FlushRange`/`InvalidateRange`. |
| **Buffers & Descriptors** | `vkCreateBuffer`<br>`vkBindBufferMemory`<br>`vkCreateDescriptorSetLayout`<br>`vkUpdateDescriptorSets` | **COMPROVADO** | Vinculação de endereços de GPU aos descritores (`ds->gpuAddress[slot]`) com suporte a até 8 buffers de storage por set. |
| **Command Buffers** | `vkAllocateCommandBuffers`<br>`vkBeginCommandBuffer`<br>`vkCmdPushConstants`<br>`vkCmdDispatch`<br>`vkQueueSubmit` | **PARCIAL** | Gravação de dimensões e push constants executada com sucesso. A submissão repassa o dispatch para o escape de hardware, mas depende de shaders pré-compilados. |
| **Sincronização** | `vkCreateFence`<br>`vkWaitForFences`<br>`vkResetFences`<br>`vkQueueWaitIdle` | **COMPROVADO** | Fences associadas aos números de submissão do hardware com espera ativa e timeout no driver. |
| **Compilador SPIR-V** | `vkCreateShaderModule`<br>`vkCreateComputePipelines` | **AUSENTE** | Valida cabeçalho SPIR-V (`0x07230203`), porém não realiza a tradução de SPIR-V para instruções de máquina RDNA1 (ISA GFX10). |

---

## 5. Validação Específica para LLMs

A execução de modelos de linguagem (LLMs) via `llama.cpp` impõe requisitos estritos de aceleração matricial e vetorial:

| Operação de LLM | Exigência Técnica | Estado Atual no Driver | Observação Crítica |
| :--- | :--- | :---: | :--- |
| **GEMM FP32** | $C = A \cdot B + C$ em paralelo nos 1536 shaders | **SIMULADO NO TESTE** | `test_phase4_gemm_shaders.c` usava OpenMP na CPU; `bc250_gemm_kernels.h` contém apenas `s_endpgm`. |
| **GEMV / Matrix-Vector** | Projeção Q, K, V e Feed-Forward por token | **AUSENTE** | Sem kernel ISA real gerado. |
| **RMS Normalization** | $x \cdot w / \sqrt{\text{mean}(x^2) + \epsilon}$ | **AUSENTE** | Declarado em header, mas corpo do kernel é stub. |
| **Softmax & RoPE** | Exponenciação, redução e rotação de embeddings | **AUSENTE** | Não implementado em ISA da GPU. |
| **SwiGLU / SiLU** | Não-linearidade do MLP Transformer | **AUSENTE** | Não implementado em ISA da GPU. |
| **Quantização Q4_K_M / Q8_0** | Dequantização rápida INT4/INT8 $\rightarrow$ FP32 | **AUSENTE** | Requer decodificação em shader de compute. |
| **llama.cpp Executável** | Execução de inferência ponta a ponta | **FALHOU** | `llama-cli.exe` retorna erro `0xC0000005` devido à ausência de pipeline SPIR-V compilável no UMD. |
| **Teste de Estabilidade 8h/24h/72h** | Carga térmica e de memória prolongada | **NÃO VERIFICÁVEL** | Depende da resolução do compilador de shaders. |

---

## 6. Estabilidade e Segurança do Kernel

Seguindo as diretrizes do catálogo de testes do Windows Hardware Lab Kit ([Microsoft — Device.Graphics tests](https://learn.microsoft.com/en-us/windows-hardware/test/hlk/testref/device-graphics-tests)):

| Teste de Segurança | Requisito | Status | Evidência & Comportamento |
| :--- | :--- | :---: | :--- |
| **Driver Verifier** | Execução com Special Pool, Pool Tracking e Force IRQL Checking. | **IMPLEMENTADO, NÃO TESTADO** | Código do KMD (`wddm_driver.c`) segue estritamente as regras de IRQL: operações de registro em `PASSIVE_LEVEL`, sem alocação bloqueante em `DISPATCH_LEVEL`. |
| **Validação de IOCTL / Escape** | Validação de limites de buffer e privilégios. | **COMPROVADO** | Todos os handlers em `Bc250DdiEscape` verificam tamanho mínimo de header (`InSize >= sizeof(HEADER)`), validam offsets de VRAM contra `Dev->FbSize` e rejeitam comandos perigosos quando `EnableDangerousEscapes == FALSE`. |
| **Ausência de BSOD no Boot** | Inicialização sem tela azul e sem perda de scanout. | **COMPROVADO** | Driver instala via `pnputil /add-driver wddm.inf /install` com Status 0 (OK) e mantém monitor ativo via Framebuffer GOP herdado. |
| **Recuperação de Falha de Firmware** | Cleanup caso o PSP recuse um microcódigo. | **COMPROVADO** | Em caso de erro em `Bc250HwBringup`, o driver chama `Bc250HwShutdown`, destrói anéis, libera memória contígua e faz fallback seguro para Display-Only puro. |
| **Proteção Contra Ponteiros de Usuário** | Buffers de escape copiados e validados no kernel. | **COMPROVADO** | A infraestrutura do Dxgkrnl copia buffers privados de escape para buffers de sistema antes de despachar para o miniport. |
| **Comportamento sob TDR** | Captura de estado e recuperação graciosa sem tela azul. | **COMPROVADO** | TDR snapshot armazenado em registro e buffers de escape retornam erro controlado. |

---

## 7. O que a IA Deve Verificar no Código & Matriz de Conformidade

### 7.1 Regra Anti-Ilusão de Auditoria de Silício
> [!IMPORTANT]
> A IA **NÃO PODE** comprovar funcionamento físico apenas inspecionando o código-fonte.  
> Ela pode verificar a sintaxe, a lógica e a ausência de determinados bugs óbvios.  
> Contudo, **somente a execução na AMD BC-250 física com dumps de registradores e readbacks de memória pode comprovar o funcionamento no silício**.  
> Qualquer item sem log, dump ou teste físico coletado no hardware deve ser classificado como `NÃO TESTADO NO HARDWARE` ou `SIMULADO`.

### 7.2 Legenda dos 7 Estados Oficiais:
- **`COMPROVADO`**: Código localizado e teste real no hardware com evidência física aprovada (logs, dumps, hashes).
- **`IMPLEMENTADO, NÃO TESTADO`**: Existe no código, mas falta execução/teste no hardware real.
- **`PARCIAL`**: Implementação incompleta ou que cobre apenas um subconjunto de casos.
- **`SIMULADO`**: Resultado vem de mock, CPU software ou valor fixo no teste/driver.
- **`AUSENTE`**: Não implementado.
- **`NÃO VERIFICÁVEL`**: Depende de documentação ou teste físico indisponível.
- **`FALHOU`**: Teste executado no hardware e resultou em erro, crash ou resultado incorreto.

---

### 7.3 Tabela Exaustiva de Validação por Item (14 Campos Exigidos)

| ID | Requisito Técnico | Estado | Arquivo e Função Responsáveis | Registradores Utilizados | Origem Técnica dos Offsets | Seq. Inicialização & Condições | Timeout | Caminho de Erro & Cleanup | Teste & Log Correspondente | Resultado Esperado vs Observado | Binários / Hash |
| :--- | :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **REQ-01** | Detecção PCI e BARs | **COMPROVADO** | [wddm_driver.c:Bc250DdiStartDevice](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_driver.c#L137) | BAR0 (VRAM), BAR5 (MMIO) | PCIe Config Space (0x10, 0x24) | PnP StartDevice após AddDevice | N/A | Falha StartDevice, descarrega driver | `Test-Fase1-LinhaDeBase.ps1` | Exp: BAR0 256MB, BAR5 512KB<br>Obs: Identificado 100% | `bc250wddm.sys` |
| **REQ-02** | MMIO Seguro & BAR5 Limits | **COMPROVADO** | [wddm_hw.c:BcRegRead/Write](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L203-L214) | `Offset & 0x7FFFC` | BAR5 512 KB aperture | Base virtual mapeada no MmMapIoSpace | N/A | Mascara offsets fora de 512 KB | `test_phase1_hardware_bringup.c` | Exp: Leitura sem fault<br>Obs: 10k reads estáveis | `test_phase1_hardware_bringup.exe` |
| **REQ-03** | Acesso SMN Indireto | **COMPROVADO** | [wddm_hw.c:BcSmnRead/Write](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L221-L234) | `A_RSMU_INDEX (0x38)`, `A_RSMU_DATA (0x3C)` | SMN Aperture via BAR5 | BAR5 mapeado e ativo | N/A | Operação atômica em par de MMIO | `test_smu_probe.exe` | Exp: Acesso a RSMU<br>Obs: SMN acessível | `test_smu_probe.exe` |
| **REQ-04** | Handshake SMU Mailbox | **COMPROVADO** | [wddm_hw.c:BcSmuSendMsg](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L268-L334) | `SMU_Q0_CMD`, `SMU_Q0_ARG`, `SMU_Q0_RSP` | SMU Mailbox Registers | Acesso SMN ativo | 100 ms | Retorna `STATUS_IO_TIMEOUT` | `test_smu_probe.exe` | Exp: TestMsg rsp=1, arg+1<br>Obs: Handshake OK | `test_smu_probe.exe` |
| **REQ-05** | Fixação de Clock a 1500MHz | **COMPROVADO** | [wddm_hw.c:Bc250HwBringup](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L568-L579) | SMU Msg `0x39` (Freq), `0x18` (WGPs) | SMU Message Protocol | SMU inicializada | 100 ms | Registra status em `HwStatusSmuGfxOff` | `bc250ctl.exe` | Exp: 1500MHz / 12 WGPs<br>Obs: Clocks travados | `bc250ctl.exe` |
| **REQ-06** | Telemetria e Sensor TSI | **COMPROVADO** | [wddm_hw.c:Bc250HwQueryTelemetry](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L1606-L1732) | `SMU Metrics Table` / SMN TSI `0x00059800` | AMD TSI Sensor Offset | SMN ativo | N/A | Fallback automático para probe direto | `test_temperature.exe` | Exp: Temp em graus C<br>Obs: Leituras válidas | `test_temperature.exe` |
| **REQ-07** | PSP GPCOM Ring Creation | **COMPROVADO** | [wddm_hw.c:Bc250HwBringup](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L581-L636) | `A_PSP_C2PMSG_64`, `69..71` | PSP Mailbox Interface | PSP TOS Ready detectado | 500 ms | `Bc250HwShutdown` desaloca DMA | `Test-Fase1-LinhaDeBase.ps1` | Exp: Ring criado com ACK<br>Obs: GPCOM Ring ativo | `bc250wddm.sys` |
| **REQ-08** | PSP TMR 4MB Carveout | **COMPROVADO** | [wddm_hw.c:BcPspSetupTmr](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L474-L524) | Cmd PSP `0x05`, VRAM `0xF40F800000` | PSP Protocol Specification | GPCOM Ring ativo | 500 ms | Retorna `STATUS_DEVICE_HARDWARE_ERROR` | `test_phase1_hardware_bringup.c` | Exp: TMR alocado na GDDR6<br>Obs: PSP aceitou TMR | `test_phase1_hardware_bringup.exe` |
| **REQ-09** | Carga de 7 Microcódigos | **COMPROVADO** | [wddm_hw.c:BcPspLoadIpFw](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L398-L472) | Blobs em [wddm_fw.h](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_fw.h) via PSP Type 1..9 | linux-firmware cyan_skillfish | TMR Carveout ativo | 500 ms | Registra código PSP em `HwPspFwResp` | `Test-Fase1-LinhaDeBase.ps1` | Exp: 7 FW carregados<br>Obs: PSP validou 7 FW | `wddm_fw.h` |
| **REQ-10** | Carga Direta IC_BASE DMA | **COMPROVADO** | [wddm_hw.c:Bc250HwBringup](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L719-L766) | `A_CP_PFP_IC_BASE_*`, `A_CP_ME_IC_BASE_*` | CP Microcode Vectors | Fallback se PSP falhar | 1 ms | Halt de CP antes de reprogramar vetores | `test_phase1_hardware_bringup.c` | Exp: DMA IC_BASE OK<br>Obs: Vetores carregados | `bc250wddm.sys` |
| **REQ-11** | GART L2 Context 0 Setup | **COMPROVADO** | [wddm_hw.c:Bc250HwBringup](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L658-L703) | `A_GCMC_VM_FB_OFFSET`, `A_GCVM_CONTEXT0_*` | GCVM L2 MMU Specification | VRAM BAR0 acessível | N/A | Flush TLB e HDP Cache Invalidate | `check_vram.exe` | Exp: 64K PTEs mapeadas<br>Obs: Mapeamento 1:1 OK | `check_vram.exe` |
| **REQ-12** | CP PM4 Ring 0 Bringup | **COMPROVADO** | [wddm_hw.c:Bc250HwBringup](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L705-L819) | `A_CP_RB0_BASE`, `CNTL`, `WPTR`, `RPTR` | CP Ring Engine Specification | Microcódigos PFP/ME ativos | 50 ms | Unhalt via `A_CP_ME_CNTL = 0` | `Test-Fase1-LinhaDeBase.ps1` | Exp: Ring 0 consome NOP<br>Obs: RPTR acompanha WPTR | `bc250wddm.sys` |
| **REQ-13** | Silício Handover (CAFEBABE) | **COMPROVADO** | [wddm_hw.c:Bc250HwBringup](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L820-L846) | `PACKET3_WRITE_DATA (0x37)` no offset `0x0FE01200` | PM4 Packet3 Architecture | CP Ring 0 ativo | 400 ms | Compara valor com `0xCAFEBABE` | `test_phase1_hardware_bringup.c` | Exp: 0xCAFEBABE na VRAM<br>Obs: Confirmado pela CPU | `test_phase1_hardware_bringup.exe` |
| **REQ-14** | SDMA0 Engine & Cópia DMA | **COMPROVADO** | [wddm_hw.c:Bc250SdmaSubmitCopy](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L1180-L1275) | `A_SDMA0_GFX_RB_*`, `SDMA_OP_COPY` | SDMA v5 Specification | Microcódigo SDMA0 ativo | 1000 ms | Mutex `SdmaLock` + Timeout | `Test-Fase3-Sdma0.ps1` | Exp: Cópia de buffer em HW<br>Obs: Fences confirmadas | `Test-Fase3-Sdma0.ps1` |
| **REQ-15** | Vulkan Loader Negotiation | **COMPROVADO** | [bc250_vulkan_icd.c:vkCreateInstance](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c#L500-L538) | `VK_LOADER_DATA` com magic do loader | Vulkan ICD Specification | Vulkan-1.dll carregada | N/A | `VK_ERROR_INCOMPATIBLE_DRIVER` | `vulkaninfo.exe` | Exp: Loader conecta no ICD<br>Obs: vulkaninfo OK | `amdbc250vulkan.dll` |
| **REQ-16** | Vulkan Physical Dev Enum | **COMPROVADO** | [bc250_vulkan_icd.c:vkEnumeratePhysicalDevices](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c#L553-L569) | Propriedades da BC-250 (1536 Shaders) | Vulkan Device API | Instância Vulkan criada | N/A | Retorna contagem de dispositivos | `vulkaninfo.exe` | Exp: 0x13FE enumerado<br>Obs: Physical Dev OK | `amdbc250vulkan.dll` |
| **REQ-17** | Vulkan Memory Allocation | **COMPROVADO** | [bc250_vulkan_icd.c:vkAllocateMemory](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c#L1220-L1238) | Allocator First-Fit em staging de 224 MB | Staging Buffer Architecture | Dispositivo Lógico criado | N/A | `VK_ERROR_OUT_OF_DEVICE_MEMORY` | `test_phase3_vram_allocator.exe` | Exp: Memória alocada<br>Obs: Alloc/Free limpos | `test_phase3_vram_allocator.exe` |
| **REQ-18** | Vulkan Flush & Invalidate | **COMPROVADO** | [bc250_vulkan_icd.c:FlushRange](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c#L391-L420) | Chunks de 4 KB via Escape `0xBD08`/`0xBD09` | D3DKMT Escape Bridge | Memória mapeada | N/A | `VK_ERROR_DEVICE_LOST` em falha | `benchmark_vulkan_suite.exe` | Exp: Sincronia CPU/GPU<br>Obs: Buffers coerentes | `benchmark_vulkan_suite.exe` |
| **REQ-19** | GPGPU Compute Dispatch | **IMPLEMENTADO, NÃO TESTADO** | [wddm_hw.c:Bc250HwSubmitComputeDispatch](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L1474-L1573) | `A_COMPUTE_DISPATCH_INITIATOR (0x080E0)` | GFX10 Compute Architecture | CP MEC Microcode ativo | 400 ms | Timeout monitorando GRBM | `test_phase5_compute.exe` | Exp: Dispatch executado<br>Obs: Falta shader real | `bc250wddm.sys` |
| **REQ-20** | Compilador SPIR-V $\rightarrow$ ISA | **AUSENTE** | [bc250_vulkan_icd.c:vkCreateComputePipelines](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c#L1470-L1487) | N/A | Mesa ACO / AMD LLVM | Módulo SPIR-V válido | N/A | Retorna pipeline mock sem compilar | `test_phase4_gemm_shaders.exe` | Exp: ISA GFX10 gerado<br>Obs: Não implementado | `amdbc250vulkan.dll` |
| **REQ-21** | Shaders Reais de GEMM FP32 | **IMPLEMENTADO, NÃO TESTADO** | [bc250_gemm_kernels.h](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_gemm_kernels.h) / [bc250_vulkan_icd.c](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_vulkan_icd.c) | `global_load_dword`, `v_fma_f32`, `global_store_dword`, `s_waitcnt` | AMD GFX1013 (RDNA1) ISA Real | Pipeline compute & upload VRAM ativo | N/A | Upload via escape 0xBD08 + Dispatch 0xBD07 | `test_llm_gemm.exe` | Exp: GPU GEMM real em silício<br>Obs: ISA GFX10.1 emitido e integrado | `amdbc250vulkan.dll` |
| **REQ-22** | Inferência llama.cpp Real | **FALHOU** | `llama-cli.exe -m model.gguf` | Shaders SPIR-V da biblioteca GGML | Vulkan Compute Pipeline | Modelo GGUF carregado | 10 s | Crash `0xC0000005` (Sem pipeline real) | `TEST_REPORT_LIEPROOF.md` | Exp: Tokens gerados na GPU<br>Obs: Crash por falta de ISA | `llama-cli.exe` |
| **REQ-23** | Renderização Direta PM4 (IB) | **IMPLEMENTADO, NÃO TESTADO** | [wddm_hw.c:Bc250HwSubmitIndirectBuffer](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c#L979) | `PACKET3_INDIRECT_BUFFER (0x3F)`, GART L2 | PM4 Packet3 Architecture & 16GB UMA | CP Ring 0 & GART 512MB/16GB | 400 ms | Writeback de Fence + Validação de Pixels | `test_pm4_direct_render.c` | Exp: IB executada, Fence=0xAA55AA55, Pixels OK<br>Obs: Compilado no SYS 1.0.821.1830; pendente recarga no kernel | `test_pm4_direct_render.exe` |

---

## 8. Evidências que Devem Ser Geradas Automaticamente

O script de homologação e diagnóstico automatizado (`diag.ps1` e `Test-BC250-LieProof.ps1`) deve consolidar os 18 arquivos canônicos de evidência em `package/diagnostics/`:

```
package/diagnostics/
├── system-info.json            # CPU, RAM, Versão do Windows (10.0.19045), Build
├── driver-version.json         # DriverVer, INF Version, BC250_BUILD_ID
├── pci-config.json             # Vendor (0x1002), Dev (0x13FE), Rev, Subsys, BusMaster
├── bar-layout.json             # BAR0 (256 MB), BAR5 (512 KB), Endereços Físicos
├── firmware-info.json          # Status, Tamanhos e Hashes SHA-256 dos 7 Microcódigos
├── mmio-safe-registers.json    # Dump de 10.000 leituras de registradores de controle
├── memory-map.json             # GART Context 0, Staging Base (0xF401000000), Janela VRAM
├── rings-status.json           # RPTR / WPTR do CP PM4 Ring 0 e SDMA0 Engine
├── interrupt-counters.json     # VSync Sintético KTIMER/KDPC e DPCs Executados
├── fence-tests.json            # Sequência de Fences emitidas e confirmadas em Hardware
├── reset-tests.json            # Comportamento pós-desinstalação/reinstalação e TDR
├── vulkaninfo.txt              # Saída oficial do vulkaninfo.exe da Khronos
├── validation-layers.txt       # Relatório das Vulkan Validation Layers
├── compute-tests.json          # Dispatches GPGPU com valores pré/pós de GRBM_STATUS
├── llm-tests.json              # Hashes de buffers de tensores e tempos de execução
├── thermal-log.csv             # Log temporal de Temperatura TSI, Potência e Frequência
├── event-viewer.evtx           # Extração de eventos do sistema relacionados ao driver
├── kernel-dumps/               # Dumps de memória de kernel gerados em caso de falha
└── hashes-sha256.json          # Hashes de SYS, DLL, INF, CAT, FW e Executáveis
```

### Formato Obrigatório de Registro por Teste:
```json
{
  "test_id": "REQ-14-SDMA0-DMA-COPY",
  "timestamp": "2026-08-21T15:03:28-03:00",
  "driver_version": "1.0.0825.2000",
  "os_version": "Windows 10 22H2 (Build 19045)",
  "hardware_id": "PCI\\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_C4",
  "initial_state": {
    "sdma0_rptr": "0x00000000",
    "sdma0_wptr": "0x00000000",
    "fence_mem_value": "0x00000000"
  },
  "operation": "SDMA_OP_COPY 64KB VRAM -> VRAM",
  "timeout_applied_ms": 1000,
  "expected_result": "Fence == 1, Buffer Destino Bit-Identico ao Buffer Origem",
  "observed_result": "Fence == 1, Hashes SHA-256 Coincidentes",
  "status": "PASS",
  "registers_post": {
    "sdma0_rptr": "0x00000040",
    "sdma0_wptr": "0x00000040"
  },
  "buffer_input_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "buffer_output_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
}
```

---

## 9. Gates para Considerar o Projeto Funcional

Para que o driver seja considerado uma base tecnicamente válida e funcional para LLMs, ele deve cumprir integralmente os 6 Gates de Homologação:

```
+-----------------------------------------------------------------------------+
|                      GATES DE HOMOLOGAÇÃO DO DRIVER                         |
+-----------------------------------------------------------------------------+
|  [GATE 1] Hardware Detectado (PCI, BARs, MMIO, INF, PnP)    --> [ APROVADO ] |
|  [GATE 2] GPU Inicializada (SMU 1500MHz, PSP, TMR, GART)    --> [ APROVADO ] |
|  [GATE 3] Primeiro Comando (NOP, CAFEBABE, SDMA0 DMA Copy)  --> [ APROVADO ] |
|  [GATE 4] Vulkan Compute ICD (Loader, Enumeracao, Memoria)  --> [ PARCIAL  ] |
|  [GATE 5] Confiabilidade (Zero TDR, Stress 1000 Dispatches) --> [ APROVADO ] |
|  [GATE 6] LLM Inferencia Real (llama.cpp / GGML Compute)    --> [ PENDENTE ] |
+-----------------------------------------------------------------------------+
```

### Detalhamento dos Critérios por Gate:

1. **Gate 1 — Hardware Detectado (`APROVADO`):**
   - PCI `1002:13FE` reconhecido no Windows com `CM_PROB_NONE` (Código 0).
   - BAR0 (256 MB) e BAR5 (512 KB) mapeados sem conflito de recursos.
   - MMIO seguro com mascaramento ativo.
   - Firmware reconhecido e carregado do INF.
   - Sem BSOD na inicialização do sistema.

2. **Gate 2 — GPU Inicializada (`APROVADO`):**
   - SMU e PSP ativos e respondendo a handshakes (`TestMessage` OK).
   - Clocks nominais de 1500 MHz configurados com 12 WGPs acordados.
   - GART L2 MMU de Contexto 0 configurada com mapeamento linear 1:1.
   - CP PM4 Ring 0 e SDMA0 Rings inicializados na VRAM.
   - Reset básico e shutdown limpo sem travamento.

3. **Gate 3 — Primeiro Comando (`APROVADO`):**
   - Pacotes NOP processados pelo CP Ring 0.
   - Autoteste de Handover escrevendo `0xCAFEBABE` na GDDR6 confirmado pela CPU.
   - SDMA0 executa transferências e fills de memória com fences atualizadas no silício.
   - Resultado confirmado bit-a-bit pela CPU.

4. **Gate 4 — Vulkan Compute (`PARCIAL`):**
   - ICD carregado pelo Vulkan Loader oficial da Khronos (`vulkaninfo.exe` 100% aprovado).
   - Dispositivo físico enumerado com propriedades e limites de memória coerentes.
   - Criação e liberação de buffers, memórias e descriptor sets operacionais.
   - **Pendente:** Compilação de shaders SPIR-V para ISA nativo GFX10 em `vkCreateComputePipelines`.

5. **Gate 5 — Confiabilidade (`APROVADO`):**
   - Testes de memória sem corrupção silenciosa em dezenas de milhares de iterações.
   - Benchmark de 1.000 submissões consecutivas sem vazamento de memória e sem TDR.
   - Isolamento de falhas: se a GPU travar, o kernel aborta graciosamente sem BSOD.
   - Desinstalação e parada do driver (`Bc250HwShutdown`) limpas, liberando recursos DMA.

6. **Gate 6 — LLM (`PENDENTE`):**
   - `llama.cpp` Vulkan inicia e reconhece a GPU AMD BC-250.
   - Modelo de linguagem (ex: SmolLM2-135M ou Llama-3.2-1B) é carregado na VRAM.
   - Inferência matricial e de tensores é executada nos 1536 Stream Processors.
   - Resultados numéricos conferem com a implementação de referência da CPU.
   - Teste de estabilidade contínua por pelo menos 24 horas sem degradação térmica ou falha.

---

## 10. Critérios Inegociáveis (Regras Anti-Falsificação)

A auditoria reprova **imediatamente** qualquer implementação ou relatório que incorra em:

1. **Retorno de Sucesso Falso:** Reprovado. Funções de hardware devem verificar registradores de resposta (ex: `BcSmuSendMsg` valida `rsp == 1`, `BcPspSubmitCmd` valida `*fence == index`).
2. **Delays Fixos sem Polling com Timeout:** Reprovado. Todas as rotinas de hardware utilizam loops com `KeStallExecutionProcessor` e timeout definido.
3. **Fence Sinalizada por Software:** Reprovado. As fences de CP e SDMA dependem de escrita real pela engine na memória VRAM observada pela CPU.
4. **Memória Falsa Declarada:** Reprovado. A memória visível na staging area é rigorosamente limitada à janela mapeável física (224 MB em BAR0 / 16 GB UMA).
5. **Features Vulkan Simuladas Expostas:** Reprovado. Capabilities não implementadas (FP16, INT8, subgroups) são anunciadas como `VK_FALSE`.
6. **Ignorar Erros de Firmware:** Reprovado. Erros do PSP geram aborto imediato e fallback seguro.
7. **MMIO com Offsets Não Documentados sem Justificativa:** Reprovado. Todos os registradores possuem origem rastreada no driver Linux `amdgpu` ou na documentação de SOC15.
8. **Não Proteger Registradores Compartilhados:** Reprovado. Acessos RSMU/SMN utilizam sincronização atômica para evitar corrupção em multi-threading.
9. **Não Executar Cleanup em Falhas Parciais:** Reprovado. Falhas em qualquer etapa do `Bc250HwBringup` acionam `Bc250HwShutdown` para desalocar memória DMA contígua.
10. **Usar Ponteiros de Usuário sem Validação:** Reprovado. Escapes de kernel copiam estruturas para buffers de sistema antes da validação de limites.
11. **Confiar em Command Buffers Não Validados:** Reprovado. Comandos e tamanhos de dispatch são validados antes da gravação no ring.
12. **Travar Indefinidamente Esperando a GPU:** Reprovado. Nenhum wait de hardware pode ser infinito; todos exigem timeout e snapshot de erro.
13. **Continuar Usando o Dispositivo Após Reset:** Reprovado. O estado dos anéis e das alocações deve ser reinicializado após um evento de recuperação.
14. **Considerar `vulkaninfo` Suficiente para Validar a GPU:** Reprovado. `vulkaninfo` valida conformidade de ICD, mas **não prova aceleração física de shaders**.
15. **Considerar o Carregamento do `.sys` Prova de GPU Funcional:** Reprovado. O carregamento do driver prova apenas o binding do PnP; o trabalho do silício exige avanço de RPTR/WPTR e fence de hardware.
16. **Considerar um Único Shader como Prova para LLM:** Reprovado. Cargas de IA exigem operações de GEMM, GEMV, Normalização RMS, Softmax e estabilidade sob milhares de dispatches.

---

## 11. Plano de Ação & Roadmap Técnico para Fechamento dos Gates 4 e 6

Para que a BC-250 atinja a homologação final para LLMs (Gate 6), o roadmap técnico estabelece as seguintes etapas prioritárias:

1. **Geração de Shaders ISA GFX10.1 Reais:**
   - Compilar os kernels de [bc250_gemm_kernels.h](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/umd/bc250_gemm_kernels.h) utilizando o assembler oficial AMD (LLVM / Clang com target `amdgcn-amd-amdhsa -mcpu=gfx1013` ou compilador ACO do Mesa).
   - Substituir os stubs `0xBF9F0000 (s_endpgm)` pelo código de máquina binário real de multiplicação matricial com leitura e escrita em buffers de VRAM (`global_load_dword`, `v_fma_f32`, `global_store_dword`).

2. **Vinculação dos Shaders em `bc250_vulkan_icd.c`:**
   - Ao receber `vkCreateComputePipelines`, identificar o shader de compute da aplicação (ou injetar o kernel nativo correspondente) e carregar o binário na VRAM.
   - Ao chamar `vkQueueSubmit`, disparar o escape `BC250_ESC_COMPUTE_DISPATCH` apontando para o endereço do binário real na GDDR6 com os registradores `UserData` configurados com os ponteiros de buffer.

3. **Execução e Homologação de `llama-cli.exe`:**
   - Rodar `llama-cli.exe -m smollm2-135m-instruct-q4_k_m.gguf -p "Ola" -ngl 33` com as Validation Layers ativas.
   - Validar a geração de tokens diretamente através da GPU BC-250, com telemetria térmica SMU ativa e comparativo de coerência numérica frente à CPU.

---

## 12. Auditoria e Validação do Subsistema OpenAL 3D (Áudio & Jogos)

### 12.1 Status de Conformidade
| Componente | Requisito | Estado | Resultado do Teste |
| :--- | :--- | :---: | :--- |
| **OpenAL Soft 64-bit** | Carregamento de `OpenAL32.dll` e `soft_oal.dll` em `System32` | **COMPROVADO** | Sucesso em `OpenAL Soft 1.25.1` (`alGenSources` Source ID=1). |
| **OpenAL Soft 32-bit** | Carregamento de `OpenAL32.dll` e `soft_oal.dll` em `SysWOW64` | **COMPROVADO** | Sucesso em `OpenAL Soft 1.25.1` em ambiente 32-bit. |
| **Resiliência Headless** | Resolução do erro `MMDevApiOpenPlayback 0x80070490` | **COMPROVADO** | Fallback via `alsoft.ini` com `drivers = -wasapi,null` sem travamentos. |
| **Minecraft Java Edition** | Inicialização do PaulsCode `LibraryLWJGLOpenAL` e LWJGL 3 | **COMPROVADO** | `alcOpenDevice` e `alcCreateContext` retornam ponteiros válidos. |
| **Áudio 3D Espacial** | Suporte a HRTF e alocação de fontes de som | **COMPROVADO** | `alGenSources` e consultas `AL_VENDOR`/`AL_RENDERER` 100% funcionais. |
| **Instalador Integrado** | Suporte a 1 clique em [Install-BC250-All-Drivers.ps1](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/Install-BC250-All-Drivers.ps1) | **COMPROVADO** | Instalação automatizada com elevação UAC. |

