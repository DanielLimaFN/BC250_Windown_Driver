# 576 — Corrida 209: a 4c.4 rodou, um defeito meu jogou pontos de divisão no IB errado, e o critério da Corrida 210

**Data:** 02/10/2026, bind às 23:13:02 · **Candidato:** lab **3.356.1.0** (SYS `CE261EEF…`) · **Critério:** [doc 575](575_4C4_PTE_PELO_SDMA_IB_DIVIDIDO_NO_ANEL_E_CRITERIO_CORRIDA209.md) §3
**Evidência:** `docs/evidence/2026-10-02_23-13-02-DESKTOP-HQAG6T4-lab-binding/`
- `display-log-this-bind-RECUPERADO.txt`: 358.153 bytes, 3.119 linhas.
- O coletor rodou antes da recuperação.

---

## 1. O que foi medido (P0)

| Sinal | Valor |
|---|---|
| `[PT_GPU] ARMADO` e `GPUMMUCAPS … PageTableUpdateMode=GPU_VIRTUAL` | sim, nos dois |
| `UpdatePageTable` no paging buffer | **sim**: 16 linhas logadas (teto do log); a primeira em `PtGpuVa=0x200000 Start=62` |
| IBs divididos (`[PT_GPU_SPLIT] #`) | 16 logados, 37 pontos (ex.: IB de 2.224 dw em 5 pontos) |
| `Hardware PTEs written` (escrita pela CPU) | **0** |
| `IB EXECUTADO (INDIRECT)` | 119 |
| `IB_TRIPOD TIMEOUT` | **3** (fences 0x1020, 0x1059, 0x1081) |
| `TDR_QUIESCE` / `TDR_RESTART PROVADO` | 6 / 6 (dois por evento) |
| `GFXHUB_FAULT` | 0 |
| `KMD_GFX_SUBMIT` / `[Present #` | **0 / 0**: o desktop nunca foi desenhado |
| `FlushTlb sem posicao` | 6 envios |

- **Os três timeouts têm a mesma assinatura:** `STATUS=46DE6756`, que decodifica como `IDLE=0` e `INSIDE_IB=1`, com o anel já desligado (`RB_CNTL=00841014`). O SDMA parou **dentro** de um pedaço de IB.
- **Final:** o TDR recuperou as três vezes (`TDR_RESTART PROVADO`). Depois veio a trava dura, poucos segundos após o terceiro evento, com o log terminando num `KMD_SDMA_SUBMIT`.

## 2. A causa (P0 do log, defeito de código meu)

A minha regra no `Bc250PagingSplitTake` dizia: um ponto com VA **menor** que o IB enviado é "velho" e vai antes desse IB. Só que o VidMm **alterna paging buffers e monta o próximo antes de enviar o anterior**:

| Envio | VA | Bytes |
|---|---|---|
| 28 | `0x23BB80` | 8.128 — levou os 2 pontos como "sem posição" |
| 29 | `0x21E000` | 832 |
| 30 | `0x21E340` | 36.768 — **travou** (fence 0x1020) |

- Os pontos de divisão do IB 30 já estavam registrados quando o 28 foi enviado. Como `0x21E340 < 0x23BB80`, o 28 os consumiu como velhos.
- Resultado: o IB 30 executou a escrita de PTE e a operação que a usa **sem invalidação entre elas**, ou seja, com a TLB velha.
- **O mesmo padrão nos outros dois timeouts:** 83→85 (`0x22FBC0` levou os pontos de `0x220CA0`) e 121→123 (`0x22E000` levou os de `0x21FBA0`).

**O que a 209 não diz:** se a escrita de PTE pelo SDMA resolve ou não a trava. O caminho novo foi exercitado, mas quebrado por esse defeito.

## 3. Veredito pelo critério do doc 575

**Desfecho C**: `IB_TRIPOD TIMEOUT` e TDR no caminho novo. Pela implicação escrita antes, voltar offline com o log, e foi o que fiz.

- **Avanço?** Inconclusivo no funcional; houve aprendizado de diagnóstico.
  - **P0:** `GPU_VIRTUAL` aceito pelo VidMm, PTE escrita pelo SDMA, zero escrita pela CPU, IB dividido executado.
  - **P0:** o VidMm monta buffers à frente dos envios.
