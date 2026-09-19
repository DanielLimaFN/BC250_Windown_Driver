# docs/257 — Corrida 21:10: C3.18 validado no silicio — 368 submits, 367 blts, 0 TDR, animacao completa

**Corrida:** `2026-09-12_21-09-56-DESKTOP-HQAG6T4-lab-binding`  
**Candidato executado:** LAB `3.161.1.0`  
**Desfecho visual:** Animacao continua das barras SMPTE com badge `0xFFBC2501` em tempo real (~27.2 FPS) por 13,8 segundos, confirmada visualmente pelo usuario: *"rodou, animacao completa"*.  
**Recuperacao:** Watchdog SYSTEM e rollback incondicional restauraram `BasicDisplay`, Codigo 0, limpo.

---

## 1. O que foi provado no silicio (P0)

1. **Eliminacao completa do Freeze / TDR no Quadro 139**:
   - A separacao do offset de fence assincrona do SDMA (`0x880` via `Bc250SdmaAsyncSlotFenceOffset`) eliminou a colisao com o offset sincrono do Present Blt (`0x800`).
   - `tdr_events: 0` (zero TDRs, contra o TDR 4101 da corrida anterior).
   - O swapchain avancou de 140 quadros para **368 submissoes completas** no MEC e **367 blts completos** no SDMA.

2. **Aceleracao 100% por GPU sem copias de CPU**:
   - `host_cpu_copy_logged: 0`
   - `Fallbacks: 0`
   - `Submitted: 367, Completed: 367`
   - `BltTotal: 504488 us` (media de 1,37 ms por blt no SDMA).

3. **MEC / Node 1 e Ring Drained**:
   - `P1SubmitAccepted: 368`
   - `P1RetiredTotal: 368`
   - `V2TrackerPushedTotal: 368`
   - `V2NotifiedTotal: 368`
   - `P1LastWptrPublished: 4416`
   - `P1LastRptrMmio: 4416`
   - `ring_drained: True`

4. **Verificacao de Pixels (Byte-Proof)**:
   - 8 de 8 zonas conferidas na GPU: barras White, Yellow, Cyan, Green, Magenta, Red, Blue e badge `0xFFBC2501` aprovados.

---

## 2. Diagnostico do Termino no Quadro 367

- A animacao rodou continuamente por 13,87 segundos.
- No quadro 367, o harness encerrou com `vkWaitForFences failed on frame 367: -4` devido a `ExecutionState=4` (`D3DKMT_DEVICEEXECUTION_STOPPED`).
- Causa: O `dwm.exe` acumulou 7 falhas consecutivas (`dwm_wer_events: 7`), o que dispara a politica do Windows de parar o adaptador grafico quando a composicao de desktop nao tem suporte nativo a D3D10/11.
- Todas as 367 operacoes do SDMA e 368 do MEC ja haviam sido concluidas e aposentadas antes da parada.

---

## 3. Proximo Passo

Com o swapchain Vulkan/RADV e o SDMA 100% validados por GPU sem TDR e com o MEC estavel, o proximo bloqueador para o funcionamento sustentado sem interrupcao do sistema e o backend D3D10/11 para o `dwm.exe` (Faixa C1/C2 e VKD3D shader bridge).
