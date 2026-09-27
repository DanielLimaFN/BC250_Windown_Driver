# DX.44 — a DDI de preempção passa a falar; critério da Corrida 87

> 23/09/2026. Faixa **offline**, sem bind. Resultado anterior:
> [Corrida 86](373_CORRIDA86_DX43_RESULTADO.md). Teto: **uma** corrida, depois de
> desligamento completo.

## 1. Motivo

**O TDR da Corrida 86 aconteceu com a GPU inteira ociosa (P0):**
- motor gráfico com 188/188 submissões aposentadas, fence escrita e
  `RPTR==WPTR`;
- zero ondas e SDMA ocioso;
- `TDR_FENCE_RECONCILE Committed=0`: nenhuma fence nossa pendente.

**No código (P0):**
- `Bc250McdmPreemptCommand` devolvia `STATUS_SUCCESS` sem fazer nada e sem
  log;
- o driver **nunca** reporta `DXGK_INTERRUPT_DMA_PREEMPTED` (busca no driver
  inteiro);
- `PreemptionAware=1`, `LowIrqlPreemptCommand=1`, granularidade
  `DMA_BUFFER_BOUNDARY`;
- `PreemptionReady` está fixo em 1 (`bc250_active_miniport.c:3294`).

**Hipótese (H):**
- o agendador do Windows pede preempção, nunca recebe a conclusão e declara
  TDR com a GPU ociosa;
- isso é também violação da Lei 4 (DDI sem backend devolvendo sucesso);
- a 86 não pôde dizer se a DDI foi chamada, porque ela era muda e o registro de
  probes some com o rollback.

## 2. A mudança (medição; nenhum retorno muda)

- **`bc250_mcdm_callbacks.c`:** as três saídas da DDI registram
  `[PREEMPT_COMMAND] #N`:
  - parâmetro inválido;
  - caps não prontos (com o status e as flags);
  - sucesso, com `PreemptionFenceId`, `Node`, `Engine`, `Flags` e o texto
    explícito "STATUS_SUCCESS SEM BACKEND: nenhuma preempção feita e
    DMA_PREEMPTED NÃO será reportado".
- **Teto próprio:** 256 primeiras chamadas mais 1 em 64. As recusas são
  sempre registradas. O número de ordem em toda linha deixa inferir o total.
- **`bc250_display_ddi.c`:** `PREEMPT_COMMAND` isento da amostragem global.
- **Host:** `EXECUTAR_D3D11_NATIVO.cmd` passa a rodar a coleta RADV também no
  ramo "Rollback incompleto".
  - Na 86, o `BC250_RECOVERY.cmd` saiu com erro, o wrapper parou antes do passo
    3b e a coleta não aconteceu.
  - Prova indireta: o marcador de boot frio foi gravado pelo watchdog, não pelo
    wrapper.
- **DX.43 (IB de aquecimento) continua.** Esta corrida é também a **segunda
  amostra** dela.

## 3. Verificação offline

- `build-contract` EXIT=0 (inclui `mcdm_callbacks_test`, que continua exigindo
  os mesmos retornos) e build lab EXIT=0.
- Portão novo `audit_dx44_preempt_command_speaks`.
  `tools/test-dx44-preempt-speaks-mutations.py` reprova **6 de 6** mutações:
  recusa de parâmetro muda, recusa por caps muda, sucesso mudo, sucesso sem
  declarar ausência de backend, sem teto, linha amostrada.
  - A primeira versão do portão foi pega pela mutação 3: a janela de 900
    caracteres enxergava o log de outra saída.
  - Agora a janela de cada `return` começa no `return` anterior.
- Portão do wrapper verificado por mutação: tirar a coleta do ramo de erro
  reprova.
- DX.43: 7/7 mutações continuam reprovando.
- Auditorias de segurança, matriz DDI, callbacks, RADV e DX.31: PASS.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.248.1.0`, `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`. SYS `CD60A95F134BAA4CEBEA2118F36CE781625446077B46BF7D451B91ECBBE4775F`, INF `654514F6…4512`, CAT `5DF7289B…A8CF`. Auditoria do pacote PASS; strings DX.43 e DX.44 conferidas no SYS empacotado. |
| UMD | inalterada, `45BB28C3…1396B9` |
| RADV | DX.42, `FCAA11FE…DD1008C` |
| Pré-voo | `tools/prepare-corrida87-dx44.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA87_DX44.cmd`, elevado, depois de **desligamento completo** (marcador armado no boot das 14:53) |

## 5. Critério, escrito antes

**Pergunta 1 — a preempção explica o TDR ocioso?** Um "TDR ocioso" é um TDR
com zero ondas, `RPTR==WPTR` no motor gráfico, fence escrita e
`TDR_FENCE_RECONCILE Committed=0`.

| | Assinatura | Implicação |
|---|---|---|
| **A** | ≥ 1 TDR ocioso **e** pelo menos uma linha `[PREEMPT_COMMAND] … SEM BACKEND` num nó antes dele, sem `DMA_PREEMPTED` (nunca existe) | O agendador pediu preempção e nunca teve resposta. Próxima faixa (DX.45): implementar a conclusão da preempção para `DMA_BUFFER_BOUNDARY`, ou seja, drenar o nó e reportar `DMA_PREEMPTED` com `PreemptionFenceId` e `LastCompletedFenceId`. Uma alternativa a pesar é declarar `PreemptionAware=0`, mas o WDDM 2.x exige preempção. |
| **B** | ≥ 1 TDR ocioso **e** zero linhas `[PREEMPT_COMMAND]` antes dele | Hipótese refutada. Próximo suspeito: o caminho de sinal de fence monitorada (submits `cs=0` com sinal logo antes do último IB na 86). |
| **C** | Linhas `[PREEMPT_COMMAND]` presentes e **nenhum** TDR ocioso | O pedido acontece, é ignorado e não derruba nesta corrida. Não conclui causa; a dívida da Lei 4 fica registrada com contagem e nós. |
| **D** | Nenhuma linha `[PREEMPT_COMMAND]` e nenhum TDR ocioso | A DDI não é chamada neste regime. A 86 fica sem explicação; registrar. |
| **E — fora do mapa** | Recusas (`RECUSADO`), classe nova de TDR, BugCheck | Descrever antes de julgar. |

**Pergunta 2 — segunda amostra da DX.43:**
- contar as trocas com `Emitido=1` e os TDRs com ondas no VMID do IB anterior
  ao de `CP_IB1`;
- **zero** reforça o desfecho A da 86;
- **um ou mais** com `Emitido=1` naquela troca rebaixa a DX.43 de correção
  para mitigação parcial.

**Controles:**
- restart provado a cada TDR;
- sem BugCheck;
- coleta pós-rollback com `post-recovery-collection.json`, inclusive se o
  rollback do wrapper falhar;
- `RADV_PREAMBLE_IDENTITY` presente.

**Função:** desktop, 60/60, pixels e device-lost. Qualquer TDR ou menos de
60/60 é falha funcional.

Desfecho parcial não é desfecho. Exemplo: linhas de preempção só **depois** do
TDR não são A.
