# PSP GPCOM ring transacional - pacote 1.17.0.0

Data: 2026-08-26.

Status: **TRANSICAO TOS OBSERVADA; CONFIRMACAO ESTRITA SUPERADA PELO 1.18; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   76%
Miniport MCDM funcional            43%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

## Por que este gate vem antes da SDMA traduzida

A captura 1.16 mostrou `SDMA0_RB_BASE=0`, `RPTR=WPTR=0`. A SDMA precisa de
microcodigo aceito pelo PSP antes de consumir uma traducao VMID8. O primeiro
passo da cadeia de firmware e comprovar que o miniport consegue criar e
destruir seu proprio ring GPCOM.

## Transacao

1. alocar 4 KiB contiguos, non-cached, abaixo de 4 GB;
2. exigir TOS ready em `C2PMSG_64` e ring herdado integralmente zerado;
3. snapshot de WPTR, endereco e tamanho;
4. programar `C2PMSG_69/70/71` e comando KM `0x00020000`;
5. exigir ACK TOS em no maximo 500 polls;
6. enviar destroy `0x000C0000` e exigir novo ACK;
7. restaurar e conferir `C2PMSG_67/69/70/71`;
8. liberar a pagina somente depois do ACK de destroy.

Se o destroy nao for confirmado, a pagina permanece alocada para impedir DMA
use-after-free; o reboot da maquina de teste encerra a sessao.

## Telemetria schema 25

- status e PA baixa do ring;
- `C2PMSG_64` anterior;
- ACK e polls de create/destroy;
- flags create, destroy, restore e readiness fisica.

O auditor exige status zero, TOS antes, ambos os ACKs validos, polls em
`1..500`, create/destroy/restore iguais a 1 e `PspRingPhysicalReady=1`.

## Build e integridade

- build completo, unit tests e auditorias: PASS;
- DriverVer `08/26/2026,1.17.0.0`;
- source SYS: `93147039CF795E100FF221504751A947AD1F2537D46DBF8D8B596466317E6F0A`;
- signed SYS: `7EA61F51E126510A48F7DFE6E9711DC9809D3C54FCF84CB809197422E70AEA73`;
- INF: `D1864A380202B77D7CDF38ABB4708F02CC8E1FA256F44A862F2452593B7DB7B3`;
- CAT: `25266C11301E7AF84E7EDC1240F98C7D6D5778A187C936FF70D89C322067DA6B`;
- manifesto de build preserva `HardwareValidated=false`, pois o pacote ainda
  nao executa trabalho de aceleracao.

## Resultado fisico e refinamento posterior

Evidencia: `docs/evidence/2026-08-26_12-26-53-DESKTOP-HQAG6T4-mcdm-replace/audit-rerun.txt`.

- o pacote registrou `PspRingStatus=0`;
- ring fisico `0x7E23C000`;
- TOS anterior `C2PMSG_64=0x80000000`;
- o KMD escreveu create `0x00020000` e observou `0x80020000`;
- o KMD escreveu destroy `0x000C0000` e observou `0x800C0000`;
- TLB fisica e VMID8 inerte continuaram aprovadas no mesmo boot;
- dispositivo permaneceu deliberadamente fail-closed com Codigo 43.

O bit 31 nao fazia parte dos comandos escritos; ele foi publicado pelo PSP.
Logo, o ensaio 1.17 demonstrou resposta do TOS. O pacote 1.18 tornou a prova
mais forte ao tambem exigir status zero nos 16 bits baixos, conforme
`(valor & 0x8000FFFF) == 0x80000000`, e e a evidencia definitiva deste gate.

Nenhum firmware, SDMA, PM4 ou compute foi executado neste pacote. O gate
seguinte e `LOAD_IP_FW` do SDMA0, com fence do PSP, resposta do comando e
checksum do microcodigo lidos do silicio.
