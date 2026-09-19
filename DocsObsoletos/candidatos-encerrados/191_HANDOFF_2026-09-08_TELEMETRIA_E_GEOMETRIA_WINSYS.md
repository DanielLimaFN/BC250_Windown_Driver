# 191 — Handoff: Geometria GPUVA 34-bit e Telemetria Diagnóstica nos Winsys (B7)

**Data:** 08/09/2026  
**Status do Silício:** Inalterado (sessão estritamente offline, zero corridas físicas).  
**Veredito:** **NÃO VALIDADO EM HARDWARE**.  
**Documentos Relacionados:** [`189_CHECKLIST_ATE_JOGOS_BC250.md`](189_CHECKLIST_ATE_JOGOS_BC250.md), [`190_HANDOFF_PARA_PROXIMA_IA_B7.md`](190_HANDOFF_PARA_PROXIMA_IA_B7.md), [`172_HANDOFF_2026-09-06_20-34.md`](172_HANDOFF_2026-09-06_20-34.md).

---

## 1. Contexto e Enquadramento Epistêmico

O trabalho desta sessão implementa o primeiro bloco offline de **B7** conforme as diretrizes estritas do projeto:

1. **Classificação como Hipótese `H`**: A espera por paging fence dentro de `vkCreateDevice` permanece catalogada como **HIPÓTESE `H`**. O ponto exato do hang observado na corrida de 06/09 20:34 permanece **NÃO VALIDADO EM HARDWARE**.
2. **Geometria de GPUVA**: A geometria do espaço GPUVA foi uniformizada em 34 bits (`0x00000003FFFFFFFFull` = 16 GiB) em ambos os winsys (`NamixWDDM` e `mesa-radv-wddm`).  
   **Ressalva explícita de medição:** O hang da corrida de 06/09 20:34 **não** decorreu da geometria de GPUVA, pois a alocação mapeada medida na ocasião foi `0x200000000` (8 GiB), estritamente dentro dos 34 bits. A correção de geometria fecha uma dívida de modelagem arquitetural, não a causa do hang.
3. **Preservação da ABI**: A estrutura `BC250_ALLOCATION_PRIVATE_V1` (40 bytes) foi mantida intacta, sem expansões prematuras ou campos fictícios.
4. **Telemetria Diagnóstica de Alta Resolução**: Adicionada instrumentação `[RADV_WDDM2] ENTER/LEAVE` antes e depois das operações críticas (`CreateAllocation2`, `MapGpuVirtualAddress`, `MakeResident` e `WaitForSynchronizationObjectFromCpu`), com flush imediato em `stderr`. A telemetria lê e registra o valor real de `CurrentFence` via `FenceValueCPUVirtualAddress` e o `TargetFence` exigido, permitindo distinguir em uma corrida física futura exatamente onde o progresso cessa.
5. **Gate do Node 1**: Mantido inalterado; será tratado quando uma submissão de fato alcançar o KMD.

---

## 2. Modificações Implementadas

### A. Repositório `NamixWDDM`
1. [`driver/umd/bc250_wddm_winsys.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_wddm_winsys.h):
   - Incluído `../core/bc250_adapter_info_contract.h`.
2. [`driver/umd/bc250_wddm_winsys.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_wddm_winsys.c):
   - Macro de logging `BC250_WINSYS_LOG` com `fflush(stderr)`.
   - `Bc250WinsysMapAllocation`: corrigido `args.MaximumAddress` de `0x0000FFFFFFFFFFFFull` para `BC250_ADAPTER_MAX_GPUVA_ADDRESS` (34 bits).
   - Telemetria de alta resolução em `Bc250WinsysAllocate`, `Bc250WinsysMapAllocation` e `Bc250WinsysWaitPaging` (com logging de `TargetFence` e `CurrentFence`).
