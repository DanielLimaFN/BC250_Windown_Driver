# 667 — P5.0: desenho da tabela de páginas de 4 níveis como o `amdgpu`, e a régua de FILLs

**Data:** 08/10/2026. **Natureza:** offline, desenho para revisão do Daniel antes do código (passo 1 da tabela de próximos passos). Mais o passo 5 (régua de FILLs), já pronto.

## 1. O que o `amdgpu` faz neste chip (P1, `inc/linux_ref`)

- **Tamanho da VM:** `gmc_v10_0.c:934`: `amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48)`, ou seja VM de 256 TB (**48 bits**), **bloco de 9 bits**, `num_level` 3 (raiz PDB2 + PDB1 + PDB0 + PTB = **4 níveis**). O dmesg do doc 552 confirma: `vm size is 262144 GB, 4 levels, block size is 9-bit`.
- **Contextos 1–15** (`gfxhub_v2_0.c:286-322`):
  - `PAGE_TABLE_DEPTH = num_level` (3);
  - `PAGE_TABLE_BLOCK_SIZE = block_size - 9` (0);
  - os 7 `*_PROTECTION_FAULT_ENABLE_DEFAULT` = 1;
  - `START = 0`, `END = max_pfn - 1` = 2^36 - 1 (páginas de 4 KB em 48 bits).
- **PDE** (`gmc_v10_0_get_vm_pde`):
  - o endereço vira PA (`vram_mc2pa`) quando não é PTE nem sistema;
  - **sem translate_further no gfx10**: o campo só é setado no caminho do gmc_v9, então o `if (!translate_further) return` sai cedo. Nenhum BFS nem TF nos PDEs.
- **L2/TLB do GFXHUB** (`gfxhub_v2_0_init_cache_regs`/`_tlb_regs`), no ramo sem translate_further: `BANK_SELECT 9`, `BIGK_FRAGMENT 6`, MX_L1 com `MTYPE UC`.

## 2. O que nós fazemos

| Item | Nós | `amdgpu` | Igual? |
|---|---|---|---|
| `GCVM_L2_CNTL` | `0x00080E01` (L2 on, fragment off, default page out, identity 1) | mesmos campos | **sim** |
| `GCVM_L2_CNTL3` | `BANK_SELECT 9`, `BIGK 6` | 9 / 6 (sem translate_further) | **sim** |
| `GCVM_L2_CNTL4`/`5` | `PDE/PTE_PHYSICAL 0`, `SMALLK 0` | idem | **sim** |
| `GCMC_VM_MX_L1_TLB_CNTL` | `0x1D59` (L1 on, mode 3, ADM 1, MTYPE UC) | idem | **sim** |
| Níveis | **2**: raiz de 13 bits (8.192 PDEs, 64 KB) + folha de 9 bits | **4**: 9 + 9 + 9 + 9 bits | **não** |
| VA | **34 bits** (16 GB) | **48 bits** | **não** |
| `PAGE_TABLE_DEPTH` / `BLOCK_SIZE` | 1 / 0 | 3 / 0 | **não** (depth) |
| PDE com PA, sem BFS/TF | sim (Q5 do doc 167; tripwire DX.13) | sim | **sim** |

**Conclusão do comparativo (P1):** a configuração de L2/TLB já está em paridade. A diferença estrutural que sobra no GFXHUB é **só a forma da árvore**: dois níveis com uma raiz larga, contra quatro níveis estreitos. Com dois níveis, cada caminhada da GPU lê a raiz de 64 KB e um PTB. Com quatro, lê PDB2→PDB1→PDB0→PTB, cada um de 4 KB, que é o padrão que o L2 do walker foi feito para cachear (PDE0 cache).

**Se isso causa a trava, é H.** Não há prova de que a raiz larga faça mal; o que há é que é a última diferença de forma no caminho da trava.

## 3. O que muda no código (P5.1)

