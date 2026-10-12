# 708 — Corrida 268: DBG.2 em regime, fim ocioso sem IB em voo, trava no mapeamento da 1ª textura de um processo novo (desfecho C)

**Corrida:** 12/10/2026. Desligar às 15:02:09, bind às 15:04:41, trava ~15:07:27,79, reinício às 15:07:53.
**Candidato:** lab 3.416.1.0. O SYS da pasta de evidência confere com o assinado (`D993B704`); `DebugTrace=1`.
**Evidência:** `docs/evidence/2026-10-12_15-04-41-DESKTOP-HQAG6T4-lab-binding/`, com a coleta automática completa.
**Scripts de análise:** `scratchpad/ev268.py`, `lat268.py`, `tick268.py`.
**Critério:** [doc 707](707_DBG2_IB_DE_PAGINACAO_NO_RASTRO_MINERACAO_DA_FASE_DO_PROCESSO_NOVO_E_CRITERIO_CORRIDA268.md) §4.

---

## 1. Pré-condição (P0): completa

| Item | Resultado |
|---|---|
| Modo debug | `[DBG1_DEBUG_TRACE] chave=1` e `[DBG1_PA_FAIXAS]` presentes |
| Eventos novos | 51.373 `PAGING_OP`, 121.804 `PAGING_DW`, 20.543 `SPLIT_REC`, 40.910 `SUBMIT_SPLIT` |
| Cobertura do anel BEV1 | 285.418 de 285.419 eventos (1 perdido em 156,5 s) |
| `ib_reconstruido` | 6 IBs remontados |
| SDMA1 | **subiu desta vez** (`BringUp=OK`, 1 poll), com 14.200 faixas de PTE (P4.2 em regime); na 266 e na 267 não tinha subido |

O lote de eventos funcionou: ~1.800 eventos/s gravados sem perda.

## 2. O que aconteceu (P0)

### Carga e plataforma

| Medida | Valor |
|---|---|
| Processos da T3 completos | 29; a trava veio no 30º |
| FILLs | 829 |
| Despejos | 288 |
| FLUSH_TLB | 20.539 |
| Submissões ao SDMA0 / ao GFX | 7.868 / 2.370, **todas com RETIRE** |
| TDR, fault, timeout de tripé | zero |
| Endereço físico das PTEs | 630.225 PTEs de sistema conferidas, zero fora da RAM |
| Tctl | 78–85 °C |
| WHEA-Logger | nenhum evento |
| Teste de RAM | 0 erros (7.943 MB × 2) |

### Os últimos 400 ms

1. Entre o tique 1450 e a última rajada, **~400 ms sem nenhum evento**: GPU, paginação e GFX parados.
   - Nesse intervalo o 30º filho criava o device D3D11, e levou **732 ms**. O normal na corrida foi ~140 ms; houve duas criações lentas antes, de 309 e 538 ms, sem trava.
2. Rajada final de 57 eventos em 6,7 ms, começando 765 ms depois do INICIO do filho:
   - uma submissão do GFX, aposentada em 0,15 ms;
   - o VidMm montando o paging buffer da **1ª textura de 8 MiB** do filho: 2.048 PTEs, 448 + 3 × 512 + 64.
3. **A UMD do filho confirma** o mesmo instante:
   - `Allocate` 8.388.608 bytes;
   - `MapAllocation` com E_PENDING e fence 7003;
   - `MakeResident` com E_PENDING e fence 7004.

   A última linha do filho é das 15:07:27,796; a do DWM tem a mesma hora.
4. **O último evento gravado** é um `SPLIT_REC`, o FlushTlb registrado dentro do handler, a 772,5 ms do INICIO.
   - Em 20.542 de 20.543 FlushTlb da corrida, o handler termina e o `PAGING_OP FLUSH_TLB` aparece 0,55–2 ms depois (p50 0,587 ms; máximo 7,9 ms). Esse tempo é o commit do journal de GPUVA.
   - O único FlushTlb sem fim registrado é **o último**, o da trava.
