# Binding fail-closed MCDM 1.0.1.0

Data: 2026-08-25.

## Veredito

**BINDING FAIL-CLOSED APROVADO. PM4/COMPUTE NAO EXECUTADOS. NAO VALIDADO PARA ACELERACAO.**

O usuario autorizou explicitamente o risco de tela preta. O harness elevado
chamou `UpdateDriverForPlugAndPlayDevicesW` com `INSTALLFLAG_FORCE`; a API
retornou sucesso, erro Win32 zero e nenhum reboot requerido.

O dispositivo selecionou `oem19.inf`, servico `bc250mcdm`, e terminou em
`ConfigManagerErrorCode=43` (`CM_PROB_FAILED_POST_START`). Esse e o resultado
esperado enquanto `StartDevice` permanece fail-closed.

## Evidencia fisica readonly

Telemetria persistida pelo driver:

```text
DriverEntryReached=1
InitializationDataReady=1
AddDeviceEntered=1
AddDeviceStatus=0
StartDeviceEntered=1
InterfaceAccepted=1
DeviceInfoStatus=0
PciStatus=0
PciBytesRead=64
BarCount=3
ResolvedBarCount=3
MmioStatus=0
TopologyStatus=0
TopologyReadCount=8
TopologyStablePasses=2
VramTotalMiB=512
CpuVisibleMiB=256
RebarSelectedMiB=256
RebarAssignedMiB=256
DiscoveryTmrStatus=5
DiscoveryTmrBytes=0
RuntimeMissing=1
StopDeviceEntered=1
RemoveDeviceEntered=1
```

`DiscoveryTmrStatus=5` corresponde a `BC250_DISCOVERY_TMR_NOT_REPORTED`.
Sem TMR descoberto e sem runtime de firmware owned, o driver recusou a ativacao.

## Limites da prova

O ensaio prova entrada real no miniport, callbacks PnP/Dxgkrnl, inventario PCI,
resolucao de recursos, leituras MMIO readonly de topologia e unwind fail-closed.
Nao prova ownership, firmware, PSP, ring, PM4, GPUVA, compute, Vulkan ou LLM.

Auditor reproduzivel:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-failclosed-binding.ps1
```

Evidencia bruta:

`docs\evidence\2026-08-25_18-58-52-DESKTOP-HQAG6T4-mcdm-1.0.1.0-failclosed-binding`

## Estado de recuperacao

O host permanece deliberadamente em `bc250mcdm`, Codigo 43, ate o recovery.
Para encerrar a Fase 0: iniciar Modo de Seguranca com Prompt de Comando, executar
`C:\BC250_RECOVERY.cmd`, reiniciar e repetir os gates de host limpo.
