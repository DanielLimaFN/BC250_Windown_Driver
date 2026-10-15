# 621 — Corrida 235: SDMA1 vivo e ocioso, e trava dura aos ~267 s

**Corrida:** 06/10/2026, bind às 14:30:44. Candidato lab 3.382.1.0 (SYS `AA9F741D…`), RADV `C6BEC54B…`, boot frio, CMOS em 256 MB.
**Evidência:**
- `docs/evidence/2026-10-06_14-30-44-DESKTOP-HQAG6T4-lab-binding/`;
- log vivo `C:\BC250_DISPLAY_LOG.txt` a partir do offset 361.037.603;
- coleta automática presente: motivo do reset e teste de RAM.

**Critério:** doc 620 §3.
**Relato do Daniel:** "travou e reiniciei".

## 1. Medições (P0)

**Pré-condição OK:** StartDevice `status=0`, `[P3B_MMHUB] LIGADA` (34/34), `[P3_IH] ATIVO`, `[CP_EOP] ARMADO`.

**SDMA1 vivo:**
- `[P4_SDMA1] firmware pelo PSP (tipo 10): Status=0 PhysicalReady=1`. O checksum foi de `0x00000000` para `0x55B182E3`, resposta do PSP 0.
- `BringUp=OK`: readback 1, `RPTR=64 WPTR=64`, writeback do RPTR na RAM 64, fence `0x5D1A0001` sobre poison em 1 poll, relógio de volta ao POST (`0x000C0100`).
- `resultado: ATIVO`. O anel ficou na RAM pela GART (VA `0xE0000`, MTYPE_UC).
- **Achado:** antes de nós, o `SDMA1_F32_CNTL` já lia `0x00000000` (HALT=0), não HALT=1 como nas Corridas 199/200. O `RB_CNTL` era `0x80840000` (anel desligado). Que a carga pelo PSP solte o F32 é **H**.
- `outros=0` na corrida inteira: o SDMA1 não gerou vetor sem dono.

**Regime:**
- ~267 s pela QPC (`1192636468` → `3861987810`, a 10 MHz);
- 9.337 IBs no SDMA0, 3.965 Presents;
- última linha `[P3_IH] regime`: `traps_sdma=9054 dpc_por_irq=9011 eop_gfx=3681 eop_emitidos=3681 dpc_gfx_por_irq=3681`, zero overflow;
- zero `TDR_QUIESCE`, `GFXHUB_FAULT`, `Blt Timeout`, `IB_TRIPOD TIMEOUT`, `DEVICE_REMOVED` e `INSIDE_IB`;
- zero recusa de ABI;
- 86× `[ActiveOpenAlloc] RECUSADO 0xC0000035`, a classe conhecida das superfícies compartilhadas (doc 619).

**Trava dura aos ~267 s, mesma assinatura.** O log termina em atividade normal:
- um FILL de paginação de 8.608 bytes submetido (`accepted=1`);
- um flip MPO3 com `PRIMARY_COPY #3229 BEGIN`;
- um `PAGING_DDI` fechado.

**Pós-trava:**
- boot às 14:36:33;
- `RESET_REASON=0x40080400`;
- RAM sem erros (7.935 MB × 2);
- placa em Código 43 pelo fusível;
- rollback do watchdog não rodou.

**Correção do doc 617:** lá escrevi "um `BUSY_YIELD`". A linha é amostrada, e o contador da 233 chegou a **#679**. Na 235 chegou a #422. É o comportamento normal da guarda de reentrância com a conclusão por interrupção, não um evento raro.

## 2. Veredito frente ao critério

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| A | **não** (Lei 10, regra 3) | Presentes: firmware, prova, `ATIVO`, zero fault/TDR, desktop por ~267 s. Falta o teardown (`[P4_SDMA1] DESLIGADO`), porque a máquina travou |
| B | não | firmware carregado |
| C | não | prova OK |
| D | não | sem regressão medida |
| E | não | |

**O que fica (P0):**
- **A P4.1 funciona como mecanismo.** O SDMA1 sobe, prova o tripé e fica ocioso, como no `amdgpu`.
- **O estado do SDMA1 não tira a trava.** Ele sai das causas suficientes.

## 3. Próximo

**P4.2: trabalho no SDMA1, como o escalonador do `amdgpu`.**
- No Linux, as atualizações de VM e as cópias se distribuem pelas duas instâncias.
- Nós concentramos no SDMA0 toda a paginação do Windows: zeramento, despejo, PTE, flip e composição. As travas acontecem nessas rajadas (docs 560, 577, 582).
- Desenho a fazer: qual parte do trabalho vai para o SDMA1. Atualizações de PTE (4c.4) e FILL de paginação são os candidatos naturais. Cada uma é uma corrida.

**NÃO VALIDADO EM HARDWARE:**
- teardown do SDMA1, do EOP do CP, do IH e da GART do MMHUB em regime;
- a P4.2.
