# PSP GPCOM ACK estrito - pacote 1.18.0.0

Data: 2026-08-26.

Status: **GATE FISICO APROVADO; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   80%
Miniport MCDM funcional            43%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

## Semantica reforcada

O pacote 1.17 tratava qualquer leitura com bit 31 como resposta do TOS. Os
comandos realmente escritos eram `0x00020000` e `0x000C0000`; portanto, a
transicao para `0x80020000`/`0x800C0000` ja vinha do PSP. O pacote 1.18 reforca
a prova exigindo tambem status zero nos 16 bits baixos.

O pacote 1.18 exige a semantica usada pelo amdgpu e pelo driver de laboratorio:

```text
(C2PMSG_64 & 0x8000FFFF) == 0x80000000
```

Logo, bit 31 deve estar ativo e os 16 bits de status baixos devem estar
zerados. O poll continua limitado a 500 iteracoes de 1 ms.

## Maquina de estados

```text
preflight TOS/ring vazio -> snapshot -> create -> ACK estrito
    -> destroy -> ACK estrito -> restaurar/readback -> liberar ring
```

Se destroy nao receber ACK, a pagina do ring permanece alocada para impedir
DMA use-after-free. O reboot e o teardown seguro da sessao de teste.

## Criterio do ensaio fisico

- `PspRingStatus=0`;
- create e destroy observados como ACK estrito, tipicamente `0x80000000`;
- polls em `1..500`;
- create, destroy, restore e physical ready iguais a `1`;
- valores `0x80020000` ou `0x800C0000` sao rejeitados como ACK;
- dispositivo permanece fail-closed, sem PM4/compute.

Depois deste gate, o candidato seguinte pode manter o ring aberto durante um
`LOAD_IP_FW` SDMA0 e exigir fence DMA do PSP, resposta zero e checksum de
microcodigo do silicio.

## Resultado fisico

Evidencia: `docs/evidence/2026-08-26_12-50-42-DESKTOP-HQAG6T4-mcdm-replace`.

- schema 26 e SYS instalado identico ao pacote;
- ring fisico `0x7E23C000`;
- create `0x00020000 -> 0x80020000`, status baixo zero, primeiro poll;
- destroy `0x000C0000 -> 0x800C0000`, status baixo zero, primeiro poll;
- create, destroy, restore e physical ready iguais a `1`;
- TLB e VMID8 inerte continuaram aprovados;
- dispositivo permaneceu fail-closed com Codigo 43.

## Build e integridade

- build completo, testes unitarios e auditoria estatica: PASS;
- regressao stale-command: PASS;
- DriverVer `08/26/2026,1.18.0.0`, schema de telemetria `26`;
- source SYS: `C1F4074E615B70F961E6398BDB73985F71FED24B6F61AA5F89A8DBB5D4AE1564`;
- signed SYS: `CF66B677AC1951DED88FD8CF82D7BA2E652613B3C01EC0960814DAB54A8F58E0`;
- INF: `E11296F024D2AB241EF727DB2251BA7935460413977678D07EE5A64A39C61507`;
- CAT: `42D94AD8DD6F102EE1C6ACFA1F41EB3246406E28AAE16CC8A9BBE28ECE9B380C`;
- instalacao/binding durante o build: nao;
- `HardwareValidated=false`.

**NAO VALIDADO EM HARDWARE**
