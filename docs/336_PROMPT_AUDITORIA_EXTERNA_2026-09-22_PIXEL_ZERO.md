# Prompt para auditoria externa — BC-250 WDDM: "draws corretos, zero fragmento"

> Cole tudo abaixo da linha em outra IA. Se ela aceitar anexos, anexe também os arquivos da
> seção 9. Escrito em 22/09/2026, depois da Corrida 71.

---

Você vai fazer uma **auditoria técnica independente** de um projeto de driver gráfico. Não quero
encorajamento nem resumo do que eu já sei: quero que você **ache o que estamos deixando passar**,
aponte erros de raciocínio, hipóteses que descartamos cedo demais e medições em que confiamos sem
prova. Seja específico e cite o elo exato da cadeia a que cada ponto se refere.

## 1. O projeto em uma frase

Driver WDDM 2.7 próprio para Windows 10 22H2 (build 19045) na **AMD BC-250** (APU de mineração,
PCI `1002:13FE`, GPU **GC 10.1.3 "Cyan Skillfish"**, RDNA1/GFX10.1, 24 CUs, 16 GB GDDR6
compartilhados), com o objetivo de rodar o desktop acelerado (DWM) e jogos sem WARP e sem
fallback por CPU. Não existe driver oficial da AMD para Windows nessa placa.

## 2. Arquitetura

- **KMD** próprio (`bc250mcdm.sys`, miniport WDDM 2.7). Traz o GPU sem ajuda do firmware do
  driver oficial: carrega RLC/CP/MEC/SDMA diretamente, programa GCVM (tradução de memória por
  VMID; um VMID por contexto), e submete os IBs dos apps no anel gráfico **CP_RB0**.
- **Display:** herdado do **UEFI GOP**. Não programamos o DCN; o scanout é a superfície que o GOP
  deixou, em VRAM. O Present copia a imagem para o scanout com **SDMA**.
- **UMD** própria (`bc250umd.dll`) que implementa as DDIs D3D10/11/DXGI e **traduz para Vulkan**:
  DXBC → SPIR-V com **vkd3d-shader**; comandos → Vulkan chamando diretamente o ICD.
- **ICD Vulkan:** fork do **Mesa RADV** (base `lfrb/mesa` ramo WDDM2, Mesa 26.2.0-devel, commit
  `801c976`) com um winsys `wddm2` que usa D3DKMT (CreateAllocation, MapGpuVirtualAddress,
  SubmitCommand, fences monitoradas). Compilador ACO, **sem LLVM**.
- **Cadeia de um draw do DWM:** DWM → DDI D3D11 → UMD → `vkCmdDraw*` → RADV monta PM4 → winsys
  concatena preâmbulo + CS num IB → `D3DKMTSubmitCommand` → KMD põe `INDIRECT_BUFFER` + `RELEASE_MEM`
  no CP_RB0 com o VMID do contexto → CP executa.

## 3. O que está PROVADO no hardware (não precisa reprovar, mas pode questionar a prova)

- O anel gráfico consome e aposenta: 446/446 submissões na última corrida, fences escritas pela GPU
  sobre valor-veneno.
- Tradução de memória: **zero faults GCVM** nas corridas 68–71. Antes da 68 havia faults
  `Cid=10` (SQC data) em `VA>>8`; a causa foi um bug do winsys (o IB concatenado era montado
  **dentro do CS do preâmbulo** e o tamanho nunca voltava, então cada submissão reexecutava todos os
  draws anteriores com descritores já liberados). Corrigido: IB em BO próprio por submissão.
  Verificado com despejo dos IBs (não há mais IB cumulativo).
- Upload de textura: o DWM entrega texturas com conteúdo (medido na origem, na memória da CPU:
  textura 2560x1080 com 100% das amostras não-nulas, janela 873x641 com 98%) e o upload
  (staging → `vkCmdCopyBufferToImage` → fence) completa.
- Present por SDMA para o scanout do GOP funcionou em 12/09 com imagem gerada por **compute**
  (584 quadros, prova byte a byte). **Mas nunca com imagem gerada por draw gráfico.**

## 4. A parede atual (Corrida 71) — com números

