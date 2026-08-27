# Gate do pacote MCDM readonly

> Situacao atual em 2026-08-25: o gate historico abaixo evoluiu ate o segundo
> binding. A versao 0.2.1.0 provou `DriverEntry`, mas falhou antes de AddDevice
> porque `DxgkDdiDispatchIoRequest` estava nulo. O pacote 0.2.2.0 corrige esse
> campo, passou a validacao offline e ainda nao foi instalado. `oem6.inf` esta
> pendente com codigo 14 e deve ser removido em Modo de Seguranca primeiro.

## Decisao de arquitetura

O primeiro driver instalavel novo e um adaptador compute-only MCDM, nao um
KMDOD. O KMDOD usa `DxgkInitializeDisplayOnlyDriver` e resolve exibicao, mas
nao materializa o caminho de compute pretendido. MCDM usa `DxgkInitialize`, a
classe PnP `ComputeAccelerator` e anuncia um dispositivo render-only.

O contrato oficial exige um conjunto amplo de DDIs de dispositivo, memoria,
contexto, scheduler, reset e telemetria. Nao basta trocar o retorno de
`DriverEntry` por sucesso ou preencher apenas Add/Start/Stop/Remove.

## Artefatos criados

- `driver\wdk\package\bc250readonly.inf`: INF MCDM para
  `PCI\VEN_1002&DEV_13FE`, servico exclusivo `bc250readonly`.
- `tools\build-bc250readonly-package.ps1`: audita a referencia do objeto a
  `DxgkInitialize`, o link por `displib.lib`, recuperacao e certificado; depois
  gera o catalogo e assina o pacote.
- `tools\stage-bc250readonly-package.ps1`: adiciona o INF ao Driver Store sem
  usar `/install`, extrai o nome publicado `oemNN.inf` e grava
  `C:\BC250_TEST_OEMINF.txt` antes de qualquer futura troca de binding.

## Resultado atual

Atualizacao de 2026-08-25: a afirmacao historica abaixo nao representa mais o
host atual. `C:\BC250_RECOVERY.cmd` diverge da fonte, nao cobre `bc250mcdm`, o
servico `bc250mcdm` esta presente e `oem18.inf` foi detectado pela identidade
estrita. O builder ativo permanece corretamente bloqueado. Consulte
`docs\21_AUDITORIA_RECOVERY_MCDM_2026-08-25.md`.

Em 2026-08-24:

- `C:\BC250_RECOVERY.cmd /DRY-RUN`: exit 0, zero alteracoes;
- copia em `C:\` identica a `tools\BC250_RECOVERY.cmd`;
- INF: `Inf2Cat /os:10_X64`, zero erros e zero avisos;
- callbacks MCDM minimos: presentes com assinaturas aceitas por `/W4 /WX`;
- DDIs ainda sem implementacao funcional: retornam `STATUS_NOT_SUPPORTED`;
- `DriverEntry`: liga `DxgkInitialize` somente na imagem kernel; o teste
  user-mode permanece isolado do Dxgkrnl;
- pacote: gerado, `Inf2Cat` sem erros/avisos e `.sys`/`.cat` assinados pelo
  certificado de desenvolvimento esperado;
- nenhum INF foi pre-estagiado;
- nenhum binding PnP foi alterado;
- `bc250dbg` permaneceu em execucao.

## Proximo gate

Pre-estagiar o pacote com `tools\stage-bc250readonly-package.ps1 -Execute` em
PowerShell elevado. Essa operacao nao usa `/install`; ela deve gravar o nome
publicado em `C:\BC250_TEST_OEMINF.txt`. Depois, criar o ensaio separado de
ativacao com captura do registro PnP/Dxgkrnl e rollback pelo Modo de Seguranca.
Pre-estagiar e ativar permanecem duas operacoes separadas.

### Incidente do marcador no primeiro pre-estagio

O primeiro pre-estagio adicionou corretamente `oem5.inf`, mas a versao inicial
do script gravou apenas `o` no marcador: uma colecao PowerShell com um unico
resultado foi reduzida a string e o indice `[0]` selecionou um caractere. O
script agora força array, exige cardinalidade 1 e rele o arquivo apos gravar.
O gravador tambem passou a terminar a unica linha com `CRLF`: o `findstr /X`
desta instalacao do Windows nao reconheceu o arquivo anterior sem terminador de
linha, embora seus oito bytes fossem exatamente `oem5.inf`.

Antes de qualquer ativacao, executar em PowerShell elevado:

```powershell
powershell -ExecutionPolicy Bypass -File tools\repair-bc250-oem-marker.ps1 -PublishedName oem5.inf
```

O reparador valida o INF publicado por classe, GUID, hardware ID, servico e
binario antes de escrever. Enquanto o marcador nao for exatamente `oem5.inf`,
a ativacao permanece bloqueada.

O marcador foi posteriormente reparado e reconhecido pelo recuperador. Antes da
ativacao, `oem5.inf` deve ser substituido pela versao 0.2.0.0 com telemetria
persistente usando `tools\refresh-bc250readonly-staged-package.ps1 -Execute`.
Consulte `docs\14_TELEMETRIA_E_REFRESH_MCDM.md`.

**NAO VALIDADO EM HARDWARE**
