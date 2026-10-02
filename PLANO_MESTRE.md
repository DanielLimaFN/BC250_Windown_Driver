# PLANO MESTRE — BC-250 no Windows

> **Escrito em 01/10/2026**, depois da Corrida 196 e da validação do controle de VRAM pela CMOS.
> Substitui os planos anteriores da raiz (`PLANO_DESKTOP_ACELERADO`, `PLANO_DE_TRABALHO_TRAVA_E_DESKTOP`, `PLANO_EXECUCAO_WDDM_GPU`, `PLANO_PRESENT_E_FAIL`, `IDEIAS_DE_AVANCO`), arquivados em [`DocsObsoletos/planos-superseded/2026-10-01/`](DocsObsoletos/planos-superseded/2026-10-01/).
> **Fonte de verdade por corrida:** [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md). **Regras:** [`CLAUDE.md`](CLAUDE.md) (Leis 1–10) e [`DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md`](DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md).
> **Legenda:** ✅ provado na placa (P0) · 🟡 parcial · ❌ falta · ⛔ bloqueado · ⏸️ dormente por decisão. Nada aqui é marcado ✅ sem prova física arquivada.

---

## 1. Aonde queremos chegar

**Jogos rodando no Windows 10 22H2 na BC-250**, pela GPU da própria placa, com o monitor na saída DisplayPort dela, sem WARP e sem nenhum fallback de software. O desktop do Windows composto pela GPU, estável por horas, sobrevivendo a TDR sem perder a tela.

O caminho escolhido (e mantido):

```text
aplicação / DWM (D3D11, sem modificação)
  → runtime D3D11 + DXGI do Windows
  → nossa UMD (bc250umd.dll): DDIs reais, tradução D3D11 → Vulkan
  → RADV (fork nosso com winsys WDDM) + tradutor DXBC → SPIR-V (vkd3d-shader)
  → nosso KMD (bc250mcdm.sys, WDDM 2.7)
  → GPU (anel gráfico, SDMA, MEC)
  → scanout herdado do GOP (cópia por SDMA)
```

---

## 2. A história em uma página

| Período | O que aconteceu | Resultado |
|---|---|---|
| 26/08 – 01/09 | Bring-up de laboratório (`bc250dbg`): PCI, BARs, PSP, firmware de RLC/PFP/ME/CE/MEC/SDMA, golden registers, tradução GCVM | ✅ placa controlada pelo nosso código |
| 02/09 – 03/09 | Miniport WDDM em **Código 0**; display herdado do GOP; SDMA apresentando no scanout | ✅ driver aceito pelo Windows |
| 04/09 – 10/09 | Escalonador, paginação, GPUVA, TDR; flush de HDP; UMD carregando no DWM | ✅ SDMA estável (19.291 submissões), TDR recupera |
| 11/09 – 15/09 | Marcos Vulkan 1–4 pela RADV: compute, present, swapchain 600 quadros, cubo | ✅ Vulkan executa na placa (M3/M4 eram compute: ver §4) |
| 16/09 | **Diretriz de qualidade** do Daniel: nada de atalho; refazer M3/M4 com `vkCmdDraw`; UMD D3D11 de verdade | regra vigente |
| 17/09 – 24/09 | UMD D3D11 nativa: `CreateDevice`, shaders DXBC→SPIR-V, anel gráfico; triângulo D3D11 composto pelo DWM (Corrida 106) | ✅ D3D11 desenha pela GPU |
| 25/09 – 26/09 | Desktop visível pela GPU (Corrida 134) e **600 s estáveis** (134, 136, 137) | ✅ primeiro desktop acelerado |
| 27/09 – 29/09 | Caça à **trava dura** (Corridas 139–172): dezenas de hipóteses refutadas; T3.3 (SDMA UTCL1) reduz a frequência; 0x10E eliminado (179) | 🟡 trava continua |
| 29/09 – 30/09 | Menu Iniciar: texturas compartilhadas, descritores, tokens, cópias (T4.0–T4.7); ícones aparecem (182) | 🟡 menu funciona às vezes |
| 30/09 – 01/10 | **Memória**: tabela de objetos cheia (188); Frente B (alias sem cópia, 190–193); M2 (segmento de VRAM além do BAR, 194–196) | 🟡 falta memória; M2.1 pronto |
| 01/10 | Descoberta: a VRAM é controlada pela **CMOS APCB**, não pelo menu da BIOS; ferramenta Windows `bc250-vram` gravou 4096 MB e o firmware aplicou | ✅ controle da VRAM no Windows |

---

## 3. Todas as funcionalidades que queremos, e o estado de cada uma

### 3.1 Plataforma e firmware

