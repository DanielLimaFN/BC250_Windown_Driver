# 560 — Corrida 202: a trava dura voltou com a CG.1 aplicada; a CG.1 não é causa suficiente

**Data:** 02/10/2026 08:43 · **Candidato:** lab **3.349.1.0** (SYS `DBE80BF3…`) · **CMOS:** 256 MB · **Boot:** frio (confirmado pelo Daniel)
**Evidência:** `docs/evidence/2026-10-02_08-43-09-DESKTOP-HQAG6T4-lab-binding/` (logs da UMD pós-trava em `umd-logs-pos-travamento/`, coletados às 08:53)
**Critério:** [doc 559](559_TDR_G1_LEITURA_DO_ANEL_GRAFICO_NO_TDR_E_CRITERIO_CORRIDA202.md) §4

---

## 1. O que aconteceu (P0)

- O driver subiu e o DWM desenhou: `DRAW GRAVADO`, `FlushCommands … CONCLUIDOS`, `INSPECAO LINEAR_BUF` com pixels não nulos.
- `[SDMA_CG] INIT: … entrada(POST)=0x000C0100 antes=0xFF0C0100 planejado=0x000C0100 lido=0x000C0100 -> OVERRIDES DO POST RESTAURADOS`.
- No rastro durável, BCG1 lê `SDMA0_CLK_CTRL=0x000C0100` em todas as amostras até a parada. A CG.1 **estava aplicada** quando travou.
- **08:44:52.001:** o `dwm.exe` (pid 1288) teve `vkAllocateMemory (Image) falhou: res=-2` duas vezes (texturas de 8 KB, `typeIndex=0`).
- **08:44:53.823:** última linha do DWM. A última linha do KMD saiu no mesmo instante, uma submissão gráfica normal executada e retirada (`Fence=728`).
- **Duração:** ~95 s do primeiro log do KMD até a parada.
- **Nenhum** dos seguintes: TDR, BugCheck, `IB_TRIPOD TIMEOUT`, `GFXHUB_FAULT`, StopDevice.
- O `[TDR_GFX_RING_TAIL]` **não disparou**, porque não houve TDR.
- **Plataforma:** Tctl entre 77 e 82 °C; SDMA1 desligado (halted, RB desligado).
- **Últimos ~600 de 44.598 linhas do KMD:** seis `TRANSFER` de superfícies de tela cheia (11.075.584 e 11.796.480 bytes) e dois `Journal NO_SPACE (num=512)`.

## 2. Veredito pelo critério escrito antes

**Pergunta 1 — desfecho B (a trava voltou).**
- Sinais presentes: restauração confirmada, trava dura, `res=-2` e uso igual ao da 201, com boot frio.
- A CG.1 **não é causa suficiente** da trava. A 201 (591,8 s sem trava) foi sorte ou carga diferente.
- A CG.1 continua correta, porque reproduz o estado que o Linux deixa. Mas deixa de ser "a correção da trava".

**Pergunta 2 — sem resposta.** Não houve TDR, e pelo critério (Lei 10.3) isso não é falha.

## 3. O que a comparação com a 201 diz, e o que não diz

| | 201 (sem trava, 591,8 s) | 202 (trava aos ~95 s) |
|---|---|---|
| `res=-2` no DWM | sim | sim, 1,8 s antes da parada |
| `TRANSFER` (despejos e retornos) | 2.823 | 324 |
| `TRANSFER` de tela cheia (11,0 e 11,8 MB) | 22 | 17 |
| `Journal NO_SPACE` | 135 | 12 |

- A 201 sobreviveu a **mais** pressão de paginação do que a 202 teve até travar.
- Logo, pressão de memória, despejo de superfície grande e `NO_SPACE`, sozinhos, também **não são suficientes** (P0 por contraexemplo).
- O que se repete nas travas medidas (200 e 202) é a **ordem temporal**: o DWM fica sem memória e ~2 s depois tudo para. Isso é correlação (P2), não causa.

## 4. Lacunas declaradas

- O motivo do reset na CMOS lido pelo coletor não ficou gravado na pasta (só saiu no console).
- Sem TDR, não há fotografia do anel gráfico na hora da parada. O rastro durável é a única janela.

## 5. Próximo passo (offline, sem corrida)

Minerar os ~2 s finais da 199, da 200 e da 202 lado a lado, contra as rajadas equivalentes da 201 que **não** travaram:
- sequência exata de `TRANSFER`, `UpdatePageTable` e `NO_SPACE`;
- submissões gráficas em voo;
- última amostra de SDMA0 e BSQ1.

O objetivo é achar o que só existe nas travas. Nenhuma corrida nova antes disso (Lei 9).

## 6. Mineração dos finais (feita em 02/10, offline)

