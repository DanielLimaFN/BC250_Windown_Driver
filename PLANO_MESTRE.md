# PLANO MESTRE — BC-250 no Windows

> **Escrito em 01/10/2026**, depois da Corrida 196 e da validação do controle de VRAM pela CMOS. **§3 revisada em 06/10/2026** (Corrida 236, paridade com o amdgpu até a P4.2).
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
| 01/10 – 06/10 | VRAM mínima de 256 MB; caça à trava por **paridade com o amdgpu** (Corridas 199–236): CG.1, 4c, M1b.1, D2/GART, P1, P7, P2 (doorbell), P1b, P3.0 (MSI), P3.0b (GART do MMHUB), P3.1/P3.2 (IH e conclusão por interrupção), P6, EOP do CP, P4.1/P4.2 (SDMA1) | ✅ cada item provado na placa; 🟡 nenhum tirou a trava. Passagem de bastão: [doc 624](docs/624_PASSAGEM_DE_BASTAO_2026-10-06_TRAVA_DURA_DEPOIS_DA_PARIDADE_P4.md) |

---

## 3. Todas as funcionalidades que queremos, e o estado de cada uma

> **Revisado em 06/10/2026, depois da Corrida 231.** As subseções 3.4 (UMD) e 3.5 (Desktop) vêm do estado de 01/10 e não foram reavaliadas linha a linha depois disso.

### 3.1 Plataforma e firmware

| Status | Funcionalidade | Situação hoje (evidência) |
|---|---|---|
| ✅ | Firmware da GPU carregado pelo nosso driver (PSP, RLC, PFP, ME, CE, MEC, SDMA) | Desde 26/08; idêntico ao do Linux (doc 552) |
| ✅ | Recuperação: watchdog, rollback, fusível anti-boot-loop, coleta automática no boot, WinRE | A coleta e o rollback rodaram sozinhos depois da trava da 231 |
| ✅ | **Controle da VRAM no Windows** pela CMOS APCB (`tools\vram-cmos\bc250-vram.ps1`), decisão de 256 MB | [doc 550](docs/550_CONTROLE_DA_VRAM_PELA_CMOS_NO_WINDOWS.md), [doc 551](docs/551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md) |
| ✅ | Driver lê o tamanho e a base da VRAM da placa a cada boot | Desde a 194 |
| ✅ | Teste de RAM e motivo do reset lidos depois da trava | 8 GB sem erro (219, 231); reset pelo `0xCF9` em todas |
| 🟡 | Temperatura nas corridas | Tctl medido na 199 (80 °C, sem calor); GPU e memória sem escala calibrada |
| ✅ | SMU gerenciada pelo firmware, como no Linux | [doc 556](docs/556_W3_SMU_NAO_E_DIFERENCA_SDMA0_E_CLOCK_GATING_NO_RASTRO_CRITERIO_CORRIDA200.md) |

### 3.2 Driver de kernel (WDDM)

| Status | Funcionalidade | Situação hoje |
|---|---|---|
| ✅ | Miniport WDDM 2.7 display+render em Código 0, display herdado do GOP (VidPn, VSync do OTG) | Desde 02–03/09 |
| ✅ | Segmentos, paginação, GPUVA por processo; PTE escrita pelo SDMA (4c.4) | 210 |
| ✅ | Submissão SDMA assíncrona e anel gráfico executando o trabalho do DWM | Milhares de IBs por corrida (13.115 na 231) |
| ✅ | TDR: detecção, reset e readmissão; desktop sobreviveu a TDR real | 153 e 231 (TDR do GFX em VMID 10 recuperado) |
| ✅ | Memória da RADV no segmento 2, como o GTT do Linux (M1b.1) | 211–212 |
| ⏸️ | Segmento de VRAM além do BAR (M2) | Dormente desde 01/10 (decisão dos 256 MB); `Lock2` e segmento 3 provados (197–198) |
| 🟡 | Tabela de objetos do KMD | 4.096 → 16.384 (paliativo T4.7); definitiva não feita |
| ❌ | Prova formal D0.7 (canários no destino do IB gráfico) | Retirada na X4 por risco de leitura pela BAR |
| ❌ | Preempção, prioridades, multi-contexto auditado, suspend/resume | Não iniciados |

### 3.3 Paridade com o amdgpu (caça à trava dura)

Decisão do Daniel de 05/10: driver igual ao amdgpu, uma diferença por corrida ([doc 593 §4](docs/593_CORRIDA218_UC1_FUNCIONOU_D2_DE_PE_TRAVA_DURA_IGUAL_E_INVENTARIO_DE_PARIDADE_COM_O_AMDGPU.md)).

