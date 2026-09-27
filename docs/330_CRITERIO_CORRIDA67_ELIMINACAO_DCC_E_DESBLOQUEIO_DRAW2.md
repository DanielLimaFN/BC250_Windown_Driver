# Critério da Corrida 67 — Eliminação de Compressão DCC & Desbloqueio do Draw 2 no DWM

> **Escrito ANTES de qualquer alteração de código ou ensaio físico** (Lei 9).
> **Candidato:** lab **3.228.1.0** (faixa DX.20).
> **Perfil:** `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT SUBMISSION.
> **Estado:** NÃO VALIDADO EM HARDWARE.

---

## 0. O que a Corrida 66 respondeu

Na Corrida 66 (lab `3.227.1.0`):
1. **O upload de texturas via Vulkan foi 100% provado:**
   - `Bc250UmdVkUploadImageData` via staging buffer executou com sucesso para a textura 590x800 e demais alocações do DWM, aplicando as barreiras de transição explícitas e validando o layout de hardware via fence física.
   - O fault em `0x2441000` foi completamente extinto.
2. **O anel gráfico físico `CP_RB0` (Node 1 GFX) aposentou 64 submissões 3D no silício:**
   - Fences avançaram de 1 até **Fence 66** (contra 10 na Corrida 65).
3. **O DWM avançou na renderização 3D:**
   - O Draw 1 do DWM (`VkDraws=1`, vertices=48, `CB=1, SRV=0`) executou e concluiu em hardware em apenas **21 milissegundos**.
4. **Isolamento exato da causa raiz do stall no Draw 2:**
   - No Draw 2 (`VkDraws=2`, vertices=126, `CB=1, SRV=1, SAMPLER=1`), o DWM amostrou a textura `590x800` (`res=0000000006929170`, alocada em `0x243100000`).
   - A imagem havia sido criada com `VK_IMAGE_TILING_OPTIMAL`, fazendo o RADV habilitar Delta Color Compression (DCC) (`reqSize=2310144` vs `2048000` bytes de pixels).
   - O upload de pixels via `vkCmdCopyBufferToImage` populou o plano de cor, mas **não inicializou os 262 KiB de metadados DCC** na cauda da alocação.
   - O descritor `T#` foi programado com `COMPRESSION_EN=1` (`SQ_IMG_RSRC_WORD6`). Ao executar `image_sample`, o Texture Cache Pipe (`Cid=10`, `TCP_UTCL0`) leu metadados de compressão não inicializados (lixo de VRAM), provocando stall severo no pipeline de descompressão ou tentativa de dereferenciar blocos inexistentes (`FaultAddrPagina=0x2431` -> `0x2431000` = `0x243100000 >> 8`), durando 2,065 s até o disparo do TDR de 2,0 s do WDDM.

---

## 1. As Perguntas Desta Corrida

1. **Pergunta 1 (Desativação de DCC e Amostragem Direta de Texels):**
   Ao configurar explicitamente `RADV_DEBUG=nodcc` na inicialização da ponte Vulkan e definir flags mutáveis nos recursos de textura, garantindo que `COMPRESSION_EN=0` em todos os descritores `T#` e eliminando a alocação e consulta de metadados DCC, o Draw 2 do DWM (`CB=1, SRV=1, SAMPLER=1`) é executado sem stall no pipeline de descompressão e sem gerar o fault `Cid=10` em `0x2431000`?

2. **Pergunta 2 (Continuidade da Apresentação DWM):**
   Com o Draw 2 concluindo em tempo hábil (< 20 ms) e sem disparar o watchdog de TDR, o DWM consegue prosseguir com a renderização dos quadros subsequentes e atingir a apresentação no swapchain / scanout do GOP sem perda de dispositivo (`DXGI_ERROR_DEVICE_REMOVED`)?

---

## 2. O que Muda no Candidato 3.228.1.0

1. **UMD Vulkan Bridge (`driver/umd/bc250_umd_vulkan_bridge.c`):**
   - Em `Bc250UmdVkBridgeInitInternal`: forçar `SetEnvironmentVariableA("RADV_DEBUG", "nodcc")` antes do carregamento do ICD e de `vkCreateInstance`. Isto garante que o driver Mesa RADV desative globalmente a compressão DCC (`radv_is_dcc_disabled = true`), programando `COMPRESSION_EN=0` em todos os descritores de imagem.
2. **UMD Vulkan Resource (`driver/umd/bc250_umd_resource_vk.c`):**
   - Na criação de imagens 2D (`Bc250UmdVkCreateResource`): adicionar `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` para reforçar a desativação de otimizações agressivas de compressão em recursos atualizados via CPU.
3. **Pacote & INF (`driver/wdk/package/bc250mcdm.inf`):**
   - Incremento da versão do driver para `3.228.1.0`.

---

## 3. Desfechos Previstos (Lei 10)

### Desfecho A — Sucesso no Draw 2 e Continuidade do Render (Meta Principal)
- **Assinatura:**
  - `RADV_DEBUG=nodcc` ativo nos logs do bridge Vulkan.
  - Draw 2 conclui com `DRAW fence concluida` em tempo reduzido (< 30 ms) sem timeout (`res=-4`).
  - Zero faults `Cid=10` em `0x2431000` ou `0x2441000`.
  - Zero TDRs durante os draws do DWM.
  - O DWM completa o quadro e aciona `D3D10DDI_HDEVICE` Present ou o harness conclui quadros adicionais.
- **Implicação:** O pipeline de amostragem de textura sem compressão DCC está plenamente funcional no silício da BC-250.

### Desfecho B — Draw 2 Conclui Rápido, mas Surge Bloqueio Subsequente
- **Assinatura:**
  - Draw 2 aposenta em milissegundos sem `Cid=10`.
  - Um draw subsequente (Draw 3+) ou a apresentação DXGI sofre recusa ou falha por outro motivo nomeado (ex: formato de render target, sincronização de swapchain ou segundo fault `Cid=9`).
- **Implicação:** O gargalo do DCC no Draw 2 foi 100% resolvido; o próximo bloqueador funcional é isolado.

### Desfecho C — Falha Persistente no Draw 2 Mesmo sem DCC
- **Assinatura:**
  - Draw 2 continua demorando > 2 s e gerando fault `Cid=10` em `0x2431000`.
- **Implicação:** A causa não reside nos metadados DCC, mas sim em instrução do shader tratando `desc[0]` como endereço plano (flat pointer) em vez de descritor de textura amostrada. Exigirá interceptação na tradução de SPIR-V / NIR.

### Desfecho D — Algo Fora do Mapa (Lei 10, Regra 2)
- **Assinatura:**
  - BugCheck, congelamento total do sistema operacional, colapso de MMIO ou falha de boot.
- **Implicação:** Investigar dump de memória do kernel e telemetria de inicialização para isolar o evento anômalo.

---

## 4. Governança e Verificação Obrigatória

- Conformidade com `AGENTS.md` e `GEMINI.md`:
  - Zero WARP, zero fallback de cópia por CPU.
  - Build e auditorias de segurança obrigatórias antes da instalação.
  - Relato estruturado obrigatório em 5 seções após a conclusão da corrida no silício.
