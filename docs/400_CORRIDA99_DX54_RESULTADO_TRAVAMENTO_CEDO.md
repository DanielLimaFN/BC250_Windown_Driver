# Corrida 99 (DX.54): resultado — travamento cedo

> **Data:** 24/09/2026, bind às 10:15:52. Kernel-Power 41 às 10:32:55 (reboot forçado pelo operador).
> **Candidato:** lab `3.260.1.0`, SYS `CF03A31B…`, UMD `B884F0F3…`.
> **Evidência:** `docs/evidence/2026-09-24_10-15-52-DESKTOP-HQAG6T4-lab-binding/`.
> - Log do KMD recuperado do log cumulativo: `display-log-this-bind-RECUPERADO.txt`, 2.810.231 bytes, sha256 `d69cce5a…a26e`, proveniência em `display-log-tail-recovered.json`.
>
> **Critério:** [doc 399 §5](399_DX54_VELOCIDADE_DO_DESKTOP_E_CRITERIO_CORRIDA99.md).

## 1. O que aconteceu

**Relato do operador:** o desktop apareceu e o mouse não mexia.

**Medido:**
- **Travamento duro:** Kernel-Power 41, sem BugCheck. No boot seguinte, `[FUSE] STARTDEVICE RECUSADO: Fusivel disparado (State=2, Boots=2, LastStatus=c0000001)`. O guardião só reverteu **depois** do reboot (RECOVERY_OK_ONLINE às 10:33:15).
- **Log de eventos vazio:** System e Application **sem nenhum evento** entre o bind e o reboot. O travamento levou junto o que não estava em disco.
- **Harness D3D11:** `conformance-stdout.tmp` com 0 bytes. O harness não terminou; na 98R ele rodou de ~25 s a ~80 s depois do bind.
- **Fim do log do KMD:** termina no meio de uma submissão SDMA de paginação. É a **mesma assinatura das 94–97**. A última linha não localiza o travamento (anel em RAM acima de PASSIVE).

| Contagem no KMD | 99 | 98R (5 min inteiros) |
|---|---|---|
| `event=KMD_CONTEXT_CREATE` | 164 | 1384 |
| `KMD_GFX_SUBMIT` | 259 | — |
| `IB EXECUTADO` (SDMA) | 1561 | — |
| `[DdiPresent] ENTER` | 8 | 9 |
| `COMPOSITE_BLT OK` | 23 | 475 |
| `[DX53_VMID] DESARME` / `QUARENTENA` | 22 / 0 | 157 / 0 |
| TDR / timeout / GFXHUB_FAULT / SpaceResolveAddress | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |

**Duração estimada:** ~35–60 s depois do bind (**H**). Na taxa da 98R (~290 contextos/min), 164 contextos são ~34 s de laço do Explorer. O harness não ter terminado é compatível.

## 2. Lacuna que manda na análise

**Os logs da UMD desta corrida não estão na pasta.** O binder copia no fim da janela, e a máquina travou antes. Ficaram em `C:\Windows\Temp`, que esta sessão não consegue ler (sem elevação).

**Sem eles, fica NÃO MEDIDO:**
- se o dwm.exe criou o anel de 4 MB;
- se o pool persistente foi criado;
- quantos desenhos entraram no lote;
- o último passo da UMD antes do travamento;
- os tempos `[DX54_TEMPO]`.

**Portanto:** nenhuma afirmação sobre **qual peça** da DX.54 está ligada ao travamento é medição. O próximo bind arquiva esses logs como residuais, e uma cópia elevada antes disso também serve.

## 3. Leitura do histórico (H, não P0)

| Corrida | Lote de desenhos | DX.53 | Resultado |
|---|---|---|---|
| 94 | ligado (16 MB) | não | travou |
| 95 | ligado | não | travou ~1,5 min |
| 96 | ligado | não | travou ~4,4 min, piscando |
| 97 | desligado | não | travou ~4,2 min |
| 98R | desligado | sim | **5 min sem travar** |
| 99 | ligado (4 MB) + pool persistente + inspeção amostrada | sim | travou em ~1 min |

**Todas as corridas com lote ligado travaram, e as duas mais cedo (95 e 99) tinham lote.** A 97 mostra que o lote não é o único caminho. Mas, com a DX.53, a única diferença entre 98R (sem travar) e 99 (travou cedo) são as três peças da DX.54, e das três o lote é a que tem histórico. **Suspeito principal: o lote (H).** O pool persistente e a inspeção amostrada não estão descartados.

