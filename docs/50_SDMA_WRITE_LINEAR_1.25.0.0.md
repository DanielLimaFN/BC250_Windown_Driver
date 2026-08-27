# WRITE_LINEAR SDMA conhecido - pacote 1.25.0.0

Data: 2026-08-26.

Status: **ENSAIO FISICO BLOQUEADO NO CONSUMO; ROLLBACK APROVADO; ACELERACAO NAO VALIDADA**.

O 1.25 troca temporariamente o COPY pelo primeiro pacote que ja foi comprovado
no laboratorio legacy:

```text
WRITE_LINEAR de 4 dwords
GCR de cache completo
FENCE MTYPE_UC
WPTR final 0x44 (68 bytes)
```

Todos os acessos BAR0 agora usam `WRITE_REGISTER_ULONG` e
`READ_REGISTER_ULONG`, como no caminho legacy. O schema 33 registra ainda o
header lido de volta, RPTR por MMIO e controle de polling aplicado.

Gate: `RPTR == WPTR == 68`, fence fora do poison, quatro dwords iguais aos
valores contidos apenas no pacote e rollback integral.

Build completo, testes offline, assinatura e auditoria: PASS.

- source SYS: `16EED50C4C80A51772E0372D3167C9A4D6897453A60B436F9ED74C6F8301BEC9`;
- signed SYS: `43476FE18C904C1B001E605D61553A55885E018CDBB0B305649B4C93ECC49F13`;
- INF: `FCB8B03A9F45FBF3888A3B80F622557F2D7A4575A5E861026F9015612A99E45B`;
- CAT: `52BA39D525DA707B3A8AF6C7C5B7F96D3F4CACAAB9F5D6DB90F07F117F4CE03D`.

**NAO VALIDADO EM HARDWARE**

## Resultado fisico

Evidencia: `docs/evidence/2026-08-26_18-01-43-DESKTOP-HQAG6T4-mcdm-replace`.

- firmware PSP e F32: PASS, com 14 transicoes de PC;
- header do `WRITE_LINEAR`: `0x00000002` lido pela CPU;
- `WPTR` registrador/shadow: `68/68`;
- `RB_CNTL`: `0x80841015`;
- `RPTR` registrador/writeback: `0/0` apos 1000 polls;
- fence: permaneceu em `0x43DAFFFE`;
- rollback de registradores/memoria: `1/1`.

Conclusao: o pacote e a publicacao estao visiveis, mas a SDMA nao iniciou o
fetch do ring. O proximo ensaio reproduz o layout fisico contiguo comprovado
no driver legacy e captura estados SDMA/UTCL apos o timeout.

**NAO VALIDADO EM HARDWARE**
