# DX.39 — telemetria de filas, fences e índice de falhas

> Continuação posterior: [DX.40](365_DX40_BUILD_COMPLETO_FLUXO_RADV_LINUX_WDDM.md) persiste o fluxo RADV por processo, amplia o índice e registra os hashes do build completo. Os hashes abaixo são a fotografia de DX.39.

> 23/09/2026. Continuação offline de [DX.38](363_DX38_IDENTIDADE_IB_E_LIMITE_SONDA_ONDAS_OFFLINE.md). Nenhum bind, instalação, pacote novo ou corrida física. **NÃO VALIDADO EM HARDWARE.**

## Objetivo e leitura correta

As Corridas 82/83 produziram milhares de linhas, mas não preservaram em arquivo a identidade da fila e o retorno de `D3DKMTSubmitCommand` de cada IB do `dwm.exe`. Isso dificultou separar IB preparado, submit aceito, fence publicada e execução efetiva. A instrumentação DX.39 acrescenta eventos com prefixo fixo `BC250_DIAG schema=1 level=<INFO|ERROR> event=<nome>` e campos `chave=valor` sem espaços. O índice coloca erros e lacunas no começo e mantém a linha do log bruto como referência. `schema=1` é apenas o formato dessas linhas, distinto de `TelemetrySchemaVersion` do manifesto do KMD.

## Pontos instrumentados

| Camada | Eventos | Campos para correlação e limites |
|---|---|---|
| RADV WDDM | `RADV_CONTEXT_CREATE`, `RADV_QUEUE_READY`, `RADV_QUEUE_DESTROY`, `RADV_QUEUE_SUBMIT_BEGIN`, `RADV_QUEUE_WAIT`, `RADV_QUEUE_SUBMIT_END`, `RADV_QUEUE_SIGNAL`, `RADV_GANG_NO_HWQUEUE` | PID, contexto RADV, IP, handle da fila, ID da fila, contagens de CS/esperas/sinais, primeiro valor de fence, NTSTATUS. O ramo gang sem hardware queue já devolvia `DEVICE_LOST`; agora nomeia o motivo. Esses eventos vão a `stderr`, cuja captura do `dwm.exe` não é garantida. |
| RADV dump opt-in | `gfx-ib-dump/*.bin.meta` com `RADV_SUBMIT` | Arquivo lateral por dump completo, criado sem sobrescrever. Registra PID, contexto, IP, fila, gang, esperas/sinais, se D3DKMT foi chamado, status, aceite, VA, DW e FNV64. Acompanha o `.bin` mesmo sem `stderr` arquivado. Aceite D3DKMT **não prova execução**. Sem chave de dump, nenhum arquivo lateral é criado. Limite: 128 dumps por processo; submissões posteriores não terão sidecar. |
| KMD | `KMD_CONTEXT_CREATE`, `KMD_CONTEXT_DESTROY`, `KMD_SDMA_SUBMIT`, `KMD_GFX_SUBMIT`, `KMD_GFX_REJECT`, `KMD_TDR_WAVE` | Handle do contexto, node, fences WDDM/anel, VMID, root, VA/tamanho/hash, estado do commit, RPTR/WPTR/SQ com flags de leitura, fence física observada, PC/status/trap da onda. Contextos são de baixa frequência; detalhes de submit ficam sob o tunable existente `GfxWaveProbe=1`, que também arma as sondas DX.38. TDR registra onda no quiesce, não em GPU ativa. |

O contexto RADV é um handle WDDM visto no processo e o contexto KMD é o handle interno do miniport; valores numéricos iguais **não são presumidos**. VA + DW + FNV64 completo identificam bytes preparados/lidos, sujeitos a repetição ou colisão. Fence física observada e `committed=1` mostram fases distintas; nenhuma linha isolada certifica pixels corretos ou desktop íntegro.

## Como localizar um problema

O binder gera `telemetry-index.md` na pasta da evidência **depois** de copiar o log final e os dumps. O script também pode ser executado offline:

```powershell
python tools/index-bc250-telemetry.py docs/evidence/<pasta-da-corrida>
python tools/correlate-gfx-ib-identity.py docs/evidence/<pasta-da-corrida>
```

O índice abre com alertas `ERROR`, medições inconclusivas, divergência de VMID, sinais legados de TDR/Present e cauda incompleta; aponta `FINAL:<linha>` ou o nome do `.meta`. Depois agrupa filas por PID/IP/contexto, lista cada `KMD_GFX_SUBMIT` com fence/VMID/hash e `MATCH_UNICO`, `AMBIGUO`, `AUSENTE` ou `INDETERMINADO`, e conta eventos. O índice limita a apresentação a 100 alertas, declara quantos ficaram fora e mantém o log bruto como fonte. Se `display-log-final-archive.json` não provar cauda completa, a primeira linha de alerta diz isso. Na evidência antiga da Corrida 83, o índice encontrou 11 sinais legados/cauda incompleta; **11 alertas não significam 11 TDRs**. Como não havia eventos DX.39, nenhuma fila ou correspondência nova foi inferida.

A correlação exclui dumps sem `.meta`, com hash/tamanho divergente ou rejeitados por D3DKMT; duplicatas continuam `AMBIGUO`. O indexador não atribui causa a TDR, não lê sozinho uma cauda resgatada fora do log FINAL, e não reconstrói submissões após o teto de dumps. Em caso de diferença, consultar o arquivo bruto e a proveniência da captura.

## Verificação offline e proveniência

- `driver/wdk/build-contract.bat`: EXIT 0, compilou KMD e testes de contrato. `audit-bc250mcdm-safety.py` e `audit-radv-wddm-safety.py`: PASS.
- `build_radv.bat` na árvore Mesa externa: EXIT 0. Patch espelhado em `external/radv-wddm/bc250-radv-wddm.patch`, SHA-256 `ae379ac18dce3ed3b0d0def91457546edd7f3346902179035a884dfbc06afe50`; detalhes em `external/radv-wddm/PROVENANCE.md`.
- Fixtures offline: match único aceito; duplicata ambígua; dump rejeitado excluído; metadados ausentes/divergentes classificados como lacuna. Parser PowerShell do binder: PASS. Não houve ensaio com `dwm.exe` nem medição de custo no silício.
- DLL RADV construída offline: SHA-256 `56073fadc4fb211140adffca98d3dc4fde2402eab961a756bc68a2d60172b7df`. DLL implantada ainda é `5988b38e86febb514c4c5f1d5fb5ce5a4b6fdd2b57668f2a1b6baca3b1a0461f` (Corrida 83). KMD empacotado também continua o da 83; build offline não é pacote de corrida.

O teste físico posterior deve comprovar que `.bin` e `.meta` são arquivados para o processo que submeteu, que as linhas `KMD_GFX_SUBMIT` aparecem com leitura completa e que o índice aponta o TDR e qualquer falha de fila/Present à linha bruta correta. Se `stderr` do DWM faltar, o `.meta` cobre o aceite da submissão, mas não suas esperas e sinais posteriores. A coleta de alto volume precisa ser medida na corrida antes de ser tratada como observabilidade suficiente.
