# Candidato 3.47.0.0 (SchemaVersion 118) — bisseção do scheduler no PFP

**Data**: 30/08/2026  
**Estado**: instrumentação observacional. **NÃO VALIDADO EM HARDWARE.**

## 1. Pergunta única

O 3.46 não alcançou a coerência porque `RLC_CP_SCHEDULERS` valia zero no
preflight da HQD. O 3.47 determina se o valor já entra zerado no trecho ou se
desaparece durante `Bc250ActiveRunPfpDirectLoad()`.

## 2. Instrumentação

Duas leituras novas do offset já catalogado `0x0003B2A8`:

```text
PfpSchedulerBefore
PfpSchedulerBeforeReadPassed
PfpSchedulerAfter
PfpSchedulerAfterReadPassed
```

O terceiro ponto já existia:

```text
HqdActiveRlcCpSchedulersBefore
```

Cada sonda abre um mapeamento read-only independente, faz uma única leitura,
confere que o transporte não registrou escrita e desfaz o mapeamento. Zero é
preservado como dado; os campos `ReadPassed` separam zero real de falha de
transporte.

## 3. Matriz de interpretação

| Antes do PFP | Depois do PFP | HQD | Conclusão |
|---|---|---|---|
| não-zero | zero | zero | PFP/soft reset elimina o scheduler; corrigir por snapshot/restore real |
| zero | zero | zero | scheduler já ausente; investigar estágio anterior, sem literal |
| não-zero | não-zero | zero | janela entre retorno do PFP e preflight HQD |
| não-zero | não-zero | não-zero | anomalia do 3.46 não reproduziu; o teste de coerência pode prosseguir |

## 4. Regra para a correção seguinte

Somente o primeiro resultado autoriza restaurar o valor: snapshot não-zero
capturado antes do PFP, escrita mascarada/dirigida no ponto causal, readback e
rollback/teardown. Nenhum candidato usará `0x58504800` como constante.

O restante do 3.46 permanece intacto. Se o scheduler estiver válido, o mesmo
boot exercita a invalidação HDP/GL2/GCVM e o tripé P1.

