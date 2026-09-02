# Candidato 3.48.0.0 (SchemaVersion 119) — publicação transacional do scheduler0

**Data**: 30/08/2026  
**Estado**: implementação offline. **NÃO VALIDADO EM HARDWARE.**

Pacote construído e auditado: SHA-256 do SYS
`317C8F9523E2FA5A28CE1C745A80652E30FF03C69EA38F2F34AB32F1B93E7CC8`.

## 1. Correção

O miniport declara explicitamente a fila que possui:

```text
ME=2, Pipe=0, Queue=0
```

Essa topologia não é inferida de memória residual: é propriedade do backend
MEC escolhido pelo miniport e coincide com as três provas físicas one-shot e
com o catálogo (`CP_ME2_PIPE0_INT_STATUS`, seletor `0x8`).

Após snapshot integral de `RLC_CP_SCHEDULERS`, a transação compõe apenas o byte
de scheduler0:

```text
desired = ENABLE | (ME << 5) | (PIPE << 3) | QUEUE = 0xC0
applied = (snapshot & ~0xFF) | (desired & 0xFF)
```

Scheduler1..3 são preservados. O readback integral é obrigatório. No teardown,
o snapshot original — inclusive zero — é restaurado e relido.

## 2. Comportamento esperado

- boot com `RLC_CP_SCHEDULERS=0`: aplica `0x000000C0`;
- boot histórico `0x58504840`: aplica `0x585048C0`;
- teardown one-shot do boot limpo: restaura `0x00000000`;
- ativação persistente publica novamente `0x000000C0` e mantém enquanto online.

O teste offline dedicado cobre exatamente o caso de entrada zero, readback e
restauração. O 3.46 continua responsável pela coerência HDP/GL2/GCVM após a
ativação.

## 3. Aceite físico

O mesmo ciclo precisa provar:

```text
HqdActiveRlcCpSchedulersBefore           = 0x00000000 (se reproduzir)
HqdActiveRlcCpSchedulersReadback & 0xFF  = 0xC0
HqdActiveRlcCpSchedulersRestorePassed    = 1
P1ActivateStatus                         = 0
P1CoherencyPassed                        = 1
RPTR == WPTR == 12
P1SelfTestPhysicalReady                  = 1
```

Nenhuma fence é escrita pela CPU. **NÃO VALIDADO EM HARDWARE.**
