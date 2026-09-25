# Corrida 124 — Candidato DX.79: Coalescência de Uploads de Textura no Staging Ring Buffer e Análise de Gargalo

> **Data do ensaio:** 25/09/2026 14:32:48 – 14:35:59  
> **Candidato:** DX.79 / `3.283.1.0` (INF fonte `3.283.0.0`)  
> **Perfil:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `P1 PERSISTENT SUBMISSION`  
> **Pasta de evidência:** [`docs/evidence/2026-09-25_14-32-48-DESKTOP-HQAG6T4-lab-binding`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-25_14-32-48-DESKTOP-HQAG6T4-lab-binding)  
> **Veredito:** **DESFECHO B (AVANÇO ARQUITETURAL COMPROVADO / GARGALO ISOLADO)** frente ao [critério 449](449_DX79_OTIMIZACAO_UPLOAD_TEXTURAS_E_CRITERIO_CORRIDA124.md).

---

## 1. Resumo Executivo e Comparativo

| Métrica | Corrida 121 (DX.76) | Corrida 122 (DX.77) | Corrida 123 (DX.78) | Corrida 124 (DX.79) | Variação (124 vs 123) |
|---|---|---|---|---|---|
| **Flushes de Upload de Textura** | ~1.200 | ~1.200 | 504 | **6** (12 no total por wrap duplo) | **-98,8% (Eliminação quase total!)** |
| **Uploads Coalescidos (`FlushSincrono=0`)** | 0 | 0 | 0 | **666 / 672 (99,1%)** | **Ganho arquitetural pleno** |
| **Total de Flushes do DWM** | 5.438 | 3.874 | 1.255 | **1.464** | Concentrados no Present e RtvFollow |
| **Flushes por Present no DWM** | 9,69 | 4,61 | 1,74 | **1,56** | **Menor índice histórico de overhead** |
| **Presents Concluídos pelo DWM** | 561 | 840 | 719 | **935** | **+30,0% (+216 quadros)** |
| **Taxa Sustentada do DWM** | 3,12 Presents/s | 5,07 Presents/s | 4,04 Presents/s | **5,45 Presents/s** | **NOVO RECORDE HISTÓRICO** |
| **Presents no KMD / Escapes** | 660 | 950 | 805 | **995 (1.098 flips)** | **Recorde de apresentações físicas** |
| **Harness Conformance** | 60/60 quadros | 60/60 quadros | 60/60 quadros | **60/60 quadros** | 100% de aceitação |
| **Estabilidade Visual e R1** | OK / R1 Código 0 | OK / R1 Código 0 | OK / R1 Código 0 | **OK / R1 Código 0** | 5/5 amostras no mesmo boot |

---

## 2. Evidências Físicas Medidas no Silício

1. **Validação do Anel de Staging e Coalescência de Uploads:**
   - O log do DWM (`bc250_umd_log_1220-FINAL.txt`) registrou **672 chamadas a `Bc250UmdVkUploadImageData`**:
     - **666 chamadas executaram com `FlushSincrono=0`**: os bytes foram copiados para a posição calculada do anel de staging (`uploadOffset`), a barreira e o `vkCmdCopyBufferToImage` foram gravados sequencialmente no command buffer ativo, e a CPU retornou imediatamente sem emitir `FlushCommands` nem esperar fences.
     - **Apenas 6 chamadas executaram com `FlushSincrono=1`**: quando a cabeça do anel de staging atingiu o limite do buffer persistente (`wrapNeeded = TRUE`), o lote pendente foi drenado com segurança e o anel resetou para offset 0.
   - Isso eliminou mais de 500 bloqueios de CPU por espera de fences de upload, liberando ~3,5 segundos de tempo de computação da CPU no DWM.

2. **Aceleração da Taxa de Apresentação do DWM:**
   - DWM atingiu **935 Presents** ao longo de 171,48 s de sessão ativa, cravando uma taxa média sustentada de **5,45 Presents/s** (superando o pico prévio de 5,07 Presents/s da Corrida 122).
   - Nos primeiros 120 segundos, foram concluídos **499 Presents** (4,16 Presents/s).

