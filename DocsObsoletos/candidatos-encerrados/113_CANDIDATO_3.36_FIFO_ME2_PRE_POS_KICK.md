# Candidato 3.36 — FIFO ME2 pré/pós e snapshots do kick

Data local: 30/08/2026
Pacote preparado: **3.36.0.0 / SchemaVersion 107**
Veredito: **NÃO VALIDADO EM HARDWARE**

Continuação de [`112_RESULTADO_FISICO_3.35.md`](112_RESULTADO_FISICO_3.35.md).

## Única variável

Observabilidade adicional, sem alterar o PM4, MQD, GCVM ou qualquer escrita
da sequência de ativação:

1. quatro leituras de `CP_MEC_ME2_HEADER_DUMP` imediatamente antes de
   `CP_HQD_ACTIVE=1`;
2. quatro leituras do mesmo FIFO depois do polling, já existentes no 3.35;
3. snapshots globais CPC/CP imediatamente antes da doorbell BAR2;
4. snapshots globais CPC/CP imediatamente depois da barreira da doorbell.

Todas as leituras novas são best-effort. Nenhuma participa de condição de
abort, conforme a regra estabelecida no handoff 110.

## Campos novos

```text
HqdActiveMecMe2BeforeActive0..3
HqdActiveCpcStatusBeforeKick
HqdActiveCpcBusyStatBeforeKick
HqdActiveCpcStalledStat1BeforeKick
HqdActiveCpStatBeforeKick
HqdActiveCpStalledStat1BeforeKick
HqdActiveCpcStatusAfterKick
HqdActiveCpcBusyStatAfterKick
HqdActiveCpcStalledStat1AfterKick
HqdActiveCpStatAfterKick
HqdActiveCpStalledStat1AfterKick
```

## Validação offline

- `driver\wdk\build-contract.bat`: `EXIT=0`.
- Código de kernel sob `/W4 /WX`: sem warnings.
- Parser dos cinco scripts com Schema literal: `PASS`.
- Pacote LAB, matriz DDI e safety audit: `PASS`.
- Strings novas encontradas em UTF-16 no SYS assinado.

## Artefatos

```text
DriverVer           08/30/2026,3.36.0.0
Schema              107
recipe cp/vmid0/sdma 1 / 0 / 0
Package SYS SHA-256 0121259E4CD91D14B0CBD3B6571ED9FD3B7AD70D0EBEF371C1F8620F079ADFB8
Package INF SHA-256 CF00A6CF424FE7350A64CAA4AB5711E40359B76C0D13BA8EA0B4850983215E5C
Package CAT SHA-256 298859474C264A10BC56FA87689439CD89BCB349A8E8E8B40B8A965F30C8E91F
```

## Próximo ensaio

O boot atual já recebeu a submissão 3.35 e não pode ser reutilizado.

1. Reboot manual obrigatório.
2. Quarentena do resíduo exato.
3. Prebinding `-LabPackage` e exigir `PASS`.
4. Bind 3.36 com ShaderDispatch.
5. Capturar a telemetria ao bind; confirmar display somente se observável.
6. Uma única submissão física no boot.

O diferencial `MecMe2BeforeActive[0..3]` versus
`MecMe2HeaderDump[0..3]` indicará se o ciclo acrescentou headers à FIFO.

**NÃO VALIDADO EM HARDWARE.**
