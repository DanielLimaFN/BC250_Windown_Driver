# 495 — Corrida 150 (DX.93): segunda janela seguida sem trava e sem KD; harness 60/60; o despejo é quase todo memória da RADV

> **Corrida:** 28/09/2026. Bind às 08:12:20 após boot frio, sem KD. Rollback online (`RECOVERY_OK_ONLINE`). **Candidato:** DX.93 / 3.300.1.0.
> **Evidência:** `docs/evidence/2026-09-28_08-12-20-DESKTOP-HQAG6T4-lab-binding` (logs `-FINAL`).
> **Relato do Daniel:** "não travou e o watchdog agiu".

## 1. Relato obrigatório
**1. Houve avanço?** Sim.
- **Funcional:** segunda janela seguida de 600 s sem trava e sem KD, com o roteiro da 148 (149 e 150). O harness voltou a 60/60.
- **Diagnóstico:** a atribuição do despejo foi medida.

**2. Sucessos (P0):**
- Paginação 95.631/95.631; 2.531 Presents, 5 `BYTE_PROOF PASS`, 1.919 `COMPOSITE_BLT OK` e zero recusadas.
- Zero TDR, fault ou `DEVICE_REMOVED`. Harness 60/60, exit 0.
- **`DX93_ALLOC_MAP` funcionou:** 16.144 alocações mapeadas, e **todos** os 8.441 despejos foram atribuídos (zero sem mapa).

**3. Falhas e observações:**
- **Despejo inalterado:** 8.441 `Dir=0` em 600 s.
- **Atribuição (P0):**

  | Classe | Conjunto de segmentos | Despejos | Volume |
  |---|---|---|---|
  | **0x00000000** (token = ponteiro de modo usuário, a winsys da RADV) | SegSet=1, CpuVisible=1 | 6.544 | ~2.346 MB |
  | 0x00000000 | SegSet=1, CpuVisible=0 | 1.896 | ~248 MB |
  | PRIMARY | SegSet=1 | 1 | 10,6 MB |
  | RESOURCE da UMD (5.656 criadas, M1) | — | **0** | — |

  Os tamanhos mais despejados são 64 KB (3.681), 80 KB (1.457), 256 KB e 512 KB: blocos de BO da RADV.
- 15.797 `PUBLICACAO_FALHA` (journal no teto).

**4. Veredito frente ao doc 494 §2: C.**
- O harness passou 60/60 sem nenhum `PT_WALK_DONO`: a faixa do slot dele estava no snapshot desta vez.
- A DX.93 **não foi exercitada**; é inconclusiva para ela.

**5. Recuperação e pendências:**
- BasicDisplay Código 0 no mesmo boot; `CLEAN_BOOT_REQUIRED` armado.
- **NÃO VALIDADOS EM HARDWARE:** a caminhada pelo dono e a redução do despejo.
- **O fim da trava ainda não é conclusivo:** 2 janelas limpas contra 5 travas seguidas antes. É encorajador, mas amostra pequena.

## 2. Leitura
- **A M1 acertou o alvo errado.** As alocações da UMD D3D (RESOURCE) quase não são despejadas. Quem enche a VRAM e é despejada é a **memória da RADV** (`vkAllocateMemory` via `radv_wddm2_bo.c`), com token-ponteiro (classe 0) e SegSet=1.
- Isso casa com a arquitetura: cada recurso D3D tem uma alocação WDDM e **também** uma memória Vulkan própria, que é onde o conteúdo realmente mora.
- **Próximo passo lógico (M1b):** na winsys da RADV, pôr no segmento de sistema os BOs de domínio **GTT** (memória que o Vulkan já trata como do sistema), com a mesma regra fail-closed:
  - só BO que o KMD não endereça por MC;
  - o buffer linear de Present do harness e superfícies de composição ficam na VRAM.

  Precisa de leitura da winsys da RADV antes (domínios, flags e como ela forma o blob BCA1 v1).
- A **M2** (VRAM além do BAR) segue planejada. Com a atribuição em mãos, ela teria mais efeito se cobrisse os BOs de VRAM da RADV **não visíveis à CPU** (1.896 despejos, ~248 MB).
