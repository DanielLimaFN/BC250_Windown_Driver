# Captura manual da telemetria do ensaio 3.30

**Procedência — leia antes de usar.** Este arquivo **não** é saída do coletor
canônico. O `confirm-bc250mcdm-lab.ps1` nunca foi executado neste ciclo: o
watchdog expirou e fez rollback automático às `2026-08-30T10:23:42-03:00`
(ver `watchdog-state.json`, estado `ROLLED_BACK`), removendo o serviço
`bc250mcdm` e, com ele, a chave de telemetria.

Os valores abaixo foram lidos **ao vivo** de
`HKLM:\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters\Probe`
via `Get-ItemProperty`, enquanto o driver ainda estava vinculado
(`bc250mcdm / Código 0`), entre o binding e o rollback. São transcrição direta
da leitura, sem interpretação e sem qualquer valor inferido.

Nenhum dump `probe-registry.json` / `probe-registry.txt` existe para este
ciclo. Se precisar da captura canônica, o ensaio tem de ser refeito.

## Valores lidos

```text
SchemaVersion                            0x00000065   (101)
HqdActiveStatus                          0x00000005   (BC250_HQD_ACTIVE_WRITE_FAILED)
HqdActiveWptrPollCntlBefore              0x00000000
HqdActiveWptrPollCntlApplied             0x80000000
HqdActiveWptrPollCntlReadback            0x00000000
HqdActiveWptrPollCntl1Before             0x00000000
HqdActiveWptrPollCntl1Applied            0x00000000
HqdActiveWptrPollCntl1Readback           0x00000000
HqdActiveQueueMaskBit                    0x00000000
HqdActivePreActiveWptrApplied            0x00000000
HqdActivePreActiveWptrReadback           0x00000000
HqdActiveSelectedSelector                0x00000008
HqdActiveRlcCpSchedulersBefore           0x58504840
HqdActiveEopRptrBefore                   0x40000000
HqdActiveEopRptrApplied                  0x00000000
HqdActiveRestorePassed                   0x00000001
HqdActiveFirstRestoreFailureStage        0x00000000   (NONE)
HqdActiveRestoreFailureCount             0x00000000
HqdActivePqControlObserved               0xD030890D
```

Consultados e **ausentes** da chave (nomes de probe não publicados nesta
versão): `HqdActiveReadCalls`, `HqdActiveOriginalSelector`,
`HqdActiveActiveBefore`.

## Evidência canônica que existe para este ciclo

- `watchdog-state.json` — `ROLLED_BACK`, `exit_code 0`, `hardware_validated false`.
- `watchdog-rollback.txt` — remoção do `oem19.inf` e retorno a BasicDisplay.
- `docs/evidence/2026-08-30_10-20-57-DESKTOP-HQAG6T4-shader-f8b/shader-dispatch-acceptance.json`
  — veredito fail-closed do aceite, produzido pelo script enquanto o driver
  estava vinculado.

## Leitura

A transação abortou fail-closed em `WRITE_FAILED` na escrita de
`CP_PQ_WPTR_POLL_CNTL.EN`, que não foi retido (`applied=0x80000000`,
`readback=0x00000000`). Todos os passos posteriores ficam em zero de forma
coerente com o abort. O rollback interno passou íntegro.

Análise completa em [`docs/106_RESULTADO_3.30_E_CANDIDATO_3.31.md`](../../106_RESULTADO_3.30_E_CANDIDATO_3.31.md).

**NÃO VALIDADO EM HARDWARE.**
