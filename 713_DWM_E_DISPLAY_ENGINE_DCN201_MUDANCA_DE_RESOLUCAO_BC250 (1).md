# 713 — DWM, VidPn e Display Engine DCN 2.0.1: Mudança de Resolução, Enumeração de Modos e Apresentação no Windows para a AMD BC-250

**Data:** 13/10/2026  
**Autor:** Engenharia de Sistemas & Antigravity IDE  
**Classificação:** Manual Técnico de Implementação do Display Engine WDDM 2.7 / DCN 2.0.1  
**Hardware Alvo:** AMD BC-250 APU (Cyan Skillfish 2 / GC 10.1.3 / DCN 2.0.1)  
**Subsistema Operacional:** Windows 10/11 DWM (`dwm.exe`), DXGI e DirectX Graphics Kernel (`dxgkrnl.sys`)

---

## 1. Visão Geral: O que Acontece quando o Usuário Muda a Resolução?

No Windows, mudar a resolução (por exemplo, de 1080p para 1440p ou 4K, ou de 60Hz para 144Hz) **não é um simples comando de mudar largura e altura**.  
O subsistema de vídeo do Windows opera através de um grafo formal chamado **VidPn (Video Present Network)** gerenciado em conjunto pelo `dxgkrnl.sys`, pelo **DWM (`dwm.exe`)** e pelo seu **KMD**.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│             USUÁRIO / JOGO / PAINEL DE CONTROLE (Mudar Resolução)           │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ EnumDisplaySettings / ChangeDisplaySettings
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         SUBSISTEMA DXGI & DWM                               │
│  - Detecta solicitação de novo modo (ex: 2560x1440 @ 144Hz)                 │
│  - Constrói a proposta de topologia VidPn (Source Mode + Target Mode)        │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                 SUBSISTEMA GRÁFICO (dxgkrnl.sys / VidPn Mgr)                │
│  1. DxgkDdiIsSupportedVidPn (Pergunta ao KMD se o hardware suporta)         │
│  2. DxgkDdiCommitVidPn (Comando formal: APLIQUE ESTA RESOLUÇÃO AGORA!)      │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ WDDM DDI (Ring 0)
┌──────────────────────────────────────▼──────────────────────────────────────┐
│                     SEU KMD CUSTOMIZADO (DCN 2.0.1)                         │
│  Passo 1: Bloqueia Scanout temporário (Blank / Double-Buffered)             │
│  Passo 2: Recalcula clocks DENTIST (PLL VCO / DISPCLK / PIXCLK)             │
│  Passo 3: Programa o OPTC/OTG com os timings (H_Total, V_Total, Sync)       │
│  Passo 4: Programa o HUBP com largura, altura, pitch e endereço físico UMA  │
│  Passo 5: Configura DIO / Link Encoder (HDMI TMDS ou DisplayPort HBR)       │
│  Passo 6: Desbloqueia tela (Unblank) e atualiza frequência do VBlank        │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Notificação VSync Interrupt
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                 DESKTOP WINDOW MANAGER (dwm.exe)                            │
│  - Destrói a Swapchain antiga de 1080p                                      │
│  - Aloca nova Swapchain de 1440p                                            │
│  - Entrega o novo buffer ao KMD via SetVidPnSourceAddressMPO3               │
│  - Retoma a composição visual na nova resolução sem artefatos ou travas     │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. A Arquitetura VidPn (Video Present Network) do Windows

O Windows modela a placa de vídeo e seus monitores como um grafo direcionado:

1. **VidPn Source (Origem dos Pixels):**
   * Representa a superfície de renderização (o Framebuffer) alocada na memória de vídeo.
   * Possui um **Source Mode Set**: lista de resoluções 2D e formatos de cor (ex: 1920x1080, 2560x1440, 3840x2160; formatos `D3DDDIFMT_A8R8G8B8`, `D3DDDIFMT_A2R10G10B10`).
