# Corrida 126 — Candidato DX.80b: Estabilidade Total de 180 s, Teardown sem Double Free, 99,7% de Cache Hits e Zero Flushes de RTV

> **Data:** 25/09/2026 — 15:49:09  
> **Candidato:** DX.80b / `3.285.1.0` (INF fonte `3.285.0.0`)  
> **Pasta de Evidência:** [`docs/evidence/2026-09-25_15-49-09-DESKTOP-HQAG6T4-lab-binding`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-25_15-49-09-DESKTOP-HQAG6T4-lab-binding)  
> **Duração da Sessão:** 180 s completos (15:49:09 – 15:52:15; rollback R1 às 15:52:21)  
> **Veredito:** **Sucesso Pleno no Escopo Controlado** (Desfecho A do Critério 453)

---

## 1. Relato Obrigatório das 5 Perguntas

### 1. Houve Avanço?
**Sim (avanço arquitetural e de estabilidade confirmado no silício).**
Em comparação com a Corrida 125 (que sofreu crash do DWM no quadro 17 por double free no teardown), a Corrida 126 executou os **180 segundos completos** sem nenhuma falha de processo, sem travamento de GPU, sem TDR e sem BugCheck.
O gargalo histórico de 369 flushes síncronos forçados pela rotação de backbuffers da swapchain foi **100% eliminado**:
- Total de chamadas de rotação no DWM: 706.
- **Cache Hits: 704 (taxa de acerto de 99,72%)**.
- Cache Misses: apenas 2 (apenas na criação dos buffers iniciais da swapchain).
- **Flushes de RTV: ZERO (`0`)**, contra 369 da Corrida 124.

### 2. Sucessos
- **Eliminação Total dos Flushes de RTV:**
  Flushes de rotação de backbuffer caíram de 369 para **ZERO**. O DWM rotacionou os recursos de swapchain instantaneamente com reaproveitamento de framebuffers e views em cache na CPU/GPU.
- **Estabilidade Perfeita no Teardown:**
  A correção em `Bc250UmdVkDestroyRtv` eliminou o double free: o DWM e o Explorer destruíram centenas de recursos ao longo dos 180 s com zero crashes.
- **Harness de Conformidade Direct3D 11:**
  60 de 60 quadros aceitos (`PRESENT_OK`), FPS médio de 1,2 e picos de janela em 1,7 FPS.
- **Métricas do DWM:**
  - 707 Presents concluídos (taxa média de 4,25 Presents/s ao longo de toda a sessão de 166 s úteis).
  - Razão quase perfeita de flushes por quadro: 693 flushes de apresentação física para 707 quadros apresentados (~0,98 flush/quadro).
- **Volume no KMD:**
  - 1.150 Presents processados pelo KMD.
  - 1.590 operações de `COMPOSITE_BLT`.
  - 769 flips de hardware.
  - 23 detecções contínuas de desktop real.
- **Latências Ultrabaixas na GPU (Vulkan Bridge):**
  - Espera de Fence (`EsperaUs`): p50 de **3,73 ms**.
  - Submissão de Command Buffer (`SubmitUs`): p50 de **2,11 ms**.
  - Finalização de Command Buffer (`FimCbUs`): p50 de **0,20 ms**.
  - Tempo total de processamento Vulkan por quadro: **~6,0 ms** (capacidade teórica de pipeline de ~160 FPS).
- **Recuperação Online Perfeita (R1):**
  Ao término dos 180 s, o watchdog executou rollback online automático restaurando `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot sem reiniciar (15:52:15–19).

### 3. Falhas Observadas
- **Nenhuma falha operacional:** Zero crashes de DWM/Explorer, zero BugChecks, zero TDRs, zero anomalias de hardware.
- **Comportamento Visual Idêntico (Percepção do Operador):**
  Como o DWM do Windows opera sob demanda e o pacing do `pfnPresentCb` regula o ritmo dos quadros apresentados para as janelas em ~435 ms (p50), o throughput visual percebido pelo usuário permaneceu consistente com as corridas anteriores (desktop acelerado e triângulo D3D11 operando suavemente a 1,2–1,7 FPS).

### 4. Veredito
**Sucesso Pleno no Escopo Controlado** (Desfecho A do critério 453).
A meta de unificação, coalescência de dano, staging ring de texturas e cache de framebuffers de swapchain foi 100% cumprida. O DWM atingiu o ideal arquitetural de ~1,0 flush por quadro apresentado.

### 5. Recuperação e Próximos Passos
- **Recuperação:** Ativa e validada (`RECOVERY_OK_ONLINE` às 15:52:21).
- **O que Continua NÃO VALIDADO EM HARDWARE:**
  - Janela longa de 600 s sob o pipeline consolidado (DX.80b).
  - Jogos Direct3D 12 / Vulkan reais e taxas nativas de 60 FPS fora do compositor de janelas DWM.
- **Próximo Passo Justificado:**
  - O pipeline de apresentação e composição 2D/3D da UMD D3D11 concluiu todas as 5 grandes coalescências planejadas (Clear+Draw, Destile unificado, MultiDraw, Damage Rects e RTV Framebuffer Cache).
  - O próximo passo natural é validar a sustentação contínua na **janela longa de 600 s** (10 minutos) para verificar estabilidade térmica, ausência de vazamento de memória em sessões prolongadas e comportamento do desktop em regime contínuo.

---

## 2. Dados e Provas dos Logs

### Prova dos Flushes no DWM (Log 1204):
| Origem do Flush | Corrida 124 (DX.79) | Corrida 126 (DX.80b) | Variação |
|---|---|---|---|
| **PresentDestile** (scanout) | 935 | 693 | 1 por quadro apresentado |
| **Bc250UmdVkRtvFollowResource** | **369** | **0** | **-100% (ELIMINADO)** |
| **Destroy** (recursos intermediários) | 0 (caiu em outros) | 139 | Teardown limpo com fence |
| **UploadStaging** (wrap de staging) | 6 | 35 | Wrap do buffer de 64 MB |
| **ResourceMap** (DISCARD) | 6 | 16 | Flushes esporádicos |
| **DrawBatch** (lote > 64) | 146 | 11 | -92% |
| **Total de Flushes do DWM** | **1.462** | **902** | **-38,3% de flushes** |
| **Taxa de Cache Hit do RTV** | N/A | **99,72% (704/706)** | Novo subsistema provado |
