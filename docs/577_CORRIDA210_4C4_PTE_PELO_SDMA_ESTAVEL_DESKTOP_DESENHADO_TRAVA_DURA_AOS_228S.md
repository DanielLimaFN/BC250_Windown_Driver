# 577 — Corrida 210: a 4c.4 funcionou (PTE pelo SDMA, 20.071 IBs, zero timeout, desktop desenhado) e a trava dura voltou aos ~228 s

**Data:** 03/10/2026, bind às 22:23:42 · **Candidato:** lab **3.357.1.0** (SYS `AE714443…`) · **Critério:** [doc 576](576_CORRIDA209_4C4_RODOU_PONTO_DE_DIVISAO_NO_IB_ERRADO_E_CRITERIO_CORRIDA210.md) §5
**Evidência:** `docs/evidence/2026-10-03_22-23-42-DESKTOP-HQAG6T4-lab-binding/`
- `display-log-this-bind-RECUPERADO.txt`: 37,2 MB, 304.670 linhas.
- Trace de paginação decodificado.
- O coletor rodou antes da recuperação.

---

## 1. Medido (P0)

| Sinal | Valor | 209 |
|---|---|---|
| `GPUMMUCAPS=GPU_VIRTUAL`, `[PT_GPU] ARMADO` | sim | sim |
| IBs de paginação executados (`IB EXECUTADO`) | **20.071** | 119 |
| IBs divididos no anel / pontos de invalidação emitidos | ~18.944 / 50.546 | 16 / 37 |
| `ORFAO` | 0 | — |
| `IB_TRIPOD TIMEOUT`, `TDR_QUIESCE`, `GFXHUB_FAULT` | **0, 0, 0** | 3, 6, 0 |
| Submissões gráficas / `Present` | **972 / 1.433** | 0 / 0 |
| Zeramentos (`FILL_POR_COPIA`) | 2.063 | — |
| Caminhadas de reserva do journal | 3 | — |
| `Journal NO_SPACE` → `TAINT` | 33 | — |
| `Present RECUSADO: copia GPU nao concluida; status=0xC01E0200` | **109** | 0 (208: 6) |
| Tctl | 71,6–81,1 °C | — |

- **A CPU não escreve mais tabela de página de processo.** As 33 linhas `Hardware PTEs written` são o texto do `NO_SPACE`, não escrita pela CPU.
- **Duração:** ~228 s pelo QPC até a trava dura (208: ~290 s; 207: ~96 s).

## 2. A trava

- **No último segundo (BPG1):** 25 despejos (65 MB despejados), 11 fills e 739 `FlushTlb`. É pressão de memória com 256 MB.
- **No log**, a sequência final é:
  1. `FILL_POR_COPIA` de uma alocação nova de 11,8 MB (tela cheia), cerca de 680 linhas antes do fim;
  2. `TRANSFER` de superfícies de 11 MB para fora e para dentro da VRAM;
  3. `DISCARD`.
  - A última linha é o início de uma submissão do Node 0.
- **Última imagem:** o último `Present` aparece cerca de 20 mil linhas antes do fim.

É a mesma assinatura de rajada de alocação de tela cheia das 202–208. A diferença: o último trabalho logado é uma `TRANSFER` depois do fill, e não o próprio fill.

## 3. Veredito pelo critério

**Desfecho B.**
- Modo GPU em uso, zero timeout, zero fault e desktop desenhado.
- Trava dura no meio de uma rajada de alocação e despejo de tela cheia.
- Ressalva escrita: a última operação logada é `TRANSFER`, com o fill de zeramento cerca de 1 s antes.

**Implicação escrita antes da corrida:** a 4c inteira sai da lista de causas suficientes. Nenhuma das etapas resolveu a trava: tirar a CPU da invalidação de TLB (4c.2), da leitura das tabelas (4c.3) e da escrita das PTEs (4c.4).

**Mesmo assim, a 4c.4 é avanço funcional (P0):**
- O driver passou a trabalhar como o Linux nas tabelas de página.
- Foi a corrida com mais IBs de paginação e mais pontos de invalidação sem nenhum timeout.

## 4. Achado colateral

O `Present` recusado com `0xC01E0200` (cópia da GPU não concluída) subiu de 6 (208) para 109. Ele aparece no meio da corrida (Present #733 a #844). O efeito no desktop **não foi medido**: o Daniel não relatou tela congelada antes da trava.

## 5. Recuperação e pendências

- **Estado:** trava dura, reset e coletor. Antes de novo bind: recuperação e boot frio.
- **NÃO VALIDADO EM HARDWARE:**
  - 600 s sem trava com 256 MB;
  - causa da trava;
  - causa dos `0xC01E0200`.
- **Próximo:** voltar às diferenças com o Linux que restam (L-B, doc 552), sem trocar a meta:
  - fences por interrupção em vez de polling;
  - SMU;
  - SDMA1;
  - a pressão de memória do DWM com 256 MB (M1b).
- A 4c.5 (zero MMIO de TLB) deixa de ser prioridade: a premissa dela era o desfecho A.
