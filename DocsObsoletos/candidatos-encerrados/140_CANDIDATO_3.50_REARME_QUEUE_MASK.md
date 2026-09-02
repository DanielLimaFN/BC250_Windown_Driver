# Candidato 3.50 — re-arme completo de `QUEUE_MASK`

**Data:** 30/08/2026  
**Pacote:** `3.50.0.0 / SchemaVersion 121`  
**SYS:** `7CA30AE0F14D4FB726DC4C17EE6A6034EB02379127E6822EE023C35FFFA4BB87`  
**Estado:** **ENSAIO INVALIDADO POR ARTEFATO HÍBRIDO.** A hipótese abaixo não
chegou ao hardware. **NÃO VALIDADO EM HARDWARE.**

> Correção de proveniência: o builder de laboratório recompilou apenas
> `bc250_active_miniport.c` e vinculou objetos antigos de
> `bc250_submission_engine.c` e `bc250_probe_telemetry.c`. A captura impossível
> (`ACTIVE=1` com `RearmQueueMaskPassed=0`) e os timestamps confirmaram o erro.
> O sucessor real é o candidato 3.51 / Schema 122.

## Hipótese única

A fila persistente é ativada vazia. Nesse instante, o hardware consome o
`QUEUE_MASK` com shadow `WPTR=0`; depois, `CP_PQ_WPTR_POLL_CNTL.EN` não está
disponível para buscar atualizações. O 3.50 testa se repetir o gatilho de load
com `WPTR=12` já publicado é suficiente para iniciar o fetch.

## Sequência transacional do experimento

```text
[PREFLIGHT] fila ativa, submissão aceita e RPTR ainda 0
     -> [SNAPSHOT] ACTIVE e cursor
     -> [APPLY] ACTIVE=0
     -> [READBACK] confirmar fila inativa
     -> [APPLY] CP_PQ_WPTR_POLL_CNTL1 = bit da ME2/P0/Q0
     -> [READBACK] exigir bit 0 = 1
     -> [APPLY] ACTIVE=1 somente se o readback passou
     -> [PROVA NO SILÍCIO] RPTR, duas fences e resultado do shader
     -> [TEARDOWN] caminho persistente já existente
```

Não há escrita de RPTR, fence ou resultado pela CPU. Falha de escrita/readback
do `QUEUE_MASK` impede a reativação, mantendo o comportamento fail-closed.

Telemetria nova:

- `P1RearmQueueMaskApplied`;
- `P1RearmQueueMaskReadback`;
- `P1RearmQueueMaskPassed`.

Auditorias offline aprovadas: build WDK, assinatura/Inf2Cat, pacote de lab,
auditoria de segurança e `git diff --check`.

**NÃO VALIDADO EM HARDWARE.**
