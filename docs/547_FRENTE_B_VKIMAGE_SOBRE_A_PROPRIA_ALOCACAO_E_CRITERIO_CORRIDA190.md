# 547 — Frente B: a VkImage passa a usar a própria alocação WDDM (sem duplicar, sem copiar) — critério da Corrida 190

> 30/09/2026, offline. Candidato **3.335.1.0**. **NÃO VALIDADO EM HARDWARE.**
> Motivação medida: [doc 546 §6.3](546_CORRIDA188_TABELA_DE_OBJETOS_CHEIA_E_CANDIDATO189_T47_I1C.md).

## 1. O problema, com os números da Corrida 189

- **Cada textura D3D ocupa a memória duas vezes:**
  - a alocação WDDM da UMD, linear, de tamanho inteiro (`bc250_d3d_umd.c:1408`);
  - a memória própria da RADV para a imagem otimizada (`bc250_umd_resource_vk.c`, `vkAllocateMemory`).
- **O conteúdo só passa da imagem para a alocação WDDM no Present** (`Bc250UmdBridgeLinearToWddm`).
- **Superfície aberta de outro processo:** a UMD criava uma imagem **nova** e copiava para ela (camadas 1/2, e a T4.6 a cada quadro).
- **Na 189:** a imagem nova de 12.386.304 bytes encontrou o segmento de ~257 MB cheio (`MakeResident 0xC0000017`). O DWM recriou o device e a máquina travou 3 s depois.

## 2. O desenho

**Fatos de leitura que o sustentam:**
- **O espaço de GPUVA é por processo e adaptador, não por device** (P1):
  - `D3DKMT_FREEGPUVIRTUALADDRESS` leva `hAdapter` (`radv_wddm2_bo.c`, liberação de VA);
  - a RADV já reparte a faixa de VA do processo com a UMD (`RADV_WDDM2_HEAP_START = 0x240000000`);
  - o KMD instala uma raiz de tabela de páginas por processo (`CTX_PROBE ... Proc=`, P0 nos logs).
- **Consequência:** uma VA que a UMD mapeou vale nos contextos da RADV do mesmo processo.
- **A submissão da RADV no WDDM2 não usa o handle da alocação:** `cs->buffers` é só um conjunto local. O handle aparece só em residência, liberação de VA e destruição (P1, `radv_wddm2_cs.c`, `radv_wddm2_bo.c`).
- **A RADV aceita imagens LINEARES** para amostragem, alvo de cor com blend e cópia (`radv_formats.c:426-463`).

**RADV** (árvore `mesa-radv-wddm`, fora deste repositório):

| Mudança | Onde |
|---|---|
| `buffer_from_gpu_va` no **fim** da `struct radeon_winsys` | `radv_radeon_winsys.h` |
| BO "estrangeiro": só VA e tamanho; sem `CreateAllocation`, `Map`, `MakeResident`, `Evict`, `FreeGpuVirtualAddress` nem `DestroyAllocation`; sem CPU. Logs `FRENTE_B from_gpu_va` e `FRENTE_B destroy` | `winsys/wddm2/radv_wddm2_bo.c` (+ campo `is_foreign_va` em `.h`) |
| Estrutura privada `RadvBc250ImportGpuVaInfo` (sType `0x42C25001`, magic `0xBC25FB01`) encadeada em `VkMemoryAllocateInfo` | `radv_bc250_import.h`, `radv_device_memory.c` |

- **Armadilha pega antes de virar corrida:** a primeira versão inseriu o ponteiro no **meio** da struct, e o ninja recompilou só 4 arquivos. Os outros objetos teriam chamado a função errada.
- **Correção:** o campo foi para o fim da struct, os 108 objetos da `vulkan_radeon.dll` foram apagados e recompilados, e o comentário no código registra o porquê.
- **DLL nova:** `5447A17D…`.

**UMD:**

