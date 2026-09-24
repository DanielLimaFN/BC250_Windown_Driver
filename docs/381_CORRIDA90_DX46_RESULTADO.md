> **CORREÇÃO (23/09, [docs/382](382_CRITERIO_CORRIDA91_DX46_JANELA300.md)):** o harness não foi cortado pelos 120 s do watchdog, mas pelo `runnerTimeout = min(180, janela−60)` = **60 s** que o binder dá ao harness com janela de 120 s.

# Corrida 90 — DX.46: anel inteiro confirmado, zero TDR, harness sem falha até o fim da janela

> 23/09/2026. Bind às 17:44:33, prazo do watchdog às 17:46:35.
> - A recuperação online ficou incompleta (o PnP não confirmou o BasicDisplay).
> - Houve reboot automático, e no segundo boot `RECOVERY_OK_ONLINE` às
>   17:47:54.
> - Sem BugCheck.
>
> **Candidato:** lab `3.251.1.0`, SYS `BE60274C…`, conferido em
> `binding-provenance.json`; UMD e RADV DX.42 inalteradas.
>
> **Critério:** [docs/380](380_DX46_ANEL_INTEIRO_E_CRITERIO_CORRIDA90.md).
>
> **Evidência:** `docs/evidence/2026-09-23_17-44-33-DESKTOP-HQAG6T4-lab-binding/`.
> - FINAL com `complete_at_capture=true`.
> - A coleta RADV pós-rollback não rodou por causa do reboot. Foi refeita
>   manualmente: 237 IB, 7 fluxos e **1 órfão**, o último IB (#93), cujo
>   `.meta` não chegou a ser escrito porque o harness foi encerrado no meio do
>   submit pelo rollback.

## 1. Houve avanço?

**Sim, o maior da série em estabilidade.**
- **Zero TDR** na corrida inteira. As 84–89 tiveram de 1 a 4 cada.
- **O harness não falhou.** Chegou a pelo menos **20/60** quadros (a linha de
  quadro sai a cada 10), `PRESENT_OK` e sem device-lost. Ainda estava
  submetendo (submit 140, 17:46:01) quando o prazo de 120 s acabou e o rollback
  o encerrou.
- Os recordes anteriores (16/60 na 87, 13/60 na 89) terminaram em device
  removed.

| Medida | 87 | 89 | **90** |
|---|---|---|---|
| TDRs | 1 | 1 | **0** |
| Harness | 16/60, falhou | 13/60, falhou | **≥20/60, sem falha, cortado pelo prazo** |
| WPTR máximo | 8248 (TDR) | 8220 (TDR) | **15720** |
| Submissões GFX | 447 | 587 | **680** (673 aposentadas) |
| Trocas com aquecimento DX.43 | 123 | 136 | **165** |
| `BYTE_PROOF PASS` de tela cheia | 2 | 2 | **3** |
| Autotestes RADV 5/5 | 3/3 | 3/3 | **3/3** |

## 2. Sucessos

- **Controle da variável (P0):** `[GfxRing] BRINGUP: CntlPlanned=0x50000B0D
  CntlReadback=0x50000B0D … ActivePassed=1`. O CP aceitou o anel de 64 KB.
- **O submit que cruzou 8192 concluiu (P0):** fence 309, WPTR 8216. Depois
  dele a GPU aposentou até a fence 673, sem TDR.
- **O CP enxerga o anel inteiro (P0):** os últimos `GFX_TRIPE_VEREDITO` têm
  `Perna1=REGISTRADOR` com `RPTR_Reg == WPTR` em 15448, 15472 e 15544 (acima
  de 8192) e fence escrita sobre poison. Zero `NAO_CONVERGIU` na corrida.
- **DX.43, quinta amostra:** 165/165 aquecimentos e zero TDR de VMID anterior.
  Somando 86–90, são 530 trocas.
- Zero `Cid=9`, zero `[PREEMPT_COMMAND]` (não houve travamento que a
  provocasse) e zero `[CP_RESET_DETECTED]`.

## 3. Falhas e limites

- **A volta real do anel não foi exercitada:** o WPTR máximo foi 15720, abaixo
  de 16384. Fica **NÃO VALIDADO EM HARDWARE**.
- **Harness incompleto:** a ~0,5 FPS, 60 quadros levam uns 2 minutos, e a
  janela de observação é de 120 s. Nesta cadência **o harness não tem como
  fechar 60/60 dentro da janela**. É limite do procedimento, e agora é o que
  corta a corrida.
- **Desempenho:** ~0,5 FPS, ou ~2 s por quadro. Ainda sem causa medida.
- **Recuperação:** a online ficou incompleta e pediu reboot; o guardião retomou
  e concluiu no segundo boot.
- **Visual:** não houve relato visual nesta corrida. O desktop e a interação
  ficam **NÃO MEDIDOS**.

## 4. Veredito frente ao critério

- **Desfecho A, primeira parte, com todos os sinais:**
  - CNTL novo confirmado;
  - o submit que cruzou 8192 concluiu;
  - `RPTR_Reg` acompanhou o WPTR reduzido por 64 KB;
  - sem `NAO_CONVERGIU`.
- **O anel pela metade era a causa dos TDRs das 87/89** (alta confiança: 2/2
  antes e 0 TDR com a correção, no mesmo regime).
- **A segunda parte (cruzar 16384)** não aconteceu, então a volta real do anel
  segue em aberto.
- **Função:** ainda falha pelo critério de 60/60 e desktop, mas **sem nenhuma
  falha de GPU medida**. O que parou o harness foi o prazo.

## 5. Recuperação e próximos passos

- **Recuperação:** `RECOVERY_OK_ONLINE` às 17:47:54, depois do reboot
  automático. O marcador de boot frio foi regravado. **Desligamento completo
  antes de outra corrida.**
- **NÃO VALIDADO EM HARDWARE:**
  - volta do anel em 16384;
  - 60/60;
  - desktop interativo;
  - causa da lentidão (~2 s por quadro);
  - causa do TDR ocioso da 86 e do travamento da 88.

**Candidatos, em ordem:**
1. **Janela de observação maior** (procedimento, não driver): subir o
   `-WatchdogTimeoutSeconds` do wrapper de 120 para ~300 s, mesmo candidato.
   Com isso o harness pode fechar 60/60 e o WPTR cruza 16384, o que exercita a
   volta real do anel. É a corrida mais barata e mais informativa agora.
2. **Lentidão:** medir onde vão os ~2 s por quadro (esperas de fence na ponte
   Vulkan, cópias de Present, submits por draw) com os tempos que já estão no
   log.
3. **Preempção de verdade** (dívida da Lei 4) e retângulos sujos recusados.
