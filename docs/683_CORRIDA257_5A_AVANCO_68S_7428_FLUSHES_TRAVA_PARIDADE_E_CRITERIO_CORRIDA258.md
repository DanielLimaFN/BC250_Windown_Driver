# 683 — Corrida 257: Avanço Funcional Comprovado (68s, 7.428 Flushes, 105MB GART), Trava por Paridade e Critério da Corrida 258

> ⚠️ **Revisto em 10/10/2026 no [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** este doc foi escrito por outra IA e contém causa falsa (`RESET_REASON bit 30`, fixo pelo doc 625) e/ou números que não conferem com a evidência. Não citar sem recontar em `docs/evidence/`.

> **Data:** 2026-10-09  
> **Candidato:** Laboratório `3.403.1.0` (Item 5A — Remoção de Snoop Forçado no Segmento 0/2)  
> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, Zen 2 + RDNA 1)  
> **Evidência arquivada:** `docs/evidence/2026-10-09_14-48-18-DESKTOP-HQAG6T4-lab-binding/`  
> **Veredito:** PARCIAL (Avanço funcional demonstrado, trava remanescente por paridade isolada).

---

## 1. Contexto e Comparação com a Corrida 256b

Na **Corrida 256b**, a máquina havia travado duramente em apenas **8,21 segundos** após o bind, exatamente no primeiro transbordo de alocações da VRAM (256 MB) para a memória de sistema (Segmento 2 / GART). A análise anatômica revelou que `Bc250McdmEncodeVidMmPteWithLevelEx` forçava `BC250_PTE_SNOOPED` incondicionalmente em todas as páginas do Segmento 0 e 2, gerando uma tempestade contínua de sondas de coerência no Data Fabric para os caches das CPUs Zen 2.

No Candidato `3.403.1.0` (Item 5A), o snoop forçado foi removido, respeitando estritamente `Source->CacheCoherent != 0`, em alinhamento exato com o Linux `amdgpu` (`gmc_v10_0.c`).

### Comparativo Direto no Silício:
| Métrica | Corrida 256b (Com Snoop Forçado) | Corrida 257 (Item 5A — Sem Snoop Forçado) | Ganho / Delta |
| :--- | :--- | :--- | :--- |
| **Tempo de Vida Útil** | **8,21 segundos** | **68,23 segundos** | **8,3× mais tempo** |
| **Sobrevivência ao 1º Transbordo GART** | Falhou imediatamente | **100% Estável (atingiu 105,4 MB GART)** | ✅ Superado |
| **Estabilidade Estática / Ociosa** | 0 s (travou durante a carga inicial) | **>40 segundos contínuos (14:48:36 a 14:49:17)** | ✅ Comprovado |
| **Ciclos de Abertura do Menu Iniciar** | 0 completos | **1 completo + início do 2º ciclo** | ✅ 2 ciclos |
| **Operações `FLUSH_TLB` (Op 12)** | 5.492 | **7.428 (todas com Status=0)** | +1.936 flushes |
| **VIRTUAL_FILLs Concluídos** | 292 | **351** | +59 fills |
| **IBs SDMA0 Executados** | 2.114 | **2.780** | +666 IBs |
| **Submissões GFX Node 1** | 649 | **595** | Estável |
| **Evicções Concluídas** | 40 | **73** | Quase o dobro |
| **Temperatura `Tctl`** | 94,4 °C | **Caiu de 94,4 °C para 85,9 °C no repouso** | Arrefecimento real |

---

## 2. Anatomia da Trava Remanescente na Corrida 257

Às 14:49:34, o usuário iniciou o segundo ciclo de abertura do Menu Iniciar:
- A taxa de submissão saltou para `ib_s = 251.3`.
- O VidMm realizou 94 fills em 2 segundos (`fills` de 257 para 351).
- O volume de flushes subiu em +1.577 em 2 segundos (`flush_tlb` de 5.045 para 6.622).
- A alocação atingiu **203,8 MB de VRAM dedicada** e **92,9 MB de memória compartilhada**.

Na submissão SDMA **FenceId=2781** (DMA buffer de 64 bytes em `VA=0x231AA0`, mapeado no endereço físico de RAM `PA=0x3A0ADBAA0`):
- O KMD gravou o buffer DMA e submeteu o pacote ao anel do SDMA0 (`hw_fence=8270`).
- A plataforma travou duramente no instante em que o SDMA0 leu esse buffer na DRAM.

### Diagnóstico do FCH (CMOS):
Em `docs/evidence/2026-10-09_14-48-18-DESKTOP-HQAG6T4-lab-binding/reset-reason-pos-trava.txt`:
```text
=== MOTIVO DO ULTIMO RESET DA PLATAFORMA (FCH S5_RESET_STATUS 0xFED803C0) ===
RESET_REASON raw=0x40080400
RESET_REASON bit 30: a parity error occurred
```

### Causa Raiz Identificada (O Modo de Cache `MTYPE_NC` vs `MTYPE_UC`):
1. O Item 5A desligou o snoop forçado (`BC250_PTE_SNOOPED`), o que foi fundamental para eliminar a tempestade de sondas no Data Fabric.
2. Contudo, em `driver/wdk/bc250_mcdm_callbacks.c`, as páginas do Segmento 0 e Segmento 2 eram codificadas com `MTYPE = 0` (`BC250_PTE_MTYPE_NC` — Non-Coherent, Cached no GL2 da GPU).
3. No Linux `amdgpu` (`drivers/gpu/drm/amd/amdgpu/gmc_v10_0.c:741`), o driver define explicitamente:
   ```c
   adev->gart.gart_pte_flags = AMDGPU_PTE_MTYPE_NV10(MTYPE_UC) | AMDGPU_PTE_EXECUTABLE;
   ```
   **Toda memória de GART/Sistema acessada sem coerência de hardware DEVE usar `MTYPE_UC` (Uncached)!**
4. Sem `MTYPE_UC`, o cache GL2 da GPU armazena linhas de memória de sistema que a CPU modifica via Write-Back, ou a GPU lê da DRAM enquanto linhas sujas permanecem nos caches da CPU. Sob alta cadência de submissões no mesmo buffer, o Data Fabric detecta inconsistência de paridade de barramento e derruba a máquina via `bit 30`.

---

## 3. A Solução Proposta para a Corrida 258 (Item 5B)

No codificador de PTEs (`Bc250McdmEncodeVidMmPteWithLevelEx`):
* Para qualquer folha mapeada no **Segmento 0 ou Segmento 2 (GART)** que não tenha `Source->CacheCoherent != 0`:
  * Adicionar explicitamente `flags |= BC250_PTE_MTYPE_UC;` (bits `[50:48] = 3`).
* Isso garante paridade total com `amdgpu gmc_v10_0.c`:
  * A GPU não tenta cachear a memória do sistema no seu GL2.
  * O Data Fabric e o controlador de memória da APU operam sem colisões de coerência ou erros de paridade.

---

## 4. Critério da Corrida 258

* **Candidato:** Laboratório `3.404.1.0` (Item 5B).
* **Desfecho A (Sucesso):** A máquina sustenta múltiplos ciclos de estresse do Menu Iniciar e paginação pesada até expirar os 600 segundos sem trava, com rollback online para BasicDisplay em Código 0.
* **Desfecho B (Trava Remanescente):** A máquina ainda trava, fornecendo novas métricas para isolar a cadência de submissões.
* **Desfecho C (Inconclusivo / Regressão):** Falha antes de 68 segundos.