5. **Os 6 últimos IBs do SDMA0 (fences 7863–7868) foram aposentados.**
   - As divisões e a máscara de VMID 8 (0x0100) estão nos lugares dos FlushTlb registrados.
   - Não há IB do SDMA0 sem RETIRE.

### Ponto cego: o que o anel de eventos não registra

O log do KMD tem duas atividades depois do último FlushTlb completo (−4,05 ms) que não estão no anel BEV1:
- **Uma submissão de paginação ao Node 0 em andamento.** Aparecem `CTX_PROBE` e `PT_WALK`, mas não o `SUBMIT_SPLIT` nem o `SUBMIT_SDMA`. Não chegou ao anel enquanto o rastro gravava, ou chegou depois do último dreno.
- **Um `COMPOSITE_BLT` de tela cheia (11.059.200 bytes) do DWM no SDMA0**, que terminou com prova de engine (`Prova=SOMENTE_ENGINE`).

Os blts de composição não geram evento BEV1. As migalhas de MMIO do último tique (1453) estão vazias, mas esse tique é anterior à rajada final.

### Handler do FlushTlb

Com a P1b, `Bc250VramCpuHdpNeeded()` é falso: o handler faz só `mfence`, `SplitRecord` e o commit do journal. **Zero MMIO no handler.**

## 3. Desfecho: C (fim ocioso)

**Assinatura C:** todo SUBMIT_SDMA tem RETIRE e não há IB em voo. Bate:
- SDMA0 7.868/7.868;
- GFX 2.370/2.370;
- nenhum IB sem RETIRE nos 6 remontados.

**Ressalva:** havia uma submissão ao Node 0 começando (`CTX_PROBE`/`PT_WALK` no log do KMD) que o rastro não alcançou. Não se pode excluir que ela tenha chegado ao anel no último ~1 ms.

**A e B não ocorreram:** o último IB não estava sem RETIRE.

**Implicação:** a hipótese da 267, "o SDMA0 parou dentro de um IB sem invalidação no meio", não é necessária. Aqui a máquina morreu com a GPU sem trabalho registrado e a CPU dentro do commit do journal, que é só memória.

## 4. Leitura

**P0, cruzando 263, 266, 267 e 268:**
- **O momento da trava é sempre o mesmo:** as alocações iniciais de um processo novo.
  - **263:** 3ª alocação, fence de paginação 7008.
  - **266:** depois das alocações.
  - **267:** primeira tabela de páginas, no `vkCreateDevice`.
  - **268:** 1ª textura, fences 7003/7004, 772 ms depois do INICIO.
- **Somando a 268 à mineração do doc 707:** 4 de 5 travas com o T3 rodando caíram nos primeiros 0,9 s de vida de um processo novo. Pelo acaso, há um filho vivo em ~21% do tempo.

**H:**
- O gatilho está ligado à **criação do espaço de endereço de um processo novo** (tabelas novas, primeira alocação, primeira VMID), não ao conteúdo de um IB específico. Na 267 havia IB em voo; na 268, não.
- Duas coisas aconteceram nos dois casos e não estão descartadas:
  1. o primeiro uso da raiz de tabela de páginas nova, quando a VMID do processo é programada;
  2. um blt de composição ou uma submissão concorrente no SDMA0, que o rastro não registra.

## 5. Recuperação e pendências

- **Estado da placa:** Código 43 pelo fusível (`StartDevice RECUSADO: Fusivel disparado`). O reinício depois da trava foi por reset, com evento 41.
- **Antes do próximo bind:** `BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset` e desligar.
- **Lacunas de instrumentação desta corrida:**
  - blts de composição e começo de submissão sem evento no anel;
  - programação de VMID e instalação de raiz sem evento;
  - migalhas de MMIO só no tique, não no dreno.

**NÃO VALIDADO EM HARDWARE:** a causa da trava.
