# 554 — M1b.1: RADV no segmento 2, pronta e dormente; e a correção do heap de 16 GB

**Data:** 01/10/2026 · **Tipo:** offline, sem corrida, **fora do candidato da 199**
**Antecede:** [doc 496](496_M1B_BLOQUEIOS_E_ROTA_REBAR_MAIS_M2.md) (três bloqueios), [doc 551](551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md) §2.3 (M1b em ordem), [doc 552](552_COLETA_LINUX_256MB_AMDGPU_ATIVO_GABARITO_E_O_QUE_MUDA.md) §3.1 (FUSION).

---

## 1. Correção: o heap local da RADV não era ~251 MB, era 16 GB fictício

No doc 552 escrevi que, sem `FUSION`, o heap "local" da nossa RADV era só a VRAM (~251 MB). **Está errado.**

Em `radv_wddm2_fill_gpu_info`, o bloco da BC-250 fixa:
- `props2.vram_vis_size = 16 GB`;
- `vram_inv_size = 0`.

Daí:
- `ac_fill_memory_info` vê toda a "VRAM" visível;
- `radv_physical_device_init_mem_types` publica um heap `DEVICE_LOCAL` de **16 GB** e um heap GTT.

O fonte (22/09) é anterior à DLL em uso (30/09), então a DLL contém isso. Classificação: P1, por leitura. O log da corrida só confirma "2 heaps", sem tamanho.

**Consequência:** a RADV promete 16 GB locais e o KMD tem ~245 MB no segmento 1. A recusa vem do KMD/VidMm, não do heap. A conclusão do 552 sobre o `FUSION` (P1, por leitura de `ac_gpu_info.c:516`) continua válida.

## 2. O que foi feito (M1b.1), tudo atrás de interruptores em 0

| Peça | Onde | O que faz | Interruptor |
|---|---|---|---|
| Tamanhos reais e FUSION | RADV `radv_wddm2_winsys.c` (`radv_wddm2_query_segment_sizes`) | Lê `CommitLimit`/`Aperture` de cada segmento por `D3DKMTQueryStatistics` (d3dkmthk.h). Ordem do nosso KMD: 1º local = visível, demais locais = invisíveis, abertura = GTT. Marca `AMDGPU_IDS_FLAGS_FUSION`. Leitura falhou → falha fechada, sem voltar aos 16 GB | `BC250_M1B_RADV_MEMORY` (`radv_wddm2_winsys.h`) |
| Domínio e comando no blob BCA1 | RADV `radv_wddm2_bo.c` | `RADV_DOMAIN_GTT` (0x8) quando a RADV pede só GTT; `RADV_COMMAND` (0x10) para BO de prioridade `RADV_BO_PRIORITY_CS` (inclui o buffer de concatenação que o KMD lê) | o mesmo |
| Contrato | `driver/core/bc250_allocation_abi_contract.*` | Aceita as duas flags **só em token da RADV**; em classe da UMD, recusa na formatação e no parse. Política pura `Bc250AllocationRadvSystemSegmentPolicy` | — (contrato) |
| Aplicação no KMD | `bc250_active_miniport.c`, `CreateAllocation` | Aplica a política só com o interruptor ligado e **sem segmento 3** publicado (256 MB). Log `[M1B_RADV_SISTEMA]` | `BC250_M1B_RADV_SYSTEM_SEGMENT_ENABLED` |
| Buffer linear do Present | UMD `bc250_umd_vulkan_bridge.c` | Pede primeiro `DEVICE_LOCAL\|HOST_VISIBLE\|HOST_COHERENT`; cadeia antiga depois, com o nível declarado no log | `BC250_M1B_PRESENT_DEVICE_LOCAL` |

**Política (fonte única):**

| Alocação | Resultado |
|---|---|
| Classe da UMD | fica no segmento 1 (o KMD as endereça por MC) |
| RADV, comando | fica no 1 |
| RADV, domínio VRAM com CPU | fica no 1 |
| RADV, domínio GTT | `SegSet 0x3`, preferido 2 → 1 |
| RADV, domínio VRAM sem CPU | `SegSet 0x3`, preferido 1 → 2 (transborda) |

O `EvictionSegmentSet` continua 0, pela lição do `0x10E` da T3.8.

## 3. Os três bloqueios do doc 496, revistos pelo código atual

1. **IB gráfico pela VRAM — menor do que o 496 dizia.**
   - O gate físico (`Bc250ActiveValidateGpuVaRange`) só percorre as tabelas de página, que moram sempre no segmento 1, e aceita PTE válida de sistema.
   - A inspeção e a impressão digital do IB são diagnóstico.
   - O que muda o comportamento é o **DX.43**: o KMD lê o DW0 do IB pela VRAM para decidir o IB de aquecimento. Com o IB no segmento 2, `CABECA_ILEGIVEL` e o aquecimento não sai.
   - **Resposta:** `RADV_COMMAND` mantém os BOs de comando no 1. Base P0 de que a GPU executa por PTE de sistema: DMA buffers (07/09) e mais de 5.000 alocações da M1 na Corrida 149, 600 s sem trava.
2. **`COMPOSITE_BLT` de BO da RADV por MC — continua aberto, mas falha fechado.**
   - O resolvedor devolve `Seg=2` e `Bc250SegmentOffsetToMc` recusa.
   - Uma fonte da RADV no segmento 2 vira composição recusada e visível no log, não cópia errada.
   - Medir a contagem é parte do critério da corrida que ligar a M1b.
   - A solução definitiva é cópia por GPUVA.
3. **Buffer linear do Present — resolvido no código** (tabela acima), dormente.

## 4. Provas offline

| Item | Resultado |
|---|---|
| `build-contract.bat` | EXIT=0; `allocation_abi_contract_test` com o bloco novo `TestAbiRadvDomainAndCommand`: flags aceitas em token da RADV, recusadas em RESOURCE e COMMAND da UMD (formatação e blob forjado), bit 0x20 ainda desconhecido, e as cinco linhas da política |
| RADV | `radv_wddm2_winsys.c`, `radv_wddm2_bo.c` e `radv_wddm2_cs.c` compilam com `BC250_M1B_RADV_MEMORY=0` e `=1`, sem aviso, pela linha de compilação do próprio `compile_commands.json`, com objeto fora da árvore. **A DLL em uso não foi religada:** continua `5447A17D…`, a que o preflight da 199 exige |
| `audit_m1b1_radv_system_segment` (novo) | PASS. **4 de 4 mutações reprovam:** sem a exclusão de COMANDO; sem a guarda do segmento 3; KMD ligado sozinho (pega a UMD **e** a RADV divergentes); formatação aceitando a flag em classe da UMD |
| Auditoria de segurança | PASS |

## 5. O que falta antes de ligar (ordem)

1. **Cópia por GPUVA** para composição de fonte no segmento 2 (bloqueio 2), ou medir primeiro quantas composições caem nele.
2. **Robustez da paginação**, que a M1b vai exercitar muito mais:
   - a corrida do `0x34` (197);
   - `Upload LOCK 0x80070057`;
   - `Offer/ReclaimResources` sem backend (Lei 4).
3. Ligar os **três** interruptores juntos (o portão exige), subir o DriverVer, recompilar a RADV, conferir os hashes e escrever o critério A–E.

**Atenção:** o fonte do KMD agora difere do pacote 3.345.1.0 assinado. É só o contrato e o bloco dormente; o comportamento é o mesmo. Qualquer novo build de laboratório precisa subir o DriverVer para 3.346.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
