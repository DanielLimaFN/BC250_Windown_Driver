# 205 — Handoff 2026-09-08 15:18 · B2 fechado no silício · o tripé executou e isolou `E_PENDING`

> **Corrida:** 08/09 15:18 (`2026-09-08_15-18-36-DESKTOP-HQAG6T4-lab-binding`)  
> **Candidato:** Lab `3.94.1.0` (SYS `8CA949DC`)  
> **Flags:** `-EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod -WatchdogTimeoutSeconds 180`  
> **Resultado:** Código 0 · B2 fechado (P0) · Tripé UMD Node 1 executou e isolou o bloqueador · tela preta  
> **Evidência arquivada:** `docs/evidence/2026-09-08_15-18-36-DESKTOP-HQAG6T4-lab-binding/`

---

## 1. B2 Fechado no Silício: Prova Física P0 da Parede Única

No handoff 204 §3.1 a ressalva que mandava no veredito era:
> *"Nenhum `BC250_UMD_LOG_*.txt` foi criado nesta corrida, apesar de o dwm.exe ter rodado e morrido oito vezes... se nesta corrida o OpenAdapter chegou a ser consultado antes do fail-fast é INDETERMINADO."*

A tarefa B2 implementou a limpeza pré-bind e o arquivamento automático dos logs UMD (`C:\Windows\Temp\BC250_UMD_LOG_*.txt`) antes do rollback. Na corrida de 15:18, os logs foram capturados intactos (`bc250_umd_log.txt` de 43 KB e logs individuais por PID).

No `bc250_umd_log_3704.txt` (`dwm.exe`, PID 3704):
```text
15:18:48.644 [pid=3704 dwm.exe] === bc250umd.dll CARREGADA ===
15:18:48.644 [pid=3704 dwm.exe] OpenAdapter10_2: Interface=0x000B0011, Version=0x00060000
15:18:48.645 [pid=3704 dwm.exe] CAPS_PROBE: desarmado (HKLM\SOFTWARE\BC250\Umd\D3D11CapsProbe)
15:18:48.645 [pid=3704 dwm.exe] OpenAdapter10_2: D3D10/11 nativo indisponivel -> E_NOTIMPL
15:18:48.645 [pid=3704 dwm.exe] OpenAdapter10_2: Interface=0x000B000B, Version=0x00020009
15:18:48.646 [pid=3704 dwm.exe] OpenAdapter10_2: D3D10/11 nativo indisponivel -> E_NOTIMPL
15:18:48.646 [pid=3704 dwm.exe] OpenAdapter10: Interface=0x000A0002, Version=0x00010000
15:18:48.647 [pid=3704 dwm.exe] OpenAdapter10: D3D10 nativo indisponivel -> E_NOTIMPL
```

**Veredito de Engenharia (P0):**
O fato de o DWM fail-fastar (`0xe0464645` / `0x887A0004`) por causa da recusa da UMD (`E_NOTIMPL` deliberado por `BC250_D3D10_11_NATIVE_READY=0` via Lei 4) é agora **PROVADO NO SILÍCIO COM EVIDÊNCIA DE HARDWARE**.
Não é mais hipótese nem P2. A telemetria UMD no perfil lab está 100% operacional.

---

## 2. O Harness do Tripé do Node 1 Executou no Silício

O executável `tools/bc250-umd-node1-tripod.exe` foi disparado pelo binder durante a janela de Código 0 (PID 788):

```text
15:18:42.378 [pid=788 bc250-umd-node1-tripod.exe] === bc250umd.dll CARREGADA ===
15:18:42.379 [pid=788 bc250-umd-node1-tripod.exe] OpenAdapter10_2: Interface=0x000B0011 ... -> E_NOTIMPL
15:18:42.380 [pid=788 bc250-umd-node1-tripod.exe] OpenAdapter10_2: Interface=0x000B000B, Version=0x00020009
15:18:42.380 [pid=788 bc250-umd-node1-tripod.exe] NODE1_TRIPOD_AUTHORIZED: processo e token conferidos; single-shot armado
15:18:42.383 [pid=788 bc250-umd-node1-tripod.exe] UMDRIVERPRIVATE: blob OK -- v1, 104 bytes, Vendor=0x1002 Device=0x13FE, NodeCount=2 (SDMA=0, Compute=1), GpuVaBits=34, PageSize=4096, VramSegId=1 VramBytes=0xF531000, GateFlags=0x0000001B
15:18:42.383 [pid=788 bc250-umd-node1-tripod.exe] OpenAdapter10_2: Interface=0x000B000B -> S_OK (BC250 Native)
15:18:42.385 [pid=788 bc250-umd-node1-tripod.exe] CalcPrivateDeviceSize: Interface=0x000A0002 -> 5568 bytes
15:18:48.493 [pid=788 bc250-umd-node1-tripod.exe] D3D10 CreateDevice: INTERFACE ACORDADA=0x000A0002
15:18:48.493 [pid=788 bc250-umd-node1-tripod.exe] Bc250UmdDeviceInit: device inicializado (Winsys=OK)
15:18:48.493 [pid=788 bc250-umd-node1-tripod.exe] NODE1_TRIPOD_AUTHORIZED: processo e token conferidos; single-shot armado
```

