# 708 — Funcionamento Completo do Driver AMD Windows para RDNA1 (amdkmdag.sys / WDDM 2.7): Engenharia Reversa Forense, Todas as Chamadas DDI do Windows e Arquitetura DCN 2.0/2.1

**Data:** 12/10/2026  
**Documento Técnico:** Manual de Arquitetura e Engenharia Reversa Forense  
**Binário Analisado:** `Packages/Drivers/Display/WT6A_INF/B413137/amdkmdag.sys` (Versão 24.30 / Adrenalin NimeZ 25.3.1, x86_64, 70.8 MB)  
**Alvo de Hardware:** AMD BC-250 APU (`DeviceID 1002:13FE`, GC 10.1.3 / RDNA1, 24 Compute Units, Carve-out VRAM de 256 MB)  
**Sistema Operacional:** Windows 10 Pro 22H2 (Build 19045), Contrato WDDM 2.7  
**Classificação de Confiança:** **P2** (desmontagens, estruturas, tabelas de exportação e ponteiros obtidos diretamente do binário oficial da AMD; interpretações conceituais marcadas como **H** conforme Lei 8).

---

## 1. Visão Geral e Arquitetura do KMD no Windows

O driver de modo kernel da AMD para Windows (`amdkmdag.sys`) opera como um miniport WDDM de arquitetura híbrida C/C++. Ele faz a ponte entre o subsistema gráfico do Windows (`dxgkrnl.sys`, `vidmm.sys`, `vidsch.sys`) e os blocos de silício da microarquitetura RDNA1 (GFX IP 10.1.x, SDMA 5.0, GVM 10.1, DCN 2.0/2.1 e SMU 11.0).

### 1.1 Camadas Internas de Abstração da AMD:
O binário é organizado internamente em namespaces C++ rigorosamente modularizados:
1. **`nsUSR` (User / OS Wrapper Layer):** Implementa as interfaces de nível de sistema operacional (WDDM DDI). Traduz as chamadas de Dxgkrnl para objetos internos (`CS_OsSegmentsMgr`, `CS_OsVidPnMgr`, `CS_OsPagingContext`, `CS_OsCpuHostAperture`).
2. **`nsSVC` (Service Layer):** Serviços de infraestrutura compartilhada (gerenciamento de memória de vídeo `CS_VmPaging`, filas de submissão, registro de comandos, OCA / crash reporting).
3. **`nsHW` (Hardware Abstraction Layer):** Abstração direta do silício RDNA1 (`CS_Tlb`, `IMTypeFactory`, `CS_DoorbellAllocator`, `CS_PageFaultMgr`).
4. **`PAGEDCIC` / `PAGEDCIR` (AMD Display Core - DC):** O mesmo motor de display presente no Linux upstream (`amdgpu/display/dc`), compilado nativamente no KMD do Windows para controlar os blocos DCN 2.0 (Navi10/14) e DCN 2.1 (Cyan Skillfish / Renoir).
5. **`PAGESIPC` / `PAGESIPD` (Silicon IP Layer):** Motores de baixo nível para programação de microcódigo, inicialização de SDMA (`sdma_5_0_start_engine`, `sdma_5_0_start_paging_queue`), KIQ e gerenciamento de interrupções (IH).

---

## 2. Inicialização do Driver no Windows e o Contrato WDDM 2.7

### 2.1 Ponto de Entrada (`DriverEntry`)
* **RVA:** `0x241c90` (Endereço virtual `0x140241c90`)
* **Parâmetros:**
  - `rcx`: `PDRIVER_OBJECT DriverObject`
  - `rdx`: `PUNICODE_STRING RegistryPath`
* **Desmontagem Forense do Setup:**
  ```x86asm
  0x140241c90: push rbp
  0x140241c92: push rbx
  0x140241c93: push r14
  0x140241c95: lea  rbp, [rsp - 0x6d0]
  0x140241c9d: sub  rsp, 0x7d0
  ...
  0x140241f2e: mov  qword ptr [rbp - 0x80], 0x11007  ; Version = DXGKDDI_INTERFACE_VERSION_WDDM2_7
  ```
O valor `0x11007` identifica o contrato formal como **WDDM 2.7** (Windows 10 2004 / 20H1 e superiores, cobrindo perfeitamente o build 19045).

### 2.2 Tabela de Inicialização (`DRIVER_INITIALIZATION_DATA`)
A estrutura `DRIVER_INITIALIZATION_DATA` reside na pilha do `DriverEntry` iniciando exatamente em `[rbp - 0x80]`. Cada ponteiro DDI é carregado via `lea rcx, [rip + disp]` e armazenado em seu offset correspondente:

