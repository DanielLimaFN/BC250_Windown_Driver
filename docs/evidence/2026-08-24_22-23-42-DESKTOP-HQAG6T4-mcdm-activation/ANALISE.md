# Analise do ensaio de ativacao MCDM das 22:23

## Resultado

O pacote `oem5.inf` foi encontrado e considerado para a BC-250, mas nao foi
selecionado. O dispositivo permaneceu no Microsoft Basic Display Adapter:

- servico ativo: `BasicDisplay`;
- INF ativo: `display.inf`;
- `ConfigManagerErrorCode=0`;
- servico `bc250readonly` ausente;
- chave de telemetria `Parameters\Probe` ausente;
- `bc250dbg` parado pelo harness.

Logo, `DriverEntry`, AddDevice e StartDevice do miniport experimental nao foram
executados. A permanencia do desktop nao valida o probe; o binding nem chegou a
acontecer.

## Decisao registrada pelo SetupAPI

O trecho iniciado as 22:23:42 registra dois candidatos:

| Candidato | Estado | Classe | Rank | Assinatura |
|---|---|---|---:|---|
| `display.inf` | `Selected / Installed` | Display | `00FB2008` | Inbox (`0D000003`) |
| `oem5.inf` | `Outranked` | ComputeAccelerator | `00FF2001` | Authenticode (`0F000000`) |

Em seguida, o SetupAPI registra `No better matching drivers found`, nenhum
dispositivo atualizado e encerra com `0x00000103` (259,
`ERROR_NO_MORE_ITEMS`). Esse codigo nao veio do miniport: significa que havia
um dispositivo correspondente, mas o pacote fornecido nao superava o driver
atual sem uma solicitacao forcada.

## Consequencia operacional

Repetir `pnputil /add-driver ... /install` nao pode mudar esse resultado: a
documentacao do PnPUtil diz expressamente que ele nao forca um pacote que nao
seja o mais bem ranqueado. O proximo ensaio deve usar
`UpdateDriverForPlugAndPlayDevicesW` com `INSTALLFLAG_FORCE`, a API documentada
para instalar o pacote correspondente mesmo quando ja existe um driver melhor.

O harness foi alterado para exigir simultaneamente `-ForceBinding`, `-Execute`
e `-IAcceptBlackScreenRisk`. A chamada nao foi executada durante esta analise.

Referencias oficiais:

- https://learn.microsoft.com/windows-hardware/drivers/devtest/pnputil-command-syntax
- https://learn.microsoft.com/windows/win32/api/newdev/nf-newdev-updatedriverforplugandplaydevicesw
- https://learn.microsoft.com/windows-hardware/drivers/install/how-windows-ranks-driver-packages

**NAO VALIDADO EM HARDWARE**
