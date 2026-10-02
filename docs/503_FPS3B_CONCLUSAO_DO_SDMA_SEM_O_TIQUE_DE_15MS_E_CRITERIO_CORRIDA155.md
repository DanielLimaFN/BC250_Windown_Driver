# 503 — FPS.3b: a conclusão da paginação deixa de esperar o tique de 15,6 ms; critério da Corrida 155

> 28/09/2026, offline. Base: as medições da Corrida 154 ([doc 502](502_CORRIDA154_DX95_VMID8_ESTAVEL_TRAVA_DURA_SEM_HARNESS_E_TIQUE_DE_15MS_NA_CONCLUSAO.md)), refeitas por `tools/analyze-fps3-retire.py`, que dá os mesmos números.

## 1. O que foi medido (P0) e o que foi lido no código (P1)

**Ciclo de um IB de paginação na 154 (média de 11.515 ciclos):**

| Trecho | ms | Parte |
|---|---|---|
| submit → o driver perceber a conclusão | 6,52 | ~46% |
| conclusão → próximo submit, dentro dos nossos FLUSH_TLB (4,2 por ciclo) | 4,32 | ~30% |
| conclusão → próximo submit, resto (VidMm) | 3,32 | ~24% |

- **Histograma submit → conclusão** (colunas de 1 ms): 309, 507, 582, 531, 412, … 181, 195, 166, 102, e então **17, 17, 8, 6** a partir de 16 ms. Plano até 15 ms, corte em 16 ms.
- **Harness (153):** 584 ms por quadro, dos quais 449 ms dentro de `pfnPresentCb` (p90 460 ms). O DWM compõe em 60–80 ms e fica ~400 ms esperando o quadro do app. Dentro da espera há ~19 IBs de paginação em série.

**Mecanismo (P1, `bc250_active_miniport.c`):**
- Todo submit do Node 0 enfileira um KDPC imediato (`SdmaRetirementDpc`), que roda microssegundos depois, e arma um `KTIMER` de 1 ms (`KeSetTimerEx`).
- O `KTIMER` só vence no tique seguinte do relógio. Na 154, esse tique era de 15,6 ms: é o corte do histograma.
- Enquanto houver trabalho em voo, o timer se rearma em cada DPC.

## 2. O que mudou

1. **Relógio da aposentadoria do SDMA:**
   - `ExAllocateTimer(..., EX_TIMER_HIGH_RESOLUTION)` no AddDevice (PASSIVE);
   - `Bc250ActiveSdmaRetirementArm` é o único ponto que arma: `ExSetTimer` de 1 ms, ou o `KTIMER` antigo se a alocação falhar;
   - a linha `[FPS3B] aposentadoria do SDMA: timer de alta resolucao ATIVO|INDISPONIVEL` diz qual relógio vale.
2. **O callback do timer novo não aposenta nem notifica:** só enfileira o **mesmo KDPC** que o submit já usa. Assim `DxgkCbNotifyDpc` continua sendo chamado só de dentro de um KDPC nosso, o caminho validado no silício. O callback de `EX_TIMER` seria um chamador nunca medido.
3. **Ciclo de vida:**
   - o `Stop` também cancela o timer novo;
   - o RemoveDevice faz `IsActive=0`, depois `ExDeleteTimer(Cancel=TRUE, Wait=TRUE)` (que espera um callback em curso terminar), depois cancela o `KTIMER` de novo, e só então libera o contexto.

   A ordem existe porque o `StopDevice` pode devolver `STATUS_DEVICE_BUSY` **antes** de zerar `IsActive`, e o RemoveDevice segue mesmo assim. Esse risco já existia com o `KTIMER`.
4. **Telemetria GFX (só log):**
   - `submit_qpc=` no `[GfxEngine] SUBMIT SUCESSO`;
   - linha nova `[GfxRetire] Retiradas= UltimaFence= retire_qpc= FromDpc=`.

   O timer do GFX (`RetirementTimer`, periódico de 1 ms) usa o mesmo mecanismo, mas **nunca foi medido**, porque nem o submit nem a conclusão tinham horário. Por isso ele **não** foi mudado nesta faixa.

## 3. Provas offline
- `build-contract` EXIT 0 com "TODOS OS TESTES APROVADOS (0 falhas)".
- Matriz DDI PASS, auditoria de segurança PASS, auditoria de no-op PASS.
- Build de laboratório EXIT 0, com `/W4 /WX` sem aviso novo.
- **`dumpbin /imports`** do SYS: `ExAllocateTimer`, `ExSetTimer`, `ExCancelTimer` e `ExDeleteTimer` importados.
- **Teste novo** no `active_miniport_test`, no caso já existente de RemoveDevice com `IsActive=1` e fence reservada:
  - o timer foi alocado com `EX_TIMER_HIGH_RESOLUTION`;
  - depois do RemoveDevice, todos os alocados foram apagados, e **todos** com Cancel e Wait.

  As outras duas suítes que linkam o miniport ganharam stubs que devolvem NULL, então exercitam a reserva pelo `KTIMER`.
