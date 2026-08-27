# Analise do binding forcado MCDM 0.2.2.0

## Avanco comprovado

| Campo | Valor |
|---|---:|
| `DriverEntryReached` | `1` |
| `InitializationDataReady` | `1` |
| `DxgkInitializeStatus` | `0` |
| `AddDeviceEntered` / `AddDeviceStatus` | `1` / `0` |
| `StartDeviceEntered` | `1` |
| `InterfaceAccepted` | `1` |
| `DeviceInfoStatus` | `0` |
| `PciStatus` | `8` (`io_bar`) |
| `PciBytesRead` | `64` |
| `BarCount` | `0` |
| `StopDeviceStatus` / `RemoveDeviceStatus` | `0` / `0` |

O callback ausente da versao anterior foi corrigido: o `dxgkrnl` registrou a
tabela, chamou AddDevice e entrou em StartDevice. Isso valida somente o ciclo
PnP e o caminho readonly ate a leitura de PCI config.

## Gate atual

`BC250_PCI_BAR_READ_IO_BAR=8`. O decodificador retornava esse erro ao encontrar
qualquer BAR com o bit `PCI_ADDRESS_IO_SPACE`, limpando tambem as BARs de
memoria ja descobertas. Uma GPU VGA pode ter uma BAR de I/O legada; ela nao
pertence ao contrato de saida, que armazena `BC250_PCI_BAR_RESOURCE` de memoria.

A correcao 0.2.3.0 ignora a entrada de I/O e continua a varredura sem compactar
ou renumerar os indices PCI das BARs de memoria. Os testes cobrem BAR0 64-bit,
BAR2, BAR4 I/O e BAR5, alem do caso em que uma BAR antes de BAR5 e I/O.

Build 0.2.3.0 offline:

- `/W4 /WX` e todos os contratos: aprovados;
- Inf2Cat: zero erros e zero avisos;
- assinaturas `.sys` e `.cat`: validas;
- `bc250readonly.sys`: `328ECEDD4D6EAC10E654B80E9E6B88D4E08D03CD4BD7C6D3516C14C8A2A5265D`;
- `bc250readonly.cat`: `078B7E77A8C0B679AADEA0A493D4382B7A52A032C1E1659751550C402016609E`.

0.2.3.0 nao foi instalado. Nao houve firmware, MMIO, compute, ring, fence nem
resultado produzido pela GPU.

**NAO VALIDADO EM HARDWARE**
