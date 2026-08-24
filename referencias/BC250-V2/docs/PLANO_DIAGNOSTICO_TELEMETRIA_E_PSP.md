# Plano de Arquitetura: Sistema de Diagnóstico "Fail-Closed", Telemetria em Tempo Real (CLI/GUI) e Anel Assíncrono PSP (AMD BC-250)

> **Projeto:** AMD BC-250 Native Driver ([BC250-V2](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2))  
> **Objetivo:** Estabelecer um ecossistema de depuração de baixo nível com isolamento de falhas por chaves de registro, monitoramento em tempo real (CLI/GUI), ferramentas de injeção de registradores e anel assíncrono GPCOM do PSP para suportar o desenvolvimento e homologação completa da pilha Vulkan / Mesa RADV.

---

## 📑 Sumário Executivo

```
+-------------------------------------------------------------------------------------------------------+
|                                    ECOSSISTEMA DE DEPUGAÇÃO & CONTROLE                                 |
+-------------------------------------------------------------------------------------------------------+
|                                                                                                       |
|  1. REGISTRO FAIL-CLOSED  ──► Isolamento modular de blocos IP (Zero-BSOD / Zero-BlackScreen)         |
|  2. FERRAMENTAS DE BAIXO  ──► bc250-smn-tool (Clocks/Tensão/8 Cores) + bc250-psp-tool (Opcodes PSP)  |
|  3. APP TELEMETRIA (CLI/GUI) ─► BC250 Control Center: Rings, GPUVM, Shaders e VRAM em tempo real     |
|  4. ANEL ASSÍNCRONO PSP   ──► Implementação do anel GPCOM (0x58000) com fallback C2PMSG              |
|  5. INOVAÇÕES ADICIONAIS  ──► Fault Tracer de GPUVM, Injector de Shader ISA e Hot-Reset de Anéis      |
|                                                                                                       |
+-------------------------------------------------------------------------------------------------------+
```

---

## 1. 🛡️ Sistema de Chaves de Registro "Fail-Closed" para Depuração Modular

O driver de kernel (`bc250wddm.sys`) lerá parâmetros configuráveis sob:  
`HKLM\SYSTEM\CurrentControlSet\Services\bc250v2\Parameters`

Se qualquer nova funcionalidade ou bloco IP de hardware apresentar instabilidade em uma máquina de teste, o desenvolvedor poderá desativar **apenas** aquele bloco via comando simples no terminal (sem precisar recompilar o driver nem reinstalar o pacote).

### 📋 Matriz de Chaves de Registro Propostas

| Chave de Registro (DWORD) | Padrão | Descrição & Utilidade no Debug |
| :--- | :---: | :--- |
| **`HwInitMaxStep`** | `0` (Todos) | Limita o bringup do hardware ao passo *N* (0=Tudo, 1=SMU, 2=PSP, 3=TMR, 4=FwLoad, 5=GART, 6=CP Ring 0, 7=SDMA0). Permite busca binária de falhas de inicialização. |
| **`HwInitFirmware`** | `1` (Ativo) | `0` = Pula a carga dos 7 microcódigos via PSP (útil para isolar falhas de DMA na memória de instruções). |
| **`HwUnhaltCp`** | `1` (Ativo) | `0` = Mantém o Command Processor congelado (`A_CP_ME_CNTL = 0x15000150`) após a carga de firmware, isolando acessos indevidos de DMA. |
| **`HwInitGfxRing`** | `1` (Ativo) | `0` = Pula a ativação do anel CP PM4 Ring 0. |
| **`HwInitSdmaRing`** | `1` (Ativo) | `0` = Pula a ativação do motor de cópia DMA (SDMA0). |
| **`HwInitGart`** | `1` (Ativo) | `0` = Desativa a alocação dinâmica da GART e opera em modo identity direto de BAR0. |
| **`DisplayWritesEnabled`** | `0` (Protegido) | `0` = Bloqueia escritas diretas nos registradores DCN HUBPREQ/OTG, mantendo o scanout herdado do UEFI GOP 100% estável e evitando telas pretas (*black-screen*). |
| **`EventRingBufferEnabled`** | `1` (Ativo) | Habilita o buffer circular de telemetria no kernel (1024 eventos) para streaming em tempo real para a GUI/CLI. |
| **`EnableCpu8CoreUnlock`** | `0` (Opcional) | `1` = Envia automaticamente a mensagem SMU Q3 `0x98` no boot para desbloquear os 8 núcleos (16 threads) da CPU Zen2. |

---

## 2. 🧰 Ferramental de Engenharia Reversa e Baixo Nível