| Offset na Struct | Registro/Pilha | Campo WDDM DDI Oficial | Ponteiro VA no `amdkmdag.sys` | Função Interna de Atendimento |
|---|---|---|---|---|
| `0x000` | `rbp - 0x80` | `Version` | `0x11007` (WDDM 2.7) | Constante de versão |
| `0x008` | `rbp - 0x78` | `DxgkDdiAddDevice` | `0x1402436a0` | `Dispatch_AddDevice` |
| `0x010` | `rbp - 0x70` | `DxgkDdiStartDevice` | `0x1402446d0` | `Dispatch_StartDevice` |
| `0x018` | `rbp - 0x68` | `DxgkDdiStopDevice` | `0x140244dc0` | `Dispatch_StopDevice` |
| `0x020` | `rbp - 0x60` | `DxgkDdiRemoveDevice` | `0x1402444d0` | `Dispatch_RemoveDevice` |
| `0x028` | `rbp - 0x58` | `DxgkDdiDispatchIoRequest` | `0x1401fc7a0` | `Dispatch_DispatchIoRequest` |
| `0x030` | `rbp - 0x50` | `DxgkDdiInterruptRoutine` | `0x1401fd5b0` | `InterruptRoutine` (IH Miniport) |
| `0x038` | `rbp - 0x48` | `DxgkDdiDpcRoutine` | `0x1401fca20` | `DpcRoutine` (IH Bottom Half) |
| `0x040` | `rbp - 0x40` | `DxgkDdiQueryChildRelations` | `0x1401ff870` | `Dispatch_QueryChildRelations` |
| `0x048` | `rbp - 0x38` | `DxgkDdiQueryChildStatus` | `0x1401ff960` | `Dispatch_QueryChildStatus` |
| `0x050` | `rbp - 0x30` | `DxgkDdiQueryDeviceDescriptor` | `0x1401ffde0` | `Dispatch_QueryDeviceDescriptor` |
| `0x058` | `rbp - 0x28` | `DxgkDdiSetPowerState` | `0x140201ac0` | `Dispatch_SetPowerState` |
| `0x060` | `rbp - 0x20` | `DxgkDdiNotifyAcpiEvent` | `0x1401fd850` | `Dispatch_NotifyAcpiEvent` |
| `0x068` | `rbp - 0x18` | `DxgkDdiResetDevice` | `0x140200a10` | `Dispatch_ResetDevice` |
| `0x070` | `rbp - 0x10` | `DxgkDdiUnload` | `0x140245040` | `Dispatch_Unload` |
| `0x078` | `rbp - 0x08` | `DxgkDdiQueryAdapterInfo` | `0x1401fffd0` | `Dispatch_QueryAdapterInfo` |
| `0x080` | `rbp + 0x00` | `DxgkDdiCreateDevice` | `0x140243960` | `Dispatch_CreateDevice` |
| `0x088` | `rbp + 0x08` | `DxgkDdiCreateAllocation` | `0x140244430` | `Dispatch_CreateAllocation` |
| `0x090` | `rbp + 0x10` | `DxgkDdiDestroyAllocation` | `0x1401fac00` | `Dispatch_DestroyAllocation` |
| `0x098` | `rbp + 0x18` | `DxgkDdiDescribeAllocation` | `0x1401f9e50` | `Dispatch_DescribeAllocation` |
| `0x0a0` | `rbp + 0x20` | `DxgkDdiGetStandardAllocationDriverData`| `0x1401fbb90` | `Dispatch_GetStandardAllocationDriverData` |
| `0x0a8` | `rbp + 0x28` | `DxgkDdiAcquireSwappingBuffer`| `0x1401fb9b0` | `Dispatch_AcquireSwizzlingRange` |
| `0x0b0` | `rbp + 0x30` | `DxgkDdiReleaseSwappingBuffer`| `0x1401fd340` | `Dispatch_ReleaseSwizzlingRange` |
| `0x0b8` | `rbp + 0x38` | `DxgkDdiPatch` | `0x1401f8920` | `Dispatch_Patch` |
| `0x0c0` | `rbp + 0x40` | `DxgkDdiSubmitCommand` | `0x140200360` | `Dispatch_SubmitCommand` |
| `0x0c8` | `rbp + 0x48` | `DxgkDdiPreemptCommand` | `0x1401fe560` | `Dispatch_PreemptCommand` |
| `0x0d0` | `rbp + 0x50` | `DxgkDdiBuildPagingBuffer` | `0x140203fa0` | `Dispatch_BuildPagingBuffer` |
| `0x0d8` | `rbp + 0x58` | `DxgkDdiSetPalette` | `0x1401feac0` | `Dispatch_SetPalette` |
| `0x0e0` | `rbp + 0x60` | `DxgkDdiSetPointerPosition` | `0x1401f8ad0` | `Dispatch_SetPointerPosition` |
| `0x0e8` | `rbp + 0x68` | `DxgkDdiSetPointerShape` | `0x140201630` | `Dispatch_SetPointerShape` |
| `0x0f0` | `rbp + 0x70` | `DxgkDdiResetFromTimeout` | `0x1402016f0` | `Dispatch_ResetFromTimeout` |
| `0x0f8` | `rbp + 0x78` | `DxgkDdiRestartFromTimeout` | `0x140201840` | `Dispatch_RestartFromTimeout` |
| `0x100` | `rbp + 0x80` | `DxgkDdiEscape` | `0x140200b80` | `Dispatch_Escape` |
| `0x108` | `rbp + 0x88` | `DxgkDdiCollectDbgInfo` | `0x140200e20` | `Dispatch_CollectDbgInfo` |
| `0x110` | `rbp + 0x90` | `DxgkDdiQueryCurrentFence` | `0x140243df0` | `Dispatch_QueryCurrentFence` |
| `0x118` | `rbp + 0x98` | `DxgkDdiGetScanLine` | `0x1401f9760` | `Dispatch_GetScanLine` |
| `0x120` | `rbp + 0xa0` | `DxgkDdiStopDeviceAndReleasePostDisplayOwnership` | `0x1401ffc90` | `Dispatch_StopDeviceAndReleasePostDisplayOwnership` |
| `0x128` | `rbp + 0xa8` | `DxgkDdiSystemDisplayEnable` | `0x1401fd630` | `Dispatch_SystemDisplayEnable` |
| `0x130` | `rbp + 0xb0` | `DxgkDdiSystemDisplayWrite` | `0x1402000f0` | `Dispatch_SystemDisplayWrite` |
| `0x150` | `rbp + 0xd0` | `DxgkDdiSubmitCommandVirtual`| `0x1401f9930` | `Dispatch_SubmitCommandVirtual` |
| `0x158` | `rbp + 0xd8` | `DxgkDdiCreateProcess` | `0x1402048d0` | `Dispatch_CreateProcess` |
| `0x160` | `rbp + 0xe0` | `DxgkDdiDestroyProcess` | `0x1402001d0` | `Dispatch_DestroyProcess` |
| `0x168` | `rbp + 0xe8` | `DxgkDdiSetVirtualMachineData`| `0x1402002c0` | `Dispatch_SetVirtualMachineData` |
| `0x1e8` | `rbp + 0x168`| `DxgkDdiSetRootPageTable` | `0x140201360` | `Dispatch_SetRootPageTable` |
| `0x1f0` | `rbp + 0x170`| `DxgkDdiGetRootPageTableSize`| `0x1402007e0` | `Dispatch_GetRootPageTableSize` |
| `0x1f8` | `rbp + 0x178`| `DxgkDdiMapCpuHostAperture` | `0x140200010` | `Dispatch_MapCpuHostAperture` |
| `0x200` | `rbp + 0x180`| `DxgkDdiUnMapCpuHostAperture` | `0x140201990` | `Dispatch_UnMapCpuHostAperture` |
| `0x248` | `rbp + 0x1c8`| `DxgkDdiResetEngine` | `0x1401fff10` | `Dispatch_ResetEngine` |
| `0x2b8` | `rbp + 0x238`| `DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3` | `0x1401f8b40` | `Dispatch_SetVidPnSourceAddressWithMultiPlaneOverlay3` |
| `0x2c0` | `rbp + 0x240`| `DxgkDdiCheckMultiPlaneOverlaySupport3` | `0x1401fcd00` | `Dispatch_CheckMultiPlaneOverlaySupport3` |
| `0x2e8` | `rbp + 0x268`| `DxgkDdiCreateHwQueue` | `0x1401fd790` | `Dispatch_CreateHwQueue` |
| `0x2f0` | `rbp + 0x270`| `DxgkDdiDestroyHwQueue` | `0x140204830` | `Dispatch_DestroyHwQueue` |
| `0x2f8` | `rbp + 0x278`| `DxgkDdiSubmitCommandToHwQueue` | `0x1401f9040` | `Dispatch_SubmitCommandToHwQueue` |
| `0x328` | `rbp + 0x2a8`| `DxgkDdiSignalMonitoredFence` | `0x140203c70` | `Dispatch_SignalMonitoredFence` |

