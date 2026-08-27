# ROADMAP OFICIAL: MIGRAÇÃO WDDM 2.0+ RENDER-ONLY (AMD BC-250)

> **Documento de Arquitetura & Cronograma de Desenvolvimento**  
> **Hardware:** AMD BC-250 APU (Cyan Skillfish / Navi12 Lite / GFX1013 / Oberon - 1536 Shaders / 16 GB GDDR6)  
> **Objetivo Principal:** Eliminar gargalos de IOCTLs de escape e habilitar o ecossistema oficial **Mesa RADV + Compilador ACO** para inferência de LLMs em silício real.

---

## 1. Visão Geral da Arquitetura WDDM 2.0+

```
+-----------------------------------------------------------------------------------+
|                        Windows Desktop / DWM (Monitor Ativo)                      |
|                  (Scanout herdado do UEFI GOP / Display Estável)                  |
+-----------------------------------------------------------------------------------+

+-----------------------------------------------------------------------------------+
|                         Aplicação de IA / LLM (llama.cpp)                         |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                 Mesa RADV UMD (vulkan_radeon.dll + Compilador ACO)                |
|           - Suporte nativo a SPIR-V -> ISA GFX1013 (Navi12 Lite)                  |
|           - Indirect Buffers (IBs) e Monitored Fences de 64 bits                  |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|             DirectX Graphics Kernel (dxgkrnl.sys - VidMm & VidSch)                |
|           - GPU Virtual Addressing (GPUVM de 48 bits)                             |
|           - Gerenciamento de 16 GB GDDR6 Local + Paginação                        |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|               bc250wddm.sys (WDDM 2.0+ Render Miniport Nativo)                    |
|           - DxgkDdiSubmitCommandVirtual (Disparo direto no Ring 0)                |
|           - DxgkDdiBuildPagingBuffer (UpdatePageTable nas PTEs da GART)           |
|           - SMU 1500 MHz | PSP Mailbox | CP PM4 | SDMA0                           |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                     Hardware AMD BC-250 (1536 Shaders / 1500 MHz)                 |
+-----------------------------------------------------------------------------------+
```

---

## 2. Fases de Implementação

### 📌 FASE 1: Driver Entry & Adaptação WDDM 2.0+ (`DxgkInitialize`)
- [ ] Atualizar assinatura e DDI table para `DXGKDDI_INTERFACE_VERSION_WDDM2_0`.
- [ ] Implementar `DxgkDdiAddDevice` e `DxgkDdiStartDevice` para Miniport Render-Only.
- [ ] Configurar `DxgkDdiQueryAdapterInfo`:
  - `DXGKQAITYPE_DRIVERCAPS` (GPUVM, Monitored Fences, 64-bit Virtual Contexts).
  - `DXGKQAITYPE_QUERYSEGMENT3` (Segmento 0: 16 GB GDDR6 Local; Segmento 1: Aperture de Sistema).
  - `DXGKQAITYPE_NUMNODES` & `DXGKQAITYPE_NODEMETADATA` (Node 0: 3D/Compute, Node 1: SDMA Copy).
  - `DXGKQAITYPE_PHYSICALADAPTERDEVICEIDS` (Vendor `0x1002`, Device `0x13FE`).
  - `DXGKQAITYPE_UMD_PRIVATE_DATA` (Suporte às structs `basic_properties` e `gpu_properties` do Mesa RADV).
- [ ] Atualizar `wddm.inf` com IDs PnP e catálogo assinado.

### 📌 FASE 2: Gerenciador de Memória Virtual da GPU (VidMm GPUVM)
- [ ] Implementar `DxgkDdiCreateAllocation` e `DxgkDdiDestroyAllocation`.
- [ ] Implementar `DxgkDdiReserveGpuVirtualAddressRange` e `DxgkDdiFreeGpuVirtualAddressRange`.
- [ ] Implementar `DxgkDdiMapGpuVirtualAddressRange` e `DxgkDdiUnmapGpuVirtualAddressRange`.
- [ ] Implementar `DxgkDdiBuildPagingBuffer`:
  - `DXGK_OPERATION_UPDATE_PAGE_TABLE`: Tradução de endereços virtuais de 48 bits para PTEs físicas do GCVM L2 MMU.
  - `DXGK_OPERATION_TRANSFER`: Paginação acelerada por hardware via engine SDMA0.
  - `DXGK_OPERATION_FILL`: Preenchimento de páginas via `SDMA_OP_CONST_FILL`.
  - `DXGK_OPERATION_FLUSH_TLB`: Invalidação de TLB com confirmação de hardware (`BcGcvmTlbFlush`).

