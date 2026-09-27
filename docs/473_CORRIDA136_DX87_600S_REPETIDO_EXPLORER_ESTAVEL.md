# 473 — Corrida 136 (DX.87, repetição com boot frio): 600 s sem trava, desktop visível o tempo todo, Explorer sem quedas

> **Corrida:** 26/09/2026, bind 22:05:40 após boot frio (22:04:48), rollback online 22:15:57 no mesmo boot. **Candidato:** DX.87 / 3.292.1.0 (mesmo das 134/135). Boot `NODEBUG`.
> **Evidência:** `docs/evidence/2026-09-26_22-05-40-DESKTOP-HQAG6T4-lab-binding` (log do KMD `-FINAL` completo, `complete_at_capture=true`, 98.604.263 bytes).
> **Relato do Daniel:** "desktop visível durante todo o teste". **Critério aplicado:** o da repetição (doc 471, seção "Critério da Corrida 135"), já que a 135 não valeu como repetição.

## Relato obrigatório
**1. Houve avanço?** Sim, funcional. É a segunda janela de 600 s sem trava (134 e 136), agora com desktop visível relatado e com o Explorer estável pela primeira vez.

**2. Sucessos (P0, exceto o visual, que é relato):**
- 600 s sem trava; `RECOVERY_OK_ONLINE` com BasicDisplay Código 0 no mesmo boot.
- Desktop visível durante todo o teste (relato do Daniel).
- DWM (pid 1192) vivo de 22:05:49 a 22:15:32: **1.544 Presents `hr=0`**, `pfnPresentCb` p50 2,8 ms, p90 16 ms, p99 88 ms.
- **Explorer: uma única instância (pid 4960) a janela inteira; zero quedas de Explorer e de SearchApp no Event Log** (134: 5 e 21; 133: 4 e 10). Só 1 LiveKernelEvent (134: 6).
- `FLUSH_TLB`: 172.121 chamadas, p50 1,35 ms, p90 1,96 ms; wrapper 172.136/172.136.
- KMD: 2.185 Presents, 37 `DESKTOP REAL DETECTADO`, 5 `BYTE_PROOF PASS` no scanout.
- Harness D3D11 60/60 `PRESENT_OK`.
- Zero TDR_QUIESCE, IB_TRIPOD TIMEOUT, GFXHUB_FAULT, `DEVICE_REMOVED`; **zero `InsideIb=1`, `Blt Timeout` e `COMPOSITE_BLT FALHOU`** (a assinatura da 135 não reapareceu com boot frio).

**3. Falhas e ressalvas:**
- 473 `NO_SPACE` e 1 `PUBLICACAO_FALHA` de GPUVA, como antes.
- Um `FLUSH_TLB` de 1.136 ms (id 14294).
- **Defeito de instrumentação:** uma linha `BC250_HANG ENTER` saiu com outra linha intercalada no meio do QPC. O parser leu um "flush de 78,7 s" que na verdade durou 36 ms. As escritas direta e drenada do logger podem se intercalar dentro de uma linha. Não afeta o driver, mas afeta a medição.
- Por que o Explorer não caiu desta vez não foi estabelecido. O candidato é o mesmo da 134; a diferença pode ser de carga, das janelas abertas ou do acaso. Não atribuir a nenhuma mudança.

**4. Veredito frente ao critério da repetição (doc 471): A.** 600 s sem trava, rollback online e desktop visível relatado. A estabilidade de 600 s passa de "uma vez" para "reproduzida duas vezes" (134 e 136) com o mesmo candidato. A 135 (boot quente) terminou em BugCheck; sem boot frio, o risco do SDMA preso existe (doc 472).

**5. Recuperação e pendências:** BasicDisplay Código 0 no mesmo boot, `CLEAN_BOOT_REQUIRED` armado. **NÃO VALIDADOS EM HARDWARE:** janela maior que 10 min, jogos, 60 FPS, causa do SDMA preso da 135, Explorer estável de forma reproduzível.

## Próximos passos propostos
1. Offline: corrigir a intercalação de linhas no logger (medição confiável).
2. Offline: investigar a textura compartilhada recusada pelo runtime (causa das quedas do Explorer nas 133/134), para não depender de sorte.
3. Hardware: uma janela mais longa (30 min) com o mesmo candidato, para medir estabilidade além de 10 min antes de mudar mais coisas.