| Status | Funcionalidade | Situação hoje (evidência) |
|:---:|---|---|
| ✅ | Firmware da GPU carregado pelo nosso driver (PSP, RLC, PFP, ME, CE, MEC, SDMA) | Tabela 1 da BASE |
| ✅ | Recuperação: watchdog, rollback para o BasicDisplay, fusível anti-boot-loop, WinRE | Tabela 1 (itens 7–12b, 174–175) |
| ✅ | **Controle da VRAM no Windows** (`tools\vram-cmos\bc250-vram.ps1`) | 01/10: leu 2048, gravou 4096, firmware aplicou 4096 após boot frio; RAM do Windows caiu exatamente 2 GiB ([doc 550](docs/550_CONTROLE_DA_VRAM_PELA_CMOS_NO_WINDOWS.md), `docs/evidence/vram-cmos/`) |
| ✅ | Driver lê o tamanho e a base da VRAM da placa a cada boot | Corridas 194–196 leram 512 MiB, igual à CMOS |
| ❌ | Temperatura e relógios da GPU/CPU medidos durante as corridas | **Nunca medidos no Windows** (o `bc250dbg` lê as sondas, mas sem escala calibrada). No Linux a CPU chegou a 89 °C sob carga |
| ❌ | Conversa com o SMU (relógios, energia, térmico) | O driver não manda mensagem ao SMU; a placa roda no estado de fábrica |

### 3.2 Driver de kernel (WDDM)

| Status | Funcionalidade | Situação hoje |
|:---:|---|---|
| ✅ | Miniport WDDM 2.7 display+render em Código 0 | Desde 02/09 |
| ✅ | Display herdado do GOP (VidPn comitado, VSync do OTG) | Desde 03/09 |
| ✅ | Segmentos de memória, paginação, GPUVA por processo, tabelas de página | Milhares de draws sem `GFXHUB_FAULT` (182–186) |
| ✅ | Submissão SDMA assíncrona e anel gráfico executando trabalho do DWM | Pixels conferidos 86.400/86.400 (181) |
| ✅ | TDR: detecção, reset, readmissão | 07/09, 17/09; **desktop sobreviveu a um TDR real na 153** (uma vez) |
| ⏸️ | Segmento de VRAM além do BAR (M2) | **Dormente desde 01/10** (decisão dos 256 MB). Provado antes de parar: `Lock2` fechado pelo M2.1 (197) e 2.086 imagens no segmento 3 sem fault (198) |
| ❌ | Memória do sistema para a GPU (M1b, como o GTT do Linux) | Caminho de memória desde 01/10; 3 bloqueios do doc 496 + paginação robusta ([doc 551 §2.3](docs/551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md)) |
| 🟡 | Tabela de objetos do KMD | 4.096 → 16.384 (paliativo T4.7); definitiva não feita |
| ❌ | Prova formal D0.7 (canários no destino do IB gráfico) | Retirada na X4 por risco de leitura pela BAR |
| ❌ | Preempção, prioridades, multi-contexto auditado, suspend/resume | Não iniciados |

### 3.3 UMD D3D11 (o que o DWM e os apps usam)

| Status | Funcionalidade | Situação hoje |
|:---:|---|---|
| ✅ | Runtime D3D11 abre a nossa UMD e cria device (feature level 10_1) | 17/09 |
| ✅ | Tradução de shaders DXBC → SPIR-V | Shaders do DWM e do menu traduzem (184) |
| ✅ | Draw D3D11 vira draw Vulkan na GPU | Triângulo em janela composto pelo DWM (106); grade do DWM (181) |
| 🟡 | Recursos compartilhados entre processos (menu → DWM) | Frente B v3: alias só em superfície aberta de outro processo. Render target com alias → tela preta (190); textura própria com alias → TDR (191/192) |
| 🟡 | Cobertura de DDIs do Explorer | Draws recusados com `draw PM4 ausente`; inventário do DWM feito ([doc 545](docs/545_INVENTARIO_UMD_DWM_E_SHELL_CORRIDAS_181_186.md)) |
| ❌ | Pesquisa / apps AppContainer | `vkCreateDevice -3` 528 vezes, motivo sem canal de log |
| 🟡 | Tradutor de shader **dentro do pacote** | Carregado de `C:\BC250_LIB\libvkd3d-shader-1.dll` (fora do pacote) |
| ❌ | Feature level 11_0+, D3D9/10 transparentes | Não iniciados |

### 3.4 Desktop

| Status | Funcionalidade | Situação hoje |
|:---:|---|---|
| ✅ | Desktop visível desenhado pela GPU | Desde a Corrida 134 |
| ✅ | DWM sem crash a corrida inteira | 183–185 (600 s) |
| 🟡 | Menu Iniciar com ícones e rápido | Ícones em ~1 s na 192; instável entre candidatos |
| ❌ | Explorer sem falta de memória | 346 faltas na 190; laço de 242 recriações na 193 (VRAM declarada de ~251 MB) |
| ❌ | Janelas, texto, transparência, resize, navegador por 60 min (gate DESKTOP-60MIN) | Nunca tentado |
| ❌ | 60 FPS no desktop | ~12–16 FPS com a tela animando (30/09); o modo do GOP é ~56 Hz |

