# Resultado físico do candidato 3.28 — GPUVA do ring comprovada

Data local: 30/08/2026  
Pacote: 3.28.0.0 / SchemaVersion 99  
Veredito: **NÃO VALIDADO EM HARDWARE**

## Resultado

O candidato foi vinculado em boot limpo, com watchdog de 180 segundos. O
dispositivo entrou em `bc250mcdm / Código 0`, executou o bring-up e preservou o
display. O gate estrito do shader reprovou:

```text
PFP direct                    PASS / PhysicalReady=1
SDMA                          PASS / PhysicalReady=1
WGP preflight                 PASS
PQ_BASE                       0x0000000000010000
Ring físico                   0x000000F400AA0000
PTE[0x10]                     0x000000F400AA00E1
Tradução GPUVA -> físico      PASS
Readback dos 64 dwords        PASS / mismatches=0
WPTR                          40
RPTR                          0
HQ_STATUS0 final              0x000080C0
HQD_ERROR / UTCL1 faults      0
Resultado                     0x43DAFFFE (poison preservado)
Fence                         0xA55A0FF0 (poison preservado)
Comparação byte a byte        4 mismatches
HqdActiveStatus               7 / PM4_NOT_CONSUMED
Memória restaurada            1
PhysicalReady                 0
```

A mudança do 3.28 funcionou no escopo que pretendia provar: o MQD publicou a
GPUVA baixa `0x10000`, a PTE apontou exatamente ao ring físico e o conteúdo PM4
foi relido sem divergência. Isso elimina a hipótese de que o ensaio ainda
programava `PQ_BASE` com o endereço físico.

O ring, contudo, não começou a ser consumido. Diferentemente do 3.27, que
terminou com `RPTR=WPTR=40` sem efeito observável, o 3.28 terminou com
`RPTR=0/WPTR=40`. Não há base para promover qualquer execução parcial a
sucesso.

## Rollback

O watchdog expirou e completou o rollback automaticamente:

```text
state                       ROLLED_BACK
detail                      BasicDisplay restoration completed
exit_code                   0
dispositivo final           BasicDisplay / Código 0
serviço bc250mcdm           ausente
marcador/pacote OEM         ausente
hardware_validated          false
```

O SYS residual pós-descarregamento permanece no diretório de drivers e só deve
ser movido por quarentena recuperável após novo boot, como nos ciclos
anteriores.

## Evidência canônica

- `docs/evidence/2026-08-30_08-39-06-DESKTOP-HQAG6T4-lab-binding/probe-full-before-watchdog.json`
- `docs/evidence/2026-08-30_08-39-06-DESKTOP-HQAG6T4-lab-binding/shader-dispatch-acceptance.json`
- `docs/evidence/2026-08-30_08-39-06-DESKTOP-HQAG6T4-lab-binding/watchdog-state.json`
- `docs/evidence/2026-08-30_08-39-06-DESKTOP-HQAG6T4-lab-binding/watchdog-rollback.txt`

## Próximo gate técnico

A próxima revisão deve explicar o estado de fetch `HQ_STATUS0=0x80C0` com fila
ativa, WPTR publicado, MEC executando e ausência de faults. A investigação deve
permanecer no caminho MEC/HQD — controle da fila, estado do scheduler, VMID e
requisitos de fetch — sem voltar a alterar a tradução do ring já comprovada.
