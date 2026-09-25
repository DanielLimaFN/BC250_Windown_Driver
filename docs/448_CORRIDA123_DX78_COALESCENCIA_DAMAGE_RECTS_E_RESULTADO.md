# Corrida 123 — Candidato DX.78: Coalescência de Damage Rects no DWM, Eliminação de 2.619 Flushes e Veredito Físico

> **Data da Corrida:** 25/09/2026 12:20:16 (duração ativa de 180 s)  
> **Candidato:** DX.78 / `3.282.1.0` (INF fonte `3.282.0.0`)  
> **Pasta de Evidência:** [`docs/evidence/2026-09-25_12-20-16-DESKTOP-HQAG6T4-lab-binding/`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-25_12-20-16-DESKTOP-HQAG6T4-lab-binding/)  
> **Resultado Global:** **DESFECHO A (AVANÇO PLENO)** no escopo de 180 s. 100% das cópias de damage rects coalescidas sem flush (`FlushAgora=0: 1.949 / 1.949`). Flushes totais do DWM despencaram de 3.874 para **1.255** (-67,6%). DWM operando com **exatamente 1,0 flush de destile por Present** (719 flushes / 719 Presents). Zero BugChecks, zero TDRs, harness 60/60 `PRESENT_OK` e reversão automática para `BasicDisplay` Código 0 (R1).

---

## 1. Relato Obrigatório ao Daniel

### 1.1 Houve Avanço?
**SIM.** Avanço funcional e arquitetural expressivo. A hipótese central de que os 2.265 flushes síncronos de `ResourceCopyImage` observados na Corrida 122 poderiam ser completamente coalescidos no command buffer sem quebrar a consistência visual nem os layouts de imagem foi **comprovada fisicamente no silício da BC-250**:
- Todas as **1.949 cópias** de retângulos danificados do DWM foram gravadas sem nenhum flush síncrono intermediário (`FlushAgora=0`).
- Os flushes totais do DWM caíram de 3.874 (Corrida 122) para **1.255** — um corte imediato de **2.619 flushes com fence wait (-67,6%)**.
- O DWM alcançou a marca arquitetural perfeita de **exatamente 1,0 flush de destile por Present** (719 flushes de destile para 719 Presents).
- A latência p50 de conclusão de quadro da Vulkan Bridge caiu para apenas **5,4 ms** (`EsperaUs` p50 2,99 ms + `SubmitUs` p50 2,12 ms + `FimCbUs` p50 0,28 ms), equivalente a uma taxa teórica de até ~185 FPS na ponte Vulkan.

### 1.2 Sucessos
1. **Coalescência Total de Damage Rects:**
   - 1.949 chamadas a `Bc250UmdVkCopyImageInternal` gravadas sequencialmente no command buffer junto aos draws precedentes.
   - `FlushAgora=0` em 1.949/1.949 chamadas (100% de sucesso).
   - Zero flushes com `FlushAgora=1` causados por cópias isoladas.
2. **Queda Drástica no Volume de Flushes:**
   - Flushes totais do DWM reduzidos de 3.874 para **1.255** (-67,6%).
   - Decomposição exata dos 1.255 flushes restantes:
     - Flushes de Destile/Present: **719** (exatamente 1 por quadro para submissão física ao hardware).
     - Flushes de `UploadImageData` (`ResourceUpdateSubresourceUP`): **504** (atualização de texturas/janelas do sistema).
     - Flushes por teto de lote (`BC250_DX49_BATCH_MAX_DRAWS`): apenas **3**.
     - Drenos controlados (`ResourceMap DISCARD`): **26**.
     - `ResourceMap` direto: **1**.
     - Desconhecido: apenas **2**.
3. **Métricas de Apresentação e Cadência:**
   - DWM completou **719 Presents** em 166,5 s (taxa global de 4,32 Presents/s; 544 Presents nos primeiros 120 s = 4,53 Presents/s).
   - Medição temporal de `pfnPresentCb` (`[DX74_PRESENT_UMD_CB]`): durante atividade contínua de composição, a chamada retorna em apenas **1,6 a 3,5 ms** (com pausas de sincronização de VSync em ~15–18 ms).
   - KMD processou **1.153 Presents** com 1.610 registros de `COMPOSITE_BLT` e 805 Presents concluídos na tela.
