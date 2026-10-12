# 705 — Mineração de 46 travas: paginação ativa no fim e a janela cega de um tique

**Data:** 12/10/2026. **Natureza:** mineração offline, sem corrida.

**Fontes:**
- o rastro durável cumulativo `C:\BC250_PAGING_TRACE.bin` (1,31 GB), fatiado por corrida pelo `start_offset` de cada `paging-trace-baseline.json` e cortado no recuo do `writer_tick`. Isso recupera também as corridas **sem** trava, que não têm `.bin` arquivado;
- os logs da UMD (`umd-logs-pos-travamento`);
- `BC250_MONITOR.csv`.

**Scripts e saídas:** `C:\Users\Daniel\AppData\Local\Temp\claude\…\scratchpad\mine\` (`build.py`, `analyze_*.py`, `out_*.txt`). Nenhum arquivo do repositório foi alterado pela mineração.

**Amostra:**
- 46 corridas com trava dura e eventos BEV1 (05/10 11:08 a 12/10 08:31);
- 7 corridas sem trava com BEV1 (222, 226, 229, 237, 238, 241, 256);
- excluídas: 211 (tela azul), 251 (StartDevice) e as que caíram em Código 43.

**Confiança:** contagens P0; interpretação H. A mineração foi feita por um agente e conferida por amostragem: o tamanho do arquivo vivo e as durações das corridas 236 e 263–266 batem com as medidas dos docs 693–702.

---

## 1. O que precede a trava (P0)

### 1.1 A máquina morre dentro do tique seguinte ao último registro

- **Último evento BEV1:** a 1–94 ms do último tique em 45 de 46 corridas.
- **Última linha da UMD**, alinhada pelo QPC: 0–120 ms **depois** do último tique em 41 de 45.
- **Os tiques são normais até o fim:** 101–191 ms, sem degradação antes da morte.

**Conclusão:** o evento que dispara a trava cai numa janela de até ~110 ms que **o rastro nunca grava**. Por isso as últimas linhas sempre parecem "tudo aposentado, GPU ociosa".

### 1.2 Paginação ativa no fim

Deltas de 1 s do BPG1, nos 3 s finais.

| Condição nos 3 s finais | Fim das travas (46) | Mesmas corridas, >10 s do fim | Outras corridas, mesmo tempo desde o bind | Corridas sem trava |
|---|---|---|---|---|
| houve FILL | **0,96** | 0,61 | 0,63 | 0,26 |
| houve despejo | **0,87** | 0,55 | 0,56 | 0,20 |
| FILL ≥ 20/s | 0,48 | 0,15 | 0,27 | 0,10 |
| FLUSH_TLB ≥ 300/s | 0,63 | 0,24 | 0,34 | 0,13 |
| paginação totalmente parada | **0,02** | 0,37 | 0,34 | 0,72 |

**Só 1 de 46 travas veio com a paginação parada.**

### 1.3 Últimos 20–50 ms (BEV1)

| Rótulo | Fim | >10 s do fim | Sem trava |
|---|---|---|---|
| FILL ≤ 50 ms e SUBMIT do SDMA0 ≤ 20 ms | **0,48** | 0,24 | 0,22 |
| SUBMIT do SDMA0 ≤ 20 ms | 0,70 | 0,43 | 0,37 |
| TRANSFER ≤ 50 ms | 0,13 | 0,16 | 0,08 |
| SUBMIT do GFX ≤ 50 ms | 0,57 | 0,50 | 0,50 |

**Nenhum op, tamanho ou tipo é exclusivo do fim.** Os FILLs do fim (op 0x838, 0x200E, 0x470, 0x5) são os mais comuns da corrida inteira.

Op do BEV1, lido no código:
- **FILL/TRANSFER:** dwords úteis do IB de paginação. Por exemplo, 0xE0 = 32 cópias = TRANSFER de 8 MiB, e 0x5 = CONST_FILL.
- **DISCARD:** o segmento.

### 1.4 UMD: espera de paginação de alocação nova

- **Em 19 de 46 travas**, algum processo termina com `WAIT_PAGING phase=ENTER` sem LEAVE, entre −117 e +7 ms do último tique.
- **Quem:** Explorer 9, menu 4, gerador de processos 4, SearchApp 2, Clima 2, DWM 1.
- **Controle:** a fração de tempo com alguma espera de paginação aberta tem mediana de 4,5% nas corridas.
- **Os 12 casos legíveis seguem a mesma sequência:** `Allocate → MapAllocation (E_PENDING) → MakeResident (E_PENDING) → WaitPaging`, com a fence alvo = atual + 2. Ou seja, a espera pela paginação de uma **alocação nova**.

### 1.5 Risco sem memória

- **Coeficiente de variação perto de 1:** 0,98 no tempo até a trava e 0,93 nos FILLs até a trava. É o comportamento esperado de um risco constante por operação.
- **Taxa ingênua**, com as corridas sem trava como censuradas: **~1 trava a cada 1.040 FILLs**, ou ~450 despejos, ou ~240 s.

### 1.6 Fatores que não explicam

| Fator | Achado |
|---|---|
| Temperatura (19 corridas com monitor) | Tctl final de 76,5 a 94,4 °C, sem limiar |
| VRAM dedicada no fim | de 41 a 233 MB, sem limiar |
| Hora do dia | sem padrão |
| Processo novo | ×2 (39% contra 19% com um processo iniciado ≤5 s antes do fim). Pode ser o mesmo efeito da paginação: processo novo → alocação nova → paginação |

## 2. Leitura (H)

- A trava está ligada ao **caminho de paginação em atividade**: alocação nova → FILL/despejo/PTE no SDMA0 → espera da fence de paginação. Não está ligada a um pacote ou tamanho específico, nem a temperatura ou ocupação da VRAM.
- Um risco baixo e constante (~1 em 1.000 FILLs) combina mais com uma **corrida de tempo** (ordem entre CPU, GPU e memória) do que com um conteúdo inválido determinístico.
- **Ressalva:** um endereço inválido raro, que só aparece em algumas alocações, também daria esse perfil. É exatamente o que o validador da DBG.1 testa.
- O gatilho provável é a **próxima** operação de paginação, a que o `WAIT_PAGING` aberto espera, executada depois do último tique gravado.

## 3. O que isto mudou no plano

1. **DBG.1b** (doc 704): no modo debug, o rastro é drenado a cada paginação e a cada submit do SDMA0, não só a cada 100 ms. Isso fecha a janela cega. Entrou no pacote da 267 (3.415.1.0).
2. **Teste A/B barato para depois da 267** (proposta da mineração): serializar ou espaçar a paginação, esperando a fence de cada FILL antes do seguinte. Se a taxa de ~1/1.000 cair, o mecanismo é de ordem ou concorrência. Esse teste só faz sentido depois de a 267 dizer se há endereço inválido.
3. **Métrica de comparação:** FILLs até a trava, com risco por FILL. Uma mudança só pode ser chamada de efeito se mover a taxa de ~1/1.040 além da dispersão. Isso exige várias corridas por configuração.

## 4. Ressalvas

- O BEV1 perde 40–84% dos eventos (antes da DBG.1). A ausência de FILL numa janela não prova que não houve FILL.
- O log do KMD é amostrado e não foi minerado nesta passada.
- As corridas com KD (246b–250) incluem pausas do depurador.
- As corridas não são independentes: o pacote muda a cada uma.
- O alinhamento UMD↔KMD tem erro de alguns ms (relógio de 15,6 ms na UMD). Três corridas têm defasagem de 290–740 ms, sem explicação.

**NÃO VALIDADO EM HARDWARE:** a causa da trava.