---

## 3. Catálogo Forense Completo das 191 Chamadas WDDM DDI do Driver AMD

O driver implementa 191 rotinas de despacho que atendem a todas as facetas do pipeline gráfico do Windows. Abaixo, as rotinas são classificadas por domínio operacional:

### 3.1 Domínio 1: Ciclo de Vida do Adaptador e PnP (10 chamadas)
1. `Dispatch_AddDevice`: Instancia o contexto do dispositivo (`MiniportDeviceContext`), aloca estruturas privadas do adaptador.
2. `Dispatch_StartDevice`: Inicializa os recursos de hardware, mapeia as BARs de MMIO (BAR0/BAR1), inicializa o subsistema de interrupções e liga as camadas C++ (`nsUSR::CS_OsSegmentsMgr`, `nsHW::CS_Tlb`).
3. `Dispatch_StopDevice`: Drena todas as filas de hardware, desliga o processamento de comandos e desassocia recursos de DMA.
4. `Dispatch_RemoveDevice`: Libera instâncias internas e memória alocada pelo driver.
5. `Dispatch_LinkDevice`: Vincula dispositivos para configurações de multi-GPU / CrossFire (desabilitado em APUs).
6. `Dispatch_StopDeviceAndReleasePostDisplayOwnership`: Transição crítica durante o encerramento ou reinicialização. O KMD para os motores de hardware, mas preserva a exibição visual na tela e grava as informações do framebuffer no `DXGK_DISPLAY_INFORMATION` para o bootloader/POST.
7. `Dispatch_SystemDisplayEnable`: Habilita a exibição do sistema em tela cheia durante BSOD / BugCheck.
8. `Dispatch_SystemDisplayWrite`: Escreve pixels de texto/bitmap diretamente no framebuffer linear durante telas azuis (BSOD) via acesso de CPU à BAR de VRAM carveout.
9. `Dispatch_SystemGetChildContainerID`: Associa portas de display aos contêineres de dispositivos PnP.
10. `Dispatch_SystemNotifySurpriseRemoval`: Trata desconexão abrupta do adaptador.

### 3.2 Domínio 2: Capacidades e Metadados do Adaptador (9 chamadas)
11. `Dispatch_QueryAdapterInfo`: O coração de consultas do WDDM. Atende:
    - `DXGKQAITYPE_UMDRIVERPRIVATE`: Comunicação direta com a UMD (`atidxx64.dll`). Valida o GUID `{FDE5BBA4-B3F9-46FB-BD-AA-07-28-CE-31-00-B4}`. Retorna ponteiros de aceleração, tamanhos de granularidade e limites de alocação de heap.
    - `DXGKQAITYPE_DRIVERCAPS`: Retorna recursos suportados (WDDM 2.7, GPUVA de 48 bits, Hardware Queues, MultiPlaneOverlay3).
    - `DXGKQAITYPE_QUERYSEGMENT3`: Retorna os segmentos de memória gerenciados (ver Seção 4).
    - `DXGKQAITYPE_NUMPOWERCOMPONENTS`: Componentes de energia gerenciados pela SMU.
12. `Dispatch_QueryChildRelations`: Enumera as saídas de vídeo (conectores DP, HDMI, eDP) conectadas à GPU.
13. `Dispatch_QueryChildStatus`: Checa o status de presença (Hot-Plug Detect - HPD) de um conector específico.
14. `Dispatch_QueryDeviceDescriptor`: Lê o EDID do monitor através de transações de I2C/DDC sobre as linhas de barramento de display.
15. `Dispatch_GetNodeMetadata`: Retorna metadados para os nós do Task Manager (3D Engine, Copy Engine, Video Decode, Video Encode).
16. `Dispatch_QueryFeatureSupport`: Consulta recursos opcionais do WDDM (ex: DirectFlip, Hardware-Accelerated GPU Scheduling - HAGS).
17. `Dispatch_QueryFeatureInterface`: Fornece ponteiros para interfaces de recursos dinâmicos.
18. `Dispatch_FeatureInterfaceReference`: Controle de vida útil (AddRef) de interfaces.
19. `Dispatch_FeatureInterfaceDereference`: Liberação (Release) de interfaces.

### 3.3 Domínio 3: Dispositivos, Contextos e Filas de Hardware (12 chamadas)
20. `Dispatch_CreateDevice`: Cria uma instância lógica de dispositivo DirectX para um processo de usuário.
21. `Dispatch_DestroyDevice`: Destrói o contexto de dispositivo lógico.
22. `Dispatch_CreateContext`: Cria um contexto de execução de GPU (3D, Compute ou Copy).
23. `Dispatch_DestroyContext`: Destrói o contexto de execução.
24. `Dispatch_SuspendContext`: Suspende temporariamente a execução de um contexto durante preempção ou troca de tarefas de prioridade máxima.
25. `Dispatch_ResumeContext`: Restaura o contexto suspenso.
26. `Dispatch_SetContextSchedulingProperties`: Configura a fatia de tempo e prioridade (tempo real, normal, baixa) no escalonador de hardware.
27. `Dispatch_SetupPriorityBands`: Configura faixas de prioridade para balanceamento de carga entre renderização e tarefas de computação assíncrona.
28. `Dispatch_CreateHwQueue`: **(WDDM 2.4+)** Instancia uma fila de hardware direta gerenciada pelo KMD.
29. `Dispatch_DestroyHwQueue`: Destrói a fila de hardware.
30. `Dispatch_SubmitCommandToHwQueue`: Submete pacotes de DMA diretamente para a fila de hardware sem passar pelo escalonador central de software.
31. `Dispatch_PresentToHwQueue`: Submete operações de apresentação sincronizadas diretamente na fila.

