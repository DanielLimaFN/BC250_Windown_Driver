# docs/284 — HANDOFF: 16 SUBMISSÕES APOSENTADAS NO RING CP_RB0 (NODE 1 GFX) E ISOLAMENTO DA TELA PRETA

> **Data:** 2026-09-17 15:40  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Candidato:** Driver WDDM `bc250mcdm.sys` + UMD `bc250umd.dll` + Mesa RADV (`vulkan_radeon.dll`)  
> **Evidência arquivada:** `docs/evidence/2026-09-17_15-12-02-DESKTOP-HQAG6T4-lab-binding/`  
> **Log KMD recuperado:** `display-log-recovered.txt` (590.085 bytes, 5.236 linhas)  
> **Norma de relato:** Estrito cumprimento de `AGENTS.md`, `GEMINI.md` e `DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md`.

---

## 1. Contexto e Objetivo do Ensaio
Execução do ensaio de conformidade do runtime Direct3D 11 nativo (`bc250-d3d11-conformance.exe`) acionado via `tools/EXECUTAR_D3D11_NATIVO.cmd` sob o perfil de laboratório do driver WDDM (`bind-bc250mcdm-lab.ps1 -RunD3D11Native -D3D11Frames 60`).

O objetivo foi avaliar a renderização e apresentação física do triângulo RGB com clear azul vibrante (`0.15, 0.45, 0.90, 1.0`) no monitor LG Ultrawide conectado ao DisplayPort da BC-250, através da UMD oficial `bc250umd.dll`.

---

## 2. Relato dos 5 Tópicos Normativos

### 1. Houve avanço?
**Sim, avanço funcional inédito no silício e avanço decisivo de diagnóstico.**

- **Avanço Funcional no Silício:** Pela primeira vez no projeto, o miniport WDDM aceitou, despachou e aposentou com sucesso **16 submissões de rendering 3D diretamente no anel gráfico físico (`CP_RB0`, Node 1 GFX)** da APU sob VMID 7, sem congelamento da GPU e sem falhas de página:
  ```text
  [SubmitVirtual Node1 GFX] GATE FISICO PASS: Root=0x45FD53000 FirstPA=0x454EF8000
  [GfxEngine] SUBMIT SUCESSO: Fence=16 NewWptr=192 DmaVA=0x200000000 SizeDW=24 VMID=7
  [SubmitVirtual Node1 GFX] FENCE COMMIT: Token=16 RingFence=16 committed=1 accepted=1
  ```
- **Avanço de Diagnóstico:** Identificação causal completa da razão pela qual a imagem não apareceu no monitor e resultou em tela preta: desconexão arquitetural na UMD entre o render target desenhado e o buffer destilado para o scanout do monitor.

### 2. Sucessos
1. **Portão de Display GOP Aprovado:** A inicialização do display herdado do UEFI convergiu em menos de 7 segundos (`compute-display-prerequisite-gate.json`: `state: PASSED`, `shared_primary: true`, `commit_vidpn: true`, `set_vidpn_source_address: true` às 15:12:08).
2. **Execução Física no Ring Gráfico (Node 1 GFX):** 16 command buffers de 24 dwords (96 bytes) consumidos no anel físico `CP_RB0`, atualizando o ponteiro de gravação (`NewWptr=192`) e aposentando todas as fences (Fences 1 a 16).
3. **Node 0 SDMA:** 446 submissões indiretas executadas sob VMID 8 com trípode comprovado (fences 4098 a 4544).
4. **OTG Scanout Estável:** O gerador de temporização de display (OTG) manteve o scanout sincronizado com 240 VSyncs lidos do hardware (`OTG_Frame=166442`).
5. **Estabilidade de Kernel e DWM:** Zero BugCheck, zero TDR de GPU. O gerenciador de janelas do Windows (`dwm.exe`) não registrou fail-fasts (`0x887A0004` e `e0464645` ausentes no log recuperado).
6. **Canal Escape COMPOSITE_BLT Ativo:** O escape por hardware operou com sucesso 7 vezes, copiando blocos de 11.059.200 bytes via SDMA.

