# Análise pós-corrida — candidato 3.115.1.0

## Veredito

**Node 1 NÃO VALIDADO EM HARDWARE.** A remoção das escritas experimentais de política WGP
entrou no silício e eliminou o `MEC2_WAIT_ON_RCIU_READ` nesta amostra, mas não fez o MEC
consumir o anel.

## Tripé B7

| Medida | Antes | Depois | Resultado |
|---|---:|---:|---|
| `P1SubmitAccepted` | 17 | 18 | passou |
| `P1RetiredTotal` | 17 | 17 | falhou |
| `P1LastWptrMmioReadback` | 204 | 216 | passou |
| `P1LastRptrMmio` | 204 | 204 | falhou |

O JSON do runner registra `hardware_validated=false`. A fence monitorada terminou em
`UINT64_MAX`, sem prova de fence nem de canários.

## O que o 3.115 realmente mudou no silício

O log novo apareceu exatamente uma vez no último bind:

```text
WGP_PG_READONLY: SPI=0x0000FFFF RLC=0x00000003 RLC_PG_CNTL=0x00000000 Policy=FIRMWARE_PRESERVED
```

Logo, o binário correto executou e não forçou `0x1F`. Contra o 3.114, o último submit do
3.115 mediu `CpcStalled=0` no pré-submit, no pós-doorbell e no snapshot do TDR. Portanto,
o sintoma `CP_CPC_STALLED_STAT1=0x00040000` não reapareceu nesta corrida. Isso valida a
remoção das escritas como higiene de estado, mas não fecha o Item 1.

## Estado exato da fila

```text
PRE : Active=1 RPTR=0 WPTR=0   HqStatus=0x40008040 CpcStalled=0
POST: Active=1 RPTR=0 WPTR=216 HqStatus=0x00008040 CpcStalled=0
TDR : Active=1 RPTR=0 WPTR=216 PqControl=0xD030090D FaultStatus=0
```

O doorbell retirou `QUEUE_IDLE` de `HQ_STATUS0` e o CPF passou de
`0x28060000` para `0x68460000`, mas o `RPTR` continuou zero. Em
`PqControl=0xD030090D`, `PQ_EMPTY` (bit 15) está **zero**. Assim, duas condições da lista
melhoraram ou já estavam satisfeitas — fila não vazia e ausência do stall RCIU —, mas o
consumo real ainda não ocorreu.

O descritor foi preservado como
`C0023F00,00011000,00000000,09800011`; a caminhada CPU resolveu o IB `0x11000` para
`0x451FCC000`, com 17 DWORDs corretos. Isso prova preparação pela CPU, não fetch da GPU.

## Discriminante novo

Há uma divergência que precisa ser tratada antes de outro ensaio: imediatamente antes do
submit, o hardware reporta `RPTR=0/WPTR=0`, enquanto o estado lógico do anel parte de 204 e
publica diretamente 216 (`Sub=17/Ret=17`). A próxima investigação deve provar se o cursor
inicial lógico precisa ser reconciliado com o par físico após a ativação/reentrada da fila.
Não alterar cursor por hipótese: primeiro adicionar gate/telemetria que compare os dois
domínios e recuse publicação incoerente.

## Recuperação

O TDR quiesceu o SDMA0 e o reiniciou com `SYNC_NOP TRIPOD PASS`. O watchdog arquivou o log
integral (74.236 linhas, SHA-256
`C06205A27BE080F594AE353773EBF69112E884B43C10C34AEDEC617CCD691A52`), removeu `oem35.inf`
e restaurou o Adaptador de Vídeo Básico da Microsoft em Código 0. Novo boot frio é
obrigatório antes de qualquer outro bind.
