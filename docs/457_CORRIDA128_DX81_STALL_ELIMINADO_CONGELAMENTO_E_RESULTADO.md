# 457 — Corrida 128 (DX.81): Stall de Escape Eliminado, Corrida de Threads no Teardown do Explorer e Resultado

> **Retificação após Corrida 129 (25/09):** A sobreposição entre `WaitPaging`, `vkAllocateMemory` e teardown do Explorer é evidência de uma corrida possível, mas os logs não comprovam deadlock nem causa do congelamento global. A DX.82 tentou protegê-la e travou antes, com DWM/Explorer/harness em esperas diferentes. Ver [análise 459](459_CORRIDA129_DX82_TRAVAMENTO_PRECOCE_ANALISE.md). As frases de "causa-raiz" abaixo são interpretação histórica, não veredito comprovado.

> **Data:** 2026-09-25 16:54:07 - 17:30:24  
> **Candidato:** DX.81 / DriverVer `09/25/2026,3.286.1.0` (SYS `6B29F049...`, UMD `0494EE02...`, INF `551515D7...`, CAT `EDC32CA0...`)  
> **Evidência:** [`docs/evidence/2026-09-25_16-54-07-DESKTOP-HQAG6T4-lab-binding`](evidence/2026-09-25_16-54-07-DESKTOP-HQAG6T4-lab-binding)  
> **Critério pré-ensaio:** [`docs/456_CRITERIO_CORRIDA128_DX81_STALL_ESCAPE_E_GPUVA4096.md`](456_CRITERIO_CORRIDA128_DX81_STALL_ESCAPE_E_GPUVA4096.md)

---

## 1. Houve avanço?

**SIM (Avanço Funcional Crítico e Diagnóstico Definitivo da Barreira Histórica de ~3-5 minutos).**

1. **Stall de Escape 100% Eliminado:** Na Corrida 127 o escape linear demorava 4.172 s e falhava com `E_FAIL`. Na Corrida 128, a cópia 2560x1080 via escape levou **4 milissegundos** (`hr=0x00000000 ResultCode=0x00000000`).
2. **DWM Estável e Contínuo sem Quedas:** O `dwm.exe` (pid 1216) executou por **3 minutos e 34 segundos contínuos**, gravando **45.023 linhas de log**, 333 flushes, 1.694 draws, sem nenhum reinício ou laço de falha. A imagem distorcida/glitched da foto anterior (causada por crash loop do DWM sobre GOP congelado) foi 100% curada.
3. **D3D11 Conformance Harness 60/60:** Completou 60/60 quadros com `Exit 0`, centro vermelho `0xFF7F4040` preservado, clear azul `0xFF2673E5` intacto, desmontou seu device com sucesso e encerrou normalmente.
4. **Isolamento da Barreira Histórica de 3 a 5 Minutos:** O congelamento às 16:57:48 foi rastreado com precisão de microssegundos em `bc250_umd_log_1252.txt` (explorer.exe): uma corrida de threads entre `WaitPaging` / `Bc250UmdVkCreateResource` em uma thread de trabalho e `Bc250UmdVkBridgeShutdown` (`vkDestroyDevice` / `FreeLibrary`) na thread de destruição de device secundário do Explorer.

---

## 2. Sucessos Comprovados

- **Performance do Escape Linear (KMD SDMA Blt):**
  - Resolução: 2560x1080 (11.059.200 bytes).
  - Tempo de execução medido: **4 ms** (contra 4.172 ms da Corrida 127).
  - Status: `hr=0x00000000 ResultCode=0x00000000`.
- **DWM 100% Estável em Renderização Acelerada:**
  - 45.023 linhas no log `bc250_umd_log_1216.txt`.
  - 333 chamadas de `Bc250Umd10Flush`.
  - 1.694 draws com shaders compilados e executados via Vulkan Bridge.
  - Zero crashes, zero laços de device-lost.
- **Harness D3D11 Nativo:**
  - 60 quadros apresentados sem falhas (`Present aceito: 60 | Presents falhos: 0`).
  - Gradiente/pixels: `NonZero=28800/28800`, `Center(640,360)=0xFF7F4040`.
  - Desmontagem limpa (`Bc250UmdDeviceTeardown concluido`).
  - Exit code final: 0.
- **Recuperação e Rollback R1:**
  - Após reset manual do congelamento às 17:30:03, o guardião R1 entrou em ação, removeu o pacote experimental limpo e confirmou `BasicDisplay` Código 0 em 5/5 amostras consecutivas sem reboot adicional.

