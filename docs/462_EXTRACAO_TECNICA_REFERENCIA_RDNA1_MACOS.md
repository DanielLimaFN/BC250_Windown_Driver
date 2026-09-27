# 462 — Extração Técnica e Engenharia Reversa da Referência RDNA 1 do macOS

> **Data:** 25/09/2026  
> **Origem dos Binários:** `referencias/RDNA1_MACOS` (macOS 15.7 Sequoia Internal, Build `24G509`, Darwin/x86_64)  
> **Alvo no Silício:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)  
> **Objetivo:** Documentar e catalogar toda a extração técnica de registradores, estruturas, opcodes de SDMA 5.0, MQD, Display Core DCN 2.1, DisplayPort AUX/EDID e VCN 2.0/2.5 para orientar o avanço das prioridades P1 a P9 da [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md).

---

## 1. Tabela de Validação Cruzada: O que Já Sabíamos vs Confirmação no macOS

Esta tabela correlaciona os itens que já havíamos derivado ou implementado no nosso driver KMD/UMD Windows com a confirmação exata encontrada nos binários de produção da Apple/AMD para a arquitetura GFX10 (RDNA 1).

| Subsistema / Recurso | Implementação Atual no Nosso KMD | Binário macOS RDNA 1 (`AMDRadeonX6000`) | Status de Confirmação |
| :--- | :--- | :--- | :--- |
| **SDMA 5.0 Constant Fill** | `BC250_SDMA_OP_CONST_FILL (11) \| (2u << 30)` = **`0x8000000B`** | `AMDGFX10SDMAChannel::writeConstantFillCommand`: `movl $0x8000000b, (%rsi)` | **100% IDÊNTICO (Bit-Exact).** Confirma nosso construtor de pacotes de fill. |
| **SDMA 5.0 PTE/PDE Update** | `BC250_SDMA_OP_PTE_PDE (12)` = **`0x0000000C`** | `AMDGFX10SDMAChannel::writeWritePTEPDECommand`: `movl $0xc, (%rsi)` | **100% IDÊNTICO.** Confirma nosso opcode de atualização de tabelas de página via SDMA. |
| **SDMA Invalidação TLB** | Opcode 16 (`0x10` SRBM/GCR Write) | `AMDGFX10SDMAChannel::writeVMInvalidateCommand`: `movl $0x10, (%r12)` | **CONFIRMADO.** Valida o opcode base `0x10` com 4 dwords para TLB flush. |
| **MQD Header Magic (GFX10)** | Header de MQD de computação GFX10 | `AMDGFX10PM4Engine::fillComputeMQD`: `movl $0xc0310800, (%rsi)` | **CONFIRMADO.** Magic/control word canônica do MQD de RDNA 1 (`0xC0310800`). |
| **HQD PQ Control** | Registrador de controle do anel de computação | `AMDGFX10PM4Engine::fillComputeMQD`: `0x244(%rsi) = 0xd130860d \| (sz << 30)` | **CONFIRMADO.** Offset `0x244` do MQD bate exatamente com `CP_HQD_PQ_CONTROL`. |
| **RLC Safe Mode** | Handshake de modo seguro para trocar anéis | `AMDGFX10PM4Engine::enterRlcSafeMode` / `exitRlcSafeMode` | **CONFIRMADO.** Confirma que o microcontrolador RLC exige intertravamento explícito. |
| **Preempção PM4** | Notificação de `DMA_PREEMPTED` em fim de dreno | `AMDGFX10PM4GraphicsChannel::sendPreemptionFrame` | **CONFIRMADO.** Confirma frame dedicado de preempção enviado ao anel gráfico. |
| **EOP Fence (Release Mem)** | PM4 `RELEASE_MEM` com evento VGT e EOP | `AMDGFX10PM4GraphicsChannel::releaseMemCmdPacket` | **CONFIRMADO.** Estrutura padronizada de cerca atômica com flush de cache. |
| **Display Core (DC)** | Display v1 via GOP UEFI (modo fixo) | `AMDRadeonX6000Framebuffer`: DCN 2.1 nativo (`rn_clk_mgr.c`, `dcn21_resource.c`) | **DESBLOQUEIO PARA P9.** Código nativo para a APU Renoir/Cyan Skillfish. |
| **Leitura de EDID** | Sintetizado a 56,13 Hz (Item A2/A3) | `AMDSupport.kext`: `AuxChannelProxy::transferI2c` / DP AUX | **DESBLOQUEIO ITEM 77.** Sequência real de leitura I2C-over-AUX no DisplayPort. |
| **VCN (Aceleração Vídeo)** | Inexistente / Não implementado | VCN 2.0 / 2.5 (`VCN2DecChannel`, `VCN2EncRing`, `VCN2HWEngine`) | **NOVA CAPACIDADE.** Estrutura completa de filas e pacotes de decodificação/codificação. |