2. **VidPn Target (Destino / Transmissor Físico):**
   * Representa o conector físico na placa (Porta HDMI ou DisplayPort).
   * Possui um **Target Mode Set**: lista de frequências de sinal de vídeo e timings de transmissão (ex: Pixel Clock de 148.5 MHz, VSync de 60.00 Hz, HSync de 67.5 kHz).
3. **VidPn Path (O Caminho):**
   * Liga uma VidPn Source a um VidPn Target. Define se a imagem será esticada (Scaling: Uncentered, Centered, Stretched) e se sofrerá rotação (Rotation: Identity, 90°, 180°, 270°).

---

## 3. As 6 Rotinas DDI que o seu KMD Deve Implementar para Mudança de Resolução

Para o Windows conseguir listar e trocar resoluções sem tela preta, o seu KMD deve exportar estas 6 funções:

```c
// 1. Enumera os conectores físicos na placa BC-250 (HDMI / DisplayPort)
NTSTATUS Bc250QueryChildRelations(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGK_CHILD_DESCRIPTOR    pChildRelations,
    IN ULONG                        ChildRelationsSize
);

// 2. Detecta se o cabo do monitor está conectado (Hot-Plug Detect - HPD)
NTSTATUS Bc250QueryChildStatus(
    IN_CONST_HANDLE                 hAdapter,
    IN_PDXGK_CHILD_STATUS           pChildStatus,
    IN BOOLEAN                      NonDestructiveOnly
);

// 3. Lê o EDID do monitor via canal I2C / AUX DDC do DCN
NTSTATUS Bc250QueryDeviceDescriptor(
    IN_CONST_HANDLE                 hAdapter,
    IN ULONG                        ChildUid,
    INOUT_PDXGK_DEVICE_DESCRIPTOR   pDeviceDescriptor
);

// 4. Motor de Enumeração de Modos Co-Funcionais (Lista resoluções disponíveis)
NTSTATUS Bc250EnumVidPnCofuncModality(
    IN_CONST_HANDLE                                  hAdapter,
    IN_CONST_PDXGKARG_ENUMVIDPNCOFUNCMODALITY_CONST pEnumCofuncModality
);

// 5. Validação de Topologia Proposta
NTSTATUS Bc250IsSupportedVidPn(
    IN_CONST_HANDLE                 hAdapter,
    INOUT_PDXGKARG_ISSUPPORTEDVIDPN pIsSupportedVidPn
);

// 6. APLICAÇÃO EFETIVA DA RESOLUÇÃO (Troca de Modo no Silício DCN)
NTSTATUS Bc250CommitVidPn(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_COMMITVIDPN_CONST pCommitVidPn
);
```

---

## 4. Passo a Passo da Mudança de Resolução no Silício DCN 2.0.1 (Cyan Skillfish)

Quando o Windows chama `Bc250CommitVidPn`, o seu KMD deve executar a sequência exata de controle de hardware no **DCN 2.0.1**:

### Passo 1: Descoberta dos Parâmetros Solicitados pelo Windows
O KMD extrai do objeto VidPn a resolução e os timings exatos:
```c
NTSTATUS Bc250CommitVidPn(
    IN_CONST_HANDLE                     hAdapter,
    IN_CONST_PDXGKARG_COMMITVIDPN_CONST pCommitVidPn
) {
    PBC250_ADAPTER pDev = (PBC250_ADAPTER)hAdapter;
    D3DKMDT_HVIDPN hVidPn = pCommitVidPn->hFunctionalVidPn;

    // Extrai o caminho ativo (Path), a Source (Resolução) e o Target (Timing)
    // Exemplo extraído: Largura = 2560, Altura = 1440, RefreshRate = 60000/1000 (60Hz)
    ULONG width  = 2560;
    ULONG height = 1440;
    ULONG pixelClockKHz = 241500; // 241.5 MHz para 1440p60

    // 1. Temporariamente coloca o OTG em modo Blank (evita ruído no monitor)
    Bc250DcnSetBlank(pDev, TRUE);

    // 2. Reprograma o PLL de Clocks DENTIST
    Bc250DcnProgramClocks(pDev, pixelClockKHz);

    // 3. Reprograma os registradores de Timing OTG
    Bc250DcnProgramTimings(pDev, width, height, pixelClockKHz);

    // 4. Reprograma o HUBP (Viewport e Pitch de Memória)
    Bc250DcnProgramHubp(pDev, width, height);

    // 5. Configura o Transmissor de Saída (HDMI ou DisplayPort)
    Bc250DcnProgramOutput(pDev);

    // 6. Desbloqueia o monitor (Unblank)
    Bc250DcnSetBlank(pDev, FALSE);

    return STATUS_SUCCESS;
}
```