### 3. FASE 3: Agendador de GPU & Submissão Virtual (VidSch & Indirect Buffers)
- [ ] Implementar `DxgkDdiCreateProcess` e `DxgkDdiDestroyProcess` (Estruturas de contexto de processo).
- [ ] Implementar `DxgkDdiCreateContextVirtual` e `DxgkDdiDestroyContext`.
- [ ] Implementar `DxgkDdiCreateHwQueue` e `DxgkDdiDestroyHwQueue`.
- [ ] Implementar `DxgkDdiSubmitCommandVirtual`:
  - Recebimento de Indirect Buffers (IBs) do Mesa RADV contendo sequências PM4 de computação.
  - Disparo no **CP PM4 Ring 0** (`A_CP_RB0_WPTR`) sem cópias manuais de memória.
- [ ] Implementar `DxgkDdiPatch` para resolução de endereços virtuais dinâmicos.

### 📌 FASE 4: Sincronização & Monitored Fences de 64 bits
- [ ] Implementar submissão de Monitored Fences gravando valor de 64 bits na memória monitorada (`DxgkCbSignalEvent`).
- [ ] Implementar `DxgkDdiInterruptRoutine` e `DxgkDdiDpcRoutine` para sinalização assíncrona.
- [ ] Implementar `DxgkDdiResetEngine` e `DxgkDdiRestartFromTimeout` para recuperação rápida de TDR pelo Windows.

### 📌 FASE 5: Integração com Mesa RADV (`vulkan_radeon.dll`) & Compilador ACO
- [ ] Configurar o manifesto JSON oficial `package/vulkan_radeon.json`.
- [ ] Validar a negociação WDDM 2.0+ pelo Winsys [radv_wddm2_winsys.c](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.c).
- [ ] Validar o reconhecimento da família `CHIP_CYAN_SKILLFISH` (GFX1013 / Navi12 Lite).
- [ ] Executar `vulkaninfo.exe` confirmando Vulkan 1.2/1.3 e compilação de shaders SPIR-V com o backend **ACO**.

### 📌 FASE 6: Homologação LLM Real (llama.cpp)
- [ ] Executar inferência real com `llama-cli.exe -m smollm2-135m-instruct-q4_k_m.gguf -p "A capital da Franca e" -ngl 33`.
- [ ] Verificar geração de tokens 100% acelerada pela GPU BC-250 (1536 Shaders).
- [ ] Comparar logits e precisão numérica contra a CPU.
- [ ] Executar teste de estresse contínuo por 8h e 24h com monitoramento térmico pela SMU.

---

## 3. Matriz de Entregáveis e Testes

| Fase | Componente Principal | Teste de Homologação | Critério de Sucesso |
| :--- | :--- | :--- | :--- |
| **Fase 1** | KMD WDDM 2.0+ Start | `test_wddm2_adapter_info.exe` | Adapter reconhecido pelo dxgkrnl com 2 nós (Compute + SDMA) e 16 GB GDDR6. |
| **Fase 2** | VidMm GPUVM | `test_wddm2_gpuvm_paging.exe` | Alocação e mapeamento de páginas virtuais de 48 bits na GART L2 sem falha. |
| **Fase 3** | VidSch Submissão Virtual | `test_wddm2_submit_ib.exe` | Indirect Buffers executados no anel PM4 com avanço de RPTR e fence de 64 bits. |
| **Fase 4** | Monitored Fences & TDR | `test_wddm2_fences_tdr.exe` | Fences sinalizadas pelo hardware sem polling de CPU; TDR recuperável. |
| **Fase 5** | Mesa RADV ICD + ACO | `vulkaninfo.exe` / `vkmark` | ICD oficial Khronos aprovado com compilação de SPIR-V para ISA nativo. |
| **Fase 6** | llama.cpp Inferência | `llama-cli.exe` | Inferência de LLM com velocidade > CPU, logits corretos e zero crashes. |
