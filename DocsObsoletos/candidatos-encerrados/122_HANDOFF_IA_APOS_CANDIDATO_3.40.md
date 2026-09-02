# Handoff para a próxima IA — estado após o candidato 3.40

Data local: 30/08/2026  
Último pacote ensaiado: **3.40.0.0 / SchemaVersion 111**  
Veredito: **NÃO VALIDADO EM HARDWARE**

Este documento é autocontido. Para detalhes ciclo a ciclo, leia os docs 118, 119 e 121.

---

## 1. Objetivo e Critério Inegociável

Precisamos executar um shader real nos CUs e fechar, no mesmo ciclo físico:
1. `RPTR == WPTR == ShaderDispatchDwords`;
2. Fence física posterior ao `DISPATCH_DIRECT` saindo de poison por escrita da GPU;
3. Resultado saindo de poison e conferido byte a byte (`0xBC250C01`);
4. Memória e registradores restaurados;
5. Nenhuma simulação por CPU.

Sem qualquer item: **NÃO VALIDADO EM HARDWARE**.

---

## 2. A Descoberta Causal do Candidato 3.40

O candidato 3.40 discriminou as transições de `CP_CPF_BUSY_STAT` (`BAR5 0x08220`):

```text
1. Antes de CP_HQD_ACTIVE=1         0x20000000 (apenas HQD_WPTR_POLL_BUSY)
2. Logo após readback de ACTIVE=1   0x60C60000 (PQ_BUSY, CONSUMED_RPTR, PQ_FETCHER, MESSAGE_BUSY)
3. Antes da observação do WPTR      0x68460000 (adiciona ROQ_PQ_BUSY)
4. Depois do WPTR, kick e polling   0x68460000 (invariante)
```

### Significado Físico:
- O estado ocupado do fetcher e da fila **NÃO é residual de boot**; ele nasce exatamente no momento em que `CP_HQD_ACTIVE = 1` é escrito.
- Ao subir `ACTIVE=1`, o hardware do Command Processor Fetcher tenta inicializar a fila HQD, entra em stall de fetcher/mensagem (`0x60C60000`), e a escrita subsequente de Doorbell Enable em `CP_PQ_STATUS` ativa `HQD_ROQ_PQ_BUSY`.
- Uma vez nesse estado, o fetcher nunca lê os pacotes PM4 do anel (`RPTR` permanece 0).

---

## 3. Próximo Candidato Mínimo: 3.41 / Schema 112

A causa raiz está na configuração de registradores HQD pré-`ACTIVE` que alimentam o Fetcher e o Mecanismo de Mensagens:
1. Comparar os campos do MQD / registradores HQD com o upstream Linux `gfx_v10_0_hqd_load` / `gfx_v10_0_kiq_init_register`:
   - `CP_HQD_DEQUEUE_REQUEST` (deve ser 0 ou inicializado de forma específica)
   - `CP_HQD_PERSISTENT_STATE` (preload size e flags de estado persistente)
   - `CP_HQD_IB_CONTROL` (min avail dwords)
   - `CP_HQD_PQ_CONTROL` (bits de min avail size, unord dispatch, priv state, kmd queue)
2. Isolar se algum campo do `CP_HQD_PQ_CONTROL` ou `CP_HQD_PERSISTENT_STATE` impede o PQ Fetcher de sair do estado BUSY.

---

## 4. Estado Atual da Máquina

- `bc250mcdm` ativo no momento da aceitação;
- **Requer rollback externo para restaurar `BasicDisplay` Código 0**:
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\rollback-bc250mcdm-lab.ps1 -Execute -IAcceptDisplayInterruptionRisk
  ```
- **Reboot manual obrigatório** antes de qualquer novo ensaio físico.

**NÃO VALIDADO EM HARDWARE.**
