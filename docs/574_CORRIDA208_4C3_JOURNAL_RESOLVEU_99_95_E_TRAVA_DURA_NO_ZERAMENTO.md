# 574 — Corrida 208 (4c.3): o journal resolveu 99,95% dos gates e a trava dura voltou no zeramento

**Data:** 02/10/2026, bind às 22:29:44 · **Candidato:** lab **3.355.1.0** (SYS `11C53A57…8468`) · **Critério:** [doc 573](573_4C3_GATES_PELO_JOURNAL_SEM_LER_TABELA_PELA_BAR_E_CRITERIO_CORRIDA208.md) §3
**Evidência:** `docs/evidence/2026-10-02_22-29-44-DESKTOP-HQAG6T4-lab-binding/`
- `display-log-this-bind-RECUPERADO.txt`: 20.711.102 bytes, 163.579 linhas.
- Trace de paginação recuperado e decodificado.
- `reset-reason-pos-trava.txt`.
- O coletor rodou **antes** da recuperação.

---

## 1. O que foi medido (P0)

| Sinal | Valor |
|---|---|
| `[PT_JOURNAL] PtWalkOnSubmit=0` | 1 (modo da 4c.3 ativo) |
| `[RING_TLB_FLUSH] ARMADO` | sim (4c.2 junto) |
| Submissões: `IB EXECUTADO (INDIRECT)` do Node 0 + `KMD_GFX_SUBMIT` | 2.312 + 3.868 = **6.180** |
| Caminhadas de reserva (`NAO resolvido pelo journal`) | **3** (0,05%), todas `NODE1_GFX_IB` com `status=10`, e as 3 deram `PT_RANGE PASS` |
| `GFXHUB_FAULT` | 0 |
| `TDR_QUIESCE` / `TDR_RESTART PROVADO` | 1 / 1 |
| `IB_TRIPOD TIMEOUT`, `IB NAO EXECUTADO`, `gate fisico falhou`, `DEVICE_REMOVED`, `res=-2` | 0 |
| `FILL_POR_COPIA` | 1.937 |
| `Journal NO_SPACE` (transação parcial abortada, PTE do hardware escrita) | **6** |
| Tctl | 72–83 °C, último 77 °C (sem calor) |

- **Duração até a trava:** cerca de 290 s pelo QPC (de `qpc=4107090278` a `qpc=7004900816`, a 10 MHz). Isso põe a trava por volta de 22:34:40. A 207 travou aos ~96 s.
- **O que o usuário viu:** ao abrir o menu, tudo congelou e depois voltou; depois disso, travou de vez.

## 2. A trava

As últimas linhas do KMD são um `FILL_POR_COPIA` de alocação nova:
- `hAlloc=0x2112`, 94.208 bytes, `Dst=0x23E000`, `N=1937`.
- O IB foi montado e o `IB_PROOF_BIND` saiu, e o log para aí, **antes** do `KMD_SDMA_SUBMIT` desse IB.

É a mesma assinatura das 202–207: a parada acontece no zeramento de uma alocação nova durante uma rajada de criação de recursos.

**As linhas `[PT_WALK] …` não são caminhada.** Com `PtWalkOnSubmit=0` elas ficaram só como rótulos de log; as chamadas `Bc250ActiveValidateGpuVaRange*` estão atrás do tunable (`bc250_active_miniport.c:13811-13940`). Na 207 a última linha antes da parada era uma caminhada pela BAR; aqui **não houve nenhuma** no Node 0.

**BPG1, últimos segundos antes da trava (deltas por segundo):**

| Contador | Faixa |
|---|---|
| Fills | 42–71 |
| `FlushTlb` | 323–875 |
| Flushes pelo anel | 44–80 |
| Despejos | até 16 (4,9 MB) |

Rajada de paginação típica de abrir a Pesquisa ou o menu.

## 3. O TDR (não é a trava)

O TDR veio do **anel gráfico**: `GFX_TRIPE_VEREDITO Perna1=NAO_CONVERGIU`, com WPTR=6040 e RPTR=6030 (`Submetidas=371`, `Aposentadas=370`). O Windows disparou o TDR, o quiesce do SDMA0 encontrou o anel ocioso, e o `TDR_RESTART PROVADO` trouxe a máquina de volta.

Foi o "travou e depois abriu" do menu. É um TDR gráfico recuperado, como o da 201, que rodou com caminhadas pela BAR. Por isso **não** é atribuível à 4c.3.

O segundo `NAO_CONVERGIU` (`Submetidas=2039`) não gerou TDR.

## 4. Veredito pelo critério

**Desfecho B.**
- `PtWalkOnSubmit=0`, com caminhadas de reserva raras (0,05%, abaixo do teto de 1%).
- Trava dura com a assinatura de sempre.
- **A leitura de tabela de página e de IB pela CPU no submit não é causa suficiente da trava** (n=1, mas a assinatura é a mesma).

Por que não é C nem D:
- **Não é C:** zero `GFXHUB_FAULT`, zero recusas de gate, e o único TDR tem precedente sem a 4c.3.
- **Não é D:** o journal resolveu 6.177 de 6.180 gates.

**Lista de causas suficientes já eliminadas com 256 MB:**
- SDMA1;
- MGCG do SDMA0 (CG.1);
- `CONST_FILL` (X2b);
- leitura de VRAM linear pela CPU (4b);
- invalidação de TLB pela CPU (4c.2);
- leitura de tabela pela CPU no submit (4c.3).

**O que sobra no build: a CPU escrevendo PTE pela BAR** (`CPU_VIRTUAL`) enquanto o SDMA escreve na VRAM. É exatamente a 4c.4.

## 5. Achado colateral: o journal diverge do hardware em `NO_SPACE`

Seis vezes, `[ActiveUpdatePT] Journal NO_SPACE … transacao parcial ABORTADA; Hardware PTEs written -> SUCCESS`. Nessas transações a PTE do silício foi escrita e o journal ficou sem o registro.

Nesta corrida isso não produziu aceitação errada: as 3 reservas resolveram por caminhada e não houve fault. Mesmo assim, é uma porta aberta para o gate pelo journal aceitar um mapeamento velho (P0 o `NO_SPACE`; H o risco).

**Antes de a 4c.4 depender mais do journal:**
- dimensionar o journal;
- ou invalidar a faixa no `NO_SPACE`, para que o journal falhe fechado e a reserva rode.

## 6. Recuperação e pendências

- **Estado da placa:** travou, com reset pelo botão e o coletor rodando antes da recuperação. No momento da análise a placa espera recuperação e boot frio.
- **CMOS:** segue em 256 MB (APCB lida pelo coletor, checksum confere).
- **NÃO VALIDADO EM HARDWARE:**
  - 256 MB sem trava dura;
  - 4c.4 e 4c.5;
  - o efeito da correção do `NO_SPACE`.
- **Próximo passo:** 4c.4 (`GPU_VIRTUAL`, PTE por `WRITE_LINEAR` pelo SDMA), com o tratamento do `NO_SPACE` do journal junto.