---

## 2. Subsistema VCN (Video Core Next 2.0 / 2.5)

A BC-250 (Cyan Skillfish 2 / GC 10.1.3) possui o bloco de aceleração de vídeo **VCN 2.0 / VCN 2.5** (a mesma geração do Navi 10 e Renoir). O binário [`AMDRadeonX6000.kext`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/RDNA1_MACOS/AMDRadeonX6000.kext) expõe 393 símbolos específicos de VCN, divididos claramente entre duas gerações:

### 2.1. Arquitetura de Filas VCN 2 vs VCN 3
* **RDNA 1 (Navi 10, Navi 12, Navi 14, BC-250):**  
  Implementa **filas separadas** para decodificação e codificação:
  * Canal de Decodificação: `VCN2DecChannel` (filas `VCN0Dec`, `VCN1Dec`).
  * Anéis de Codificação: `VCN2EncRing` e `VCN2EncLLQRing` (Low Latency Queue).
  * Engine de Hardware: `VCN2HWEngine`.
* **RDNA 2 (Navi 21, Navi 23):**  
  Implementa **Unified Queue (Fila Unificada)**:
  * `VCN3UnifiedChannel`, `VCN3UnifiedRing` e `VCN3HWEngine`.

### 2.2. Protocolo de Comandos e Pacotes de Decodificação (`VCN2DecChannel`)
Para submissão de trabalho de decodificação (H.264, HEVC/H.265, VP9, JPEG), o canal expõe as rotinas de framing:
```cpp
// Métodos de construção de pacotes no buffer de decodificação:
void initializeStart(void* buffer);            // Cabeçalho de início de quadro de decodificação
void initializeEnd(void* buffer);              // Fechamento de sessão de decodificação
void initializeFence(void* buffer);            // Emissão de cerca de conclusão de hardware
void initializeTrap(void* buffer);             // Emissão de interrupção TRAP (IV ring)
void generateNOPs(unsigned int count, void* buffer); // Preenchimento com padding NOP
uint32_t getOneDwordNOPCommand();              // Comando NOP unitário (1 DWORD)
uint32_t getIBAlignmentFactor();               // Fator de alinhamento obrigatório do Indirect Buffer
```

### 2.3. Controle do Anel de Codificação (`VCN2EncRing`)
O anel de codificação segue uma topologia muito similar à do SDMA/PM4:
* `programReadAndWritePointers(uint64_t)`: Programa os ponteiros de base de RPTR e WPTR na VRAM.
* `enableReadPointerWriteBack()` / `disableReadPointerWriteBack()`: Habilita writeback de RPTR em memória da GPU para evitar leituras MMIO na CPU.
* `writeTail()`: Atualiza o registrador de campainha (doorbell) ou MMIO para disparar a execução do codificador.

### 2.4. Propriedades e Chaves no `Info.plist`
No [`Info.plist`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/RDNA1_MACOS/AMDRadeonX6000.kext/Contents/Info.plist) de `AMDRadeonX6000`:
* `IOGVACodec`: `"AMDVCN2"` (identificador do codec de hardware para RDNA 1).
* `EnableSwVCNFWLoading`: `1` (carregamento de microcódigo VCN pelo driver, se a SMU autorizar).
* `EnableSwVCNUnifiedQueue`: `0` para RDNA 1 (`1` somente para RDNA 2).
* `EnableVCNFWLog`: `0` (desliga tracing de debug no firmware para evitar contenção).

---

## 3. Subsistema de Display: DCN 2.0 e DCN 2.1 (`AMDRadeonX6000Framebuffer`)

O arquivo [`AMDRadeonX6000Framebuffer.kext`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/RDNA1_MACOS/AMDRadeonX6000Framebuffer.kext) contém o **AMD Display Core (DC)** com suporte explícito à família **DCN 2.1** (APUs Renoir / Cyan Skillfish 2 / BC-250).

### 3.1. Arquivos C do Upstream AMD Preservados no Binário
O binário preserva os nomes dos arquivos C do driver da AMD, comprovando a herança arquitetural:
* `rn_clk_mgr.c` e `rn_clk_mgr_vbios_smu.c`: Gerenciador de clock do **Renoir (DCN 2.1)** integrado com a VBIOS/SMU.
* `dcn21_resource.c`: Descritor de recursos do DCN 2.1 (número de pipes, DPPs, Hubps, planos de overlay).
* `dcn21_link_encoder.c`: Codificador físico de enlace (PHY) do DisplayPort em DCN 2.1.
* `dcn20_hubbub.c`, `dcn20_hubp.c`, `dcn20_optc.c`, `dcn20_dccg.c`: Blocos fundamentais do pipeline de display.

