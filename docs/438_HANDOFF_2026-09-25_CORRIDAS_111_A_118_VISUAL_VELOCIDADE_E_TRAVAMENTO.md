# Passagem de bastão — BC-250 WDDM após as Corridas 111–118

> **Data:** 25/09/2026, após a análise da Corrida 118. **Leia primeiro este documento, o topo de [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md), [`AGENTS.md`](../AGENTS.md), os critérios/resultados [`425`](425_CORRIDA112_TRAVAMENTO_FLUSH_TLB_E_DIAGNOSTICO_DE_VELOCIDADE.md)–[`437`](437_CORRIDA118_DX73_TRIANGULO_PIXELS_COPIA_E_R1.md), e só então o histórico [`420`](420_HANDOFF_2026-09-24_CORRIDAS_105_A_110_DWM_ESTAVEL_TRAVAMENTO_E_TMR.md).** O doc 420 descreve o estado de 24/09; várias pendências dele foram resolvidas depois. Este handoff é o ponto de partida operacional, não substitui a evidência de cada corrida.

## 0. Resumo para quem acabou de chegar

**Produto pretendido:** BC-250 (`1002:13FE`, GC 10.1.3, 24 CUs, 16 GB GDDR6) no Windows 10 22H2 x64, monitor ligado ao DisplayPort da placa, desktop WDDM display+render e jogos D3D12/Vulkan sem WARP ou fallback de software. Display v1 herda o UEFI GOP, modo único 2560×1080 a ~56,13 Hz, scanout em VRAM real, cópias via SDMA; **não escrever registradores DCN** antes de P9. O modo GOP atual limita atualização visível a ~56 Hz; 90/200 FPS visíveis exigirão outro timing/modo, embora render interno acima disso seja questão separada.

**O que funciona agora, no escopo medido:** o candidato lab **3.277.1.0 / DX.73** completou a Corrida 118 de 180 s. Daniel viu desktop legível, triângulo apenas na janela, sem piscar. Harness D3D11 60/60 `PRESENT_OK`; 60/60 cópias do quadro 1280×720 preservaram o pixel central `FF7F4040` e canto `FF2673E5` nos dois lados físicos da cópia, com 5/5 amostras coincidentes. Watchdog recuperou BasicDisplay Código 0 no mesmo boot em cinco amostras. **Veredito A do critério específico da 118, não sucesso do produto.** A 117, com UMD/RADV idênticos e outra instrumentação KMD, não mostrou o triângulo. A falha visual é intermitente e a DX.73 não a corrigiu por construção: só registrou valores que já eram lidos.

**Dois bloqueios centrais:** (1) **velocidade**: na 118 o DWM iniciou 322 Presents nos primeiros 120 s (**2,68/s**), harness **0,9 FPS**; `pfnPresentCb` do harness p50 **443 ms**, enquanto `DxgkDdiPresent` p50 **0,746 ms**. A maior espera medida está fora dessa DDI, sem local exato; (2) **confiabilidade**: corridas de 600 s travaram duramente depois de ~3–8 min, e a 115 travou antes dos 180 s. O watchdog só recupera no mesmo boot quando o kernel continua vivo. Não há prova de 600 s estáveis, 60 FPS ou jogos.

**Estado da máquina ao entregar:** `Adaptador de Vídeo Básico da Microsoft` OK/Código 0. `C:\BC250_REQUIRE_CLEAN_BOOT.json` está em `CLEAN_BOOT_REQUIRED`, com `rollback_boot_ticks=639259346665000000`, igual ao boot atual no último exame. **Antes de qualquer novo bind, Daniel deve fazer boot frio com corte de energia.** O binder verifica e recusa o mesmo boot; não remova o marcador manualmente. `C:\Windows\MEMORY.DMP` ainda é o arquivo antigo de 24/09 13:56:30, 887.730.418 bytes. Novo dump sob hang não foi obtido.

## 1. Navegação e fonte de verdade