| Status | Item | Situação hoje |
|---|---|---|
| ✅ | Clock gating como o POST deixa (CG.1) | 201; não basta (202) |
| ✅ | 4c: PTE pelo SDMA, flush pelo anel, sem a CPU ler tabela de página | 205–210; não basta |
| ✅ | D2: anel e fences do SDMA0 na RAM pela GART da VMID 0, tabela única, MTYPE_UC | 215–218; não basta |
| ✅ | P1: sem instrumentação lendo a VRAM em regime | 219; não basta |
| ✅ | P7: anel e fences do GFX na GTT | 220; não basta |
| ✅ | P2: WPTR do SDMA0 por doorbell (abertura do BIF ligada) | 223; não basta |
| ✅ | P1b: CPU na VRAM pela DRAM, sem HDP (como o amdgpu em APU) | 226/228; não basta |
| ✅ | P3.0: MSI entregue pelo Windows | 229 |
| ✅ | P3.0b: GART da VMID 0 no MMHUB (armada depois do PSP, que é cliente do MMHUB com buffers em RAM física) | 230/231; não basta |
| ✅ | P3.1: IH ligado (anel na GART, TRAP do SDMA0, ISR, prova por vetor) | 232: 2.341 TRAPs = 2.341 vetores = 2.341 ISR; trava aos ~77 s ([doc 615](docs/615_CORRIDA232_IH_VIVO_EM_REGIME_TRAPS_1_PARA_1_E_TRAVA_DURA_AOS_77S.md)) |
| ✅ | P3.2: conclusão do SDMA0 por interrupção | 233: 10.931 DPCs pelo ISR em ~584 s, zero TDR/timeout; trava dura no fim; não basta ([doc 617](docs/617_CORRIDA233_CONCLUSAO_POR_INTERRUPCAO_EM_REGIME_584S_E_TRAVA_DURA.md)) |
| ❌ | P3.3: RPTR do IH por doorbell | Depois da P3.2 |
| ✅ | P6: MTYPE_UC na folha dos BOs da RADV com `GL2_BYPASS` (RADV nova declara a flag) | 234: folhas UC nos IBs concatenados da RADV, zero recusa de ABI; não basta ([doc 619](docs/619_CORRIDA234_P6_E_EOP_DO_CP_EM_REGIME_TRAVA_DURA_AOS_136S.md)) |
| ✅ | EOP do CP: conclusão do anel gráfico pela interrupção | 234: 1.236 emitidos = 1.236 vetores = 1.236 DPCs; não basta ([doc 619](docs/619_CORRIDA234_P6_E_EOP_DO_CP_EM_REGIME_TRAVA_DURA_AOS_136S.md)) |
| ✅ | P4.1: SDMA1 vivo e ocioso (firmware pelo PSP tipo 10, anel provado) | 235: prova em 1 poll, ATIVO, zero vetor sem dono; não basta ([doc 621](docs/621_CORRIDA235_P41_SDMA1_VIVO_E_TRAVA_DURA_AOS_267S.md)) |
| ✅ | P4.2: escritas de PTE no SDMA1 (troca FENCE/POLL entre os anéis); paginação segue no SDMA0, como o amdgpu | 236: 8.414 faixas no SDMA1, Done = Go em todas as linhas, zero fault/TDR; não basta ([doc 623](docs/623_CORRIDA236_P42_PTE_NO_SDMA1_EM_REGIME_E_TRAVA_DURA_AOS_45S.md)) |
| ❌ | P5: VM de 4 níveis e 48 bits (nós: 2 níveis, 34 bits) | Custo alto |
| ⛔ | **Trava dura** | 226 e 229 passaram dos 600 s (a 229 sem abrir o menu); 228, 231, 232, 233, 234, 235 e 236 travaram (~454, ~325, ~77, ~584, ~136, ~267 e ~45 s). Fecha com 600 s repetidos com uso normal |

### 3.4 UMD D3D11 (o que o DWM e os apps usam)

| Status | Funcionalidade | Situação hoje |
|---|---|---|
| ✅ | Runtime D3D11 abre a nossa UMD e cria device (feature level 10_1) | 17/09 |
| ✅ | Tradução de shaders DXBC → SPIR-V | Shaders do DWM e do menu traduzem (184) |
| ✅ | Draw D3D11 vira draw Vulkan na GPU | Triângulo composto pelo DWM (106); grade do DWM (181); desktop desde a 134 |
| 🟡 | Recursos compartilhados entre processos (menu → DWM) | Frente B v3; recusas e artefatos no Clima (223, 226) |
| 🟡 | Cobertura de DDIs do Explorer | Draws recusados com `draw PM4 ausente`; inventário em [doc 545](docs/545_INVENTARIO_UMD_DWM_E_SHELL_CORRIDAS_181_186.md) |
| ❌ | Pesquisa / apps AppContainer | `vkCreateDevice -3` (01/10); não reavaliado |
| 🟡 | Tradutor de shader **dentro do pacote** | Carregado de `C:\BC250_LIB\libvkd3d-shader-1.dll` |
| ❌ | Feature level 11_0+, D3D9/10 transparentes | Não iniciados |

