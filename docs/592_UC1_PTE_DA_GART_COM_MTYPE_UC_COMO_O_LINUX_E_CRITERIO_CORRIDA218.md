# 592 — UC.1: PTE de RAM da GART com MTYPE_UC, como o Linux; critério da Corrida 218

**Data:** 05/10/2026 · **Candidato:** lab **3.366.1.0** · **Base:** [doc 591](591_CORRIDA217_VMID0_UNICA_OK_D2_CAI_COM_FENCE_INVISIVEL_NA_RAM_E_TRAVA_DURA_AOS_4MIN.md)
Trabalho offline. **NÃO VALIDADO EM HARDWARE.**

## 1. A comparação com o amdgpu

Fontes locais do upstream: `gmc_v10_0.c` e `sdma_v5_0.c` (cópia da sessão de 28/09), `inc/linux_ref/gfxhub_v2_0.c` e `inc/bc250dbg_abi.h`.

| Ponto | Linux (P1) | Nosso (3.365) | Igual? |
|---|---|---|---|
| Pacote FENCE do SDMA | `SDMA_OP_FENCE` com `MTYPE(0x3)` UC (`sdma_v5_0_ring_emit_fence`) | `OP_FENCE` com `3u << 16` (UC) | sim |
| Sincronização antes da fence | `emit_mem_sync`: GCR que **só invalida** (GLM/GL1/GLV/GLK/GL2 INV) | GCR antes da fence | equivalente |
| `GCMC_VM_MX_L1_TLB_CNTL.MTYPE` | UC (`gfxhub_v2_0_init_tlb_regs`) | `0x1D59`, campo no bit 11 = 3 (UC) | sim |
| **PTE de memória de sistema na GART** | `gart_pte_flags = MTYPE_UC \| EXECUTABLE` (`gmc_v10_0.c:738`), mais VALID\|SYSTEM\|SNOOPED\|READ\|WRITE (`amdgpu_gart_bind`) | `0x77`: **MTYPE = 0 (NC)** | **não** |

**Leitura (H, com mecanismo coerente com o P0 da 217):**
- Com MTYPE NC, a escrita da GPU na RAM pode ficar no GL2.
- Como o GCR antes da fence só invalida, nada força a fence a chegar à RAM; ela só aparece quando a linha é despejada do GL2.
- Isso explica as três observações:
  - os 117 IBs que retiraram, por despejo do GL2 sob tráfego de paginação;
  - o timeout com o anel drenado e a fence ainda no poison;
  - o `RPTR_Wb_RAM` divergente já na ativação da D2 (24 contra 40 no registrador).
- O Linux nunca depende de despejo, porque toda PTE da GART é UC.

## 2. A correção (UC.1)

- `BC250_GCVM_GART_PTE_SYSTEM_FLAGS` passa de `0x77` para `BC250_GCVM_GART_PTE_MTYPE_UC | 0x77ull`, ou seja, `0x0003000000000077` (`bc250_gcvm_global_mmu.h`).
- Uma variável só. As entradas do MEC (VRAM, `0xE1`) e a página dummy (`0x61`) não mudam. A PTE do segmento 2 também não muda (ver §4).
- **Teste:** `gcvm_global_mmu_test` confere a PTE com MTYPE_UC nos bits 50:48 e o endereço intacto.
- **Portão:** `UC.1` dentro de `audit_vmid0_single_table_and_split_progress`.
- **Mutação:** voltar para `0x77` reprova a auditoria e a suíte (`gcvm_global_mmu_test`, linha 274).
- `build-contract` EXIT=0; build de laboratório "TODOS OS TESTES APROVADOS (0 falhas)"; auditorias de segurança e do pacote: PASS. O imediato `0x0003000000000077` está no SYS assinado.

| Arquivo | SHA-256 |
|---|---|
| SYS | `4FD6B2AD1BD57BC6F0DA0899D64E016D4C0C5111037CEF4F7E73388C76B55614` |
| INF | `B3B9A7567437B4156BD69B5414F46A92AAA7F024222B4E7D1DBE1FF98A7DF88C` |
| CAT | `E8A5AA46C8B3F62334C604B63C8FBB9C885BFB81D418886BAF59E47F9A2FB978` |
| UMD | `8782A5E5…` (inalterada); RADV `80EBB248…` (inalterada) |

**Scripts:** `tools/prepare-corrida218-d2-gart-mtype-uc.ps1` e `tools/EXECUTAR_CORRIDA218_D2_GART_MTYPE_UC_600S.cmd`.

## 3. Critério da Corrida 218 (Lei 10)

**Pré-condição de validade:**
- `[D2_GART] TABELA ... PTE[32]=0x3...077` (MTYPE_UC visível no valor);
- as duas linhas `MEC (...) na tabela da GART` com `divergentes=0`;
- `[D2_ANEL_SISTEMA] ATIVO`.

Desfechos:

- **A — a D2 sobrevive à carga.**
  - *Assinatura:* zero `IB_TRIPOD TIMEOUT`, sem `anel de volta na VRAM`, e o anel na RAM até o fim.
  - *Implicação:* a fence invisível era o MTYPE NC. O critério de trava do doc 587 passa a valer: com a D2 de pé, 600 s sem trava dura apontam o anel e as fences em RAM como diferença relevante frente ao Linux; trava dura com a D2 de pé tira a D2 das causas suficientes.
- **B — a D2 sobrevive, mas há trava dura.**
  - *Assinatura:* A até a trava.
  - *Implicação:* anel e fence em RAM não bastam. Próximo candidato: conclusão por interrupção (doc 582).
- **C — a D2 cai de novo com a mesma assinatura** (anel drenado, fence no poison, foto intacta).
  - *Implicação:* o MTYPE não era a causa, ou não era a única. Próximo passo: a leitura pela CPU (RAM `MmNonCached` contra o GTT cacheado com snoop do Linux) e a flag SNOOPED.
- **D — a D2 cai com outra assinatura** (fault, registradores ou PTEs divergentes).
  - *Implicação:* aplica-se o doc 588 §5.
- **E — fora do mapa:** descrever antes de julgar.

**Condição do rollback:** se a janela chegar ao fim, o rollback do watchdog fecha no mesmo boot, sem o laço, com o log parando de crescer e `RECOVERY_OK_ONLINE`. É o primeiro teste da LC.1.

## 4. Fora desta faixa, registrado

A PTE do segmento 2 (memória de sistema da RADV) também é `0x77`, com MTYPE NC. No Linux isso equivale ao padrão para GTT de usuário (`AMDGPU_VM_MTYPE_DEFAULT` → NC). Mas um BO criado com `COHERENT`, `EXT_COHERENT` ou `UNCACHED` vira UC (`gmc_v10_0_get_vm_pte`). Se a RADV usa memória coerente para fences e sincronização no segmento 2, as escritas da GPU podem ficar presas no GL2 do mesmo jeito. **H**, a investigar depois da 218. Não mexi, para manter uma variável por corrida.

## 5. Pendências

- **Antes do bind:** recuperação, reset do fusível e boot frio.
- **NÃO VALIDADO EM HARDWARE:**
  - a UC.1;
  - a D2 estável;
  - a LC.1 no teardown;
  - 600 s sem trava com 256 MB.
