# Análise da segunda captura

Data: 2026-08-22 19:07. Build do driver SHA-256
`626540e3c88ff0610feb475ecda98b228d3b30be6cf88a940c668cd7492dd7c4`.

## Resultado do mecanismo de segurança

O preflight SMU foi executado somente por leitura:

- Q0 CMD (`0x03B10A08`) = `0x00000037`;
- Q0 ARG (`0x03B10A48`) = `0x00000000`;
- Q0 RSP (`0x03B10A68`) = `0x00000000`.

A mensagem `0x37` é `GetGfxFrequency`. Como RSP permaneceu zero, o coletor
classificou Q0 como não pronto e omitiu todas as mensagens. Nenhuma escrita de
mailbox ocorreu nesta segunda captura.

Isto é evidência de uma transação Q0 não concluída no estado atual do boot.
Não permite concluir que o SMU esteja morto nem que o endereço esteja errado.
O transporte SMU Q0 permanece **NÃO VALIDADO EM HARDWARE**.

## PSP

`C2PMSG_64 = 0x80020000` continua com TOS pronto. `C2PMSG_81` mudou de
`0x00551196` para `0x005612B7`; isso confirma que o campo é status/heartbeat
variável, não uma versão do SOS. O rótulo da ferramenta foi corrigido.

O anel PSP externo permaneceu em `0x000000044FFCC000`, 4 KiB, WPTR `0x80`.
Ele não pertence ao `bc250dbg` e não deve ser destruído ou substituído.

## SDMA

O diagnóstico corrigido mostrou:

- RB habilitado: `RB_CNTL = 0x00001001`;
- base codificada: `0xF40FD000`, equivalente a GPU `0xF40FD00000`;
- WPTR MMIO: `0x44C`;
- RPTR MMIO: `0`;
- shadows/writeback: indisponíveis para o anel externo.

Portanto, não é possível calcular trabalho pendente nem declarar progresso.
A variação de `STATUS_REG` entre amostras e do PC em `STATUS2_REG` é apenas
estado dinâmico; sem RPTR, fence e resultado, execução SDMA permanece
**NÃO VALIDADA EM HARDWARE**.

## Próximo gate

1. Fazer desligamento completo e ciclo frio da máquina para restaurar o estado
   de firmware/mailbox sem escrever registradores de reset.
2. Repetir o baseline corrigido. O preflight deve encontrar Q0 RSP não zero
   antes de permitir uma única consulta segura.
3. Registrar os endereços de RPTR writeback e WPTR poll do anel externo.
4. Somente depois decidir se uma observação do anel externo é segura ou se o
   projeto deve criar um ring próprio, com snapshot e rollback explícitos.
