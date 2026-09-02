# Stage MCDM 1.0.1.0 sem binding

Data: 2026-08-25.

## Veredito atual

**STAGE APROVADO. BINDING NAO EXECUTADO. NAO VALIDADO EM HARDWARE.**

O pacote fail-closed foi copiado para o Driver Store por uma execucao elevada
feita pelo usuario. O dispositivo nao foi instalado, atualizado ou religado ao
pacote experimental.

## Evidencia observada

- INF publicado: `oem19.inf`;
- marcador: `C:\BC250_TEST_OEMINF.txt`, contendo somente `oem19.inf`;
- exatamente um INF MCDM correspondente em `C:\Windows\INF`;
- exatamente um repositorio `bc250mcdm.inf_amd64_*` no FileRepository;
- INF, SYS e CAT do repositorio iguais aos hashes do manifesto;
- assinatura do SYS e CAT valida com thumbprint
  `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`;
- BC-250 ainda ligada a `BasicDisplay`, `ConfigManagerErrorCode=0`;
- servico `bc250mcdm` ausente;
- `C:\Windows\System32\drivers\bc250mcdm.sys` ausente;
- recovery `DRY-RUN` reconhece o pacote e o marcador.

Hashes do pacote estagiado:

```text
INF  BF088BED386EB3F727E704651DB8AB625FF6A5A3763991FCCE59E4A60C46B393
SYS  32C3D8BA77DBD564ED8794566CCB998FE34E43E1C108DBB1ECE0D5833F5EFE1B
CAT  78CE0C9B3BA48BB576A800BA2172016CF76839DC6546DD88822F5BEB64C1BD5A
```

## Gate automatizado

`tools\audit-bc250mcdm-staged.ps1` e somente leitura e exige:

1. recovery e pacote local integros;
2. marcador com exatamente um `oemNN.inf`;
3. cardinalidade unica no INF publicado e no FileRepository;
4. hashes e assinaturas iguais ao manifesto;
5. BC-250 ainda em `BasicDisplay`, Codigo 0;
6. ausencia do servico e do SYS ativo;
7. reconhecimento do pacote pelo recovery em `DRY-RUN`.

O gate retornou exit 0. Isso prova somente a integridade do stage e a ausencia
de binding. Nao prova `DriverEntry`, `AddDevice`, `StartDevice`, MMIO, PM4,
compute, Vulkan ou LLM.

## Proximo limite de autorizacao

O binding e uma operacao separada, com risco de perda temporaria de video e
necessidade de recovery em Modo de Seguranca. Nenhum harness de binding deve ser
executado sem uma nova autorizacao explicita do usuario.

O harness `tools\bind-bc250mcdm-failclosed.ps1` ja esta preparado. Seu modo
padrao passou com zero escrita; a rota mutavel exige simultaneamente `-Execute`,
`-IAcceptBlackScreenRisk`, PowerShell elevado, processo 64-bit e uma segunda
execucao limpa do gate pós-stage imediatamente antes da chamada de binding.
`tools\capture-bc250mcdm-binding.ps1` coleta PnP, SetupAPI, eventos, servico e a
telemetria `Parameters\Probe` apos o ensaio.
