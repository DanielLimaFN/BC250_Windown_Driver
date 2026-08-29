# PONTO DE RETOMADA MESTRE: DRIVER WDDM DISPLAY & COMPUTE AMD BC-250 (CYAN SKILLFISH 2)

> **ATUALIZAÇÃO CRÍTICA — 3.23 APROVADO / CANDIDATO 3.24 PRONTO:** o miniport 3.23 foi vinculado com
> imagem presente, Código 0 e cadeia VidPN/Commit/VSync/Present comprovada.
> A confirmação visual foi aceita e o watchdog foi desarmado. Um snapshot
> posterior confirmou `2560x1080@60`, serviço em execução e ausência de erros
> relevantes no log do Sistema. Este é um smoke test físico de display, não
> uma validação de aceleração, estabilidade prolongada, reboot ou energia.
> Portanto, os percentuais de 100% abaixo são históricos e não representam
> estabilidade de produção. O 3.23 também sobreviveu ao primeiro reboot, foi
> confirmado e depois removido por rollback controlado. O 3.24 foi então
> vinculado, confirmou imagem visível e comprovou `DmmVsyncCount=7` depois de
> Present bem-sucedido. O candidato ativo é o `3.24.0.0 / SchemaVersion 96`,
> ainda **NÃO VALIDADO EM HARDWARE**. Consulte
> [`90_TELA_PRETA_VIDPN_DISPLAY_3.22.0.0.md`](90_TELA_PRETA_VIDPN_DISPLAY_3.22.0.0.md).

> **Data de Atualização**: 28 de Agosto de 2026  
> **Target**: AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)  
> **Sistema Operacional**: Windows 11 Pro x64 (Build 26100)  
> **Objetivo Final**: Driver WDDM completo (`Class=Display`), nativo e estável, operando em **Código 0** com aceleração de hardware e sem tela preta.

---

## 1. Status Geral Consolidado

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pré-WDDM (Lab)        ██████████████████▋░  93%   Estável e Isolado
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Provado em Silício
GCVM & Tradução de Memória       ██████████████████░░  90%   Host PA no Contexto 0
RLC, CP & PM4 Direct Load        ████████████████████ 100%   FENCE VRAM PROVADA!     (+0)
Telemetria & Observabilidade     ███████████████░░░░░  75%   Gate visual aprovado
Compute & Async Queues (MEC/MQD) ██████████████░░░░░░  70%   JT_STAT FF0303 Ativo
Miniport WDDM / Display Completo █████████████████░░░  85%   3.24 ativo; VSync provado

