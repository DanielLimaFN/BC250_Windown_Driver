# 668 — P5.1: tabela de páginas de 4 níveis implementada, pacote lab 3.398.1.0 e critério da Corrida 252

**Data:** 08/10/2026, noite. **Natureza:** offline, sem corrida. Desenho em [doc 667](667_P5_0_DESENHO_TABELA_DE_PAGINAS_DE_4_NIVEIS_COMO_O_AMDGPU_E_REGUA_DE_FILLS.md).
**Pergunta da corrida:** com a árvore de páginas da GPU igual à do `amdgpu` (4 níveis de 9 bits, 48 bits), a trava dura some?

## 1. O que mudou (uma variável de investigação; a SOMBRA sai porque era de outra pergunta)

| # | Onde | Antes (até a 251b) | Agora (3.398.1.0) |
|---|---|---|---|
| 1 | `driver/core/bc250_gpuva_geometry.h` (**novo**, fonte única) | geometria espalhada em dois headers | `BC250_P5_QUATRO_NIVEIS 1`: 4 níveis, 9 bits em todos, VA 48. Chave em 0 volta a 2 níveis (raiz 13 bits) / 34 bits, sem outra mudança |
| 2 | `bc250_gpuva_space_contract.h` | `LEVEL_COUNT 2`, `ROOT 13`, `VA = 12+9+13` | inclui a geometria; `VA = 12 + 9·(N−1) + raiz` → 48; `ROOT_MAX_ENTRIES` 512, `TRANSLATABLE_LIMIT` 2^48, `PT_SIZE` 4 KB |
| 3 | `bc250_gcvm_vmid8_transaction.h` (CNTL/END dos contextos 1–15) | `0x00555403` / `0x007FFE03`, `END = 0x0:003FFFFF` (literais) | derivados: `DEPTH = N−1 = 3`, `BLOCK_SIZE = 9−9 = 0` → **`0x00555407` / `0x007FFE07`**; `END = 2^36−1 = 0xF:FFFFFFFF` (como `gfxhub_v2_0_setup_vmid_config`) |
| 4 | `bc250_mcdm_callbacks.c` | `PAGETABLELEVELDESC`: nível 0 = 9 bits, nível 1 = 13 bits/64 KB; `GetRootPageTableSize` exigia `Levels == 2`; `UpdatePageTable` escolhia folha×raiz | por nível pela geometria: 9 bits / 4 KB nos níveis 0..3; `GetRootPageTableSize` confere `LEVEL_COUNT`; limites do `UpdatePageTable` por nível |
| 5 | `bc250_active_miniport.c`, walkers de PT (verboso e silencioso) | raiz + folha fixos | laço do nível N−1 até 1 (PDE decodificado como PDE, recusa de reservado[5:3] em todo nível), depois a PTE; com 2 níveis o caminho é idêntico ao anterior |
| 6 | `bc250_active_miniport.c`, diagnóstico da arena (DX.17, índice 4096 da raiz) | sempre | só com 2 níveis (o índice 4096 não existe numa tabela de 9 bits) |
| 7 | log do `[TDR_GCVM_FAULT]` | "EXCEDE 34 BITS" | "EXCEDE %u BITS" pela geometria |
| 8 | **SOMBRA.1** | 1 (tela congelada, 251/251b) | **0**: o Present/flip volta ao scanout real, **a tela volta a mostrar o desktop** |
| 9 | `driver/umd/bc250_wddm_winsys.c` | `MaximumAddress = 2^34−1` (derivado) | teto da UMD **fixado em 2^34−1** (a mesma janela dos heaps da RADV, 8–16 GB). A UMD do pacote é a **mesma B73F9BDA** da 251b; o fonte agora diz o que o binário faz |
| 10 | `tools/audit-bc250mcdm-safety.py` | — | portão `audit_p5_geometria_unica_de_4_niveis` |

**Não mudou:**
- **VMID 0:** a GART da D2/MEC e o MMHUB/IH continuam com `DEPTH 0`, e todos os escritores de `CONTEXT0_CNTL` foram conferidos;
- o L2/TLB do GFXHUB, já em paridade;
- a escrita de PTE pelo SDMA (4c.4/P4.2): a faixa registrada é o endereço das entradas na VA do processo de paginação, independente do nível;
- a RADV `B4AE2E14`;
- a foto CG (segue desligada) e o refresh de dois pontos;
- TMR.1, RLC rodando, G.1, P3.3=0.