### 3.4 Domínio 4: Gerenciamento de Memória, Alocações e GPUVA (17 chamadas)
32. `Dispatch_CreateAllocation`: Recebe pedidos de criação de recursos de memória da UMD. Preenche `DXGK_ALLOCATIONINFO`, atribuindo os requisitos de segmento (Segmento 1 para superfícies de renderização primária/scanout; Segmento 2 para buffers de vértices e texturas gerais).
33. `Dispatch_DestroyAllocation`: Desfaz alocações e notifica o VidMm para desalocar as páginas físicas.
34. `Dispatch_OpenAllocation`: Abre um recurso compartilhado entre dois processos distintos (ex: composição DWM).
35. `Dispatch_CloseAllocation`: Fecha o identificador do processo para o recurso compartilhado.
36. `Dispatch_DescribeAllocation`: Descreve propriedades da alocação (pitch, dimensões, formato) para o VidMm.
37. `Dispatch_GetStandardAllocationDriverData`: Fornece dados para alocações padronizadas pelo sistema (superfícies de tela, staging buffers).
38. `Dispatch_ValidateUpdateAllocationProperty`: Valida alterações de propriedades de alocações existentes.
39. `Dispatch_SetAllocationBackingStore`: Altera a área de apoio (backing store) de alocações virtuais.
40. `Dispatch_CreateProcess`: **(WDDM 2.0+)** Inicializa o espaço de endereçamento virtual da GPU (GPUVA) para um novo processo, instanciando o diretório de páginas raiz.
41. `Dispatch_DestroyProcess`: Destrói a árvore de tabelas de páginas do processo.
42. `Dispatch_SetVirtualMachineData`: Configura o isolamento de VM para processos em contêineres ou hipervisores.
43. `Dispatch_GetRootPageTableSize`: Retorna a cota de memória necessária para o diretório raiz de tabelas de páginas.
44. `Dispatch_SetRootPageTable`: Define o endereço físico base do Page Directory da GPU no MMHUB para o processo ativo.
45. `Dispatch_MapCpuHostAperture`: Mapeia faixas de memória do sistema na Abertura do Host da CPU (`ConfigureHart`).
46. `Dispatch_UnMapCpuHostAperture`: Desfaz o mapeamento da abertura.
47. `Dispatch_BuildPagingBuffer`: **(O motor crítico de movimentação de memória!)** Executa cópia, preenchimento e atualização de tabelas de página via SDMA (detalhado na Seção 4).
48. `Dispatch_AcquireSwizzlingRange` / `Dispatch_ReleaseSwizzlingRange`: Legado para abertura linear de texturas swizzled (remanescentes de compatibilidade).

### 3.5 Domínio 5: Submissão de Comandos e Execução de Hardware (13 chamadas)
49. `Dispatch_SubmitCommand`: Envio tradicional de comandos de DMA pré-WDDM 2.0 (mantido para compatibilidade).
50. `Dispatch_SubmitCommandVirtual`: **(WDDM 2.0+)** Principal ponto de submissão do Windows. Recebe o buffer de comandos construído pela UMD sob o espaço GPUVA do processo, programa fences de sincronização e toca a doorbell do CP (Command Processor).
51. `Dispatch_PreemptCommand`: Solicita preempção de hardware imediata de um comando em voo caso uma tarefa de prioridade mais alta seja enfileirada.
52. `Dispatch_Patch`: Aplica correções de endereços físicos em buffers de comando quando alocações são realocadas pelo VidMm.
53. `Dispatch_QueryCurrentFence`: Lê o último fence de hardware concluído pelo motor para confirmar aposentadoria de comandos.
54. `Dispatch_SignalMonitoredFence`: Emite sinalização em fences monitorados da CPU/GPU usando escritas atômicas de 64 bits.
55. `Dispatch_Render`: Processamento de renderização em modo kernel (quando necessário).
56. `Dispatch_RenderKm`: Renderização kernel-mode para chamadas D3D internas.
57. `Dispatch_RenderGdi`: Aceleração de operações primitivas do subsistema GDI (BitBlt, ColorFill).
58. `Dispatch_Present`: Operação tradicional de apresentação de frame para o display.
59. `Dispatch_SetSchedulingLogBuffer`: Configura o buffer circular de log do escalonador de hardware.
60. `Dispatch_FormatHistoryBuffer`: Formata logs de histórico de execução para diagnósticos do ETW (Event Tracing for Windows).
61. `Dispatch_CalibrateGpuClock`: Calibra o timestamp da GPU contra o contador de performance da CPU (`QueryPerformanceCounter`) para sincronização precisa de áudio/vídeo.

### 3.6 Domínio 6: Interrupções e Sincronização (7 chamadas)
62. `Dispatch_ControlInterrupt`: Liga/desliga o envio de interrupções de VSYNC por fonte de display.
63. `Dispatch_ControlInterrupt2`: Extensão para controle avançado de interrupções de sincronização.
64. `Dispatch_ControlInterrupt3`: Controle de interrupções para Hardware Queues independentes.
65. `InterruptRoutine`: Rotina de Serviço de Interrupção (ISR) de nível de hardware. Lê o registrador de status do Interrupt Handler (IH), confirma o recebimento e enfileira a DPC.
66. `DpcRoutine`: Procedimento Chamado Diferido (DPC). Processa eventos de fences aposentados, flips completados e falhas de página.
67. `Dispatch_CreateCpuEvent`: Cria eventos de sincronização rastreados pela CPU.
68. `Dispatch_DestroyCpuEvent`: Destrói eventos de sincronização.

