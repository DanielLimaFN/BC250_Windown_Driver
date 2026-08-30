# Prompt de retomada para a próxima IA

Assuma o desenvolvimento do driver BC-250 (`1002:13FE`, GC 10.1.3, Windows
11 x64 kernel) no repositório `C:\Users\Daniel\Desktop\NamixWDDM`.

Leia primeiro, nesta ordem:

1. `CLAUDE.md`;
2. `AGENTS.md`;
3. `BC250_HARDWARE_RULES.md`;
4. `docs/119_HANDOFF_IA_APOS_CANDIDATO_3.39.md`;
5. `docs/118_RESULTADO_FISICO_3.39.md`.

O doc 119 é autocontido e contém o estado técnico, hashes, armadilhas de
ferramenta e o próximo teste mínimo. O roadmap consolidado foi atualizado para
a revisão 4.1.

Resumo do ponto de partida: o 3.39/Schema 110 mediu
`CP_CPF_BUSY_STAT=0x68460000` antes da doorbell, depois dela e no polling final.
Já estavam ocupados antes do kick `HQD_PQ`, `WPTR_POLL`, `ROQ_PQ`,
`CONSUMED_RPTR`, `PQ_FETCHER` e `HQD_MESSAGE`. Portanto, a causa primária está
no protocolo de `CP_HQD_ACTIVE`/publicação inicial do WPTR, não na doorbell.
`RPTR/WPTR=0/40`; resultado `0x43DAFFFE` e fence `0xA55A0FF0` permaneceram
envenenados. Nenhuma wave executou.

Primeira entrega: preparar o candidato 3.40/Schema 111, sem mudar PM4, MQD ou
GCVM. Adicione somente amostras read-only de `CP_CPF_BUSY_STAT` imediatamente
antes e depois de `CP_HQD_ACTIVE`, e antes/depois do limite de publicação do
WPTR. Registre tudo como observação; não crie abort, atraso ou escrita nova.
Compile e teste antes de propor ensaio físico.

Estado da máquina ao final do handoff: rollback externo concluído,
`BasicDisplay`, Código 0. Como o 3.39 foi submetido neste boot, faça reboot
manual antes de qualquer novo ensaio. Uma única submissão física por boot. O
operador executa/confirma os passos de hardware; você prepara, compila, audita
e analisa os pacotes.

Nunca declare sucesso por build, `STATUS_SUCCESS` ou mudança parcial de estado.
O aceite continua sendo o tripé estrito (`RPTR==WPTR`, fence física saindo do
poison e resultado correto byte a byte), com rollback verificado.

**NÃO VALIDADO EM HARDWARE.**
