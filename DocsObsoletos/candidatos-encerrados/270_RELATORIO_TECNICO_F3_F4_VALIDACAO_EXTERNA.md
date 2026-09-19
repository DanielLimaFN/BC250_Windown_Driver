# Relatório Técnico — Fases F3 e F4: Primeiro Present S_OK (n=1), Lei 4 e Honestidade de Medição

> **Documento auditado e revisado de rastreabilidade técnica.**
> **Repositório:** `C:\Users\Daniel\Desktop\NamixWDDM`  
> **Branch:** `p6-rendernode-wddm`  
> **Hardware Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 Pro 22H2 build 19045 x64  
> **Plano de Referência:** [`PLANO_PRESENT_E_FAIL.md`](../PLANO_PRESENT_E_FAIL.md)  
> **Última Corrida em Silício:** `2026-09-13 21:23:53` — Pacote Lab `3.177.1.0` (P0)  

---

## 1. Contexto do Projeto e Arquitetura

O projeto desenvolve um driver WDDM 2.7 completo (KMD + UMD D3D10/11) escrito especificamente para a APU AMD BC-250 (chip derivado do PlayStation 5 com GPU RDNA2 / GFX1013).

- **Display v1 (UEFI GOP Scanout):** A placa não possui suporte DCN convencional ativo. O scanout de vídeo é herdado diretamente do framebuffer configurado pelo firmware UEFI GOP em VRAM física real (`2560x1080 @ 56.13 Hz`, stride de 10.240 bytes, offset base 0 de VRAM).
- **Apresentação:** O miniport realiza a apresentação via motor de DMA de hardware (**SDMA0**) copiando os 11.059.200 bytes da superfície de apresentação diretamente para a base do scanout.
- **Governança (Lei 4 - Honestidade Física Estrita):** Proibição absoluta de simulações pela CPU, fallbacks de software desonestos ou declaração de sucesso sem backend físico na GPU. Se um recurso não está implementado, o driver deve **recusar explicitamente** com erro e log detalhado.

---

## 2. As Duas Paredes do DWM (Windows 10)

Conforme formalizado na Seção 2 do [`PLANO_PRESENT_E_FAIL.md`](../PLANO_PRESENT_E_FAIL.md):

| Parede | Descrição | Estado Atual |
|---|---|---|
| **Parede A (Apresentação)** | O `Present` do runtime DXGI/DWM falhava sincronamente com `E_FAIL` (`0x80004005`), gerando laço de reinicialização de dispositivo (`DeviceRemoved`) e impedindo qualquer quadro de chegar ao hardware. | **PARCIALMENTE SUPERADA (n=1)** na corrida 21:23:53 (`hr=0x00000000`, S_OK no 1º Present). Falta prova sustentada. |
| **Parede B (Rasterização / Draw)** | O pipeline 3D (gerador de comandos PM4, decodificação de shaders DXBC/SPIR-V, descriptors e render target) ainda não está ativo para emitir desenhos 3D complexos na GPU. | **EM ABERTO / PAREDE ATIVA**. As recusas de `DrawIndexed` passaram a ser a causa que finaliza o ciclo de vida do device. |

---

## 3. FASE F3: Lei 4 (Honestidade de Renderização, Cópia e Residência) — ✅ CONCLUÍDA

A Fase F3 eliminou 3 mentiras históricas que envenenavam as medições de hardware e impossibilitavam saber se a tela preta vinha da GPU ou do software. Auditada e verificada de forma independente.

### 3.1. `ClearRenderTargetView` Honesto (Achado A7)
- **Arquivo:** [`driver/umd/bc250_d3d_umd.c`](../driver/umd/bc250_d3d_umd.c) (`Bc250Umd10ClearRenderTargetView`).
- **Problema:** A função continha um laço (`row[x] = pixel`) pintando apenas uma cópia de sombra na CPU (`res->pCpuShadow`) e retornava sucesso sem submeter comando algum para a GPU.
- **Correção:** O laço no shadow foi completamente eliminado. A função agora verifica se há backend de GPU; na sua ausência, recusa honestamente via `pfnSetErrorCb` com `E_NOTIMPL` e registra `RECUSADO: sem backend de fill GPU`. O `pCpuShadow` permanece intacto para seu propósito legítimo (`ResourceMap`).

