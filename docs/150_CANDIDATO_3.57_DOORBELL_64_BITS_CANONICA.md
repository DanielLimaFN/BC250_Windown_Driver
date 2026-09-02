# Candidato 3.57 — Doorbell MEC2 canônica em uma única escrita de 64 bits

**Data:** 31/08/2026  
**Pacote:** `3.57.0.0`  
**Schema de telemetria:** `127`  
**Veredito atual:** **NÃO VALIDADO EM HARDWARE**

## Objetivo

Isolar a transação PCIe que desperta a fila persistente da Fase P2. O
candidato anterior publicava primeiro 32 bits e depois 64 bits no mesmo slot
de doorbell. O 3.57 emite somente uma escrita alinhada de 64 bits, depois que
shadow WPTR, MQD, coerência e WPTR MMIO passaram pelos respectivos readbacks.

## Contrato físico preservado

- `CP_MEC_DOORBELL_RANGE_LOWER = 0x00000110` permanece intacto.
- `CP_MEC_DOORBELL_RANGE_UPPER = 0x00000110` permanece intacto.
- A única abertura tocada é `BAR2 + 0x110`, derivada do snapshot e confirmada
  por `CP_HQD_PQ_DOORBELL_CONTROL = 0x40000110`.
- `BAR2+0x0` e `BAR2+0x220` não são escritos.
- O tamanho real da BAR2 resolvido pelo PnP limita o acesso.
- Divergência de mapeamento, alinhamento, limite, enable ou offset falha
  fechado antes da escrita.

## Nova observabilidade

O Schema 127 acrescenta:

- `P2DoorbellWriteFailures`;
- `P2DoorbellLastValueLow` e `P2DoorbellLastValueHigh`;
- `P2DoorbellControlBefore` e `P2DoorbellControlAfter`;
- `P2DoorbellHitObserved`.

`DOORBELL_HIT` é transitório e serve apenas como diagnóstico. Ele não
substitui o tripé físico obrigatório: `RPTR == WPTR`, fence gravada pela GPU
com poison check e integridade dos dados.

## Estado offline

- Build integral dos contratos: aprovado.
- Imagem de laboratório: compilada.
- Assinatura do SYS/CAT e Inf2Cat: aprovados.
- Auditoria do pacote: aprovada.
- Auditoria de segurança: aprovada.
- SHA-256 do SYS empacotado:
  `455CAF61BC047CFDB6A136BB91EA6D48325A290F93F425E3BEDE0376F3BA6B73`.

Compilação e auditoria não provam execução da GPU. Até um ensaio iniciado por
boot frio produzir o tripé completo, o candidato permanece
**NÃO VALIDADO EM HARDWARE**.
