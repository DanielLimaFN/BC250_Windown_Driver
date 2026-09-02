# Resultado físico do candidato 3.35

Data local: 30/08/2026
Ensaiado: **3.35.0.0 / SchemaVersion 106**
Veredito: **NÃO VALIDADO EM HARDWARE**

Evidência canônica:
`docs/evidence/2026-08-30_12-41-38-DESKTOP-HQAG6T4-lab-binding/`.

## Ciclo transacional

- Boot frio confirmado; `HqdActiveWptrPollCntl1DirtyEntry = 0`.
- Prebinding LAB: `PASS`.
- Binding: `bc250mcdm`, Código 0.
- O gate visual não foi confirmado: a captura Windows retornou
  `DXGI_ERROR_DEVICE_REMOVED (0x887A0005)`.
- O watchdog executou rollback às 12:44:40.
- Estado final: `BasicDisplay`, Código 0.

Uma única submissão física ocorreu neste boot.

## Resultado da fila

```text
HqdActiveStatus                 7 (PM4_NOT_CONSUMED)
RptrAfterKick / MmioLast / After 0 / 0 / 0
RptrNonZeroSamples              0
WptrMmioLast                    40
PQ_CONTROL                      0xD030090D
HQ_STATUS0                      0x00008040
CP_CPC_STATUS                   0xA0000082
CP_CPC_BUSY_STAT                0x04080000
CP_CPC_STALLED_STAT1            0x00000000
CP_STALLED_STAT1                0x00000C00
resultado / fence               0x43DAFFFE / 0xA55A0FF0 (poison)
TriadPassed / PhysicalReady     0 / 0
```

O `RPTR=1` do 3.34 nao se repetiu. No 3.35 nenhuma das amostras observou RPTR
nao-zero. Isso confirma a advertência do handoff: o consumo de um dword nao e
deterministico e nao pode, isoladamente, provar defeito do primeiro header.

## Prefixo real do ring relido da memória

```text
DW0 0xC0027600  SET_SH_REG
DW1 0x0000020C  COMPUTE_PGM_LO relativo a SH base
DW2 0x00000040  shader GPUVA >> 8
DW3 0x00000000  COMPUTE_PGM_HI
DW4 0xC0027600  SET_SH_REG
DW5 0x00000212  COMPUTE_PGM_RSRC1 relativo a SH base
DW6 0x20AF0000  PGM_RSRC1
DW7 0x00000004  PGM_RSRC2
```

O ring publicado corresponde byte a byte ao plano offline. A captura prova o
conteúdo em memória, mas nao prova que o MEC o buscou.

## Dumps de header

```text
ME1  DEF0DEF0 DEF1DEF1 DEF2DEF2 DEF3DEF3
ME2  7C027377 C0000E00 DEF0DEF0 DEF1DEF1
```

O ME2 devolveu conteúdo diferente do FIFO vazio do ME1, portanto o offset
`0x0823C` e observável. Porém os valores nao correspondem a `RingDword0..7` e,
com RPTR sempre zero, nao podem ser atribuídos ao pacote deste ensaio. Podem
ser conteúdo anterior/residual da FIFO ou tráfego interno do microengine.

## Conclusão e próximo passo mínimo

Nao alterar ainda `0xC0027600` para `0xC0027602`: a correlação causal entre o
header sem bit compute e o stall do 3.34 nao foi reproduzida.

O próximo candidato deve continuar somente observacional:

1. drenar e registrar ME2 antes da ativação/publicação do trabalho;
2. drenar novamente depois do polling;
3. publicar snapshots de CPC/CP imediatamente antes do kick e após o kick;
4. manter PM4, MQD, GCVM e sequência de ativação inalterados;
5. não transformar os novos snapshots em abort.

Isso separará FIFO residual de headers adicionados pelo ciclo atual e poderá
mostrar em qual transição o comportamento variável `RPTR 0/1` nasce.

**NÃO VALIDADO EM HARDWARE.**
