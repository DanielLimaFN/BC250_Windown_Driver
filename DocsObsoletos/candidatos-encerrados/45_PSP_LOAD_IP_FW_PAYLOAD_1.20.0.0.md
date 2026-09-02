# PSP LOAD_IP_FW payload SDMA0 - pacote 1.20.0.0

Data: 2026-08-26.

Status: **FENCE PSP APROVADA; FIRMWARE AINDA RECUSADO POR STAGING DESALINHADO; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   82%
Miniport MCDM funcional            45%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

## Diferenca objetiva para 1.19

O arquivo completo continua sendo instalado, medido e autenticado por SHA-256.
Somente a regiao entregue ao PSP mudou:

```text
arquivo autenticado: 33792 bytes, SHA-256 15D0...9E95
header AMD:            256 bytes
PA enviada:            allocation PA + 0x100
tamanho enviado:       33536 bytes
firmware type:         9 (SDMA0)
ucode version:         52 (0x34)
```

Essa forma coincide com a evidencia historica que obteve `RespStatus=0`. O
pacote exige novamente fence PSP, resposta zero, checksum fisico não zero/nem
all-ones, destroy e restore. O checksum nao e fixado pelo KMD.

O gate 1.19 provou que ring, comando, WPTR e fence do PSP funcionam: a fence
mudou para `1` no segundo poll e a resposta `0xFFFF0008` foi publicada. Assim,
o 1.20 altera somente offset/tamanho do blob.

## Build e integridade

- build completo e testes unitarios: PASS;
- DriverVer `08/26/2026,1.20.0.0`, schema 28;
- source SYS: `BA240E4CD0E5ACCDDF3E3F4DA61016E0340077C72330D533450CDEF5D13F0D66`;
- signed SYS: `2603A79530D442CBA0A9984FFCC23291F2231A360343DF022521D1084B694671`;
- INF: `576C54D8F03466984173D5258726EC0E2E30148114D2FAAA3BA1E22165D16680`;
- CAT: `3B0C42926C177A507586B151BD58B34960D9C9C342C62B1F053FF3155571C93B`;
- firmware no catalogo: hash esperado confirmado;
- `HardwareValidated=false`.

**NAO VALIDADO EM HARDWARE**

## Resultado fisico

Evidencia: `docs/evidence/2026-08-26_13-16-37-DESKTOP-HQAG6T4-mcdm-replace`.

- payload 33.536 bytes e fence PSP no segundo poll;
- resposta permaneceu `0xFFFF0008`;
- o payload comecava em `allocation PA + 0x100`, portanto desalinhado;
- no caminho legacy aprovado, o payload era copiado para uma nova alocacao e
  comecava na PA alinhada fornecida por `MmGetPhysicalAddress`;
- o 1.21 move o payload para o inicio da alocacao e valida seu hash separado.
