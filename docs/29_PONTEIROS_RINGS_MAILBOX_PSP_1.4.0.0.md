# Ponteiros de rings e mailbox PSP - pacote 1.4.0.0

Data: 2026-08-25.

Status: **PACOTE CONSTRUIDO; NAO VALIDADO EM HARDWARE**.

O pacote amplia o snapshot readonly 1.3.0.0 de 12 para 27 DWORDs. Cada
registrador é lido duas vezes pelo BAR MMIO atribuído pelo PnP; sucesso exige 54
leituras idênticas.

Novas evidências:

- CP RB0: RPTR, WPTR low/high e endereço de writeback de RPTR low/high;
- SDMA0: RPTR/WPTR low/high e endereço de writeback de RPTR low/high;
- PSP: `C2PMSG_35`, `36`, `37` e `81` nos offsets MP0 diretos já catalogados.

As leituras não enviam comando PSP e não alteram rings. Mailbox não é tratada
como descrição de TMR sem semântica confirmada; ponteiro só pode produzir uma
reserva se o ring estiver habilitado e base/tamanho/faixa forem válidos.

Telemetria: schema 11. Versão: 1.4.0.0.

- source SYS SHA256: `3E43DFB9FA7BB8F3E5728088AFDCCD95104BE9927CE0720DD60C812C8414368C`;
- signed SYS SHA256: `18198A691C9612FEEF5E5471F7C793082525ED85CFC34905E57367ABD0C4EC8C`;
- CAT SHA256: `654C351A2B6143D1C979451F8B00C116B717258619D851C41A59E062780BBD89`;
- KMD `/W4 /WX`, testes offline e auditoria fail-closed: PASS.

PM4, compute, firmware e GPUVA continuam desabilitados.

## Resultado físico

Evidência: `docs\evidence\2026-08-25_20-35-01-DESKTOP-HQAG6T4-mcdm-replace`.

- schema 11, status zero, 54 leituras e duas passagens coerentes;
- SYS ativo igual ao hash assinado do pacote;
- CP RPTR/WPTR e endereço de writeback: todos zero;
- SDMA RPTR/WPTR e endereço de writeback: todos zero;
- CP permanece em repouso com base sentinela; SDMA permanece desabilitado;
- PSP 35/36/37/81: `FFFFFFFF/FFFFFFFF/00000000/004D5227`;
- a mailbox 81 é heartbeat variável do SOS, não endereço ou reserva;
- inventário continua incompleto, sem PM4/compute.

Conclusão: não há ring CP ou SDMA herdado válido. As quatro mailboxes amostradas
não informam a TMR. O próximo snapshot deve ler o estado e a descrição do GPCOM
ring em `C2PMSG_64/67/69/70/71`.
