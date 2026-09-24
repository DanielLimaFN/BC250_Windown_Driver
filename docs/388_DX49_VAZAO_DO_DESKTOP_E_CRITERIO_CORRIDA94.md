# DX.49 — vazão do desktop (4 partes); critério da Corrida 94

> 23/09/2026. Faixa **offline**, sem bind.
> **Base:** [Corrida 93](387_CORRIDA93_DX48_RESULTADO_DESKTOP_INTERATIVO_LENTO.md),
> em que o desktop ficou visível e interativo a ~0,6 FPS, com orçamento medido.
> **Teto:** **uma** corrida, depois de desligamento completo.

## 0. Quatro variáveis numa corrida, por decisão do Daniel

A regra é uma variável comportamental por corrida. O Daniel pediu os itens 1 a 4
juntos. Para a corrida continuar legível, **cada parte tem assinatura própria no
log** (§4), então o ganho ou a regressão de cada uma é atribuível. Se algo
regredir, a ordem de reversão está em §4 (D).

## 1. Pergunta

**Base medida (P0, minuto 21:28 da Corrida 93, dwm.exe):**
- 24 s depois de `UploadImageData SUCESSO`;
- 9,6 s em 425 esperas de fence (flush antes **e** depois de cada draw);
- ~10 mil linhas de trace por minuto com write-through;
- 17 dirty rects recusados.

**Pergunta:** cada custo é o que a leitura do código diz, e removê-los aumenta
os presents por minuto?

## 2. A mudança

**Parte 1 — staging persistente** (`bc250_umd_resource_vk.c`, `bc250_umd_vulkan_bridge.c`):
- `Bc250UmdVkUploadImageData` usa um staging do bridge, mapeado uma vez, que
  cresce quando precisa e só é liberado no shutdown. Crescer drena o que estiver
  gravado antes de liberar o staging anterior;
- a fence da cópia continua sendo esperada antes de declarar
  `SHADER_READ_ONLY`;
- a barreira do upload passou de `TOP_OF_PIPE`/acesso 0 para
  `ALL_COMMANDS`/`MEMORY_WRITE`, porque agora pode haver draws anteriores no
  mesmo command buffer amostrando a imagem;
- trace: `[DX49_STAGING] Persistente=1 Cresceu= AlocUs= TotalUs=`.

**Parte 2 — lote de draws** (`bc250_d3d_umd.c`, `bc250_umd_descriptors.c`,
`bc250_umd_pipeline_cache.c`, `bc250_umd_blt.inl`):
- **O draw só grava.** Não há mais flush antes, e o flush depois só acontece se:
  - o draw usou o caminho direto;
  - leu memória de recurso sem sombra;
  - ou o lote chegou a 64 draws.
- **VB, IB e CB** vão para um anel de 16 MB host-visible, em faixa própria e
  alinhada a 256 B. O espaço é decidido **antes** de gravar o draw: se não
  couber, o lote é drenado antes; se passar do tamanho do anel, o draw vai pelo
  caminho direto de antes.
- **O anel volta ao início** só quando a fence do flush conclui. Com timeout ele
  fica onde está.
- **Dreno antes de destruir ou reescrever objeto referenciado:**
  - recurso, RTV, DSV e shader;
  - pipeline despejada do cache e o próprio cache;
  - RTV que segue um recurso rotacionado;
  - redimensionamento do buffer linear;
  - composição pelo KMD (`BltViaKmd`) e a ponte Vulkan→WDDM.
- **Render pass:** todo flush fecha o render pass aberto por um clear antes de
  `vkEndCommandBuffer`, via `pRenderPassOpen`.
- **Traces:**
  - `DRAW GRAVADO NO LOTE … [DX49_BATCH] Lote= Anel= Direto= FlushAgora=`;
  - `FlushCommands … [DX49_BATCH] DrawsNoLote= RingUsado= EsperaUs=`;
  - `[DX49_BATCH] DRENO antes de <motivo>`.

**Parte 3 — trace leve** (`bc250_wddm_winsys.c`):
- nome da imagem, caminhos e handles são resolvidos uma vez por processo
  (`InitOnceExecuteOnce`);
- cada linha é um `WriteFile`, sem `WRITE_THROUGH` nem `FlushFileBuffers`;
- **custo declarado:** a linha vai para o cache do sistema e sobrevive à morte
  do processo, mas num travamento da máquina se perdem os últimos segundos do
  log da UMD. O log do KMD não muda.

**Parte 4 — dirty rects** (`bc250_display_ddi.c`):
- `SrcRect` e `DstRect` vazios **nos dois lados** passam a valer como
  superfície inteira. É o que o dxgkrnl manda no Present de flip: medido
  `(0,0)-(0,0)` nas amostras da 93;
- qualquer outro Src/Dst continua exigindo tela cheia;
- a recusa passa a dizer `motivo=` com Src, Dst e o retângulo culpado.

## 3. Verificação offline

- **Builds:** `build-umd` EXIT=0 (todas as suítes da UMD, incluindo a nova
  `umd_dx49_batch_test`) e build lab EXIT=0 (inclui `build-contract` e a
  auditoria de segurança).
