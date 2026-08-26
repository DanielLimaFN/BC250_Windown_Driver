# Inventario runtime parcial MCDM 1.2.0.0

Data: 2026-08-25.

## Veredito

**INVENTARIO PARCIAL FISICO APROVADO. MEMORIA LIVRE NAO ANUNCIADA.**

O pacote 1.2.0.0 foi instalado pelo ciclo direto, publicado como `oem19.inf` e
terminou em Codigo 43 por `RuntimeMissing=1`, mantendo o miniport fail-closed.

## Evidencia

```text
SchemaVersion                         9
RuntimeInventoryStatus               1 (INCOMPLETE)
RuntimeInventoryEvidenceFlags        0x3
  SCANOUT_PROBED                      presente
  DISCOVERY_TMR_PROBED                presente
  PLATFORM_CARVEOUTS_PROBED           ausente
RuntimeInventoryReservationCount     1
RuntimeInventoryReservedBytes        11059200
RuntimeInventoryComplete             0
```

A unica reserva materializada e o framebuffer herdado em offset zero. O
Discovery TMR foi sondado e retornou `NOT_REPORTED`; nenhuma reserva foi
fabricada para ele. Como os carveouts da plataforma ainda nao foram sondados,
o contrato mantem `TotalFreeBytes=0`, `CpuVisibleFreeBytes=0`, ownership flags
zero e staging flags zero.

## Correcao de seguranca

Antes deste pacote, o contrato offline podia retornar `INVENTORY_COMPLETE`
mesmo com um subconjunto de fontes. Agora:

- cada reserva presente exige o respectivo flag de evidencia;
- flags desconhecidos sao rejeitados;
- somente `0x7` permite o estado completo;
- relatorio parcial preserva reservas conhecidas, mas nao anuncia faixa livre;
- testes cobrem a impossibilidade de promover o caso `0x3`.

## Identidade instalada

```text
DriverVer   08/25/2026,1.2.0.0
SYS SHA256  B4638157DF9D5D53126E3D611940D8549A250F34BA344C6FD906143390B491FD
```

Evidencia bruta:

`docs\evidence\2026-08-25_19-37-53-DESKTOP-HQAG6T4-mcdm-replace`

Nenhum segmento VidMm, GPUVA, firmware, PSP, ring, PM4 ou compute foi ativado.

**NAO VALIDADO EM HARDWARE**