### 3.5 Estabilidade

| Status | Funcionalidade | Situação hoje |
|:---:|---|---|
| 🟡 | 600 s sem trava | Já houve (134, 136, 137, 182–185, 190), mas não é a regra |
| ⛔ | **Sem trava dura** | 189, 191, 192, 193 e 196 terminaram em trava ou reset. Assinatura: um FILL/TRANSFER de paginação no SDMA que nunca conclui, VRAM perdendo conteúdo, CPU sem relógio; abaixo do Windows (sem BugCheck em 48 min, Corrida 157) |
| ❌ | 8 horas (gate ROBUSTEZ-8H) | Nunca tentado |
| 🟡 | TDR com o desktop sobrevivendo (gate TDR-DISPLAY) | Uma vez (153) |

### 3.6 Vulkan, jogos e além

| Status | Funcionalidade | Situação hoje |
|:---:|---|---|
| ✅ | Vulkan executando na placa (RADV + nosso KMD) | Marcos 1–4 |
| 🟡 | Gates gráficos formais M3-GFX e M4-GFX (`vkCreateGraphicsPipelines` + `vkCmdDraw`) | Programas compilados; **nunca rodaram na placa** |
| ❌ | ICD Vulkan instalado para qualquer aplicativo (loader do sistema) | RADV só carregada pela nossa UMD a partir de `C:\BC250_LIB` |
| ❌ | WSI Vulkan integrado ao DWM (janela, não scanout direto) | Não iniciado |
| ❌ | Jogo D3D11 real | Não iniciado |
| ❌ | Jogo Vulkan real | Não iniciado |
| ❌ | D3D12 (UMD D3D12 traduzida) | Não iniciado |
| ❌ | OpenGL (Zink) | Não iniciado |

### 3.7 Display (DCN, opcional para jogar)

| Status | Funcionalidade | Situação hoje |
|:---:|---|---|
| ✅ | Inventário somente leitura do DCN, frame counter | Tabela 1 |
| ❌ | Trocar resolução, EDID real, hotplug, segundo monitor, VRR/HDR, flip sem cópia | P9, depois de tudo |

### 3.8 Produto

| Status | Funcionalidade | Situação hoje |
|:---:|---|---|
| 🟡 | Pacote assinado (teste) com auditorias | Cada candidato; assinatura de teste, `testsigning` ligado |
| ❌ | Instalador/desinstalador de produto, sem tunables de laboratório | Não iniciado |
| ❌ | RADV e vkd3d-shader empacotados com o driver | Hoje vêm de `C:\BC250_LIB` |

---

## 4. Onde avançamos de verdade (provas físicas)

1. **A placa é nossa:** firmware carregado e verificado por nós, sem driver da AMD.
2. **O Windows aceita o driver** como placa de vídeo completa (Código 0, WDDM 2.7), com display herdado do GOP.
3. **Memória virtual da GPU funciona** com o Windows gerenciando: milhares de draws sem falha de tradução.
4. **Trabalho real na GPU:** SDMA (19.291 submissões seguidas), compute (MEC), anel gráfico com pixels conferidos.
5. **Desktop do Windows desenhado pela GPU**, com DWM estável por 600 s e o menu Iniciar com ícones.
6. **TDR funciona** e já devolveu o desktop vivo uma vez.
7. **Diagnóstico forte:** migalhas que sobrevivem à trava, recuperação da cauda de log, coleta FINAL, logs da RADV, inventário de DDIs.
8. **VRAM sob controle:** sabemos quem manda (CMOS APCB), temos a ferramenta no Windows e o driver lê o valor real.

## 5. Onde falhamos (e o que aprendemos)

**Falhas técnicas abertas:**

| # | Falha | Por que importa |
|---|---|---|
| F1 | **Trava dura não resolvida.** Mais de 60 corridas, dezenas de hipóteses refutadas com medição (C-states, HDP, canários, fatiar FILL, leitura pela BAR, aliasing WC/UC). A T3.3 reduziu, não eliminou | Cada corrida pode morrer por ela sem dizer nada sobre a faixa em teste. **Bloqueia todos os gates de estabilidade** |
| F2 | **Nunca medimos temperatura nem separamos "placa" de "driver".** O W1 (estresse com o BasicDisplay) foi escrito e nunca rodou; o Linux de referência não subiu o `amdgpu` | A comunidade relata travas da BC-250 acima de 60–65 °C, e a nossa CPU bateu 89 °C no Linux |
| F3 | **Pouca VRAM declarada** (~251 MB de 512): o Explorer fica sem memória e o DWM recria o device | Desde 01/10 a resposta **não** é VRAM maior (decisão dos 256 MB): é usar a memória do sistema como o Linux (M1b) e paginar sem erro |
| F4 | **Compartilhamento por cópia** (P-2): o DWM copiava superfícies do menu, gerando os defeitos das T4.0–T4.6 | Frente B resolve em parte; o alias em texturas próprias deu TDR |
| F5 | **FPS baixo** (12–16) | Flushes síncronos, log em disco por linha, pipelines recriados |
| F6 | **Gates gráficos formais nunca rodaram** (M3-GFX, M4-GFX, D0.7) | A rasterização é provada na prática pelo DWM, mas não pelo gate da diretriz |

