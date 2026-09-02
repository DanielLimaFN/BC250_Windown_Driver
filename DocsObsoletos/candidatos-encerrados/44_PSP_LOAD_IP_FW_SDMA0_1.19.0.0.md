# PSP LOAD_IP_FW SDMA0 - pacote 1.19.0.0

Data: 2026-08-26.

Status: **TRANSPORTE/FENCE PSP APROVADO; FIRMWARE RECUSADO POR CABECALHO INCLUIDO; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   80%
Miniport MCDM funcional            43%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

## Objetivo limitado

Carregar somente o microcodigo SDMA0 pelo ring GPCOM owned do miniport. Este
pacote nao configura o ring SDMA, nao altera GCVM ativa e nao envia PM4 ou
compute.

Firmware incluido no catalogo:

- arquivo `cyan_skillfish2_sdma.bin`;
- 33.792 bytes;
- PSP firmware type `9`;
- ucode version `52`;
- SHA-256 `15D0D3626DA7F2513F03B13BB7E02EEEECCAB276276AE27D0B6067B5F25E9E95`.

O KMD abre a copia instalada em `System32\\drivers`, exige tamanho exato e
calcula SHA-256 antes de permitir DMA.

## Transacao fisica

```text
preflight/snapshot -> create GPCOM -> ACK estrito
  -> command LOAD_IP_FW -> WPTR=16 -> fence PSP
  -> resposta status zero -> checksum SDMA0
  -> destroy GPCOM -> ACK estrito -> restore/readback
```

Ring, comando, fence e firmware usam alocacoes contiguas non-cached abaixo de
4 GB. A CPU inicializa o fence com zero, mas nunca o sinaliza.

Se destroy falhar depois de create, todas as quatro alocacoes permanecem
pinadas ate reboot para evitar DMA use-after-free. O microcodigo carregado nao
possui rollback por registrador; reboot/reset da GPU encerra esse estado.

## Telemetria schema 27

- status, tamanho e tipo do firmware;
- ACK create/destroy;
- fence esperado, observado e polls;
- status bruto da resposta PSP;
- checksum SDMA antes/depois;
- flags fence/response/checksum/destroy/restore/physical ready.

## Build e integridade

- build completo e testes unitarios: PASS;
- timeout de fence com destroy/restore: PASS offline;
- DriverVer `08/26/2026,1.19.0.0`;
- source SYS: `F95E73906DBA5D58B3A1FE2D66D52BA37B433EA0805D5D24E74EC43F149FF1E5`;
- signed SYS: `4FC145AA00A73E88857B7A9CA8634DAD6626352B7200FD275169E0BEFC8CB562`;
- INF: `D2377E1060EC0462D93A0CEE690A4F17F2D1BF9626CDD288381C8EAE8E818488`;
- CAT: `4C33C099E2C471E7A1D89540A941B612710C38567EED64862E98C4B07DDCAFAF`;
- `HardwareValidated=false`.

**NAO VALIDADO EM HARDWARE**

## Resultado fisico e causa

Evidencia: `docs/evidence/2026-08-26_13-12-27-DESKTOP-HQAG6T4-mcdm-replace`.

- fence PSP `0 -> 1` no segundo poll;
- resposta `0xFFFF0008` (`TEE_ERROR_ITEM_NOT_FOUND`);
- checksum permaneceu zero;
- destroy e restore passaram;
- causa: foram enviados 33.792 bytes, incluindo o header AMD de `0x100` bytes;
- a evidencia historica de sucesso enviou somente 33.536 bytes a partir do
  offset `0x100`;
- pacote 1.20 corrige para `PA + 0x100`, tamanho 33.536.