| Documento/artefato | Papel atual |
|---|---|
| [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) | Fonte única do estado e pendências; as entradas novas ficam no topo. As tabelas históricas inferiores não substituem as corridas recentes. |
| [`docs/164_ROADMAP_WDDM_JOGOS.md`](164_ROADMAP_WDDM_JOGOS.md) e [`281`](281_PLANO_PRODUTO_DESKTOP_E_JOGOS_TRANSPARENTES.md) | Escopo de produto; fases antigas do roadmap são histórico. |
| [`docs/403`](403_HANDOFF_2026-09-24_TRAVAMENTO_DURO_E_VELOCIDADE_DO_DESKTOP.md), [`412`](412_HANDOFF_2026-09-24_CORRIDA104_DX60_E_CONTINUIDADE.md), [`420`](420_HANDOFF_2026-09-24_CORRIDAS_105_A_110_DWM_ESTAVEL_TRAVAMENTO_E_TMR.md) | Contexto anterior; não usar como status corrente. |
| [`docs/425`](425_CORRIDA112_TRAVAMENTO_FLUSH_TLB_E_DIAGNOSTICO_DE_VELOCIDADE.md)–[`437`](437_CORRIDA118_DX73_TRIANGULO_PIXELS_COPIA_E_R1.md) | Critérios prévios, resultados e provas das corridas recentes. |
| `docs/evidence/<timestamp>-...-lab-binding/` | Provas por bind. Preferir arquivos `*-FINAL`, `binding-provenance.json`, `safe-bind-final.json` e `display-log-final-archive.json`. |

**Governança:** depois de cada corrida, relatar ao Daniel (1) avanço frente à corrida pertinente, (2) sucessos/métricas/prova, (3) falhas e limites, (4) veredito frente ao critério *escrito antes*, (5) recuperação e pendências. `PRESENT_OK`, Exit 0, Código 0, prova amostrada de cópia e BasicDisplay restaurado não equivalem, isoladamente, a visual correto ou jogo funcional. Não repetir barras de percentuais antigas. Uma mudança offline deve ser chamada **não validada em hardware**. Daniel executa o bind físico; a IA compila, analisa, prepara e documenta.

## 2. Linha do tempo que importa

| Corrida | Candidato | Janela/observação | Resultado e ponto de aprendizado | Doc |
|---|---|---|---|---|
| 105–107 | DX.61–64 | 180 s | Guardião R1; contrato de destino Blt e janela D3D11 com triângulo composto pelo DWM; depois DX.64 eliminou recriação do device na janela curta. | [`413`](413_DX61_63_RECOVERY_BLT_DESTINO_SEGUIMENTO_ALOCACAO_E_CRITERIO_CORRIDA105.md)–[`416`](416_CORRIDA107_DX64_RESULTADO_E_CRITERIO_CORRIDA108_600S.md) |
| 108–110 | DX.64–66 | 600 s | Travamentos duros ~3–6 min; Radeon Software não é condição necessária. Na 110, segmento WDDM sobrepunha TMR e página da migalha estava dentro do TMR; causa do hang **não provada**. | [`417`](417_CORRIDA108_TRAVAMENTO_6MIN_RADEON_E_DX65_CRITERIO_CORRIDA109.md)–[`421`](421_CORRIDA110_RESULTADO_TRAVAMENTO_TMR_E_MIGALHA_RECUSADA.md) |
| **111** | DX.67, 3.271.1.0 | Travou ~6–7 min, desktop sem artefatos antes | Segmento isolado de TMR/folgas, mas hang persistiu. Última migalha em `SDMA_REG_READ` depois da leitura; não localiza a causa. | [`423`](423_CORRIDA111_DX67_SEGMENTO_LIMPO_TRAVAMENTO_E_MIGALHAS_B.md) |
| **112** | DX.68, 3.272.1.0 | Travou ~7 min; reboot pelo botão | Migalha em DDI de paginação `FLUSH_TLB` na fase `ANTES`, sem batimento depois; isso marca chamada em andamento, não instrução/causa. Mediu ~550 mil logs MSAA do Explorer e 3.250 draws DWM com flush imediato. | [`425`](425_CORRIDA112_TRAVAMENTO_FLUSH_TLB_E_DIAGNOSTICO_DE_VELOCIDADE.md) |
| **113** | DX.69, 3.273.1.0 | 180 s, visual legível, R1 | Amostragem do log MSAA. DWM primeiros 120 s **2,60/s**, harness 1,0 FPS. Ganho de taxa observado, causalidade exclusiva não provada. | [`427`](427_CORRIDA113_DX69_GANHO_DE_DWM_E_RECUPERACAO_R1.md) |
| **114** | DX.70, 3.274.1.0 | 180 s, R1 | DWM **3,09/s**, harness 1,3 FPS. **Falha visual**: triângulo na janela mais uma cópia grande piscando no desktop. UMD fazia escrita direta no scanout **e** callback WDDM. Foto arquivada. | [`429`](429_CORRIDA114_DX70_RESULTADO_DUPLA_APRESENTACAO.md) |
| **115** | DX.71, 3.275.1.0 | Travou ~2 min 19 s; recuperação só no boot seguinte | Removida rota direta do DXGI Present. Daniel viu apenas a janela, sem duplicação; 60/60, DWM **3,18/s**. Hang e ausência de dump impediram fechar A. Última fase textual `FLUSH_TLB` é comum demais para apontar causa. | [`431`](431_CORRIDA115_DX71_JANELA_CORRIGIDA_TRAVAMENTO_E_RASTRO.md) |
| **116** | Mesmo DX.71 | 180 s, visual correto, R1 | DWM **2,40/s**, abaixo do piso prévio 2,60; harness 1,2 FPS. Veredito **parcial**. Callback `pfnPresentCb` do harness p50 **433 ms**. O critério prévio não tinha categoria para visual/R1 corretos com taxa abaixo do piso; não reescrever depois. | [`433`](433_CORRIDA116_DX71_180S_VISUAL_ESTAVEL_R1_FPS_ABAIXO_DO_PISO.md) |
| **117** | DX.72, 3.276.1.0 | 180 s, R1; **triângulo ausente** | 60/60, DWM **2,78/s**, harness 1,1 FPS. Todos os 60 Blt da DDI em `STATUS_SUCCESS`, p50 **0,676 ms**; callback UMD p50 **444 ms**. Veredito **E** por falha visual. | [`435`](435_CORRIDA117_DX72_TEMPO_PRESENT_E_TRIANGULO_AUSENTE.md) |
| **118** | DX.73, 3.277.1.0 | 180 s, triângulo só na janela, R1 | **A** do critério curto: 60/60 e 60/60 pixels centrais/cantos preservados até destino da cópia. DWM **2,68/s**, harness 0,9 FPS, callback p50 **443 ms**, DDI p50 **0,746 ms**. Nada de estabilidade longa ou FPS alvo. | [`437`](437_CORRIDA118_DX73_TRIANGULO_PIXELS_COPIA_E_R1.md) |

