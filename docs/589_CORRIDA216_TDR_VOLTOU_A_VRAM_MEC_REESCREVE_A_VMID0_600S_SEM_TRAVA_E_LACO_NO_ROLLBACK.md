# 589 — Corrida 216: o TDR voltou para a VRAM, o MEC reescreve a VMID 0, 600 s sem trava dura e um laço nosso no rollback

**Data:** 04/10/2026, bind às 22:08:56; rollback às 22:18:57; máquina reiniciada pelo Daniel em 05/10 às 08:40.
**Candidato:** lab **3.364.1.0** (SYS `02A6795A…`), D2 por GART com foto e TDR que volta para a VRAM.
**Critério:** [doc 588](588_CORRIDA215_GART_LIGOU_PRIMEIRO_IB_FAULT_NA_VMID0_E_TELA_AZUL_NO_TDR.md) §5.
**Evidência:** `docs/evidence/2026-10-04_22-08-56-DESKTOP-HQAG6T4-lab-binding/`:
- `display-log-this-bind-FINAL.txt`: 136 MB, 1.087.287 linhas;
- `safe-bind-watchdog-journal.jsonl`;
- logs da UMD.

O log vivo `C:\BC250_DISPLAY_LOG.txt` ficou com **25,2 GB**. Ver §1.4.

## 1. Medido (P0)

### 1.1 A D2 ligou de novo e caiu no primeiro IB, como na 215

- A foto logo depois de ligar estava limpa: `divergentes=0`, `BASE=0x00000004:6FF02001`, PTEs conferidas e `FAULT_STATUS=0`.
- O primeiro IB de paginação parou igual à 215: `IB_TRIPOD TIMEOUT ... WPTR=1368 RPTR=216`.

### 1.2 A foto no timeout respondeu: desfecho A

```
[D2_GART_FOTO] timeout de IB: ... Regs lidos=7 divergentes=2 (CNTL=0x007FFE01 BASE=0x000000F4:0FF93000 ...)
PTE[0..3]=0x46FFFF061 0x45C6C9077 0x45C6CA077 0x46FFFF061 divergentes=0 FAULT_STATUS=0x00001A30
```

- A tabela da GART está intacta.
- O PT_BASE do contexto 0 deixou de apontar para a nossa GART (`0x46FF02001`) e passou a valer `0xF40FF93000`.

### 1.3 O TDR voltou para a VRAM, sem tela azul

A sequência no log foi:
1. `TDR_RESTART FALHOU` (pela GART);
2. `[D2_GART] DESLIGADA (rollback)`;
3. `TDR_RESTART PROVADO`;
4. `anel de volta na VRAM e provado; a corrida segue SEM a D2`.

O `ResetFromTimeout` do dxgkrnl veio depois e também fechou com `TDR_RESTART PROVADO`. Na 215, esse mesmo ponto terminou em `0x116`.

### 1.4 A janela de 600 s chegou ao fim sem trava dura, e o laço veio depois

**Contagens da janela:**

| Evento | Quantidade |
|---|---|
| `IB EXECUTADO` | 72.521 |
| `Present` | 4.389 (~7 por segundo) |
| `IB_TRIPOD TIMEOUT` | 1, o da GART |
| `GFXHUB_FAULT`, `NO_MEMORY`, `DEVICE_REMOVED`, BugCheck | 0 |

**O laço no fim da janela:**
- O último evento normal é `[DestroyProcess]` mais uma rajada de `PAGING_DDI`, perto de 22:18:57, quando o watchdog começou o rollback.
- A partir daí o `BuildPagingBuffer` `Op=11` (UpdatePageTable, VA `0x23E000`, `Addr=0xEEBB`) devolve sem parar `INSUFFICIENT_DMA_BUFFER sem progresso, forca o envio`.
- O contador passou de 2^31 (`#-2132800288`). São cerca de 2,1 bilhões de voltas em ~10 h, até o reinício manual.
- A CPU continuou viva: o vsync seguia contando e o log cresceu 25 GB.
- Na janela de 600 s foram só **8** envios forçados, todos resolvidos.

**Recuperação:** o watchdog retomou no boot seguinte e fechou em `RECOVERY_OK_ONLINE`, com o BasicDisplay em Código 0 em 5 amostras.

### 1.5 O que os 25 GB dizem, lidos em fluxo (05/10)

