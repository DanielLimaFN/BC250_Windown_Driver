# 717 — Completar o flip pela própria interrupção (D05), preparado offline, sem pacote novo

**Data:** 12/10/2026, offline (sem corrida). Continuação do [doc 716](716_PASSAGEM_DE_BASTAO_2026-10-12_DCN1_A_DCN3_DIRQL1_E_O_QUE_FAZER_A_SEGUIR.md) §6.1, item barato que "não depende de D06".

**Pedido do Daniel:** preparar offline o item "completar o flip pela interrupção" da lista de oportunidades do doc 716. Nada aqui foi empacotado nem rodado na placa — é código, teste, portão e mutação, prontos para entrar no pacote da próxima corrida (274) quando o Daniel der o OK junto com D06/D12/D17.

## 1. O que a leitura do código achou

A interrupção de flip do HUBP0 (fonte 0x4F) já chegava 1:1 desde a DCN.1 (D05: "chega 1:1; ainda não usada para completar o flip, só conta"). A leitura de `Bc250DisplayDcnIsr` e `Bc250VsyncDpcRoutine` (`driver/wdk/bc250_display_ddi.c`) achou o motivo exato:

- O ISR (DIRQL) já chama `KeInsertQueueDpc` para QUALQUER fonte do DCN, inclusive o flip — isso é DIRQL-safe e já existia.
- Mas só VSTARTUP/VUPDATE/VLINE0 marcam `IrqPending`/`IrqLastQpc` (o flip não marca — ele não é um evento de vblank). Como nenhuma das três chega ao IH na 273 (D06, [doc 715](715_CORRIDA273_SEM_TRAVA_175S_COM_T3_DIRQL1_EM_EFEITO_VSYNC_NAO_ROTEADO_AUX0_EM_MODO_DDC.md)), `IrqLastQpc` nunca é atualizado e `Bc250Dcn2VsyncFromIrq` sempre devolve `-1` (ou `0`, dependendo do estado).
- Em `Bc250VsyncDpcRoutine`, quando esse retorno levava a um `return` antecipado (nenhum VSYNC novo para notificar o DXGK), a chamada a `Bc250DcnFlipPollLatch()` — que ficava DEPOIS desses retornos — nunca era alcançada naquela execução do DPC.
- Resultado: um DPC disparado SÓ pelo flip podia não conferir o latch; a resolução ficava esperando o próximo tique do timer de 4 ms (ou a próxima vez que o contador de quadros do OTG mudasse por acaso antes do `return`). Daí "só conta".

## 2. A correção

1. **ISR (DIRQL, sem mudança de risco):** no ramo `BC250_DCN_IRQ_FLIP` de `Bc250DisplayDcnIsr`, além de `IrqFlip++`, agora liga `s_Bc250Dcn2.FlipIrqDpcPending` por `InterlockedExchange(..., 1)` — só isso, sem log e sem lock (DIRQL.1 continua valendo; a auditoria confere que o ISR não ganhou `SpinLock` nem `Bc250DisplayLog`).
2. **DPC (`Bc250VsyncDpcRoutine`):** a conferência do latch (`Bc250DcnFlipPollLatch()`) foi movida para ANTES de qualquer saída antecipada ligada a notificar VSYNC ao DXGK. Ela roda em toda chamada do DPC, timer ou IRQ, e o sinalizador do ISR é consumido (zerado) no mesmo instante.
3. **Atribuição (quem resolveu, o timer ou a interrupção):** nova função pura, testável offline, sem registrador:

   ```c
   int Bc250DcnFlipLatchIsViaIrq(int FlipIrqDpcPendingConsumed, int LatchDetectedThisCall);
   ```

   Devolve 1 só quando o sinalizador do ISR estava ligado **e** a mesma chamada do DPC resolveu um latch novo (`Bc250DcnFlipPollLatch` passou a devolver `BOOLEAN`, não `VOID`). Contador novo: `s_Bc250Dcn.LatchedViaFlipIrq`.
4. **Evidência nomeada (Lei 1/3):** `[DCN1_RESUMO]` e `[DCN1_TEARDOWN]` agora publicam `latchados_via_irq_do_flip=N de latchados=M` — a próxima corrida responde sozinha se a interrupção, e não o timer, está completando o flip.

