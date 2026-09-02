# Resultado físico do 3.47 — scheduler já zero antes do PFP

**Data**: 30/08/2026  
**Pacote**: 3.47.0.0 / SchemaVersion 118  
**SYS**: `CD680164D3808A298763A3C045B434241FCFDC0815E3E731338CE39C6033A718`  
**Veredito**: **NÃO VALIDADO EM HARDWARE**.

As duas sondas read-only fecharam:

```text
PfpSchedulerBeforeReadPassed = 1
PfpSchedulerBefore           = 0x00000000
PfpSchedulerAfterReadPassed  = 1
PfpSchedulerAfter            = 0x00000000
HqdActiveRlcCpSchedulersBefore = 0x00000000
```

O PFP não zerou o registro: ele já entrou zero. SDMA e PFP fecharam suas provas
físicas, mas a HQD recusou `schedulerMe=0`; P1 não chegou à coerência.

O resultado revela uma dependência herdada: a transação esperava que firmware
ou driver anterior já tivesse publicado scheduler0. Em boot limpo isso não é
garantido. A correção deve fazer o miniport publicar a fila MEC que possui,
preservando scheduler1..3 e restaurando o snapshot no teardown.

Evidências:

- `docs/evidence/2026-08-30_21-21-34-DESKTOP-HQAG6T4-lab-binding`;
- `docs/evidence/2026-08-30_21-22-05-DESKTOP-HQAG6T4-p1-submission`.

