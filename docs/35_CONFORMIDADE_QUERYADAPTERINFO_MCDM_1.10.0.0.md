# Conformidade QueryAdapterInfo MCDM - pacote 1.10.0.0

Data: 2026-08-26.

Status: **GATE FISICO APROVADO PARA CONFORMIDADE E GEOMETRIA; ACELERACAO NAO VALIDADA**.

A geometria fisica aprovada no pacote 1.9 foi preservada, mas a ABI de consulta
foi corrigida antes de permitir que o Dxgkrnl inicialize VidMm.

## Correcao de contrato

Segundo as diretrizes MCDM da Microsoft, o compute-only KMD:

- deve atender `DRIVERCAPS`, `NUMPOWERCOMPONENTS`,
  `HISTORYBUFFERPRECISION` e `QUERYSEGMENT4`;
- nao deve atender `QUERYSEGMENT`, `QUERYSEGMENT2` ou `QUERYSEGMENT3`;
- deve manter `SupportNonVGA=FALSE`;
- antes de anunciar caps completos, precisa de GPUVA, paging, TDR por engine,
  preempcao, submit e history buffer reais.

O pacote 1.10 implementa a consulta de duas passagens de `QUERYSEGMENT4`,
incluindo `SegmentDescriptorStride`, e descreve a mesma faixa fisica aprovada:

- base GPU local derivada: `0x00A8C000`;
- endereco CPU derivado: `0xC0A8C000` neste host;
- tamanho: `0x0F132000`;
- segmento 1, linear, local e CPU-visible;
- paging buffer reservado em 64 KiB;
- zero aperture/GART anunciado.

`NUMPOWERCOMPONENTS` retorna zero. `HISTORYBUFFERPRECISION`, `DRIVERCAPS`,
metadata operacional de nos e caps fisicos permanecem fechados enquanto
`FullMcdmReady=0`. Isso impede anunciar TDR, GPUVA, paging ou submit antes de
suas provas fisicas.

## Gate esperado

Schema 18:

- `McdmSegment4Ready=1`;
- `McdmFullCapsReady=0`;
- geometria, inventario, segundo preflight e transacao VRAM iguais ao 1.9;
- Code 43 intencional;
- zero GPUVA, page table, PM4, SDMA, firmware ou compute.

O teste offline cobre as duas chamadas de `QUERYSEGMENT4`, stride invalido,
tipos legacy proibidos, caps MCDM e as consultas obrigatorias. A validacao
fisica deste pacote comprovara somente que a geometria continua pronta; o
Dxgkrnl ainda nao consumira a resposta enquanto `StartDevice` falhar fechado.

## Resultado fisico

Execucao em 2026-08-26, evidencia
`docs/evidence/2026-08-26_02-19-18-DESKTOP-HQAG6T4-mcdm-replace`:

- `SchemaVersion=18`, servico `bc250mcdm`, Code 43 intencional;
- `RuntimeCapsStatus=0`, `RuntimeCapsReady=1`;
- base `0x00A8C000`, tamanho `0x0F132000`, dois nos;
- `McdmSegment4Ready=1` e `McdmFullCapsReady=0`;
- inventario e segundo preflight aprovados;
- transacao VRAM com readback e restauracao aprovados;
- hash do SYS ativo igual ao SYS assinado do pacote 1.10.

O gate 1.10 esta, portanto, aprovado no hardware apenas para a preservacao da
geometria e para o estado fail-closed. Nenhum comando PM4 foi submetido e nao
houve avanco de RPTR/WPTR, fence de GPU ou resultado de compute.

## Build e integridade

- build KMD `/W4 /WX`: PASS;
- matriz MCDM e auditoria de seguranca: PASS;
- signability e catalogo: PASS, sem erros ou avisos;
- source SYS: `7C3181D3F744E22F26C54A4EBEE64EA0E5B42D6A2E3EDD75F68F90B9E28A7222`;
- signed SYS: `6B7AF05AD663E18C140C2F19C4895433F07A85AC650C87ECBC94E467EC33C17A`;
- INF: `1AF3243C982AA6423588477EF1E66ED55DCE25D96F027AF82B4B92A82393AC9A`;
- CAT: `13842E65D9DA2C7D0BCAAD316FFC95C781DA38D6AB889DEAF0EB27B981874C58`;
- `HardwareValidated=false` no manifesto.

Veredito: **CONFORMIDADE/GEOMETRIA APROVADAS FISICAMENTE; ACELERACAO NAO VALIDADA**.