### 3.2. Funções Críticas de DCN 2.1 Identificadas
1. **Tradução de Memória Virtual para Scanout (DCHVM):**
   * `dcn21_dchvm_init()` e `dcn20_init_vm_ctx()`:
     Configuram as aberturas do MMHUB de display para que o Hubp possa buscar linhas do framebuffer via GPUVA (em vez de scanout físico contíguo).
2. **Cálculo de Marca d'Água (Watermarks):**
   * `dcn21_calculate_wm(struct dc *, struct dc_state *, ...)`:
     Calcula as marcas d'água de latência de DRAM (urgente, p-state switch, stutters) em APUs onde a memória de vídeo é compartilhada (GDDR6 na BC-250).
3. **Validação de Largura de Banda de Ponto Flutuante:**
   * `dcn21_validate_bandwidth_fp()` e `dcn21_fast_validate_bw()`:
     Verifica se a taxa de pixel requerida pelo monitor cabe na banda de memória disponível sem provocar sub-alimentação de FIFO (*underflow*).
4. **Workaround de Silício de DCN 2.1:**
   * `DCN21_147_wa` e `dcn21_s0i3_golden_init_wa`:
     Rotinas de correção de erratas de hardware em inicialização e suspensão no DCN 2.1.

---

## 4. DisplayPort AUX, Link Training e Leitura de EDID (`AMDSupport.kext`)

Este módulo contém a solução para o **Item 77 da [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) (EDID real por AUX)**.

### 4.1. Arquitetura de Transações DP AUX / I2C
No [`AMDSupport.kext`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/RDNA1_MACOS/AMDSupport.kext), a classe `AuxChannelProxy` gerencia o canal auxiliar:
* `AuxChannelProxy::transferI2c(DPDeviceInfo*, bool isRead, IOI2CRequest*)`:
  Executa a transação I2C-over-AUX padronizada pelo VESA DisplayPort (endereço I2C `0x50` para bloco EDID base, `0x30` para extensões CEA/CTA-861).
* `AppleDisplayDescriptor`: Estrutura de análise que valida o cabeçalho `0x00 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF 0x00`, checksum de 8 bits e descritores detalhados de timing (`IODisplayTimingRangeV2`).

### 4.2. Máquina de Estados de Link Training
* `DisplayPortUtilities::AGDCEnableLinkTraining(AGDCDPPortConfig_t*)`:
  Coloca a PHY do DisplayPort em modo de treinamento.
* `DisplayPortUtilities::trainLink()` / `AtiDeviceControl::trainingAttempt(...)`:
  Executa a fase 1 (Clock Recovery — padrão de treino TPS1) e fase 2 (Channel Equalization / Symbol Lock — padrões TPS2/TPS3/TPS4).
* `AtiDeviceControl::trainingEnd(...)`:
  Finaliza o link training com o bitrate negociado (RBR 1.62 Gbps, HBR 2.7 Gbps ou HBR2 5.4 Gbps) e ativa o fluxo principal (*main link*).

---

## 5. Engenharia Reversa do SDMA 5.0 (`AMDRadeonX6000.kext`)

Desmontagem de precisão realizada com `llvm-objdump`:

### 5.1. Pacote `CONSTANT_FILL` (Opcode 11 / `0x0B`)
```assembly
00000000000676f4 <AMDGFX10SDMAChannel::writeConstantFillCommand>:
   67707: movl $0x8000000b, (%rsi)   ; Header: Opcode 0x0B, Bit 31=1 (DW fill)
   6770d: movl %edx, 0x4(%rsi)       ; DW1: Endereço de destino [31:0]
   67714: movl %edx, 0x8(%rsi)       ; DW2: Endereço de destino [63:32]
   67717: movl %r8d, 0xc(%rsi)       ; DW3: Valor constante (fill data pattern)
   67727: movl %ecx, 0x10(%rsi)      ; DW4: Contagem de bytes (alinhada a 4 bytes, máscara 0x3FFFFC)
```
* **Conclusão:** O pacote consome exatamente **5 DWORDs**. A máscara de contagem máxima é `0x3FFFFC` (4 MB menos 4 bytes por pacote).