### 3. Falhas e Diagnóstico Causal Detalhado
1. **Desconexão entre Render Target PM4 WDDM e Scanout Vulkan Bridge (Causa Raiz da Tela Preta):**
   - Na chamada `Bc250Umd10Draw` (`driver/umd/bc250_d3d_umd.c`), `Bc250UmdExecuteVkDraw` encontrou o pipeline gráfico Vulkan não inicializado para a chamada imediata e acionou o fallback `Bc250UmdEmitPm4Draw`.
   - `Bc250UmdEmitPm4Draw` construiu 24 dwords de PM4 (`Bc250Pm4EmitColorTarget0` + `Bc250Pm4EmitWindowScissor` + `Bc250Pm4EmitDrawIndexAuto`) e despachou diretamente na alocação WDDM (`dst->Allocation.GpuVirtualAddress`). Essas foram exatamente as 16 submissões registradas em `CP_RB0`.
   - Na apresentação (`Bc250DxgiPresent1`), `Bc250UmdPresentVulkanDetileAndScanout` assumiu que a renderização havia ocorrido na textura da **Vulkan Bridge** (`srcRes->VkImg`). Como o render foi para a alocação WDDM, a textura Vulkan continha zeros puros (uninitialized).
   - O comando `vkCmdCopyImageToBuffer` copiou esses zeros para `LinearPresentBuf` (`LinearPresentMem`), e a ponte `Bc250UmdBridgeLinearToWddm` disparou o escape `COMPOSITE_BLT` via SDMA para transferir o buffer linear para a superfície do monitor.
   - O SDMA copiou com sucesso 11.059.200 bytes de zeros para a memória ativa do display GOP, cobrindo o monitor de preto:
     ```text
     [Bc250Escape] COMPOSITE_DATA_PROOF INCONCLUSIVO_ZERO: Match=5/5; zero contra zero NAO prova pixel
     [Bc250Escape] COMPOSITE_BLT OK RECT 1:1: 2560x1080 srcPitch=10240 dstPitch=10240 DstXY=0,0 SrcMc=0xF40E18C000 DstMc=0xF40B65C000 Bytes=11059200 Prova=ZERO_INCONCLUSIVO
     ```
2. **Configuração de SwapChain com 2 Buffers no Harness:**
   - Em `d3d11_native_conformance_test.cpp`, o swapchain foi configurado com `BufferCount = 2` e `DXGI_SWAP_EFFECT_DISCARD`.
   - Apenas o Buffer 0 foi obtido e associado a uma Render Target View (`pRTV`). A cada `Present`, o DXGI alternou para o Buffer 1 (nunca limpo nem desenhado), apresentando buffers não renderizados nos frames subsequentes.
   - A chamada `pContext->OMSetRenderTargets` estava posicionada após `ClearRenderTargetView`.
3. **Reboot Manual pelo Operador:**
   - Com o monitor coberto por zeros e o watchdog configurado para 300 s, o operador aguardou ~2 minutos sem feedback visual e reiniciou a máquina pelo botão físico de energia.
   - O desligamento forçado gerou evento de Kernel-Power (ID 41) e armou o fusível de boot `C:\BC250_BOOT_FUSE.dat` para `State=2 (TRIPPED)`.

### 4. Veredito
**Parcial / Inconclusivo.**
- **Aprovado:** Execução física de comandos no anel `CP_RB0` (Node 1 GFX) e transferências SDMA 2D no silício.
- **Não Aprovado:** Os dados transferidos para o scanout físico continham zeros (`0x00000000`), gerando tela preta. O teste não atinge `NonZeroSamples=5/5` nem a confirmação visual do triângulo RGB.