Varredura do log vivo a partir do fim da captura FINAL (offset 136.422.695), sem abrir o arquivo. As 196.258 linhas fora do laço foram arquivadas em `display-log-pos-janela-SEM-O-LACO.txt` (sha256 `ee7060c4…`).

- **Duração:** o vsync foi de #33.780 a **#2.253.180** e o `OTG_Frame` de 42.592 a 2.278.056. A 56,13 Hz isso dá **~11 h** com interrupção e DPC vivos, até o reinício das 08:40. O rastro binário concorda: `writer_tick` 345.863 e `snapshot_time` de ~10,5 h. A máquina não morreu. Estava presa no laço.
- **Iterações:** o contador foi de 1 a 2^31 e passou para negativo até −2.132.791.313. São ~2,16 bilhões de recusas, ~55 mil por segundo (~18 µs cada).
- **Nenhum outro evento do driver nas 11 h:** zero `IB EXECUTADO`, zero submissão, zero `StopDevice`, zero TDR, zero `DestroyProcess`, zero `Present`. O `StopDevice` do rollback nunca chegou: o desligamento do dispositivo pelo watchdog ficou esperando o VidMm, que estava preso no nosso `BuildPagingBuffer`.
- **GPU ociosa o tempo todo:**
  - 2.589 `GFX_TRIPE_VEREDITO` idênticos (fence `0x14F2`, RPTR igual ao WPTR), ou seja, a retirada relendo a última fence;
  - o rastro termina com `gfx_ring_idle=true`, `fence_memory == last_submitted == 0x14F2`, `GRBM_STATUS 0xA0003028`;
  - migalha em voo: `PAGING_DDI` operação `0xB` (UpdatePageTable).
- **Sempre a mesma operação:** UpdatePageTable de 1 PTE em VA `0x23E000` (Seg 1, `Addr 0xEEBB`), repetida. O VidMm reenvia a mesma operação e nós a recusamos sempre.
- **MMIO vivo:** o contador `[HDP1_FLUSH]` foi de #327.680 a #4.980.736 durante o laço, cerca de um flush a cada ~430 voltas. Não é trava de barramento.
- **Linhas cortadas** (`ngBuffer] Entered`, `rca o envio`): várias threads escrevendo no log ao mesmo tempo, sem perda de sentido.

Conclusão (P0): o rollback travou por um laço de software nosso, com a GPU e o barramento saudáveis. Não é a trava dura. Depois da extração, o log de 25 GB não tem mais informação útil além do que já foi arquivado.

## 2. Leitura

### 2.1 Quem reescreve a VMID 0 é o nosso MEC

Confiança **P1** pela leitura do código, com o valor casando com o log.

- `bc250_compute_mqd_contract.c:2751-2772`, no bring-up da fila de compute dentro do StartDevice, programa o contexto 0 com:
  - `PT_BASE = Buffers->PtGpuAddress`;
  - `CNTL = 0x007FFE01`.
- `bc250_submission_engine.c:331` define `PtGpuAddress = basePhysical + BC250_SUBMISSION_PT_OFFSET`, que resulta em `0xF40FF93000` (região `HQD_MQD_ACTIVE` em VRAM `0xFF90000`).
- A ordem no log confere:
  - linha 59: inventário com a nossa GART;
  - linha 86: `HQD_MQD_ACTIVE`;
  - linha 559: foto com a raiz do MEC.
- A D2 e o MEC disputam a mesma VMID 0, cada um com a sua tabela. No Linux existe uma tabela só, a GART.

> **Correção (doc 590):** as §2.2 e §2.3 abaixo estão erradas no mecanismo. A tabela do MEC traduz no silício, porque a fence do motor de submissão é gravada em VA `0x2200` na VMID 0 e retira (P0). Sobra só a correlação da 216, que continua H e sem mecanismo.

### 2.2 A raiz que o MEC escreve está fora do formato

Confiança **P1**.

- O MEC escreve uma raiz em **endereço MC** (`0xF4…`) e **sem o bit VALID**. As PTEs da tabela dele também carregam MC.
- O catálogo exige PA (Q5, doc 03). O Linux escreve `amdgpu_gmc_pd_addr()`, que dá PA \| VALID (`gmc_v10_0_get_vm_pde` converte MC em PA).
- A D2 escreveu PA \| VALID e traduziu (P0 na 215).
- A fila do MEC funciona mesmo assim porque os endereços dela estão na abertura de FB, que não passa pela tabela. Ou seja, o contexto 0 do MEC está armado e, até onde se mediu, ninguém o usa.

