# Resultado físico do candidato 3.36

Data local: 30/08/2026
Ensaiado: **3.36.0.0 / SchemaVersion 107**
Veredito: **NÃO VALIDADO EM HARDWARE**

Evidência canônica:
`docs/evidence/2026-08-30_12-59-24-DESKTOP-HQAG6T4-lab-binding/`.

## Ciclo

- boot frio, `BasicDisplay`, Código 0;
- quarentena e prebinding LAB: `PASS`;
- uma submissão física;
- `bc250mcdm`, Código 0, display confirmado visível pelo operador;
- watchdog desarmado com o token exato;
- acceptance reprovado corretamente.

## Diferencial do kick

```text
                              antes          depois         polling final
CP_CPC_STATUS                 A0000080       A0000082       A0000082
CP_CPC_BUSY_STAT              00000000       04010000       04000000
CP_CPC_STALLED_STAT1          00000000       00050000       00050000
CP_STAT                       88000000       00000000       00000000
CP_STALLED_STAT1              00000C00       00000C00       00000C00
```

Decodificação por `gc_10_1_0_sh_mask.h`:

- `CP_CPC_STATUS.bit1`: `MEC2_BUSY` acende somente depois do kick;
- `0x00010000` em `BUSY_STAT`: `MEC2_LOAD_BUSY`, transitório;
- `0x04000000`: `MEC2_PIPE0_BUSY`, permanece;
- `0x00010000` em `STALLED_STAT1`: `MEC2_DECODING_PACKET`;
- `0x00040000`: `MEC2_WAIT_ON_RCIU_READ`.

Logo, a doorbell causa diretamente a entrada do MEC2 no decoder e a espera
por RCIU. Não é um estado herdado do boot.

## FIFO ME2

```text
antes de ACTIVE  DEF0DEF0 DEF1DEF1 DEF2DEF2 DEF3DEF3
apos polling     DEF2DEF2 DEF3DEF3 DEF4DEF4 DEF5DEF5
```

Nenhum valor corresponde ao prefixo do ring. O dump não oferece, neste ciclo,
prova de fetch do header, mas o diferencial CPC prova a reação causal ao kick.

## Cursores e tripé

```text
RPTR after-kick / last / after  0 / 0 / 0
RPTR non-zero samples           0
WPTR                             40
resultado                        0x43DAFFFE (poison)
fence                            0xA55A0FF0 (poison)
TriadPassed / PhysicalReady      0 / 0
```

## Rollback interno

O teardown registrou duas falhas. A primeira, preservada como causa raiz:

```text
stage       1 = ACTIVE_DEACTIVATE
offset      0x0000C82C = CP_HQD_ACTIVE
esperado    0
observado   1
transport   0 (readback divergente, não falha de transporte)
```

`CP_PQ_WPTR_POLL_CNTL1` restaurou corretamente para zero. O driver permanece
vinculado e o display visível, mas um novo ensaio exige rollback externo e
reboot manual.

## Próximo teste mínimo

Alterar somente o primeiro header `SET_SH_REG` do plano:

```text
DW0: 0xC0027600 -> 0xC0027602 (shader type compute)
DW4: permanece 0xC0027600 como marcador do próximo limite
```

Predição falsificável: se o bit compute resolve a primeira decodificação,
`RPTR` deve ultrapassar o primeiro pacote e parar no segundo, sem que isso seja
confundido com validação de shader.

**NÃO VALIDADO EM HARDWARE.**
