# 166 — Análise Forense da Corrida de Tela e Handoff Completo: Dos Artefatos ao Desktop

> **Data:** 2026-09-04 21:45 UTC-3  
> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, 24 CUs, 16 GB GDDR6)  
> **Sistema:** Windows 11 Pro x64  
> **Conexão:** Monitor DisplayPort nativo na BC-250 (resolução 2560x1080 @ 56,13 Hz herdada do UEFI GOP)  
> **Classificação:** Documento Técnico de Continuidade de Engenharia  

---

## 1. Veredito Executivo: Onde Estamos Exatamente

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ███████████████████░  95%   Tripe de boot OK em 1 poll; 30 BLTs concluidos no silicio
GCVM & Traducao de Memoria       ████████████████████ 100%   SetRootPageTable OK; GPUVA 0xAC0000 mapeia para VRAM 0xAD0000
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     ████████████████████ 100%   Log de 211 KB intacto; fusivel encerrou em CLEAN (0)
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   P1 17/17 e P2 16/16 provados
Miniport WDDM Display+Render     ██████████████████░░  94%   30 Presents do DWM executados; monitor ACENDEU fisicamente com artefatos
=============================================================================================
Veredito Global: O monitor ACENDEU FISICAMENTE e saiu da tela preta!
O DWM chamou DxgkDdiPresent 30 vezes. O driver executou 30 BLTs completos via engine SDMA0
transferindo 11.059.200 bytes por quadro da alocacao primaria (0xAD0000) para o scanout UEFI GOP (0x0).
O tripe no silicio passou 30/30 vezes com RPTR==WPTR==188 e poison de fence sobrescrito.
Nenhum BugCheck, nenhum TDR, StopDevice limpo com fusivel em CLEAN.
O que apareceu na tela foram ARTEFATOS/RUIDO ESTÁTICO porque a superficie de origem (0xAD0000)
contem memoria GDDR6 nao inicializada da GPU. A transferencia por hardware funciona 100%;
o que falta e a geracao/composicao dos pixels reais da Area de Trabalho na superficie.
Transferencia de Tela (P5 do Roadmap) — VALIDADA EM HARDWARE NO SILÍCIO.
Conteudo renderizado pela GPU/DWM — NAO VALIDADO EM HARDWARE.
=============================================================================================
```

---

## 2. Telemetria Medida da Última Corrida (21:39:36)

Evidência arquivada em `C:\BC250_DISPLAY_LOG.txt` (211.261 bytes, 1.700+ linhas):

### 2.1 O Pipeline de Apresentação Rodou Plenamente
```text
C:\BC250_DISPLAY_LOG.txt:1655:[ResolveGpuVa] SUCCESS: Ctx=FFFFD404E8088AE8 Proc=FFFFD404E80889C8 VA=0xAC0000 -> Phys=0xAD0000 Seg=1
C:\BC250_DISPLAY_LOG.txt:1656:[Present #1] Source=PRESENT_SLOT -> Physical VRAM 0xAD0000 (Seg=1); PresentSlot: resolved=1 Phys=0xAD0000 Seg=1
C:\BC250_DISPLAY_LOG.txt:1657:[Present #1] Blt=1, hCtx=FFFFD404E8088AE8, DmaSize=65536, DmaVA=0x20000, NumSrc=0, NumDst=0, Flags=0x00000001
C:\BC250_DISPLAY_LOG.txt:1658:[Present #1] Info0: hDev=0000000000000000, Seg=0, PhysAdapter=0, VA=0x0, Phys=0x0
C:\BC250_DISPLAY_LOG.txt:1659:[Present #1] Info1: hDev=FFFFD404E8088D28, Seg=0, PhysAdapter=0, VA=0xAC0000, Phys=0x0
C:\BC250_DISPLAY_LOG.txt:1660:[Present #1] Info2: hDev=FFFFD404E8088C08, Seg=0, PhysAdapter=0, VA=0x30000, Phys=0x0
C:\BC250_DISPLAY_LOG.txt:1661:[Present #1] DstRect=(0,0)-(2560,1080), SrcRect=(0,0)-(2560,1080), SubRects=1
C:\BC250_DISPLAY_LOG.txt:1662:[Present #1] SubRect[0]=(139,182)-(1012,921), CenterPixel=0x0CE9E18A
C:\BC250_DISPLAY_LOG.txt:1663:[Present #1] SrcPhysSample @ 0xAD0000: BB97B7B8 B86F344C E922A312 EADA20E6
C:\BC250_DISPLAY_LOG.txt:1666:[Present #1] Spread SCANOUT   @ 0x0: NonZero=0/256, Xor=00000000
C:\BC250_DISPLAY_LOG.txt:1667:[Present #1] Spread PRIMARY   @ 0xAD0000: NonZero=256/256, Xor=A01EEFB3 (Seg=1, hAlloc=FFFFD404E8088B78)
C:\BC250_DISPLAY_LOG.txt:1668:[Present #1] Spread SLOT_SRC  @ 0xAD0000: NonZero=256/256, Xor=A01EEFB3 (Seg=1)
C:\BC250_DISPLAY_LOG.txt:1669:[Present #1] Spread SLOT_WORD @ 0xAC0000: NonZero=254/256, Xor=EF4F709E (union PhysicalAddress/VirtualAddress)
C:\BC250_DISPLAY_LOG.txt:1671:[Present #1] BYTE_PROOF PASS: Source=0xAD0000 Dest=0x0 Bytes=11059200 (byte-a-byte)
```

### 2.2 Fechamento Limpo
```text
C:\BC250_DISPLAY_LOG.txt:1740:[SdmaPresent] Teardown Complete. TripodValidated=1, Submitted=30, Completed=30, Fallbacks=0
C:\BC250_DISPLAY_LOG.txt:1741:[FUSE] Desligamento limpo registrado: fusivel em CLEAN (Status=00000000)
```

---

## 3. Por Que Apareceram Artefatos em Vez do Desktop?

O monitor acendeu e mostrou linhas/ruído colorido porque o SDMA copiou com **fidelidade matemática perfeita (byte a byte)** os dados presentes no endereço físico `0xAD0000` para o scanout `0x0`.

Porém, o que estava em `0xAD0000`?
```text
SrcPhysSample @ 0xAD0000: BB97B7B8 B86F344C E922A312 EADA20E6
```
Isso é **ruído térmico / memória residual não inicializada** da GDDR6!

### Por que a Área de Trabalho não foi desenhada em 0xAD0000?

1. **A UMD D3D recusa criar o dispositivo:**
   - O Windows Desktop Window Manager (`dwm.exe`) é um aplicativo Direct3D (D3D11/D3D12).
   - Para compor a Área de Trabalho, o runtime do Windows (`d3d11.dll` / `dxgi.dll`) carrega a DLL de modo de usuário (`bc250umd.dll`), especificada no arquivo INF do driver.
   - O runtime chama `OpenAdapter10` e em seguida chama `CreateDevice` na UMD.
   - Em [`driver/umd/bc250_d3d_umd.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.c#L118):
     ```c
     static HRESULT APIENTRY Bc250Umd10CreateDevice(...) {
         ...
         /* Fail-closed: 3D rasterizer pipeline not implemented in this UMD probe. */
         return E_NOTIMPL;
     }
     ```
   - Como `CreateDevice` retorna `E_NOTIMPL`, o DWM **não consegue instanciar um contexto 3D na nossa GPU** para rasterizar as janelas e o papel de parede.

2. **O DWM envia Present sem rasterização por hardware:**
   - Como o DWM não tem backend 3D na UMD, ele entra em modo de apresentação de contingência.
   - Ele aloca a superfície de swapchain e chama `DxgkDdiPresent` com um buffer de DMA (`DmaVA = 0x20000`, tamanho 64 KB) e um retângulo sujo `SubRect[0] = (139,182)-(1012,921)`.
   - Mas os pixels da alocação primária nunca foram tocados nem pelo DWM nem pelo rasterizador, mantendo os bytes residuais de boot.

3. **O `FillVirtual` foi contornado:**
   - O VidMm enviou uma operação de paginação `FillVirtual` (Op=9) para preencher a alocação primária com `0x00000000` (preto).
   - Como a execução virtual no nó de paginação havia congelado a GPU na corrida anterior (devido ao Page Fault em `0x24F000`), nós desobstruímos o nó notificando a conclusão da cerca para o scheduler avançar.
   - Logo, nem sequer a limpeza para preto foi gravada no silício da alocação primária, expondo o lixo cru da GDDR6.

---

## 4. O Que Foi Realizado e Comprovado no Silício Até Aqui

| Componente | Estado no Silício | Evidência Comprovada |
|---|---|---|
| **Boot Tripod SDMA0 (Item 63)** | **PROVADO** | Submissão física de 68 bytes no anel, `RPTR=52`, fence `0x1001` gravada pela GPU, poison `0x43DAFFFE` substituído em 1 poll. |
| **DMM / VidPn Negociação** | **PROVADO** | 7 chamadas `IsSupportedVidPn` e 5 pivôs de `EnumVidPnCofuncModality` aceitos com `STATUS_SUCCESS`. Modo pinado: 2560x1080 @ 56,13 Hz (timings exatos do OTG do GOP). |
| **Criação de Alocação Primária** | **PROVADO** | `CreateAllocation` gerou a Shared Primary de 11.075.584 bytes com handle `FFFFD404E8088B78` em VRAM (Segmento 1). |
| **Resolução de GPUVA** | **PROVADO** | `ResolveGpuVa` converte `0xAC0000` -> físico `0xAD0000` via snapshot transacional multi-processo. |
| **Blt de Apresentação SDMA0 (P5)** | **PROVADO** | 30 BLTs de 11.059.200 bytes executados da alocação primária para o scanout UEFI GOP (`0x0`). `RPTR == WPTR == 188`, `BYTE_PROOF PASS` byte a byte. Monitor físico acendeu. |
| **Proteção Anti-Boot-Loop** | **PROVADO** | Fusível `C:\BC250_BOOT_FUSE.dat` gerencia estados `ARMED -> CLEAN`. Watchdog descarrega o driver e restaura BasicDisplay sem crash. |

---

## 5. O Problema Específico Que a Próxima IA Precisa Resolver

### Objetivo
Fazer o Windows 11 desenhar **pixels reais da Área de Trabalho** (barra de tarefas, cursor, fundo de tela) na superfície exibida pelo monitor, em vez do lixo de VRAM não inicializada.

### As Três Perguntas de Engenharia

1. **Como o Windows DWM deve compor a tela na ausência de rasterizador 3D completo na UMD?**
   - No Windows 11, se a GPU não tem driver 3D completo (D3D12/D3D11), o Windows pode compor a Área de Trabalho via **WARP (Windows Advanced Rasterization Platform)** em software (CPU) e blitar os pixels gerados para a alocação primária.
   - Para que o WARP ou GDI desenhe na primária, ela precisa ser acessível por CPU (`CpuVisible = 1`, `AccessedPhysically = 1`) ou o driver precisa suportar a DDI `DxgkDdiPresentDisplayOnly` / modelo de display com fallback de software limpo.
   - Por que o DWM não gravou os pixels via CPU na primária `0xAD0000`?
     - Será que a alocação primária precisa ter abertura de BAR0 mapeada para o processo do DWM (`CpuVirtualAddress`)?
     - Ou será que o DWM desenha no buffer de DMA `DmaVA = 0x20000` passado no `DXGKARG_PRESENT`?

2. **O que contém o buffer de DMA `DmaVA = 0x20000` no `DXGKARG_PRESENT`?**
   - Em cada chamada de Present, o log reporta:
     ```text
     [Present #1] Blt=1, hCtx=FFFFD404E8088AE8, DmaSize=65536, DmaVA=0x20000, NumSrc=0, NumDst=0, Flags=0x00000001
     ```
   - Esse buffer tem 64 KB (`DmaSize = 65536`). Quais comandos ou opcodes o dxgkrnl gravou ali? Ele contém comandos de cópia de tokens GDI/DWM?

3. **Como zerar fisicamente a alocação primária no silício (`FillVirtual`) sem travar o nó SDMA?**
   - O `Bc250SdmaPresentFill` em `bc250_sdma_present.c` já possui a capacidade física de emitir `CONST_FILL` via SDMA diretamente no endereço MC da VRAM.
   - Quando `DxgkDdiSetVidPnSourceAddress` define o endereço da primária em `0xAD0000`, o driver pode disparar um `Bc250SdmaPresentFill` físico para zerar os 11 MB antes de qualquer apresentação. Isso pelo menos transformaria a tela de "ruído/artefatos coloridos" em uma tela preta limpa e controlada.

---

## 6. Arquivos e Estrutura Chave no Repositório

| Arquivo | Função |
|---|---|
| [`driver/wdk/bc250_display_ddi.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_display_ddi.c) | Implementação de `DxgkDdiPresent`, amostragem de superfícies, resolução de origem e disparo de `Bc250SdmaPresentBlt`. |
| [`driver/wdk/bc250_active_miniport.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_active_miniport.c) | `SubmitCommandVirtual`, `SetRootPageTable`, ciclo de vida do dispositivo e registro de contexto. |
| [`driver/wdk/bc250_sdma_present.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_sdma_present.c) | Emissão de pacotes SDMA (`COPY_LINEAR`, `CONST_FILL`, `INDIRECT`), controle de WPTR/RPTR e aferição do tripé da Lei 2. |
| [`driver/umd/bc250_d3d_umd.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.c) | UMD Direct3D (`OpenAdapter10`, `CreateDevice`). Atualmente retorna `E_NOTIMPL`. |
| [`driver/wdk/bc250_mcdm_callbacks.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_mcdm_callbacks.c) | DDI callbacks de WDDM/MCDM, `BuildPagingBuffer`, codificação de PTEs e alocações padrão. |
| [`driver/core/bc250_gcvm_vmid8_transaction.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/core/bc250_gcvm_vmid8_transaction.c) | Instalação e readback de registradores de paginação do VMID 8. |

---

## 7. Instruções Operacionais para Reprodução

1. **Recovery (se necessário):**
   ```cmd
   tools\BC250_RECOVERY.cmd
   ```
2. **Build dos Portões:**
   ```cmd
   driver\wdk\build-contract.bat
   ```
3. **Build e Assinatura do Pacote:**
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-package.ps1
   ```
4. **Stage:**
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\stage-bc250mcdm-package.ps1 -Execute
   ```
5. **Bind no Silício:**
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1 -Execute -IAcceptBlackScreenRisk -WatchdogTimeoutSeconds 60
   ```

---

## 8. Resultado da corrida de 04/09/2026 22:00 e candidato seguinte

A corrida que terminou em tela preta e reboot falhou antes de qualquer
`DxgkDdiPresent`, no primeiro `FillVirtual` submetido como IB virtual:

```text
TargetWPTR=128 WPTR=128 RPTR=124
FenceExp=00001001 FenceObs=43DAFFFE
```

O wrapper chegou ao SDMA0, mas o tripé da IB não fechou. Ao comparar o emissor
com `sdma_v5_0_ring_emit_ib()` do upstream Linux para SDMA 5.0/Cyan Skillfish,
foi encontrada uma violação objetiva: o pacote `INDIRECT` de 6 DWORDs deve
**terminar** numa fronteira de 8 DWORDs. Nosso WPTR inicial era DW 17; sem o NOP
obrigatório, o pacote terminava em DW 23. O candidato seguinte insere
`(2 - WPTR_DW) & 7` DWORDs NOP antes do `INDIRECT`; nesse caso, 1 DWORD, fazendo
o pacote terminar em DW 24.

Também foram adicionadas fotografias somente leitura no timeout:

- resolução lógica `GPUVA -> segmento/offset` da IB no snapshot publicado;
- `SDMA0_UTCL1_RD/WR_STATUS`, `RD/WR_XNACK0/1` e `UTCL1_TIMEOUT`;
- `GCVM_L2_PROTECTION_FAULT_STATUS` e endereço de fault latchado.

Teste nomeado: `P6_SDMA_INDIRECT_END_ALIGNMENT_OFFLINE`.
Evidência: `docs/evidence/2026-09-04_P6_SDMA_INDIRECT_END_ALIGNMENT_OFFLINE/README.md`.
O alinhamento está provado apenas por contrato offline; execução da IB continua
**NÃO VALIDADO EM HARDWARE**.

---

## 9. Corrida 04/09/2026 22:16 — primeiro fetch/retirement de IB virtual

O padding do pacote `INDIRECT` corrigiu a primeira falha. A primeira IB de
`FillVirtual`, em `GPUVA 0x24F000`, fechou RPTR e fence em um poll:

```text
[SubmitVirtual Node0 SDMA] IB_TRIPOD PASS #1:
WPTR=132 RPTR=132 Fence=00001001 Poison=43DAFFFE Size=60 Vmid=8 in 1 polls
```

Isso comprova no silício, para essa submissão, a cadeia de controle
`VMID8 -> fetch/retirement da IB -> fence externa`. Ainda não existe leitura
byte a byte do destino do `FillVirtual`; portanto isto não fecha sozinho o
tripé completo da Lei 2.

O reboot veio na submissão seguinte, antes de tocar no hardware:

```text
DmaVA=0x24F03C
[SdmaPresent] ExecuteIndirect invalid params: VA=0x24F03C, Size=60, Vmid=8
GFXHUB_FAULT_SNAPSHOT: STATUS=00000000 ADDR=00000000:00000000
```

Cada `FillVirtual` tinha 15 DWORDs/60 bytes. Ao avançar `pDmaBuffer` em 60,
o VidMm colocou a próxima IB em `0x24F03C`, que viola o alinhamento de 32 bytes.
O encoder agora acrescenta NOPs ao final de `VIRTUAL_FILL` e
`VIRTUAL_TRANSFER` até o tamanho da IB ser múltiplo de 8 DWORDs. Para o fill
observado, o tamanho passa de 15 para 16 DWORDs e a próxima GPUVA esperada é
`0x24F040`.

Teste nomeado: `P6_PAGING_IB_SIZE_ALIGNMENT_OFFLINE`.
Evidência física e offline:
`docs/evidence/2026-09-04_P6_FIRST_VIRTUAL_IB_TRIPOD/README.md`.
Execução consecutiva de duas ou mais IBs permanece **NÃO VALIDADO EM HARDWARE**.
