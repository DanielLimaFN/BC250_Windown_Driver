# Corrida 127 — Candidato DX.80b: Ensaio de Resistência em Janela Longa de 600 s (10 Minutos)

> **Data:** 25/09/2026  
> **Candidato:** DX.80b / `3.285.1.0` (INF fonte `3.285.0.0`)  
> **Objetivo:** Submeter o pipeline de aceleração WDDM consolidado (coalescência de draws, unificação destile, damage rects diretos, staging ring de texturas e cache de framebuffers de swapchain sem flushes) ao teste de resistência em regime contínuo de **600 segundos (10 minutos)**. O ensaio busca verificar se o driver supera a barreira histórica de travamento que ocorria entre 3 e 6 minutos nas Corridas 108–112, confirmando ausência de vazamento de memória em buffers de staging e integridade térmica/operacional sustentada.

---

## 1. Contexto Histórico das Janelas Longas (600 s)

- Nas Corridas 108 a 112 (ensaio de 600 s):
  - **Corrida 108:** travamento em ~6 min (Radeon Software ativa).
  - **Corrida 109:** travamento em ~3 min (sem Radeon Software).
  - **Corrida 110:** travamento em ~6 min (conflito de segmento e TMR).
  - **Corrida 111:** travamento em ~6 min 17 s (segmento isolado, causa aberta em paginação/flush TLB).
  - **Corrida 112:** travamento em ~6 min 53 s (camadas de migalhas MMIO).
- A partir da Corrida 113, as sessões foram mantidas em **180 s** para estabilizar o pipeline de rendering da UMD, eliminar os gargalos de sincronização e implementar o anel de staging e o cache de RTV.
- Com as Corridas 123, 124 e 126 provando estabilidade total em 180 s com **zero flushes desnecessários** e **99,7% de cache hits**, o driver retorna à janela longa de **600 segundos** com um pipeline drasticamente mais leve e eficiente na CPU e na GPU.

---

## 2. Identificação do Pacote e Hashes de Controle

- **DriverVer Empacotado:** `09/25/2026,3.285.1.0` (INF fonte `3.285.0.0`)
- **Perfil:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `P1 PERSISTENT SUBMISSION`
- **Hashes SHA-256:**
  - `bc250mcdm.sys`: `C100449DC9A36E2F07E3C5739D0D5E684FDBD5A8C9A25299CB9BB3394CCD406D`
  - `bc250mcdm.inf`: `12F5B713D97B482361BD3379745B394BE1269B9B2B6FC25279389DC3510F43F9`
  - `bc250mcdm.cat`: `107CE5904E75D8E5FA353B0395FD4DAFE08DD57F23AD0697865F914A2A0F6D92`
  - `bc250umd.dll`: `E2DE8A312C8BAAF0487F3E10148B121CE8003EDDB3D80794ED949238992BF918`
  - RADV externo: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- **Validação Offline:**
  - `python tools\audit-bc250mcdm-safety.py`: **PASS**.
  - `powershell tools\audit-bc250mcdm-lab-package.ps1`: **PASS**.
  - `python tools\test-dx80-rtv-cache-mutations.py`: **PASS (8/8 mutações reprovadas)**.
  - `powershell tools\prepare-corrida127-dx80b-600s.ps1`: **PASS**.

---

## 3. Critérios de Avaliação da Corrida 127 (600 s)

| Desfecho | Critério | Interpretação |
|---|---|---|
| **A (Avanço Pleno — Marco de 10 Minutos Conquistado)** | (1) Janela de **600 s completa** sem travamento, congelamento, crash de DWM, TDR ou BugCheck;<br>(2) Desktop legível, interativo e sem artefatos ao longo de todos os 10 minutos;<br>(3) Conformance harness com 60/60 `PRESENT_OK` na fase inicial;<br>(4) Log do DWM comprova sustentação do cache de RTV (`FollowHitCount` dominante, zero flushes síncronos de RTV) e do staging ring sem vazamento de memória;<br>(5) Rollback online restaura `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot (R1) às 10 minutos. | Marco histórico conquistado: superação da barreira de 6 minutos e validação de regime contínuo de 10 minutos no Windows 10 acelerado. |
| **B (Avanço Parcial — Superação dos 6 Minutos com Parada Tardia)** | O sistema sobrevive além de 360 s (6 minutos), demonstrando ganho sobre as Corridas 108–112, mas sofre parada ou congelamento antes dos 600 s. | Avanço temporal comprovado, permitindo capturar o dump de kernel da parada tardia para isolar a causa remanescente. |
| **C (Falha por Congelamento Histórico entre 3 e 6 Minutos)** | Congelamento de tela ou travamento duro entre 180 s e 360 s, similar às Corridas 108–112. | A causa do travamento histórico permanece ativa independentemente da carga da UMD; acionamento imediato de dump via teclado. |
| **D (Falha Operacional Imediata / Regressão)** | Queda do DWM ou BugCheck antes dos 180 s. | Regressão operacional imprevista. |

---

## 4. Procedimento Seguro de Execução

1. **Reinicialização a Frio com Corte de Energia:**
   - Desligar completamente o host, aguardar 10 segundos para descarga capacitiva da VRAM GDDR6 e ligar novamente.
2. **Executar o Runner Preparado:**
   - Abrir o prompt de comando elevado (Administrador) e executar:
     ```cmd
     tools\EXECUTAR_CORRIDA127_DX80B_600S.cmd
     ```
3. **Observação e Interação Durante os 10 Minutos:**
   - Acompanhar a execução do triângulo na janela do harness (~50 primeiros segundos).
   - Após a conclusão do harness, o desktop permanecerá ativo e acelerado.
   - O operador pode mover o mouse, alternar janelas ou deixar o sistema em repouso observando a estabilidade da tela até que o watchdog realize o rollback automático aos 10 minutos (600 s).
4. **Procedimento em Caso de Travamento:**
   - Caso o sistema congele entre 3 e 6 minutos (ou a qualquer momento):
     - Segure o **CTRL DIREITO** e pressione **SCROLL LOCK duas vezes** para gravar o dump de memória (`MEMORY.DMP`).
     - Se o teclado responder e gerar a tela azul de dump manual (BugCheck 0xE2), aguardar a gravação de 100% e reiniciar.
     - Se o teclado não responder, aguardar 30 segundos e pressionar o botão de reset (sem desligar a chave da tomada) para preservar os registradores de hardware.
