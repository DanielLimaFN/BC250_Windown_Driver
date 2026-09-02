# Informações e Base de Conhecimento Mestre — AMD BC-250 (Fase 2: Compute & SDMA)

> **Documento Consolidado de Engenharia, Histórico Físico e Diretrizes Inegociáveis**  
> *Target: AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2 / Oberon Lite) | Windows 11 x64*  
> *Criado em: 2026-08-29*

---

## 1. Identificação do Hardware e Topologia do Silício

- **Dispositivo PCI:** `PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00`
- **Arquitetura da GPU:** AMD RDNA1/RDNA2 Híbrido (*Cyan Skillfish 2 / GC 10.1.3 / GFX1013*).
- **Configuração Físico-Lógica:**
  - 24 Compute Units ativas (12 WGPs / 1536 Stream Processors).
  - Capacidade de silício: Die com até 40 CUs presentes, sendo 24 CUs liberadas por padrão na VBIOS de mineração.
- **CPU Host:** AMD Zen 2 (8 núcleos físicos na pastilha, com **6 núcleos ativos** habilitados pela VBIOS, máscara `0x77` = `01110111b`).
- **Memória:** 16 GB GDDR6 unificada (barramento de 256 bits, compartilhada dinamicamente entre CPU e GPU).
- **Mapeamento de BARs PCI comprovado:**
  - **BAR0 (VRAM / Framebuffer):** `0x00000000C0000000` (256 MB mapeável, com ReBAR até a VRAM total).
  - **BAR2 (Doorbell):** `0x00000000D0000000` (2 MB de abertura de doorbells de hardware).
  - **BAR5 (MMIO):** `0x00000000FE800000` (512 KB não-cacheados / `PAGE_NOCACHE`).
- **Acesso ao Barramento SMN (System Management Network):**
  - Registrador de Índice: `0x00038` no BAR5.
  - Registrador de Dados: `0x0003C` no BAR5.
  - **Regra do Flush de PCI Posting:** Ao escrever no índice (`0x38`), é **obrigatório** ler de volta o índice (`0x38`) antes de ler ou escrever em `0x3C`. Sem isso, o buffer de escrita PCIe engole a transação e o hardware devolve `0x00000000`.

---

## 2. As Lições Críticas de Estabilidade (O Que NUNCA Fazer)

Estas são as **causas raízes físicas** identificadas e comprovadas que causaram telas pretas ou travamentos em versões anteriores (gates 1.x a 3.23):

### 🚫 A. NUNCA invalidar o Contexto / Engine 0
- O registrador `R_GCVM_INVALIDATE_ENG0_REQ` (`0x00028210u` / `0x8210`) desliga imediatamente a Engine 0.
- Na arquitetura do BC-250 (que não tem display controller DCN autônomo e herda o scanout do UEFI GOP), **Engine 0 é a Engine de Display / VMID 0**.
- Invalidar a Engine 0 apaga o sinal de vídeo na hora, gerando tela preta irreversível sem reboot.
- **Regra:** Todas as operações de Compute e SDMA devem operar estritamente em **VMID 8 ou superior**, nunca no VMID 0.

### 🚫 B. NUNCA sobrescrever os primeiros 16 MB da VRAM
- O UEFI GOP aloca o framebuffer de scanout na base do BAR0 (endereço físico `0xC0000000`, offset 0 da VRAM).
- Em resolução 2560x1080 @ 60Hz (formato ARGB 32-bit), a tela consome **11.059.200 bytes (~11 MB)** contíguos de memória.
- No passado, alocações de teste no offset 1 MB (`0x00107000`) gravaram tabelas de páginas por cima dos pixels ativos, congelando a imagem.
- **Regra:** Buffers de teste, anéis de compute, MQDs e tabelas de páginas devem ser alocados **acima de 16 MB** (`0x01000000` em diante).

### 🚫 C. NUNCA tocar em `R_RLC_CNTL` durante o StartDevice do Windows
- Escrever em `R_RLC_CNTL` (`0x0003B000`) ou reprogramar `GCMC_SYS_APERTURE_LOW/HIGH` altera a rota de onde o controlador lê os pixels.
- **Regra:** O `StartDevice` do driver principal (`bc250mcdm.sys`) deve permanecer **puro Display-Only** (`SkipHwBringup = 1`).

### 🚫 D. NUNCA disparar VSync fora de DIRQL sem sincronização
- `DxgkCbNotifyInterrupt(DXGK_INTERRUPT_DISPLAYONLY_VSYNC)` invocado de um timer DPC sem sincronização congela o agendador do `dxgkrnl.sys`.
- **Regra:** O VSync do KMDOD deve ser encapsulado via `DxgkCbSynchronizeExecution`.

