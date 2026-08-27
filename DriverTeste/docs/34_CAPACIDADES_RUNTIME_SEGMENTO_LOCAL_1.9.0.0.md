# Capacidades runtime e segmento local - pacote 1.9.0.0

Data: 2026-08-26.

Status: **GATE DE CAPACIDADES RUNTIME APROVADO EM HARDWARE**.

Este incremento cria a primeira ponte entre o inventario fisico aprovado na
Fase 2 e as respostas de memoria/capacidade exigidas pelo Dxgkrnl. Nenhuma
capacidade e promovida enquanto o contexto ativo, o inventario integral e a
transacao VRAM reversivel nao estiverem aprovados.

## Implementacao

- `QueryAdapterInfo(DRIVERCAPS)` anuncia WDDM 2.7, compute-only, non-VGA e dois
  nos somente depois do provider runtime retornar pronto;
- `PHYSICALADAPTERCAPS` descreve dois nos e escolhe SDMA0 como paging/copy;
- `GetNodeMetadata` descreve `BC250 SDMA0` como copy e
  `BC250 Compute MEC` como other;
- `QUERYSEGMENT` e `QUERYSEGMENT3` descrevem exatamente um segmento local,
  CPU-visible, cujo tamanho vem de `CpuVisibleFreeBytes` e cujo endereco CPU
  vem do BAR traduzido pelo PnP;
- o paging buffer inicial e limitado a 64 KiB no segmento 1;
- nenhum GART, GPUVA, GPU MMU, IOMMU, submit, TDR por engine ou segundo segmento
  e anunciado;
- sem provider runtime pronto, as consultas operacionais falham com
  `STATUS_DEVICE_NOT_READY`.

O inventario reporta `0x0F574000` bytes CPU-visible livres no total, mas esse
total nao pode ser anunciado como uma faixa iniciada em zero: o scanout ocupa o
inicio do BAR. O provider deriva a faixa contigua entre o fim do scanout
(`0x00A8C000`) e o inicio do staging planejado (`0x0FBBE000`). Neste host, o
segmento esperado tem base `0x00A8C000` e tamanho `0x0F132000` (252.911.616
bytes). Nenhum desses limites e gravado como constante no callback.

## Gate fisico esperado

O pacote continua retornando falha intencional em `StartDevice`, portanto
Codigo 43 ainda e o resultado correto. O schema 17 deve registrar:

- `RuntimeCapsStatus=0`;
- `RuntimeCapsReady=1`;
- `RuntimeCapsBaseLow32=0x00A8C000`;
- `RuntimeCapsLocalLow32=0x0F132000`;
- `RuntimeCapsNodeCount=2`;
- a transacao VRAM do pacote 1.8 permanece aprovada;
- SYS ativo igual ao hash assinado do pacote.

Essa telemetria prova que as capacidades podem ser materializadas a partir do
contexto fisico. Ela ainda nao prova que o Dxgkrnl consumiu as respostas, pois o
miniport permanece fail-closed antes da inicializacao VidMm completa.

## Build e integridade

- build KMD `/W4 /WX`: PASS;
- matriz MCDM: 36/36 callbacks minimos e 5/5 extensoes GPUVA;
- testes offline de driver caps, physical caps, nos e segmento: PASS;
- auditoria de seguranca e signability: PASS;
- source SYS: `6F530A5936E4A15158041CBA99A39C89E81B0480984422EDB5ACF9502C93D408`;
- signed SYS: `5D902E8D343B0654ECE2DE3553F00D84181619AABF10AF005589FADC8F79EC4F`;
- INF: `545360A337AA3FEA7E1809D56D53AFFF73E483691D6F7CC2755323013582AAE8`;
- CAT: `4B10C41BBFD9D3632C87D6D2987ABDD96BC029E85DFB683843DF2B91965D667D`;
- signer: `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`.

Veredito: **INTEGRIDADE APROVADA; ACELERACAO NAO VALIDADA**.

## Resultado fisico

Evidencia:
`docs\\evidence\\2026-08-26_02-01-26-DESKTOP-HQAG6T4-mcdm-replace`.

- schema 17 confirmado;
- `RuntimeCapsStatus=0` e `RuntimeCapsReady=1`;
- base local `0x00A8C000` e tamanho `0x0F132000`, exatamente como derivados;
- dois nos reportados pelo provider;
- inventario integral, segundo preflight e transacao VRAM continuaram aprovados;
- SYS ativo `5D902E8D...EC4F`, identico ao pacote assinado;
- `bc250mcdm`, Codigo 43 intencional, sem reboot requerido;
- nenhum PM4, SDMA, firmware, PSP ou compute foi executado.

Gate do provider fisico de capacidades/segmento: **PASS EM HARDWARE**.
VidMm ainda nao consumiu o segmento porque `StartDevice` permanece fail-closed.

Nota de conformidade posterior: a revisao da especificacao MCDM mostrou que
`QUERYSEGMENT` e `QUERYSEGMENT3` nao devem ser atendidos por compute-only; MCDM
exige `QUERYSEGMENT4`. Portanto, o resultado 1.9 permanece valido apenas como
prova fisica da geometria do provider. O pacote 1.10 corrige a ABI antes de
qualquer tentativa de ativacao VidMm.
