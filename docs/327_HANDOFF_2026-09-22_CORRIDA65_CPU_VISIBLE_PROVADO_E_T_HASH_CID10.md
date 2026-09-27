# Handoff Corrida 65 — 22/09/2026 (10:52:21)

> **Candidato testado:** lab **3.226.1.0** (faixa DX.18)
> **SYS SHA-256:** `F2A3F42C3823D592CAB428ED9497EB457A85C854A676C5CFCBB2C70971DB614D`
> **Pasta de evidência:** `docs/evidence/2026-09-22_10-52-21-DESKTOP-HQAG6T4-lab-binding/`
> **Critério prévio:** [docs/326_CRITERIO_CORRIDA65_CPU_VISIBLE_RESOURCE_E_DESCRITOR_FIX.md](326_CRITERIO_CORRIDA65_CPU_VISIBLE_RESOURCE_E_DESCRITOR_FIX.md)

---

## 1. Resumo Executivo e Veredito

- **Houve avanço?** **SIM, avanço funcional e diagnóstico decisivo.**
  - **Avanço funcional:** A falha de lock `0x80070057` (`pfnLockCb` failure em `BC250_ALLOCATION_TOKEN_CLASS_RESOURCE`) foi **100% ELIMINADA**. Todos os uploads do DWM via VRAM visível pela CPU passaram com `hr=0x00000000`.
  - **Avanço funcional:** O bypass da cópia meta via Vulkan compute funcionou com sucesso comprovado (`ResourceUpdateSubresourceUP: textura sincronizada via WDDM allocation upload; bypass de Vulkan compute copy`).
  - **Avanço funcional:** O anel gráfico físico `CP_RB0` (Node 1 GFX) consumiu e **aposentou 10 submissões 3D no silício** sob `VMID 1` com fences avançando de 1 a 10.
  - **Avanço diagnóstico:** O fault `Cid=10` não ocorreu mais no upload de textura, mas reapareceu quando o shader executou e dereferenciou o descritor de textura `T#` via `PsSetShaderResources` / `[VK_DESCRIPTOR] BOUND`.
- **Veredito frente ao critério de docs/326:** **PARCIAL.** Meta 1 (CPU-visible resource lock e bypass de compute copy) atingida; Meta 2 (eliminação completa do stall `Cid=10`) parcial, isolando o mecanismo exato do descritor `T#`.

---

## 2. Métricas e Controles Medidos (P0)

| Controle | Corrida 64 (3.225.1.0) | Corrida 65 (3.226.1.0) | Observação |
|---|---:|---:|---|
| **`pfnLockCb` em RESOURCE** | Falhou (`0x80070057`) | **SUCESSO (`0x00000000`)** | **100% resolvido** em 2 MiB, 8.9 MiB, etc. |
| **Bypass Vulkan Meta Compute** | Inativo | **ATIVO e PROVADO** | Texturas atualizadas via VRAM direta |
| **GFX Node 1 Aposentadas** | 0 / 6 | **10 / 11** | Fences 1..10 aposentadas no silício |
| **`IB EXECUTADO (INDIRECT)`** | 1.723 | **1.710+** | Execução SDMA sob VMID 8 estável |
| **Faults drenados / sem latch** | 1 / 6 | **1 / 6** | `Cid=10` drenado em `0x2441` |
| **BugCheck** | 0 | **0** | Nenhum BSOD |
| **Watchdog Rollback** | `RECOVERY_OK_ONLINE` | **`RECOVERY_OK_ONLINE`** | BasicDisplay Código 0 em 5/5 amostras |
| **D3D11 Native Harness** | Quadro 1 fail | **Quadro 1 fail** | Reset causado pelo TDR do DWM |

---

## 3. Análise Detalhada dos Achados

### 3.1 Upload de Alocações RESOURCE em VRAM Visível pela CPU (Provado)
No log `bc250_umd_log_9940.txt` (DWM):
```
10:52:30.036 [pid=9940 dwm.exe] ResourceUpdateSubresourceUP: textura copiada linha-a-linha (590x800 bpp=4 SrcPitch=2360 DstPitch=2560)
10:52:30.037 [pid=9940 dwm.exe] [WDDM_WINSYS] Upload ENTER: hAlloc=0x40002f80 Bytes=2048000
10:52:30.038 [pid=9940 dwm.exe] [WDDM_WINSYS] Upload LEAVE: hr=0x00000000 Bytes=2048000
10:52:30.039 [pid=9940 dwm.exe] ResourceUpdateSubresourceUP: textura sincronizada via WDDM allocation upload (590x800); bypass de Vulkan compute copy
```
As chamadas de `pfnLockCb` para buffers e texturas com token class `BC250_ALLOCATION_TOKEN_CLASS_RESOURCE` foram aceitas pelo VidMm sem falhas, comprovando a eficácia da alteração em `bc250_active_miniport.c` e `bc250_allocation_abi_contract.c`.

### 3.2 O Mecanismo do `Cid=10` Isolado no Descritor `T#`
O KMD capturou no TDR:
```
[TDR_GCVM_FAULT] DECODIFICADO Status=0x0010153C -> Vmid=1 WalkerError=6 MappingError=1 PermissionFaults=0x3 Cid=10 Rw=0 MoreFaults=0 | FaultAddrPagina=0x0000000000002441 -> GpuVa=0x2441000
[TDR_GCVM_FAULT] CANDIDATO <<20 = 0x244100000 DENTRO da faixa viva Base=0x243F80000 Size=0x1F4000 Offset=0x180000 Proc=0000002F0000002F Alloc=000000D5000000D5 Token=0x5245530026D40018 Nome=RESOURCE Seg=1 (LOCAL_VRAM) SegOff=0x1325000
```
- A alocação `Token=0x5245530026D40018` é a textura `590x800 bpp=4` com base `0x243F80000`.
- O offset `0x180000` corresponde à página `0x244100000`.
- Em hardware RDNA/GFX10, `ac_build_texture_descriptor` codifica o descritor de imagem `T#` com `desc[0] = va >> 8` (`0x244100000 >> 8 = 0x2441000`).
- Quando o Pixel Shader do DWM tenta amostrar a textura através de `desc[0]`, a GCVM recebe a requisição de translação em `0x2441000` (porque o shader ou cache lê o campo sem recompor `<< 8`, ou o cliente `Cid=10` acessa `desc[0]` como endereço plano), batendo em `PDE=0`.

---

## 4. Próximos Passos
1. Investigar a composição do descritor `T#` em `bc250_umd_descriptors.c` e na ponte Vulkan/RADV para identificar como o sampler do Pixel Shader é configurado para texturas SRV.
2. Manter a integridade de testes e governança conforme `AGENTS.md`.