### 3.7 Domínio 7: TDR, Resets de Hardware e Recuperação OCA (11 chamadas)
69. `Dispatch_QueryEngineStatus`: Chamado pelo Watchdog do Windows para verificar se um motor de GPU específico está travado ou se ainda está progredindo.
70. `Dispatch_QueryDependentEngineGroup`: Identifica quais motores de hardware compartilham o mesmo domínio de reset (ex: se o motor 3D travar, o motor de Compute também deve ser resetado).
71. `Dispatch_ResetEngine`: **Reset de Nível 1 (Soft Reset)**. Reseta individualmente a fila ou os Compute Units que travaram através do KIQ (Kernel Interface Queue), **preservando o display (DCN) intacto sem piscar a tela**.
72. `Dispatch_ResetHwEngine`: Reset individual de uma Hardware Queue.
73. `Dispatch_ResumeHwEngine`: Retoma o agendamento após o reset da Hardware Queue.
74. `Dispatch_ResetFromTimeout`: **Reset de Nível 3 (Hard Reset / ASIC Reset)**. Disparado quando o soft reset falha. Invoca `TTL_ForceMode1Reset` para forçar o reinício da ASIC inteira através da SMU/PSP.
75. `Dispatch_RestartFromTimeout`: Reinicializa as estruturas de hardware após a conclusão do hard reset.
76. `Dispatch_CollectDbgInfo`: Coleta dados de telemetria e despejo de registradores para inclusão no minidump do Windows durante um TDR.
77. `Dispatch_CollectDiagnosticInfo`: Coleta diagnósticos detalhados para o Visualizador de Eventos.
78. `Dispatch_SaveMemoryForHotUpdate`: Salva o estado de memória da GPU durante atualizações a quente de driver.
79. `Dispatch_RestoreMemoryForHotUpdate`: Restaura o estado salvo após a substituição do binário do driver.

### 3.8 Domínio 8: Display, VidPn e Display Core Next (DCN) (26 chamadas)
80. `Dispatch_IsSupportedVidPn`: Valida se uma topologia de rede de vídeo (VidPn) proposta é viável pelo hardware de display.
81. `Dispatch_RecommendFunctionalVidPn`: Recomenda uma topologia funcional de display baseada nos monitores detectados.
82. `Dispatch_EnumVidPnCofuncModality`: Enumera modos de exibição cofuncionais (resoluções, taxas de atualização suportadas mutuamente).
83. `Dispatch_RecommendVidPnTopology`: Recomenda a associação entre caminhos de fonte e alvos.
84. `Dispatch_RecommendMonitorModes`: Sugere resoluções ideais para os monitores conectados.
85. `Dispatch_CommitVidPn`: Aplica definitivamente uma topologia de display ativa, configurando os links físicos e timings.
86. `Dispatch_SetTimingsFromVidPn`: Programa os geradores de temporização de hardware (OPTC) com os parâmetros de timing vertical e horizontal.
87. `Dispatch_UpdateActiveVidPnPresentPath`: Atualiza atributos de cor, rotação e escalonamento de um caminho de exibição ativo.
88. `Dispatch_SetVidPnSourceVisibility`: Liga ou desliga a saída de varredura visual de uma fonte (usado para economia de energia do monitor).
89. `Dispatch_SetVidPnSourceAddress`: Programa o endereço base de memória para a varredura primária no HUBP.
90. `Dispatch_CheckMultiPlaneOverlaySupport`: Verifica suporte a planos múltiplos sobrepostos (MPO).
91. `Dispatch_CheckMultiPlaneOverlaySupport2`: Extensão para MPO com suporte a HDR.
92. `Dispatch_CheckMultiPlaneOverlaySupport3`: Validação moderna de MPO3 com taxas de atualização independentes e conversão de espaço de cores por plano.
93. `Dispatch_SetVidPnSourceAddressWithMultiPlaneOverlay`: Aplica novos endereços a múltiplos planos de display.
94. `Dispatch_SetVidPnSourceAddressWithMultiPlaneOverlay2`: Aplicação atômica de múltiplos planos com suporte a metadados de cor.
95. `Dispatch_SetVidPnSourceAddressWithMultiPlaneOverlay3`: **(O método mestre de apresentação no Windows 10/11!)** Executa flips atômicos de planos de desktop e vídeo diretamente no hardware DCN sem cópias de SDMA.
96. `Dispatch_GetMultiplaneOverlayCaps`: Retorna os limites de hardware dos planos de overlay (resoluções máximas, formatos PixelFormat suportados).
97. `Dispatch_GetPostCompositionCaps`: Retorna capacidades de pós-processamento de cor do MPC e OPP.
98. `Dispatch_CancelFlips`: Cancela flips pendentes na fila em caso de encerramento do aplicativo.
99. `Dispatch_SetFlipQueueLogBuffer`: Configura o buffer de registro de eventos da fila de flips.
100. `Dispatch_UpdateFlipQueueLog`: Atualiza estatísticas de latência de flip para o DWM.
101. `Dispatch_SetInterruptTargetPresentId`: Sincroniza a geração de interrupções de VBLANK com um frame específico (Present ID).
102. `Dispatch_GetScanLine`: Retorna a linha de varredura vertical atual do monitor (usado para sincronismo fino e prevenção de tearing).
103. `Dispatch_QueryVidPnHWCapability`: Consulta acelerações de hardware da topologia de display.
104. `Dispatch_SetPointerPosition`: Atualiza a posição X/Y do cursor do mouse no hardware (sem redesenhar a tela).
105. `Dispatch_SetPointerShape`: Carrega o bitmap do cursor do mouse diretamente na memória de cursor do HUBP.

### 3.9 Domínio 9: Gerenciamento de Energia e ACPI (6 chamadas)
106. `Dispatch_SetPowerState`: Transições de estado de energia do dispositivo (D0 ativo, D3 sleep).
107. `Dispatch_SetPowerComponentFState`: Transições finas de energia por componente funcional (F0 ativo, F1 power-gated via SMU).
108. `Dispatch_PowerRuntimeControlRequest`: Handshake com o subsistema de Runtime Power Management (PEP) do Windows.
109. `Dispatch_NotifyAcpiEvent`: Recebe notificações de eventos ACPI da placa-mãe (ex: botão de energia, dock/undock, mudança de alimentação AC/bateria).
110. `Dispatch_SetStablePowerState`: Força clocks e voltagens fixas (utilizado por testes de desempenho e profiling).
111. `Dispatch_SetTrackedWorkloadPowerLevel`: Ajusta o perfil energético de acordo com a carga de trabalho reportada pelo sistema operacional.

