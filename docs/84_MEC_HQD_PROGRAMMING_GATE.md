# Gate de programação MEC/HQD (pré-ativação)

Este é o próximo ensaio físico permitido após a evidência 1.66. Ele não pode
publicar trabalho: `CP_HQD_ACTIVE` deve permanecer `0`, o doorbell deve
permanecer `0` e `PQ_WPTR_LO/HI` deve permanecer `0` durante toda a transação.

## Pré-condições obrigatórias

- banco MEC1/P0/Q0 selecionado pela prova de isolamento, não pelo eco do seletor;
- snapshot físico 1.66 íntegro: banco inativo, bases e cursores zerados;
- buffers MC planos, alinhados, fora de scanout/TMR/reservas;
- MQD de 512 bytes gerado e validado com `HqdActive=0` e `UseDoorbell=0`;
- duas leituras HQD estáveis imediatamente antes da escrita.

## Ordem de APPLY, ainda sem execução

1. Gravar na memória MC o MQD, ring vazio, EOP, RPTR/WPTR e fence poison;
2. ler novamente todos os buffers e validar byte a byte;
3. selecionar o banco MEC comprovado;
4. programar somente as bases/configurações: MQD, EOP, VMID, PQ base, report
   addresses, `PQ_CONTROL`, estado persistente, prioridades e quantum;
5. manter explicitamente `DOORBELL_CONTROL=0`, `PQ_WPTR_LO/HI=0` e
   `HQD_ACTIVE=0`;
6. ler cada campo escrito duas vezes, rejeitando `0xFFFFFFFF`, divergência ou
   qualquer `ACTIVE!=0`;
7. restaurar todos os registradores no sentido inverso e conferir o snapshot
   original; restaurar e conferir a memória MC.

Qualquer falha salta diretamente para rollback. Nenhuma fence pode ser tratada
como prova nesta etapa: a ausência de movimento de `RPTR` é o resultado correto.

## Gate seguinte, separado

Somente após esta transação passar no silício será permitido um candidato de
ativação: `ACTIVE=1`, depois publicação controlada do WPTR/doorbell e validação
do tripé `RPTR==WPTR`, scratch e fence física GPU.

**NÃO VALIDADO EM HARDWARE.**
