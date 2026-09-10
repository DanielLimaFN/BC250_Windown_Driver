# Análise da Corrida 08/09/2026 08:44–08:46 (Pacote 3.90.0.0)

## 1. Identidade e Metadados
- **Data/Hora:** 2026-09-08 08:44:37 até 08:46:45 BRT
- **DriverVer:** `09/08/2026,3.90.0.0`
- **Nome OEM:** `oem34.inf`
- **Configuração de Submissão:** `SdmaPlanoAFallback=0` (somente `INDIRECT`, default), `TdrInduceHang=0` (sem hang induzido), `GpuvaLabPromotion=0` (default)
- **Watchdog:** 120 segundos, token `c77cbbcf-0545-4df3-8f0c-51fa048825c8`
- **Desfecho:** Código 0 alcançado, 33 submissões WDDM executadas (0 recusadas, 0 timeouts), VidPn comitado, 31 Presents SDMA executados. Tela preta por ausência do backend D3D10/11 (DWM fail-fast 0x887A0004). Watchdog acionou rollback online com restauração perfeita do BasicDisplay Código 0.

## 2. Métricas no Silício (P0)

| Métrica | Medição nesta Corrida | Status |
|---|---:|:---:|
| Estado do Miniport pós-bind | `Service=bc250mcdm, Status=OK, Code=0` | ✅ PASS |
| Submissões WDDM Node 0 aceitas | **33** | ✅ PASS |
| Submissões não executadas | **0** | ✅ PASS |
| Submissões `IB EXECUTADO (INDIRECT)` | **32** | ✅ PASS |
| Submissões com `TRIPE_PROVADO` completo | **3** (todas as 3 primeiras de paginação) | ✅ PASS |
| Submissões com 2 pernas (RPTR==WPTR + fence) | **30** | ✅ PASS |
| Timeouts / Travamentos de ring | **0** | ✅ PASS |
| BugChecks / BSOD | **0** | ✅ PASS |
| Invalidação de GPUVA no `DestroyProcess` | **2 processos limpos** (zero faixas órfãs) | ✅ PASS |
| Restauração da MMU Global no `StopDevice` | **26 registradores restaurados**, Ack=1 | ✅ PASS |
| Fusível Anti-Boot-Loop | **CLEAN (0)** gravado no desligamento | ✅ PASS |
| Rollback Online do Watchdog | **BasicDisplay restaurado, Code=0** | ✅ PASS |

## 3. Comportamento do Display e DWM
- O KMD executou `CommitVidPn` e `SetVidPnSourceAddress (Addr=0xAB0000, Segment=1)`.
- Foram executados 31 blts de Present por SDMA (`PresentSlot: resolved=1 Phys=0xAB0000 Seg=1`).
- A amostragem de VRAM (`SrcPhysSample @ 0xAB0000`) mediu `NonZero=0/256`, indicando buffer de primária completamente zerado.
- Os logs da UMD (`bc250_umd_log_8068.txt`, etc.) mostram `dwm.exe` requisitando interfaces D3D10/D3D11 (`0x000B0011`, `0x000B000B`, `0x000A0002`).
- Com `BC250_D3D10_11_NATIVE_READY=0`, a UMD devolveu `E_NOTIMPL` honestamente (sem inventar caps 3D inexistentes).
- O DWM fail-fastou com `0xE0464645` / `0x887A0004` (`DXGI_ERROR_UNSUPPORTED`), gerando 2 dumps completos de ~218 MB.
- O watchdog atingiu o timeout de 120s sem confirmação visual e executou o rollback limpo.
