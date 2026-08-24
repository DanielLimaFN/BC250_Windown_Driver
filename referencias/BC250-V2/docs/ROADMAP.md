# Roadmap Oficial de Desenvolvimento: Driver AMD BC-250 (Cyan Skillfish)

> **Projeto:** `BC250-V2`  
> **Status Atual:** Fases 1 e 2 homologadas em silício real. Fase 3 (SDMA0) concluída, Fase 4 (Compute) em validação.
> Fase 5 (múltiplas resoluções) foi despriorizada: o lado do driver está pronto e verificado, mas o Windows não expõe a lista de modos — ver `docs/FASE3_DIAGNOSTICO.md`.
> Suite de testes em `tests/` (`run-all.ps1`).

---

## Diagrama Visual de Evolução

```
+---------------------------------------------------------------------------------------+
|  FASE 1: Linha de Base Estável (Display-Only KMDOD + VSync 60Hz + GOP 2560x1080)      |
|  [STATUS: CONCLUÍDO E HOMOLOGADO EM SILÍCIO REAL]                                     |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
|  FASE 2: Otimização de Performance 2D & Apresentação (Blit SIMD + Dirty Rects)        |
|  - Streaming não-temporal para o BAR0 (SSE2 128-bit; AVX2 256-bit ainda pendente)     |
|  - Medido: ~444 MB/s para o BAR0, ganho de 2x com dirty rect                          |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
|  FASE 3: Aceleração de Cópia por Hardware (Motor SDMA0)   [CONCLUÍDO]                 |
|  - Inicialização segura do microcódigo SDMA0 sem interferir no GOP                    |
|  - Descarregamento de cópias do DWM da CPU para o motor DMA da GPU                    |
|  - Interface de benchmark de largura de banda PCIe                                    |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
|  FASE 4: Compute & GPGPU (D3D12 Compute / Vulkan ICD / OpenCL)  [EM VALIDAÇÃO]        |
|  - Anéis de compute MEC1/MEC2 (24 CUs / 1536 Shaders a 1500 MHz)                      |
|  - User-Mode Driver (UMD) mínimo para IA (llama.cpp, Ollama)                           |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
|  FASE 5: Múltiplas Resoluções & Modos de Display   [BLOQUEADO - DESPRIORIZADO]        |
|  - Lado do driver PRONTO: 12 modos publicados, EDID sintético aceito pelo Windows     |
|  - Lado do SO NÃO FUNCIONA: lista de modos do GDI vazia, BADMODE até no modo ativo    |
|  - Ver docs/FASE3_DIAGNOSTICO.md: 6 hipóteses testadas, causa ainda aberta            |
+---------------------------------------------------------------------------------------+
```

---

## Detalhamento Técnico das Fases

### FASE 1: Linha de Base Estável (Concluída)
- [x] Arquitetura Microsoft KMDOD pura (`DxgkInitializeDisplayOnlyDriver`).
- [x] Herança transparente do Framebuffer UEFI GOP (`DxgkCbAcquirePostDisplayOwnership`).
- [x] Mapeamento BAR0 64-bit seguro (`PAGE_WRITECOMBINE`).
- [x] Motor de VSync sintético a 60 Hz via temporizador DPC (`DxgkCbNotifyInterrupt`).
- [x] Conector permanente `HpdAwarenessAlwaysConnected`.
- [x] Apresentação funcional (`DxgkDdiPresentDisplayOnly`) com dirty rects.
- [x] Script de limpeza profunda de pacotes e serviços legados (`clean_all_drivers.ps1`).
- [x] Instalador automatizado com assinatura digital Authenticode (`install-wddm.ps1`).

---

### FASE 2: Otimização de Performance 2D e Apresentação (Próximo Passo)
- [ ] **2.1 Blit Vetorizado com Instruções AVX2:**
  - Substituir o `memcpy` escalar linha a linha por rotinas alinhadas com registradores YMM de 256 bits.
  - Utilizar instruções de streaming não-temporais (`_mm256_stream_si256` / `movntdq`) para gravar diretamente na VRAM do BAR0 sem poluir a memória cache L3 da CPU.
- [ ] **2.2 Bounding Box Inteligente de Dirty Rects:**
  - Fusão e otimização de múltiplos retângulos parciais enviados pelo DWM para minimizar transações no barramento PCI-Express.
- [ ] **2.3 Detecção de Frame Estático (Throttling):**
  - Descartar cópias quando os retângulos de origem não contiverem alterações visuais, economizando ciclos de barramento.