---

## 3. Estado Atual Comprovado no Silício (O Que JÁ FUNCIONA)

| Componente | Evidência Física no Hardware | Estado |
|---|---|---|
| **Display 3.24.0.0** | Código 0, imagem em 2560x1080 @ 60Hz, VSync funcional (`DmmVsyncCount >= 7`) | **PROVADO** |
| **SMU Mailbox (Q0)** | Handshake `TestMessage (0x01)` com echo `42`, SMU v88.6.0 (IF 8), GFX Clock 1500 MHz, VID 99 (931 mV) | **PROVADO** |
| **SMU Mailbox (Q3)** | Leitura de tensão do Core CPU (824-831 mV) e GPU VDD (931 mV) | **PROVADO** |
| **Diodo Térmico TSI** | SMN `0x00059800` calibrado pela fórmula AMD: `(tsiRaw * 125 - 49000) / 1000`, ~77.5 °C em repouso | **PROVADO** |
| **SDMA0 Engine** | Ring consumido no silício, `RPTR=68/68`, fence física `0x43DAFFFE -> 0xBC250123`, 4 DWORDs com 0 mismatch | **PROVADO** |
| **CP Ring 0 (PM4)** | Ring submetido e drenado (`RPTR=15/16`), fence física `0xBC250123`, microengines ativas (`JT_STAT=0x00FF0303`) | **PROVADO** |
| **Serviço Compute** | Endpoint `\\.\Bc250Compute` operacional via driver `bc250compute.sys`, permitindo MMIO/SMN direto | **PROVADO** |

---

## 4. Por Que o Chrome / Jogos Ainda Rodam em Software (WARP)?

1. O driver ativo `bc250mcdm 3.24.0.0` está registrado no Windows como **KMDOD (Kernel-Mode Display-Only Driver)** através de `DxgkInitializeDisplayOnlyDriver`.
2. Um driver Display-Only por especificação da Microsoft **não provê pipeline de renderização 3D/DirectX**.
3. O Windows 11 automaticamente redireciona todas as chamadas de Canvas, WebGL e Direct3D para o **WARP (`d3d10warp.dll`)**, renderizando em software nos núcleos Zen 2 da CPU.
4. Por isso o `GRBM_STATUS` fica em repouso (`GUI_ACTIVE=0`, `CP_BUSY=0`) durante o uso do Chrome, enquanto a CPU esquenta.

---

## 5. Arquitetura da Fase 2: Ativação dos Anéis de Compute & SDMA

Para ativar a aceleração sem quebrar o vídeo, o trabalho deve ser isolado do pipeline de display:

```text
[Aplicações / UMD / Testes]
           │
           ▼
[Serviço bc250compute.sys / IOCTLs sob demanda]
           │  (Opera exclusivamente em VMID 8+ e VRAM > 16 MB)
           ▼
┌─────────────────────────────────────────────────────────────┐
│                 GPU AMD BC-250 (GC 10.1.3)                  │
│                                                             │
│  [Display Engine / VMID 0]       [Compute / SDMA Engine]    │
│  • Scanout UEFI 2560x1080        • SDMA0 DMA Rings          │
│  • VSync 60Hz DPC                • MEC1/MEC2 Async HQD      │
│  • VRAM 0x000000..0x01000000     • VRAM > 0x01000000        │
│  • Intocável / Protegido         • Ativação sob demanda     │
└─────────────────────────────────────────────────────────────┘
```

### Regras de Execução para Filas Assíncronas MEC/HQD:
1. **Guarda de GFXOFF (`COMPUTE_GFXOFF_GUARD`):**
   - O hardware BC-250 congela filas assíncronas se o GFXOFF estiver ativo.
   - Obrigatório enviar `PPSMC_MSG_DisallowGfxOff = 0x15` na Queue 3 antes de tocar nos anéis e reativar com `0x14` após a conclusão.
2. **Bug do ACE Threadgroup:**
   - O chip possui um bug conhecido em `PARTIAL_TG_EN` quando usado com dimensões de threadgroup.
   - Forçar dispatches de compute para o modo **thread-dimension**.
3. **Seleção Dinâmica do Scheduler:**
   - O scheduler do Windows no BC-250 é `0x58504840`. A ativação do bit `ENABLE=0x80` resulta em `0x585048C0` e seletor GRBM `0x8` (ME2/P0/Q0).
4. **Flush HDP e Coerência de Cache:**
   - Antes de publicar o WPTR da fila, executar o flush de coerência:
     - `HDP_MEM_COHERENCY_FLUSH_CNTL` (`0x000086D0u` = `1`)
     - `GCVM_L2_CNTL2` (`0x00028208u` = `3`)