## 4. Veredito frente ao critério

- **Desfecho:** **D — travamento**.
- **Controle da DX.54:** não verificável sem os logs da UMD. Pela regra do critério, a corrida **não mediu** a velocidade da DX.54.
- **O que mediu:** que esta configuração trava cedo, com a mesma assinatura de sempre.

## 5. Recuperação e pendências

- **Estado da máquina:** BasicDisplay em Código 0 (recovery online depois do reboot) e marcador de boot frio armado.
- **Evidência:** o log do KMD está arquivado; os logs da UMD estão pendentes.
- **NÃO VALIDADO EM HARDWARE:**
  - ganho de velocidade da DX.54;
  - anel de 4 MB;
  - pool persistente;
  - estabilidade além de 5 min;
  - laço do Explorer.
- **Próximo passo recomendado:**
  1. Copiar os logs da UMD (elevado) e ler o último passo do dwm.exe e do harness.
  2. Corrida 100 = DX.54 com **só o lote desligado** (`s_Bc250Dx51DrawBatchEnabled = FALSE`), pool persistente e inspeção amostrada mantidos. É uma variável contra a 99.
     - **Não trava, e fica mais rápido:** o lote é o gatilho e a velocidade vem do pool.
     - **Trava:** o suspeito passa para o pool persistente ou a inspeção.

## 6. Adendo: logs da UMD recuperados (cópia elevada, 24/09)

**Cópia:** `tools/copiar-logs-umd-corrida99.ps1` → `umd-logs-pos-travamento/`, 4 logs com `SHA256SUMS.txt`:
- dwm.exe (1212);
- harness (1136);
- Explorer.EXE (6648);
- dxgiadaptercache (6080).

**Todos terminam às 10:16:31.8, ~39 s depois do bind.** Isso fecha a duração (antes era H) e fecha a lacuna da §2.

**Controle da DX.54 no dwm.exe (P0):**
- `POOL PERSISTENTE criado` às 10:16:03;
- **158/158** `BOUND` com `Pool=PERSISTENTE`, zero fallback;
- `ANEL de dados por draw criado: 4194304 bytes` às 10:16:12, depois do 32º desenho;
- `[DX54_INSPECAO]` às 10:16:30.

**Explorer:** zero anel (limiar funcionou).

**Velocidade (P0 de tempo, 30 s de amostra):**

| Medida | 98R | 99 |
|---|---|---|
| `DescUs` por desenho (mediana) | ~14 ms (estimado pelo intervalo) | **32,5 µs** (máx 8,8 ms) |
| Flush do dwm.exe: `SubmitUs` / `EsperaUs` / `FimCbUs` (mediana) | — | 4,0 ms / 1,7 ms / 1,0 ms |
| Desenhos por lote no dwm.exe | 1 | até 18 (130 flushes com 0, 41 com 1, resto 2–18) |
| Intervalo entre os últimos Presents | p50 391 ms | 150–300 ms (10:16:30.5 → 30.9) |

**Harness:** `SubmitUs` mediana **89 ms**, máximo **1.057.629 µs**. A submissão do harness passou a esperar até 1 s dentro do `vkQueueSubmit`.

**No instante do travamento (10:16:31.80–.81), três processos estavam em chamada ao mesmo tempo:**
- o **harness** entrou em `FlushCommands ENTER` (DrawsNoLote=0) às .808;
- o **dwm.exe** entrou em `FlushCommands ENTER` (DrawsNoLote=0, logo depois de um upload de textura de 3,6 MB) às .814;
- o **Explorer** estava em `vkCreateInstance` às .814.

Nenhum dos dois `ENTER` tem conclusão.

**Leitura:**
- **P0:** o pool persistente eliminou o custo de descritores.
- **P2:** o travamento acontece com duas submissões GFX de processos diferentes em voo.
- **Os lotes do dwm.exe logo antes do travamento eram pequenos:** os 8 últimos flushes concluídos (10:16:30.65–.97) tiveram 0 ou 1 desenho. O último lote com 2 ou mais desenhos foi às 10:16:30.595, ~1,2 s antes. Não há medição de que um lote grande estava em voo no instante do travamento.
- **H:** que o lote maior (IBs com vários desenhos) somado à troca de contexto entre processos abra o travamento. A 97 travou sem lote, então isto não é causa única.

**Próximo:** Corrida 100 com só o lote desligado, mantendo o pool persistente (o ganho medido) e a inspeção amostrada.