**Ferramentas:** `cmp_finais.py` e `tail_seq.py` no scratchpad da sessão. Fontes: os logs do KMD recuperados e os logs da UMD pós-trava de cada pasta. A cauda do KMD e o rastro da 202 foram arquivados nesta pasta com SHA-256 (`SHA256-recuperados.txt`).

### 6.1 O último trabalho antes de cada trava (P0)

| Corrida | Último trabalho de GPU antes da parada | Antecedência | O que o DWM fazia |
|---|---|---|---|
| 199 | FILL de **11.059.200 B** (`Dst=0xCCA000`), sem linha de execução depois | 4,7 ms | `dwm.exe` (1608) em `WaitPaging`, à espera dessa paginação |
| 200 | FILL de **11.075.584 B** (`Dst=0x290000`) executado, depois IB gráfico de 280 DW retirado | 19 ms | Recriou o buffer linear do Present (`LAZY`, 2ª vez no processo) e parou entre `INSPECAO LINEAR_BUF` e `GRADE` |
| 202 | FILL de **11.075.584 B** (`Dst=0x290000`) executado, depois IB gráfico de 280 DW retirado | 17 ms | Idem: `LAZY` pela 2ª vez e parada entre `INSPECAO` e `GRADE` |

- **As três travas** pararam até 20 ms depois do FILL de zeramento de uma alocação **nova** de tela cheia (11 MB, CPU visível, segmento 1).
- **Por acaso é improvável.** Esses FILLs de 11 MB acontecem 18 a 66 vezes por corrida. Uma janela de 20 ms logo depois de um deles cobre ~0,4–0,9% de cada corrida, então três em três não é coincidência (P0 como correlação).
- **Na 200 e na 202 a sequência é idêntica até o endereço:**
  - FILL `Dst=0x290000`;
  - IB gráfico de 280 DW;
  - retirada;
  - o DWM lê o buffer pela BAR e para.
- **No rastro da 202,** a última amostra (231,42 s, 200 ms antes da parada) mostra o SDMA0 e o GFX **ociosos** e com os anéis drenados. O FILL fatal foi submetido depois dessa amostra.

### 6.2 O que NÃO separa as travas da 201 (que sobreviveu)

- A 201 fez o mesmo FILL de 11 MB em `Dst=0x290000` e sobreviveu.
- Às 08:08:05 a 201 fez a mesma recriação do `LAZY` depois de `res=-2` no DWM e sobreviveu. A leitura em grade terminou em 26–44 ms, como em outras 94 vezes.
- **A leitura da CPU pela BAR não é necessária:** na 199 o DWM não lia nada, esperava a paginação. Isso confirma a lição do doc 526 ("último acesso da CPU ≠ causa").

### 6.3 Leitura

O que se repete é o **FILL de paginação de uma alocação nova**, agora sempre a maior do sistema (tela cheia). Antes da T3.3 era um FILL de qualquer tamanho (doc 526, 14 de 14). As duas hipóteses vivas do doc 526 seguem sem separação:
- **H1, o pacote `CONST_FILL`;**
- **H2, o destino recém-mapeado** (primeira escrita da GPU sob PTEs recém-escritas).

O teste que as separa é o **X2b do doc 526 §8** (zerar por cópia em vez de `CONST_FILL`), que **nunca rodou**: a série pulou para a T3.3.

### 6.4 Diferença nova contra o Linux no próprio pacote (P1)

| | Linux `sdma_v5_0_emit_fill_buffer` | Nosso `Bc250SdmaEmitConstFill` |
|---|---|---|
| Cabeçalho | `SDMA_PKT_HEADER_OP(SDMA_OP_CONST_FILL)`, **fillsize=0** (byte) | `CONST_FILL \| (2 << 30)`, **fillsize=2** (DWORD) |
| Máximo por pacote | `fill_max_bytes = 0x400000` (4 MiB) | 256 KiB (X2a) |

- **Fonte Linux:** `referencias/linux_upstream_2026-09-29/sdma_v5_0.c:2033–2052`. **Fonte nossa:** `driver/core/bc250_sdma_packet_builder.c:171`.
- O Linux não programa `SDMA0_RELAX_ORDERING_LUT` (zero ocorrências), então esse registrador fica no POST nos dois lados e **não** é diferença.
- **Ressalva:** o Linux zera VRAM nova bem menos que o VidMm. Só faz isso com `VRAM_CLEARED`, enquanto o Windows pede um FILL para **toda** alocação nova. "O Linux não trava" pesa pouco contra a H1.
- **Ressalva (H):** se o `fillsize` muda o caminho de escrita no hardware, não sei. Fica como candidato, não como causa.

**Estado da máquina:** recuperação às 08:53, BasicDisplay Código 0 em 5 amostras, pacote experimental removido e fusível rearmado limpo. Boot frio antes de qualquer novo bind.

**NÃO VALIDADO EM HARDWARE:** desktop estável com 256 MB.
