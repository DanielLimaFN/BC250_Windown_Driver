# 706 — Corrida 267: DBG.1 com zero endereço inválido e o último milissegundo na tabela de um processo novo

**Data:** 12/10/2026, bind às 14:18:00.
**Candidato:** lab 3.415.1.0 (SYS assinado `90700F78`), `DebugTrace=1`, mesma carga T3.
**Evidência:** `docs/evidence/2026-10-12_14-18-00-DESKTOP-HQAG6T4-lab-binding/`, com coleta automática completa:
- `paging-trace-this-bind-RECUPERADO.bin` (6.366.120 bytes);
- `display-log-this-bind-RECUPERADO.txt`;
- `umd-logs-pos-travamento/`;
- `ramtest-pos-trava.txt`;
- `mca-whea-pos-trava.txt`.

**Scripts de análise:** `scratchpad/ev267.py` e `var267.py`, que leem os eventos BEV1 com o decodificador do repositório.

**Critério:** [doc 704](704_DBG1_MODO_DEBUG_VALIDADOR_DE_ENDERECO_FISICO_PACOTE_3415_E_CRITERIO_CORRIDA267.md).

**Boot:** o desligamento frio foi às 13:12. Depois dele vieram quatro reinícios do Windows Update, sem bind no meio. Por isso o preflight foi liberado com `BC250_ALLOW_WARM_BOOT=1`.

---

## 1. Pré-condição (P0)

- `[DBG1_DEBUG_TRACE] chave=1 (present=1): MODO DEBUG`.
- `[DBG1_PA_FAIXAS]`: 7 faixas de RAM do Windows; faixa proibida = carve-out da VRAM `[0x460000000,+0x10000000)`.
- FT.1, MEC.2 e `GfxWaveProbe=0` em efeito.
- **SDMA1 não subiu** (`PROOF_TIMEOUT`, `polls=20000`): segunda corrida seguida, depois da 266. As PTEs foram escritas no SDMA0, como na 266.

## 2. Desfecho B: zero endereço inválido, e trava igual

### Validador de endereço físico (BPA1, último registro)

| Contador | Valor |
|---|---|
| PTEs de memória de sistema conferidas | **591.075** |
| fora da RAM | 0 |
| carve-out da VRAM | 0 |
| abaixo de 1 MiB | 0 |
| sem tabela | 0 |
| entradas do segmento 2 | 0 |
| amostras BPV1 | 0 |

- **Leitura:** a classe "GPU recebe PTE de sistema para um endereço que não é RAM" sai das causas (P0, nesta corrida).
- **Ressalva:**
  - Todas as folhas de sistema vieram como segmento 0, isto é, endereço físico direto. Nenhuma passou pelo segmento 2, então a parte "endereços crus do segmento 2" do critério B não foi exercitada; o desfecho D fica sem objeto nesta configuração.
  - As folhas de VRAM (segmento 1) não são conferidas pelo validador.

### Corrida

| Medida | Valor |
|---|---|
| Duração até a trava | ~213 s |
| Processos completos da T3 | 29 |
| Momento da trava | dentro do 30º processo, durante o `vkCreateDevice` (o último log da RADV é a consulta dos heaps de memória) |
| FILLs | 644 |
| Despejos | 245 |
| FLUSH_TLB | 17.042 |
| Submissões ao SDMA0 / ao GFX | 6.570 / 1.666 |
| TDR, fault ou timeout de tripé | zero |
| Tctl | 76,9–86,1 °C |
| Teste de RAM | 0 erros |
| WHEA-Logger | sem evento |

## 3. A DBG.1b fechou a janela cega (P0)

- **Cobertura do anel BEV1:** 42.480 de 42.526 eventos (99,9%), contra 16–60% antes.
- **Último evento gravado:** um `PAGING_PTE`.
- **Quando a máquina morreu:** depois que esse evento foi gravado no disco e antes do próximo dreno. Os eventos de paginação saem a cada ~0,7 ms nesse trecho, então a morte veio em cerca de 1 ms (H, pela cadência).

### Últimos 8 ms (t = 0 é o último evento)

| t (ms) | Evento |
|---|---|
| −7,97 | PTE folha: VA 0x244000 → VRAM 0x25000 |
| −7,11 | FILL (CONST_FILL) de 4 KiB em 0x244000 |
| −7,02 … −3,30 | PTEs que desmapeiam 0x23E000–0x243000: tabelas do processo anterior, que saiu |
| −2,45, −1,55, −0,74 | PTEs que remapeiam 0x23E000, 0x23F000 e 0x240000 para as tabelas novas (VRAM 0xE902000 e 0xE903000) |
| −1,63 | PTE: 511 entradas em 0x200001000 invalidadas |
| −0,78 | PTE folha: VA 0x200000000 → VRAM 0x25000 (1ª alocação do processo novo) |
| **−0,60** | **SUBMIT_SDMA FenceId 6579, 512 bytes, IB em VA 0x23AC00 — sem RETIRE** |
| 0,00 | PTE nível 1: 512 entradas em 0x200000000 → tabela nova 0xE902000 |

