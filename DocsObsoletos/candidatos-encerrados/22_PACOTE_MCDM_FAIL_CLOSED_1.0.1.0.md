# Pacote MCDM fail-closed 1.0.1.0

Data: 2026-08-25.

## Veredito

**INTEGRIDADE OFFLINE APROVADA. NAO VALIDADO EM HARDWARE.**

O pacote nao foi instalado, pre-estagiado ou associado ao dispositivo.

## Identidade

- `DriverVer=08/25/2026,1.0.1.0`;
- classe `ComputeAccelerator`;
- hardware ID `PCI\VEN_1002&DEV_13FE`;
- servico `bc250mcdm`;
- binario `bc250mcdm.sys`;
- certificado de teste
  `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B`.

## Hashes

- SYS bruto:
  `9E8469F41D38A2614D37E88531E510AD986A3B1868E959EAE2B98BC1BA71A1F7`;
- SYS assinado:
  `32C3D8BA77DBD564ED8794566CCB998FE34E43E1C108DBB1ECE0D5833F5EFE1B`;
- INF:
  `BF088BED386EB3F727E704651DB8AB625FF6A5A3763991FCCE59E4A60C46B393`;
- catalogo:
  `78CE0C9B3BA48BB576A800BA2172016CF76839DC6546DD88822F5BEB64C1BD5A`;
- recovery instalado:
  `2B0367F2C5501609841C0768B6B78F67143D2E336A48A898F29B2F2D88F61ADD`.

## Gates aprovados

- recovery instalado identico e com cobertura MCDM;
- matriz MCDM 36/36 e GPUVA 5/5 estrutural;
- auditoria fail-closed;
- manifesto CP sem escrita PSP/firmware;
- referencia a `DxgkInitialize` e link por `displib`;
- `Inf2Cat` sem erros ou avisos;
- Authenticode valido no SYS e catalogo;
- manifesto externo liga SYS bruto, artefatos assinados, INF, catalogo,
  recovery e signer;
- `audit-bc250mcdm-package.ps1` retorna exit 0.

## Gates ainda fechados

- servico orfao `bc250mcdm` ainda precisa ser tratado pelo procedimento de
  recuperacao antes de um ensaio limpo;
- nenhum stage ou binding do pacote 1.0.1.0 foi realizado;
- `StartDevice` continua fail-closed;
- ownership, inventario, memoria, GPUVA, PSP, interrupcoes, rings, PM4,
  compute, Vulkan e LLM nao possuem prova fisica.