| Mudança | Onde |
|---|---|
| `Bc250UmdVkTryCreateAliasedImage`: imagem `VK_IMAGE_TILING_LINEAR`, `initialLayout = PREINITIALIZED`, memória importada pela GPUVA da própria alocação. Aceita só se a RADV confirmar: formato com suporte linear para o uso, `offset == 0`, `rowPitch == RowPitch`, tamanho ≤ alocação e VA alinhada. Senão segue o caminho atual (memória própria), com o motivo no log `FRENTE_B NAO` | `bc250_umd_resource_vk.c` |
| Política pura `Bc250UmdFrenteBPreCheck` / `Bc250UmdFrenteBLayoutCheck` | `bc250_umd_caps_probe.c/.h` |
| Superfície aberta com alias: `Bc250UmdVkAdoptAliasedContent` faz a barreira `PREINITIALIZED → SHADER_READ_ONLY`, **sem cópia**; camadas 1–3 e T4.6 não rodam para ela | `bc250_umd_descriptors.c` |
| Rotação de identidade leva `VkMemAliased` junto | `bc250_d3d_umd.c` |
| Espelho do contrato, com assert de tamanho (40 bytes) | `bc250_radv_import.h` |
| Carga de `vkGetImageSubresourceLayout` e `vkGetPhysicalDeviceFormatProperties` | `bc250_umd_vulkan_bridge.c/.h` |

**Fora do alias, de propósito:** mips > 1, array > 1, MSAA e depth (sem layout linear). Esses ficam com memória própria, como hoje.

**Dívida declarada:**
- **Present redundante:** com alias, o Present ainda faz "imagem → buffer linear → alocação", ou seja, copia a memória sobre si mesma (o mesmo conteúdo). É custo, não erro. Remover é o passo seguinte, depois de a corrida provar o alias.
- **Tearing possível:** não há sincronização entre o produtor (menu) e o DWM além da que o dxgkrnl já faz no Present. O DWM pode ler um quadro em escrita.

## 3. Provas offline

- **Builds:**
  - RADV: `ninja` com EXIT 0 e 110/110, `FRENTE_B from_gpu_va` no binário;
  - UMD: 0 erros, 0 avisos, todas as suítes PASS.
- **Teste novo `TestFrenteBAliasPolicy`**, com os números da 189:
  - aceita 2592x1088, pitch 10496, 11.419.648 bytes, VA `0x202C90000`;
  - recusa, uma regra de cada vez: sem alocação, VA zero, mips, array, MSAA, depth, altura maior que a alocação, pitch 10368 (sem o alinhamento de 256 B), o tamanho otimizado de 12.386.304, offset ≠ 0, VA desalinhada e tamanho zero.
- **Mutação:** relaxar a regra do pitch reprova na asserção certa (`line 1291`).
- **Portão novo `audit_frente_b_alias_is_bounded`:**
  - exige a política, o LINEAR, o PREINITIALIZED e a adoção;
  - exige que a T4.6 não recopie quando há alias e que a rotação leve o vínculo;
  - exige o mesmo sType/magic na UMD e na RADV.
  - Mutação (T4.6 recopiando com alias) reprova.
- **Pacote 3.335.1.0:**
  - **Artefatos:** SYS `8000EE83…` (idêntico ao da 189), UMD `8D2FD05B…`, INF `CFE6D98F…`, CAT `FE66A385…`.
  - **Auditorias:** as 4 passam.
  - **Strings conferidas na UMD:** `FRENTE_B`, `superficie aberta lida na propria memoria`, `I.1c`.
  - **Pré-check:** `prepare-corrida190-frenteb.ps1` com EXIT 0.

**O que o offline não prova:** que a VA da UMD traduz nos contextos da RADV, que o pitch da RADV bate na prática e que o menu aparece. Só a corrida diz.

## 4. Critério da Corrida 190 (escrito antes, Lei 10)

**Procedimento:**
1. Boot frio.
2. Rodar `tools\EXECUTAR_CORRIDA190_FRENTEB_600S.cmd`.
3. Abrir o menu umas 5 vezes desde cedo, anotando a hora e o que aparece.
4. Uma vez, digitar na Pesquisa.

