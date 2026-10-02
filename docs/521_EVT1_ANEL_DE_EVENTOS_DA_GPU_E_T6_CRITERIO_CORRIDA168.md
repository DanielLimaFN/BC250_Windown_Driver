# 521 — EVT.1: o anel de eventos da GPU ligado, e T6 na mesma corrida; critério da Corrida 168

> 29/09/2026, offline. Candidato **3.313.1.0** (inclui HDP.1 e T3.1). **Nada desta faixa foi validado em hardware.**

## 1. Por quê

O exame do FILL (doc 520 §8) não achou defeito no código. Deixou duas perguntas que o log de texto não responde, porque a cauda dele se perde na trava:
- **havia um IB executando** no SDMA (ou no GFX) no instante da parada, e de que tipo?
- a parada vem da GPU escrevendo em rajada, ou de uma transição de energia da plataforma?

Na 167, todas as migalhas de MMIO estavam concluídas, e o log terminou ~1,2 ms depois de um FILL de 11 MB.

A DX.96 tinha desenhado exatamente o instrumento para a primeira pergunta: um anel de 48 eventos da GPU na página de migalhas (a mesma VRAM que sobrevive ao reset rápido), com QPC, tipo, nó, tamanho, endereço, fence e um check contra decaimento. **Mas nunca teve escritor nem leitor:** `Bc250CrumbEvent` não era chamada em lugar nenhum (P1, busca no repositório).

## 2. A mudança (só instrumentação; nenhum acesso a registrador muda)

**Escritores:**

| Evento | Onde | Campos |
|---|---|---|
| `PAGING_FILL` / `PAGING_TRANSFER` | `Bc250McdmBuildFillVirtual` / `BuildTransferVirtual`, ao montar | Op = dwords úteis do IB, bytes do passo, destino (e origem na TRANSFER) |
| `PAGING_DISCARD` | `BuildPagingBuffer`, caso DISCARD | segmento, endereço |
| `SUBMIT_SDMA` | submit do nó 0, antes do log "SUBMISSAO DESPACHADA" | caminho, bytes do DMA buffer, VA, `SubmissionFenceId` |
| `RETIRE_SDMA` | aposentadoria do nó 0, em todo `result.Completed` | idem, com a fence da conclusão |
| `SUBMIT_GFX` | submit do nó 1, depois do commit | Op = commit aceito, bytes do IB, VA, fence do anel |
| `RETIRE_GFX` | `[GfxRetire]` | quantidade, `LastRetiredFence` |

**Leitor** (`Bc250CrumbReportEvents`, chamado por `Bc250CrumbReportPrevious` no StartDevice seguinte):
- copia o anel para pool;
- descarta e **conta** eventos cujo check não fecha (VRAM decaída);
- lista os válidos do mais novo ao mais antigo, com o tempo até o último (`[EVT1_GPU] … antes_do_ultimo=X ms`);
- declara por nó `[EVT1_EM_VOO] no=N: último submit … -> CONCLUIDO` ou `SEM CONCLUSAO REGISTRADA`.

A página é zerada ao armar (`Bc250CrumbArm`), então não sobra evento de corrida antiga.

**Custo:** ~12 escritas UC em VRAM por evento, na faixa privada UC (T3.1). É da mesma ordem das migalhas de MMIO que já existem. Declarado como perturbação de tempo possível.

## 3. Provas offline

- Auditoria de segurança: **PASS**, com o gate novo `audit_evt1_gpu_event_ring_is_wired`.
- **5 mutações, todas reprovaram**, e a auditoria voltou a PASS depois de restaurar (`scratchpad/mutate_evt1.py`):
  - E1: submit do SDMA sem evento;
  - E2: FILL sem evento;
  - E3: leitor aceita evento degradado;
  - E4: o reporte não chama o leitor;
  - E5: vazamento de pool no retorno antecipado.
- Build de laboratório EXIT 0 e auditoria do pacote **PASS**.
- Strings conferidas no SYS assinado: `EVT1_GPU] seq=`, `EVT1_EM_VOO`, `SEM CONCLUSAO REGISTRADA`, `PAGING_FILL`, `SUBMIT_SDMA`, `RETIRE_GFX`, `T31_UC_MAP` e `HDP1_INIT`.
- `tools/prepare-corrida168-evt1.ps1` (leitura): EXIT 0. Boot sem `DEBUG`.

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `A3C524C2079042EFD938EA91A5A61C684F86B86EC9C5D8830390FADFCFDF3E5B` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `4DCEBEEEFBD73C6C3B1E9DFD253BA280FCFA46AF6577700E851B4E3E3064D255` |
| `bc250mcdm.cat` | `00F889B3C988D7AD4C6797B91DA6A5017B3939B150A4F680B89544C8876AAC4F` |