## 3. Testes, portão e mutação

- **Teste de contrato** (`driver/tests/dcn_flip_contract_test.c`, bloco 8): as quatro combinações da tabela verdade de `Bc250DcnFlipLatchIsViaIrq`, mais um caso com sinalizador negativo (qualquer valor não-zero conta como ligado).
- **Portão novo** `audit_dcn1b_flip_completo_pela_interrupcao` (`tools/audit-bc250mcdm-safety.py`), registrado na lista de auditoria. Confere:
  - o ISR só liga `FlipIrqDpcPending` por `InterlockedExchange`, sem log/lock;
  - `Bc250DcnFlipLatchIsViaIrq` é pura (sem `Bc250DcnRead`/`Bc250DcnWrite` no corpo);
  - `Bc250DcnFlipPollLatch()` é chamada no DPC **antes**, textualmente, de `Bc250Dcn2VsyncFromIrq(`;
  - o DPC consome o sinalizador e conta `LatchedViaFlipIrq`;
  - o `[DCN1_RESUMO]` nomeia `latchados_via_irq_do_flip=`.
- **Mutação provada (Lei 10):** desfiz a correção (devolvi `Bc250DcnFlipPollLatch()` para a posição antiga, depois do `fromIrq`) e roda a auditoria — **reprovou** com as duas mensagens esperadas do portão novo. Reaplicada a correção, a auditoria volta a `PASS`.
- **Compilação:** `dcn_flip_contract_test.exe` compilado e executado (offline, user-mode, mesmo `CFLAGS` do `build-contract.bat`): **OK**, zero falhas. `bc250_display_ddi.c` compilado isoladamente com `/W4 /WX` e o mesmo toolchain VS2026 + WDK 10.0.28000.0 do projeto, com `BC250_ENABLE_DXGK_REGISTRATION` definido: **zero warnings, EXITCODE=0**. `python tools/audit-bc250mcdm-safety.py`: **PASS** (todas as auditorias, incluindo as pré-existentes).
- **Não executado ainda:** o `build-contract.bat` completo (todas as ~70 suites + o `.sys`), o empacotador de laboratório e qualquer corrida física. Isso fica para quando este item entrar no pacote da próxima corrida.

## 4. Estado e próximo passo

- **NÃO VALIDADO EM HARDWARE.** Tudo aqui é leitura de código, escrita de código, teste puro e auditoria estática — nenhum registrador foi tocado na placa.
- Nenhuma chave nova do INF: esta mudança reaproveita a infraestrutura já armada por `DcnFlip`/`DcnExt`; não há escrita de registrador adicional (o único registrador novo tocado é o mesmo `HUBPREQ0_FLIP_INTERRUPT` que a DCN.3 já reconhecia e já tinha permissão de ack).
- **Fica pronto para entrar no pacote da Corrida 274** junto com D06 (leitura), a ampliação de janela e D17 opcional, como o doc 716 §5 propôs. Quando o Daniel aprovar o pacote da 274 (e der o OK específico de D12, que é escrita), este item entra sem corrida própria — o sinal que prova se funcionou é `latchados_via_irq_do_flip` > 0 no `[DCN1_RESUMO]`/`[DCN1_TEARDOWN]` daquela corrida.
- Critério, escrito antes (Lei 10), para quando a 274 rodar:
  1. `latchados_via_irq_do_flip` cresce com os latches (idealmente perto do total, já que D05 mede 1:1) → a interrupção do flip está completando a maioria das resoluções, não o timer de 4 ms → confirmada.
  2. `latchados_via_irq_do_flip=0` com `latchados` > 0 → a interrupção não está sendo entregue a tempo do DPC rodar antes do próximo latch (ou o IH não está armado); volta a ser hipótese e o próximo suspeito é a latência ISR→DPC, não mais a posição da chamada no código.
  3. Os baldes `latch em quadros`/`latch em ms` do mesmo resumo continuam fechando a conta (`FECHA`) como nas corridas 269–273 — qualquer regressão de contabilidade (que não deveria acontecer, já que a lógica de resolução do latch não mudou) é E (fora do mapa) e precisa de investigação própria antes de aceitar o resultado.
