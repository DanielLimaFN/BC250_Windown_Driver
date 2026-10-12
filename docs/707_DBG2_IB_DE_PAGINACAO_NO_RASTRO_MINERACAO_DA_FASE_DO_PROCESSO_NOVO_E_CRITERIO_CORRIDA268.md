# 707 — DBG.2: o IB de paginação no rastro, mineração da fase do processo novo e critério da Corrida 268

**Data:** 12/10/2026, offline. **Base:** [doc 706](706_CORRIDA267_DBG1_ZERO_ENDERECO_INVALIDO_E_O_ULTIMO_MILISSEGUNDO_NA_TABELA_DE_UM_PROCESSO_NOVO.md).

**Pergunta.** Na 267 a máquina morreu ~1 ms depois de o SDMA0 receber o IB de paginação 6579, que não tem RETIRE. O que esse IB continha? Havia invalidação de TLB entre a troca do mapeamento das tabelas e a escrita feita por elas?

---

## 1. Mineração (sem corrida)

**Fonte:**
- 47 corridas com trava, fatiadas do rastro cumulativo;
- logs da T3 (`BC250_PROCESSO_NOVO.log`).

**Scripts:** `scratchpad/mine2/` (`q1_t3.py`, `q23.py`, `q4.py`, `align_check.py`).

**Confiança:** a mineração foi feita por um agente e conferida em parte. O caso da 267 bate com a minha análise do doc 706: fence 6579, sem RETIRE.

### 1.1 A trava cai nos primeiros 0,9 s de vida de um processo novo (P0, n=4)

| Corrida | Trava − INICIO do último filho | Último passo do filho no log | Fase |
|---|---|---|---|
| 263 | 416 ms | antes de "ALOCADAS" (típico 341 ms) | durante as alocações |
| 265 | 4.523 ms | FIM 2,5 s antes | entre ciclos |
| 266 | 874 ms | ALOCADAS e "MEMORIA depois" | depois da espera de 500 ms |
| 267 | 85 ms | só INICIO (`DEVICE criado` sai em ~145 ms) | criação do device |

As corridas 263b e 264 travaram antes de a T3 começar.

- **Base:** fora do ciclo da trava, há um filho vivo em 21% do tempo.
- **Resultado:** 3 das 4 travas caíram com filho vivo, todas nos primeiros 0,9 s. Pelo acaso, P ≈ 0,01–0,03.
- **Leitura:** sugestão forte, não prova (n=4, H). Cada trava parou num passo diferente do filho.

### 1.2 O último envio ao SDMA0 sem RETIRE (P0)

- **Nas 47 travas:** 5 terminam com um SUBMIT_SDMA sem RETIRE.

| Corrida | Fence | Bytes | SUBMIT → último tique |
|---|---|---|---|
| 06/10 10:00 | 13111 | 9.600 | 8,6 ms |
| 06/10 11:35 | 11065 | 32.864 | 9,6 ms |
| 07/10 09:16 | 11931 | 416 | 11,9 ms |
| 07/10 22:49 | 2840 | 192 | 60,5 ms |
| 12/10 14:18 (267) | 6579 | 512 | 15,7 ms |

- **Perda de eventos não explica.** Esperado só por perda: 0,5 caso, pelos SUBMITs a mais de 2 s do fim, que ficam sem RETIRE em 0–6% por corrida.
- **Seq contínuo:** nas 5 não falta nenhum seq entre o SUBMIT e o fim.
- **Latência normal** (101.033 SUBMITs): p99,9 2,1 ms e máximo 12,8 ms. As 5 ficaram 8,6–60,5 ms sem RETIRE.
- **Leitura (H):** em ~1/10 das travas há um IB do SDMA0 que não terminou. Nas outras 42 o fim é ocioso, como já se sabia.

Duas hipóteses continuam abertas:
- o SDMA0 parou dentro do IB;
- a aposentadoria parou junto com a máquina.

### 1.3 O que não se destacou (P0)

- **O FILL de 4 KiB por CONST_FILL como último FILL:** 2 de 47, contra 0,7 esperado.
- **Último FILL mais comum:** 64 KiB (24 de 47).

### 1.4 Exploratório (H)

- **Ritmo de SUBMIT_SDMA nos últimos 200 ms:** 2,3× o de janelas aleatórias da mesma corrida; acima da média em 39 de 46.
- **Ressalva:** o dreno final é mais completo do que o do meio da corrida.

## 2. DBG.2 (só no modo debug, só memória)

**Nenhuma leitura ou escrita de registrador nova. Com `DebugTrace=0` nada disto roda.**

### Eventos novos no anel BEV1 (`bc250_mmio_breadcrumb.h`)

| Tipo | Quando | Conteúdo |
|---|---|---|
| 9 `PAGING_OP` | todo `BuildPagingBuffer` | operação, GPUVA onde começou no paging buffer, bytes que o driver escreveu, NTSTATUS e um elo de 8 bits |
| 10 `PAGING_DW` | logo depois | os primeiros 16 dwords emitidos, 4 por evento, com o mesmo elo |
| 11 `SPLIT_REC` | todo FlushTlb registrado | GPUVA do ponto e máscara crua |
| 12 `SUBMIT_SPLIT` | todo IB indireto do SDMA0 | máscara de invalidação no anel antes do IB, nº de pontos e de órfãos, e cada ponto (dw, máscara) |

### Outras mudanças no driver

