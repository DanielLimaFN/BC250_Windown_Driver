# Registro da segunda tentativa física — Shader F8b

Data local: 30/08/2026  
Pacote ensaiado: 3.26 / Schema 98  
Veredito: **NÃO VALIDADO EM HARDWARE**

## Resultado executivo

O segundo ensaio atravessou o gate que bloqueou a primeira tentativa:

```text
PfpDirectStatus=0
PfpDirectPhysicalReady=1
PfpDirectRb0RptrAfter=31
PfpDirectRb0WptrApplied=31
PfpDirectRegRestore=1
PfpDirectMemRestore=1
PfpDirectFaultAfter=0
```

O plano observável foi construído com 40 dwords, `DISPATCH_DIRECT` no índice
25 e fence posterior no índice 32. A transação HQD, contudo, retornou
`HqdActiveStatus=2 (BC250_HQD_ACTIVE_PREFLIGHT_FAILED)` antes de aplicar o
estado da fila. Resultado, fence e cursores permaneceram zerados. Não houve
execução parcial promovida a sucesso.

## Máquina de estados observada

```text
[PREFLIGHT DE BOOT/PACOTE: PASS]
  -> [SNAPSHOT/PROVA PFP: PASS, rollback interno PASS]
  -> [PLANO SHADER: PASS, 40 dwords]
  -> [PREFLIGHT WGP: FAIL antes de SNAPSHOT/APPLY HQD]
  -> [PROVA SHADER: NÃO ALCANÇADA]
  -> [ROLLBACK PNP: watchdog ROLLED_BACK, BasicDisplay Código 0]
```

## Evidência canônica

- `docs/evidence/20260830-003532-DESKTOP-HQAG6T4-f8b2-preflight/residue-quarantine.json`
  registra a quarentena recuperável do SYS residual antes do ensaio.
- `docs/evidence/2026-08-30_00-37-15-DESKTOP-HQAG6T4-lab-binding/probe-full-before-watchdog.json`
  contém a telemetria completa lida antes do rollback.
- `docs/evidence/2026-08-30_00-37-15-DESKTOP-HQAG6T4-lab-binding/shader-dispatch-acceptance.json`
  registra a reprovação estrita, plano 40/25/32, receita correta e ausência da
  prova física de shader.
- `docs/evidence/2026-08-30_00-37-15-DESKTOP-HQAG6T4-lab-binding/watchdog-state.json`
  registra `state=ROLLED_BACK`, `exit_code=0`,
  `detail="BasicDisplay restoration completed"` e
  `hardware_validated=false`.

## Causa raiz confirmada

Os offsets catalogados do preflight eram:

```text
CC_GC_SHADER_ARRAY_CONFIG       0x000089BC
SPI_PG_ENABLE_STATIC_WGP_MASK  0x0000935C
RLC_PG_ALWAYS_ON_WGP_MASK      0x0003B14C
```

Eles estavam presentes em `bc250_compute_mqd_contract.h` e eram solicitados
duas vezes pelo preflight, mas não estavam em
`Bc250MmioTopologyOffsetAllowed`. Assim,
`Bc250MmioTopologyRead32` gravava zero no destino e retornava falha na primeira
leitura; a expressão de curto-circuito abortava antes de calcular a máscara
não-harvestada. Isso explica conjuntamente:

```text
ShaderCcArrayPass1/2=0
ShaderSpiWgpMaskPass1/2=0
ShaderRlcAonWgpPass1/2=0
ShaderNonHarvestedWgpMask=0
ShaderWgpPreflightPassed=0
HqdActiveStatus=2
```

Logo, esses zeros não constituem uma leitura física dos três registradores.
Foram saídas fail-closed do transporte recusando offsets ainda não
allowlisted.

## Correção offline e candidato 3.27

Os três offsets foram adicionados somente a
`Bc250MmioTopologyOffsetAllowed`. Nenhum deles foi adicionado à allowlist de
escrita. O teste de transporte agora exige simultaneamente:

1. leitura bem-sucedida dos três valores simulados catalogados;
2. recusa das três tentativas de escrita;
3. `WriteCount` inalterado após as recusas.

Resultados offline:

```text
build-contract.bat:     PASS
MMIO transport test:    PASS (read allowed / write denied)
Inf2Cat:                Errors=None, Warnings=None
Lab package audit:      PASS
DriverVer:              08/30/2026,3.27.0.0
Source SYS SHA-256:      E3D9B053E248B83BFC5A71DAB81CEB2163C7B687E875136B56E229A1CDCDB19B
Package SYS SHA-256:     11D0E7DF08E07FD40DCB13A37D6D4FFD266F2B4ED920CEC3130C5C7B738DECEB
Package INF SHA-256:     B65A6A02B464CA4CA88D46DF1C16722C86CDAE41897EB0C9E6997A9CDEC88E8F
Package CAT SHA-256:     981CC93716E4246A195C17EDA76EA96038587CCF93EA51CD07925C8132479CB5
```

O candidato 3.27 não foi vinculado nem executado no hardware. A próxima
tentativa continua condicionada a novo boot limpo, autorização explícita,
watchdog e aceite integral do buffer envenenado, fence posterior,
`RPTR == WPTR`, comparação byte a byte e rollback.