Conformidade BC250_HARDWARE_RULES: 100% | Miniport MCDM Funcional: 95% | Memória/GPUVA: 75%
=============================================================================================
Veredito Global: NÃO VALIDADO EM HARDWARE (display smoke test aprovado; produção pendente)
=============================================================================================
```

---

## 2. A Grande Conquista: Código 0 Oficialmente Alcançado!

Nos testes realizados em `2026-08-28 21:04` e `21:14`, foi registrado no silício em `device-after.json`:

```json
{
    "Name": "BC-250 MCDM Fail-Closed Development Adapter",
    "PNPDeviceID": "PCI\\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\\4&19CAF403&0&0041",
    "Service": "bc250mcdm",
    "Status": "OK",
    "ConfigManagerErrorCode": 0
}
```

O erro de **Código 43 foi completamente superado**. O Windows 11 aceitou a assinatura do driver, carregou o serviço `bc250mcdm.sys`, validou a tabela DDI e marcou o dispositivo com **`Status: OK` (Código 0)** sem triângulo amarelo no Gerenciador de Dispositivos.

---

## 3. As Lições Aprendidas e a Cadeia de Descobertas

### A. Por que dava Código 43 anteriormente?
1. **Falta de callbacks obrigatórias de KMDOD**: O subsistema `dxgkrnl.sys` exige que a tabela `KMDDOD_INITIALIZATION_DATA` contenha ponteiros não-nulos para:
   - `DxgkDdiNotifyAcpiEvent`
   - `DxgkDdiGetScanLine`
   - `DxgkDdiSetPalette`
   - `DxgkDdiControlEtwLogging`
   - `DxgkDdiCollectDbgInfo`
   - `DxgkDdiNotifySurpriseRemoval`
2. **`SetPowerState` retornando `STATUS_NOT_SUPPORTED`**: O driver estava usando o callback do MCDM que rejeitava a mudança de estado para `PowerDeviceD0`. Ao implementar `Bc250DxgkDdiSetPowerState` respondendo com `STATUS_SUCCESS` para D0/D3, o Windows completou a transição de inicialização e alcançou **Código 0**.

### B. Por que dá Tela Preta quando entra em Código 0?
Existem **três causas raízes físicas** identificadas no código que derrubam o sinal de vídeo quando o Desktop Window Manager (DWM) assume a exibição:

1. **Invasão da Memória do Framebuffer pelo PFP Direct Load**:
   - O UEFI GOP aloca o scanout na base do BAR0 (endereço físico `0xC0000000`, offset 0 da VRAM).
   - O monitor do usuário opera em **2560x1080 @ 60Hz**, ocupando **11.059.200 bytes (~11 MB)** contíguos de VRAM.
   - Em `bc250_pfp_direct_load.c` (linha 42 e 802):
     ```c
     #define BC250_CP_PT_SLICE_OFFSET  0x00107000u /* 1 MB */
     ```
     O código gravava uma tabela de páginas de teste exatamente no offset 1 MB da VRAM, **escrevendo dados de microcódigo por cima dos pixels ativos da tela**.
2. **Invalidação do Contexto 0 (Engine de Display)**:
   - Em `bc250_pfp_direct_load.c` (linhas 789 e 1016):
     ```c
     (void)Bc250PfpWrite(Context, R_GCVM_INVALIDATE_ENG0_REQ, 0x00000001u);
     ```
     O código disparava invalidação do Engine 0 (`GCVM Engine 0 = Display Engine / VMID 0`). No chip BC-250 (que não possui pipeline de exibição DCN dedicado com CRTCs autônomos, dependendo do framebuffer herdado do GOP), invalidar o Engine 0 desliga o scanout de vídeo imediatamente.
3. **Escrita em `R_RLC_CNTL` durante o StartDevice**:
   - `bc250_pfp_direct_load.c` tocava no registrador `R_RLC_CNTL` (0x0003B000). A base de conhecimento do projeto em `referencias/BC250-V2/docs/BASE_DE_CONHECIMENTO.md` adverte categoricamente:
     > *"NUNCA reprogramar GCMC_SYS_APERTURE_LOW/HIGH, invalidar TLBs do motor de display ou tocar em RLC_CNTL durante o arranque normal. Qualquer escrita nessas aperturas altera o mapeamento de onde o controlador de vídeo está lendo os pixels, causando tela preta imediata."*
4. **Requisito de IRQL do VSync (`DxgkCbNotifyInterrupt`)**:
   - `DxgkCbNotifyInterrupt(DXGK_INTERRUPT_DISPLAYONLY_VSYNC)` foi chamado a partir de um temporizador DPC (`DISPATCH_LEVEL`). A especificação Microsoft WDDM determina que `DxgkCbNotifyInterrupt` só pode ser invocado em `DIRQL` (nível de interrupção do hardware) ou sincronizado via `DxgkCbSynchronizeExecution`. Chamar fora de DIRQL provoca bugcheck/freeze silencioso do subsistema gráfico.

---

## 4. O Que Fazer a Seguir (Instruções Claras para a Próxima IA)

### Passo 1: Separar o Display do Bring-up de Testes de Hardware
A inicialização PnP de exibição (`StartDevice`) deve ser **exclusivamente focada em colocar a tela no ar**, exatamente como demonstrado no driver de referência `referencias/BC250-V2/wddm_driver.c`:
1. Chamar `DxgkCbAcquirePostDisplayOwnership` e capturar a geometria exata (`PhysicAddress = 0xC0000000`, `Width = 2560`, `Height = 1080`, `Pitch = 10240`).
2. Mapear o framebuffer via `MmMapIoSpaceEx(..., PAGE_READWRITE | PAGE_WRITECOMBINE)`.
3. **NÃO EXECUTAR** testes agressivos de registradores (`PfpDirectLoad`, `HqdActive`, `TlbFlush`) durante o `StartDevice`.
4. Deixar esses testes e a ativação de anéis assíncronos de compute (SDMA / MEC HQD) para serem disparados sob demanda (via IOCTL / Escape / submissão UMD) ou no serviço isolado `bc250compute.sys`.

### Passo 2: Corrigir a Sincronização do VSync
Em `driver/wdk/bc250_display_ddi.c`, encapsular o disparo de VSync com `DxgkCbSynchronizeExecution`:
```c
static BOOLEAN Bc250SynchronizedVsyncCallback(PVOID SynchronizeContext)
{
    PBC250_DISPLAY_STATE pState = (PBC250_DISPLAY_STATE)SynchronizeContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA data;

    RtlZeroMemory(&data, sizeof(data));
    data.InterruptType = DXGK_INTERRUPT_DISPLAYONLY_VSYNC;
    data.DisplayOnlyVsync.VidPnTargetId = 0;

    pState->DxgkInterface.DxgkCbNotifyInterrupt(pState->DxgkInterface.DeviceHandle, &data);
    return TRUE;
}
```
E na DPC:
```c
g_Bc250DisplayState.DxgkInterface.DxgkCbSynchronizeExecution(
    g_Bc250DisplayState.DxgkInterface.DeviceHandle,
    Bc250SynchronizedVsyncCallback,
    &g_Bc250DisplayState,
    0,
    &syncResult);