As taxas DWM acima são **inícios de Present por segundo nos primeiros 120 s desde o primeiro Present**, não medição direta de quadros efetivamente exibidos. O DWM apresenta sob demanda; não traduzir esse número em FPS do monitor. Comparar a mesma janela e a mesma carga, e ainda assim não atribuir variação entre corridas a uma linha de código sem isolamento.

## 3. Evidência da Corrida 118 e proveniência do candidato ativo

**Evidência:** `docs/evidence/2026-09-25_09-06-32-DESKTOP-HQAG6T4-lab-binding/`. O `display-log-final-archive.json` declara `complete_at_capture=true`, 24.554.878 bytes. Hashes SHA-256: KMD final `D6AD824E671EBF25936DBD2A8B783BD6FF5AC02C72D26C9B03826779C66F7005`; UMD DWM PID 1200 `FE8B488BE9773B094CB89BF690002AA70F9CEE3DD360A62822063D6A16CEC8A6`; UMD harness PID 8012 `D955DA4C4C598175F0304B0F2B9AF992E0784A339DCE713B72CA4270599089C6`. `safe-bind-final.json` tem `RECOVERY_OK_ONLINE`, `armed_boot_ticks=boot_ticks=639259346665000000` e cinco BasicDisplay/0 entre 09:09:37 e 09:09:41. O estado `BLOCKED` no journal foi intermediário na mesma tentativa, **não** resultado terminal. Três selftests RADV deram 5/5.