E no `harness.stderr.txt`:
```text
[WDDM_WINSYS pid=788] Allocate ENTER: Token=0x434D440003140001 Size=65536 Align=4096
[WDDM_WINSYS pid=788] Allocate LEAVE: hr=0x00000000 hAlloc=0x40000180
[WDDM_WINSYS pid=788] MapAllocation ENTER: hAlloc=0x40000180 Min=0x10000 Max=0x3FFFFFFFF Pages=16 Exec=1
[WDDM_WINSYS pid=788] MapAllocation LEAVE: hr=0x8000000A VA=0x11000 PagingFence=7001
CommandBufferInit: MapAllocation falhou (CALLBACK_FAILED)
tripod UMD nao fechou; log=C:\Windows\Temp\BC250_UMD_LOG_788.txt CreateDevice=0x80004001
```

---

## 3. Diagnóstico da Falha: O Falso Erro em `0x8000000A` (`E_PENDING`)

### O que o Windows fez
Em WDDM 2.0+, a especificação do `pfnMapGpuVirtualAddressCb` define expressamente:
> *"MapGpuVirtualAddress queues a request to the video memory manager and returns to the user mode driver immediately while the request is processed. The request is queued on the device paging queue and the user mode driver must ensure it synchronizes against the returned device paging fence value."*

O runtime do DirectX / VidMm atribuiu com sucesso o endereço virtual `VA = 0x11000`, enfileirou a paginação das 16 páginas na fila de paginação (`hPagingQueue`), atribuiu a `PagingFenceValue = 7001` e retornou `hr = 0x8000000A` (`E_PENDING` / `STATUS_PENDING`).

### O que a nossa Winsys fez
Em `driver/umd/bc250_wddm_winsys.c`:
```c
hr = Context->Callbacks->pfnMapGpuVirtualAddressCb(Context->RuntimeDevice, &args);
if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
```
Como `0x8000000A` tem o bit 31 ligado (`FAILED(0x8000000A)` avalia como `TRUE`), a função tratou o enfileiramento assíncrono padrão do WDDM como falha fatal (`BC250_WINSYS_CALLBACK_FAILED`), abortando a inicialização do buffer de comando.

---

## 4. Correções Implementadas (Faixa B7.1, Offline)

1. **`Bc250WinsysMapAllocation`**:
   Aceita expressamente `E_PENDING` (`(HRESULT)0x8000000AL`), salvando `VirtualAddress` e `PagingFenceValue`:
   ```c
   if (FAILED(hr) && hr != (HRESULT)0x8000000AL /* E_PENDING / STATUS_PENDING */) {
       return Bc250WinsysRememberFailure(Context, hr);
   }
   ```
2. **`Bc250WinsysMakeResident`**:
   Aceita expressamente `E_PENDING` (`(HRESULT)0x8000000AL`), atualizando `PagingFenceValue` quando emitida:
   ```c
   if (FAILED(hr) && hr != (HRESULT)0x8000000AL /* E_PENDING / STATUS_PENDING */) {
       return Bc250WinsysRememberFailure(Context, hr);
   }
   ```
3. **`Bc250WinsysWaitPaging`**:
   Se `Allocation->PagingFenceValue == 0u`, retorna `BC250_WINSYS_OK` imediatamente (operação já síncrona/sem cerca pendente).
4. **Testes Unitários**:
   Adicionada a suite `TestPagingPendingStatus()` em `driver/tests/bc250_wddm_winsys_test.c`, validando os caminhos de `E_PENDING` e subsequente espera pela cerca de paginação.

---

## 5. Próximo Passo

Submeter a próxima corrida com o pacote lab atualizado (3.94.2.0):
Com `E_PENDING` aceito, o `CommandBufferInit` sucede, `Bc250UmdRunNode1ProofTransaction` aloca e mapeia a memória de destino, escreve os canários e dispara o comando PM4 no Node 1 sob watchdog.