### 5.2. Pacote `WRITE_PTE_PDE` (Opcode 12 / `0x0C`)
```assembly
000000000006774a <AMDGFX10SDMAChannel::writeWritePTEPDECommand>:
   6776b: movl $0xc, (%rsi)          ; Header: Opcode 0x0C (PTE/PDE generation)
   67771: movl %r10d, 0x4(%rsi)      ; DW1: Endereço da tabela de página [31:0] (alinhado a 8 bytes)
   67775: movl %edx, 0x8(%rsi)       ; DW2: Endereço da tabela de página [63:32]
   67778: movl %r8d, 0xc(%rsi)       ; DW3: Máscara / Flags base da PTE [31:0]
   67780: movl %r8d, 0x10(%rsi)      ; DW4: Máscara / Flags base da PTE [63:32]
   67784: movl %r9d, 0x14(%rsi)      ; DW5: Incremento de endereço físico por entrada
```

### 5.3. Pacote `VM_INVALIDATE` (Invalidação de TLB)
* Opcode emitido: `0x10` (Opcode 16).
* Para invalidação de faixa estendida (*range invalidate*), o driver copia um bloco pré-formatado de **24 DWORDs (96 bytes)** via `rep movsl` e preenche:
  * Offset `+0x20`: Início da GPUVA (Low 32)
  * Offset `+0x2C`: Início da GPUVA (High 32)
  * Offset `+0x38`: Fim da GPUVA (Low 32)
  * Offsets `+0x48`, `+0x4C`: Flags de sincronismo e VMID.

---

## 6. PM4 Engine, MQD e Preempção Gráfica/Computação

### 6.1. Leiaute do Memory Queue Descriptor (MQD) de GFX10
Desmontagem da função `AMDGFX10PM4Engine::fillComputeMQD`:
* `MQD[0x00]` (Offset `0x000`): **`0xC0310800`** — Magic e controle de versão do MQD de GFX10.
* `MQD[0x0B]` (Offset `0x02C`): `0x00000001`.
* `MQD[0x17]` (Offset `0x05C`): `0xFFFFFFFFFFFFFFFF`.
* `MQD[0x20]` (Offset `0x080`): `0x00000003`.
* `MQD[0x80]` (Offset `0x200`): `CP_HQD_PQ_BASE_LO` (Endereço base do anel, alinhado a 256 bytes).
* `MQD[0x81]` (Offset `0x204`): `CP_HQD_PQ_BASE_HI`.
* `MQD[0x88]` (Offset `0x220`): `CP_HQD_PQ_RPTR_REPORT_ADDR_LO`.
* `MQD[0x89]` (Offset `0x224`): `CP_HQD_PQ_RPTR_REPORT_ADDR_HI`.
* `MQD[0x8B]` (Offset `0x22C`): `CP_HQD_PQ_WPTR_LO`.
* `MQD[0x91]` (Offset `0x244`): **`CP_HQD_PQ_CONTROL`** = **`0xD130860D`** `| (queue_size_log2 << 30)`.

### 6.2. Protocolo de Modo Seguro do RLC (`RLC_SAFE_MODE`)
Antes de tocar nos anéis gráficos ou de computação (para criação de fila, preempção ou reset), o driver Apple executa:
1. Chama `AMDGFX10PM4Engine::enterRlcSafeMode()`.
2. Aguarda confirmação de que o RLC entrou em modo passivo.
3. Altera os ponteiros de MQD ou registradores de HQD.
4. Chama `AMDGFX10PM4Engine::exitRlcSafeMode()` para retomar a operação normal de energia.

---

## 7. Mapeamento de Diretrizes e Próximos Passos de Engenharia

Com este catálogo extraído, as seguintes decisões técnicas ficam travadas:

1. **Item 77 (EDID por AUX):**  
   Não é necessário implementar todo o DCN para ler o monitor. A rotina `AuxChannelProxy::transferI2c` do [`AMDSupport.kext`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/RDNA1_MACOS/AMDSupport.kext) mostra como interrogar o canal AUX da PHY DisplayPort de forma autônoma.
2. **Prioridade P9 (Display Nativo DCN 2.1):**  
   O arquivo [`AMDRadeonX6000Framebuffer.kext`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/RDNA1_MACOS/AMDRadeonX6000Framebuffer.kext) é o manual de registradores e sequenciamento de hardware do DCN 2.1. Todas as estruturas de Hubbub, Hubp e clock manager de Renoir estão disponíveis para consulta por desmontagem.
3. **Auditoria de Paging SDMA e Invalidação de TLB:**  
   Nossos construtores `BC250_SDMA_OP_CONST_FILL (0x8000000B)` e `BC250_SDMA_OP_PTE_PDE (0x0C)` foram **100% corroborados pelo binário da Apple**, descartando erros de codificação de opcode no KMD.
4. **VCN (Aceleração Multimídia Futura):**  
   Quando o driver atingir maturidade gráfica (pós-P9), a implementação do VCN 2.0/2.5 deve utilizar o modelo de canais segregados (`VCN2DecChannel` e `VCN2EncRing`), pois o modelo de fila unificada pertence exclusivamente a RDNA 2.