### 3.10 Domínio 10: Virtualização de GPU e Hipervisor (10 chamadas)
112. `Dispatch_Virt_SubmitCommand`: Submissão em ambientes virtualizados (Hyper-V / guest).
113. `Dispatch_Virt_SubmitCommandVirtual`: Submissão virtualizada com GPUVA emulado.
114. `Dispatch_Virt_QueryCurrentFence`: Leitura de fence virtualizado.
115. `Dispatch_Virt_ResetFromTimeout`: Reset solicitado pelo hipervisor.
116. `Dispatch_Virt_SetStablePowerState`: Controle de clock virtualizado.
117. `Dispatch_Virt_PreemptCommand`: Preempção em ambiente convidado.
118. `Dispatch_Virt_BuildPagingBuffer`: Paginação delegada ao host.
119. `Dispatch_Virt_QueryDependentEngineGroup`: Topologia de motores virtualizados.
120. `Dispatch_Virt_QueryEngineStatus`: Status de motores no hipervisor.
121. `Dispatch_Virt_ResetEngine`: Reset de motor no ambiente virtualizado.

### 3.11 Domínio 11: Controle de Display, AUX, DDC/CI, MUX e Brilho (14 chamadas)
122. `Dispatch_I2CTransmitDataToDisplay`: Transmissão de comandos I2C/DDC/CI para configuração do monitor.
123. `Dispatch_I2CReceiveDataFromDisplay`: Leitura de respostas I2C/DDC/CI do monitor.
124. `Dispatch_DPAuxIoTransmission`: Transmissão nativa pelo canal AUX do DisplayPort (leitura de DPCD, controle de lanes).
125. `Dispatch_DPI2CIoTransmission`: Transações I2C tuneladas pelo canal AUX do DisplayPort.
126. `Dispatch_DPSBMTransmission`: Envio de mensagens Sideband MSG para topologias DisplayPort Multi-Stream Transport (MST).
127. `Dispatch_QueryDPCaps`: Consulta de capacidades do receptor DisplayPort.
128. `Dispatch_GetDPAddress`: Obtém o endereço de nó na topologia DisplayPort MST.
129. `Dispatch_GetPossibleBrightness`: Enumera os níveis de brilho suportados pelo painel integrado.
130. `Dispatch_SetBrightness`: Ajusta a intensidade da luz de fundo (backlight) via PWM do DCN.
131. `Dispatch_GetBrightness`: Lê o nível atual de brilho.
132. `Dispatch_GetBrightnessCaps`: Consulta limites de granularidade do backlight.
133. `Dispatch_SetBrightnessState`: Liga/desliga o painel de backlight.
134. `Dispatch_DisplayDetectControl`: Controle de detecção e polling de displays.
135. `Dispatch_QueryConnectionChange`: Notifica o sistema operacional sobre alterações na conexão física dos displays.

### 3.12 Domínio 12: Proteção de Conteúdo, OPM e Segurança (10 chamadas)
136. `Dispatch_OPMGetCertificateSize`: Retorna o tamanho do certificado HDCP da GPU.
137. `Dispatch_OPMGetCertificate`: Lê o certificado criptográfico do coprocessador de segurança (PSP).
138. `Dispatch_OPMCreateProtectedOutput`: Estabelece uma sessão de saída protegida (HDCP) com o display.
139. `Dispatch_OPMGetRandomNumber`: Gera números aleatórios para handshake de troca de chaves criptográficas.
140. `Dispatch_OPMSetSigningKeyAndSequenceNumbers`: Configura chaves simétricas e números de sequência para o fluxo protegido.
141. `Dispatch_OPMGetInformation`: Consulta status de integridade do link criptografado.
142. `Dispatch_OPMGetCOPPCompatibleInformation`: Compatibilidade com o legado Certified Output Protection Protocol (COPP).
143. `Dispatch_OPMConfigureProtectedOutput`: Habilita criptografia de hardware HDCP nos transmissores de display.
144. `Dispatch_OPMDestroyProtectedOutput`: Encerra a sessão criptografada.
145. `Dispatch_CreateProtectedSession`: Cria uma sessão protegida de decodificação e processamento DRM na GPU.

*(As 46 chamadas restantes consistem em extensões v3 de brilho, suporte a multiplexadores gráficos de notebooks `DisplayMux*`, hooks de calibração de paleta e stubs legados de overlay mantidos para total conformidade com o DDI do Windows).*

---

## 4. Anatomia Profunda da Paginação de Memória e Resolução da Trava Dura

A trava dura (**hard freeze**) na BC-250 sob paginação pesada foi diagnosticada a partir de três discrepâncias fundamentais entre a arquitetura de silício do RDNA1 e a implementação do driver.

### 4.1 Separação Física dos Canais de SDMA 5.0 (A1)
Na microarquitetura RDNA1, o controlador de SDMA 5.0 disponibiliza dois anéis de hardware completamente desacoplados por instância:

1. **`GFX_RB` (Anel de Cópia / DMA Geral):**
   - Registradores base:
     - `0x80`: `mmSDMA0_GFX_RB_CNTL`
     - `0x81`: `mmSDMA0_GFX_RB_BASE`
     - `0x82`: `mmSDMA0_GFX_RB_BASE_HI`
     - `0x83`: `mmSDMA0_GFX_RB_RPTR`
     - `0x88`: `mmSDMA0_GFX_RB_WPTR`
   - Doorbells: Registradores `0x100` e `0x101`.
2. **`PAGE_RB` (Anel Dedicado de Paginação):**
   - Registradores base:
     - `0xE0`: `mmSDMA0_PAGE_RB_CNTL`
     - `0xE1`: `mmSDMA0_PAGE_RB_BASE`
     - `0xE2`: `mmSDMA0_PAGE_RB_BASE_HI`
     - `0xE3`: `mmSDMA0_PAGE_RB_RPTR`
     - `0xE8`: `mmSDMA0_PAGE_RB_WPTR`
   - Doorbells: Registradores **`0x112`** e **`0x113`**.

```
    ┌────────────────────────────────────────────────────────┐
    │                 Subsistema do Windows                  │
    └───────────┬────────────────────────────────┬───────────┘
                │ D3D11 / Present Blit           │ VidMm Paging Buffer
                ▼                                ▼
       ┌─────────────────┐              ┌─────────────────┐
       │ SDMA0 - GFX_RB  │              │ SDMA0 - PAGE_RB │
       │  (Base MMIO 80) │              │  (Base MMIO E0) │
       └────────┬────────┘              └────────┬────────┘
                │ Doorbell 100/101               │ Doorbell 112/113
                ▼                                ▼
   [ Fila Geral de Cópias / DMA ]        [ Fila Dedicada de Paginação ]
```