**Falhas de processo (minhas), já corrigidas em regra, que não podem voltar:**

- **Sucesso falso carimbado por portão** (pelo menos seis vezes: comparação crua de RPTR, TDR sem backend, memcpy no Draw, `sdma_blt_executed`, Marco 2 por cópia de CPU, "prova" zero sobre zero). Hoje: todo portão novo tem mutação que o derruba.
- **Marcos superestimados:** M3/M4 eram compute, não `vkCmdDraw`; o "desktop real" de 10/09 era o BasicDisplay voltando.
- **Corridas gastas em defeito de host:** coleta de log em ramo morto, flag `HardwareAccess` que causou deadlock, ordem de registro do TDR, duas variáveis num candidato (12/09).
- **Evidência perdida:** pastas truncadas por BugCheck, logs da UMD como foto de 5 s. Corrigido com resgate da cauda (C2) e coleta FINAL.
- **Hipótese apresentada como causa** antes da medição (várias; ex. "todos os TDRs seguem o blt de tela cheia").
- **Acreditar no menu da BIOS** por três corridas: o "teste cego" de VRAM não testava nada até descobrirmos a CMOS.

---

## 6. O plano

Ordem por **o que destrava o resto**. Cada fase tem gate de saída e teste nomeado (Lei 3). Corrida é confirmação, não descoberta: o que puder ser respondido offline, é.

### Fase 0 — VRAM mínima (decisão de 01/10, depois da 198) — ATUAL

> **Decisão do Daniel:** UMA de **256 MB** e fazer tudo funcionar com o mínimo, sem VRAM grande como muleta ([doc 551](docs/551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md)). Na BC-250 VRAM e RAM são a mesma GDDR6; 256 MB é o BAR, então toda a VRAM fica visível à CPU, e a memória extra vem do segmento 2 (M1b, como o GTT do Linux). O M2 fica dormente.
>
> **Corrida 198** (M2 completo, 4096 MB): 2.086 imagens no segmento 3, ~212 s com zero fault e zero TDR, e terminou na trava dura (tela preta, sem BugCheck).
>
> **Corrida 199 (revista em 01/10):** candidato **3.345.1.0** = 3.344.1.0 + instrumentação que só mede (temperatura Tctl e estado do SDMA1 a cada 1 s no rastro, motivo do último reset da plataforma lido e limpo pelo `-Ler`), com a CMOS em 256. Não se gasta uma corrida só para testar 256. Critério A–E no [doc 553 §3](docs/553_INSTRUMENTACAO_PLATAFORMA_TCTL_SDMA1_RESET_E_CRITERIO_CORRIDA199.md); script `tools\EXECUTAR_CORRIDA199_UMA256_600S.cmd`.

> **Resultado da 199 (01/10 21:44, [doc 555](docs/555_CORRIDA199_256MB_SOBE_TRAVA_SEM_CALOR_SEM_ERRO_DE_FABRIC.md)):** 256 MB **sobe** como previsto (base, tamanho, sem segmento 3) e o desktop apareceu; travou ao abrir a Pesquisa (~140 s). Calor, SDMA1 ativo e erro de fabric registrado no FCH **caíram** como explicação da trava. Próximo: W3 sobre a SMU (`cyan_skillfish_ppt.c`) e SDMA0 no batimento.

> **Resultado da 204 e 4c ([doc 565](docs/565_CORRIDA204_4B_VALIDO_TRAVOU_LEITURA_DA_CPU_NAO_BASTA.md), [doc 566](docs/566_4C_DESENHO_PTE_PELA_GPU_COMO_O_LINUX_E_O_TESTE_DE_ORDEM.md)):** 4b válido e travou ao abrir a Pesquisa, com o zeramento de uma alocação nova em voo: a leitura da CPU não basta. Achado de código: com `CPU_VIRTUAL` o KMD escreve PTE e invalida a TLB pela CPU **no build**, fora de ordem com a GPU, sobre uma área de rascunho reaproveitada. Próximo: 4c-ordem (drenar o SDMA antes de tocar PTE pela CPU) ou 4c completa (`GPU_VIRTUAL`, PTE e flush de TLB por pacote como o Linux); escolha do Daniel.

