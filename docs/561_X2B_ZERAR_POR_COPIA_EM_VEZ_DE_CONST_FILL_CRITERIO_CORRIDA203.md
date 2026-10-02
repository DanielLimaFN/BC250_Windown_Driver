# 561 — X2b: zerar alocação nova por cópia em vez de `CONST_FILL`; critério da Corrida 203

**Data:** 02/10/2026 · **Tipo:** offline, sem corrida · **Candidato:** lab **3.350.1.0**
**Antecede:** [doc 560](560_CORRIDA202_TRAVA_DURA_VOLTOU_COM_CG1_CG1_NAO_E_SUFICIENTE.md) §6 (mineração dos finais) e [doc 526](526_HANDOFF_2026-09-29_TRAVA_DO_FILL_FAIXAS_HDP1_A_X4_CORRIDAS_166_172.md) §8 (desenho original do X2b).

---

## 1. Por que este é o teste

O que se sabe sobre as travas:
- **199, 200 e 202** pararam até 20 ms depois do FILL de zeramento de uma alocação nova de tela cheia (P0, doc 560 §6).
- **166 a 172:** 14 de 14 travas com um FILL de paginação em voo (P0, doc 526).
- **A `TRANSFER`** (`COPY_LINEAR`), ~40% dos movimentos, nunca foi o último trabalho de uma trava.

Restam duas hipóteses, que nenhuma corrida separou:
- **H1 — o pacote:** o `CONST_FILL` trava a placa.
- **H2 — o destino:** o que trava é a primeira escrita da GPU numa alocação nova, sob PTEs recém-escritas.

O X2b troca **só o pacote**. Mantém o mesmo destino, as mesmas PTEs, o mesmo momento e no máximo 256 KiB por pacote. Os desfechos A e B separam H1 de H2.

## 2. O que mudou (offline)

### 2.1 KMD: o FILL de padrão 0 vira cópia

`Bc250McdmBuildFillByCopy` (`driver/wdk/bc250_mcdm_callbacks.c`), chamado por `Bc250McdmBuildFillVirtual` quando `FillPattern == 0`:

**Layout do pedaço do paging buffer desta operação:**
- `[32 KiB de zeros]`;
- `[COPY_LINEAR × N]` de 32 KiB cada, fonte = os zeros, destino contíguo;
- `[NOPs de alinhamento]`.

**Regras:**
- **Os zeros na frente são executados pela engine como SDMA NOP de 1 DW.** O opcode 0 é NOP (P1, `sdma_v5_0.c`), então o IB continua válido.
- **Fonte:** `DmaBufferGpuVirtualAddress + DmaBufferWriteOffset`. Pelo contrato (P1, `d3dkmddi.h`, `DXGKARG_BUILDPAGINGBUFFER`), o primeiro é a GPUVA do início do DMA buffer e o segundo o deslocamento da operação corrente. Isso resolve a dúvida que o doc 526 deixou aberta.
- **Tamanho da região de zeros:** a maior entre 32, 16, 8 e 4 KiB que caiba no espaço restante. Sem espaço nem para 4 KiB, multipasse legal (`INSUFFICIENT_DMA_BUFFER`).
- **Contas:** a primária medida (11.075.584 B) dá 338 cópias, 10.560 DW, um passo só, no paging buffer de 64 KiB.
- **Padrão ≠ 0 continua em `CONST_FILL`.** Nunca foi observado nas corridas.
- **Sem `DmaBufferGpuVirtualAddress`,** fica em `CONST_FILL` e loga `[X2B_FILL] SEM DmaBufferGpuVirtualAddress -> CONST_FILL`.
- **Interruptor:** `BC250_X2B_FILL_BY_COPY 1`.
- **Log:** `[DX91_PAGING_MOVE] op=FILL_POR_COPIA … Zero=… ZeroBytes=… DmaVA=… WriteOff=… WriteOff+DmaSize=…`.
- **EVT.1:** usa o mesmo evento `PAGING_FILL`, com a origem = GPUVA dos zeros (nunca 0).

### 2.2 Decodificador de prova e submit

**Decodificador (`bc250_paging_proof_contract.h`):** novo `BC250_PAGING_PROOF_OP_FILL_BY_COPY`. Só é reconhecido se:
- há região de zeros na frente;
- a fonte é constante;
- o destino é contíguo;
- cada fatia cabe na região.

**Submit (`bc250_active_miniport.c`):** loga `[X2B_FILL] FONTE: Zero=… DmaVA=… Confere=…`, com `Confere=1` só se a fonte == GPUVA do IB submetido. Além disso:
- caminha a fonte como `SRC`;
- se a fonte divergir, loga `FONTE DIVERGENTE`.

### 2.3 Provas offline

- **`build-contract` EXIT=0.**
  - `mcdm_callbacks_test`: bloco novo X2b com 338 cópias, fonte constante e destino contíguo. Mais o caso sem GPUVA (fica em `CONST_FILL`) e o caso de buffer sem espaço (multipasse sem progresso).
  - Os dois testes do fatiamento X2a passaram a usar padrão ≠ 0, para continuar cobrindo o `CONST_FILL`.
  - `sdma_packet_builder_test`: decodificação de `FILL_BY_COPY` com três casos negativos.