### 2.1 `bc250-smn-tool.exe` (Monitor e Injetor SMN / SMU)
Equivalente e aprimorado em relação ao `bar5-smn-test.exe` da comunidade:
* **Leitura/Escrita SMN:** Acesso a qualquer registrador interno de barramento da APU via BAR5 + `0x38` (Endereço SMN) e `0x3C` (Dados SMN).
* **Painel de P-States e Clocks:** Leitura em tempo real de `GFXCLK` (1500 MHz), `SOCCLK`, `MEMCLK`, `FCLK`, tensões `VDDGFX` e `VDDSOC` em mV.
* **Thermal Controller:** Monitor de sensores térmicos (Edge, Hotspot/Junction e SoC).
* **Governor de Frequência:** Permite setar perfis dinâmicos de clock (500 MHz até 2000 MHz) com a sequência segura de 7 passos.
* **CPU Core Unlock:** Disparo da mensagem SMU `0x98` para ativar os 8 núcleos Zen2 da APU.

### 2.2 `bc250-psp-tool.exe` (Injetor de Comandos PSP)
Ferramenta para explorar a segurança e os firmwares do Secure OS (SOS):
* **Injeção de Opcodes PSP:** Envio de comandos diretos via anel GPCOM (`0x05` Setup TMR, `0x06` Load IP FW, `0x0F` Get Attestation, `0x20` Load TOC, `0x21` Autoload RLC).
* **Dump de Atestação:** Validação das assinaturas criptográficas dos microcódigos em execução.

---

## 3. 🖥️ Dashboard de Telemetria e Monitoramento em Tempo Real (CLI + GUI)

Para acelerar a implementação da pilha Vulkan / Mesa RADV, propomos o desenvolvimento do **BC250 Control Center / Studio**:

```
+----------------------------------------------------------------------------------------------------+
|                                    BC250 CONTROL CENTER & STUDIO                                   |
+----------------------------------------------------------------------------------------------------+
|  GPU: AMD BC-250 (1536 Shaders / 24 CUs / 1500 MHz)          Driver: WDDM 1.0.821 (bc250wddm.sys)  |
|  Temp: 52°C Edge | 58°C Junction | VDDGFX: 850 mV           Memória: 16 GB GDDR6 (4 GB VRAM Carve) |
+----------------------------------------------------------------------------------------------------+
|                                                                                                    |
|  [ MOTORES DE HARDWARE (RINGS) ]                [ MAPA DE GDDR6 & APERTURAS (16 GB) ]              |
|  • CP PM4 Ring 0: [▓▓▓▓▓▓░░░░░░░░░░] 35%        • 0x00000000 (0-16MB)  : UEFI GOP Display Scanout  |
|    WPTR: 1024 | RPTR: 1024 | Idle: SIM          • 0x01000000 (16-240MB): Staging / Compute Buffers |
|  • SDMA0 Ring   : [▓▓░░░░░░░░░░░░░░] 12%        • 0x0EC00000 (236MB)   : GART L2 MMU Page Table    |
|    Throughput: 14.8 GB/s | Fences: OK           • 0x0F800000 (248MB)   : PSP TMR Secure Carveout   |
|  • PSP GPCOM Ring: [▓░░░░░░░░░░░░░░] 5%         • 0x0FE00000 (254MB)   : CP PM4 Ring 0 Buffer      |
|                                                                                                    |
|  [ SHADER ENGINE & GRBM TELEMETRY ]             [ EVENT STREAM & ESCAPE LOGGER (FILTRAVEL) ]       |
|  • GRBM Status: 0x00000000 (IDLE)               19:28:10.124 [SUBMIT_IB] Opcode 0x3F (25 DW) - OK |
|  • SPI (Shader Processor): 24/24 CUs ATIVAS     19:28:10.125 [FENCE_ACK] Value 0xAA55AA55 na VRAM  |
|  • CUs em Execução: 0 (Aguardando Dispatch)     19:28:10.130 [VK_SUBMIT] CommandBuffer 0x01200000  |
|                                                                                                    |
+----------------------------------------------------------------------------------------------------+
```

### Principais Telas e Recursos do App:
1. **Live Ring Engine Monitor:** Visualização em tempo real de `WPTR` e `RPTR` dos anéis CP Ring 0, SDMA0 e PSP com latência em microssegundos.
2. **GPU Memory Map Visualizer:** Mapa de blocos da GDDR6 em tempo real, mostrando alocações de Texturas, RenderTargets, Shaders, GART e TMR.
3. **Shader Pipeline Inspector:** Visualização dos 12 WGPs (24 CUs) e estado do SPI / GRBM para detectar gargalos ou shader hangs instantaneamente.
4. **Live Event Log Stream:** Filtro de eventos do driver (IOCTLs, D3DKMT Escapes, Submissões de Indirect Buffers e chamadas Vulkan).
5. **Dynamic Power & Frequency Profile:** Slider interativo para alternar entre os perfis do SMU (Eco 500MHz, Balanced 1175MHz, Gaming 1600MHz, OC 1800/2000MHz).