> **Próxima, Corrida 204 ([doc 564](docs/564_4B_CPU_NAO_LE_A_VRAM_PELA_BAR_E_CRITERIO_CORRIDA204.md)):** 4b, lab 3.351.1.0: a UMD deixa de ler o buffer linear de Present pela CPU (BAR). Se travar, 4c: tabelas de página pelo SDMA como o Linux. `tools\EXECUTAR_CORRIDA204_4B_CPU_SEM_LEITURA_DA_VRAM_600S.cmd`.

> **Resultado da 203 ([doc 562](docs/562_CORRIDA203_X2B_VALIDO_TRAVOU_IGUAL_CONST_FILL_NAO_E_A_CAUSA.md)):** X2b válido (zero `CONST_FILL`) e trava igual ao abrir o menu: o pacote não é a causa. O elo comum das quatro travas é a criação de alocação nova; próxima mineração: leitura de VRAM pela BAR concorrente com GPU.

> **Resultado da 202 e próxima, Corrida 203 ([doc 560](docs/560_CORRIDA202_TRAVA_DURA_VOLTOU_COM_CG1_CG1_NAO_E_SUFICIENTE.md), [doc 561](docs/561_X2B_ZERAR_POR_COPIA_EM_VEZ_DE_CONST_FILL_CRITERIO_CORRIDA203.md)):** a 202 travou com a CG.1 aplicada; as três travas com 256 MB param até 20 ms depois do FILL de zeramento de uma alocação nova. A 203 testa o X2b (3.350.1.0): zerar por cópia em vez de `CONST_FILL`. Sem trava → o pacote (H1); trava com a cópia por último → o destino (H2). `tools\EXECUTAR_CORRIDA203_X2B_ZERAR_POR_COPIA_600S.cmd`.

> **Próxima, Corrida 202 ([doc 559](docs/559_TDR_G1_LEITURA_DO_ANEL_GRAFICO_NO_TDR_E_CRITERIO_CORRIDA202.md)):** repete a 201 (CG.1, n=2) com leitura do anel gráfico no TDR. `tools\EXECUTAR_CORRIDA202_REPETICAO_CG1_600S.cmd`.

> **Resultado da 201 ([doc 558](docs/558_CORRIDA201_PRIMEIRA_CORRIDA_256MB_SEM_TRAVA_DURA_MGCG_DO_POST.md)):** **sem trava dura pela primeira vez com 256 MB** (591,8 s, rollback limpo), com o MGCG do SDMA0 devolvido ao POST. Um TDR gráfico (VMID 10) foi recuperado. Próximo: repetir a mesma corrida (n=2) com boot frio; em paralelo, analisar o hang gráfico offline.

> **Resultado da 200 e próxima, Corrida 201 ([doc 557](docs/557_CORRIDA200_SDMA0_OCIOSO_NA_PARADA_E_CG1_MGCG_DO_POST_CRITERIO_CORRIDA201.md)):** a 200 parou com SDMA0 e GFX ociosos, 2 s depois de o DWM ficar sem memória. Achado: nós desligamos o MGCG do SDMA0 e o Linux não. A 201 testa o candidato 3.348.1.0 (CG.1), que devolve o MGCG ao padrão do POST: `tools\EXECUTAR_CORRIDA201_SDMA0_MGCG_600S.cmd`. Se não mudar, o próximo suspeito é a pressão de memória (M1b).

> **Próxima, Corrida 200 (candidato 3.347.1.0, [doc 556](docs/556_W3_SMU_NAO_E_DIFERENCA_SDMA0_E_CLOCK_GATING_NO_RASTRO_CRITERIO_CORRIDA200.md)):** W3 tirou a SMU da lista. A 200 grava o estado do SDMA0 a cada 100 ms e o clock gating, para saber se o SDMA0 trabalhava quando a placa parou. Script `tools\EXECUTAR_CORRIDA200_SDMA0_CG_600S.cmd`; depois de trava, `tools\coletar-pos-trava.ps1` primeiro.

### Histórico da Fase 0 anterior — Corridas 197/198 com 4096 MB

> **Resultado da 197:** V-ok (base `0x370000000`, 4096 MiB, segmento 3 de 3824 MiB); **`Lock2` fechado** (zero falhas na corrida inteira); desktop visível; DWM sem laço de recriação. **BugCheck `0x10E`/`0x34` ~2 min depois**, num `Lock` da nossa UMD na Pesquisa sobre uma STAGING recém-trazida da RAM (pilha P0; causa H, mesmo padrão da 175). Próximo: **Corrida 198, M2 completo** (3.344.1.0, imagens da RADV no segmento 3) ([doc 548 §10–§11](docs/548_M2_SEGMENTO3_VRAM_ALEM_DO_BAR_DINAMICO_E_CRITERIO_CORRIDA194.md)). Se o `0x34` voltar igual (desfecho D), ele vira item da Fase 1. O texto abaixo é o plano como foi escrito antes da 197.

