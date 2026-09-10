# 191 — Handoff: Fundação D3D10/11 e Winsys Comum Concluídos (Offline)

**Data:** 08/09/2026  
**Status:** Implementado e validado offline (`build-umd.bat` EXIT=0, `build-contract.bat` EXIT=0, auditorias PASS)  
**Ambiente:** Windows 10 22H2 x64 | AMD BC-250 (`1002:13FE`, GC 10.1.3)  
**Hardware tocado nesta sessão:** **ZERO** (regras normativas estritas mantidas).

---

## 1. Resumo Executivo

Nesta sessão, avançamos na infraestrutura compartilhada do **B7** (Backend compartilhado D3D10/11 + Vulkan), executando e validando offline:
1. **Opção 2 (Fundação de Adapter & Device D3D10/11)** em [`driver/umd/bc250_d3d_umd.*`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.c):
   - Estruturas de adapter e device para o runtime D3D10/11 (`BC250_UMD_ADAPTER`, `BC250_UMD_DEVICE`).
   - Caching do blob validado `UMDRIVERPRIVATE` no adapter.
   - Ciclo de vida do device conectado ao winsys comum (`Bc250UmdDeviceInit`, `Bc250UmdDeviceTeardown`).
   - Portão estrito de fail-closed: `BC250_D3D10_11_NATIVE_READY` permanece estritamente `0`, com `CreateDevice` retornando `E_NOTIMPL` limpo sem nenhum anúncio de capacidade falso.
   - Preenchimento completo das tabelas de funções de device (`D3D10_1DDI_DEVICEFUNCS` com 105 entradas e `DXGI_DDI_BASE_FUNCS`).
   - 13 testes de contrato passando em [`driver/tests/bc250_d3d_umd_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_d3d_umd_test.c).

2. **Opção 1 (Extensões do Winsys WDDM Comum)** em [`driver/umd/bc250_wddm_winsys.*`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_wddm_winsys.c):
   - Suporte completo a residency: `Bc250WinsysMakeResident` (via `pfnMakeResidentCb`, atualizando `PagingFenceValue`), `Bc250WinsysEvict` (via `pfnEvictCb`), e evicção automática no `Bc250WinsysDeallocate` para alocações residentes.
   - Classificação de primária: `Bc250WinsysAllocationIsPrimary` verificando o token `BC250_ALLOCATION_TOKEN_CLASS_PRIMARY` nos 32 bits superiores (`BC250_ALLOCATION_TOKEN_CLASS_SHIFT = 32`).
   - Suporte a monitored fences: estrutura `BC250_WINSYS_MONITORED_FENCE`, helpers `Bc250WinsysCreateMonitoredFence` (`D3DDDI_MONITORED_FENCE`), `Bc250WinsysDestroyMonitoredFence`, `Bc250WinsysSignalFenceFromCpu`, e `Bc250WinsysWaitForFenceFromCpu` (com fast-path na CPU evitando chamada de kernel quando `*CpuVirtualAddress >= TargetFenceValue`).
   - Suíte de testes de contrato atualizada em [`driver/tests/bc250_wddm_winsys_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_wddm_winsys_test.c): `TestLifecycle`, `TestResidencyAndPrimaryClass`, `TestMonitoredFenceLifecycle` e `TestFailClosed`.

---

## 2. Arquivos Modificados / Criados

| Arquivo | Componente | Papel |
|---|---|---|
| [`driver/umd/bc250_d3d_umd.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.h) | UMD D3D | Definições de adapter, device, winsys embedding e protótipos DDI. |
| [`driver/umd/bc250_d3d_umd.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.c) | UMD D3D | Implementação do adapter blob cache, device lifecycle, populate DDI tables e fail-closed gate. |
| [`driver/umd/bc250umd.def`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250umd.def) | UMD D3D | Exportações dos entry points de adapter e device lifecycle para os testes de contrato. |
| [`driver/umd/bc250_wddm_winsys.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_wddm_winsys.h) | Winsys Comum | Estruturas `BC250_WINSYS_ALLOCATION` (com `Resident`), `BC250_WINSYS_MONITORED_FENCE` e protótipos de residency/sync. |
| [`driver/umd/bc250_wddm_winsys.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_wddm_winsys.c) | Winsys Comum | Implementação de `MakeResident`, `Evict`, auto-eviction, `AllocationIsPrimary`, e lifecycle de monitored fences. |
| [`driver/tests/bc250_d3d_umd_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_d3d_umd_test.c) | Testes UMD | 4 novos testes cobrindo adapter/blob, device foundation, fail-closed gate e population de tabelas D3D10.1/DXGI. |
| [`driver/tests/bc250_wddm_winsys_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_wddm_winsys_test.c) | Testes Winsys | Mock callbacks para residency/fences e 2 novos testes completos (`TestResidencyAndPrimaryClass`, `TestMonitoredFenceLifecycle`). |
| [`driver/umd/build-umd.bat`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/build-umd.bat) | Build UMD | Link do `bc250_wddm_winsys.obj` no `bc250_d3d_umd_test.exe`. |

---

## 3. Resultados dos Testes Offline

1. **`driver/umd/build-umd.bat`**:
   - `UMD RUNTIME CONTRACT: OK (21/21 invariantes de seguranca validadas)`
   - `BC250 D3D UMD CONTRACT TEST: ALL UMD CONTRACT TESTS PASSED (13/13 testes)`
   - `WDDM WINSYS CONTRACT: PASS (callbacks offline; hardware not touched)`
   - EXIT Code: 0.

2. **`driver/wdk/build-contract.bat`**:
   - Compilação dos contratos de kernel (`bc250mcdm.sys`, `bc250compute.sys`).
   - Matriz DDI: 98 anunciadas, 0 falhas, 3 dívidas declaradas, 1 aviso.
   - Auditoria de segurança do MCDM: PASS.
   - EXIT Code: 0.

3. **Auditorias de Segurança Python**:
   - `python tools/audit-bc250mcdm-safety.py` -> **PASS**
   - `python tools/audit-radv-wddm-safety.py --mesa-root C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm` -> **PASS**

---

## 4. Estado Atual e Próximos Passos

Com a Fundação D3D10/11 e o Winsys Comum implementados e validados offline:
1. **Instrumentação de telemetria de alta resolução no RADV**:
   - Já aplicada no fork local (`mesa-radv-wddm`) em `CreateAllocation2`, `MapGpuVirtualAddress`, `MakeResident`, `WaitForSynchronizationObjectFromCpu`.
   - Geometria GPUVA clampada em 34 bits em ambos os repositórios.
2. **Próxima Corrida com Hardware (quando autorizada por Daniel)**:
   - Medir a sequência exata onde `vkCreateDevice` estaciona com telemetria P0.
   - Observar o gate do Node 1 assim que um submit real do scheduler chegar ao KMD.
3. **Pilar B7 subsequente**:
   - Parser e validador DXBC (Rota B: DXBC → SPIR-V → RADV/ACO) para atender o pipeline 3D exigido pelo DWM sem capacidades falsas.
