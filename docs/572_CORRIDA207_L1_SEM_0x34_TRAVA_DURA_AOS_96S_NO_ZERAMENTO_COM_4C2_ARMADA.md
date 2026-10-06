# 572 — Corrida 207 (L1 + 4c.2): sem `0x10E`, mas **trava dura aos ~96 s** no zeramento de alocações novas de tela cheia, com a 4c.2 armada

**Data/hora:** 02/10/2026, bind 21:55:18; trava ~96 s depois (Daniel abriu o menu) · **Candidato:** lab **3.354.1.0** (SYS `26781F18…`, UMD `A1BD1657…`) · **CMOS:** 256 MB
**Evidência:** `docs/evidence/2026-10-02_21-55-18-DESKTOP-HQAG6T4-lab-binding/`. Sem BugCheck (o `MEMORY.DMP` é o da 206, 15:38). Resgatados pelo offset do baseline:
- `display-log-this-bind-RECUPERADO.txt` (3.718.223 bytes, SHA-256 `97317d5f…b4af0e`);
- `paging-trace-this-bind-RECUPERADO.bin` (1.855.560 bytes, SHA-256 `a35f315a…58e714`) e `paging-trace-decodificado.json`.
- **Lacuna:** o `coletar-pos-trava.ps1` não rodou antes da recuperação; os logs da UMD do fim da corrida (C:\Windows\Temp e AppContainer) não existem mais. Só há o retrato de ~10 s depois do bind.

**Critério:** [doc 571 §3](571_L1_BUFFER_SEM_LOCK_CONTRA_0x10E_34_INSTRUMENTACAO_E_CRITERIO_CORRIDA207.md).

---

## 1. O que foi medido (P0)

**L1 aplicada:**
- 8 linhas `L1 … PULADO` no retrato do DWM (pid 1292). Entre elas, o buffer de 163.840 bytes (`hAlloc=0x80002f80`), o mesmo tamanho do que derrubou a 206.
- `LockUs` aparece nos `Upload LEAVE` restantes (15–71 µs no retrato, sem `LOCK_LENTO`).

**4c.2 armada e emitindo:** `VEREDITO ANEL: PROVADO`, `[RING_TLB_FLUSH] ARMADO`, linhas `#1`..`#8` e `#512`. O BPG1 conta **575 flushes pelo anel** e **2.960 `FlushTlb`** no total da corrida. Zero `RECUSADO`, zero `GFXHUB_FAULT`, zero `TDR_QUIESCE`, zero `IB_TRIPOD TIMEOUT`.

**BPG1, o primeiro rastro de paginação por segundo (cumulativo no fim):**

| | Total da corrida (~96 s) | Corrida 206 (~120 s), pelo log |
|---|---|---|
| Despejos da VRAM (`LOCAL_TO_SYSTEM`) | 100 (≈86,5 MiB) | 379 |
| Voltas para a VRAM (`SYSTEM_TO_LOCAL`) | **1** (10,6 MiB) | 13 |
| FILLs | 480 | — |

- **Voltas para a VRAM caíram de 13 para 1.** É compatível com a L1 ter tirado as voltas forçadas pelo `Lock` dos buffers. É P0 da contagem, mas não é comparação controlada: as corridas tiveram duração e uso diferentes.
- **Últimos segundos antes da parada:** despejos de 1 a 6 por segundo, até 3,7 MiB/s, de 2 a 14 flushes pelo anel por segundo. Nenhuma rajada fora do padrão da própria corrida.
- **O rastro parou ~0,8 s depois do último BPG1** (`writer_tick` 880 → 888): a thread de PASSIVE_LEVEL morreu junto com a máquina. É a assinatura de sempre da trava dura.

**Onde parou (log do KMD):** os últimos movimentos de paginação são três **zeramentos de alocações novas e grandes**: `hAlloc …6B0`, 11.419.648 bytes (classe RESOURCE da UMD); e `hAlloc …6B2`, 12.582.912 bytes (RADV, token de classe 0), em dois passes. A última linha é a montagem do IB de 41.504 bytes desse zeramento (`FILL_POR_COPIA`, `N=553`). **É a mesma assinatura das travas anteriores** (doc 560 §6: parada logo depois do FILL de zeramento de uma alocação nova de tela cheia).

**Atualização: logs da UMD do fim, trazidos pelo `coletar-pos-trava.ps1`** (`umd-logs-pos-travamento/`, 19 arquivos; motivo de reset `0x40080400`, o mesmo de sempre):
- A máquina parou às **21:57:04,5**: as últimas linhas do DWM (04.486), da Pesquisa (04.497) e do menu (04.304) são desse instante.
- Às 21:57:04.446 a **Pesquisa** criou um render target novo de **2592×1088** (`Allocate … Size=11419648`, `CreateResource … Dim=3 Fmt=87 … Bind=0xA VkRes=1`). É a alocação `…6B0` do KMD; a de 12.582.912 bytes (`…6B2`, token de classe 0) é a memória da RADV para o `VkImage` desse mesmo recurso. Os dois zeramentos foram o último trabalho de paginação.
- **Zero `res=-2` e zero `LOCK_LENTO` em todos os 13 processos.** A trava aconteceu **sem** falta de memória visível na UMD: ela não depende da pressão de memória que as 199–204 tinham.
- A L1 agiu em 9 processos (DWM, Pesquisa, menu, Configurações, Shell, Cortana, entrada de texto).

## 2. Veredito pelo critério

**Desfecho B:** L1 aplicada, sem `0x10E`, e trava dura.

- **A 4c.2 (invalidação da TLB em ordem, pelo SDMA) não tira a trava.** Com ela armada e emitindo, a máquina parou no mesmo lugar de sempre. A invalidação da TLB pela CPU, fora de ordem, **sai da lista de causas suficientes** (P0 desta corrida).
- **Sobre o `0x34`: inconclusivo.** Ele não apareceu, mas a corrida durou ~96 s, menos que os ~2 min em que a 206 caiu. O que se pode dizer: o `Lock` que o disparava não aconteceu (linhas `L1 … PULADO`) e as voltas para a VRAM quase sumiram.

## 3. O que sobra e o próximo passo

O elo comum de todas as travas medidas continua sendo **uma alocação nova recebendo PTE e sendo zerada**. Com a 4c.2 descartada, o que ainda separa o nosso driver do Linux nesse instante:
1. **A PTE é escrita pela CPU, pela BAR, no build** (4c.4, `GPU_VIRTUAL`). O Linux escreve pelo SDMA.
2. **A caminhada das tabelas pela CPU durante a carga** (4c.3): a cada submissão de paginação, o KMD lê a tabela de página pela BAR (`[PT_WALK]`), com o SDMA escrevendo na VRAM ao mesmo tempo. A última linha do log é exatamente uma caminhada dessas.

Pela escada do doc 567, o próximo é a **4c.3** (gates sem ler a tabela pela CPU), uma variável, e depois a **4c.4**.

**Recuperação:** placa em Código 43 depois do reset; `tools\BC250_RECOVERY.cmd`, reset do fusível e boot frio antes do próximo bind. **Daqui para frente, rodar o `tools\coletar-pos-trava.ps1` elevado ANTES da recuperação**, senão os logs da UMD do fim da corrida se perdem (como aqui).

**NÃO VALIDADO EM HARDWARE:** correção da trava; eliminação do `0x34`.
