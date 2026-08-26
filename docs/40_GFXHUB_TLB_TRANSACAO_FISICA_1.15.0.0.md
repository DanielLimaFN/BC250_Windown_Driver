# Transacao fisica GFXHUB TLB - pacote 1.15.0.0

Data: 2026-08-26.

Status: **GATE TLB FISICO APROVADO; ACELERACAO AINDA NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   72%
Miniport MCDM funcional            43%
Memoria/GPUVA WDDM                 25%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

## Escopo da escrita

Durante o segundo preflight aprovado, depois da transacao VRAM reversivel, o
miniport remapeia o BAR5 derivado dos recursos PnP e executa uma unica
invalidacao para VMID 8 na engine GFXHUB 17.

Allowlist de write:

- `0xA3A4`: range low da engine 17;
- `0xA3A8`: range high da engine 17;
- `0xA2D0`: request da engine 17.

O ACK `0xA318` permanece somente leitura. Nao existem writes PSP, firmware,
ring, WPTR, PM4 ou MEC neste pacote.

## Protocolo

1. validar integralmente o plano GC 10.1.3;
2. capturar range low/high e ACK anterior;
3. rejeitar ACK `0xFFFFFFFF` antes de qualquer request;
4. programar range completo e conferir readback;
5. escrever request `0x00F80100` para VMID 8, flush type 0;
6. ler `REQ` imediatamente, satisfazendo o dummy read obrigatorio;
7. consultar ACK por no maximo 1000 iteracoes de 1 us;
8. rejeitar ACK `0xFFFFFFFF` e exigir o bit `0x100`;
9. restaurar range low/high e conferir readback, inclusive em timeout.

`TlbFlushPhysicalReady=1` so e registrado quando request, dummy read, ACK e
restauracao forem todos comprovados. Falha de qualquer etapa preserva os
valores crus e mantem o driver em Code 43.

## Telemetria schema 23

- status, VMID, engine e request;
- ranges antes e programados;
- valor lido no dummy read;
- ACK antes/depois e quantidade de polls;
- flags de request escrito, dummy read, ACK observado e range restaurado;
- readiness fisica derivada da transacao.

A auditoria posterior ao binding exige status 0, VMID 8, engine 17, request
correto, range correto, ACK decodificado com bit `0x100`, 1..1000 polls e
restauracao confirmada.

## Build e integridade

- build completo e testes de sucesso, timeout e all-ones: PASS;
- auditoria de allowlist MMIO e fail-closed: PASS;
- source SYS: `EF95400E7497093006A7D7F4539BBDECCF6326F71B7A980CD711D7EC35745B47`;
- signed SYS: `1A8901A29B95EB09318B4B1C9A0EBC8293B2A7F10C35B8E6DD3873D458D5E8DF`;
- INF: `8A6932A1B209B00BCE34E87AE5944B9630C6936714E5FFC6EB84E2C9D8E9590A`;
- CAT: `9F0E5CCBA2C1E8DBED9061DFD3524A6EADBCBB1EB076D3E7E5E04C7EA2FCFBD3`;
- `HardwareValidated=false`.

## Resultado fisico

O ensaio `2026-08-26_08-51-40` aprovou a transacao:

- status 0, VMID 8, engine 17;
- request `0x00F80100` escrito e relido no dummy read;
- ACK antes `0x00000000`;
- ACK depois `0x00000100`, observado no primeiro poll;
- range programado `FFFFFFFF/0000001F`;
- range anterior `00000000/00000000` restaurado e conferido;
- `TlbFlushPhysicalReady=1`.

O primeiro auditor abortou depois do binding porque PowerShell interpretou o
literal `0xFFFFFFFF` como `-1`. A comparacao foi corrigida para
`[uint32]::MaxValue`; a auditoria foi reexecutada sobre o mesmo SYS e registro,
sem reinstalacao, e passou.

Evidencia:
`docs\evidence\2026-08-26_08-51-40-DESKTOP-HQAG6T4-mcdm-replace`.

Veredito: **INVALIDACAO TLB GFXHUB VALIDADA; PM4/COMPUTE NAO EXECUTADOS**.
