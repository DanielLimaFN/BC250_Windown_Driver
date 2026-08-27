# Arquitetura do Driver WDDM Nativo: AMD BC-250 (Cyan Skillfish)

> **Documento de Especificação Técnica & Arquitetura Oficial**  
> **Repositório:** `bc250-win`  
> **Módulo:** `wddm/` (Desenvolvimento 100% Nativo e Independente)

---

## 1. O Que Sabemos com 100% de Certeza (Fatos de Hardware Validados)

Todo o pipeline abaixo foi exaustivamente instrumentado, testado e validado em silício real com zero falhas através do nosso driver de depuração (`bc250dbg.sys`) e da suíte de controle (`bc250ctl.exe`):

### A. Identificação e Espaço de Endereçamento MMU / PCI
- **Device ID:** `0x13FE` (PCI `VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00`).
- **BAR5 (MMIO):** 512 KB de registradores GPU mapeados no espaço físico do host.
- **BAR0 (VRAM Framebuffer):** 256 MB de abertura direta mapeada na VRAM física da GPU (`0xF400000000`).
- **SMN Indirect Window:** Acesso aos barramentos de gerenciamento interno via registradores NBIO:
  - `A_NB_SMN_INDEX = 0x00038`
  - `A_NB_SMN_DATA  = 0x0003C`

### B. Gestão de Energia & Frequência (SMU)
- **Desativação de GFXOFF:** Envio de Mensagem `0x06` na Fila 2 (`MP1_SMN_MSG_QUEUE2_MSG = 0x03B10908`, `ARG0 = 0x04`, `RESP = 0x03B10934`) retorna Status `0x01` (Sucesso).
- **Controle de Clocks:** GPU opera estável a **1500 MHz** com **12 WGPs (24 CUs / 1536 Shaders)** totalmente acordados.

### C. Segurança e Carregamento de Microcódigos (PSP Mailbox)
- **GPCOM Ring:** Alocado em **Host System RAM Contígua** via DMA bus-mastering. Registrado através de `C2PMSG_69..71` e ativado via `C2PMSG_64 = 0x80000000`, atingindo handshake `0x80020000` (TOS pronto).
- **TMR Carveout:** Região de segurança de **4 MB** reservada na VRAM em `GPU 0xF40F800000` via comando `0x00050000`.
- **Autenticação dos 7 Firmwares:**
  1. `SDMA0` (`0x14`): microcódigo F32 carregado e operando em loop de polling.
  2. `RLC` (`0x01`): microcódigo de controle de raster e energia.
  3. `CE` (`0x02`): microcódigo de Constant Engine.
  4. `PFP` (`0x03`): microcódigo de Prefetch Parser.
  5. `ME` (`0x04`): microcódigo da Micro-Engine principal.
  6. `MEC` (`0x05`): microcódigo de Compute Engine 1.
  7. `MEC2` (`0x05`): microcódigo de Compute Engine 2.
  *Todos os 7 autenticados com `RespStatus = 0x0`.*

### D. Gerenciamento de Memória Gráfica (GCVM L2 MMU)
- **Abertura de Sistema (System Aperture):** `[0xF400000000, 0xF420000000)` (512 MB de VRAM).
- **Identity Aperture Desativada:** Alinhada com o driver oficial do Linux (`disable_identity_aperture`).
- **TLB L1 Control:** `0x00001859u`.
- **Proteção e Tradução:** `STALLED_STAT2 = 0x00000000` (zero stalls de leitura RCIU/L2).

### E. Command Processor (CP) PM4 Execution
- **Ring 0 Buffer:** Localizado em VRAM `0xF40FE00000` (coberto pela Abertura de Sistema).
- **Engatilhamento de WPTR:** Ordem canônica 64-bit SOC15: escrita em `CP_RB0_WPTR` (LO) seguida de `CP_RB0_WPTR_HI` (HI).
- **Avanço de RPTR:** Configuração de `RB_NO_UPDATE = 0` em `CP_RB0_CNTL` permite que o hardware consuma pacotes e atualize o ponteiro autonomamente.
- **Golden Context Registers:** `VGT_NUM_INSTANCES = 1` (`0x30934`) e `PA_SC_LINE_STIPPLE_STATE = 0` (`0x30A04`).
- **Desengatilhamento:** `CP_ME_CNTL = 0x0` pós-configuração de anel.

### F. Regra de Ouro de Estabilidade de Vídeo (DCN)
- **NUNCA** tocar em `RLC_CNTL` (`0x3B000`) nem tentar power-gating agressivo de display.
- O sinal do monitor é mantido ativo pelo Framebuffer linear herdado do UEFI GOP.

---

## 2. Diagnóstico: Por Que o Driver da Comunidade Falhou?

O código da comunidade (`AMD-BC-250-Windows-Driver-main`) possui falhas conceituais graves:
1. **Tentativa de Atuar como Full Render Display Miniport:**
   - Registrou-se via `DxgkInitialize` declarando suporte a troca de modos de vídeo dinâmicos, CRTCs virtuais e pipelines de display que a placa BC-250 no Windows não possui.
   - Ao inicializar, o Windows DirectX Kernel (`dxgkrnl.sys`) **desativou o Framebuffer do BIOS/UEFI** para tentar comutar o modo do display.
   - Como a BC-250 depende do Framebuffer linear para exibir vídeo, o sinal do monitor foi desligado permanentemente (tela preta irrecuperável e crash do PnP Evento 219).
