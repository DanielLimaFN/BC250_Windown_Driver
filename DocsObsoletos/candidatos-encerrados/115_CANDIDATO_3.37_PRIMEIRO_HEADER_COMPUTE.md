# Candidato 3.37 — bit compute somente no primeiro header

Data local: 30/08/2026
Pacote preparado: **3.37.0.0 / SchemaVersion 108**
Veredito: **NÃO VALIDADO EM HARDWARE**

Continuação de [`114_RESULTADO_FISICO_3.36.md`](114_RESULTADO_FISICO_3.36.md).

## Hipótese e única variável

O 3.36 provou que o kick causa a transição:

```text
MEC2 ocioso -> MEC2_LOAD_BUSY + MEC2_PIPE0_BUSY
             + MEC2_DECODING_PACKET + MEC2_WAIT_ON_RCIU_READ
```

O primeiro pacote em memória é `SET_SH_REG 0xC0027600`, sem o bit 1 usado por
`PACKET3_COMPUTE`. O 3.37 altera somente esse primeiro header:

```text
DW0  0xC0027600 -> 0xC0027602
DW4  0xC0027600 permanece inalterado
```

Nenhum outro dword, registrador, MQD, mapeamento GCVM ou passo de ativação foi
alterado.

## Predição falsificável

Se o bit compute é necessário para o primeiro `SET_SH_REG`, o RPTR deve
ultrapassar o limite inicial e então parar no segundo pacote ainda sem o bit.
Esse avanço é diagnóstico; não valida shader, fence ou aceleração.

Se RPTR e estados permanecerem equivalentes ao 3.36, a hipótese cai ou o
bloqueio antecede a interpretação desse bit.

## Validação offline

- suíte completa: `EXIT=0`;
- teste exige `ProgramPacketIndex = 0xC0027602`;
- teste exige `ResourcePacketIndex = 0xC0027600`;
- prefixo relido esperado atualizado somente em `RingDword0`;
- pacote LAB e safety audit: `PASS`;
- kernel `/W4 /WX`: sem warnings.

## Artefatos

```text
DriverVer           08/30/2026,3.37.0.0
Schema              108
recipe cp/vmid0/sdma 1 / 0 / 0
Package SYS SHA-256 FD254566A35A470FEA101572F6D537392BC08E047AA47B169ACF823DFB4B7984
Package INF SHA-256 705D8A580D91F954C0DF8DF6C53A4A546ACCA1CA582BE39CE8D8BB855179578F
Package CAT SHA-256 0B3916D07D503D744A87A80C9C7BC7A3110B37D12BA73CE9B64D98DABD70B37B
```

O próximo ensaio exige rollback externo do 3.36 e reboot manual. Uma única
submissão física por boot.

**NÃO VALIDADO EM HARDWARE.**
