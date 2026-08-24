# ROADMAP DE DESENVOLVIMENTO: DRIVER NATIVO AMD BC-250 (WDDM2 + ACO)

## Objetivo Central
Construir e validar um driver nativo de kernel (`bc250wddm.sys`) e runtime Vulkan (`vulkan_radeon.dll`) para a **AMD BC-250 (Cyan Skillfish / Navi12 Lite / 16 GB GDDR6)** com a mesma estabilidade, controle de hardware e performance do driver Linux `amdgpu.ko` + Mesa RADV.

---

## Arquitetura de Comunicação e Execução

```
[ Aplicação: LM Studio / llama.cpp / Jogos 3D / MemTest ]
                          │
                          ▼ (Vulkan API 1.3 / 1.4)
[ UMD: vulkan_radeon.dll (Mesa RADV com Compilador ACO) ]
  ├── Compilador ACO: Transforma Shaders SPIR-V em Instruções RDNA GFX10.1
  ├── Gerador PM4: Constrói Pacotes de Comando (IT_DISPATCH_DIRECT, SET_SH_REG)
  └── Winsys WDDM2: Despacha via D3DKMTEscape / Ring Buffer
                          │
                          ▼ (D3DKMTEscape IOCTL)
[ KMD: bc250wddm.sys (Driver de Kernel de Alta Performance) ]
  ├── SMU Manager: Controle de Frequência (1500 MHz), Ativação de 12 WGPs (24 CUs)
  ├── PSP Security Processor: Carregamento Autenticado das 7 Firmwares (SDMA, RLC, CE, PFP, ME, MEC, MEC2)
  ├── GCVM L2 MMU: Mapeamento de 16 GB GDDR6 e Aperture de Sistema
  ├── CP PM4 Ring 0: Anel Circular de Comandos na VRAM (4 KB com RPTR/WPTR Handshake)
  └── SDMA0 Engine: Motor de Cópia DMA em Hardware (> 10 GB/s)
                          │
                          ▼ (PCIe MMIO & VRAM Direct)
[ Hardware: AMD BC-250 Silicon (1536 Shaders / 24 CUs / 16 GB GDDR6) ]
```

---

## As 6 Fases de Desenvolvimento e Homologação

### 🟢 FASE 1: Ativação do Bringup de Hardware e Validação do Ring Buffer
- [ ] Ativar `SkipHwBringup = 0` e `EnableDangerousEscapes = 1` no `bc250wddm.sys`.
- [ ] Executar o pipeline de inicialização no boot:
  1. Handshake SMU (Test Message + Forçar 1500 MHz + Ativar 12 WGPs).
  2. Inicialização do PSP GPCOM Ring em Host RAM DMA.
  3. Carga das 7 firmwares via PSP (`SDMA0`, `RLC`, `CE`, `PFP`, `ME`, `MEC`, `MEC2`).
  4. Carveout do TMR de 4 MB na VRAM.
  5. Configuração da MMU GCVM L2 e contexto 0.
  6. Configuração do CP PM4 Ring 0 e SDMA0 Ring na VRAM.
- [ ] **Teste de Validação:** `test_phase1_hardware_bringup.exe`:
  - Valida telemetria de todas as 7 firmwares autenticadas com status `0x00000000`.
  - Envia pacotes `PKT3_NOP` para o anel CP e valida avanço de `RPTR == WPTR`.
  - Executa escrita de memória em hardware via `PKT3_WRITE_DATA`.
  - Executa cópia DMA de 4 MB via motor SDMA0 e valida hash bit-a-bit.

---

### 🟢 FASE 2: Conexão do Mesa RADV Winsys ao Escape Bridge do Kernel
- [ ] Em `radv_wddm2_cs.c`, conectar a função `radv_wddm2_cs_submit` ao comando `BC250_ESCAPE_CMD_SUBMIT_PM4`.
- [ ] Empacotar os Command Buffers gerados pelo compilador ACO diretamente nas Indirect Buffers (IBs) do Ring 0.
- [ ] Implementar sincronização de cercas (Fences) baseada no `RPTR` e `FenceSequence` do hardware.
- [ ] **Teste de Validação:** `test_phase2_radv_escape.exe`:
  - Envio de command buffers Vulkan reais via API Khronos e confirmação de execução pelo Command Processor físico.

---

### 🟢 FASE 3: Gerenciamento Direto de VRAM (BAR0 de 16 GB) no RADV
- [ ] Em `radv_wddm2_bo.c`, implementar alocador de páginas contíguas na faixa física `0xF400000000`.
- [ ] Configurar alinhamento em páginas de 64 KB para proteção contra Page Faults na GCVM L2.
- [ ] Suportar ReBAR / Resizable BAR (Abertura direta de 16 GB para a CPU).
- [ ] **Teste de Validação:** `test_phase3_vram_allocator.exe`:
  - Alocação e gravação paralela de buffers de 1 GB, 2 GB, 4 GB e 8 GB com medição de largura de banda e integridade de dados.

---

### 🟢 FASE 4: Execução de Shaders de Multiplicação Matricial (GEMM) nos 1536 Shaders
- [ ] Escrever e compilar Compute Shader GLSL de multiplicação matricial $C = A \times B$ em SPIR-V.
- [ ] Compilar via ACO para microcódigo GFX10.1 e despachar nos 12 WGPs (24 CUs).
- [ ] Medir throughput em TFLOPS FP32 e FP16 no silício real.
- [ ] **Teste de Validação:** `test_phase4_gemm_benchmark.exe`:
  - Execução de tensores de matrizes $4096 \times 4096$ e validação matemática de resultado bit-a-bit contra a CPU.

---

### 🟢 FASE 5: Inferência Completa de Inteligência Artificial com `llama.cpp` (Vulkan)
- [ ] Vincular o backend `ggml-vulkan.dll` à `vulkan_radeon.dll`.
- [ ] Carregar modelos GGUF (SmolLM2, Mistral 7B, Llama 3 8B, Qwen 2.5 7B) com offload total de camadas (`-ngl 99`).
- [ ] Monitorar a ocupação do motor gráfico via telemetria do `GRBM_STATUS` durante a geração de tokens.
- [ ] **Teste de Validação:** `test_phase5_llm_inference.bat`:
  - Medição de tempo de primeiro token (TTFT) e taxa contínua de tokens por segundo em hardware puro.

---

### 🟢 FASE 6: Renderização Gráfica 3D e Swapchain Fluida
- [ ] Implementar o canal de blit acelerado via SDMA0 no `vkQueuePresentKHR` para o framebuffer do monitor.
- [ ] Validar compatibilidade com `VulkanCube`, emuladores, demos 3D e softwares gráficos.
- [ ] **Teste de Validação:** Execução contínua de renderização 3D a 60+ FPS sem engasgos ou TDRs.