2. **Conflito de Segmentos de Memória:**
   - O callback `DXGKQAITYPE_QUERYSEGMENT` da comunidade retornou faixas arbitrárias de memória que colidiram com o gerenciador de memória do Windows.

---

## 3. A Nova Arquitetura Nativa: KMDOD (Display-Only Driver)

Para garantir **zero telas pretas**, **estabilidade máxima** e **boot instantâneo**, nosso driver adotará o padrão oficial Microsoft **KMDOD (`DxgkInitializeDisplayOnlyDriver`)**:

```
+-------------------------------------------------------------------+
|                        Windows Desktop / DWM                      |
+-------------------------------------------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
|                      DirectX Graphics Kernel                      |
|                           (dxgkrnl.sys)                           |
+-------------------------------------------------------------------+
                                  |
        +-------------------------+-------------------------+
        |                                                   |
        v                                                   v
+-------------------------------+   +-------------------------------+
|  wddm_driver.c (KMDOD Core)   |   |   wddm_hw.c (Bring-up Nativo) |
| - DxgkInitializeDisplayOnly   |   | - SMU GFXOFF Disable (Fila 2) |
| - DxgkCbAcquirePostDisplayOwn |   | - PSP GPCOM Ring em Host RAM  |
| - DxgkDdiPresentDisplayOnly   |   | - SDMA0 uCode Loading         |
|   (Blit direto no Linear FB)  |   | - PSP TMR 4MB + 6 Firmwares   |
| - Zero Mode Switch / Zero Drop|   | - GCVM L2 MMU Aperture        |
| - Monitor 100% Estável        |   | - GFX CP Ring 0 em VRAM       |
+-------------------------------+   +-------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
|               Hardware AMD BC-250 (Cyan Skillfish)                |
|    - BAR0: Framebuffer Linear 1920x1080 @ 60Hz                    |
|    - BAR5: MMIO 512 KB                                            |
|    - 12 WGPs / 24 CUs Ativos a 1500 MHz                           |
+-------------------------------------------------------------------+
```

---

## 4. Estrutura de Arquivos do Módulo `wddm/`

```
wddm/
├── ARCHITECTURE.md          # Este documento de especificação e arquitetura
├── build.bat                # Script de compilação, geração do catálogo .cat e assinatura
├── wddm.h                   # Definições, estruturas limpas e protótipos de kernel
├── wddm_driver.c            # Ponto de entrada oficial KMDOD (DriverEntry, PresentDisplayOnly)
├── wddm_hw.c                # Nosso pipeline comprovado de bring-up de hardware (PSP/SDMA/CP/SMU)
├── wddm_fw.h                # Microcódigos binários embutidos diretamente no driver
├── wddm.inf                 # Arquivo de instalação PnP Display-Only limpo
└── package.bat              # Script para consolidar o pacote final assinado
```

---

## 5. Plano de Implementação Passo a Passo

### Etapa 1: Estruturas & Cabeçalhos Limpos (`wddm.h` e `wddm_fw.h`)
- Definir a estrutura `BC250_WDDM_DEVICE` contendo apenas o estado estritamente necessário (MMIO Virtual Base, Framebuffer Virtual Base, Dimensões de Tela, Ponteiros de Anel).
- Exportar os arrays binários dos 7 microcódigos em `wddm_fw.h`.

### Etapa 2: Motor de Hardware Nativo (`wddm_hw.c`)
- Portar diretamente as rotinas testadas e comprovadas de `dbg/hw.c` e `dbg/psp.c`:
  - `Bc250HwBringup`: Executa a sequência atômica (SMU $\rightarrow$ PSP $\rightarrow$ SDMA0 $\rightarrow$ TMR $\rightarrow$ 6 Firmwares $\rightarrow$ GCVM $\rightarrow$ CP).
  - `Bc250HwShutdown`: Congela com segurança o CP e a SDMA sem tocar no DCN.

### Etapa 3: Ponto de Entrada KMDOD (`wddm_driver.c`)
- Implementar `DriverEntry` registrando a tabela `KMDDOD_INITIALIZATION_DATA` via `DxgkInitializeDisplayOnlyDriver`.
- Em `DxgkDdiStartDevice`:
  - Mapear BAR5 (MMIO) e BAR0 (Framebuffer).
  - Chamar `DxgkCbAcquirePostDisplayOwnership` para herdar o Framebuffer já ativo no boot.
  - Executar `Bc250HwBringup`.
- Em `DxgkDdiPresentDisplayOnly`:
  - Transferir com precisão os retângulos sujos (*dirty rects*) para a memória linear.

### Etapa 4: INF, Catálogo de Segurança & Build Automatizado
- Criar `wddm.inf` limpo configurando a classe `Display` e o serviço `bc250wddm`.
- Configurar `build.bat` para compilar com WDK 10.0.26100, gerar `wddm.cat` via `Inf2Cat` e assinar com o certificado `BC250-WIN Test`.

---

## 6. Critérios de Sucesso e Validação

1. **Instalação PnP sem Erro:** `pnputil /add-driver wddm.inf /install` retorna Sucesso (Código 0).
2. **Preservação Contínua do Vídeo:** Monitor permanece ligado e responsivo durante todo o processo de instalação e reinicialização.
3. **Gerenciador de Dispositivos:** O dispositivo aparece como *"AMD BC-250 Graphics Adapter (Display-Only)"* com status *"Este dispositivo está funcionando corretamente"*.
4. **Hardware Ativo em Segundo Plano:** O Command Processor, a SDMA0 e a SMU continuam 100% funcionais a 1500 MHz.