- **Rastro em lote:** cada dreno grava até 256 registros por `ZwWriteFile` e um flush por lote. Antes eram um `ZwWriteFile` e um `ZwFlushBuffersFile` **por evento**, o que não acompanharia ~5× mais eventos.
- **Espelho em RAM:** passa de 512 para 2.048 eventos; o dreno no modo debug vai até 1.024.

### Decodificador

`tools/decode-bc250-paging-trace.py` ganhou `ib_reconstruido`, que remonta os 6 últimos IBs do SDMA0:
- operações em ordem de dw, com dwords e nome do opcode SDMA (só os da fonte única `bc250_sdma_packet_builder.h`);
- FlushTlb caídos no buffer;
- divisões realmente usadas no envio;
- se o IB foi aposentado.

O VidMm reusa paging buffers, então só contam operações montadas depois do envio anterior do mesmo intervalo. Testado com eventos sintéticos, incluindo reuso de buffer e operação de 0 bytes no mesmo endereço; a primeira versão perdia o FlushTlb nesse caso e foi corrigida.

### Limite

16 dwords por operação. Um `UPDATE_PAGE_TABLE` de 512 entradas mostra o cabeçalho do pacote e as primeiras entradas, não todas.

### Preflight

O de 268 aceita reinícios depois do último "Desligar" se não houve bind nem desligamento sujo (41) desde então. Na 267, os reinícios eram do Windows Update.

## 3. Pacote 3.416.1.0

| Item | Valor |
|---|---|
| DriverVer | `10/12/2026,3.416.1.0` (fonte 3.416.0.0) |
| SYS assinado | `D993B704D223D53522EE576047F36F2775A4CA44BB766AD1505A8398530D78BE` |
| SYS fonte (lab) | `16E3DE04117491EC6E7D0B1D9FAD8AA038C15445EBB3C6723FECF5339656F908` |
| INF | `8CBD94DBE6517FADC5D824F20FB9902C39794D9AC5EEEAF443FD6EFE642991CF` (`DebugTrace=1`, `GfxWaveProbe=0`) |
| CAT | `C07019B080BC761A22F5DA92F84E9DB337B9BE6D92DDE009B2C8B31003C6ED47` |
| UMD, RADV, T3 | iguais à 267 (`B73F9BDA`, `B4AE2E14`, `768C82F7`) |
| Cópia | `driver/wdk/package/output_lab_3.416.1.0_268` |

**Verificações:**
- `build-contract.bat`: EXIT 0.
- Build de laboratório (`-Execute`): EXIT 0. A 1ª chamada foi sem `-Execute` e o empacotador recusou o SYS obsoleto, como deve.
- Empacotador: EXIT 0.
- `audit-bc250mcdm-lab-package.ps1`: PASS.
- `audit-bc250mcdm-safety.py`: PASS.
- **Portão novo `audit_dbg2_paging_ib_no_rastro`, provado por 4 mutações:**
  1. sem o teste do modo debug no registro;
  2. sem o flush do resto do lote;
  3. `SUBMIT_SPLIT` fora do modo debug;
  4. `SPLIT_REC` fora do modo debug.

  A mutação 2 passou na 1ª versão do portão (a chamada dentro do laço mascarava), e o portão foi endurecido.
- `PAGING_OP`, `PAGING_DW`, `SPLIT_REC` e `SUBMIT_SPLIT` estão no SYS.

## 4. Critério da Corrida 268 (escrito antes)

**Lançador:** `tools/EXECUTAR_CORRIDA268_DBG2_IB_DE_PAGINACAO_600S.cmd`. Mesmo roteiro da 267, com a T3.

**Antes:** `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e **desligar**.

**Pré-condição** (sem ela não há desfecho):
- `[DBG1_DEBUG_TRACE] chave=1`;
- eventos `PAGING_OP` no rastro, com dwords;
- cobertura do anel ≥ 99% no último segundo;
- `ib_reconstruido` não vazio.

### Desfechos

- **A — o último IB sem RETIRE escreve por um endereço cujo PTE mudou no mesmo IB, sem invalidação no meio.**
  - *Assinatura:* em `ib_reconstruido`, o último IB tem `aposentado: false`. Contém um `UPDATE_PAGE_TABLE` que remapeia um endereço do processo de paginação, seguido de escrita por esse endereço. Não há divisão nem FlushTlb entre os dois.
  - *Implicação:* candidata forte a mecanismo. Próximo: A/B forçando invalidação nesse ponto.
- **B — o último IB sem RETIRE tem a invalidação no lugar certo.**
  - *Assinatura:* divisão (dw e máscara com a VMID 8) entre a troca e o uso.
  - *Implicação:* "falta de invalidação" sai. Ler os pacotes do IB para achar o que mais ele faz.
- **C — fim ocioso:** todo SUBMIT_SDMA tem RETIRE e não há IB em voo, como em 42 de 47.
  - *Implicação:* a trava não está num IB do SDMA0. A DBG.2 ainda descreve a última paginação, mas a pergunta muda.
- **D — 600 s sem trava.**
  - *Implicação:* dispersão. O modo debug muda o tempo, então não vale como efeito.
- **E — fora do mapa.** Por exemplo:
  - perda de eventos > 1%;
  - paginação visivelmente mais lenta;
  - `PAGING_OP` sem dwords;
  - TDR.

  *Implicação:* descrever antes de julgar.

**Regra (Lei 10):** desfecho parcial não é desfecho. A e B exigem o último IB remontado; sem ele não há A nem B.

**NÃO VALIDADO EM HARDWARE:** a DBG.2 inteira.
