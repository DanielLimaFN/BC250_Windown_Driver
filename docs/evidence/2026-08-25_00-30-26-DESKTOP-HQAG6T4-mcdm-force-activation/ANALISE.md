# Analise do binding forcado MCDM 0.2.3.0

## Resultado comprovado

| Campo | Valor |
|---|---:|
| `DxgkInitializeStatus` | `0` |
| `AddDeviceStatus` | `0` |
| `StartDeviceEntered` / `InterfaceAccepted` | `1` / `1` |
| `DeviceInfoStatus` | `0` |
| `PciStatus` | `0` |
| `PciBytesRead` | `64` |
| `BarCount` | `3` |
| `RuntimeMissing` | `1` |
| `StopDeviceStatus` / `RemoveDeviceStatus` | `0` / `0` |

A BAR de I/O VGA foi ignorada corretamente e tres BARs de memoria foram
decodificadas. O callback PCI fez somente uma leitura do header; nao houve
sizing write. StartDevice entao chegou ao bloqueio deliberado de runtime e
retornou `STATUS_NOT_SUPPORTED`, produzindo codigo PnP 43 sem reboot pendente.

## Proximo probe offline

0.2.4.0 executa `Bc250ResolveCmTranslatedBars` antes de `RuntimeMissing`. Assim,
o proximo ensaio medira se as tres identidades PCI casam de forma nao ambigua
com os intervalos e comprimentos realmente atribuidos pelo PnP. O schema 2
acrescenta `CmStatus`, `ResourceStatus` e `ResolvedBarCount`.

O pacote 0.2.4.0 passou `/W4 /WX`, todos os testes, Inf2Cat e assinatura:

- `bc250readonly.sys`: `ADF888DD999FAC99C31D978733323C00A0733EFE826CA503FCC7C79CAC5E8D57`;
- `bc250readonly.cat`: `E258993B173D81A9A041D2CCF327E5CECEB1B2BFF07A51C266A355FAC800C4F9`.

0.2.4.0 nao foi instalado. Nenhum MMIO, firmware, ring, fence ou compute foi
executado.

**NAO VALIDADO EM HARDWARE**
