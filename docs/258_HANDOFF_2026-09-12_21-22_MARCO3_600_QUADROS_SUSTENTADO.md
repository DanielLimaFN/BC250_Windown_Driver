# docs/258 — Corrida 21:22: Marco 3 Sustentado — 600/600 quadros Vulkan no silício, 0 TDR, DWM contido

**Corrida:** `2026-09-12_21-21-51-DESKTOP-HQAG6T4-lab-binding`  
**Candidato executado:** LAB `3.161.1.0` com `-EnableD3D11LabPromotion`  
**Desfecho visual:** Animação contínua e sustentada dos 600 quadros no monitor físico (~22 FPS), confirmada visualmente pelo usuário: *"perfeito rodou novamente aquela animacao aparentimente sem problemas"*.  
**Recuperação:** Watchdog e rollback incondicional restauraram `BasicDisplay`, Código 0, limpo.

---

## 1. O que foi provado no silício (P0)

1. **Execução e Conclusão Integral dos 600 Quadros (`swapchain_completed: True`)**:
   - `P1SubmitCalls`: **600**
   - `P1SubmitAccepted`: **600**
   - `P1RetiredTotal`: **600**
   - `V2TrackerPushedTotal`: **600**
   - `V2NotifiedTotal`: **600**
   - `V2NotifyBatches`: **600**
   - `P1LastWptrPublished`: **7200**
   - `P1LastRptrMmio`: **7200**
   - `ring_drained`: **True**
   - `first_failed_frame`: **-1** (`failure_mode: NONE`)
   - `process_exit_zero`: **True**

2. **Neutralização da Parede do DWM**:
   - Na corrida anterior (21:10), sem `D3D11LabPromotion`, o `dwm.exe` sofreu 7 falhas consecutivas (`0x887A0004`), forçando o Windows a parar o dispositivo no quadro 367 (`ExecutionState=4`).
   - Com `-EnableD3D11LabPromotion`, a UMD atendeu `OpenAdapter10_2` (`0x000B000B`), entregou os caps 3D e inicializou o dispositivo D3D10/11 para o `dwm.exe` (pid 10628, log de 40.130 linhas).
   - `status_device_removed`: **False** — o adaptador permaneceu operacional até o final do teste.

3. **Zero TDRs e Apresentação 100% por GPU**:
   - `tdr_events`: **0**
   - `host_cpu_copy_logged`: **0**
   - `no_host_copy_fallback`: **True**
   - `sdma_blt_executed`: **True**
   - `live_kernel_events`: **0**
   - O motor SDMA apresentou todos os quadros para o scanout herdado do GOP sem nenhuma cópia por CPU.

4. **Verificação de Pixels (Byte-Proof)**:
   - 8 de 8 zonas de amostragem conferidas na GPU: White, Yellow, Cyan, Green, Magenta, Red, Blue e Center Badge (`0xFFBC2501`) aprovados.

---

## 2. Métricas do Ensaio

| Métrica | Valor Medido |
|---|---|
| Quadros renderizados | 600 / 600 |
| Duração do swapchain | 27,42 segundos |
| Taxa de quadros média | 21,9 FPS |
| Submissões aceitas no MEC | 600 |
| Aposentadorias no MEC | 600 |
| WPTR final / RPTR final | 7200 / 7200 (anel completamente drenado) |
| TDRs observados | 0 |
| Falhas do harness | 0 (Exit 0) |

---

## 3. Próximo Passo

Com o Marco 3 sustentado e 100% provado no silício (600 quadros, 0 TDR, MEC e SDMA estáveis, e DWM estabilizado com `D3D11LabPromotion`), a fundação para aceleração gráfica direta na BC-250 está consolidada. O próximo estágio de avanço é a implementação do compilador de shaders / pipeline de rasterização 3D real (Rota B: DXBC → SPIR-V → RADV/ACO) para que o DWM e aplicações D3D11 possam realizar `DrawIndexed` por GPU em vez de depender de recusa honesta.