- DriverVer `3.313.1.0` (fonte `3.313.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA168_EVT1_600S.cmd`. Mesmo roteiro da 165–167.

## 5. A variável da corrida: T6 (C-states da CPU desligados)

O Daniel aplica antes do bind, e **não reverte antes da análise**, para eu registrar o estado com `powercfg /qh`:

```
powercfg /setacvalueindex scheme_current sub_processor IDLEDISABLE 1
powercfg /setactive scheme_current
```

O código muda só em instrumentação, então a única variável de comportamento da 168 é a T6.

## 6. Critério da Corrida 168 (escrito antes; Lei 10)

**Base:** uma limpa não prova nada; duas seguidas são o mínimo; uma trava diz "não basta".

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava, com `IDLEDISABLE=1` conferido | Primeira limpa com a T6. Repetir idêntico. Duas seguidas: a transição de energia da CPU entra como gatilho, e a correção passa a ser de configuração. |
| **B1** | Trava; no boot seguinte `[EVT1_EM_VOO] no=0 … SEM CONCLUSAO REGISTRADA`, cujo submit tem os bytes do IB do último `PAGING_FILL` do anel (dwords úteis arredondados para 8 × 4) | A máquina parou **com um FILL executando no SDMA**. A T6 não basta. A próxima frente é o FILL (a rajada de escrita da GPU na DRAM compartilhada): fatiar ou espaçar, conferir destino e PTE daquele FILL. |
| **B2** | Trava; `[EVT1_EM_VOO]` diz **CONCLUIDO** nos dois nós | A GPU estava ociosa ou tinha terminado tudo o que recebeu. O FILL em voo não é o gatilho, e a pista dos 9 FILLs era a cauda do log. A frente passa para o lado da CPU e da plataforma (T2, T3.2). |
| **B3** | Trava; em voo só no nó 1 (GFX), ou um IB de SDMA que não é FILL (TRANSFER, apresentação) | Descrever o IB e seguir por ele. |
| **C** | Sem informação: `ANTERIOR AUSENTE`, ou `[EVT1_GPU] validos=0`, ou quase tudo `degradados` | Reset tardio ou VRAM decaída. Nada se conclui sobre o FILL. Repetir com reset em até 10 s. |
| **E** | Fora do mapa | Descrever antes de julgar. |

- **Desfecho parcial não conta:** B1 exige o submit **sem** conclusão e o casamento de tamanho com o último FILL. Um "sem conclusão" de outro tamanho é B3.
- **Informação nova em qualquer desfecho de trava:** a linha do tempo dos últimos 48 eventos da GPU antes da parada, com milissegundos.

## 7. Resultado da Corrida 168 (29/09/2026, bind 08:36:19, pacote 3.313.1.0, T6 aplicada)

**Evidência:** `docs/evidence/2026-09-29_08-36-19-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-RECUPERADO.txt`, 5.532.667 bytes, SHA-256 `459CA330…CC33`. Evento 41 às 08:37:48. `powercfg /qh` depois da corrida: `IDLEDISABLE` AC = `0x1` (P0, T6 aplicada).

**Trava em 54,9 s**, com a mesma assinatura das migalhas: última `SDMA_REG_READ 0xFC4` concluída na CPU00.

**Veredito: B1.**
- O anel de eventos sobreviveu: 46 válidos, 2 degradados, 0 vazios.
- **O último evento da GPU é `SUBMIT_SDMA` de 32 bytes (fence 1716)**, gravado 2,09 ms depois de `PAGING_FILL` de 2.273.280 bytes para `0x244000` com `op=0x5` (5 dwords, arredondados para 8 = 32 bytes: casa).
- `[EVT1_EM_VOO] no=0 … SEM CONCLUSAO REGISTRADA`. Nó 1 (GFX): último submit **concluído** 35 ms antes; o GFX estava ocioso.
- **A máquina parou com um FILL de paginação submetido ao SDMA e ainda sem conclusão (P0, n=1).** Na mesma janela, os IBs de 32 bytes anteriores foram aposentados 1,5 ms depois do submit (1713, 1714, 1715).
- O anel também foi além do log de texto: o submit (QPC 1.965.162.773) é posterior à última linha do log (1.965.154.291).

**A T6 não basta:** trava com os C-states da CPU desligados (n=1).

**Sem regressão:** 1.715 `IB EXECUTADO`, 1.185 `TRIPE_PROVADO`, 272 Presents, 5 `BYTE_PROOF PASS`; zero timeout, fault, TDR e `DEVICE_REMOVED`. O único `SOBREPOE` é o do PFP no boot, já conhecido e transitório.

**Leitura combinada (P0 + corpus):**
- 10 de 10 travas com um FILL como último trabalho de paginação: 9 pelo log, 1 pelo anel, que é medição direta;
- em todas, a última operação de MMIO da máquina é a leitura de volta da invalidação do HDP (`0xFC4`), feita pelo laço que espera a conclusão do SDMA;
- os FILLs são milhares por corrida e quase todos terminam; o que muda na trava não é o pacote em si.

**Hipótese de trabalho (H):** a combinação "SDMA escrevendo em rajada na DRAM compartilhada (CONST_FILL, só escrita)" com "CPUs sondando a conclusão com invalidação do HDP e leitura de volta a cada iteração". O Linux não sonda assim: RPTR e fence ficam em memória do sistema, e o HDP só é mexido quando a CPU vai ler dados da GPU (plano T2).

**Recuperação:** Código 43 e fusível TRIPPED esperados. Antes de outro bind: `BC250_RECOVERY.cmd`, `-Reset` e boot frio. **Reverter a T6** (`IDLEDISABLE 0`) depois desta análise, porque ela não resolveu.