g_Bc250DisplayState.DxgkInterface.DxgkCbNotifyDpc(g_Bc250DisplayState.DxgkInterface.DeviceHandle);
```

### Passo 3: Garantir que o `audit-bc250mcdm-failclosed-binding.ps1` passe em Modo Display
O script de substituição do driver (`tools/replace-bc250mcdm.ps1`) executa `audit-bc250mcdm-failclosed-binding.ps1` no final.
Certifique-se de que quando `SkipHwBringup = 1` estiver ativo, o script de auditoria aceite o modo display estável e saia com `exit 0`.

---

## 5. Comandos de Compilação e Teste

```cmd
:: 1. Compilar os contratos e os drivers
cmd.exe /c "driver\wdk\build-contract.bat"

:: 2. Construir e assinar o pacote do driver
powershell -ExecutionPolicy Bypass -File "tools\build-bc250mcdm-package.ps1"

:: 3. Auditar a integridade do pacote gerado
powershell -ExecutionPolicy Bypass -File "tools\audit-bc250mcdm-package.ps1"

:: 4. Instalação e teste no silício (executado pelo Daniel como Administrador)
powershell -ExecutionPolicy Bypass -File "tools\replace-bc250mcdm.ps1"
```

---

## 6. Arquivos Chave para Consulta Imediata
- **Base de Conhecimento Crítica sobre GOP & Tela Preta**: [`referencias/BC250-V2/docs/BASE_DE_CONHECIMENTO.md`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/BC250-V2/docs/BASE_DE_CONHECIMENTO.md)
- **Implementação do Driver KMDOD de Referência**: [`referencias/BC250-V2/wddm_driver.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/referencias/BC250-V2/wddm_driver.c)
- **Display DDI de Produção**: [`driver/wdk/bc250_display_ddi.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_display_ddi.c)
- **Active Miniport (StartDevice & PnP)**: [`driver/wdk/bc250_active_miniport.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_active_miniport.c)
- **Script de Build**: [`driver/wdk/build-contract.bat`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/build-contract.bat)
- **Script de Substituição do Usuário**: [`tools/replace-bc250mcdm.ps1`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/replace-bc250mcdm.ps1)