---

### Passo 2: Cálculo de Clocks DENTIST (PLL VCO / DISPCLK / PIXCLK)
Na APU Cyan Skillfish, os clocks de display derivam do PLL **DENTIST VCO** (lido via registrador SMN `CLK4_CLK_PLL_REQ` em `0x16C00 + 0x460E`):

```c
#define mmDENTIST_DISPCLK_CNTL          (0x00C0 + 0x64)
#define DENTIST_DISPCLK_CHG_DONE        (1U << 19)

void Bc250DcnProgramClocks(PBC250_ADAPTER pDev, ULONG targetPixClkKHz) {
    // 1. Ler a frequência base do VCO (geralmente ~2.67 GHz na BC-250)
    ULONG vcoKHz = pDev->DcnVcoKHz; // ex: 2670000 kHz

    // 2. Calcular o divisor DENTIST necessário para suprir o novo Pixel Clock
    // DID = (VCO / TargetClock) ajustado em escala de quartos
    ULONG div4 = (vcoKHz * 4) / targetPixClkKHz;
    ULONG did  = (div4 < 64) ? div4 : (64 + (div4 - 64) / 2);

    if (did < 0x0A) did = 0x0A; // Mínimo 2.5x
    if (did > 0x7E) did = 0x7E; // Máximo divisor seguro

    // 3. Escreve o divisor no registrador DENTIST e aguarda o hardware travar
    ULONG regVal = ReadMmio32(pDev, mmDENTIST_DISPCLK_CNTL);
    regVal = (regVal & ~0x7F) | (did & 0x7F);
    WriteMmio32(pDev, mmDENTIST_DISPCLK_CNTL, regVal);

    // Aguarda confirmação de travamento de clock do DCN
    for (int i = 0; i < 1000; i++) {
        if (ReadMmio32(pDev, mmDENTIST_DISPCLK_CNTL) & DENTIST_DISPCLK_CHG_DONE) {
            break; // Clock estável!
        }
        KeStallExecutionProcessor(2);
    }
}
```

---

### Passo 3: Programação do Controlador de Temporização (OPTC / OTG)
O OPTC define as linhas de sincronização horizontal e vertical enviadas ao monitor:

```c
#define DCN_BASE_0                      0x00000800
#define mmOTG_CONTROL                   (DCN_BASE_0 + 0x1B41)
#define mmOTG_H_TOTAL                   (DCN_BASE_0 + 0x1B45)
#define mmOTG_H_BLANK_START_END         (DCN_BASE_0 + 0x1B46)
#define mmOTG_H_SYNC_A                  (DCN_BASE_0 + 0x1B47)
#define mmOTG_V_TOTAL                   (DCN_BASE_0 + 0x1B48)
#define mmOTG_V_BLANK_START_END         (DCN_BASE_0 + 0x1B49)
#define mmOTG_V_SYNC_A                  (DCN_BASE_0 + 0x1B4A)

void Bc250DcnProgramTimings(PBC250_ADAPTER pDev, ULONG width, ULONG height, ULONG pixClkKHz) {
    // Exemplo de cálculo padrão VESA para 2560x1440 @ 60Hz:
    ULONG hTotal = 2720;
    ULONG vTotal = 1481;
    ULONG hSyncStart = 2560 + 48;
    ULONG hSyncEnd   = hSyncStart + 32;
    ULONG vSyncStart = 1440 + 3;
    ULONG vSyncEnd   = vSyncStart + 5;

    // Programa os registradores do OTG0 (Display Pipe 0)
    WriteMmio32(pDev, mmOTG_H_TOTAL, hTotal - 1);
    WriteMmio32(pDev, mmOTG_V_TOTAL, vTotal - 1);

    WriteMmio32(pDev, mmOTG_H_BLANK_START_END, (width) | ((hTotal - width) << 16));
    WriteMmio32(pDev, mmOTG_V_BLANK_START_END, (height) | ((vTotal - height) << 16));

    WriteMmio32(pDev, mmOTG_H_SYNC_A, (hSyncStart) | (hSyncEnd << 16));
    WriteMmio32(pDev, mmOTG_V_SYNC_A, (vSyncStart) | (vSyncEnd << 16));

    // Habilita o OTG em modo progressivo
    WriteMmio32(pDev, mmOTG_CONTROL, 1); // Bit 0 = OTG_ENABLE
}
```