### 5. Recuperação e Pendências
- **Estado Atual da Máquina:** Máquina reiniciada com sucesso em boot limpo, operando em `BasicDisplay` (Código 0).
- **Fusível de Boot:** `C:\BC250_BOOT_FUSE.dat` está em `State=2 (TRIPPED)` (`LastStatus=0xC0000001`) e bloqueia novos binds até ser resetado com `tools\inspect-boot-fuse.ps1 -Reset`.
- **O que Continua NÃO VALIDADO EM HARDWARE:**
  - Exibição de triângulo com cores não-nulas comprovadas (`NonZeroSamples=5/5`) no scanout do monitor via D3D11 nativo.
  - Coerência 1:1 entre a alocação de saída da UMD e a superfície de scanout apresentada.

---

## 3. Ações Corretivas Executadas Offline (17/09 15:50)

1. **Harmonização do Fluxo de Apresentação da UMD (`driver/umd/bc250_d3d_umd.c`):**
   - Implementada a função `Bc250UmdPresentBltToScanout` para acionar diretamente o escape SDMA `BC250_ESCAPE_OP_PRESENT_BLT` da alocação WDDM para o scanout físico.
   - Atualizada a função `Bc250UmdPresentVulkanDetileAndScanout`:
     - Se rendering PM4 ocorreu (`NativePm4DrawCount > 0` e `NativeVkDrawCount == 0`), a alocação WDDM é preservada (não sofre a sobrescrita destilada de zeros da bridge Vulkan) e apresentada diretamente ao scanout físico do monitor.
     - Se rendering Vulkan ocorreu (`NativeVkDrawCount > 0` ou `InRenderPass`), destila `srcRes->VkImg` para `LinearPresentMem` e copia para a alocação WDDM.
2. **Robustez na Criação de RTV/DSV Vulkan (`driver/umd/bc250_umd_resource_vk.c`):**
   - Em `Bc250UmdVkCreateRtv` e `Bc250UmdVkCreateDsv`, aplicado clamp estrito em `layerCount` e `baseArrayLayer` evitando que `ArraySize = (UINT)-1` cause rejeição pelo Mesa RADV (`vkCreateImageView`).
3. **Ajuste de SwapChain e Ordem de Binding no Harness (`driver/tests/d3d11_native_conformance_test.cpp`):**
   - `scd.BufferCount = 1` com `DXGI_SWAP_EFFECT_DISCARD` garantindo que o Buffer 0 seja o único render target apresentado a cada frame pelo DXGI.
   - Chamada `pContext->OMSetRenderTargets(1, &pRTV, NULL)` reposicionada para antes de `ClearRenderTargetView` e no início do loop de renderização.
4. **Validações e Builds Concluídos:**
   - `tools/build-d3d11-native-conformance.bat` Exit 0 (`driver/tests/output_d3d11_native/bc250-d3d11-conformance.exe`).
   - `driver/umd/build-umd.bat` Exit 0 (`bc250umd.dll` e testes de contrato de submissão e trípode Node 1 em PASS).
   - `driver/wdk/build-contract.bat` Exit 0 (105 DDIs conformes, matriz de segurança PASS).
   - `tools/package-bc250mcdm-lab.ps1` Exit 0 (pacote `output_lab` assinado, catálogo `.cat` gerado).

---

## 4. Prontidão para a Corrida Física 14

1. **Procedimento de Disparo pelo Operador:**
   - Executar Prompt de Comando elevado (Administrador):
     ```cmd
     tools\EXECUTAR_D3D11_NATIVO.cmd
     ```
   - O script executará o preflight automático, resetará o fusível (`inspect-boot-fuse.ps1 -Reset`), atualizará a DLL Vulkan em `C:\BC250_LIB`, aplicará o binding seguro com watchdog de 300 s, executará os 60 quadros do harness D3D11 nativo e fará o rollback seguro para `Microsoft BasicDisplay`.
2. **Critérios de Veredito da Corrida 14:**
   - `NonZeroSamples > 0` na inspeção da UMD ou no escape `PRESENT_BLT`.
   - Submissões Node 1 GFX (`CP_RB0`) aposentadas com fences incrementais.
   - Observação física visual de cores (triângulo RGB e clear azul vibrante) no monitor LG Ultrawide conectado ao DisplayPort.
