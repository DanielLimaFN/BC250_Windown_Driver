# PSP GPCOM readonly - pacote 1.5.0.0

Data: 2026-08-25.

Status: **PACOTE CONSTRUIDO; NAO VALIDADO EM HARDWARE**.

O snapshot passa a ler 32 DWORDs em duas passagens, totalizando 64 leituras.
Foram adicionadas as mailboxes PSP que descrevem o estado TOS e o GPCOM ring:

- `C2PMSG_64`: estado/ACK do TOS;
- `C2PMSG_67`: WPTR;
- `C2PMSG_69/70`: endereço low/high do ring;
- `C2PMSG_71`: tamanho do ring.

Esses campos identificam um eventual ring PSP herdado em memória do host. Eles
não descrevem automaticamente uma TMR de VRAM e não são promovidos a reservas.
Não há escrita MMIO, comando PSP, firmware, PM4 ou compute.

Telemetria schema 12. Build KMD `/W4 /WX`, testes e auditorias: PASS.

- source SYS: `AAF475148D51ED2E47B87CA1F71C56DB787B0BAA07F049AFAEF5D66769FB74EC`;
- signed SYS: `ED18AD894A10F57FA1184518CED9E6E7B7AEC4D78031D8C8AD0B9BB84E5CF21E`;
- CAT: `D27585DD670DCBA2021B42EC68AFD72909A65853DD97E6A75E75CB311A18051D`.

## Resultado físico

Evidência: `docs\evidence\2026-08-25_20-39-11-DESKTOP-HQAG6T4-mcdm-replace`.

- schema 12, status zero, 64 leituras e duas passagens coerentes;
- SYS ativo igual ao hash assinado;
- `C2PMSG_64=0x80000000`: TOS pronto, sem eco de ring KM;
- `C2PMSG_67/69/70/71=0`: WPTR, endereço e tamanho GPCOM zerados;
- `C2PMSG_81=0x004DB399`: heartbeat do SOS, variável e não endereçável;
- nenhum GPCOM ring herdado existe no instante do `StartDevice`;
- as mailboxes não descrevem a TMR interna do PSP; ela permanece não reportada;
- inventário continua `INCOMPLETE`, sem PM4/compute.

O próximo gate é repetir a mesma captura 1.5.0.0 após reboot limpo. Isso evita
confundir estado residual de substituições online com estado inicial da placa.

## Repetição pós-reboot

Evidência: `docs\evidence\2026-08-25_20-43-40-DESKTOP-HQAG6T4-mcdm-replace`.

Todos os campos estruturais coincidiram com a captura anterior: topologia,
ReBAR, scanout, GCVM, CP, SDMA, TOS e GPCOM. Somente `C2PMSG_81` mudou de
`0x004DB399` para `0x002B2CDB`, confirmando sua natureza de heartbeat. Gate de
repetibilidade pós-reboot: PASS.