- **Portão `audit_x2b_fill_by_copy`: 4 de 4 mutações reprovam.**
  1. Sem o desvio para a cópia.
  2. Fonte sem o `WriteOffset`.
  3. Decodificador sem a checagem de fatia.
  4. Divergência muda.
- **Auditoria de segurança e auditoria do pacote: PASS.**

**Pacote 3.350.1.0** (UMD e RADV idênticas às da 199–202):

| Arquivo | SHA-256 |
|---|---|
| SYS | `586E3E4F6C3F8CA7FE3DE1E3CF63AFD0F00012E5DDD2F3EA48B660E8B2CBB12E` |
| INF | `E4F6A830054C7466FD595AAF59C0EB35AD7505A033F330E42F727F3B30053150` |
| CAT | `F679CA93D8FD8939A180D0DAE0F4F530CFDF0A5998F7B111B2D4E6E58C592A0F` |
| UMD | `ED025FAB…` |

- RADV `5447A17D…`.
- `FILL_POR_COPIA`, `X2B_FILL` e `FONTE DIVERGENTE` estão no SYS assinado, junto com `[SDMA_CG]` e `[TDR_GFX_RING_TAIL]`.
- **Scripts:**
  - `tools\prepare-corrida203-x2b-zerar-por-copia.ps1`;
  - `tools\EXECUTAR_CORRIDA203_X2B_ZERAR_POR_COPIA_600S.cmd`.

## 3. Corrida 203 — critério (escrito antes, Lei 10)

**Preparação:**
1. Recuperação já feita às 08:53 da 202. Boot frio de verdade (tirar da tomada).
2. `EXECUTAR_CORRIDA203_X2B_ZERAR_POR_COPIA_600S.cmd` elevado.
3. **Mesmo uso da 201 e da 202:** menu Iniciar, Pesquisa, Gerenciador de Tarefas, Configurações. Anotar a hora de cada tela preta.
4. Se travar: `tools\coletar-pos-trava.ps1` antes de tudo.

**Validade do ensaio (vale para todos os desfechos):**
- `op=FILL_POR_COPIA` presente, inclusive em FILLs de 11 MB.
- Zero `op=FILL ` de padrão 0. Um FILL de padrão 0 que ainda saia em `CONST_FILL` é o caminho `SEM DmaBufferGpuVirtualAddress`.
- Todo `[X2B_FILL] FONTE` com `Confere=1`.
- Zero `FONTE DIVERGENTE`.

**Pergunta — o pacote ou o destino?**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A — não travou** | Ensaio válido, 600 s sem trava dura, rollback programado **e** `res=-2` no DWM (carga comparável à da 202) | O `CONST_FILL` está implicado (H1), n=1. Repetir antes de chamar de correção. Depois, testar se é o modo DWORD ou o tamanho (doc 560 §6.4) |
| **B — travou com a cópia de zeros por último** | Ensaio válido e o último trabalho de paginação antes da parada (≤ 20 ms) é um `FILL_POR_COPIA` | O pacote não é a causa: é o destino (H2). Próximo: conferir as PTEs do destino pela vista de software antes do submit e comparar a ordem tabela → flush → TLB com o Linux (`amdgpu_vm.c`) |
| **B' — travou com outro trabalho por último** | Ensaio válido, trava, e o último trabalho não é um zeramento | O elo "FILL de alocação nova" se rompe. Descrever o último trabalho antes de julgar |
| **C — ensaio inválido** | Qualquer `Confere=0`, `FONTE DIVERGENTE` ou `SEM DmaBufferGpuVirtualAddress` | O X2b não foi aplicado como desenhado; sem conclusão sobre H1/H2. `WriteOff+DmaSize` e `DmaVA` no log dizem o que o dxgkrnl entrega de fato |
| **D — regressão** | `IB_TRIPOD TIMEOUT`, `GFXHUB_FAULT`, TDR ou BugCheck ligado a um `FILL_POR_COPIA` | O próprio caminho da cópia está quebrado (leitura da fonte no paging buffer) |
| **E — fora do mapa** | Qualquer outra coisa (por exemplo, lixo visível em janelas novas: alocação não zerada) | Descrever antes de julgar |

**Regras:**
- **Lei 10.3:** A exige `res=-2`. Sem ele a carga foi menor e a corrida não testa.
- **Risco declarado:**
  - Se a semântica de `DmaBufferWriteOffset` for outra, a cópia lê outra parte do paging buffer. A alocação nova sai com conteúdo não zerado (C, possível lixo na tela). Não se espera trava por isso: a fonte continua mapeada.
  - O IB de paginação do zeramento passa de ~0,9 KB para até ~42 KB, em várias páginas. A caminhada do IB cobre todas.

**NÃO VALIDADO EM HARDWARE.**
