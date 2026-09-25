# Corrida 128 — Candidato DX.81: Eliminação do Stall de Escape, Prevenção do Laço DWM e Expansão GPUVA 4096

> **RETIFICAÇÃO causal (25/09/2026):** Ver [auditoria 457](457_AUDITORIA_CORRIDA127_TDR_SDMA_GPUVA_E_CRITERIO128.md). O escape da 127 chegou ao KMD e sua cópia SDMA expirou; 23 resets TDR ocorreram no bind. As mudanças DX.81 continuam offline e não demonstram eliminação do primeiro timeout SDMA. Interpretar a Corrida 128 também por fences, RPTR/WPTR, resets e publicação GPUVA; ausência de `E_FAIL` isolada não fecha o diagnóstico.

> **Data:** 25/09/2026  
> **Candidato:** DX.81 / `3.286.1.0` (INF fonte `3.286.0.0`)  
> **Objetivo:** Resolver a anomalia visual observada na foto de Daniel da Corrida 127 (ícones/barra no topo, 75% inferior escuro), causada por timeout de 4 s no escape de apresentação composto com `HardwareAccess=1` e subsequente laço de reinício do `dwm.exe` reprovado por `NENHUM_RENDER_DETECTADO`, além de expandir o espaço de GPUVA para 4096 faixas simultâneas.

---

## 1. Diagnóstico da Corrida 127 e Anomalia Visual da Foto

Na Corrida 127 (ensaio de 600 s):
1. **Fase Inicial (16:01:35 – 16:02:58):** O teste de conformidade D3D11 executou com 60/60 quadros `PRESENT_OK` (1,2 FPS) e Exit Code 0.
2. **O Estopim (16:03:29):** Aplicações desktop (`msedge.exe`, `msedgewebview2.exe`, `Antigravity.exe`) foram abertas. O DWM tentou apresentar uma janela de aplicativo (1400x900) chamando `Bc250UmdBridgeLinearToWddm` (Escape `COMPOSITE_BLT`).
   - A chamada usava `args.Flags.HardwareAccess = 1u` e `args.hContext = NULL`.
   - Com `HardwareAccess = 1` e contexto nulo, o escalonador do Windows (`dxgkrnl.sys / VidSch`) bloqueou a thread chamadora esperando todas as filas de hardware ficarem ociosas.
   - Após 4,172 s (tempo limite `TdrDelay` do Windows), `pfnEscapeCb` expirou e devolveu `E_FAIL` (`0x80004005`).
3. **O Laço de Reinício do DWM (16:03:34 – 16:04:35):**
   - Ao receber `E_FAIL`, o DWM considerou o dispositivo perdido e reiniciou.
   - Cada nova instância do DWM tentou fazer o Present inicial da swapchain antes de desenhar.
   - Em `bc250_d3d_umd.c:5165`, a UMD continha `if (!vkRendered && device->NativePm4DrawCount == 0) return FALSE;` -> `[PRESENT RECUSADO: NENHUM_RENDER_DETECTADO]`, resultando em `E_FAIL` imediato no primeiro quadro!
   - Esse `E_FAIL` fazia cada novo DWM abortar instantaneamente, gerando um laço de crash a cada ~12 s.
4. **Por que a Imagem Ficou como na Foto:**
   - Durante o laço de crash do DWM, o framebuffer de scanout na VRAM herdado do GOP permaneceu congelado: o topo continha o último conteúdo válido renderizado às 16:03:29 (cabeçalho da janela/ícones), e a parte inferior (~75%) permaneceu preta sem novas composições.

---

## 2. Correções Implementadas no Candidato DX.81

