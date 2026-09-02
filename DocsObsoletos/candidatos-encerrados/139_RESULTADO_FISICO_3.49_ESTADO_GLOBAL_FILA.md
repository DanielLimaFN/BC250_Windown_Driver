# Resultado físico 3.49 — estado global da fila persistente

**Data:** 30/08/2026  
**Pacote:** `3.49.0.0 / SchemaVersion 120`  
**SYS:** `360266FBE28A6E89C62579806B71A5A0E742A1180B87217738A52803C02B288D`  
**Veredito:** **P1 REPROVADO — NÃO VALIDADO EM HARDWARE.**

O binding terminou em `bc250mcdm / Código 0`, o display permaneceu visível e o
watchdog foi desarmado por confirmação humana. Isso não valida compute.

## Resultado do gate P1

Passaram:

- fila persistente ativada (`P1EngineState=1`, `P1ActivateStatus=0`);
- scheduler0 publicado como ME2/P0/Q0 (`RLC_CP_SCHEDULERS=0x585048C0`);
- `CP_MEC_CNTL=0` e `CP_ME_CNTL=0`, portanto MEC/ME não estavam haltados;
- coerência HDP/GL2/GCVM e submissão aceitas;
- shadow e registrador receberam `WPTR=12`;
- nenhum `HQD_ERROR`.

Falharam:

- `RPTR` do registrador e da memória permaneceram em `0`;
- fence de dispatch, resultado e fence do anel permaneceram envenenados;
- nenhuma submissão foi aposentada.

## Fronteiras observadas

```text
                         após ativação   após submit   após re-arme
CP_MEC_CNTL              0x00000000      0x00000000   0x00000000
CP_ME_CNTL               0x00000000      0x00000000   0x00000000
RLC_CP_SCHEDULERS        0x585048C0      0x585048C0   0x585048C0
CP_PQ_WPTR_POLL_CNTL     0x00000000      0x00000000   0x00000000
CP_PQ_WPTR_POLL_CNTL1    0x00000000      0x00000000   0x00000000
WPTR / RPTR              0 / 0           12 / 0        12 / 0
CP_CPC_STATUS            0x00000000      0xA0000080   0xA0000082
HQ_STATUS0               0x40000040      0x00008040   0x00008040
```

O contrato de ativação havia escrito `CNTL1=1` e confirmado readback. Como o
caminho persistente retorna antes do teardown, o zero posterior não foi
restauração do driver: o hardware consumiu/auto-limpou o gatilho. O re-arme do
3.45–3.49 fez apenas `ACTIVE=0 -> 1`; ele não repetiu `QUEUE_MASK=1` e, portanto,
não reproduziu integralmente a sequência one-shot que já executou shader.

Evidências:

- `docs/evidence/2026-08-30_21-35-20-DESKTOP-HQAG6T4-lab-binding/`
- `docs/evidence/2026-08-30_21-38-23-DESKTOP-HQAG6T4-p1-submission/`

**NÃO VALIDADO EM HARDWARE.**
