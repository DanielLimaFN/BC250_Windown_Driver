# DX.47 — a preempção passa a ser concluída; critério da Corrida 92

> 23/09/2026. Faixa **offline**, sem bind. Base:
> [Corrida 91](383_CORRIDA91_RESULTADO_53_DE_60_E_TDR_DE_PREEMPCAO.md) (53/60, TDR
> com a GPU ociosa depois de um `PreemptCommand` sem resposta). Teto: **uma**
> corrida, depois de desligamento completo.

## 1. A variável única

**Contrato (P1):** depois de `DxgkDdiPreemptCommand`, o driver tem que concluir
com `DXGK_INTERRUPT_DMA_PREEMPTED`. O nosso nunca fazia isso (P0, docs/374), e
nas 86 e 91 o agendador declarou TDR com a GPU ociosa.

**Como ficou:**
1. **`Bc250McdmPreemptCommand`** (`bc250_mcdm_callbacks.c`):
   - com backend instalado, delega a `Bc250ActivePreemptHandler` e registra
     `[PREEMPT_COMMAND] … REGISTRADO`;
   - sem backend, o comportamento anterior continua declarado no log (os testes
     offline continuam valendo).
2. **`Bc250ActivePreemptHandler`** (`bc250_active_miniport.c`):
   - só **registra** o pedido por nó (`PreemptionFenceId`, engine) e enfileira o
     DPC de retirement;
   - **não notifica de dentro da DDI**: com o VidSch na pilha, isso é a
     reentrância que já custou um 0x133.
3. **Retirement** (`Bc250ActiveRetireAndNotify`), **depois** do dreno das
   conclusões, chama `Bc250ActiveTryCompletePreemptions`. A operação
   sincronizada nova `BC250_ACTIVE_FENCE_SYNC_PREEMPT`:
   - consulta o nó pelo rastreador (`Bc250WddmFenceTrackerNodeState`: em voo,
     reservas, última fence notificada), somando a fila própria do SDMA no
     Node 0;
   - **só com o nó drenado** notifica `DMA_PREEMPTED` com
     `LastCompletedFenceId` = a última fence já notificada como concluída;
   - com trabalho pendente, espera o próximo retirement (`[PREEMPT_WAIT]`).
4. **Caso ocioso:** o retirement antes retornava cedo quando nada era drenado,
   que é justamente o caso das 86 e 91. Agora a preempção é tentada também
   nesse caso.
5. **Legalidade:** deixar o que já estava no anel terminar é permitido para a
   granularidade declarada (`DMA_BUFFER_BOUNDARY`). Com tudo concluído, o
   dxgkrnl não tem pacote a reenviar.
6. **TDR:** o quiesce limpa os pedidos pendentes dos dois nós, para que um
   `DMA_PREEMPTED` antigo nunca saia depois do restart. O handler é desinstalado
   no teardown.

**Nada mais muda:** DX.43, DX.44 (log), DX.45b, DX.46, UMD e RADV ficam como
estão.

## 2. Verificação offline

- `build-contract` EXIT=0. `active_miniport_test` ganhou o caso por nó: com uma
  reserva no nó 1, o nó 1 mostra 1 reserva, o nó 0 mostra 0 e o nó 2 é
  recusado.
  - Mutação no nível da suíte: parar de contar reservas por nó derruba o
    `active_miniport_test` (arquivo restaurado).
- Portão novo `audit_dx47_preemption_completes` e
  `tools/test-dx47-preemption-mutations.py`: **9/9 mutações reprovam**:
  - DDI não delega;
  - notifica sem checar nó drenado;
  - ignora a fila do SDMA;
  - `LastCompleted` errado;
  - retorno antes da preempção no caso ocioso;
  - notifica de dentro da DDI;
  - handler não instalado;
  - TDR não limpa o nó 1;
  - `PREEMPT_DONE` amostrado.
- Portão da DX.44 ajustado para as 4 saídas da DDI; a suíte dela segue 6/6.
- DX.43 7/7, DX.45 11/11, DX.46 6/6, DX.31 PASS; auditorias de segurança,
  matriz DDI, callbacks e RADV: PASS.

## 3. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.252.1.0`. SYS `CFCDF8496A6548082E91E1344D584E9AF98EBFA5515AF2F71E135B344B87FEB9`, INF `B28FC442…7D7B`, CAT `1799D81B…9FDF`. Auditoria do pacote PASS; `[PREEMPT_DONE]`, `REGISTRADO`, DX.45b e DX.43 no SYS empacotado. |
| UMD / RADV | inalteradas: `45BB28C3…`, DX.42 `FCAA11FE…` |
| Janela | 300 s (`EXECUTAR_CORRIDA92_DX47.cmd` define `BC250_WATCHDOG_SECONDS=300`) |
| Pré-voo | `tools/prepare-corrida92-dx47.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA92_DX47.cmd`, elevado, depois de **desligamento completo** (marcador armado no boot das 18:46:13) |

## 4. Critério, escrito antes

**Controle:** `CntlPlanned=0x50000B0D`, janela de 300 s.

| | Assinatura | Implicação |
|---|---|---|
| **A** | Todo `[PREEMPT_COMMAND] … REGISTRADO` é seguido de `[PREEMPT_DONE]` do mesmo nó **e** nenhum TDR ocioso (zero ondas, anel drenado, `Committed=0`) **e** harness **60/60** | A preempção era a causa do TDR ocioso. Pela tabela, a estabilidade da GPU fica de pé; o próximo foco é o desktop interativo. |
| **A′** | `PREEMPT_DONE` para todo pedido e nenhum TDR ocioso, mas o harness falha por outra classe | A preempção está resolvida; a classe nova vira a pergunta. |
| **B** | TDR ocioso **depois** de um `PREEMPT_DONE` do mesmo nó | A conclusão sai, mas não basta. Reler a ordem `DMA_COMPLETED`/`DMA_PREEMPTED` e as fences reportadas. |
| **C** | `[PREEMPT_WAIT]` repetido sem nunca virar `PREEMPT_DONE`, seguido de TDR | O nó não é considerado drenado. Olhar a contagem de em voo e reservas na linha `PREEMPT_WAIT`. |
| **D** | Nenhum `[PREEMPT_COMMAND]` e 60/60 | A variável não foi exercitada. Estável, mas inconclusivo quanto à DX.47. |
| **E — fora do mapa** | BugCheck (em especial 0x119 ou 0x133), travamento, `DMA_PREEMPTED` que desorganiza as fences | Descrever antes de julgar. Um 0x119 aponta contrato de fence violado: reverter a DX.47. |

**Secundárias:**
- DX.43, 7ª amostra;
- volta do anel (WPTR > 16384);
- quadros por segundo;
- `[CP_RESET_DETECTED]`;
- relato visual do Daniel.

Desfecho parcial não é desfecho. Exemplo: A exige os três sinais juntos.

**Depois da corrida:** a tabela-checklist atualizada acompanha o relatório.