- Operador: **tela preta** até o watchdog restaurar o BasicDisplay.
- O DWM faz ~90 draws por processo no alvo 2560x1080 que depois apresenta (`RT0` do draw ==
  `hSurfaceToPresent`; mesma `VkImage` no RTV, no draw e na leitura).
- **Vértices realmente desenhados** (índices lidos do index buffer R16, `BaseVertex=0`), em pixels
  de tela: faixa `(0,0)–(2560,259)` cor `FFFFFFFF`; janela `(237,259)–(1094,861)` `FFFFFFFF`; barra
  `(0,1040)–(2560,1080)` `FF000000`; z = 1.0. Constant buffer do VS: `[0.00078, -0.00185, -1, 1]`
  = transformação tela 2560x1080 → NDC. Scissor e viewport = tela inteira; `CullMode=NONE`;
  preenchimento sólido.
- **Programas ACO** (via `VK_KHR_pipeline_executable_properties`): o PS de um draw **sem textura**
  é só `v_interp` de `attr0` → `v_cvt_pkrtz` → `exp mrt0 compr done vm`. O PS texturizado é
  `attr0 × image_sample`. O VS roda como **NGG** (`HW (NEXT_GEN_GEOMETRY_SHADER)`) com código de
  **culling de primitivas no shader** (transformação de viewport vinda de SGPRs).
- **Leitura do alvo logo após cada draw** (`vkCmdCopyImageToBuffer` para um buffer
  HOST_VISIBLE|HOST_COHERENT, depois `vkMapMemory` e grade de 1 linha em 4 × 1 pixel em 8): 84
  leituras, **todas zero** — inclusive logo após um draw **sem textura, cor branca**, num retângulo
  com ~750 pontos da grade dentro.
- Todos os Presents: grade 0/86.400.

## 5. Pontos cegos que NÓS já identificamos (verifique e amplie)

1. **O instrumento de leitura nunca teve controle positivo.** Em 688 inspeções desde 17/09
   nunca apareceu um valor não-nulo. O harness de conformidade limpa para azul
   `(0.15,0.45,0.90,1)` e desenha um triângulo — seria o controle ideal —, mas no processo dele o
   `vkMapMemory` do buffer linear falha com `VK_ERROR_MEMORY_MAP_FAILED (-5)` e logo depois vem TDR.
   **Não sabemos se "zero" significa "nada foi escrito" ou "a leitura é cega".**
2. **Golden settings do GC nunca aplicados.** O Linux (amdgpu, `gfx_v10_0.c`) aplica uma tabela de
   ajustes por chip. Temos a tabela do Cyan Skillfish (34 registradores: `PA_SC_ENHANCE`,
   `PA_SC_BINNER_EVENT_CNTL_0`, `CB_HW_CONTROL_3/4`, `DB_DEBUG/3/4`, `GB_ADDR_CONFIG`,
   `SQ_ARB_CONFIG`, `TA_CNTL_AUX`, `GL2C_CTRL2/3`, `CH_PIPE_STEER` …) só em teste offline; o KMD não
   aplica nem lê. Também não sabemos o que mais o amdgpu programa na init do GFX que nós não
   programamos (ex.: `gfx_v10_0_constants_init`, `GRBM_CNTL`, `PA_SC_TILE_STEERING_OVERRIDE`,
   configuração de RB/SE, `SPI_CONFIG_CNTL`, `GE_*`, registradores de binning).
3. **NGG.** RADV liga NGG e culling NGG para GFX10. Se NGG ou o culling falhar neste chip/neste
   bring-up, o resultado é exatamente "vértices válidos, zero fragmento".
4. **TDR recorrente de "ondas que não terminam"** (1–2 por corrida): `SQ_DEBUG_STS_GLOBAL Busy=1`,
   240 ondas por SA, CP parado com PFP em `ACQUIRE_MEM` e ME em `EVENT_WRITE`, sem fault. Aparece
   inclusive no **primeiro draw do harness** (um triângulo simples). Pode ser o mesmo defeito.

## 6. Hipóteses que já descartamos (diga se descartamos errado)