- **O quê:** candidato M2.1 (3.343.1.0): abertura no conjunto suportado das alocações da RADV com CPU, para o `Lock2` voltar a funcionar com o segmento 3 declarado.
- **Atenção:** a CMOS está agora em **4096 MB**, então a 197 mede duas coisas juntas (decisão do Daniel). Valores esperados com 4096: `VramTotalMiB=4096`, base física `0x370000000`, segmento 3 `[0x10000000, +0xEF000000)` = **3824 MiB**, RAM do Windows ≈12,6 GB (já medida: 12.591.636.480 bytes).
- **Gate:** critério A–E do [doc 548 §9](docs/548_M2_SEGMENTO3_VRAM_ALEM_DO_BAR_DINAMICO_E_CRITERIO_CORRIDA194.md) e tabela V-ok/V-base/V-leitura do §9.1.
- **Roteiro:** boot frio e `tools\EXECUTAR_CORRIDA197_M21_LOCK_600S.cmd` (já lê a CMOS antes do bind).

### Fase 1 — Trava dura: separar placa de driver (prioridade máxima depois da 197)

| Passo | O que é | Quem | Custa corrida? | Responde |
|---|---|---|---|---|
| **1.1** | **Sensores térmicos no log do KMD.** O `bc250dbg` já lê as sondas SMN de temperatura (borda, junção, memória) e do ventilador (`dbg/thermal.c`), mas como valor **cru, sem escala calibrada**. Levar essa leitura (só leitura, em PASSIVE_LEVEL, com procedência) ao batimento das migalhas do `bc250mcdm` a cada 2 s, e calibrar a escala contra uma referência (HWiNFO com o BasicDisplay ou `sensors` no Linux) | Eu; calibração com o Daniel | Não (vai de carona) | Os sensores subiam antes de cada trava? Mesmo sem escala, a tendência informa |
| **1.2** | **W1 — plataforma sozinha.** `tools\bc250-platform-stress.exe` (já compilado) com o BasicDisplay, 30–60 min, mais ventilador e fonte conferidos | Daniel roda | Não | Trava sem o nosso driver → plataforma (térmica, fonte, memória). Não trava → precisa da GPU paginando |
| **1.3** | **W2 — reprodutor da trava.** Harness sem DWM que gira alocação e liberação de VRAM para forçar FILLs, em ritmo controlado | Eu escrevo | Sim, curtas | Transforma "2 travas em 13 corridas" em minutos; cada hipótese vira ensaio curto |
| **1.4** | **W3 — diferença contra o amdgpu por leitura** (`sdma_v5_0.c`, `gmc_v10_0.c`, `gfxhub_v2_0.c`, `mmhub_v2_0.c`, `amdgpu_ttm.c`, `cyan_skillfish_ppt.c`) | Eu | Não | Lista de diferenças nomeadas para o W2 testar |
| **1.5** | Linux de referência de novo (opcional): o `amdgpu` falhou no IP discovery. Testar outro kernel/Fedora 43 | Daniel | Não | Gabarito de carga e de registradores |

**Gate da fase:** a trava ganha **um reprodutor** (W2) ou **uma causa de plataforma** (W1/temperatura). Sem um dos dois, nenhuma faixa de driver mexe na trava.

> **Atualização 01/10 (coleta Linux com 256 MB, [doc 552](docs/552_COLETA_LINUX_256MB_AMDGPU_ATIVO_GABARITO_E_O_QUE_MUDA.md)):** o passo 1.5 rodou e deu **L-B**.
> - O `amdgpu` aguentou 149 despejos completos da VRAM, com o vkcube rodando e a GPU a 79 °C, **sem travar** em 8,5 min. A placa não trava sozinha sob paginação pesada (H forte, uma sessão).
> - **Firmware idêntico** ao nosso, então firmware está descartado.
> - Diferenças nomeadas que entram no W3, em ordem:
>   1. **SMU:** o Linux gerencia relógios e tensões, nós não;
>   2. **SDMA1:** o Linux o usa, nós nem o inicializamos;
>   3. **Fences por interrupção** contra o nosso polling;
>   4. clock gating, HDP e MMHUB.
> - **Instrumento novo (1.6):** ler no Windows, só leitura, o registro de motivo de reset da plataforma (FCH `S5_RESET_STATUS`, hoje `0x40080400` com "parity error"). Procedimento: corte de energia total, uma trava, e a leitura diz se a trava gera erro de paridade ou sync flood. O endereço precisa ser conferido na fonte do kernel antes (Lei 7).
> - **Feito offline em 01/10 ([doc 553](docs/553_INSTRUMENTACAO_PLATAFORMA_TCTL_SDMA1_RESET_E_CRITERIO_CORRIDA199.md)):** o 1.6 entrou na ferramenta da CMOS (lê e limpa como o Linux), e o 1.1 virou o registro `BPS1` do KMD: Tctl pela conversão do k10temp (alcance pelo par SMN da GPU é H, a amostra se autovalida) e SDMA1 só leitura. Endereços conferidos na fonte (P1). Os sensores sem procedência do `bc250dbg` foram descartados.

