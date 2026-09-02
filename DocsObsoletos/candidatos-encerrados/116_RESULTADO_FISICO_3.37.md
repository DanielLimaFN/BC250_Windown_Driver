# Resultado físico do candidato 3.37

Data local: 30/08/2026
Ensaiado: **3.37.0.0 / SchemaVersion 108**
Veredito: **NÃO VALIDADO EM HARDWARE**

Evidência canônica:
`docs/evidence/2026-08-30_13-10-18-DESKTOP-HQAG6T4-lab-binding/`.

## Ciclo

- boot frio, `BasicDisplay`, Código 0;
- quarentena e prebinding LAB: `PASS`;
- uma submissão física;
- watchdog executou rollback externo ao expirar a janela de confirmação;
- após o rollback: `BasicDisplay`, Código 0;
- nenhuma segunda submissão foi feita neste boot.

## Mutação causal confirmada no ring

```text
DW0  C0027602  primeiro SET_SH_REG, SHADER_TYPE=COMPUTE
DW1  0000020C
DW2  00000040
DW3  00000000
DW4  C0027600  segundo SET_SH_REG, marcador preservado
DW5  00000212
DW6  20AF0000
DW7  00000004
```

O readback do ring confirma que o único bit experimental chegou à memória
consumida pela fila. Portanto, o resultado não pode ser atribuído a pacote
antigo ou falha de publicação do novo header.

## Diferencial do kick

```text
                              antes          depois         polling final
CP_CPC_STATUS                 A0000080       A0000082       A0000082
CP_CPC_BUSY_STAT              00000000       04080000       04090000
CP_CPC_STALLED_STAT1          00000000       00040000       00040000
CP_STAT                       88000000       00000000       00000000
CP_STALLED_STAT1              00000C00       00000C00       00000C00
```

Decodificação:

- `MEC2_BUSY` acende somente depois do kick;
- `MEC2_PIPE0_BUSY` permanece;
- `MEC2_MESSAGE_BUSY` aparece depois do kick e permanece no polling final;
- `MEC2_WAIT_ON_RCIU_READ` permanece;
- ao contrário do 3.36, `MEC2_DECODING_PACKET` e `MEC2_LOAD_BUSY` não aparecem.

O bit `SHADER_TYPE=COMPUTE` alterou de forma reproduzível o estado interno do
MEC2, eliminando neste ciclo o sintoma explícito de `DECODING_PACKET`. Isso é
progresso diagnóstico, não prova de consumo do pacote.

## FIFO ME2

```text
antes de ACTIVE  DEF0DEF0 DEF1DEF1 DEF2DEF2 DEF3DEF3
apos polling     DEF2DEF2 DEF3DEF3 DEF4DEF4 DEF5DEF5
```

O dump continua sem expor o header do ring.

## Cursores e tripé

```text
RPTR before / after-kick / last / after  0 / 0 / 0 / 0
RPTR non-zero samples                    0
WPTR                                     40
resultado                                0x43DAFFFE (poison)
fence                                    0xA55A0FF0 (poison)
TriadPassed / PhysicalReady              0 / 0
```

A predição de avanço até o segundo `SET_SH_REG` foi falsificada. Nenhuma wave
executou e nenhum resultado de shader foi produzido.

## Rollback

O teardown interno registrou uma divergência:

```text
stage       9 = HQD_PLAN
offset      0x0000C834 = CP_HQD_PERSISTENT_STATE
esperado    0x0BE05301
observado   0x0BEC5301
```

O watchdog concluiu o rollback externo, removeu `oem19.inf` e restaurou
`BasicDisplay`, Código 0.

## Próxima hipótese mínima

Como o decoder deixou de acusar `DECODING_PACKET`, mas o `RPTR` continuou em
zero com `MEC2_WAIT_ON_RCIU_READ` e `MEC2_MESSAGE_BUSY`, o próximo ciclo deve
investigar a dependência RCIU/estado de mensagem anterior ao consumo do ring,
sem adicionar abort novo e sem alterar simultaneamente outros pacotes PM4.

**NÃO VALIDADO EM HARDWARE.**
