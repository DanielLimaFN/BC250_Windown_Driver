# Base de Conhecimento: Driver WDDM Nativo para AMD BC-250 (Cyan Skillfish)

> **Documento Oficial de Engenharia & Especificação de Silício**  
> **Status:** 100% Validado em Silício Real  
> **Projeto:** `BC250-V2`

---

## 1. Identificação do Hardware & Topologia PCI

- **Hardware ID:** `PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00`
- **Arquitetura de Silício:** AMD Cyan Skillfish (APU derivada do chip gráfico Oberon / Navi 10 Lite, 12 WGPs / 24 CUs / 1536 Shaders).
- **BAR5 (MMIO):** 512 KB (`0x80000` bytes) de registradores de controle mapeados em memória física não-cacheada (`PAGE_NOCACHE`).
- **BAR0 (VRAM / Framebuffer):** 256 MB (`0x10000000` bytes) ou superior mapeados em espaço físico de 64 bits (`PAGE_WRITECOMBINE`).
- **Barramento SMN (System Management Network):**
  - Registrador de Índice: `A_NB_SMN_INDEX` (`0x00038` no BAR5).
  - Registrador de Dados: `A_NB_SMN_DATA` (`0x0003C` no BAR5).

---

## 2. Inicialização e Gestão de Energia (SMU)

- **Desativação de GFXOFF:**
  - Envio de Mensagem `0x06` na Fila 2 da SMU (`MP1_SMN_MSG_QUEUE2_MSG = 0x03B10908`, `ARG0 = 0x04`, `RESP = 0x03B10934`).
  - Resposta esperada: Status `0x01` (Sucesso).
  - Garante que a GPU não entre em sono profundo de clock-gating e mantenha o clock nominal de **1500 MHz**.

---

## 3. Segurança e Microcódigos (PSP Mailbox & Firmwares)

- **GPCOM Ring (Host RAM):**
  - Alocado em memória física contígua do sistema do Windows via DMA.
  - Registrado via `C2PMSG_69..71` e ativado por `C2PMSG_64 = 0x80000000`.
  - Handshake de inicialização do Trusted OS: `0x80020000`.
- **TMR (Trusted Memory Region):**
  - Carveout de 4 MB alocado em VRAM `0xF40F800000` via comando PSP `0x00050000`.
- **Cadeia de 7 Firmwares Autenticados:**
  1. `SDMA0` (`0x14`): motor de DMA e cópia de memória.
  2. `RLC` (`0x01`): controle de raster e energia.
  3. `CE` (`0x02`): Constant Engine.
  4. `PFP` (`0x03`): Prefetch Parser.
  5. `ME` (`0x04`): Micro-Engine principal do Command Processor.
  6. `MEC` (`0x05`): Compute Engine 1.
  7. `MEC2` (`0x05`): Compute Engine 2.
  *Todos autenticados com `RespStatus = 0x0`.*

---

## 4. O Pipeline de Vídeo e Framebuffer UEFI GOP

### A Regra Fundamental de Estabilidade de Display:
A placa AMD BC-250 opera no Windows através do **scanout linear do firmware UEFI GOP (Graphics Output Protocol)**.

1. **Herança do Framebuffer no Boot:**
   - Durante a inicialização do Windows, o driver deve invocar `DxgkCbAcquirePostDisplayOwnership` no `StartDevice`.
   - O SO fornece o endereço físico do framebuffer (`PhysicAddress`), largura (ex: 1920), altura (ex: 1080), pitch (ex: 7680 bytes) e formato (`D3DDDIFMT_A8R8G8B8`).
2. **Mapeamento do Framebuffer:**
   - Se o framebuffer estiver contido no BAR0, o driver mapeia a faixa com `MmMapIoSpaceEx(..., PAGE_READWRITE | PAGE_WRITECOMBINE)`.
3. **Proibição de Escrita em Registradores de GCVM/Display no StartDevice:**
   - **NUNCA** reprogramar `GCMC_SYS_APERTURE_LOW/HIGH`, invalidar TLBs do motor de display ou tocar em `RLC_CNTL` durante o arranque normal.
   - Qualquer escrita nessas aperturas altera o mapeamento de onde o controlador de vídeo está lendo os pixels, causando tela preta imediata.

---

## 5. Contrato do Windows 10/11 WDDM Display-Only (KMDOD)

### A. Registro do Driver:
- Deve utilizar exclusivamente `DxgkInitializeDisplayOnlyDriver(&initialization, &callbacks)`.
- **Não** declarar `InstalledDisplayDrivers` no INF (o KMDOD utiliza o WARP da Microsoft para DWM e 3D).

### B. Conector e Detecção de Monitor (HPD):
- `QueryChildRelations`: Declarar `ChildRelations[0].ChildDeviceType = TypeVideoOutput`, `ChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected`, `ChildRelations[0].ChildUid = 0`.
- `QueryChildStatus`: Retornar `ChildStatus->HotPlug.Connected = TRUE`.
- `QueryDeviceDescriptor`: Retornar `STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA` (permite que o Windows use as propriedades herdadas do display default).

### C. Motor de VSync (Temporizador DPC a 60 Hz):
- O subsistema `dxgkrnl.sys` exige que o driver atenda a `DxgkDdiControlInterrupt(DXGK_INTERRUPT_DISPLAYONLY_VSYNC, TRUE)`.
- O driver deve rodar um temporizador periódico de kernel (`KeSetTimerEx` a 16.66 ms) que invoca `DxgkCbNotifyInterrupt` com `DXGK_INTERRUPT_DISPLAYONLY_VSYNC` e `DxgkCbNotifyDpc`.
- **Sem esse motor de VSync, o agendador de apresentação congela** e o Windows nunca chama `PresentDisplayOnly`.

### D. Apresentação na Tela (`DxgkDdiPresentDisplayOnly`):
- O Windows entrega retângulos sujos (`pSource` e `pDstRects`).
- O driver executa a cópia linear direta (`memcpy` / blit de memória) do buffer de sistema para o framebuffer físico mapeado em `dev->DisplayVirtualBase`.

---

## 6. Histórico de Diagnósticos e Lições Aprendidas

| Problema Encontrado | Causa Raiz Identificada | Solução Definitiva Validada |
|---|---|---|
| **Código 43 no Start (`CM_PROB_FAILED_POST_START`)** | Driver declarava `TargetId` inválido ou falhava em `AcquirePostDisplayOwnership`. | Aceitar qualquer `TargetId` retornado pela UEFI e mapear BAR0 de forma 64-bit segura. |
| **Tela Preta no Boot com Driver da Comunidade** | Driver tentou ser Full Render Miniport, desativou o GOP e tentou troca dinâmica de CRTCs inexistentes. | Usar arquitetura KMDOD Display-Only com herança de framebuffer. |
| **Tela Preta pós-StartDevice** | Bring-up de GCVM/SDMA em `StartDevice` reconfigurou aperturas de memória do scanout da UEFI. | Manter modo Display-Only Puro no StartDevice (`SkipHwBringup = 1`). |
| **Parada em `QueryDeviceDescriptor` sem chamar `Present`** | Desativação de callbacks de VSync (Perfil B) impediu o `dxgkrnl` de avançar o agendador de quadros. | Ativar motor de VSync via timer DPC de 60 Hz e responder `DxgkCbNotifyInterrupt`. |
| **Driver antigo retido na DriverStore** | `DriverVer` com data/versão estática fez o `pnputil` ignorar novos binários. | Versionamento dinâmico automático (`MMdd.HHmm`) a cada compilação. |