### 3.5 Desktop

| Status | Funcionalidade | Situação hoje |
|---|---|---|
| ✅ | Desktop visível desenhado pela GPU | Desde a 134; ~325 s na 231 |
| ✅ | DWM sem crash a corrida inteira | 183–185, 226, 229 |
| 🟡 | Menu Iniciar com ícones e rápido | Funciona; instável entre candidatos |
| ❌ | Explorer sem falta de memória | Faltas e laços de recriação em 190/193 |
| ❌ | Janelas, texto, transparência, resize, Alt+Tab, navegador por 60 min (gate DESKTOP-60MIN) | Nunca tentado |
| ❌ | 60 FPS no desktop | ~12–16 FPS (30/09); o modo do GOP é ~56 Hz |

### 3.6 Estabilidade

| Status | Funcionalidade | Situação hoje |
|---|---|---|
| 🟡 | 600 s sem trava | Acontece (134, 136, 137, 182–185, 201, 222, 226, 229), mas não é a regra |
| ⛔ | **Sem trava dura** | Ver 3.3: mesma assinatura desde a 199 (GPU ociosa, máquina morta entre duas observações) |
| ❌ | 8 horas (gate ROBUSTEZ-8H) | Depende da trava dura |
| 🟡 | TDR com o desktop sobrevivendo (gate TDR-DISPLAY) | 153 e 231; falta repetir |

### 3.7 Vulkan, jogos e além

| Status | Funcionalidade | Situação hoje |
|---|---|---|
| ✅ | Vulkan executando na placa (RADV + nosso KMD), cubo texturizado | Marcos 1–4 |
| 🟡 | Gates gráficos formais M3-GFX e M4-GFX (`vkCreateGraphicsPipelines` + `vkCmdDraw`) | Programas compilados; nunca rodados como gate |
| ❌ | ICD Vulkan instalado para qualquer aplicativo (loader do sistema) | RADV só carregada pela nossa UMD a partir de `C:\BC250_LIB` |
| ❌ | WSI Vulkan integrado ao DWM (janela, não scanout direto) | Não iniciado |
| ❌ | Jogo D3D11 real / jogo Vulkan real | Não iniciados |
| ❌ | D3D12 (UMD D3D12 traduzida) / OpenGL (Zink) | Não iniciados |

### 3.8 Display (DCN, P9) e produto

| Status | Funcionalidade | Situação hoje |
|---|---|---|
| ✅ | Inventário somente leitura do DCN, frame counter | Tabela 1 |
| ❌ | Trocar resolução, EDID real, hotplug, segundo monitor, VRR/HDR, flip sem cópia | P9, depois de tudo |
| 🟡 | Pacote assinado (teste) com auditorias | Cada candidato; assinatura de teste, `testsigning` ligado |
| ❌ | Instalador/desinstalador de produto, sem tunables de laboratório | Não iniciado |
| ❌ | RADV e vkd3d-shader empacotados com o driver | Hoje vêm de `C:\BC250_LIB` |

### 3.9 Caminho mais curto até finalizar

1. **Trava dura:** P3.1, P3.2, P6, EOP do CP, P4.1 e P4.2 feitas (232–236) e nenhuma tirou a trava. Sobram P3.3 (barata, improvável) e P5 (cara), mais as diferenças de **política** de paginação ainda não inventariadas (doc 624 §6). Ela bloqueia tudo o que vem depois.
2. **Desktop completo:** memória do Explorer, recursos compartilhados, Pesquisa/AppContainer e o gate DESKTOP-60MIN.
3. **Estabilidade:** ROBUSTEZ-8H e TDR-DISPLAY repetido.
4. **Jogos:** ICD Vulkan no sistema, WSI em janela, jogo Vulkan e jogo D3D11 reais.
5. **Produto e P9:** pacote sem `C:\BC250_LIB`, instalador, DCN.

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