| Desfecho | Assinatura (o que os logs mostram) | Implicação |
|---|---|---|
| **A — alias funcionou** | `FRENTE_B SIM` nos logs do dwm.exe e do Explorer; `FRENTE_B ADOTADA` para as superfícies do menu; **zero** `MakeResident failed` nos `-RADV`; nenhum `DestroyDevice` do dwm.exe depois de abrir o menu; o Daniel vê ícones e textos | A duplicação era a causa da falta de memória. Próximo: tirar a cópia redundante do Present e medir o custo por quadro |
| **B — alias recusado** | Predomínio de `FRENTE_B NAO`, com um motivo dominante (ex.: `pitch da RADV difere`) | O motivo nomeado escolhe a correção (alinhar o pitch na criação, etc.). Comparar a contagem de `CreateAllocation2` grandes com a 189 |
| **C — alias aceito, imagem errada** | `FRENTE_B SIM/ADOTADA`, mas o menu aparece preto, listrado ou com cores trocadas, ou `DRAW_READBACK` com grade zerada onde antes havia conteúdo | Layout ou ordem: o conteúdo não está onde a imagem lê. Investigar o pitch efetivo e a sincronização produtor/DWM |
| **D — a VA não vale na RADV** | `GFXHUB_FAULT`, TDR ou `DEVICE_LOST` logo depois dos primeiros `FRENTE_B SIM`, com endereço dentro de uma VA importada | **A hipótese da VA por processo cai.** Voltar ao 3.334.1.0 e importar por handle compartilhado |
| **D2 — a RADV recusou a importação** | `RADV recusou a importacao por GPUVA` na UMD, ou `FRENTE_B RECUSADO` no `-RADV` | Problema de encanamento (sType/magic, tamanho); corrigir offline |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar |

**Notas:**
- **A trava dura continua possível**, porque o mecanismo físico dela não foi tocado. Se acontecer, registrar se veio depois de um `DestroyDevice` do DWM (o padrão da 189) ou sem ele. Isso também informa.
- **Desfecho parcial não conta (Lei 10.3):** A exige os quatro sinais.

## 5. Corrida 190 (30/09 22:44, 3.335.1.0) — o alias funcionou, mas apagou os buffers do DWM

- **Pasta:** `docs/evidence/2026-09-30_22-44-31-DESKTOP-HQAG6T4-lab-binding/`.
- **Duração e saída:** 600 s sem trava; `RECOVERY_OK_ONLINE` (BasicDisplay Código 0 em 5 amostras).
- **O que o Daniel viu:** tela preta, exceto em volta do que se mexia (o cursor); o menu abriu rápido e com ícones.

1. **Houve avanço?** Sim, funcional e de diagnóstico, com uma regressão visível.
2. **Sucessos (P0):**
   - o alias funcionou em todos os processos: dwm.exe com 60 `FRENTE_B SIM` e 0 `NAO`, StartMenuExperienceHost com 224 SIM, Pesquisa, ShellExperienceHost;
   - 4 superfícies abertas adotadas sem cópia (`FRENTE_B ADOTADA`) e 0 `T4.6 RECOPIA`;
   - **0 `MakeResident failed` no dwm.exe**, contra a falha que derrubou a 189;
   - **a VA da UMD vale nos contextos da RADV:** zero `GFXHUB_FAULT`, TDR e timeout (o desfecho D não ocorreu);
   - menu rápido e com ícones (Daniel).
3. **Falhas:**
   - **Regressão (P0):** só 6 de 61 Presents com a superfície cheia, contra 28 de 29 na 188. A primeira queda foi do Present #6 (256/256) para o #7 (0/256), e a cópia de apresentação (`COMPOSITE_BLT`) partiu de um buffer linear zerado (`INCONCLUSIVO_ZERO`). **O mecanismo não foi identificado:**
     - descartados por medição: FILL, DISCARD e TRANSFER sobre o buffer, e o render pass que limpa;
     - a inspeção do buffer linear pela CPU dizia 86.400/86.400 não-zero no mesmo Present, o que indica que essa inspeção **não é prova** (H: lê um mapeamento antigo).
   - Explorer: 346 `MakeResident 0xC0000017` em pedidos de 64 KiB (0 na 188). Leitura (H): com a tabela de objetos 4× maior (T4.7), o limite passou a ser o segmento, e não a tabela. 34 texturas do Explorer ficaram fora do alias por terem mips.
   - O dwm.exe criou o device 4 vezes; a causa não foi investigada.
4. **Veredito:** **parcial.** Ocorreram sinais de A (SIM, ADOTADA, zero OOM no DWM, menu com ícones), mas a tela preta fora do cursor é uma assinatura que nenhum desfecho previa, então cai em **E**: render target com alias quebra a persistência dos buffers do DWM.
5. **Pendências:**
   - o mecanismo da perda de conteúdo continua **H**;
   - a falta de memória do Explorer continua aberta (mips fora do alias).

## 6. Frente B v2 (3.336.1.0) e critério da Corrida 191

