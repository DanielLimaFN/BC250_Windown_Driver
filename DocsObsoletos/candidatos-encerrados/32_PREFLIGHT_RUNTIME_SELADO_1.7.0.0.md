# Segundo preflight com runtime selado - pacote 1.7.0.0

Data: 2026-08-26.

Status: **PACOTE CONSTRUIDO; NAO VALIDADO EM HARDWARE**.

O primeiro preflight readonly descobre topologia e retorna `RuntimeMissing`. O
miniport então adquire post-display ownership, constrói o inventário integral e,
se ele for `OK`, materializa uma entrada runtime selada com:

- as duas reservas físicas do relatório;
- índices BAR PCI conhecidos;
- flags de ownership produzidas pelo inventário completo;
- a mesma geração do snapshot;
- manifesto tipado PFP/ME/CE/MEC/MEC_JT1.

Essa entrada é passada a `Bc250DxgkReadonlyStageRuntime` e o preflight readonly é
executado novamente no mesmo `StartDevice`. O pacote continua retornando
`STATUS_NOT_SUPPORTED` depois do gate: ainda não anuncia adapter operacional,
não cria segmentos e não escreve VRAM/MMIO.

Gate físico esperado:

- schema 14;
- `RuntimeStageStatus=0`;
- `SecondPreflightStage=8` (`PREFLIGHT_READY`);
- `SecondReadonlyGatePassed=1`;
- candidato TMR e staging apenas planejados, com zero bytes escritos e zero
  chamadas ao transporte PSP;
- Code 43 fail-closed.

Hashes:

- source SYS: `E180DDD502BA1D42D55096E83CF81F0973F95EF1135420080DEFA2FC6734150B`;
- signed SYS: `537BAA1355290F6BEF96F47EB514838A6F14CBFD7836C9B6D8116CB8B2FEBED5`;
- CAT: `B3154D65C0BB07DB2C6907B4BD333BA56B8531F339AFC20021B4D336CFF8C033`.

Build KMD `/W4 /WX`, testes e auditorias: PASS. PM4/compute não executados.

## Resultado físico

Evidência: `docs\evidence\2026-08-26_01-36-01-DESKTOP-HQAG6T4-mcdm-replace`.

- schema 14 e SYS ativo igual ao pacote;
- inventário `OK`, flags `0x7`, duas reservas, completo;
- `RuntimeStageStatus=0`;
- `SecondPreflightStage=8` (`PREFLIGHT_READY`);
- `SecondReadonlyGatePassed=1` e ownership flags `0x7F`;
- TMR candidata: offset `0x0FC00000`, 4 MiB;
- staging candidato: offset `0x0FBBE000`, 267.440 bytes;
- `FirmwareBytesStaged=0` e `PspTransportCalls=0`;
- Code 43 fail-closed esperado.

Gate de runtime selado/preflight físico: **PASS**. Isto prova planejamento e
ownership; ainda não prova que a CPU ou GPU escreveu na memória candidata.
