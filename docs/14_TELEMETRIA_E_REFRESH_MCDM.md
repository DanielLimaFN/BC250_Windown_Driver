# Telemetria persistente e refresh do pacote MCDM

> Atualizacao: 0.2.4.0 comprovou tres BARs resolvidas contra o PnP. O pacote
> atual 0.3.0.0 acrescenta o inventario MMIO readonly e usa o schema 3. A
> descricao de telemetria abaixo permanece valida para todas as versoes.

## Objetivo

A versao 0.2.0.0 do probe MCDM registra evidencias de ciclo de vida em:

```text
HKLM\SYSTEM\CurrentControlSet\Services\bc250readonly\Parameters\Probe
```

O INF cria somente essa chave exclusiva do servico. O driver grava valores
`REG_DWORD`; nao escreve MMIO, PCI config, VRAM, firmware, ring ou fence.

## Campos

- `DriverEntryReached` e `InitializationDataReady`;
- `DxgkInitializeStatus`;
- `AddDeviceEntered` e `AddDeviceStatus`;
- `StartDeviceEntered`, `InterfaceAccepted` e `DeviceInfoStatus`;
- `PciStatus`, `PciBytesRead` e `BarCount`;
- no schema 2, `CmStatus`, `ResourceStatus` e `ResolvedBarCount`;
- no schema 3, `MmioStatus`, status/contagem da topologia e valores RCC/GCMC;
- `RuntimeMissing`;
- estados de StopDevice e RemoveDevice;
- `Sequence`, `LastField` e `LastValue`.

Esses valores provam apenas passagem por callbacks e resultados de software ou
leituras readonly. Eles nao provam compute nem execucao na GPU.

## Refresh antes da ativacao

Depois de recuperar o pacote falho em Modo de Seguranca, para substituir a
versao anterior no Driver Store
sem trocar o binding, executar em PowerShell elevado:

```powershell
powershell -ExecutionPolicy Bypass -File tools\refresh-bc250readonly-staged-package.ps1 -Execute
```

O script exige BC-250 em `BasicDisplay`, codigo de erro zero, servico
`bc250readonly` ausente e identidade exata do pacote antigo. Ele remove somente
o pacote pre-estagiado, sem `/uninstall`, adiciona 0.3.0.0 sem `/install`, grava
o novo marcador e exige cardinalidade final igual a um pacote BC250 readonly.

Nao ativar o dispositivo ate o refresh e o novo `DRY-RUN` da recuperacao serem
confirmados.

**NAO VALIDADO EM HARDWARE**
