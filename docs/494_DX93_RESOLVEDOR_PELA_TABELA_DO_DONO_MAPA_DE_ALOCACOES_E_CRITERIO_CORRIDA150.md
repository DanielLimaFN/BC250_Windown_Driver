# 494 — DX.93: resolvedor pela tabela de página do dono e mapa de alocações; M2 fica para candidato separado; critério da Corrida 150

> 28/09/2026, offline. Origem: doc 493 (Corrida 149). O Daniel pediu os itens 1, 2 e 3. Os itens 1 e 2 estão neste candidato. O item 3 (M2) fica para um candidato separado, porque mexe na conversão de endereço, no codificador de PTE, no QUERYSEGMENT4 e nas leituras da CPU. Entrando junto, uma regressão na 150 não teria atribuição (Leis 9 e 10).

## 1. O que mudou
1. **Resolvedor pela tabela do dono** (`Bc250ActiveResolveDisplayGpuVa`, 3º passo):
   - Quando o snapshot não tem a faixa do dono, o KMD caminha a **tabela de página real do processo dono**: raiz do registro do processo e `Bc250ActiveWalkGpuVaQuiet`, o mesmo usado no gate de IB.
   - Só aceita página no **segmento local**. Fora dele, que inclui a memória do sistema da M1, recusa com `PT_WALK_DONO RECUSADO`.
   - Nunca usa a raiz de outro processo: a DX.91 continua valendo.
   - Sucesso é logado como `PT_WALK_DONO SUCCESS`.
2. **Mapa de alocações** (`[DX93_ALLOC_MAP] Handle=... Token=... Classe=... Size=... SegSet=... Preferido=... CpuVisible=...`), uma linha por alocação criada e sem amostragem.
   - É o **mesmo handle** que aparece em `DX91_PAGING_MOVE hAlloc=`, então dá para atribuir cada despejo a uma classe e a um conjunto de segmentos.
   - É logado **depois de soltar o `ObjectLock`**, porque o FLUSH_TLB disputa esse lock e I/O de log sob ele é a classe do deadlock da DX.83.
3. **Portão `audit_dx93_owner_walk_and_alloc_map`**, com **três mutações e três reprovações**: aceitar fora do segmento local, caminhar sem exigir dono, logar o mapa sob o lock.
4. `build-contract` EXIT 0, build de laboratório EXIT 0, auditorias PASS. A M1 e a DX.91 continuam no pacote.

| Artefato (3.300.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `6C569BFE35C7661FF2B3E8BAE867C8B81E2F20BCB2130FFA6C72F94E1C043A40` |
| `bc250mcdm.sys` fonte lab | `C0631DE3B4D719522E44D0CEC9465445ADE671076391F068BAA80F2E57A6F090` |
| `bc250umd.dll` (igual à 149) | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `13101A2596B895DF4A5D685C470411CC665A83B35FB173F534DE8AFED29E3395` |
| `bc250mcdm.cat` | `4D9F2A6D7C62A53E6476EA0F1C553A06717D2C2D502BCDA753E03288A64E930B` |

## 2. Critério da Corrida 150 (escrito antes)
**Runner:** `tools\EXECUTAR_CORRIDA150_DX93_600S.cmd`, sem KD, harness atrasado 150 s. Roteiro da 148/149: desktop, Gerenciador de Tarefas e navegador. Boot frio antes.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Harness **60/60** e pelo menos um `PT_WALK_DONO SUCCESS`; desktop correto; sem trava | O snapshot no teto era a causa da perda do Present; a caminhada resolve. Próximo: M2 e atribuição do despejo. |
| **B** | Harness falha de novo, com `PT_WALK_DONO RECUSADO` no slot dele | A página do slot não está no segmento local ou a tabela não a tem. O `PageMc` da linha diz qual. |
| **C** | Harness 60/60 **sem** nenhum `PT_WALK_DONO` | O snapshot não chegou ao teto nesta corrida; a DX.93 não foi exercitada (inconclusivo para ela). |
| **D** | Trava | Comparar com 147/148; a M1 e esta faixa não bastam. |
| **E** | Algo fora do mapa, incluindo `DX93_ALLOC_MAP` ausente ou corrupção visual | Descrever antes de julgar. |

**Medida paralela, sem veredito:** atribuição dos `DX91_PAGING_MOVE op=TRANSFER Dir=0` por `Classe`/`SegSet`, cruzando pelo `hAlloc`.

**NADA DA DX.93 FOI VALIDADO EM HARDWARE.**