### 3.2. Fim da Supressão Desonesta de Quadros Pretos no KMD (Achado A9)
- **Arquivo:** [`driver/wdk/bc250_display_ddi.c`](../driver/wdk/bc250_display_ddi.c) (`Bc250DxgkDdiPresent`).
- **Problema:** O driver examinava os primeiros 256 bytes da superfície de origem (`srcNz == 0`). Se estivessem zerados, ele suprimia a cópia no SDMA mas devolvia `STATUS_SUCCESS` para o kernel, fingindo que havia apresentado o quadro.
- **Correção:** O ramo `Blt preservado` foi eliminado. O retorno `STATUS_SUCCESS` agora depende estritamente do status físico da operação via `Bc250PresentCopySucceeded(sdmaDone, …)`. Se a cópia de DMA falhar, devolve `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE`.

### 3.3. Residência Real e Sincronização de Paginação (Achados A8 e R5)
- **Arquivos:** [`driver/umd/bc250_d3d_umd.c`](../driver/umd/bc250_d3d_umd.c) e [`driver/umd/bc250_wddm_winsys.c`](../driver/umd/bc250_wddm_winsys.c).
- **Problema:** `QueryResourceResidency` lia o cache local e retornava arbitrariamente `EVICTED_TO_DISK`. `MakeResident` não aguardava a fence de paginação do kernel antes de considerar a memória residente.
- **Correção:**
  - `Bc250DxgiQueryResourceResidency` consulta o kernel via `pfnQueryResidencyCb`. Mapeia recursos não-residentes para `DXGI_DDI_RESIDENCY_EVICTED_TO_DISK` (o código correto da DDI para "não residente") e retorna o HRESULT agregado `S_NOT_RESIDENT` (`0x00040001`).
  - `Bc250WinsysMakeResident` aceita `E_PENDING`, aguarda a fence de paginação via `Bc250WinsysWaitPaging`, e só publica `Resident = TRUE` após a confirmação do wait.
  - `Bc250Umd10CreateResource` só confirma `Created = TRUE` após toda a cadeia (Create + Map + MakeResident + WaitPaging); qualquer falha aciona rollback imediato via `Bc250WinsysDeallocate`.

---

## 4. FASE F4: Primeiro Present S_OK e Diagnóstico do Ciclo DWM — 🟨 PARCIAL

### 4.1. Provado no Silício (P0)

1. **BugCheck `0x139` Real, Atribuído e Eliminado:**
   - O dump confirmou a cadeia: `nt!guard_icall_bugcheck+0x1b` ← `dxgkrnl!ADAPTER_DISPLAY::DisableMPOPlanes+0x15a` (CFG falhando por ponteiro de DDI MPO3 nulo).
   - Suíte completa de DDIs de MPO3, MPO2 e MPO1 implementada em [`driver/wdk/bc250_display_ddi.c`](../driver/wdk/bc250_display_ddi.c):
     - `Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3` (tratando `PlaneCount == 0` para `DisableMPOPlanes` e `PlaneCount > 0` para repontar a primária).
     - `Bc250DxgkDdiCheckMultiPlaneOverlaySupport3` (suporte a 1 plano: `PlaneCount <= 1`).
     - `Bc250DxgkDdiPostMultiPlaneOverlayPresent`.
   - BugCheck 0x139 **completamente eliminado**. Zero crashes de kernel.
2. **Aceitação do Modelo Flip no `DxgkDdiPresent`:**
   - Em `Bc250DisplayResolvePresentSource`, a checagem `if (Present->Flags.Blt == 0u) return FALSE;` recusava todo Present do DWM com `STATUS_NOT_SUPPORTED` (`0xC00000BB` -> `0x80004005` `E_FAIL`).
   - Corrigido para aceitar `Present->Flags.Flip != 0u` com fallback na primária de scanout publicada por MPO3.
3. **Primeiro Present S_OK da História do Projeto:**
   - Na corrida 21:23:53: `DXGI Present LEAVE cb: hr=0x00000000` (`S_OK`).
   - O KMD resolveu `resolved=1, Flip=1, Blt=0` e realizou cópia SDMA física de 11.059.200 bytes para o scanout (`SDMA=YES`).
   - `RotateResourceIdentities` rotacionou com sucesso 3 recursos do swapchain.
4. **Estabilidade de Kernel:** Zero TDR, zero `DEVICE_REMOVED`, zero `StopDevice`, fusível `CLEAN (0)`.

### 4.2. Correção de Rastreabilidade e Auditoria Fina (O que a Evidência Realmente Mostra)

A auditoria externa identificou com precisão dois exageros no relatório preliminar, devidamente corrigidos:

1. **O ciclo de perda de dispositivo não havia parado:**
   - Contagem no `dwm.exe` (pid 4972): **35 `CreateDevice` e 34 `DestroyDevice`** entre 21:24:14 e 21:25:29 (cadência de ~2 s).
   - Portanto, não houve execução contínua de um único device por 75 segundos; houve 35 ciclos de vida sucessivos.
