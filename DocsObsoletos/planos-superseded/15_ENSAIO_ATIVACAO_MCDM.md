# Ensaio de ativacao do probe MCDM

## Resultado do oitavo binding forcado 0.5.0.0 (22:43)

**Correcao posterior:** a auditoria mostrou que a DDI acrescentava por conta
propria os bits `EXCLUSIVE_ADAPTER` e `INVENTORY_COMPLETE`. Portanto
`OwnershipFlags=0x7F` e `ReadonlyGatePassed=1` nao comprovam ownership fisico,
inventario integral, faixa livre ou exclusao de scanout. Continuam validas as
leituras fisicas de PCI/recursos/MMIO/topologia/ReBAR. O codigo atual para em
`RuntimeMissing=1` quando nao existe produtor externo dessas evidencias.

O caminho registrou schema 5 e 59 campos de telemetria.
PCI/recursos/MMIO/topologia/ReBAR passaram fisicamente; ownership e os contratos
seguintes apenas aceitaram flags internos, hoje reclassificados. O manifesto
registrou PFP 263168, ME 263168, CE 263040, MEC 267440 e
MEC_JT1 896 bytes. O plano candidato calculou TMR em `0x1FC00000` e staging em
`0x0FFBE000`.

Nao houve operacao de firmware: `FirmwareBytesStaged=0` e
`PspTransportCalls=0`. `ReadonlyGatePassed=1` significa apenas que o plano foi
construido. StartDevice retornou `STATUS_NOT_SUPPORTED`, o dispositivo ficou em
Codigo 43 e Stop/Remove terminaram em zero. Antes do proximo ensaio, recuperar
`oem16.inf` em Modo de Seguranca.

### Recovery posterior

O recovery atualizado removeu servico, binario e todos os pacotes readonly
identificados. Depois do reboot, CIM e PnPUtil confirmaram `BasicDisplay`,
Codigo 0, somente `display.inf` e nenhum reboot pendente. Assim, a maquina nao
permanece no Codigo 43 desta evidencia.

## Resultado do setimo binding forcado 0.4.0.0 (01:22)

ReBAR passou no hardware: mascara `0x700`, encoding 8 e 256 MiB
selecionados/atribuidos. Os status de ownership e `OwnershipFlags=0x7F` nao sao
prova fisica porque os dois bits finais eram autoatribuidos pela DDI. O gate
intencional `RuntimeMissing=1` foi alcancado e
Stop/Remove terminaram em zero. O Codigo 43 corresponde ao retorno
`STATUS_NOT_SUPPORTED`; nao e prova de compute.

O pacote offline 0.5.0.0 remove esse gate e constroi apenas o preflight
candidato com o manifesto CP auditado. Ele nao copia blob, nao chama PSP e nao
reserva fisicamente VRAM. Antes de testa-lo, recuperar `oem16.inf` em Modo de
Seguranca.

## Resultado do sexto binding forcado 0.3.0.0 (01:05)

O inventario MMIO readonly passou: `MmioStatus=0`, leitor e contrato de
topologia em zero, oito leituras em duas passagens, VRAM 512 MiB, janela CPU
256 MiB, FB `0xF400..0xF41F` e offset `0x450`. O unmap terminou antes de
`RuntimeMissing=1`. O pacote offline 0.4.0.0 sela ownership e audita ReBAR.

## Resultado do quinto binding forcado 0.2.4.0 (00:56)

As tres BARs PCI foram resolvidas contra `CM_RESOURCE_LIST` sem ambiguidade:
`CmStatus=0`, `ResourceStatus=0`, `ResolvedBarCount=3`. O probe parou depois em
`RuntimeMissing=1`, com teardown limpo. O pacote offline 0.3.0.0 usa esse
ownership PnP para o primeiro inventario MMIO estritamente readonly.

## Resultado do quarto binding forcado 0.2.3.0 (00:30)

O parser PCI passou com `PciStatus=0`, 64 bytes e tres BARs de memoria. A
telemetria chegou ao gate proposital `RuntimeMissing=1`. Portanto o codigo PnP
43 desta rodada corresponde ao `STATUS_NOT_SUPPORTED` fail-closed, seguido por
Stop/Remove com status zero. A versao offline 0.2.4.0 acrescenta a resolucao
readonly das BARs contra `CM_RESOURCE_LIST` antes desse gate e nao mapeia MMIO.

## Resultado do terceiro binding forcado 0.2.2.0 (00:20)

O registro MCDM passou: `DxgkInitializeStatus=0`, AddDevice entrou e retornou
sucesso, e StartDevice recebeu uma interface valida. `DxgkCbGetDeviceInformation`
tambem retornou sucesso. A leitura de PCI config readonly retornou os 64 bytes,
mas o parser encerrou com `PciStatus=8` (`io_bar`) e zero BARs de memoria porque
considerava fatal a BAR de I/O legada do adaptador VGA. Stop/Remove completaram
com status zero. O codigo PnP final foi 43, sem reboot pendente.

