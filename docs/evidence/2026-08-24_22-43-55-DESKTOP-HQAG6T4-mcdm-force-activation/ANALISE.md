# Analise do binding 0.5.0.0

## Correcao de interpretacao posterior

Auditoria do codigo posterior ao ensaio encontrou que
`EXCLUSIVE_ADAPTER` e `INVENTORY_COMPLETE` eram acrescentados pela propria DDI.
Assim, `OwnershipFlags=0x7F`, `OwnershipState=1` e
`ReadonlyGatePassed=1` registram que o contrato interno aceitou flags
autoatribuidos; eles **nao provam** ownership fisico, inventario completo de
reservas, faixa livre ou exclusao de scanout. Os dados readonly de PCI, recursos
PnP, topologia MMIO e ReBAR permanecem evidencias fisicas validas. O plano
TMR/staging permanece somente um calculo sobre inventario incompleto.

O codigo offline atual removeu essa autoatribuicao: sem produtor runtime ele
termina em `RuntimeMissing=1` antes de ownership/preflight.

## Identidade do ensaio

- INF publicado: `oem16.inf`;
- modo: `UpdateDriverForPlugAndPlayDevicesW`, `INSTALLFLAG_FORCE`;
- retorno da API: sucesso, Win32 0, sem reboot requerido;
- SHA-256 SYS: `BFB5D980CFC2F44718B24705AE65ABBF81022A7E8081BE09F38DAE6DBB7411F2`;
- SHA-256 CAT: `96F79201176726C8AF268BD5F6E13763D9E9F0ECA6BBFBD61F245479E286D0B9`;
- schema de telemetria: 5.

## Gates aprovados no caminho readonly

`DriverEntry`, `DxgkInitialize`, AddDevice e StartDevice foram observados. PCI,
recursos PnP, MMIO, topologia e ReBAR retornaram status zero. Foram detectados
512 MiB de VRAM e 256 MiB CPU-visible, com oito leituras de topologia em duas
passagens estaveis. Os status zero posteriores de ownership/endereco/PSP sao
resultado de precondicoes autoatribuidas e ficam reclassificados como exercicio
do contrato, nao evidencia fisica de ownership.

| Payload | Bytes |
|---|---:|
| PFP | 263168 |
| ME | 263168 |
| CE | 263040 |
| MEC principal | 267440 |
| MEC_JT1 | 896 |

O plano candidato colocou a TMR de 4 MiB em `0x1FC00000` e o staging
CPU-visible em `0x0FFBE000`. Esses enderecos sao resultado do planejador; nao
constituem reserva fisica nem autorizam escrita.

## Limite da evidencia

```text
FirmwareBytesStaged = 0
PspTransportCalls   = 0
ReadonlyGatePassed  = 1
```

Nenhum blob foi copiado, nenhuma TMR foi configurada e nenhum comando foi
enviado ao PSP. Nao houve firmware aceito/ativo, ring, RPTR/WPTR, fence ou
resultado produzido pela GPU.

StartDevice retornou deliberadamente `STATUS_NOT_SUPPORTED`; o Codigo 43 e o
fail-closed esperado. StopDevice e RemoveDevice retornaram zero. Antes de outro
pacote ou binding, remover `oem16.inf` em Modo de Seguranca com
`C:\BC250_RECOVERY.cmd` e confirmar `BasicDisplay`, Codigo 0.

**NAO VALIDADO EM HARDWARE**
