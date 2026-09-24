# Corrida 91 — 53/60, volta do anel provada; o TDR que sobra é preempção sem resposta com a GPU ociosa

> 23/09/2026. Bind às 18:40:28, janela de 300 s (confirmada:
> `timeout_seconds=300`), prazo às 18:45:29.
> - Recuperação online incompleta, reboot automático, `RECOVERY_OK_ONLINE` às
>   18:46:52 no segundo boot.
> - Sem BugCheck.
>
> **Candidato:** o mesmo da 90, lab `3.251.1.0`, SYS `BE60274C…` (conferido).
>
> **Critério:** [docs/382](382_CRITERIO_CORRIDA91_DX46_JANELA300.md).
>
> **Evidência:** `docs/evidence/2026-09-23_18-40-28-DESKTOP-HQAG6T4-lab-binding/`,
> FINAL `complete_at_capture=true`, coleta RADV pós-rollback presente.

## 1. Houve avanço?

**Sim, o maior da série.**
- **Harness 53/60** `PRESENT_OK`. A série tinha 0–16/60, e a 90 fez ≥20 sem
  falha, cortada pelo tempo.
- **Volta completa do anel de 64 KB provada.**
- Um TDR, no quadro 54.

| Medida | 90 | **91** |
|---|---|---|
| Harness | ≥20/60, cortado em 60 s | **53/60**, device removed no quadro 54 |
| TDRs | 0 | 1 (classe "ociosa") |
| WPTR máximo | 15720 | **19080** (passou de 16384) |
| Submissões GFX | 680 | **1316** |
| Trocas com aquecimento DX.43 | 165 | **409** |
| FPS do harness | ~0,5 | 0,5 → **1,0** no fim |
| Autotestes RADV 5/5 | 3/3 | 3/3 |

## 2. Sucessos

- **Controle:** `CntlPlanned=0x50000B0D`, janela 300 s. O candidato é o
  esperado.
- **Anel (P0):**
  - o submit que cruzou 8192 (fence 327) **e** o que cruzou **16384** (fence
    594, WPTR 16436) concluíram;
  - a GPU aposentou até a fence 681 depois disso;
  - zero `NAO_CONVERGIU`;
  - no TDR, `RPTR=2696` = 19080 − 16384: o CP estava exatamente no WPTR
    reduzido pelo anel de 64 KB.
  - **A DX.46 fica validada nas duas voltas.**
- **DX.43, sexta amostra:** 409/409 aquecimentos, zero TDR de VMID anterior e
  zero `Cid=9`. Somando 86–91, são 939 trocas.
- 3 `BYTE_PROOF PASS` de tela cheia. Zero `[CP_RESET_DETECTED]`.

## 3. Falhas

**Um TDR com a GPU ociosa (P0), idêntico ao da Corrida 86:**

| Campo | 86 | 91 |
|---|---|---|
| Anel | `RPTR==WPTR` (5244) | `RPTR=2696` = `WPTR 19080 mod 16384` |
| Fences | 188/188 aposentadas | tudo aposentado, `TDR_FENCE_RECONCILE Committed=0` |
| Ondas | 0 | 0 |
| ME / PFP | `PFP_SYNC_ME` / opcode `0x0E` | `PFP_SYNC_ME` / opcode `0x0E` |
| `CP_BUSY_STAT` | `GfxContextBusy=1` | `GfxContextBusy=1` |
| `IB1` | IB do harness `0x240030000` | IB do harness `0x240030000` |
| Preempção | DDI era muda (não medida) | **`[PREEMPT_COMMAND] #1` antes do TDR**, sem resposta |

- **Consequência:** o harness recebeu device removed (`0x887A0020`) no quadro 54.
- **Visual:** sem relato nesta corrida; desktop e interação **NÃO MEDIDOS**.
- **Recuperação:** online incompleta, com reboot, como na 90.

## 4. Veredito frente ao critério

- **Desfecho C, com todos os sinais:** o harness falhou antes de 60, com TDR.
  - **Porém o WPTR passou de 16384 com sucesso**, então o critério de C
    ("WPTR abaixo de 16384") não se aplica literalmente.
  - Pela regra "descrever antes de julgar", fica registrado como **E**: a volta
    do anel foi provada e o TDR é de outra classe.
- **Função: falha** (53/60), mas a mais perto do objetivo até aqui.

**Leitura do TDR:**
- **P0:**
  - a GPU estava ociosa, sem trabalho nosso pendente;
  - a única coisa pendente era o pedido de preempção do agendador;
  - o driver **nunca** reporta `DXGK_INTERRUPT_DMA_PREEMPTED` (busca no driver
    inteiro, docs/374).
- **P1 (contrato WDDM):** depois de `DxgkDdiPreemptCommand`, o driver precisa
  concluir a preempção com `DMA_PREEMPTED`. Com a GPU ociosa, a conclusão é
  imediata (nada a preemptar).
- **Conclusão (alta confiança, 2 casos: 86 com a DDI muda, 91 com a DDI
  medida):** o agendador esperou a conclusão que nunca veio e declarou TDR.
  - É a dívida da Lei 4 da DX.44 (`STATUS_SUCCESS` sem backend), agora com
    custo medido.

## 5. Próximo passo proposto — DX.47: concluir a preempção

**Uma variável:** implementar a conclusão da preempção para a granularidade
declarada (`DMA_BUFFER_BOUNDARY`):
- `PreemptCommand` guarda o pedido por nó (`PreemptionFenceId`);
- quando o nó estiver drenado (imediatamente, se já estiver; senão, no
  retirement em que ficar), o driver notifica `DXGK_INTERRUPT_DMA_PREEMPTED`
  com:
  - `PreemptionFenceId` = o pedido;
  - `LastCompletedFenceId` = a última fence de submissão concluída;
  - `NodeOrdinal` / `EngineOrdinal`;
- deixar os pacotes já no anel terminarem é legal para essa granularidade: com
  tudo concluído, nada é reenviado.

**Critério:**
- `[PREEMPT_COMMAND]` seguido de `DMA_PREEMPTED notificado`;
- nenhum TDR ocioso;
- harness 60/60 na janela de 300 s.

## 6. Recuperação

- **Estado final:** `RECOVERY_OK_ONLINE` às 18:46:52. O marcador de boot frio
  foi regravado. **Desligamento completo antes de outra corrida.**
- **NÃO VALIDADO EM HARDWARE:**
  - 60/60;
  - conclusão da preempção;
  - desktop interativo;
  - causa da lentidão;
  - o travamento da 88.