**Conferido por leitura (P1):**
- **Raízes:** toda raiz instalada em GCVM vem do VidMm (`SetRootPageTable`): VMID 7/8, pool e VMID 9 de compute. Nenhuma tabela montada pelo KMD cai sob um contexto com `DEPTH=3`.
- **VMID 9:** o compute também passa a `DEPTH=3`. A raiz dele é a do VidMm, então isso é o certo.
- **Inventário:** journal, espaço de GPUVA, snapshots e o teto do adaptador derivam de `BC250_GPUVA_PT_VA_BITS`, e o espaço aceita até 48.

## 2. Prova offline (não é corrida)

- `build-contract.bat` **EXIT=0** (suíte inteira).
- `build-umd.bat` EXIT=0. Ele rodou só para os testes; a DLL de saída foi restaurada para a mesma 173AD84A (sem assinatura), que assina como B73F9BDA.
- **Testes reescritos com os dois ramos por extenso**, para que um erro de derivação reprove em vez de concordar:
  - CNTL `0x00555407`/`0x007FFE07` e END `0xF:FFFFFFFF`;
  - construtor genérico `Bc250GcvmBuildVmidCntl(1,4,9) == 0x00555407` com DEPTH 3;
  - GPUVA do adaptador 48 / `0xFFFFFFFFFFFF`;
  - `PAGETABLELEVELDESC` 9 bits / 4 KB nos níveis 1..3;
  - journal: aceita a 1ª página acima de 2^34 e recusa 2^48;
  - VMID 9 `0x80555407`;
  - candidato `<<20` de fault: dentro do teto, mas sem faixa viva.
- **Auditoria de segurança PASS**, com o portão novo. **Sete mutações, todas reprovadas pelo portão:**
  1. SOMBRA em 1;
  2. walker sem laço;
  3. `GetRootPageTableSize` com 2 literal;
  4. CNTL literal;
  5. redefinição de ROOT no contrato;
  6. geometria 3×9;
  7. arena sem a guarda.
- **Auditoria do pacote PASS.**
- **No binário assinado (contagem de imediatos, 3.398 contra 3.397):**
  - `0x007FFE07` 3 contra 0 e `0x007FFE03` 0 contra 3;
  - `0x00555407` 1 contra 0 e `0x00555403` 0 contra 1;
  - strings novas do walker presentes (`L%u idx=`, `niveis=%u`);
  - `[SOMBRA1] ATIVO` ausente (compilado fora).

**Incidente meu, declarado:** ao trocar os walkers, um script fatiou o `bc250_active_miniport.c` a partir da primeira ocorrência da assinatura, que era o protótipo (linha 334), não a definição. Isso apagou ~13.000 linhas com mudanças sem commit. O arquivo foi reconstruído a partir do backup de edição mais recente (v17, que já tinha a 251b) e das partes intactas, com asserções de igualdade fora dos walkers. O pacote saiu desse arquivo reconstruído e passou build, testes e as duas auditorias.

## 3. Pacote

| | |
|---|---|
| DriverVer | **3.398.1.0** (fonte do INF em 3.398.0.0) |
| SYS lab | `C973EE22…` |
| SYS assinado | `B4EC4FB32D185931EB020B72446010F207047655F55B81968A76B8D956565671` |
| INF | `7B0908290C2EF6E5EFE9723EBBEAC3A5C4BD9601A88BE92A1C2E625CB6B33750` |
| CAT | `1A70CE5CFA682BF40205FBEFA930AFDB1D2FE2B3F8F23F9AB216E09DAFF2D915` |
| UMD | `B73F9BDA…` (igual à 251b) |
| RADV | `B4AE2E14` (igual) |
| Backup | `driver/wdk/package/output_lab_3.398.1.0_252` |
| Lançador | `tools/EXECUTAR_CORRIDA252_P5_QUATRO_NIVEIS_MENU_600S.cmd` (preflight `prepare-corrida252-p5-quatro-niveis-menu-600s.ps1`, hashes selados) |

## 4. Antes da corrida (Daniel)

1. A placa está em Código 43 desde a 251b:
   - `tools\BC250_RECOVERY.cmd`, elevado;
   - `tools\inspect-boot-fuse.ps1 -Reset`;
   - **desligar** (boot frio, não reiniciar).
2. CMOS em 256 MB (o lançador lê e o preflight exige).
3. Roteiro igual ao da 251b/250: só o menu Iniciar, Win e Esc em ciclos até o rollback. **Desta vez a tela deve mostrar o desktop e o menu.**

