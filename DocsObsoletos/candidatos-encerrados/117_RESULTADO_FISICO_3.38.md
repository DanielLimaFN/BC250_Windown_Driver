# Resultado físico do candidato 3.38

Data local: 30/08/2026
Ensaiado: **3.38.0.0 / SchemaVersion 109**
Veredito: **NÃO VALIDADO EM HARDWARE**

Evidência canônica:
`docs/evidence/2026-08-30_13-20-52-DESKTOP-HQAG6T4-lab-binding/`.

## Ciclo

- boot frio, `BasicDisplay`, Código 0;
- quarentena e prebinding LAB: `PASS`;
- uma submissão física;
- `bc250mcdm`, Código 0, display confirmado visível pelo operador;
- watchdog desarmado com o token exato;
- acceptance reprovado corretamente.

## Variável causal

Todos os seis pacotes `SET_SH_REG` do plano observável receberam
`SHADER_TYPE=COMPUTE`. Nenhum pacote não-SH, MQD, mapeamento GCVM ou passo de
ativação foi alterado.

O readback confirma pelo menos os dois primeiros limites:

```text
DW0  C0027602
DW4  C0027602
```

## Diferencial do kick

```text
                              antes          depois         polling final
CP_CPC_STATUS                 A0000080       A0000082       A0000082
CP_CPC_BUSY_STAT              00010000       04090000       04080000
CP_CPC_STALLED_STAT1          00000000       00040000       00000000
CP_STAT                       88000000       00000000       00000000
```

Decodificação:

- `MEC2_BUSY` acende depois do kick;
- `MEC2_PIPE0_BUSY` permanece;
- `MEC2_MESSAGE_BUSY` permanece no polling final;
- `MEC2_WAIT_ON_RCIU_READ` aparece depois do kick, mas desaparece no final;
- `MEC2_DECODING_PACKET` continua ausente.

Aplicar o bit compute aos demais `SET_SH_REG` eliminou a espera RCIU
persistente observada no 3.37, mas não produziu consumo confirmado do ring.

## Cursores e tripé

```text
RPTR before / after-kick / last / after  0 / 0 / 0 / 0
RPTR non-zero samples                    0
WPTR                                     40
resultado                                0x43DAFFFE (poison)
fence                                    0xA55A0FF0 (poison)
TriadPassed / PhysicalReady              0 / 0
```

O PC do MEC teve 34 transições, mas terminou em `0x17FB`. Nenhuma wave foi
provada e nenhum dado observável foi produzido.

## Rollback interno

```text
stage       9 = HQD_PLAN
offset      0x0000C834 = CP_HQD_PERSISTENT_STATE
esperado    0x0BE05301
observado   0x0BEC5301
```

## Próxima direção

O próximo ensaio não deve alterar novamente os headers `SET_SH_REG`: a espera
RCIU já se resolveu. A observação agora deve discriminar o protocolo de
mensagem/doorbell do MEC2 e o momento de confirmação do RPTR, mantendo as novas
leituras como observações, nunca como abort.

**NÃO VALIDADO EM HARDWARE.**