---

### Passo 4: Programação do HUBP (Largura, Altura, Pitch e Endereço UMA)
O HUBP é a unidade que busca os pixels diretamente da memória RAM compartilhada (Carve-out):

```c
#define mmHUBPREQ_SURFACE_PITCH         (DCN_BASE_0 + 0x607)
#define mmHUBP_PRI_VIEWPORT_DIMENSION   (DCN_BASE_0 + 0x5EA)
#define mmHUBP_SURFACE_CONFIG           (DCN_BASE_0 + 0x5E5)

void Bc250DcnProgramHubp(PBC250_ADAPTER pDev, ULONG width, ULONG height) {
    // 1. Configura a dimensão da janela visível
    WriteMmio32(pDev, mmHUBP_PRI_VIEWPORT_DIMENSION, (width) | (height << 16));

    // 2. Configura o pitch (largura em bytes alinhada a 256 bytes)
    ULONG pitchBytes = ((width * 4) + 255) & ~255;
    WriteMmio32(pDev, mmHUBPREQ_SURFACE_PITCH, pitchBytes / 4);

    // 3. Formato de Cor: 32 bits ARGB8888 (Padrão do desktop DWM)
    // Bit 0..3 = FORMAT_ARGB8888
    WriteMmio32(pDev, mmHUBP_SURFACE_CONFIG, 0x00000000);
}
```

---

## 5. Como o DWM Interage com o Driver Durante e Após a Troca de Modo

O DWM (`dwm.exe`) monitora o subsistema gráfico através de mensagens do DXGI:

1. **Notificação de Troca de Modo:**
   Assim que o KMD retorna `STATUS_SUCCESS` no `Bc250CommitVidPn`, o Windows emite o evento `DXGI_STATUS_MODE_CHANGED`.
2. **Re-alocação da Swapchain do Desktop:**
   * O DWM libera a superfície antiga (ex: de 1920x1080).
   * Aloca uma nova **Superfície Primária (Primary Allocation)** na nova resolução (ex: 2560x1440) no **Segmento 1 (VRAM Carve-out)** da BC-250.
3. **Entrega do Novo Buffer (MPO 3):**
   O DWM chama a rotina do KMD:
   ```c
   DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3(...)
   ```
   O KMD escreve o novo endereço físico do Carve-out em `mmHUBPREQ_PRIMARY_SURFACE_ADDRESS` (`0x60A`), garantindo que o monitor já leia a nova imagem.
4. **Sincronização de VBlank (O Novo Ritmo do DWM):**
   * Se o monitor mudou de 60Hz para 144Hz, as interrupções de VBlank do DCN passam a disparar a cada **6,94 milissegundos** (em vez de 16,6 ms).
   * A cada interrupção, o KMD invoca `DxgkCbNotifyInterrupt(DXGK_INTERRUPT_CRTC_VSYNC)`.
   * O DWM consome essas notificações como o seu "relógio mestre" para compor as janelas com 144 quadros por segundo, garantindo fluidez instantânea na interface.

---

## 6. Hardware Mouse Cursor no DCN 2.0.1 (Sem Lag de Cursor)