## 5. Critério da Corrida 252 (escrito antes)

**Pré-condição (todas; se faltar uma, a corrida não responde à pergunta):**
- **P5-a:** `[McdmQAI] GPUMMUCAPS: VaBits=48, Levels=4`.
- **P5-b:** `[McdmQAI] PAGETABLELEVELDESC: Level=N, Bits=9, Size=4096` para todo nível que o VidMm consultar (esperados 0..3).
- **P5-c:** `[McdmSetRootPageTable] ... MaxEntries=512 (rootBits=9)` e `[ActiveSetRootPT]` sem FAIL.
- **P5-d:** `[VMID_POOL] INSTALACAO ... Readback=1 ... Ready=1`. O readback é por igualdade exata com `0x007FFE07`, então isso é o `DEPTH=3` provado no silício.
- **P5-e:** `UpdatePageTable` em níveis 1..3 sem `[UpdatePT FAIL ...]`.
- **P5-f:** desktop visível e menu operado (a SOMBRA saiu).

| Desfecho | Assinatura (o que o log/tela mostra) | Implicação (para onde vai) |
|---|---|---|
| **A** | Pré-condição completa, **600 s sem trava**, rollback limpo, **≥ 2.000 FILLs** | A forma da árvore é o mecanismo provável. **Repetição obrigatória** (n=1 não fecha: houve 600 s sem trava antes, na 237/238) |
| **A'** | Pré-condição completa, 600 s sem trava, mas < 2.000 FILLs | Inconclusivo: carga baixa, comparável à 237 parada. Repetir com o mesmo roteiro |
| **B** | Pré-condição completa e **trava dura com a assinatura de sempre** (GPU ociosa no último tique, sem BugCheck, Código 43 pelo fusível) | A forma da árvore sai das causas suficientes. Próximo: o núcleo restante (paginação pelo SDMA0 / ordem de submissão / MEC), sem mexer na P5 |
| **C** | `GFXHUB_FAULT`/`[TDR_GCVM_FAULT]`, `[UpdatePT FAIL ...]`, `[ActiveSetRootPT] FAIL`, `[VMID_POOL] RECUSA` ou `[PT_WALK ...] FALHA Lk` com PDE inválido | Defeito de implementação da P5 (encoder de PDE nos níveis 2/3, limites, raiz). Corrigir offline; a corrida não responde à pergunta |
| **D** | Código 43 ou BugCheck **antes do desktop**, ou `GPUMMUCAPS` recusado | Regressão de boot da P5. Voltar a chave para 0 (2 níveis) num pacote novo e minerar o StartDevice |
| **E** | Fora do mapa (por exemplo, tela preta com a pré-condição OK, artefato visual, trava em outro ponto) | Descrever o observado antes de julgar |

**Régua (doc 667 §6):**
- Na 251b, a trava veio aos ~53 s com 412 FILLs; na 250, aos ~213 s com 1.754.
- A dispersão histórica vai de 8 a 584 s. Uma trava "tardia" sozinha **não** é desfecho A.

**Teto e abandono:**
- No máximo **2 corridas** da P5 (a 252 e uma repetição, ou uma correção de C/D).
- Se B se repetir, a P5 fica (é paridade com o `amdgpu`) e sai da investigação da trava.

## 6. Riscos declarados

1. **Encoder de PDE nos níveis 2 e 3.** Ele nunca rodou no silício; até aqui só o nível 1 → PTB. O formato é o mesmo no gfx10 (sem translate_further), mas um bug escondido pelos 2 níveis aparece como desfecho C.
2. **VA acima de 16 GB.** O VidMm pode pôr alocações dele (paginação, primária) acima de 16 GB. Journal, espaço, walkers e teto do adaptador foram varridos e derivam de 48. A UMD e a RADV continuam abaixo de 16 GB.
3. **Diagnóstico de fault.** O candidato `<<20` deixa de ser descartado pela largura e passa a ser julgado pela faixa viva. É diagnóstico apenas, não muda caminho de GPU.
4. **Memória.** O VidMm aloca PDB2/PDB1/PDB0 de 4 KB por processo e faixa. Deve ser menos que a raiz de 64 KB de hoje. NÃO VALIDADO EM HARDWARE.

**NÃO VALIDADO EM HARDWARE:** nada da P5.1. Este documento é preparação.
