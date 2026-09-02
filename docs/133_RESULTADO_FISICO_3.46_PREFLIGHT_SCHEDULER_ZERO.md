# Resultado físico do 3.46 — preflight recusou scheduler zero

**Data**: 30/08/2026  
**Pacote**: 3.46.0.0 / SchemaVersion 117  
**SYS**: `C1480B18D386A379802C60DA444FF5EC850AB66CA1BA532F338D4E4E625DB973`  
**Veredito**: **NÃO VALIDADO EM HARDWARE**.

## 1. Operação

- resíduo 3.45 (`46A158CA…`) identificado e quarentenado;
- binding do SYS 3.46 com hash conferido;
- `bc250mcdm / Código 0` e display visível;
- SchemaVersion 117 confirmado;
- watchdog desarmado corretamente pela confirmação visual.

Evidência canônica:
`docs/evidence/2026-08-30_21-10-35-DESKTOP-HQAG6T4-lab-binding`.

## 2. Onde o ciclo parou

O caminho anterior ao HQD passou:

```text
SdmaRingWorkStatus      = 0
SdmaRingPhysicalReady  = 1
PfpDirectStatus        = 0
PfpDirectPhysicalReady = 1
```

A transação HQD e o `Online` persistente recusaram o mesmo preflight:

```text
HqdActiveStatus                = 2  (PREFLIGHT_FAILED)
HqdActiveRlcCpSchedulersBefore = 0x00000000
HqdActiveSelectedSelector      = 0
P1ActivateStatus               = 2
P1EngineState                  = 0  (OFFLINE)
P1SelfTestAttempted            = 0
```

`RLC_CP_SCHEDULERS=0` decodifica `ME=0`, que não identifica uma fila MEC
válida. O contrato abortou antes de selecionar banco ou aplicar a programação
da HQD. No ensaio 3.45, o valor no mesmo ponto era `0x58504800`, decodificando
ME2/Pipe0/Queue0.

## 3. O que este ensaio não respondeu

A correção de coerência do 3.46 não foi alcançada:

```text
P1CoherHdpApplied       = 0
P1CoherInvalidatePolls  = 0
P1CoherencyPassed       = 0
P1DoorbellKicks         = 0
P1SubmitCalls           = 0
```

Esses zeros não refutam a hipótese HDP/GL2/GCVM. Não houve submissão, kick,
fetch, fence ou prova de shader neste ciclo.

## 4. Disciplina fail-closed

Não se deve substituir `RLC_CP_SCHEDULERS` por `0x58504800` ou outro literal.
O seletor da fila precisa vir da topologia física observada; inventá-lo quando
o registrador vale zero violaria o preflight e poderia selecionar banco errado.

O próximo ensaio deve partir novamente de desligamento completo. Se o valor
zero se repetir, o próximo candidato deve ser somente observacional: capturar
o registro em limites anteriores do bring-up para identificar em qual etapa
ele deixa de conter a topologia, sem escrita nova.

## 5. Defeito corrigido no gate

O gate dizia "GPU consumiu na primeira janela" sempre que o re-arme não era
tentado. Isso era falso quando `P1SelfTestAttempted=0`. O diagnóstico agora
declara explicitamente que motor, coerência e kick não foram alcançados.

