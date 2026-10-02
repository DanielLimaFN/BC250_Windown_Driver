# 563 — Mineração: leitura da VRAM pela CPU nas travas, e como o Linux atualiza as tabelas de página

**Data:** 02/10/2026 · **Tipo:** offline, sem corrida
**Antecede:** [doc 562](562_CORRIDA203_X2B_VALIDO_TRAVOU_IGUAL_CONST_FILL_NAO_E_A_CAUSA.md) (Corrida 203). **Escolha pendente com o Daniel:** candidato 4a ou 4b (§4).

---

## 1. Coletor da 203 (P0)

- **Motivo do reset:** `0x40080400` → `0x40000400` depois do W1C. É o mesmo padrão da 199–202, com o bit 19 do reset pela `CF9`.
- **Nenhum `res=-2`** em nenhum processo. Diferente da 200 e da 202: a trava não precisou do DWM sem memória.
- **Últimas linhas (09:52:47):**
  - DWM: `DXGI Present` + `FlushCommands ENTER` (6 draws);
  - StartMenu: `Present LEAVE hr=0`;
  - TextInputHost: criando um buffer;
  - **SearchApp (7308): `INSPECAO LINEAR_BUF:` às 09:52:47.482, sem a linha `GRADE` seguinte.**

## 2. Varredura: um processo parado no meio da leitura em grade pela CPU

**O que é a leitura em grade.** `Bc250UmdVkInspectLinearPresentBuf` é **diagnóstico**: grade DX.23 (1 linha em 4, 1 pixel em 8) sobre o buffer linear `HOST_VISIBLE`. Ela lê ~2,7 MB da VRAM pela BAR, mapeada pela CPU, e leva ~20–29 ms. Roda em dois pontos da UMD:
- `[DRAW_READBACK] #1` depois do primeiro draw de cada device;
- a inspeção amostrada do Present.

**Método:** `grade_scan.py`, sobre os logs de UMD pós-trava das pastas desde 28/09. A pergunta é se o último registro de algum processo é a amostra de 5 pontos **sem** a `GRADE` que sempre vem depois.

| Corrida | Grades completas na corrida | Duração média | Processo parado no meio da grade | Fração do tempo com grade em curso |
|---|---|---|---|---|
| 199 (21:44) | 41 | 26 ms | **indeterminado**: a Pesquisa (AppContainer) não foi coletada, o coletor só existe desde a 200 | — |
| **200** (22:14) | 63 | 18 ms | **`dwm.exe` (1284), +0 ms do fim** | ~2,0% |
| 201 (08:04, não travou) | 507 | 22 ms | — (sobreviveu) | ~1,9% |
| **202** (08:43) | 48 | 24 ms | **`dwm.exe` (1288), +0 ms** | ~1,2% |
| **203** (09:50) | 49 | 21 ms | **`SearchApp.exe` (7308), −16 ms** | ~1,0% |
| 166–172, 197, 198 | 28–145 por corrida | 14–29 ms | nenhum | — |

**Leitura (P0 como correlação):**
- Nas três travas com 256 MB **cobertas pelo coletor** (200, 202 e 203), um processo estava no meio da leitura em grade.
- Por acaso, a chance seria ~2% × 1,2% × 1,0% ≈ 2·10⁻⁶.
- Em 203 havia **dois** leitores da VRAM pela BAR ao mesmo tempo: a grade do SearchApp e a caminhada da tabela de páginas do KMD (577 caminhadas em 100 s).
- O GPU tinha trabalho em voo (IB gráfico com troca de VMID) e o SDMA estava ocioso.

**O que não se sustenta (Lei 1):**
- **Não é universal.** Nas travas da série 166–172 e na 198 não havia grade em curso. Aquela série tinha outros acessos da CPU à VRAM no fim, como canários pela BAR na 170 e na 171. A 172 terminou numa leitura de registrador, não de VRAM.
- **A 201 fez 507 grades sem travar.** A leitura não trava sempre; se participa, é por concorrência com outra coisa.
- **Isso não reabre a "leitura pela BAR" refutada no doc 526.** Aquela refutação removeu só os canários do KMD (X3/X4). As leituras de modo usuário e a caminhada de tabela continuaram em todas as corridas desde então.

## 3. Linux: quem escreve as tabelas de página (P1)

**Fontes:** `referencias/linux_upstream_2026-09-29/amdgpu_vm.c` e `amdgpu_drv.c`, baixados hoje com procedência.

- `amdgpu_vm_manager_init`: com `amdgpu_vm_update_mode = -1` (padrão) e a VRAM toda visível (`amdgpu_gmc_vram_full_visible`), o modo vira `AMDGPU_VM_USE_CPU_FOR_COMPUTE`. Esse é o caso da BC-250 com 256 MB.
- `amdgpu_vm_init` (VM **gráfica**): `use_cpu_for_update` só com o bit `FOR_GFX`. Com o padrão, **a atualização das tabelas é pelo SDMA**. Só as VMs de compute (KFD) usam a CPU.
- A documentação do parâmetro diz: *"0 = never (default except for large BAR(LB)) … 2 = Compute only (default for LB)"*.

**Diferença com o nosso driver.** No Linux, a carga gráfica da coleta L-B (vkcube, desktop, 149 despejos) nunca escreveu nem leu PTE pela CPU. Nós fazemos as duas coisas pela BAR:
- **Escrita (funcional):** `[ActiveUpdatePT] … Hardware PTEs written`.
- **Leitura (diagnóstico):** `[PT_WALK]` a cada submit de paginação.

## 4. Candidatos para a Corrida 204 (uma variável cada)

| | O que muda | Apoio medido | Custo | Se não travar | Se travar igual |
|---|---|---|---|---|---|
| **4a** | O KMD para de caminhar a tabela de páginas pela CPU no submit (`[PT_WALK]` do IB, da fonte e do destino). O IB continua validado pelo snapshot de software do GPUVA | Último ato da CPU só na 203 | Rebuild do SYS; a UMD não muda | A leitura de PTE pela BAR participa | A caminhada sai de suspeita |
| **4b** | A UMD para de ler o buffer linear pela CPU (as duas chamadas de `Bc250UmdVkInspectLinearPresentBuf`). A cópia para a alocação WDDM e o Present não mudam | **3 de 3** travas cobertas (200, 202, 203), p ≈ 2·10⁻⁶ | Rebuild da UMD: hash novo, reempacotar depois do build | A leitura da VRAM pela CPU em concorrência com a GPU participa. Isso aponta o produto para o M1b (memória CPU-visível no segmento 2, não na VRAM) | A correlação foi coincidência; 4a fica como próximo |

**Recomendação:** **4b.**
- É o único elo presente nas três travas medidas.
- É remoção de diagnóstico, reversível.
- A leitura da 203 mostra a caminhada do KMD concorrendo com a grade. Se a 4b não bastar, a 4a é o passo seguinte natural.
- O "escrever PTE pelo SDMA como o Linux" (4c) fica para depois. É mais trabalho e só se justifica se 4a e 4b não mudarem nada.

**NÃO VALIDADO EM HARDWARE.**
