# 617 — Corrida 233: conclusão do SDMA0 por interrupção em regime por ~584 s, e trava dura

**Corrida:** 06/10/2026, bind às 11:35:59. Candidato lab 3.380.1.0 (SYS `4A4E19B4…`), boot frio, CMOS em 256 MB.
**Evidência:** `docs/evidence/2026-10-06_11-35-59-DESKTOP-HQAG6T4-lab-binding/`; o log é o `C:\BC250_DISPLAY_LOG.txt` a partir do baseline. Coleta automática presente.
**Critério:** doc 616 §2.

**Relato do Daniel:**
1. Durante a corrida: "deu uma tela preta, e depois voltou".
2. Depois: "travou, tive que reiniciar".

## 1. Medições (P0)

**Pré-condição OK:** PSP, `[P3B_MMHUB] LIGADA`, `[P3_IH] ATIVO`, StartDevice `status=0`.

**A P3.2 funcionou em regime até o fim:**
- última linha `[P3_IH] regime` (~581 s): `traps_emitidos=10956 traps_sdma=10956 ISR=10939 dpc_por_irq=10931 overflows=0 outros=0`;
- **o TRAP disparou o DPC de aposentadoria 10.931 vezes.** A diferença para os traps (25) é a fusão esperada: o DPC já estava na fila;
- `vetores` só passa `ISR` por 17, que são vetores consumidos numa mesma chamada.

**Regime:**
- ~584 s, até 2.238 Presents, 11.077 IBs executados;
- zero `TDR_QUIESCE`, `GFXHUB_FAULT`, `Blt Timeout`, `IB_TRIPOD TIMEOUT` e `DEVICE_REMOVED`;
- **nenhum TDR** no log do Sistema;
- `BUSY_YIELD`: a linha é amostrada e o contador chegou a #679 (correção do doc 621; eu havia escrito "um") e um `COMPOSITE_BLT RECUSADO (resolucao)` com destino `0x0` (574,7 s), a classe conhecida das superfícies entre processos.

**A tela preta que voltou não tem assinatura no log do KMD:**
- os flips (`SetVidPnSourceAddressWithMultiPlaneOverlay3`) seguem normais até ~583,9 s;
- no Application log, às 11:45:03 (~538 s), `Microsoft.Msn.Weather.exe parou de interagir` (MoAppHang). Que tenha sido isso é **H**.

**Trava dura aos ~584 s.** O log termina numa submissão normal do Node 0 (`accepted=1`). Mesma assinatura.

**Linha do tempo do pós-trava:**
1. boot às 11:46:56;
2. o watchdog fez o rollback às 11:47:19, depois do boot;
3. `RECOVERY_OK_ONLINE`;
4. `RESET_REASON=0x40080400` (bit 19, reset pelo `0xCF9`), como as outras;
5. teste de RAM sem erros (7.947 MB × 2);
6. BasicDisplay em Código 0, fusível inexistente (CLEAN).

**Nota:** o LiveKernelEvent `193/804/c0000001/108` das 11:47:18 é o mesmo que aparece a cada bind e rollback desde 08/09 e segue sem decodificação.

## 2. Veredito frente ao critério

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| A | **não** (Lei 10, regra 3) | Presentes: `dpc_por_irq` crescendo junto com os traps, zero timeout e zero TDR do SDMA, desktop por ~584 s. Falta o teardown, porque a máquina travou |
| B | não | |
| C | não | sem timeout nem regressão medida |
| D | não | sem 0x133; um único `BUSY_YIELD` |

**O que fica (P0):**
- **A P3.2 funciona como mecanismo:** a conclusão do SDMA0 foi disparada pela interrupção durante 584 s, sem perder nenhum TRAP.
- **A conclusão por interrupção não tira a trava.**
- A P3 inteira (MSI, GART do MMHUB, IH, TRAP e conclusão por IRQ) **sai das causas suficientes**.
- Foi a corrida mais longa desde a 229, mas uma amostra não permite atribuir isso à P3.2.

## 3. Próximo (paridade, doc 593 §4 e PLANO_MESTRE §3.3)

Ordem do inventário que sobra:
1. **P6:** MTYPE coerente (UC) nos objetos da RADV no segmento 2. Custo baixo/médio; o Linux mapeia BO coerente como UC, e nós sempre como NC.
2. **EOP do CP:** conclusão do anel gráfico por interrupção (o GFX ainda é por timer).
3. **P4:** SDMA1.
4. **P5:** VM de 4 níveis.
5. **P3.3:** RPTR do IH por doorbell.

**NÃO VALIDADO EM HARDWARE:** teardown do IH e da GART do MMHUB em regime; teardown com transporte remapeado.