- **Mudança única:** render target volta a ter memória própria (`Bc250UmdFrenteBPreCheck` com `RenderTarget` → recusa nomeada).
- **Com alias continuam:** texturas e superfícies abertas, inclusive o lado do DWM que lê o menu.
- **Inalterados:** SYS `8000EE83…` e RADV `5447A17D…`. UMD nova `8F72CBE0…`.
- **Teste e portão:** caso novo no teste, com mutação verificada; portão estendido. Auditorias passam e `prepare-corrida191-frentebv2.ps1` dá EXIT 0.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Presents com superfície cheia em proporção igual à 188 (≥ 90%), `FRENTE_B ADOTADA` > 0, menu com ícones, e o desktop visível inteiro (Daniel) | A perda de conteúdo era do alias em render target. Investigar o mecanismo offline antes de religá-lo |
| **B** | Tela preta fora do cursor de novo | Não era (só) o render target; olhar as texturas com alias e a cópia de apresentação |
| **C** | Desktop volta, mas o menu volta a ficar cinza ou lento | O alias das superfícies abertas depende do render target do produtor ter alias; ligar só no produtor |
| **E** | Fora do mapa | Descrever antes de julgar |

## 7. Corrida 191 (01/10 07:20, 3.336.1.0) — desktop inteiro de volta; um TDR de GFX e trava dura

- **Pasta:** `docs/evidence/2026-10-01_07-20-08-DESKTOP-HQAG6T4-lab-binding/`.
- **Logs recuperados depois do reboot:**
  - log do KMD resgatado do arquivo cumulativo (`bc250_display_log-tail-recovered.txt`, SHA-256 `E87FD8F9…`);
  - logs de AppContainer copiados das pastas dos pacotes para `umd-logs-appcontainer/`.
- **Relato do Daniel:** 07:24, travada ao abrir o menu; 07:25, o menu não abriu; ~07:27, travada, tela preta e reset.

1. **Houve avanço?** Sim. A v2 devolveu o desktop inteiro: **51 de 51 Presents com a superfície cheia**, contra 6/61 na 190 e 28/29 na 188. É o desfecho A da §6 no eixo do desktop.
2. **Sucessos:** desktop íntegro; zero `GFXHUB_FAULT`; o TDR foi recuperado (`TDR_RESTART PROVADO`, `Display 4101` às 07:24:08, "parou de responder e se recuperou").
3. **Falhas:**
   - **TDR de GFX às 07:24:08, o primeiro em 10 corridas (182–190 tiveram zero):**
     - a submissão travada foi do **menu** (StartMenuExperienceHost 6696), às 07:24:04.800: 2 desenhos num alvo 16x16, com pipelines recém-compilados;
     - o 2º desenho lê uma textura 102x102 `Usage=3` (STAGING) e um buffer de texels de 512 KiB;
     - no TDR: `GUI_ACTIVE=1`, `CP_BUSY=1`, `EopDoneBusy=1`, `GfxContextBusy=1`, ou seja, o desenho em execução não terminou;
     - a preempção pedida pelo Windows esperou essa submissão (`PREEMPT_WAIT ... EmVoo=1`);
     - **causa: H.** As candidatas são a textura STAGING com alias e o shader recém-traduzido. Se essa textura tinha alias **não ficou registrado**, porque o log da Frente B corta depois das 32 primeiras decisões por carga da DLL.
   - **Depois do TDR:** um dwm.exe novo (11148) e recriação de recursos.
   - **Trava dura (~07:28):** o log do KMD termina num `TRANSFER` de paginação de 11.075.584 bytes (uma primária) submetido à SDMA, sem conclusão. É a mesma família das travas de FILL e TRANSFER, outra vez depois de uma rajada de recriação.
4. **Veredito:** **parcial.**
   - O critério da §6 fecha **A no desktop**.
   - O menu não pôde ser avaliado depois das 07:24, por causa do TDR.
   - O TDR e a trava ficam fora do mapa (E).
5. **Recuperação:**
   - reset do Daniel e rollback do watchdog no boot (BasicDisplay Código 0);
   - **NÃO VALIDADOS EM HARDWARE:** a causa do TDR e a causa física da trava.

### 7.1 Candidato 3.337.1.0 (Corrida 192): só instrumentação

- **Mudança:** a linha `CreateResource` registra `Alias=0/1` para **todo** recurso, sem o corte das 32 primeiras. Num próximo TDR, o recurso lido pelo desenho travado poderá ser classificado.
- **Comportamento:** idêntico ao da 191.
- **Artefatos:** UMD `A1300553…`, SYS e RADV inalterados. Auditorias passam; `prepare-corrida192-frentebv2.ps1` EXIT 0.