### A mesma sequência passou 32 vezes antes

O passo "PTE nível 1 em 0x200000000" (o processo novo ganhando sua primeira tabela) aconteceu **33 vezes** na corrida. Nas 32 primeiras:

- o próximo evento veio ~0,05 ms depois;
- o submit ao SDMA0 que precede o passo saiu entre 0,8 e 8,5 ms antes (normalmente ~2,0 ms) e **foi aposentado** em todas.

Na 33ª, a da trava, o submit saiu 0,60 ms antes, foi o mais próximo da série, e **não teve RETIRE**.

### Latência submit → retire do SDMA0 na corrida (n = 6.540)

| p50 | p90 | p99 | máximo |
|---|---|---|---|
| 0,065 ms | 0,152 ms | 0,394 ms | 0,641 ms |

O 6579 estava sem RETIRE depois de 0,60 ms. Isso está acima do p99 e abaixo do máximo. Como sinal, é fraco (P2): a interrupção ou o DPC podem ter atrasado.

**O que mudou no conteúdo do IB.** No padrão normal, o IB de ~448–512 bytes leva o FILL, os desmapeamentos e o primeiro remapeamento; a folha de 0x200000000 e as tabelas de nível 1 e 2 vão num IB de 9.216 bytes ~5 ms depois. No 6579, o lote também incluiu:
- os remapeamentos de 0x23F000 e 0x240000;
- a invalidação de 511 entradas;
- a folha de 0x200000000.

Ou seja, o SDMA0 escreveu, no mesmo IB, PTEs pelos endereços do processo de paginação (0x23E000–0x240000) cujo próprio mapeamento acabara de mudar.

## 4. Leitura

- **P0:**
  - nenhuma PTE de sistema aponta para fora da RAM;
  - a trava caiu na criação da primeira tabela de páginas de um processo novo, num passo que já tinha passado 32 vezes;
  - o último submit ao SDMA0 (6579) não tem RETIRE gravado.
- **H, n=1:**
  - o SDMA0 parou dentro do 6579;
  - o lote misturou, num IB só, a troca do mapeamento das tabelas no endereço do processo de paginação e a escrita por esse mesmo endereço.

  Isso exige uma invalidação de TLB entre as duas operações. É o tipo de fronteira que a 4c.4 resolve dividindo o IB em cada FlushTlb, e que já errou uma vez (doc 576). Isso **não está provado**: não sei quais dwords o 6579 continha, nem onde o VidMm pôs o FlushTlb.
- **Conferido com o histórico:**
  - a pista do doc 691 ("trava até ~3 s depois de um processo novo começar a usar a GPU");
  - o doc 705 (paginação ativa no fim, espera de paginação de alocação nova);

  As duas batem com isto.

## 5. Próximo passo proposto (para decisão)

**DBG.2, só medição, no modo debug:**
- gravar no rastro os dwords de cada IB de paginação do SDMA0 (até 512 bytes; o 6579 tinha 512) e a posição de cada ponto de divisão e de cada FLUSH_TLB;
- marcar cada FLUSH_TLB pedido pelo VidMm com a sequência do evento.

Na próxima trava isso diz exatamente o que o SDMA0 executava e se havia invalidação entre a troca do mapeamento e o uso. Sem registrador novo.

**Alternativa A/B, mudança de comportamento:** forçar invalidação de TLB e espera antes de qualquer escrita por endereço do processo de paginação cujo PTE mudou no mesmo lote.

## 6. Recuperação e pendências

- **Estado da placa:** o reinício depois da trava foi um **reiniciar**, e a placa está em Código 43 pelo fusível (`StartDevice RECUSADO: Fusivel disparado`).
- **Antes do próximo bind:** `tools\BC250_RECOVERY.cmd`, `tools\inspect-boot-fuse.ps1 -Reset` e **desligar**.
- **SDMA1:** `PROOF_TIMEOUT` em 2 binds seguidos (266, 267): investigar antes de contar a P4.2 como em regime.
- **Preflight:** confunde "reinício do Windows Update depois de um desligamento frio" com boot morno. Proposta: procurar o último "Desligar" e conferir que não houve bind desde então.

**NÃO VALIDADO EM HARDWARE:** a causa da trava; o mecanismo da seção 4.
