# Resultado físico do candidato 3.39

Data local: 30/08/2026
Ensaiado: **3.39.0.0 / SchemaVersion 110**
Veredito: **NÃO VALIDADO EM HARDWARE**

Evidência canônica:
`docs/evidence/2026-08-30_13-45-57-DESKTOP-HQAG6T4-lab-binding/`.

## Ciclo

- boot frio, `BasicDisplay`, Código 0;
- quarentena e prebinding LAB: `PASS`;
- uma submissão física;
- `bc250mcdm`, Código 0, display confirmado visível pelo operador;
- watchdog desarmado com o token exato;
- acceptance reprovado corretamente.

## Variável observacional

O PM4, MQD, GCVM e a ativação permanecem iguais ao 3.38. O candidato adiciona
somente três leituras de `CP_CPF_BUSY_STAT` (`BAR5 0x08220`): antes do kick,
imediatamente depois e no polling final. O transporte permite leitura e proíbe
escrita nesse offset.

## Resultado CPF

```text
antes do kick   0x68460000
depois do kick  0x68460000
polling final   0x68460000
```

Decodificação por `gc_10_1_0_sh_mask.h`:

```text
0x40000000  HQD_PQ_BUSY
0x20000000  HQD_WPTR_POLL_BUSY
0x08000000  HQD_ROQ_PQ_BUSY
0x00400000  HQD_CONSUMED_RPTR_BUSY
0x00040000  HQD_PQ_FETCHER_BUSY
0x00020000  HQD_MESSAGE_BUSY
```

Todos esses estados já existiam antes da doorbell e permaneceram invariantes.
Portanto, a causa primária antecede o kick: nasce durante a ativação/publicação
da HQD ou no protocolo interno disparado por ela.

## CPC e cursores

```text
                              antes          depois         polling final
CP_CPC_BUSY_STAT              00010000       04000000       04000000
CP_CPC_STALLED_STAT1          00000000       00010000       00010000
RPTR                          0              0              0
WPTR                                         40
MEC PC transitions                                          0
```

Neste boot, o sintoma `MEC2_DECODING_PACKET` reapareceu e o PC não transitou.
Isso mostra variação entre boots, mas não altera a conclusão causal do CPF,
pois seu estado ficou idêntico nos três pontos da mesma transação.

## Tripé e rollback interno

```text
resultado  0x43DAFFFE (poison)
fence      0xA55A0FF0 (poison)
RPTR/WPTR  0/40
```

O teardown interno teve duas divergências; a primeira foi
`ACTIVE_DEACTIVATE`, `CP_HQD_ACTIVE 0xC82C`, esperado `0`, observado `1`.

## Próxima direção

O próximo teste mínimo deve separar a transição causada por `CP_HQD_ACTIVE`
daquela causada pela publicação do WPTR: capturar `CP_CPF_BUSY_STAT` antes de
ACTIVE, imediatamente depois de ACTIVE e antes de qualquer publicação/kick.
Essas etapas entram apenas como observação, nunca como abort.

**NÃO VALIDADO EM HARDWARE.**