### Fase 2 — Memória com o mínimo (M1b, como o GTT do Linux)

Reescrita em 01/10 pela decisão dos 256 MB ([doc 551 §2.3](docs/551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md)). O M2 (segmento além do BAR) saiu deste caminho e ficou dormente.

0. **A RADV saber que é APU** (achado do [doc 552 §3.1](docs/552_COLETA_LINUX_256MB_AMDGPU_ATIVO_GABARITO_E_O_QUE_MUDA.md)).
   - **Defeito (P1):** a nossa winsys nunca preenche `ids_flags`, então `has_dedicated_vram = true` e, como a mesma função fixa a VRAM visível em 16 GB, a RADV promete **16 GB locais fictícios** sobre ~245 MB reais (corrigido no [doc 554](docs/554_M1B1_RADV_NO_SEGMENTO_2_DORMENTE_E_CORRECAO_DO_HEAP.md) §1; eu tinha escrito ~251 MB). Quem recusa é o KMD.
   - **M1b.1 feita offline e dormente (doc 554):** tamanhos reais e FUSION na RADV, domínio e comando no blob BCA1, política única no contrato, KMD aplicando só sem segmento 3, buffer do Present local; três interruptores em 0, travados juntos por portão. Falta a cópia por GPUVA (bloqueio 2) e a robustez da paginação antes de ligar.
   - **Gabarito (P0, Linux):** heap local de 5,20 GiB sobre 256 MB de VRAM, com o excedente em GTT.
   - **Correção:** `ids_flags |= AMDGPU_IDS_FLAGS_FUSION`.
   - **Depende do passo 1:** sem o KMD aceitar o segmento 2, não há onde pôr o excedente.
1. **Domínio no blob da RADV:** a alocação passa VRAM/GTT ao KMD, e GTT vira `SegSet` com o segmento 2 (abertura = memória do sistema). Gabarito do Linux: com a VRAM cheia, 534 MiB em GTT, incluindo 338 MiB de objetos sem acesso de CPU.
2. **Gate e inspeção do IB gráfico** aceitando páginas de sistema.
3. **Composição a partir de memória do sistema** (cópia por GPUVA em vez de MC contíguo).
4. **Tipo de memória explícito** para o buffer linear do Present.
5. **Paginação robusta**, que com 256 MB é o caso normal:
   - o `0x34` (Lock da UMD durante um TRANSFER da mesma alocação, Corridas 175 e 197);
   - os `Upload LOCK falhou 0x80070057`;
   - `Offer/Reclaim` de verdade.
6. **Frente B final:** alias seguro; entender o TDR do alias em textura própria (191/192) antes de reabrir.
7. **Tabela de objetos definitiva** (dinâmica ou um objeto por textura), no lugar do paliativo de 16.384.
8. **Instrumento do driver:** o KMD ler o bloco APCB da CMOS no boot (só leitura) e logar `pedido` contra `aplicado` ([doc 550 §7](docs/550_CONTROLE_DA_VRAM_PELA_CMOS_NO_WINDOWS.md)).

**Gate:** com UMA de **256 MB**, Explorer, menu e Pesquisa por 600 s sem `OUT_OF_DEVICE_MEMORY` / `vkAllocateMemory -2`, sem laço de device-lost e sem `0x10E`. O driver também precisa funcionar com 512 (fábrica) sem mudança.

### Fase 3 — Desktop mínimo funcional completo

1. Draws recusados do Explorer (`draw PM4 ausente`) implementados.
2. Canal de log para AppContainer e causa do `vkCreateDevice -3` da Pesquisa.
3. Menu Iniciar 100% (ícones, texto, Pesquisa) em todas as aberturas.
4. Tradutor de shader dentro do pacote (D2.2/D2.5).
5. `OfferResources1`/`ReclaimResources(1)` de verdade, via `pfnOfferAllocationsCb`/`pfnReclaimAllocationsCb`. Hoje devolvem `S_OK` sem backend (Lei 4; achado na 197).
6. `Upload LOCK falhou: hr=0x80070057` em uploads grandes (1,6 MB e 11 MB) no menu, na Pesquisa e no Explorer: 1.349 na 193, 86 na 197.

**Gate DESKTOP-60MIN** (diretriz): Explorer, menu, janelas sobrepostas, texto, transparência, resize, navegador e apps simultâneos por 60 min, sem laço de device-lost, sem WARP, BC-250 como adaptador ativo.

### Fase 4 — Desempenho