> **07/10/2026, Corrida 241 pronta ([doc 633](docs/633_ENCADEAMENTO_GFX_DA_RADV_COMO_O_UPSTREAM_E_CRITERIO_CORRIDA241.md)):** RADV `DFD8D120` com o encadeamento GFX como o upstream; KMD igual.
>
> **07/10/2026, depois da Corrida 240 ([doc 632](docs/632_CORRIDA240_RLC_PARADO_CONFIRMADO_PREAMBULO_DO_MENU_E_ENCADEAMENTO_DA_RADV_COM_VMID7.md)):** RLC parado (P0); o TDR vem do preâmbulo da RADV, cujo encadeamento GFX tem VMID 7 fixa e sem VALID (fora do upstream). Próximo: Corrida 241 com o encadeamento como o upstream; depois, o bit VALID do KMD e o RLC rodando.
>
> **07/10/2026, Corrida 240 pronta ([doc 631](docs/631_INSTRUMENTACAO_RLC_IH_IB_DO_MENU_E_CRITERIO_CORRIDA240.md)):** 3.385.1.0 + RADV 7B82C4AE, só instrumentação; achado: o RLC fica parado em regime (o `pfp_direct_load` devolve `RLC_CNTL` a 0), diferença de paridade nova a confirmar.
>
> **07/10/2026, mineração dos TDRs ([doc 630](docs/630_MINERACAO_DOS_TDR_DO_GFX_SQ_ERROS_DEPOIS_DO_RESET_E_RLC_SEM_SAFE_MODE.md)):** erros do SQ só depois do nosso reset do GFX; RLC nunca entra em safe mode; reset diferente do amdgpu. Próximo: instrumentação só de leitura (RLC, IH, IB parado) e, depois, reset do GFX como o amdgpu.
>
> **07/10/2026, depois da Corrida 239 ([doc 629](docs/629_CORRIDA239_SO_MENU_TRES_TDR_DO_GFX_E_TRAVA_DURA_22S_DEPOIS.md)):** só o menu basta para travar (n=1): três TDRs do GFX a partir do primeiro IB de um device novo do menu, todos recuperados, e trava dura 22 s depois. Próximo: mineração offline dos TDRs do GFX (239, 231, 216, 208, 201).
>
> **06/10/2026, depois da Corrida 238 ([doc 627](docs/627_CORRIDA238_NAO_FOI_PARADA_600S_SEM_TRAVA_COM_USO_LEVE_E_PROXIMO_PASSO.md)):** ~600 s sem trava com uso leve (não foi parada). Bissecção por carga: **Corrida 239 pronta**, só o menu Iniciar ([doc 628](docs/628_CRITERIO_CORRIDA239_SO_O_MENU_INICIAR.md)), `tools\EXECUTAR_CORRIDA239_SO_MENU_600S.cmd`.
>
> **06/10/2026, depois da Corrida 237 ([doc 626](docs/626_CORRIDA237_PARADO_600S_SEM_TRAVA_TEARDOWNS_PROVADOS_E_CRITERIO_CORRIDA238.md)):** W1 sem trava (45 min); 237 parada passou 600 s sem trava, com todos os teardowns provados. Próximo: Corrida 238, repetição parada (mesmo `.cmd`); se passar, bissecção por carga.
>
> **06/10/2026, depois do doc 625:** W1 (45 min com BasicDisplay) e Corrida 237 (mesmo 3.384.1.0, parado no desktop, n=2 com a 238 se passar), critérios no [doc 625](docs/625_RESET_REASON_RESPONDIDO_POR_MINERACAO_W1_E_CRITERIO_CORRIDA237_PARADO.md). O RESET_REASON de controle saiu por mineração.
>
> **06/10/2026, depois da Corrida 236:** os próximos passos vigentes estão no [doc 624 §7](docs/624_PASSAGEM_DE_BASTAO_2026-10-06_TRAVA_DURA_DEPOIS_DA_PARIDADE_P4.md) (passagem de bastão). A tabela abaixo é o histórico da 4c e fica como registro.

> Reescrito em 02/10/2026: o Daniel escolheu a **4c completa em 5 etapas**, uma corrida cada ([doc 567](docs/567_4C_COMPLETA_EM_5_ETAPAS_E_CRITERIO_CORRIDA205.md)). A tabela anterior está no git.