**Critério da 192:**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Desktop inteiro (≥ 90% de Presents cheios), menu com ícones várias vezes, zero `TDR_QUIESCE` | O TDR da 191 foi isolado (a taxa de base continua a ser estimada) |
| **B** | TDR de novo, e os recursos lidos pelo desenho travado têm `Alias=1` | A Frente B em textura lida pela GPU é suspeita; restringir o alias e repetir |
| **C** | TDR de novo, com `Alias=0` nos recursos do desenho | O alias está fora; investigar o shader (hash) e a tradução |
| **E** | Fora do mapa | Descrever |

## 8. Corrida 192 (01/10 07:41, 3.337.1.0) — o mesmo IB trava a GPU lendo uma textura com alias

- **Pasta:** `docs/evidence/2026-10-01_07-41-13-DESKTOP-HQAG6T4-lab-binding/`.
- **Logs:** KMD resgatado do arquivo cumulativo; AppContainer copiados para `umd-logs-appcontainer/`. Os logs do dwm.exe e do Explorer ficaram em `C:\Windows\Temp`; o próximo bind os arquiva como residuais.
- **Relato do Daniel:**
  - 07:42, travou ao abrir o menu, ~1 s de tela preta, e depois o menu abriu com ícones;
  - 07:43, o menu abriu e os ícones vieram em ~1 s;
  - ~07:44, tela preta e trava.
- **Eventos do Windows:** `Display 4101` às 07:42:27 (TDR recuperado); Kernel-Power 41 / EventLog 6008 no boot das 07:46 (desligamento sujo).

1. **Houve avanço?** De diagnóstico, sim. O TDR passou a ter causa delimitada.
2. **Sucessos:**
   - o TDR foi recuperado e o menu abriu com ícones em ~1 s (07:43);
   - a instrumentação `Alias=0/1` respondeu na primeira tentativa.
3. **Falhas e medições (P0):**
   - **O IB travado é idêntico ao da 191:** VMID 10, `DW=1064`, `Fnv64=4092CF3C8621F809`.
   - O processador de comandos parou em `WAIT_REG_MEM` logo depois de `RELEASE_MEM` / `EVENT_WRITE`, com `EopDoneBusy=1` e `GfxContextBusy=1`: esperava o fim de desenhos que não terminaram.
   - **Quem submeteu:** ShellExperienceHost (11860). O lote tinha 2 desenhos; o 2º usa os **mesmos shaders da 191** (VS `D86B7E93`, PS `59C609E5`) e lê:
     - um buffer de texels de 524.288 bytes, com `Alias=0`;
     - uma textura **256x80 `Alias=1`** (LINEAR sobre a própria alocação).
   - **Trava dura:** o log termina logo depois de um FILL de paginação de 4 KiB, ~3 min depois do TDR e da recriação. É a mesma família, e de novo pós-recriação.
4. **Veredito:** é o **desfecho B** do critério da §7.1: TDR de novo, com o recurso lido marcado `Alias=1`.
5. **Pendências:**
   - **Por que uma textura LINEAR com alias trava esse shader: H.** As candidatas são o descritor de imagem linear (a RADV trata a placa como GFX10.3; memória `bc250-radv-identifica-gfx1013-como-gfx10-3`) e o conteúdo e a sincronização.
   - Na 190, com alias ainda mais amplo, **não houve TDR**: o desenho pode não ter ocorrido ali. Não se conclui nada da ausência.

### 8.1 Frente B v3 (3.338.1.0), critério da Corrida 193

- **Mudança única:** alias **só em superfície aberta de outro processo** (`OpenedSurface`, o DWM lendo o menu: o ganho medido na 190). Texturas próprias voltam à imagem otimizada com memória própria.
- **Provas offline:** teste com o caso 256x80 recusado; mutação verificada; portão estendido; auditorias passam; `prepare-corrida193-frentebv3.ps1` EXIT 0.
- **Artefatos:** UMD `5CEC87A9…`, SYS e RADV inalterados.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Zero `TDR_QUIESCE`, desktop inteiro (≥ 90%), menu com ícones em várias aberturas, `FRENTE_B ADOTADA` > 0 | A textura própria com alias era a causa do TDR. Investigar o descritor linear offline antes de religar |
| **B** | TDR com o mesmo `Fnv64=4092CF3C8621F809`, e o 256x80 com `Alias=0` | O alias não era a causa; investigar os shaders `D86B7E93`/`59C609E5` e o buffer de texels |
| **C** | Zero TDR, mas o menu volta a ficar cinza ou lento | O ganho do menu dependia do alias nas texturas do produtor |
| **E** | Fora do mapa | Descrever |

