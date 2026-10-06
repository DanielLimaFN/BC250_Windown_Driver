# 570 — Corrida 206 (4c.2): flush pelo anel rodou ~1.100 vezes, e BugCheck `0x10E`/`0x34` ao abrir o menu, com os mesmos argumentos da 197

**Data/hora:** 02/10/2026, bind 15:36:33, BugCheck ~15:38:30, reboot 15:38:42 · **Candidato:** lab **3.353.1.0** (SYS `CAC00090…`) · **CMOS:** 256 MB
**Evidência:** `docs/evidence/2026-10-02_15-36-33-DESKTOP-HQAG6T4-lab-binding/`. O BugCheck matou o watchdog; a cauda do log do KMD foi resgatada pelo offset do baseline (`4.893.434.960`) em `display-log-this-bind-RECUPERADO.txt` (5.315.507 bytes, SHA-256 `ce4a496f…4855db`). Os logs da UMD na pasta são só o retrato de ~10 s depois do bind (sem `-FINAL`).
**Critério:** [doc 569 §3](569_4C2_FLUSH_TLB_PELO_SDMA_NO_ANEL_E_CRITERIO_CORRIDA206.md).

---

## 1. O que a 4c.2 fez (P0)

- `[VMFLUSH_PROBE] VEREDITO ANEL: PROVADO` (mesmas cinco provas da 205) e `[RING_TLB_FLUSH] ARMADO`.
- Flush pelo anel **emitido e crescendo**: linhas `#1`..`#8`, `#512` e `#1024`; última submissão com `FenceId=1108`. Máscaras vistas: `0x0380` (VMIDs 7, 8, 9) no começo, `0x039E` e `0x0FEE` com o pool cheio.
- 12.305 `FlushTlb` no build (`operation=12`), todos sem MMIO de invalidação.
- **Zero** `RING_TLB_FLUSH RECUSADO` (o anel nunca ficou sem espaço por 1 s), zero `GFXHUB_FAULT`, zero `TDR_QUIESCE`, zero `IB_TRIPOD TIMEOUT`, zero `IB NAO EXECUTADO`. 1.107 `IB EXECUTADO`, 450 submissões gráficas.

## 2. Como terminou (P0)

- **BugCheck `0x10E` (`0x34`, `0xB`, `0x1`, `0x0`)**, `MEMORY.DMP` de 868.550.266 bytes às 15:38. O Daniel estava abrindo o menu.
- **São exatamente os argumentos da Corrida 197** (doc 548 §10). Lá a pilha (P0) foi `SearchApp.exe → DxgkLock → VidMmBeginCPUAccess → SetupAllocationForCPUAccess → VIDMM_RECYCLE_MULTIRANGE::Rotate`: um `Lock` da nossa UMD D3D11 sobre uma STAGING de 241.664 bytes que o último TRANSFER acabara de trazer da RAM. `0x34` = *"A heap allocation has received a state transition event incompatible with current state"* (doc oficial do WDK).
- Nesta corrida também há um TRANSFER `Dir=1` de **241.664 bytes** (`hAlloc=…77`) entre os últimos seis. A última linha do KMD é uma submissão de paginação normal (`FenceId=1108`, `PASS` nas três caminhadas).
- **A pilha desta corrida ainda não foi lida:** o `cdb` sem elevação recebe acesso negado ao `MEMORY.DMP`.
- **Estado da placa:** Código 43 (`CM_PROB_FAILED_POST_START`) com `bc250mcdm`; fusível gravado às 15:38:39.

## 3. Veredito pelo critério

**Desfecho E (fora do mapa).** `ARMADO` com flushes crescendo e nenhum dos sinais de C, mas a corrida acabou em BugCheck, que nenhum desfecho previa.

**O que dá para dizer e o que não dá:**
- **P0:** o flush pelo anel funciona em carga real: mais de mil vezes, sem fault, sem TDR e sem falta de espaço no anel.
- **NÃO se pode atribuir o `0x34` à 4c.2 nem descartá-la.** O mesmo `0x34`, com os mesmos argumentos, já apareceu na 175, na 178 e na 197, sem 4c.2. Mas é a primeira corrida com 4c.2 e ela morreu em ~2 min. Uma ligação possível (H): o VidMm recebe o `FlushTlb` como concluído no build, mas a invalidação agora só acontece quando o SDMA chega na entrada seguinte; se o VidMm reaproveita a memória da cópia (o "recycle") apoiado nisso, o estado pode divergir. Isso é hipótese, e é exatamente o que a pilha decide.

## 4. Próximo passo (sem corrida)

1. **Ler a pilha do `MEMORY.DMP` elevado** (o Daniel roda o `cdb`):
   - pilha igual à da 197 (`DxgkLock`/`BeginCPUAccess` → `Rotate`, sem nada de paginação ou de flush) → é a classe antiga do `Lock` da UMD durante TRANSFER, e a 4c.2 segue na próxima corrida;
   - pilha diferente, passando por conclusão de paginação, `FlushTlb` ou fence de paginação → a 4c.2 entra como suspeita direta e eu reviso o desenho do flush adiado antes de qualquer corrida.
2. Recuperação: `tools\BC250_RECOVERY.cmd`, reset do fusível, boot frio.
3. `SdmaRingTlbFlush=0` no registro desarma a 4c.2 sem reinstalar, se for preciso isolar.

**Atualização (pilha lida pelo Daniel, `dump-analyze.txt`):** pilha **igual à da 197**, processo `dwm.exe`, nenhum quadro de paginação ou de flush. A alocação é o buffer de vértices dinâmico de 160.000 bytes do DWM. Resposta e correção no [doc 571](571_L1_BUFFER_SEM_LOCK_CONTRA_0x10E_34_INSTRUMENTACAO_E_CRITERIO_CORRIDA207.md).

**NÃO VALIDADO EM HARDWARE:** 4c.2 como correção da trava; causa do `0x34`.
