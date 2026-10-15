# 616 — P3.2: conclusão do SDMA0 disparada pela interrupção, e critério da Corrida 233

**Origem:** a Corrida 232 (doc 615) provou o IH: 2.341 TRAPs viraram 2.341 vetores e 2.341 chamadas do nosso ISR. Mas o ISR só contava. Quem aposentava as submissões era o timer de 1 ms. A diferença com o amdgpu que sobra na P3 é essa: lá, o TRAP do SDMA dispara `amdgpu_fence_process` (`sdma_v5_0_process_trap_irq`), e não existe polling.

## 1. O que muda (candidato 3.380.1.0)

**Uma diferença:** quem acorda a aposentadoria do SDMA0.

- **ISR** (`Bc250ActiveP3Isr`, DIRQL):
  - `Bc250SdmaPresentIhIsr` agora devolve quantos TRAPs do SDMA0 a chamada consumiu.
  - Se consumiu algum, com a chave `BC250_P3_IRQ_COMPLETION=1` e o adaptador ativo, o ISR enfileira **o mesmo KDPC** que o timer e o submit já usam: `SdmaRetirementDpc` → `Bc250ActiveSdmaRetirementDpcRoutine` → `Bc250ActiveSdmaRetireAndNotify`, caminho provado no silício.
  - `KeInsertQueueDpc` vale em qualquer IRQL e devolve FALSE quando o DPC já está na fila.
  - **Nenhuma notificação ao dxgkrnl sai do ISR.** O `DxgkCbSynchronizeExecution`/`DxgkCbNotifyInterrupt` e o `DxgkCbNotifyDpc` continuam onde estavam, no DPC.
- **Timer como rede:** com o IH armado, o timer de aposentadoria passa de 1 ms para **10 ms** e só cobre interrupção perdida. Sem o IH, continua em 1 ms. **Diferença declarada:** o amdgpu não tem essa rede.
- **Telemetria:** `dpc_por_irq` (DPCs enfileirados pelo ISR) entra na linha `[P3_IH] regime`.

**O que não muda:**
- a lógica de aposentadoria (`PollRetirement`, guarda `SdmaRetireBusy`, notificação em lote);
- o anel gráfico, que continua por timer: o EOP do CP não está ligado.

**Provas offline:**
- `build-contract` EXIT=0, SYS de laboratório confirmado, auditorias PASS;
- portão `audit_p3_ih_waits_for_mmhub_gart` com **14 mutações reprovadas**: as 10 anteriores e mais 4 novas:
  1. ISR que enfileira o DPC sem TRAP;
  2. ISR que notifica o dxgkrnl em DIRQL;
  3. rede de 10 ms sem o IH armado;
  4. ISR que não enfileira o DPC.

**Incidente durante as mutações, corrigido:**
- Numa execução do script, a restauração de `bc250_active_miniport.c` falhou (`OSError`: arquivo bloqueado no momento da escrita). A mutação "arme da P3 antes do PSP" ficou aplicada.
- A auditoria pegou: reprovou com a regra da P3.0b.1. Desfiz à mão.
- O script passou a repetir a restauração e a conferir o conteúdo, e o fonte foi conferido por hash antes e depois da execução final.
- O pacote foi recompilado a partir do fonte restaurado.

**Pacote 3.380.1.0:** perfil `DISPLAY_RENDER_LAB`, auditoria PASS.

| Arquivo | SHA-256 |
|---|---|
| SYS | `4A4E19B40943D5527A5E2E3DDD277E6B1D75D67086A8ED2DF32C3E07CC1FDEC3` |
| INF | `9BF6D44BAB69000C24EE3DBBC5D884F9CF122845DEC7683125135107FFAC2D48` |
| CAT | `3BFC4E99BC42EC830083C9FAB3A2F97DFA52C1EF8B25DDDBD817834763D9E442` |
| UMD | `8782A5E5…` (inalterada) |

**Scripts:** `tools/prepare-corrida233-p32-conclusao-por-interrupcao.ps1` e `tools/EXECUTAR_CORRIDA233_P32_CONCLUSAO_POR_INTERRUPCAO_600S.cmd`. O preflight a seco parou só na CMOS.

## 2. Critério da Corrida 233

**Pré-condição:** PSP OK, StartDevice `status=0`, `[P3B_MMHUB] LIGADA`, `[P3_IH] ATIVO`. Uso normal: menu e Pesquisa.

**Desfechos:**

- **A — conclusão pela interrupção em regime**
  - *Assinatura:* todos os sinais abaixo.
    - linhas `[P3_IH] regime` com `dpc_por_irq` crescendo junto com `traps_sdma` (pode ficar menor, porque DPCs já na fila se fundem);
    - `IB EXECUTADO ... FromDpc=1` em regime;
    - zero `Blt Timeout`, zero `TDR_QUIESCE` do SDMA;
    - desktop normal;
    - teardown limpo (`[P3_IH] DESARMADO (teardown)`, `[P3B_MMHUB] DESLIGADA ... falhas=0`).
  - *Implicação:* a P3 fecha como mecanismo. Se a trava ainda vier, ela sai das causas, e o próximo da paridade é a **P6** (MTYPE coerente) ou o EOP do CP.
- **B — o ISR não enfileira**
  - *Assinatura:* `dpc_por_irq=0` com `traps_sdma` crescendo.
  - *Implicação:* a condição no ISR falha (contexto, `IsActive`, chave). A aposentadoria segue pelo timer de 10 ms, mais lenta. Ler o contexto.
- **C — regressão de desempenho ou de conclusão**
  - *Assinatura:* `Blt Timeout`, TDR do SDMA, ou desktop visivelmente mais lento que na 232.
  - *Implicação:* interrupções perdidas com a rede em 10 ms. Medir as latências `retire_qpc` contra a 232.
- **D — DPC em laço**
  - *Assinatura:* BugCheck `0x133`, ou `BUSY_YIELD` em rajada.
  - *Implicação:* a guarda de reentrância não segura o par ISR/DPC. Chave em 0.
- **E — fora do mapa**
  - *Assinatura:* descrever o observado antes de julgar.
  - *Implicação:* a definir.

**A trava dura continua possível.** Se vier, conta como a primeira amostra com conclusão por interrupção.

## 3. Pendências

**NÃO VALIDADO EM HARDWARE:**
- a P3.2;
- a P3.3 (RPTR do IH por doorbell);
- o EOP do CP (anel gráfico por interrupção).