- **`umd_dx49_batch_test`**, com dispatch Vulkan espião, testa que:
  - o anel entrega faixas alinhadas e disjuntas;
  - o que não cabe é recusado sem avançar a cabeça;
  - o dreno sem gravação não submete nada;
  - o flush fecha o render pass antes do End;
  - o anel fica retido quando a fence dá timeout;
  - o staging é reusado e cresce drenando antes.
- **Portão novo `audit_dx49_desktop_throughput`** e
  `tools/test-dx49-throughput-mutations.py`: **17/17 reprovam**.
- **Portões antigos ajustados sem afrouxar:**
  - DX.25: a string do draw mudou porque "fence concluida" deixou de ser
    verdade por draw;
  - DX.34: aceita Src/Dst vazios e exige a validação nova.
- **Demais suítes:** DX.43 7/7, DX.44 6/6, DX.45 11/11, DX.46 6/6, DX.47 9/9,
  DX.48 6/6 e B.1 30/30. Também passam Corrida41, descriptor/wave, DX.31, a
  matriz DDI e a auditoria de callbacks no-op.
- **Falha anterior a esta faixa:** `test-f3-audit-mutations.py` procura
  `UNREFERENCED_PARAMETER(hRenderTargetView);`, que não existe nem no último
  commit. Não foi causada pela DX.49; fica registrada.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.254.1.0` SYS `EA3F02B328D590DD6FC734D29E99733CA01BA8495F525882AFC39A08C45A281E`, INF `405D1033…12AE`, CAT `AA81BE61…8A63`. Auditoria do pacote PASS; `motivo=%s`, `SRC_DST_NEM_VAZIOS_NEM_TELA_CHEIA` e `DX48_LIFECYCLE` presentes no SYS |
| UMD | `B786E3608743979D1E5B7C4D0C24C9ABDF84FD58A29BD75177523F2DAF36B6FF`; `DX49_BATCH`, `DX49_STAGING`, `DRAW GRAVADO NO LOTE` e `DRENO antes de` presentes |
| RADV | inalterada, DX.42 `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida94-dx49.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA94_DX49.cmd`, elevado, janela de 300 s, depois de **desligamento completo** (marcador armado no rollback de 21:30:59) |

## 5. Critério, escrito antes

**Controle:**
- `StartDevice LEAVE … status=0x00000000` e `CntlPlanned=0x50000B0D`;
- no log do dwm.exe, `[DX49_BATCH] ANEL de dados por draw criado` e
  `[DX49_STAGING] staging persistente CRIADO`.

Se o controle faltar, a corrida não mede a DX.49.

**Assinaturas por parte** (valem em qualquer desfecho):

| Parte | Assinatura de que pagou | Assinatura de que não pagou |
|---|---|---|
| 1 staging | Uploads 1307×681 com `Persistente=1` e intervalo até a linha seguinte **< 100 ms** (era ~1100) | Intervalo ainda ~1 s: o custo não era a liberação do staging (a atribuição P2 cai), e o suspeito passa a ser a paginação do próprio upload |
| 2 lote | Mediana de `DrawsNoLote` **> 1** e flushes/min **bem abaixo de 425** | `DrawsNoLote` quase sempre 1, com o motivo em `DRENO antes de …` |
| 3 trace | Linhas consecutivas de um mesmo passo (ex.: `Upload ENTER`→`LEAVE`) a **< 1 ms** (eram ~2 ms) | Intervalo igual ao da 93 |
| 4 dirty rects | Linhas `DIRTY_RECTS n=… DIRTY_RECT_PROOF Match=` aparecem | Só `DIRTY_RECTS_RECUSADOS motivo=…`, e o motivo nomeia a próxima correção |

**Desfechos** (medidos no log do dwm.exe, contando `DXGI Present LEAVE hr=0`
por minuto):

| | Assinatura | Implicação |
|---|---|---|
| **A** | Presents/min **≥ 70** (≥ 2× os 35–38 da 93), sem TDR, sem travamento, 60/60, e o Daniel relata desktop mais fluido | A vazão subiu. O próximo passo sai do novo orçamento. |
| **B** | Presents/min **entre 45 e 69**, sem TDR nem travamento | Ganho parcial. As assinaturas por parte dizem qual não pagou, e esse vira o alvo. |
| **C** | Presents/min **< 45**, sem TDR nem travamento | As hipóteses de custo não se sustentam no conjunto. Refazer o orçamento com os tempos novos (`EsperaUs`, `TotalUs`). |
| **D** | Regressão: TDR, `DEVICE_REMOVED`, `SubmissionFailed`, recusa `*_ANEL_SEM_ESPACO`, quadro velho ou lixo na tela, ou travamento | O lote quebrou vida útil ou ordem. **Reverter primeiro a parte 2**; as partes 1, 3 e 4 não mudam ordem de GPU. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

Desfecho parcial não é desfecho: A exige os três sinais (taxa, estabilidade e
relato visual).

**Secundárias:**
- ciclo de vida DX.48 (`AddDevice #2`?);
- preempção (`REGISTRADO` → `PREEMPT_DONE`);
- DX.43;
- guardião;
- relato visual.

**Depois da corrida:** relatório com as 5 seções e a tabela-checklist
atualizada.
