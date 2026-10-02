# 511 — Corrida 158: DX.96-HDP não impediu a trava; migalhas nítidas mostram UpdatePageTable em curso; FPS medido (1,66) e dividido

> Corrida de 28/09/2026. Bind às 15:58:33, candidato 3.307.1.0 (DX.96-HDP + FPS.3b + DX.95 + DX.94), sem KD, harness aos 30 s. Trava por volta de 16:00:46 (~2 min 13 s depois do bind, ~32 s depois de o harness terminar). Reset rápido; Kernel-Power 41 às 16:01:19.
>
> Evidência:
> - pasta `docs/evidence/2026-09-28_15-58-33-DESKTOP-HQAG6T4-lab-binding/`, com a cauda recuperada (a corrida vai até a linha ~242.847);
> - logs da UMD em `umd-logs-pos-travamento/`, com o harness completo (pid 8240).

## 1. Relato (cinco pontos)

**1. Houve avanço?** De diagnóstico, **sim**:
- o primeiro FPS medido depois da FPS.1 e da FPS.3b;
- a primeira divisão do quadro;
- o primeiro retrato nítido das migalhas no instante da trava.

Funcional, não: a trava continuou.

**2. Sucessos (P0):**
- `[DX96_HDP] HDP_MISC_CNTL ... encontrado=0x2D600861 ... escrito=0`. **O bit 0 já vem ligado do POST**: o toggle antigo o desligava em execução, e agora ele fica no estado do firmware.
- A fence do SDMA continua visível: 9.671 `IB EXECUTADO`, zero `IB_TRIPOD TIMEOUT` e zero `Blt Timeout`. **O desfecho C não ocorreu:** o toggle não mascarava coerência.
- A FPS.3b se repetiu: SDMA submit → conclusão com p50 1,51 ms e p90 1,56 ms (n=6.752).
- DX.95 limpa: uma troca do VMID8, zero TDR.
- **Harness 60/60** `PRESENT_OK`: **1,66 FPS**, 603 ms por quadro, `pfnPresentCb` p50 453,7 ms.

**3. Falhas (P0):**
- **Trava dura** com tela preta, sem BugCheck.
- **O FPS não mudou:** 1,66 contra 1,64 na 150 e 1,71 na 153, apesar da paginação 5× mais rápida e da conclusão 3× mais rápida.

**4. Veredito contra o doc 510:** **B.** O toggle de `HDP_MISC` **não** é o gatilho da trava. A correção fica, porque é o comportamento do upstream e não houve regressão de coerência.

**5. Recuperação:**
- O fusível vai recusar o StartDevice.
- Recuperação, `-Reset` do fusível e boot frio com o monitor ligado.
- **NÃO VALIDADO EM HARDWARE:** a causa da trava.

## 2. Migalhas nítidas no instante da trava (P0, reset rápido e pouco decaimento)

| Processador | Estado | Sequência |
|---|---|---|
| CPU00 | `PAGING_DDI` op **0x0B = UPDATE_PAGE_TABLE**, fase **ANTES** (dentro da DDI) | 589.025 |
| CPU00 | último acesso da máquina: `SDMA_REG_READ 0xFC4` (leitura de confirmação de `HDP_READ_CACHE_INVALIDATE`), concluída | 589.031 = `ultimaSeq` |
| CPU09 | `SUBMIT_DDI` etapa **0x14 = SNAPSHOT_IB** (validação do IB pela caminhada nas tabelas de página), fase ANTES | 269.024.482 = 2²⁸ + **589.026** (um bit alto trocado) |

**Leitura (H):** no instante da trava, uma CPU estava **escrevendo PTEs** na tabela de páginas (que fica na VRAM, pela BAR) e outra estava **lendo tabelas de página pela BAR** para validar um IB. O último acesso de registrador foi, de novo, o fim da sequência do HDP. Sem o toggle de `HDP_MISC`, o último passo passou a ser a leitura de `0x3FC4`, o que é consistente com a hipótese de que a posição dela é só frequência.

## 3. O volume da caminhada de páginas pela CPU (P0)
Em 128 s de corrida, a validação do submit caminhou **1.013.696 páginas** (~7.900 por segundo):

| Faixa | Caminhadas | Páginas |
|---|---|---|
| DEST | 9.584 | 711.952 |
| SRC | 4.018 | 290.142 |
| IB | 9.671 | 9.698 |
| IB do GFX (Node 1) | 896 | 1.904 |

Cada página custa leituras de PDE/PTE pela BAR (frente 2 do doc 509). **Esse tráfego da CPU sobre a VRAM cresce com a paginação**, exatamente como a taxa de trava, e roda **ao mesmo tempo** que o VidMm escreve nas mesmas tabelas. A frente 2 também apontou que a BAR inteira está mapeada write-combined pelo nosso driver, enquanto o VidMm mapeia as tabelas por outro mapeamento (tipo de cache a confirmar).

## 4. A divisão do quadro do harness (P0, `tools/analyze-fps3-retire.py`)

| Dentro de cada `pfnPresentCb` (média de 60) | Valor |
|---|---|
| Duração | **454,5 ms** |
| IBs de paginação | 25,9 |
| Esperando conclusão do SDMA | 22,7 ms |
| Dentro dos nossos FLUSH_TLB | 121,9 ms |
| Submits GFX | 2,1 |
| **Resto (VidMm entre operações, outras DDIs, espera de GFX)** | **309,9 ms** |

A espera por quadro ficou em ~450 ms em todas as corridas medidas (150: 440; 153: 449; 158: 454), enquanto os IBs por quadro **subiram** de 19,5 para 25,9. **H:** o VidMm ocupa o tempo liberado com mais paginação (despejo e restauração contínuos, que é o ~2,6 GB de BOs da RADV do doc 495). Isso aponta para **reduzir a paginação**, não só acelerá-la: a memória unificada e o orçamento de VRAM (M1b) viram trabalho de FPS.

## 5. Próximo passo proposto (uma variável)

**DX.97 — caminhada de páginas só nas pontas das faixas:**
- no submit, caminhar o IB inteiro (é pequeno) e **só a primeira e a última página** das faixas SRC e DEST, em vez de todas;
- continuam valendo a validação de que a faixa existe e o posicionamento dos canários do tripé (que usam o início do destino; a conferir no código antes de mudar);
- sai de ~7.900 para algumas centenas de páginas por segundo.

**Critério a escrever antes:**
- trava some em duas corridas seguidas → a leitura concorrente das tabelas pela BAR é o gatilho;
- trava igual → não é, e o próximo ensaio é o `powercfg` sem C-states.

Efeito colateral medível: o tempo do submit cai, e o quadro mostra quanto disso era FPS.
