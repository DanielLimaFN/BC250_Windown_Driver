# Resultado 3.50 — ensaio invalidado por build híbrido

**Data:** 30/08/2026  
**Pacote ensaiado:** `3.50.0.0 / SchemaVersion 121`  
**Veredito:** ensaio sem valor para a hipótese de `QUEUE_MASK`.
**NÃO VALIDADO EM HARDWARE.**

A telemetria registrou simultaneamente:

```text
P1RearmQueueMaskApplied  = 0
P1RearmQueueMaskReadback = 0
P1RearmQueueMaskPassed   = 0
P1RearmActiveAfterOn     = 1
```

Essa combinação não pode ser produzida pelo fonte 3.50: a escrita de
`ACTIVE=1` fica dentro do ramo que primeiro define `RearmQueueMaskPassed=1`.

Os timestamps fecharam a causa:

```text
output_lab/bc250mcdm.sys              00:40:12
output/bc250_probe_telemetry_driver   00:41:54
output/bc250_submission_engine_driver 00:41:55
```

O SYS de laboratório foi vinculado antes da recompilação dos dois objetos. O
script aceitava objetos apenas por existência, sem provar frescor. Assim, o
hardware executou o re-arme antigo, sem a nova escrita de `QUEUE_MASK`.

Evidência física preservada em
`docs/evidence/2026-08-30_21-46-36-DESKTOP-HQAG6T4-p1-submission/`.

**NÃO VALIDADO EM HARDWARE.**
