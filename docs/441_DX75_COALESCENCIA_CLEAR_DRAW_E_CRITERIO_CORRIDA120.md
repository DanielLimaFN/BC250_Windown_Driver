# DX.75: coalescencia de Clear+Draw, eliminacao de dreno vazio e criterio da Corrida 120

> 25/09/2026. Base: [Corrida 119](440_CORRIDA119_DX74_DECOMPOSICAO_TEMPO_PRESENT_CB_E_R1.md). Alteracao **OFFLINE; NAO VALIDADA EM HARDWARE**.

## 1. Lacunas medidas na Corrida 119 e acoes implementadas

Na Corrida 119, o rastreamento temporal por QPC (`DX.74`) isolou com exatidao os tempos de execucao:
- **`pfnPresentCb` no kernel:** `Bc250DxgkDdiPresent` levou p50 **0,67 ms** e o submit SDMA levou **0,27 ms** (DDI ultrarrapida). A retencao de ~438 ms decorreu do dxgkrnl aguardando a liberacao do frame redirecionado pelo DWM (cadencia do swapchain).
- **Tempo de renderizacao do harness:** Cada quadro do harness gastou **~339 ms** entre presents, divididos em **4 submissoes sincronas ao hardware** via `vkQueueSubmit` + `vkWaitForFences`:
  1. `09:48:50.881`: `[DX49_BATCH] DRENO antes de draw pelo caminho direto: DrawsPendentes=0` -> `SubmitUs = 69.5 ms`.
  2. `09:48:50.956`: Draw flush (`Bc250UmdExecuteVkDraw`, linha 3473) -> `SubmitUs = 69.4 ms`.
  3. `09:48:51.035`: Draw readback (`Bc250UmdReadbackDrawTarget`, linha 3026) -> `SubmitUs = 78.2 ms`.
  4. `09:48:51.130`: Present destile flush (`Bc250UmdPresentVulkanDetileAndScanout`, linha 5192) -> `SubmitUs = 95.1 ms`.
  - **Tempo total em `vkQueueSubmit`:** **~312 ms por quadro**!

### Correcoes da DX.75 (`driver/umd/bc250_d3d_umd.c`)

1. **Coalescencia de `ClearRenderTargetView` com `Bc250UmdExecuteVkDraw`:**
   - Em Vulkan nativo, o clear deve ser executado pelo hardware tile/raster unit no inicio do render pass (`loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR`), e os comandos de draw devem desenhar diretamente dentro do mesmo render pass.
   - O codigo anterior encerrava incondicionalmente o render pass antes do draw (`vkCmdEndRenderPass`) e acionava um dreno sincrono (`Bc250UmdVkDrainPending`), forcando a submissao de um command buffer que continha apenas o clear (custando ~70 ms) para em seguida abrir um novo render pass (`loadOp = LOAD`) e submeter o draw em outro command buffer (~70 ms).
   - Na DX.75, se `pDevice->InRenderPass` esta ativo no mesmo framebuffer (`rtv->VkFb`) e a pipeline nao exige encerramento para barreiras de imagem de SRV (`BindingCount == 0`), o render pass **permanece aberto** e o draw e gravado diretamente sobre o clear.
2. **Eliminacao do dreno com zero draws:**
   - A chamada `Bc250UmdVkDrainPending(&pDevice->VkBridge, "draw pelo caminho direto")` agora so e disparada quando existem draws pendentes de lotes anteriores (`pDevice->VkBridge.PendingDraws > 0`). Quando `PendingDraws == 0`, nenhum submit ou espera sincrona de GPU e disparado.
   - Isso **elimina integralmente o Flush 1** (~70 ms de parede economizados por quadro).
3. **Reducao do teto de inspecao de readback:**
   - `BC250_DRAW_READBACK_MAX` foi reduzido de `40u` para `3u`.
   - Nos quadros 1 a 3, a grade de telemetria continua sendo lida para prova de pixels do alvo no log.
   - A partir do quadro 4, o readback e imediatamente ignorado, **eliminando integralmente o Flush 3** (~78 ms de parede economizados por quadro nos quadros 4 a 60).
4. **Fechamento seguro no Present:**
   - Em `Bc250UmdPresentVulkanDetileAndScanout`, qualquer render pass que tenha permanecido aberto e devidamente encerrado antes da barreira de transferencia (`VK_PIPELINE_STAGE_TRANSFER_BIT`).

---

## 2. Portoes e candidato DX.75

- Build contratual, testes offline, build SYS lab, build UMD, auditoria de seguranca (`audit-bc250mcdm-safety.py`), assinatura de catalogo e auditoria de pacote lab (`audit-bc250mcdm-lab-package.ps1`): **TODOS PASS**.
- Pacote lab **3.279.1.0**, perfil `DISPLAY_RENDER_LAB`:
  - `bc250mcdm.sys`: `7BDCA67C06FB3270A9F5D83E83F4EE21AE306128AA7D2756D2B74C1639F6DB40`
  - `bc250mcdm.pdb`: `452650F829558E51F8636A51BBC0BC99FD2527A5C6D5EEC16490264CE9339B16`
  - `bc250umd.dll`: `9F02C24FA3821ADF70A2C7E072DF2AD1DB6210C16CA366701FC2A02E27581C87`
  - `bc250mcdm.inf`: `9C4E2AEEE38ABD29CFB6169DD68D9FBA031AF4EADD24C1FFE28796DEF58DF6EA`
  - `bc250mcdm.cat`: `900C2F56C35A82992B0B55110434BF90DAB1ECE3D66AB32F14822354710541E2`
  - `vulkan_radeon.dll`: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- Script de preflight e verificacao estrita: `tools/prepare-corrida120-dx75.ps1`.
- Runner elevado: `tools/EXECUTAR_CORRIDA120_DX75_180S.cmd`.
- `CLEAN_BOOT_REQUIRED`: **boot frio com corte fisico de energia obrigatorio antes do bind**.

---

## 3. Criterio escrito antes da Corrida 120

Controles: pacote 3.279.1.0, 60 quadros do harness em janela de 180 s, logs integrais KMD e UMD coletados pelo guardiao e restauracao limpa para BasicDisplay Codigo 0 no mesmo boot.

| Desfecho | Condicao | Veredito |
|---|---|---|
| **A** | Eliminacao do dreno sincrono com `DrawsPendentes=0` (Clear+Draw no mesmo lote); diminuicao do tempo de parede por quadro no harness; triangulo colorido visualmente estavel na janela; 60/60 `PRESENT_OK`; rollback limpo para BasicDisplay Codigo 0 | Sucesso pleno de otimizacao e estabilidade. Reducao substancial de latencia de apresentacao e renderizacao comprovada. |
| **B** | Coalescencia Clear+Draw confirmada nos logs (`DrawsPendentes=0` suprimido), mas triangulo ausente na janela do harness | Diagnostico valido de reducao de flushes; intermitencia visual aberta para isolamento na rasterizacao. |
| **C** | Render pass falha ou reabre de forma divergente sem coalescencia | Inconclusivo; investigar condicoes de estado do render pass. |
| **E** | Travamento do sistema, BugCheck, device-lost ou falha fora dos criterios | Descrever a evidencia exata coletada. Se travar, acionar dump manual (Ctrl direito + Scroll Lock 2x). |