---

## 3. Falhas e Análise Causa-Raiz (O Congelamento às 16:57:48)

Às 16:57:48, aos 214 segundos de teste (~3 min 34 s), a interface congelou totalmente.

### Anatomia da Falha Concorrente em `explorer.exe` (pid 1252):

```text
16:57:48.681 [pid=1252] CreateResource: res=00000000113D7E70 Dim=1 Fmt=0 Size=4096 GpuVa=0x200197000
16:57:48.685 [pid=1252] [WDDM_WINSYS] MakeResident LEAVE: hr=0x8000000A PagingFence=7010
16:57:48.685 [pid=1252] [WDDM_WINSYS] WaitPaging ENTER: TargetFence=7010 CurrentFence=7008 SyncObj=0x80009040
                       --> Thread A bloqueia no kernel esperando a paging fence 7010.

16:57:48.689 [pid=1252] D3D10 DestroyDevice: pDrvPrivate=0000000005389FA0, device=0000000005389FA0
16:57:48.689 [pid=1252] Bc250UmdDeviceTeardown: device=0000000005389FA0 iniciando teardown
16:57:48.896 [pid=1252] [WDDM_WINSYS] DestroyPresentContext ENTER: hContext=000000000456C970
16:57:48.909 [pid=1252] [WDDM_WINSYS] DestroyPresentContext LEAVE: hr=0x00000000
16:57:48.910 [pid=1252] [VK_BRIDGE] Bc250UmdVkBridgeShutdown ENTER: DeviceActive=1
                       --> Thread B entra no teardown da VkBridge, chama vkDeviceWaitIdle, destroi pools e DLL.

16:57:48.985 [pid=1252] [WDDM_WINSYS] WaitPaging LEAVE: hr=0x00000000 TargetFence=7010 CurrentFence=7010
                       --> Thread A acorda da paginacao. Como DeviceActive e VkBridgeInitialized ainda eram TRUE,
                       continua a execucao de CreateResource!
16:57:48.986 [pid=1252] [VK_RES] Bc250UmdVkCreateResource: BUFFER size=4096 bytes (bindFlags=0x4)
16:57:48.986 [pid=1252] [VK_RES] Buffer MemoryRequirements: reqSize=4096, memTypeIndex=2
                       --> Thread A chama vkAllocateMemory no VkDevice exatamente enquanto Thread B destroi o VkDevice
                       ou descarrega a DLL (FreeLibrary)!
                       --> HANG TOTAL: Deadlock/corrupcao de heap no loader Vulkan ou espera infinita.
```

Essa concorrência explica por que corridas longas (108, 109, 110, 111, 112, 127, 128) invariavelmente paravam por volta dos 3 aos 6 minutos: esse é exatamente o momento em que componentes auxiliares do shell (thumbnails do Explorer, pré-visualizações da barra de tarefas, assistentes em background) terminam seu ciclo de vida e destroem seus devices Direct3D secundários.

---

## 4. Veredito

**PARCIAL.**
- O objetivo central da DX.81 — eliminar o stall de escape e estabilizar o DWM em tela completa — foi 100% atingido.
- A meta de 600 segundos ininterruptos falhou aos 214 segundos devido à colisão de threads no teardown de devices secundários.

---

## 5. Recuperação e Próximos Passos (Candidato DX.82)

- **Driver Ativo:** `BasicDisplay` (Código 0) restaurado com sucesso.
- **Próximo Passo (DX.82):**
  1. Adicionar sincronização exclusiva (`SRWLOCK DeviceLock`) em `BC250_UMD_DEVICE`.
  2. No início de `Bc250UmdDeviceTeardown`, invalidar imediatamente `DeviceActive = FALSE` e `VkBridge.DeviceActive = FALSE`, e adquirir `AcquireSRWLockExclusive(&pDevice->DeviceLock)` para drenar qualquer thread de trabalho em andamento antes de destruir recursos Vulkan.
  3. Em `Bc250Umd10CreateResource`, adquirir `AcquireSRWLockShared(&device->DeviceLock)` e revalidar `DeviceActive` antes de qualquer chamada Vulkan pós-WaitPaging.
  4. Em `Bc250UmdVkDestroyResource` e funções da bridge, abortar imediatamente se `!pBridge->DeviceActive || pBridge->Device == VK_NULL_HANDLE`.
  5. Empacotar DX.82 e executar a Corrida 129 para cruzar os 600 segundos.