- DCC em textura (o fault era do SQC, que não lê DCC; e o `RADV_DEBUG=nodcc` não mudou nada).
- Falta de invalidação de cache entre IBs (o RADV abre cada IB com `ACQUIRE_MEM` GLI/GLK/GLV/GL1/GL2).
- Textura amostrando zero (draw sem textura também sai zero).
- Imagem errada (mesma `VkImage` em RTV, draw e leitura).
- Scissor/viewport/culling (tela inteira; `CullMode=NONE`).
- `storeOp` descartando o resultado (`STORE` nos dois render passes; draws usam `loadOp=LOAD`).
- Estado D3D11 perdido na tradução — corrigido na última faixa: fatores de blend (antes nunca
  mapeados), dados por instância, stride 0, `location=InputRegister`, formatos X8/sRGB, viewport
  com altura negativa (D3D → Vulkan). Nenhuma recusa nova apareceu.

## 7. O que eu quero de você

1. **Lista priorizada de causas possíveis** para "draw executa (fence retira), vértices e CB
   corretos, alvo continua zero", específica para **GFX10.1 com bring-up próprio (sem amdgpu)**.
   Para cada uma: por que ela produziria exatamente estes sintomas, e **qual medição barata a
   confirma ou refuta**.
2. **O que o amdgpu faz na inicialização do GFX10 (e do Cyan Skillfish em particular) que um
   bring-up próprio costuma esquecer** e que afeta rasterização, color buffer, binning ou NGG.
   Cite função/arquivo do kernel quando souber; diga quando não tiver certeza.
3. **Como validar o instrumento de leitura** com um controle positivo que não dependa do DWM
   (por exemplo: `vkCmdClearColorImage` com cor conhecida no alvo seguido da mesma leitura; ou
   escrita por compute; ou `vkCmdFillBuffer` no buffer linear) e o que cada resultado implicaria.
4. **Se o `vkMapMemory` falhando com -5 num processo e funcionando em outro** sugere algo sobre o
   winsys (mapeamento de alocação WDDM, `pfnLockCb`, alocação já mapeada, memória não visível).
5. **Riscos no método:** medições em que confiamos e que podem estar enganando; variáveis que
   mudamos juntas; conclusões tiradas de ausência de linha em log.
6. **O próximo experimento de maior informação por corrida**, sabendo que cada corrida física é
   cara (boot frio, bind, ~2 min de janela, rollback) — idealmente um que separe em um só ensaio
   "nada é rasterizado" / "é rasterizado mas não é escrito" / "é escrito mas a leitura é cega".

## 8. Regras do projeto que sua resposta deve respeitar

- **Sem WARP, sem fallback por CPU**, nada de "copie pela CPU para ver a tela".
- **Nada de escrever em registradores de display (DCN)**; leitura é permitida.
- Registradores só com **procedência** (offset citado de fonte: headers do amdgpu/Mesa); não
  invente endereço.
- Classifique cada afirmação sua: **P1** (derivado de código/documentação upstream que você
  conhece), **H** (hipótese), e diga quando não sabe.
- Toda sugestão de ensaio deve dizer **o que o log vai mostrar** em cada desfecho.

## 9. Arquivos para anexar (se a sua interface aceitar)

- `docs/335_CRITERIO_CORRIDA71_VERTICES_DESENHADOS_E_ALVO_APOS_DRAW.md` (critério da última corrida)
- `BASE_DE_AVANCO.md` (histórico com todas as corridas; o topo tem 66–71)
- `driver/umd/bc250_d3d_umd.c` (função `Bc250UmdExecuteVkDraw` e `Bc250UmdReadbackDrawTarget`)
- `driver/umd/bc250_umd_vulkan_bridge.c` (criação do buffer linear e `Bc250UmdVkInspectLinearPresentBuf`)
- `driver/umd/bc250_umd_resource_vk.c` (criação de imagem, RTV e render passes)
- `driver/core/bc250_golden_settings_contract.c` (a tabela nunca aplicada)
- `docs/336A_ANEXO_LOGS_CORRIDA71.txt` — já recortado: um draw completo sem textura
  (`[DRAW_DATA]`, `[DRAW_VERTS]`, `[DRAW_READBACK]`, `GRADE`), os `[VK_PIPELINE_IR]` do VS/PS desse
  pipeline, o harness (clear azul, `vkMapMemory -5`, TDR) e o bloco de TDR do KMD (`TDR_GFX_*`)
- `external/radv-wddm/bc250-radv-wddm.patch` (delta do fork do RADV)
