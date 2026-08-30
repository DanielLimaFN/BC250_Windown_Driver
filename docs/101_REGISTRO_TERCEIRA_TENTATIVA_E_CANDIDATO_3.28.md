# Terceira tentativa física e candidato 3.28 — PQ_BASE em GPUVA

Data local: 30/08/2026  
Pacote ensaiado: 3.27 / Schema 98  
Novo pacote offline: 3.28 / Schema 99  
Veredito: **NÃO VALIDADO EM HARDWARE**

## Resultado físico do 3.27

O terceiro ensaio atravessou o preflight WGP e aplicou a fila MEC1/HQD. A
telemetria relevante foi:

```text
WGP não-harvestado / always-on : 0x00000007 / 0x00000003
Plano PM4                     : 40 dwords
DISPATCH_DIRECT / fence       : índices 25 / 32
CP_HQD_PQ_BASE                : 0x000000F400AA0000
RPTR / WPTR                   : 40 / 40
Resultado antes/depois        : 0x43DAFFFE / 0x43DAFFFE
Fence antes/depois            : 0xA55A0FF0 / 0xA55A0FF0
Mismatch byte a byte          : 4
HqdActiveStatus               : 7 (PM4_NOT_CONSUMED)
Memória restaurada            : 1
Registradores restaurados     : 0
PhysicalReady                 : 0
```

Portanto, `RPTR == WPTR` isoladamente não foi aceito como execução. O resultado
e a fence permaneceram envenenados e o gate falhou fechado. O watchdog
restaurou `BasicDisplay` em Código 0.

Evidência canônica:

- `docs/evidence/2026-08-30_00-48-06-DESKTOP-HQAG6T4-lab-binding/probe-full-before-watchdog.json`
- `docs/evidence/2026-08-30_00-48-06-DESKTOP-HQAG6T4-lab-binding/shader-dispatch-acceptance.json`
- `docs/evidence/2026-08-30_00-48-06-DESKTOP-HQAG6T4-lab-binding/watchdog-state.json`

## Causa de endereçamento corrigida

O Contexto 0 instalado pelo ensaio mapeia GPUVAs baixas para as páginas físicas
do bloco de VRAM:

```text
GPUVA 0x2200  -> fence física
GPUVA 0x4000  -> shader físico
GPUVA 0x5000  -> resultado físico
GPUVA 0x10000 -> ring físico
```

O 3.27, porém, inicializava `Props.HqdBaseGpuAddress` com o endereço físico do
ring. Assim, `CP_HQD_PQ_BASE` recebeu `0xF400AA0000`, fora da faixa virtual
`0..2 MiB` descrita pela própria page table. O contrato de referência também
distingue explicitamente o GPUVA usado para indexar a page table do endereço
físico gravado na PTE (`referencias/AMD-BC-250-Windows-Driver/src/kmd/amdbc250_dream_kmd.c`,
linhas 6101 e 6212-6216).

O 3.28 mantém `Buffers.RingGpuAddress` como endereço físico do backing, mas
programa `CP_HQD_PQ_BASE` com a GPUVA de VMID 0 `0x10000`. Antes de qualquer
`APPLY`, o caminho observável agora exige:

1. `PTE[0x10]` com flags exatas `0xE1`;
2. tradução `GPUVA 0x10000 -> Buffers.RingGpuAddress` exata;
3. readback integral dos 64 dwords preparados no ring;
4. falha fechada se qualquer uma dessas provas divergir.

A telemetria Schema 99 publica GPUVA, físico, PTE, resultado da tradução e
mismatches do readback do ring. O aceite físico também verifica esses campos.

## Validação offline e artefato

```text
build-contract.bat             PASS
compute_mqd_contract_test      PASS
PTE deliberadamente errada     MEMORY_FAILED antes de APPLY
PowerShell acceptance parser   PASS
Inf2Cat                        Errors=None, Warnings=None
Lab package audit              PASS
DriverVer                      08/30/2026,3.28.0.0
TelemetrySchemaVersion         99
Package SYS SHA-256            63BA15E6E3F4EC044D39F46900B868B56BA25332D01C54C58DAD68F2E3D18455
Package INF SHA-256            B5A9A7811BEE183542BBBAD065ECECD54F49DEB4E0EDFA6915B66089BB09FA8B
Package CAT SHA-256            4C0FB545E984878088143BCF018481498D7E2ED99DBA528BC5828B27495F8E36
```

O pacote foi construído e auditado, mas **não foi instalado nem executado**.
O próximo gate físico permanece: shader executado nos CUs, resultado saindo do
poison, `RPTR == WPTR == 40`, fence física posterior ao dispatch, comparação
byte a byte sem mismatch e rollback integral no mesmo ciclo.