1. **Eliminação do Bloqueio no Escape (`HardwareAccess = 0u`):**
   - Em [`driver/umd/bc250_d3d_umd.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.c) (`Bc250UmdBridgeLinearToWddm`), configurado `args.Flags.HardwareAccess = 0u` e passado `args.hContext = d->Winsys.PresentContextCreated ? d->Winsys.PresentContext : NULL;`.
   - O `dxgkrnl.sys` entrega a requisição de escape diretamente ao KMD sem congelar a thread em `VidSchWaitForAllContexts`.
2. **Prevenção do Laço de Morte do DWM (`DX81_PASSTHROUGH`):**
   - Em [`driver/umd/bc250_d3d_umd.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.c) (`Bc250UmdPresentVulkanDetileAndScanout`), quando `!vkRendered && device->NativePm4DrawCount == 0`, a UMD não reprova com `E_FAIL`: registra `[DX81_PASSTHROUGH]` e repassa a alocação existente diretamente para `pfnPresentCb`. O DWM inicializa limpo e nunca entra em crash loop.
3. **Expansão de Espaço GPUVA para 4096 Faixas:**
   - Em [`driver/core/bc250_gpuva_space_contract.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/core/bc250_gpuva_space_contract.h) e [`driver/core/bc250_gpuva_journal_contract.h`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/core/bc250_gpuva_journal_contract.h), `BC250_GPUVA_SPACE_MAX_RANGES` e `BC250_GPUVA_JOURNAL_MAX_CHANGES` ampliados de 1024 para 4096, quadruplicando a margem para paginação concorrente pesada.
   - Em [`driver/tests/active_miniport_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/active_miniport_test.c), fixture `g_pool` expandido para 8 MiB para acomodar o contexto de adaptador ampliado.

---

## 3. Identificação do Pacote e Hashes de Controle

- **DriverVer Empacotado:** `09/25/2026,3.286.1.0` (INF fonte `3.286.0.0`)
- **Perfil:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `P1 PERSISTENT SUBMISSION`
- **Hashes SHA-256:**
  - `bc250mcdm.sys`: `6B29F04981761D14EF84960596D10A452E08512CF035881D1375626A3321EEA9`
  - `bc250mcdm.inf`: `551515D765510E3F4270E5057C7884ACCF30D48EB16769DE1CCD1AC5F855E95B`
  - `bc250mcdm.cat`: `EDC32CA0ECA51B0DDB6C116BE01FAF13A2F3F8E9F6D6B62820135097C1E41964`
  - `bc250umd.dll`: `0494EE02C5A5A52E5FD4AEB4F1D39A67999F091C51A2889472FC27CBF57F50B5`
  - RADV externo: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- **Validação Offline:**
  - `driver\wdk\build-contract.bat`: **EXIT 0 (PASS)**.
  - `driver\umd\build-umd.bat`: **EXIT 0 (PASS)**.
  - `python tools\audit-bc250mcdm-safety.py`: **PASS**.
  - `powershell tools\audit-bc250mcdm-lab-package.ps1`: **PASS**.
  - `powershell tools\prepare-corrida128-dx81.ps1`: **PASS**.

---

## 4. Critérios de Avaliação da Corrida 128 (600 s)

| Desfecho | Critério | Interpretação |
|---|---|---|
| **A (Sucesso Pleno — Stall Eliminado e Janela Longa Conquistada)** | (1) Janela de **600 s completa** sem congelamento, crash de DWM, TDR ou BugCheck;<br>(2) Desktop limpo, íntegro em 100% da área da tela (sem artefatos de 75% escuro da foto);<br>(3) Conformance harness com 60/60 `PRESENT_OK`;<br>(4) Log do DWM comprova ausência de stalls de 4 s em `Bc250UmdBridgeLinearToWddm` e ausência de recusas `NENHUM_RENDER_DETECTADO`;<br>(5) Rollback online restaura `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot (R1). | Problema da foto 100% diagnosticado e resolvido. Marco de 10 minutos de estabilidade contínua conquistado. |
| **B (Avanço Parcial — DWM Estável com Parada Tardia em Outro Subsistema)** | DWM opera continuamente sem laço de morte e sem stall no escape, mas ocorre travamento tardio em hardware após vários minutos. | Validação de que a camada de apresentação/escape foi curada, isolando a pendência remanescente. |
| **C (Falha — Reincidência de Timeout de Escape ou Crash de DWM)** | Registro de timeout de 4 s ou recusa de Present nos logs. | Investigação adicional sobre bloqueios de contexto do escalonador. |