---

## 4. 🔄 Análise Técnica: Implementação do Anel Assíncrono GPCOM do PSP (`0x58000`)

### 4.1 Comparação entre C2PMSG Direto vs. Anel GPCOM

| Aspecto Técnico | C2PMSG Direto (Atual) | Anel GPCOM (`0x58000`) |
| :--- | :--- | :--- |
| **Mecanismo de Comunicação** | Mailbox de 3 registradores (`C2PMSG_35/36/81`). | Fila circular em RAM (`MmAllocateContiguousMemory`) de 64B por comando (`psp_gfx_rb_frame`). |
| **Sincronização** | Bloqueante (Spinlock na CPU esperando o registrador). | Assíncrona via Write Pointer (`C2PMSG_67`) e Fences de hardware na memória. |
| **Capacidade de Carga** | 1 comando por vez. | Múltiplos comandos enfileirados em lote (*batch*). |
| **Comandos Suportados** | Comandos básicos de carga de microcódigo. | Suporte completo a atestação criptográfica (`0x0F`), `SETUP_TMR` (`0x05`), `AUTOLOAD_RLC` (`0x21`). |
| **Impacto no Driver** | Simples e direto. | Extremamente robusto para drivers avançados com múltiplos contextos. |

### 4.2 Decisão Arquitetural: Estratégia Híbrida Inteligente
Podemos implementar o **Anel GPCOM na base `0x58000`** dentro de [wddm_hw.c](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/wddm_hw.c) como o motor primário de comunicação com o PSP, mantendo o **C2PMSG direto como fallback automático**.

```c
/* Exemplo de Inicialização do Anel GPCOM */
#define A_PSP_MP0_BASE                  0x58000u
#define A_PSP_C2PMSG_64                 (A_PSP_MP0_BASE + 0x200u) /* Ring Status / ACK */
#define A_PSP_C2PMSG_67                 (A_PSP_MP0_BASE + 0x20Cu) /* Ring WPTR Kick */
#define A_PSP_C2PMSG_69                 (A_PSP_MP0_BASE + 0x214u) /* Ring Address LO */
#define A_PSP_C2PMSG_70                 (A_PSP_MP0_BASE + 0x218u) /* Ring Address HI */
#define A_PSP_C2PMSG_71                 (A_PSP_MP0_BASE + 0x21Cu) /* Ring Size (4096) */
#define A_PSP_C2PMSG_81                 (A_PSP_MP0_BASE + 0x244u) /* SOS Boot Status */
```

---

## 5. 💡 Novas Ideias para Acelerar a Implementação do Vulkan

1. **GPUVM Page Fault Tracer (Detector de Erros de Memória da GPU):**
   - Registrar no log do kernel qualquer tentativa de shader ou Indirect Buffer ler endereços fora da System Aperture, gravando `FAULT_GPU_ADDR`, `FAULT_VMID` e `FAULT_STATUS`, eliminando o tempo de adivinhação quando um jogo travar.
2. **Direct ISA Shader Bytecode Injector:**
   - Ferramenta de linha de comando para compilar pequenos kernels ISA GFX10.1 (ex: GEMM FP32/FP16, Clear, Copy) diretamente em hexadecimal, injetar na VRAM e disparar a execução via Escape `0xBD07` sem precisar do pipeline completo de compilação SPIR-V.
3. **Soft-Reset de Motores & Watchdog Anti-TDR:**
   - Rotina no kernel que reseta individualmente o CP PM4 (`A_GRBM_SOFT_RESET`) e o SDMA0 sem resetar o chip inteiro nem derrubar o display do Windows caso um shader malformado entre em loop infinito.
4. **Vulkan Pipeline Cache Persistente em VRAM:**
   - Alocar uma área contígua na GDDR6 dedicada ao armazenamento de shaders compilados em binário de máquina ISA, permitindo carregamento instantâneo de pipelines Vulkan.

---

## 📅 Roteiro de Implementação Sugerido

* [ ] **Fase A (Curto Prazo):** Implementar as Chaves de Registro Fail-Closed em `wddm_driver.c` e `wddm_hw.c`.
* [ ] **Fase B (Curto Prazo):** Adicionar o Desbloqueio da CPU (6 $\rightarrow$ 8 Cores via SMU Msg `0x98`) no driver e na suíte de testes.
* [ ] **Fase C (Médio Prazo):** Criar a ferramenta CLI `bc250-smn-tool.exe` e o `bc250-psp-tool.exe`.
* [ ] **Fase D (Médio Prazo):** Integrar o Anel Assíncrono GPCOM do PSP (`0x58000`) como motor primário.
* [ ] **Fase E (Longo Prazo):** Desenvolver o Dashboard gráfico `BC250 Studio / Control Center` (Win32/C).