**Pacote que rodou:** `driver/wdk/package/output_lab/`, perfil `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `DriverVer` empacotado `09/25/2026,3.277.1.0` (INF fonte `3.277.0.0`). Hashes SHA-256: SYS assinado `5C33FCB9633CD58EAEA61ED7F83ED9BDB640AD1C27D8FBA346512CA6834C70C5`; PDB privado `93292A51839BC090A3B5F7C9753BD8B3C23A7D851DB4877C1D795901E900E036`; UMD `884A0A60CB5B1AE3C55D849A72491CC961C64EF268285EB248FDE0E2DAB29AC7`; INF `F0584C70021402B180F0256CAC3F4726CD6511090823E88CE184ABB7C52B64E6`; CAT `A777CB930152953C1374E7BCF72CD018D0DC3A84FD5FAF980B0D2A2CE844ACF7`; RADV externo implantado `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`. O manifesto assinado/auditado está em `driver/wdk/package/output_lab.manifest.json` e as cópias de SYS/PDB exatos também estão na evidência da 118.

**Git:** branch `p6-rendernode-wddm`, HEAD `8a73051` no momento do handoff. As mudanças DX.56–73 e docs 404–437 estão **sem commit**; antes deste documento `git status --porcelain` tinha 22 arquivos modificados e 91 não rastreados. Preserve o trabalho existente; não faça reset/clean para “organizar”. `*.sys` e `*.pdb` são ignorados por `.gitignore`; **um commit normal não carrega o binário assinado nem os símbolos**. Antes de atribuir um binário a fonte ou fazer outro pacote, conferir hashes do manifesto/evidência. A DLL UMD e manifesto são rastreados, o CAT/INF do pacote aparecem modificados. Não commitamos nesta passagem.

## 4. Arquitetura e locais exatos de código

| Caminho | Local | Estado/prova |
|---|---|---|
| DXGI Present UMD | `driver/umd/bc250_d3d_umd.c`, `Bc250DxgiPresent` e preparo por volta de 5100–5385 | DX.71 deixa **somente WDDM** apresentar DXGI. O alvo Vulkan é destilado em buffer linear, ponte para alocação WDDM, `pfnPresentCb`. Não reintroduzir `radv_bc250_present_scanout` nesta rota: causou imagem grande duplicada na 114. |
| Ponte/cópia | `driver/umd/bc250_d3d_umd.c`, `Bc250UmdBridgeLinearToWddm`; KMD `driver/wdk/bc250_active_miniport.c` por volta de 15545–15745 | Escape `COMPOSITE_BLT` SDMA confere 5 amostras antes/depois; DX.73 registra centro/canto 1280×720 com pitches do escape. 118: 60/60 `Src=Dst=FF7F4040`, canto `FF2673E5`. Prova amostral, não byte a byte da imagem inteira. |
| DDI Present KMD | `driver/wdk/bc250_display_ddi.c`, `Bc250DxgkDdiPresent` por volta de 3404–4120 | DX.72 usa `KeQueryPerformanceCounter` e registra todos os Blt e Flip amostrado antes da linha nova de log. Mede só execução válida dessa DDI, não duração do callback UMD nem submissão posterior. O teste offline `driver/tests/display_vidpn_test.c` simula o contador importado. |
| `CenterPixel` antigo | `driver/wdk/bc250_display_ddi.c`, amostra `SubRect` por volta de 3625–3655 | Usa `CurrentMode.Pitch` do GOP 2560×1080 para origem Blt 1280×720. **Inválido como centro do harness**: na 116 o triângulo estava visível e esse campo lia azul. Usar DX.73, não este campo, para o pixel central. |
| Fluxo Vulkan | `driver/umd/bc250_umd_vulkan_bridge.c`, `Bc250UmdVkFlushCommands` ~816–900; `driver/umd/bc250_d3d_umd.c` ~3047–3058 e ~3290 | 118: 220 flushes do harness, `SubmitUs` p50 64,3 ms; 60 com Draw e 160 com `DrawsNoLote=0`. Estes últimos podem conter clear, destilação/cópia e readback; `DrawsNoLote=0` não significa command buffer vazio. Batching `s_Bc250Dx51DrawBatchEnabled=FALSE` por bissecção DX.55: ligado piscou/travou nas 94–96/99. Não religar por palpite. |
| Segurança/recovery | `tools/bind-bc250mcdm-lab.ps1`, `tools/watch-bc250mcdm-safe-bind.ps1`, `tools/EXECUTAR_D3D11_NATIVO.cmd` | Watchdog 180 s nos ensaios recentes; se kernel morre, só pode completar no boot seguinte. Marker `CLEAN_BOOT_REQUIRED` impede bind no mesmo boot. Falhas de recuperação são falha funcional, mesmo que `PRESENT_OK`. |

**Leitura crítica da latência:** na 118, harness p50 de `pfnPresentCb` **443 ms**, DDI Present **0,746 ms**. O restante está fora **dessa DDI cronometrada**. Isso **não prova** que seja o scheduler: pode envolver VidMm, fila, residência, outras DDIs e esperas do runtime. O bloco de 60 quadros teve p50 107 ms do começo do DXGI Present até a ponte WDDM, 1 ms da ponte à entrada do callback, 443 ms de ida e volta do callback e 224 ms da saída até o próximo Present. São distribuições distintas; não somar medianas como “um quadro típico”. Ver doc 437 e logs UMD exatos.

## 5. O que eu fiz e erros que não se devem repetir

1. **DX.71 (antes desta sessão):** removi a escrita direta no scanout do DXGI Present porque a foto da 114 e 653 chamadas extras mostravam dupla rota. A 115 e 116 observaram janela correta, mas a 117 perdeu o triângulo. **Erro de inferência a evitar:** uma corrida visual correta não estabelece correção permanente; a intermitência permanece aberta.
2. **Corrida 115:** o rastro terminou em `FLUSH_TLB` fase 5. A mesma assinatura ocorreu em **933/1296** amostras (~72%); eu havia dado peso excessivo à última linha. Não atribuir hang ao flush sem dump/stack ou discriminação temporal melhor. O último log pode ser vítima ou mero evento frequente.
3. **Corrida 116:** defini piso DWM 2,60/s e categorias A/B/E que não cobriam “visual/R1 corretos com taxa abaixo do piso”. Registrei **parcial**, sem reescrever o critério. Em novos critérios, prever resultados mistos de modo explícito.
4. **DX.72 build:** introduzi `KeQueryPerformanceCounter` na DDI e esqueci que `display_vidpn_test.exe` liga a DDI em user mode. Primeiro build falhou em `__imp_KeQueryPerformanceCounter`; adicionei stub monotônico em `driver/tests/display_vidpn_test.c`, repeti build e auditorias, que passaram. Nunca confundir uma falha de link de teste com falha do SYS.
5. **Runner DX.72:** criei `.cmd` em LF; a auditoria acusou que `cmd.exe` corromperia a leitura. Converti para CRLF **antes do bind**. A auditoria do repositório exige CRLF para runners Windows.
6. **Runner DX.73:** a cópia inicial do runner apontava por engano para `prepare-corrida118-dx72.ps1`; corrigi para `prepare-corrida118-dx73.ps1` e conferi preflight **antes da Corrida 118**. Validar sempre a referência cruzada runner→preflight.
7. **Pixel falso:** o `CenterPixel` legado do KMD usa pitch do display para um Blt de tamanho menor; na 117 ele deu `FF00A0F4` e poderia ter sido tomado como prova do triângulo perdido. Na 116, visual correto, deu azul. DX.73 usa os pitches reais da cópia. Não usar amostras sem conferir geometria/endereço.
8. **Taxa e prova:** `PRESENT_OK` não lê pixels; `COMPOSITE_DATA_PROOF SAMPLED_PASS` prova cinco pontos até a alocação, não a janela no monitor. `BYTE_PROOF PASS` no scanout da primary DWM não prova o triângulo do harness. O relato visual do Daniel é parte do critério. DWM Presents/s não são automaticamente FPS exibidos.

## 6. Segurança e reprodução do pacote

**Nunca executar `tools/EXECUTAR_D3D11_NATIVO.cmd` ou o runner da 118 só para “conferir”: eles fazem bind físico.** Para inspecionar sem instalar:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare-corrida118-dx73.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/audit-bc250mcdm-lab-package.ps1
python tools/audit-bc250mcdm-safety.py
```

