# Registro da primeira tentativa física — Shader F8b

Data local: 30/08/2026  
Veredito: **NÃO VALIDADO EM HARDWARE**

## Resultado executivo

A primeira tentativa não chegou ao shader. O miniport foi vinculado com
Código 0 e a cadeia PSP/SDMA permaneceu funcional, mas o carregamento PM4
direto terminou em `PfpDirectStatus=14 (PM4_NOT_CONSUMED)`. Por construção,
HQD e dispatch não foram chamados depois dessa falha. O watchdog expirou e
restaurou o dispositivo para `BasicDisplay`.

Não houve fence de shader, mudança do buffer envenenado nem prova
`RPTR == WPTR`; portanto nenhum sucesso parcial é promovido a validação.

## Máquina de estados observada

```text
[PREFLIGHT: PASS]
  -> [SNAPSHOT: PASS]
  -> [APPLY: binding Código 0]
  -> [READBACK: SYS instalado com hash exato]
  -> [PROVA NO SILÍCIO: FAIL em PM4_NOT_CONSUMED]
  -> [ROLLBACK/TEARDOWN: watchdog ROLLED_BACK, BasicDisplay restaurado]
```

O rollback de registradores e memórias do shader não foi exercitado porque o
dispatch não foi alcançado. O rollback do binding/PnP foi verificado
fisicamente pelo watchdog.

## Evidência persistida

- `docs/evidence/2026-08-30_00-18-41-DESKTOP-HQAG6T4-lab-binding/shader-dispatch-acceptance.json`
  registra Schema 98, `HqdActiveStatus=1`, ausência da telemetria de shader e
  veredito `NAO VALIDADO EM HARDWARE`.
- `docs/evidence/2026-08-30_00-18-41-DESKTOP-HQAG6T4-lab-binding/watchdog-state.json`
  registra `state=ROLLED_BACK`, `exit_code=0`,
  `detail="BasicDisplay restoration completed"` e
  `hardware_validated=false`.
- `tools/_elevated_bind_f8b.log` registra o binding, Código 0 e hash instalado
  igual ao pacote.
- `tools/_elevated_rollback_f8b.log` registra a exclusão do pacote `oem19.inf`
  e a restauração para `BasicDisplay`.

Durante a janela física, uma leitura somente de `Parameters\Probe` mostrou:

```text
PspRingStatus=0                 PspRingPhysicalReady=1
PspLoadFwStatus=0               PspLoadFwPhysicalReady=1
SdmaRingWorkStatus=0
PfpDirectStatus=14              PfpDirectPhysicalReady=0
PfpDirectPcTransitions=47       PfpDirectFaultAfter=0
HqdActiveStatus=1               (sentinela; HQD não chamado)
```

Esses valores foram observados ao vivo, mas a versão então vigente do escritor
de aceitação não os serializou no JSON. Eles são diagnóstico secundário; os
artefatos persistidos acima são a evidência canônica para a reprovação e o
rollback.

## Causa e correção

O primeiro pacote opt-in habilitava `ShaderDispatch=1`, mas ainda usava os
defaults `CpSoftReset=0` e `SdmaAutoCtxSw=1`. Isso não reproduzia a receita do
ensaio físico documentado em `docs/95_RELATORIO_ATIVACAO_HQD_2026-08-29.md`,
que obteve 40/40 passagens com:

```text
CpSoftReset=1
Vmid0PageTable=0
SdmaAutoCtxSw=0
```

O candidato de laboratório 3.26 sela os três valores no INF e no manifesto.
O binder agora falha fechado antes de qualquer alteração PnP se houver
divergência no contrato, em qualquer hash de SYS/INF/CAT/firmware/recuperação
ou no certificado de SYS/CAT.

## Candidato 3.26 verificado offline

```text
DriverVer:             08/30/2026,3.26.0.0
Telemetry Schema:      98
SkipHwBringup:         0
ShaderDispatch:        1
CpSoftReset:           1
Vmid0PageTable:        0
SdmaAutoCtxSw:         0
SYS SHA-256:            A291649A51206658C0EB6DB4538631C7AF6E74A327D1093D02D66061862D20AF
INF SHA-256:            86AE0D007645A1DFDC9BA848C65702860DA6A54A19DA0AD7570CEF6886C920FE
CAT SHA-256:            62D98679BC357C42AB0794633A3FD09385A78CBF8DDC16E047A1CD3F161AC2BC
Hashes divergentes:    0
SYS/CAT Authenticode:  Valid
Inf2Cat:                Errors=None, Warnings=None
build-contract.bat:     PASS
```

Essas verificações são somente preparação offline. O próximo gate continua
sendo uma tentativa física única em boot limpo, com watchdog armado e aceite
estrito do resultado, fence posterior, cursores e rollback.
