# 564 — 4b: a CPU não lê mais o buffer linear de Present pela BAR; critério da Corrida 204

**Data:** 02/10/2026 · **Tipo:** offline, sem corrida · **Candidato:** lab **3.351.1.0**
**Antecede:** [doc 563](563_MINERACAO_LEITURA_DA_VRAM_PELA_BAR_NAS_TRAVAS_E_TABELAS_DE_PAGINA_NO_LINUX.md).
**Decisão do Daniel:** 4b agora. Se não funcionar, atualizar as tabelas de página pelo SDMA como o Linux (§4).

---

## 1. Por que

- Nas três travas com 256 MB cobertas pelo coletor (200, 202, 203), um processo parou **no meio** da leitura em grade do buffer linear pela CPU (P0 como correlação, chance por acaso ~2·10⁻⁶).
- A leitura é **telemetria**: o resultado só vai para o log.

## 2. O que mudou (só a UMD)

**Interruptor:** `BC250_UMD_CPU_INSPECTS_LINEAR_BUF 0` (`bc250_umd_vulkan_bridge.h`).

**`Bc250UmdVkInspectLinearPresentBuf`:**
- sai antes de qualquer `vkMapMemory` e devolve 0;
- loga `INSPECAO LINEAR_BUF DESLIGADA (4b)` (4 primeiras + 1 em 256).

**Present:**
- `inspectThis` fica falso;
- o log diz `NonZeroGrade=DESLIGADA_4B`, nunca `AMOSTRA_ZERO` sem ter lido.

**`DRAW_READBACK`:** a cópia pela GPU para o buffer linear e o flush **continuam iguais**. Só a leitura da CPU sai. O trabalho de GPU fica idêntico e a variável é só o acesso da CPU.

**O que não muda (declarado):**
- Os outros 5 `vkMapMemory` da UMD são uploads, em que a CPU **escreve** na VRAM. Continuam.
- O KMD é o da 203: X2b, CG.1 e caminhada de PTE no submit.

**Provas offline:**
- `build-umd` EXIT=0.
  - `umd_present_grid_test` reescrito: com o interruptor em 0, exige **zero** `vkMapMemory` e retorno 0 com conteúdo não nulo.
  - O teste original da grade fica sob o interruptor em 1, contando 2 mapeamentos.
- `build-contract` EXIT=0.
- Portão `audit_4b_cpu_does_not_read_linear_buf`: **4 de 4 mutações reprovam**.
  1. Interruptor em 1.
  2. Retorno antecipado removido.
  3. Present voltando a dizer `AMOSTRA_ZERO`.
  4. Teste sem a exigência de zero mapeamentos.
- Auditoria de segurança e auditoria do pacote: PASS.

**Pacote 3.351.1.0:**

| Arquivo | SHA-256 |
|---|---|
| SYS | `07859E32883EAC529C29B85EEF0FA5A5AC1E912417F71C473D6851D59173B490` (mesmo fonte da 203, rebuild) |
| INF | `6C5C0BFA4493E0616A545D494B030D1D78A09FB17B68503216A8A3D45EDC7D27` |
| CAT | `E4B64FD1B17270A99D051C98EC82661EE2634C45C826FA7046C1E0BCA52759A2` |
| UMD | `6654865DF4F63057C1EC6F41B588043CE211D7D77B312530C5CB1FB05734F2F1` (nova) |

- RADV `5447A17D…`.
- `DESLIGADA (4b)` e `DESLIGADA_4B` estão na UMD assinada; `FILL_POR_COPIA` e `[SDMA_CG]` no SYS.
- **Não recompilar a UMD depois do empacotamento:** o build não é determinístico e o preflight recusa.
- **Scripts:**
  - `tools\prepare-corrida204-4b-cpu-sem-leitura-da-vram.ps1`;
  - `tools\EXECUTAR_CORRIDA204_4B_CPU_SEM_LEITURA_DA_VRAM_600S.cmd`.

## 3. Corrida 204 — critério (escrito antes, Lei 10)