O pacote offline 0.2.3.0 passa a ignorar BARs de I/O e continua varrendo as BARs
de memoria, preservando os indices originais. Antes do novo ensaio, remover
0.2.2.0/`oem10.inf` em Modo de Seguranca.

## Resultado do segundo binding forcado 0.2.1.0 (23:51)

O `DriverEntry` foi comprovadamente executado e a imagem instalada foi
confirmada por hash como 0.2.1.0. Mesmo anunciando WDDM 2.7 (`0xC004`),
`DxgkInitialize` repetiu `0xC0000059`; AddDevice e StartDevice nao foram
chamados. Assim, a hipotese anterior de incompatibilidade WDDM foi revogada.

A desmontagem de `dxgkrnl!DpiInitializeInternal` da build local mostrou que a
mesma rotina retorna `STATUS_REVISION_MISMATCH` quando qualquer callback base
obrigatorio esta nulo. A tabela do probe tinha `DxgkDdiDispatchIoRequest` nulo
no offset `0x28`. O pacote 0.2.2.0 preenche o campo com um stub fail-closed
(`STATUS_NOT_SUPPORTED`) e passou testes offline, Inf2Cat e assinatura.

Antes de testa-lo, remover `oem6.inf` com `C:\BC250_RECOVERY.cmd` em Modo de
Seguranca e reiniciar. Nao usar o reboot pendente do pacote falho como tentativa
de correcao.

## Resultado da primeira tentativa (22:23)

O ensaio com `pnputil /install` nao vinculou o miniport. O SetupAPI selecionou
`display.inf` (`Rank 00FB2008`, Inbox) e marcou `oem5.inf` como `Outranked`
(`Rank 00FF2001`, Authenticode). O retorno 259 e `ERROR_NO_MORE_ITEMS`: havia
match, mas o pacote nao era melhor e a operacao nao pediu force.

A BC-250 permaneceu saudavel em `BasicDisplay`, o servico `bc250readonly` nao
foi criado e a telemetria ficou ausente. Portanto nenhum callback do miniport
foi executado. Analise completa na pasta de evidencia
`2026-08-24_22-23-42-DESKTOP-HQAG6T4-mcdm-activation`.

## Escopo

Este ensaio solicita o primeiro binding PnP real do `bc250readonly` na BC-250.
Ele nao testa compute. O comportamento esperado do probe atual e:

1. `DriverEntry` chama `DxgkInitialize`;
2. Dxgkrnl pode chamar AddDevice e StartDevice;
3. StartDevice faz inventario PnP/PCI/MMIO/ReBAR readonly;
4. a versao 0.5.0.0 constroi um plano candidato de TMR/firmware, sem gravar
   bytes e sem chamar o PSP;
5. retorna `STATUS_NOT_SUPPORTED` e mantem a telemetria para analise.

## Risco e recuperacao

A BC-250 e o adaptador de video atual. O binding pode apagar a tela e impedir o
retorno ao desktop. Nao existe rollback ao vivo: uma remocao ao vivo do driver
legado ja causou bugcheck e esse risco nao sera repetido por conveniencia.

Se a tela ficar preta:

1. iniciar Modo de Seguranca com Prompt de Comando;
2. executar `C:\BC250_RECOVERY.cmd`;
3. reiniciar o Windows.

## Execucao

Em PowerShell elevado:

```powershell
powershell -ExecutionPolicy Bypass -File tools\activate-bc250readonly-probe.ps1 -ForceBinding
```

Esse comando e somente o preflight e faz zero escrita. Para o primeiro binding
real, depois de conferir novamente a recuperacao:

```powershell
powershell -ExecutionPolicy Bypass -File tools\activate-bc250readonly-probe.ps1 -ForceBinding -Execute -IAcceptBlackScreenRisk
```

O harness valida pacote, assinatura, cardinalidade no Driver Store, recovery,
BasicDisplay saudavel e ausencia dos servicos conflitantes. Depois:

- cria `docs\evidence\<data>-<host>-mcdm-force-activation`;
- para `bc250dbg` se ele estiver rodando;
- inicia um coletor PowerShell oculto;
- chama `UpdateDriverForPlugAndPlayDevicesW` com `INSTALLFLAG_FORCE`;
- captura PnP, SetupAPI, eventos, servicos e a chave `Parameters\Probe`.

O modo antigo sem `-ForceBinding` continua apenas como diagnostico de ranking;
ele interpreta o retorno 259 explicitamente e nao deve ser repetido esperando
uma troca de driver.

O coletor foi exercitado separadamente em 2026-08-24 e produziu todos os
artefatos esperados sem ativar ou reiniciar dispositivo.

## Criterio de leitura

Telemetria de callbacks, enumeração PnP ou retorno de `DxgkInitialize` nao prova
aceleracao. Mesmo que todos aparecam, compute continua nao validado enquanto
nao houver ring fisico, RPTR/WPTR, fence escrito pela GPU, resultado correto e
ausencia de fallback.

**NAO VALIDADO EM HARDWARE**