- **Portão `audit_fps3b_sdma_retire_high_resolution_timer`**, com três mutações e três reprovações:
  - callback chamando `DxgkCbNotifyDpc`;
  - `ExDeleteTimer` sem esperar;
  - `Stop` sem `ExCancelTimer`.
- **Correção no caminho:** a primeira versão movia o corpo do DPC para uma função nova, e os portões existentes (retirada com `FromDpc=TRUE` dentro da rotina de DPC; o log do submit GFX logo após o spinlock, DX.35) reprovaram. Desfiz a refatoração em vez de afrouxar os portões. O corpo do DPC está idêntico ao validado.
- Conferido por string no SYS assinado: `[FPS3B]`, `[GfxRetire]`, `submit_qpc=`, `retire_qpc=` e `DX95 Node`.

| Artefato (3.306.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `87C6377BF20BD8A5E8CED1A1BF81B27A80921BAAE9BCE9FC8861BAA3CF3E5C9E` |
| `bc250mcdm.sys` fonte lab | `A32EE92824DC9C10C06238938286DDCF7AF62E273106C9F8EBB46564E504CCE2` |
| `bc250umd.dll` (igual à 149–154) | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `9D1AFB41FA5A6839F9952EAEE4BFCF661BCDA8E271025ACEB349AEE258366CAB` |
| `bc250mcdm.cat` | `67FD5DF081F99D3AC6D74858BCF5BA262C2043062E40FEAF76A87A0C44155230` |

O pacote leva também a DX.94 e a DX.95.

## 4. Previsão (escrita antes, para poder ser refutada)
- **submit → conclusão:** p50 ≤ ~1,5 ms, com o histograma concentrado em 0–2 ms e **sem** a massa de 3–15 ms.
- **Ciclo de paginação:** de ~14,2 para ~8,5 ms, ~1,6× mais IBs por segundo. A parte do VidMm e dos FLUSH_TLB não muda.
- **Harness:** economia de ~5–6 ms × ~19 IBs ≈ 100–115 ms por quadro, isto é, de 584 para ~470 ms, **~2 FPS**. Não se espera um salto grande: o resto está no VidMm e no FLUSH_TLB.

## 5. Critério da Corrida 155

- **Runner:** `tools\EXECUTAR_CORRIDA155_FPS3B_600S.cmd` (preparo `tools\prepare-corrida155-fps3b.ps1`), sem KD, harness atrasado 150 s, 600 s.
- **Antes:** `BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset` e boot frio com corte de energia.
- **Análise:** `python tools/analyze-fps3-retire.py <log do KMD> <log da UMD do harness>`.
- **Linha de base:** a 154 para a latência, e a 153 para o harness.

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A** | `[FPS3B] ... ATIVO`; SDMA submit → conclusão com **p50 ≤ 2 ms e p90 ≤ 4 ms**; sem BugCheck; harness 60/60 com ms por quadro medido | O tique era o custo da conclusão. O FPS do harness diz quanto isso valia no quadro; o próximo gargalo é o que sobrar do ciclo (VidMm/FLUSH_TLB, FPS.2). |
| **B** | `[FPS3B] ... ATIVO`, mas o histograma ainda plano até ~15 ms | O timer novo não está vencendo no prazo, ou o KDPC atrasa. Olhar `SdmaRetirementHrFires` e a distribuição por `FromDpc`. |
| **C** | `[FPS3B] ... INDISPONIVEL` | `ExAllocateTimer` falhou no hardware. O ensaio não aconteceu; entender por quê antes de repetir. |
| **D** | Regressão: BugCheck 0x133 (DPC_WATCHDOG), 0xD1, TDR novo ou tela preta | Reverter a FPS.3b. O 0x133 é o risco conhecido de DPC frequente demais, e o doc 261 registra essa classe. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Observações que não entram nos desfechos:
- a latência do GFX (`[GfxRetire]`), medida pela primeira vez: define se vale mudar também o `RetirementTimer`;
- as linhas `DX95 Node` e as trocas do VMID8 (estabilidade da DX.95);
- a trava dura, caso volte.

**NADA DA FPS.3b FOI VALIDADO EM HARDWARE.**