**Preparação:**
1. Recuperação (a placa está em Código 43 desde a 203), depois boot frio de verdade.
2. `EXECUTAR_CORRIDA204_4B_CPU_SEM_LEITURA_DA_VRAM_600S.cmd` elevado.
3. **Mesmo uso da 201 a 203:** menu Iniciar várias vezes, Pesquisa, Gerenciador de Tarefas, Configurações. Anotar a hora de cada tela preta.
4. Se travar: `tools\coletar-pos-trava.ps1` antes de tudo.

**Validade do ensaio:**
- `INSPECAO LINEAR_BUF DESLIGADA (4b)` presente nos logs da UMD.
- **Zero** `INSPECAO LINEAR_BUF:` de amostra e **zero** `INSPECAO LINEAR_BUF GRADE`.
- O Present diz `NonZeroGrade=DESLIGADA_4B`.
- `FILL_POR_COPIA` com `Confere=1`, como na 203.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A — não travou** | Ensaio válido, 600 s sem trava dura, rollback programado, com o menu aberto várias vezes | A leitura da VRAM pela CPU concorrente com a GPU participa (n=1). Repetir. Para o produto: a memória que a CPU **lê** não pode ficar na VRAM. O caminho é o M1b (alocações de leitura pela CPU no segmento 2) |
| **B — travou, sem caminhada de PTE no fim** | Ensaio válido, trava, e o fim do log do KMD **não** está num `[PT_WALK]` | A grade não era a causa. Segue a decisão do Daniel: **4c**, tabelas de página pelo SDMA como o Linux (§4) |
| **B' — travou com o KMD na caminhada de PTE** | Ensaio válido, trava, última linha do KMD num `[PT_WALK]` (como na 203) | O acesso da CPU às tabelas de página fica como suspeito principal. **4c** cobre isso: o Linux nem escreve nem lê PTE de VM gráfica pela CPU |
| **C — ensaio inválido** | Qualquer linha de amostra ou `GRADE`, ou UMD com hash diferente do selado | A 4b não foi aplicada; sem conclusão |
| **D — regressão** | Desktop deixa de aparecer, Present falha, ou TDR/BugCheck novos | A inspeção não era só telemetria; investigar antes de tudo |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar |

**Regra (Lei 10.3):** A exige os 600 s **e** o uso completo, com o menu aberto várias vezes. Corrida curta ou sem menu não testa.

## 4. Plano 4c (se B ou B'): tabelas de página pelo SDMA, como o Linux

**Referência (P1):** `amdgpu_vm.c`.
- Com a VRAM toda visível, só VM de compute usa a CPU (`AMDGPU_VM_USE_CPU_FOR_COMPUTE`).
- VM gráfica usa `amdgpu_vm_sdma_funcs`, em `amdgpu_vm_sdma.c` (já em `referencias/`).

**Desenho a detalhar antes de codar, para não empilhar variáveis:**
1. **Escrita.** O `UpdatePageTable` do VidMm (Op 11) deixa de escrever PTE pela CPU pela BAR. Passa a emitir no próprio paging buffer pacotes SDMA `WRITE_LINEAR` / `PTEPDE`, executados pela engine sob VMID 8, como o resto da paginação.
   - Ler `amdgpu_vm_sdma.c` (`amdgpu_vm_sdma_update`, `copy_pte`, `set_pte_pde`) e o formato de `SDMA_OP_PTEPDE` em `sdma_v5_0.c` antes de escrever um encoding (Lei 7).
2. **Leitura.** A caminhada `[PT_WALK]` no submit sai junto. Com PTE escrita pela GPU, ler pela CPU depois seria outro acesso à BAR e exigiria coerência.
   - A validação passa a ser pela vista de software, o journal de GPUVA, que o KMD já mantém.
3. **Ordem e flush.** Seguir a ordem do Linux: atualização das tabelas → flush do HDP/TLB pelo fluxo de VM, sem leitura de VRAM pela CPU no meio.

Itens 1 e 2 juntos são **uma** variável: "a CPU não toca tabela de página de VM gráfica", que é exatamente a diferença P1 contra o Linux.

**NÃO VALIDADO EM HARDWARE.**