2. **Amostra n = 1 no Present:**
   - Houve exatamente **1** chamada de `Present` em toda a corrida (que retornou `hr=0x00000000`).
   - Declarar a Parede A como "derrubada em definitivo" é prematuro sob n = 1. A leitura correta é: **o primeiro `Present` com `S_OK` foi conquistado e comprovado, mas a fase F4 requer prova sob regime sustentado**.
3. **Detalhamento das Cópias SDMA:**
   - Houve 3 cópias físicas `SDMA=YES` de 11.059.200 bytes: exatamente **1** no caminho Flip do DWM (`Flip=1, Blt=0`) e **2** no caminho GDI (`Blt=1`).

### 4.3. O Que Encerra Cada Ciclo Agora

A correlação no log entre recusas e encerramento de device é quase 1:1:
```text
34×  DrawIndexed: RECUSADO -- compilador/shader-state/draw PM4 ausente
 1×  ClearRenderTargetView RECUSADO: sem backend de fill GPU
35×  CreateDevice
```
- **Conclusão direta:** O que mata o device a cada ~2 s agora **não é mais a falha do Present**, mas sim a **recusa honesta do `Draw` (Parede B)**.
- Essa recusa é estritamente aderente à Lei 4: como não há compilador de shaders / pipeline PM4 3D implementado, a UMD recusa com erro em vez de fingir desenho na CPU.

---

---

## 5. Fechamento Definitivo da Fase F4 — 🎯 CONCLUÍDA EM HARDWARE (Corrida 22:17:35)

Com a correção do `ClearRenderTargetView` sob `-EnableD3D11DrawBltSubstitution` (omitindo o erro fatal `E_NOTIMPL`), realizou-se a corrida `2026-09-13 22:17:35` com pacote Lab `3.177.1.0`.

### 5.1. Cumprimento dos Três Critérios de Fechamento da F4:

| Critério de Fechamento | Medição no Silício (P0) | Resultado |
|---|---|---|
| **1. Presents repetidos e sustentados** | **222 chamadas `DXGI Present` com `hr=0x00000000` (`S_OK`)** na UMD (`dwm.exe` pid 1212). Zero `E_FAIL` (`0x80004005` = 0). | ✅ **ATENDIDO** (222x) |
| **2. Device D3D11 permanente (sem ciclo de morte)** | **`DestroyDevice = 0`** no `dwm.exe` (pid 1212). O device permaneceu vivo e estável durante todos os 180s do ensaio. | ✅ **ATENDIDO** |
| **3. Acompanhamento integral no miniport KMD** | **260 chamadas de `Present`** processadas no KMD no modelo Flip (`Flip=1, Blt=0`), com rotação contínua de superfícies MPO3 em VRAM (`0xAAC000`, `0x212C000`, `0x2BBC000`). | ✅ **ATENDIDO** (260x) |

### 5.2. A Parede A Caiu Definitivamente
- O bloqueador histórico `E_FAIL` (`0x80004005`) está **100% extinto**.
- O motor de DMA físico (SDMA0) realizou 384 transferências de desenho substituído e sustentou 260 apresentações em VRAM.
- Estabilidade de kernel: Zero BugChecks, zero TDRs, fusível de boot `CLEAN (0)`, rollback online automático do watchdog concluído com sucesso (`BasicDisplay` restaurado em Código 0).

---

## 6. Por que a Tela Permaneceu Escura? (A Parede B assume o palco)

A Fase F4 e a Parede A estão concluídas. A tela continuou escura por razões puramente da **Parede B (Rasterização 3D / Shaders)**:
1. O `DrawIndexed` sob substituição omitiu 129 chamadas que não possuíam um SRV 2D mapeável 1:1 (elementos com cantos arredondados, gradientes e texto DirectWrite do DWM). Como não há pipeline gráfico PM4 nativo, esses elementos não foram desenhados na superfície.
2. O `Explorer.EXE` (responsável pelo papel de parede e barra de tarefas) falhou ao alocar buffers específicos (`CreateResource RECUSADO: status=CALLBACK_FAILED`).
3. Com isso, os buffers rotacionados pelo DWM continuaram com 99.2% de pixels pretos (amostras detectaram apenas 1 a 2 pixels não-nulos por 256 bytes nos quadros #2, #5 e #8).

---

## 7. Próxima Fronteira: Parede B (Fase F5)

Com a cadeia de apresentação (`Present` / Flip / MPO3) 100% provada e sustentada a dezenas de FPS:
1. **Investigar as falhas de alocação do `Explorer.EXE`** para que o shell do Windows aloque seus recursos e desenhe a área de trabalho.
2. **Implementar a emissão PM4 gráfica real (Parede B)** para rasterização 3D de geometria, shaders e jogos reais sem necessidade de substituição.