## 9. Corrida 193 (01/10 07:57, 3.338.1.0) — sem TDR; a falta de memória voltou e o DWM entrou em laço de recriação

- **Pasta:** `docs/evidence/2026-10-01_07-57-05-DESKTOP-HQAG6T4-lab-binding/`.
- **Roteiro:** o Daniel só rodou, sem mexer em nada, e a máquina travou.
- **Eventos do Windows:** nenhum `Display 4101` (zero TDR). Kernel-Power 41 no boot das 08:49.

1. **Houve avanço?** De diagnóstico, sim: a causa do TDR e a parede de memória ficaram separadas por medição.
2. **Sucessos (P0):**
   - **zero TDR** (`TDR_QUIESCE` = 0) e zero `GFXHUB_FAULT`;
   - os **mesmos shaders do IB travado** (VS `D86B7E93`, PS `59C609E5`) e o mesmo tipo de desenho (`SRV=1 SRV_BUFFER=1`) rodaram **milhares de vezes sem alias** (1.931 no menu, 33 no ShellExperienceHost), com as esperas concluídas.
   - Junto com a 191/192, isso atribui o TDR à **textura própria com alias** (P2: duas corridas com TDR e alias, uma sem TDR e sem alias, mesmo desenho). O mecanismo continua H.
3. **Falhas (P0):**
   - o 1º dwm.exe (1272) fechou o adaptador às 07:58:38;
   - o 2º (11160) entrou em **laço de recriação do device: 242 `CreateDevice`/`DestroyDevice`, um a cada ~1,7 s**;
   - **causa de cada volta:** `vkAllocateMemory (Image) falhou: res=-2` numa textura 2560x1080 (imagem otimizada de 11.796.480 bytes). Sem alias, a memória duplicada voltou e o segmento não comporta;
   - a trava dura veio às 08:06:12, com o log do KMD terminando num **FILL de 11.075.584 bytes** (uma primária recriada) sem conclusão.
4. **Veredito:** é o desfecho **B/E** do critério da §8.1 no eixo da memória: zero TDR, mas o sistema não fica de pé sem alias.
5. **A parede, medida:**

| Medida | Valor |
|---|---|
| `VramTotalMiB` (sondagem) | 512 MiB |
| `CpuVisibleMiB` / BAR | 256 MiB |
| Segmento declarado (`RuntimeCapsLocalLow32=0xEF74000`) | ~251 MB |

**Metade da VRAM reservada pela placa não é declarada ao Windows.**

### 9.1 Próximo passo proposto: M2, um segundo segmento local sem acesso de CPU

- **O que é:** declarar `[256 MiB, 512 MiB − reservas)` como **segmento 2**: local à GPU e **não** visível à CPU.
- **Quem vai para lá:**
  - as alocações que a RADV cria com `RADEON_FLAG_NO_CPU_ACCESS`, que são as imagens otimizadas, justamente as que estouram;
  - quando o tipo de memória é só `DEVICE_LOCAL`, a winsys já **não** marca `BC250_ALLOCATION_FLAG_CPU_VISIBLE` (`radv_wddm2_bo.c:510`).
- **Ganho imediato:** ~+250 MB (a memória de vídeo útil quase dobra), **sem mudar a BIOS**.
- **Se a BIOS permitir UMA maior** (P2 da comunidade: opções até vários GB), esse mesmo segmento cresce sem código novo.
- **Por que agora rende, se o doc 496 §2 dizia o contrário:** naquela época a pressão vinha de BOs `CpuVisible=1`. Na 189 e na 193, a pressão é de imagens otimizadas, que não precisam de CPU.
- **Riscos e trabalho:**
  - os caminhos do KMD que endereçam por MC (composição, Present, gate do IB) precisam do mapeamento segmento→MC também para o segmento 2;
  - o `EvictionSegmentSet` e os conjuntos suportados precisam ser coerentes (lição do 0x10E da T3.8);
  - auditar tudo offline antes de qualquer corrida.

**Pendente com o Daniel:** conferir na BIOS (P3.00) se existe opção de UMA / Frame Buffer Size e qual valor está selecionado. Só anotar, sem mudar.