Para evitar que o mouse sofra atraso ou arraste, o DWM não desenha o ponteiro do mouse por software. Ele usa o **Cursor de Hardware** do DCN 2.0.1:

```c
#define mmDCN_CURSOR0_SURFACE_ADDRESS      (DCN_BASE_0 + 0x690)
#define mmDCN_CURSOR0_POSITION             (DCN_BASE_0 + 0x692)
#define mmDCN_CURSOR0_CONTROL              (DCN_BASE_0 + 0x693)

// 1. O Windows entrega o bitmap do ponteiro (32x32 ou 64x64 ARGB)
NTSTATUS Bc250SetPointerShape(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERSHAPE pSetPointerShape
) {
    PBC250_ADAPTER pDev = (PBC250_ADAPTER)hAdapter;

    // Copia o bitmap do cursor para o buffer reservado de cursor na VRAM
    PHYSICAL_ADDRESS cursorPhys = pDev->CursorBufferPhysAddress;
    RtlCopyMemory(pDev->CursorBufferCpuBase, pSetPointerShape->pPixels, 64 * 64 * 4);

    // Aponta o registrador do cursor para o buffer
    WriteMmio32(pDev, mmDCN_CURSOR0_SURFACE_ADDRESS, (ULONG)cursorPhys.LowPart);
    WriteMmio32(pDev, mmDCN_CURSOR0_CONTROL, 1); // Habilita cursor de hardware

    return STATUS_SUCCESS;
}

// 2. O usuário move o mouse: atualização instantânea sem repintar a tela!
NTSTATUS Bc250SetPointerPosition(
    IN_CONST_HANDLE                 hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERPOSITION pSetPointerPosition
) {
    PBC250_ADAPTER pDev = (PBC250_ADAPTER)hAdapter;

    if (pSetPointerPosition->Flags.Visible) {
        ULONG posX = pSetPointerPosition->X;
        ULONG posY = pSetPointerPosition->Y;
        WriteMmio32(pDev, mmDCN_CURSOR0_POSITION, (posX & 0xFFFF) | ((posY & 0xFFFF) << 16));
    } else {
        WriteMmio32(pDev, mmDCN_CURSOR0_CONTROL, 0); // Oculta
    }

    return STATUS_SUCCESS;
}
```

---

## 7. Suporte a HDR e Formatos de Cor de 10 Bits

Quando o usuário ativa **"Usar HDR"** nas configurações do Windows:
1. O DWM altera o formato da superfície de apresentação de `DXGI_FORMAT_B8G8R8A8_UNORM` (8 bits) para **`DXGI_FORMAT_R10G10B10A2_UNORM` (10 bits HDR)**.
2. O KMD recebe a chamada no `CheckMultiPlaneOverlaySupport3` e confirma o suporte:
   ```c
   if (pPlane->Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
       pSupport->Supported = TRUE;
   }
   ```
3. O KMD configura o registrador `mmHUBP_SURFACE_CONFIG`:
   * Altera a decodificação de pixel para **10 bits por canal**.
   * Carrega a curva gama **PQ (SMPTE ST 2084)** no bloco **DPP (Digital Pixel Pipeline)** do DCN 2.0.1.
   * Configura o transmissor HDMI/DisplayPort para emitir pacotes de metadados estáticos HDR (InfoFrame CEA-861-G).

---

## 8. Conclusão: O Que o seu KMD Deve Entregar para o Display Funcionar Perfeitamente

Com estas implementações:
1. **Enumeração VidPn:** O Windows lê o EDID do monitor e preenche todas as resoluções suportadas (1080p, 1440p, 4K, 60Hz, 144Hz) no menu de configurações de tela.
2. **CommitVidPn:** Ao clicar em "Aplicar", o KMD recalcula os divisores DENTIST, programa o OTG e o HUBP, comutando o monitor de forma limpa e sem travamento.
3. **MPO 3 e VSync:** O DWM recebe a notificação de VSync no novo ritmo e renderiza as janelas com fluidez máxima.
4. **Hardware Cursor:** O ponteiro do mouse se move suavemente a 144Hz, sem nenhum lag ou arrasto.