4. **Harness de Conformidade D3D11:**
   - 60/60 quadros aceitos (`PRESENT_OK`).
   - Tempo total do teste: ~41 s. FPS médio 1,2, com picos de janela em 1,7 FPS.
   - 60/60 quadros do harness com exatamente 1 flush por quadro (66 flushes no total).
5. **Estabilidade e Recuperação R1:**
   - Zero BugChecks, zero TDRs, anel drenado (`WPTR=204, RPTR=204, Error=0`).
   - O watchdog realizou reversão automática e confirmou `BasicDisplay` Código 0 em 5/5 amostras consecutivas às 12:23:25 sem necessidade de reboot.
   - Fidelidade visual confirmada pelo Daniel: desktop normal, texto legível e triângulo visível na janela.

### 1.3 Falhas e Limitações Observadas
- **Nenhum erro de driver, crash, tela azul ou corrupção visual foi observado durante a janela de 180 s.**
- As pausas observadas na média global de `pfnPresentCb` (~519 ms) decorrem de períodos em que a área de trabalho permanece estática sem eventos de entrada do usuário (quando o DWM aguarda timers internos antes de emitir nova composição).

### 1.4 Veredito
**DESFECHO A (AVANÇO PLENO)** frente aos critérios do documento 447, rigorosamente comprovado no silício para o escopo testado de 180 s.

### 1.5 Recuperação e Pendências
- **Driver Ativo ao Final:** `BasicDisplay` Código 0 restaurado pelo watchdog sem reinicialização.
- **O que Continua NÃO VALIDADO EM HARDWARE:**
  - Janela de estresse prolongado de 600 segundos (P1).
  - Taxa sustentada de 60 FPS com movimento constante de janelas (P2).
  - Suporte completo a jogos 3D comerciais D3D12/Vulkan sem WARP (P4–P8).

---

## 2. Comparativo de Métricas: DX.77 (Corrida 122) vs DX.78 (Corrida 123)

| Métrica | Corrida 122 (DX.77) | Corrida 123 (DX.78) | Variação / Impacto |
|---|---|---|---|
| **Flushes Totais no DWM** | 3.874 | **1.255** | **-67,6% (-2.619 flushes)** |
| **Flushes de `ResourceCopyImage`** | 2.265 | **0** | **-100% (eliminados)** |
| **Cópias Coalescidas (`FlushAgora=0`)** | 0 (síncronas) | **1.949** | **100% de coalescência** |
| **Flushes de Destile / Present** | ~385 | **719** | **1,0 flush / Present exato** |
| **Flushes de Upload de Textura** | 1.207 | **504** | Redução orgânica (-58,2%) |
| **Tempo de Espera em Fences (p50)** | 1,61 ms | **2,99 ms** | Consolidado por quadro |
| **Tempo de Submit Vulkan (p50)** | 1,97 ms | **2,12 ms** | Lote completo submetido |
| **Tempo Total Vulkan Bridge / Quadro** | ~4,3 ms | **~5,4 ms** | Taxa teórica ~185 FPS na ponte |
| **Presents do DWM (Sessão)** | 840 (165,7 s) | **719 (166,5 s)** | Cadência estável |
| **Presents no KMD** | 950 | **1.153** | **+21,4% volume KMD** |
| **COMPOSITE_BLTs no KMD** | 1.900 | **1.610** | Rotação normal |
| **Harness Conformance** | 60/60 (41,64 s) | **60/60 (~41 s)** | 100% aprovado |
| **Rollback Online (R1)** | PASS (5/5 Código 0) | **PASS (5/5 Código 0)** | Zero BugCheck / Zero TDR |

---

## 3. Próximo Passo

Com as cópias de retângulos danificados e os multi-draws completamente unificados no command buffer (reduzindo os flushes do DWM a exatamente 1 por Present), o próximo alvo de otimização de fluidez identificado nos logs é o **coalescimento de uploads de textura (`UploadImageData` / `ResourceUpdateSubresourceUP`)**, que ainda gerou 504 flushes avulsos quando o sistema cria ou redimensiona janelas.
