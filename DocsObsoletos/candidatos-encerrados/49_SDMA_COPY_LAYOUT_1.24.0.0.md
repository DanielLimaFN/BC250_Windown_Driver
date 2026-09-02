# Layout Navi10 do COPY_LINEAR SDMA - pacote 1.24.0.0

Data: 2026-08-26.

Status: **LAYOUT CORRIGIDO, MAS RING AINDA NAO CONSUMIDO; ROLLBACK APROVADO; ACELERACAO NAO VALIDADA**.

O 1.23 confirmou firmware e F32 ativos, mas o RPTR permaneceu zero. A causa foi
isolada no layout do pacote COPY do builder compartilhado.

Layout corrigido:

```text
dw0 header COPY_LINEAR
dw1 byte_count - 1
dw2 endian/reserved = 0
dw3 src_lo
dw4 src_hi
dw5 dst_lo
dw6 dst_hi
dw7..11 GCR
dw12..15 FENCE MTYPE_UC
```

O schema 32 adiciona readback do WPTR no registrador, shadow do WPTR e RB_CNTL
aplicado. O gate continua exigindo `RPTR == WPTR == 64`, fence fora do poison,
64 dwords corretos e rollback integral.

Build completo e todos os testes offline, assinatura e auditoria: PASS.

- source SYS: `CA3CD10F3129280259176A793AE0FDD2B36B58220CDAAB3315FE4BA76F2C4956`;
- signed SYS: `90743789C89EBE7C109D7DB07587B7E80B1FF6B30EB2DC17655042C02D58310A`;
- INF: `83DB0C6D46DE6ACC3D406CE8C5652465DA8859E7CDC87E4981AB965DAB253FCC`;
- CAT: `8852971177DA77D4C69AA159C05699DEAB19874BD91D85B7452E1E340A992D7F`.

## Resultado fisico

Evidencia:
`docs\evidence\2026-08-26_17-53-47-DESKTOP-HQAG6T4-mcdm-replace`.

```text
RPTR/WPTR:          0 / 64
WPTR reg/shadow:    64 / 64
RB_CNTL:            80841015
Fence:              permaneceu 43DAFFFE
F32:                ativa, 14 transicoes
Rollback regs/mem:  1 / 1
```

A correcao do layout era necessaria, mas nao foi a unica causa. A publicacao do
WPTR passou integralmente; o consumidor ainda nao buscou o ring. O proximo gate
reproduz o `WRITE_LINEAR` de quatro dwords que ja passou no driver legacy e usa
as mesmas primitivas `READ/WRITE_REGISTER_ULONG` para BAR0.

**NAO VALIDADO EM HARDWARE**