3. [`driver/tests/bc250_wddm_winsys_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/bc250_wddm_winsys_test.c):
   - Asserções estritas em `TestMapGpuVa` validando `Args->MaximumAddress == BC250_ADAPTER_MAX_GPUVA_ADDRESS` e `0x00000003FFFFFFFFull`.
4. [`docs/189_CHECKLIST_ATE_JOGOS_BC250.md`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/189_CHECKLIST_ATE_JOGOS_BC250.md):
   - Atualizado estado dos itens 2 e 3 de B7 com a geometria corrigida e telemetria instrumentada.

### B. Repositório Externo `mesa-radv-wddm` (`C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm`)
1. [`src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.h`](file:///C:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.h):
   - Adicionado `volatile uint64_t *paging_fence_cpu_va;` em `struct radv_wddm2_winsys`.
2. [`src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.c`](file:///C:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_winsys.c):
   - Em `radv_wddm2_winsys_create`, após `CreatePagingQueue`, armazena `ws->paging_fence_cpu_va = (volatile uint64_t *)create_paging_queue.FenceValueCPUVirtualAddress;` e loga o endereço e valor inicial.
   - Configurado particionamento do heap para `ws->is_bc250`: `_32bit_heap` (4 GiB a 8 GiB), `heap` (8 GiB a 16 GiB, tamanho `0x200000000ull`), e `replay_heap` clampado no limite de 34 bits.
3. [`src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c`](file:///C:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c):
   - Incluído `<stdio.h>`.
   - `radv_wddm2_reserve_va_range`: clampado `MaximumAddress` em 34 bits para `is_bc250`; telemetria `ENTER/LEAVE MapGpuVirtualAddress (reserve)`.
   - `radv_wddm2_bo_create_internal`:
     - Telemetria `ENTER/LEAVE CreateAllocation2` (com `is_bc250`, `size`, `align`, `pdata_size`, `cur_paging_fence`).
     - Clamp do `max` de GPUVA em `0x00000003FFFFFFFFull` quando `ws->is_bc250`.
     - Telemetria `ENTER/LEAVE MapGpuVirtualAddress` (com `hAlloc`, `base_req`, `min`, `max`, `pages`, `map_fence`, `cur_paging_fence`).
     - Telemetria `ENTER/LEAVE MakeResident` (com `hAlloc`, `resident_fence`, `cur_paging_fence`).
     - Telemetria `ENTER/LEAVE WaitForSynchronizationObjectFromCpu (paging)` (com `target_fence`, `cur_paging_fence` antes e depois).
   - `radv_wddm2_bo_from_handle`:
     - Clamp do `max_va` em 34 bits quando `ws->is_bc250`.
     - Telemetria `ENTER/LEAVE` para `MapGpuVirtualAddress`, `MakeResident` e `WaitForSynchronizationObjectFromCpu`.
   - `radv_wddm2_bo_make_resident`:
     - Telemetria `ENTER/LEAVE` para `D3DKMTMakeResident` e `D3DKMTWaitForSynchronizationObjectFromCpu`.
4. [`src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c`](file:///C:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm/src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c):
   - `vk_wddm2_fence_wait`: telemetria `ENTER/LEAVE WaitForSynchronizationObjectFromCpu (fence_wait)` com `fence_h`, `target_fence`, e `cur_fence` lido atomicamente do `value_map`.
5. [`src/vulkan/runtime/vk_wddm2_monitored_fence.c`](file:///C:/Users/Daniel/Desktop/bc250-win/bc250-win/mesa-radv-wddm/src/vulkan/runtime/vk_wddm2_monitored_fence.c):
   - `vk_wddm2_monitored_fence_wait_many`: telemetria `ENTER/LEAVE WaitForSynchronizationObjectFromCpu (monitored_fence)` com contagem e status.

---

## 3. Resultados de Compilação e Verificação Offline

| Componente | Comando | Resultado |
|---|---|:---:|
| Mesa RADV WDDM2 (`vulkan_radeon.dll`) | `build_radv.bat` | **EXIT=0** (7/7 link limpo) |
| Loader e Negociação ICD | `bc250-radv-compute-gate.exe --identity-only` | **EXIT=0** (interface v7) |
| Auditoria RADV WDDM Safety | `python tools\audit-radv-wddm-safety.py` | **PASS** |
| Auditoria MCDM Safety | `python tools\audit-bc250mcdm-safety.py` | **PASS** |
| Suíte de Contratos KMD | `driver\wdk\build-contract.bat` | **EXIT=0** (todas as suítes PASS) |
| Suíte UMD + Winsys | `driver\umd\build-umd.bat` | **EXIT=0** (21/21 UMD, D3D probe e Winsys PASS) |

---

## 4. O que a Próxima Corrida Física Distinguirá

Quando o proprietário do hardware decidir executar uma corrida do harness `test-radv-wddm-compute-tripod.ps1`, a telemetria recém-adicionada gravará em `harness-stderr.txt`:

1. Se `CreateAllocation2` for aceita pelo KMD com a ABI de 40 bytes;
2. Se `MapGpuVirtualAddress` retornar com a VA mapeada e qual o valor de `PagingFenceValue` gerado pelo VidMm;
3. Se `MakeResident` devolver sucesso e qual o `PagingFenceValue` da residência;
4. Ao entrar em `WaitForSynchronizationObjectFromCpu`, qual é o `target_fence` exigido e qual é o `cur_paging_fence` lido instantaneamente da CPU VA da monitored fence:
   - Se o processo parar antes de `LEAVE WaitForSynchronizationObjectFromCpu`, e `cur_paging_fence < target_fence`, fica demonstrado que a engine de paginação não aposentou a fence (confirmando experimentalmente a Hipótese `H`);
   - Se parar em outro ponto anterior (ex.: alocação ou mapeamento), a hipótese `H` estará refutada para aquele ponto;
   - Se `WaitForSynchronizationObjectFromCpu` retornar `STATUS_SUCCESS` e avançar, o bloqueio estará em estágio subsequente de `vkCreateDevice`.
