# GCVM backing owned e VMID8 inerte - pacote 1.16.0.0

Data: 2026-08-26.

Status: **GATE FISICO APROVADO; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   76%
Miniport MCDM funcional            43%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

Os percentuais subiram somente depois da telemetria fisica aprovar o gate.

## Objetivo

O pacote 1.16 liga a page table GCVM a memoria que pertence ao allocator do
miniport. Ele ainda nao habilita traducao. O ensaio comprova separadamente:

1. duas paginas de 4 KiB sem sobreposicao, roles `PAGE_TABLE` e `TEST_BUFFER`,
   alocadas top-down dentro da janela CPU-visible;
2. PTE 0 `VALID|READ|WRITE` apontando para o buffer owned;
3. escrita e leitura integral dos 4 KiB da page table;
4. snapshot do contexto VMID8 e rejeicao se `ENABLE_CONTEXT=1`;
5. programacao inerte de PT base/start/end, com readback;
6. invalidacao GFXHUB engine 17 ja comprovada pelo pacote 1.15;
7. restauracao conferida dos seis registradores, dos 4 KiB e das duas leases.

`GpuVaDescriptorReady`, `RootPageTableAbiReady` e
`PagingCpuUpdateReady` continuam em zero. Nenhuma engine consome o endereco
virtual neste pacote.

## Registradores escritos

Somente os registradores PT do VMID8 foram adicionados a allowlist:

- base: `0xA3EC/0xA3F0`;
- start: `0xA46C/0xA470`;
- end: `0xA4EC/0xA4F0`.

`GCVM_CONTEXT8_CNTL` em `0xA220` e apenas lido. A transacao recusa o ensaio se
o bit zero estiver ligado. O request/range TLB continua limitado aos offsets
documentados no pacote 1.15; o ACK continua read-only.

## Telemetria schema 24

O auditor exige:

- `GcvmBackingStatus=0`, ownership, readback e restore iguais a 1;
- `GcvmVmid8Status=0`, readback, TLB e restore iguais a 1;
- `GcvmVmid8CntlBefore & 1 == 0`;
- PTE com flags `0x61` e endereco igual a
  `(FbOffset16M << 24) + GcvmTargetOffsetLow32`;
- `GcvmInertPhysicalReady=1`;
- todos os gates TLB do schema 23 ainda aprovados;
- todos os sinais GPUVA operacionais ainda em zero.

## Build e integridade

- build kernel `/W4 /WX`, testes e auditorias estaticas: PASS;
- unit test de programacao/readback/TLB/rollback VMID8: PASS;
- DriverVer: `08/26/2026,1.16.0.0`;
- source SYS: `35AC289BC11AAA06240932A1CDC184B4D038A3ECD20BCBC93454EF23B256FBC2`;
- signed SYS: `9FDAF6D6E86C76EB6FC9F13FA682486A9C38013FF15E4AF674F37FF803B5AD65`;
- INF: `2C7667EA852A173E28509BBFAB2475D6CC698F138FF72D6EB14A8965A91ED5C8`;
- CAT: `AB629F107DAE9B05A01720C52FC93321505A36EA7E68166BD81AD614CA2A67F4`;
- `HardwareValidated=false`.

## Proximo gate

Depois da aprovacao fisica deste candidato, o proximo pacote podera habilitar
um contexto GCVM por tempo limitado e mandar a SDMA consumir um VA. O gate so
sobe com RPTR alcancando WPTR, fence escrita pela GPU, destino integral correto,
faults ausentes e rollback completo.

## Resultado fisico

Evidencia `2026-08-26_09-33-47-DESKTOP-HQAG6T4-mcdm-replace`:

- page table offset `0x0FBBC000`, MC `0x4_5FBBC000`;
- target offset `0x0FBBD000`;
- PTE `0x4_5FBBD061`, exatamente `VALID|READ|WRITE` para o target owned;
- `GcvmBackingStatus=0`, readback e restore iguais a 1;
- contexto anterior `0x007FFE80`, portanto `ENABLE_CONTEXT=0`;
- `GcvmVmid8Status=0`, readback, TLB e restore iguais a 1;
- ACK TLB `0x00000100` no primeiro poll;
- `GcvmInertPhysicalReady=1`;
- sinais GPUVA operacionais permaneceram em zero.