| # | Onde | Mudança |
|---|---|---|
| 1 | `driver/core/bc250_gpuva_space_contract.h` | `LEVEL_COUNT 4`; todos os níveis com 9 bits; `VA_BITS 48` (12 + 4×9); `ROOT_MAX_ENTRIES 512`; `TRANSLATABLE_LIMIT 2^48` |
| 2 | `bc250_mcdm_callbacks.c` (`QUERYPAGETABLELEVELDESC`) | Devolve 9 bits / 4 KB para os níveis 0..3, em vez de "folha × raiz" |
| 3 | UpdatePageTable (GPU_VIRTUAL `bc250_mcdm_callbacks.c:~1530` e CPU_VIRTUAL `bc250_active_miniport.c:~17186`) | `levelBits` passa a 9 em todo nível; o encoder de PDE vale para os níveis 1..3 (mesmo formato no gfx10) |
| 4 | `bc250_gcvm_vmid8_transaction.c` (CONTEXTn_CNTL) | `PAGE_TABLE_DEPTH = 3`; `END_ADDR = 2^36 - 1` |
| 5 | Journal de GPUVA (`bc250_gpuva_journal_contract.c`) | Limite de VA 2^48 |
| 6 | PT_WALK de diagnóstico e tripwire de PDE (`bc250_active_miniport.c:~13399, ~17466`) | Caminhada de 4 níveis ou desligada; o tripwire passa a valer para os níveis 1..3 |
| 7 | Blob privado da UMD (`GpuVaBits`) e RADV | Sai do runtimeCaps (fonte única, A11). **RADV conferida:** `radv_wddm2_winsys.c:919-935` fixa os heaps da BC-250 entre 8 e 16 GB. Isso é um subconjunto dos 48 bits, então funciona sem mudança e **a RADV `B4AE2E14` fica**. Usar VA acima de 16 GB é melhoria futura, não pré-requisito |
| 8 | SOMBRA.1 | Volta a 0 |
| 9 | Auditoria | Portão novo: geometria em fonte única (níveis × bits = VA − 12), DEPTH = níveis − 1, END coerente com VA; mutações |
| 10 | Testes offline | Casos de 4 níveis no UpdatePageTable (limites por nível), QAI por nível, CNTL/END calculados |

**Não muda:**
- a VMID 0 (GART da D2 e do MEC, `DEPTH 0`);
- o MMHUB/IH;
- a escrita de PTE pelo SDMA (4c.4/P4.2), que escreve por endereço sem conhecer o nível;
- o L2/TLB, que já está em paridade.

**Custo de memória:** o VidMm passa a alocar PDB2/PDB1/PDB0 de 4 KB cada, por processo e por faixa usada. Fica na casa de dezenas de KB por processo, contra a raiz de 64 KB de hoje. É menos.

## 4. Riscos

1. O VidMm com 4 níveis e 48 bits monta tabelas por outro caminho (`UpdatePageTable` em níveis 1..3). Se o encoder de PDE tiver um bug escondido pelos 2 níveis, aparece como fault de tradução. O tripwire e os faults ligados ajudam a pegar.
2. A RADV continua abaixo de 16 GB (item 7). O VidMm, porém, pode pôr alocações dele (paginação, primária) em VA acima de 16 GB. Todo ponto nosso que ainda assumir 34 bits (journal, resolução de VA, PT_WALK) precisa ser varrido antes do pacote.
3. **Regressão de boot** (Código 43) se o QAI e o CNTL ficarem incoerentes. A auditoria do item 9 existe para isso.

## 5. Critério da Corrida 252 (rascunho, final junto com o pacote)

**Pré-condição:**
- `[McdmQAI] GPUMMUCAPS: VaBits=48, Levels=4`;
- `PAGETABLELEVELDESC` com Bits=9 nos quatro níveis;
- o CNTL do contexto lido com `DEPTH=3`;
- zero `GFXHUB_FAULT`;
- desktop no ar (imagem de volta, SOMBRA em 0);
- menu operado.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava, ≥ 2.000 FILLs | A forma da árvore é o mecanismo provável; repetição obrigatória |
| **B** | Trava igual com a pré-condição OK | A forma da árvore sai; o núcleo restante (paginação pelo SDMA0, ordem de submissão, MEC) |
| **C** | Fault de tradução ou recusa no UpdatePageTable | Defeito de implementação; corrigir offline |
| **D** | Código 43 / BugCheck no boot | Regressão; reverter a P5 |
| **E** | Fora do mapa | Descrever antes de julgar |

## 6. Régua de FILLs (passo 5, pronto)

- **Ferramenta:** `tools/regua-fills.py` (só leitura; usa `decode-bc250-paging-trace.py`).
- **Saída:** `docs/regua-fills.csv`. São **39 corridas** com rastro BPG1, da 207 em diante; os contadores são cumulativos e vale o maior valor visto.
- **Números:** FILLs mínimo 15, **mediana ~793**, máximo 4.465 (249). A 250 deu 1.767 e a 251b 415.
- **Ressalva:** a tabela inclui as corridas de 600 s sem trava. O FILL no fim delas é carga, não "FILLs até a trava". A classificação por corrida fica para a análise que usar a régua.
- **Uso:** dispersão de mais de duas ordens de grandeza com o mesmo pacote. Um desfecho com n = 1 só é forte quando sai dessa faixa: 600 s sem trava com muitos FILLs, ou trava com pouquíssimos.

**NÃO VALIDADO EM HARDWARE:** nada da P5; ela é desenho.