---

### FASE 3: Aceleração de Cópia por Hardware (Motor SDMA0) [CONCLUÍDO]
- [x] **3.1 Inicialização Segura do SDMA0 pós-Boot:**
  - Autenticação e provisionamento do firmware `cyan_skillfish2_sdma.bin` via PSP Mailbox em segundo plano após o display já estar completamente estabilizado.
- [x] **3.2 Blit Assíncrono via DMA:**
  - Motor SDMA0 inicializado e habilitado com sucesso (`Hw_Sdma_Status = 0`, `RB_CNTL = 0x80001015`).
- [x] **3.3 IOCTLs de Telemetria e Benchmark:**
  - Endpoints via `DxgkDdiEscape` implementados para cópias SDMA e medição de largura de banda na VRAM.

---

### FASE 4: Aceleração de Compute & GPGPU (AI, Direct3D 12 Compute, Vulkan) [IMPLEMENTADO / EM VALIDAÇÃO]
- [x] **4.1 Ativação dos Registradores e Pipeline de Compute:**
  - Configuração de `SPI_PG_MASK = 0xFFFFFFFF` (12 WGPs / 24 CUs / 1536 Shaders a 1500 MHz) e `CC_ARRAY_CONFIG = 0xFFE00000`.
  - Mapeamento e controle dos registradores `COMPUTE_PGM_LO/HI`, `COMPUTE_DIM_*`, `COMPUTE_NUM_THREAD_*`, `COMPUTE_USER_DATA_*` e `COMPUTE_DISPATCH_INITIATOR`.
- [x] **4.2 Interface User-Mode para Compute & VRAM Staging:**
  - Implementação dos escapes de kernel `BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS`, `BC250_ESCAPE_CMD_COMPUTE_DISPATCH`, `BC250_ESCAPE_CMD_VRAM_WRITE` e `BC250_ESCAPE_CMD_VRAM_READ`.
- [x] **4.3 Suite de Testes & Benchmark GPGPU:**
  - Criação da aplicação de teste `test_phase5_compute.c` / `test_phase5_compute.exe` para envio de shaders GFX10 RDNA1 e medição de throughput/latência.
- [ ] **4.4 User-Mode Driver (UMD) Vulkan/OpenCL para Cargas de IA (llama.cpp / Ollama):**
  - Camada de compatibilidade para frameworks de inferência.
### FASE 5: Múltiplas Resoluções e Modos de Display

> **BLOQUEADO EM SILÍCIO REAL.** O lado do driver está implementado e verificado por telemetria
> (13 modos de origem, 13 de monitor, pruning de cofuncionalidade correto, `Commit_Status=0`).
> Porém o Windows não expõe lista de modos utilizável: `EnumDisplaySettings` com filtro de monitor
> retorna 0 modos, e `ChangeDisplaySettingsEx(CDS_TEST)` retorna `DISP_CHANGE_BADMODE` para
> **todas** as resoluções, incluindo os 2560x1080 atualmente ativos. Falhar no modo ativo prova que
> o bloqueio é anterior e independente da Fase 3.
>
> **Causa mais provável identificada:** o driver não fornecia EDID algum, e o Windows
> registrou o monitor como `Generic Non-PnP Monitor` com `BAD_EDID`, caindo num mode set
> default mínimo que estrangula a lista independentemente do VidPN. Um EDID sintético foi
> implementado no fonte e validado aritmeticamente, mas **não compilado nem instalado**
> (edição concorrente com o SDMA0 da Fase 4). Ver `docs/FASE3_DIAGNOSTICO.md`.

- [x] **5.1 Co-funcionalidade VidPN Expandida:** (implementado e verificado no driver)
  - Adicionar suporte a resoluções adicionais nas tabelas de modo (`DdiEnumVidPnCofuncModality`):
    - 1920x1080 (Full HD)
    - 1600x900
    - 1280x720 (HD)
    - 2560x1080 (Ultrawide Nativo)
- [~] **5.2 Scaling e Centralização por Software:** (implementado; não verificável fim-a-fim até o bloqueio ser resolvido)
  - Quando o usuário selecionar uma resolução menor nas configurações do Windows, o driver realiza a centralização ou scaling no framebuffer nativo de 2560x1080.
- [ ] **5.3 Rotação de Tela:**
  - Suporte aos ângulos de rotação 90°, 180° e 270° via algoritmo de transposiçao de pixels no `PresentDisplayOnly`.

---

