# Snapshot readonly de registradores de plataforma - pacote 1.3.0.0

Data: 2026-08-25.

Status: **PACOTE CONSTRUIDO; NAO VALIDADO EM HARDWARE**.

## Objetivo

Capturar duas leituras estáveis, sem escrita MMIO, dos registradores brutos que
descrevem o contexto GCVM 0 e o estado herdado dos rings CP RB0 e SDMA0. Este
incremento serve para descobrir o estado deixado pelo firmware/driver anterior;
não habilita filas, GPUVA, PM4 ou compute.

## Superfície readonly

- GCVM: `0xA3AC`, `0xA3B0`, `0xA42C`, `0xA430`, `0xA4AC`, `0xA4B0`;
- CP RB0: `0xC100`, `0xC104`, `0xC2C4`;
- SDMA0: `0x4B80`, `0x4B84`, `0x4B88`.

O transporte MMIO aceita somente offsets explicitamente permitidos. Cada um dos
12 DWORDs é lido em duas passagens; qualquer divergência rejeita o snapshot.
Resultado esperado em sucesso: 24 leituras, duas passagens estáveis e telemetria
schema 10 com os valores brutos.

## Limite de confiança

Os valores não produzem `PLATFORM_CARVEOUTS_PROBED`, não geram reservas e não
alteram `RuntimeInventoryComplete=0`. Endereços e campos de controle somente
serão decodificados depois de estabilidade física e validação contra os limites
de VRAM/BAR.

## Build 1.3.0.0

- build KMD `/W4 /WX`: PASS;
- testes offline e auditoria fail-closed: PASS;
- source SYS SHA256: `2D1D2B94D43B99AD72121F5ECB400DAB70156A40FC5D9804B5F5DDCD2F85171D`;
- signed SYS SHA256: `B0A61D9C7D32D9893D6D32DB41553B644B8CD60A1FC70561679655121405CD80`;
- CAT SHA256: `1AAC89B81CF998F7FA8D3301588371670E5B9A50773C823DD6DE83D31AD58493`.

## Gate físico

Executar `tools\replace-bc250mcdm.ps1`, confirmar `service=bc250mcdm`, observar o
Code 43 fail-closed esperado e analisar `PlatformSnapshotStatus`, contadores e os
12 DWORDs registrados no diretório de evidência. PM4/compute continuam proibidos.

## Resultado físico

Executado em 2026-08-25. Evidência:
`docs\evidence\2026-08-25_19-55-48-DESKTOP-HQAG6T4-mcdm-replace`.

- binding: `oem19.inf`, serviço `bc250mcdm`, Code 43 fail-closed esperado;
- schema 10, `PlatformSnapshotStatus=0`, 24 leituras e duas passagens estáveis;
- SYS ativo SHA256 igual ao SYS assinado do pacote;
- GCVM base/start/end: todos zero, portanto nenhuma page table herdada ativa;
- CP RB0: base `0xFEDCBAEF`, base high zero, controle `0x00A00000`;
- SDMA0: base low/high zero, controle `0x80840000` desabilitado;
- inventário permanece `INCOMPLETE`, flags `0x3`, uma reserva de scanout e
  11.059.200 bytes conhecidos como ocupados.

`0xFEDCBAEF` não é aceito como endereço de ring: é uma sentinela fora do espaço
de VRAM observado. Junto dos controles de repouso/desabilitado, a captura prova
que não existe ring CP/SDMA herdado válido nesses engines no instante do
`StartDevice`. Ela não prova ausência de PSP TMR ou de outros carveouts.

Status final deste marco: **VALIDADO EM HARDWARE SOMENTE PARA SNAPSHOT READONLY;
PM4/COMPUTE NAO EXECUTADOS**.