* **Erro fatal na BC-250:** O driver atual submete os comandos do VidMm no `GFX_RB` (base `0x80`). Quando uma cópia de Present do scanout colide com uma operação de preenchimento ou migração de página, os dois comandos concorrem pelo mesmo anel, gerando condição de corrida no ponteiro de escrita (`WPTR`) e travando permanentemente o engine de DMA.
* **Correção Obrigatória:** O engine de paginação deve ser direcionado exclusivamente para a base **`0xE0` (`PAGE_RB`)** com suas doorbells dedicadas `0x112`/`0x113`.

### 4.2 Invalidação Atômica de TLB com Semáforo de Hardware (A2)
Quando o VidMm chama `BuildPagingBuffer` com `DXGK_OPERATION_UPDATE_PAGE_TABLE` ou `FLUSH_TLB`:
1. O driver AMD aloca dinamicamente um motor de invalidação livre (`nsHW::CS_Tlb::AllocateInvalidateEngine`, RVA `0x140559810`).
2. O driver monta uma transação de hardware contendo 3 escritas consecutivas e inseparáveis:
   - Registrador 1: Endereço baixo (`ENG*_ADDR_LO`, offset `+8`)
   - Registrador 2: Endereço alto (`ENG*_ADDR_HI`, offset `+0xc`)
   - Registrador 3: **Semáforo de exclusão mútua (`ENG*_SEM`, offset `+0`)**, seguido pela leitura de `ENG*_STATUS` (`+4`).
3. Em invalidações síncronas por MMIO (`vm_10_1_invalidate_tlbs_and_caches_per_ip`, RVA `0x14148e124`):
   - Escreve a requisição no registrador `0x3e` (`REQ_CNTL`).
   - Executa loop de polling no registrador **`0x3f` (`ACK_STATUS`)** aguardando `bit 0 == 1`.
   - Se o silício não responder após 50 iterações (`0x32`), dispara o erro crítico: `"Wait ENG ACK timeout!"` (Offset `0xa7f9bc`).
4. **Barreiras de Ordem de Memória:**
   O KMD emite explicitamente instruções **`lfence`** antes (RVA `0x14148e2ec`) e depois (RVA `0x14148e305`) do acesso para garantir que o pipeline fora-de-ordem dos núcleos Zen da APU não leia descritores desatualizados antes do hardware completar a descarga da TLB.

### 4.3 Domínio de Sistema Plano (Flat Domain) em RDNA1 (A4)
No código fonte original da AMD (`C:\constructicon\builds\gfx\one\24.30\drivers\swip\gvm\src\vm\10\sys_domain_10_1.c`):
> `"GVM doesn't support multiple level page table in system domain!"`

* **Consequência Arquitetural (P2):** O hardware do RDNA1 **proíbe terminantemente** árvores de múltiplos níveis para a memória física do sistema (GART/RAM). O mapeamento da memória do sistema deve utilizar **uma única tabela de páginas primária e plana** (`vm_10_1_create_primary_page_table`). Configurar árvores de 2 ou 4 níveis no domínio de sistema corrompe o MMHUB e gera congelamento imediato sob paginação.

### 4.4 Declaração de Segmentos ao VidMm para a APU BC-250 (A5)
O driver formaliza a divisão de memória perante o VidMm:
1. **Segmento 1 (VRAM Local Carve-out):**
   - Classe: `IUmaCarveoutFactory`
   - Tamanho: 256 MB (tamanho fixo determinado pelo firmware UEFI da BC-250)
   - Flags WDDM: `CpuVisible = 1`, `Aperture = 0`
   - Uso: Framebuffer primário de scanout e superfícies que exigem baixa latência de varredura.
2. **Segmento 2 (Memória do Sistema / Abertura de Host):**
   - Classe: `CS_OsSegmentsMgr` e `CS_OsCpuHostAperture::ConfigureHart`
   - Flags WDDM: `CpuVisible = 0`, `Aperture = 1`
   - Endereçamento de PTE: As PTEs deste segmento recebem os **Endereços Físicos do Sistema reais (`SysPA`)**, validados dentro da faixa configurada por `ConfigureHart`.

### 4.5 Desativação Obrigatória de Power Gating via Registro INF (A6)
A perda de ACK da TLB ocorre com frequência quando o subsistema de gerenciamento de energia desliga blocos de barramento prematuramente. No pacote NimeZ / AMD oficial para APUs, as seguintes chaves são mandatárias:
```ini
; Configuração Crítica de Estabilidade em APUs AMD
HKR,,SMU_DisableMmhubPowerGating,%REG_DWORD%,1        ; Impede que o MMHUB durma e perca ACKs de TLB
HKR,,DisableDynamicGfxMGPowerGating,%REG_DWORD%,1   ; Desativa Medium Grain Power Gating dinâmico
HKR,,PP_SclkDeepSleepDisable,%REG_DWORD%,1          ; Desativa sono profundo do shader clock
HKR,,KMD_EnablePageMigration,%REG_DWORD%,0          ; Força serialização síncrona pelo VidMm
HKR,,Gmx_AdvancedVmPagingMode,%REG_DWORD%,0         ; Desabilita modos especulativos instáveis de paginação
```

---

## 5. Arquitetura e Implementação do DCN Completo para RDNA1

A transição da renderização para o display sem intervenção de SDMA é o pilar definitivo para a estabilidade do sistema.

### 5.1 O Pipeline de Hardware DCN 2.0 / 2.1
No RDNA1, o subsistema de display é composto por blocos em cascata que realizam leitura, pós-processamento, composição e sincronismo:

```
┌────────────────────────────────────────────────────────┐
│             HUBP (Display Hub Pipe)                    │
│ • Descompactação DCC                                   │
│ • Endereço Base da Superfície (Luma/Chroma)           │
│ • Conversão de Formatos e Tiling                       │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│             DPP (Display Pipe and Plane)               │
│ • Input Gamma (1D LUT)                                 │
│ • Scaler (SCL) e Espaço de Cores (CSC)                 │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│             MPC (Multiple Pipe Combiner)               │
│ • Composição Multi-Plane Overlay (MPO3)                │
│ • Blending Alfa e Matriz de Cores 3D LUT               │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│             OPP (Output Pixel Processor)               │
│ • Formatação de Saída (8/10/12 bits)                   │
│ • Dithering e Clamping de Cores                        │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│             OPTC / OTG (Timing Generator)              │
│ • Geração de VSYNC, HSYNC, VBLANK                      │
│ • Trava de Registradores Sombra (OTG_GLOBAL_SYNC_LOCK) │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│             Transmissores Físicos (PHY / eDP)          │
│ • Saída física para monitor / dummy plug               │
└────────────────────────────────────────────────────────┘
```