No fim desta sessão, os três portões acima passaram (o preflight da 118 foi executado antes do bind; os outros passaram ao empacotar). Para reconstruir **outro** candidato, elevar versão no INF fonte, alterar uma variável com critério escrito antes e executar o build contratado/lab, depois assinatura e auditoria:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr
powershell -NoProfile -ExecutionPolicy Bypass -File tools/audit-bc250mcdm-lab-package.ps1
python tools/audit-bc250mcdm-safety.py
```

O primeiro comando **compila** SYS de lab e roda contratos; seu nome avisa do conteúdo de bring-up da imagem, mas **não faz bind**. O empacotador **não compila**, só materializa/assina a imagem corrente. Se UMD mudar, executar `driver/umd/build-umd.bat` antes do pacote. Verificar RADV externo por SHA-256 no preflight. Não armar `GfxWaveProbe` de modo improvisado: esta opção escreve seletores durante TDR e o empacotador exige `-IAcceptSelectorWritesDuringTdr`; o pacote da 118 já inclui a configuração testada. Qualquer runner `.cmd` novo deve estar em CRLF e apontar ao seu preflight. O preflight da 118 fixa hashes: **não reutilizá-lo para versão nova**.

**Se houver travamento duro:** a configuração atual registra dump de kernel (`CrashDumpEnabled=2`, `AlwaysKeepMemoryDump=1`, `CrashOnCtrlScroll=1` para `i8042prt`/`kbdhid`). Daniel pode tentar **segurar Ctrl direito e pressionar Scroll Lock duas vezes**, aguardar BugCheck/dump, e só então resetar se o teclado não responder. Isso não foi provado sob hang; registrar se funcionou. Para preservar migalhas de VRAM, um reset pelo botão é diferente de cortar energia; o corte é exigido **antes do próximo bind**, depois de recuperar os logs. Não interpretar `RECOVERY_OK_ONLINE` em boot novo como R1.

## 7. Próximos passos recomendados, com testes que fecham

| Ordem | Trabalho concreto | Teste/critério de fechamento | Cuidado |
|---|---|---|---|
| **P0 velocidade** | Localizar os ~443 ms de `pfnPresentCb` fora da DDI Present com trace temporal do caminho dxgkrnl/VidMm/KMD; aproveitar as 60 chamadas Blt repetíveis do harness. Examinar também `FlushCommands` por **tipo real de comando**: clear, draw, destilação, upload/readback, não só `DrawsNoLote`. | Um ensaio curto deve entregar linha temporal pareada por quadro e separar esperas dentro/fora das DDIs e trabalho Vulkan. Só propor alteração após identificar uma etapa que possa ser removida ou tornada assíncrona sem perder pixel/fence. Comparar com 118: callback p50 443 ms, `SubmitUs` 64,3 ms, DWM 2,68/s, harness 0,9 FPS. | Batching com anel ficou desligado por travamentos/piscar nas 94–96/99. Não religar nem remover `vkWaitForFences`/`vkDeviceWaitIdle` sem prova de dependência. |
| **P0 visual intermitente** | Reproduzir o desfecho da 117 sob DX.73 ou posterior, preservando centro/canto de **cada** uma das 60 cópias e observação do monitor. | Se `Src` divergir de `FF7F4040`, investigar ponte Vulkan→WDDM; se `Dst` divergir de `Src`, investigar SDMA; se `Src=Dst=FF7F4040` e triângulo ausente, investigar Present/composição DWM/janela. Repetição com critério escrito antes, não seleção retrospectiva de corrida favorável. | Na 118 `Src=Dst` não prova que a 117 também tinha esses valores; só DX.73 registra os valores. |
| **P0 estabilidade** | Voltar ao ensaio 600 s com candidato controlado, desktop visual, evidência durável e protocolo de dump/rollback. | Completar 600 s **sem hang**, com visual correto, harness e DWM, e R1 no mesmo boot; se travar, obter dump/stack ou registrar teclado não responsivo e migalhas, sem culpar última linha. Repetir para intermitência antes de declarar robustez. | A 115 travou aos ~139 s; as 108–112 em 3–8 min. Uma corrida de 180 s não fecha. |
| **Produto** | Ampliar aplicação D3D11 real, D3D12/Vulkan, residência/sincronização/recuperação e depois DCN básico em P9 conforme `281`/BASE. | Jogos/aplicações **não modificados**, pixels conferidos e estabilidade sob carga; sem WARP/fallback. | Não reduzir meta a demo ou render-only. |

**Primeiro passo prático da próxima IA:** ler `driver/umd/bc250_umd_vulkan_bridge.c:816` e chamadas em `bc250_d3d_umd.c` para separar os 220 flushes por trabalho gravado; depois desenhar trace de `pfnPresentCb` sem mudar apresentação. Em paralelo, escolher se o próximo bind mede latência ou reproduz visual/hang; **um critério prévio para cada ensaio**. Nenhum novo candidato está preparado depois da 118. O pacote `output_lab` continua o 3.277.1.0 validado no ensaio curto.
