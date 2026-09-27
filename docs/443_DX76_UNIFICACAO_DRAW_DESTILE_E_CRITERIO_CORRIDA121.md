# DX.76: unificacao de Draw com Destile do Present, eliminacao de flushes intermediarios e criterio da Corrida 121

> 25/09/2026. Base: [Corrida 120](442_CORRIDA120_DX75_COALESCENCIA_CLEAR_DRAW_E_RESULTADO.md). Alteracao **OFFLINE; NAO VALIDADA EM HARDWARE**.

## 1. Motivacao e diagnostico da Corrida 120

Na Corrida 120, a coalescencia de `ClearRenderTargetView` com `Bc250UmdExecuteVkDraw` eliminou os drenos vazios (`DrawsPendentes=0`) e reduziu o numero de flushes de 243 para 123 em 60 quadros, elevando a taxa de 0,94 FPS para 1,34 FPS (com picos de 1,7 FPS).

Contudo, cada quadro a partir do quadro 4 ainda executava **2 submissoes sincronas completas ao hardware** com espera de fence (`vkWaitForFences`, ~60 ms cada):
1. **Flush 1 (Draw batch):** `Bc250UmdExecuteVkDraw` gravava o draw, encerrava o render pass e disparava imediatamente `Bc250UmdVkFlushCommands` porque a flag historica `!usedRing` avaliava como verdadeira. O hardware submetia e esperava ~60 ms.
2. **Flush 2 (Present destile):** DXGI Present chamava `Bc250UmdPresentVulkanDetileAndScanout`, abria um novo command buffer, gravava as barreiras de transferencia e a copia de des-tiling (`vkCmdCopyImageToBuffer`), e disparava um segundo `Bc250UmdVkFlushCommands` (~60 ms), seguido por um `vkDeviceWaitIdle` redundante.
3. **Custo total de submissao Vulkan:** ~120-140 ms de parede por quadro gastos exclusivamente em sincronizacao intermediaria de GPU.

---

## 2. Alteracoes implementadas na DX.76 (`driver/umd/bc250_d3d_umd.c`)

1. **Unificacao de Draw e Destile no mesmo Command Buffer (1 Flush por Quadro):**
   - Em `Bc250UmdExecuteVkDraw`, a condicao de flush imediato foi ajustada:
     ```c
     readbackActive = (rtv->pResource && pDevice->DrawReadbackCount < BC250_DRAW_READBACK_MAX);
     flushNow = (directRef || readbackActive || batchPosition >= BC250_DX49_BATCH_MAX_DRAWS);
     ```
   - Nos quadros rotineiros (quadros 4 a 60), o draw permanece gravado no command buffer ativo sem submissao imediata (`flushNow = FALSE`, `FlushAgora = 0`).
   - Quando o DXGI Present e' acionado, `Bc250UmdPresentVulkanDetileAndScanout` obtem o **mesmo command buffer aberto**, anexa a barreira de transferencia (`COLOR_ATTACHMENT -> TRANSFER_SRC`), grava `vkCmdCopyImageToBuffer` para des-tiling, anexa a barreira de memoria para o host e a barreira de retorno para color attachment.
   - Em seguida, dispara uma **unica chamada `Bc250UmdVkFlushCommands` por quadro**: Clear + Draw + Destile sao submetidos e executados em fluxo continuo no hardware da GPU.
   - O numero de flushes por quadro cai de 2 para **exatamente 1**, economizando ~60 ms de parede por quadro.
2. **Eliminacao de `vkDeviceWaitIdle` redundante:**
   - Como `Bc250UmdVkFlushCommands` ja aguarda conclusao formal da fence (`vkWaitForFences`) com barreira de memoria visivel para o host (`VK_ACCESS_HOST_READ_BIT`), a chamada subsequente `vkDeviceWaitIdle` foi removida, eliminando travamentos de pipeline e overhead de polling de dispositivo.
3. **Preservacao de Seguranca e Readbacks de Diagnostico:**
   - Se `directRef` for verdadeiro (recurso sem sombra de CPU), o flush imediato e' mantido.
   - Nos quadros 1 a 3, `readbackActive` permanece verdadeiro, mantendo as leituras de diagnostico da grade do alvo.
   - Se um segundo draw ocorrer antes do Present sem lote no anel, `Bc250UmdVkDrainPending` no topo de `Bc250UmdExecuteVkDraw` drena com seguranca.

---

## 3. Candidato e integridade do pacote DX.76

- DriverVer: **`09/25/2026,3.280.1.0`** (perfil `DISPLAY_RENDER_LAB`).
- Hashes oficiais:
  - `bc250mcdm.sys`: `7BDCA67C06FB3270A9F5D83E83F4EE21AE306128AA7D2756D2B74C1639F6DB40`
  - `bc250mcdm.pdb`: `452650F829558E51F8636A51BBC0BC99FD2527A5C6D5EEC16490264CE9339B16`
  - `bc250umd.dll`: `9A7F60DA1101E0A0D0F589711E1CE6E457A3660C866E959E0859F47B8E114C8F`
  - `bc250mcdm.inf`: `EA6DA5B9DCD7C9E35804209DFDC085C49CDA1DA26FA6D3CBD44E390025F41D35`
  - `bc250mcdm.cat`: `75C7D1CD44DCF2C439B71A6A8ED4B4D9F90B81ED2E62BDD579009B41895ED524`
  - `vulkan_radeon.dll`: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- Auditoria de seguranca (`python tools/audit-bc250mcdm-safety.py`): **PASS (100%)**.
- Auditoria do pacote lab (`powershell tools/audit-bc250mcdm-lab-package.ps1`): **PASS**.
- Suíte contratual (`driver\wdk\build-contract.bat`): **PASS**.

---

## 4. Criterio formal da Corrida 121

Controles: pacote lab 3.280.1.0, 60 quadros em janela de 180 s, logs integrais KMD e UMD coletados pelo guardiao e reversao online R1 para BasicDisplay Codigo 0 no mesmo boot.

| Desfecho | Condicao | Veredito |
|---|---|---|
| **A** | Total de `FlushCommands` no harness cai para **~63** (1 flush por quadro nos quadros 4..60); `DrawsNoLote=1` no flush do Present; tempo por quadro reduzido em relacao a Corrida 120; triangulo colorido visualmente estavel na janela; 60/60 `PRESENT_OK`; rollback limpo para BasicDisplay Codigo 0 | Sucesso pleno da unificacao Draw+Destile. Pipeline Vulkan operando com 1 unica submissao por quadro. |
| **B** | Queda de flushes confirmada nos logs (`~63` flushes), mas triangulo ausente na janela do harness | Diagnostico valido de reducao de overhead; falha visual na transferencia para scanout. |
| **C** | Flushes permanecem em 2 por quadro ou `FlushCommands` reporta falha | Refutacao da unificacao; analisar condicao de flush disparada. |
| **E** | Travamento do sistema, BugCheck, device-lost ou falha fora dos criterios | Descrever a evidencia exata coletada. Se travar, acionar dump manual (Ctrl direito + Scroll Lock 2x). |
