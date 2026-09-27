# Corrida 100 (DX.55, lote desligado): resultado

> **Data:** 24/09/2026, bind às 10:50:49.
> - Todos os logs da UMD param às **10:56:51** (~6 min 02 s depois do bind).
> - Kernel-Power 41 às 11:02:41: reboot forçado pelo operador, sem rollback do guardião antes disso.
>
> **Candidato:** lab `3.261.1.0`, SYS `F643EDC9…`, UMD `51288052…`.
> **Evidência:** `docs/evidence/2026-09-24_10-50-49-DESKTOP-HQAG6T4-lab-binding/`.
> - Log do KMD recuperado: `display-log-this-bind-RECUPERADO.txt`, 40.432.426 bytes, sha256 `d93bc998…e0ae`.
> - Logs da UMD por cópia elevada: `umd-logs-pos-travamento/` (8 logs, `SHA256SUMS.txt`).
>
> **Critério:** [doc 401 §4](401_DX55_BISSECCAO_LOTE_DESLIGADO_CRITERIO_CORRIDA100.md).

## 1. Controle: DX.55 rodou como desenhada (P0)

**dwm.exe:**
- `POOL PERSISTENTE criado`;
- **2303/2303** `BOUND` com `Pool=PERSISTENTE`, zero fallback;
- **zero** anel criado (dwm e Explorer).

**KMD:**
- 275 desarmes DX.53, zero quarentena;
- zero TDR, timeout ou GFXHUB_FAULT.

## 2. Métricas

| Medida | 98R | 100 |
|---|---|---|
| Present/min do dwm.exe (`LEAVE hr=0`) | 87 / 112 / 120 / 70 | **156 / 178 / 151 / 173 / 157** (min 10:52–10:56) |
| Intervalo entre Presents p50 / p90 | 391 / 1864 ms | **294 / 570 ms** (n=878) |
| `DescUs` mediano | ~14 ms (estimado) | 253 µs |
| Flush do dwm: `SubmitUs` / `EsperaUs` / `FimCbUs` (mediana) | — | 2,1 / 1,0 / 1,0 ms |
| `event=KMD_CONTEXT_CREATE` | 1384 em ~4,8 min | 2078 em ~6 min (~345/min) |
| `COMPOSITE_BLT OK` / `RECUSADO (resolucao)` | 475 / 0 | 638 / **241** |

**Ganho de velocidade:** ~1,4–1,5× em Present/min e p50 −25%. É real, mas não chega à barra de 1,5× com folga nem a p50 ≤ 260 ms.

## 3. Falhas

- **Travamento duro às ~6 min**, mesma assinatura das 94–99: o log do KMD termina em submissão SDMA de paginação, sem nada no log de eventos.
- **O harness D3D11 não rodou, por defeito meu:**
  - o `EXECUTAR_CORRIDA100_DX55.cmd` foi gerado com fim de linha LF;
  - a auditoria de segurança, que o script do harness roda depois do bind, reprovou (`12 linha(s) sem CRLF`);
  - o bind e a janela seguiram, e só o harness ficou de fora;
  - **corrigido:** wrappers da 99 e da 100 em CRLF, e a auditoria de segurança entrou no pré-voo (`prepare-corrida100-dx55.ps1`), para recusar **antes** do bind.
- **Falha de ponte sem lote:** 241 `COMPOSITE_BLT RECUSADO (resolucao)` (status=9, destino não residente), todos entre 10:52 e 10:54, que é quando msedge, msedgewebview2 e Claude.exe abriram device. Zero em 10:55–10:56.
  - A H da DX.54 ("o piscar era o anel de 16 MB") **perde força**: a falha que o acompanhava apareceu com zero anel.
  - Os 244 `status=10` foram recuperados pela varredura de faixa (`RANGE_SCAN SUCCESS`) e compuseram OK.

## 4. Fora do mapa

- **`Radeonsoftware.exe`** (AMD Radeon Software) carregou a nossa UMD às 10:55:59–10:56:04. É a primeira vez neste bloco de corridas. O travamento veio 47 s depois. Correlação de uma amostra, **H**.
- **No instante do travamento:**
  - o **Explorer** estava em `Bc250UmdVkBridgeShutdown` (destruição de device do laço);
  - o **dwm.exe** estava em `D3D10 Flush`, logo depois de `RotateResourceIdentities`.
  - Na 99, o Explorer estava em `vkCreateInstance`.
  - Com ~6 eventos de contexto por segundo, a proximidade não discrimina.

## 5. Veredito frente ao critério

**Desfecho C: travamento.**

**O que isso reposiciona:**
- **O lote não é o gatilho do travamento.** Com o lote desligado, a 100 travou.
- **A 98R não provou a DX.53 como correção do travamento.** A 98R (mesma configuração de estabilidade, sem pool) rodou 5 min limpos; a 100 travou em 6 min. A ressalva do doc 398 se confirmou: a margem era de 0,6 min. A implicação "a janela do VMID era a causa" cai de P2 para **refutada como causa suficiente**. A DX.53 continua correta como código (corrida de dados real removida).

**Tempos até o travamento:** 95 ~1,5 min, 96 ~4,4, 97 ~4,2, 99 ~0,65, 100 ~6,0.

**O que NÃO isola a causa:**
- O staging persistente e o trace durável estão em todas as que travaram desde a 94. Mas a **88 e a 92 também travaram**, antes da DX.49 ([docs/387](387_CORRIDA93_DX48_RESULTADO_DESKTOP_INTERATIVO_LENTO.md)).
- A 93 rodou 300 s limpa a ~100 contextos/min.
- O travamento é anterior às faixas DX.49–DX.55.

**O que as corridas sugerem (H):**
- ele aparece mais cedo ou mais tarde conforme a carga;
- desde a 94, a rotatividade de contextos do Explorer subiu para 290–390/min, porque o laço gira mais rápido quando o driver é mais rápido;
- ~6 min é a janela mais longa medida com essa carga.

## 6. Recuperação e pendências

- **Estado da máquina:** BasicDisplay em Código 0 (recovery online depois do reboot) e marcador de boot frio armado.
- **NÃO VALIDADO EM HARDWARE:**
  - estabilidade além de ~4–6 min em qualquer configuração;
  - harness 60/60 com a DX.55;
  - desktop sem piscar;
  - laço do Explorer.
- **Próximo passo:** decisão do Daniel, entre:
  1. **Bissecção barata:** staging persistente desligado (pool mantido). Uma variável, mas a 88/92 mostram que ele não é a causa única.
  2. **Tirar a carga:** atacar o laço do Explorer, porque a rotatividade de contextos é o confundidor de todas as corridas. A causa segue não identificada offline e exigiria instrumentação nova do lado do dxgkrnl.
  3. **Localizar o travamento:** hoje a última linha do KMD se perde (anel em RAM). Uma trilha que sobreviva ao reset diria onde a máquina parou.
