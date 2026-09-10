# Análise pós-corrida — laboratório 3.116.1.0

**Data:** 2026-09-09 17:47–17:50  
**Veredito:** reconciliação do cursor validada; tripé Node 1 reprovado

## Resultado objetivo

O candidato corrigiu exatamente a geometria que pretendia corrigir. Antes do ensaio, a
telemetria persistente dizia `Submitted=Retired=17`, `Wptr=204`; a fila física estava
drenada em `RPTR=0/WPTR=0`. O primeiro submit Node 1 foi aceito e publicou o descritor em
`WPTR=12`, provando que o cursor lógico foi rebaseado para o cursor físico.

O MEC, porém, não consumiu o descritor:

| Perna | Medição | Veredito |
|---|---|---|
| Publicação | `WPTR 0 -> 12`, submit aceito `17 -> 18` | PASS |
| Consumo | `RPTR 0 -> 0`, aposentados `17 -> 17` | FAIL |
| Efeito | fence observada 0; tripé/canários ausentes | FAIL |

O JSON do harness registra `hardware_validated=false`. Não houve recusa de node, plano ou
emissão.

## Discriminantes físicos

Imediatamente antes do submit:

```text
MEC_PRE_SUBMIT: Active=1 RPTR=0 WPTR=0 HqStatus=40008040 VMID=00000900
MEC_PRE_SUBMIT_CPF: CpcStalled=00040000
```

Imediatamente depois do doorbell:

```text
MEC_POST_DOORBELL: Active=1 RPTR=0 WPTR=12 HqStatus=00008040 VMID=00000900
MEC_POST_DOORBELL_CPF: CpcStalled=00040000
```

Na captura posterior:

```text
RPTR=0 WPTR=12 PqControl=D030090D FaultStatus=0 FaultAddr=0:0
```

`PQ_EMPTY=0` e a fila deixa o estado idle, mas o read pointer não avança. O
`CpcStalled=0x00040000` já existe **antes** do doorbell. Portanto, a corrida refuta duas
explicações fortes: (1) cursor lógico 204 era a causa suficiente; (2) retirar as escritas
especulativas de power-gating havia eliminado de forma estável o stall RCIU. A ausência do
stall na 3.115 foi transitória.

`FaultStatus=0` reduz a evidência para fault pontual de GCVM, mas não autoriza declarar a
tradução como causa eliminada. O readback zero do seletor também não deve ser usado sozinho
para afirmar banco errado, pois a mesma transação lê `VMID=0x900` na HQD alvo.

## Segurança e encerramento

O watchdog copiou o log integral antes do rollback: 76.526 linhas, SHA-256
`85E58D12767B76236F7DD50ACB1F95E8CB7605DE8E29F4979EE9DF629343FEAB`. Em seguida removeu
`oem35.inf` e restaurou o Adaptador de Vídeo Básico da Microsoft em Código 0.

## Próximo passo

Não repetir 3.116.1.0. Preservar a reconciliação, porque ela corrigiu um defeito real, e
construir o próximo discriminante em torno do `MEC2_WAIT_ON_RCIU_READ` presente antes do
doorbell. Qualquer nova escrita em registrador deve vir de fonte P1/P0 e ter hipótese
binária; esta corrida não autoriza restaurar as escritas especulativas retiradas na 3.115.
