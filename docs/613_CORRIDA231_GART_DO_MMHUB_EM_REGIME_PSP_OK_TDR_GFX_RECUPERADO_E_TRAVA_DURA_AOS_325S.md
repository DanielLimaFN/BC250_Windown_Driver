# 613 — Corrida 231: GART do MMHUB em regime e PSP OK; TDR do GFX recuperado e trava dura aos ~325 s

**Corrida:** 06/10/2026, bind às 10:00:15. Candidato lab 3.378.1.0 (SYS `B31B621A…`), boot frio, CMOS em 256 MB.
**Evidência:** `docs/evidence/2026-10-06_10-00-15-DESKTOP-HQAG6T4-lab-binding/`. O log é `display-log-this-bind-RECUPERADO.txt`; também estão na pasta o rastro de paginação, `reset-reason-pos-trava.txt` e `ramtest-pos-trava.txt`.
**Critério:** doc 612 §4.

## 1. Medições (P0)

**A P3.0b.1 fez o que devia:**
- `PspRingStatus=0` em 1 poll; a 230 tinha falhado com 500 polls.
- PFP e HQD OK, `StartDevice LEAVE status=0x00000000`.
- `[P3B_MMHUB] resultado: LIGADA (34/34 conferidos, ACK da VMID 0 no MMHUB) PROTECTION_FAULT_STATUS=0x00000000`, armada depois do PSP. Valores iguais aos da 230.
- `Bc250ActiveGfxEngineOnline SUCESSO`.

**Regime, com a GART do MMHUB ligada:**
- desktop por ~325 s, até 1.325 Presents;
- 13.115 IBs executados;
- zero `GFXHUB_FAULT`, zero `Blt Timeout` e zero `Blt RECUSADO`.

**TDR aos ~315 s:**
- **Gatilho:** o anel do GFX (VMID 10, IB de 2.488 DW da RADV) parou; `[TDR_GFX_PROBE] Responsive=0 (RPTR 1670->1670)`.
- **Recuperação completa:**
  - SDMA0 reconstruído com readback;
  - `TDR_GFX_RESTART_PROOF PASS`;
  - fences reconciliadas;
  - `adaptador aceita submissao novamente`.

  É a mesma classe de TDR gráfico em VMID 10 da Corrida 201.

**Trava dura aos ~325 s:** ~10 s depois do TDR. O log termina no meio de submissões normais do Node 0, com IB executado, fence sobre poison e nenhum erro. É a assinatura das travas 199–228.

**Pós-trava:**
- `RESET_REASON=0x40080400` (bit 19, reset pelo `0xCF9`), igual às outras;
- teste de RAM sem erros (8.827 MB × 2);
- rollback `RECOVERY_OK_ONLINE` no boot.

## 2. Veredito frente ao critério

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| A | **não** (Lei 10, regra 3) | Faltou o sinal do teardown (`DESLIGADA ... falhas=0`); a corrida morreu antes |
| B | não | O PSP passou |
| C | não | O display ficou de pé por 325 s com fault zero no arme; depois da trava não há leitura |
| D | não observável | O teardown não aconteceu |

**Resultado:** é a trava dura prevista no critério, *"conta como amostra com uso normal"*. Ela veio depois de um TDR do GFX recuperado; se o TDR teve papel, é **H**.

**O que fica:**
- **A P3.0b.1 está validada no StartDevice** (P0): o PSP funciona com a GART do MMHUB armada depois dele.
- **A GART do MMHUB se manteve por ~325 s** com o desktop vivo (P0).
- **A GART do MMHUB não tira a trava** (P0 nesta amostra). Ela não é causa suficiente, mas como a P3.0b é pré-requisito do IH, continua ligada.
- **A correção do teardown com transporte vazio** não foi exercitada: **NÃO VALIDADO EM HARDWARE**.

## 3. Próximo

**P3.1: ligar o IH** (`BC250_P3_IH_ENABLED 1`). O código já está escrito (doc 609 §3):
- anel na GART, entradas 128..192;
- `TRAP` do SDMA0;
- ISR;
- prova por vetor no arme, com falha fechada.

Com a GART do MMHUB provada, o VA do anel do IH passa a ter tradução. A conclusão das submissões continua por polling nesta etapa: uma diferença por corrida.