1. Medir antes de mexer (R4 do plano antigo): flushes por draw, cópias por quadro, pipelines recriados.
2. Tirar flushes síncronos do caminho de destruição e do `DISCARD`.
3. Log fora do caminho quente (sem `WRITE_THROUGH` por linha, mantendo a sobrevivência à trava pelas migalhas).
4. Cache de pipelines entre recargas da DLL.

**Gate:** frame time medido de ponta a ponta; meta de 56–60 FPS no desktop animado, no modo do GOP.

### Fase 5 — Robustez

- **ROBUSTEZ-8H:** 8 horas de uso, pressão de memória, abrir/fechar processos.
- **TDR-DISPLAY:** hang controlado → reset → readmissão → desktop visível, repetível.

### Fase 6 — Gates gráficos formais (diretriz de 16/09)

- **M3-GFX** e **M4-GFX** rodando na placa (programas já compilados), com pixels conferidos e engine identificada.
- **D0.7:** canários byte a byte no destino do IB gráfico, por um caminho que não leia pela BAR no meio da carga.

### Fase 7 — Vulkan para aplicativos e primeiros jogos

1. ICD Vulkan registrado no sistema (RADV empacotada), `vulkaninfo` pela placa.
2. WSI Vulkan integrado ao DWM (janela composta, não scanout direto).
3. Primeiro jogo D3D11 pela nossa UMD; primeiro jogo Vulkan.
4. Feature level 11_0 e o que os jogos pedirem, só quando implementado.

**Gate:** jogo real na tela, FPS medido, prova de ausência de fallback (fences e contadores da GPU).

### Fase 8 — D3D12 e OpenGL

- UMD D3D12 traduzida (rota VKD3D-Proton ou equivalente como **driver**, não DLL por aplicativo).
- OpenGL por Zink.

### Fase 9 — Display (DCN, opcional)

Modos, EDID real por AUX, hotplug, flip sem cópia. Só depois da Fase 5, com snapshot e rollback (Lei 5, P9 da BASE).

### Fase P — Produto (contínua)

Instalador e desinstalador, remoção dos tunables de laboratório, RADV e tradutor no pacote, assinatura, documentação final.

---

## 7. Próximos passos concretos

> Reescrito em 02/10/2026, depois da Corrida 204. A tabela anterior (da 199) está no git.

| # | Passo | Quem | Depende de |
|---:|---|---|---|
| 1 | Recuperação (`tools\BC250_RECOVERY.cmd`, fusível) e boot frio: a placa está em Código 43 desde a 204 | Daniel | — |
| 2 | Escolher entre **4c-ordem** e **4c completa** ([doc 566](docs/566_4C_DESENHO_PTE_PELA_GPU_COMO_O_LINUX_E_O_TESTE_DE_ORDEM.md)) | Daniel | — |
| 3 | Implementar a opção escolhida, com testes, portão por mutação, pacote e critério A–E | Eu | 2 |
| 4 | Corrida 205 com o mesmo uso da 201–204 (menu, Pesquisa, Gerenciador de Tarefas, Configurações) e `coletar-pos-trava.ps1` se travar | Daniel | 3 |
| 5 | Decidir se X2b (zerar por cópia) e 4b (sem leitura da CPU) ficam ou voltam, depois que a trava tiver causa | Daniel e eu | 4 |
| 6 | M1b (memória do sistema para a GPU) segue estacionado até a trava ter causa ou reprodutor | — | 4 |

## 8. Regras que continuam valendo

- **Critério escrito antes de cada corrida** (Lei 10): assinatura e implicação separadas, desfecho E sempre, desfecho parcial não conta.
- **Uma variável por candidato**, ou as duas declaradas por escrito quando o Daniel decidir juntar (como a 197).
- **O empacotador copia, não compila:** conferir strings novas no binário assinado.
- **Boot frio entre binds.** E, desde 01/10, **saber o valor da CMOS** antes de cada corrida (`bc250-vram.ps1 -Ler`).
- **Imagem só conta se anterior ao rollback.** Desktop do BasicDisplay é recuperação, não sucesso.
- **Depois de cada corrida:** os 5 pontos da diretriz + a tabela ✅🟡❌ do desktop.

## 9. Mapa dos documentos

| Onde | O quê |
|---|---|
| Raiz | `PLANO_MESTRE.md` (este), `BASE_DE_AVANCO.md`, `CLAUDE.md`/`AGENTS.md`/`GEMINI.md`/`.cursorrules` (regras), `DIRETRIZ_QUALIDADE_...md`, `rules.md`, `RECOVERY_BC250V4.md`, `README.md` |
| `docs/` | Docs numerados por faixa e corrida; os ativos desta fase são 545–550 |
| `docs/evidence/` | Evidência de cada corrida e de cada uso do `bc250-vram` |
| `DocsObsoletos/` | Arquivo morto (nada apagado). Planos antigos em `planos-superseded/2026-10-01/`; logs e dumps soltos da raiz em `raiz-superseded/2026-10-01/` |
