# DX.74: rastreamento temporal de pfnPresentCb e criterio da Corrida 119

> 25/09/2026. Base: [Corrida 118](437_CORRIDA118_DX73_TRIANGULO_PIXELS_COPIA_E_R1.md). Alteracao **OFFLINE; NAO VALIDADA EM HARDWARE**.

## Lacuna medida e alteracao

Na Corrida 118, o triângulo colorido e o desktop foram renderizados visualmente com fidelidade perfeita (60/60 amostras de centro `0xFF7F4040` e canto preservadas na cópia), mas a taxa do harness permaneceu em apenas ~0,9 FPS (~1,11 s/quadro) e o DWM registrou 2,68 inícios de Present/s nos primeiros 120 s.

A análise fina dos 220 flushes e das chamadas da Corrida 118 revelou um gargalo desproporcional:
- A chamada `pfnPresentCb` na UMD consumiu em média **~443 ms por quadro** (ex.: n=1 gastou 443.342 us; n=32 gastou 442.923 us).
- Em contraste, a DDI no kernel (`Bc250DxgkDdiPresent`) registrou duração interna de apenas **~0,75 ms** (`ElapsedUs=750`).
- Existe uma disparidade de **~442 ms** não instrumentada entre a entrada no callback em user-mode e o retorno da execução. Essa latência pode residir no runtime do dxgkrnl (fila de sincronização/escalonamento com o scheduler do WDDM), na espera por fences de SDMA/GFX, ou no retorno/sinalização assíncrona após a DDI.

A **DX.74** estabelece um rastreamento temporal unificado por QPC (QueryPerformanceCounter):
1. **UMD (`driver/umd/bc250_d3d_umd.c`):** Instrumentação de alta precisão ao redor de `pfnPresentCb` tanto em `Bc250DxgiPresent` quanto em `Bc250DxgiPresent1`, emitindo `[DX74_PRESENT_UMD_CB]` e `[DX74_PRESENT1_UMD_CB]` com `StartQpc`, `EndQpc`, `ElapsedUs` e `hr`.
2. **KMD (`driver/wdk/bc250_display_ddi.c`):** Emissão de `[DX74_PRESENT_KMD_TEMPO]` com `StartQpc`, `EndQpc` e `ElapsedUs` na saída de `Bc250DxgkDdiPresent`.
3. **KMD SDMA Submit (`driver/wdk/bc250_active_miniport.c`):** Adição de `qpc=%llu` no evento estruturado `BC250_DIAG schema=1 level=INFO event=KMD_SDMA_SUBMIT`.

Como `QueryPerformanceCounter` em modo de usuário e `KeQueryPerformanceCounter` no kernel compartilham a mesma base de ticks e frequência de hardware do processador, podemos decompor os ~443 ms em:
- **Latência pré-DDI (dxgkrnl / escalonamento):** `KMD_StartQpc - UMD_StartQpc`.
- **Tempo de execução da DDI:** `KMD_EndQpc - KMD_StartQpc`.
- **Latência pós-DDI (aposentadoria / sinalização / desmonte):** `UMD_EndQpc - KMD_EndQpc`.

Esta instrumentação é puramente de diagnóstico temporal, não altera contratos de apresentação nem escrita de registradores e não afeta o caminho crítico gráfico.

## Portoes e candidato

- Build contratual, teste offline, build SYS lab, build UMD, auditoria de seguranca, assinatura, auditoria de pacote e preflight somente leitura: **PASS**. Nenhum bind com DX.74 realizado.
- Pacote lab **3.278.1.0**, perfil `DISPLAY_RENDER_LAB`, mesmos tunables da Corrida 118:
  - `bc250mcdm.sys`: `5EC12AF4AE22A7BFDDD6939F953CF095505F74B43B7F73B3A541369A538833BD`
  - `bc250mcdm.pdb`: `703E67B6E5D044FF1CBCAE64D0FE5032CCE9E003E4F3C4AC1A9B426A47DD4459`
  - `bc250umd.dll`: `00F9CC7326BC32F75CE155CFA1D7AB504EA867BD90527EC5E4CACF8DECD6F4AB`
  - `bc250mcdm.inf`: `BD897C8FB916CB814F79EEEA71F77BFE489160598EE975D605A2A04FDB0240BA`
  - `bc250mcdm.cat`: `76CBFEDFDC34849C1F1869A17500B6AE68DB3D9FCEAD60B5F15497A50CC02770`
  - `vulkan_radeon.dll`: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- `tools/prepare-corrida119-dx74.ps1` confere hashes/manifesto/RADV, auditorias, bloqueio do Radeon Software e configuracao de dump.
- Runner elevado: `tools/EXECUTAR_CORRIDA119_DX74_180S.cmd`.
- `CLEAN_BOOT_REQUIRED`: **boot frio com corte físico de energia obrigatório antes do bind**.

## Criterio escrito antes da Corrida 119

Controles: pacote 3.278.1.0, 60 quadros do harness em janela de 180 s, logs integrais KMD e UMD coletados pelo guardião e restauração limpa para BasicDisplay Código 0 no mesmo boot.

| Desfecho | Condicao | Veredito |
|---|---|---|
| **A** | Correlação QPC entre UMD (`DX74_PRESENT_UMD_CB`) e KMD (`DX74_PRESENT_KMD_TEMPO`) em 100% dos quadros amostrados; decomposição de latência pré-DDI, DDI e pós-DDI conclusiva; triângulo visível na janela; 60/60 `PRESENT_OK`; rollback limpo para BasicDisplay Código 0 | Sucesso de rastreamento e diagnóstico. Gargalo de latência do Present CB rigorosamente isolado para atuação na DX.75. |
| **B** | Correlação QPC obtida e registrada com sucesso, mas triângulo ausente na janela do harness | Diagnóstico temporal válido; intermitência visual reproduzida sob instrumentação QPC. |
| **C** | Inconsistência nos registros QPC ou falha na correlação UMD ↔ KMD | Instrumentação inconclusiva; refinar captura temporal. |
| **E** | Travamento do sistema, BugCheck, device-lost ou falha fora dos critérios | Descrever a evidência exata coletada. Se travar, acionar dump manual (Ctrl direito + Scroll Lock 2x). |
