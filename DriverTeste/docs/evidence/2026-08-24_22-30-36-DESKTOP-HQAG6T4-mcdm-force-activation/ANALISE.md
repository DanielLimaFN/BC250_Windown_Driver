# Analise do primeiro binding forcado MCDM

## Resultado comprovado

O binding forçado funcionou no nivel PnP e o sistema chamou o `DriverEntry` de
`bc250readonly.sys`. A telemetria persistente registrou:

| Campo | Valor |
|---|---:|
| `DriverEntryReached` | `1` |
| `InitializationDataReady` | `1` |
| `DxgkInitializeStatus` | `0xC0000059` |

`0xC0000059` e `STATUS_REVISION_MISMATCH`. O SetupAPI fornece a classificacao
mais precisa do estado imediato: `CM_PROB_FAILED_DRIVER_ENTRY` (`0x25`) com
status `0xC0000059`. O codigo PnP observado depois, 14
(`CM_PROB_NEED_RESTART`), e o estado pendente da troca; nao transforma a falha
de `DriverEntry` em sucesso.

AddDevice e StartDevice nao foram alcancados. Nao houve inventario PCI/MMIO pelo
miniport, compute, submissao, firmware ou execucao no silicio.

## Hipotese inicial revogada em 23:51

Esta evidencia isolada levou inicialmente a atribuir `0xC0000059` a versao DDI.
O ensaio posterior de 23:51 carregou uma imagem confirmada em `0xC004` e repetiu
o mesmo status. Portanto a explicacao abaixo por versao nao e mais valida.

A desmontagem do `dxgkrnl.sys` exato da maquina mostrou a causa real: a rotina
de registro tambem retorna `STATUS_REVISION_MISMATCH` quando um callback base
obrigatorio esta nulo. O campo ausente era `DxgkDdiDispatchIoRequest`, offset
`0x28` em `DRIVER_INITIALIZATION_DATA`. Consulte a evidencia
`2026-08-24_23-51-50-DESKTOP-HQAG6T4-mcdm-force-activation`.

Esta pasta continua provando DriverEntry e a falha anterior a AddDevice, mas nao
prova a causa que foi inicialmente registrada.

**NAO VALIDADO EM HARDWARE**
