# Analise do segundo binding forcado MCDM

## Resultado comprovado

A imagem 0.2.1.0 instalada, o arquivo no Driver Store e o pacote de origem foram
confirmados como identicos por SHA-256. A telemetria persistente registrou:

| Campo | Valor |
|---|---:|
| `DriverEntryReached` | `1` |
| `InitializationDataReady` | `1` |
| `DxgkInitializeStatus` | `0xC0000059` |

AddDevice e StartDevice nao foram alcancados. O codigo PnP 14 representa o
reboot pendente da troca e nao valida o driver. Nao houve compute, submissao,
firmware ou execucao comprovada no silicio.

## Causa isolada

A imagem efetivamente gravava `DRIVER_INITIALIZATION_DATA.Version=0xC004`, e a
implementacao local de `displib.lib` aceita esse valor. Isso revoga a hipotese
de incompatibilidade WDDM feita apos o primeiro binding.

Com os simbolos exatos do `dxgkrnl.sys` 10.0.19041.2913, a desmontagem de
`dxgkrnl!DpiInitializeInternal` mostrou que `0xC0000059` tambem e o retorno para
qualquer callback base obrigatorio ausente. Para `DRIVER_INITIALIZATION_DATA`,
os offsets validados incluem:

| Offset | Callback |
|---:|---|
| `0x08` | AddDevice |
| `0x10` | StartDevice |
| `0x18` | StopDevice |
| `0x20` | RemoveDevice |
| `0x28` | `DxgkDdiDispatchIoRequest` |
| `0x30` | InterruptRoutine |
| `0x38` | DpcRoutine |
| `0x40` | QueryChildRelations |
| `0x48` | QueryChildStatus, ou alternativa de connection change |
| `0x50` | QueryDeviceDescriptor |
| `0x58` | SetPowerState |
| `0x68` | ResetDevice |
| `0x70` | Unload |

Todos estavam preenchidos, exceto `DxgkDdiDispatchIoRequest` no offset `0x28`.
Esse e o primeiro gate deterministico que explica exatamente o status observado.

## Correcao offline 0.2.2.0

O callback ausente foi ligado a um stub fail-closed que retorna
`STATUS_NOT_SUPPORTED`. O teste de contrato agora exige o ponteiro e exercita o
retorno. O build passou `/W4 /WX`, todos os testes, link MCDM, Inf2Cat sem
erros/avisos e assinatura valida do `.sys` e `.cat`.

O pacote 0.2.2.0 nao foi instalado nem executado. A maquina permanece pendente
em `oem6.inf`, codigo 14, e deve ser recuperada em Modo de Seguranca antes do
proximo ensaio.

Hashes do pacote offline 0.2.2.0 gerado depois desta analise:

- `bc250readonly.sys`: `DFCED4F4EDEF4BAD25243B143B4C8B111605823FF4F2814F9157707F24C20B93`;
- `bc250readonly.cat`: `23BBE0CDDE76482EB0DF0503B3800AC9CE1D03AC65F001D30F686AC19C6F228E`.

**NAO VALIDADO EM HARDWARE**