| # | Passo | Quem | Depende de |
|---:|---|---|---|
| 1 | ~~Recuperação e boot frio~~ feito | Daniel | — |
| 2 | ~~Corrida 205 (4c.1)~~ **feita, desfecho C** ([doc 568](docs/568_CORRIDA205_4C1_ANEL_PROVADO_IB_TRAVA_NO_SRBM_WRITE_600S_SEM_TRAVA_DURA.md)): flush pelo SDMA provado no anel; dentro de IB para no `SRBM_WRITE` | Daniel | 1 |
| 3 | 4c.2 **pronta** ([doc 569](docs/569_4C2_FLUSH_TLB_PELO_SDMA_NO_ANEL_E_CRITERIO_CORRIDA206.md)): `FlushTlb` pelo SDMA no anel antes do IB seguinte, lab 3.353.1.0 → **Corrida 206**, `tools\EXECUTAR_CORRIDA206_4C2_FLUSH_TLB_NO_ANEL_600S.cmd` | Daniel | 2 |
| 4 | ~~Corrida 208 (4c.3)~~ **feita, desfecho B** ([doc 574](docs/574_CORRIDA208_4C3_JOURNAL_RESOLVEU_99_95_E_TRAVA_DURA_NO_ZERAMENTO.md)): journal resolveu 99,95% dos gates; trava dura aos ~290 s no zeramento | Daniel | 3 |
| 5 | 4c.4 **pronta** ([doc 575](docs/575_4C4_PTE_PELO_SDMA_IB_DIVIDIDO_NO_ANEL_E_CRITERIO_CORRIDA209.md)): `GPU_VIRTUAL`, PTE por `WRITE_LINEAR`, IB dividido no anel, journal fechado no `NO_SPACE`, lab 3.356.1.0 → **Corrida 209**, `tools\EXECUTAR_CORRIDA209_4C4_PTE_PELO_SDMA_600S.cmd` | Daniel | 4 |
| 5b | ~~Corrida 209~~ **feita, desfecho C** ([doc 576](docs/576_CORRIDA209_4C4_RODOU_PONTO_DE_DIVISAO_NO_IB_ERRADO_E_CRITERIO_CORRIDA210.md)): ponto de divisão no IB errado (defeito meu); corrigido em lab 3.357.1.0 → **Corrida 210**, `tools\EXECUTAR_CORRIDA210_4C4_DIVISAO_CORRIGIDA_600S.cmd` | Daniel | 5 |
| 5c | ~~Corrida 210~~ **feita, desfecho B** ([doc 577](docs/577_CORRIDA210_4C4_PTE_PELO_SDMA_ESTAVEL_DESKTOP_DESENHADO_TRAVA_DURA_AOS_228S.md)): 4c.4 estável (20.071 IBs, zero timeout, desktop desenhado), trava dura aos ~228 s; a 4c sai das causas suficientes | Daniel | 5b |
| 5d | M1b.1 **ligada** ([doc 579](docs/579_M1B1_LIGADA_RADV_NO_SEGMENTO2_E_CRITERIO_CORRIDA211.md)): RADV no segmento 2, lab 3.358.1.0 → **Corrida 211**, `tools\EXECUTAR_CORRIDA211_M1B1_RADV_SEGMENTO2_600S.cmd` | Daniel | 5c |
| 5e | ~~Corrida 211~~ M1b.1 ativa, tela azul por recusa de IB dividido (doc 580); corrigido em 3.360.1.0 → **Corrida 212**, `tools\EXECUTAR_CORRIDA212_M1B1_PEDACOS_DE_ANEL_600S.cmd` | Daniel | 5d |
| 5f | ~~Corrida 212~~ **feita, desfecho B** ([doc 581](docs/581_CORRIDA212_SEM_FALTA_DE_MEMORIA_SEM_DEFEITO_DE_HOST_TRAVA_DURA_AOS_259S.md)): M1b.1 fecha a cascata (n=2); trava dura aos ~259 s, independente da memória e da 4c | Daniel | 5e |
| 6 | Mineração feita ([doc 582](docs/582_MINERACAO_DAS_TRAVAS_256MB_GPU_OCIOSA_MAQUINA_MORRE_ENTRE_DUAS_OBSERVACOES_E_O_QUE_O_LINUX_FAZ_DIFERENTE.md)): SMU e SDMA1 fora, memória fora; sobram D2 (anel e fences em memória de sistema, proposta) e D1 (interrupção) | Daniel decide, eu implemento | 5f |
| 6a | ~~D2 por AGP~~ refutada (Corridas 213/214, doc 586); **D2 por GART pronta** ([doc 587](docs/587_D2_POR_GART_DA_VMID0_COMO_O_LINUX_E_CRITERIO_CORRIDA215.md)), lab 3.363.1.0 → **Corrida 215**, `tools\EXECUTAR_CORRIDA215_D2_POR_GART_600S.cmd` | Daniel | 6 |
| 7 | Decidir se X2b e 4b ficam ou voltam, e retomar o M1b, quando a trava tiver causa | Daniel e eu | — |

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