- O que a 4c.4 quer testar continua **NÃO VALIDADO EM HARDWARE**.

## 4. Correção (offline)

- **Regra nova:** um ponto pertence **só** ao envio cujo intervalo `[IbVa, IbVa+IbBytes]` o contém. Os outros esperam o envio do próprio buffer.
- **Ponto no fim de um IB e no início do seguinte** (buffers contíguos, como 29→30) fica com o primeiro enviado. As PTEs que ele cobre estão antes daquele ponto, então invalidar no fim do anterior já precede tudo o que vem depois.
- **`Va = 0`** (sem DMA buffer) vai antes do próximo IB de paginação.
- **Órfão:** um ponto que não casa em 64 envios vai antes do IB, com log `ORFAO`. Invalidação a mais não faz mal; a menos, faz.
- **Log dos IBs divididos** passou de 16 para 64 linhas.
- **Teste novo** em `mcdm_callbacks_test.c`:
  - a **sequência medida** 28/29/30 com os VAs reais: o 28 e o 29 não levam nada, o 30 leva os dois pontos nos offsets 8 e 56;
  - a ambiguidade do fim/início;
  - `Va = 0`;
  - o órfão no 64º envio. O teste reprovou na minha primeira versão por contar 63, erro meu no teste.
- **Portão** `audit_4c4_pte_by_sdma_and_split_ib`:
  - proíbe `e.Va < IbVa` no `Take`;
  - exige o casamento por intervalo e a idade de órfão;
  - exige o teste da sequência medida.
- **Mutação verificada:** reintroduzir a regra antiga reprova o portão **e** a suíte (linha da sequência medida).

**Pacote (assinado, auditado, NÃO INSTALADO):** lab **3.357.1.0**.
- SYS `AE714443…9E08`, INF `8A3D80BC…CA33`, CAT `80CF424C…5DDE`; UMD `A1BD1657…` igual.
- `build-contract` EXIT=0; auditorias de segurança, pacote de laboratório, matriz e noop PASS.
- String `ORFAO (nao casou` conferida no SYS.
- Preflight `prepare-corrida210-4c4-divisao-corrigida.ps1`: hashes e manifesto conferem. Recusou só a CMOS antiga, que o roteiro lê antes.

## 5. Critério da Corrida 210 (escrito antes, Lei 10)

O critério é o mesmo do doc 575 §3: mesma variável, e a correção é de defeito. Há um sinal a mais, e as regras ficam mais estreitas.

**Roteiro:** recuperação, boot frio, CMOS em 256 MB, `tools\EXECUTAR_CORRIDA210_4C4_DIVISAO_CORRIGIDA_600S.cmd`, mesmo uso. Se travar: reset e **coletor elevado antes da recuperação**.

**Sinal novo:** `[PT_GPU_SPLIT] … sem posicao ou ORFAO`. Esperado: raro, com zero órfãos.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Modo GPU em uso (centenas de `UpdatePT` e de IBs divididos), zero `IB_TRIPOD TIMEOUT`, zero fault, desktop desenhado (`KMD_GFX_SUBMIT` e `Present` > 0) e ~600 s sem trava dura | Compatível com a CPU escrevendo PTE como causa (n=1). A 4c.5 repete e limpa o MMIO restante |
| **B** | Modo GPU em uso, sem timeout nem fault, desktop desenhado, e **trava dura** com a assinatura de sempre (zeramento) | A 4c inteira sai da lista de causas suficientes. Voltar às diferenças restantes com o Linux |
| **C** | `IB_TRIPOD TIMEOUT` com `INSIDE_IB=1`, `GFXHUB_FAULT`, TDR fora do gráfico conhecido, `ORFAO` frequente, ou `RECUSA` do caminho novo | O caminho GPU ainda erra. Offline com o log; `PtUpdateGpuVirtual=0` reverte |
| **D** | Modo GPU desarmado, ou zero `UpdatePT` no paging buffer | A corrida não testou a 4c.4 |
| **E** | Fora do mapa | Descrever antes de julgar |

**Regras:**
- Uma trava antes de o desktop ser desenhado, sem timeout, é E e não B.
- Desfecho parcial não conta.

**NÃO VALIDADO EM HARDWARE.**