### 2.3 Hipótese nova para a trava dura

Confiança **H**.

- Em **todas** as corridas de outubro com inventário (199 a 212), a VMID 0 ficou ligada com a raiz `0xF40FF93000`.
- A **216 é a única** em que ela ficou **desligada** durante a carga. O retorno do TDR restaurou o snapshot anterior ao MEC: `GCVM_INVENTARIO_2 VMID00 MUDOU: 0x007FFE01 -> 0x007FFE80, PtBase -> 0x0`.
- A 216 foi também a primeira desde a 201 a chegar aos 600 s sem trava dura.
- O mecanismo plausível: uma caminhada na VMID 0 lê o "endereço físico" `0xF40FF93000`, que não existe. Isso pode prender o fabric sem MCE, compatível com "GPU ociosa e máquina morta entre duas observações" (doc 582).
- **Contra a hipótese:** a 201 tinha a raiz do MEC armada e não travou. Então a raiz armada não basta para travar. Uma corrida só não prova nada.

### 2.4 O laço do rollback é defeito meu, da 4c.4

Confiança **P1** pelo código e **P0** pelo log.

- `bc250_mcdm_callbacks.c:1294` recusa o paging buffer quando `Bc250PagingSplitPendingCount() >= 12`. Esse contador é **global**: inclui pontos de divisão de **outros** buffers, que só saem da fila quando o buffer deles é enviado (`Bc250PagingSplitTake`, no submit).
- Quando o VidMm para de enviar, os 12 pontos nunca saem e a fila nunca envelhece. Isso aconteceu no teardown do rollback, provavelmente porque um buffer vazio não chega ao submit.
- Resultado: todo buffer novo, mesmo vazio, é recusado para sempre.
- A trava do rollback é esse laço na thread de paginação, **não** a trava dura das corridas anteriores, em que a máquina morria sem escrever nada.

## 3. Veredito pelo critério do doc 588

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| **A** — registradores divergentes | **SIM** | 2 registradores divergentes (PT_BASE LO/HI) e PTEs intactas |
| B — PTEs divergentes | não | `divergentes=0` |
| C — tudo intacto e mesmo assim fault | não | — |
| D — sem timeout | não | 1 timeout |
| **Condição do TDR** (voltar para a VRAM sem tela azul) | **SIM** | `anel de volta na VRAM e provado`, zero BugCheck |
| E — fora do mapa | sim, além do A | 600 s sem trava dura com a VMID 0 desligada; laço infinito no rollback |

**Veredito:** **sucesso de diagnóstico** (A, com o escritor identificado) e **sucesso da proteção do TDR**. A D2 em si continua **falha**: não sobreviveu ao primeiro IB, agora com causa conhecida.

## 4. Próximos passos (offline, a decidir)

1. **Corrigir o laço (obrigatório).**
   - A recusa deve contar só os pontos de divisão do buffer **atual**.
   - Um buffer sem nenhum ponto próprio nunca é recusado. Os pontos de outros buffers que não forem enviados viram órfãos e são tratados como invalidação antes do próximo IB.
   - Teste offline reproduzindo "12 pendentes alheios e buffer vazio", que hoje entra em laço.
2. **Uma tabela só para a VMID 0, que é o que o Linux faz.**
   - O MEC deixa de programar o contexto 0: as filas dele estão na abertura de FB e não precisam de tradução.
   - O contexto 0 passa a ser só da GART da D2, em PA \| VALID.
   - Isso também corrige o formato da §2.2 e testa a §2.3.
3. **Corrida 217:** D2 por GART com o contexto 0 só nosso, mais a correção do laço. O critério deve separar o efeito da D2 do efeito de tirar a raiz do MEC: a 216 já é um ponto do "MEC desligado sem D2".

**NÃO VALIDADO EM HARDWARE:**
- a D2 além do primeiro IB;
- a hipótese da raiz do MEC na trava dura;
- 600 s sem trava com 256 MB de forma repetível (uma corrida só, e com a D2 desligada);
- rollback sem laço.