### 5.2 Mapeamento de Registradores DCN 2.1 e Mecânica de Flip Atômico

A classe `optc2_lock` (RVA `0x14188e7e0`) e a rotina `optc2_unlock` (RVA `0x14188e980`) manipulam os registradores de sincronismo de hardware:

1. **Estrutura de Acesso a Registradores no AMD DC:**
   Conforme identificado na desmontagem em `amdkmdag.sys`, os objetos de hardware DCN utilizam tabelas de ponteiros:
   - `rcx + 0x20`: Tabela de offsets de registradores (`regs`)
   - `rcx + 0x28`: Tabela de deslocamentos de bits (`shift`)
   - `rcx + 0x30`: Tabela de máscaras de bits (`mask`)
   - `rcx + 0x10`: Ponteiro de contexto (`dc_context`)

2. **Sequência Exata do Flip Atômico no Windows (`MPO3 DirectFlip`):**
   Ao receber `Dispatch_SetVidPnSourceAddressWithMultiPlaneOverlay3`:
   - **Passo 1 — Travar Registradores Sombra:**
     O KMD invoca `optc2_lock` escrevendo `1` no registrador `OTG0_OTG_GLOBAL_SYNC_LOCK`:
     ```c
     // Impede que os blocos de varredura leiam registradores parcialmente atualizados
     REG_UPDATE(OTG_GLOBAL_SYNC_LOCK, OTG_GLOBAL_SYNC_LOCK, 1);
     ```
   - **Passo 2 — Programar o Endereço Base da Superfície no HUBP0:**
     O driver escreve os endereços físicos do novo frame gerado pela renderização:
     ```c
     // Registradores do HUBP0 (Display Hub Pipe 0)
     REG_SET(HUBPREQ0_DCSURF_SURFACE_ADDRESS_HIGH, 0, PRIMARY_SURFACE_ADDRESS_HIGH, (DWORD)(PhysicalAddress >> 32));
     REG_SET(HUBPREQ0_DCSURF_SURFACE_ADDRESS_LOW,  0, PRIMARY_SURFACE_ADDRESS_LOW,  (DWORD)(PhysicalAddress & 0xFFFFFFFF));
     ```
   - **Passo 3 — Atualizar Pitch e Formato de Cor:**
     O KMD atualiza os registradores `HUBPREQ0_DCSURF_SURFACE_PITCH` e `HUBP0_DCSURF_SURFACE_CONTROL`.
   - **Passo 4 — Destravar os Registradores Sombra:**
     O KMD invoca `optc2_unlock` escrevendo `0` no registrador `OTG0_OTG_GLOBAL_SYNC_LOCK`:
     ```c
     REG_UPDATE(OTG_GLOBAL_SYNC_LOCK, OTG_GLOBAL_SYNC_LOCK, 0);
     ```
   - **Passo 5 — Comutação Instantânea por Hardware:**
     No exato momento em que o feixe eletrônico atinge o período de **VBLANK**, a lógica interna do OPTC comuta atomicamente os registradores sombra para os registradores ativos. A imagem na tela é atualizada **sem transferir 1 único byte por SDMA e sem gastar ciclos de CPU em laços de espera**.

### 5.3 Assunção do Display do UEFI GOP sem Reinicializar o PHY Físico
Na plataforma BC-250, onde monitores físicos podem estar ausentes ou operar via dummy plug:
1. Durante a fase `Dispatch_StartDevice`, o driver lê o estado pré-programado pelo UEFI GOP:
   - Lê os divisores de frequência e clocks ativos de `dcn20_dccg_init` (RVA `0x141953f20`).
   - Lê a geometria de temporização ativa: `OTG0_OTG_H_TOTAL`, `OTG0_OTG_V_TOTAL`.
2. Em vez de desligar e tentar retreinar o link físico (o que causaria falha de handshake em conectores sem sinal), o KMD adota as dimensões do GOP e conecta o pipeline `HUBP0 -> DPP0 -> MPC0 -> OPP0 -> OPTC0` diretamente sobre o framebuffer herdado.

---

## 6. Plano de Ação Direta para o Driver da BC-250

Com base em todas as evidências forenses e desmontagens obtidas no driver oficial da AMD, as seguintes correções devem ser implementadas no código-fonte do driver da BC-250 para erradicar a trava dura e estabilizar o display:

1. **Migração do Motor de Paginação para `PAGE_RB` (Solução A1):**
   - Alterar a base de registradores da fila de paginação de `0x80` (`mmSDMA0_GFX_RB_CNTL`) para **`0xE0` (`mmSDMA0_PAGE_RB_CNTL`)**.
   - Mapear e utilizar as doorbells dedicadas **`0x112` e `0x113`**.
   - Desacoplar completamente a fila de cópias do Present da fila do VidMm.
2. **Inclusão do Semáforo de Invalidação de TLB (Solução A2):**
   - Na rotina de invalidação da TLB, emitir o pacote atômico de 3 operações incluindo a escrita mandatória do semáforo `VM_INVALIDATE_ENG*_SEM` antes de consultar o `ACK_STATUS` no registrador `0x3f`.
   - Adicionar barreiras `_mm_lfence()` antes e após a manipulação de descritores de página.
3. **Imposição de Tabela Plana para a Memória do Sistema (Solução A4):**
   - Garantir que a alocação de páginas do Segmento 2 (RAM do sistema) utilize tabela de páginas primária de nível único (`flat domain`).
4. **Configuração de Registro INF (Solução A6):**
   - Aplicar formalmente no arquivo INF do driver:
     `SMU_DisableMmhubPowerGating = 1`
     `DisableDynamicGfxMGPowerGating = 1`
     `PP_SclkDeepSleepDisable = 1`
     `KMD_EnablePageMigration = 0`
5. **Transição do Scanout para Flip no HUBP0 (Fase P9):**
   - Desativar a cópia periódica do Present por SDMA no scanout herdado.
   - Implementar o atendimento de `DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3` escrevendo o endereço da alocação diretamente em `HUBPREQ0_DCSURF_SURFACE_ADDRESS` protegido pela trava `OTG0_OTG_GLOBAL_SYNC_LOCK`.