3. **Operação do KMD e Scanout:**
   - O driver de modo kernel processou **1.990 chamadas a `COMPOSITE_BLT`** (995 apresentações completadas com SDMA blt 2560x1080).
   - O hardware registrou **1.098 rotações de buffer (`Flip=1`)** e 26 detecções de desktop real.

4. **Recuperação e Rollback Online (R1):**
   - Ao término da janela de 180 segundos, o watchdog acionou a reversão automática sem reiniciar a máquina.
   - O guardião confirmou `BasicDisplay` Código 0 em 5 amostras consecutivas no mesmo boot (14:35:53 a 14:35:57).

---

## 3. Decomposição Rigorosa dos Flushes do DWM na Corrida 124

A mineração exata de todos os 1.464 eventos `[VK_BRIDGE] FlushCommands ENTER:` em `bc250_umd_log_1220-FINAL.txt` revelou a seguinte distribuição de chamadores:

| Chamador | Flushes | % do Total | Diagnóstico |
|---|---|---|---|
| **`DXGI Present` (`DestileAndScanout`)** | **935** | 63,9% | Ótimo: exatamente 1 flush por quadro apresentado. |
| **`Bc250UmdVkRtvFollowResource`** | **369** | **25,2%** | **Gargalo dominante isolado:** flush com fence wait a cada troca de imagem do backbuffer. |
| **`Bc250UmdVkDestroyResource`** | 116 | 7,9% | Teardown de superfícies temporárias descartadas. |
| **`ResourceMap` (`WRITE_DISCARD`)** | 29 | 2,0% | Buffers dinâmicos descartados. |
| **`UploadImageData` (`wrapNeeded`)** | 12 | 0,8% | 6 eventos de wrap de anel (2 flushes por wrap). |
| **`ExecuteVkDraw` (lote > 64)** | 3 | 0,2% | Estouro de capacidade de draw batch. |
| **Total** | **1.464** | **100,0%** | **1,56 flushes por Present** |

---

## 4. O Novo Gargalo Isolado: `Bc250UmdVkRtvFollowResource`

- **Mecanismo Identificado:**
  Em [`driver/umd/bc250_umd_resource_vk.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_umd_resource_vk.c#L600):
  ```c
  if (pDevice->InRenderPass) {
      pBridge->vk.vkCmdEndRenderPass(pBridge->CommandBuffer);
      pDevice->InRenderPass = FALSE;
  }
  res = Bc250UmdVkFlushCommands(pBridge);
  ```
  Sempre que o DWM alterna os backbuffers de sua swapchain interna ou recria as imagens de swapchain do desktop, `pRtv->VkImageAtCreation != pRes->VkImg`.
  A função `Bc250UmdVkRtvFollowResource` detecta essa troca e, para criar o novo `VkImageView` e `VkFramebuffer`, ela executa um `Bc250UmdVkFlushCommands` síncrono incondicional.
- **Impacto no Desempenho:**
  Foram **369 flushes síncronos** causados exclusivamente por essa linha. Cada flush impõe `SubmitUs` (~2-5 ms) + `EsperaUs` (~3-12 ms), somando **~3 a 5 segundos de espera síncrona na CPU**.
- **Solução Arquitetural para o Candidato DX.80:**
  1. Fechar o render pass ativo se estiver aberto (`vkCmdEndRenderPass`), mas **não forçar flush nem esperar fence** caso não haja colisão de layout.
  2. Cachear os `VkFramebuffer` e `VkImageView` associados a cada `VkImage` do recurso no próprio recurso ou no RTV, evitando recriações desnecessárias.
  3. Coalescer a mudança de framebuffer no mesmo command buffer, eliminando esses 369 flushes adicionais e aproximando o DWM de **1,0 flush estrito por quadro**.
