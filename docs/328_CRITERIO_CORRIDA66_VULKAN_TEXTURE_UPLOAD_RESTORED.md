# Critério da Corrida 66 — Restauração do Upload Vulkan de Texturas & Eliminação do Bypass

> **Escrito ANTES de qualquer alteração de código ou ensaio físico** (Lei 9).
> **Candidato:** lab **3.227.1.0** (faixa DX.19).
> **Perfil:** `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT SUBMISSION.
> **Estado:** NÃO VALIDADO EM HARDWARE.

---

## 0. O que a Corrida 65 respondeu

Na Corrida 65 (lab `3.226.1.0`):
1. **O upload de alocações RESOURCE em VRAM visível pela CPU foi 100% resolvido:**
   - Todas as chamadas de `pfnLockCb` para buffers e texturas com token class `BC250_ALLOCATION_TOKEN_CLASS_RESOURCE` retornaram `hr=0x00000000`.
   - O erro `0x80070057` foi completamente extinto.
2. **O anel gráfico físico `CP_RB0` (Node 1 GFX) aposentou 10 submissões 3D no silício:**
   - Fences avançaram de 1 a 10 sob VMID 1.
   - Os primeiros 8 draws (Draws 1 a 8), que utilizavam apenas Constant Buffers (`bindings=1 (CB=1 SRV=0 SAMPLER=0)` ou `bindings=3` sem leitura de textura não inicializada), executaram e aposentaram fences em hardware com total sucesso.
3. **Isolamento exato da falha no Draw 9 e o mecanismo do fault `Cid=10`:**
   - No Draw 9, o DWM vinculou pela primeira vez a textura `res=000000000AC896F0` (`590x800 bpp=4`, `Token=0x5245530026D40018`, `GpuVa=0x243F80000`) como Shader Resource View (`srv0=00000000062DE4E8`, `SRV=1 SAMPLER=1`).
   - Por conta do bypass introduzido na UMD no candidato 3.226.1.0 (`if (res->WddmAllocationUploaded)`), a função `Bc250UmdVkUploadImageData` foi ignorada, e o layout da imagem Vulkan foi forçado no CPU struct para `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` sem que a imagem fosse de fato populada ou transicionada na GPU.
   - A alocação WDDM (`res->Allocation`, onde os pixels foram gravados via `Bc250WinsysUploadAllocation`) e a alocação Vulkan (`res->VkImg` / `res->VkMem`, onde o pipeline 3D de fato lê) são alocações totalmente distintas em faixas de memória separadas.
   - Como `Bc250UmdVkUploadImageData` não rodou para essa textura:
     1. A memória de `res->VkMem` permaneceu não inicializada.
     2. A `VkImage` permaneceu no estado de hardware `UNDEFINED` (metadados DCC/HTILE não inicializados).
     3. Quando o Pixel Shader executou `image_sample` no Draw 9, a amostragem atingiu a memória de metadados/tiling não inicializada da textura, gerando o fault de translação GCVM `Cid=10` em `0x2441000` (`0x244100000 >> 8`).
     4. A onda de shader parou, o evento EOP nunca completou a escrita, e o Command Processor estagnou em `WAIT_REG_MEM`, levando ao timeout e TDR.

---

## 1. As Perguntas Desta Corrida

1. **Pergunta 1 (Restauração do Upload Vulkan de Textura):**
   Ao remover os blocos de bypass `WddmAllocationUploaded` de `Bc250UmdVkCreateResource` e `Bc250UmdSyncTextureToVkImage`, garantindo que `Bc250UmdVkUploadImageData` seja invocado para carregar dados via staging buffer e aplicar barreiras de transição explícitas (`UNDEFINED -> TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL`) com fence física confirmada, a `VkImage` correspondente passa a ter conteúdo legítimo e estado de hardware coerente em silício?

2. **Pergunta 2 (Eliminação do Stall `Cid=10` no Draw 9):**
   Com a textura devidamente populada na memória Vulkan e com os metadados de hardware devidamente transicionados e validados por fence antes do draw, o Draw 9 executa sem gerar fault de translação `Cid=10` na leitura de textura, eliminando o hang no `WAIT_REG_MEM` e permitindo que a submissão de desenho seja concluída com avanço das fences?

---

## 2. O que Muda no Candidato 3.227.1.0

1. **UMD Vulkan Resource (`driver/umd/bc250_umd_resource_vk.c`):**
   - No tratamento de `pInitialDataUP` em `Bc250UmdVkCreateResource`: removido o bypass condicionado a `pRes->WddmAllocationUploaded`. `Bc250UmdVkUploadImageData` é invocado diretamente para carregar os texels do subrecurso 0 e efetuar a transição de layout na GPU.
2. **UMD D3D10/11 Principal (`driver/umd/bc250_d3d_umd.c`):**
   - Em `Bc250UmdSyncTextureToVkImage`: removido o bypass condicionado a `res->WddmAllocationUploaded`. Todas as sincronizações de textura geradas por `ResourceUpdateSubresourceUP` ou `ResourceUnmap` passam a invocar `Bc250UmdVkUploadImageData`, mantendo o `res->VkImg` plenamente sincronizado com o shadow e com layout garantido por fence física.
3. **Pacote & INF (`driver/wdk/package/bc250mcdm.inf`):**
   - Incremento da versão do driver para `3.227.1.0`.

---

## 3. Desfechos Previstos (Lei 10)

### Desfecho A — Sucesso no Draw 9 e Continuidade do Render (Meta Principal)
- **Assinatura:**
  - `[VK_RES] UploadImageData SUCESSO` registrado para a textura `590x800` (e demais texturas do DWM).
  - Draw 9 conclui com `DRAW fence concluida` sem timeout (`res=-4`) no `FlushCommands`.
  - Zero stalls no `WAIT_REG_MEM` e zero TDRs causados por `Cid=10` em `0x2441` ou `0x2431`.
  - Fences GFX avançam além de 10.
- **Implicação:** O pipeline de amostragem de textura via Vulkan na BC-250 está funcional e sincronizado. Avança para a apresentação completa e mitigação de eventuais bloqueios na cadeia de swapchain/scanout.

### Desfecho B — Upload Vulkan Passa, mas Draw 9 Falha por Outro Motivo
- **Assinatura:**
  - `UploadImageData SUCESSO` registrado para todas as texturas.
  - Draw 9 falha ou sofre TDR com assinatura diferente: outro `Cid` (ex: `Cid=9`, `Cid=0`), outro opcode de pacote ou outro endereço de proteção que não corresponda à perda de 8 bits do descritor de textura.
- **Implicação:** A textura foi carregada e transicionada com êxito; o bloqueador passa a ser outro componente de shader ou formato de recurso na pipeline.

### Desfecho C — Falha na Execução de Bc250UmdVkUploadImageData
- **Assinatura:**
  - `UploadImageData RECUSADO: flush/fence devolveu res=-4` registrado no log da UMD.
  - Recusa limpa (fail-closed) no descritor de desenho por ausência de conteúdo válido.
- **Implicação:** A cópia buffer-to-image via staging buffer sofre timeout ou rejeição na GPU. Investigar o mecanismo de transferência de dados do staging buffer.

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