5. **Tripé Obrigatório de Validação no Silício:**
   - `RPTR` deve alcançar `WPTR`.
   - Fence física escrita pela GPU na VRAM (verificada com poison pré-existente).
   - Dados de saída conferidos byte a byte com zero mismatch.

## 7. Descobertas Críticas do Silício na Fase 2 (Fevereiro/Agosto 2026)

### A. Checksums Oficiais de Firmware SDMA0
- Ao carregar `cyan_skillfish2_sdma.bin` na SRAM do SDMA via PSP Mailbox (`0x3B208` / `0x3B200`), o hardware grava na porta PCIe `BC250_SDMA0_UCODE_CHECKSUM` (`0x04A24u`):
  - **`0xAB6305C6`** (Checksum oficial de produção da AMD).
  - **`0x55B182E3`** (Checksum alternativo de laboratório).
- Ambos são 100% autênticos e válidos. Não deve haver tentativas de sobreposição ou recarga se qualquer um deles estiver presente.

### B. O Duplo Endereçamento de Memória (Host PA vs MC Crossbar)
- **Host Physical Address (CPU):** O processador enxerga a VRAM pelo BAR0 PCI em **`0x00000000C0000000ull`**.
- **Memory Controller Address (GPU / MC):** As microengines da GPU (SDMA, CP PM4, RLC) acessam os mesmos canais GDDR6 através do barramento interno de MC em **`0x000000F400000000ull`** (`BC250_MC_VRAM_BASE`).
- **Consequência Crucial:** O ponteiro de anel e endereços nos pacotes PM4/SDMA devem **sempre** somar `0x000000F400000000ull`, enquanto o kernel do Windows mapeia via `MmMapIoSpaceEx` em `0xC0000000ull`.
- Mapear a VRAM com **`PAGE_NOCACHE`** é essencial para evitar que leituras de fences pela CPU fiquem retidas em buffers Write-Combining.

### C. Sequência de Flush HDP Canônica GFX10
- Para garantir coerência entre as caches de L2/L1 e a GDDR6, a rotina completa exige leitura pós-escrita:
  1. Leitura de `R_HDP_MISC` (`0x03FCCu`).
  2. Escrita de `0u` em `R_HDP_FLUSH` (`0x04A80u`) com readback.
  3. Escrita de `1u` em `R_HDP_INVALIDATE` (`0x03FC4u`) com readback.
  4. Toggle do bit 0 em `R_HDP_MISC` (`misc | 1u` seguido de `misc & ~1u`) com readback.

### D. Unhalt e Clocks do Command Processor (CP PM4)
- Registrador mestre de controle: `R_CP_ME_CNTL` (`0x000086D8u` no SOC15).
- A máscara de unhalt total deve limpar os 3 blocos:
  - `BC250_PFP_HALT (0x20000000u)`: Prefetch Parser (busca pacotes do anel).
  - `BC250_ME_HALT  (0x10000000u)`: Microengine principal.
  - `BC250_CE_HALT  (0x01000000u)`: Constant Engine.
  - Total: `R_CP_ME_CNTL &= ~0x31000000u`.
- Desbloqueio de clocks do CP:
  - `R_CGTT_CPF_CLK_CTRL` (`0x0003C2C4u`) = `(read & ~0xFCFF8FFFu) | 0xF8000100u`.
  - `R_GE_FAST_CLKS` (`0x00008920u`) = `(read & ~0x3FFFFFFFu) | 0x0000493Eu`.
  - `R_CP_SD_CNTL` (`0x0000C6DCu`) = `(read & ~0x800007FFu) | 0x000005FFu`.
- Gatilho do anel físico: `R_CP_RB0_ACTIVE` (`0x0000C680u` = `1u`).

---

## 8. Procedimentos de Emergência e Recuperação

- **Script de Recuperação Mestre:** `C:\BC250_RECOVERY.cmd` (restaura `BasicDisplay` caso ocorra tela preta).
- **Ativação / Parada do Serviço de Telemetria:**
  - Iniciar: `tools\start-bc250compute-service.ps1`
  - Parar: `tools\stop-bc250compute-service.ps1`
- **Monitor de Silício em Tempo Real:**
  - Executar: `tools\monitor\bc250_monitor.exe`
  - Monitora frequência do SMU, tensões, diodo TSI e bits de atividade do `GRBM_STATUS` (`GUI_ACTIVE`, `CP_BUSY`).
- **Runner de Execução Física (SDMA & PM4):**
  - Executar: `tools\monitor\bc250_silicon_runner.exe`
