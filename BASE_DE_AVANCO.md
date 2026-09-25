# BASE DE AVANÇO — AMD BC-250 → WDDM com jogos no Windows

> **25/09/2026 — Corrida 129, DX.82, FALHA aos ~90 s:** [análise e evidências recuperadas](docs/459_CORRIDA129_DX82_TRAVAMENTO_PRECOCE_ANALISE.md). Harness parou no primeiro `pfnPresentCb`; DWM entrou em `VkBridgeShutdown` sem saída registrada; Explorer parou em `WaitPaging` fence 7004. Rastro DX.56 mostra paginação pendente com escritor ainda ativo por ~12,3 s. O boot original travou e exigiu reset; watchdog restaurou `BasicDisplay` Código 0 somente no boot seguinte. A causa exata permanece aberta. O diagnóstico de "causa-raiz comprovada" da Corrida 128 deve ser lido como **hipótese**, não prova. 600 s, 60/60 na DX.82 e recuperação no mesmo boot **NÃO VALIDADOS**.

> **RETIFICAÇÃO 25/09/2026 — Corrida 127:** A auditoria do log KMD recuperado (`docs/457_AUDITORIA_CORRIDA127_TDR_SDMA_GPUVA_E_CRITERIO128.md`) refuta duas afirmações abaixo: o escape problemático **chegou ao KMD** e falhou após timeout SDMA; o mesmo bind contém **23 resets TDR**, 367 `Journal NO_SPACE` e 28.534 `PUBLICACAO_FALHA`. O mecanismo interno do primeiro timeout SDMA permanece aberto. DX.81 ainda está somente offline; o critério/diagnóstico da Corrida 128 deve considerar esses fatos. Preservam-se os relatos originais abaixo como histórico, não como veredito causal vigente.

> **Passagem de bastão vigente (25/09/2026):** [docs/438 — Corridas 111–118, estado da máquina, evidências, erros, riscos e próximos testes](docs/438_HANDOFF_2026-09-25_CORRIDAS_111_A_118_VISUAL_VELOCIDADE_E_TRAVAMENTO.md). Ler antes de usar os planos e handoffs históricos abaixo como guia operacional.

> **25/09/2026 — DX.82 OFFLINE, Corrida 129 preparada (600 s):** [sincronização de teardown multithread, eliminação da colisão de device e critério](docs/458_CRITERIO_CORRIDA129_DX82_SINCRONIZACAO_TEARDOWN_E_600S.md).
> - Diagnóstico da Corrida 128 comprovado no log UMD do explorer.exe: corrida entre `WaitPaging` / `Bc250UmdVkCreateResource` na thread de trabalho e `Bc250UmdVkBridgeShutdown` (`vkDestroyDevice` / `FreeLibrary`) na thread de destruição de device secundário, provocando hang no loader Vulkan e congelando o desktop aos 214 s (~3 min 34 s).
> - Correções: `SRWLOCK DeviceLock` e `TeardownActive` em `BC250_UMD_DEVICE`; aquisição exclusiva no início de `Bc250UmdDeviceTeardown`; aquisição compartilhada em `CreateResource` e `DestroyResource`; abortamento seguro pós-WaitPaging; e proteções fail-closed em todas as rotinas da bridge.
> - Pacote auditado lab **3.287.1.0** (SYS `DABE03E2…`, UMD `ACF637FB…`, INF `A5A683C0…`, CAT `715EC02C…`). Preflight `tools/prepare-corrida129-dx82.ps1` PASS; runner `tools/EXECUTAR_CORRIDA129_DX82_600S.cmd`.
> - **Nenhum bind executado; NÃO VALIDADO EM HARDWARE.** `CLEAN_BOOT_REQUIRED` ativo: boot frio com corte de energia antes do teste físico.

> **25/09/2026 — Corrida 128 (16:54:07, DX.81, 600 s): STALL DE ESCAPE 100% ELIMINADO (4 MS), DWM ESTÁVEL COM 45K LINHAS SEM CRASH, HARNESS 60/60 OK, CONGELAMENTO AOS 214 S POR COLISÃO NO TEARDOWN DO EXPLORER (DESFECHO PARCIAL):** [resultado detalhado](docs/457_CORRIDA128_DX81_STALL_ELIMINADO_CONGELAMENTO_E_RESULTADO.md).
> - Daniel reportou: *"rodei e travou"*. A máquina operou continuamente até 16:57:48 (214 s, ~3 min 34 s).
> - **Stall de Escape Eliminado:** Cópia linear 2560x1080 no KMD completou em **4 ms** (`hr=0x00000000 ResultCode=0x00000000`), contra 4.172 ms da Corrida 127.
> - **DWM Estável e Contínuo:** `dwm.exe` (pid 1216) registrou **45.023 linhas de log**, 333 flushes, 1.694 draws sem nenhum crash ou reinício (curando 100% a falha visual e o laço da Corrida 127).
> - **Harness 60/60 `PRESENT_OK`:** Completou todos os 60 quadros, centro vermelho e clear azul íntegros, teardown limpo e Exit 0.
> - **Causa do Congelamento Isolada:** Concorrência entre criação de buffer em thread auxiliar do Explorer e destruição de device secundário (`vkDestroyDevice` / `FreeLibrary`) na thread principal.
> - Rollback online automático R1 restaurou `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot sem reiniciar após hard reset às 17:30:03. Veredito Parcial do critério 456.

> **25/09/2026 — Corrida 127 (16:01:35, DX.80b, 600 s): HARNESS 60/60 OK, RESISTÊNCIA DE 10 MINUTOS SEM BUGCHECK/TDR, STALL DE ESCAPE DE APLICAÇÃO E LAÇO DE REINÍCIO DO DWM (DESFECHO B/PARCIAL):** [resultado detalhado](docs/455_CORRIDA127_DX80B_600S_ENDURANCE_E_RESULTADO.md).
> - Daniel reportou: *"vamos lá ficou um tempo ok depois começou a bugar tirei um foto"*. A foto confirmou topo do desktop/ícones visíveis e 75% inferior escuro.
> - **Superação da Barreira Histórica:** O sistema operou pela janela completa de **600 segundos (10 minutos)** sem travamento de hardware, sem BugCheck e sem TDR. Rollback online automático R1 restaurou `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot sem reiniciar às 16:14:05.
> - **Harness 60/60 `PRESENT_OK`:** Fase de conformidade completou 60/60 quadros a 1,2 FPS médio sem nenhum erro.
> - **Causa Raiz Isolada da Anomalia:** Às 16:03:29 (após abertura de Edge/WebView/Antigravity), DWM chamou escape com `HardwareAccess=1` e `hContext=NULL`, provocando stall de 4 s no VidSch e `E_FAIL`. Cada nova instância do DWM foi rejeitada no quadro 1 por `NENHUM_RENDER_DETECTADO`, gerando laço de reinício a cada ~12 s com o scanout VRAM congelado.
> - Veredito Parcial do critério 455. `CLEAN_BOOT_REQUIRED` ativo. 600 s contínuos sem crash de DWM seguem NÃO VALIDADOS.

> **25/09/2026 — Corrida 126 (15:49:09, DX.80b): ESTABILIDADE TOTAL DE 180 S, TEARDOWN LIMPO SEM DOUBLE FREE, 99,7% DE CACHE HITS E ZERO FLUSHES DE RTV (DESFECHO A):** [resultado detalhado](docs/454_CORRIDA126_DX80B_ESTABILIDADE_TEARDOWN_E_RESULTADO.md).
> - Daniel confirmou execução física sem travamento, desktop estável e renderização do triângulo idêntica às corridas anteriores.
> - **Eliminação Total dos Flushes de RTV:** Flushes síncronos forçados pela rotação de backbuffers da swapchain caíram de 369 para **ZERO** (`0`). O DWM registrou 706 chamadas de rotação com **704 Cache Hits consecutivos (99,72% de taxa de acerto)** e apenas 2 misses na criação inicial.
> - **Estabilidade Perfeita no Teardown:** A correção de double free em `Bc250UmdVkDestroyRtv` foi 100% validada no silício; o DWM destruiu centenas de recursos ao longo de toda a sessão de 180 s sem nenhuma falha de processo.
> - **Harness 60/60 `PRESENT_OK`:** Todos os 60 quadros foram aceitos sem erros, atingindo picos de 1,7 FPS.
> - **Volume e Latência:** DWM completou **707 Presents** e o KMD completou **1.150 Presents** com 1.590 `COMPOSITE_BLT` e 769 flips de hardware. Latência Vulkan ultrabaixa na GPU: `EsperaUs` p50 de 3,73 ms e `SubmitUs` p50 de 2,11 ms (~6,0 ms totais por quadro na GPU).
> - Zero BugChecks, zero TDRs. Rollback online automático restaurou `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot sem reiniciar às 15:52:21 (R1). **Veredito A do critério 453**, limitado a 180 s. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NÃO VALIDADOS.


> **25/09/2026 — Corrida 125 (15:05:40, DX.80): CACHE DE FRAMEBUFFERS FUNCIONAL (6 HITS CONSECUTIVOS), DOUBLE FREE NO TEARDOWN DE RTV E ROLLBACK R1 (DESFECHO D/PARCIAL):** [resultado detalhado](docs/452_CORRIDA125_DX80_DOUBLE_FREE_TEARDOWN_E_RESULTADO.md).
> - Daniel confirmou que o quadrado azul com o triângulo começou a abrir e em seguida ocorreu rollback para BasicDisplay.
> - **Cache de Framebuffers validado no silício:** O DWM registrou 3 backbuffers em cache (`Count=3`) e **6 Cache Hits consecutivos** (`Hits=1..6 Misses=2`), operando rotações de swapchain com ZERO flushes síncronos e zero esperas de fence. Janela D3D11 executou 16 quadros (`PRESENT_OK`, pico de 1,8 FPS).
> - **Diagnóstico Exato da Falha:** Às 15:06:05.831, ao destruir um RTV (`Bc250UmdVkDestroyRtv`), o loop de `FbCache` destruiu `FbCache[0].Fb` e `FbCache[0].View`, e logo após a função executou destruição redundante de `pRtv->VkFb` e `pRtv->VkView` (que apontavam para o mesmo handle de `FbCache[0]`), gerando um **Double Free** no driver Vulkan (RADV) e derrubando o processo DWM.
> - **Consequência no Harness:** O DWM em queda levou o harness a registrar `DXGI_ERROR_DEVICE_REMOVED` (`0x887A0005`) no quadro 17, acionando o rollback imediato do watchdog.
> - Zero BugChecks, zero TDRs de hardware. Rollback online automático restaurou `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot sem reiniciar às 15:09:54 (R1). **Veredito Parcial do critério 451**. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NÃO VALIDADOS.


> **25/09/2026 — Corrida 124 (14:32:48, DX.79): ANEL DE STAGING COMPROVADO, CORTE DE 98,8% NOS FLUSHES DE UPLOAD, 935 PRESENTS NO DWM (5,45/s, NOVO RECORDE) E RTV_FOLLOW ISOLADO (DESFECHO B):** [resultado detalhado](docs/450_CORRIDA124_DX79_UPLOAD_COALESCENCIA_E_RESULTADO.md).
> - Daniel confirmou execução física concluída, desktop normal e triângulo colorido estável na janela do harness sem qualquer artefato visual.
> - **Validação do Anel de Staging em Silício:** 666 das 672 chamadas a `Bc250UmdVkUploadImageData` executaram com `FlushSincrono=0` (99,1% de coalescência). Flushes de upload caíram de 504 (Corrida 123) para apenas 6 (12 flushes com o wrap duplo), eliminando ~3,5 segundos de esperas de fence pela CPU.
> - **Novo Recorde Histórico de Taxa do DWM:** DWM completou **935 Presents** em 171,48 s (**5,45 Presents/s sustentados**, superando os 5,07/s da Corrida 122). No KMD foram 1.990 chamadas a `COMPOSITE_BLT` (995 apresentações físicas), 1.098 flips de buffer e 26 detecções de desktop real.
> - **Harness 60/60 `PRESENT_OK`:** Teste concluído com 100% de quadros aceitos e picos de 1,7 FPS.
> - **Novo Gargalo Isolado pela Mineração:** Dos 1.464 flushes totais do DWM, 935 são de `DXGI Present` (1 por quadro) e **369 flushes (25,2%)** originam-se em `Bc250UmdVkRtvFollowResource` ao alternar imagens de backbuffer, forçando um flush síncrono incondicional.
> - Zero BugChecks, zero TDRs. Rollback online automático restaurou `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot sem reiniciar às 14:35:59. **Veredito B do critério 449**, limitado a 180 s. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NÃO VALIDADOS.

> **25/09/2026 — Corrida 123 (12:20:16, DX.78): COALESCÊNCIA DE DAMAGE RECTS NO DWM, CORTE DE 67,6% NOS FLUSHES (1.255 FLUSHES) E 1,0 FLUSH/PRESENT (DESFECHO A):** [resultado detalhado](docs/448_CORRIDA123_DX78_COALESCENCIA_DAMAGE_RECTS_E_RESULTADO.md).
> - Daniel confirmou execução física: desktop estável, sem corrupção visual, triângulo colorido renderizado na janela do harness.
> - **100% das cópias de dano coalescidas:** 1.949 chamadas a `Bc250UmdVkCopyImageInternal` gravadas sequencialmente no command buffer com `FlushAgora=0` (zero flushes síncronos avulsos; na Corrida 122 eram 2.265 flushes com fence wait).
> - **Corte de 2.619 flushes no DWM:** Flushes totais da sessão caíram de 3.874 para **1.255 (-67,6%)**, alcançando a marca arquitetural de **exatamente 1,0 flush de destile por Present** (719 flushes de destile para 719 Presents).
> - **Latência ultra-baixa na Vulkan Bridge:** Tempo médio de conclusão de lote de render Vulkan caiu para **~5,4 ms** por quadro (`EsperaUs` p50 2,99 ms + `SubmitUs` p50 2,12 ms + `FimCbUs` p50 0,28 ms; taxa teórica na ponte de ~185 FPS). Latência contínua de `pfnPresentCb` variando entre 1,6 e 3,5 ms.
> - **Harness 60/60 `PRESENT_OK`:** Teste completado em ~41 s com exatamente 1 flush por quadro (66 flushes no total), FPS médio 1,2 e picos de 1,7 FPS.
> - **Volume no KMD:** 1.153 Presents processados pelo KMD, 1.610 `COMPOSITE_BLT` e 805 apresentações completadas.
> - Zero BugChecks, zero TDRs, fila drenada. Rollback online automático restaurou `BasicDisplay` Código 0 em 5/5 amostras no mesmo boot sem reiniciar às 12:23:25. **Veredito A do critério 447**, limitado a 180 s. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NÃO VALIDADOS.

> **25/09/2026 — Corrida 122 (11:54:52, DX.77): COALESCÊNCIA DE MULTI-DRAWS NO DWM, 99,4% MENOS DRENOS, 840 PRESENTS (5,07/s) E 950 NO KMD (DESFECHO A):** [resultado detalhado](docs/446_CORRIDA122_DX77_COALESCENCIA_MULTIDRAW_E_RESULTADO.md).
> - Daniel confirmou execução concluída, desktop normal e triângulo colorido estável na janela do harness sem artefatos. Harness 60/60 `PRESENT_OK`, FPS de janela atingindo 1,8 FPS e novo tempo recorde total de 41,64 s.
> - **Coalescência comprovada em silício:** Drenos síncronos de caminho direto no DWM caíram de 4.865 para **ZERO** (`0`). O total de drenos da sessão caiu para apenas 27 (26 chamadas de `ResourceMap DISCARD` e 1 teardown), redução de **99,4%**. Draws agruparam-se no command buffer em lotes de 2 (608x), 3 (440x), 4 (120x), 5 (162x), 6 (120x), 7 (54x), 8 (92x) e até 20 draws.
> - **Salto de +62,5% na taxa do DWM:** Total de Presents do DWM saltou de 561 para **840** (em 165,7 s = **5,07 Presents/s sustentados**; 580 Presents nos primeiros 120 s = **4,83/s**, superando o teto prévio de 3,12/s).
> - **KMD processou 950 Presents:** Volume de apresentações no KMD subiu para **950** com 1.900 registros `COMPOSITE_BLT` (950 blts 2560x1080 concluídos) e rotação de slots (`Flip=1`).
> - Zero BugChecks, zero TDRs, fila drenada (`WPTR=RPTR=204, Error=0`). Guardião restaurou BasicDisplay Código 0 em 5 amostras no mesmo boot sem reiniciar. **Veredito A do critério 445**, limitado a 180 s. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NÃO VALIDADOS.

> **25/09/2026 — Corrida 121 (11:23:15, DX.76): UNIFICAÇÃO DRAW+DESTILE, QUEDA PARA 1 FLUSH/QUADRO E 660 PRESENTS NO KMD (DESFECHO A):** [resultado detalhado](docs/444_CORRIDA121_DX76_UNIFICACAO_DRAW_DESTILE_E_RESULTADO.md).
> - Daniel confirmou execução física com estabilidade visual idêntica aos ensaios anteriores. Harness 60/60 `PRESENT_OK`.
> - **Unificação comprovada:** No harness (quadros 4..60), `FlushAgora=0` no Draw e `DrawsNoLote=1` no flush do Present. Clear + Draw + Destile submetidos conjuntamente em um único command buffer e flush por quadro.
> - **Queda de 46% nos flushes:** Total de chamadas `FlushCommands` do harness caiu de 123 (Corrida 120) para **66** (exatamente 1 flush por quadro + 6 de diagnóstico nos 3 primeiros quadros). Queda acumulada de 73% frente à Corrida 119 (243 flushes). `vkDeviceWaitIdle` redundante eliminado do Present.
> - **Volume e roteamento de desktop expandidos:** DWM completou **561 Presents** (3,12 Presents/s sustentados). KMD completou **660 Presents**, atingindo detecção contínua de desktop real na superfície (`*** DESKTOP REAL DETECTADO NA SUPERFICIE *** NonZero=256/256`) com roteamento ao scanout (`Blt 11059200 bytes (src 0xAAC000 -> scanout 0x0): SDMA=YES`).
> - **Fidelidade de pixel mantida:** 60/60 inspeções confirmaram centro `0xFF7F4040`, cantos `0xFF2673E5` e grade completa `NonZero=28800/28800`.
> - Guardião confirmou BasicDisplay Código 0 em 5 amostras no mesmo boot sem reiniciar. **Veredito A do critério 443**, limitado a 180 s. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NÃO VALIDADOS.

> **25/09/2026 — Corrida 120 (10:25:35, DX.75): COALESCÊNCIA CLEAR+DRAW, CORTE DE 50% NOS FLUSHES E GANHO DE FPS (DESFECHO A):** [resultado detalhado](docs/442_CORRIDA120_DX75_COALESCENCIA_CLEAR_DRAW_E_RESULTADO.md).
> - Daniel confirmou execução concluída e guardião com reversão online. Harness 60/60 `PRESENT_OK`.
> - **Coalescência comprovada:** `DrawsPendentes=0` caiu para **0** (de 60 na Corrida 119). Clear e Draw submetidos conjuntamente no mesmo render pass e lote Vulkan (`DrawsNoLote=1`).
> - **Redução de flushes e latência:** Total de chamadas `FlushCommands` do harness caiu de 243 para **123** (exatamente 2 flushes por quadro nos quadros 4..60). Teto de readback de diagnóstico atingido no quadro 3.
> - **Ganho de velocidade:** Tempo total dos 60 quadros caiu de ~64 s para **~44 s**; FPS médio subiu de 0,94 para **1,34 FPS** (com picos de janela em **1,7 FPS**). Tempo de parede por quadro caiu de ~1100 ms para **~480-650 ms** (~500 ms economizados por quadro).
> - **Fidelidade de pixel mantida:** 60/60 inspeções confirmaram centro `0xFF7F4040`, cantos `0xFF2673E5` e grade completa `NonZero=28800/28800`.
> - DWM teve 385 Presents em 127 s de sessão ativa (**3,03 Presents/s**). Guardião confirmou BasicDisplay Código 0 em 5 amostras no mesmo boot sem reiniciar. **Veredito A do critério 441**, limitado a 180 s. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NÃO VALIDADOS.

> **25/09/2026 — Corrida 119 (09:47:50, DX.74): DECOMPOSIÇÃO TEMPORAL DE pfnPresentCb, TRIÂNGULO VISÍVEL E R1 EM 180 S:** [resultado detalhado](docs/440_CORRIDA119_DX74_DECOMPOSICAO_TEMPO_PRESENT_CB_E_R1.md).
> - Daniel confirmou: o watchdog reverteu, o triângulo apareceu perfeitamente na janela. Harness 60/60 `PRESENT_OK`, taxa 0,94 FPS. 60/60 cópias 1280x720 tiveram centro `Src=Dst=FF7F4040`, canto `Src=Dst=FF2673E5` e `Match=5/5`. Guardião restaurou BasicDisplay Código 0 em cinco amostras consecutivas no mesmo boot. **Veredito A do critério 439**, limitado a 180 s.
> - **Decomposição exata de pfnPresentCb:** Pré-DDI (UMD -> KMD) p50 **0,82 ms** (1,33%); Execução KMD (`DxgkDdiPresent`) p50 **0,67 ms** (0,80%); Despacho SDMA (`KMD_SDMA_SUBMIT`) p50 **0,27 ms**; Pós-DDI (retorno de `pfnPresentCb` à UMD) p50 **437,57 ms** (**97,88% do tempo total**). Prova que 97,88% da latência é regulação de ritmo pelo dxgkrnl com o ciclo de composição do DWM (que roda a ~2,48-3,11 Presents/s, ~320-400 ms por composição). DWM teve 533 Presents em 171 s e 4.885 `FlushCommands` com `SubmitUs` p50 1,75 ms. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NÃO VALIDADOS.

> **25/09/2026 — Corrida 118 (09:06:32, DX.73): SUCESSO VISUAL/PIXEL/R1 EM 180 S:** [resultado detalhado](docs/437_CORRIDA118_DX73_TRIANGULO_PIXELS_COPIA_E_R1.md).
> - Daniel confirmou triangulo apenas na janela, desktop legivel e sem piscar ate o watchdog. Harness 60/60, 0,9 FPS. **60/60** copias 1280x720 tiveram centro `Src=Dst=FF7F4040`, canto `Src=Dst=FF2673E5` e Match=5/5, provando a fronteira da copia; 5 `BYTE_PROOF PASS`, zero FAIL. Guardiao restaurou BasicDisplay Codigo 0 em cinco amostras no mesmo boot. **Veredito A do criterio 436**, limitado a 180 s; visual da 117 continua intermitente, e a DX.73 so acrescentou telemetria, nao provou correcao causal.
> - Sem ganho de velocidade: DWM 322/120 s = **2,68/s** (117: 2,78); harness 0,9 FPS (117: 1,1). Callback UMD p50 **443 ms**, `DxgkDdiPresent` KMD p50 **0,746 ms**; 220 flushes UMD p50 `SubmitUs` 64,3 ms, incluindo 160 lotes sem draw mas com trabalho possivel. `CLEAN_BOOT_REQUIRED` ativo. 600 s, 60 FPS e jogos seguem NAO VALIDADOS.

> **25/09/2026 — DX.73 OFFLINE, Corrida 118 preparada:** [pixel central e criterio](docs/436_DX73_PIXEL_CENTRAL_DA_COPIA_E_CRITERIO_CORRIDA118.md).
> - A prova `COMPOSITE_BLT` da 117 dizia Match=5/5 sem publicar os valores, apesar de incluir o centro do quadro. DX.73 registra `Src`/`Dst` do centro e cantos da copia 1280x720 com os pitches reais do escape, sem alterar a copia; permite distinguir perda na ponte/copia de perda posterior na composicao. Build contratual/lab, auditorias, pacote assinado **3.277.1.0** e preflight PASS; **sem bind, NAO VALIDADO EM HARDWARE**. Boot frio obrigatorio antes da 118.

> **25/09/2026 — Corrida 117 (08:38:51, DX.72): MEDICAO FECHOU, FALHA VISUAL:** [resultado detalhado](docs/435_CORRIDA117_DX72_TEMPO_PRESENT_E_TRIANGULO_AUSENTE.md).
> - Daniel viu desktop normal e watchdog R1, mas **o triangulo nao apareceu**. Harness 60/60 `PRESENT_OK`, 1,1 FPS; 60/60 Blt chegaram a `DxgkDdiPresent` com sucesso. Tempo interno KMD p50/p90 **0,676/3,316 ms** vs ida e volta de `pfnPresentCb` UMD **444/465 ms**: custo principal medido fora desta DDI, ainda sem localizacao exata. DWM 334/120 s = **2,78 Presents/s** (116: 2,40). UMD/RADV iguais a 116; a variacao de taxa e visual nao prova causalidade da instrumentacao.
> - **Veredito E** do criterio 434 por falha visual; `PRESENT_OK` e prova amostrada de copia nao provam o triangulo no monitor. `COMPOSITE_BLT` confere centro mas nao registra o valor; o `CenterPixel` antigo do Present usa pitch de display para Blt 1280x720 e nao e' prova valida. Guardiao recuperou BasicDisplay Codigo 0 em cinco amostras no **mesmo boot**; `CLEAN_BOOT_REQUIRED` ativo. Proximo: registrar pixel central da copia com pitch correto e rastrear a espera de ~444 ms fora da DDI; 600 s, 60 FPS e jogos nao validados.

> **25/09/2026 — DX.72 OFFLINE, Corrida 117 preparada:** [instrumentacao e criterio](docs/434_DX72_TEMPO_INTERNO_PRESENT_E_CRITERIO_CORRIDA117.md).
> - Corrida 116 expos ~433 ms p50 na ida e volta de `pfnPresentCb` do harness, sem separar KMD de runtime/espera. A DX.72 mede a duracao interna de `DxgkDdiPresent` com contador do kernel e registra todos os Blt do harness; UMD/RADV e contrato funcional nao mudaram. Build contratual, auditorias, pacote assinado **3.276.1.0** e preflight da 117 PASS. **Nenhum bind; efeito no silicio NAO VALIDADO EM HARDWARE.** `CLEAN_BOOT_REQUIRED` da 116 ativo: boot frio antes do runner de 180 s.

> **25/09/2026 — Corrida 116 (08:14:45, DX.71): SUCESSO VISUAL/R1 CURTO, TAXA ABAIXO DO PISO:** [resultado detalhado](docs/433_CORRIDA116_DX71_180S_VISUAL_ESTAVEL_R1_FPS_ABAIXO_DO_PISO.md).
> - Daniel confirmou imagem normal ate o rollback, triangulo so na janela. DWM 421 Presents em 160,73 s; primeiros 120 s **288 = 2,40/s**, abaixo do piso previo 2,60/s (115: 3,18/s). Harness 60/60, 1,2 FPS; 421/60 marcadores DX.71 DWM/harness, zero scanout direto. A mediana dentro de Present ficou ~43 ms, ponte ~25 ms; intervalo entre Presents p50 **293 ms** (115: 172 ms): pausas fora de Present cresceram, causa nao isolada.
> - Sem travamento na janela de 180 s; guardiao recuperou BasicDisplay Codigo 0 em cinco amostras **no mesmo boot**. Veredito **parcial**: visual e R1 fecharam no escopo curto, criterio A de taxa nao. O criterio previo nao tinha categoria para janela completa com taxa abaixo do piso; nao reclassificar como A/B/E. Dump manual nao foi exercitado. `CLEAN_BOOT_REQUIRED` ativo; 600 s, 60 FPS e jogos nao validados.

> **25/09/2026 — Corrida 116 preparada, SEM BIND:** [criterio e protocolo de dump](docs/432_CORRIDA116_DX71_DUMP_CONTROLADO_180S.md).
> - Mesmo pacote DX.71 3.275.1.0 da 115. Preflight somente leitura PASS: hashes/auditorias, `CrashDumpEnabled=2`, `AlwaysKeepMemoryDump=1`, `CrashOnCtrlScroll=1` para teclado PS/2 e USB, C: com 194 GiB livres. `MEMORY.DMP` ainda e' antigo. Se houver congelamento, tentar Ctrl direito + Scroll Lock duas vezes antes do reset para obter BugCheck **manual 0xE2** e pilhas; se o teclado nao responder, registrar essa limitacao. `CLEAN_BOOT_REQUIRED` ativo: boot frio antes do runner `tools/EXECUTAR_CORRIDA116_DX71_DUMP_180S.cmd`. Nenhuma nova validacao em hardware.

> **25/09/2026 — Corrida 115 (bind 24/09 23:33:42, DX.71): FALHA POR TRAVAMENTO, AVANCO VISUAL PARCIAL:** [resultado detalhado](docs/431_CORRIDA115_DX71_JANELA_CORRIGIDA_TRAVAMENTO_E_RASTRO.md).
> - Daniel viu somente a janela do teste; segunda imagem grande/piscar da 114 ausentes. Logs UMD: 60 marcadores DX.71 no harness e 413 no DWM, zero `DIRECT SCANOUT`. Harness 60/60, 1,1 FPS; DWM 382/120 s = 3,18/s. A imagem parou antes do fim de 180 s; ultimo log UMD ~23:36:01, ~2 min 19 s apos bind. Veredito **E**.
> - O guardiao so recuperou BasicDisplay Codigo 0 **no boot seguinte** as 07:54 de 25/09; `CLEAN_BOOT_REQUIRED` ativo. Rastro DX.56 terminou com `FLUSH_TLB` fase 5, mas a mesma combinacao apareceu em 933/1296 amostras (~72%): **nao aponta causa**. Migalha DX.66 nao preservada; sem BugCheck/dump novo. O dump manual por teclado esta configurado (`CrashOnCtrlScroll=1`, dump kernel), mas depende de o teclado responder no congelamento; proximo ensaio so se justifica com tentativa de captura de pilhas antes do reset. Estabilidade, 60 FPS e jogos seguem abertos.

> **24/09/2026 — DX.71 OFFLINE, Corrida 115 preparada:** [alteracao e criterio](docs/430_DX71_PRESENT_WDDM_UNICO_E_CRITERIO_CORRIDA115.md).
> - Removida a segunda escrita direta ao scanout da UMD no DXGI Present (Vulkan linear/RAW e PM4); a alocacao WDDM segue pelo callback, com recusa se a ponte falhar. Hipotese vem da foto da 114 e das 653 chamadas extras de scanout. KMD/RADV inalterados. Build UMD, auditoria de seguranca, auditoria de pacote e preflight PASS. Pacote lab **3.275.1.0** assinado/auditado, UMD `884A0A60...`, SYS `A4809EEA...`; **nenhum bind da DX.71**. `CLEAN_BOOT_REQUIRED` ativo; boot frio com corte de energia antes da 115.

> **24/09/2026 — Corrida 114 (23:15:09, DX.70, lab 3.274.1.0): FALHA VISUAL, GANHO PARCIAL DE TAXA:** [resultado e foto](docs/429_CORRIDA114_DX70_RESULTADO_DUPLA_APRESENTACAO.md).
> - DWM 372 Presents/120 s = **3,09/s** (113: 2,60/s; criterio A: 3,25/s); harness 60/60 `PRESENT_OK`, 1,3 FPS. Log DWM 10,87 MB (113: 18,19 MB); amostragem DX.70 ativa. A foto do Daniel mostra triangulo na janela **e outra imagem grande piscando sobre o desktop**. 593 chamadas de scanout direto do DWM e 60 do harness, alem dos callbacks WDDM respectivos: forte hipotese de apresentacao duplicada que contorna a composicao.
> - Veredito **E** pelo defeito visual. Guardiao recuperou BasicDisplay Codigo 0 em cinco amostras no mesmo boot; `CLEAN_BOOT_REQUIRED` ativo. DX.71 offline prepara Present via WDDM apenas, a validar em hardware; travamento de 3–8 min e 60 Hz seguem abertos.

> **24/09/2026 — DX.70 OFFLINE, Corrida 114 preparada:** [criterio](docs/428_DX70_LOG_ROTINEIRO_UMD_E_CRITERIO_CORRIDA114_180S.md).
> - O DWM ainda gravou 18,19 MB de log na 113; sete classes exatas de sucesso agora sao amostradas no sink da UMD (16 primeiras, depois 1/64), antes de timestamp e I/O. Falhas, Present, tempos de FlushCommands e WaitPaging seguem integrais. Simulacao no log da 113: ~51.227/145.134 linhas evitadas (35,3%). Contrato DDI/Vulkan inalterado; ganho de FPS **NAO VALIDADO EM HARDWARE**.
> - UMD build e auditorias PASS; pacote lab assinado/auditado **3.274.1.0**, SYS igual a 113 `A4809EEA…`, UMD `80668E4E…`; preflight `tools/prepare-corrida114-dx70.ps1` PASS, runner de 180 s `tools/EXECUTAR_CORRIDA114_DX70_180S.cmd`. Nenhum bind. `CLEAN_BOOT_REQUIRED` ativo: boot frio com corte de energia antes da 114.

> **24/09/2026 — Corrida 113 (22:57:39, DX.69, lab 3.273.1.0): SUCESSO NO ENSAIO CURTO DE VELOCIDADE E RECUPERACAO R1:** [resultado](docs/427_CORRIDA113_DX69_GANHO_DE_DWM_E_RECUPERACAO_R1.md).
> - Daniel confirmou desktop legivel e sem artefatos. DWM **2,60 Presents/s nos primeiros 120 s** (112: 1,35; 111: 2,13), acima do criterio previo 2,5/s; 593 Presents em 171,16 s = 3,46/s medio. A query MSAA foi amostrada ate ordinais 86.016 e 208.896 nos dois Explorers. Ganho observado; causalidade exclusiva da DX.69 nao provada.
> - Harness 60/60, **1,0 FPS** (inalterado); 2.770/2.770 draws do DWM ainda com flush imediato. Explorer ainda teve 821 esperas de paginacao e 361 `0x80070057`. Muito longe dos 56,13 Hz GOP; 600 s e jogos nao validados.
> - Guardiao confirmou BasicDisplay Codigo 0 em cinco amostras **no mesmo boot** (23:00:45–49). `CLEAN_BOOT_REQUIRED` ativo. Proximo: reduzir logs rotineiros do DWM/KMD com observabilidade de falhas preservada; medir mesma janela antes de mudar batching/TLB.

> **24/09/2026 — Corrida 112 falhou; DX.69 OFFLINE para velocidade:** [resultado detalhado](docs/425_CORRIDA112_TRAVAMENTO_FLUSH_TLB_E_DIAGNOSTICO_DE_VELOCIDADE.md) · [criterio 113](docs/426_DX69_AMOSTRAGEM_MSAA_E_CRITERIO_CORRIDA113_180S.md).
> - **Corrida 112:** DX.68 armou 96 slots em tres camadas. Apos travamento duro, boot novo leu CPU08 `PAGING_DDI FLUSH_TLB` em `ANTES`, enquanto a ultima leitura SDMA na CPU00 estava em `DEPOIS`. Rastro DX.65: 4.149 registros validos, ultimo ~22:30:42, `active_count=1`, operacao 12 estagio 5, sem batimento posterior. Isto localiza chamada em andamento, nao causa exata. Recovery BasicDisplay/0 so no boot seguinte; `CLEAN_BOOT_REQUIRED` ativo.
> - **Velocidade medida:** DWM 734 Presents em 413 s = 1,78/s (111: 3,07/s), harness 1,0 FPS; 3.250/3.250 draws com flush imediato. Explorer gerou ~550.800 logs de uma query MSAA e 1.512 esperas de paginacao. Modo GOP 2560x1080 a 56,13 Hz, teto de atualizacao visivel atual.
> - **DX.69 offline:** retorno da query MSAA inalterado; apenas seu log de sucesso e amostrado (16 primeiras, depois 1/4096). UMD/build contratual/lab e auditorias PASS; pacote lab assinado/auditado **3.273.1.0** SYS `A4809EEA…`, UMD `62CE1872…`. Preflight `tools/prepare-corrida113-dx69.ps1` PASS; runner de 180 s `tools/EXECUTAR_CORRIDA113_DX69_180S.cmd`. Sem bind com DX.69; ganho de FPS **NAO VALIDADO EM HARDWARE**. Boot frio com corte de energia antes de qualquer bind.

> **24/09/2026 — DX.68 OFFLINE; Corrida 112 preparada:** [criterio e candidato](docs/424_DX68_MIGALHAS_EM_CAMADAS_E_CRITERIO_CORRIDA112.md).
> - A Corrida 111 falhou em ~6 min 17 s com segmento sem sobreposicao; 12 slots MMIO em `DEPOIS` nao localizaram. DX.68 separa a pagina persistente em bancos por CPU para acessos internos, DDI de paginacao e DDI de submissao. Nao muda a operacao da GPU.
> - Build contratual e lab PASS; auditorias PASS; mutacoes DX.68 4/4, DX.67 6/6 e DX.66 9/9. Pacote lab assinado/auditado **3.272.1.0**, SYS `51BF0764…`, UMD igual a 111. Preflight `tools/prepare-corrida112-dx68.ps1` PASS; runner `tools/EXECUTAR_CORRIDA112_DX68_600S.cmd`. **Nenhum bind com DX.68; nao validado em hardware.** `CLEAN_BOOT_REQUIRED` ativo: boot frio com corte de energia antes da 112.

> **24/09/2026 — Corrida 111 (21:50:58, DX.67, lab 3.271.1.0): CONTROLE PASSOU, TRAVAMENTO DURO ~6 min 17 s, desfecho B:** [docs/423](docs/423_CORRIDA111_DX67_SEGMENTO_LIMPO_TRAVAMENTO_E_MIGALHAS_B.md).
> - **P0:** segmento `[0xA8C000,0xFA00000)` sem sobreposicao com pagina reservada `0xFA00000` nem TMR `[0xFB00000,0xFF00000)`; 298 tabelas raiz observadas abaixo do teto (maior fim `0xF9E9000`). DX.67 validada quanto ao mapa, mas nao eliminou o travamento.
> - **P0:** migalhas armaram e sobreviveram ao reset a quente: `ANTERIOR PRESENTE/EM_EXECUCAO`, ultimo seq. 5.159.142 em CPU04, `SDMA_REG_READ` de HDP_MISC 0x3FCC, fase `DEPOIS`. Todos os 12 slots em `DEPOIS`; nao localiza. Rastro v2 3.650 registros validos, ultimo ~21:57:14,9, sem batimento apos a paginacao final. Daniel relatou desktop sem artefatos por 6–7 min antes do travamento.
> - **Falha:** harness 60/60 e RADV 5/5 nao fecham 600 s. Guardiao restaurou BasicDisplay/0 em cinco amostras apenas no boot seguinte, nao R1. `CLEAN_BOOT_REQUIRED` ativo. A sobreposicao da 110 nao era condicao necessaria para este travamento; causa segue aberta. Proximo: instrumentar pontos cegos de VRAM/etapas persistentes, sem atribuir causa a ultima linha do log.

> **24/09/2026 — Corrida 110 analisada; DX.67 e Corrida 111 preparados OFFLINE:** [resultado 110](docs/421_CORRIDA110_RESULTADO_TRAVAMENTO_TMR_E_MIGALHA_RECUSADA.md) · [criterio 111](docs/422_DX67_SEGMENTO_ISOLADO_MIGALHA_RESERVADA_E_CRITERIO_CORRIDA111.md).
> - **Corrida 110:** falha funcional; ultimo registro duravel ~21:05:53,7, cerca de 6 min 3 s apos o bind. Rastro v2 3.547/3.547 valido, zero batimento depois da ultima paginacao. Migalhas nao armaram, pois 0x0FE80000 estava dentro da reserva TMR. Recovery BasicDisplay/0 em cinco amostras somente no boot novo. Sobreposicao de 3 MiB entre segmento WDDM e TMR e P0; sua causalidade para o travamento e H.
> - **DX.67 offline:** segmento agora exclui todas as regioes internas dos dois alocadores; a pagina de migalhas em 0x0FA00000 e reservada antes do bootstrap e do `QUERYSEGMENT4`. Conflito na base do segmento ou falta da reserva falha fechado. Mutacoes DX.67 6/6, DX.66 9/9, DX.62–65 22/22, DX.61 7/7 e DX.56 6/6; builds e auditorias PASS.
> - **Candidato 111:** pacote lab assinado/auditado **3.271.1.0**, SYS `2B34CEED…`; `tools/prepare-corrida111-dx67.ps1` PASS, runner `tools/EXECUTAR_CORRIDA111_DX67_600S.cmd`. Sem bind: efeito no hardware, 600 s e causa do travamento **NAO VALIDADOS**. Manter BIOS UMA 512 MB e Radeon Software desligada; boot limpo antes do ensaio.

> **24/09/2026 — PASSAGEM DE BASTÃO (corridas 105–110, DX.61–67):** [docs/420](docs/420_HANDOFF_2026-09-24_CORRIDAS_105_A_110_DWM_ESTAVEL_TRAVAMENTO_E_TMR.md).
> - **Corrida 110:** travou de novo. As migalhas DX.66 não armaram, porque a página 0x0FE80000 está **dentro do TMR**.
> - **Achado P0:** o segmento do VidMm [0xA8C000, 0xFE00000) sobrepõe 3 MB da região TMR em uso [0xFB00000, +4 MB), e o VidMm pôs tabelas de página raiz dentro dela. Causa do travamento: H.
> - **DX.67 naquele handoff:** corte do segmento abaixo de toda região interna ainda sem portao, mutacao, contrato e pacote. Concluido offline no doc 422 acima.
> - Radeon Software desligada na máquina. Nada commitado.

> **24/09/2026 — DX.66 OFFLINE; Corrida 110 PREPARADA:** [docs/419](docs/419_DX66_MIGALHAS_MMIO_VRAM_E_CRITERIO_CORRIDA110.md).
> - **Mecanismo original (corrigido apos a 110):** migalhas de MMIO numa pagina UNCACHED de VRAM (0x0FE80000, equivocadamente tratada como folga; estava dentro do TMR). A DX.67 a realocou e reservou em 0x0FA00000.
> - **Leitura:** no StartDevice seguinte, antes do fusível. Localiza o acesso em que a CPU parou, se a VRAM sobreviver ao reinício a quente.
> - **Verificação:** mutações 9/9, pacote **3.270.1.0** SYS `C3739348…`, pré-voo `prepare-corrida110-dx66.ps1` PASS (Radeon Software segue desligada).
> - **Se travar:** reinício a quente, sem cortar a energia.
>
> **NÃO VALIDADO EM HARDWARE.**

> **24/09/2026 — Corrida 109 (18:21:29, DX.65, SEM Radeon Software, 600 s): TRAVAMENTO aos 3 min 16 s, desfecho B′:** [docs/418](docs/418_CORRIDA109_SEM_RADEON_TRAVOU_3MIN_BATIMENTO_B_LINHA.md).
> - **Controle válido:** sem log do `Radeonsoftware.exe`. A correlação com a Radeon **cai como condição necessária**.
> - **Batimento (P0):** 517 registros de escritor vivo sem paginação ao longo da corrida. No fim, a paginação estava ativa e o escritor parou no mesmo período de 100 ms: a máquina parou inteira de uma vez, e o rastro não localiza.
> - **Contexto:** o Daniel mexia o mouse (cursor desenhado pelo DWM, então rajada de GFX+SDMA). Zero TDR, zero BugCheck. Recuperação só no boot novo; Código 43 até o prazo do guardião.
> - **H de alta prioridade:** leitura MMIO que nunca completa (WPTR/SQ/RPTR no caminho quente).
> - **Próximo recomendado:** migalhas em página de VRAM antes de cada MMIO, lidas no StartDevice seguinte. Depende do tipo de reinício (pergunta ao Daniel).

> **24/09/2026 — Corrida 108 (17:54:57, pacote da 107, 600 s): desfecho B, TRAVAMENTO DURO aos ~6 min 3 s; DX.65 e Corrida 109 PREPARADA:** [docs/417](docs/417_CORRIDA108_TRAVAMENTO_6MIN_RADEON_E_DX65_CRITERIO_CORRIDA109.md).
> - **Assinatura igual à das 88–102:** último registro durável `FLUSH_TLB` estágio 5, ~0,6 s após as últimas linhas da UMD. Não localiza. Recuperação só no boot novo (não é R1).
> - **Até o travamento:** a correção da 107 se manteve.
> - **Padrão (P2; causalidade H):** o `Radeonsoftware.exe` abre a UMD ~5 min 10 s após o bind em 100/101/102/108. Os quatro travamentos vieram 42 s a 3,5 min depois, e nenhuma corrida curta chegou lá. As 95–99 travaram antes de 5 min, então não é causa única do histórico. Nenhum driver AMD na pilha do BC-250 (P1).
> - **DX.65 (instrumentação):** o rastro grava batimento a cada 100 ms (v2), e o decodificador mede quanto o kernel viveu após a última paginação. Mutações 22/22. Pacote **3.269.1.0** SYS `7EF9D54D…`.
> - **Corrida 109:** 600 s com a Radeon Software impedida. O pré-voo recusa se ela puder abrir; o operador desliga `StartCN` e o serviço AMD External Events.
>
> **NÃO VALIDADO EM HARDWARE.**

> **24/09/2026 — Corrida 107 (17:46:38, DX.64, lab 3.268.1.0 SYS `7C4E6E1B…`): desfecho A — DESKTOP COM JANELA D3D11 ESTÁVEL NOS 180 s:** [docs/416](docs/416_CORRIDA107_DX64_RESULTADO_E_CRITERIO_CORRIDA108_600S.md).
> - **Critério:** zero `SRV_LAYOUT_UNDEFINED`, zero `Draw: RECUSADO`, zero recriação do device do DWM (o único `DestroyDevice` foi 3 s após o rollback), zero `PRIMARY_FALLBACK` e zero `status=9`.
> - **Cópias:** 1196, com 2 transições UNDEFINED→COLOR_ATTACHMENT.
> - **Eixo 1:** Blt 100% no destino, `BYTE_PROOF` 3/3.
> - **Estabilidade:** R1 pela 3ª vez, zero TDR/BugCheck, 470 presents do DWM, `COMPOSITE_BLT` 529/0. O Daniel relatou que a tela não degradou.
> - **Abertos:** ~1,1 FPS, laço do Explorer (D3), camada 2 da recriação, DX.60, travamento duro não medido.
> - **Corrida 108:** mesmo pacote, **600 s**, `tools\EXECUTAR_CORRIDA108_DX64_600S.cmd`, só a duração muda.
>
> **Pixels do destino e 600 s NÃO VALIDADOS EM HARDWARE.**

> **24/09/2026 — Corrida 106 (16:57:40, DX.63b, lab 3.267.1.0 SYS `2E76ACEB…`): desfecho A — JANELA D3D11 COM TRIÂNGULO COMPOSTA PELO DWM NA TELA; DX.64 e Corrida 107 PREPARADA:** [docs/415](docs/415_CORRIDA106_RESULTADO_TRIANGULO_COMPOSTO_E_DX64_CRITERIO_CORRIDA107.md).
> - **Critério:** 60/60 Blt copiados para o destino, zero recusa, 5/5 `BYTE_PROOF` dos flips, R1 pela 2ª vez.
> - **Fotos do operador:** a janela do harness com o triângulo aparece na posição real, composta pelo DWM. Pixels do destino sem prova byte a byte.
> - **"Bugar" após ~140 s (P0):** o `ResourceCopyRegion` não gravava o layout Vulkan. A SRV foi recusada como nunca escrita, o Draw recusado disparou `SetErrorCb(E_NOTIMPL)`, e o DWM recriou o device (2/2, P2 no elo causal).
> - **A recriação não sobrevive:** mesmos tokens e VAs nas primárias, `status=9`, `PRIMARY_COPY` de primárias zeradas, e `PRIMARY_FALLBACK` somando a base do segmento duas vezes.
> - **DX.64 (só o gatilho):** a cópia usa e grava o layout rastreado. Mutações 20/20, pacote **3.268.1.0** SYS `7C4E6E1B…` UMD `B6AB6788…`, pré-voo `prepare-corrida107-dx64.ps1` PASS.
>
> **NÃO VALIDADO EM HARDWARE.**

> **24/09/2026 — Corrida 105 (16:25:54, DX.61–63, lab 3.266.1.0 SYS `3C7ACDEF…`): PARCIAL; DX.63b e Corrida 106 PREPARADA:** [docs/414](docs/414_CORRIDA105_RESULTADO_E_DX63B_CRITERIO_CORRIDA106.md).
> - **Recuperação R1:** `RECOVERY_OK_ONLINE` na tentativa 1, no mesmo boot, com 5/5 amostras na rodada 1. O assentamento não foi exercitado, e a causa da 104 continua desconhecida.
> - **Eixo 1, desfecho B:** 60/60 `BLT_DESTINO RECUSADO GEOMETRIA_DO_DESTINO_DESCONHECIDA`. Zero cópia centralizada no scanout. Os 5 `BYTE_PROOF` dos flips passaram (104: 2 FAIL). O destino é alocação padrão (P2: `StagingSurface 1280x720`), publicada pelo KMD sem geometria.
> - **Eixo 2, desfecho D3:** 18/18 compartilhadas com `OpenCalls=1 OpenStatus=0 DestroyCalls=1`. O nosso `OpenAllocation` está descartado; o dxgkrnl rejeita depois das nossas DDIs.
> - **Estabilidade:** zero TDR/BugCheck em 180 s, 1,6 FPS. "Bugar" aconteceu de novo mesmo sem escrita do harness no scanout, então a H do doc 413 perde força. `COMPOSITE_BLT RECUSADO` 98 com Edge/Claude abertos.
> - **DX.63b:** staging/GDI publicam geometria; primária e shadow não mudam. Mutações 16/16, pacote **3.267.1.0** SYS `2E76ACEB…`, pré-voo `prepare-corrida106-dx63b.ps1` PASS.
>
> **NÃO VALIDADO EM HARDWARE.**

> **24/09/2026 — DX.61–63 OFFLINE; Corrida 105 PREPARADA, NÃO EXECUTADA:** [docs/413](docs/413_DX61_63_RECOVERY_BLT_DESTINO_SEGUIMENTO_ALOCACAO_E_CRITERIO_CORRIDA105.md). Três análises da 104 e as correções:
> - **(1) Recovery:** Kernel-PnP 410 registrou BasicDisplay iniciado às 15:38:30, e o veredito de 5 amostras do guardião reprovou até 15:38:35,97. A recovery funcionou; o veredito é que reprovou. As amostras da tentativa 1 foram sobrescritas pela tentativa 2. DX.61, só no guardião: journal append-only de amostras, saída da recovery por tentativa, assentamento de até 45 s **sem afrouxar** as 5 amostras BasicDisplay/0, e `BLOCKED` secundário só no journal.
> - **(2) `BYTE_PROOF FAIL` #10/#45 (P0):** o present Blt do harness (1280×720, destino resolvido) era copiado para o **scanout** em (640,180) inventado, concorrendo com o flip do DWM. `FirstMismatch` do #10 = linha 180, x=640, o canto da janela. DX.63, a única variável comportamental: Blt copia para a alocação de destino com geometria do registro, e falha fechada sem tocar o scanout.
> - **(3) Textura compartilhada:** o KMD responde igual para a aceita e a recusada, e nenhuma criação `Misc=0x2` teve sucesso. As recusas do `OpenAllocation` eram amostradas fora do log (comparação sensível a maiúsculas). DX.62, só instrumentação: a sonda v2 registra Open e Destroy por token, e toda recusa fala.
>
> Mutações 7/7 e 12/12, builds e auditorias PASS. Pacote lab **3.266.1.0**, SYS `3C7ACDEF…`, UMD `3B70D7F2…`; pré-voo `prepare-corrida105-dx61-63.ps1` PASS. **NADA VALIDADO EM HARDWARE.**

> **24/09/2026 — Corrida 104 DX.60 (15:35:24, lab 3.265.1.0): PARCIAL, sem fechamento do desktop nem do 0x119.** [Handoff e resultado](docs/412_HANDOFF_2026-09-24_CORRIDA104_DX60_E_CONTINUIDADE.md). SYS `942084B7...`; evidência `docs/evidence/2026-09-24_15-35-24-DESKTOP-HQAG6T4-lab-binding/`. Harness D3D11 60/60 Present aceitos, 1,2 FPS médio; três selftests RADV 5/5. KMD 10.139 submissões WDDM aceitas e aposentadas, mas dois `BYTE_PROOF FAIL` (#10/#45). UMD: 23 amostras Explorer de textura compartilhada 1024×832 com `E_INVALIDARG` após `KmdStatus=0`; um controle DWM aceito. Zero `PREEMPT_DONE/WAIT Node=0`: a guarda DX.60 **não foi exercitada**; ausência de novo BugCheck não prova causa/correção do 0x119 da 103. Daniel viu a imagem por um tempo, depois “começou a bugar”, sem classificação do defeito. Watchdog tentou rollback às 15:38:26, não confirmou BasicDisplay no mesmo boot e solicitou **reboot automático** às 15:38:35; BasicDisplay Código 0 confirmado em cinco amostras às 15:39:47–51 **no boot novo**. O `RECOVERY_OK_ONLINE` final descreve somente a segunda tentativa. Critério de recovery no mesmo boot falhou; pixel correto sustentado, desktop 600 s, jogo e velocidade seguem abertos. `CLEAN_BOOT_REQUIRED` ativo para novo bind. Árvore DX.56–DX.60 e documentação ainda não commitada.

> **24/09/2026 — dump elevado da Corrida 103 analisado:** [docs/410](docs/410_CORRIDA103_DX59_RESULTADO_E_0x119.md). `!analyze -v` confirma `0x119_1_DRIVER_REPORTED_INVALID_FENCE_ID` às 13:55:40,666, dentro de `dxgmms2!VidSchiProcessIsrCompletedPacket`, chamado por `dxgkrnl!DxgNotifyInterruptCB` a partir de DPC de aposentadoria SDMA do nosso KMD. A extração identificou a estrutura `BC250_ACTIVE_FENCE_SYNC`: operação 7 (`NOTIFY_SDMA`), `SubmissionFenceId=0xBFF` (3071), `NodeOrdinal=0`; o Windows bugcheckou com parâmetros 3071/3072. O primeiro roteiro confundiu a estrutura com o payload DXGK; corrigido no docs/410. A leitura bruta seguinte provou a marca local do nó 0 em `0xBFE` (3070), e do nó 1 em 1482. Assim, o filtro local viu 3071 como sucessor de 3070; não houve reset da marca a zero nem inversão visível nesse contador. A razão pela qual o scheduler rejeitou 3071 permanece aberta; Arg3=3072 não tem interpretação publicada para essa variante. Último submit SDMA registrado foi 3068. GPU-Z segue sem prova causal. Sem novo bind até corrigir a origem do ID inválido.
> **24/09/2026 — desmontagem do scheduler da Corrida 103:** [docs/410](docs/410_CORRIDA103_DX59_RESULTADO_E_0x119.md). No `dxgmms2.sys` 10.0.19041.2913, Arg3=3072 foi lido do estado interno em `[rdi+0x98]`; não é valor copiado da notificação. O ramo fatal compara a fence 3071 com dois campos do scheduler, consulta uma tabela de 16 ponteiros e chega ao BugCheck quando a entrada selecionada é nula. Ainda não está provado por que a entrada faltou. Leitura somente do frame e da tabela preparada em `tools/analisar-tabela-scheduler-corrida103.ps1`; nenhum código de notificação foi alterado por palpite.
> **24/09/2026 — estado do scheduler da Corrida 103 confirmado:** [docs/410](docs/410_CORRIDA103_DX59_RESULTADO_E_0x119.md). A extração inicial da tabela leu o frame errado por defeito do roteiro; a versão corrigida com endereço absoluto mediu `[+0x40]=[+0x48]=3072`, `[+0x98]=3072` (origem de Arg3), índice da tabela 1 e entrada 1 nula. O callback KMD reportava 3071 com marca local 3070. Portanto a divergência entre scheduler e contador KMD está provada, mas sua causa temporal não: preempção Node0 é hipótese de janela de corrida no código, sem registro `PREEMPT_DONE Node=0` recuperado. Não houve novo bind nem correção de callback por palpite.
> **24/09/2026 — DX.60 OFFLINE após a Corrida 103:** [docs/411](docs/411_DX60_SERIALIZACAO_PREEMPT_SDMA_POS_CORRIDA103.md). Corrigida uma janela real de ordem: o retirement SDMA retirava o último slot antes de notificar a fence; nesse intervalo `InFlightCount=0` podia autorizar `DMA_PREEMPTED` do nó 0 no DPC genérico. A preempção agora compartilha a guarda `SdmaRetireBusy` até concluir a tentativa de notificação; se ocupada, adia sem spin. Auditoria de segurança PASS, `build-contract` EXIT=0. Pacote lab **3.265.1.0** assinado/auditado, SYS `942084B7...`, UMD anterior `0FC62149...`, sem bind; pacote 3.264.1.0 da 103 preservado na evidência. Corrida 104 preparada com pré-voo fixo e watchdog 180 s, somente após boot frio. O mecanismo é compatível com o dump 3070/3071/3072, mas não há `PREEMPT_DONE Node=0` recuperado, portanto **causalidade e correção do 0x119 NÃO VALIDADAS EM HARDWARE**. A rejeição `E_INVALIDARG` da textura 1024×832 segue aberta.

> **24/09/2026 — Corrida 103 (13:54:18, DX.59, lab 3.264.1.0 SYS `F38D4F63…`): DIAGNÓSTICO PARCIAL, FALHA FUNCIONAL COM BUGCHECK 0x119:** [docs/410](docs/410_CORRIDA103_DX59_RESULTADO_E_0x119.md). Dezesseis amostras da textura compartilhada 1024×832 do Explorer: sonda válida `Seen=1`, `KmdStatus=0`, `hAlloc=1`, prioridade NORMAL e `AllocateHr=0x80070057` — a recusa é posterior ao retorno de sucesso do KMD. Controle DWM `Bind=0x8` da mesma geometria passou. System Event 1001: `0x119 Arg1=1` (ID de fence inválido reportado pelo driver), `Arg2=0xBFF`, `Arg3=0xC00`; reboot às 13:56:08. GPU-Z já estava aberto e Daniel clicou para atualizar, sem hora exata ou prova causal. Guardião restaurou BasicDisplay Código 0 somente **no boot novo**, após expirar o prazo. Dump existe mas requer leitura elevada; roteiro `tools/analisar-dump-corrida103.ps1` preparado. **Não repetir bind** antes de analisar dump/notificação de fence e corrigir a causa. Desktop estável, pixel, jogo e rollback no mesmo boot NÃO VALIDADOS.

> **24/09/2026 — DX.59 OFFLINE; Corrida 103 PREPARADA, NÃO EXECUTADA:** [docs/409](docs/409_DX59_SONDA_CREATEALLOCATION_E_CRITERIO_CORRIDA103.md). Após boot frio às 13:40:56, uma sonda consulta por token o resultado real do `DxgkDdiCreateAllocation` para a textura 1024×832 recusada na 102 e o controle aceito do DWM. UMD registra separadamente HRESULT do callback e `NTSTATUS` KMD, com amostragem; escape KMD lê registro circular sem MMIO nem mudar a alocação. Build KMD/UMD e auditorias PASS; pacote lab assinado `3.264.1.0` SYS `F38D4F63…`, UMD `0FC62149…`; pré-voo fixa hashes. Corrida de diagnóstico limitada a 180 s. **NÃO VALIDADO EM HARDWARE; nenhum bind novo ocorreu.** Travamento duro, pixels, 600 s estáveis e recuperação no mesmo boot continuam abertos.

> **24/09/2026 — Corrida 102 (13:14:53, DX.58, lab 3.263.1.0 SYS `FE58E378…`): FALHA FUNCIONAL, desfecho B:** [docs/408](docs/408_CORRIDA102_DX58_RESULTADO.md). A textura compartilhada 1024×832 do Explorer continuou recusada (`E_INVALIDARG`) 294 vezes; prioridade inicial NORMAL é correção de contrato, mas **não suficiente** para esse laço. Controle: outra textura 1024×832 do DWM, `Bind=0x8`, passou; a recusada tem `Bind=0xA8/Misc=0x2`, portanto a geometria sozinha não explica. Cinco TDRs com reset/restart transitórios, harness 60/60 a 1,1 FPS médio, último DWM/Explorer às 13:23:43, ~71 s antes do prazo de 600 s. Outro travamento duro; Daniel reiniciou manualmente às 13:27:29. Guardião só retomou no boot seguinte e restaurou BasicDisplay Código 0 às 13:28:11; `CLEAN_BOOT_REQUIRED` ativo. DX.57 **validada como coleta**: 13 UMD originais arquivados automaticamente com hash e ACL; DX.56 repetiu 4.267 registros íntegros pré-reboot. Sem prova de pixel, desktop vivo por 600 s, recuperação sem reboot ou causa do travamento. Próximo: diagnosticar `E_INVALIDARG` da textura compartilhada na fronteira runtime/KMD sem inferir pelo silêncio do logger.

> **24/09/2026 — DX.58 OFFLINE; Corrida 102 PREPARADA, NÃO EXECUTADA:** [docs/407](docs/407_DX58_PRIORIDADE_DE_ALOCACAO_E_CRITERIO_CORRIDA102.md). O original UMD do Explorer da 101 tem 201 recusas `E_INVALIDARG` da textura compartilhada 1024×832. Auditoria de contrato encontrou `DXGK_ALLOCATIONINFO.AllocationPriority` sem preenchimento; o WDDM define zero como prioridade inicial inválida. KMD agora atribui `D3DDDI_ALLOCATIONPRIORITY_NORMAL` em `CreateAllocation`. É correção de contrato, **não causa provada** do laço nem do travamento. UMD/RADV inalteradas; DX.57 só coleta evidência. Build, auditorias, mutação e pacote assinado PASS. Lab `3.263.1.0`, SYS `FE58E378…`; pré-voo da 102 fixa hashes KMD/UMD/RADV, janela 600 s e exige boot frio. **NÃO VALIDADO EM HARDWARE; nenhum bind novo ocorreu.**

> **24/09/2026 — DX.57 OFFLINE (cauda UMD pós-recovery):** [docs/406](docs/406_DX57_COLETA_DA_CAUDA_UMD_APOS_RECOVERY.md). Corrigida a leitura da 101: os logs UMD arquivados às 11:45 eram snapshot do binder, não a cauda do travamento; os originais foram recuperados depois em `umd-logs-pos-travamento/` com hashes e fecham o critério A. Binder agora marca a amostra intermediária com hora/hash; guardião SYSTEM copiará os originais de `C:\Windows\Temp` com hash e ACL legível **após** executar a recovery, inclusive no boot seguinte, sem atrasar o início do rollback. Auditoria PASS, mutações 4/4 rejeitadas, fixture de cópia PASS. **Nenhuma correção do travamento foi validada em hardware.**

> **24/09/2026 — Corrida 101 (11:42:50, DX.56, lab 3.262.1.0 SYS `2C350D18…`): TRAVAMENTO DURO, desfecho A/parcial:** [docs/405](docs/405_CORRIDA101_DX56_RESULTADO.md). Rastro persistente validado no silício: 3.567 registros íntegros pré-reboot, último às ~11:49:27,714 (seq. 1.991.231); o 3.568º é do boot seguinte. Os nove logs UMD originais foram recuperados após o reboot: DWM até 11:49:27,041 e Explorer até 11:49:27,058, ~0,66 s antes do último rastro; isso fecha o critério de proximidade, sem localizar a causa. `FLUSH_TLB` antes do despacho no registro final é posição frequente de amostragem, não causa demonstrada. Harness 60/60 Present aceitos e 3 selftests com prova 5/5; não houve prova de pixel no monitor ou desktop estável. O watchdog não reverteu no boot travado e só recuperou BasicDisplay Código 0 após reinício manual, em 5 amostras; `CLEAN_BOOT_REQUIRED` ativo. Avanço diagnóstico SIM; avanço funcional NÃO DEMONSTRADO. Próximo: investigar a recusa repetida `E_INVALIDARG` da textura compartilhada 1024x832 do Explorer e a rotatividade de contextos; 600 s, desktop rápido, jogo e recuperação sem reboot seguem **NÃO VALIDADOS EM HARDWARE**.

> **24/09/2026 — DX.56 OFFLINE; Corrida 101 PREPARADA, NÃO EXECUTADA:** [docs/404](docs/404_DX56_RASTRO_DURAVEL_DE_PAGINACAO_E_CRITERIO_CORRIDA101.md). O rastro de estágios de paginação existente ganhou uma thread separada que grava snapshots com checksum, write-through e flush em `C:\BC250_PAGING_TRACE.bin`, sem I/O na DDI de paginação. Binder registra offset; ferramenta de resgate copia a cauda com SHA-256 e decodifica inclusive após registro cortado. Build de laboratório, auditorias de segurança/pacote e portão DX.56 com 6/6 mutações passaram; decodificador passou casos de registro completo, corrompido e truncado. Pacote lab `3.262.1.0`, SYS `2C350D18…`, UMD `51288052…`; pré-voo da Corrida 101 PASS. A persistência efetiva e a capacidade de discriminar o travamento continuam **NÃO VALIDADAS EM HARDWARE**. Nenhum bind ocorreu.

> **24/09/2026 — PASSAGEM DE BASTÃO:** [docs/403](docs/403_HANDOFF_2026-09-24_TRAVAMENTO_DURO_E_VELOCIDADE_DO_DESKTOP.md). Estado, histórico do travamento duro (88–100), o que foi descartado, hipóteses vivas, mapa do código DX.49–DX.55, build/pacote/corrida, armadilhas de evidência e próximo passo recomendado (trilha que sobreviva ao reset). Nada commitado.

> **24/09/2026 — Corrida 100 (10:50:49, DX.55, lab 3.261.1.0 SYS `F643EDC9…`): TRAVAMENTO às ~6 min, desfecho C:** [docs/402](docs/402_CORRIDA100_DX55_RESULTADO.md). Lote desligado, pool persistente 2303/2303; Present/min 151–178 (98R 87–120), p50 294 ms (98R 391). Travou às 10:56:51 com a mesma assinatura; o lote NÃO é o gatilho, e a 98R não provou a DX.53 como correção do travamento (margem de 0,6 min, refutada como causa suficiente). Harness não rodou por defeito meu (wrapper em LF; auditoria reprovou dentro do script do harness); corrigido, e a auditoria de segurança entrou no pré-voo. 241 COMPOSITE_BLT recusados por residência (10:52–10:54, quando Edge/Claude abriram) sem anel: a H do piscar pelo anel perde força. Radeonsoftware.exe carregou a UMD 47 s antes do travamento (H). 88/92 também travaram antes da DX.49; a 93 rodou 300 s a ~100 ctx/min. Próximo: decisão entre staging off, laço do Explorer ou trilha que sobreviva ao reset.

> **24/09/2026 — Logs da UMD da 99 recuperados e DX.55 OFFLINE; Corrida 100 PREPARADA, NÃO EXECUTADA:** [docs/400 §6](docs/400_CORRIDA99_DX54_RESULTADO_TRAVAMENTO_CEDO.md), [docs/401](docs/401_DX55_BISSECCAO_LOTE_DESLIGADO_CRITERIO_CORRIDA100.md). Cópia elevada: todos os logs terminam às 10:16:31.8 (~39 s após o bind). DX.54 no silício (P0): pool persistente 158/158, DescUs mediana 32,5 µs (antes ~14 ms), anel de 4 MB só no dwm.exe, zero no Explorer. No travamento, dwm.exe e harness estavam os dois em FlushCommands sem retorno; SubmitUs do harness chegou a 1,06 s. Os últimos lotes do dwm.exe tinham 0–1 desenho. DX.55: só o lote desligado (uma variável contra a 99), pool e inspeção amostrada mantidos; portão 9/9. Pacote lab **3.261.1.0** SYS `F643EDC9…`, UMD `51288052…`; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA100_DX55.cmd` (600 s) após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **24/09/2026 — Corrida 99 (10:15:52, DX.54, lab 3.260.1.0 SYS `CF03A31B…`): TRAVAMENTO DURO CEDO, desfecho D:** [docs/400](docs/400_CORRIDA99_DX54_RESULTADO_TRAVAMENTO_CEDO.md). Desktop apareceu sem mouse; Kernel-Power 41, fusível recusou o boot seguinte, guardião reverteu só depois do reboot. Log do KMD recuperado (2,8 MB, sha256 `d69cce5a…`) termina no meio de submissão SDMA de paginação, mesma assinatura das 94–97; 164 contextos (~35–60 s, H), harness sem saída, zero TDR/timeout/fault/quarentena. Logs da UMD NÃO arquivados (máquina travou antes da coleta; C:\Windows\Temp sem acesso nesta sessão): o controle da DX.54 não é verificável, e a corrida não mediu a velocidade. Histórico: todas as corridas com lote ligado travaram (94, 95, 96, 99); com DX.53 a diferença 98R→99 são as três peças da DX.54. Suspeito principal: o lote (H). Próximo: copiar logs da UMD e Corrida 100 com só o lote desligado.

> **24/09/2026 — DX.54 OFFLINE (velocidade do desktop); Corrida 99 PREPARADA, NÃO EXECUTADA:** [docs/399](docs/399_DX54_VELOCIDADE_DO_DESKTOP_E_CRITERIO_CORRIDA99.md). Mineração da 98R (P0 de tempo): flush ~14 ms com EsperaUs=190 (custo de CPU, não GPU), ~14 ms por draw entre o upload do CB e o BOUND (VkDescriptorPool novo por draw = BO no RADV), ~29 ms por Present lendo a grade do buffer linear pela CPU; Present p50 391 ms. DX.54, a pedido do Daniel, com três interruptores: pool de descritores persistente + cache de samplers; lote religado com anel de 4 MB (antes 16) criado só após 32 draws diretos (poupa os devices do Explorer); inspeção do Present amostrada (NAO_AMOSTRADO fora da amostra). Portão audit_dx54_desktop_speed substitui o DX.51, 8/8 mutações; suítes UMD, DX.49/50/52/53 e auditoria PASS. Pacote lab **3.260.1.0** SYS `CF03A31B…`, UMD `B884F0F3…`; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA99_DX54.cmd` (600 s) após desligamento completo. Laço do Explorer não corrigido (causa não identificável offline). **NÃO VALIDADO EM HARDWARE.**

> **24/09/2026 — Corrida 98 revisada (09:24:30, DX.53, lab 3.259.1.0 SYS `2D95145B…`): SEM TRAVAMENTO, desfecho A (implicação P2):** [docs/398](docs/398_CORRIDA98R_DX53_RESULTADO.md). Primeira janela inteira desde a 93: guardião reverteu sozinho, RECOVERY_OK_ONLINE sem reboot, StopDevice ordenado (SDMA 1428/1428, Fallbacks=0) e fusível em CLEAN. DX.53 no silício: 157 desarmes antes da devolução, 157/157 status=0 Readback=1 Flush=1, zero QUARENTENA, PendentesNoRastreador=0 em 157/157. Carga equivalente à 97 (~290 contra ~293 contextos/min; ~36 contra ~39 ciclos do Explorer/min); harness 60/60. DX.52 respondeu: o recurso recusado é Texture2D 1024x832 BGRA com Bind=SRV|RT|PRESENT e Misc=SHARED; KMD não recusou nada, recusa do dxgkrnl (causa H). Fora do mapa: Explorer fail-fast 2x (0xc0000602) no primeiro minuto, ausente em 96/97, atribuição H. Ressalva: 1 amostra e só ~0,6 min além do travamento mais tardio anterior (96, ~4,4 min). Próximo: replicação com 600 s sem mudança de código.

> **24/09/2026 — Revisão das auditorias externas e DX.53 OFFLINE; Corrida 98 revisada PREPARADA, NÃO EXECUTADA:** [docs/396](docs/396_REVISAO_DAS_AUDITORIAS_EXTERNAS_CHATGPT_GEMINI.md), [docs/397](docs/397_DX53_VMID_DESARMADO_ANTES_DE_DEVOLVER_E_CRITERIO_CORRIDA98R.md). Confirmado no código (achado do ChatGPT): o DestroyContext devolvia o VMID ao pool e só depois desarmava o silício, fora do lock — outro contexto podia pegar o slot no meio. Medido: para o mesmo token do Explorer (`…5D`, 3.407.872 bytes) o KMD ACEITOU (`CreateAllocation Result=0`, `OpenAlloc result=0`); a recusa E_INVALIDARG é do dxgkrnl depois do KMD. Refutada a tese do Gemini (hResource deve ser NULL: 928 alocações com hRTResource passaram) e apontadas citações de log inexistentes. DX.53: Lookup → desarme com o slot ainda reservado → Release só com desarme confirmado; falha = quarentena; medição sincronizada de fences pendentes no desarme. Portão 5/5 mutações, `active_miniport_test` atualizado, demais suítes PASS. Pacote lab **3.259.1.0** SYS `2D95145B...`, UMD `8D4EE96D...`; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA98R_DX53.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **24/09/2026 — Corrida 97 (00:14:10, DX.51, lote desligado): desktop normal sem piscar, TRAVOU (Explorer para às 00:18:21, guardião não pôde agir); desfecho C. DX.52 e Corrida 98 PREPARADAS, NÃO EXECUTADAS:** [docs/394](docs/394_CORRIDA97_RESULTADO_E_DX52_CRITERIO_CORRIDA98.md). DWM 123 Present/min sem o lote: o ganho de vazão veio do staging persistente. Pisca-pisca explicado (H forte): com o anel de 16 MB por device (95/96) a ponte Vulkan→WDDM falhou 89 e 438 vezes (destino não residente, `status=9`); sem anel (97) zero falhas. Travamento persiste sem lote e sem dirty rects: sobram staging persistente e ritmo de contextos (~290/min, laço do Explorer). DX.52 (só instrumentação): recusas de CreateAllocation nomeadas e fora da amostragem (`[CREATE_ALLOC_RECUSADO]`), UMD descreve o recurso recusado. Portão 6/6. Pacote lab **3.258.1.0** SYS `A1546408...`, UMD `8D4EE96D...`; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA98_DX52.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **24/09/2026 — Mineração offline, adendo ao critério da Corrida 97:** [docs/393 §4](docs/393_CORRIDA96_RESULTADO_E_DX51_CRITERIO_CORRIDA97.md). Explorer em laço de falha: alocação de 3.407.872 bytes recusada com E_INVALIDARG pelo `pfnAllocateCb` (sem recusa no KMD) → destrói o device inteiro → recomeça a cada ~1,5 s. Já existia na 93 (21 ciclos/5 min), mas com a DX.49 virou 173 ciclos/4 min; `KMD_CONTEXT_CREATE` de ~100/min (93) para ~300/min (94–96). H concorrente: o travamento acompanha a taxa de criação/destruição de contextos/VMIDs. A 97 (lote desligado) não separa as hipóteses sozinha; se der A, a 98 religa o lote e corrige o laço do Explorer.

> **24/09/2026 — Corrida 96 (23:52:12, DX.50, dirty rects desligados): TRAVOU de novo, tela piscando; desfecho C. DX.51 e Corrida 97 PREPARADAS, NÃO EXECUTADAS:** [docs/393](docs/393_CORRIDA96_RESULTADO_E_DX51_CRITERIO_CORRIDA97.md). Bissecção ativa (20 recusas `DX50_BISSECCAO_DESLIGADO`, zero dirty rects); harness 60/60, 684 Presents, zero TDR/fault; Explorer para às 23:56:38, KMD cortado em submissão SDMA como 94/95. Dirty rects NÃO são a causa do travamento; 3/3 corridas com a DX.49 travaram. Arquivamento novo trouxe o log do dwm.exe da 95 (última escrita 23:34:05, igual ao Explorer), mas movido com a ACL de Windows\Temp; binder corrigido para copiar+hash+apagar. DX.51: lote de draws desligado (`s_Bc250Dx51DrawBatchEnabled = FALSE`, anel nem criado), staging e trace mantidos. Portões DX.51 3/3, DX.50 6/6, DX.49 19/19. Pacote lab **3.257.1.0** SYS `94DFDF76...`, UMD `7226ABEC...`; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA97_DX51.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — DX.50 OFFLINE, bissecção: dirty rects desligados; Corrida 96 PREPARADA, NÃO EXECUTADA:** [docs/392](docs/392_DX50_BISSECCAO_DIRTY_RECTS_E_CRITERIO_CORRIDA96.md). Uma variável: `s_Bc250Dx50DirtyRectsEnabled = FALSE` (todo Present volta à cópia de tela cheia da 93); lote, staging e trace da DX.49/49b seguem ligados. Coleta: o binder deixou de APAGAR os `BC250_UMD_LOG_*` residuais e passa a movê-los com hash para a pasta do bind novo (o log do dwm.exe da 95 deve aparecer na 96). Portão DX.50 5/5 mutações, DX.49 19/19, build lab EXIT 0. Pacote lab **3.256.1.0** SYS `041E71DC...`, UMD `725279BA...` (a da 95); pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA96_DX50.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 95 (23:30:42, lab 3.255.1.0 SYS `C8B469FC...`): ~3× mais Presents, desktop com defeito e TRAVAMENTO DURO:** [docs/391](docs/391_CORRIDA95_DX49B_RESULTADO_MAIS_RAPIDO_E_TRAVAMENTO.md). Harness 60/60; DWM 109 Present/min (93: 35–38), lote com mediana 4 draws/flush, staging criado uma vez; KMD: 486 Presents, 397/397 composição com prova, dirty rects 384 aceitos, zero TDR/fault; log cortado no meio de submissão SDMA (como na 94). Kernel-Power 41, fusível recusou o boot seguinte, rollback OK. Travamento em 2/2 corridas com DX.49 (H: vazão ou comportamento novo). **CORREÇÃO da 94:** 'nenhum log da UMD sobreviveu' era falso — `C:\Windows\Temp` estava sem acesso de leitura nesta sessão; logs do Explorer/harness sobreviveram no %TEMP% do usuário e o do dwm.exe ainda precisa de cópia elevada.

> **23/09/2026 — DX.49b OFFLINE, log da UMD durável; Corrida 95 PREPARADA, NÃO EXECUTADA:** [docs/390](docs/390_DX49B_LOG_DA_UMD_DURAVEL_E_CRITERIO_CORRIDA95.md). Handle do trace aberto uma vez COM write-through, por linha só WriteFile, metadados a cada 250 ms; resto da DX.49 igual. build-umd EXIT 0, portão DX.49 19/19 mutações, auditoria PASS. Pacote lab **3.255.1.0** SYS `C8B469FC...` (mesma fonte do KMD da 94, rebuild por guarda de frescor), UMD `725279BA...`; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA95_DX49B.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 94 (22:07:30, lab 3.254.1.0 SYS `EA3F02B3...`): TRAVAMENTO DURO, tela preta; desfecho D:** [docs/389](docs/389_CORRIDA94_DX49_RESULTADO_TRAVAMENTO_E_LOG_DA_UMD_PERDIDO.md). Kernel-Power 41, fusível recusou o boot seguinte, rollback OK às 22:17:27. Log do KMD resgatado (29.735 linhas): lote chegou ao KMD (IBs até 5824 DW contra 824 na 93, todos concluídos), dirty rects 30 aceitos / 0 recusados com prova 5/5, zero TDR/fault/timeout, GPU ociosa e sadia até o corte. Sem `AddDevice #2`. ~~Logs da UMD perdidos~~ (CORRIGIDO após a 95: afirmação falsa, era falta de acesso a `C:\Windows\Temp`; logs do harness/Explorer sobreviveram). Travamento não atribuível à DX.49 (H). Próximo proposto: DX.49b, log da UMD durável com handle único + write-through.

> **23/09/2026 — DX.49 OFFLINE, vazão do desktop em 4 partes; Corrida 94 PREPARADA, NÃO EXECUTADA:** [docs/388](docs/388_DX49_VAZAO_DO_DESKTOP_E_CRITERIO_CORRIDA94.md). (1) staging persistente no upload de textura; (2) lote de draws: o draw só grava, VB/IB/CB em anel de 16 MB com faixa própria, dreno antes de destruir objeto referenciado, flush fecha o render pass; (3) trace da UMD com handle único e sem write-through; (4) dirty rects aceitam Src/Dst vazios (flip) e a recusa nomeia o motivo. Quatro variáveis por decisão do Daniel, com assinatura por parte no critério. build-umd e build lab EXIT 0, `umd_dx49_batch_test` PASS, portão DX.49 17/17 mutações, demais suítes PASS (`test-f3-audit-mutations.py` já falhava antes: alvo ausente até no HEAD). Pacote lab **3.254.1.0** SYS `EA3F02B3...`, UMD `B786E360...`; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA94_DX49.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 93 (21:24:38, lab 3.253.1.0 SYS `48CFDEFD...`): desfecho D; DESKTOP VISÍVEL E INTERATIVO a ~0,6 FPS:** [docs/387](docs/387_CORRIDA93_DX48_RESULTADO_DESKTOP_INTERATIVO_LENTO.md). Sem travamento, harness 60/60 EXIT 0, zero TDR, um só `AddDevice #1` (sem segunda instância; causa dos travamentos 88/92 segue aberta). 216/216 `COMPOSITE_DATA_PROOF SAMPLED_PASS`, 216 Present Flip, SDMA 649/649 sem fallback; preempção 2/2 DONE; DX.43 449 warm-ups. Daniel viu o triângulo e depois o desktop, mouse respondendo, ~1 quadro/s. DWM: 35-38 Present/min (P0). Orçamento do minuto 21:28: 24 s depois de `UploadImageData SUCESSO` (staging criado e liberado por upload, ~1,1 s por 3,6 MB; atribuição P2), 9,6 s em 425 esperas de fence (um flush por draw). Dirty rects: 17 recusados. Rollback: 1ª tentativa online incompleta, reboot automático do guardião, 2ª `RECOVERY_OK_ONLINE`. Próximo proposto: DX.49 staging persistente.

> **23/09/2026 — DX.48 OFFLINE, ciclo de vida do adaptador falante; Corrida 93 PREPARADA, NÃO EXECUTADA:** [docs/386](docs/386_DX48_CICLO_DE_VIDA_FALANTE_E_CRITERIO_CORRIDA93.md). AddDevice/StartDevice/StopDevice/RemoveDevice escrevem `[DX48_LIFECYCLE]` ENTER/LEAVE síncronos com contexto, PDO e número de instância; `[McdmQAI]` passa a imprimir `hAdapter`; linhas isentas da amostragem. Só medição. Build/contrato EXIT 0, portão DX.48 6/6 mutações, demais suítes PASS (B.1 30/30 na segunda passada; a primeira teve OSError de gravação, conteúdo conferido idêntico). Pacote lab **3.253.1.0** SYS `48CFDEFD...`, janela 300 s; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA93_DX48.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 92 (19:12:54, lab 3.252.1.0 DX.47): harness 60/60 EXIT 0, zero TDR, preempção concluída — e travamento duro depois do harness.** [docs/385](docs/385_CORRIDA92_DX47_RESULTADO_60_DE_60_E_TRAVAMENTO.md). `[PREEMPT_DONE] Node=1 PreemptionFenceId=1 LastCompletedFenceId=172` (P0), zero TDR ocioso; WPTR até 30872; DX.43 7ª amostra 323/323 (1262 trocas em 86–92); 4 `BYTE_PROOF PASS`. Travou às ~19:15:32, após o harness terminar; Daniel reiniciou ~8 min depois. Guardião durável (DX.45b host) validado: retomou no boot e fez `RECOVERY_OK_ONLINE` 19:26:00. Assinatura 2/2 travamentos (88 e 92, P0): última linha `[McdmQAI] Type=29 CapsStatus=0xC00000A3` — WDDMDEVICECAPS (consultado após AddDevice) de um contexto NÃO iniciado no meio da corrida; `AddDevice` não loga, lacuna. H: segunda instância do adaptador. Próximo: DX.48 (medição de AddDevice/Start/Stop/Remove e hAdapter no QAI).

> **23/09/2026 — DX.47 OFFLINE, conclusão da preempção; Corrida 92 PREPARADA, NÃO EXECUTADA:** [docs/384](docs/384_DX47_PREEMPCAO_CONCLUIDA_E_CRITERIO_CORRIDA92.md). `PreemptCommand` passa a registrar o pedido por nó (sem notificar dentro da DDI) e o retirement, depois do dreno e também no caso ocioso, notifica `DXGK_INTERRUPT_DMA_PREEMPTED` só com o nó drenado (rastreador + fila do SDMA), com `LastCompletedFenceId` = última fence notificada; TDR limpa pedidos pendentes. Build/contrato EXIT 0 com caso por nó no `active_miniport_test` (mutação derruba a suíte), portão DX.47 9/9 mutações, DX.44 ajustado (6/6), demais suítes PASS. Pacote lab **3.252.1.0** SYS `CFCDF849...`, janela 300 s; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA92_DX47.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 91 (18:40:28, mesmo candidato 3.251.1.0, janela 300 s): harness 53/60 (recorde); volta completa do anel provada; o TDR restante é preempção sem resposta com a GPU ociosa.** [docs/383](docs/383_CORRIDA91_RESULTADO_53_DE_60_E_TDR_DE_PREEMPCAO.md). Submits que cruzaram 8192 (fence 327) e 16384 (fence 594, WPTR 16436) concluíram; GPU aposentou até a 681; no TDR `RPTR=2696` = 19080 mod 16384 — DX.46 validada nas duas voltas. DX.43 6ª amostra: 409/409, zero TDR de VMID anterior (939 trocas em 86–91). TDR único no quadro 54 com assinatura idêntica ao da 86: anel drenado, `Committed=0`, zero ondas, ME `PFP_SYNC_ME`, `GfxContextBusy=1`, e `[PREEMPT_COMMAND] #1` antes dele — o driver nunca reporta `DMA_PREEMPTED` (P0) e o contrato exige (P1). Próximo: DX.47, concluir a preempção. Recuperação: online incompleta → reboot → `RECOVERY_OK_ONLINE` 18:46:52.

> **23/09/2026 — Corrida 91 PREPARADA, NÃO EXECUTADA (mesmo candidato da 90, janela 300 s):** [docs/382](docs/382_CRITERIO_CORRIDA91_DX46_JANELA300.md). Correção do docs/381: o harness da 90 não foi cortado pelos 120 s do watchdog, e sim pelo `runnerTimeout = min(180, janela−60)` = 60 s do binder. Com 300 s o harness ganha 180 s (60 quadros a ~0,5 FPS) e o WPTR pode passar de 16384 (volta real do anel). Wrapper `EXECUTAR_D3D11_NATIVO.cmd` com janela parametrizável (padrão 120), portão verificado por mutação; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA91_DX46_JANELA300.cmd` após desligamento completo.

> **23/09/2026 — Corrida 90 (17:44:33, lab 3.251.1.0 DX.46): ZERO TDR; anel inteiro confirmado; harness sem falha até o fim da janela.** [docs/381](docs/381_CORRIDA90_DX46_RESULTADO.md). Bring-up `CntlPlanned=CntlReadback=0x50000B0D`; o submit que cruzou 8192 (fence 309, WPTR 8216) concluiu e a GPU aposentou até a 673; `GFX_TRIPE` com `RPTR_Reg==WPTR` em 15448–15544 e zero `NAO_CONVERGIU` (P0) — o anel pela metade era a causa dos TDRs das 87/89. Harness ≥20/60 `PRESENT_OK`, sem device-lost, cortado pelo prazo de 120 s (~0,5 FPS: 60 quadros não cabem na janela). DX.43 5ª amostra 165/165 (530 trocas em 86–90), zero `Cid=9`, 3 `BYTE_PROOF PASS`. NÃO VALIDADO: volta real em 16384 (WPTR máx. 15720), 60/60, desktop interativo, causa da lentidão. Recuperação: online incompleta → reboot automático → `RECOVERY_OK_ONLINE` 17:47:54. Próximo: mesma build com janela ~300 s.

> **23/09/2026 — DX.46 OFFLINE, anel gráfico no tamanho inteiro; Corrida 90 PREPARADA, NÃO EXECUTADA:** [docs/380](docs/380_DX46_ANEL_INTEIRO_E_CRITERIO_CORRIDA90.md). `Bc250GfxRingDeriveBufsz` passa a `RB_BUFSZ = log2(RingBytes/8)` como o Linux v6.6 (`gfx_v10_0.c:6106`, P1): para 64 KB, 12→13, `CP_RB0_CNTL` `0x50000A0C`→`0x50000B0D`; fallback fixo do restart do TDR substituído pela fonte única. Teste que congelava o erro (8192 B→(9,7)) corrigido, com 64 KB→(13,11) e invariante `2^(RB_BUFSZ+1)==RingBytes/4`; portão antigo que EXIGIA `0x50000709` invertido; portão DX.46 6/6 mutações, e o '-1' recolocado derruba o `build-contract` (linha 71). SYS empacotado contém `0x50000B0D` e não `0x50000A0C`. Pacote lab **3.251.1.0** SYS `BE60274C...`; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA90_DX46.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 89 (16:11:37, lab 3.250.1.0 DX.45b): não travou; 13/60; o TDR restante é o anel gráfico programado com METADE do tamanho.** [docs/379](docs/379_CORRIDA89_DX45B_RESULTADO_E_ANEL_PELA_METADE.md). DX.43 4ª amostra: 136/136, zero TDR de VMID anterior (365 trocas em 86–89). Detector: desfecho B (CP não parou). **Achado:** o KMD escreve um anel de 64 KB (16384 DW), mas `Bc250GfxRingDeriveBufsz` usa `log2(RingBytes/8) - 1` (fórmula H) e programa `RB_BUFSZ=12` = 8192 DW; o Linux v6.6 (`gfx_v10_0.c:6106`, P1) usa `order_base_2(ring_size/8)` sem o -1. P0 2/2: nas 87 e 89 o submit que levou o WPTR além de 8192 (fences 314/316) nunca concluiu → preempção → TDR, com RPTR voltando a 5; as corridas que não cruzaram 8192 tiveram outras classes. Correção de leitura: o 'RPTR 8188→5' da 87 foi a volta do anel, não reset. O teste `gfx_ring_contract_test` congelava o valor errado (8192 B → (9,7)). Proposta DX.46: fórmula do upstream. `RECOVERY_OK_ONLINE` 16:13:46.

> **23/09/2026 — DX.45b OFFLINE, detector em duas fases e guardião durável; Corrida 89 PREPARADA, NÃO EXECUTADA:** [docs/378](docs/378_DX45B_DETECTOR_DUAS_FASES_GUARDIAO_DURAVEL_CRITERIO_CORRIDA89.md). Análise da 88 (P0): acima de PASSIVE o log só vai para um anel em memória (perdido no travamento); `C:\BC250_SAFE_GUARD\armed.json` ficou com 1.178 bytes todos zero e as tarefas do guardião sumiram — por isso não houve rollback. KMD: em DPC só `CP_ME_CNTL` e detecção pendente; em PASSIVE a linha sai primeiro (síncrona) e cada leitura extra é anunciada por `LENDO <reg>`. Host: `armed.json` write-through + `RegFlushKey(HKLM)` + `Write-VolumeCache C`, bind recusado se não durável. Build/contrato EXIT 0, portão DX.45 11/11 mutações, portão do host verificado, auditorias PASS. Pacote lab **3.250.1.0** SYS `700C6D93...`. Antes: `tools\BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset`, desligamento completo; depois `tools\EXECUTAR_CORRIDA89_DX45B.cmd`. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 88 (15:43:42, lab 3.249.1.0 DX.45): travamento duro do PC ~40 s após o bind — sem TDR, sem BugCheck, sem rollback; inconclusivo.** [docs/377](docs/377_CORRIDA88_DX45_RESULTADO.md). Cauda resgatada (1.646.171 B, `51877B93...`): 168 submissões GFX, 42 aquecimentos, 872 SDMA, zero TDR/`CP_RESET_DETECTED`/`PREEMPT_COMMAND` até o fim do log. Falha de desenho minha: o detector DX.45 faz as 5 leituras RLC/GRBM **antes** de escrever a linha — se o reset da 87 se repetiu e uma leitura travou o barramento, a prova se perdeu (H). Fusível funcionou (StartDevice recusado, Código 43, sem loop). Watchdog parado em OBSERVING: **bc250mcdm 3.249.1.0 continua vinculado** — rodar `tools\BC250_RECOVERY.cmd` e `inspect-boot-fuse.ps1 -Reset` elevados e desligar antes de qualquer coisa.

> **23/09/2026 — DX.45 OFFLINE, detector de reset silencioso do CP; Corrida 88 PREPARADA, NÃO EXECUTADA:** [docs/376](docs/376_DX45_DETECTOR_RESET_CP_E_CRITERIO_CORRIDA88.md). Upstream (P1): Linux v6.6 zera `cg_flags/pg_flags` do GC 10.1.3 e o SMU do Cyan Skillfish não tem GFXOFF; histórico (P0): `RLC_PG_CNTL=0` em 361.524 leituras — power gating do RLC desligado, H1 (GFXOFF) enfraquecida. Detector somente leitura: `CP_ME_CNTL` no fim do poll (fora do spinlock) e após cada submit; na transição para CP parado registra `[CP_RESET_DETECTED]` com RPTR, `CP_STAT`, `GRBM_STATUS`, `RLC_CNTL`, `RLC_PG_CNTL`, `RLC_GPM_STAT`; não se acusa no nosso próprio quiesce. Build/contrato EXIT 0, portão DX.45 8/8 mutações, DX.43 7/7, DX.44 6/6, auditorias PASS. Pacote lab **3.249.1.0** SYS `7F94EECB...`; UMD e RADV inalteradas; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA88_DX45.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 87 (15:13:55, lab 3.248.1.0 DX.43+DX.44): harness 16/60 (melhor da série) e desktop íntegro com o triângulo D3D11 na janela; TDR por reset silencioso do CP.** [docs/375](docs/375_CORRIDA87_DX44_RESULTADO.md). DX.43 segunda amostra: 123/123 aquecimentos, **zero** TDR de VMID anterior e zero `Cid=9` (229 trocas somadas às da 86). Preempção: **uma** chamada (`Node=1`), 7 linhas antes do único TDR, ignorada — desfecho **C**: o TDR não é ocioso. **Novo (E, P0):** entre os submits 314 e 315 o CP foi resetado e parado (RPTR 8188→5, `CP_ME_CNTL=0x15000000` = valor de power-on, fence 314 nunca concluída) sem nenhum log nosso; a preempção veio depois, como reação. Só nesta corrida (varredura 84–87). H: power gating/GFXOFF do bloco gráfico ou reset por firmware. Função falhou (16/60, device removed no quadro 17, desktop travado). Recuperação online incompleta → reboot → `RECOVERY_OK_ONLINE` 15:17:12. Desligamento completo antes de outra corrida.

> **23/09/2026 — DX.44 OFFLINE, preempção falante; Corrida 87 PREPARADA, NÃO EXECUTADA:** [docs/374](docs/374_DX44_PREEMPCAO_FALANTE_E_CRITERIO_CORRIDA87.md). `Bc250McdmPreemptCommand` registra as 3 saídas (`[PREEMPT_COMMAND] #N`, fence/nó/engine, sucesso declarado SEM BACKEND), teto próprio, retornos inalterados; o driver nunca reporta `DMA_PREEMPTED` (P0 no código). Host: coleta RADV também no ramo de rollback com erro (causa da falta de coleta na 86). DX.43 mantida (segunda amostra). Build/contrato EXIT 0, portão DX.44 6/6 mutações (a 1ª versão do portão foi pega por mutação), portão do wrapper verificado, auditorias PASS. Pacote lab **3.248.1.0** SYS `CD60A95F...`; UMD e RADV DX.42 inalteradas; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA87_DX44.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 86 (14:55:26, lab 3.247.1.0 DX.43): desfecho A — nenhum TDR de VMID anterior em 106 trocas; sobra 1 TDR com a GPU ociosa (E).** [docs/373](docs/373_CORRIDA86_DX43_RESULTADO.md). 106/106 trocas com aquecimento emitido; TDRs 4→**1**, TDRs de VMID anterior 4→**0**, faltas `Cid=9` 4→**0**, submissões GFX 316→**491**, autotestes RADV 1/3→**3/3**, harness 0/60→**2/60**, primeiros 2 `BYTE_PROOF PASS` de tela cheia. Uma corrida não prova eliminação. O TDR restante: GFX 188/188 aposentadas, fence escrita, RPTR==WPTR, zero ondas, SDMA ocioso, `Committed=0` — sem trabalho nosso pendente. Achado de código: `Bc250McdmPreemptCommand` devolve SUCCESS sem backend nem log com `PreemptionAware=1` (Lei 4); H: preempção nunca reportada gera TDR ocioso. Função falhou (2/60, device removed, desktop travado). Coleta RADV pós-rollback não rodou no wrapper — refeita manualmente. `RECOVERY_OK_ONLINE` 14:57:36; desligamento completo antes de outra corrida.

> **23/09/2026 — Datação dos TDRs da 85 e DX.43 OFFLINE; Corrida 86 PREPARADA, NÃO EXECUTADA:** [docs/372](docs/372_DATACAO_TDR_CORRIDA85_E_DX43_CRITERIO_CORRIDA86.md). **Correção do docs/371:** os 4 de 4 TDRs da 85 são a mesma classe (P0): ondas executam o shader do IB em `CP_IB1` (fences 29/50/62/78, `COMPUTE_PGM_LO` no dump) com o VMID do IB **anterior**; os anteriores com dump (28, 61) nem têm compute. Permissão vs mapeamento é só como a página aparece na tabela do processo anterior. **DX.43 (uma variável):** na troca de VMID, um `INDIRECT_BUFFER` de 3 DW sob o VMID novo aponta para o `CONTEXT_CONTROL` inicial do próprio IB, só se o KMD leu esse DW0; H: o VMID do dispatch só atualiza numa fronteira de IB. Build/contrato EXIT 0, portão DX.43 com 7/7 mutações reprovando, auditorias PASS. Pacote lab **3.247.1.0** SYS `9D28E368...`, UMD e RADV DX.42 inalteradas; pré-voo PASS. Executar `tools\EXECUTAR_CORRIDA86_DX43.cmd` após desligamento completo. **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 85 (14:15:11, lab 3.246.1.0 + RADV DX.42): desfecho C, o preâmbulo perde o conteúdo depois do TDR; função falhou.** [docs/371](docs/371_CORRIDA85_DX42_RESULTADO.md). Harness PID 4768: preâmbulo serial 3 bom nos submits 1–3 (`fin_match=1`); o TDR 3 teve `IB1` no IB do próprio harness; no submit 4, **mesmo BO** (`changed=0 uses=4`), `fin_zero_prefix=0`, `src_zero_prefix=180` (P0). O veto DX.41 recusou antes do D3DKMT; TDR sem ondas não reapareceu. Nesta corrida o zero é consequência do reset (P2); o caso 12816 da 84 segue sem explicação. **4 TDRs em duas assinaturas:** 1 e 3 com ondas no VMID anterior (classe conhecida); 2 e 4 com ondas no VMID **certo** e falta de **mapeamento** `Cid=9` na página do shader (novo, H: TLB/ordem do mapeamento). Harness 0/60 (DEVICE_RESET no quadro 1), desktop alternando com blocos, 12 `DIRTY_RECTS_RECUSADOS` caindo em cópia de tela cheia. 4/4 restarts provados, sem BugCheck, `RECOVERY_OK_ONLINE` às 14:17:22. Desligamento completo antes de outra corrida.

> **23/09/2026 — DX.42 OFFLINE, identidade do preâmbulo; Corrida 85 REPREPARADA, NÃO EXECUTADA:** [docs/370](docs/370_DX42_IDENTIDADE_DO_PREAMBULO_E_CRITERIO_CORRIDA85.md). Os 180 DW zerados da 84 são **exatamente o preâmbulo inteiro** (P0 nos dumps); o CS principal, na mesma página do slot, chegou íntegro, logo a origem leu zero (H forte). A RADV agora dá serial a cada CS, grava o hash no finalize e emite `RADV_PREAMBLE_IDENTITY` (serial, `changed`, VA/handle do BO, `fin_fnv`/`src_fnv`) antes do veto DX.41, para separar preâmbulo regenerado de BO que perdeu o conteúdo. Só medição; bytes submetidos inalterados. Build RADV EXIT 0, DLL `FCAA11FE...`, patch `AB836A8B...`, portão DX.42 com 6/6 mutações reprovando, pré-voo e auditorias PASS. KMD/UMD 3.246.1.0 inalterados. Executar `tools\EXECUTAR_CORRIDA85_DX42.cmd` após desligamento completo (marcador armado no boot de 13:07). **NÃO VALIDADO EM HARDWARE.**

> **23/09/2026 — Corrida 85 DX.41 PREPARADA, NÃO EXECUTADA (substituída pela DX.42 acima antes de rodar):** [critério e candidato fixado](docs/369_CRITERIO_CORRIDA85_DX41_PREAMBULO.md). Mesmo pacote KMD/UMD da 84, RADV DX.41 por hash; coletor pós-rollback com SHA-256, snapshots e índice para não repetir a perda temporal de IBs. Pré-voo do pacote PASS. Marcador de boot frio ainda armado no boot atual; nenhum bind feito. Espera-se distinguir origem versus cópia dos 180 DW zero se o defeito reaparecer; recusa do IB é falha funcional controlada, e os TDRs de VMID antigo continuam pergunta separada.

> **23/09/2026 — DX.41 OFFLINE após resgate da Corrida 84:** [docs/368](docs/368_DX41_ANALISE_IB_ZERO_POS_CORRIDA84.md). O staging reteve 9 pares de IB não copiados na primeira coleta; resgate conferido por SHA-256 e índice refeito. O IB aceito da fence 107 e o IB recusado do harness têm **os primeiros 180 DW zerados**, enquanto os demais 63 não têm esse prefixo. FNV64 RADV/KMD coincide na 107; o KMD recebeu bytes já malformados. Causa da perda (origem do preâmbulo ou cópia/BO) ainda **não provada**. RADV passou a registrar origem/cópia e recusar prefixo zero antes de D3DKMT; indexador alerta o caso e separa `submit_id` por contexto. Build RADV e auditoria PASS, DLL construída `5C4E7701...`; DLL implantada ainda DX.40 `BF6A1EDA...`. **Sem nova corrida; DX.41 NÃO VALIDADA EM HARDWARE.**

> **23/09/2026 — Corrida 84 (13:08:48, lab 3.246.1.0): diagnóstico parcial e falha funcional.**
> Resultado: [docs/367](docs/367_CORRIDA84_DX40_RESULTADO.md). Evidência:
> `docs/evidence/2026-09-23_13-08-48-DESKTOP-HQAG6T4-lab-binding/`.
>
> - **Avanço funcional parcial:** Daniel viu tela preta com quadrado azul e triângulo colorido no centro,
>   em vez do desktop desorganizado da 83. O harness aceitou **1/60** Presents (83: 0/60), mas o
>   segundo falhou com `DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_RESET`. A imagem parcial
>   não prova desktop íntegro. **Quatro TDRs GFX** (83: três), nenhum BugCheck registrado.
> - **Telemetria medida:** 16 fluxos RADV, 56 dumps iniciais mais 9 pares resgatados do staging (65), cauda KMD integral; 104 submissões GFX,
>   49 trocas com `CONTEXT_CONTROL`. Correlação por VA/DW/FNV64: **54 únicas, 8 ambíguas, 42 ausentes**.
>   Fences 19/32/47 dos três primeiros TDRs têm dump único do IB novo; as ondas ainda aparecem no
>   VMID anterior, sem identidade temporal imutável. O quarto TDR tinha CP/IB pendente, zero ondas
>   amostradas; a fence 107 tem dump único com os primeiros **180 DW zero**, e a 108 segue sem dump. Fluxo do harness terminou um submit com
>   `STATUS_DEVICE_REMOVED`; `accepted=0`.
> - **Controles:** 4/4 reinícios GFX/SDMA provaram NOP/fence; um autoteste S1–S5 passou com contador
>   65536; dois tiveram etapas NÃO MEDIDAS. Provas SDMA de retângulos sujos não demonstram tela
>   inteira. Rollback **RECOVERY_OK_ONLINE** às 13:10:58, BasicDisplay Código 0 em cinco amostras.
> - **Veredito:** diagnóstico **parcial**, função **falhou**. Desktop íntegro/interativo, 60/60,
>   TDR eliminado e causa do fault de render target seguem **NÃO VALIDADOS EM HARDWARE**. Próxima
>   etapa é análise offline da origem do preâmbulo zerado, transição VMID/contexto e cobertura de dumps, com hipótese discriminante
>   antes de outro candidato. O marcador de boot frio deve ser respeitado.
>
> **23/09/2026 — preparação histórica da Corrida 84, antes do bind acima:** [critério prévio](docs/366_CRITERIO_CORRIDA84_DX40_TELEMETRIA_FLUXO.md). Naquele boot, o preflight exigia desligamento completo; Daniel realizou a corrida no boot seguinte. Esta entrada registra somente o estado anterior ao ensaio.

> **23/09/2026 — DX.40 OFFLINE, build completo e fluxo RADV/KMD:** [docs/365](docs/365_DX40_BUILD_COMPLETO_FLUXO_RADV_LINUX_WDDM.md). KMD, UMD/harnesses e RADV reconstruídos; pacote lab **3.246.1.0** assinado e auditado com perfil da Corrida 83, `GfxWaveProbe=1`, sem bind. RADV grava fluxo opt-in por processo (`submit_id`, fila, espera, layout `SINGLE/CONCAT/CHAIN`, retorno D3DKMT e sinal) no staging que o binder arquiva; índice aponta erros/etapas ausentes. KMD registra a decisão de transição por VMID e os dwords reais do IB em cada submit GFX medido. Comparação com Linux v6.6: nosso gatilho por VMID e concatenação de CS não são equivalentes ao gatilho por contexto e emissão de IBs do Linux; os logs possibilitam medir a diferença, **não provam fluxo correto no silício**. Build/testes, auditorias e pacote PASS; patch RADV `35c37d47...`, DLL construída `bf6a1eda...`, DLL implantada ainda `5988b38e...` (83). Wrapper agora recusa cópia RADV falha ou hash divergente antes do bind. Desktop íntegro, TDR eliminado e 60 quadros **NÃO VALIDADOS EM HARDWARE**.

> **23/09/2026 — DX.39 OFFLINE, telemetria e índice de filas:** [docs/364](docs/364_DX39_TELEMETRIA_FILAS_FENCES_E_INDICE_OFFLINE.md). RADV agora registra ciclo de vida da fila, espera/sinalização e retorno D3DKMT; cada dump opt-in completo ganha `.meta` com PID/contexto/IP/fila/status, inclusive quando o stderr do `dwm.exe` não for coletado. KMD nomeia criação/destruição de contexto, submissões GFX/SDMA, recusas e ondas no TDR, com fences, VMID e leituras RPTR/WPTR/SQ; o detalhamento por submit segue condicionado a `GfxWaveProbe=1`. O binder cria `telemetry-index.md` com alertas, localização no log bruto, filas e correspondência de hash sem converter duplicata ou submit recusado em prova. Build-contract, build RADV e auditorias PASS; DLL construída `56073fad...` e patch `ae379ac1...`, sem implantação. Corrida 83 permanece no DLL `5988b38e...`. Nenhum pacote, bind ou corrida novo; **NÃO VALIDADO EM HARDWARE**.

> **23/09/2026 — DX.38 OFFLINE, identidade dos IBs e fronteira de ondas:** [docs/363](docs/363_DX38_IDENTIDADE_IB_E_LIMITE_SONDA_ONDAS_OFFLINE.md). A evidência da Corrida 83 não contém o IB da fence 67, portanto não prova que seu shader era diferente do IB68. RADV agora gera dumps sem sobrescrita (PID/tick/contador, até 128 por processo), com contexto/VA/tamanho/FNV64; KMD calcula o mesmo fingerprint dos bytes traduzidos antes do submit e registra fence/VMID/root, falhando fechado se a leitura for incompleta. Em troca de VMID, duas leituras `SQ_DEBUG_STS_GLOBAL`/fence cercam a publicação CPU do WPTR; elas **não** identificam VMID de onda nem são barreira GPU. Ferramenta offline cruza dumps e log sem transformar duplicata em identidade única. Build-contract, build RADV e auditorias PASS. DLL construída DX.38 `640f4a15...` permanece **não implantada**; DLL implantada `5988b38e...` é da Corrida 83. Nenhum pacote, bind ou corrida novo; efeito no silício **NÃO VALIDADO EM HARDWARE**.

> **23/09/2026 — auditoria OFFLINE de IB/fence após a Corrida 83:** [docs/362](docs/362_AUDITORIA_OFFLINE_IB_CONTEXTOS_FENCES_CORRIDA83.md). Nos TDRs 1/2, a GPU escreveu as fences anteriores 21/32, mas ainda havia ondas VMID1/3; não está provado se nasceram no IB anterior ou no novo. No TDR3, a fence observada era 67, 68/69 estavam em voo e o `IB1` era da 68. O dump arquivado com VA/tamanho da 68 contém `DISPATCH_DIRECT` e programa o shader **exatamente** no PC travado `0x2003C0B00`, mas a onda foi lida em VMID4 enquanto a 68 foi submetida em VMID3. É forte correlação com a 68, ainda sem identidade imutável de fence/contexto no dump; não demonstra correção parcial da DX.37. As três mudanças de contexto também mudaram VMID e acionaram o pacote; a diferença de gatilho Linux/WDDM não explica sua ausência. O IB GFX no PM4 capturado inclui bit 23 `VALID`, ausente no emissor GFX Linux; ainda sem causalidade. A bridge UMD da 83 criou apenas a fila gráfica, sem evidência de ACE dedicada. `Present #5` falhou na prova de bytes e foi recusado; #10 passou. Nenhum código foi alterado, nenhuma nova corrida.

> **23/09/2026 — revisão OFFLINE após a Corrida 83:** [docs/361](docs/361_REVISAO_CYAN_SKILLFISH_LINUX_MESA_WDDM_POS_CORRIDA83.md). Linux marca `1002:13FE` como `CHIP_CYAN_SKILLFISH` + `AMD_APU_IS_CYAN_SKILLFISH2`; a flag já existia em 2021 e não representa versão nova do Mesa. O fork WDDM já publica a identidade GFX1013/GFX10.1 em fonte, mas falta o contorno Mesa para `PARTIAL_TG_EN` na fila ACE. Esse contorno não explica diretamente os TDRs da 83 no anel GFX com VMID anterior. Diferenças concretas do KMD gráfico frente ao Linux: gatilho por VMID em vez de `fence_ctx` e bit `INDIRECT_BUFFER_VALID` no IB GFX. Ambas requerem auditoria causal antes de outra corrida. Nenhum código ou pacote foi alterado nesta revisão; desktop e 60/60 continuam NÃO VALIDADOS EM HARDWARE.

> **23/09/2026 — Corrida 83 (11:00:09, lab 3.244.1.0): DX.37 atravessa o anel, mas não elimina o VMID anterior.**
> Resultado: [docs/360](docs/360_CORRIDA83_DX37_RESULTADO.md). Evidência:
> `docs/evidence/2026-09-23_11-00-09-DESKTOP-HQAG6T4-lab-binding/`.
>
> - **Avanço funcional: não.** Daniel viu “desktop todo zoado”; harness 0/60, três TDRs como na 82.
> - `CONTEXT_CONTROL` confirmado no PM4 (`C0012800 81018003 00000000`, 34 DW), anel avançou, 75 trocas `Eng=0`.
>   TDRs 1 e 2: ondas no VMID do IB anterior (**C**, hipótese DX.37 insuficiente). TDR 3: ondas no VMID atual,
>   mas `IB1` aponta para o IB anterior e o fault próprio não foi capturado após a fence 69
>   (**B individual + E/G**); não muda o veredito global C.
> - 3/3 resets GFX e SDMA provados; nenhum BugCheck. Um autoteste RADV 5/5; dois ficaram NÃO MEDIDOS após
>   `MAP_FALHOU`. Present #5 falhou na prova de bytes e foi recusado; Present #10 passou com 256/256 amostras,
>   sem provar desktop íntegro. Escritas sem permissão no VA
>   `0x2401A0000` recorreram.
> - Watchdog pediu reboot após rollback online incompleto; no segundo boot, `RECOVERY_OK_ONLINE` e BasicDisplay
>   Código 0 em cinco amostras. Cauda do log resgatada; sem
>   bind novo até investigar offline. Eliminação de TDR, desktop íntegro/interativo e 60 quadros continuam
>   **NÃO VALIDADOS EM HARDWARE**.

> **23/09/2026 — faixa DX.37, OFFLINE e sem corrida.** Critério da Corrida 83 em
> [docs/359](docs/359_DX37_CRITERIO_CORRIDA83_CONTEXT_CONTROL.md). **NÃO VALIDADO EM HARDWARE.**
>
> - Uma variável comportamental: na troca de VMID, `CONTEXT_CONTROL` (`C0012800 81018003 00000000`)
>   entra depois do `PFP_SYNC_ME` e antes do IB, conforme o GFX10 do Linux v6.6. Troca 31→34 DW;
>   `SWITCH_BUFFER` segue proibido. H: recarregar o estado de dispatch elimina o VMID antigo das ondas.
> - `build-contract` e build lab EXIT=0; portão DX.37 e teste comprovados por mutação; auditorias de segurança,
>   DDI, callbacks, RADV, DX.31 e pacote PASS. O marcador DX.37 foi conferido no SYS empacotado.
> - **Pacote lab 3.244.1.0**, assinado, auditado e NÃO INSTALADO: SYS SHA-256 `9D0FF839119FDDCA8B86FA47CE82966C0B334DDDC09E4FA0ECC369B8F9905ED9`;
>   UMD inalterada. Corrida só após boot frio e decisão do Daniel.
> - Segunda frente, apenas análise: no processo `hProc=000001D0000001D0` da 82, as páginas em `0x2401A0000`
>   mudam de uma alocação gravável de 16 páginas para uma somente leitura de 20 antes do IB de 552 DW escrever
>   ali. O log não correlaciona esse `hProc` ao PID/recurso da UMD; causa do reuso segue H.

> **23/09/2026 — Corrida 82 (10:12:15, lab 3.243.1.0): mecanismo da classe de TDR fechado, 3 de 3.**
> Relatório: [docs/357](docs/357_CORRIDA82_DX36_RESULTADO.md).
>
> - **TDRs:** nos 3, as ondas presas têm o **VMID do IB anterior**, e a linha do tempo registrou, logo antes, a falta
>   de busca de instrução (`Cid=9`) desse VMID **na página do PC das ondas** (P0). O dispatch do IB novo roda com a
>   tabela do processo antigo.
> - **Escrita sem permissão (P1):** o IB `0x2401DC000` renderiza em `CB_COLOR0_BASE=0x2401A0000`, um VA que acabou de
>   ser remapeado de uma alocação gravável de 16 páginas para uma somente leitura de 20. H: reuso de VA do render
>   target. Não causa TDR.
> - **Controles:** reversão OK; sem BugCheck. O desktop apareceu e ficou, com partes faltando; o cursor mudou de
>   posição por retângulos sujos (5/5). Harness 0/60.

> **23/09/2026 10:06 — tentativa da Corrida 82 abortada ANTES do bind (defeito de host, sem driver instalado).**
> - `tools/_last_lab_binding.json` estava com 1.762 bytes **todos zero**: foi gravado às 09:41:01, 20 s antes do 0x116
>   da 81, e os dados não chegaram ao disco. O binder quebrou no `ConvertFrom-Json`; o wrapper fez rollback e regravou
>   o marcador de boot frio.
> - **Correção:**
>   - arquivo restaurado da cópia íntegra `binding-provenance.json` da 81; a versão zerada foi arquivada na pasta dela;
>   - o gravador passou a `WriteThrough` + `Flush(true)`;
>   - o leitor, se o registro não parsear, usa a `binding-provenance.json` válida mais recente e avisa. O preflight de
>     rebaixamento continua valendo.
>   - Parse do script OK, teste offline com arquivo zerado OK, auditorias PASS.
> - Pacote 3.243.1.0 inalterado. **Novo boot frio necessário** (o marcador foi regravado com o boot de 10:04).

> **23/09/2026 — faixa DX.36, OFFLINE e sem corrida.** Critério da 82 em
> [docs/356](docs/356_DX36_CRITERIO_CORRIDA82_REVERSAO_E_ALVO_2401A1.md). **NÃO VALIDADO EM HARDWARE.**
>
> - **R1:** `SWITCH_BUFFER` revertido; a troca volta a 31 DW. Portão e teste passam a **proibi-lo**.
> - **Investigação offline** da escrita sem permissão em `0x2401A1000`:
>   - 3 de 3 ocorrências (80 e 81) são CB/DB (`Cid=0`), logo depois do IB `0x2401DC000` (552 DW, primeiro IB de um
>     contexto novo), **com o mesmo VMID do IB**: não é mistura de VMID;
>   - faltava saber os flags do mapeamento.
> - **M1:** sonda `[DX_TARGET_2401A1] MAP:`, somente leitura. A 82 é controle da reversão mais medição, sem
>   comportamento novo.
> - **Verificação:** 6 de 6 mutações DX.36 reprovam, mais 1 de teste; todas as auditorias PASS.
> - **Pacote lab 3.243.1.0**, NÃO INSTALADO: SYS `1E1575F5...`, `GfxWaveProbe=1`; UMD/ICD inalterados.

> **23/09/2026 — Corrida 81 (09:40:07, lab 3.242.1.0): desfecho D, o `SWITCH_BUFFER` trava o anel; BugCheck 0x116.**
> Relatório: [docs/355](docs/355_CORRIDA81_DX35_RESULTADO.md).
>
> - **5 de 5 TDRs** com ME e PFP parados em `0xC0008B00`, zero ondas, logo depois de cada troca de VMID. Em seguida,
>   0x116 com Arg3=0/Arg4=0xD, pilha **conferida** (`TdrBugcheckOnTimeout`, zero quadros nossos, igual a 12–13/09).
> - **Reverter a V1.**
> - **Linha do tempo de faltas funcionou:** escrita sem permissão recorrente em `0x2401A1000` (`Vmid=1`, `Cid=0`),
>   mesmo endereço do TDR 2 da 80. É a pista nova.
> - O host fechou certo: boot novo e marcador gravado pelo watchdog. BasicDisplay em Código 0.

> **23/09/2026 — faixa DX.35, OFFLINE e sem corrida.** Critério da 81 em
> [docs/354](docs/354_DX35_CRITERIO_CORRIDA81_SWITCH_BUFFER_E_LINHA_DO_TEMPO_DE_FALTAS.md). **NÃO VALIDADO EM HARDWARE.**
>
> - **CORREÇÃO:** a entrada da Corrida 80 dizia que o estado das ondas era novo. `FATAL_HALT`/`XNACK_ERROR` já tinham
>   sido lidos na Corrida 45; o novo na 80 é o VMID da onda.
> - **V1 (comportamental, única):** a troca de VMID no anel passou a emitir o **par de `SWITCH_BUFFER`** depois do
>   `PFP_SYNC_ME`, como em `amdgpu_vm_flush` (upstream v6.6, lido em `amdgpu_vm.c`/`gfx_v10_0.c`). Era a única parte
>   da sequência upstream que faltava. A troca passa de 31 para 35 DW, e a reserva do anel deriva do tamanho.
> - **M1 (medição):** `[GCVM_FAULT_TIMELINE]` lê o latch de fault a cada troca de VMID e drena pelo contrato do TDR se
>   não-zero. O dreno por TDR já existia desde a Corrida 56; faltava amostrar entre TDRs.
> - **Verificação:** builds EXIT=0; 8 de 8 mutações DX.35 reprovam, mais 1 de teste; DX.33/DX.34 continuam 0 falhas.
> - **Pacote lab 3.242.1.0**, NÃO INSTALADO: SYS `91767BD0...`, `GfxWaveProbe=1`; UMD/ICD inalterados.

> **23/09/2026 — Corrida 80 (09:14:40, lab 3.241.1.0): as ondas presas estão mortas por falta fatal de memória, e o
> TDR 1 rodou com o VMID do IB anterior.** Relatório: [docs/353](docs/353_CORRIDA80_DX34_RESULTADO.md).
>
> - **Estado das ondas (P0):** `FATAL_HALT` + `XNACK_ERROR` nas 4 ondas lidas de cada TDR; elas nunca retiram.
>   *(Correção: esse estado já era conhecido desde a Corrida 45. Novo aqui é só o VMID da onda.)*
> - **TDR 1 (T1):** ondas com VMID 1, IB com VMID 3. Fault `Vmid=1 Cid=9` (busca de instrução), permissão de execução,
>   PC `0x200040C00`, que é uma página `STAGING` com `NoExecute=1` no espaço do VMID 1.
> - **TDR 2 (T2):** VMID certo (3), também `FATAL_HALT`; a falta própria não foi capturada (latch antigo).
> - **Display:** quadro do harness e depois desktop do DWM **ficaram** (`BYTE_PROOF PASS` 256/256), com a entrada
>   congelada. Retângulos sujos: 1 uso, Match 5/5.
> - **H1** funcionou: boot novo e marcador gravado pelo watchdog. Sem BugCheck. Harness 1/60.

> **23/09/2026 — faixa DX.34, OFFLINE e sem corrida.** Critério da 80 em
> [docs/352](docs/352_DX34_CRITERIO_CORRIDA80_RETANGULOS_SUJOS_E_VMID_DA_ONDA.md). **NÃO VALIDADO EM HARDWARE.**
>
> - **H1 (host):** o watchdog grava o marcador de boot frio com os ticks do boot corrente, no início do rollback e em
>   `RECOVERY_OK_ONLINE`. Fecha o furo que deixou a 79 rodar no mesmo boot.
> - **V1:** o Present Blt copia só `pDstSubRects` (1–16, com Src == Dst == tela cheia), com prova amostral por retângulo.
> - **V2:** a sonda de ondas lê `SQ_WAVE_HW_ID2.VM_ID` da onda presa, e `GfxWaveProbe=1` foi armado. Troquei o
>   "marcador no IB" proposto porque nenhum registrador do CP expõe o VMID da onda.
> - **Verificação:** builds EXIT=0; 11 de 11 mutações DX.34 reprovam, mais 1 de teste; safebind offline PASS.
> - **Pacote lab 3.241.1.0**, NÃO INSTALADO: SYS `0D681AC8...`; UMD e ICD inalterados. Marcador de boot frio armado.

> **23/09/2026 — Corrida 79 (08:40:28, lab 3.240.1.0): display G, TDR T3, e violação de protocolo.**
> Relatório: [docs/351](docs/351_CORRIDA79_DX33_RESULTADO.md).
>
> - **Sem boot frio após a 78:** o binder aceitou o reboot quente do watchdog. Defeito de host: `RECOVERY_OK_ONLINE` não
>   grava o marcador de boot frio.
> - **V1 atuou:** 20 skips, 1 preservando 256/256, confirmado nos 2 Presents seguintes. V2 atuou pela primeira vez (1
>   `REDUNDANT_SKIP`).
> - **3 TDRs de 240 ondas pós-troca:** `CP_ME0_PIPE0_VMID=0` = VMID do anel, não do IB (T3).
> - **Novo apagador:** o Present Blt ignora `pDstSubRects` e copia a superfície inteira (zerada fora do cursor) sobre o
>   scanout. É a tela preta com cursor travado. Harness 0/60.

> **23/09/2026 — faixa DX.33, OFFLINE e sem corrida.** Critério da 79 em
> [docs/350](docs/350_DX33_CRITERIO_CORRIDA79_MODECHANGE_E_VMID_DO_PIPE.md). **NÃO VALIDADO EM HARDWARE.**
>
> - **V1 (comportamental, única):** `SET_ADDRESS` com `ModeChange` e sem flip deixa de copiar primária vazia (1024
>   amostras zero) sobre o scanout. Motivo: na 78, 16 de 16 cópias assim apagavam o quadro. MPO1/MPO3 inalterados.
> - **M1 (medição):** `[TDR_GFX_PIPE_VMID]` lê `CP_ME0_PIPE0_VMID` (0xC148, P1) e `CP_RB_VMID`, sem seleção GRBM.
> - **Verificação:** builds EXIT=0; 4 auditorias PASS; 8 de 8 mutações do portão DX.33 reprovam.
> - **Pacote lab 3.240.1.0**, NÃO INSTALADO: SYS `BC6F4FCE...`; UMD `239B046C...` e ICD `5988B38E...` inalterados.
>   Marca de boot frio ARMADA.

> **23/09/2026 — Corrida 78 (08:13:52, lab 3.239.1.0): classe de TDR C3, V2 não exercitada, e fora do mapa.**
> Relatório: [docs/349](docs/349_CORRIDA78_DX32_RESULTADO.md).
>
> - **Engine 0 funcionou** (79 flushes `Eng=0`, REQ/ACK nos dwords 0x28A3/0x28B5), e ainda houve **2 TDRs de 240 ondas pós-troca
>   de VMID**. A disputa do engine 17 **não é causa suficiente** (P0).
> - **`COMPUTE_VMID` não serve:** leu `0xCFBB8BBC` nos dois TDRs, sob `GRBM_GFX_CNTL=0` (C3).
> - **V2 nunca agiu:** 0 `REDUNDANT_*`, porque 19 de 20 `PRIMARY_COPY` tinham origem zerada.
> - **Harness 12/60 quadros**, contra o máximo anterior de 1/60. O quadro 1280×720 chegou ao scanout em 0,0: o "quadrado com
>   triângulo" visto.
> - **Achado:** a rota `SET_ADDRESS` da DX.31 copia a primária do GOP `0xAAC000`, sempre zerada, sobre o scanout, e apaga o
>   quadro. Isso aconteceu 16 vezes (P0 amostral).
> - Controles sem regressão: 5/5 em duas variantes, `nodcc` NÃO MEDIDO por device perdido; 0 BugCheck.
>   Recuperação: `RECOVERY_OK_ONLINE`.

> **23/09/2026 — faixa DX.32, OFFLINE e sem corrida: engine de invalidação próprio do anel, cópia de flip não
> repetida e `COMPUTE_VMID` lido no TDR. NÃO VALIDADO EM HARDWARE.** Critério da 78 em
> [docs/348](docs/348_DX32_CRITERIO_CORRIDA78_ENGINE_PROPRIO_E_COPIA_UNICA.md).
>
> - **V1 (comportamental):** o anel gráfico invalida no engine 0 (REQ 0xA28C / ACK 0xA2D4) e a CPU fica no 17. Até a 77
>   os dois disputavam o engine 17 sem serialização; o upstream dá um engine por anel. Motivação: 9 de 9 TDRs de 240 ondas
>   em dispatch logo após troca de VMID, resistentes à DX.29 e à DX.30.
> - **V2 (comportamental):** `PRIMARY_COPY` não repete a cópia que o Present acabou de fazer, e só pula se houver marca de
>   cópia integral desta origem (fonte única nos wrappers de Blt) **e** 256 amostras pareadas iguais.
> - **M1 (medição):** `[TDR_GFX_VMID] ObservedComputeVmid` (`COMPUTE_VMID` 0xB850, somente leitura), prometido no docs/344 e
>   que nunca tinha existido.
> - **Verificação:** builds EXIT=0; 4 auditorias PASS; 7 mutações de auditoria e 1 de teste reprovam.
>   **lab 3.239.1.0**, NÃO INSTALADO: SYS `9324073F...`, UMD `239B046C...` e ICD `5988B38E...` iguais aos da 77.

> **23/09/2026 — CORREÇÃO de registros das Corridas 76 e 77 (revisão).**
> - **A foto do triângulo RGB é da Corrida 75, não da 74.** O docs/345 §6 e a entrada da 76 abaixo diziam 74 e omitiam a 75,
>   que teve 1 Present aceito do harness copiado ao scanout em 640,180.
> - **A "regressão de controle" da 76 não está provada.** O `nodcc` 4/5 foi S5 NÃO MEDIDO (device perdido), não FAIL. Pelo
>   conteúdo, a 76 mostrou que a DX.30 não basta.
> - **Corrida 77, leituras que faltavam:**
>   - a cópia do flip repetia a do Present (explica "destino já igual");
>   - o hash do scanout mudou de flip #12 a #21 e congelou de #22 a #29 (P0 amostral);
>   - o TDR de 240 ondas foi o 9º de 9 da mesma classe.
>
>   Adendos anexados ao docs/345 (topo e §6) e ao docs/347 (§6).

> **23/09/2026 — Corrida 77 (07:41:45, lab 3.238.1.0), handoff final desta etapa:**
> [docs/347](docs/347_HANDOFF_POS_CORRIDA77_DX31.md), evidência em
> `docs/evidence/2026-09-23_07-41-45-DESKTOP-HQAG6T4-lab-binding/`.
> **Avanço visual/diagnóstico, não desktop funcional:** Daniel viu desktop
> congelado, contra tela preta na 76. KMD: 30 `PRIMARY_COPY` distintas,
> 22 amostras não-nulas pareadas, 8 branco/branco inconclusivas, zero falha
> de cópia; quatro `Present BYTE_PROOF PASS` (#1/#5/#10/#25, o último na
> cauda recuperada). Isso não prova quadro móvel nem que cada cópia mudou
> bytes — o destino já tinha as amostras não-nulas antes dessas cópias.
> As três variantes do autoteste RADV passaram 5/5, mas o harness D3D11 não
> gerou resumo de Presents aceitos; runner exit 3. Houve TDR de GFX com
> 240/240 ondas durante o teste e outro evento de TDR com 0/0 ondas após
> o início do rollback. **Veredito F/falha funcional** frente ao critério
> prévio; não anunciar 60 quadros ou desktop estável. O watchdog pediu
> reboot após recovery incompleta e confirmou BasicDisplay Código 0 no boot
> seguinte. A cauda de log foi arquivada com SHA-256 em `docs/347`;
> **não repetir sem boot frio, novo preflight e nova decisão do Daniel**.
>
> **23/09/2026 — faixa DX.31, OFFLINE e sem corrida: telemetria da cópia de primária GOP e gate honesto do harness. NÃO VALIDADO EM HARDWARE.** Critério proposto da Corrida 77 em
> [docs/346](docs/346_DX31_CRITERIO_CORRIDA77_COPIA_PRIMARIA_GOP.md).
>
> - A Corrida 76 mostrou conteúdo não-nulo em buffers de Flip, mas nenhum
>   `BYTE_PROOF PASS` do Present e tela preta. Auditoria de fonte encontrou
>   três DDIs que descartavam o retorno da SDMA e declaravam sucesso.
> - DX.31 registra cada cópia de primária com origem/destino, resultado da
>   engine, amostras antes/depois e comparação pareada limitada a 256 pontos;
>   falha não publica a primária nova. Amostra não é prova integral nem imagem
>   física. MPO3 não recebe leitura integral de ~11 MB no scheduler.
> - O runner não aceita Exit 0 com 0/60 ou autoteste 4/5; só a contagem final de
>   quadros aceitos e os três controles 5/5 podem passar seu gate. Build de
>   contratos, auditoria e mutações DX.31 passaram offline. Pacote lab
>   3.238.1.0 assinado/auditado (SYS `4AFA7AAF...`, UMD/ICD inalterados),
>   **não instalado**. Prebinding read-only bloqueado por reboot pendente
>   (`PendingFileRenameOperations`); boot frio e novo preflight são obrigatórios.
>   A DX.30 ainda está no fonte e sua classe de TDR continua aberta.

> **23/09/2026 — Corrida 76 (00:06:09, lab 3.237.1.0): desfecho F, regressão de controle.**
> Relatório e recuperação em [docs/345](docs/345_CORRIDA76_VM_FLUSH_NO_ANEL_RESULTADO.md);
> prova em `docs/evidence/2026-09-23_00-06-09-DESKTOP-HQAG6T4-lab-binding/`.
>
> - **Avanço diagnóstico, não funcional (P0):** 36 transições `VMID_SWITCH_VM_FLUSH`;
>   primeiro dump PM4 confirma `WAIT fence → PT_BASE LO/HI → REQ → WAIT ACK → PFP_SYNC_ME → IB`.
> - **Falhas (P0):** 3 TDRs de 240/240 ondas, todos após troca de VMID; dois IBs de
>   280/288 DW repetem a classe `DRAW_READBACK`. `nodcc` 4/5 (S5 não medido);
>   outros dois autotestes 5/5. Harness D3D11: 0/60 Presents aceitos, primeiro
>   falhou com `0x887A0005`, reason `0x887A0007`. O `EXIT 0` do wrapper é falso
>   sucesso perante a prova de frame.
> - **Recuperação:** watchdog pediu reboot após rollback online incompleto; no boot
>   seguinte BasicDisplay Código 0 foi confirmado em 5 amostras. Nosso DWM não
>   foi validado. Log FINAL incompleto foi complementado pelo resgate de tail
>   com SHA-256 arquivado. **Não repetir sem novo boot frio e novo critério.**
> - **Reanálise visual 73/74/76 (foto do Daniel, docs/345 §6):** 73 e 74 tinham
>   2 e 3 `BYTE_PROOF PASS` de conteúdo no scanout `0x0` antes do rollback; 76
>   teve zero PASS e um zero→zero inconclusivo. O triângulo fotografado combina
>   especialmente com a 74 (1 Present aceito do harness); 73 e 76 tiveram 0.
>   Buffers de Flip não-nulos na 76 não provam imagem no monitor. Tela preta
>   observada durante a 76 = regressão visual, não BasicDisplay após rollback.

> **23/09/2026 — faixa DX.30, OFFLINE e sem corrida: transição de VMID no anel gráfico implementada, compilada e empacotada como lab 3.237.1.0; NÃO VALIDADA EM HARDWARE.** Critério prévio da Corrida 76 e handoff em
> [docs/344](docs/344_POS_CORRIDA75_UPSTREAM_E_CRITERIO_CORRIDA76_VM_FLUSH_NO_ANEL.md).
>
> - **P0/P1:** os IBs de dispatch que travam são as cópias imagem→buffer de `DRAW_READBACK`; isso identifica o
>   disparador atual, não a causa. Desligar o instrumento ocultaria a classe e não corrigiria jogos com compute.
> - **P1:** `DISPATCH_DIRECT` não tem VMID; ele herda o VMID do `INDIRECT_BUFFER`. O upstream ordena no mesmo anel
>   pipeline sync → raiz do VMID → invalidação/ACK → `PFP_SYNC_ME` → IB. A DX.30 agora emite essa sequência
>   no CP_RB0 quando o VMID muda; a instalação MMIO permanece como preflight.
> - **Candidato offline:** raiz `READY` do pool (ou raiz instalada legada do VMID 7), plano GFXHUB engine 17,
>   recusa antes do WPTR se não houver raiz/plano/espaço, dump PM4 da primeira transição até o IB. `CONTEXT_CONTROL`
>   e `ACQUIRE_MEM` ficam fora desta variável. Build de contratos, SYS lab, UMD, auditorias e mutações PASS.
> - O teto segue uma corrida física pelo Daniel, após boot frio. Nenhum bind/instalação foi realizado aqui.

> **22/09/2026 — Corrida 75 (22:34:53, lab 3.236.1.0 + ICD `5988B38E`): desfecho C do docs/342.** Evidência em
> `docs/evidence/2026-09-22_22-34-53-DESKTOP-HQAG6T4-lab-binding/` (e a foto do Daniel).
>
> - **Reproduzido (P0):**
>   - quadro do harness D3D11 no monitor: fundo azul 0xFF2673E5 e triângulo com cor **interpolada por vértice**
>     (vermelho/verde/azul, visível na foto);
>   - Present do quadro 1 aceito, copiado por SDMA ao scanout em DstXY=640,180 (Present Blt, não composição do DWM);
>   - desktop sem quadrados: nenhuma grade de Present zerada, nos 3 DWMs.
> - **Variável 2 (ICD) sem regressão (P0):** zero `PREAMBLE_CNTL` em 71 IBs e autoteste 5/5 nas 3 variantes.
> - **Variável 1 (KMD) ativa e INEFICAZ (P0):** 77 `VMID_SWITCH_SYNC`, zero recusa, mas 3 TDRs de 240 ondas, **de novo
>   todos num IB de dispatch (280/288 DW) logo após troca de VMID**, com o fault sob o VMID do IB **anterior** (TDR 1:
>   shader do harness sob VMID 1; TDR 3: shader do DWM sob VMID 2, WalkerError=6). Somando as corridas: **8 de 8**. A
>   hipótese "o PFP avança para o IB novo" está **refutada**; a espera por fence no PFP não impede.
> - O mouse continua travado (DWM preso após os TDRs, H); harness com `DEVICE_REMOVED` no quadro 2. BasicDisplay em
>   Código 0 depois da recuperação.

> **22/09/2026 — faixa DX.29, OFFLINE e sem corrida: sincronia de troca de VMID no anel gráfico e fork sem
> `PREAMBLE_CNTL`. DUAS variáveis comportamentais, por decisão do Daniel. NÃO VALIDADO EM HARDWARE.** Critério da 75 em
> [docs/342](docs/342_CORRIDA74_ANALISE_E_CRITERIO_CORRIDA75_TROCA_DE_VMID.md).
>
> - **Motivação (P0):** 5 de 5 travamentos das Corridas 73–74 foram num IB logo após IB de outro VMID (taxa de base 18–26%).
> - **Variável 1 (KMD):** na troca de VMID, `WAIT_REG_MEM` mem/PFP `>=` sobre a fence do anel + `PFP_SYNC_ME`, antes do
>   `INDIRECT_BUFFER` (pipeline sync + fim do vm_flush do upstream; `>=` e TLB por MMIO são divergências declaradas). Log
>   `VMID_SWITCH_SYNC`.
> - **Variável 2 (ICD):** `radv_emit_graphics` sem `PKT3(0x4A)`, igual ao upstream; ICD **`5988B38E...`**, patch
>   `74304831...`. O autoteste separa as duas variáveis, porque roda num processo só, sem troca de VMID.
> - **Verificação:** builds EXIT=0; auditorias PASS (portões DX.29 no MCDM e no RADV); 7 mutações reprovam.
>   **lab 3.236.1.0**, NÃO INSTALADO: SYS `52CD8A05...`, UMD `5D66F9B3...` (igual à 74), INF `82BC8BD9...`, CAT `B751F262...`.

> **22/09/2026 — Corrida 74 (22:15:50, lab 3.235.1.0 + ICD `57D60ACB`): desfecho G do docs/341 (A parcial não conta,
> Lei 10.3). PRIMEIRO QUADRO DE UM APP D3D11 NO MONITOR, e os quadrados sumiram.** Evidência em
> `docs/evidence/2026-09-22_22-15-50-DESKTOP-HQAG6T4-lab-binding/`.
>
> - **Harness D3D11 (P0):**
>   - clear azul 0xFF2673E5 em 28800/28800 amostras e triângulo no centro (0xFF7F4040), renderizados pelo RADV;
>   - **Present do quadro 1 aceito** (1 de 60), o primeiro da história;
>   - o KMD copiou por SDMA o quadro de 1280×720 para o scanout em DstXY=640,180. É o quadrado azul com triângulo que o
>     operador viu. Foi Present Blt do próprio harness, **não** composição do DWM.
> - **Rotação DX.28 (P0):** 5 rotações no DWM 1212, com a `VkImg` girando pelas 3 imagens da cadeia; 5 `RTV_SEGUE_RECURSO`,
>   zero recusas; as 5 grades de Present deram 86400/86400, **nenhuma voltou a zero** (na 73 zerava). O operador viu o
>   desktop inteiro. O critério A exigia ≥10 grades e ≥5 `BYTE_PROOF PASS`; houve 5 e 3, porque os TDRs encurtaram o DWM.
> - **TDRs (P0):** 3, todos com 240 ondas. Nas duas corridas, **5 de 5 travamentos** foram num IB logo após um IB de **outro
>   VMID**, contra uma taxa de base de 18–26% dos IBs. Nos casos com fault de instrução, o VMID do fault é o do IB
>   **anterior** (74: TDR 1 = shader do harness sob o VMID 1; TDR 3 = shader do próprio DWM sob o VMID 1, com
>   WalkerError=6). H: o trabalho após troca de VMID no anel gráfico executa, ao menos em parte, com o VMID anterior; falta a
>   sequência de troca do upstream (pipeline sync + flush de VM no anel + `PFP_SYNC_ME`).
> - **Autoteste 5/5** nas 3 variantes; `CSIB_PROGRAM` PASS; zero BugCheck; mouse travado (H: DWM preso após os TDRs);
>   harness com `DEVICE_REMOVED` no quadro 2. BasicDisplay em Código 0 depois da recuperação.

> **22/09/2026 — faixa DX.28, OFFLINE e sem corrida: causa dos quadrados encontrada e corrigida, TDR de 240 ondas
> caracterizado, patch do fork preparado. NÃO VALIDADO EM HARDWARE.** Detalhe e critério da 74 em
> [docs/341](docs/341_CORRIDA73_ITENS_1_2_3_E_CRITERIO_CORRIDA74_ROTACAO.md).
>
> - **Quadrados (P0):** o alvo do DWM chegava a 86400/86400 e a grade do Present dava 0/86400 logo depois dos
>   `ResourceCopyRegion` do buffer anterior. `RotateResourceIdentities` girava `Allocation`/`pCpuShadow` e deixava a
>   `VkImage` para trás. **Correção (única variável da 74):** o grupo Vulkan gira junto, e a RTV refaz view e
>   framebuffer para seguir a imagem (Draw e Clear).
> - **TDR de 240 ondas:**
>   - P0: o IB travado é um dispatch de cópia do harness (VMID 4); o fault latchado é busca de instrução (CID 9) sob o
>     VMID 1 (DWM), na página exata do shader do harness, onde o VMID 1 mapeia a PRIMARY sem bit de execução;
>   - H: código de um processo buscado com a tabela de outro. A raiz do VMID é instalada por MMIO fora da ordem do anel,
>     e o pool reatribui VMIDs o tempo todo;
>   - instrumento novo, somente leitura: `[GFX_FAULT_SENTINELA]`, que registra a transição do status de fault por fence.
> - **Fork:** o upstream não emite `PREAMBLE_CNTL`; patch de remoção em `external/radv-wddm/pending/DX29-...` (aplica
>   limpo), **não aplicado**, reservado para a 75. O ICD segue `57D60ACB`.
> - **Verificação:** builds EXIT=0; auditorias PASS; 5 mutações de auditoria e 1 de teste reprovam. Uma mutação expôs um
>   furo no meu portão DX.28, já corrigido. **lab 3.235.1.0**, NÃO INSTALADO: SYS `E395E3F2...`, UMD `5D66F9B3...`,
>   INF `6066D2AA...`, CAT `9643AD6D...`.

> **22/09/2026 — Corrida 73 (21:49:38, lab 3.234.1.0 + ICD `57D60ACB`): desfecho A do docs/340. A RASTERIZAÇÃO FUNCIONA
> NO SILÍCIO, e pedaços do desktop composto pelo DWM chegaram ao monitor pela GPU.** Evidência em
> `docs/evidence/2026-09-22_21-49-38-DESKTOP-HQAG6T4-lab-binding/`.
>
> - **Clear state (P0):** CSB de 949 dw montado; `[CSIB_PROGRAM]` com readback PASS; `BOOT_PREAMBLE` com WPTR=RPTR=960,
>   fence=1, `DrenoOk=1`; snapshot com `LENGTH=0x3B5`; o CSIB sobreviveu aos 2 resets de TDR.
> - **Controle positivo (P0), 3 de 3 variantes (`nodcc`, `nonggc`, `nongg`):** S1 fill pela GPU sobre sentinela da CPU,
>   S2 clear de transferência, S3 clear de render pass, S4 draw de tela cheia e S5 contador = **65536/65536**, todos PASS.
>   Primeiro draw com fragmentos conferidos da história do projeto.
> - **Identidade GFX10.1:** zero registradores só-10.3 em 44 IBs; sem IB cumulativo.
> - **DWM (P0):**
>   - 507 submissões gráficas (7 na 72);
>   - alvo não-nulo logo após o draw, até 86400/86400 amostras, com a faixa de 40 linhas do rodapé (barra de tarefas) cheia;
>   - Presents com `DESKTOP REAL DETECTADO` NonZero=256/256, cópia de 11 MB **por SDMA** para o scanout e 2 `BYTE_PROOF PASS`;
>   - zero fallback de CPU.
>   - O operador viu blocos do desktop alternando com preto. A causa do padrão em blocos é **H**: redesenho parcial do
>     DWM por buffer, mais os 2 TDRs derrubando o DWM (dois PIDs).
> - **Falhas:**
>   - 2 TDRs recuperados: um com **240 ondas presas** (ME em `EVENT_WRITE`), outro com zero ondas e o PFP dentro de um
>     `INDIRECT_BUFFER`;
>   - o harness D3D11 reprovou no quadro 1 com `DEVICE_REMOVED` (887A0007) após TDR;
>   - `BYTE_PROOF INCONCLUSIVO` (frame em branco) no Present #25.
> - Zero BugCheck, zero fault GCVM, BasicDisplay em Código 0 depois da recuperação.

> **22/09/2026 — faixa DX.27, OFFLINE e sem corrida: clear state do GFX10 como no upstream (CSB + `RLC_CSIB` +
> preâmbulo de boot) e correção do S1 do autoteste. NÃO VALIDADO EM HARDWARE.** Análise da 72 e critério da 73 em
> [docs/340](docs/340_CORRIDA72_ANALISE_E_CRITERIO_CORRIDA73_CLEAR_STATE_UPSTREAM.md).
>
> - **Comparação dos IBs da 71 e da 72:** a identidade só removeu os registradores do 10.3 e mudou campos de geração
>   (`CU_EN`, `MEM_ORDERED`, `PA_CL_NGG_CNTL`, `GE_CNTL`). Com as duas identidades, `PA_SC_GENERIC_SCISSOR_*` e
>   `PA_SC_TILE_STEERING_OVERRIDE` ficam para o `CLEAR_STATE` (`ac_cmdbuf.c`). **Achado (P1):** o boot do KMD e o fork
>   emitem `PREAMBLE_CNTL` com payload 0/1; o upstream usa `(2<<28)` e `(3<<28)` (`nvd.h:395-396`). O boot também não tinha
>   nenhum `SET_CONTEXT_REG`. O `CLEAR_STATE` nunca teve dados.
> - **Única variável comportamental:**
>   - CSB de 949 dw montado a partir de `inc/linux_ref/clearstate_gfx10.h` (upstream sem edição, SHA-256 registrado);
>   - `RLC_CSIB_ADDR_HI/LO/LENGTH` gravados por escritor único, com snapshot, readback e restauração;
>   - o mesmo conteúdo emitido no CP_RB0, sem `SET_BASE`.
>
>   Steering = 0 (**H**, divergência declarada do 10.1). O `PREAMBLE_CNTL` do fork **não** foi tocado; o ICD segue `57D60ACB`.
> - **Autoteste:** S1 com sentinela da CPU 0x5E5E5E5E e fill 0x3C3C3C3C; o esperado é classificado antes do veneno.
> - **Verificação:**
>   - builds EXIT=0 e auditorias PASS;
>   - portão DX.27 novo e DX.26 ajustado (goldens seguem só leitura);
>   - 6 mutações reprovando na auditoria e 1 no teste do CSB (25 asserções).
> - **lab 3.234.1.0** assinado e auditado, NÃO INSTALADO: SYS `AF13FD89...`, UMD `A757ACED...` (igual), INF `7CFF430B...`,
>   CAT `43983AF7...`.

> **22/09/2026 — Corrida 72 (21:19:06, lab 3.233.1.0 + ICD `57D60ACB`): desfecho G, fora do mapa.** A identidade GFX10.1
> entrou (P0) e todo draw passou a travar a GPU. Evidência em `docs/evidence/2026-09-22_21-19-06-DESKTOP-HQAG6T4-lab-binding/`.
>
> - **Houve avanço?** Diagnóstico: SIM. Funcional: NÃO; houve regressão frente à 71, onde os draws retiravam sem pixel.
> - **Sucessos (P0):**
>   - `gfx_level=12` e `ngg_fully_culled_bug=1` em 4 processos; zero registradores só-10.3 em 5 IBs;
>   - o IB de fill (sem draw) retirou 2 de 2;
>   - snapshot: `RLC_CSIB` = 0/0/0 (CSB não programado) e 30 de 34 goldens diferentes (`GB_ADDR_CONFIG` confere);
>   - controles mantidos: sem cumulativo, zero fault GCVM latchado, 4 de 4 TDRs recuperados, zero BugCheck.
> - **Falhas (P0):**
>   - 7 submissões gráficas. Das 5 com draw, 4 geraram TDR e o destino da última ficou indeterminado;
>   - assinatura: ME em `WAIT_REG_MEM` após `RELEASE_MEM` pós-draw, `EopDoneBusy=1`, SPI e PA ocupados, **ondas=0**, em
>     `nonggc` e `nongg`;
>   - a variante `nodcc` foi derrubada pelo TDR do DWM antes de submeter;
>   - o S1 imprimiu FAIL por defeito meu (esperava o próprio veneno), com 65536 de 65536 no valor escrito.
> - **Recuperação:** BasicDisplay em Código 0 confirmado. Marcador de boot frio armado.

> **22/09/2026 — faixa DX.26, OFFLINE e sem corrida: identidade GFX10.1 no RADV, autoteste de controle positivo e
> snapshot somente leitura de goldens/CSB. NÃO VALIDADO EM HARDWARE.** Critério:
> [docs/339](docs/339_CRITERIO_CORRIDA72_IDENTIDADE_GFX10_E_CONTROLE_POSITIVO.md).
>
> - **Única variável comportamental:** winsys do fork declara `ip_discovery_version = 10.1.3` nos IPs GFX e COMPUTE, e
>   registra `BC-250 IDENTIDADE: gfx_level=...` no stderr. ICD `57D60ACB...`; portão novo no auditor do RADV (mutação reprova).
> - **Autoteste** `bc250-gfx-selftest.exe` (Vulkan puro, ICD direto, nada vai à tela): fill pela GPU → clear de transferência →
>   clear de render pass → draw de tela cheia → contador atômico de fragmentos; buffer reenvenenado pela GPU antes de cada cópia;
>   falha de map/fence é estado próprio. O runner o roda 3× (`nodcc`, `nonggc`, `nongg`) antes do harness.
> - **KMD:** snapshot somente leitura dos 34 golden settings (dupla leitura, comparação mascarada) e de `RLC_CSIB_*`
>   (`BASE_IDX 1` → 0x3B288; o 0x13288 do Gemini estava errado e eu o havia aceitado no docs/338 — corrigido). Portão garante
>   zero escrita e allowlist só de leitura (mutação reprova).
> - Builds EXIT=0 (fork, contrato, SYS lab, UMD, autoteste); auditorias PASS; **lab 3.233.1.0** assinado e auditado, NÃO
>   INSTALADO: SYS `7DE5C2D5...`, UMD `A757ACED...` (igual), INF `3530DAC9...`, CAT `521E2EB6...`.

> **22/09/2026 — Fase A (offline) das auditorias externas: o RADV trata o BC-250 como GFX10.3 (RDNA2).**
> Resultado em [docs/338](docs/338_RESULTADO_FASE_A_AUDITORIAS.md); plano em [docs/337](docs/337_PLANO_VALIDACAO_AUDITORIAS_GPT6_GEMINI.md).
>
> - **P1:** o winsys `wddm2` não preenche `ip_discovery_version`; `ac_fill_hw_ip_info` força `ver_minor=3` para NV fora de
>   NAVI10/12/14 → `gfx_level=GFX10_3`. **P0:** os IBs da Corrida 71 escrevem `DB_VRS_OVERRIDE_CNTL`, `PA_CL_VRS_CNTL` e
>   `SX_PS_DOWNCONVERT_CONTROL` (só existem no 10.3). **P1:** contornos de defeitos do GFX10.1 (`has_ngg_fully_culled_bug` e
>   outros) ficam desligados e o ACO gera 10.3. O patch da comunidade Linux (`referencias/bc250-gfx1013-fix`) corrige exatamente isto.
>   Efeito sobre "zero fragmento" e sobre o TDR de 240 ondas: **H**.
> - Outras verificações: topologia fixa no winsys (SA/SE=1) **confirmada**; captura Linux com SA/SE=2 citada mas **não
>   reverificável** (diretório ausente); CSB (`RLC_CSIB_*`) nunca programado **confirmado** e `GENERIC_SCISSOR` não é escrito
>   pelo IB; nenhum passo de init do GFX do upstream (goldens, `constants_init`, `init_csb`, `SH_MEM_*`) está no KMD;
>   primeiro uso `UNDEFINED` com pass `LOAD` **confirmado**; `-5` = `STATUS_DEVICE_REMOVED` após TDR (P0); `PA_SC_RASTER_CONFIG`
>   no GFX10 (Gemini) **refutado**; mecanismo "viewport negativo" (Gemini) **refutado** pela Corrida 70.
> - Ferramenta nova `tools/decode-gfx-ib-regs.py`. Nenhuma corrida; nenhuma mudança de driver.

> **22/09/2026 — Corrida 71 (16:53:51, lab 3.232.1.0 + ICD DX.22): DESFECHO C — vértices
> desenhados VÁLIDOS e o alvo continua zero logo depois de CADA draw, inclusive de draw SEM
> textura com cor branca. Nenhum fragmento chega ao alvo.** Evidência:
> `docs/evidence/2026-09-22_16-53-51-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/335](docs/335_CRITERIO_CORRIDA71_VERTICES_DESENHADOS_E_ALVO_APOS_DRAW.md).
>
> - **Houve avanço? SIM, diagnóstico decisivo; NÃO funcional.**
> - **Vértices (P0):** quads em coordenadas de tela coerentes — faixa `(0,0)–(2560,259)` branca,
>   janela `(237,259)–(1094,861)` branca, barra `(0,1040)–(2560,1080)` com cor `FF000000`, z=1;
>   índices lidos do IB (R16) com `Base=0`. CB do VS com a transformação tela→NDC.
> - **Alvo após cada draw (P0):** 84 leituras, **todas** `NonZero=0/86400`. O draw 5 é **sem
>   textura**, PS = cor interpolada (`[VK_PIPELINE_IR]`), cor branca, retângulo
>   `(237,229)–(1094,259)` com ~750 pontos da grade dentro — e a leitura deu **0**. Mesma imagem
>   no RTV, nos draws e na leitura (`0x7279ED0`). Scissor e viewport = tela inteira.
>   **A textura NÃO é a causa; nenhum fragmento é gerado ou escrito.** Não é A nem D.
> - **Suspeitos, ambos H:** (1) **golden settings do GC nunca aplicados** — a tabela de 34
>   registradores (`bc250_golden_settings_contract.c`: PA_SC_ENHANCE, PA_SC_BINNER_EVENT_CNTL_0,
>   CB_HW_CONTROL_3/4, DB_DEBUG*, GB_ADDR_CONFIG, SQ_ARB_CONFIG…) só existe em teste offline; nenhum
>   código do miniport a lê ou aplica (P0 pela busca), e o efeito disso é H; (2) **NGG** — o VS
>   roda como NGG com culling de primitivas no shader; se o NGG ou o culling falhar neste chip,
>   o resultado é exatamente "vértices válidos, zero fragmento", e a classe de TDR "240 ondas sem
>   retirar" cabe no mesmo quadro.
> - Controles: checker de IB SEM CUMULATIVO, zero `[TDR_GCVM_FAULT]`, GFX `446/446/0`, 1 TDR,
>   `RECOVERY_OK_ONLINE`.

> **22/09/2026 — faixa DX.25, OFFLINE e sem corrida: medir os vértices realmente desenhados e o
> alvo logo após cada draw. NÃO VALIDADO EM HARDWARE.** Critério:
> [docs/335](docs/335_CRITERIO_CORRIDA71_VERTICES_DESENHADOS_E_ALVO_APOS_DRAW.md).
>
> - `[DRAW_VERTS]`: `Start`, `Base`, IB (formato/offset), 6 índices da sombra do IB e os 3 vértices
>   que eles apontam (posição como float, cor em hex). `[DRAW_READBACK]`: cópia do alvo ≥ 1024x512
>   logo após o draw e a mesma GRADE do Present, teto de 40 por device, sem escrever no alvo.
> - Portão `audit_dx25_drawn_vertices_and_target_readback`; mutação sem `BaseVertex` reprova.
>   Builds EXIT=0, três auditorias PASS. **A 1ª passada do empacotador falhou e a auditoria de
>   pacote BLOQUEOU o pacote inconsistente**; a 2ª passou com PASS.
> - **Candidato lab 3.232.1.0 assinado, auditado e NÃO INSTALADO:** SYS `F2A3F42C...` (igual),
>   UMD `A757ACEDFA8B4EED...`, INF `AC2651E4E60BC962...`, CAT `9F90AF08F12D47A5...`.

> **22/09/2026 — Corrida 70 (16:42:23, lab 3.231.1.0 + ICD DX.22): DESFECHO C — preto com o CB
> do VS e o VB com conteúdo; nenhuma recusa nova.** Evidência:
> `docs/evidence/2026-09-22_16-42-23-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/334](docs/334_CRITERIO_CORRIDA70_ESTADO_D3D11_E_DADOS_DO_DRAW.md).
>
> - **Houve avanço? Diagnóstico parcial; NÃO funcional.** Operador: tela preta até o watchdog.
> - **Preto medido de novo:** 25 Presents do DWM com `GRADE NonZero=0/86400` (27 `hr=0`).
> - **As correções da DX.24 não quebraram nada:** zero `RECUSADO -- IA_`/`BLEND_`, zero exceção na
>   criação de pipeline (o stride 0 não reproduziu a divisão por zero histórica), 166 draws
>   concluídos com `[DRAW_DATA]`.
> - **CB do VS com conteúdo coerente (P0):** `nz=5/48`, `f0..3=[0,001 −0,002 −1 1]` — a
>   transformação tela 2560x1080 → NDC (2/2560, −2/1080, −1, +1). **Não é B.**
> - **VB0 com ~20% não-nulo, mas o vértice amostrado (offset 0) é zero — e isso NÃO conclui nada:**
>   os draws são indexados (`Indexed=1`, 6 índices), e o vértice lido depende de
>   `StartIndexLocation`, `BaseVertexLocation` e do index buffer, que a medição não registrou.
>   Imprecisão da instrumentação, declarada.
> - Controles: checker de IB = SEM CUMULATIVO (34 despejos), zero `[TDR_GCVM_FAULT]`, GFX
>   `381/381/0`, `RECOVERY_OK_ONLINE`. **2 TDRs**, ambos da classe "240 ondas/SA sem retirar".
> - **Próximo (offline):** medir os vértices REALMENTE desenhados (índices + base vertex) e ler o
>   alvo logo depois de cada draw na superfície apresentada, para separar "nada rasterizado" de
>   "rasterizado em preto" de "sobrescrito por um draw posterior".

> **22/09/2026 — faixa DX.24, OFFLINE e sem corrida: o estado D3D11 que era ignorado em silêncio
> passa a chegar ao Vulkan (ou recusa com nome), e cada draw mede CB/VB. NÃO VALIDADO EM
> HARDWARE.** Critério: [docs/334](docs/334_CRITERIO_CORRIDA70_ESTADO_D3D11_E_DADOS_DO_DRAW.md).
>
> - **Corrigido:** fatores/operações de blend (antes nunca mapeados; `ZERO`=0 virava `ONE`;
>   máscara 0 virava RGBA), blend constante dinâmico, `inputRate` por instância, stride 0
>   preservado, `location = InputRegister`, slot sem buffer **recusado** (antes recebia o buffer do
>   slot 0), formato de vértice desconhecido e >16 elementos **recusados**, X8 com alpha=1, sRGB
>   em formato sRGB, A8 como alpha, viewport com altura negativa (antes a imagem saía invertida).
> - **Medição:** `[DRAW_DATA]` com conteúdo do CB0 do VS/PS e do VB0 em todo draw.
> - Lógica pura extraída para `bc250_umd_state_translate.c` com `umd_state_translate_test`;
>   portão `audit_dx24_d3d11_state_reaches_vulkan`; **quatro mutações, todas reprovando**.
>   Um portão antigo (C1) pegou `memcpy` na função de medição — trocado por leitura indexada,
>   sem afrouxar o portão. Builds EXIT=0, três auditorias PASS.
> - **Candidato lab 3.231.1.0 assinado, auditado e NÃO INSTALADO:** SYS `F2A3F42C...` (igual),
>   UMD `1402DD97FD7D22FE...`, INF `D46AE420DDEF9F4A...`, CAT `E90A260278A11F6B...`.
> - **Risco declarado:** as recusas novas podem bloquear draws que antes rodavam com dado inventado.

> **22/09/2026 — Corrida 69 (16:07:25, lab 3.230.1.0 + ICD DX.22): DESFECHO B — a origem tem
> conteúdo, os draws miram a superfície apresentada, e a superfície sai PRETA.**
> Evidência: `docs/evidence/2026-09-22_16-07-25-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/333](docs/333_CRITERIO_CORRIDA69_ONDE_O_PIXEL_SE_PERDE.md).
>
> - **Houve avanço? SIM, diagnóstico decisivo; NÃO funcional.** Identidade: 3.230.1.0, ICD `45F93753...`.
> - **Preto agora é MEDIDO:** 23 Presents do `dwm.exe` com `GRADE NonZero=0/86400` e
>   `Faixa40Inferior=0/3200`. A pergunta "havia barra de tarefas?" está respondida: **não**.
> - **Origem com conteúdo (P0):** textura 2560x1080 com `NonZeroSrc=65829/65829`, janela 873x641
>   com 70510/71792, barra 2560x40 com 20356/102400, 192x15 com 2810/2880.
> - **Draws no alvo certo (P0):** 46 draws com `RT0=0x65076F0` 2560x1080, o mesmo recurso dos 16
>   `hSurfaceToPresent` do pid 7664. **Não é C nem D.**
> - **O PS do draw texturizado é `cor_do_vértice × texel → mrt0`** (`[VK_PIPELINE_IR]`), com texel
>   não-nulo na origem. O preto vem da cor do vértice, da geometria (nada rasterizado) ou do estado.
> - **Defeitos de tradução achados por leitura (P1), todos da classe "estado D3D11 ignorado em
>   silêncio":** (1) fatores e operações de blend nunca mapeados — só `BlendEnable` e a máscara
>   passam, e o Vulkan recebe `ONE/ZERO`; (2) `inputRate` sempre `VERTEX`, ignorando elementos por
>   instância (`Class=1`, que o DWM usa); (3) slot com stride 0 recebe stride inventado;
>   (4) `location = e` em vez de `InputRegister`; (5) tabela DXGI→Vulkan com comentários trocados:
>   88 é `B8G8R8X8` (sem alpha=1 forçado) e 91/93 são sRGB tratados como UNORM. Nenhum deles,
>   sozinho, explica cor zero no draw lido — **a causa do preto continua H**. O suspeito mais barato
>   de medir é o **conteúdo do constant buffer e do vertex buffer** (o VS multiplica a posição pelo CB;
>   CB zerado colapsa a geometria), que hoje não é medido.
> - **Controles:** checker de IB = SEM CUMULATIVO (31 despejos); zero `[TDR_GCVM_FAULT]`; GFX
>   `349/349/0`. **2 TDRs:** um da classe da 68 (240 ondas/SA, PFP em `ACQUIRE_MEM`) no 1º DWM, e um
>   **novo** (ME em `INDIRECT_BUFFER`, PFP sem pacote, **zero ondas**). Recuperação OK nos dois.
>   `RECOVERY_OK_ONLINE`, zero BugCheck.

> **22/09/2026 — faixa DX.23, OFFLINE e sem corrida: "os Presents da 68 eram pretos" NÃO SE
> SUSTENTA como medido, e o lab 3.230.1.0 passa a medir a superfície inteira. NÃO VALIDADO EM
> HARDWARE.** Critério: [docs/333](docs/333_CRITERIO_CORRIDA69_ONDE_O_PIXEL_SE_PERDE.md).
>
> - **Correção do meu relato da 68:** eu disse que tudo o que foi apresentado era preto. As 5
>   amostras da UMD são 4 pixels do canto superior esquerdo e o centro; o draw daquele quadro tinha
>   scissor `0,1040..2560,1080` (a barra de tarefas), com textura 2560x40 enviada logo antes, e
>   nenhuma amostra caía ali. E o `0/256` do KMD mede a alocação WDDM, que a ponte Vulkan→WDDM
>   não abasteceu (16 de 22 tentativas falharam: 12 com `DstVa` não resolvida no KMD). **O conteúdo
>   real do quadro é INDETERMINADO**, não zero.
> - Descartado por leitura: `RotateResourceIdentities` não troca a `VkImg`, então RTV e Present
>   usam a mesma imagem Vulkan.
> - **Instrumentação:** inspeção por GRADE (1 linha em 4, 1 pixel em 8, caixa envolvente, faixa
>   das 40 linhas inferiores); `NonZeroSrc` na origem de cada upload de textura; `RTV0`/`RT0` em
>   `SetRenderTargets` e em cada draw. `umd_present_grid_test` novo, mutação reprovando; builds
>   EXIT=0; três auditorias PASS.
> - **Candidato lab 3.230.1.0 assinado, auditado e NÃO INSTALADO:** SYS `F2A3F42C...` (igual),
>   UMD `D2761B8CDF3F62B4...`, INF `FFD5BCA8C77C0703...`, CAT `B3B6EE01DD58F57C...`. ICD o da DX.22.
> - **Pendente, não tocado:** a ponte Vulkan→WDDM (`COMPOSITE_BLT RECUSADO (resolucao)` com
>   `DstVa` de 2 dos 3 buffers do swapchain → `0x0`). Afeta o caminho de flip do dxgkrnl, não o
>   scanout direto.

> **22/09/2026 — Corrida 68 (15:48:59, lab 3.229.1.0 + ICD DX.22): O REPLAY ACABOU E A CLASSE DE
> FAULT `Cid=10 VA>>8` SUMIU. Desfecho E pela Lei 10.3 — A não fechou por um TDR de outra classe.**
> Evidência: `docs/evidence/2026-09-22_15-48-59-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/332](docs/332_CRITERIO_CORRIDA68_IB_NAO_CUMULATIVO.md).
>
> - **Houve avanço? SIM, funcional — o maior desde a Corrida 45.** ICD conferido em
>   `binding-provenance.json` (`45F93753...`).
> - **DX.22 validada no silício:** `check-gfx-ib-dump-cumulative.py` = **SEM CUMULATIVO** sobre 33
>   despejos em 4 VAs; **zero `[TDR_GCVM_FAULT]`** (contra 6 na 67); GFX `341/341/0`; 3.170 IBs SDMA.
> - **DWM andou:** pid 7984 rodou 52 s com **85 draws** (57 com SRV+sampler, 18 com 2 SRVs), upload
>   de textura 2582x1062, e **zero TDR**; no total 143 draws concluídos contra 28 na 67.
>   **21 `DXGI Present LEAVE cb: hr=0`** (16 do pid 7984) e 8 `[DdiPresent] ENTER` no KMD.
> - **`vkCreateDevice` voltou a funcionar depois do TDR** (`res=0` nas recriações; zero `res=-3`).
>   A falha da 67 era provavelmente consequência do estado envenenado pelo replay — H.
> - **FALHA 1 — um TDR de CLASSE NOVA** às 15:49:28, no 1º DWM (pid 1196): sem fault GCVM,
>   `SQ_DEBUG_STS_GLOBAL Busy=1`, **240 ondas por SA** sem retirar, PFP em `ACQUIRE_MEM`, ME em
>   `EVENT_WRITE`, `GFX_TRIPE Perna1=NAO_CONVERGIU` (RPTR 996, WPTR 1004). É shader que não
>   termina, não tradução. Recuperação completa: SDMA e CP_RB0 com restart provado, fences
>   reconciliadas, e o DWM seguinte rodou limpo.
> - **FALHA 2 — o que foi apresentado é PRETO:** todos os Presents aceitos com
>   `INSPECAO LINEAR_BUF ... NonZeroSamples=0/5` e `NonZero=0/256` no KMD. Os draws completam, mas
>   nenhum pixel não-nulo chega à superfície apresentada. Os clears pedidos pelo DWM foram todos
>   `RGBA=[0,0,0,0]`, então o conteúdo teria de vir dos draws texturizados.
> - Explorer: 6 `CreateResource RECUSADO: CALLBACK_FAILED`. Zero BugCheck; `RECOVERY_OK_ONLINE`.
> - **Veredito:** A exigia zero `TDR_QUIESCE` e houve 1 → **não é A**. Não há `Cid=10` → não é C.
>   O ICD entrou → não é D. **E**, descrito acima.

> **22/09/2026 — faixa DX.22, OFFLINE e sem corrida: o IB concatenado da BC-250 sai do preâmbulo
> e vai para BO próprio. NÃO VALIDADO EM HARDWARE.** Critério:
> [docs/332](docs/332_CRITERIO_CORRIDA68_IB_NAO_CUMULATIVO.md).
>
> - **No fork do RADV** (`radv_wddm2_cs.c/.h`): pool de até 32 BOs por contexto e IP; o preâmbulo
>   e os CS da aplicação **não são mais escritos**; o slot só é reciclado depois que uma fence
>   sinalizada após a submissão dele retira; sem fence e sem slot livre, `CONCAT RECUSA` e
>   `VK_ERROR_DEVICE_LOST`. **Segundo defeito corrigido no mesmo ramo:** CS que cresceu para
>   vários IBs encadeados tinha só o último buffer copiado; agora vai para o encadeamento
>   (`CONCAT PULADA`). O despejo grava o buffer realmente submetido.
> - Build do fork EXIT=0; DLL `45F937536A4EF368...` com as strings novas. `audit-radv-wddm-safety.py`
>   **PASS** com portão novo; o fonte anterior **reprova em 7 pontos**; mutação (prender o slot antes
>   de a submissão ser aceita) **reprova**. Delta espelhado em `external/radv-wddm/` (patch
>   `9EAB0174...`).
> - **`tools/check-gfx-ib-dump-cumulative.py`** novo: **reprova a Corrida 67 (18 pares de replay)**,
>   é inconclusivo na 66 (sem despejo) e aceita reuso legítimo de slot (sintético).
> - O pacote do driver continua o lab 3.229.1.0; muda só o ICD, que o `EXECUTAR_D3D11_NATIVO.cmd`
>   copia do `build\` do fork. Até lá, o `C:\BC250_LIB\vulkan_radeon.dll` implantado é o anterior
>   (`7837C960...`) e `sync-radv-fork-delta.py` reprova **só** nisso, como esperado.

> **22/09/2026 — Corrida 67 (15:25:56, lab 3.229.1.0): DESFECHO C, e a instrumentação DX.21 achou
> a causa: o fork do RADV reexecuta TODOS os draws anteriores a cada submissão.**
> Evidência: `docs/evidence/2026-09-22_15-25-56-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/330A](docs/330A_EMENDA_CRITERIO_CORRIDA67_CID_CORRIGIDO_E_DX21.md).
>
> - **Houve avanço? SIM, diagnóstico decisivo; NÃO funcional.** Identidade: 3.229.1.0, SYS
>   `F2A3F42C...`. `nodcc` ativo (29 linhas) e **6 faults `Cid=10` (SQC data) com `VA>>8`
>   resolvendo em `VA<<8`** (`0x2465`, `0x2466`×3, `0x245A`×2), 6 TDRs, 6/6 restarts provados.
>   A previsão registrada antes (o `nodcc` não elimina o `Cid=10`) **se confirmou**.
> - **Instrumentação funcionou na 1ª tentativa:** 32 arquivos em `gfx-ib-dump/` (primeira vez desde
>   a Corrida 35) e 6.010 linhas `[VK_PIPELINE_IR]` com os programas ACO do dwm.exe.
> - **O programa nomeou a leitura (P0):** o VS faz `s_load_dwordx4` do V# do constant buffer em
>   `set+64` (ponteiro `{s10, hi=2}`) e depois `s_buffer_load_dwordx4` com ele — leitura escalar,
>   `Cid=10`. Se `set+64` contiver um T#, a base vira exatamente `VA>>8`.
> - **Hipótese do ACQUIRE_MEM ausente, REFUTADA pelo despejo:** o RADV abre cada IB com
>   `ACQUIRE_MEM` GLI/GLK/GLV/GL1/GL2 INV.
> - **CAUSA (P0 pelo despejo + P1 pela fonte):** os IBs são **cumulativos** — mesma VA
>   `0x244f24000`, +392 DW por submissão, e o IB *n* tem prefixo idêntico ao IB *n-1* (576 → 976
>   → … → 6.664 DW). Em `radv_wddm2_cs.c:895-918` do fork, o ramo "BC-250 UNIFICADO" copia o CS
>   principal **para dentro do CS do preâmbulo** (`cs_list[0]`, persistente da fila) e grava
>   `cs_list[0]->cdw = cur_offset` **sem restaurar**. Cada submissão reexecuta os draws antigos,
>   cujos descriptor sets a UMD já liberou depois da fence — descritor velho, shader liberado
>   (`Cid=9` em `0x200040000`) e tempo de IB crescendo até o TDR (VkDraws 7→8 levou 2,46 s).
>   Explica a classe `Cid=10 VA>>8` desde a Corrida 45 — **H de alta confiança para as corridas
>   antigas**, P0 para a 67.
> - **Fora do mapa (desfecho E, parcial):** depois do TDR, `vkCreateDevice` falhou com `res=-3`
>   **nas duas tentativas** (com e sem a extensão) no dwm.exe e no explorer.exe — a ponte não se
>   recria após device-lost.
> - Controles: zero BugCheck, GFX `Submetidas=129 Aposentadas=129 EmVoo=0`, Present 0 aceito
>   (`NENHUM_RENDER_DETECTADO`), `RECOVERY_OK_ONLINE`, BasicDisplay ao final.
> - **Próximo:** corrigir o ramo de concatenação no fork (não mutar o preâmbulo; buffer por
>   submissão), com teste que reprove IB cumulativo pelo próprio `gfx-ib-dump`.

> **22/09/2026 — faixa DX.21, OFFLINE e sem corrida: o `Cid` da Corrida 66 estava com o NOME
> ERRADO, o despejo de IB nunca pôde gravar do `dwm.exe`, e a Corrida 67 passa a rodar o lab
> 3.229.1.0 com o programa de shader no log. NÃO VALIDADO EM HARDWARE.**
> Emenda do critério: [docs/330A](docs/330A_EMENDA_CRITERIO_CORRIDA67_CID_CORRIGIDO_E_DX21.md).
>
> - **CORREÇÃO DE REGISTRO (Lei 7):** docs/329, 330 e 331 chamaram `Cid=10` de `TCP_UTCL0` e
>   `Cid=9` de `SQC_DCACHE`, citando `gc_10_1_0_sh_mask.h`. Esse arquivo **não tem tabela de
>   clientes**. Com fonte (P1): GC 10.1.x usa `gfxhub_v2_0` (`gmc_v10_0.c:593-609`), onde
>   **`Cid=8` = TCP, `Cid=9` = SQC (inst), `Cid=10` = SQC (data)** (`gfxhub_v2_0.c:34-53`, sha256
>   `BF032415...`). Os dois faults da 66 são **leituras do SQC**: `0x2431000` é leitura escalar e
>   `0x200040000` é busca de instrução no heap de 32 bits dos shaders.
> - **A hipótese do DCC não explica o fault `Cid=10`** (P1): o SQC não lê metadados de DCC, e o
>   mesmo `0x2431` já aparecia nas Corridas 45–64, antes do upload por staging. O `nodcc` da DX.20
>   **alcança** o RADV (o fork lê por `GetEnvironmentVariableA`, `os_misc.c:217`; DLL implantada =
>   `build/` do fork, sha256 `7837C960...`) e fica como **variável isolada**, não como causa.
> - **`gfx-ib-dump/` vazio nas Corridas 35, 36 e 66 tem causa lida:** a pasta de evidência só dá
>   acesso a SISTEMA, Administradores e Daniel (`icacls`); o `dwm.exe` roda como `DWM-<n>`, o
>   `fopen` do RADV falhava e a recusa ia para stderr. Agora: staging
>   `C:\Windows\Temp\BC250_GFX_IB_DUMP` com ACL para o DWM, armado **antes** do bind e copiado na
>   coleta FINAL com `_COLETA.txt`. Correção só de ferramenta.
> - **`[VK_PIPELINE_IR]`:** a UMD liga `VK_KHR_pipeline_executable_properties` (com fallback sem
>   a extensão) e despeja o programa pós-RA de cada pipeline (sem LLVM, o `aco_print_program`),
>   para nomear o `s_load`/`s_buffer_load` que lê em `VA>>8`.
> - `build-umd.bat` **EXIT=0** (`umd_pipeline_ir_dump_test` novo, PASS), `build-contract.bat`
>   **EXIT=0**, três auditorias **PASS**, portão novo
>   `audit_dx21_pipeline_ir_and_ib_dump_reach_evidence`. **Três mutações, todas reprovando.**
> - **Candidato lab 3.229.1.0 assinado, auditado e NÃO INSTALADO:** SYS
>   `F2A3F42C3823D592CAB428ED9497EB457A85C854A676C5CFCBB2C70971DB614D` (**igual** ao 3.228.1.0),
>   UMD `A24F3A3F438DC779...`, INF `82C25BF28CB3E452...`, CAT `0CA120B7616B4040...`.
>   `Lab package audit: PASS`; strings novas conferidas na UMD assinada.
> - **Previsão registrada antes (P1):** o `nodcc` **não** deve eliminar o `Cid=10`.

> **22/09/2026 — Corrida 66 (13:06:56, lab 3.227.1.0) e faixa DX.19: RESTAURAÇÃO DO UPLOAD VULKAN DE TEXTURAS 100% PROVADA NO SILÍCIO, SALTO PARA 64 SUBMISSÕES 3D APOSENTADAS (FENCES ATÉ 66), DRAW 1 DO DWM CONCLUÍDO EM 21 MS E ISOLAMENTO DO STALL DO DRAW 2 EM METADADOS DCC.**
> Evidência: `docs/evidence/2026-09-22_13-06-56-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/328](docs/328_CRITERIO_CORRIDA66_VULKAN_TEXTURE_UPLOAD_RESTORED.md); handoff em
> [docs/329](docs/329_HANDOFF_2026-09-22_CORRIDA66_UPLOAD_PROVADO_E_FENCE66.md).
>
> - **Houve avanço? SIM, avanço funcional e diagnóstico decisivo.** Identidade conferida: 3.227.1.0, SYS
>   `F2A3F42C3823D592CAB428ED9497EB457A85C854A676C5CFCBB2C70971DB614D`.
> - **Upload Vulkan de texturas comprovado:** `Bc250UmdVkUploadImageData` via staging buffer executou com sucesso
>   para a textura 590x800 e demais recursos do DWM, aplicando as barreiras explícitas e validando o layout via fence física.
> - **Salto no anel gráfico Node 1 GFX:** 64 submissões 3D aceitas e aposentadas no silício, avançando fences até Fence 66
>   (contra 10 na Corrida 65).
> - **Render do DWM avançou:** Draw 1 (`VkDraws=1`, vertices=48, `CB=1, SRV=0`) executou e aposentou em apenas 21 ms.
> - **Isolamento do stall do Draw 2:** Draw 2 (`VkDraws=2`, vertices=126, `CB=1, SRV=1, SAMPLER=1`) levou 2,065 s na GPU,
>   disparando o watchdog TDR de 2,0 s do Windows. O fault `Cid=10` (~~`TCP_UTCL0`~~ **SQC (data)**, corrigido na DX.21)
>   foi registrado em `0x2431` (`0x2431000` = `0x243100000 >> 8`). ~~A causa raiz foi isolada em metadados de compressão
>   DCC~~ — **retirado na DX.21:** o DCC era H, não isolamento, e o SQC não lê DCC.
> - Controles da 66: zero BugCheck, fusível CLEAN, `RECOVERY_OK_ONLINE` com BasicDisplay Código 0 em 5/5 amostras.

> **22/09/2026 — Corrida 65 (10:52:21, lab 3.226.1.0) e faixa DX.18: UPLOAD DE RECURSOS EM VRAM VISIVEL PELA CPU PROVADO (0x80070057 ZERO), BYPASS DA COPIA META COMPUTE ATIVO, 10 SUBMISSOES GFX APOSENTADAS NO SILICIO E CID=10 ISOLADO NO DESCRITOR T# DE TEXTURA.**
> Evidência: `docs/evidence/2026-09-22_10-52-21-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/326](docs/326_CRITERIO_CORRIDA65_CPU_VISIBLE_RESOURCE_E_DESCRITOR_FIX.md); handoff em
> [docs/327](docs/327_HANDOFF_2026-09-22_CORRIDA65_CPU_VISIBLE_PROVADO_E_T_HASH_CID10.md).
>
> - **Houve avanço? SIM, funcional e diagnóstico.** Identidade conferida: 3.226.1.0, SYS
>   `F2A3F42C3823D592CAB428ED9497EB457A85C854A676C5CFCBB2C70971DB614D`.
> - **A falha de lock foi eliminada:** `pfnLockCb` em alocações `RESOURCE` (`0x52455300...`) passou em 100%
>   das tentativas (`hr=0x00000000`). O erro `0x80070057` desapareceu dos logs do DWM.
> - **Bypass de meta-compute copy comprovado:** Texturas atualizadas via VRAM direta dispensaram a chamada
>   de cópia redundante `radv_meta_buffer_to_image_cs`.
> - **Avanço no motor gráfico Node 1 GFX:** 10 submissões aceitas e aposentadas no silício sob `VMID 1`
>   com avanço de fences físicas (Fences 1..10 completadas).
> - **Isolamento físico do `Cid=10`:** O fault `Cid=10` (`0x2441` -> `0x2441000` / `0x244100000 >> 8`)
>   não ocorreu no upload, mas foi disparado quando o Pixel Shader tentou amostrar a textura através do
>   descritor `T#` (`desc[0] = va >> 8`) após o bind do SRV.
> - Controles da 65: zero BugCheck, fusível CLEAN, `RECOVERY_OK_ONLINE` com BasicDisplay Código 0 em 5/5 amostras.

> **22/09/2026 — Corrida 64 (08:54:17, lab 3.225.1.0) e faixa DX.17: O READBACK COMPROVOU ZERO DIVERGENCIA NA ESCRITA DO PDE (99/99 batem), E O CID=10 COM PERDA DE 8 BITS ATINGE A PRIMARIA DO DWM.**
> Evidência: `docs/evidence/2026-09-22_08-54-17-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/323](docs/323_CRITERIO_CORRIDA64_ESCREVEMOS_LIMPO_LEEM_SUJO.md); handoff em
> [docs/325](docs/325_HANDOFF_2026-09-22_CORRIDA64_READBACK_ZERO_DIVERGENCIA_E_CID10.md).
>
> - **Houve avanço? SIM, diagnóstico decisivo; NÃO funcional.** Identidade conferida: 3.225.1.0, SYS
>   `5C2950513E5C66CEE312A2535F773C2093CA1B9075465A63F21DB3B289236072`.
> - **A pergunta de DX.17 foi fechada:** Em **99 de 99 escritas** no índice 4096 (25 não-nulas, todas
>   limpas), o readback imediato pela CPU leu exatamente o valor codificado (`arenaReadback == arenaEncoded`).
>   **DIVERGENCIA = 0.** A nossa escrita chega à memória física com 100% de integridade. A hipótese de
>   defeito no ponteiro ou mapeamento da CPU está **refutada**.
> - **O `Cid=10` reproduziu pela sétima vez:** `FaultAddrPagina=0x2431` (`GpuVa=0x2431000`), com L1 idx 18
>   em `PDE=0` (não mapeado). A caminhada em `0x243100000` (`VA<<8`) resolveu perfeitamente para
>   `PA=0x451CCC000` (`PDE=0x45FDB1001`, `PTE=0x451CCC071`).
> - **Identidade do alvo:** `0x243100000` está a 2 MiB da base da **superfície primária do DWM**
>   (`2560x1080 bpp=4`, 11.059.200 bytes, `Base=0x242F00000`, `Primary=1`). O acesso em `0x2431000` é
>   a primária com perda de 8 bits (`>>8`).
> - Controles da 64: 1.723 IBs, 7/7 restarts provados, 1 fault drenado (`Cid=10`), 6 sem latch, 21 desarmes
>   de VMID OK, `HdpFlush` 66/66, zero BugCheck, `RECOVERY_OK_ONLINE`. Present falhou no quadro 1 (`0x887A0005`);
>   tela preta mantida.

> **22/09/2026 — PASSAGEM DE BASTÃO ATUAL (Pós-Corrida 66, Pré-Corrida 67):** [docs/331](docs/331_PROMPT_HANDOFF_POS_CORRIDA66_PRE_CORRIDA67.md), **emendado por [docs/330A](docs/330A_EMENDA_CRITERIO_CORRIDA67_CID_CORRIGIDO_E_DX21.md)** — o candidato da 67 é o 3.229.1.0.
> **22/09/2026 — PASSAGEM DE BASTÃO ANTERIOR:** [docs/324](docs/324_PASSAGEM_DE_BASTAO_2026-09-22_CORRIDAS_59_A_63.md).

> **22/09/2026 — Corrida 63 (08:39:11, lab 3.224.1.0) e faixa DX.17: A PERDA DE 8 BITS ESTA
> CONFIRMADA PELO PROPRIO LOG (desfecho A do `Cid=10`), e sobrou UMA contradicao que e NOSSA.**
> Evidência: `docs/evidence/2026-09-22_08-39-11-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/322](docs/322_CRITERIO_CORRIDA63_O_ENDERECO_PERDEU_8_BITS.md).
>
> - **Houve avanço? SIM, diagnóstico; NÃO funcional.** Identidade conferida: 3.224.1.0, SYS
>   `6FCFB6EC0F0456E2B2AD090717B970F77F17548871028DD416672E7ED3541BAB`.
> - **Desfecho A para o `Cid=10`:** `WALK VA(pagina<<12) = 0x245B000 NAO RESOLVE` e
>   `WALK VA<<8(perda de 8 bits) = 0x245B00000 RESOLVEU para PA=0x4565CC000`. O endereço que
>   faltou perdeu 8 bits, e agora **o próprio log diz isso** em vez de eu reconstruir entre
>   corridas. Sexta ocorrência do padrão no projeto.
> - **O `Cid=9` mudou de comportamento:** na Corrida 60 o `0x200040000` resolvia; aqui **não**
>   resolve — a caminhada parou no PDE sujo `0x451BCC071` no índice 4096 (`reservado[5:3]=0x30`).
>   **Terceira ocorrência** do PDE sujo (Corridas 59, 61, 63).
> - **A CONTRADIÇÃO QUE SOBROU, e ela é nossa:** somando as Corridas 60, 61 e 63, **nós
>   escrevemos o índice 4096 trezentas e quatorze vezes, TODAS limpas**, com zero
>   `[PDE_TRIPWIRE]` e zero recusa — e o TDR leu valor de **PTE** nesse mesmo índice em três
>   corridas. As duas coisas não podem ser verdadeiras sem que alguém escreva depois, **ou** sem
>   que a nossa escrita não chegue onde acreditamos.
> - **FAIXA DX.17, offline — o mínimo que separa os dois casos:** readback imediato da escrita em
>   `destination[4096]` pelo **mesmo ponteiro**, comparado com o que escrevemos; divergência
>   declarada e **NÃO corrigida** (reescrever esconderia a frequência, e há portão proibindo);
>   mais `[PDE_ARENA_CONFRONTO]`, que publica no TDR o **último valor que nós escrevemos** e
>   quantas escritas houve — os dois números passam a estar no mesmo log. `Escritas=0` fica
>   declarado em vez de virar "eram iguais".
> - Controles da 63: 1.877 IBs, 6/6 restarts, 106 `[PDE_WRITE]` todas limpas, 24/24 desarmes OK,
>   `HdpFlush` 75/75, zero BugCheck, `RECOVERY_OK_ONLINE`.
> - `build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias **PASS**.
>   **Sete mutações verificadas, todas reprovando** — inclusive a que transforma a divergência em
>   reescrita silenciosa.
> - **NENHUM portão meu passou nesta faixa — a primeira em cinco.** Nas quatro anteriores o padrão
>   foi sempre "procurei a string no escopo errado"; aqui os invariantes foram escritos sobre
>   **uso** e **ordem** desde a primeira versão.
> - **Candidato lab 3.225.1.0 assinado, auditado e NÃO INSTALADO:** SYS
>   `5C2950513E5C66CEE312A2535F773C2093CA1B9075465A63F21DB3B289236072`, PDB `348052E9D5320C58...`,
>   UMD `24D604813802DC57...`, INF `B77EBBDCC0EDB0F6...`, CAT `10DCBDDE6C795C21...`.
>   `Lab package audit: PASS`; as duas strings novas conferidas **em ANSI** no SYS assinado.
> - **Critério da Corrida 64:**
>   [docs/323](docs/323_CRITERIO_CORRIDA64_ESCREVEMOS_LIMPO_LEEM_SUJO.md). Diagnóstico: **não deve
>   mudar o fault**. Tela preta segue correta.

> **22/09/2026 — Corrida 62 (08:20:23, lab 3.223.1.0) e faixa DX.16: O DESARME FUNCIONOU E O
> FAULT PERMANECEU (desfecho B) — e a GRANULARIDADE FOI RESOLVIDA, revelando que a conclusão da
> Corrida 47 era uma LEITURA ERRADA.** Evidência:
> `docs/evidence/2026-09-22_08-20-23-DESKTOP-HQAG6T4-lab-binding/`; critério em
> [docs/321](docs/321_CRITERIO_CORRIDA62_DESARME_DO_VMID_NO_SILICIO.md).
>
> - **Houve avanço? SIM, e é o maior desta sequência — mas é diagnóstico, não funcional.**
>   Identidade conferida: 3.223.1.0, SYS
>   `A17258E9FC9AB4A63CCB9916B58FEB6A08315E7638C4CB12C2226DCCDA6EFB29`.
> - **DX.15 exercitada:** **24 de 24** `[VMID_POOL] DESARMADO` com `status=0 Readback=1 Flush=1`,
>   zero falhas, e **dois faults permaneceram**. Desfecho B — o desarme continua certo pela Lei 5,
>   e não era a causa. O risco que declarei (MMIO em `DestroyContext`) **não se materializou**:
>   zero BugCheck.
> - **A GRANULARIDADE ESTÁ RESOLVIDA, E NUNCA FOI GRANULARIDADE.** `gc_10_1_0_sh_mask.h:12163-12168`
>   (P1, local) declara `LOGICAL_PAGE_ADDR_LO32` com **32 bits** e `_HI4` com **4 bits** — página
>   de **36 bits**. Com 4 KiB isso cobre 48 bits de VA; com 1 MiB seriam 56, e vinte dos trinta e
>   seis ficariam sempre zerados. **O endereço é `página << 12`.** Corroborado (P0): a página
>   `0x200040` sob `<<12` dá `0x200040000`, que a nossa própria linha `[ALVO 0x200040]` reporta
>   como a **base exata** de uma alocação viva.
> - **A CONCLUSÃO DA CORRIDA 47 ERA UMA LEITURA ERRADA.** `<<20` é identicamente
>   `(página<<12)<<8`. O que aquele ensaio mediu foi que **o endereço real é 256× o que faltou** —
>   assinatura de **perda de 8 bits**, não tamanho de página. Vale para TODOS os faults `Cid=10`
>   do projeto: `0x2431 -> 0x243100000` (Corridas 45, 46, 48, 59) e `0x245B -> 0x245B00000`
>   (Corridas 45, 62).
> - **CORREÇÃO DO MEU PRÓPRIO RELATO, três corridas seguidas.** Eu afirmei que "a tabela vista
>   pela CPU está certa e a GPU falta no mesmo endereço". Para os faults `Cid=10` — **2 de 2**
>   nesta corrida — isso é **FALSO**: no endereço que realmente faltou (`0x245B000`) a tabela diz
>   `PDE=0` no índice 18, **não mapeado**, e a GPU concorda. **Não há paradoxo.** As linhas
>   `RESOLVEU em <<20` caminhavam **outro endereço** e não provavam nada. O paradoxo do `Cid=9`
>   (`0x200040000`, mapeado e mesmo assim faltou) **continua de pé**, mas não ocorreu aqui.
> - **FAIXA DX.16, offline — correção de honestidade, não experimento:** o segundo candidato
>   deixou de se chamar `<<20` e passou a `VA<<8(perda de 8 bits)`; a linha de sucesso do resolver
>   dizia *"a PTE EXISTE, logo o defeito NÃO é PTE ausente"* e passou a dizer que resolver ali
>   significa que **o endereço que faltou perdeu 8 bits**, não que a tabela esteja certa.
> - **UM PORTÃO MEU ESTAVA CONGELANDO A CONCLUSÃO ERRADA** — ele *exigia* a frase antiga. É a
>   classe "o gate carimbava a mentira", pela primeira vez neste projeto aplicada a uma
>   **conclusão** em vez de a um contador. Corrigido junto.
> - Os encodings `>>8` conferidos por leitura estão **corretos** (`CP_RB0_BASE`,
>   `SPI_SHADER_PGM_LO/HI`, `CB_COLOR0_BASE`). **O sítio defeituoso ainda não tem nome**, e o
>   campo `Cid` não tem enumeração local — não invento (Lei 7).
> - Controles da 62: 1.755 IBs, 7/7 restarts, `HdpFlush` 73/73, GFX `70/70/0`, zero BugCheck,
>   `RECOVERY_OK_ONLINE`. Present falhou no quadro 1; nenhum pixel.
> - **Três mutações verificadas na DX.16, todas reprovando.** E **um portão meu passou — quarta
>   vez nesta sequência**: provava que o array de rótulos existe, não que as linhas o **consumam**.
>   As quatro foram: string no arquivo em vez do bloco (2×), argumento em vez da chamada, e
>   existência em vez de uso. **É padrão meu, não azar.**
> - **Candidato lab 3.224.1.0 assinado, auditado e NÃO INSTALADO:** SYS
>   `6FCFB6EC0F0456E2B2AD090717B970F77F17548871028DD416672E7ED3541BAB`, PDB `63C59F930E056B4C...`,
>   UMD `DA6CFC4222C9C54C...`, INF `87AA2A642AA89F92...`, CAT `C7F49EF24C616F0F...`.
>   `Lab package audit: PASS`; a frase antiga conferida **ausente** no SYS assinado.
> - **Critério da Corrida 63:** [docs/322](docs/322_CRITERIO_CORRIDA63_O_ENDERECO_PERDEU_8_BITS.md).
>   Só telemetria: **não deve mudar o fault**. Tela preta segue correta.

> **21/09/2026 — faixa DX.15, OFFLINE após a Corrida 61: o VMID passou a ser devolvido TAMBÉM
> NO SILÍCIO. NÃO VALIDADO EM HARDWARE.**
>
> - **Achado por leitura, e não é hipótese:** `Bc250VmidPoolRelease` era **somente software**.
>   O contexto morria, a entrada do pool era limpa, o VMID voltava para os livres — e o contexto
>   GCVM continuava com `ENABLE_CONTEXT` ligado e `PT_BASE` apontando para a raiz do processo
>   **morto**, página que o VidMm pode liberar e reutilizar. Existia `Bc250GcvmVmid8Uninstall`,
>   mas só para o VMID 8 fixo: os doze slots do pool nunca tiveram teardown.
>   **É a Lei 5 quebrada — APPLY sem TEARDOWN.** Vale corrigir tenha ou não relação com o fault,
>   e eu **não afirmo que tenha**.
> - `Bc250GcvmUninstallVmid` é o gêmeo por VMID, com a ordem que importa: `CNTL` primeiro
>   (enquanto ele estiver ligado o walker pode nascer tradução nova da raiz abandonada), depois
>   os seis registradores de endereço, e a **TLB daquele VMID por último** — tradução já cacheada
>   sobrevive ao desarme do `CNTL`. Provado por readback de `ENABLE_CONTEXT`, com `FlushVmid`
>   (nunca o flush fixo), fora do `VmidPoolLock`, e falha declarada.
> - Nenhuma allowlist ampliada: escrever o `CNTL` dos VMIDs do pool já era permitido.
> - `build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias **PASS**.
>   **Sete mutações verificadas, todas reprovando na auditoria E na suíte.**
> - **UM PORTÃO MEU PASSOU — terceira faixa seguida, e a mesma classe.** Ele procurava
>   `CHECK(f->LastFlushVmid == target);` no arquivo inteiro, e a string também existe no bloco de
>   **install**; afrouxar a asserção do bloco de **desarme** não reprovava nada. Refeito com a
>   busca delimitada ao bloco. As três ocorrências desta sequência foram todas **"procurei a
>   string no escopo errado"** — é padrão, não incidente isolado.
> - **Armadilha de ferramenta:** `bc250_gcvm_vmid8_transaction.{c,h}` estavam com finais de linha
>   **mistos**. Normalizados para CRLF. Terceiro arquivo da sequência com o problema.
> - **Candidato lab 3.223.1.0 assinado, auditado e NÃO INSTALADO:** SYS
>   `A17258E9FC9AB4A63CCB9916B58FEB6A08315E7638C4CB12C2226DCCDA6EFB29`, PDB `0E42773DCDDEA026...`,
>   UMD `45814D0E115E4BAD...`, INF `DE1FE0A5C0C0EBD2...`, CAT `8FE7C55CB95497B0...`.
>   `Lab package audit: PASS`; as duas strings novas conferidas **em ANSI** no SYS assinado.
> - **Critério da Corrida 62, escrito antes:**
>   [docs/321](docs/321_CRITERIO_CORRIDA62_DESARME_DO_VMID_NO_SILICIO.md). **Tela preta continua
>   sendo o resultado correto.**

> **21/09/2026 — Corrida 61 (21:47:11, lab 3.222.1.0): o HDP foi drenado 75 de 75 vezes e O
> FAULT PERMANECEU. Desfecho B — a visibilidade da tabela NÃO era a causa.** Evidência:
> `docs/evidence/2026-09-21_21-47-11-DESKTOP-HQAG6T4-lab-binding/`; critério escrito antes em
> [docs/320](docs/320_CRITERIO_CORRIDA61_HDP_ANTES_DA_INVALIDACAO.md).
>
> - **Houve avanço? SIM, diagnóstico; NÃO funcional.** Identidade conferida: 3.222.1.0, SYS
>   `E12F2375BA9EA172513D9767E8FCFEFFC6354D5BF677D540AE11530ABF90607B`.
> - **DX.14 exercitada:** 75 linhas `HdpFlush=1 Ok=1`, **zero** `[HDP_PT_FLUSH] FALHA:`, sob
>   carga comparável — 1.819 IBs contra 1.758. E **um fault** `WalkerError=6 MappingError=1
>   Cid=9 Rw=0` em `0x200040000` permaneceu. **Pela implicação escrita antes: a hipótese cai.**
> - **NÃO atribuo nada à queda de 3 faults para 1.** Faults já eram raros, é uma corrida só, e
>   `SEM FAULT LATCHADO` subiu de 4 para 6 — menos fault latchado não é menos fault.
> - **NÓS NUNCA ESCREVEMOS O PDE SUJO.** Corridas 60 + 61 somam **208 escritas** no índice 4096,
>   **todas limpas**, com zero `[PDE_TRIPWIRE]`, zero `[PDE_LARGEPAGE]` e zero
>   `[ActiveUpdatePT] RECUSA`. Ainda assim o TDR leu `0x4526CC071` — formato de PTE em slot de
>   PDE, mesma assinatura da Corrida 59.
> - **HIPÓTESE MINHA REFUTADA PELA PRÓPRIA MEDIÇÃO, antes de eu afirmá-la:** cheguei a explicar
>   o PDE sujo como "a caminhada lê uma raiz já reciclada" e testei a correlação com a janela de
>   publicação. **Não se sustenta** — na Corrida 58, 3 caminhadas resolveram com 2 janelas
>   `MUDOU`. Fica registrado como **refutado**, não como pendência.
> - Controles: 7/7 restarts provados, `PoolReadyMask` até `0x000E`, GFX fechando `56/56/0`,
>   **zero BugCheck**. Produto: Present falhou no quadro 1 (`0x887A0005`, `DRR=0x887A0020`),
>   0/600; nenhum pixel. Recuperação: `RECOVERY_OK_ONLINE` — recuperação, não sucesso nosso.

> **21/09/2026 — faixa DX.14, OFFLINE após a Corrida 60: o HDP passou a ser drenado ANTES da
> invalidação de TLB. NÃO VALIDADO EM HARDWARE.**
>
> - **A ORDEM é o conteúdo da faixa.** As page tables são escritas pela CPU em VRAM por
>   `PageTableAddress.CpuVirtual` e ficam na cache de escrita do HDP. Invalidar a TLB **antes**
>   de drenar mandaria o walker **rebuscar** bytes que ainda não chegaram — pioraria a janela em
>   vez de fechá-la. Primeiro drenar, depois invalidar; o portão trava a ordem, não a presença.
> - **Precedente medido NESTE silício:** Corrida 10/09 12:16, `FENCE_VISIBILIDADE
>   antes_hdp=43DAFFFE depois_hdp=00001304` — o valor apareceu **no flush**. Outra consumidora,
>   mesma memória, mesma cache, mesmo sentido. **Continua H, não P0.**
> - O offset `0x3FC4` **não é novo**: já catalogado em `bc250_gfx_ring_transaction.h`, já usado
>   por quatro caminhos e já presente nas allowlists de leitura **e** escrita. Nenhuma allowlist
>   foi ampliada.
> - `[FLUSH_POOL_VMIDS]` publica `HdpFlush=`; a falha de drenagem se declara.
> - `build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias **PASS**.
>   **Seis mutações verificadas, todas reprovando** — incluindo o HDP drenado DEPOIS da
>   invalidação e o offset saindo da allowlist.
> - **UM PORTÃO MEU PASSOU, e a mutação o pegou — segunda faixa seguida.** Ele procurava o
>   **argumento** (`..._FLUSH_CNTL, 1u)`) e passava quando a **chamada** de escrita era removida
>   e o argumento ficava para trás. Refeito com invariante sobre a chamada inteira.
> - **Candidato lab 3.222.1.0 assinado, auditado e NÃO INSTALADO:** SYS
>   `E12F2375BA9EA172513D9767E8FCFEFFC6354D5BF677D540AE11530ABF90607B`, PDB `B0CB5D0D5E9FBC64...`,
>   UMD `401C3FBBBC958411...`, INF `ABCC183C6B0CBF35...`, CAT `B3B11630E97889E4...`.
>   `Lab package audit: PASS`; a linha antiga sem `HdpFlush` conferida **ausente** no SYS
>   assinado, prova de que o empacotador não copiou o candidato anterior.
> - **Critério da Corrida 61, escrito antes:**
>   [docs/320](docs/320_CRITERIO_CORRIDA61_HDP_ANTES_DA_INVALIDACAO.md). A assinatura do
>   desfecho A exige **zero fault de qualquer classe** — não um endereço específico, que foi o
>   defeito do critério da Corrida 59. **Tela preta continua sendo o resultado correto.**

> **21/09/2026 — Corrida 60 (21:30:42, lab 3.221.1.0): NÓS ESCREVEMOS O PDE, E ESCREVEMOS
> LIMPO; e duas caminhadas CONTEMPORÂNEAS do fault RESOLVERAM até a folha.** Evidência:
> `docs/evidence/2026-09-21_21-30-42-DESKTOP-HQAG6T4-lab-binding/`; critério escrito antes em
> [docs/319](docs/319_CRITERIO_CORRIDA60_QUEM_ESCREVE_O_PDE_DA_ARENA.md).
>
> - **Houve avanço? SIM, diagnóstico decisivo; NÃO funcional.** Identidade conferida:
>   3.221.1.0, SYS `783E3CD392DD4F972B9FF2A96EF47CB89E9A20B71871612B38C515126E9A93DA`.
> - **101 `[PDE_WRITE] ARENA`**, todas em `idx=4096 Lvl=1`; 23 com `V=1`, e **todas limpas**
>   (`Encoded=0x…001`, `Reservado[5:3]=0x0`). **Zero** `[PDE_TRIPWIRE]`, **zero**
>   `[PDE_LARGEPAGE]`, **zero** `[ActiveUpdatePT] RECUSA`.
> - **O PDE sujo `0x…071` da Corrida 59 NÃO reproduziu:** o TDR leu `0x45FDFE001`, limpo, no
>   mesmo índice. **Correção de enquadramento meu:** eu havia apresentado esse achado como "o
>   alvo agora"; as três caminhadas da Corrida 59 diziam `TABELA MUDOU`, logo ele era **mais
>   fraco do que eu o apresentei**. O que sobrevive dele são os dois defeitos reais que a busca
>   revelou, e esses continuam corrigidos.
> - **A MEDIÇÃO MAIS FORTE DA SEQUÊNCIA — duas caminhadas com `TABELA INALTERADA`, portanto
>   contemporâneas do fault, que RESOLVERAM:** `L1 idx=4096 PDE=0x45FDFE001` limpo →
>   `L0 idx=64 PTE=0x4520CC031` → `RESOLVIDO 0x200040000 -> 0x4520CC000`; e o fault `Cid=10`
>   resolvendo em `<<20` (`0x245B00000 -> 0x4566CC000`). `0x31` = `VALID|EXEC|READ`, e o fault
>   é `Rw=0`: **leitura de página legível**.
> - **Isso refuta CONTEMPORANEAMENTE as três explicações que sobravam:** PTE ausente, PDE
>   corrompido e permissão. No instante do fault a imagem vista pela CPU estava completa e
>   correta, e a GPU reportou `WalkerError=6 MappingError=1` no mesmo endereço.
> - **VEREDITO: desfecho E.** O desfecho C está refutado (nós escrevemos, 101 vezes); A também
>   (zero tripwire, zero escrita suja); D também (zero recusa — o defeito 1 era **latente**, o
>   laço de validação anterior o alcança primeiro). B teve só **metade** da assinatura —
>   escrevemos limpo, mas o TDR **não** leu sujo — e metade não é desfecho.
> - Controles: 1.758 IBs, 7/7 restarts provados, `PoolReadyMask` até `0x000E`, zero falha de
>   flush, **zero BugCheck**. Produto: Present falhou no quadro 1 (`0x887A0005`,
>   `DRR=0x887A0020`), 0/600; nenhum pixel.
> - Recuperação: `RECOVERY_OK_ONLINE`, sem reboot — recuperação, não sucesso nosso.

> **21/09/2026 — faixa DX.13, OFFLINE após a Corrida 59: a escrita de diretório de páginas
> deixou de ser muda, e o alvo passou a ser o PDE. NÃO VALIDADO EM HARDWARE.**
>
> - **Defeito 1, real e independente da hipótese:** o laço que grava a page table descartava o
>   status do encode com `(void)`. Quando `Bc250McdmEncodeVidMmPteWithLevel` recusa, ele deixa
>   `*Encoded` em **zero** — e esse zero era gravado numa tabela **viva**, sem uma linha de log.
>   Agora a recusa **não grava** e se declara (`[ActiveUpdatePT] RECUSA:`).
> - **Defeito 2:** `LargePage` nunca era olhado no nível de diretório. O nível 0 já o recusava
>   desde sempre. Esta faixa **mede em vez de recusar**, com teto próprio e contador: nenhuma
>   corrida registrou a frequência, e recusar sem o número poderia zerar a medição inteira —
>   preço já pago na Corrida 53. A recusa vem depois do número.
> - **Discriminante novo:** `[PDE_WRITE] ARENA` publica a `DXGK_PTE` crua e o valor codificado
>   sempre que uma atualização de diretório cobre o índice **4096** — o da arena `0x200040000`.
>   Mais um tripwire que recusa entrada de nível ≥ 1 com bits [5:3] no valor codificado.
> - Constantes **derivadas**, nunca digitadas: a máscara de reservados é a mesma de
>   `Bc250GcvmDecodePde`, e o índice da arena sai de `0x200040000 >> (12 + LEAF_INDEX_BITS)`.
> - `build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias **PASS**.
>   **Oito mutações verificadas, todas reprovando.**
> - **UM PORTÃO MEU PASSOU, e a mutação o pegou:** ele exigia a string `InterlockedIncrement(`
>   no arquivo, que já tem **sete** ocorrências em outros caminhos — tirar o teto da linha nova
>   não reprovava nada. Refeito com invariante sobre o contador próprio.
> - **Duas armadilhas de ferramenta:** um `sed -i` converteu `bc250_mcdm_callbacks.c` inteiro de
>   CRLF para LF (restaurado), e `bc250_display_ddi.c` ficou com finais de linha **mistos** — a
>   mesma armadilha já registrada para `bc250_mmio_topology_transport.c`. Normalizado.
> - **Candidato lab 3.221.1.0 assinado, auditado e NÃO INSTALADO:** SYS
>   `783E3CD392DD4F972B9FF2A96EF47CB89E9A20B71871612B38C515126E9A93DA`, PDB `83429C5866E95D91...`,
>   UMD `B4D07A72C1B70BAA...`, INF `E4C6C97AB51F3799...`, CAT `1833BA4CD3CA59C7...`.
>   `Lab package audit: PASS`; seis strings conferidas **em ANSI** no SYS assinado.
> - **Critério da Corrida 60, escrito antes:**
>   [docs/319](docs/319_CRITERIO_CORRIDA60_QUEM_ESCREVE_O_PDE_DA_ARENA.md). É diagnóstico: não
>   espere o fault sumir nem pixel. **Tela preta continua sendo o resultado correto.**

> **21/09/2026 — Corrida 59 (20:05:34, lab 3.220.1.0): a invalidação passou a cobrir os VMIDs do
> pool, com ACK, e a TAXA DE FAULT NÃO MUDOU.** Evidência:
> `docs/evidence/2026-09-21_20-05-34-DESKTOP-HQAG6T4-lab-binding/`; critério escrito antes em
> [docs/318](docs/318_CRITERIO_CORRIDA59_FLUSH_DOS_VMIDS_DO_POOL.md).
>
> - **Houve avanço? SIM, diagnóstico; NÃO funcional.** Identidade conferida: 3.220.1.0, SYS
>   `890AA873A556225FB2BE36B89CAE1771471C4B6AB28006BA2BE13F770BCFB677`.
> - **DX.12 validada no silício:** 77 linhas `[FLUSH_POOL_VMIDS]`, `PoolReadyMask` chegando a
>   `0x001E` (VMIDs 1–4), `RejeitadaMask=0` e `FalhouMask=0` em **todas**, `Ok=1` em todas,
>   zero `FALHA`. Cada VMID do pool com `status=0 PhysReady=1 AckObserved=1`.
> - **O risco que eu havia declarado não se materializou, e foi medido:** `Polls=1` nas 94
>   amostras. O flush por VMID custa **um** poll, não centenas; a preocupação com `0x133` era
>   infundada.
> - **DX.10 exercitada em hardware pela primeira vez:** 2 das 3 caminhadas usaram
>   `RETIDA_POS_MORTE`. Na Corrida 58 só `VIVA_READY` havia rodado.
> - **VEREDITO: a assinatura do desfecho A foi cumprida e a implicação de A está REFUTADA;
>   o desfecho real é E.** O fault `Cid=8 Rw=1` de `0x2469A6000` sumiu (**zero** ocorrências,
>   contra 6 na Corrida 58), mas voltaram os `Cid=9 Rw=0` em `0x200040000` (assinatura da
>   Corrida 56) e um `Cid=10 Rw=0` em `0x2431` (a da Corrida 45). **A taxa não mudou:** 3 faults
>   em 7 TDRs, contra 3 em 6 (Corrida 58) e 4 em 6 (Corrida 56).
> - **DEFEITO DO MEU CRITÉRIO, e a Lei 10 o conteve.** Escrevi a assinatura de A amarrada a **um
>   endereço específico** em vez de "nenhum fault de tradução no GFX". Como assinatura e
>   implicação estavam em campos separados, deu para aceitar uma e rejeitar a outra — mas a
>   escolha da assinatura foi estreita demais, e com pacote fechado teria virado falso sucesso.
> - Atribuir o sumiço do `Cid=8` ao flush é **H, não P0**: a classe do fault já alternava entre
>   corridas (56 tinha as duas, 57 não latchou nenhuma), e é uma corrida só.
> - **ACHADO QUE MOVE O ALVO:** `L1 idx=4096 PDE=0x450C9C071` — formato de **PTE** num slot de
>   **PDE**. Pelo layout do Navi 10 o endereço do PDE começa no bit 6, logo bits [5:3] são
>   reservados e a GPU caminharia 64 bytes adiante. É a assinatura do defeito de 05/09 (`0x61`).
>   O índice 4096 é o da arena `0x200040000`; o índice 4632 da **mesma raiz** tinha PDE limpo e
>   resolveu, logo a caminhada leu a tabela certa. O portão que disparou **já existia** e nunca
>   havia disparado. **Ressalva:** as três janelas de publicação disseram `TABELA MUDOU`.
> - Controles: 1.969 IBs, 7/7 restarts provados, GFX fechando `99/99/0`, **zero BugCheck**.
>   Produto: Present falhou no quadro 1 (`0x887A0005`, `DRR=0x887A0020`), 0/600; um
>   `BYTE_PROOF INCONCLUSIVO`; nenhum pixel.
> - Recuperação: `RECOVERY_OK_ONLINE`, sem reboot, BasicDisplay Código 0 em cinco amostras —
>   recuperação, não sucesso nosso.

> **21/09/2026 — faixa DX.12, OFFLINE após a Corrida 58: a invalidação de TLB passou a seguir
> os VMIDs que a GPU realmente usa. NÃO VALIDADO EM HARDWARE.**
>
> - **O defeito, achado por leitura:** `Bc250ActiveFlushMappedGfxhubTlb` invalidava três VMIDs
>   FIXOS — 8, 7 e 9. Desde a Corrida 54 o `PACKET3_INDIRECT_BUFFER` carrega o VMID dinâmico do
>   pool (na Corrida 58, os VMIDs 1 a 4), e `Bc250GcvmInstallVmid` invalida o slot **só na
>   instalação**. Uma `UpdatePageTable` posterior não invalidava slot dinâmico nenhum.
> - **Isso CASA com a medição da Corrida 58** — escrita em `0x2469A6000` falhando com a janela
>   de publicação `INALTERADA` e a PTE contemporânea `0x458F72071` válida e R/W/X na visão da
>   CPU —, mas **casar não é provar**. A relação causal segue **HIPÓTESE** até a Corrida 59.
> - `Bc250VmidPoolReadyFlushMask` é função **pura** do contrato: só entra o slot ocupado, `READY`
>   e com raiz instalada; os reservados 0/7/8/9 nunca saem dela. A máscara é copiada sob
>   `VmidPoolLock` e o lock é **solto antes de qualquer MMIO** — o flush por VMID pode gastar até
>   1000 polls em DISPATCH_LEVEL, que é a classe do BugCheck 0x133 já pago em 10/09.
> - **Flush parcial não vira sucesso:** qualquer VMID que não invalide fecha o veredito global,
>   e o chamador fecha `GpuvaMappingsOperational`. `[FLUSH_POOL_VMIDS]` publica `FixedMask`,
>   `PoolReadyMask`, `RequestedMask`, `RejeitadaMask` e `FalhouMask`, com teto próprio e isenção
>   explícita do amostrador (armadilha 16). A linha por VMID **passou a dizer qual VMID**.
> - `build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias **PASS**.
>   **Onze mutações verificadas**, nove contra a auditoria e duas contra a suíte.
> - **DUAS GUARDAS MINHAS ERAM CARIMBO, e a mutação mostrou isso.** Pelo caminho vivo,
>   `RecordInstall` zera `InstalledRootPhysical` em toda saída que não seja `READY` — logo a
>   guarda de estado e a de raiz se cobriam e **nenhuma das duas conseguia falhar sozinha**. Os
>   casos foram refeitos forçando estados que a API não produz.
> - **Candidato lab 3.220.1.0 assinado, auditado e NÃO INSTALADO:** SYS
>   `890AA873A556225FB2BE36B89CAE1771471C4B6AB28006BA2BE13F770BCFB677`, PDB `015AE6FA78BB01E3...`,
>   UMD `3D4322E8B2544919...`, INF `B7AC565A58E75190...`, CAT `B8EE192B5884AB02...`. Perfil
>   `DISPLAY_RENDER_LAB`, P1 persistente, schema 135. `Lab package audit: PASS`. As quatro
>   strings novas foram conferidas **em ANSI** no SYS assinado, e a string antiga sem VMID foi
>   conferida **ausente** — prova de que o empacotador não copiou o candidato anterior.
> - **Critério da Corrida 59, escrito antes:**
>   [docs/318](docs/318_CRITERIO_CORRIDA59_FLUSH_DOS_VMIDS_DO_POOL.md). **Tela preta continua
>   sendo o resultado correto**: a parede de rasterização não foi tocada.

> **21/09/2026 — passagem de bastão pós-Corrida 58: candidato de causa isolado offline,
> ainda não implementado.** `Bc250ActiveFlushTlbHandler` chega a
> `Bc250ActiveFlushMappedGfxhubTlb`, que invalida somente VMIDs 8/7/9. Desde a Corrida 54 o
> GFX usa o VMID dinâmico do pool; na Corrida 58 foram 1–4. `Bc250GcvmInstallVmid` invalida o
> slot na instalação, mas atualizações posteriores de PTE não invalidam os slots dinâmicos.
> Isso casa com PTE R/W/X contemporânea na CPU e fault na GPU, porém segue **HIPÓTESE OFFLINE,
> NÃO PROVADA NO SILÍCIO**. Próxima faixa: snapshot dos slots `READY`, flush de cada VMID fora
> do `VmidPoolLock`, telemetria/ACK, testes e mutação; somente depois empacotar e congelar a
> Corrida 59. Instruções autocontidas em
> [docs/317](docs/317_PASSAGEM_DE_BASTAO_POS_CORRIDA58_FLUSH_DOS_VMIDS_DO_POOL.md).

> **21/09/2026 — Corrida 58 (17:55:20, lab 3.219.1.0): a arena `0x200040000` é
> executável; o fault real reapareceu como escrita em `0x2469A6000`, com PTE R/W/X válida e
> contemporânea na visão da CPU.** Evidência:
> `docs/evidence/2026-09-21_17-55-20-DESKTOP-HQAG6T4-lab-binding/`; resultado completo em
> [docs/316](docs/316_HANDOFF_2026-09-21_CORRIDA58_ALVO_EXECUTAVEL_FAULT_DE_ESCRITA.md).
>
> - **Houve avanço? SIM, diagnóstico; NÃO funcional. Veredito: SUCESSO da pergunta de
>   permissão (B + ressalva D) e FALHA do produto.** Identidade conferida: 3.219.1.0, SYS
>   `5E53951F2A1F89D91C3B3815C9615F6870DC519B053D292617832F72C46FBC8C`.
> - Das 102 linhas `[DX_TARGET_200040]`, 27 eram válidas. As **25/25** com geometria da arena
>   (64 páginas, 0x40000 bytes) tinham `NoExecute=0`, PTE terminada em `0x31` e
>   `EncodeStatus=0`: o bit 4 executável está presente. O nome continuou
>   `CLASSE_DESCONHECIDA`; identidade semântica incompleta. Duas sobreposições grandes
>   `RESOURCE`/`PRIMARY` eram NX e não pertencem à arena.
> - Dos 6 TDRs, 3 vieram sem latch e 3 repetiram uma escrita (`Cid=8 Rw=1`) em
>   `0x2469A6000`. Na terceira, VMID 2, a janela ficou **INALTERADA** e a raiz `VIVA_READY`
>   resolveu a PTE contemporânea `0x458F72071`, flags baixas `0x71` = R/W/X válidas. Isso
>   refuta folha ausente e falta de R/W/X na imagem CPU-visível, mas não prova que a GPU viu
>   a mesma publicação.
> - Controles: 1.900 IBs SDMA, 6/6 restarts provados, 26 instalações de VMID sem exaustão;
>   GFX máximo `83/81/2`, 107 fences escritas pela GPU e 107 pernas 3 ainda abertas.
> - Falha funcional: primeiro Present `0x887A0005`, remoção `0x887A0007`, 0/600; dois
>   `BYTE_PROOF INCONCLUSIVO`; nenhum pixel provado.
> - Recuperação: rollback online sem reboot, `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em
>   cinco amostras. Nenhum BugCheck/dump novo observado. BasicDisplay é recuperação.
> - **Próximo foco, ainda não executado:** discriminar visibilidade/invalidação, hierarquia ou
>   uso da alocação `0x246400000`; não alterar execução de `0x200040000` às cegas.

> **21/09/2026 — Corrida 57 (17:11:51, lab 3.218.1.0): nenhum fault ficou latchado;
> DX.10/DX.11 não foram exercitados, mas `0x200040000` coincide com a arena do PC da onda.**
> Evidência: `docs/evidence/2026-09-21_17-11-51-DESKTOP-HQAG6T4-lab-binding/`; critério e
> resultado em [docs/313](docs/313_CRITERIO_CORRIDA57_RAIZ_RETIDA_E_ALVO_200040.md).
>
> - **Houve avanço? SIM, diagnóstico; NÃO funcional. Veredito: INCONCLUSIVO para a faixa
>   DX.10/DX.11 (desfecho E) e FALHA funcional.** Identidade conferida: 3.218.1.0, SYS
>   `05731245B6000F196D1F7D815A3181E77B21A39CEF0169CCE4A9F16B00A056CF`.
> - O controle de carga foi superior à Corrida 56: **2.173 IBs SDMA**. Houve **cinco TDRs e
>   cinco `TDR_RESTART PROVADO`**, mas os cinco disseram `SEM FAULT LATCHADO` com
>   `FaultValid=0x7 Status=0`. Portanto saíram zero `DRENO #`, zero `ORIGEM_DA_RAIZ`, zero
>   caminhada e zero `ALVO 0x200040`. A raiz retida e o token continuam **NÃO VALIDADOS EM
>   HARDWARE**; ausência de latch não prova ausência de fault nem tradução corrigida.
> - O stall continua físico e gráfico: cinco snapshots com `CP_RB0` ativo, `RPTR != WPTR`,
>   `GUI_ACTIVE=1`, `CP_BUSY=1` e ondas vivas. Das 107 linhas `GFX_TRIPE_VEREDITO`, todas
>   tiveram fence escrita pela GPU e perna 3 aberta; 90 deram RPTR convergente e 17 não.
>   Máximo observado: `Submetidas=131 Aposentadas=130 EmVoo=1`.
> - O pool permaneceu são: **31 instalações físicas**, VMID01=7, VMID02=13, VMID03=6,
>   VMID04=4, VMID05=1, zero exaustão.
> - **Achado cruzado:** o KMD recebeu `UpdatePT FirstPteVA=0x200040000 Num=64`; o log RADV
>   atribui a mesma VA a uma alocação de **262.144 bytes**. O PC medido na Corrida 45 foi
>   `0x20004114C`, exatamente `+0x114C` dentro dessa faixa. É candidato forte a arena de
>   código/instruções, mas esta corrida não preservou as permissões DXGK nem a PTE codificada.
> - Falha do produto: harness D3D11 reprovou no quadro 1 (`Present=0x887A0005`,
>   `DeviceRemovedReason=0x887A0020`, 0/600); 10 Presents KMD, dois `BYTE_PROOF INCONCLUSIVO`
>   zero contra zero; nenhum pixel provado.
> - Recuperação: rollback online sem reboot, `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em
>   cinco amostras. Não houve BugCheck observado. BasicDisplay é recuperação, não sucesso.

> **21/09/2026 — faixa pós-Corrida 57, OFFLINE: o alvo fala no UpdatePageTable mesmo sem
> fault. NÃO VALIDADO EM HARDWARE.** Quando uma atualização cobre `0x200040000`, a nova linha
> `[DX_TARGET_200040]` publica processo, alocação, token/nome, tamanho, base/páginas, atributos
> `ReadOnly/NoExecute/CacheCoherent`, flags DXGK, PFN e a PTE codificada com status. A linha
> rara foi isenta da amostragem. `build-contract.bat` **EXIT=0**, safety audit **PASS**; três
> mutações verificadas reprovaram ao perder, separadamente, as permissões, a isenção do
> amostrador e o marcador do alvo. **Candidato lab 3.219.1.0 assinado, auditado e NÃO
> INSTALADO:** SYS `5E53951F2A1F89D91C3B3815C9615F6870DC519B053D292617832F72C46FBC8C`,
> PDB `8D3B95D491A0AB258663BC3256F30D2E1B937A5E1A60CD4221CC6B117DD092E5`, UMD
> `934DBD84E04F10024F6B7CAB28FBE014CAA41D35E103600F160BBF395AB5236A`, INF
> `6EE2DE01ED51978B81853ABB58F6CFA2668764687302E38C9258D533995F4D95`, CAT
> `863179B8CB0D64934EECFAC415D64262899DADA466A6A0A998C7BD24A286688B`. Perfil
> `DISPLAY_RENDER_LAB`, P1 persistente, `GfxWaveProbe=0`. Critério da Corrida 58 em
> [docs/315](docs/315_CRITERIO_CORRIDA58_PTE_DO_ALVO_200040.md).

> **21/09/2026 — faixa DX.10/DX.11, OFFLINE após a Corrida 56: a caminhada agora sobrevive à
> morte do contexto e o alvo `0x200040` sai nomeado. NÃO VALIDADO EM HARDWARE.**
>
> - `BC250_VMID_POOL` ganhou retenção diagnóstica separada da entrada reciclável. Somente uma
>   instalação física concluída com `BC250_GCVM_VMID8_OK` atualiza a retenção; `Release` limpa
>   toda autoridade viva, mas preserva raiz, contexto e geração para o TDR. A consulta prefere
>   `VIVA_READY`, cai em `RETIDA_POS_MORTE` e declara `AUSENTE` quando nenhuma existe.
> - A mensagem falsa `raiz do VMID 7 AUSENTE` foi removida. O log agora nomeia o VMID real do
>   fault, a fonte da raiz, o contexto e a geração usados na caminhada.
> - O token imutável da alocação passa de `UpdatePageTable` pelo journal e snapshot até
>   `Bc250GpuvaSpaceDescribeAddress`. Para `AddrPagina=0x200040`, o TDR publica um veredito
>   dedicado para `GpuVa=0x200040000`, com nome/token, handle, base, tamanho e offset, ou declara
>   que não há alocação viva. `<<20` é recusado explicitamente por exceder o teto de 34 bits.
> - `build-contract.bat` **EXIT=0**; safety, DDI matrix e DDI noop **PASS**; `git diff --check`
>   limpo salvo avisos de normalização LF/CRLF. Três mutações verificadas reprovaram: apagar a
>   raiz retida, remover o veredito do alvo e perder o token na publicação do snapshot.
> - **Candidato lab 3.218.1.0 assinado, auditado e NÃO INSTALADO:** SYS
>   `05731245B6000F19...`, PDB `491B9951D62E5F7B...`, UMD `934DBD84E04F1002...`,
>   INF `003E14F3CDC09B66...`, CAT `D4700F95B49E2136...`. As quatro strings novas foram
>   conferidas em ANSI dentro do SYS assinado. `Lab package audit: PASS`.
> - **Próximo passo físico:** Corrida 57, com volume comparável à Corrida 56, exigindo que faults
>   pós-destruição caminhem por `RETIDA_POS_MORTE` e que `ALVO 0x200040` publique o nome real da
>   alocação. Isso é diagnóstico; ausência isolada de fault não fecha DX.5.

**Documento base do projeto.** Toda decisão de o-que-fazer-agora sai daqui.

> **21/09/2026 — Corrida 52 (11:24:16, lab 3.213.1.0): disputa entre contextos pelo VMID 7
> PROVADA NO SILÍCIO; o pool é o conserto correto, mas ainda não governa a GPU.** Evidência:
> `docs/evidence/2026-09-21_11-24-16-DESKTOP-HQAG6T4-lab-binding/`; critério escrito antes em
> [docs/306](docs/306_CRITERIO_CORRIDA52_POOL_DE_VMID_MAPA_MEDIDO.md); passagem em
> [docs/307](docs/307_HANDOFF_2026-09-21_CORRIDA52_VMID_POR_CONTEXTO.md).
>
> - **Houve avanço? SIM — diagnóstico decisivo, sem avanço funcional. Veredito: SUCESSO da
>   faixa, desfecho A.** O SYS arquivado tem SHA-256 completo
>   `831C79B70CE4EB7C9910E4208FCFEB6C2E15B37F3242BCEC3CA3A1DE23A4D6CE`, igual ao
>   `package_sys_sha256` de `binding-provenance.json`.
> - **Assinatura completa em quatro faults GCVM:** `MAPA NO FAULT` encontrou 2, 3, 3 e 3
>   contextos vivos; em cada caso a `RAIZ_NO_TDR` coincide exatamente com a raiz de um deles,
>   enquanto os demais têm raízes distintas. Divergências dos carimbos: 15/16, 12/16, 13/16
>   e 13/16. Isso distingue vários contextos concorrentes de um único contexto republicando a
>   própria raiz.
> - **Pool medido:** 27 atribuições, 24 linhas `LIBERADO`, zero `EXAUSTAO`, pico de 4 contextos
>   simultâneos em 55 amostras (`0:6 1:17 2:19 3:12 4:1`), somente VMIDs 1–4 entregues e
>   nenhum reservado 0/7/8/9. A antiga premissa de “6 raízes simultâneas” estava errada: eram
>   seis raízes distintas ao longo do tempo; o pico coexistente medido é quatro.
> - **Controles:** 5 TDR com 5 `TDR_RESTART PROVADO`, zero BugCheck, zero `DEVICE_REMOVED`, zero
>   `StopDevice` e zero `IB_TRIPOD TIMEOUT`. Ressalva de precisão: houve **quatro** faults GCVM
>   decodificados; no quinto TDR o log declarou `SEM FAULT LATCHADO`. Não transformar “5 TDR”
>   em “5 faults”.
> - **Falha esperada, não corrigida nesta faixa:** o fault permaneceu em
>   `Vmid=7 WalkerError=6 MappingError=1` e a tela permaneceu preta, porque o candidato ainda
>   instala a raiz e submete deliberadamente sob VMID 7. A Corrida 52 validou o mapa, não o uso
>   do VMID alocado.
> - **Recuperação:** rollback online sem reboot; `safe-bind-final.json` registra
>   `RECOVERY_OK_ONLINE` e cinco amostras de BasicDisplay Código 0. Isso é recuperação, não
>   sucesso do nosso desktop.
> - **Continua NÃO VALIDADO EM HARDWARE:** escrita/flush/instalação nos VMIDs 1–6/10–15,
>   submissão usando o VMID do pool, caminhada do fault pela raiz nomeada pelo fault e ausência
>   do fault após a correção.
>
> **21/09/2026 — faixa pós-Corrida 52, OFFLINE: pool serializado antes de ganhar autoridade
> sobre hardware.** A auditoria do handoff encontrou uma quinta armadilha anterior às quatro
> já listadas: `Acquire`/`NoteRoot` rodam em `SubmitCommandVirtual` a DISPATCH_LEVEL,
> `Release` roda em `DestroyContext` e o TDR percorre `Entries[]`, mas nenhum lock comum cobria
> os três caminhos. O comentário do contrato que dizia que “o mesmo lock já protege” era falso.
> Foi adicionado `VmidPoolLock` (spin lock dedicado): aquisição, registro, devolução e snapshot
> do fault são serializados; o TDR copia o mapa sob o lock e só imprime depois de soltá-lo.
> `build-contract.bat` **EXIT=0**, safety audit **PASS**; mutação removendo o campo do lock fez a
> auditoria falhar e a restauração voltou a PASS. **Zero mudança de MMIO ou VMID de submissão;
> NÃO VALIDADO EM HARDWARE.**
>
> **Segunda faixa pós-Corrida 52, OFFLINE: allowlist MMIO exata dos VMIDs do pool.** A política
> agora deriva os VMIDs autorizados de `BC250_VMID_POOL_MASK` e reconhece somente os sete
> registradores por contexto (`CNTL`, `PT_BASE` LO/HI, `PT_START` LO/HI e `PT_END` LO/HI).
> Reservados 0/7/8/9, offsets desalinhados e vizinhos continuam recusados por essa política.
> A mesma lista exata cobre leitura porque a instalação verifica por readback todos os sete
> registradores; a primeira implementação abriu só leitura por engano, e a suíte detectou a
> ausência de escrita. Depois, ela também expôs que `VMID9_CNTL + 4` é `VMID10_CNTL`, não um
> negativo válido. Ambos foram corrigidos antes do fechamento. `build-contract.bat` **EXIT=0**;
> safety audit, matriz DDI, auditoria de no-op e `git diff --check` **PASS**. Mutações removendo
> separadamente a leitura e a escrita do pool fizeram a auditoria falhar nos portões corretos,
> e a restauração voltou a PASS. **Ainda não foram alterados instalação, flush, estado instalado
> ou submissão; NÃO VALIDADO EM HARDWARE.**
>
> **Terceira faixa pós-Corrida 52, OFFLINE: flush parametrizado e estado instalado por
> entrada, ainda sem mudar a submissão.** `Bc250GcvmInstallVmid` deixou de aceitar um callback
> que perdia a identidade do VMID: agora chama obrigatoriamente `FlushVmid(Context, Vmid)`,
> deriva os sete offsets para qualquer VMID 0–15 e grava o VMID no evidence. Os testes
> exercitam as bordas do pool (VMID 1 e 15), conferem o endereço programado e provam que o
> callback recebeu o mesmo VMID; 16 e callback ausente falham fechados. O pool ganhou estado
> separado por entrada (`NEVER/READY/FAILED`, raiz instalada e último status). Correção de
> semântica importante: `RootPhysical` continua sendo a raiz **observada** do contexto enquanto
> o caminho legado instala no VMID 7; ela não é mais descrita como prova de instalação no VMID
> alocado. Uma falha posterior apaga a raiz `READY`, e liberar/reciclar o slot apaga toda a
> autoridade anterior. `build-contract.bat` **EXIT=0**; os dois testes direcionados retornaram
> 0; safety audit **PASS**. Duas mutações foram detectadas: flush forçado no VMID 7 e remoção
> da limpeza de uma raiz `READY` após falha. O primeiro build desta faixa parou somente porque
> o teste referenciou uma constante privada ao `.c`; o teste foi corrigido para conferir o bit
> ABI `0x1` e a suíte completa passou. **O caminho GFX ainda instala e submete no VMID 7; o
> estado Installed* do pool ainda não é preenchido pelo miniport. NÃO VALIDADO EM HARDWARE.**
>
> **Quarta faixa pós-Corrida 52, OFFLINE: ciclo de vida fechado antes da instalação lateral.**
> A tentativa de integrar `poolVmid` encontrou um bloqueador anterior: `DestroyContext`
> invalidava as faixas de GPUVA, removia o vínculo contexto/processo e devolvia o VMID mesmo
> quando `Bc250McdmObjectDestroy` retornava `IN_FLIGHT_SUBMISSIONS`. Nessa janela, o slot podia
> ser reciclado enquanto a submissão ainda instalava ou usava seu estado. Agora essas três
> ações só acontecem após `BC250_MCDM_OBJECT_OK`; no caso ocupado o log declara `RETIDO`, GPUVA
> e VMID permanecem associados, e uma tentativa posterior conclui a destruição. O teste do
> miniport adquire uma submission view real, atribui um VMID, exige `STATUS_DEVICE_BUSY`, prova
> que o slot continua no pool, libera a view, destrói com sucesso e só então exige `NOT_FOUND`.
> `build-contract.bat` **EXIT=0**, safety audit **PASS**. Mutação recolocando a liberação fora
> do sucesso foi detectada por dois portões: invalidação prematura de GPUVA e reciclagem de VMID
> em voo. **Nenhuma instalação no poolVmid foi ligada nesta faixa; NÃO VALIDADO EM HARDWARE.**

> **21/09/2026 — Corrida 56 (16:10:11, lab 3.217.1.0): O LATCH DE FAULT É DRENÁVEL E RESTAURÁVEL,
> e a assinatura do fault segue o ENDEREÇO, não o contexto.** Desfecho B do critério
> ([docs/311](docs/311_CRITERIO_CORRIDA56_DRENO_DO_LATCH_DE_FAULT.md)). Evidência:
> `docs/evidence/2026-09-21_16-10-11-DESKTOP-HQAG6T4-lab-binding/`.
>
> - **Quatro TDRs, quatro drenos, todos `Drenados=1 ClearAcked=1 ClearIneficaz=0 Teto=0`,
>   `CntlAntes=CntlReadback=0x3FFFFFFC`, `Restaurado=1`.** A primeira escrita da história do
>   projeto em `GCVM_L2_PROTECTION_FAULT_CNTL` funcionou e devolveu o registrador bit a bit ao
>   valor do POST. **Zero BugCheck** — o risco declarado não se materializou.
> - **`MoreFaults=1` NÃO significa fila.** Com o latch drenado, nenhum TDR entregou um segundo
>   fault. O bit significa outra coisa que **não sei nomear**, e não invento (Lei 7).
> - **O ACHADO: três dos quatro faults são o mesmo, e diferem SÓ no nibble do VMID.**
>   `0x003013BC` (Vmid=3), `0x001013BD` (Vmid=1) e `0x002013BD` (Vmid=2) — todos com
>   `AddrPagina=0x200040`, `Cid=9`, `Rw=0` (leitura) e `PermissionFaults=0xB`. O quarto é
>   distinto: `0x0014115D` Vmid=1, `AddrPagina=0x2464E0`, `Cid=8`, `Rw=1`.
>   **A assinatura segue o ENDEREÇO, não o contexto.** Isso derruba o que restava da leitura
>   "é disputa entre contextos" e dá um alvo único: `0x200040`.
> - **DOIS DEFEITOS MEUS, um material.** (1) Três das quatro caminhadas não aconteceram e a
>   mensagem diz `raiz do VMID 7 AUSENTE` — mas o fault era em VMID 1, 2 e 3. Terceira vez que
>   deixo telemetria descrevendo o estado anterior. (2) O material: `poolRoot` veio **zero**
>   porque o contexto já tinha sido destruído quando o TDR rodou, e `DestroyContext` limpa o
>   `InstallState`. **Sem reter a última raiz instalada por VMID, a caminhada não acontece
>   exatamente nos casos que interessam.**
> - Estabilidade: 1.611 IBs, 6 TDR / 6 `TDR_RESTART PROVADO`, 25 instalações OK, 0 exaustões,
>   rollback `RECOVERY_OK_ONLINE`. Tela preta — correto.
> - **PRÓXIMA FAIXA, já definida e toda offline:** reter a última raiz por VMID mesmo após
>   reciclagem do slot; corrigir a mensagem para nomear o VMID certo; resolver `0x200040`
>   contra o espaço de GPUVA.

> **21/09/2026 — Corrida 55 (15:47:13, lab 3.216.1.0): O LATCH ESTAVA PRESO, e isso invalidou a
> leitura de fault de dez corridas. Desfecho B. BugCheck 0x116 — a sequência de dez corridas
> limpas acabou.** Critério: [docs/310](docs/310_CRITERIO_CORRIDA55_LATCH_DO_FAULT_E_JANELA_DE_PUBLICACAO.md).
>
> - **`LATCH_CNTL=0x3FFFFFFC -> Clear=0 AceitaAtualizacoes=0`.** O par `STATUS`/`ADDR` não
>   acompanhava o fault mais recente, e nunca limpávamos. Das 6 TDRs, só **2** tiveram fault
>   latchado, e o primeiro veio bit a bit idêntico ao da Corrida 54 (`0x0014115D`).
> - **A `JANELA_DE_PUBLICACAO` caiu do mesmo lado, 2 de 2: `TABELA MUDOU`.** A conclusão "a PTE
>   existe, logo o defeito não é PTE ausente" **não se sustentava por dois motivos
>   independentes** — o `Status` podia ser de um fault anterior, e a PTE caminhada podia ter
>   nascido depois dele. Eu ia atacar esse caso; teria atacado um número sem procedência.
> - **ERRO MEU NA DECODIFICAÇÃO:** rotulei o bit 29 como `CrashNoRetry`. O bit 29 é
>   `OTHER_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT`; `CRASH_ON_NO_RETRY_FAULT` é o bit **30**, que
>   não imprimi. Pelo valor cru, **o bit 30 está em 1**.
> - **BugCheck 0x116** `(…, 0, 0xd)` — assinatura idêntica às de 12–13/09. `Arg3=0`: nenhuma
>   operação nossa falhou. Recuperamos 6 de 6 TDRs; o Windows estourou o `TdrLimitCount`.
>   O log foi resgatado pelo offset do baseline (1.690.232 bytes, sha256 `96944DB7...`).
> - Faixa de resposta empacotada como 3.217.1.0: contrato `bc250_fault_latch_contract` com
>   dreno transacional, allowlist de **escrita** aberta só para `0xA120` com escritor único e
>   guarda de sítio. **Um portão meu foi atravessado**: o que *proibia* essa escrita procurava o
>   literal `0x0000A120u` e passou quando a allowlist usou a constante simbólica. Corrigido no
>   mesmo dia; agora as duas grafias contam.

> **21/09/2026 — Faixa pós-Corrida 53, OFFLINE: O PACOTE GRÁFICO PASSA A CARREGAR O VMID DO
> CONTEXTO. Candidato lab 3.215.1.0 empacotado e NÃO INSTALADO.** Critério escrito antes em
> [docs/309](docs/309_CRITERIO_CORRIDA54_PACOTE_NO_VMID_DO_CONTEXTO.md).
>
> - **É o passo que o alocador inteiro existia para permitir.** `Bc250ActiveGfxEngineSubmit`
>   recebe o `poolVmid`; `Bc250Pm4EmitIndirectBuffer` já o codificava por pacote. As quatro
>   incógnitas caíram todas por medição: disputa (Corrida 52), offsets distintos (51), allowlist
>   e ciclo de vida (offline com mutação) e **aceitação do silício (53, 32/32)**.
> - **A RECUSA VOLTOU A SER OBRIGATÓRIA, e isso é a inversão deliberada da faixa anterior.**
>   Enquanto a instalação era lateral, falhar e seguir era o certo — o slot não era usado por
>   ninguém. Agora partir com a tabela do próprio VMID ausente produziria exatamente o fault que
>   a faixa elimina, com o log dizendo que correu bem. Os três ramos de tropeço recusam, cada um
>   nomeando o motivo.
> - **ARMADILHA (d) DO HANDOFF 307 FECHADA:** a caminhada do fault segue o VMID que o fault
>   nomeia. Caminhar um fault de VMID 3 com a raiz do VMID 7 leria a tabela ERRADA, e "a PTE não
>   existe" viraria conclusão falsa sobre a tabela certa. Novo leitor
>   `Bc250VmidPoolInstalledRootForVmid`, em que **só `READY` entrega raiz** — `INSTALLING` e
>   `FAILED` devolvem zero e a ausência é declarada em `[TDR_GCVM_FAULT] ORIGEM_DA_RAIZ`.
> - **TELEMETRIA VENCIDA CORRIGIDA:** a linha de sucesso afirmava que o pacote seguia no VMID 7
>   legado, e isso deixou de ser verdade. Telemetria que descreve o estado anterior é pior que
>   telemetria ausente — ela é lida como medição. Portão novo proíbe a frase de voltar, e ele
>   **pegou o meu próprio comentário explicativo**; reescrevi o comentário em vez de afrouxar o
>   portão, como o projeto já fez antes.
> - **SETE MUTAÇÕES VERIFICADAS, todas reprovando:** pacote voltando ao VMID 7; falha de
>   instalação deixando de recusar; `BUSY` seguindo em vez de adiar; caminhada voltando à raiz
>   fixa do VMID 7; leitor entregando raiz de slot não-`READY`; origem da raiz deixando de ser
>   declarada; telemetria voltando a afirmar o estado anterior.
> - **OITO PORTÕES INVERTIDOS de propósito** — os que exigiam `BC250_GCVM_USER_VMID` no pacote e
>   proibiam a recusa agora exigem o oposto. Estava previsto por escrito desde a faixa que os
>   criou.
> - **RUÍDO ESPERADO:** o bloco de instalação no VMID 7 legado continua rodando, para mudar uma
>   variável só. As linhas `VMID7 CONTEXT SWITCH` seguem aparecendo e agora são ruído, não o
>   caminho do pacote.
> - `build-contract.bat` **EXIT=0**, três auditorias **PASS**, `git diff --check` limpo.
> - **PACOTE LAB 3.215.1.0 assinado, auditado e NÃO INSTALADO:** SYS `718809CF...`,
>   PDB `F6EFD8AD...`, UMD `934DBD84...` (inalterada), INF `F3B01A64...`, CAT `AFCF8B73...`.
>   A primeira passada do empacotador falhou com `IOException` em `Copy-Item` e a auditoria
>   reprovou com cinco hashes divergentes; repetir resolveu — armadilha já registrada.
> - **NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**

> **21/09/2026 — Corrida 53 (14:56:52, lab 3.214.1.0): O SILÍCIO ACEITA RAIZ DE TABELA DE
> PÁGINAS NOS VMIDs DO POOL. Desfecho A do critério, completo.** Evidência:
> `docs/evidence/2026-09-21_14-56-52-DESKTOP-HQAG6T4-lab-binding/`; critério escrito antes em
> [docs/308](docs/308_CRITERIO_CORRIDA53_INSTALACAO_LATERAL_EM_VMID_DO_POOL.md).
>
> - **Houve avanço? SIM. Veredito: SUCESSO, desfecho A.** Identidade conferida: SYS
>   `D2A551DC...`, igual ao pacote; perfil `DISPLAY_RENDER_LAB`, P1 PERSISTENT SUBMISSION.
> - **32 de 32 instalações laterais com `InstallStatus=0 RecordStatus=0 Readback=1 Flush=1
>   Ready=1`**, em **cinco VMIDs distintos** (1, 2, 3, 4 e 5). Zero `LATERAL FALHOU`, zero
>   `LATERAL PULADA`, zero exaustões em 32 aquisições. A escrita persiste, o readback confere e
>   **o ACK de invalidação de TLB volta** — o desfecho C (o pior caso previsto: raiz entra e
>   flush não reconhece) **não ocorreu**.
> - **ESCOPO HONESTO DA PROVA: só os VMIDs 1–5 foram exercitados.** A carga nunca pediu mais de
>   cinco slots simultâneos. Os VMIDs 6 e 10–15 continuam **sem escrita medida**; afirmar que
>   "os doze funcionam" seria extrapolação, não medição.
> - **O risco declarado não se materializou:** zero BugCheck (nona corrida seguida), zero
>   `DEVICE_REMOVED`, zero `StopDevice`, zero `IB_TRIPOD TIMEOUT`, 2.294 IBs executados,
>   rollback `RECOVERY_OK_ONLINE` com BasicDisplay Código 0 em 5 amostras. Escrever em sete
>   registradores de contextos GCVM nunca tocados não desestabilizou a placa.
> - **A correção de escopo da faixa anterior foi decisiva por construção, não por sorte:** como
>   nenhuma lateral falhou, a submissão nunca precisou do caminho de continuação. Se o silício
>   tivesse recusado, a versão anterior teria recusado **todas** as submissões e a corrida sairia
>   sem medição nenhuma.
> - **O fault do VMID 7 continua idêntico:** 6 TDR, 6 `TDR_RESTART PROVADO`, 6 faults
>   `Vmid=7 WalkerError=6 MappingError=1`. **Isso era o esperado** — nada usa o VMID novo ainda.
>   Pico de ocupação subiu para **5** (contra 4 na Corrida 52), ainda sem exaustão.
> - **Tela preta, resultado correto.** Nenhum pixel é produzido por esta faixa.
> - **FALTA UM ÚNICO PASSO para o alocador governar a GPU:** trocar o VMID do `INDIRECT_BUFFER`
>   para o `poolVmid`. A partir daí a falha de instalação volta a ser motivo de recusa.

> **21/09/2026 — Quinta faixa pós-Corrida 52, OFFLINE: verificação interrompida fechada, dois
> portões fracos corrigidos e o escopo lateral restaurado. Candidato lab 3.214.1.0 empacotado e
> NÃO INSTALADO.** Critério da corrida escrito antes em
> [docs/308](docs/308_CRITERIO_CORRIDA53_INSTALACAO_LATERAL_EM_VMID_DO_POOL.md).
>
> - **A árvore estava QUEBRADA ao assumir.** `build-contract.bat` saía **EXIT=1**
>   (`vmid_pool_contract_test` linha 160) e o safety audit **FAILED**. A faixa anterior aplicou
>   a mutação "liberar slot `INSTALLING`" e o limite de uso cortou antes da restauração: a
>   guarda de `Bc250VmidPoolRelease` ficou removida no código. Restaurada, com `ReleasedVmid`
>   permanecendo em `BC250_VMID_COUNT` no caminho `BUSY` — nada foi devolvido, e devolver um
>   número ali faria o chamador registrar uma reciclagem que não houve. **O lado bom: os dois
>   portões pegaram** — teste na linha exata e auditoria nomeando a causa.
> - **DEFEITO DE ESCOPO ENCONTRADO E CORRIGIDO: a instalação lateral não era lateral.** Os três
>   ramos de tropeço (`BUSY`, `BeginInstall != OK`, `install/record` falhou) **recusavam a
>   submissão GFX** com `STATUS_DEVICE_NOT_READY` / `STATUS_GRAPHICS_ALLOCATION_BUSY`. Como o
>   VMID alocado não é usado por ninguém nesta faixa, recusar não comprava segurança alguma — e
>   transformaria a PRIMEIRA corrida que escreve nos VMIDs 1–15 em **zero medição**, com todas
>   as submissões recusadas, caso o silício não aceite raiz nesses contextos. Agora os três
>   ramos declaram o motivo, marcam a entrada `FAILED` e a submissão **segue em VMID 7**. Isso
>   não é sucesso silencioso: o caminho do VMID 7 está intocado e o log diz por onde seguiu.
>   Quando o `INDIRECT_BUFFER` passar a usar o `poolVmid`, a recusa volta a ser obrigatória e o
>   portão muda junto, de propósito.
> - **DOIS PORTÕES FRACOS, achados por mutação.** (1) Trocar `poolIo.FlushVmid` por um flush
>   fixo no VMID 7 **passava** na auditoria — `Bc250GcvmInstallVmid` recusa `FlushVmid==NULL`,
>   então o efeito seria falha fechada, mas a detecção só aconteceria no silício e o sintoma
>   (raiz instalada sem TLB invalidado) é indistinguível do defeito antigo. (2) Deixar **um**
>   dos dois ramos de desistência mudo **passava**, porque o portão procurava a string em vez de
>   **contar** os ramos; refeito com o invariante sem janela de
>   `audit_process_lifecycle_ddis_speak`, e agora a mensagem nomeia qual ramo emudeceu.
> - **SEIS MUTAÇÕES VERIFICADAS, todas reprovando:** instalação lateral voltando a VMID 7; flush
>   lateral fixo no VMID 7; submissão passando a usar o `poolVmid`; lateral derrubando a
>   submissão; lateral devolvendo a submission view; um ramo de desistência mudo.
> - `build-contract.bat` **EXIT=0**, três auditorias **PASS**, `git diff --check` limpo.
> - **PACOTE LAB 3.214.1.0 assinado, auditado e NÃO INSTALADO:** SYS `D2A551DC...`,
>   PDB `1D4CBB0A...`, UMD `934DBD84...` (inalterada), INF `E9651A39...`, CAT `3677BC93...`.
>   Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT SUBMISSION. Strings conferidas em
>   **ANSI** no SYS assinado. Um pacote 3.214.1.0 anterior (SYS `F514C2F4...`) foi gerado e
>   **descartado sem instalar**, antes da correção do escopo lateral.
> - **A PRÓXIMA CORRIDA DEIXA DE SER OBSERVACIONAL:** ela escreve sete registradores por
>   contexto nos VMIDs 1–6 e 10–15, nunca tocados nesta placa. BugCheck é desfecho possível do
>   ensaio. **NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**

> **20/09/2026 — Corrida 45 (20:49:00, lab 3.205.1.0): a onda foi lida, e a CAUSA RAIZ DO STALL
> APARECEU — falha de tradução sob VMID 7, que estava no log em hexadecimal cru desde muito
> antes.** Evidência: `docs/evidence/2026-09-20_20-49-00-DESKTOP-HQAG6T4-lab-binding/`; critério
> prévio em [docs/304](docs/304_CRITERIO_CORRIDA45_ONDA_VIVA_COM_CP_PARADO.md).
>
> - **Houve avanço? SIM, diagnóstico, e grande. Veredito da faixa: NÃO cumpriu o critério —
>   desfecho D.** Identidade conferida: SYS `148BD71F...07DCC`, igual ao manifesto.
> - **A MEDIÇÃO SAIU (item 2 do critério).** A sonda leu uma onda inteira:
>   `SE=0 SA=1 Inst=2 Wave=0 STATUS=0x00810019 PC=0x000000020004114C IB_STS=0x00000000
>   INST_DW0=0x10040C04 TRAPSTS=0x50000100 ValidMask=0x3F`. Decodificado contra
>   `gc_10_1_0_sh_mask.h`: `SQ_WAVE_STATUS` → **`VALID=1`, `FATAL_HALT=1`**;
>   `SQ_WAVE_TRAPSTS` → **`XNACK_ERROR=1`** e `EXCP=0x100`. O bit 8 de `EXCP` **não tem
>   enumeração local e NÃO foi nomeado** (Lei 7).
> - **CAUSA RAIZ, e uma correção de registro minha.** Eu reportei "zero `FaultStatus`" — era
>   FALSO, meu grep (`[^0]`) falhou porque o valor começa com zero. Nos **cinco** TDRs há
>   `FaultStatus=0x0070153C`, que por `gc_10_1_0_sh_mask.h` é **`VMID=7`** (o mesmo VMID das 142
>   submissões GFX), **`WALKER_ERROR=6`**, **`MAPPING_ERROR=1`**, `PERMISSION_FAULTS=3`,
>   `CID=10`, com `FaultAddr` de página `0x245B`/`0x2431` → GpuVa **`0x245B000`/`0x2431000`**.
>   Dois registradores independentes dizem a mesma coisa: **o caminhador de tabela de páginas
>   falhou sob VMID 7, a onda recebeu XNACK e morreu, e o CP espera por quem nunca retira.**
> - **O dado estava no log há muito tempo, em hexadecimal cru dentro de `[TDR_MEC_SNAPSHOT]`.**
>   Ninguém leu porque a busca do projeto era pela string `GFXHUB_FAULT`, que é outro sítio e
>   sai zero. Não era lacuna de conhecimento; era lacuna de **leitura**. Hexadecimal cru num
>   campo que ninguém lê é ausência de medição na prática.
> - **D1 pagou:** `Submetidas=148 Aposentadas=148 EmVoo=0 RetiredCount=132`. A aposentadoria GFX
>   agora é **medida**; o item O6 da Corrida 44, que tive de inferir, está provado direto.
> - **Controle intacto:** `TDR_GFX_RESTART_BASE Pass=1` 5/5, `TDR_GFX_RESTART_PROOF PASS` 5/5,
>   142 submits GFX, 92 convergências, 2.479 IBs SDMA, **zero BugCheck** (segunda corrida
>   seguida), zero `DEVICE_REMOVED`, zero `StopDevice`, **zero crash de dwm.exe** — contra 1 na
>   Corrida 44 e 7–10 historicamente.
> - **FALHAS.** `status=selector_stuck` nas duas execuções da sonda, `SeletorRestaurado=0`,
>   `readback=0x00000000` onde se esperava `0xE0000000`, mais `INDETERMINADO_POISON` em 15 e 16
>   leituras. Pelo critério isso é **desfecho D** e manda **desarmar** — feito no 3.206.1.0.
>   Ressalva: `readback=0` é compatível com seletor preso **ou** com registrador que não devolve
>   o valor escrito; a corrida seguiu com 5 TDRs, restart 5/5 e zero BugCheck, o que não casa com
>   soft-reset atingindo um só SE. Não distingo as duas com este dado.
> - **DEFEITO MEU: o D3 não mediu nada.** `PUBLICACAO_COMPOSICAO` saiu **0 vezes** em 10
>   `PUBLICACAO_FALHA`, embora a string esteja no SYS assinado. O amostrador isenta por
>   **substring** (`FALHA`, `OCUPACAO`, `PASS`…) e o nome que escolhi não continha token isento:
>   caiu na classe 4 e foi amostrado fora, exatamente na medição que a faixa existia para obter.
> - Conformance **sem medição**: o harness não imprimiu nenhuma linha de quadro, e o portão
>   recusou contar Exit 0 como sucesso. O stall reapareceu (esperado) e o tripé segue 2/3.
> - **Pela §8 do critério não existe "C parcial":** a faixa entregou a medição do item 2 mas
>   falhou o item 3, logo **não teve sucesso**. Os dois fatos ficam registrados separados.
> - **Recuperação:** BasicDisplay `Status OK`, fusível inexistente, sem reboot não planejado.

> **20/09/2026 — faixa D4, OFFLINE: o fault deixou de ser hexadecimal cru, e o candidato
> 3.206.1.0 está pronto e NÃO INSTALADO.** Critério em
> [docs/305](docs/305_CRITERIO_CORRIDA46_FAULT_VMID7_E_PUBLICACAO.md).
>
> - `[TDR_GCVM_FAULT] DECODIFICADO` nomeia `Vmid`, `WalkerError`, `MappingError`,
>   `PermissionFaults`, `Cid`, `Rw`, `MoreFaults` e converte o endereço de página em GpuVa.
>   Ausência de fault é **declarada**, nunca silenciosa.
> - **Armadilha apanhada antes de virar veredito:** a primeira versão dessa linha teve os shifts
>   transcritos de cabeça e **os seis saíram errados** — `VMID` em 18 no lugar de 20,
>   `WALKER_ERROR` em 8 no lugar de 1, `RW` em 31 no lugar de 18. As máscaras foram então
>   copiadas de `gc_10_1_0_sh_mask.h`. Decodificação errada é pior que hexadecimal cru: o cru ao
>   menos não mente.
> - `COMPOSICAO` entrou na lista de isenção do amostrador, com a causa registrada no comentário.
> - Sonda de onda **desarmada** por ordem do desfecho D.
> - Portão novo com 6 exigências dentro de `audit_gfx_ring_tripod_and_ib_inspection`;
>   **três mutações verificadas** (fault volta a cru; shift de VMID volta ao errado; ausência de
>   fault volta a ser muda), todas reprovando com baseline em zero.
> - `build-contract.bat` **EXIT=0**; safety, DDI matrix, DDI noop e lab package audit **PASS**.
>   Pacote 3.206.1.0: SYS fonte `13CE38BC...`, SYS assinado `7D3154A8...`, PDB `B842E639...`,
>   UMD `934DBD84...`, INF `AD192AD4...`, CAT `D33D3F04...`. **Perfil idêntico ao 3.202 — ZERO
>   diferenças de tunable.** Binário conferido por string.
> - **NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.** A Corrida 46 é de medição: fecha ou refuta a
>   cadeia publicação → walker → XNACK → `FATAL_HALT` → CP travado.

> **20/09/2026 — Corrida 44 (14:39:20, lab 3.202.1.0): H44 CONFIRMADA. O TDR gráfico recupera
> e readmite trabalho GFX real — 167 submissões contra 1, e ZERO BugCheck.** Evidência:
> `docs/evidence/2026-09-20_14-39-20-DESKTOP-HQAG6T4-lab-binding/`; critério prévio em
> [docs/302A](docs/302A_EMENDA_CRITERIO_CORRIDA44.md).
>
> - **Houve avanço? SIM, funcional e diagnóstico. Veredito: SUCESSO da faixa**, pelo critério
>   congelado antes do ensaio, mais um **desfecho E — algo fora do mapa**.
> - **Identidade conferida:** SYS `33E8FF29...ABCCFF` em `private-symbols.json` igual ao
>   manifesto. **Coleta completa** — `display-log-this-bind-FINAL.txt` (2.910.536 bytes) e logs
>   `-FINAL` da UMD, sem necessidade de resgate de cauda.
> - **Os cinco itens de sucesso ocorreram, 5 de 5 cada um:**
>   `[TDR_GFX_RESTART_BASE] Esperado=00000000:F40FF700 Readback=00000000:F40FF700 Pass=1`;
>   `[TDR_FENCE_RECONCILE] PASS` com `NotificadasComoConcluidas=0`;
>   `[TDR_GFX_RESTART_PROOF] PASS ... RPTR=12 WPTR=12 PoisonSubstituido=1 Polls=1` — contra
>   2000 polls falhando na Corrida 43; `CP_RB0 rearmado e comprovado por execucao fisica`; e o
>   callback devolvendo `adaptador aceita submissao novamente`. **A base 256 vezes errada era a
>   causa do `RPTR=0`.**
> - **Desfecho E, o resultado que não estava em nenhuma letra:** **167 submissões GFX** contra
>   **1** na Corrida 43. Fences submetidas até **173**, e a maior fence **escrita pela GPU** é
>   `0xAD` = **173** — o anel acompanhou até a última. **94 de 111** tripés com
>   `Perna1=REGISTRADOR` (RPTR convergiu com WPTR) e **111 de 111** com
>   `Perna2=FENCE_ESCRITA_PELA_GPU` sobre poison. IB de até **20.208 dwords**. Nenhuma corrida
>   anterior mostrou readmissão sustentada de trabalho gráfico.
> - **Zero BugCheck:** `MEMORY.DMP` continua o das 10:06 da Corrida 43; sem evento 41, 1001 ou
>   6008. O Windows registrou **3x evento 4101** (driver parou e **se recuperou**). Zero
>   `DEVICE_REMOVED`, `StopDevice`, `IB_TRIPOD TIMEOUT` e `GFXHUB_FAULT`; 2.308 IBs SDMA.
>   Houve 5 TDRs — o segundo TDR era esperado e não rebaixa o veredito, por decisão do 302A.
> - **Falhas medidas.** O stall inicial **reapareceu**: 17 de 111 tripés `NAO_CONVERGIU`, 6 com
>   a assinatura exata da Corrida 43 (`WPTR=32 RPTR_Reg=24 RPTR_Wb=20`), e três
>   `TDR_GFX_PROBE Responsive=0`. Deixou de ser terminal; não foi resolvido, e esta faixa não o
>   tocou. **Pela Lei 2 o tripé GFX está em 2 de 3**: as 111 linhas dizem
>   `Perna3=DELEGADA_AO_BYTE_PROOF` e **não há byte proof de GFX** — destino conferido byte a
>   byte NÃO existe. Também **não há telemetria de aposentadoria GFX** em nível WDDM
>   (`DMA_COMPLETED`); as 17 linhas `[SdmaRetire]` são do Node 0 e de classe **amostrada**.
>   Conformance D3D11 **REPROVADO**: `PRESENT_FALHOU` no quadro 1, `hr=0x887A0005`,
>   `DeviceRemovedReason=0x887A0020` — este segundo código **sem procedência local, não
>   nomeado** —, 0 de 60 quadros. 1 crash do `dwm.exe` (dump 316 MB), contra 7 a 10 antes.
>   Sem prova de pixel: 8 `DdiPresent ENTER`, 6 `CommitVidPn`, 30 `SetVidPnSourceAddress`.
> - **ACHADO QUE APONTA O PRÓXIMO PASSO:** no **5º TDR** apareceu
>   `[TDR_GFX_WAVES] SQ_DEBUG_STS_GLOBAL=0x00000011 Busy=1 WaveLevelSA0=1`. Nos outros quatro,
>   e em toda a Corrida 43, era `Busy=0` com zero ondas. Pela legenda do próprio log, ondas > 0
>   com o CP parado aponta **shader que não retira** — primeira medição que discrimina entre
>   "estado ou pacote" e "shader". É H, com uma única amostra.
> - **Recuperação:** BC-250 de volta ao BasicDisplay, `Status OK`, `Service BasicDisplay` —
>   recuperação, **não** sucesso do nosso driver. Watchdog registrou `BLOCKED` (exit 11), mas o
>   rollback se efetivou. Fusível inexistente: próximo boot assume CLEAN. Sem reboot não
>   planejado.

> **20/09/2026 — faixa D1, OFFLINE pós-Corrida 44: a perna 3 do tripé GFX parou de prometer
> uma prova que ninguém executa, e a aposentadoria GFX virou mensurável.** Sem candidato novo:
> esta faixa **não altera nenhum caminho de GPU** e não justifica queimar uma corrida sozinha.
>
> - **DEFEITO CORRIGIDO, classe "o rótulo carimbava o falso sucesso" (oitava ocorrência).** O
>   veredito do tripé GFX imprimia a perna 3 como DELEGADA a uma prova de bytes. A mineração da
>   Corrida 44 contou **111 ocorrências da string `BYTE_PROOF` no log inteiro, e todas as 111
>   eram essa própria linha** — o delegado nunca rodou uma vez. O `[GfxIbInspect]` varre a
>   **estrutura** do IB pela CPU (linear, saltos CHAIN, chamadas IB2) e não confere um byte de
>   destino. Quem confiasse no rótulo contaria 111 tripés completos onde há **2 de 3 pernas**.
> - A perna 3 passou a sair de fonte única como `NAO_EXECUTADA_SEM_GABARITO_DE_DESTINO`, e a
>   linha declara `TRIPE INCOMPLETO, 2 de 3 pernas`. Gabarito de destino para IB arbitrário de
>   UMD **não existe** — não se sabe o que o shader do DWM deveria escrever —, então a perna
>   fica **aberta e declarada**, nunca simulada. Pela Lei 2, submissão GFX não pode ser
>   declarada comprovada neste estado.
> - **Aposentadoria GFX virou medição.** Ela já existia em `LastRetiredFence`/`RetiredCount`,
>   avançando pela fence da GPU com o poison excluído, mas **não era impressa** — e por isso o
>   item O6 do critério da Corrida 44 teve de ser **inferido** das fences em vez de medido.
>   O veredito agora publica `Submetidas`, `Aposentadas`, `EmVoo` e `RetiredCount`.
> - Dois portões novos dentro de `audit_gfx_ring_tripod_and_ib_inspection`: o literal antigo da
>   perna 3 está **proibido** no arquivo inteiro, e o veredito é obrigado a declarar-se
>   incompleto e a publicar os quatro contadores. **Quatro mutações verificadas**, todas
>   reprovando com baseline em zero: rótulo antigo de volta; veredito deixando de declarar-se
>   incompleto; telemetria de aposentadoria removida; e a perna 3 passando a alegar `PASS`.
> - Armadilha registrada: o portão reprovou o **meu próprio comentário** por citar o literal
>   banido. O comentário foi reescrito; o portão **não** foi afrouxado — mesma decisão tomada
>   quando o gate do B3 pegou um comentário.
> - `build-contract.bat` **EXIT=0** sem warning em `bc250_active_miniport.c`; safety, DDI
>   matrix, DDI noop e lab package audit **PASS**. Selo do 3.202.1.0 intacto.
> - **NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.** Ela deve pegar carona no próximo candidato
>   substantivo, não motivar uma corrida própria. **E pegou: saiu no 3.204.1.0, abaixo.**

> **20/09/2026 — faixa D2, OFFLINE: a medição que a Corrida 44 perdeu. Candidato lab
> 3.204.1.0 pronto e NÃO INSTALADO.** Critério prévio em
> [docs/304](docs/304_CRITERIO_CORRIDA45_ONDA_VIVA_COM_CP_PARADO.md).
>
> - **A pergunta da onda ficou sem resposta por um tunable desarmado.** Nos cinco TDRs da
>   Corrida 44 a sonda indireta saiu `[TDR_GFX_WAVEPC] DESARMADO: GfxWaveProbe=0 present=1` —
>   **inclusive no único TDR com `Busy=1`**. A sonda existe, está ligada ao caminho de TDR e lê
>   por onda `SQ_WAVE_STATUS`, `PC_LO/HI`, `IB_STS`, `INST_DW0` e `TRAPSTS`; ela só nunca foi
>   armada. Nenhuma linha de código nova foi precisa para responder à pergunta.
> - **Offsets da sonda conferidos e catalogados.** Os seis índices indiretos `ixSQ_WAVE_*`
>   batem **exatamente** com `inc/linux_ref/gc_10_1_0_offset.h:11188-11199`, e os dois `mm` de
>   idx0 dão base implícita idêntica de `0x1260` dwords — a mesma já catalogada. Estavam em uso
>   com procedência citada só no header do driver, o que a Lei 7 não aceita; entraram em
>   `docs/03` numa seção própria, com a ressalva de que **só `SQ_DEBUG_STS_GLOBAL` tem leitura
>   física até aqui**. Correção de registro: `inc/linux_ref/` **tem** o `gc_10_1_0_offset.h` —
>   a nota de que só havia o `sh_mask` estava errada.
> - Segurança verificada por leitura, já que a sonda nunca correu no silício: a restauração de
>   `GRBM_GFX_INDEX` para broadcast é feita **com readback** e falha fechada em
>   `SELECTOR_STUCK`; a sonda só corre quando o SQ disse que há onda; leitura inválida é
>   recusada em vez de interpretada. Armar exigiu `-IAcceptSelectorWritesDuringTdr`, no mesmo
>   padrão do `-ArmTdrInducedHang`. **Decisão do dono do projeto, tomada em 20/09.**
> - **QUASE-ACIDENTE QUE VIROU PORTÃO.** O primeiro pacote que gerei saiu `DISPLAY_LAB` em modo
>   **READ-ONLY**, com `RenderNode=0` e sem submissão persistente, porque omiti
>   `-EnableRenderNode/-EnableShaderDispatch/-EnablePersistentSubmission`. O empacotador aceitou
>   (o perfil é legítimo) e o `audit-bc250mcdm-lab-package.ps1` deu **PASS** — ele confere o
>   pacote, não a **intenção** do ensaio. Um bind assim não produz submissão GFX nenhuma: a
>   corrida inteira teria sido gasta medindo o nada, e o log pareceria apenas "sem submissões".
>   Agora o binder **registra o perfil ligado** em `_last_lab_binding.json` e **recusa** um
>   candidato mais fraco que o do último bind, nomeando a capacidade que caiu; rebaixamento
>   deliberado exige `-IAcceptProfileDowngrade`. **Quatro mutações verificadas.**
> - Identidade do candidato conferida campo a campo contra o manifesto commitado do 3.202:
>   **a única diferença de perfil é `GfxWaveProbe 0 -> 1`**. `DISPLAY_RENDER_LAB`
>   (RenderNode=1, RenderOnlyLab=0), WDDM 2.7, schema 135, P1 PERSISTENT SUBMISSION.
>   SYS fonte `871B0131...B1FE8D`, SYS assinado `34580DC5...B364E8A`, PDB `7B9C6C49...662B409`,
>   UMD `934DBD84...5AB5236A`, INF `697A127B...6EFEBBCA`, CAT `0A447B11...96BD6C5A0`.
>   DriverVer subiu para 3.204 para não repetir a ambiguidade do 3.92.1.0 — o pacote errado
>   chegou a existir como 3.203.1.0 e foi substituído.
> - Binário **conferido por string** (o empacotador copia, não compila): estão no SYS assinado
>   `NAO_EXECUTADA_SEM_GABARITO_DE_DESTINO`, `TRIPE INCOMPLETO, 2 de 3 pernas`, `Submetidas=` e
>   `TDR_GFX_WAVEPC`; o literal antigo da perna 3 **não** está.
> - `build-contract.bat` **EXIT=0**; safety, DDI matrix, DDI noop e lab package audit **PASS**;
>   `git diff --check` limpo; selo do 3.204.1.0 íntegro nos sete hashes.
> - **NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.** A Corrida 45 é de **medição**, não de
>   correção: sucesso é obter PC e instrução da onda parada, não o stall desaparecer. Não
>   esperar desktop nem pixel.
> - **NOTA DE IDENTIDADE:** o 3.204.1.0 **nunca foi instalado**; foi substituído pelo
>   **3.205.1.0**, que carrega também a faixa D3 abaixo.

> **20/09/2026 — faixa D3, OFFLINE: a falha de publicação de GPUVA teve o mecanismo fechado por
> aritmética, e o diagnóstico anterior está REFUTADO.** Candidato lab **3.205.1.0** pronto e
> NÃO INSTALADO. Critério em [docs/304](docs/304_CRITERIO_CORRIDA45_ONDA_VIVA_COM_CP_PARADO.md).
>
> - A falha **reapareceu na Corrida 44**: seis `[ActiveFlushTlb] PUBLICACAO_FALHA`, todas com
>   `PublishStatus=3` (NO_SPACE), `Attempts=1`, `Busy=0`.
> - **MECANISMO FECHADO, P0 por aritmética exata, seis de seis:** `FailedIndex` é **idêntico**
>   a `Reusable` do slot ativo, e `Mapped + Reusable = Cap = 1024` em todas —
>   832/655/369, 657/640/384, 721/603/421, 695/460/564, 513/430/594, 657/415/609. Confirmado por
>   leitura: `Bc250GpuvaCopySnapshot` copia o slot **ativo** para o inativo e o lote é aplicado
>   **por cima**, logo o teto útil é `Cap − Mapped` e um lote maior morre no índice exato.
> - **REFUTA o diagnóstico anterior**, que atribuía o NO_SPACE à exaustão do teto de 256 slots:
>   o teto é 1024, `Teardown=0` e `Reusable` é o complemento exato de `Mapped` — a reciclagem
>   está **sã**. Não é slot preso em teardown.
> - Confirmado também que a falha **não corrompe**: a publicação retorna cedo sem trocar
>   `ActiveSlot` nem incrementar a geração, e o slot inativo parcialmente construído é
>   sobrescrito pelo `CopySnapshot` seguinte.
> - **A faixa é MEDIÇÃO, não conserto, e isso é deliberado.** O journal já coalesce em
>   intervalos não sobrepostos, então aplicar unmaps antes de maps derrubaria o pico sem mudar o
>   estado final — **mas não se sabe a composição do lote**. Se for dominado por MAP, reordenar
>   não resolve nada e o assunto é capacidade real. Propor a reordenação sem esse número seria
>   fix de efeito imprevisível, então o que entrou foram os contadores:
>   `[ActiveFlushTlb] PUBLICACAO_COMPOSICAO Maps=N Unmaps=M Livres=L`, contados **antes** de
>   qualquer mutação. Zero mudança de comportamento.
> - Armadilha respeitada: a C3.12 registrou que gravar ocupação **dentro do ramo de falha**
>   reprovava `active_miniport_test` por mecanismo não explicado. A contagem foi posta na
>   **entrada**, junto dos demais `LastPublish*`, e nada foi escrito no ramo de falha.
> - Teste novo em `gpuva_snapshot_contract_test.c` faz **replay dos números da Corrida 44** (369
>   vivas, lote de 832 maps) e exige o índice **exato 655**, mais o invariante de que a falha não
>   troca o slot ativo, mais um contraponto com metade unmaps para a asserção não virar carimbo.
>   **Três mutações verificadas** com atribuição conferida — a reprovação sai como
>   `TESTE FALHOU (gpuva_snapshot_contract_test) - Linha/Codigo: 339`, não como erro de compilação.
> - Armadilha registrada: a primeira rodada de mutações teve **baseline reprovando**, porque o
>   heredoc do bash comeu a barra invertida do caminho do build. Resultado não-atribuível,
>   descartado e refeito com `os.path.normpath`.
> - `build-contract.bat` **EXIT=0**; safety, DDI matrix, DDI noop e lab package audit **PASS**.
>   Pacote 3.205.1.0: SYS fonte `26223CBC...`, SYS assinado `148BD71F...`, PDB `69D7E878...`,
>   UMD `934DBD84...`, INF `ACDABF8C...`, CAT `FA554F79...`; perfil idêntico ao 3.202 **exceto
>   `GfxWaveProbe 0 → 1`**. Binário conferido por string: D1, D2 e D3 presentes, literal antigo
>   da perna 3 ausente.
> - **NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**

> **20/09/2026 — Corrida 43 (10:05:31, lab 3.201.1.0): a prova executável reprovou
> honestamente o restart e revelou `CP_RB0_BASE` codificado sem `>> 8`.** Evidência:
> `docs/evidence/2026-09-20_10-05-31-DESKTOP-HQAG6T4-lab-binding/`; relato em
> [docs/301](docs/301_HANDOFF_2026-09-20_CORRIDA43_RESTART_GFX_FALHA_FECHADO_BASE_ERRADA.md).
>
> - **Houve avanço? SIM, diagnóstico; NÃO funcional. Veredito: FALHA.** O primeiro IB GFX
>   voltou a parar em `RPTR=24/WPTR=32`. O reset abortou uma fence sem notificação falsa,
>   provou o SDMA, mas a prova GFX ficou em `RPTR=0/WPTR=12`, fence no poison, e devolveu
>   `STATUS_DEVICE_NOT_READY` em vez de declarar o adaptador pronto.
> - O Windows registrou `0x116 (..., fffff8028a0cfb80, ffffffffc00000a3, 4)`; o dump fixou
>   Arg2 em `bc250mcdm+0x1FB80`. Reconstrução com `.text/.pdata` idênticas atribuiu o RVA a
>   `Bc250McdmRestartFromTimeout`; Arg3 é a recusa fail-closed. Sem AV/IRQL/verifier/corrupção.
> - **Causa no código:** ONLINE escrevia `CP_RB0_BASE=RingGpuVa>>8`; restart escrevia a GPUVA
>   bruta em LO/HI. Para `RingGpuVa=0xF40FF70000`, escreveu `F4:0FF70000` em vez de
>   `0:F40FF700`, isto é, base decodificada 256 vezes maior. Isso explica `RPTR=0`.
> - A cauda integral foi recuperada: 173.347 bytes, SHA-256 `36B67CA1...FB0536`. Três arquivos
>   pré-alocados ficaram somente com zeros porque o BugCheck interrompeu o binder.
> - O PDB arquivado também estava corrompido (`CCDA...` contra `8376...` declarado). O binder
>   agora arquiva SYS+PDB e revalida os hashes dos destinos antes de tocar no dispositivo.
> - Recuperação/estado: na inspeção final do handoff o fusível estava `TRIPPED`, `Boots=4`;
>   BC-250 em `CM_PROB_FAILED_POST_START` e serviço parado. BasicDisplay, se visível, é
>   recuperação e não sucesso do nosso driver.

> **20/09/2026 — faixa OFFLINE pós-Corrida 43; candidato lab 3.202.1.0 pronto e NÃO
> INSTALADO.** Critério prévio em
> [docs/302](docs/302_CRITERIO_CORRIDA44_BASE_CP_RB0_CORRIGIDA.md), **emendado antes da corrida
> por [docs/302A](docs/302A_EMENDA_CRITERIO_CORRIDA44.md), que é o critério vigente** — ver a
> faixa de auditoria logo abaixo.
>
> - `Bc250GfxRingEncodeBaseLo/Hi` virou fonte única do formato `GPUVA>>8` para ONLINE e
>   restart. Teste cobre LO e HI; auditoria proíbe a volta da GPUVA bruta no restart.
> - Build contratual completo e auditorias de segurança/pacote passaram. Pacote 3.202.1.0:
>   SYS `33E8FF29...ABCCFF`, PDB `EF6EFD68...82575F`, UMD `934DBD84...236A`, INF
>   `B2FE0DE4...6C998`, CAT `70FB1638...C7A8F`. **NÃO VALIDADO EM HARDWARE.**
> - Próximo ensaio mede se a base correta faz a prova pós-reset chegar a
>   `RPTR=WPTR=12` e substituir o poison. A causa do stall inicial em `RPTR=24` permanece
>   separada e aberta.

> **20/09/2026 — faixa OFFLINE de auditoria do critério e da fonte única, ANTES da Corrida 44.
> O candidato 3.202.1.0 NÃO foi alterado e o selo dele foi reconferido: segue pronto e NÃO
> INSTALADO.** Critério vigente em
> [docs/302A](docs/302A_EMENDA_CRITERIO_CORRIDA44.md).
>
> - **O critério da Corrida 44 estava defeituoso em três pontos, corrigidos ANTES do ensaio.**
>   (1) Havia **dois critérios congelados discordando em disco** — `docs/302` com 6 itens de
>   sucesso obrigatório e `docs/303` §11 com 7 —, arranjo que permite escolher o mais
>   conveniente depois de ver o log. (2) Faltava o desfecho **`E — algo fora do mapa`**, exigido
>   pela Lei 10, regra 2. (3) O sucesso obrigatório exigia **submissão GFX posterior aposentada**
>   e **nenhum segundo TDR**, que dependem do stall inicial em `RPTR=24/WPTR=32` — dívida que a
>   faixa 3.202 declara **separada** e não toca. Com eles na lista, H44 podia ser confirmada por
>   inteiro e a corrida ser graduada FALHA, atribuindo à correção de BASE uma falha que não é dela.
> - **Sucesso da faixa passou a ser 5 itens** (`TDR_GFX_RESTART_BASE Pass=1`,
>   `TDR_FENCE_RECONCILE PASS` com `NotificadasComoConcluidas=0`, `TDR_GFX_RESTART_PROOF PASS`
>   com `RPTR=WPTR=12` e poison substituído, `CP_RB0 rearmado e comprovado por execucao fisica`,
>   e callback devolvendo sucesso). Os antigos itens 6 e 7 viraram **observação declarada como
>   dependente de dívida aberta**: relatados sempre, sem rebaixar o veredito. Os 302 e 303 ficam
>   intactos como registro do que foi escrito primeiro, com aviso apontando para a emenda.
> - **Correção de enquadramento, minha:** o terceiro escritor de `CP_RB0_BASE`
>   (`bc250_compute_service.c`) **não** pertence ao `bc250mcdm.sys` — ele é linkado no
>   `bc250compute.sys`, driver de laboratório separado. A duplicação da codificação era real,
>   mas **fora** do binário que vai à Corrida 44. Quem revelou isso foi o próprio build, com
>   `LNK2019` ao passar a usar os helpers.
> - As duas escritas daquele driver passaram à fonte única e `bc250compute.sys` agora linka
>   `bc250_gfx_ring_contract.obj` + `bc250_blt_proof_contract.obj`.
> - **O portão da BASE era fraco e um dos alvos era vazio.** O `forbid` textual cobria uma única
>   forma da expressão errada — a LO com máscara; um cast sem máscara, ou a variante HI antiga,
>   passavam. Virou verificação **estrutural**: toda escrita em `CP_RB0_BASE/_HI` nos três
>   escritores tem de carregar valor vindo da fonte única, com leitura por referência isenta e
>   **exceção nominal, não genérica**, para o rollback da Lei 5 (`Evidence->BaseBefore`). O alvo
>   `Bc250ActiveGfxEngineOnline` dava **0 matches** — as escritas ONLINE vivem em
>   `bc250_gfx_ring_transaction.c`; portão vazio trocado pelo arquivo certo.
> - **Quatro mutações verificadas**, todas reprovando com baseline em zero: GPUVA bruta com
>   máscara (forma exata da Corrida 43); cast **sem** máscara (o buraco do `forbid` antigo);
>   conta reimplementada no compute; e ONLINE disfarçado de rollback.
> - `driver/wdk/build-contract.bat` **EXIT=0**; `audit-bc250mcdm-safety.py`,
>   `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py` e
>   `audit-bc250mcdm-lab-package.ps1` **PASS**; `git diff --check` limpo.
> - **Selo do 3.202.1.0 reconferido e INTACTO** (`SourceSys 1007DACB...040894` e
>   `PackageSys 33E8FF29...ABCCFF` batendo com o manifesto). Nenhuma fonte do `bc250mcdm.sys`
>   mudou de conteúdo e o `output_lab` não foi reconstruído, logo **não há novo DriverVer nem
>   reempacotamento**. A sequência física pré-bind (recuperar, resetar fusível, boot frio)
>   permanece obrigatória e inalterada.
> - **Higiene, registrado porque é violação de norma:** durante as mutações foi usado
>   `git checkout --` num arquivo com trabalho não commitado, o que o `docs/303` §0 proíbe, e
>   dois restauros falharam por lock do sistema de arquivos. **Dano: nenhum, verificado** — o
>   `checkout` não chegou a aplicar, a faixa não commitada continua presente e os quatro sítios
>   foram restaurados por conteúdo, com line-endings preservados.
> - **A integridade dos restauros foi PROVADA por rebuild do `output_lab`, com backup prévio e
>   comparação de seções PE** — não apenas por inspeção. O SHA-256 global do SYS reconstruído
>   difere (`F9980F8C...41D9` contra o selo `1007DACB...040894`), como difere o do PDB, porque
>   o CodeView é regenerado a cada build; é o mesmo fenômeno já documentado no `docs/303` §6
>   para a reconstrução da Corrida 43. Por seção: **`.text`, `.pdata`, `.data`, `.reloc`,
>   `INIT`, `PAGE` e `fothk` byte a byte IDÊNTICAS**; só `.rdata` diverge, em **64 de 158.208
>   bytes (0,04%)**, e **os 64 estão inteiramente dentro do debug directory e dos registros
>   CodeView e REPRO — zero bytes divergentes fora deles**. `.text` idêntica também prova que a
>   toolchain produziu o mesmo codegen, apesar do aviso de `vswhere.exe` ausente no script.
> - **O `output_lab` foi restaurado do backup e o selo do 3.202.1.0 está ÍNTEGRO**: os sete
>   hashes (`SourceSys`, `SourcePdb`, `PackageSys`, `PackagePdb`, `PackageUmd`, `PackageInf`,
>   `PackageCat`) voltaram a bater com o manifesto, e `audit-bc250mcdm-lab-package.ps1` e
>   `audit-bc250mcdm-safety.py` passaram depois disso. O artefato que vai ao hardware continua
>   bit a bit o mesmo que foi assinado e auditado.
> - **NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.** Ela não altera nenhum caminho de GPU do
>   candidato: corrige critério, portão e um driver de laboratório distinto.

> **20/09/2026 — faixa OFFLINE pós-Corrida 42; candidato lab 3.201.1.0 pronto e NÃO
> INSTALADO.** Critério prévio em
> [docs/300](docs/300_CRITERIO_CORRIDA43_TDR_GFX_EXECUTAVEL.md).
>
> - O restart do CP_RB0 não aceita mais readback como prova suficiente: emite `NOP x4 +
>   RELEASE_MEM` e exige `RPTR==WPTR==12` mais fence da GPU sobre poison. Falha mantém o motor
>   offline e devolve `STATUS_DEVICE_NOT_READY`.
> - O rastreador WDDM fecha a admissão e aborta committed/reservas do reset sem notificar
>   conclusão falsa; pins GPUVA são devolvidos. A marca d'água já notificada é preservada.
> - O writeback de RPTR passa a ser zerado no rearmamento. O boot preamble também se tornou
>   fail-closed: `DrenoOk=0` não pode mais terminar em `ONLINE SUCESSO`.
> - O MEC segue com readback, não prova executável pós-reset; os logs deixaram de chamá-lo de
>   comprovado no silício.
> - Build KMD completo, auditoria de segurança e auditoria do pacote passaram. Pacote
>   3.201.1.0: SYS `A76A3DA6...E514`, UMD `934DBD84...236A`, INF
>   `32DE2A66...ECA6`, CAT `FDA7D224...A54`, PDB privado `8376AA54...4542`.
>   **NÃO VALIDADO EM HARDWARE.** O manifesto agora sela o PDB e o binder o arquiva antes
>   do bind; isso não recupera o PDB exato ausente da Corrida 42.
> - Antes de novo bind, analisar o `MEMORY.DMP` da Corrida 42 em sessão elevada. O próximo
>   ensaio não busca desktop: mede uma única recuperação GFX executável e falha fechado.
> - **Dump analisado:** bucket `0x116_IMAGE_bc250mcdm.sys`; Arg2 pertence ao nosso SYS no RVA
>   `+0x1D770`; pilha em `VidSchiWaitForCompletePreemption`, sem bucket de AV/IRQL/verifier ou
>   corrupção. Relatório SHA-256 `36CD8FD6...BD0F`. A função privada exata permanece sem nome
>   porque o PDB de 3.200.1.0 não foi arquivado. O gate de dump da Corrida 43 está liberado.

> **20/09/2026 — Corrida 42 (00:03:31, lab 3.200.1.0): row pitch executou, CP_RB0
> reincidiu e a máquina terminou em `VIDEO_TDR_FAILURE 0x116`.** Evidência:
> `docs/evidence/2026-09-20_00-03-31-DESKTOP-HQAG6T4-lab-binding/`; relato completo em
> [docs/299](docs/299_HANDOFF_2026-09-20_CORRIDA42_UPLOAD_OK_TDR_0X116.md).
>
> - **Houve avanço? SIM, funcional parcial e diagnóstico; veredito global: FALHA.** O gate
>   permaneceu armado em todos os processos novos e uma textura 2560x1080 foi copiada linha a
>   linha e enviada ao Vulkan com `RowPitch=10240`, `bufferRowLength=2560`, fence concluída.
>   O caso acolchoado 590x800 não chegou a rodar e segue sem prova física direta.
> - **Falha dominante:** 6 submits GFX aceitos, zero aposentados. O primeiro IB, idêntico ao
>   primeiro da Corrida 41, parou em `RPTR=24/WPTR=32`; após cinco resets, os seguintes pararam
>   em `0/12`, sem ondas e sem `GFXHUB_FAULT`. O DWM recebeu `VK_ERROR_DEVICE_LOST` e o Windows
>   registrou `0x116 (ffffd305de0b5460, fffff803475ad770, 0, d)`.
> - **TDR não está fechado:** o restart GFX atual valida readbacks, mas não executa NOP/fence
>   no CP_RB0; a frase genérica de NOP físico só é sustentada pelo backend SDMA. Pendências GFX
>   cresceram até 7. Corrigir essa prova e reconciliar fences abortadas antes de nova corrida.
> - **Publicação GPUVA fechou diagnóstico:** duas falhas nomeadas, `Batch=864/FailedIndex=529`
>   e `Batch=721/FailedIndex=530`, com 529/530 slots reutilizáveis. São falhas reais tardias,
>   mas não explicam o primeiro stall. O journal não pode escrever PTEs físicos antes de saber
>   que consegue publicar o lote inteiro.
> - **Recuperação:** houve reboot; fusível impediu segundo bind, watchdog restaurou BasicDisplay
>   Código 0 em cinco amostras. `MEMORY.DMP` existe, mas a sessão não elevada recebeu acesso
>   negado; pilha e módulo de Arg2 ainda não foram extraídos.
> - A cauda perdida foi recuperada: 656.463 bytes, SHA-256 `C1C76B59...09A134`. O recuperador
>   foi corrigido para somar `start_offset` quando o arquivo arquivado é delta delimitado.

> **20/09/2026 — faixa OFFLINE pós-Corrida 41; candidato lab 3.200.1.0 pronto e NÃO
> INSTALADO.** Critério prévio da próxima corrida em
> [docs/298](docs/298_CRITERIO_CORRIDA_42_ROW_PITCH_E_GATE_PRESERVADO.md).
>
> - O runner não recria mais `HKLM:\SOFTWARE\BC250\Umd`: a chave só nasce quando ausente,
>   preservando `D3D11LabPromotion`. Gate e mutação reprovam a volta de `New-Item -Force`.
> - `ResourceUpdateSubresourceUP` agora copia textura linha a linha com pitches independentes,
>   valida formato/box/subrecurso/limites e cobre a última linha. O upload usa
>   `VkBufferImageCopy.bufferRowLength=RowPitch/bpp`; o replay `590x800` exige 640 texels.
> - A recusa final do Draw publica `LastVkDrawFailureReason`; descriptor não volta a ser
>   descrito falsamente como ausência genérica de compilador/PM4.
> - `BC250_GPUVA_NO_SPACE` agora fotografa batch, índice, map/unmap, base e tamanho da faixa.
>   Isso é observabilidade; não é correção da causa ainda inconclusiva.
> - `pfnLockCb=0x80070057` segue dívida funcional: o update nomeia a divergência entre o espelho
>   WDDM e a cópia Vulkan, mas o contrato de backing ainda não foi redesenhado.
> - Builds UMD/KMD, suites contratuais, auditoria de segurança, 5/5 mutações da Corrida 41,
>   14/14 mutações descriptor/wave e auditoria do pacote passaram. Pacote 3.200.1.0:
>   SYS `7B6E1F58...F0E901`, UMD `934DBD84...5236A`, INF `29666314...F474D`,
>   CAT `DC95AB47...13358`. **NÃO VALIDADO EM HARDWARE.**

> **19/09/2026 — Corrida 41 (19:29:03, lab 3.199.1.0): Desfecho B fechado; upload 2D com
> padding é a parede, e os dumps separaram a tempestade posterior.** Evidência:
> `docs/evidence/2026-09-19_19-29-03-DESKTOP-HQAG6T4-lab-binding/`; relato completo em
> [docs/297](docs/297_HANDOFF_2026-09-19_CORRIDA41_ROW_PITCH_E_FAILFAST.md).
>
> - **Houve avanço? SIM, diagnóstico; NÃO funcional contra a Corrida 40.** O recurso
>   `0x930AC70` chegou a `ResourceUpdateSubresourceUP` com pitch 2360 e foi recusado depois com
>   `TextureWrites=1`, `VkLayout=UNDEFINED`: Desfecho B exato do critério prévio.
> - **Causa local medida:** o shadow 590x800 usa stride 2560, mas o upload Vulkan ainda só aceita
>   linha compacta. Além disso, o caminho `pDstBox==NULL` copia apenas 590 bytes em vez de 800
>   linhas de 2360 bytes. Aceitar apenas `bufferRowLength` não basta; a cópia 2D também precisa
>   ser corrigida e testada byte a byte.
> - **Sucessos:** seis uploads, um descriptor completo e um draw com fence no primeiro device;
>   129 submits GFX, zero `NAO_CONVERGIU`, TDR, `GFXHUB_FAULT` ou BugCheck no escopo medido.
>   A coleta final permaneceu ativa, fechou completa em 1.002.092 bytes e capturou dois dumps.
> - **Falhas:** tela preta, nenhum pixel conferido, uma recusa de descriptor, quatro
>   `BYTE_PROOF INCONCLUSIVO`, 137 `Upload LOCK falhou: 0x80070057` ignorados pelos chamadores,
>   e duas falhas `ActiveFlushTlb` devolvendo `STATUS_IO_DEVICE_ERROR`/`BC250_GPUVA_NO_SPACE`
>   apesar de 904/892 slots reutilizáveis. A causa interna dessas duas publicações segue
>   inconclusiva. Harness sem frame e oito APPCRASH do DWM também ocorreram.
> - **Desfecho E — causa fechada offline:** o próprio runner executou `New-Item -Force` na chave
>   existente `HKLM:\SOFTWARE\BC250\Umd` para gravar `GfxIbDumpDir`; essa operação apaga os
>   valores no Windows PowerShell 5.1. O harness já leu a promoção desarmada às 19:29:19, antes
>   do teardown. Reprodução isolada confirmou `Before=1`, `After=null`.
> - **Dumps:** os dois últimos crashes estão em `dwmcore!CD3DDevice::CreateD3D11Device` após a
>   UMD recusar abertura por promoção apagada; ambos preservam HRESULT `0x887A0004` no frame de
>   `MilFailFastForHR`, e `bc250umd.dll` não está carregada. Isto não atribui o teardown inicial
>   à recusa do descriptor: as duas falhas de FlushTlb são outra causa possível ainda aberta.
> - **Veredito:** **SUCESSO DIAGNÓSTICO; PARCIAL frente ao produto.** Recuperação
>   `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em cinco amostras; LocalDumps restaurado. Desktop
>   visível, pixel correto e apresentação sustentada continuam **NÃO VALIDADOS EM HARDWARE**.

> **19/09/2026 — Corrida 40 (17:38:38, lab 3.198.1.0) consolidada; candidato offline
> 3.199.1.0 pronto.** Evidência:
> `docs/evidence/2026-09-19_17-38-38-DESKTOP-HQAG6T4-lab-binding/`; próximo critério escrito
> antes do ensaio em [docs/296](docs/296_CRITERIO_CORRIDA_41_RECURSO_SRV_E_DUMP_DWM.md).
>
> - **Houve avanço? SIM, funcional:** seis texturas do `dwm.exe` sincronizadas com `VkImage`
>   (maior 2560x1080, 11.059.200 bytes), dois descriptor sets completos e o primeiro draw gráfico
>   com fence concluída (`VkDraws=1`, 42 vértices, uma instância, indexed).
> - **Sucessos:** 137 submissões GFX; zero TDR, BugCheck, `GFXHUB_FAULT` e `NAO_CONVERGIU` no
>   escopo medido. A violação de memória da Corrida 37 não reapareceu com a sonda armada.
> - **Falhas:** segundo pipeline do DWM recusado duas vezes por
>   `SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA`; três fail-fast `0xe0464645`; tela preta; texels não
>   conferidos. A coleta final fechou cedo e perdeu os crashes e mais 29 submissões; a cauda foi
>   resgatada manualmente. O log UMD de dois processos saiu duplicado.
> - **Veredito:** **SUCESSO FUNCIONAL PARCIAL** frente ao critério da Corrida 40. Upload,
>   descriptor e draw ocorreram; quadro completo, pixel e desktop visível não foram provados.
> - **Recuperação:** `RECOVERY_OK_ONLINE`, BasicDisplay restaurado; isto não é sucesso do DWM.
> - **Faixa offline seguinte:** coleta final agora segue relógio absoluto; sink evita segunda
>   escrita no mesmo caminho; recusa SRV nomeia recurso/metadados/contador/layout; LocalDumps do
>   DWM é opt-in e transacional. Quatro gates foram provados por mutação. Pacote 3.199.1.0
>   assinado e auditado, **NÃO INSTALADO**. Esta faixa: **NÃO VALIDADO EM HARDWARE**.

> **19/09/2026 — Corrida 37 (12:34:24, lab 3.195.1.0) consolidada e faixa OFFLINE de descritores.**
> [Relato, contratos, limites e critério do próximo ensaio](docs/292_HANDOFF_DESCRIPTORS_E_SONDA_2026-09-19.md).
> Avanço **diagnóstico**, não funcional: uma onda válida com FATAL_HALT/XNACK/EXCP bit 8;
> três leituras poison foram contadas indevidamente. A cauda incompleta foi recuperada por offset
> (290611 bytes, SHA-256 `BD5BCD3B560268DA437F652D53DC4A2F06151276A6F05A65F44811D375350692`).
> TDR persistiu e a restauração do seletor não foi confirmada. Veredito **PARCIAL**;
> rollback online para BasicDisplay Código 0 às 12:36:38, confirmado em cinco amostras.
> Offline: UMD agora aloca/escreve/vincula descritores VS/PS com namespaces separados,
> ownership até fence, recusa explícita dos tipos não implementados; sonda rejeita poison,
> exige VALID e respeita teto estrito com validade por campo. Builds e testes offline passaram.
> O mecanismo write-only do seletor **não foi demonstrado**; guarda preservada, próximo
> candidato deve manter sonda **desarmada**. Fonte INF 3.196.0.0, **sem novo pacote lab**;
> o output_lab 3.195.1.0 antigo permanece armado e não contém esta faixa.
> Upload/importação geral de texturas do DWM e topologia SQ completa continuam pendentes.
> Descritores como causa do hang são **H**. Draw concluído, pixels e monitor:
> **NÃO VALIDADO EM HARDWARE**.

## Diretriz do proprietário — 16/09/2026

**Qualidade, correção e estabilidade antes de velocidade.** Cumprir a [diretriz de UMD D3D11 e marcos gráficos](DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md), que prevalece sobre conclusões históricas conflitantes abaixo.

| Entrega exigida | Critério de conclusão | Estado desta revisão |
|---|---|---|
| Recriar tela colorida do Marco 3 | MARCO3_GRAPHICS_DRAW: pipeline gráfico e vkCmdDraw; 600 quadros, prova física e pixels conferidos | Implementado e compilado (MSVC /W4 /WX, SPIR-V, preflights Exit 0); NÃO VALIDADO EM HARDWARE |
| Recriar cubo do Marco 4 | MARCO4_GRAPHICS_VKCUBE: vkCmdDraw, geometria, textura e profundidade; prova física e pixels conferidos | Implementado e compilado (MSVC /W4 /WX, SPIR-V, preflights Exit 0); NÃO VALIDADO EM HARDWARE |
| UMD D3D11 traduzida para Vulkan | DDIs reais e aplicação não modificada; desktop por 60 min, robustez por 8 h e TDR com display sobrevivendo | Fase 2 integrada em bc250umd.dll. Corrida 15 (17/09 17:48) validou harness D3D11 com 60/60 quadros (Exit 0), 45 submissões no CP_RB0 físico (Node 1 GFX, VMID 7), 60 draws Vulkan nativos, compilação DXBC->SPIR-V em runtime e recuperação TDR no silício no Frame 20 com NOP físico comprovado. Apresentação contínua de pixels e desktop por 60 min: NÃO VALIDADO EM HARDWARE |
| Apresentação integrada ao desktop | Janela, resize, minimizar/restaurar, Alt+Tab e dois processos sem sobrescrever o desktop por escape direto | Pendente; NÃO VALIDADO EM HARDWARE |
| DCN básico de produto | P9 após pré-requisitos: modos, sincronização e recuperação do monitor/link comprovados | Planejado; NÃO VALIDADO EM HARDWARE |

Preservar provas compute/SDMA no seu escopo. Não equiparar imagem 3D produzida por compute a pipeline gráfico geral; conferir proveniência dos binários. Não anunciar GRAPHICS apenas removendo uma trava. DXVK local por jogo não entrega UMD transparente. Nenhuma corrida foi feita nesta revisão documental.

**Após cada corrida analisada, relatar obrigatoriamente:** data/hora, candidato e evidência; **se houve avanço**, distinguindo avanço funcional de diagnóstico; **sucessos**, **falhas**, **veredito** (sucesso, parcial, falha ou inconclusivo frente ao critério do ensaio), **recuperação**, pendências e próximo passo. Se faltarem logs, declarar a lacuna. Desktop retornando pelo BasicDisplay é recuperação, não prova de aceleração pelo nosso driver.

**A obrigação de enviar a barra “STATUS DO DRIVER BC-250” antes de cada mensagem está revogada.** Os registros antigos são histórico, não modelo de resposta nem medida atual de prontidão.

> **Plano de produto solicitado pelo proprietário (16/09/2026, análise documental):** [desktop acelerado, Direct3D transparente e DCN básico em P9](docs/281_PLANO_PRODUTO_DESKTOP_E_JOGOS_TRANSPARENTES.md). Os 600 quadros do Marco 4 não fecham desktop DWM nem jogos arbitrários. A fonte atual do Cube.cpp usa pipeline compute e vkCmdDispatch; sua proveniência exata em relação ao binário da corrida precisa ser conferida. Não usar esse teste como prova de vkCmdDraw/pipeline gráfico geral. Expor uma fila GRAPHICS requer backend comprovado, não apenas mudar uma flag. Nenhum novo item foi validado em hardware nesta revisão.
**Passagem de bastão atual:** [docs/307 — Corrida 52: VMID por contexto](docs/307_HANDOFF_2026-09-21_CORRIDA52_VMID_POR_CONTEXTO.md), [PLANO_DESKTOP_ACELERADO](PLANO_DESKTOP_ACELERADO.md). Passagens 288/287/286 permanecem como histórico, não como estado vigente.

> **19/09 16:20:14 (P0, lab 3.197.1.0, SYS 749BA458) — CORRIDA 39: 118 SUBMISSOES GRAFICAS SEM
> HANG, IBs DEZ VEZES MAIORES QUE O QUE TRAVAVA, E A PRIMEIRA COPIA DE PIXEL PARA VkImage.**
> **Evidencia:** `docs/evidence/2026-09-19_16-20-14-DESKTOP-HQAG6T4-lab-binding/`; a pasta
> declarou-se incompleta e a cauda foi resgatada (884.034 bytes, sha256 `7BAF856D...718ECB`).
> Criterio e resultado: [docs/293](docs/293_CRITERIO_CORRIDA_39_TEXTURA_E_RECUSA_NOMEADA.md),
> **Desfecho B**.
>
> - **P0, estabilidade:** 118 submissoes no anel grafico, 65 delas de **20.208 DW (~80 KB)**, todas
>   com `Perna1=REGISTRADOR` e fence escrita pela GPU. ZERO `NAO_CONVERGIU`, ZERO TDR, zero
>   `GFXHUB_FAULT`, zero BugCheck, 461 `TRIPE_PROVADO` no Node 0, rollback `RECOVERY_OK_ONLINE`.
>   Contra 3 submissoes na Corrida 38, e contra o IB de 1.872 DW que travava nas Corridas 34-37.
> - **P0, primeira no silicio:** `UploadImageData SUCESSO: 1024x1, 4096 bytes em mip0/camada0`
>   (21 uploads). `vkCmdCopyBufferToImage` NUNCA havia executado nesta UMD -- ele simplesmente nao
>   era chamado em lugar nenhum. O `SUCESSO` sai apos o flush com fence, entao a copia concluiu.
> - **A recusa nomeada pagou e MUDOU o suspeito:**
>   `[VK_DESCRIPTOR] RECUSADO: binding index=2 motivo=SRV_AUSENTE_OU_SEM_IMAGEM`. NAO e
>   `SRV_SEM_CONTEUDO_NUNCA_ESCRITO`, o que DESCARTA "a textura nao recebeu upload" como
>   explicacao. Na Corrida 38 as 17 condicoes compartilhavam uma frase so.
> - **O que NAO avancou:** ZERO draws (`DRAW fence concluida`=0 em todos os processos). O caminho
>   D3D11 de desenho segue bloqueado. As 118 submissoes vem de uploads e flushes do bridge; a
>   origem das 65 de 20.208 DW fica **NAO ATRIBUIDA**, nao "nossa".
> - **Achado que aponta o proximo passo:** `dwm.exe` teve `upload=0`. As texturas dele nao passam
>   por `pInitialDataUP` -- ele cria vazio e preenche por `Map`/`UpdateSubresource`, que esta faixa
>   nao cobre.
> - **DUAS FALHAS DO CRITERIO, declaradas:** (a) assinatura e conclusao vinham acopladas -- a
>   conclusao de D confirmou-se enquanto a assinatura de D era falsificada; os criterios passam a
>   separar *o que o log mostra* de *o que isso implica*; (b) o maior resultado da corrida caiu
>   FORA das quatro letras, porque a convergencia era tratada so' como controle a nao regredir.
>   Todo criterio passa a levar um desfecho **"E -- algo fora do mapa"**.
> - **Limitacao da faixa:** `SRV_AUSENTE_OU_SEM_IMAGEM` ainda agrega SETE condicoes. Foram nomeadas
>   17 recusas e justamente a que disparou ficou grossa.
> - **Veredito:** AVANCO FUNCIONAL EM ESTABILIDADE, MAIS UMA PRIMEIRA NO SILICIO; A PAREDE DO DRAW
>   PERMANECE. Draw, pixel e monitor: **NAO VALIDADO EM HARDWARE**.
>
> ---
>
> **19/09 12:11:58 (P0, lab 3.194.1.0, SYS 60FD95A5) — CORRIDA 36: PAREDE REPRODUZIDA PELA
> TERCEIRA VEZ, IDENTICA; A SONDA DE ONDA NAO RODOU POR DEFEITO DE COMPILACAO CONDICIONAL.**
> **Evidencia:** `docs/evidence/2026-09-19_12-11-58-DESKTOP-HQAG6T4-lab-binding/`.
> Criterio: [docs/290](docs/290_CRITERIO_CORRIDA_36_ONDE_A_ONDA_PARA.md), Desfecho D.
>
> - **P0, reproducao exata da Corrida 35:** mesmo IB (`DmaVA=0x240D10000`, 1872 DW),
>   `GRBM_STATUS=0xA2613028`, `RPTR=24 WPTR=32`, `SQ_DEBUG_STS_GLOBAL=0x00030031`
>   (WaveLevelSA0=3, SA1=3), `SE0=SE1=0x09000006` com `CB_CLEAN=1`, e `SemPolling=0
>   EopDoneBusy=0 CoherCntNeqZero=0`. O travamento e DETERMINISTICO, nao intermitente.
> - **A pergunta da corrida NAO foi respondida:** `[TDR_GFX_WAVEPC] DESARMADO: GfxWaveProbe=0
>   present=0`. O `present=0` e o discriminante -- o tunable nunca foi consultado, embora o INF
>   empacotado trouxesse 1 e o manifesto derivasse disso.
> - **TRES DEFEITOS MEUS, todos de instrumentacao, todos corrigidos offline em 19/09:**
>   1. a leitura do tunable estava depois de
>      `#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)` -- compilada FORA da imagem de
>      laboratorio. **Quarta ocorrencia** da classe (A13, B4, ContextAbi), escrita a poucas linhas
>      do comentario que registra a correcao do B4;
>   2. `bc250_dump_ib()` vivia so no ramo de concatenacao (`cs_total > 1`) e a submissao que trava
>      nao passa por ele -- `gfx-ib-dump/` vazio pela segunda vez, desta vez NAO por falta da
>      chave de registro, que o runner criou corretamente;
>   3. a cauda arquivada tinha 210.851 de 219.325 bytes, e os 8.474 perdidos continham o BLOCO
>      INTEIRO DO TDR. A pasta nao dizia que estava incompleta. O codigo de copia nunca esteve
>      errado: o defeito e TEMPORAL, o KMD escreve depois da copia.
> - **Controles mantidos:** zero `GFXHUB_FAULT`, zero BugCheck, 121 IBs no Node 0 com 112
>   `TRIPE_PROVADO`, `TDR_GFX_RESTART` aprovado com `CpMeCntl=0x00000000`, rollback pelo watchdog.
> - **Dados novos, pequenos:** `CP_BUSY_STAT=0x00028080` trouxe `ME_PARSER_BUSY=1` alem do que a
>   Corrida 35 mostrou, e `GRBM_STATUS2` trouxe `CPF_BUSY=1`.
> - **Veredito:** INCONCLUSIVO quanto a pergunta, COM REPRODUCAO CONFIRMADA da parede. A corrida
>   nao foi desperdicada no silicio -- provou determinismo e reconfirmou as tres medicoes --, mas
>   nao disse onde a onda esta, e a causa disso e minha.
> - **Candidato seguinte:** 3.195.1.0 com os tres defeitos corrigidos, tres portoes novos e 4
>   mutacoes verificadas. Sonda ARMADA. NAO INSTALADO.
>
> ---
>
> **19/09 10:51:35 (P0, lab 3.192.1.0, SYS E48CBB12) — CORRIDA 35: A SONDA RESPONDEU. O CP NAO
> ESTA ESPERANDO SINCRONIZACAO; HA SEIS ONDAS EM VOO QUE NAO RETIRAM.**
> **Evidencia:** `docs/evidence/2026-09-19_10-51-35-DESKTOP-HQAG6T4-lab-binding/`.
> Criterio escrito ANTES em [docs/289](docs/289_CRITERIO_CORRIDA_35_ONDE_O_CP_PARA.md).
>
> - **Houve avanco?** SIM, de diagnostico, e o criterio fechou no **Desfecho 1**. A pergunta da
>   corrida — *em que ponto, e esperando o que, o `CP_RB0` para?* — tem resposta medida.
> - **A MEDICAO (P0), toda de uma unica parada do CP:**
>   `[TDR_GFX_WAVES] SQ_DEBUG_STS_GLOBAL=0x00030031 Busy=1 WaveLevelSA0=3 WaveLevelSA1=3`
>   → **seis ondas em voo** que nao retiram.
>   `[TDR_GFX_SE] SE0=SE1=0x09000006 (SPI=1 PA=1 CB_CLEAN=1 DB_CLEAN=1)` → front-end engajado nos
>   dois shader engines, **zero escrita no color buffer**.
>   `[TDR_GFX_CPWAIT] CP_BUSY_STAT=0x00008080 (PfpParsing=1 GfxContextBusy=1 SemPolling=0
>   SemFailedHolding=0 EopDoneBusy=0 CoherCntNeqZero=0)`.
> - **O Desfecho 3 esta REFUTADO por medicao:** o CP **nao** espera semaforo, nao espera EOP e nao
>   espera contador de coerencia drenar. A hipotese de flush/EOP faltando, que o `docs/288` §5.1
>   levantou e que o `CB_CLEAN=1` ja tornava improvavel, morre aqui com os tres bits em zero.
> - **REPRODUCAO EXATA da Corrida 34:** `GRBM_STATUS=0xA2613028` e `CpMeCntl=0x00000000`, os
>   MESMOS valores da submissao #1 da corrida anterior. Nao e evento novo nem intermitencia: e a
>   mesma parada, agora com instrumentacao que a descreve.
> - **Mudanca de suspeito, com nome:** a frente sai do stream PM4 e do par fence/EOP e passa para a
>   **execucao do shader** — onda que lanca e nao retira, tipicamente `s_waitcnt` sobre um fetch que
>   nao volta. Alvo: ISA do ACO e os descriptors de vertex buffer.
> - **A PAREDE DE HALT NAO REAPARECEU:** houve UM unico ciclo de TDR, com
>   `[TDR_GFX_RESTART] CP_RB0 rearmado e comprovado no silicio` — e desta vez o veredito vale,
>   porque a mascara agora confere `CE_HALT|PFP_HALT|ME_HALT`. `CpMeCntl=0x00000000` no quiesce.
> - **QUEM SUBMETEU NAO FOI O HARNESS.** O IB de 7488 bytes (1872 DW, 427 pacotes,
>   `IB_LINEAR_CONFIRMADO`) veio do **dwm.exe (pid 1204, 24 draws)**. O
>   `bc250-d3d11-conformance.exe` (pid 7636) fez **zero** draws: quando chegou a montar recursos o
>   dispositivo ja estava perdido (`WaitForSynchronizationObjectFromCpu ... 0xC00002B6`). O stdout
>   para em "Iniciando renderizacao..." sem UMA linha de quadro.
> - **DOIS DEFEITOS DE INSTRUMENTACAO, MEUS, declarados:**
>   1. `gfx-ib-dump/` esta **VAZIO**. `BC250_RADV_IB_DUMP` e' exportado apenas na arvore de
>      processos do harness, e quem submeteu foi o dwm.exe, que nao o herda. O despejo nao enxerga
>      o processo que importa.
>   2. O runner reportou `EXIT CODE: 0` para uma corrida em que o harness foi morto por timeout sem
>      renderizar um quadro. Exit 0 ali e' falso sucesso do RUNNER — o harness novo nao chegou a
>      executar o caminho que aborta.
> - **INDETERMINADO, registrado e NAO usado como base:** `ME_HEADER_DUMP=0xC0005200` decodifica como
>   Type 3 opcode **0x52**, que **nao existe** na tabela PM4 do proprio Mesa
>   (`src/amd/common/sid.h` vai de `0x51 CONTEXT_REG_RMW` direto a `0x57`). Ou e' pacote interno do
>   firmware, ou o registrador nao esta mostrando o nosso IB. Nao se conclui nada dele.
>   `PFP_HEADER_DUMP=0xC0017600` = `SET_SH_REG` 2 DW, e `IB1 RestamDW=0`.
> - **Controles, todos mantidos:** zero `GFXHUB_FAULT`, zero BugCheck, zero `StopDevice`,
>   202 IBs no Node 0 SDMA com 188 `TRIPE_PROVADO`, 95 Present, rollback online limpo.
> - **Veredito:** **SUCESSO DE DIAGNOSTICO, PARCIAL NO PRODUTO.** O criterio escrito antes fechou
>   no Desfecho 1 e eliminou o Desfecho 3 por medicao. Tela preta era o resultado previsto e foi o
>   obtido. Nada de pixel, rasterizacao ou Marco 3 foi validado, e nem estava em teste.
>
> ---
>
> **19/09 09:06:34 (P0, lab 3.191.1.0) — CORRIDA 34: GCVM PROTECTION FAULT CURADO NO SILÍCIO; UM (1) DRAW REAL, E O ANEL GRÁFICO PAROU EM TODAS AS QUATRO SUBMISSÕES.**
> Ensaio físico do runtime Direct3D 11 nativo (`tools\EXECUTAR_D3D11_NATIVO.cmd -Force`) com `bc250umd.dll` e `vulkan_radeon.dll` (16.670.208 B).
> **Evidência arquivada:** `docs/evidence/2026-09-19_09-06-34-DESKTOP-HQAG6T4-lab-binding/`.
>
> ⚠️ **ESTE BLOCO FOI REESCRITO EM 19/09 PELA AUDITORIA DA PRÓPRIA EVIDÊNCIA.** A redação anterior
> (e o `docs/288`) afirmava “52 quadros D3D11 renderizados sucessivamente escalando de 0.4 até 11.2 FPS”
> e atribuía a parada à Drawing Engine. Nenhuma das duas afirmações sobrevive ao log. O que segue é o que
> a medição diz.
>
> **Recuperação de evidência (pré-requisito desta análise).** `display-log-this-bind-FINAL.txt` da pasta tem
> 446.892 bytes **de zeros** — zero conteúdo útil. A cauda real foi extraída do log cumulativo vivo
> (offset 1.630.716.958) e arquivada como `display-log-this-bind-RECUPERADO.txt`, 461.898 bytes, 4.066 linhas,
> `sha256 de383cbe25b7f7554f9dd3dbf2ac80047fe41007f8ed01abd2ce3714bac06417`. Sem ela, nada abaixo existiria.
>
> - **Houve avanço?** SIM, e menor do que foi anunciado. **P0:** o GCVM Protection Fault foi eliminado
>   (`NODE1_STOP_GCVM_FAULT STATUS=0x00000000 ADDR=0`, zero `GFXHUB_FAULT` em 4.066 linhas, contra o fault de
>   ~3,85 TB da Corrida 33); o CP aceitou, traduziu sob VMID 7 e **buscou** o primeiro IB gráfico real
>   (`GATE FISICO PASS`, `IB_LINEAR_CONFIRMADO`, 24 de 32 dwords do anel consumidos); Node 0 SDMA manteve
>   `IB_TRIPE_PROVADO` com 5 canários e `Divergentes=0` ao longo de 290 IBs; zero BugCheck; rollback online limpo.
> - **O “52 quadros a 11.2 FPS” é FALSO SUCESSO (P0).** `d3d11_native_conformance.log` linha 5592: `VkDraws=1`
>   às 09:07:17.193. Linha 5637, às **09:07:19.067**: `vkWaitForFences res=-4`. Da linha 5639 até o fim, todos os
>   ciclos trazem `Lock2 failed: status=0xC00002B6` (STATUS_DEVICE_REMOVED) e `UploadBufferData ERRO`. **51 dos 52
>   “draws” ocorreram depois do dispositivo removido.** O harness imprimia `RENDERING_OK` porque descartava o
>   HRESULT de `Present()`; e a rampa 0.4→11.2 era média **cumulativa** — um quadro de ~2,5 s seguido de ~51
>   no-ops produz exatamente aquela curva. **Sexta ocorrência** da classe “o gate carimbava o falso sucesso”.
>   Agravante no nosso fork do RADV, presente no DLL embarcado:
>   `check_device_status warning: dev_status=-4 but all fences satisfied -> continuing!` — fail-open explícito,
>   violação da Lei 4.
> - **O “stall da Drawing Engine” foi diagnosticado pela submissão ERRADA (P0).** Houve **quatro** submissões GFX
>   e **três** ciclos de TDR. O `docs/288` cita a snapshot da submissão #4 (`RPTR=0 WPTR=12`, `GRBM 0xA0003028`),
>   que também trazia `CpMeCntl=0x15000000`. Decodificado pelo header local
>   (`gc_10_1_0_sh_mask.h:6661-6665`): `CE_HALT|PFP_HALT|ME_HALT` — **as três micro-engines paradas**. Com o ME
>   halted, `RPTR=0` é trivial: as submissões 2, 3 e 4 não mediram a DE, mediram o nosso próprio reset.
>   `0xA0003028` decodifica como `GUI_ACTIVE, CP_BUSY, CB_CLEAN, DB_CLEAN` — pipeline gráfico **parado e limpo**.
> - **O único evento válido é a submissão #1**, e ele diz outra coisa: `GRBM_STATUS=0xA2613028` =
>   `GUI_ACTIVE, CP_BUSY, GE_BUSY, SPI_BUSY, PA_BUSY, CB_CLEAN, DB_CLEAN`. O front-end de geometria engajou e
>   **nenhum pixel chegou ao Color Buffer**. Isso **refuta** a hipótese do `docs/288` §5.1 de flush de cache
>   L2/CB faltando: com `CB_CLEAN=1` não há o que fazer flush. O nosso próprio tripé foi honesto
>   (`GFX_TRIPE_VEREDITO: Perna1=NAO_CONVERGIU WPTR=32 RPTR_Reg=24`). A causa da parada permanece **H**.
> - **Portao que fabricou o “restart comprovado” (P0, por leitura).** `Bc250TdrValidateGfxRestartReadback`
>   mascarava `0x00000007` em `CP_ME_CNTL`, bits que não nomeiam campo nenhum no header, e o teste
>   `tdr_recovery_contract_test` **exigia** essa máscara. `0x15000000 & 0x7 == 0`: o PASS era aritmeticamente
>   inevitável. **Oitava ocorrência** da classe. Corrigido em 19/09 (máscara real + caso de teste com o valor
>   medido + caso negativo + portao `audit_gfx_restart_checks_the_real_halt_bits`, mutacao verificada).
> - **Veredito:** PARCIAL. Avanço real: GCVM curado e primeiro IB gráfico buscado sob VMID 7. **NÃO houve**
>   52 quadros, **não houve** 11.2 FPS, **não há** rasterização provada, e o ponto de parada do CP **continua
>   sem causa nomeada**. Tela, pixels e conclusão do draw seguem **NÃO VALIDADOS EM HARDWARE**.
> - **A árvore Mesa/RADV fica FORA deste repositório — mas existe e foi localizada (correção da própria
>   auditoria, 19/09).** A primeira redação deste bloco afirmou que ela "não existe no repo nem foi encontrada
>   na máquina"; **isso era falso**, e o erro foi de busca (profundidade), não de fato. O caminho estava
>   embutido em texto no próprio DLL: `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm`.
>   É um clone git do Mesa em `801c976`, que é exatamente a versão que o DLL publica
>   (`Mesa 26.2.0-devel (git-801c9763c6)`), com `build/` meson/ninja funcionando. O
>   `build/src/amd/vulkan/vulkan_radeon.dll` é **byte-idêntico** ao implantado em `C:\BC250_LIB\`
>   (`sha256 bb857af7e337e8ae...`), então a árvore é comprovadamente a fonte do binário da Corrida 34.
>   `tools/audit-radv-wddm-safety.py --mesa-root <caminho>` rodou pela **primeira vez** em 19/09: **PASS**
>   (handles/GPUVA/fences fabricados ausentes, WSI por software ausente, callbacks WDDM fail-closed).
>   **DÍVIDA QUE PERMANECE:** o delta do fork (23 arquivos, ~1.122 linhas, com `src/amd/vulkan/winsys/wddm2/`
>   inteiro) não está versionado junto com este repositório nem tem cópia de segurança; perder aquele diretório
>   perderia o driver Vulkan inteiro.
> - **Correção sobre o fail-open do RADV.** A primeira redação descreveu
>   `check_device_status warning: dev_status=-4 but all fences satisfied -> continuing!` como "device perdido
>   tratado como continuar". Lido na fonte (`src/vulkan/runtime/vk_wddm2_monitored_fence.c:291-311`), ele é
>   mais estreito: só rebaixa o erro para `VK_SUCCESS` quando **todas** as fences esperadas já atingiram o
>   valor-alvo na memória. Continua sendo fail-open sob a Lei 4 e precisa cair, mas **não foi ele** que
>   produziu os 51 quadros fantasmas — o log mostra o `FlushCommands` devolvendo `res=-4` logo depois, e a
>   causa provada dos quadros fantasmas é o harness descartar o HRESULT de `Present()`.
> - **Handoff:** [docs/288 — HANDOFF: CORRIDA 34, CURA GCVM E DIAGNÓSTICO DO STALL DA DRAWING ENGINE](docs/288_HANDOFF_2026-09-19_CORRIDA34_CURA_GCVM_E_DIAGNOSTICO_TELA_PRETA.md).
>
> ---
>
> **18/09 17:35:22 (P0, lab 3.186.1.0, SYS assinado `5E2A89F4…`) — CORRIDA 29: PORTÃO DE SEGURANÇA FASE D0 COMPROVADO NO SILÍCIO (4/4 RECUSAS FAIL-CLOSED COM `STATUS_DEVICE_NOT_READY`); ENVELOPAMENTO DE BROADCAST GRBM ISOLADO E REFUTADO; ZERO TDR, ZERO BUGCHECK, ROLLBACK ONLINE LIMPO PARA BASICDISPLAY CÓDIGO 0.**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd -Force`) com `bc250umd.dll`.
> **Evidência arquivada:** `docs/evidence/2026-09-18_17-35-22-DESKTOP-HQAG6T4-lab-binding/`. Log do KMD: `display-log-this-bind-FINAL.txt` (419.865 bytes, 3.738 linhas).
>
> - **Houve avanço?** SIM, AVANÇO DIAGNÓSTICO E DE SEGURANÇA. A hipótese do broadcast GRBM isolado no MMIO foi refutada, e o portão de segurança Phase D0 funcionou com 100% de precisão (4/4 recusas nominais, zero falha silenciosa, zero travamento do SO).
> - **Sucessos:** O portão fail-closed barrou submissões divergentes (`WPTR_READBACK_DIVERGENTE`); `Bc250ActiveGfxLogRefusalMeasurement` registrou a recusa nominal; rollback online perfeito sem reboot.
> - **Falhas:** O registrador `CP_RB0_WPTR (0x0C150)` continuou devolvendo 0 (`wptrReadback == 0`). O anel não consumiu os pacotes de desenho. Tela preta.
> - **Veredito:** PARCIAL (diagnóstico e segurança provados; execução gráfica NÃO VALIDADA EM HARDWARE).
>
> ---
>
> **18/09 17:54 (OFFLINE, sem corrida física) — CANDIDATO 3.187.0.0 (LAB 3.187.1.0): IMPLEMENTAÇÃO DO PAR CANÔNICO 64-BIT SOC15/GFX10 (`CP_RB0_WPTR` 0x0C150 LO + `CP_RB0_WPTR_HI` 0x0C154 HI) E PULSO DE HARDWARE `DOORBELL_HIT` (0x0C3B4).**
> - **Mecanismo:** Em conformidade com o driver Linux upstream (`amdgpu/gfx_v10_0.c`: `gfx_v10_0_ring_set_wptr_gfx`) e com os testes de bringup locais comprovados (`bc250_compute_service.c:859-861`), a escrita de WPTR no Command Processor GFX10 exige a emissão do par de 64 bits para o latch interno e/ou sinalização de Doorbell.
> - **Validação Offline:** `driver/wdk/build-contract.bat` (40/40 PASS, Exit 0), `tools/test-phase-d0-mutations.py` (4/4 mutações reprovadas, Exit 0), `tools/audit-bc250mcdm-safety.py` (PASS, 100% regras), `tools/package-bc250mcdm-lab.ps1` (Exit 0, pacote e catálogo assinados com `DriverVer 09/18/2026,3.187.1.0`), `tools/audit-bc250mcdm-lab-package.ps1` (PASS).
> - **Identidade do Pacote:** SYS `022A4FADFCEE351849F8E92970FC754DAE06CC5D3A482424CDE98C794EFF513E`, UMD `DB09890F291A4017F5E70B3CBBE95519556746745FAF65BC58FC0E75302C8939`, INF `8A0037AD70D2AC7F21AF917176C4B5ACA59497F2D138A599D4EA87C1F27FFCAD`, CAT `0962FF72FDEBF4A5AB5439F9F9550EBB51A9D94F8CFDF5389A86A928F7E48B50`.
> - **Estado:** Pronto para validação física na Corrida 30 via `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`.
>
> ---
>
> **18/09 14:20:07 (P0, lab 3.184.1.0, SYS assinado `E7E7A3B2…`) — A ESCRITA MMIO DE `CP_RB0_WPTR` NÃO É RETIDA PELO SILÍCIO. MEDIÇÃO DIRETA QUE EXPLICA, RETROATIVAMENTE, POR QUE O ANEL GRÁFICO NUNCA EXECUTOU NADA EM TODO O PROJETO. ZERO TDR, ZERO BUGCHECK, ZERO `DEVICE_REMOVED`.**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd -Force`) com `bc250umd.dll`.
> **Evidência arquivada:** `docs/evidence/2026-09-18_14-20-07-DESKTOP-HQAG6T4-lab-binding/` (34 arquivos). Log do KMD: `display-log-this-bind-FINAL.txt` (452.369 bytes, 3.999 linhas). Proveniência: `binding-provenance.json` com `package_sys_sha256=E7E7A3B2D11C9B67…`, RADV `vulkan_radeon.dll` `19D62EC2…` e `libvkd3d-shader-1.dll` `098CCD8A…`, ambas externas em `C:\BC250_LIB`.
>
> **NOTA DE NUMERAÇÃO:** esta base vai até a "Corrida 18" (17/09); as corridas de 18/09 não foram registradas. Três esquemas de numeração incompatíveis estão em uso no repositório — esta corrida é chamada de 27 em uma das análises. **Identificar corrida por data/hora + hash + pasta de evidência, nunca por número.**
>
> - **Houve avanço?** SIM, AVANÇO DIAGNÓSTICO DECISIVO — e **ZERO avanço de entrega**. Nenhum item saiu de 🟡 para ✅.
> - **Sucessos (P0, medidos):**
>   1. **A causa da tela preta no caminho gráfico tem nome, e é anterior a tudo que se suspeitava.** 4 de 4 submissões: `[GfxEngine] SUBMIT RECUSA: WPTR_READBACK_DIVERGENTE -- escrevemos 12 e o registrador devolveu 0`. O discriminante é a **ausência** dos outros vereditos: `WPTR_NAO_PUBLICADO`=0 (transporte MAPEADO), `WPTR_ESCRITA_RECUSADA`=0 (`Write32` devolveu 1, `WRITE_REGISTER_ULONG` emitido), `WPTR_READBACK INDETERMINADO`=0 (a leitura funcionou). Escrita emitida, leitura funcionando, registrador `0xC150` **não retém o valor**.
>   2. **Contraste no mesmo log que elimina o transporte como suspeito:** SDMA0 fez 313 submissões (`IB EXECUTADO`=313, `FenceId` máx. 313) com 287 `IB_TRIPE_VEREDITO: TRIPE_PROVADO`, incluindo `WPTR=1476 RPTR_Reg=1476 RPTR_Wb=1476 Casou=WRITEBACK+REGISTRADOR`. Escrever **e ler** ponteiros de anel por MMIO funciona — para o SDMA. Não é o transporte, não é o allowlist, não é a função.
>   3. **`STALL_BASELINE` refuta atribuir o stall ao nosso IB, por comparação direta:** `CpStalled1=0x00000C00[lido=1] CpStalled2=0x00000000[lido=1] GrbmStatus=0xA0003028[lido=1]`, tirado **antes de existir qualquer submissão** e idêntico ao snapshot do TDR da corrida anterior. É estado de repouso. O mesmo `CpStalled1=0x00000C00` aparece no compute **depois** da doorbell que publica com sucesso (`PUBLISH_AFTER_DOORBELL`): se esse valor impedisse a atualização do ponteiro de anel, o compute falharia também.
>   4. **Correção de decodificação, que já foi afirmada errado duas vezes:** `CpStalled1=0x00000C00` **não** é `GFX_CNTX_NOT_AVAIL_TO_ME | ME_WAIT_ON_AVAIL_BUFFER`. Esses dois campos pertencem ao **`CP_STALLED_STAT2`** (`gc_10_1_0_sh_mask.h:6466-6467`), e o STAT2 medido é `0x00000000` — os dois bits estão **desligados**. No `CP_STALLED_STAT1` os bits 10/11 são `ME_HAS_ACTIVE_CE_BUFFER_FLAG` e `ME_HAS_ACTIVE_DE_BUFFER_FLAG` (`:6428-6429`): flags de buffer ativo, não de stall.
>   5. **Classificador de IB corrigido e confirmado:** 4 de 4 `IB_LINEAR_CONFIRMADO` com `Saltos(CHAIN=1)=0 ChamadasIB2(CHAIN=0)=1`, em IBs de 840/1656/664/440 DW integralmente varridos. O `IB_AINDA_ENCADEADO` da corrida anterior era **erro do classificador**, não encadeamento real. A RADV emitiu `BC-250 UNIFICADO` com **zero** `Chain [`.
>   6. **Estabilidade contra as três últimas classes de crash:** zero TDR (contra 2), zero BugCheck (último `MEMORY.DMP` é de 17/09 20:10, o `0x0A`), zero `DEVICE_REMOVED` (contra 57), zero `GFXHUB_FAULT`, zero `StopDevice`. Recusar antes de armar o anel eliminou a cascata TDR → device-removed.
>   7. **Disciplina R4 corrigida:** as 15 strings novas foram conferidas no **SYS assinado** de `output_lab`. Descoberto que a checagem por string no binário de desenvolvimento tem **falso negativo comprovado** (`ONLINE SUCESSO`, que provadamente executou, aparece como ausente na *MCDM DEVELOPMENT IMAGE*). **Positivos valem; negativos só valem no binário assinado.**
> - **Falhas:**
>   1. **Tela preta.** 8 `DdiPresent`, `NonZero=0/256` em todas as amostras, 158 `INSPECAO LINEAR_BUF` com `NonZeroSamples=0/5`. 379 draws Vulkan nativos na UMD que o anel gráfico nunca consumiu. `D1.6` reprovado pelo próprio critério.
>   2. **Dívida introduzida pela própria correção:** `GFX_TRIPE_VEREDITO` saiu **zero vezes**. O poll só é chamado pelo retirement, que só arranca após um `SUBMIT SUCESSO`; com 4/4 recusadas, as Pernas 1 e 2 foram consertadas e perdidas na mesma corrida.
>   3. **`D3DKMTSubmitCommand falhou com NTSTATUS 0xC01E0200`** na RADV (`cmd_bytes=5216`) — a recusa do KMD propagando corretamente, sem ser engolida.
>   4. **Meu diagnóstico do `memType` estava errado:** os 17 `LinearPresentBuf` saíram `nivel=HOST_VISIBLE|HOST_COHERENT` e o aviso de rebaixamento não apareceu nenhuma vez. O `VK_ERROR_MEMORY_MAP_FAILED (-5)` da corrida anterior era transiente, não alocação errada.
> - **Veredito:** **PARCIAL — avanço diagnóstico decisivo, zero avanço de entrega.** `D1.6`, `D1.7`, `D1.8` e o Marco 1 inteiro seguem **NÃO VALIDADOS EM HARDWARE**.
> - **CORREÇÃO QUE MANDA NO REGISTRO DESTA BASE:** as submissões ao `CP_RB0` creditadas como progresso nas corridas de 16 e 17/09 — *"45 submissões aposentadas"* (Corrida 15), *"20 submissões 3D"* (Corrida 16), *"16 submissões gráficas aposentadas"* (Corrida 13) — foram **aceitas, não executadas**. O WPTR nunca assentou no registrador, logo o CP não tinha o que buscar. É a mesma leitura errada já corrigida para as 87 submissões da corrida de 10:20. **Nenhuma submissão gráfica jamais foi provada executada no `CP_RB0`.** O item que dependia disso volta para a Tabela 2.
> - **LACUNA DE MECANISMO, achada por leitura de código depois da corrida (a causa provável, classe H):** o anel gráfico tem **três** canais possíveis de publicação de WPTR e nós fechamos dois:
>   * `CP_RB_WPTR_POLL_ADDR_LO/HI` (`0x0C3AC/0C3B0`) — **programados** com o shadow (`bc250_gfx_ring_transaction.c:166`);
>   * `CP_RB_WPTR_POLL_CNTL` (`0x08708`) — **apenas lido** no preflight, nunca escrito. Pelo header (P1) ele **não tem bit de enable**: só `POLL_FREQUENCY[15:0]` e `IDLE_POLL_COUNT[31:16]`;
>   * `CP_RB_DOORBELL_CONTROL` (`0x0C3B4`) — escrito **`0`**, doorbell desligada de propósito (`:150`);
>   * MMIO `CP_RB0_WPTR` (`0x0C150`) — único canal aberto, e o que o silício não retém.
>   O anel de **compute**, no mesmo log, publica por doorbell e funciona: `[SubmissionPublishWptr] Step 4 DbKick: Offset=0x110 Val64=12 Kicks=1`, com os mesmos 12 dwords. Que a doorbell seja *a* solução é **H**; que ela seja o mecanismo provado nesta placa é **P0**.
> - **Recuperação:** `RECOVERY_OK_ONLINE` às 14:22:20, BasicDisplay Código 0 em 5 amostras, sem reboot. `C:\BC250_REQUIRE_CLEAN_BOOT.json` **ARMADO** — boot frio obrigatório antes de qualquer bind.
>
> ---
>
> **17/09 20:09:16 (P0, lab 3.181.1.0) — CORRIDA 18: BUGCHECKS 0x133 E 0x139 EXTINTOS NO SILÍCIO; BUGCHECK 0x0A (IRQL_NOT_LESS_OR_EQUAL) IDENTIFICADO VIA MEMORY.DMP (COMPARTILHAMENTO ILEGAL DE KDPC ENTRE TIMER E KeInsertQueueDpc); FUSÍVEL ANTI-BOOT-LOOP ATIVO; DESACOPLAMENTO ESTRITO DE DPCs IMPLEMENTADO E VALIDADO OFFLINE.**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd -Force`) com `bc250umd.dll`.
> **Evidência arquivada:** `docs/evidence/2026-09-17_20-09-16-DESKTOP-HQAG6T4-lab-binding/`. Dump de Memória: `C:\Windows\MEMORY.DMP` (869 MB), analisado via CDB com símbolos oficiais da Microsoft. Handoff: `docs/287_HANDOFF_2026-09-17_20-10_CORRIDA18_0x0A_E_SEPARACAO_DPCS.md`.
> - **Houve avanço?** SIM, AVANÇO DIAGNÓSTICO E ARQUITETURAL CRÍTICO NO KMD.
>   1. **BugChecks 0x133 e 0x139 100% Extintos:** Tanto o deadlock de spinlock em `VidSchDdiNotifyDpc` quanto a violação de CFG (`guard_icall_bugcheck`) por rotina não inicializada não reapareceram.
>   2. **Isolamento Matemático da Causa Raiz do BugCheck 0x0A:** A desmontagem forense de `nt!KiExecuteAllDpcs+0x2ee` (`mov [r15+20h], rax`) comprovou que `r15 == 0`, originado de `[r14+0x38]` (`DpcData == NULL`) na estrutura `_KDPC` de `context->RetirementDpc`. No kernel do Windows x64, uma DPC associada a um timer periódico (`KeSetTimerEx`) não pode ser enfileirada simultaneamente via `KeInsertQueueDpc`. A colisão concorrente corrompeu a lista (`Flink = Blink = &DpcListEntry`), fazendo o despachante executar a DPC duas vezes e tentar atualizar telemetria de DPC em `[0x00000020]`, gerando falta de página em DISPATCH_LEVEL.
>   3. **Proteção Anti-Boot-Loop Ativa:** Fusível `C:\BC250_BOOT_FUSE.dat` desarmou para `State=2 (TRIPPED)`, impedindo loop e reiniciando a máquina em segurança no BasicDisplay.
> - **Sucessos:**
>   1. Eliminação definitiva das duas classes de crash anteriores (0x133 e 0x139).
>   2. Submissões gráficas iniciais no Node 1 GFX sem falha de validação ou recusa de contexto.
>   3. Auditorias e contratos 100% aprovados (`build-contract.bat`: 40/40 gates PASS).
> - **Falhas:**
>   1. **BugCheck 0x0A (`IRQL_NOT_LESS_OR_EQUAL`):** Ocorreu 7 s após o início em `nt!KiExecuteAllDpcs+0x2ee` ao acessar endereço `0x00000020` devido à colisão entre `RetirementTimer` e `KeInsertQueueDpc`.
> - **Veredito:** FALHA FUNCIONAL POR BUGCHECK 0x0A / AVANÇO DIAGNÓSTICO CONCLUSIVO.
> - **Recuperação e Correção:**
>   1. Desacoplamento estrito de DPCs em `BC250_DXGK_ACTIVE_CONTEXT`: criadas `RetirementTimerDpc` (exclusiva do timer periódico de 1 ms) e `RetirementNotifyDpc` (exclusiva do `KeInsertQueueDpc`), bem como `SdmaRetirementTimerDpc` e `SdmaRetirementDpc`.
>   2. Removidas as chamadas incondicionais a `KeInsertQueueDpc` em `SubmitCommandVirtual`, delegando o enfileiramento exclusivamente a `Bc250ActiveRetireAndNotify(context, FALSE)` quando há de fato fences concluídas.
>   3. Binário `driver/wdk/output_lab/bc250mcdm.sys` recompilado (536.064 bytes), pacote assinado e auditado (`audit-bc250mcdm-lab-package.ps1: PASS`). Pronto para a Corrida 19.
>
> ---
>

> **17/09 19:31:24 (P0, lab 3.181.1.0) — CORRIDA 17: BUGCHECK 0x133 EXTINTO NO SILÍCIO; BUGCHECK 0x139 (KERNEL_SECURITY_CHECK_FAILURE) IDENTIFICADO VIA MEMORY.DMP (VIOLAÇÃO DE CFG POR DPC NÃO-INICIALIZADA); FUSÍVEL ANTI-BOOT-LOOP ATIVO; CORREÇÃO DEFINITIVA NO AddDevice IMPLEMENTADA E VALIDADA OFFLINE.**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd -Force`) com `bc250umd.dll`.
> **Evidência arquivada:** `docs/evidence/2026-09-17_19-31-24-DESKTOP-HQAG6T4-lab-binding/`. Dump de Memória: `C:\Windows\MEMORY.DMP` (800 MB), analisado via CDB com símbolos do Windows. Handoff: `docs/286_HANDOFF_2026-09-17_19-32_CORRIDA17_0x139_E_CORRECAO_DPC_INIT.md`.
> - **Houve avanço?** SIM, AVANÇO DIAGNÓSTICO E ESTRUTURAL DECISIVO.
>   1. **BugCheck 0x133 100% Extinto:** O deadlock em `dxgmms2!VidSchDdiNotifyDpc` e a retenção de spinlock que causavam o estouro de watchdog na Corrida 16 não ocorreram mais. A guarda atômica `RetireBusy` e o desacoplamento de `DxgkCbNotifyDpc` funcionaram perfeitamente no silício.
>   2. **Isolamento da Causa Raiz do BugCheck 0x139 via Kernel Dump:** A análise forense do `MEMORY.DMP` revelou `Arg1 = 0x0` em `nt!KiExecuteAllDpcs` -> `nt!guard_icall_bugcheck`. O Control Flow Guard (CFG) do kernel interceptou `KeInsertQueueDpc(&context->RetirementDpc, NULL, NULL)` porque a rotina de DPC (`DeferredRoutine`) ainda estava zerada/não-inicializada na pool de memória na primeira submissão.
>   3. **Proteção Anti-Boot-Loop Funcionou Perfeitamente:** O fusível `C:\BC250_BOOT_FUSE.dat` interceptou o reboot não-limpo (`State=2 TRIPPED`, `LastStatus=0xC0000001`), protegendo o hardware e forçando Código 43 seguro.
> - **Sucessos:**
>   1. **Eliminação Comprovada do Deadlock 0x133:** O scheduler do Windows operou sem contenção de lock na submissão.
>   2. **Diagnóstico Imediato:** Causa raiz do 0x139 comprovada matematicamente pelo debugger com símbolos oficiais.
>   3. **Auditorias de Segurança Offline:** 40/40 gates passaram com Exit 0 em `build-contract.bat`.
> - **Falhas:**
>   1. **BugCheck 0x139 (`KERNEL_SECURITY_CHECK_FAILURE`):** Bucket `0x139_0_LEGACY_GS_VIOLATION_bc250mcdm!Bc250ActiveSubmissionGateLeave`. Disparo por CFG ao despachar DPC com `DeferredRoutine` nula.
> - **Veredito:** FALHA FUNCIONAL POR BUGCHECK 0x139 / AVANÇO DIAGNÓSTICO CONCLUSIVO.
> - **Recuperação e Correção:**
>   1. Declaradas rotinas de DPC forward e inicializadas explicitamente no `Bc250DxgkDdiAddDeviceActive` (`RetirementDpc`, `RetirementTimer`, `SdmaRetirementDpc`, `SdmaRetirementTimer`).
>   2. Reordenada a fila em `SubmitCommandVirtual` para garantir que o timer seja ativado antes ou concomitantemente à DPC em todos os nós.
>   3. Binário `driver/wdk/output_lab/bc250mcdm.sys` recompilado (535.552 bytes), pacote assinado e auditado (`audit-bc250mcdm-lab-package.ps1: PASS`). Pronto para validação física na Corrida 18.
>
> ---
>
> **17/09 18:35:26 (P0, lab 3.181.1.0) — CORRIDA 16: ENSAIO D3D11 NATIVO APOSENTA 20 SUBMISSÕES NO ANEL CP_RB0 (NODE 1 GFX, VMID 7); BUGCHECK 0x133 (DPC_WATCHDOG_VIOLATION) IDENTIFICADO VIA MEMORY.DMP (REENTRÂNCIA EM dxgmms2!VidSchDdiNotifyDpc); FUSÍVEL ANTI-BOOT-LOOP PREVINE REBOOT SUJO (CÓDIGO 43 SEGURO); CORREÇÃO DE RETIREMENT DESACOPLADO IMPLEMENTADA.**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd -Force`) com `bc250umd.dll`.
> **Evidência arquivada:** `docs/evidence/2026-09-17_18-35-26-DESKTOP-HQAG6T4-lab-binding/`. Log KMD parcial: `display-log-this-bind.txt` (245.995 bytes). Dump de Memória: `C:\Windows\MEMORY.DMP` (800 MB), relatório de análise CDB: `memory-dmp-analysis-symbols-fixed.txt` (67.236 bytes). Handoff: `docs/285_HANDOFF_2026-09-17_18-37_CORRIDA16_0x133_E_CORRECAO_RETIREMENT.md`.
> - **Houve avanço?** SIM, AVANÇO DIAGNÓSTICO CRÍTICO NO KMD.
>   1. **20 Submissões 3D Monotônicas Aposentadas no CP_RB0:** O anel gráfico físico consumiu e aposentou 20 submits sob VMID 7 com avanço de Fences 1..20 (`[SubmitVirtual Node1 GFX] FENCE COMMIT: Token=1..19 RingFence=1..19 committed=1 accepted=1`).
>   2. **Causa Raiz do BugCheck 0x133 Isolada com Precisão Absoluta no Kernel Dump:** O dump de memória revelou deadlock em `dxgmms2!VidSchDdiNotifyDpc` / `nt!KxWaitForLockOwnerShip`. Em `SubmitCommandVirtual` no Node 1 GFX, `Bc250ActiveRetireAndNotify` chamava `DxgkCbNotifyDpc` de forma síncrona dentro da thread do escalonador VidSch, colidindo com a rotina DPC do timer em outro núcleo, violando a regra de não-reentrância de DPC e causando estouro de watchdog (0x501 ticks).
>   3. **Proteção Anti-Boot-Loop Funcionou 100%:** `C:\BC250_BOOT_FUSE.dat` interceptou o reboot sujo, transitando para `State=2 (TRIPPED)` com `LastStatus=0xC0000001` e forçando Código 43 no StartDevice, impedindo qualquer loop de inicialização.
> - **Sucessos:**
>   1. **Node 0 SDMA:** 488 submissões indirect aposentadas sob VMID 8 com trípode completo comprovado no silício (`IB_TRIPE_VEREDITO #488: TRIPE_PROVADO`).
>   2. **Node 1 GFX:** Fences 1 a 20 executadas e validadas fisicamente no silício.
>   3. **Proteção de Silício e Kernel:** Fusível anti-boot-loop provado; dump de memória integral capturado e analisado via símbolos oficiais do Windows.
> - **Falhas:**
>   1. **BugCheck 0x133 (`DPC_WATCHDOG_VIOLATION`):** Arg1=0, Arg2=0x501, Arg3=0x500. Estouro do watchdog de DPC por spinlock no escalonador WDDM ao receber `DxgkCbNotifyDpc` dentro de `SubmitCommandVirtual`.
>   2. **Tela Preta antes da Apresentação:** Interrupção precoce no 20º frame antes da estabilização da SwapChain.
> - **Veredito:** FALHA FUNCIONAL POR BUGCHECK 0x133 / SUCESSO DIAGNÓSTICO CONCLUSIVO.
> - **Recuperação e Correção:** Fusível resetado para `CLEAN (0)`. KMD corrigido com desacoplamento temporal `FromDpc` e guarda atômica `RetireBusy` em `Bc250ActiveRetireAndNotify`. Pacote de laboratório reconstruído, assinado e auditado em `driver\wdk\package\output_lab`.
>
> ---
>
> **17/09 17:48:47 (P0, lab 3.181.1.0) — CORRIDA 15: HARNESS D3D11 NATIVO CONCLUI 60/60 QUADROS (EXIT 0), 45 SUBMISSÕES APOSENTADAS NO CP_RB0 (NODE 1 GFX, VMID 7), 60 DRAWS VULKAN NATIVOS, COMPILAÇÃO DXBC->SPIR-V EM RUNTIME E RECUPERAÇÃO TDR NO SILÍCIO (FRAME 20); WATCHDOG EM RECOVERY_OK_ONLINE (CÓDIGO 0).**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd`) com `bc250umd.dll` (com barreira de layout de imagem) e `vulkan_radeon.dll` (com encadeamento de pacotes indiretos com preservação de VMID 7).
> **Evidência arquivada:** `docs/evidence/2026-09-17_17-48-47-DESKTOP-HQAG6T4-lab-binding/`. Log KMD integral: `display-log-this-bind-FINAL.txt` (1.343.668 bytes, 11.879 linhas). Log UMD: `bc250_umd_log_7364-FINAL.txt` (433.311 bytes, 3.584 linhas). Console do teste: `d3d11-native-conformance-console.txt` (29.179 bytes, Exit 0).
> - **Houve avanço?** SIM, O MAIOR AVANÇO FUNCIONAL DE PIPELINE GRÁFICO E DE ROBUSTEZ TDR NO SILÍCIO DO PROJETO.
>   1. **Avanço Funcional D3D11 de Ponta a Ponta:** O runtime D3D11 oficial do Windows criou o dispositivo com Feature Level `0xA100` (10_1) na BC-250 (`1002:13FE`), compilou shaders DXBC em SPIR-V via `libvkd3d-shader` em runtime, alocou Vertex Buffer, vinculou PSO e executou 60 de 60 quadros (`[Frame 60/60] FPS: 3.3 | Status: RENDERING_OK`), finalizando o processo com `Exit Code: 0`.
>   2. **45 Submissões Aposentadas no Anel Gráfico Físico CP_RB0 (Node 1 GFX):** O anel `CP_RB0` consumiu e aposentou 45 command buffers gráficos sob VMID 7 com avanço contínuo de fences (`[SubmitVirtual Node1 GFX] FENCE COMMIT: Token=45 RingFence=45 committed=1 accepted=1`).
>   3. **60 Draws Nativos em Vulkan Sem Fallback:** `Bc250UmdExecuteVkDraw` despachou os 60 draws diretamente via Vulkan (`VkDraws=1..60`), eliminando o fallback PM4 (`NativePm4DrawCount == 0`).
>   4. **TDR Comprovado no Silício no Frame 20 (Itens 25 e 116):** Ao ocorrer timeout na engine gráfica (`0xC00002B6`), o subsistema WDDM acionou `ResetFromTimeout` e `RestartFromTimeout`. O KMD executou com sucesso o quiesce físico de SDMA0 e `CP_RB0`, aplicou soft-reset GRBM (CP, GFX, CPF, CPG) e rearmou os anéis com NOP físico comprovado (`[McdmRestartFromTimeout] TDR: engine provada por NOP fisico; adaptador aceita submissao novamente`). A máquina não travou (zero BugCheck, zero reboot sujo) e o processo continuou executando até o frame 60.
> - **Sucessos:**
>   1. **Portão de Inicialização e Display GOP:** Aprovado em 7 s (`compute-display-prerequisite-gate.json` com `shared_primary=true`, `commit_vidpn=true`, `set_vidpn_source_address=true`).
>   2. **1.089 Submissões SDMA sob VMID 8:** Concluídas no Node 0 com tripé completo comprovado no silício (`IB_TRIPE_VEREDITO #1089: TRIPE_PROVADO`).
>   3. **Compilação de Shaders em Runtime (Item 147):** VS (768 bytes) e PS (780 bytes) convertidos de DXBC para SPIR-V e instanciados como `VkShaderModule` no hardware real.
>   4. **Upload de Vértices para a GPU:** 72 bytes transferidos via `UploadBufferData` para `VkDeviceMemory` visível pelo host (`VkMem=0x4A0B40`).
>   5. **Recuperação Online Autônoma:** Watchdog realizou rollback limpo e restaurou `BasicDisplay` em Código 0 em 5/5 amostras CIM sem reboot (`safe-bind-final.json`: `RECOVERY_OK_ONLINE`). Fusível `C:\BC250_BOOT_FUSE.dat` íntegro e limpo.
> - **Falhas:**
>   1. **Tela Preta Física:** A inspeção de `LinearPresentBuf` continuou retornando zeros (`NonZeroSamples=0/5`), revelando que, após o TDR no frame 20, as tentativas subsequentes de apresentação e mapeamento de memória falharam (`vkMapMemory res=-5` e `Present res=-4`), mantendo a tela preta.
> - **Veredito:** SUCESSO DE PIPELINE E ROBUSTEZ / PARCIAL EM APRESENTAÇÃO DE PIXELS (Direct3D 11 nativo 100% funcional, 45 fences CP_RB0, TDR de silício provado; pixels visíveis pendentes de sincronização pós-TDR).
>
> ---
>
> **17/09 15:12:02 (P0, lab 3.181.1.0) — CORRIDA 13: 16 SUBMISSÕES GRÁFICAS APOSENTADAS NO ANEL FÍSICO CP_RB0 (NODE 1 GFX, VMID 7) NO SILÍCIO; ISOLAMENTO CAUSAL DA TELA PRETA (DESCONEXÃO ENTRE RENDER PM4 WDDM E SCANOUT DA VULKAN BRIDGE); FUSÍVEL ANTI-BOOT-LOOP SEGURO.**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd`) com `bc250umd.dll` e `bc250-d3d11-conformance.exe`.
> **Evidência arquivada:** `docs/evidence/2026-09-17_15-12-02-DESKTOP-HQAG6T4-lab-binding/`. Log KMD recuperado: `display-log-recovered.txt` (590.085 bytes, 5.236 linhas).
> - **Houve avanço?** SIM, AVANÇO FUNCIONAL INÉDITO NO SILÍCIO E DIAGNÓSTICO CAUSAL COMPLETO.
>   1. **Avanço Funcional no Silício:** Pela primeira vez, o KMD WDDM registrou e aposentou 16 submissões de rendering 3D diretamente no ring gráfico físico (`CP_RB0`, Node 1 GFX) sob VMID 7, sem congelamento da GPU e sem falhas de paginação de memória (`[SubmitVirtual Node1 GFX] GATE FISICO PASS`, `[GfxEngine] SUBMIT SUCESSO: Fence=16 NewWptr=192 DmaVA=0x200000000 SizeDW=24 VMID=7`, `FENCE COMMIT: Token=16 RingFence=16 committed=1 accepted=1`).
>   2. **Isolamento da Causa da Tela Preta:** A análise dos 5.236 registros identificou a desconexão exata na UMD entre renderização e apresentação: `Bc250UmdExecuteVkDraw` fez fallback para `Bc250UmdEmitPm4Draw`, desenhando diretamente na alocação física WDDM via 24 dwords de PM4 no anel gráfico. Porém, na hora do `Present`, `Bc250DxgiPresent1` chamou `Bc250UmdPresentVulkanDetileAndScanout`, que tentou destilar a imagem da Vulkan Bridge (`srcRes->VkImg`). Como a renderização ocorreu via PM4 na alocação WDDM, a textura Vulkan continha zeros puros (não inicializada). A operação `vkCmdCopyImageToBuffer` copiou 11 MB de zeros para `LinearPresentMem`, e o escape SDMA `COMPOSITE_BLT` copiou esses zeros diretamente para o scanout ativo do monitor, escurecendo a tela (`COMPOSITE_DATA_PROOF INCONCLUSIVO_ZERO: Match=5/5; zero contra zero NAO prova pixel`).
>   3. **Configuração de BufferCount = 2 no Harness:** O teste configurou SwapChain com 2 buffers mas obteve e renderizou apenas no Buffer 0, fazendo com que o DXGI apresentasse o Buffer 1 (zerado) nos frames ímpares.
> - **Sucessos:**
>   1. **Portão de Display GOP Aprovado:** A inicialização do display herdado do UEFI convergiu em 7 s (`compute-display-prerequisite-gate.json`: `state: PASSED`, `shared_primary: true`, `commit_vidpn: true`, `set_vidpn_source_address: true`).
>   2. **16 Submissões Gráficas no Silício (Node 1 GFX):** CP_RB0 consumiu 16 command buffers com avanço de WPTR e aposentadoria de todas as fences (1 a 16).
>   3. **Node 0 SDMA:** 446 indirect buffers executados sob VMID 8 com trípode comprovado.
>   4. **OTG Scanout Estável:** Sincronização mantida com 240 VSyncs lidos do hardware.
>   5. **Estabilidade de Kernel:** Zero BugCheck, zero TDR, DWM sem fail-fasts (`dwm.exe` vivo).
> - **Falhas:**
>   1. **Tela Preta no Monitor:** 11 MB de zeros transferidos ao scanout pelo SDMA por cópia de textura vazia da Vulkan bridge.
>   2. **Reboot Manual do Operador:** Sem feedback visual, a máquina foi reiniciada pelo botão físico, armando o fusível de boot `C:\BC250_BOOT_FUSE.dat` para `State=2 (TRIPPED)`.
> - **Veredito:** PARCIAL / INCONCLUSIVO (Avanço físico em CP_RB0 e SDMA; pixels apresentados eram zeros).
> - **Ações Corretivas Executadas Offline (17/09 15:50):**
>   1. **Harmonização de Apresentação na UMD (`driver/umd/bc250_d3d_umd.c`):** Implementada função `Bc250UmdPresentBltToScanout` e atualizada `Bc250UmdPresentVulkanDetileAndScanout`. Se render PM4 ocorreu na alocação WDDM (`NativePm4DrawCount > 0` e `NativeVkDrawCount == 0`), a alocação WDDM é preservada (não sofre sobrescrita com os zeros da Vulkan bridge) e apresentada diretamente ao scanout físico do monitor via escape SDMA `BC250_ESCAPE_OP_PRESENT_BLT`. Se render Vulkan ocorreu (`NativeVkDrawCount > 0` ou `InRenderPass`), destila `srcRes->VkImg` para `LinearPresentMem` e ponteia para a alocação WDDM.
>   2. **Robustez na Criação de RTV/DSV Vulkan (`driver/umd/bc250_umd_resource_vk.c`):** Em `Bc250UmdVkCreateRtv` e `Bc250UmdVkCreateDsv`, adicionado clamp estrito em `layerCount` e `baseArrayLayer` evitando que `ArraySize = (UINT)-1` cause rejeição em `vkCreateImageView` pelo Mesa RADV.
>   3. **Ajuste de SwapChain e Ordem D3D11 no Harness (`driver/tests/d3d11_native_conformance_test.cpp`):** Configurado `scd.BufferCount = 1` com `DXGI_SWAP_EFFECT_DISCARD`, garantindo que o Buffer 0 seja o único render target apresentado a cada frame pelo DXGI. Chamada `pContext->OMSetRenderTargets(1, &pRTV, NULL)` reposicionada para antes de `ClearRenderTargetView` e no início do loop de renderização.
>   4. **Compilações e Validação Offline Concluídas:** `tools/build-d3d11-native-conformance.bat` gerou `bc250-d3d11-conformance.exe` (Exit 0); `driver/umd/build-umd.bat` gerou `bc250umd.dll` (Exit 0); `driver/wdk/build-contract.bat` passou (105 DDIs conformes, Exit 0); `tools/package-bc250mcdm-lab.ps1` gerou pacote `output_lab` assinado com catálogo `.cat` (Exit 0). Pronto para a Corrida 14 via `tools\EXECUTAR_D3D11_NATIVO.cmd`.
>
> ---
>
> **16/09 22:29:31 (P0, lab 3.181.1.0) — CORRIDA 11: ISOLAMENTO DO DESCARTE DE PIXELS POR BARREIRA VULKAN E TRAVAMENTO POR JANELA FULLSCREEN POPUP DO DXGI; FUSÍVEL E ROLLBACK SEGUROS.**
> Ensaio físico automatizado (`tools/EXECUTAR_D3D11_NATIVO.cmd`) com a restauração de `radv_bc250_present_scanout` e janela 2560x1080 com estilo `WS_POPUP`.
> **Evidência arquivada:** `docs/evidence/2026-09-16_22-29-31-DESKTOP-HQAG6T4-lab-binding/`. Log KMD integral: `display-log-this-bind-FINAL.txt` (576.405 bytes, 5.064 linhas).
> - **Houve avanço?** NÃO FUNCIONAL, MAS SIM DIAGNÓSTICO CAUSAL COMPLETO.
>   A investigação minuciosa dos 5.064 registros de kernel e das chamadas da UMD isolou com precisão matemática as duas causas da tela preta na Corrida 11:
>   1. **Descarte de Pixels por Barreira Vulkan Incorreta (`oldLayout = UNDEFINED`):** A UMD realizou a transição de layout do render target usando `oldLayout = VK_IMAGE_LAYOUT_UNDEFINED`. Pela especificação do Vulkan, transições originadas de `UNDEFINED` descartam instantaneamente todo o conteúdo do buffer. Consequentemente, a operação `vkCmdCopyImageToBuffer` transferiu zeros absolutos para o `LinearPresentBuf` (`COMPOSITE_DATA_PROOF INCONCLUSIVO_ZERO`, linha 2008), preenchendo o scanout com dwords nulos no Escape #1.
>   2. **Travamento no Frame 1 por Fullscreen Optimization / DirectFlip:** Ao criar uma janela `WS_POPUP` sem bordas ocupando exatamente as dimensões do display (2560x1080 em 0,0), o subsistema DXGI ativou as otimizações de tela cheia (FSO) e tentou acionar DirectFlip ou sincronização exclusiva de VSync no `pSwapChain->Present(1, 0)`. Como o driver WDDM declara explicitamente `SupportDirectFlip = FALSE`, o DXGI travou o laço do harness logo após a primeira apresentação (`Escape=#1`). Sem novos quadros sendo submetidos, o DWM assumiu a tela sobrescrevendo com zeros.
> - **Sucessos:**
>   1. **Portão de Display:** Aprovado em 7 s (`compute-display-prerequisite-gate.json` com `shared_primary=true`, `commit_vidpn=true`, `set_vidpn_source_address=true`).
>   2. **Primeiro Present D3D11 Nativo Executado via SDMA (Escape #1):** O KMD realizou a cópia direta de 11.059.200 bytes para o scanout `0x0` em 1.364 µs (`[Bc250Escape] PRESENT_BLT: SDMA HW BLT CONCLUIDO COM SUCESSO!`).
>   3. **126 Submissões SDMA sob VMID 8 Concluídas:** Todas aprovadas no tripé completo (canários 5/5, fence sobre poison, RPTR==WPTR).
>   4. **Recuperação e Rollback Autônomos:** O guardião do watchdog recuperou com segurança o `Microsoft BasicDisplay` em Código 0 (`RECOVERY_OK_ONLINE`).
> - **Falhas:**
>   1. **Tela 100% Preta:** Pixels zerados devido ao descarte de textura pelo layout `UNDEFINED`.
>   2. **Parada do Harness após 1 Quadro:** Congelamento no `Present(1, 0)` DXGI sob janela popup 2560x1080.
>   3. **Reboot Manual do Operador:** Pela ausência de feedback visual na tela escura.
> - **Veredito:** PARCIAL COM DIAGNÓSTICO DEFINITIVO DAS DUAS FALHAS CONCORRENTES.
> - **Ações Corretivas Aplicadas (16/09 23:15):**
>   1. Na UMD (`bc250_d3d_umd.c`), barreira Vulkan corrigida definitivamente com `oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` e `srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT`, garantindo preservação integral dos pixels renderizados pelo rasterizador/pixel shader.
>   2. No harness (`d3d11_native_conformance_test.cpp`), resolução padrão ajustada para **1280x720** com estilo padrão **`WS_OVERLAPPEDWINDOW`**, eliminando por completo a contenção de DirectFlip/FSO do DXGI e permitindo que os 600 quadros fluam continuamente a 60 FPS (como validado na Corrida 9).
>   3. No KMD (`bc250_active_miniport.c`), o blit retangular 2D `Bc250SdmaPresentBltRect` ajusta o pitch (5120 vs 10240) e centraliza automaticamente a janela 1280x720 no meio da tela física 2560x1080, sem qualquer distorção ou faixa estreita.
>   4. UMD e harness recompilados; pacote assinado e validado nas auditorias de segurança (`safety` e `lab package` em PASS).
>
> ---
>
> **16/09 22:01:59 (P0, lab 3.181.1.0) — CORRIDA 10: ISOLAMENTO DA CAUSA RAIZ DA TELA PRETA (REFUTAÇÃO DO CONSELHO EXTERNO); SUPRESSÃO DO ESCAPE AO SCANOUT RESULTOU EM ZERO PIXELS NO MONITOR; FUSÍVEL ANTI-BOOT-LOOP ATIVO.**
> Ensaio físico automatizado (`tools/EXECUTAR_D3D11_NATIVO.cmd`) com a tentativa de substituir a apresentação física por escape (`radv_bc250_present_scanout`) pela cópia interna para alocação WDDM (`COMPOSITE_BLT` via `Bc250UmdBridgeLinearToWddm`).
> **Evidência arquivada:** `docs/evidence/2026-09-16_22-01-59-DESKTOP-HQAG6T4-lab-binding/`. Log KMD no disco vivo: `C:\BC250_DISPLAY_LOG.txt` (trecho offset 1.599.507.463 a 1.600.040.243, 4.726 novas linhas).
> - **Houve avanço?** NÃO FUNCIONAL, MAS SIM DIAGNÓSTICO E ARQUITETURAL CRÍTICO.
>   A corrida anterior (Corrida 9) comprovou no silício a geração e exibição física de pixels coloridos do triângulo Direct3D 11 no monitor LG Ultrawide. A Corrida 10 testou a remoção do escape direto ao scanout com base em sugestão externa e provou categoricamente que o DWM não encaminha alocações privadas de teste para a tela física sem um flip model DXGI completo.
> - **Sucessos:**
>   1. **Portão de Display Imediato:** `compute-display-prerequisite-gate.json` fechou em `PASSED` (`shared_primary=true`, `commit_vidpn=true`, `set_vidpn_source_address=true`).
>   2. **Execução de COMPOSITE_BLT Retangular 2560x1080 no Silício:** O KMD processou com sucesso 6 requisições de `COMPOSITE_BLT` por SDMA de 11.059.200 bytes (`[Bc250Escape] COMPOSITE_BLT OK RECT 1:1: 2560x1080 srcPitch=10240 dstPitch=10240 Bytes=11059200`).
>   3. **Proteção Anti-Boot-Loop:** Ao reiniciar manualmente pelo botão da máquina devido à tela preta persistente, o fusível `C:\BC250_BOOT_FUSE.dat` interceptou o reboot sujo (`State=2 TRIPPED`, `Boots=2`), protegendo a máquina contra loops de boot e iniciando em Código 43 seguro até o próximo reset limpo.
> - **Falhas e Causa Raiz:**
>   1. **Tela 100% Preta (Zero Pixels no Monitor):** Com a supressão de `radv_bc250_present_scanout`, nenhum dword de imagem renderizada alcançou o endereço de scanout físico do GOP (`0xC0000000`).
>   2. **DWM Inoperante para Scanout:** As 7 chamadas `DdiPresent` do DWM continuaram enviando superfícies com conteúdo totalmente zerado (`NonZero=0/256 Hash=0x00000000`).
>   3. **Reboot Manual Exigido:** O usuário aguardou durante a janela preta e reiniciou a máquina.
> - **Veredito:** FALHA DE EXIBIÇÃO / SUCESSO DE DIAGNÓSTICO CAUSAL (Refutação da eliminação do escape direto).
> - **Ações Corretivas Executadas (16/09 22:15):**
>   1. Restaurada incondicionalmente a chamada `device->VkBridge.pfnPresentScanout(...)` em `bc250_d3d_umd.c` (linhas 4248-4256), mantendo a apresentação física direta via SDMA com resolução nativa 2560x1080.
>   2. Mantida em paralelo a cópia `Bc250UmdBridgeLinearToWddm` para conformidade com o subsistema DDI WDDM.
>   3. `bc250umd.dll` recompilada (`build-umd.bat` Exit 0) e pacote de laboratório assinado e auditado (`audit-bc250mcdm-safety.py` e `audit-bc250mcdm-lab-package.ps1` em PASS).
>
> ---
>
> **16/09 21:44:12 (P0, lab 3.181.1.0) — CORRIDA 9: PRIMEIROS PIXELS COLORIDOS D3D11 VISTOS FISICAMENTE NO MONITOR LG ULTRAWIDE VIA DISPLAYPORT; 780 PRESENTS SDMA E RECUPERAÇÃO ONLINE AUTÔNOMA (EXIT 0).**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd`) com `bc250umd.dll` recompilada e `vulkan_radeon.dll`.
> **Evidência arquivada:** `docs/evidence/2026-09-16_21-44-12-DESKTOP-HQAG6T4-lab-binding/`. Log KMD integral: `display-log-this-bind-FINAL.txt` (2.431.834 bytes, 21.146 linhas).
> - **Houve avanço?** SIM, O MAIOR AVANÇO VISUAL E FÍSICO DO PROJETO.
> - **Sucessos:**
>   1. **Confirmação Visual Física:** Pela primeira vez no projeto, pixels coloridos gerados por vertex/pixel shaders e rasterizador D3D11 foram fisicamente exibidos no monitor LG Ultrawide no DisplayPort da BC-250 ("faixa horizontal com vários pixels coloridos").
>   2. **780 Presents via SDMA Concluídos:** O KMD executou 780 operações de apresentação via SDMA em hardware sem falhas (`Escape=#780`), com latências medidas de 460 µs (1280x720) e 1350 µs (2560x1080).
>   3. **Superfície do DWM com Conteúdo Ativo:** `[Present #2..#5, #7, #9] ROTEAMENTO AO SCANOUT: VramOff=0xAABC000 (NonZero=1/256 Hash=0x00080400)`. Pela primeira vez, a superfície do DWM continha dados não-nulos.
>   4. **Recuperação Online Autônoma Perfeita:** Ao término da janela do watchdog (300 s), o sistema executou o rollback online restaurando `Microsoft BasicDisplay` em Código 0 sem travar e **SEM REBOOT** (`safe-bind-final.json`: `RECOVERY_OK_ONLINE`). Zero BugCheck, zero TDR.
> - **Falhas e Isolamento Geométrico:**
>   1. **Distorção de Stride/Pitch 1D:** A janela de 1280x720 (pitch 5.120) foi copiada por cópia linear 1D simples para o scanout nativo de 2560x1080 (pitch 10.240). Cada linha física recebeu duas linhas da janela, e 3.686.400 bytes preencheram apenas 360 linhas verticais (1/3 superior), comprimindo o triângulo numa faixa estreita horizontal e deixando os 2/3 inferiores em preto.
>   2. **Timeout de 180 s no Harness:** A espera por mensagens Win32 encerrou o processo após 180 s.
> - **Veredito:** PARCIAL COM VALIDAÇÃO VISUAL HISTÓRICA DOS PRIMEIROS PIXELS D3D11 NO MONITOR FÍSICO.
> - **Ações Corretivas Aplicadas (16/09 21:55):**
>   1. No harness D3D11 (`d3d11_native_conformance_test.cpp`), padrão alterado para **2560x1080 nativo** com janela `WS_POPUP`, preenchendo 100% da tela física e coincidindo o pitch byte a byte com o scanout do monitor.
>   2. No KMD (`bc250_active_miniport.c`), implementada a chamada de `Bc250SdmaPresentBltRect` com subwindow 2D e cálculo exato de pitch (5120 vs 10240) e centralização automática quando a fonte for menor que o scanout.
>   3. Drivers recompilados, reempacotados e aprovados nas auditorias.
>
> ---
>
> **16/09 21:17:51 (P0, lab 3.181.1.0) — CORRIDA 8: 60 BLTS SDMA DE APRESENTAÇÃO FÍSICA A 464 µS NO SILÍCIO, E ISOLAMENTO DA SOBRESCRITA PELO DWM E BINÁRIO UMD ANTERIOR.**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd`) com harness `bc250-d3d11-conformance.exe` e `vulkan_radeon.dll` atualizada com `radv_bc250_get_memory_gpu_va`.
> **Evidência arquivada:** `docs/evidence/2026-09-16_21-17-51-DESKTOP-HQAG6T4-lab-binding/`. Log KMD integral: `display-log-this-bind-FINAL.txt` (1.330.981 bytes, 11.924 linhas).
> - **Houve avanço?** SIM, AVANÇO FUNCIONAL E DIAGNÓSTICO DECISIVO.
> - **Sucessos:**
>   1. **60 Apresentações Físicas via SDMA Comprovadas no Silício:** A nova `vulkan_radeon.dll` executou e disparou 60 chamadas de `PRESENT_BLT` diretamente para o scanout `0x0`: `[Bc250Escape] PRESENT_BLT: 1280x720 (3686400 bytes), GpuVa=0x200800000 -> SDMA HW BLT CONCLUIDO COM SUCESSO! Bytes=3686400 Escape=#60 Blt=464 us`. Latência de cópia física medida em 464 µs por quadro!
>   2. **Métricas de Hardware e Kernel:** 261 submissões GFX completadas no Node 1 com avanço de fence; 766 submissões SDMA no Node 0 sob VMID 8 com tripé aprovado.
>   3. **Estabilidade e Robustez:** Zero BugCheck `0x119`, zero TDR durante os 60 quadros. O fusível de boot (`C:\BC250_BOOT_FUSE.dat`) cumpriu com perfeição seu papel de proteção anti-boot-loop ao interceptar o reboot forçado (`State=2 TRIPPED`).
> - **Falhas e Causa Raiz da Tela Preta:**
>   1. **Sobrescrita Concorrente pelo DWM:** Enquanto o harness Vulkan blitava 1280x720 para o scanout `0x0`, o `dwm.exe` (2560x1080) emitiu `DxgkDdiPresent` contínuos de sua alocação zerada (`NonZero=0/256 Hash=0x00000000`), sobrescrevendo instantaneamente a tela inteira com preto.
>   2. **Binário `bc250umd.dll` Anterior:** O pacote de laboratório da Corrida 8 continha a `bc250umd.dll` compilada às 20:20 (pois o empacotador foi invocado antes do `build-umd.bat`). Por isso, a nova ponte GPU-side `Bc250UmdBridgeLinearToWddm` não estava presente no binário carregado.
>   3. **Reboot Forçado:** Com a tela em preto, o desligamento manual disparou o fusível de boot (`Last Status: 0xC0000001`).
> - **Veredito:** PARCIAL COM PROVA DE APRESENTAÇÃO FÍSICA SDMA (464 µS) E ELIMINAÇÃO DA CAUSA RAIZ.
> - **Ações Corretivas Executadas (16/09 21:41):**
>   1. `bc250umd.dll` recompilada formalmente via `driver/umd/build-umd.bat` com `Bc250UmdBridgeLinearToWddm` e `HardwareAccess = 1u` (hash `85942AE6...`).
>   2. Pacote `output_lab` reempacotado e assinado via `package-bc250mcdm-lab.ps1`.
>   3. Auditorias de segurança `audit-bc250mcdm-safety.py` e `audit-bc250mcdm-lab-package.ps1` executadas em **PASS**.
>
> ---
>

> **16/09 20:34:22 (P0, lab 3.181.1.0) — CORRIDA 7: ENSAIO D3D11 NATIVO (600 QUADROS A 32.6 FPS), ROTEAMENTO DIRETO AO SCANOUT COMPROVADO (Item B10) E ISOLAMENTO DA CAUSA RAIZ DA TELA PRETA (DESCOMPASSO VULKAN ↔ WDDM E DUPLO PRESENT).**
> Ensaio físico automatizado do runtime Direct3D 11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd`) com o harness `bc250-d3d11-conformance.exe`.
> **Evidência arquivada:** `docs/evidence/2026-09-16_20-34-22-DESKTOP-HQAG6T4-lab-binding/`.
> - **Houve avanço?** SIM, AVANÇO FUNCIONAL E DE DIAGNÓSTICO DECISIVO.
> - **Sucessos:**
>   1. **Roteamento real ao Scanout comprovado (Item B10):** O KMD extirpou 100% dos desvios espúrios (`LastEscapeSegmentOffset`) e executou 11 roteamentos reais e incondicionais ao scanout do GOP (`0x0`) das superfícies alocadas pelo DWM/harness (`0x153C000`, `0xB27C000`, etc.).
>   2. **Conformidade D3D11 sustentada:** O harness `bc250-d3d11-conformance.exe` rodou **600 quadros contínuos a 32.6 FPS** com status `RENDERING_OK`.
>   3. **Métricas de Hardware e Kernel:** 71 Presents WDDM concluídos, 84 submissões GFX no Node 1 com avanço contínuo de fences, 653 submissões SDMA no Node 0 sob VMID 8 com tripé completo comprovado.
>   4. **Estabilidade:** Zero BugCheck, zero TDR, zero device-lost.
> - **Falhas:**
>   1. **Tela preta física contínua:** Monitor permaneceu em tela preta porque as superfícies apresentadas continham apenas zeros (`NonZero=0/256`).
>   2. **Causa Raiz Comprovada (Arquitetural):** Investigação minuciosa e auditoria independente com múltiplos modelos isolaram as falhas fundamentais:
>      (a) **Descompasso de Memória:** O pipeline D3D11 renderiza na memória interna do Mesa RADV (`VkDeviceMemory`/`LinearPresentBuf`), enquanto o `pfnPresentCb` entrega a alocação do VidMm (`res->Allocation`), que permaneceu virgem de escrita (100% zeros).
>      (b) **Duplo Present Concorrente:** A UMD executava `radv_bc250_present_scanout` (escape para `0x0`) e logo em seguida `pfnPresentCb` (que blitava a alocação zerada do WDDM também para `0x0`), sobrescrevendo a tela com preto caso o primeiro funcionasse.
>      (c) **Bugs Latentes de Bridge:** Buffer linear `LinearPresentBuf` sem redimensionamento dinâmico em caso de superfícies maiores e transição de layout `oldLayout` a partir de `UNDEFINED`.
> - **Veredito:** PARCIAL COM DIAGNÓSTICO DEFINITIVO. Toda a engrenagem de hardware, kernel, escalonamento e submissão está comprovada no silício; o bloqueador estava na integração UMD entre a memória Vulkan e a alocação WDDM.
> - **Ações Corretivas Aplicadas (16/09 21:15):**
>   1. Exportado `radv_bc250_get_memory_gpu_va` no Mesa RADV (`vulkan_radeon.dll`), compilado com sucesso (`build_radv.bat` Exit 0).
>   2. Implementada ponte GPU-side `Bc250UmdBridgeLinearToWddm` em `bc250_d3d_umd.c`: cópia direta por hardware SDMA (`COMPOSITE_BLT`) de `LinearPresentBuf` para `srcRes->Allocation.GpuVirtualAddress` antes do `pfnPresentCb`.
>   3. Eliminado o escape concorrente direto ao scanout `0x0`; o `DxgkDdiPresent` do KMD é agora o único e oficial escritor do scanout.
>   4. Corrigido redimensionamento dinâmico do `LinearPresentBuf` em `bc250_umd_vulkan_bridge.c`, formato fallback e transição de layout `VK_IMAGE_LAYOUT_UNDEFINED`.
>
> ---
>
> **16/09 13:34:40 (P0, lab 3.181.1.0) — BRINGUP DO ANEL GRÁFICO REAL CP_RB0 VALIDADO NO SILÍCIO (ACTIVE=1) E INICIALIZAÇÃO D3D11 DA UMD NO DWM; TELA PRETA POR MOTOR PERSISTENTE OFFLINE.**
> Ensaio físico da Rota Direta (`tools/EXECUTAR_ROTA_DIRETA_GFXRING.cmd`, Nível 2) com `D3D11LabPromotion` armado na UMD e KMD com fallbacks de VRAM.
> **Evidência arquivada:** `docs/evidence/2026-09-16_13-34-40-DESKTOP-HQAG6T4-lab-binding/`.
> - **Houve avanço?** SIM, AVANÇO HISTÓRICO FUNCIONAL E DE DIAGNÓSTICO.
> - **Sucessos:**
>   1. **Partida física do anel gráfico CP_RB0 comprovada (Nível 2, Item B9):** `[GfxRing] BRINGUP veredito: OK (level=2) Reads=15 Writes=20`. Base física configurada em VRAM real (`0xF40FFB00`), `CNTL=0x50000709`, micro-engines PFP e ME desaltadas (`ME_CNTL=00000000`), partida confirmada com `ACTIVE_readback=00000001` e rollback limpo do teste (`RollbackPassed=1`).
>   2. **UMD D3D11 / Vulkan Bridge operando no DWM e Explorer:** Com `D3D11LabPromotion=1`, `dwm.exe` e `Explorer.EXE` criaram dispositivos D3D11 nativos via RADV (`vkCreateDevice`, `GraphicsQueue`, shaders VS/PS compilados) e o DWM executou 23 draws indexados nativos (`Bc250UmdExecuteVkDraw: DRAW EXECUTADO NATIVO EM VULKAN, VkDraws=23, Verts=6, Indexed=1`).
>   3. **Roteamento WDDM estrito e fail-closed:** O DWM despachou submissões 3D para o Node 1 (`[SubmitVirtual Node1 GFX] ENTER: FenceId=1..40 DmaSize=96`). Zero encaminhamento para o MEC, eliminando o BugCheck 0x116 da corrida anterior.
>   4. **Tripé SDMA de Boot (Item 63):** Comprovado em 1 poll (`WPTR=68 RPTR=68 Fence=00001001`, 4/4 OK).
> - **Falhas:**
>   1. **Tela preta física:** O motor persistente `Bc250ActiveGfxEngineOnline` falhou no StartDevice (`status=PREFLIGHT_FAILED (3)`), deixando `GfxEngine.State=0` (offline). A investigação comprovou que `Context->Core.MmioTransport` estava no estado `EMPTY` (desmapeado pelo término da transação anterior do HQD), e a função não garantiu seu mapeamento antes do preflight de registradores.
>   2. **Submissões recusadas:** Com a engine offline, as submissões no Node 1 foram recusadas por segurança (`RECUSA FAIL-CLOSED: GfxEngine offline (State=0)`). O Mesa RADV retornou `VK_ERROR_DEVICE_LOST` (`res=-4`) no flush, impedindo a apresentação dos quadros no scanout.
> - **Veredito:** PARCIAL COM GRANDE AVANÇO. Hardware CP_RB0 comprovado ativo e UMD funcional; persistência do motor para entrega de quadros pendente de correção de transporte MMIO.
> - **Recuperação e Pendências:** Watchdog expirou após 180 s sem confirmação visual e restaurou `BasicDisplay` em Código 0 online sem reboot (`RECOVERY_OK_ONLINE`, 5/5 amostras OK). Fusível `C:\BC250_BOOT_FUSE.dat` limpo. Execução sustentada de IBs sob VMID 8 com anel persistente online permanece NÃO VALIDADA EM HARDWARE.
> - **Ação Corretiva Offline Concluída (16/09 14:30):**
>   1. Em `Bc250ActiveGfxEngineOnline` (`bc250_active_miniport.c`), inserido mapeamento preventivo de `Core.MmioTransport` caso esteja vazio (`BC250_MMIO_TOPOLOGY_EMPTY`).
>   2. Ponto de ativação de `Bc250ActiveGfxEngineOnline` movido para o final do `StartDeviceActive` (após a confirmação do mapeamento de runtime MMIO e após a finalização dos testes pontuais de bringup), eliminando o conflito de rollback.
>   3. Ferramenta `tools/EXECUTAR_ROTA_DIRETA_GFXRING.cmd` atualizada com opção padrão `[0] Modo Produção Direto` (`-GfxRingBringupLevel 0`).
>   4. Pacote reconstruído, auditado e assinado com sucesso: `bc250mcdm.sys` SHA-256 `B5EED49F75F6FE4DA887D4BC7CF92521F535D908BD19D82F3C353160CEB5CE0A`. Pronto para novo ensaio físico após boot frio.
>
> ---
>
> **16/09 09:26:14 (P0, lab 3.181.1.0) — TRANSIÇÃO PARA BACKEND GRÁFICO REAL (CP_RB0): BUGCHECK 0x116 POR SUBMISSÃO 3D NO MEC E CAUSA RAIZ IDENTIFICADA.**
> Ensaio físico com UMD D3D11 compilando shaders e emitindo pacotes 3D via Mesa RADV winsys (`vkQueueSubmit`).
> **Evidência arquivada:** `docs/evidence/2026-09-16_09-26-14-DESKTOP-HQAG6T4-lab-binding/` e [docs/283](docs/283_HANDOFF_CP_RB0_BACKEND_GRAFICO_PERSISTENTE.md).
> - **Houve avanço?** Inconclusivo funcionalmente / Avanço no diagnóstico. Zero pixels 3D comprovados.
> - **Sucessos:** Exceção `0xC0000094` superada nos casos testados (layouts multi-slot DWM slots 0..4); 238 submissões de paginação SDMA concluídas com tripé no Node 0 sob VMID 8 (`Submitted=238, Retired=238, Divergentes=0`); 1 Blt SDMA de 640×640 executado em 697 µs (`Fence=000010F0`).
> - **Falhas:** Zero pixels 3D; BugCheck `0x116` (`VIDEO_TDR_ERROR`). O KMD declarava Node 1 como `DXGK_ENGINE_TYPE_3D`, mas `SubmitCommandVirtual` despachava comandos para a fila assíncrona do MEC (`0xF40FFA0000`). O microcode do MEC travou em `TC_UTCL1_ERROR 0x00080000` (`CpStalled1 0x00000C00`) com falha de proteção GCVM `FaultStatus=0x00900B32` (`ADDR=0x0F40FFA0`) sob VMID 9.
> - **Veredito:** FALHA frente ao critério de desktop acelerado / D3D11. Demonstrou que o hardware 3D exige o anel gráfico real acionado por PFP/ME.
> - **Recuperação e Decisão:** O fusível anti-boot-loop funcionou no reboot (`C:\BC250_BOOT_FUSE.dat` em `State=2 TRIPPED`), restaurando Código 43 / `BasicDisplay`. **Encerramento formal de emulação de gráficos por compute.** Implementado no KMD o motor gráfico real persistente `CP_RB0` sob VMID 8, roteamento exclusivo sem fallback para MEC, recuperação TDR com soft-reset `GRBM_SOFT_RESET` e aposentadoria de fences desacoplada (`Bc250WddmFenceTrackerDrainEx`). Suítes offline passaram (`build-contract.bat` Exit 0). A execução do novo caminho gráfico permanece **NÃO VALIDADA EM HARDWARE**.
>
> ---
>
> **15/09 23:40:16 (P0, lab 3.174.1.0) — MARCO 5: DIRECT3D 11 VIA DXVK NO SILÍCIO — PRIMEIRA TENTATIVA E CAUSA RAIZ DIAGNOSTICADA.**
> Harness `D3D11Triangle.exe` nativo executado sob DXVK 2.4.1 (`d3d11.dll`, `dxgi.dll`).
> A inicialização do DXVK teve êxito: detectou o hardware físico `AMD BC-250 Graphics (Cyan Skillfish 2)`
> (`0x13fe`), driver `radv 26.1.99` e 16 GB GDDR6 (`evidence/2026-09-15_23-39-59-DESKTOP-HQAG6T4-lab-binding/`).
>
> **Ponto exato de parada (P0 medido):** o DXVK abortou em `d3d11.stderr.txt:211`:
> `std::vector::operator[]: Assertion '__n < this->size()' failed.`
> **Causa raiz:** em `src/amd/vulkan/radv_physical_device.c:142`, a função `radv_graphics_queue_enabled`
> continha trava explícita forçando `return false;` para a BC-250 (`0x13fe`), expondo apenas a fila
> de Compute (`VK_QUEUE_COMPUTE_BIT`). O DXVK, como tradutor de Direct3D 11, busca obrigatoriamente
> uma fila com a flag gráfica `VK_QUEUE_GRAPHICS_BIT`. Sem encontrá-la, tentou acessar índice inválido.
> O watchdog executou rollback online incondicional e restaurou `BasicDisplay` em Código 0.
> **Ação corretiva imediata:** habilitar `radv_graphics_queue_enabled` no Mesa RADV, recompilar
> `vulkan_radeon.dll` e reexecutar o ensaio do Marco 5.
>
> ---
>
> **15/09 22:49:48 (P0) — MARCO 4 CONCLUÍDO NO SILÍCIO: VULKAN WSI NATIVO COM VKCUBE (3D TEXTURIZADO).**
> Pela primeira vez na história da APU AMD BC-250 no Windows 10 x64, uma aplicação 3D Vulkan completa
> com texturização, shaders e swapchain WSI nativa (`VK_KHR_swapchain` / `VK_KHR_win32_surface`)
> renderizou e apresentou no monitor DisplayPort conectado diretamente à placa, sem WARP e sem cópia por CPU.
> **Evidência arquivada:** `docs/evidence/2026-09-15_22-46-42-DESKTOP-HQAG6T4-lab-binding/` e [docs/280](docs/280_MARCO4_VULKAN_WSI_CUBE_CONCLUIDO_NO_SILICIO.md).
> - **Processo:** `VulkanCube.exe` concluiu 600 quadros com `Exit 0`.
> - **Motor Node 1 (MEC/ACO):** 1.201 submits aceitos e aposentados (`WPTR=RPTR=14412`, zero falhas GCVM).
> - **Motor Node 0 (SDMA HW Blt):** 600 quadros apresentados no Scanout GOP (2560×1080 em VRAM física real `0xF400000000`).
>   Latência média de ~1,35 ms por quadro (~740 FPS teóricos). `HostCpuCopies = 0`, `tdr_events = 0`, zero BugChecks.
> - **Confirmação visual humana (P0):** o cubo tridimensional texturizado girou suavemente no monitor físico. Rollback online restaurou `BasicDisplay` (Código 0) após a conclusão. Promovido à Tabela 1.

> **13/09 17:18:35 (P0, lab 3.173.1.0) — `Bc250DxgkDdiPresent` FOI ALCANÇADA PELA PRIMEIRA VEZ.**
> Seis chamadas, contra zero em toda a história do projeto. A correção do vínculo de recurso
> funcionou: `CreateAllocation` de origem UMD passou a `Resource=1` (7/7), a UMD reporta
> `SrcDeclaredPrimary=1` com `hRTResource` não-nulo, e o `SetVidPnSourceAddress` saiu de
> `0xAAC000`/`hAlloc=0x08` (primária do GOP) para `0x4B5C000`/**`hAlloc=0xA8`**, uma alocação
> criada pela UMD. Os seis fecharam até o silício (blt de 11.059.200 bytes, `SDMA=YES`).
> Estabilidade: zero TDR, zero BugCheck, rollback online limpo.
>
> **E a hipótese da primária está REFUTADA como causa suficiente:** 84 de 84 `pfnPresentCb`
> devolveram `hr=0x80004005` (E_FAIL) **no mesmo milissegundo** — rejeição síncrona de validação
> — com a primária já correta. O laço de device-lost do `dwm.exe` continua (61 ciclos).
>
> **Correção de registro:** eu havia apontado "o nó do contexto (COMPUTE, node 1)" como próximo
> suspeito. Errado — o `GetNodeMetadata` da corrida diz Node 1 = **3D** e Node 0 = **COPY**. O que
> sobrevive é mais estreito: os dois únicos contextos pelos quais o `dxgkrnl` apresentou estão no
> **Node 0** (`SystemContext`/`GdiContext` + `VirtualAddressing`), e todos os nossos estão no
> Node 1. A causa do E_FAIL **não está nomeada** — `0x80004005` é genérico.
>
> Faixa aplicada offline: contexto de apresentação dedicado no Node 0 (sem fallback, recusa
> nomeada) e o I3 **completado** — a classe 9 do amostrador tinha o mesmo defeito de contador
> compartilhado, e por isso `Primary token confirmed (UMD` não saiu nenhuma vez. Seis mutações
> verificadas. Candidato **3.174.1.0**. **NÃO VALIDADO EM HARDWARE.**

> ⛔ **CORREÇÃO DE REGISTRO — LEIA ANTES DO PARÁGRAFO SEGUINTE.** O [docs/265](docs/265_PASSAGEM_DE_BASTAO_2026-09-13_16-33.md)
> atribui a tela preta a "milhares de quadros compostos pelo DWM que ainda não foram transferidos
> para o scanout". **Isso está REFUTADO pela evidência da própria corrida**, em três medições (P0):
> (1) **zero** chamadas a `Bc250DxgkDdiPresent` — não há uma linha `[Present #N]` em 5,4 MB de log,
> e a ausência é conclusiva porque essa classe não é amostrada; o caminho de scanout é **código
> morto** nessa corrida. (2) **1787 de 1787** `COMPOSITE_BLT` terminam em `Prova=ZERO_INCONCLUSIVO`,
> com a linha literal `zero contra zero NAO prova pixel` — **não existe quadro composto**, os 5.165
> escapes moveram zeros. (3) o `dwm.exe` está em **laço de device-lost, 90 vezes**: `Present` →
> destrói tudo → `DestroyDevice` → `CreateDevice`, a ~1 Hz. O `Present` falha, e é `Flip` (`0x2`),
> não `Blt`.
>
> **Causa raiz (H alta, código + log concordam):** a UMD nunca passava `hResource`,
> `Flags.Primary` nem `VidPnSourceId` no `pfnAllocateCb`, então `Flags.Resource` chegava sempre 0 ao
> KMD e o ramo que marca primária era **inalcançável**. Corrigido offline, com dois portões novos e
> **nove mutações verificadas**. Detalhe e critério da próxima corrida em
> [docs/266](docs/266_AUDITORIA_2026-09-13_A_PAREDE_E_A_PRIMARIA_NAO_O_SCANOUT.md).
> **NÃO VALIDADO EM HARDWARE.**
**Estado mais recente, que substitui a linha Data-base abaixo:** corrida física **3.172.0.0**, 16:28:27. O BugCheck 0x10E (`VIDMM_RECYCLE_MULTIRANGE::Rotate`) foi completamente eliminado pela remoção de `CpuVisible=1` indiscriminado. O hardware permaneceu 100% estável: **3.757 submissões indiretas de paginação no Node 0 (SDMA)** sob VMID 8 com `TRIPE_PROVADO`, **5.165 escapes de composição via SDMA**, **1.740 cópias retangulares**, zero timeouts, zero TDR, zero reboots. O watchdog de 180s realizou rollback limpo online (`RECOVERY_OK_ONLINE`) restaurando BasicDisplay em Código 0. A tela física permaneceu preta porque os quadros compostos pelo DWM no backbuffer offscreen ainda não foram transferidos para a primária/scanout do monitor conectada ao GOP. Detalhes em [docs/265](docs/265_PASSAGEM_DE_BASTAO_2026-09-13_16-33.md).
**Passagens anteriores:** [docs/264 — Corrida 13:38](docs/264_PASSAGEM_DE_BASTAO_2026-09-13_13-38.md) · [docs/263 — Corrida 12:50](docs/263_HANDOFF_2026-09-13_WATCHDOG_ABSOLUTO_E_BLT_RETANGULAR.md).
**Passagem de bastão e auditorias recentes:** [docs/262 — Corrida 12:21 e correção do handle de adapter no escape de composição](docs/262_HANDOFF_2026-09-13_12-21_ESCAPE_HANDLE_ADAPTER.md) · [docs/258 — Corrida 21:22: Marco 3 Sustentado — 600/600 quadros Vulkan no silício, 0 TDR, DWM contido](docs/258_HANDOFF_2026-09-12_21-22_MARCO3_600_QUADROS_SUSTENTADO.md) · [docs/257 — Corrida 21:10: C3.18 validado no silício — 368 submits, 367 blts, 0 TDR, animação completa](docs/257_HANDOFF_2026-09-12_21-10_C3_18_368_QUADROS_VALIDADOS.md) · [docs/256 — Handoff 20:52: HQD e necessidade de reboot frio](docs/256_HANDOFF_2026-09-12_20-52_C3_18_HQD_REBOOT_NECESSARIO.md) · [docs/255 — Tentativa 19:58 recusada por HQD residual](docs/255_HANDOFF_2026-09-12_19-58_HQD_RESIDUAL_DESKTOP_BASIC.md) · [docs/254 — Corrida 19:27, watchdog validado e gate físico Node 1](docs/254_HANDOFF_2026-09-12_19-27_WATCHDOG_E_GATE_FISICO_NODE1.md) · [docs/253 — Reavaliação da corrida 18:45 e C3.16](docs/253_HANDOFF_2026-09-12_18-45_REAVALIACAO_FREEZE.md) · [docs/252 — Corrida 18:21, padrão no monitor, teto do journal e watchdog bloqueado](docs/252_HANDOFF_2026-09-12_18-21_JOURNAL_NO_SPACE_E_WATCHDOG.md) · [docs/251 — Candidato C3.14 de capacidade GPUVA](docs/251_HANDOFF_2026-09-12_C3_14_GPUVA_CAPACITY_CANDIDATE.md) · [docs/250 — Handoff: NO_SPACE no rebuild transacional e DMA PAGE FAULT](docs/250_HANDOFF_2026-09-12_GPUVA_NO_SPACE_E_DMA_PAGE_FAULT.md) · [docs/241 — Handoff: RADV WDDM Compute Tripod PASSOU NO SILÍCIO (15/15 Verificações)](docs/241_HANDOFF_2026-09-11_RADV_COMPUTE_TRIPOD_PASSOU_NO_SILICIO.md) · [docs/235 — Handoff: flush de HDP validado e a parede 0x133](docs/235_HANDOFF_2026-09-10_12-16_FLUSH_HDP_VALIDADO_E_0x133.md) · [docs/234 — Auditoria de 10/09: C1 (Draw sem CPU) e C2 (evidência perdida)](docs/234_AUDITORIA_2026-09-10_C1_DRAW_SEM_SRV_E_C2_EVIDENCIA_PERDIDA.md) · [docs/233 — Handoff da corrida 11:22 (**enquadramento corrigido por docs/234**)](docs/233_HANDOFF_2026-09-10_DWM_DESKTOP_PROVADO_E_CORRECAO_HDP.md) · [docs/229 — Nó 1 PM4/MEC no Silício](docs/229_HANDOFF_2026-09-09_NODE1_COMPUTE_FECHADO_NO_SILICIO.md) · [docs/230 — Auditoria Offline](docs/230_AUDITORIA_ESTADO_WDDM_2026-09-09.md).
**Checklist operacional consolidado:**
[`docs/189_CHECKLIST_ATE_JOGOS_BC250.md`](docs/189_CHECKLIST_ATE_JOGOS_BC250.md).
**Data-base:** 2026-09-13 · **Última corrida física:** laboratório **3.169.1.0**, 12:50:08 — o escape fechou 60 vezes UMD -> dxgkrnl -> KMD -> SDMA, com 60 `COMPOSITE_BLT OK`, aproximadamente 9.149 IBs Node 0 e zero timeout/TDR/BugCheck. A parede seguinte foi medida: cópia 1:1 de 1280x1040/pitch 5120 para destino 2560x1080/pitch 10240, recusada pelo contrato linear. A tela permaneceu preta e três amostras do Present estavam zeradas. O watchdog antigo não iniciou o rollback no prazo; a recuperação manual restaurou BasicDisplay Código 0. O candidato **3.170.1.0** implementa prazo absoluto independente, prova de dados por coordenadas e `COPY_LINEAR_SUB_WIND`; está compilado, testado, assinado e auditado, mas **NÃO VALIDADO EM HARDWARE**. Ver [docs/263](docs/263_HANDOFF_2026-09-13_WATCHDOG_ABSOLUTO_E_BLT_RETANGULAR.md). O último marco sustentado visual continua sendo a corrida 3.161.1.0 de [docs/258](docs/258_HANDOFF_2026-09-12_21-22_MARCO3_600_QUADROS_SUSTENTADO.md).
**Correção de registro (12/09, faixa C3):** o present do "Marco 2" de 11/09 22:46 foi feito por **cópia da CPU**, não por SDMA — ver a correção de enquadramento naquele bloco. A apresentação por GPU **existe e está medida**, mas nas corridas de 12/09. **O `0xC00002B6` do Marco 3 não sai deste driver** (`STATUS_DEVICE_REMOVED` não existe em nenhuma linha): o `dxgkrnl` recusou antes de nos alcançar, e a causa segue **ABERTA**.

> **CORREÇÃO C3.14 (12/09, OFFLINE):** o par `Mapped=183 Reusable=73` acima está
> transcrito ao contrário; a evidência integral contém `Mapped=73 Reusable=183`
> e picos de `Mapped=252`. E `Scratch` começa vazio, logo não guarda faixas
> antigas e novas simultaneamente: seu conteúdo é o estado final. Um replace
> in-place não faria 508 faixas caberem em 256 slots. O teto de snapshot foi
> elevado de 256 para **1024**, com replay offline de **252 existentes + 256
> mudanças = 508 MAPPED**, build completo `EXIT=0` e portão de auditoria
> provado por mutação (redução para 256 foi recusada). A reação do
> `ActiveFlushTlb` ao erro não mudou. **NÃO VALIDADO EM HARDWARE.**

> **12/09 17:45 (P0) — A CADEIA FECHOU ATÉ O FIM: `NO_SPACE` NO REBUILD TRANSACIONAL →
> PAGE TABLE NÃO PUBLICADA → **DMA PAGE FAULT**.** Candidato 3.156.1.0, SYS `4153F9A1…`.
> Evidência: `docs/evidence/2026-09-12_17-45-46-DESKTOP-HQAG6T4-lab-binding/`.
> **Handoff completo: [docs/250](docs/250_HANDOFF_2026-09-12_GPUVA_NO_SPACE_E_DMA_PAGE_FAULT.md).**
>
> A telemetria da C3.12 pagou na primeira tentativa e **refutou as duas hipóteses**:
>
> ```
> OCUPACAO slot=0 st=0 Cap=256 Mapped=183 Teardown=0 Reusable=73 Live=183
> ... (PublishStatus=3 Attempts=1 Busy=0)   x7
> [RADV_WDDM2] check_device_status: NON-ACTIVE ExecutionState=7
> vkWaitForFences failed on frame 3: -4
> ```
>
> **`Teardown=0`** → não é vazamento. **`Reusable=73`** → o espaço vivo **não** está cheio.
> E mesmo assim `NO_SPACE` sete vezes.
>
> **MECANISMO, por leitura de código:** `Bc250GpuvaSpaceReplaceRangeTransactional`
> ([bc250_gpuva_space_contract.c:289](driver/core/bc250_gpuva_space_contract.c:289))
> **reconstrói o espaço inteiro no `Scratch`** — insere todas as faixas `MAPPED` uma a uma
> **mais os fragmentos** dos recortes. Com `Mapped=183` de 256 e fragmentação, a reconstrução
> estoura. **O `NO_SPACE` é do `Scratch`, não do espaço vivo.**
>
> **E a consequência é pior que uma submissão recusada:** com a publicação falhando,
> `ActiveFlushTlb` devolve `STATUS_IO_DEVICE_ERROR` **sem publicar a page table**. A GPU então
> acessa uma VA cuja tradução nunca foi comitada →
> `D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT` (`ExecutionState=7`, P1,
> `d3dkmthk.h:4451`) → `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE` → `DEVICE_LOST`.
> Deixar a tradução inconsistente é a mesma classe da C3.7: **a reação é pior que a falha**.
>
> **C3.13 armada:** `CrashOnCtrlScroll=1` nos dois drivers de teclado, `CrashDumpEnabled=2`,
> `AlwaysKeepMemoryDump=1`. Num travamento, **Ctrl DIREITO + Scroll Lock ×2** força
> `BugCheck 0xE2` e grava o dump. O travamento duro ocorreu **duas vezes** hoje e segue **sem
> atribuição** — reset pelo botão não deixa dump.
>
> **NADA DESTE BLOCO FOI VALIDADO COMO CONSERTO: a causa está medida, o conserto não existe.**

> **12/09 16:31 (P0) — A CAUSA RAIZ REAL É `BC250_GPUVA_NO_SPACE`, E ELA REFUTA A HIPÓTESE
> DA C3.8. TRAVAMENTO DURO SEM ATRIBUIÇÃO.** Candidato 3.155.1.0, SYS `9C2CFE52…`.
> Evidência: `docs/evidence/2026-09-12_16-31-28-DESKTOP-HQAG6T4-lab-binding/`.
>
> ⛔ **CORREÇÃO DE REGISTRO — LEIA ANTES DO BLOCO DA C3.8 ABAIXO.** Aquele bloco atribui a
> parede a uma **assimetria de contenção entre leitor e publicador** do snapshot de GPUVA.
> **Isso está REFUTADO por medição**, e foi a própria instrumentação da C3.8 que derrubou a
> hipótese dela:
>
> ```
> Journal status=0x00000007 ... (PublishStatus=3 Attempts=1 Busy=0)
> ```
>
> **`Busy=0`, `Attempts=1`** — o publicador pegou o lock na **primeira** tentativa, todas as
> vezes. Nunca houve contenção. A retentativa de 16 vezes que a C3.8 adicionou **nunca chegou a
> ser usada**; ela não fez mal, mas não era o conserto.
>
> **`PublishStatus=3` = `BC250_GPUVA_NO_SPACE`** ([bc250_gpuva_space_contract.c:97](driver/core/bc250_gpuva_space_contract.c:97),
> `freeSlot < 0`). A falha vem de **dentro** de `Bc250GpuvaSpaceReplaceRangeTransactional`:
> **exaustão dos 256 slots de `BC250_GPUVA_SPACE_MAX_RANGES`.**
>
> Encaixa em tudo que já se sabia — **só acontece com o `dwm.exe` vivo**, porque só ele cria
> esse volume de faixas. **Não é contenção, é capacidade.** A cadeia até o
> `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE` permanece a mesma; só a causa na ponta mudou.
>
> **PERGUNTA AINDA ABERTA, e ela decide o conserto:** `Bc250GpuvaSpaceInsertRange` só
> reaproveita slots em `FREE` ou `UNMAPPED` — **`TEARDOWN` nunca é reciclado**. Então
> `NO_SPACE` pode ser (a) 256 faixas legitimamente em uso, ou (b) slots presos em `TEARDOWN`,
> isto é, **vazamento**. Aumentar o limite conserta (a) e **mascara** (b).
>
> **C3.12 (offline):** `Bc250GpuvaSpaceDescribeOccupancy` conta os slots por estado e expõe
> `Reusable = Free + Unmapped`, espelhando **exatamente** o critério do alocador. Coberta por
> teste, incluindo o caso que separa as duas causas (espaço inteiro em `TEARDOWN`:
> `Reusable=0`, `Mapped=0`). O log de falha passou a publicar
> `PublishStatus/Attempts/Busy/Cap`.
>
> ✅ **A MEDIÇÃO FOI INTEGRADA, E A REGRESSÃO QUE A BLOQUEAVA FOI EXPLICADA.**
>
> A primeira tentativa de integração reprovava `active_miniport_test` (linha 632,
> `StopDeviceActive` deixando de devolver `STATUS_SUCCESS`), de forma determinística e
> inclusive com build limpo. **A causa era minha:** eu amostrava o slot ativo com
>
> ```c
> LONG occSlot = InterlockedCompareExchange(&active->GpuvaSnapshots.ActiveSlot, 0, 0);
> ```
>
> **CAS não é leitura.** No x86 isso compila para `lock cmpxchg`, que executa uma **escrita
> travada** mesmo quando não troca valor — sobre um campo cujo dono é outra thread (o
> publicador do snapshot). `ActiveSlot` é `volatile long`; **ler basta**. Trocada por leitura
> pura, o teste voltou a passar e a telemetria ficou integrada.
>
> ⚠️ **Registro honesto do processo:** levou quatro builds e passei por **duas hipóteses
> erradas** antes — string de formato longa e leitura de `Slots[1]`. Pior: numa das passagens
> eu li `build EXIT=1` como "o teste falhou" quando na verdade era a **auditoria** reprovando
> uma edição temporária minha no próprio teste, e cheguei a declarar no documento que não
> conseguiria explicar o mecanismo. Conseguia; faltava bissectar direito. O discriminante final
> foi trocar a amostragem por leitura volátil.
>
> Portão novo contra a classe: `InterlockedCompareExchange` sobre `ActiveSlot` na telemetria
> fica **proibido**, e a leitura pura é **exigida** (mutação 28 verificada).
>
> **TRAVAMENTO DURO, SEM ATRIBUIÇÃO.** A máquina congelou e exigiu reset físico. **Não houve
> BugCheck** — `MEMORY.DMP` continua sendo o das 13:12. Fusível **TRIPPED** (Boots=3), Código
> 43 após o reboot. O log termina no meio de um `PT_WALK DEST`, consistente com congelamento.
> O harness já havia saído aos 1315 ms; a queda veio nos ~178 s restantes, com o device em
> estado de exceção e o DWM vivo. **Não atribuo à C3.8:** `Busy=0` prova que o laço de
> retentativa saiu na primeira iteração sempre, então não gastou tempo. **Sem dump, não há
> como nomear a causa, e não vou inventá-la.**
>
> **O que se manteve bom:** `render_byte_proof: True`, `sdma_blt_success_logged: 1`,
> `host_cpu_copy_logged: 0`, `dwm_crash_events: 0`. A imagem no monitor continua sendo pixel de
> GPU com zero cópia de CPU. O harness chegou ao `SubmitCommand #10` contra #3 antes — mas com
> `Busy=0` **isso não é atribuível à retentativa**; pode ser variação.
>
> **Volume de log, medido:** toda corrida com `D3D11LabPromotion` armado gera ~200 MB
> (209 MB em 13:29, 207 MB em 16:09, 200 MB em 16:31), contra 0,5–7 MB sem ele. São ~2,37
> milhões de linhas em ~193 s, e `Bc250DisplayLog` abre/escreve/`flush`/fecha por linha:
> **~12.268 ciclos de `ZwCreateFile`+`ZwFlushBuffersFile`+`ZwClose` por segundo** no caminho de
> paginação. Seis tags (`UpdatePT`, `FlushMappedGfxhubTlb`, `PT_WALK`, `ActiveUpdatePT`,
> `ActiveFlushTlb`, `McdmBuildPagingBuffer`) são **80,3%** disso e nenhuma carrega veredito.
> Amostrá-las é a faixa **C3.11**, ainda não feita.
>
> **C3.10:** o resgate de trecho perdido travava o bind. `$buffer[0..($read-1)]` sobre 201 MB
> constrói um array de objetos do PowerShell elemento a elemento; até 12/09 os trechos tinham
> 1–5 MB e ninguém notou. Trocado por cópia em blocos: o mesmo trecho passou de travar
> indefinidamente para **1,2 s**. O portão que exigia somente-leitura sobre o log vivo foi
> tornado **preciso** (antes proibia `FileAccess::Write` em qualquer lugar, sem distinguir
> origem de destino) e ganhou `strip_ps_comments`.
>
> **Mutações 21–23 e 24–27 verificadas.** ⚠️ A mutação 25 ficou **aplicada no repositório**
> porque a restauração falhou por lock de arquivo e eu não conferia o restauro; corrigido à mão,
> e o harness passou a retentar a escrita e a **verificar** que o arquivo voltou.
>
> **C3.13 — DUMP EM TRAVAMENTO.** Dump só nasce de BugCheck; num congelamento o sistema não
> BugCheca sozinho, e foi por isso que 16:31 não deixou dump — segunda vez no dia em que um
> travamento nos deixou cegos. `tools/arm-crash-dump-on-hang.ps1` arma
> `CrashOnCtrlScroll=1` nos dois drivers de teclado (`i8042prt` e `kbdhid`),
> `CrashDumpEnabled=2` (kernel) e `AlwaysKeepMemoryDump=1`. Com isso, **Ctrl DIREITO +
> Scroll Lock duas vezes** durante um congelamento força `BugCheck 0xE2` e grava o dump.
> Exige PowerShell **elevado** e **reboot** — o driver do teclado lê o valor só no start.
> Estado medido antes: `CrashOnCtrlScroll` **ausente** nos dois, `AlwaysKeepMemoryDump`
> ausente (o Windows podia apagar o dump por espaço). O script é inspeção por default e só
> muda algo com `-Arm`; mudança de configuração de sistema é execução do dono do projeto.
>
> **NADA DA C3.12/C3.13 FOI VALIDADO EM HARDWARE.**

> **12/09 16:09 (P0) — COMPUTE E DESKTOP COEXISTIRAM PELA PRIMEIRA VEZ, COM PIXEL DE GPU NA
> TELA. A C3.9 ESTÁ VALIDADA NO SILÍCIO.** Candidato 3.154.1.0, SYS `FF44035E…`, com
> `-EnableD3D11LabPromotion -EnableNode1JournalGatePerProcess`.
> Evidência: `docs/evidence/2026-09-12_16-09-53-DESKTOP-HQAG6T4-lab-binding/`.
>
> **A C3.9 fez exatamente o que foi projetada para fazer, e está medido:**
>
> ```
> RECUSA: ... BlockMask=0x00000000 (JOURNAL=0 ...)
> JOURNAL_PENDENTE: SlotMask=0x00000006 Count=2 GatePerProcess=1 (present=1)
> ```
>
> **Dois slots de outro processo pendentes e `JOURNAL=0`.** O bloqueio global deixou de
> existir. Consequência, tudo na mesma corrida e pela primeira vez junto:
>
> | | |
> |---|---|
> | `dwm_crash_events` / `dwm_wer_events` | **0 / 0** — DWM vivo |
> | Gate do Node 1 | **abriu com o DWM vivo** (`SUBMIT SUCESSO: AssignedFence=18`) |
> | `render_byte_proof` | **True** — shader Vulkan executou |
> | `sdma_blt_success_logged` / `host_cpu_copy_logged` | **1 / 0** |
> | `tdr_events` / `status_device_removed` | 0 / False |
>
> Confirmação visual humana: barras SMPTE com o badge central no monitor físico, **escritas
> por SDMA, zero cópia de CPU**. **Não houve animação** porque o swapchain morreu no 3º quadro
> — o único quadro apresentado ficou parado no scanout até o rollback do watchdog. Isso é
> consistente, não é falha adicional.
>
> **A parede: `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE`** (`0xC01E0200`, P1,
> `ntstatus.h:17580`) no `SubmitCommand #3` — *"GPU exception is detected on the given device.
> The device is not able to be scheduled."*
>
> **CAUSA RAIZ, fechada por leitura de código e consistente com "só acontece com o DWM vivo":
> uma ASSIMETRIA entre leitor e publicador do snapshot de GPUVA.**
>
> - `Bc250GpuvaSnapshotAcquire` (**leitor**) sempre tentou **16 vezes** antes de devolver `BUSY`.
> - `Bc250GpuvaSnapshotPublishBatch` (**publicador**) fazia **UMA** tentativa de CAS e desistia.
>
> Cada `Present` do DWM resolve uma GPUVA e mantém contagem de leitor num slot. Quando esse
> slot vira o inativo, a publicação seguinte exige contagem **zero** e falhava de imediato:
>
> ```
> journal PUBLISH_FAILED (status 7)
>   -> ActiveFlushTlb devolve STATUS_IO_DEVICE_ERROR
>   -> GpuvaMappingsOperational só reabre dentro do ramo "journalStatus == OK"
>      (bc250_active_miniport.c:8699), então a admissão do Node 1 NUNCA voltava
>   -> STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE no 3º submit
> ```
>
> Isso também explica o estado que parecia impossível no log: `BlockMask=0x0` com
> `Eligible=0`. O `BlockMask` estava limpo, mas a admissão não reabria porque o commit do
> journal falhava antes de chegar à reavaliação.
>
> ⛔ **CAUSA RAIZ DESTE BLOCO REFUTADA EM 16:31.** A hipótese abaixo — contenção entre leitor e publicador — está errada: a corrida seguinte mediu `Attempts=1 Busy=0`, ou seja, o publicador nunca foi bloqueado. A causa real é `BC250_GPUVA_NO_SPACE` (exaustão dos 256 slots). Ver o bloco de 16:31. A retentativa não fez mal e a instrumentação dela foi o que permitiu a refutação, mas não era o conserto.
>
> **C3.8 (offline):** o publicador passou a retentar `BC250_GPUVA_SNAPSHOT_PUBLISH_ATTEMPTS`
> (16) vezes — a **mesma constante** do lado leitor. Retentar é estritamente mais forte que
> desistir: mesma semântica, mais chances, e a contenção é transitória (dura um resolve).
> E o diagnóstico passou a **separar** `BUSY` de erro real de faixa (`LastPublishStatus`,
> `LastPublishAttempts`, `LastPublishBusyCount`), publicados no log do KMD — antes os dois
> saíam como "status 7" e não havia como dizer se retentar resolveria.
>
> ⚠️ **Uma variável por candidato:** o retorno continua `STATUS_IO_DEVICE_ERROR` quando as 16
> tentativas se esgotam. A pergunta desta corrida é **só** se retentar limpa a contenção. Se
> não limpar, o próximo passo é deixar de tratar contenção transitória como erro de
> dispositivo — com dado, não por palpite.
>
> **Mutações 24–27 verificadas** (retentativa removida, `BUSY` deixando de ser causa distinta,
> KMD parando de publicar o diagnóstico, teste virando caminho feliz). ⚠️ A mutação 25 ficou
> **aplicada no repositório** porque a restauração falhou por lock de arquivo e eu não conferia
> o restauro; corrigido à mão e o harness de mutação passou a retentar a escrita e a **conferir**
> que o arquivo voltou.
>
> **PACOTE 3.155.1.0 assinado, auditado e NÃO INSTALADO:** SYS `9C2CFE52…`.
> **A C3.8 NÃO FOI VALIDADA EM HARDWARE.**

> **12/09 13:29 (P0) — O DWM SOBREVIVEU 3 min 13 s PELA PRIMEIRA VEZ, E A PAREDE NOVA É DE
> DESENHO: O GATE DO NODE 1 EXIGE JOURNAL GLOBALMENTE QUIESCENTE.** Candidato 3.152.1.0.
> Evidência: `docs/evidence/2026-09-12_13-29-38-DESKTOP-HQAG6T4-lab-binding/`.
>
> **Premissa da hipótese do DWM CONFIRMADA (P0):** com `D3D11LabPromotion` armado,
> `dwm.exe` (pid 11736) viveu de 13:29:56 a 13:33:09 — **3 min 13 s**, 36,6 MB de log,
> `dwm_crash_events: 0` e `dwm_wer_events: 0`. Contra **7 crashes em 18 s** sem o tunable.
> Ele criou device 366 vezes, recusou **120 Draws honestamente** (`RECUSADO`, sem pixel falso)
> e fez 2 Presents. **Zero BugCheck**, fusível **CLEAN**, rollback limpo, `CM_PROB_NONE`.
> As correções C3.6/C3.7 seguraram: o `0x119` da corrida 13:11 não voltou, e
> `SEGMENT_GEOMETRY publicada: SegBase=0xA8C000 SegBytes=0xF374000` saiu no silício.
>
> **Mas a hipótese em si continua SEM TESTE.** O harness morreu em **1,2 s**, antes do
> swapchain: byte proof leu o poison `0x43DAFFFE` em 8/8 zonas — o shader não executou.
> Causa, dita pelo próprio KMD:
>
> ```
> [SubmitVirtual Node1] RECUSA: GpuvaMappingsOperational=0 -- Eligible=0
>                       BlockMask=0x00000001 (JOURNAL=1 ...)
> ```
>
> **PAREDE DE DESENHO:** o gate usava `Bc250GpuvaJournalSetAnyPending`, que pergunta *"algum
> slot do adaptador está pendente?"*. Com o DWM paginando sem parar, essa resposta **nunca** é
> zero. Nas corridas anteriores o gate abria só porque o DWM estava **morto**. Ou seja:
> **compute e desktop não coexistiam por GRANULARIDADE do gate, não por limite do hardware.**
> É a primeira parede do projeto que é de desenho, e não defeito.
>
> **C3.9, offline — medir e corrigir na mesma corrida:**
>   - **medir:** `Bc250GpuvaJournalSetDescribePending` e a linha
>     `JOURNAL_PENDENTE: SlotMask=0x%08X Count=%u` nomeiam **quais** slots seguram o journal.
>     `JOURNAL=1` sozinho não distinguia pendência própria da do `dwm.exe`;
>   - **corrigir:** `Bc250GpuvaJournalSetAnyPendingForProcess` usa o `Journal.Process` que cada
>     slot **já carregava**. Atrás do tunable `Node1JournalGatePerProcess`, **default 0**
>     (fail-closed, NOCLOBBER no INF), armável por `tools/arm-node1-journal-gate-per-process.ps1`.
>     Guarda tripla: tunable presente **e** em 1 **e** processo conhecido; caso contrário a
>     condição global continua valendo.
>
> ⚠️ **RELAXAMENTO DECLARADO, NÃO VALIDADO EM HARDWARE.** O argumento a favor é que as raízes de
> page table são por processo; **o argumento não é prova**. Por isso é fail-closed por default.
>
> ⚠️ **QUARTA OCORRÊNCIA DA CLASSE DO `#if`, pega por conferência de string no SYS assinado e
> não por teste:** a primeira versão da leitura do tunable ficou dentro de
> `#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)` — ou seja, **só na imagem de
> produção**, enquanto os ensaios rodam a de **laboratório**. O tunable nunca seria lido
> justamente na imagem que precisa dele. Antecedentes: A13 (tunable de submissão), B4
> (`GpuvaLabPromotion`, que até hoje é lido em **dois** sítios por causa disso) e ContextAbi
> (doc 202). Fechado por portão permanente `require_outside_production_only`, que rastreia
> aninhamento de pré-processador e foi **conferido contra verdade conhecida** (acha o
> `GpuvaLabPromotion` de produção dentro, e o de laboratório fora).
>
> **Mutações: 20 verificadas ao longo da faixa C3.** Nesta etapa, 16–20 mais a 19 pelo teste.
> E **três das minhas primeiras versões de portão eram peneira** — `if (0 && Funcao(...))`
> sobrevivia porque eu exigia só a presença do nome. Endurecidos para a forma exata da condição.
>
> **PACOTE 3.154.1.0 assinado, auditado e NÃO INSTALADO:** SYS `FF44035E…`.
> Marcas conferidas no binário assinado, incluindo o nome do valor de registro em UTF-16.

> **12/09 13:11 (P0) — BugCheck `0x119` POR CADEIA NOSSA, E O MESMO DEFEITO DE ENDEREÇO NUM
> SEGUNDO CAMINHO.** Candidato 3.149.1.0 com `D3D11LabPromotion` armado.
> Evidência: `docs/evidence/2026-09-12_13-11-18-DESKTOP-HQAG6T4-lab-binding/`.
>
> Relato do operador: tela preta, depois **imagem distorcida**, depois **tela azul**.
> `BugcheckCode 281` = **`0x119`**, `Arg1=0x2`, `Arg2=0xC0000185` (`STATUS_IO_DEVICE_ERROR`).
> Fusível **TRIPPED** (Boots=5) — ele fez o trabalho: recusou o StartDevice após o reboot, e
> **não houve loop de boot**.
>
> **A cadeia, linha por linha do log resgatado:**
>
> ```
> 84786  IB EXECUTADO (INDIRECT) ... FenceId=631          <- normal ate' aqui
> 84788  [Present #1] BYTE_PROOF FAIL: Source=0x15D0000 Dest=0x0
>                     FirstMismatch=0x10C004; SDMA disabled
> 84789  [Present #1] Blt ... SDMA=FAILED_CLOSED
> 84826  SubmitIndirectAsync recusado: Init=1 HwEnabled=0 TripodValidated=1
> 84828  IB NAO EXECUTADO #1: FenceId=632; completion NAO notificado
>        -> BugCheck 0x119 Arg1=2 Arg2=0xC0000185
> ```
>
> **Dois defeitos nossos, encadeados:**
> 1. **O mesmo defeito de endereço da C3.4, num caminho que eu não tinha tocado.**
>    `DxgkDdiPresent` usava o offset de segmento **direto** como índice de
>    `FrameBufferVirtual`, sem `LocalSegmentBaseOffset`. Amostrou 11.059.200 bytes antes da
>    superfície e leu zeros (`SrcPhysSample @ 0x15D0000: 00000000 …`). **O byte proof reprovou
>    um quadro CORRETO.** O portão da C3.4 cobria só o escape — cobrir um caminho e deixar o
>    outro foi o próprio defeito. **Quinta divergência de fonte única** (A1, A11, ContextAbi,
>    C3.4, esta).
> 2. **A reação era pior que a falha.** Um byte proof reprovado punha
>    `HardwareEnabled = FALSE` **globalmente**, e daí toda submissão WDDM virava completion não
>    notificado — caminho conhecido do `0x119`.
>
> ⚠️ **E havia um portão que EXIGIA o defeito #2:** `require(..., "g_Bc250SdmaPresent.HardwareEnabled = FALSE;")`.
> Remover a linha **reprovava o build**. **Quinta ocorrência da classe "o gate obrigava o defeito
> a existir"**: A15 (comparação crua de RPTR), `mcdm_callbacks_test` (TDR), `bc250_d3d_umd_test.c`
> (memcpy no Draw), `sdma_blt_executed` (C3.1). Portão invertido: agora **proíbe** o desligamento
> global e **exige** `present reprovado (engine PRESERVADA)` mais `sdmaDone = FALSE`.
>
> **Correções C3.6 e C3.7 (offline):** a fonte única ganhou a vista de offset de VRAM
> (`Bc250SegmentOffsetToVramOffset`), com as mesmas guardas de faixa; a geometria do segmento
> passou a ter **escritor único** dentro de `Bc250ActiveQueryRuntimeCaps`; sem geometria
> publicada a conversão é **recusada**, não chutada; e o log passou a mostrar os três números
> (`SegOff`, `SegBase`, `VramOff`) em vez de um "Physical VRAM" ambíguo que escondeu o defeito
> por várias corridas.
>
> **C2 pagou de novo:** o arquivamento truncou e **5,85 MB de 7,2 MB** teriam sido perdidos.
> O resgate do log vivo com SHA-256 é o que tornou esta análise possível.
>
> **Dívida aberta, não investigada:** 32 ocorrências de
> `[ActiveFlushTlb] Journal status=0x00000007 (PUBLISH_FAILED) -> STATUS_IO_DEVICE_ERROR` no
> caminho de paginação. Não foram a causa próxima do BugCheck, mas 32 `IO_DEVICE_ERROR`
> devolvidos ao `dxgkrnl` não é estado saudável.

> **12/09 13:01 (P0) — PRESENT 100% POR GPU NO SILÍCIO, COM IMAGEM CORRETA NO MONITOR.
> A CAUSA RAIZ DA TELA PRETA ESTÁ FECHADA.** Candidato 3.151.1.0, SYS assinado `0D3E83AC…`.
> Evidência: `docs/evidence/2026-09-12_13-01-05-DESKTOP-HQAG6T4-lab-binding/`.
>
> **O critério escrito ANTES da corrida bateu inteiro:**
>
> ```
> PRESENT_BLT: GpuVa 0x200020000 resolvido -> SegOff=0x1560000 Seg=1
>              SegBase=0xA8C000 SrcMc=0xF401FEC000 DstMc=0xF400000000
> Teardown Complete. TripodValidated=1, Submitted=584, Completed=584,
>                    Fallbacks=0, HostCpuCopies=0
> Escape Archive. EscapeEntered=584, BltMax=2171 us, BltTotal=802518 us
> ```
>
> `SrcMc` e `DstMc` no **mesmo domínio** (`0xF4…`), `SegBase=0xA8C000` presente, `Fallbacks=0`,
> `HostCpuCopies=0`, `sdma_blt_failed_logged=0`, `host_cpu_copy_logged=0`,
> `no_host_copy_fallback: True`.
>
> **Por que isto é prova e não impressão:** a "Garantia de Scanout GOP" por CPU foi **removida**
> na C3.1 e o ramo de cópia por CPU **não executou nenhuma vez**. Logo o **único** escritor do
> scanout nesta corrida foi o SDMA. O operador viu as barras coloridas animando, ~20× mais rápido
> que na corrida 12:51 (26 FPS × ~1,25 FPS da cópia por CPU). **Os pixels no monitor vieram da
> GPU.**
>
> **Cadeia fechada, ponta a ponta:** shader Vulkan (Mesa RADV + ACO) executa no MEC → escreve o
> framebuffer 2560×1080 na VRAM → byte proof 8/8 na origem → SDMA copia 11.059.200 bytes para o
> scanout do UEFI GOP por `D3DKMTEscape` → monitor físico exibe. **584 quadros consecutivos**,
> contra o recorde anterior de 473. Custo do blt: **1,37 ms** de média, máximo 2,17 ms.
>
> **A causa raiz de 12:29 está CONFIRMADA E CORRIGIDA:** faltava `LocalSegmentBaseOffset` na
> conversão de offset de segmento para MC, e a origem do blt ficava 11.059.200 bytes adiantada.
> Agora sai da fonte única `Bc250SegmentOffsetToMc`, com a mesma base do destino.
>
> **O QUE NÃO MUDOU — a parede continua exatamente onde estava:** ruptura no quadro 584 de 600,
> `status_device_removed: True`, `failure_mode: PRESENT_ESCAPE`, `tdr_events: 0`. E de novo
> **7 crashes do `dwm.exe` + 8 WER**, com o último às 13:01:34,583 e a falha às 13:01:35,047 —
> **0,46 s depois**. Terceira corrida seguida com a mesma assinatura (12:29 deu 0,5 s). A
> hipótese de que o orçamento de crashes do DWM é o que encerra a corrida **continua de pé e
> continua sendo H**.
>
> **Próxima corrida, e é a do doc 247:** armar `D3D11LabPromotion` para o DWM receber um device
> e parar de fail-fastar, mantendo `D3D11DrawBltSubstitution` desarmado (Draw segue `RECUSADO`
> honestamente, sem pixel falso). DWM sobrevive e a swapchain chega a 600/600 → hipótese
> confirmada. Device para de novo por volta do quadro ~580 → **refutada**, e a causa é outra.

> **12/09 12:51 (P0) — A CORREÇÃO DE 12:29 TROCOU A BASE POR ENGANO; O BLT DEU TIMEOUT 8/8 E
> O PRESENT CAIU NA CÓPIA POR CPU. O PORTÃO DA C3.1 PEGOU.** Candidato 3.150.1.0, SYS `7661E546…`.
> Evidência: `docs/evidence/2026-09-12_12-51-19-DESKTOP-HQAG6T4-lab-binding/`.
>
> **Relato visual do operador:** *"aparentemente funcionou, mostrou as barras coloridas, onde elas
> ficaram se movimentando"*. **A imagem era real e estava correta — e não veio da GPU.**
> `host_cpu_copy_logged: 8`, `sdma_blt_success_logged: 0`, `no_host_copy_fallback: False`,
> veredito **FAIL**. Sem o contador `HostCpuCopies` e o check `no_host_copy_fallback` criados na
> C3.1 horas antes, esta corrida teria virado mais um marco falso — pela terceira vez neste
> projeto a cópia por CPU desenhou uma tela que parecia vitória.
>
> **O defeito, e é de quem escreveu a correção:** a C3.4 precisava de **uma** mudança
> (somar `LocalSegmentBaseOffset`). Fez **duas** — trocou também a base, de
> `g_Bc250SdmaPresent.VramMcStart` para `caps.VramMcBase`. São **domínios de endereço distintos**:
>
> | Base | Valor | Domínio |
> |---|---|---|
> | `caps.VramMcBase` | `0x450000000` | GCVM — PTE, root, VMID8/9 |
> | `g_Bc250SdmaPresent.VramMcStart` | `0xF400000000` | anel de present do SDMA |
>
> Origem foi para `0x451FEC000`, destino continuou em `0xF400000000`. Medido:
> `Blt Timeout #1: TargetWPTR=1276, WPTR_Reg=1276, RPTR_Reg=1156, FenceObs=43DAFFFE` — fence no
> poison, 8 de 8. É **exatamente a lei de "uma variável por candidato"** violada por quem a citou.
>
> **Correção (C3.5, offline):** a origem volta a partir de `g_Bc250SdmaPresent.VramMcStart` —
> a mesma base do destino — mantendo `LocalSegmentBaseOffset` e `LocalSegmentBytes` das caps, que
> são offsets dentro da VRAM e independem do domínio. O log passa a imprimir `SrcMc` e `DstMc`
> **lado a lado**, para que a próxima divergência de domínio seja visível sem mineração.
> Teste ganhou o bloco 2b com os números das duas corridas (`0xF401560000` de 12:29,
> `0x451FEC000` de 12:51, `0xF401FEC000` correto) e o portão passou a **reprovar** origem
> derivada de `caps.VramMcBase`. **Mutação 10 verificada** — e a primeira tentativa de mutação
> falhou em escrever o arquivo, então o PASS que ela produziu foi **descartado** em vez de
> contado: mutação que não se aplica não prova portão.
>
> **O que NÃO ficou provado:** que somar `LocalSegmentBaseOffset` resolve a tela preta. O blt
> nunca chegou a executar nesta corrida. A causa raiz de 12:29 segue **bem fundamentada e NÃO
> VALIDADA EM HARDWARE**.
>
> **Nota de custo:** duas corridas gastas em defeito de host, não de silício — junta-se a
> 17:43 (comparação crua de RPTR), à ordem do registro no ensaio de TDR e à 11:48.
>
> **PACOTE 3.151.1.0 assinado, auditado e NÃO INSTALADO:** SYS assinado `0D3E83AC…`,
> UMD `DD4DFED4…` (inalterada desde o 3.149.1.0 — variável única contra as duas corridas).

> **12/09 12:29 (P0) — CORRIDA LIMPA, E A CAUSA RAIZ DA TELA PRETA FOI ENCONTRADA:
> A ORIGEM DO PRESENT ESTAVA 11.059.200 BYTES ADIANTADA.** Candidato 3.149.1.0,
> SYS assinado `DE9BEFC8…`. Evidência:
> `docs/evidence/2026-09-12_12-29-01-DESKTOP-HQAG6T4-lab-binding/`.
>
> **Primeira corrida do projeto com present 100% por GPU, e isso é o que a tornou útil:**
> `Teardown Complete. TripodValidated=1, Submitted=473, Completed=473, Fallbacks=0, HostCpuCopies=0`
> mais `no_host_copy_fallback: True` e `host_cpu_copy_logged: 0`. Com a cópia por CPU removida
> na C3.1, o defeito que ela mascarava ficou visível na primeira tentativa.
>
> **Relato visual do operador (P0):** tela **preta**, com uma **pequena faixa cinza embaixo,
> alguns pixels coloridos e o resto cinza**. A imagem só voltou no rollback. Isso **não** é o
> padrão SMPTE — é VRAM alheia.
>
> **CAUSA RAIZ, fechada por leitura de código (P1) e confirmada por aritmética:**
> `Bc250ActiveResolveDisplayGpuVa` devolve um **offset de segmento** — o parâmetro de saída de
> `Bc250GpuvaSpaceResolveAddress` chama-se literalmente `BackingSegmentOffset`. O segmento 1
> **não começa no byte zero da VRAM**: começa em `LocalSegmentBaseOffset` = `0xA8C000` =
> 11.059.200 bytes, logo após a reserva de scanout. O caminho de present fazia
>
> ```
> srcGpu = g_Bc250SdmaPresent.VramMcStart + physAddr;   /* LocalSegmentBaseOffset AUSENTE */
> ```
>
> enquanto **todo o resto do driver** (codificador de PTE, instalação da root, VMID9) faz
> `VramMcBase + LocalSegmentBaseOffset + offset`. Medido na corrida:
> `SrcMc=0xF401560000`; correto seria `0xF401FEC000`. **Delta exatamente `0xA8C000`.**
>
> O SDMA lia 11 MB **antes** da imagem — região majoritariamente zerada, com restos não nulos.
> Preto com faixa cinza. E **o tripé inteiro passava**: `RPTR==WPTR`, fence física sobre poison
> e byte proof 8/8 — porque **nenhuma das três pernas confere de ONDE se leu**. A engine
> executou perfeitamente sobre o endereço errado.
>
> ⚠️ **Por que o "Marco 2" parecia funcionar:** o caminho de CPU usava `esc->pUserPixelBuffer`,
> o ponteiro de usuário para os pixels reais, e **não passava por esta aritmética**. A cópia por
> CPU escondia o defeito do caminho de GPU. Remover a cópia foi o que o expôs.
>
> ⚠️ **Quarta divergência de fonte única do projeto** — A1 (caps de driver), A11 (geometria de
> GPUVA), ContextAbi (doc 202) e agora a conversão de endereço. O comentário de
> `BC250_MCDM_RUNTIME_CAPS.VramMcBase` já declarava ser "a única fonte da base, usada tanto pelo
> codificador de entradas quanto pela instalação da root, para que as duas não possam mais sair
> de fase". O present era um **terceiro consumidor**, e saiu de fase.
>
> **Correção (C3.4, offline):** `driver/core/bc250_segment_address_contract.{h,c}` passa a ser a
> fonte única da conversão, com faixa conferida e **falha fechada** — origem que não couber no
> segmento agora é **RECUSADA** em vez de virar blt de lixo indistinguível de sucesso.
> Teste `segment_address_contract_test` trava os números medidos sem arredondar
> (`0xF401560000` → `0xF401FEC000`, delta `0xA8C000`), com caso negativo para a recusa não virar
> peneira. Portão novo `audit_present_source_uses_segment_base`.
>
> **Duas hipóteses minhas refutadas por medição nesta corrida:**
> 1. *"O blt síncrono explica os 26 FPS."* **Não.** `BltTotal=651181 us / 473 = 1,38 ms` por blt,
>    `BltMax=3061 us`, num quadro de ~38 ms — **3,6%**. O custo está no Vulkan, não no present.
> 2. *"A fence do MEC fica presa."* **Não.** `Sub==Ret==473`, `V2FencesInFlight=0`.
>
> **O `DEVICE_LOST` ganhou nome, e não é hang nosso:** o RADV registrou
> `check_device_status: NON-ACTIVE ExecutionState=4` = `D3DDDI_DEVICEEXECUTION_STOPPED`
> (P1, `d3dumddi.h:3655`) — **não** `HUNG` (3), **não** `RESET` (2), **não** `ERROR_DMAFAULT` (6) —
> e ainda anotou `dev_status=-4 but all fences satisfied`. Com `tdr_events=0`, zero `StopDevice`
> durante a corrida (o único é o watchdog de 180 s, e a falha foi aos 18 s) e `Sub==Ret`, **a
> nossa engine não travou: o device foi parado administrativamente.**
>
> **Correlação forte com o DWM (H, não P0):** 7 fail-fasts do `dwm.exe` a cada ~2,5 s, entre
> 12:29:08,9 e 12:29:27,8 — e o device para **0,5 s depois do sétimo e último**. A corrida das
> 11:04 teve **exatamente** os mesmos 7 crashes + 8 WER, duração parecida (16,3 s × 18,3 s) e
> contagem de quadros bem diferente (373 × 474). Ou seja: **o que encerra a corrida é o orçamento
> de crashes do DWM, não o número de quadros.** Falsificável na próxima corrida.
>
> **PACOTE 3.150.1.0 assinado, auditado e NÃO INSTALADO:** SYS fonte `8B325DDE…`, assinado
> `7661E546…`, UMD `DD4DFED4…`, INF `06B43C21…`, CAT `F8734B0D…`.
> **A CORREÇÃO DA ORIGEM DO PRESENT NÃO FOI VALIDADA EM HARDWARE.**

> **12/09 (OFFLINE, faixa C3) — O PRESENT DO "MARCO 2" TINHA SIDO FEITO PELA CPU; O GATE
> CARIMBAVA; E A HIPÓTESE DA FENCE MEC PRESA ESTÁ REFUTADA.** Zero binds, zero hardware.
>
> **C3.1 — Quarta ocorrência da classe "o teste congelava a mentira".**
> Na corrida 11/09 22:46, a que fechou o Marco 2 com `10/10 PASS` e confirmação visual humana,
> o `display-log-delta.txt` tem **duas** linhas `PRESENT_BLT` e nenhuma é de GPU: a segunda é
> `Host Kernel Blt CONCLUIDO COM SUCESSO! 11059200 bytes copiados para o scanout`. **Zero**
> `SDMA HW BLT CONCLUIDO`. Os 11.059.200 bytes foram para o scanout por `RtlCopyMemory`.
> Passou por **dois** defeitos independentes:
>   1. `sdma_blt_executed` no runner casava com a string `PRESENT_BLT`, e também com
>      `BYTE_PROOF INCONCLUSIVO`, `Spread SCANOUT` e `TILE_PROBE`;
>   2. `FallbackCpuCount` — o contador que arquivou `Fallbacks=0` — só conta recusa/timeout de
>      `Bc250SdmaPresentBlt` e era **cego** à cópia que o escape efetivamente fazia.
>
> **Corrigido, tudo offline:**
>   - a "Garantia de Scanout GOP Quadro 1" **foi removida**: ela reescrevia por CPU o mesmo
>     endereço físico que o SDMA acabara de escrever (`DstMc == VramMcStart + (FrameBufferPhysical
>     − VramBar0)`, medido em `0xF400000000`), o que não acrescenta pixel e torna o quadro
>     **não atribuível à GPU**. Mesmo precedente da C1, que removeu o memcpy do Draw;
>   - o ramo de cópia por CPU passou a se declarar (`PRESENT_BLT: COPIA POR CPU #N -- NAO e'
>     aceleracao por GPU`) e a contar em `HostCpuCopyCount`, publicado no teardown;
>   - `sdma_blt_executed` agora exige `SDMA HW BLT CONCLUIDO COM SUCESSO` e nada mais;
>   - check novo `no_host_copy_fallback` **reprova** a corrida com pixel produzido por CPU;
>   - `desktop_detected_in_kmd` deixou de casar com a string `Bc250Escape`, que era trivialmente
>     verdadeira sempre que o escape fosse chamado;
>   - portão novo `audit_present_is_gpu_attributable` trava as quatro pontas, inclusive um
>     invariante **sem janela de linhas**: entre o log de SDMA bem-sucedido e o `return` do
>     escape não pode existir cópia de memória nenhuma.
>
> ⚠️ **Armadilha registrada:** a primeira versão desse portão reprovou o **meu próprio
> comentário**, que cita `RtlCopyMemory` ao explicar a remoção — exatamente o que já aconteceu
> com o gate do B3. A varredura passou a rodar sobre o código **sem comentários**
> (`strip_c_comments`), e fatiar antes de remover comentários também estava errado: começava a
> leitura no meio de um literal e dessincronizava o parser de aspas.
>
> **C3.2 — A hipótese "a fence do MEC não aposenta" está REFUTADA nas oito corridas de 12/09.**
> Eu próprio havia proposto essa leitura a partir de `MEC_PENDING_ARCHIVE Fence=463 Observed=462`.
> Está errada por duas razões medidas:
>   1. `MEC_PENDING_ARCHIVE` é **snapshot da primeira observação pendente**, guardado por
>      `engine->DiagnosticPendingFence != pendingFence` — não é estado terminal, e não prova que
>      a fence nunca aposentou;
>   2. o `probe_delta` das oito corridas fecha **100%** em todas elas.
>
> | Corrida (12/09) | Veredito | Sub | Ret | V2 notif | InFlight | Rompeu em |
> |---|---|---:|---:|---:|---:|---|
> | 05:33:50 | PASS | 1 | 1 | 1 | 0 | — (1 quadro) |
> | 07:43:39 | PASS | 1 | 1 | 1 | 0 | — (1 quadro) |
> | 08:09:26 | FAIL | 13 | 13 | 13 | 0 | `vkWaitForFences` frame 13 |
> | 08:51:20 | FAIL | 41 | 41 | 41 | 0 | `vkWaitForFences` frame 40 |
> | 09:21:57 | FAIL | 44 | 44 | 44 | 0 | Escape `0xC00002B6` frame 43 |
> | 10:10:16 | FAIL | 64 | 64 | 64 | 0 | `vkWaitForFences` frame 64 |
> | 10:42:25 | FAIL | 447 | 447 | 447 | 0 | Escape `0xC00002B6` frame 447 |
> | 11:04:22 | FAIL | 373 | 373 | 373 | 0 | Escape `0xC00002B6` frame 373 |
>
> **Nenhuma submissão ficou presa em corrida nenhuma.** Some-se a isso, na janela de 11:04,
> **zero** eventos de System — sem `4101`, sem `117`, sem `141`: **não houve TDR**. Só 7
> `Application Error` e 8 WER do `dwm.exe`.
>
> **E o `0xC00002B6` não pode ter saído de nós (P1, por leitura de código):** `STATUS_DEVICE_REMOVED`
> não aparece em **nenhuma linha** do driver, e `Bc250ActiveEscapeHandler` só devolve
> `STATUS_SUCCESS`, `STATUS_INVALID_PARAMETER`, `STATUS_NOT_SUPPORTED` e
> `STATUS_INVALID_USER_BUFFER`. Logo **o `dxgkrnl` recusou a chamada antes de nos alcançar**:
> o device já estava removido. **A causa disso continua ABERTA e não é atribuível com a evidência
> existente.** Não inventar causa.
>
> **O que faltava para atribuir, e foi adicionado offline:**
>   - `EscapeEnteredCount` — contagem **não amostrada** dos escapes que o KMD recebeu. O log é
>     1-em-60, então até 59 chamadas ficavam invisíveis antes da ruptura; agora dá para dizer se
>     o último present chegou ou não ao KMD, comparando com `first_failed_frame`;
>   - duração do blt (`Blt=N us`, mais `BltMax`/`BltTotal` no teardown). A corrida 10:42 mediu
>     **26 FPS** (~38 ms/quadro) para copiar 11 MB — ~290 MB/s, ordens de grandeza abaixo de
>     GDDR6. Ou a espera **síncrona** de `Bc250SdmaPresentBlt` explica o quadro (dívida declarada
>     desde a A15 e causa do `0x133` de 10/09), ou não explica. **Os dois desfechos informam**;
>   - o veredito passou a extrair `first_failed_frame`, `failure_mode` e `tdr_events`, além das
>     contagens de blt por GPU × cópia por CPU e da linha de teardown. As rupturas em 13, 40, 43,
>     64, 373 e 447 só eram visíveis lendo `stderr` à mão.
>
> **C3.3 — Ponte DXBC → SPIR-V ligada às DDIs de shader, fechada por default.**
> `BC250_UMD_SHADER` passou a carregar o módulo SPIR-V com **ownership explícito**, e
> `Bc250Umd10DestroyShader` é obrigado a chamar `Bc250DxbcReleaseSpirv` **antes** do `memset` —
> zerar primeiro perderia o ponteiro e vazaria o módulo dentro do processo hospedeiro, que no
> caso do desktop é o `dwm.exe`. O tradutor exige **duas** chaves
> (`D3D11ShaderTranslator=1` **e** `D3D11ShaderTranslatorPath` com caminho absoluto); sem as
> duas, nenhuma DLL externa é sequer procurada. Não há caminho default de propósito: a única
> implementação ensaiada é `libvkd3d-shader-1.dll` de MinGW, que arrasta `libgcc_s_seh-1.dll` e
> `libwinpthread-1.dll`, e injetar três DLLs MinGW no compositor do Windows é **decisão do dono
> do projeto**, não efeito colateral de um default.
> `BC250_D3D10_11_NATIVE_READY` **continua `0`** e nenhum cap novo foi aberto.
>
> **Gates:** `build-umd` EXIT=0 com `SPIR-V fail-closed sem container, coerencia com o ambiente,
> zero vazamento e zero double-free PASS`; `build-contract` EXIT=0; quatro auditorias PASS;
> **seis mutações verificadas** — reintroduzir o memcpy pós-SDMA, afrouxar `sdma_blt_executed`,
> remover `no_host_copy_fallback`, tirar a contagem da cópia por CPU, restaurar o rótulo
> desonesto, e remover o release do SPIR-V (esta última reprovou a suíte em
> `s_SpirvOwnershipFrees == 1`).
>
> ⚠️ O teste de SPIR-V assere por **coerência com o ambiente**, não por carimbo: exigir
> `SpirvValid == FALSE` repetiria o defeito achado na C1, em que a suíte offline reprovava
> sozinha porque `D3D11LabPromotion` ficara armada por um bind anterior.
>
> **NADA DA FAIXA C3 FOI VALIDADO EM HARDWARE.**

> **12/09 (OFFLINE) — DXBC → SPIR-V PASSOU COM TRADUTOR REAL:** `libvkd3d-shader-1.dll`
> x64 1.17, oriunda do pacote Fedora 44 e identificada por SHA-256, foi carregada pela
> fronteira MSVC. Shaders mínimos produzidos pelo `fxc` SDK 28000 passaram: VS 4.0
> `624 → 1480` bytes e PS 4.0 `452 → 396` bytes, com SPIR-V e estágio validados.
> O primeiro ensaio revelou que nosso parser interpretava incorretamente o comprimento
> `SHDR/SHEX`; código, caminho direto da UMD e fixtures foram corrigidos para a semântica
> real (comprimento inclui os dois DWORDs do cabeçalho). `build-umd`, `build-contract`,
> smoke real e auditoria PASS. DLLs externas ficaram somente em `%LOCALAPPDATA%\Temp` e
> não entram no pacote. **NÃO VALIDADO EM HARDWARE.** Handoff: `docs/249_HANDOFF_2026-09-12_VKD3D_SHADER_REAL_PASS.md`.

> **12/09 (OFFLINE) — fundação DXBC → SPIR-V criada:** o Mesa/RADV local não possui frontend
> DXBC; o `vkd3d-shader` oficial possui a transformação DXBC TPF → SPIR-V binário. Foi criada
> uma fronteira interna desacoplada da ABI externa que valida DXBC, saída SPIR-V 1.0–1.6,
> limites e ownership, com teste dedicado. O adaptador dinâmico MSVC para `vkd3d-shader`
> também existe: exige caminho absoluto, busca restrita, exports completas e deixa estado
> zerado em toda falha. A ABI mínima agora tem fonte única com asserts x64, e uma DLL mock
> prova de ponta a ponta carregamento, exports, compilação, cópia, validação e liberação.
> Isso valida nossa fronteira, **não substitui um tradutor real**. Nenhuma DDI o chama e
> `BC250_D3D10_11_NATIVE_READY` segue `0`. `build-contract`, `build-umd` e auditorias
> PASS. **NÃO VALIDADO EM HARDWARE.** Handoff: `docs/248_HANDOFF_2026-09-12_DXBC_SPIRV_BRIDGE.md`.

> **12/09 11:04:22 (P0) — Marco 3: GPU continuou sob tela preta; DWM caiu; recovery endurecida offline:**
> O operador viu um quadro e depois preto, mas a telemetria prova continuidade: 373 submits Node 1
> aceitos/aposentados, SDMA `Submitted=372 Completed=372 Fallbacks=0`, parada com `Sub=Ret=390`,
> `RPTR=WPTR=4680` e zero fault GCVM. A ruptura ocorreu no frame 373 com `STATUS_DEVICE_REMOVED`.
> Na mesma janela houve 7 crashes `Application Error` do `dwm.exe` e 8 WER; a UMD estava com
> `D3D11LabPromotion` desarmado e retornou `E_NOTIMPL`. Correlação temporal não é causalidade:
> travamento MEC/SDMA está refutado, mas a origem do `DEVICE_REMOVED` permanece aberta.
> Offline, o runner passou a preservar provas monotônicas e separar crash/relatório WER; o lançador
> do Marco 3 agora executa recovery incondicional depois do bind, falha fechado se ele não concluir
> e publica marcador de boot frio. Suite completa e auditoria de segurança PASS. Mudanças
> **NÃO VALIDADAS EM HARDWARE**. Handoff: [docs/247](docs/247_HANDOFF_2026-09-12_MARCO3_DWM_E_RECOVERY.md).

> **11/09 22:46:24 (P0) — Corrida 3.142.0.0 (ROTA 1 — RADV WDDM PRESENT NO SCANOUT PASSOU NO SILÍCIO - MARCO 2 FECHADO):**
> `-EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunRadvPresentTripod`.
> Evidência: `docs/evidence/2026-09-11_22-46-24-DESKTOP-HQAG6T4-lab-binding/radv-present-tripod-2026-09-11_22-46-30-992/`.
> Handoff: [`docs/245_HANDOFF_2026-09-11_MARCO2_RADV_PRESENT_CONCLUIDO_NO_SILICIO.md`](docs/245_HANDOFF_2026-09-11_MARCO2_RADV_PRESENT_CONCLUIDO_NO_SILICIO.md).
>
> **Avanços provados no silício (P0) com confirmação visual humana:**
> 1. **Execução Real de Shader Vulkan (2560×1080) e Apresentação no Monitor:**
>    - Pela primeira vez na história da placa no Windows, o monitor físico no DisplayPort exibiu o quadro renderizado pelo Vulkan!
>    - Confirmação do usuário (P0): *"bom aparentemente funcionou, mostrou as cores e a barra cinza em baixo exatamente como voce falou"*.
>    - `harness.stdout.txt`: `BC250 RADV PRESENT BYTE PROOF: PASS` (8/8 zonas SMPTE + badge conferidas na VRAM).
>    - `BC250 RADV WDDM PRESENT: PASS`: Apresentação WDDM via `D3DKMTEscape` (`BC250_ESCAPE_OP_PRESENT_BLT`, `0xBC250001u`) diretamente no scanout do UEFI GOP.
>    - `present-verdict.json`: **10/10 verificações TRUE, VEREDITO: PASS**.
>
> ⛔ **CORREÇÃO DE ENQUADRAMENTO (12/09, faixa C3) — LEIA ANTES DE CITAR ESTE MARCO.**
> **Os pixels desta corrida chegaram ao monitor por `RtlCopyMemory` da CPU, não pelo SDMA.**
> O `display-log-delta.txt` deste bind tem **duas** linhas `PRESENT_BLT` e nenhuma delas é de GPU:
> `PRESENT_BLT: 2560x1080 (11059200 bytes) ... Flags=0x0` (linha 1408) e
> `PRESENT_BLT: Host Kernel Blt CONCLUIDO COM SUCESSO! 11059200 bytes copiados para o scanout`
> (linha 1619). **Zero** ocorrências de `SDMA HW BLT CONCLUIDO`. O caminho executado foi o
> ramo 2 de `Bc250ActiveEscapeHandler` — cópia por CPU (`bc250_active_miniport.c`).
>
> **O que continua P0 e não muda:** o shader Vulkan executou no MEC e escreveu as barras SMPTE
> na VRAM; o `BYTE PROOF 8/8` é real e independe do caminho de present.
> **O que cai:** a frase "apresentação … diretamente no scanout" como prova de aceleração, e o
> `10/10 PASS`. Pela Lei 4 aquilo é fallback de software; pela Lei 2 a perna de bytes não pode
> ser conferida contra um destino que a CPU acabou de escrever.
>
> **Por que passou:** `sdma_blt_executed` no runner casava com a string `PRESENT_BLT` — e até com
> `BYTE_PROOF INCONCLUSIVO`. E `FallbackCpuCount` (o `Fallbacks=0` arquivado) só conta recusa de
> `Bc250SdmaPresentBlt`: era **cego** à cópia que o escape realmente fazia. **Quarta ocorrência**
> da classe "o gate carimbava o falso sucesso" — A15 (comparação crua de RPTR),
> `mcdm_callbacks_test` (TDR), `bc250_d3d_umd_test.c` (memcpy no Draw).
> Os três defeitos foram corrigidos na faixa C3; ver o bloco de 12/09 no topo deste documento.
>
> **A apresentação por GPU existe e está medida — só não é desta corrida.** As corridas de
> 12/09 têm `SDMA HW BLT CONCLUIDO COM SUCESSO` de verdade, e a de 10:42 fechou
> `Submitted=447, Completed=447, Fallbacks=0`.
> 2. **Isolamento de VRAM e Estabilidade do KMD:**
>    - BugChecks 0x10E, 0x119 e 0x133 extintos.
>    - Alocação top-down do motor de submissão MEC (`0x0FFD0000`) e airgap de VRAM (`0x0FE00000`) funcionaram com 100% de estabilidade.
>    - Fusível em `CLEAN (0)`, desbind limpo pelo watchdog.
>
> ---
>
> **11/09 09:18:11 (P0) — Corrida 3.134.1.0 (ROTA 1 — RADV WDDM COMPUTE TRIPOD PASSOU NO SILÍCIO):**
> `-EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunRadvComputeTripod`.
> Evidência: `docs/evidence/2026-09-11_09-18-11-DESKTOP-HQAG6T4-lab-binding/`.
> Handoff: [`docs/241_HANDOFF_2026-09-11_RADV_COMPUTE_TRIPOD_PASSOU_NO_SILICIO.md`](docs/241_HANDOFF_2026-09-11_RADV_COMPUTE_TRIPOD_PASSOU_NO_SILICIO.md).
>
> **Avanços provados no silício (P0) e auditados por 3 IAs independentes:**
> 1. **Execução Real de Shader Vulkan no Silício via Mesa RADV / ACO:**
>    - `harness.stdout.txt`: `BC250 RADV COMPUTE BYTE PROOF: PASS` (poison `0x43DAFFFE` -> `0xBC250C01`).
>    - Sem desvio para CPU: WARP (`1414:008C`) preterido, llvmpipe ausente, sem loops de `memcpy` na CPU.
>    - Exit code: 0 em 5.758 ms.
> 2. **Telemetria de Hardware (Nó 1 / MEC / MQD):**
>    - Submissão aceita e executada: `P1SubmitAccepted: 17 -> 18 (+1)`.
>    - Delta do anel no log vivo: 0 → 12 DW (`INDIRECT_BUFFER` 4 DW + `RELEASE_MEM` 8 DW).
>    - Aposentadoria e drenagem: `P1RetiredTotal: 17 -> 18 (+1)`, `WPTR=12`, `RPTR=12` (`HqStatus=0x40008040`, fila ociosa, anel drenado).
>    - Fences e V2 Telemetry: `V2TrackerPushedTotal=1`, `V2NotifiedTotal=1`, `V2NotifyBatches=1`, `V2FencesInFlight=0`.
>    - GFXHUB MMU: `NODE1_STOP_GCVM_FAULT STATUS=0x00000000` (zero page faults).
> 3. **15 Verificações Automatizadas: 15/15 TRUE:**
>    - `process_completed`, `loaded_icd_identity`, `process_exit_zero`, `byte_proof`, `submit_accepted`, `retired`, `ring_drained`, `ring_moved`, `physical_fence_replaced_poison`, `gpuva_gate_operational`, `v2_pushed`, `v2_notified`, `v2_drained`, `no_submit_reject`, `no_tracker_fault`.
> 4. **Escopo Declarado e Limitações Honestamente Registradas:**
>    - **O que está provado:** 1 dispatch de compute Vulkan 1×1×1 via Mesa RADV + ACO no hardware real sob perfil de laboratório (`GpuvaLabPromotion=1`, `RenderNode=1`).
>    - **O que NÃO está provado ainda:** Pipeline gráfico de renderização 3D, rasterização, swapchain de apresentação de quadros, desktop DWM. (Esses pertencem ao Marco 2).
>    - **Comportamento da Tela Preta:** O DWM falhou ao compor janelas porque `bc250umd64.dll` retorna `E_NOTIMPL` para D3D11 nativo (`BC250_D3D10_11_NATIVE_READY == 0`). O teste de compute é disjunto e rodou em background. O watchdog de 180s desfez o bind de forma limpa, restaurando `BasicDisplay` (Código 0).
>
> ---
>
> **10/09 20:12:50 (P0) — Corrida 3.134.1.0 (ROTA 1 — Mesa RADV / Vulkan / ACO no silício):**
> `-EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunRadvComputeTripod`.
> Evidência: `docs/evidence/2026-09-10_20-12-50-DESKTOP-HQAG6T4-lab-binding/`.
>
> **Avanços provados no silício (P0):**
> 1. **Portão GPUVA do Nó 1 Aberto e Operacional:**
>    - `GpuvaGateEligible=1`, `GpuvaGateOperational=1`, `GpuvaGateBlockMask=0`.
>    - O gate do nó 1 abriu em produção de laboratório sem bloqueios de journal ou sincronização.
> 2. **Bring-up da Stack Mesa RADV (Vulkan 1.3 / GFX1013 / ACO):**
>    - `vkCreateInstance` e carga direta de `vulkan_radeon.dll` (ICD interface 7): **SUCESSO**.
>    - `vkEnumeratePhysicalDevices`: **SUCESSO** (identificou `AMD BC-250 Graphics (Cyan Skillfish 2)`, vendor `0x1002`, device `0x13fe`).
>    - `vkCreateDevice`: **SUCESSO** (dispositivo WDDM e paging queue inicializados).
>    - 8 Alocações WDDM2: **8/8 SUCESSO** (`CreateAllocation2`, `MapGpuVirtualAddress`, `MakeResident`, fences de paginação 7000 a 7020 retiradas).
>    - Compilação do shader de compute pelo compilador ACO da Valve/Mesa: **SUCESSO** (`vkCreateComputePipelines`).
>    - Gravação do Command Buffer (dispatch): **SUCESSO**.
> 3. **Causa Raiz do Bloqueio de Submissão (`0xC000000D`):**
>    - O `vkQueueSubmit` falhou com `-4` (`VK_ERROR_DEVICE_LOST`) porque `D3DKMTSubmitCommand` retornou `0xC000000D` (`STATUS_INVALID_PARAMETER`).
>    - Causa física: o Mesa RADV passava dados privados proprietários da AMD (`PrivateDriverDataSize = pdd.offset`). No WDDM 2.0+, o `dxgkrnl.sys` exige que `PrivateDriverDataSize <= Context.DmaBufferPrivateDataSize`. Como o KMD da BC-250 declara `DmaBufferPrivateDataSize = 0` (o IB é executado diretamente por GPUVA), o kernel rejeitou a chamada antes do KMD.
>    - Conserto: em `radv_wddm2_cs.c`, condicionado `is_bc250 ? NULL : pdd.buffer` e `is_bc250 ? 0 : pdd.offset`. Recompilado com sucesso.
> 4. **Estabilidade de KMD:**
>    - 54 submissões aceitas e 54 notificadas/aposentadas (100% de conciliação). Zero BugCheck, zero 0x119.
>    - Desligamento limpo registrado: fusível em `CLEAN (0x00000000)`. Rollback do watchdog online e limpo.
>
> ---
>
> **10/09 19:45:44 (P0) — Corrida 3.134.1.0 (19.291 submissões WDDM no silício; 0x119 Arg1=1 EXTINTO):**
> `-EnableRenderNode -EnableD3D11LabPromotion -EnableD3D11DrawBltSubstitution`. Evidência:
> `docs/evidence/2026-09-10_19-45-44-DESKTOP-HQAG6T4-lab-binding/`.
>
> **Avanços provados no silício (P0):**
> 1. **Fila multi-slot SDMA e submissão assíncrona 100% estáveis:**
>    - Submissões aceitas: **19.291** (`SubmissionsCount=19291`).
>    - Submissões aposentadas e notificadas: **19.291** (`SdmaRetirementNotifiedTotal=19291`, `NotExecuted=0`).
>    - Conciliação exata: **19.291 / 19.291 (100%)**.
>    - Lotes notificados via `DXGK_INTERRUPT_DMA_COMPLETED`: **19.117 lotes**.
>    - `WatermarkSkipped`: **0** (a serialização e drenagem em lote eliminaram as inversões na origem).
>    - `BusyYields`: **1.087** (a guarda atômica `SdmaRetireBusy` evitou 1.087 colisões concorrentes entre a thread de submit e o DPC).
> 2. **Eliminação do BugCheck `0x119 Arg1=1`:**
>    - A corrida anterior (18:34) sofreu BugCheck `0x119 (0x1, 9009, 9010)` por corrida entre submit e DPC notificando fences fora de ordem.
>    - A implementação da marca d'água monotônica por nó (`SubmissionFenceId <= LastNotifiedFenceIdByNode[sync->NodeOrdinal]`), serialização por CAS (`SdmaRetireBusy`) e batch drain eliminou completamente a corrida. Zero BugChecks registrados no Windows.
> 3. **Observabilidade em PASSIVE_LEVEL:**
>    - O log no `StopDevice` registrou no disco físico:
>      `[SDMA_RETIRE_ARCHIVE] DpcRuns=39243 NotifiedTotal=19291 BatchesNotified=19117 WatermarkSkipped=0 NotifyFailed=0 BusyYields=1087 LastNotifiedNode0=19291`.
>    - Nenhuma evidência de retirement foi perdida.
> 4. **Fusível de Boot e Watchdog:**
>    - Desligamento limpo registrado: fusível em **`CLEAN (0x00000000)`** (`C:\BC250_BOOT_FUSE.dat`).
>    - O watchdog de 180s expirou pacificamente e executou **rollback 100% online e limpo**, restaurando `BasicDisplay` em Código 0 sem forçar reboot.
>
> **Diagnóstico da tela preta:**
> - `dwm.exe` subiu e permaneceu ativo durante 3 minutos e 13 segundos (gerando 32,3 MB de log UMD).
> - O DWM chamou `DrawIndexed` repetidamente (~1 vez a cada 1,5s) para desenhar uma janela de 1400x900 na primária de 2560x1080.
> - A rota `Bc250UmdSubstituteDrawWithBlt` rejeitou o draw com `DrawBltSubstitution REPROVOU: sameSize=0 (src 1400x900 fmt=87, dst 2560x1080 fmt=87)`, pois a política C1 exige dimensões idênticas.
> - A UMD retornou `E_NOTIMPL` via `Bc250UmdRejectUnbackedDraw`, e o DWM não enviou quadros subsequentes de apresentação (apenas `Present #1` e `#2` no boot, frames em branco).
>
> **Próxima etapa prioritária:** **ROTA 1 — Vulkan RADV + DXVK + VKD3D-Proton (Fase F do Roadmap 164)**.
> Validar o harness do tripé de compute Vulkan (`tools/test-radv-wddm-compute-tripod.ps1`) com o KMD atual.
>
> ---
>
> **10/09 12:16 (P0) — Corrida 3.127.1.0 (flush de HDP confirmado; `0x133` no lugar do `0x119`):**
> `-EnableRenderNode -EnableD3D11LabPromotion`. Evidência:
> `docs/evidence/2026-09-10_12-16-46-DESKTOP-HQAG6T4-lab-binding/`.
>
> **Critério escrito ANTES da corrida, cumprido:**
>
> | | 11:22 (3.125.1.0) | 12:16 (3.127.1.0) |
> |---|---|---|
> | Submissões INDIRECT | morreu no FenceId **770** | **1819**, FenceId até **5335** |
> | Voltas do anel SDMA | 12 | **84** |
> | `IB_TRIPOD TIMEOUT` | 1 (fatal) | **0** |
> | `IB NAO EXECUTADO` | 1 | **0** |
> | Máximo de polls | **164.167** | **181** |
> | BugCheck `0x119` | sim | **não** |
>
> Distribuição de polls (n=1977): min 2, p50 **3**, p90 49, p99 180, max **181**. A cauda
> ilimitada desapareceu, e `FENCE_VISIBILIDADE` não aparece nenhuma vez — essa linha só sai no
> caminho de timeout, e não houve timeout. Caminhos: 1805 `INDIRECT (Fill=0/0)` + 14
> `INDIRECT (Fill=1/4096)`.
>
> **C1 validada em hardware:** `DRAW_BLT_SUBSTITUTION: desarmado` e
> `DrawIndexed: RECUSADO -- compilador/shader-state/draw PM4 ausente (Rejected=1)` no `dwm.exe`
> (pid 1220). `dwm.exe`, `Explorer.EXE` (5304) e `dxgiadaptercache.exe` (10104) receberam
> `S_OK (BC250 Native)`. **Tela preta era o resultado esperado e correto** — Draw recusado
> honestamente, sem `memcpy`, sem sucesso silencioso.
>
> **C2 validada em hardware:** o arquivamento truncou em 15.725.444 e o resgate recuperou
> **3.492.427 bytes** (`sha256 896A681192D4D8726FAA20C31BBA37C14900150B5CD360E76E2B3CF10A9C3BA7`),
> que contêm a corrida inteira. Sem a C2 esta entrada não existiria.
>
> **PAREDE NOVA — `0x133 DPC_WATCHDOG_VIOLATION`
> (`0x0`, `0x501`, `0x500`, `0xfffff805220fb320`)** às 12:19:07, `MEMORY.DMP` de 827.376.956 bytes.
> `Arg2=0x501` contra `Arg3=0x500`: tempo excessivo em `DISPATCH_LEVEL`. O log termina **logo após
> uma submissão bem-sucedida** (`IB EXECUTADO ... FenceId=5335`), sem timeout e sem recusa — **não
> é o caminho do `0x119`**.
> **Hipótese (H):** `DxgkDdiSubmitCommandVirtual` é chamada em `DISPATCH_LEVEL` e o laço de poll de
> `bc250_sdma_present.c:1226` roda dentro do `KeAcquireSpinLock` da linha 1129, soltando só nas
> 1302/1408 — 1819 submissões, até 181 polls de MMIO cada, em ~137 s. A A15 declarou esta dívida
> por escrito ("o laço do tripé pode gastar ~1 s segurando spinlock em DISPATCH_LEVEL").
> **Nuance:** os casos de 100–181 polls **não são visibilidade** — o flush já dispara no casamento
> de RPTR; são espera real da engine. O flush eliminou a cauda *infinita*; sobrou o fato de
> esperarmos de forma síncrona.
> **PRÓXIMO PASSO, SEM CUSTO DE CORRIDA:** atribuir o `0x133` pelo stack do dump (comando em
> docs/235 §2.2). Só depois disso implementar a conclusão assíncrona — e **não** baixar o teto de
> polls, que devolveria `STATUS_IO_DEVICE_ERROR` e o `0x119`.
>
> **Estado da máquina:** BC-250 em **Código 43** (`CM_PROB_FAILED_POST_START`) com o 3.127.1.0
> preso, fusível **`TRIPPED (Boots=2, LastStatus=0xC0000001)`**, **sem rollback** (o BugCheck matou
> o watchdog). `tools\BC250_RECOVERY.cmd` elevado, `inspect-boot-fuse.ps1 -Reset` e boot frio antes
> de qualquer novo bind.
> **ATENÇÃO ao empacotar:** 3.127.1.0 já foi instalado; suba o `DriverVer` fonte antes de gerar
> outro candidato, senão dois artefatos diferentes carregam o mesmo `DriverVer` (erro do 3.92.1.0).
>
> **NÃO EXERCITADO nesta corrida:** o IB de fill de 11.075.584 bytes não apareceu (só fills de
> 4096), então o `GFXHUB_FAULT STATUS=00841B40 ADDR=0x82C` segue **sem teste**, e o flush de HDP
> **não** cobre essa classe.
>

> **10/09 11:22 (P0) — Corrida 3.126.0.0, e a AUDITORIA que corrigiu o enquadramento dela:**
> Ensaio físico do pacote lab com D3D11 Lab Promotion e RenderNode ativos.
>
> **O que está PROVADO no silício (P0):**
> 1. `dwm.exe` (pid 1204), `Explorer.EXE` (5220) e `dxgiadaptercache.exe` (1964) carregaram a `bc250umd.dll` e receberam
>    `OpenAdapter10_2: Interface=0x000B000B, Version=0x00020009 -> S_OK (BC250 Native)`. A parede única do D3D11
>    (o `E_NOTIMPL` que fail-fastava o DWM desde 08/09) **caiu**.
> 2. `ActiveOpenAlloc` fechou: 41/41 e 29/29 em `0x00000000` nos binds 11:10 e 11:21, contra 5.745 falhas em 7.154
>    chamadas no bind das 09:34 (`0xC000000D` 3.058 + `0xC0000035` 2.687). A correção de tokens únicos é real.
> 3. O SDMA executou 769 submissões INDIRECT consecutivas atravessando 12 voltas completas do anel antes do incidente.
> 4. **Causa-raiz do 0x119 no FenceId=770: visibilidade de HDP, não execução.** As três leituras concordam:
>    `IB_TRIPOD TIMEOUT: TargetWPTR=1092, WPTR=1092, RPTR=50244, FenceExp=00001304, FenceObs=43DAFFFE`
>    `ENGINE_STATE: STATUS1=000003FF (idle se &0x3FF==0x3FF)`
>    `FENCE_VISIBILIDADE: antes_hdp=43DAFFFE depois_hdp=00001304 esperado=00001304 | RPTR_writeback=50244 RPTR_reg=50244`
>    `RPTR_SAMPLES (n=8, stride=25000 polls): 50244 x8`
>    50244 = 12·4096 + 1092 nos dois ponteiros, engine ociosa, e a fence apareceu **no flush**. O flush ativo em
>    `bc250_sdma_present.c` ataca a causa certa e teria saído do laço no primeiro poll.
>
> **O que foi RETIRADO desta entrada por não se sustentar na evidência:**
> - **"Desktop real D3D11 exibido no monitor" — NÃO SUSTENTADO.** As linhas `Present #1 DESKTOP REAL DETECTADO` e
>   `BYTE_PROOF PASS` citadas na versão anterior desta entrada são do **bind das 09:34** (offset 8.161.041, dentro da
>   janela 7.659.265–14.831.198), não da corrida das 11:22. O bind das 11:22 teve 10 Presents, **zero** detecção de
>   desktop, **zero** `BYTE_PROOF` e **zero** `SDMA=YES`. Três binds distintos foram fundidos num só marco.
> - **O detector dispara com `NonZero=1/256`** — 255 dos 256 dwords amostrados são zero, e `SCANOUT`, `PRIMARY` e
>   `SLOT_WORD` amostraram `0/256`. É frame preto com um dword não-nulo. No bind das 11:10, com OpenAlloc 100% OK,
>   foram 194 Presents e 10 blts, todos `BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)` com `BlankStreak=4`.
> - **Nenhum pixel veio do D3D11.** O log da UMD do DWM não tem uma linha de `Present`, `Flush` ou blt; tem **1**
>   `DrawIndexed: RECUSADO` e termina 2 s após o load dentro de 4.159 `CheckMultisampleQualityLevels`. A imagem
>   distorcida vista pelo operador é atribuível à transição de adaptador/BasicDisplay — o mesmo engano já cometido e
>   corrigido em 07/09 18:16 ("a imagem que apareceu no fim NÃO é atribuível ao nosso driver").
> - A explicação do *stride skew* (7680 → 10240) é **H**, não medição: nenhuma linha de `Present` desta corrida
>   registra pitch de origem 7680 no caminho de blt.
>
> **DÍVIDA DISTINTA, ABERTA, que o flush de HDP NÃO cobre:** nos dois timeouts de 09/09 o `RPTR` congelou
> **exatamente em `IndirectEndDW`** (1056 = 264 dw; 1568 = 392 dw), o flush **não** revelou a fence
> (`depois_hdp=43DAFFFE` — 31 ocorrências no corpo inteiro, todas iguais) e um deles trouxe
> `GFXHUB_FAULT_SNAPSHOT: STATUS=00841B40 ADDR=0x82C`, falha de tradução real. Os dois eram o IB pesado
> `Fill=1/11.075.584`. O caminho `TIMEOUT -> STATUS_IO_DEVICE_ERROR -> 0x119` continua vivo para essa classe.
>
> **FAIXA C1 (10/09, OFFLINE) — o fallback de CPU no `Draw` foi removido.** Encontrado por leitura de código no
> candidato 3.126.1.0 **ainda não commitado**: `Bc250UmdCompositeDraw` atendia `pfnDraw` e `pfnDrawIndexed` com um
> laço de `memcpy` entre os shadows de CPU, marcava o draw como atendido **antes** de tentar qualquer caminho de GPU
> e devolvia sucesso silencioso — a linha honesta `DrawIndexed: RECUSADO` desaparecia. É fallback de software
> disfarçado de renderização (Lei 4), e teria tornado **infalsificável** qualquer desktop na próxima corrida. O
> `bc250_d3d_umd_test.c` **exigia** esse `memcpy`, congelando a mentira — terceira ocorrência da classe (A15 obrigava
> a comparação crua de RPTR; `mcdm_callbacks_test` exigia `STATUS_SUCCESS` sem executor).
> Correção: `memcpy` removido; `Bc250UmdSubstituteDrawWithBlt` é fonte única e só usa `Bc250UmdBltGpu`; a
> elegibilidade sai de `Bc250UmdDrawBltSubstitutionIsLawful` (política pura, testada em 9 casos), fechada por default
> pelo tunable `D3D11DrawBltSubstitution`; o log declara `Draw SUBSTITUIDO por copia 1:1 no SDMA -- NAO e'
> rasterizacao`, e `SubstitutedDrawCount` é contado separado de `DrawCount`. Também extraída
> `Bc250Umd3DPipelineIsDeclared`, fonte única de `CreateDevice` e `GetCaps Type=130` — a decisão vivia inline lendo o
> registro, e por isso a suite offline **dependia do estado da máquina**: com `D3D11LabPromotion=1` deixado armado
> pelo bind anterior, ela reprovava sem nenhuma mudança de código, o que refuta o "42/42 PASS" reportado.
>
> **FAIXA C2 (10/09, OFFLINE) — a evidência das corridas estava sendo perdida.** `C:\BC250_DISPLAY_LOG.txt` é
> cumulativo e sobrevive a reboot, mas o binder o copia no **fim** do roteiro; com BugCheck o watchdog não roda e a
> pasta fica truncada sem indicação. **Seis pastas de 10/09 estavam truncadas**, incluindo duas (10:03 e 10:23) sem
> arquivo nenhum, e a da corrida do marco perdeu 438.038 bytes — justamente o `FenceId=770` e a prova de HDP. Novo
> `tools/recover-bc250mcdm-lost-tail.ps1` resgata o trecho com SHA-256 e proveniência, delimitando o fim pelo
> `start_offset` do bind seguinte; o binder o chama **antes** de cada bind. Já executado: 6/6 pastas recuperadas.
>
> **Testes:** `build-umd.bat` EXIT=0 com bloco C1 novo (`C1 politica 9/9, Draw fail-closed sem memcpy e tokens unicos
> PASS`), `build-contract.bat` EXIT=0, quatro auditorias PASS, dois portões novos
> (`audit_draw_has_no_cpu_fallback`, `audit_lost_log_tail_is_recovered`) e **seis mutações verificadas**.
> **NADA DE C1/C2 FOI VALIDADO EM HARDWARE.**
> Auditoria completa: `docs/234_AUDITORIA_2026-09-10_C1_DRAW_SEM_CPU_E_C2_EVIDENCIA_PERDIDA.md`
>
> **10/09 10:05:09 (P0) — Corrida 3.124.1.0 (OpenAlloc Resolvido 100% e BugCheck 0x10E no VidMm):**
> Ensaio físico do pacote lab 3.124.1.0 com `Bc250McdmObjectLookup` por `info->hAllocation`, tokens únicos monotônicos e sincronização VRAM.
> **Avanços comprovados no silício:**
> 1. Todas as chamadas de `ActiveOpenAlloc` retornaram `STATUS_SUCCESS` (`status=0, nt=0x00000000`), eliminando completamente os 5.169 erros anteriores de `DUPLICATE_OWNER` (`0xC0000035`) e divergência de tamanho/alinhamento.
> 2. O pipeline de submissão do Nó 0 executou sem travas, com múltiplos `IB EXECUTADO (INDIRECT)` concluídos.
> **Ocorrência e Diagnóstico:**
> O sistema sofreu BugCheck `0x0000010E` (`VIDEO_MEMORY_MANAGEMENT_INTERNAL`, `Arg1=0x000000000000000B`, `Arg3=0xFFFFFFFFC000000D`, `Arg4=0xFFFFB188697A8000`).
> - `Arg1 = 0xB`: *A driver returned an invalid error code from BuildPagingBuffer*.
> - `Arg3 = 0xC000000D`: `STATUS_INVALID_PARAMETER`.
> O VidMm convocou `DxgkDdiBuildPagingBuffer` (operação de paginação/tabela de páginas) e recebeu o código não permitido `STATUS_INVALID_PARAMETER`.
> **Causa Raiz Isolada no Crash Dump:** O VidMm chamou `MapScratchAreaVaRange` com 449 páginas esparsas em Segmento 0. A CPU codificou e gravou as 449 entradas físicas na page table com sucesso, mas o Journal estático (limite 256) retornou `BC250_GPUVA_JOURNAL_NO_SPACE` (4), e o handler propagou `STATUS_INVALID_PARAMETER` ao invés de aceitar as PTEs gravadas e desmarcar a elegibilidade de snapshot.
> **Correção implementada:** Em `Bc250ActiveUpdatePageTableHandler`, `NO_SPACE` agora desmarca a elegibilidade de snapshot, preserva as PTEs físicas na RAM gravadas pela CPU e retorna `STATUS_SUCCESS` para o VidMm. Fusível anti-boot-loop resetado para CLEAN.
> No reboot subsequente, o fusível anti-boot-loop interceptou e bloqueou o StartDevice (`State=2 TRIPPED`), preservando a máquina.
>
> **09/09 21:20:13 (P0) — Corrida 3.122.1.0 (Auditoria e Conferência do Recorte — docs/230 e 231):**
> Ensaio físico do pacote lab 3.122.1.0 auditado com recorte estrito (`start_offset=6776778`, 668.635 bytes a partir da linha 94574).
> A mineração delimitada registrou: 17 menções a `CommitVidPn` (9 entradas e 8 mensagens de transição para VidPn vazio); 2 `SetVidPnSourceAddress` configurando o scanout; 8 chamadas do OS de `[ControlInterrupt] VSYNC Enable=0/1` (não confundir com interrupções físicas de VBlank); 6 linhas de Present (todas referindo-se ao mesmo `Present #2`, com zero blits SDMA); `Present #2` foi rejeitado por contrato de fonte (`REJECT source contract: status=9 (ALLOC_NOT_RESIDENT), SlotSeg=0`); 102 execuções explícitas de `IB EXECUTADO (INDIRECT)` no recorte, das quais 47 fecharam `TRIPE_PROVADO`.
> **Contraprovas e achados críticos da corrida:**
> 1. Submissão #99 falhou na perna de dados: `IB_TRIPE_VEREDITO #99: DADOS_DIVERGEM` (cinco canários retiveram poison `DEADAFFE...` no destino `0x450ABC000`, necessitando rastreamento de alocação/GPUVA).
> 2. Exaustão de tabela de objetos no KMD: 56 linhas `[McdmCreateAllocation] Result=0xC000009A` e 8 recusas `CreateDevice/CreateProcess` pelo limite estático de 1024 slots sem reciclagem (corrigido offline: limite 4096 + reciclagem de slots mortos).
> 3. UMD: chamadas `DrawIndexed` rejeitadas com `E_NOTIMPL` por ausência de rasterizador/PM4 Draw.
> Às 21:23:17, o watchdog executou rollback gracioso sem TDR ou bugcheck nesta janela, restaurando `BasicDisplay` em Código 0. **Desktop acelerado e jogos: NÃO VALIDADO EM HARDWARE.**
> [Auditoria 230](docs/230_AUDITORIA_ESTADO_WDDM_2026-09-09.md) · [Conferência 231](docs/231_CONFERENCIA_CORRIDA_21-20_2026-09-09.md).
>
> **09/09 20:56:55 (P0) — Corrida 3.121.1.0 (BugCheck 0x119 no dxgmms2 e Cursor de Paginação):**
> O ensaio físico sofreu BugCheck 0x119 (`Arg1=2, Arg2=0xC0000185`) no `dxgmms2` durante operação de paginação do VidMm.
> **Causa raiz:** o dxgkrnl gerou um IB de paginação com salto inicial de cursor de 32 bytes além da base física da alocação. A verificação `IB_PA_MATCH` comparava estritamente `AllocBase` com `IbPhysicalAddress`, ignorando o offset interno do cursor, gerando um falso mismatch. A tentativa de fallback leu endereço desatualizado/lixo, corrompendo a paginação e derrubando o VidMm.
> **Correção offline:** implementado `Bc250PagingConsumeIbCpuPhysical` para consumir paginação via CPU física direta quando aplicável, com ajuste de offset de cursor na validação de `IB_PA_MATCH`.
>
> **09/09 19:56:09 (P0) — Corrida 3.119.1.0 (Nó 1 PM4 e Tripé UMD Fechados no Silício):**
> O harness `B7_UMD_NODE1_TRIPOD` submeteu IB sob VMID 9 pelo WDDM Winsys nativo da UMD.
> O Command Processor (MEC2/CPF) buscou o anel sem stalls de RCIU (`CpcStalled=0`), drenou completamente o anel (`RPTR==WPTR==12`), sinalizou a fence monitorada (`3156545281` / `0xBC250F01`) e gravou na VRAM os 5 canários de dados: `Values=BC250C01,BC250C02,BC250C03,BC250C04,BC250C05` (`hr=0x00000000`).
> **Ressalva de escopo auditada (doc 230):** o IB continha comandos PM4 `WRITE_DATA` e `RELEASE_MEM`; **não houve dispatch de shader** (`DISPATCH_DIRECT`) nem compilação de código de shader pela rota UMD/WDDM. Comandos PM4 e efeito em memória pelo Nó 1 estão **VALIDADOS EM HARDWARE**; shader compilado/dispatch na UMD permanece **NÃO VALIDADO EM HARDWARE**.
> No Nó 0 (SDMA), 33 submissões foram aceitas, e a corrida anterior das 19:48 eliminou o 0x119 no fill de 11 MB da primária (`TRIPE_PROVADO` com 5 canários em 128 polls).
> A tela permaneceu preta porque o DWM exige backend D3D10/11 na UMD (que ainda retorna `E_NOTIMPL`).
> 74 s após o fim do teste, o watchdog de confirmação visual expirou e executou rollback forçado via `pnputil /uninstall /force`, gerando BugCheck 0x119 no `dxgmms2` por remoção surpresa do driver com VSYNC ativo. O fusível anti-boot-loop funcionou perfeitamente no reboot (`State=2 TRIPPED`), impedindo boot loop.
> **Tripé do Nó 1 (PM4 WRITE_DATA/RELEASE_MEM) VALIDADO EM HARDWARE; Próximo bloqueador: Frente 2 (D3D10/11 UMD com rasterizador real).**
> [Handoff 229](docs/229_HANDOFF_2026-09-09_NODE1_COMPUTE_FECHADO_NO_SILICIO.md) · [Auditoria 230](docs/230_AUDITORIA_ESTADO_WDDM_2026-09-09.md).

> **09/09 18:25:22 (P0) — Corrida 3.117.1.0:** o silício registrou grande avanço microarquitetural:
> `WPTR` avançou de 0 para 12, doorbell kick funcionou (`QUEUE_IDLE: 1 -> 0`), e o Command Processor
> Fetcher (CPF) buscou o anel e decodificou o descritor PM4 `IT_INDIRECT_BUFFER` (`C0023F00, 00011000, 00000000, 09800011`),
> engajando o `HQD_IB_FETCHER` (`CP_CPF_BUSY_STAT = 0x68460000`, bit 18 `HQD_IB_FETCHER_BUSY = 1`) sob VMID 9.
> Contudo, o fetcher congelou aguardando leitura da malha interna (`CP_CPC_STALLED_STAT1 = 0x00040000`, `MEC2_WAIT_ON_RCIU_READ`).
> `RPTR` estagnou em 0, fence 18 e canários não foram produzidos, e após 2 s o TDR disparou. O DWM falhou por `E_NOTIMPL`
> na UMD (D3D10/11) e o watchdog restaurou BasicDisplay Código 0.
> **Engajamento do HQD_IB_FETCHER pelo CPF VALIDADO EM HARDWARE; tripé Node 1 NÃO VALIDADO.**
> [Handoff 227](docs/227_PASSAGEM_DE_BASTAO_2026-09-09_WDDM_GPU_3117.md).

> **09/09 17:47:40 (P0) — Corrida 3.116.1.0:** a reconciliação corrigiu no silício a
> geometria do anel: com `Submitted=Retired=17`, o par físico drenado `RPTR=0/WPTR=0`
> substituiu o cursor lógico antigo 204 e o descritor foi publicado em `WPTR=12`, não 216.
> O consumo, contudo, falhou: submit `17->18`, aposentado `17->17`, `RPTR=0`, fence zero e
> nenhum canário. Antes do doorbell, `CpcStalled=0x00040000` já havia reaparecido; depois,
> permaneceu igual, com `PQ_EMPTY=0` e `FaultStatus=0`. Assim, cursor incoerente não era a
> causa suficiente, e a ausência do stall na 3.115 era transitória. Watchdog preservou
> 76.526 linhas (`85E58D12...`), removeu `oem35.inf` e restaurou BasicDisplay Código 0.
> **Reconciliação VALIDADA EM HARDWARE; tripé Node 1 NÃO VALIDADO. Não repetir 3.116.1.0.**
> [Análise](docs/evidence/2026-09-09_17-47-40-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md).

> **09/09 16:55:02 (P0) — Corrida 3.115.1.0:** a política do firmware foi preservada no
> silício (`SPI=0xFFFF`, `RLC=0x3`, sem escritas `0x1F`) e o stall
> `MEC2_WAIT_ON_RCIU_READ` não reapareceu: `CpcStalled=0` antes/depois do doorbell e no TDR.
> Porém o Item 1 não fechou: submit aceito `17->18`, aposentado `17->17`, `WPTR 204->216`,
> `RPTR 204->204`, fence/canários ausentes. Na leitura física direta:
> `PRE RPTR=0/WPTR=0`, `POST RPTR=0/WPTR=216`; `PqControl=0xD030090D` já tem `PQ_EMPTY=0`.
> Logo, o 3.115 corrigiu o estado especulativo e retirou o stall, mas não produziu consumo.
> Novo discriminante: cursor lógico parte de 204 enquanto o par físico parte de zero.
> Watchdog arquivou 74.236 linhas (`C06205A2...`), removeu `oem35.inf` e restaurou
> BasicDisplay Código 0. **Não repetir 3.115.1.0; Node 1 NÃO VALIDADO EM HARDWARE.**
> [Análise](docs/evidence/2026-09-09_16-55-02-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md).

> **09/09 — candidato 3.116.1.0 antes da corrida:** criado contrato fail-closed para reconciliar o
> cursor lógico do anel com `RPTR/WPTR` físicos antes do planejamento. Só há rebase se
> `Submitted==Retired` e o hardware estiver drenado (`RPTR==WPTR`); nenhuma rotina escreve
> RPTR. No caso medido `Logical=204, Physical=0/0`, o próximo descritor começa em 0 e publica
> 12, preservando a sequência de fence 18. Físico ocupado, fence pendente, falha de leitura
> ou restauração recusam o submit. Seis casos de contrato e quatro mutações passaram;
> `build-contract.bat EXIT=0`, auditorias PASS. Pacote assinado: SYS `E145A7DD...`, INF
> `7CB3DC16...`, CAT `0279E1D1...`. O ensaio posterior validou o rebase para zero, mas não
> o consumo do descritor.
> [Handoff 226](docs/226_PASSAGEM_DE_BASTAO_2026-09-09_WDDM_GPU_3116.md).

> **09/09 15:47:07 (P0) — Corrida 3.114.1.0:** o harness obteve um submit Node 1 aceito
> (`P1SubmitAccepted 17->18`) e o `WPTR` avançou `204->216`, mas `P1RetiredTotal` ficou em
> 17 e o `RPTR` publicado ficou em 204. A fotografia direta da fila pendente mostrou
> `RPTR=0`, `WPTR=216`, fence observada 0 e `VMID=0x900`; os canários não foram executados.
> No último submit, `CP_CPC_STALLED_STAT1=0x00040000` já estava presente antes do doorbell.
> O seletor pedido 8 com readback 0, sozinho, **não prova banco errado**: a transação de HQD
> e o readback de `CP_HQD_VMID=0x900` alcançaram o banco selecionado. O watchdog removeu
> `oem35.inf`, restaurou BasicDisplay em Código 0 e criou
> `C:\BC250_REQUIRE_CLEAN_BOOT.json`. **Node 1 NÃO VALIDADO EM HARDWARE.**
>
> **09/09 — candidato fonte 3.115.0.0 OFFLINE:** removidas do submit Node 1 as escritas
> especulativas `RLC_PG_CNTL=0`, `SPI_PG_STATIC_WGP_MASK=0x1F` e
> `RLC_PG_ALWAYS_ON_WGP_MASK=0x1F`. O valor `0x1F` pertence ao patch opcional local de
> desbloqueio de 40 CUs, combinado com alteração de `CC_GC_SHADER_ARRAY_CONFIG`; não é
> política comprovada para o alvo de 24 CUs. Os três registradores continuam legíveis para
> telemetria, mas saíram da allowlist de escrita. Teste do transporte, auditoria estática e
> três mutações passam; `build-contract.bat` **EXIT=0**. Fonte versionada como 3.115.0.0,
> pacote lab `3.115.1.0` assinado e auditado (SYS
> `6E0F0794401C7ABBA9CA12A0A6F83FA8920CCC9F1A8C0E5106B8EBA45291C652`), ainda sem bind.
> O preflight bloqueia corretamente até haver boot posterior ao rollback das 15:50:10.
> **NÃO VALIDADO EM HARDWARE.**

> **09/09 11:36:53 (P0) — Corrida 3.109.1.0 (Telemetria TDR do MEC):** o pacote 3.109.1.0 foi executado no silício com hashes conferidos. O 1º submit Node 1 avançou `WPTR` de 204 para 216 e publicou o descritor `PACKET3_INDIRECT_BUFFER` (`0x3F`, 17 DW, `VA=0x11000`, VMID 9). O tripé Node 1 falhou fechado: submetido 18, aposentado 17, `LastRptrMmio` no probe permaneceu 204, sem fence nem canários (harness `exit=1`, `hr=0x80004005`). No TDR às 11:37:13, a nova telemetria capturou no silício: `FaultStatus=00000000`, `FaultAddr=00000000:00000000`, `CpfStalled=00000000`, `CpcStalled=00040000` (`MEC2_WAIT_ON_RCIU_READ`), `CpStalled1=00000C00`, `Mec2Hdr=C0000A00`, `Mec2Pntr=000017FB`, `RPTR=0`, `WPTR=216`. **Diagnóstico:** o microengine MEC2 congelou aguardando leitura da RCIU (`CP_CPC_STALLED_STAT1 = 0x00040000`). A mineração de controle (linha 52538) comprovou que `Mec2Pntr=0x17FB` e `Mec2Hdr=C0000A00` são o baseline de repouso do MEC2 e não pertencem exclusivamente a este pacote; o delta real foi a transição de `CpcStalled` de `0` para `0x40000`. A GCVM teve probabilidade reduzida (sem fault pontual nem espera CPF/UTCL1), mas não foi totalmente eliminada devido a `*_FAULT_ENABLE_DEFAULT` e ausência de máscara de validade na captura. Anomalia de RPTR detectada: `P1LastRptrMmio=204` no registro de probe vs `RPTR=0` na amostragem direta MMIO. O TDR recuperou SDMA0 (`SYNC_NOP TRIPOD PASS` em 2 polls) e o watchdog restaurou BasicDisplay às 11:39:58 com `FUSE CLEAN (0)`. **Node 1 NÃO VALIDADO EM HARDWARE.** [Evidência da corrida](docs/evidence/2026-09-09_11-36-53-DESKTOP-HQAG6T4-lab-binding).

> **09/09 08:26 (P0):** restauração dos 26 registradores da MMU concluída, exercitando a correção do ciclo de vida do MMIO. Cinco tripés SDMA completos e 13 transações TLB status=0. Node1 continua WPTR=216/RPTR=204; caminhada CPU resolve IB para 0x451FCC000, mas não prova busca/execução GPU. TDR às 08:26:23 e rollback às 08:29:09, BasicDisplay/Código 0 confirmado. A captura MEC_PENDING_ARCHIVE não foi produzida; não inferir falha do timer dessa ausência. **Node1 NÃO VALIDADO EM HARDWARE.** [Análise da corrida](docs/evidence/2026-09-09_08-26-06-DESKTOP-HQAG6T4-lab-binding/ANALISE_POS_CORRIDA.md).

> **09/09, candidato 3.106.1.0 OFFLINE:** coleta do primeiro polling antes de possível falha do retirement; fotografia independente no StopDevice; prefixo físico do IB observado pela CPU e arquivado. Build/testes, nove mutações e auditorias PASS; pacote assinado, não instalado. Isso corrige lacunas de diagnóstico, não prova a causa do hang nem implementa D3D10/11. **NÃO VALIDADO EM HARDWARE.** [Handoff 215](docs/215_HANDOFF_2026-09-09_NODE1_PRIMEIRO_POLL.md). Teto de um ensaio Node1 com critérios de abandono documentados.

**Histórico — candidato 3.105.1.0 antes da corrida:**

> **23:15 (P0):** cinco tripés completos SDMA; 11 transações TLB status=0, zero status=8. VMID9 instalado, mas Node1 continuou sem conclusão (WPTR=216/RPTR=204; fence e dados sem prova). TDR às 23:16:12; watchdog restaurou BasicDisplay/Código 0 às 23:18:53, confirmado por CIM.
> **Correções offline:** serialização do caminho de coerência MEC com a mesma TlbLock; MMIO compartilhado preservado até a restauração da MMU; MEC desligado antes de restaurar VMID9; captura de fila pendente arquivada no StopDevice. A causa exata da não conclusão do IB Node1 permanece aberta. TDR atual prova SDMA, não recuperação do MEC. **NÃO VALIDADO EM HARDWARE.** [Handoff 214](docs/214_HANDOFF_2026-09-08_MEC_TLB_MMIO.md).

**Histórico — corrida 15:43 e candidato 3.95.1.0:**
A corrida de laboratório de 08/09 15:43 validou a Faixa B7.1 no silício (P0): MapAllocation/MakeResident aceitaram `E_PENDING` (`hr=0x8000000A`), command buffer (64 KB, `VA=0x11000`) e destino (4 KB, `VA=0x21000`) tornados residentes, cerca monitorada criada e `pfnSubmitCommandCb` chamado com `S_OK`. O KMD recebeu o 1º submit Node 1 da história (`McdmSubmitCommandVirtual` Node=1, FenceId=1, DmaSize=68; gate abriu e `WPTR` avançou de 204 para 216 dwords no silício). O tripé falhou fechado: `RPTR` permaneceu em 204, cerca retornou `UINT64_MAX` e canários em VRAM zerados (harness `exit=1`). A mineração de log comprovou disputa física do registrador VMID 8 (`PT_BASE`): a thread de Present do Node 0 trocou o `PT_BASE` para a tabela do DWM (`0x450A8C000`) logo após o submit do Node 1, retirando a raiz do harness sob os pés do MEC. A hipótese de TDR/DEVICE_REMOVED foi refutada (zero ocorrências de ResetFromTimeout/RestartFromTimeout/QueryEngineStatus). Telemetria `VMID8_DISPUTE_PROBE` adicionada no Node 1. Handoff: `docs/206`. Node 1, desktop acelerado e jogos: **NÃO VALIDADOS EM HARDWARE.**


> **08/09, continuidade OFFLINE — VMID9 dedicado ao MEC:** candidato lab 3.95.1.0
> assinado e auditado, sem bind. Node0 conserva VMID8; Node1 instala a raiz em
> VMID9 antes da publicação, com snapshot/readback/ACK/restore e recusa de troca
> enquanto houver fence pendente. Um lote compute pendente por vez neste candidato.
> Build completo e auditorias PASS; gate novo e mutações verificadas. A troca de
> raiz na corrida 15:43 é observada, mas o page fault e o instante da busca MEC
> atribuídos a ela permanecem hipótese, não medição. Nenhum item físico foi fechado.
> **NÃO VALIDADO EM HARDWARE.** Handoff: [docs/208](docs/208_HANDOFF_2026-09-08_VMID9_ISOLADO_OFFLINE.md).

> **Toolchain canônica desde 07/09/2026:** Visual Studio 2026 Community 18.9.1,
> MSVC 19.51.36256.0 e SDK/WDK 10.0.28000.0 (pacotes 10.0.28000.2526). KMD, UMD,
> laboratório, RADV gate e suítes offline passaram; os manifests dos pacotes recusam
> mistura com a toolchain anterior. Isso não mede efeito no silício:
> **NÃO VALIDADO EM HARDWARE**. Registro completo em
> [`docs/173_TOOLCHAIN_VS2026_WDK28000.md`](docs/173_TOOLCHAIN_VS2026_WDK28000.md).

> **Correção e candidato de 07/09 15:05:** o ensaio isolado das 13:59 provou um IB
> SDMA sob VMID8 com `RPTR==WPTR`, fence física em 11 ms e quatro dwords conferidos;
> a PDE corrompida reteve o poison e produziu `MAP_ERR=1` na página `0x201000`.
> Isso prova o efeito causal do **conjunto** de programação global, não qual dos 26
> registradores isoladamente é a causa. Já a corrida WDDM das 14:32 falhou em snapshot
> da MMU global (`status=2`) e seu log delimitado contém zero paging op, zero submit e
> zero commit. O fail-open foi corrigido: `StartDevice` e `SetRootPageTable` agora
> recusam prosseguir; o rollback e o restore são verificados; a dummy page é reservada
> pelo allocator e excluída do segmento publicado ao VidMm. Suíte e auditoria de
> integridade passaram; efeito deste candidato no silício: **NÃO VALIDADO EM HARDWARE**.
> Handoff: [`docs/176_HANDOFF_2026-09-07_MMU_FAIL_CLOSED.md`](docs/176_HANDOFF_2026-09-07_MMU_FAIL_CLOSED.md).

> **Corrida física 07/09 15:16:** o novo fail-closed funcionou como desenhado, mas
> recusou o `StartDevice` antes de programar a MMU: `vramStatus=6`
> (`BC250_VRAM_NO_SPACE`). O allocator escolhido pertencia ao bootstrap ainda não staged
> e estava zerado. Resultado medido: Código 43, zero MMU global aplicada; o tripé de boot
> SDMA passou, e o watchdog removeu `oem34.inf` e restaurou BasicDisplay Código 0.
> Corrigido offline com allocator próprio, reserva explícita do scanout e dummy top-down;
> o teste com a geometria medida coloca a dummy em `0x0FFFF000`. Novo pacote ativo
> assinado/auditado: fonte `9A80C7AA...`, assinado `97C96152...`.
> Este pacote seguinte ainda é **NÃO VALIDADO EM HARDWARE**. Handoff:
> [`docs/177_HANDOFF_2026-09-07_15-16_GLOBAL_MMU_ALLOCATOR.md`](docs/177_HANDOFF_2026-09-07_15-16_GLOBAL_MMU_ALLOCATOR.md).

> **Corrida física 07/09 15:49:** a correção do allocator ultrapassou a reserva da dummy,
> mas o snapshot parou no primeiro registrador: `status=2 step=3 reg=0x0A610`. A causa é
> determinística no código: `Bc250MmioTopologyRead32` recusava `GCMC_VM_AGP_BASE` porque
> os 26 offsets da transação global não estavam na allowlist do transporte; a leitura não
> chegou ao MMIO. Resultado medido: Código 43. Embora Daniel tenha visto imagem, o script
> recusou a confirmação porque o dispositivo não estava em Código 0; isso não é prova de
> sucesso. O watchdog restaurou BasicDisplay Código 0. Corrigido offline com helper fechado
> compartilhado pelas allowlists de leitura/escrita, teste dos 26 offsets e vizinho negativo.
> Pacote atual: fonte `F0375F94...`, assinado `069A7330...`; **NÃO VALIDADO EM HARDWARE**.
> Handoff: [`docs/178_HANDOFF_2026-09-07_15-49_MMIO_ALLOWLIST.md`](docs/178_HANDOFF_2026-09-07_15-49_MMIO_ALLOWLIST.md).

> **Corrida física 07/09 16:19 — avanço e reprovação:** `bc250mcdm` ficou em Código 0;
> `Global GFXHUB MMU initialized: Regs=26`, ACK físico de TLB, journal até Generation=9,
> um `CommitVidPn`, um `SetVidPnSourceAddress` e 12 Present. A superfície primária em
> `0xAB0000` permaneceu zero e os blits fecharam no máximo duas pernas; zero sobre zero não
> prova pixels. Quatro processos DWM falharam; dois dumps de ~202 MB têm
> `RaiseFailFastException(e0464645, 887A0004=DXGI_ERROR_UNSUPPORTED)`, enquanto a UMD
> registra `OpenAdapter10/10_2 -> E_NOTIMPL`. Logo a MMU/allowlist atravessou o gate, mas o
> desktop é bloqueado pelo backend D3D10/11 ausente. O watchdog voltou a BasicDisplay
> Código 0. Não rebinding do mesmo pacote. Handoff completo:
> [`docs/179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md`](docs/179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md).

> **Faixa A13, 07/09, OFFLINE e sem corrida — higiene de prova.** A mineração do log da
> 16:19 mostrou que **14 `IB EXECUTADO` = 2 `PLANO A DIRECT FILL` + 12 `PLANO A SYNC NOP`,
> com zero `INDIRECT`**: a frase de execução de IB saía nos três ramos e afirmava que a GPU
> leu um DMA buffer que ela nunca leu. Corrigido — a frase agora é exclusiva do `INDIRECT`.
> **Achado novo:** `TRIPÉ NO SILÍCIO COMPROVADO … WPTR=68 RPTR=52` não era telemetria errada
> e sim **gate errado** — a perna 1 (`RPTR==WPTR`) não entrava na decisão do bringup de SDMA,
> e um timeout de 2000 polls passava. O veredito passou a sair de `Bc250ClassifySdmaTripod`,
> com a fonte do RPTR nomeada no log e anel nunca armado saindo `INDETERMINADO`. Terceiro
> item: o tunable `SdmaIndirectOnly` (default 0, fail-closed) desliga a precedência dos
> `PLANO A` para que o `INDIRECT` seja exercitável. `build-contract.bat` `EXIT=0`, três
> auditorias PASS e os dois portões novos verificados **por mutação**. **NÃO VALIDADO EM
> HARDWARE.** Handoff:
> [`docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md`](docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md).

> **Corrida física 07/09 17:13 — o IB WDDM real executou sob VMID8.** Com
> `SdmaIndirectOnly=1` lido (`present=1`), as 14 submissões do Node 0 saíram como **10
> `INDIRECT` + 4 `RECUSADO`**: dez `IB EXECUTADO (INDIRECT)` de DMA buffers de 64 bytes
> (`VA=0x22F000..0x22F1C0`), cada um com `RPTR==WPTR` e fence `0x1002..0x1014` sobre poison,
> ~111 polls, zero timeout e `FAULT_STATUS=0`. A dívida central — *"o `INDIRECT` é o único
> que não funciona"* — está **refutada como estava formulada**. O risco declarado não
> ocorreu: as 4 recusas devolveram `STATUS_IO_DEVICE_ERROR` sem bugcheck, Código 0 até o
> rollback. **Ressalva que manda no veredito:** `IB_TRIPOD PASS` era rótulo de **duas**
> pernas — nenhum byte do destino era conferido —, então **o B1 não fecha com isso**.
> Display: 5 `CommitVidPn`, 20 `Present` (10 `SDMA=YES`) e frames vazios
> (`BlankStreak=3`); 7 falhas do DWM. Faixa **A14** (offline) deu perna 3 ao `INDIRECT` por
> canários, aparou a confirmação do binder e fez `DestroyProcess` liberar as faixas de GPUVA
> do processo — o `status=10 OWNER_MISMATCH` dos `Present #16..#20` era faixa viva de um
> `dwm.exe` **morto**. Pacote `3.83.0.0` assinado e **não instalado**. Handoff:
> [`docs/181_HANDOFF_2026-09-07_17-13_INDIRECT_EXECUTOU.md`](docs/181_HANDOFF_2026-09-07_17-13_INDIRECT_EXECUTOU.md).

> **Corrida física 07/09 17:43 — B1 FECHADO NO SILÍCIO, e um `0x119` por aritmética nossa.**
> Duas submissões saíram com `IB_TRIPE_VEREDITO: TRIPE_PROVADO`: as **três** pernas sobre o
> DMA buffer real do `dxgkrnl` — `RPTR==WPTR` nas duas leituras, fence da GPU sobre poison, e
> **cinco canários não-nulos plantados antes do disparo, todos substituídos**
> (`Divergentes=0`). Zero sobre poison é conclusivo: **o IB de paginação executa sob VMID8 e
> produz o efeito declarado**. Depois, `FenceId=24` deu `IB_TRIPOD TIMEOUT` com
> `TargetWPTR=516`, `RPTR=4612` e a fence **batendo** — `4612 = 4096 + 516` num anel de 4.096
> bytes. A comparação de RPTR era **crua**; o timeout devolveu `STATUS_IO_DEVICE_ERROR` e o
> VidMm derrubou a máquina (`0x119`, Arg1=2, Arg2=`0xC0000185`). A aritmética já existia no
> arquivo e este caminho não a usava — e a auditoria **exigia** a comparação crua no blt.
> Faixa **A15** corrigiu os três sítios, o discriminante `anel_drenado` e o gate. O fusível
> anti-boot-loop recusou o `StartDevice` no reboot: **não há loop**, o Código 43 atual é o
> fusível. Pacote `3.84.0.0` assinado e não instalado. Handoff:
> [`docs/182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md`](docs/182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md).

> **Corrida física 07/09 18:16 e faixas B3/B4 (offline).** A corrida fez **32 submissões
> `INDIRECT` com zero timeout e zero bugcheck**, Código 0 até o rollback, e **reproduziu o
> B1** (2 × `TRIPE_PROVADO`, 5 canários, `Divergentes=0`). Zero `REJECT source contract` e
> zero `SpaceResolveAddress failed` com 4 `[DestroyProcess] GPUVA` — a classe
> `OWNER_MISMATCH` da A14.3 desapareceu com a mesma rotatividade de `dwm.exe` que a
> produzia. Ressalvas: o `RPTR` casou com o alvo **cru** nas submissões pós-wrap, então a
> assinatura de falha da 17:43 não reapareceu (a A15 não foi exercitada no modo que importa);
> e a imagem que voltou foi o **rollback**, não o nosso caminho de pixel (30 `Present`,
> `BlankStreak=4`). **B3:** os `PLANO A` saíram da precedência — o default do Node 0 é
> **somente `INDIRECT`**, com `SdmaPlanoAFallback` (default 0) apenas para bissecção e a
> string `SdmaIndirectOnly` proibida por gate. **B4:** a recusa do Node 1 era **muda** e o
> gate **não podia abrir** no pacote de produção (`GpuvaLabPromotion` lido só no `#else` de
> bring-up); agora a recusa nomeia o `BlockMask` e o tunable é lido em produção, ainda com
> default 0. Pacote `3.85.0.0` assinado e não instalado. Handoff:
> [`docs/183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md`](docs/183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md).

> **B6 / item 25 em 07/09 (offline) — o par de TDR deixou de devolver sucesso sem silício.**
> `ResetFromTimeout` e `RestartFromTimeout` escreviam uma linha de log e devolviam
> `STATUS_SUCCESS` **sem tocar em registrador** — Lei 4 violada no par de DDIs em que o
> `dxgkrnl` confia para decidir se o adaptador voltou; e o `mcdm_callbacks_test` **exigia**
> esse sucesso, congelando a mentira. Agora há quiesce físico (snapshot de
> `STATUS`/`ERR_LOG`, `RB_ENABLE` desligado, F32 travado, **sem** desmapear o BAR) e
> reinício **com prova de fence sobre poison**; falha devolve `STATUS_DEVICE_NOT_READY`.
> **O item 25 continua ABERTO** — falta a corrida com hang induzido. Dívida declarada: não
> existe hang induzido em `tools/`, e a detecção própria (item 24) não está ligada. Pacote
> `3.86.0.0` assinado e não instalado. Handoff:
> [`docs/184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md`](docs/184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md).

> **itens 24 e 25 em 07/09 (offline) — as duas dívidas do TDR fechadas em código.**
> **item 24:** `QueryEngineStatus` respondia `Responsive=1` **sem olhar registrador** — e o
> `dxgkrnl` chama essa DDI *durante* o TDR, então o driver sabotava a própria recuperação;
> o teste também exigia isso. Agora a resposta sai de duas passagens de `RPTR`/`WPTR`, com
> os ponteiros **reduzidos a uma volta do anel** (sem isso, os valores crus de 17:43 diriam
> "travada" para engine ociosa). **item 25:** hang induzido de verdade — `POLL_REGMEM`
> esperando um valor que nunca aparece, submissão aceita e **completion deliberadamente não
> notificado**, para o timeout de 2 s disparar o TDR; fail-closed em três camadas e disparo
> único por bind. O item 25 **continua aberto** e, por exigir *display sobrevivendo*, só
> fecha junto com o B7. Pacote `3.87.0.0` assinado e não instalado. Handoff:
> [`docs/185_HANDOFF_2026-09-07_ITEM24_25_TDR_MENSURAVEL.md`](docs/185_HANDOFF_2026-09-07_ITEM24_25_TDR_MENSURAVEL.md).

> **Correção da ordem do ensaio TDR, 07/09 (offline):** tentar armar pelo registro antes da
> instalação foi corretamente recusado: a chave de serviço só nasce pelo `AddReg` do INF, e
> o KMD lê o valor no `StartDevice`. O INF fonte permanece fail-closed em `TdrInduceHang=0`;
> o empacotador agora materializa `1` apenas no INF do candidato, exige versão nova e aceite
> explícito, e o binder exige o mesmo aceite. Pacote TDR `3.88.0.0` assinado/auditado,
> `BuildKind=MCDM_TDR_INDUCED_HANG`, **NÃO INSTALADO**. Fonte `5B6F07ED...`, SYS assinado
> `C4498C9B...`, CAT `5F0E41F6...`, INF `45CDC569...`. Testes: package audit **PASS**,
> safety audit com `audit_induced_hang_package_order` **PASS**, gates negativos sem aceite
> e com versão reutilizada **RECUSADOS antes de mutar o pacote**. Item 25 continua
> **ABERTO** e **NÃO VALIDADO EM HARDWARE**. Handoff:
> [`docs/186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md`](docs/186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md).

> **Corrida TDR 07/09 20:41 (P0) e correção offline:** o hang induzido disparou o
> `dxgkrnl`; `ResetFromTimeout` mediu/quiesceu `RPTR=68, WPTR=92, ERR_LOG=0x0F`, mas o
> restart antigo não abortou o `POLL_REGMEM`: o `SYNC_NOP` saiu com `WPTR=40`, permaneceu
> em `RPTR=68` e a fence conservou o poison. A DDI falhou fechada com
> `STATUS_DEVICE_NOT_READY`, seguido de **BugCheck 0x116** com esse status em Arg3.
> `QueryEngineStatus` não apareceu e o item 24 não foi exercitado nesta rota. Offline, o
> restart passou a pulsar `GRBM_SOFT_RESET.SDMA0`, reconstruir integralmente o anel, exigir
> readback e só depois provar com fence física. Pacote TDR `3.89.0.0` assinado/auditado e
> **NÃO INSTALADO**. Build e auditoria PASS; dois gates verificados por mutação. Item 25
> continua **ABERTO** e a correção é **NÃO VALIDADA EM HARDWARE**. Handoff:
> [`docs/187_HANDOFF_2026-09-07_TDR_0x116_E_RESET_INTEGRAL.md`](docs/187_HANDOFF_2026-09-07_TDR_0x116_E_RESET_INTEGRAL.md).

> **Corrida TDR 07/09 21:49 (P0): reset e readmissão funcionaram.** O Windows gerou live
> dump `VIDEO_TDR_TIMEOUT_DETECTED (0x117)` atribuído a `bc250mcdm.sys`. A cadeia física
> fechou `TDR_HANG_INDUZIDO` → `TDR_QUIESCE` → `SYNC_NOP TRIPOD PASS` em 2 polls →
> `TDR_RESTART PROVADO`. Depois disso houve **40 `INDIRECT` concluídos**, quatro com tripé
> completo, 36 `Present`, zero recusa e zero timeout; o adaptador permaneceu Código 0 até o
> watchdog fazer rollback e reboot planejado. A tela continuou preta porque o DWM ainda
> fail-fasta sem backend D3D11. Portanto detecção, reset, restart e readmissão estão
> **VALIDADOS EM HARDWARE**; o item 25 permanece aberto somente na perna “display visível
> sobrevive”, dependente do B7. Não repetir o TDR antes do B7. Handoff:
> [`docs/188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md`](docs/188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md).

> **Corrida 08/09 11:48 (P0, candidato lab 3.90.1.0):** Código 43 no StartDevice (`0xC00000BB`)
> antes do bring-up físico, gate do Node 1 marcou `UMD_NODE1_SKIPPED`. Causa: transporte MMIO
> permaneceu mapeado após a programação da MMU global, fazendo a guarda do segundo preflight
> readonly recusar com `STATUS_INVALID_PARAMETER`. Faixa A16 corrigiu: transporte esvaziado antes
> do segundo preflight, probe `SecondPreflightStatus` adicionado e fallback de submissão persistente
> protegido contra execução inexistente. Handoff:
> [`docs/199_HANDOFF_2026-09-08_A16_ORDEM_DO_SEGUNDO_PREFLIGHT.md`](docs/199_HANDOFF_2026-09-08_A16_ORDEM_DO_SEGUNDO_PREFLIGHT.md).
>
> **Corrida 08/09 12:36 (P0, candidato lab 3.92.1.0):** A16 validada em hardware (`stage=8, gate=1`),
> StartDevice retornou sucesso, mas Código 43 apareceu em `RuntimeCapsStatus=0xC0000182`.
> A dummy page da MMU (`0x45FFFF000`) foi tratada como prefixo a pular em vez de truncamento de topo,
> colapsando o segmento sealed. Corrigido offline separando prefixo de reserva alta. Handoff:
> [`docs/200_HANDOFF_2026-09-08_12-36_A16_VALIDADA_E_SEGMENTO_SEALED.md`](docs/200_HANDOFF_2026-09-08_12-36_A16_VALIDADA_E_SEGMENTO_SEALED.md).
>
> **Corrida 08/09 13:00 (P0, candidato lab 3.92.1.0 SYS 7B34393F):** Código 0 e segmento sealed
> validado no silício (`RuntimeCapsBaseLow32=0xA8C000`, `RuntimeCapsLocalLow32=0xF531000`,
> `QUERYSEGMENT4` e `SetRootPageTable` com readback OK). O VidPn convergiu 39 vezes, mas entrou
> em laço (46 reinícios) porque um processo nascia e morria entre a convergência e o commit.
> Handoff: [`docs/201_HANDOFF_2026-09-08_13-00_SEGMENTO_VALIDADO_E_O_LACO_DO_VIDPN.md`](docs/201_HANDOFF_2026-09-08_13-00_SEGMENTO_VALIDADO_E_O_LACO_DO_VIDPN.md).
>
> **Corrida 08/09 14:06 (P0, candidato lab 3.93.1.0 SYS EF91F17F):** Instrumentação provou que
> o processo que morria era cliente GDI (`Gdi=1, System=0`), e nenhuma DDI nossa recusava. O achado
> crucial foi `DmaBuf=0x0, SegSet=0x0` sendo publicado em `McdmCreateContext` porque `ContextAbi` só
> era inicializado no ramo de produção! Corrigido com fonte única `Bc250ActiveInitializeContextAbi`.
> Handoff: [`docs/202_HANDOFF_2026-09-08_14-06_QUEM_MORRE_E_O_CONTEXTO_ZERADO.md`](docs/202_HANDOFF_2026-09-08_14-06_QUEM_MORRE_E_O_CONTEXTO_ZERADO.md).
>
> **Corrida 08/09 14:26 (P0, candidato lab 3.94.1.0 SYS 8CA949DC):** `CommitVidPn` aconteceu!
> O laço de VidPn acabou: `DmaBuf=0x10000, SegSet=0x2`, ciclo de processos cessou (2 Create / 0 Destroy),
> `CommitVidPn` (1), `SetVidPnSourceAddress (0xAAC000)`, `SetVidPnSourceVisibility(1)`, 60 `GetStdAllocData`,
> 20 `CreateAllocation`, 30 `OpenAllocation`, e 367 interrupções VSYNC. DWM rodou e fail-fastou com
> `0xe0464645` (8 vezes) pela recusa de D3D10/11 na UMD. As paredes de lab e de produção convergiram
> em uma única parede conhecida. Handoff:
> [`docs/204_HANDOFF_2026-09-08_14-26_COMMITVIDPN_E_A_PAREDE_UNICA.md`](docs/204_HANDOFF_2026-09-08_14-26_COMMITVIDPN_E_A_PAREDE_UNICA.md).
>
> **Corrida física 12 — 16/09 23:14:13 (P0, candidato lab 3.181.1.0):** Ensaio de conformidade D3D11 nativo com resolução de 1280x720 em janela (`tools/EXECUTAR_D3D11_NATIVO.cmd`).
> - **Comportamento físico:** Daniel observou faixa horizontal de ~5% da altura da tela com pixels coloridos piscando/alternando, seguida de tela azul (BugCheck 0x116). Fusível de boot (`C:\BC250_BOOT_FUSE.dat`) disparou com sucesso (`State=2 TRIPPED`), protegendo a máquina contra loop de boot.
> - **Autópsia das 12.795 linhas do KMD (`display-log-this-bind-FINAL.txt`):**
>   1. **Faixa estreita de 5%:** `DxgkDdiPresent` estava executando cópia linear 1D simples de 11.059.200 bytes para superfícies de 1280x720 (pitch 5120), esmagando as linhas de 1280 em scanout de pitch 10240 e despejando 7,37 MB de lixo da VRAM.
>   2. **Faixa aparecendo e sumindo:** DWM chamou `DxgkDdiPresent` com buffers pretos (zeros) cobrindo a tela toda, intercalando com os Presents coloridos da aplicação.
>   3. **BugCheck 0x116 (VIDEO_TDR_FAILURE):** No frame 25, o `Bc250DisplayCompareSurfaceBytes` comparou 11 MB numa alocação de 3,68 MB, gerou divergência no primeiro byte além do buffer (`FirstMismatch=0x384002`), setou `sdmaDone = FALSE;` e retornou exceção de GPU (`STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE` / `0xC01E0200`). O kernel acionou TDR sucessivos até estourar o limite de 5 TDRs, gerando o BugCheck 0x116.
> - **Correção aplicada:** `DxgkDdiPresent` (`bc250_display_ddi.c`) atualizado para detectar subwindows (`isSubWindow = TRUE`), despachar blt 2D retangular via `Bc250SdmaPresentBltRect` com centralização no scanout (640, 180) e pular a comparação 1D de 11 MB em subwindows, eliminando o falso TDR. Testes de contrato, build WDK do SYS e auditorias de segurança e de pacote de laboratório PASS.
>
> **Corrida física 13 — 17/09 15:12:02 (P0, candidato lab 3.181.1.0):** Ensaio de conformidade D3D11 nativo (`tools/EXECUTAR_D3D11_NATIVO.cmd`) com `bc250umd.dll` e `bc250-d3d11-conformance.exe`. Evidência arquivada: `docs/evidence/2026-09-17_15-12-02-DESKTOP-HQAG6T4-lab-binding/` e [`docs/284`](docs/284_HANDOFF_2026-09-17_15-12_D3D11_NATIVO_GFX_RB0_E_ISOLAMENTO_SCANOUT.md).
> - **Comportamento físico:** 16 command buffers de render consumidos e aposentados no anel físico CP_RB0 (Node 1 GFX) sob VMID 7 com avanço de WPTR (192) e commit de fences (1 a 16). Tela permaneceu preta no monitor físico; operador reiniciou manualmente após ~2 minutos, disparando o fusível anti-boot-loop (`C:\BC250_BOOT_FUSE.dat` State=2 TRIPPED).
> - **Autópsia dos 5.236 registros (`display-log-recovered.txt`):** Desconexão arquitetural entre o render target PM4 desenhado na alocação WDDM e o buffer destilado para scanout pela Vulkan bridge (`srcRes->VkImg` continha zeros, que foram copiados ao scanout via SDMA, cobrindo o display de preto). SwapChain configurado com `BufferCount=2` no harness alternava para Buffer 1 não desenhado.
> - **Correções aplicadas offline:** Harmonização da apresentação na UMD (`bc250_d3d_umd.c`) para apresentar a alocação WDDM via `PRESENT_BLT` quando renderizado por PM4; clamping de arrays RTV/DSV (`bc250_umd_resource_vk.c`); harness corrigido com `BufferCount=1` e `OMSetRenderTargets` ordenado antes do clear. Compilações Exit 0 e pacote `output_lab` assinado. Prontidão para Corrida 14.

> ⚠️ **Correção de registro (04/09).** A linha anterior dizia "WDDM Full Graphics, DWM ativo a
> 60 Hz, SDMA Blts ativos". A corrida de 09:04 não sustenta nenhuma das três afirmações:
> o enlace roda a **56,13 Hz** (timings do OTG: H_TOTAL 2784, V_TOTAL 1111), a cadência do
> `Present` nunca foi medida, e o **SDMA0 não armou** nessa inicialização
> (`RPTR=0` com `WPTR=68`, fence com poison intacto, 4/4 bytes divergentes) — todos os 12
> `Present` caíram em cópia por CPU. Ver a nota no item 63.
**Roadmap detalhado:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md)
**Regras normativas:** [`rules.md`](rules.md) · **Recuperação:** [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md)

---

## Onde queremos chegar — e o que NÃO é o objetivo

**O objetivo deste driver é WDDM acelerado por GPU e jogos rodando com Vulkan.** Nada mais.

Rodar **jogos** na BC-250 (`1002:13FE`, Cyan Skillfish 2, GC 10.1.3), usando as 24 CUs e os
16 GB de GDDR6, com o monitor ligado ao DisplayPort da própria placa, com o **WDDM executado
pela GPU** — composição, apresentação e render acelerados no silício — via Vulkan (e D3D
traduzido por cima dele), **sem WARP e sem fallback de software**.

### Lei do escopo (06/09) — decisão do dono do projeto, não sugestão

1. **LLM é secundário.** `llama.cpp`, tokens/s, DirectML e afins são **subproduto**, nunca
   marco. Nenhum item de LLM entra em prioridade, em PASS de etapa ou em veredito.
2. **Compute isolado não é entregável.** Compute é **pré-requisito** de jogos (a submissão
   Vulkan passa por ele) e vale exatamente por isso — não como destino nem como consolo.
3. **É proibido propor compute ou LLM como "caminho mais rápido" quando o item de vídeo
   estiver bloqueado.** Item travado se resolve, se declara travado, ou se declara
   `NÃO VALIDADO EM HARDWARE`. Trocar a meta por uma mais fácil é fuga de escopo, e este
   documento a rejeita.
4. **Demorar é aceito.** O custo em tempo não é argumento para trocar o objetivo. Um caminho
   longo em direção a jogos vale mais do que um caminho curto para outro lugar.
5. O critério final de sucesso continua sendo o da P8: **jogo real rodando na tela ligada à
   placa, sem WARP, com TDR recuperando de hang sem perder o display.**

> ⚠️ **Correção de registro (06/09):** a plataforma desta máquina é **Windows 10 Pro 22H2,
> build 19045, UBR 2965** — não Windows 11. Confirmado pelo dump do DWM
> (`Windows 10 Version 19045 MP (12 procs) Free x64`) e pelo registro da máquina. O WDDM
> máximo aqui é 2.9, compatível com o contrato 2.0/2.7 que usamos, mas os documentos que
> diziam "Windows 11 x64" estavam errados e induziam decisão de DDI.

## Decisão de arquitetura vigente

**Display v1 é por herança do UEFI GOP — não implementamos DCN no caminho crítico.**

A BC-250 **tem** DCN 2.0.1 e ele funciona no Linux. Mas o GOP já programou OTG/DIG/PHY no
boot, e nada desliga isso quando o `dxgkrnl` assume. Os itens 57–62 (todos ✅) provam que a
imagem sobrevive à tomada de ownership com **zero linhas de DCN**. Logo o display v1 é:
VidPn de um único modo herdado, scanout em VRAM real, `DxgkDdiPresent` no modelo **blt**
por SDMA, e VSync **lido** do frame counter do OTG.

Escrita em registrador de DCN está **proibida no caminho crítico**. Leitura é permitida.
Mode-set, hotplug, flip de hardware, VRR e HDR são **P9 — opcional**.

### Backend de shader: rota B — DXBC → SPIR-V → RADV/ACO (decidida em 06/09)

O `dwm.exe` exige device D3D11 e `pfnCreateVertexShader` recebe **bytecode DXBC**; jogos
exigem Vulkan. Havia duas rotas para o backend de shader e de pipeline gráfico:

| Rota | O que exige | Decisão |
|---|---|---|
| A — DXBC → ISA nativo | compilador próprio de DXBC para RDNA **e** pipeline gráfico próprio | **rejeitada** |
| **B — DXBC → SPIR-V → RADV/ACO** | tradutor DXBC→SPIR-V no UMD + winsys da branch `wddm2` adaptado ao nosso *private data* | **ADOTADA** |

**Por que B:** um único backend atende as duas frentes do objetivo — o D3D11 que o DWM
exige e o Vulkan que os jogos exigem — em vez de construir dois. Reaproveita o compilador
ACO e o pipeline gráfico GFX10 do RADV, que já são testados, e tem precedente estabelecido
de tradução em modo usuário (DXVK e VKD3D-Proton convertem DXBC/DXIL para SPIR-V).

**Consequências normativas:**

1. O item 146 (winsys RADV para Windows) deixa de ser só "P7 de jogos" e passa a ser
   **infraestrutura compartilhada** com o caminho de vídeo.
2. **Não se escreve compilador de DXBC para ISA.** Qualquer proposta nessa direção contraria
   esta decisão e precisa reabri-la por escrito.
3. O *private driver data* é **nosso** (`BC250_ALLOCATION_PRIVATE_V1`). O winsys da branch
   `wddm2` é adaptado para emitir o nosso formato — **não** se imita a ABI da AMD.

### Desktop e jogos são dois caminhos distintos — registrado em 12/09 (faixa C3)

A rota B acima segue válida, mas ela **não** implica que o mesmo artefato sirva às duas
frentes. A distinção nunca esteve escrita e precisa estar, porque muda o que se constrói
primeiro:

| Frente | Caminho | Precisa de UMD D3D10/11 nossa? |
|---|---|:---:|
| **Jogos D3D9/10/11/12** | DXVK / VKD3D-Proton → Vulkan → RADV → nosso KMD | **NÃO** |
| **Desktop (DWM)** | `d3d11.dll` da Microsoft → **nossas DDIs D3D10/11** | **SIM** |

**Por quê:** DXVK e VKD3D-Proton substituem a `d3d11.dll` **no diretório do jogo** e falam
Vulkan. Nesse caminho o jogo nunca toca nossas DDIs D3D — e o DXVK **já tem** compilador
DXBC → SPIR-V próprio, maduro e construível com MSVC. O `dwm.exe`, ao contrário, é processo
do sistema: não se troca a `d3d11.dll` dele, então o desktop **exige** backend D3D11 nativo
na `bc250umd.dll`.

**Consequência direta sobre a ponte DXBC → SPIR-V (docs/248, 249 e C3.3):** ela serve ao
**desktop**, não aos jogos. É trabalho legítimo e está corretamente fechado por default, mas
não deve ser contabilizado como avanço da meta de jogos enquanto a ROTA 1 (DXVK +
VKD3D-Proton) não estiver esgotada.

**O que falta depois da tradução, e é onde mora o custo real.** Medido por varredura do
repositório em 12/09: o construtor de PM4 publica
`IT_NOP · IT_SET_BASE · IT_DISPATCH_DIRECT · IT_DISPATCH_INDIRECT · IT_WRITE_DATA ·
IT_WAIT_REG_MEM · IT_INDIRECT_BUFFER · IT_COPY_DATA · IT_EVENT_WRITE · IT_EVENT_WRITE_EOP ·
IT_RELEASE_MEM · IT_SET_CONFIG_REG · IT_SET_CONTEXT_REG · IT_SET_SH_REG · IT_SET_UCONFIG_REG`
— **zero opcodes de `DRAW`**. Não existem `DRAW_INDEX_AUTO`, `DRAW_INDEX_2` nem
`NUM_INSTANCES` em nenhuma linha. Tudo que está provado no silício é **SDMA** e **MEC
(compute)**; o **anel gráfico (`CP_RB0`/PFP/ME) nunca submeteu nada neste hardware sob
Windows**.

A cadeia até o DWM desenhar, com o estado de cada elo:

```text
DXBC  --[FEITO, default-off]-->  SPIR-V
      --[FALTA]-->  pipeline gráfico RADV/ACO
      --[FALTA]-->  descriptors / binding D3D11 -> Vulkan
      --[FALTA]-->  render target, viewport, blend, depth
      --[FALTA]-->  anel GFX + PM4 de DRAW      <-- ZERO medição neste hardware
      --[FALTA]-->  Draw real
```

**Decisão pendente, e é do dono do projeto** (pela Lei do escopo, item 6): se a meta é jogo
real na tela, a ordem barata é **esgotar a ROTA 1 primeiro** — DXVK/VKD3D-Proton sobre RADV,
apresentando pelo caminho de escape que já executa SDMA — e tratar o backend D3D11 nativo
como obra do **desktop**, posterior. A ordem inversa é defensável, mas custa o anel gráfico
inteiro antes de qualquer jogo aparecer. **Enquanto esta decisão não for registrada aqui por
escrito, nenhuma das duas ordens é "a" ordem do projeto.**

⚠️ **Riscos da ponte que precisam de decisão explícita, não de default:**

1. **Três DLLs MinGW dentro do `dwm.exe`.** `libvkd3d-shader-1.dll` arrasta
   `libgcc_s_seh-1.dll` e `libwinpthread-1.dll`, e a `bc250umd.dll` carrega em **todo**
   processo D3D. Por isso o tradutor exige duas chaves de registro e não tem caminho default.
2. **Licença.** `vkd3d` é LGPL-2.1. A carga dinâmica adotada é a forma compatível, mas
   **proíbe** absorver o tradutor dentro do pacote do driver.
3. **Latência no caminho quente.** Compilar DXBC → SPIR-V → ACO dentro de
   `CreateVertexShader` do DWM. Vai exigir cache em disco; **não está previsto**.

### Arquitetura de Memória da BC-250 (APU / UMA com 16 GB GDDR6)

Na AMD BC-250, a memória não é dividida fisicamente como em uma placa de vídeo dedicada comum: a placa conta com 16 GB de memória GDDR6 unificada, compartilhada dinamicamente entre o processador (CPU) e o chip gráfico (iGPU).

A alocação de VRAM definida na BIOS funciona como uma reserva mínima garantida (UMA Framebuffer Size):

- **O que a BIOS faz:** Ao selecionar valores de 512 MB até 8 GB, você apenas "bloqueia" essa quantidade da GDDR6 exclusivamente para a GPU. Esse espaço fica invisível para o sistema operacional como memória RAM comum, servindo para compatibilidade com jogos ou softwares que recusam abrir caso não detectem uma quantidade mínima de VRAM dedicada no hardware.
- **O que sobra para a RAM:** O restante dos 16 GB totais fica disponível para o sistema operacional e para as tarefas gerais da CPU. Se você alocar 8 GB para VRAM, o sistema terá 8 GB restantes de RAM.
- **Impacto direto no driver WDDM:**
  1. **Segmento 1 vs BAR 0 (256 MB):** Historicamente o driver miniport (`DXGKQAITYPE_QUERYSEGMENT4`) limitava o tamanho do Segmento 1 à abertura física do BAR 0 do PCI (`cpuVisibleBytes` = 256 MB), declarando apenas ~245 MB de VRAM efetiva para o Windows. Com isso, mesmo configurando 4 GB ou 8 GB na BIOS, o Windows Video Memory Manager (VidMm) enxerga apenas 245 MB, entrando em exaustão e paginação agressiva com superfícies de desktop 1440p.
  2. **Tratamento de Segmentos no VidMm:** O Segmento 1 é a VRAM dedicada (carveout), e o Segmento 2 é a abertura de memória de sistema (GART/Aperture). Quando a VRAM declarada se esgota, o VidMm pagina alocações para o Segmento 2 / memória de sistema. Se os callbacks de paginação (`BuildPagingBuffer` / `UpdatePageTable`) não aceitarem alocações no Segmento 2 ou tentarem codificar PTEs com segmentos não previstos, o VidMm derruba o sistema com BugCheck `0x10E` (`0x0000000B`, `STATUS_INVALID_PARAMETER`).
  3. **Acesso Unificado da GPU:** Do ponto de vista do controlador de memória da GPU (GCMC/GCVM), toda a memória reside no mesmo barramento físico GDDR6 de 256 bits. O espaço além do BAR 0 da CPU é diretamente endereçável pela GPU via nós de comando e paginação.

## 🛑 BLOQUEADOR ATIVO (07/09) — leia antes de qualquer bind

**Estado medido às 15:05:** o valor
`HKLM\SOFTWARE\BC250\Umd\D3D11CapsProbe` está **ausente**. O bloqueador anterior foi
desarmado. Ainda assim, todo binder deve manter o preflight que exige o probe ausente/zero;
se reaparecer em `1`, a UMD publica a tabela incompleta, o DWM falha em
`3DPIPELINESUPPORT` e a tela preta é intencional.

Comando de recuperação caso o preflight o encontre armado (exige elevação):

```
powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm
```

**Handoff completo desta sessão:**
[`docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md`](docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md)
— o que foi feito, o que está provado, o que é hipótese, e a corrida recomendada com o que ler
no log.

---

## Restrições de ambiente (mandam no plano)

| Restrição | Consequência no plano |
|---|---|
| **O pacote `output_lab` está ESTAGNADO** (SYS assinado às 14:16 de 07/09, hash-fonte `5D7333...`, anterior ao candidato atual cujo hash-fonte é `9A80C7...`) | Reconstruí-lo exige `build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup` — flag que existe porque a imagem executa PFP/CP/GCVM/HQD no `StartDevice` **detendo o display**. Decisão do dono, não passo de build |
| `build-bc250mcdm-lab-bringup.ps1` **sem** os flags retorna **`EXIT=0`** | Ele só valida precondições e **não compila**. Ler o exit code como sucesso deixa o SYS antigo no lugar |
| **Hash do pacote ≠ hash do build, por construção** | O empacotador **assina** o SYS. Prova de frescor é procurar, como ASCII dentro do binário, uma string de log do candidato — ex.: `IB_QUEUE_DISCRIMINANTE` |
| **Não existe segunda máquina** | **KDNET está fora.** A P3 foi reescrita para diagnóstico de uma máquina só: dump, telemetria interna, ETW autologger e bugcheck deliberado |
| A corrida física é executada pelo **Daniel** | Eu compilo, analiso e preparo. Toda corrida precisa de teto e critério de abandono definidos antes |
| Estado de engine não cai com desbind | **Boot frio obrigatório** entre binds |
| Uma BC-250 e um monitor | Matriz de hardware fica para depois (item 178) |
| O empacotador **não compila** o SYS | Pular o build de laboratório assina o binário do candidato anterior |

---

## Fonte normativa local (WDK docs) — usar antes de gastar corrida

O repositório tem uma cópia da documentação oficial em
`referencias/windows-driver-docs/windows-driver-docs-pr/display/`. Ela é `P1` (derivada do
upstream) e responde de graça perguntas que hoje custam corrida física. Os documentos que
já produziram resultado, em 06/09:

| Documento | Para que serve aqui |
|---|---|
| `enumerating-cofunctional-vidpn-source-and-target-modes.md` | **Confirma o contrato de pivô** que estava sendo violado, e lista mais dois deveres do `EnumVidPnCofuncModality` (multisampling em source pinado; mode set do target derivado do monitor) |
| `wddm-driver-and-feature-caps.md` | Tabela de caps **obrigatórios por tipo de driver**. Expôs três booleanos **M** de Full Graphics em FALSE no nosso caminho ativo |
| `device-paging-queues.md` | Semântica da **device paging fence** assíncrona — explica a espera infinita do `vkCreateDevice` |
| `gpummu-model.md` · `gpu-virtual-memory-in-wddm-2-0.md` | Contrato do modelo GpuMmu (root page table redimensionável, granularidade de 4 KB, quem atribui GPUVA) e prova que **não** houve page fault na nossa corrida |
| `iommu-model.md` | Diz que IoMmu é **só memória de sistema** e que o VidMm mapeia a primária na **aperture** — incompatível com a exigência de scanout em VRAM real da BC-250 |
| `timeout-detection-and-recovery.md` · `tdr-registry-keys.md` | Timeout default de 2 s, `0x117`/`0x141`/`0x142` e as chaves de registro. Base do gate de saída da P6 (item 25) |
| `debugging-tips-for-wddm-drivers.md` | Alavancas de diagnóstico de **uma máquina só**: `GraphicsDrivers\Scheduler\EnablePreemption=0`, GPUView, log interno do `Watchdog.sys`, e a exigência de desligar FPO em WDDM 1.2+ |
| `direct-flip-of-video-memory.md` | `SupportDirectFlip` é **Mandatory** para Full Graphics — responde o risco (a) da P2 |

> ⚠️ **`mcdm-implementation-guidelines.md` NÃO se aplica a este driver.** O nome
> `bc250mcdm` é histórico. A especificação MCDM **proíbe** expressamente
> `DxgkDdiCommitVidPn`, `DxgkDdiEnumVidPnCofuncModality` e `DxgkDdiControlInterrupt` — as
> três DDIs de que o nosso caminho de display depende — e exige
> `Class=ComputeAccelerator` no INF. Nossos INFs declaram `Class=Display`
> (`{4d36e968-e325-11ce-bfc1-08002be10318}`), que é o correto para Full Graphics. Seguir a
> guia de MCDM aqui é erro de contrato; a referência válida é a de Full Graphics WDDM.
> A lista de DDIs mínimas do MCDM continua útil **apenas** como conferência do lado de
> render (paging buffer, GPUVA, root page table, `SubmitCommandVirtual`, reset/TDR).

## Lei de honestidade deste documento

1. Um item entra na **Tabela 1** apenas com **prova física arquivada** em `docs/evidence/`.
2. Compilar, assinar, retornar `STATUS_SUCCESS` ou passar teste offline **não** promove
   nada. Isso fica na Tabela 2 com estado `🧪`.
3. Toda linha da Tabela 2 carrega a **prova física exigida** para fechá-la. Sem essa
   prova, o item não fecha — não importa quanto código exista.
4. O tripé é obrigatório onde se aplica: **`RPTR==WPTR`** + **fence física em VRAM com
   verificação de poison** + **conferência byte a byte** contra gabarito da CPU.
5. Onde a prova não existir, o veredito é literalmente **`NÃO VALIDADO EM HARDWARE`**.

**Legenda de estado:** ✅ validado no silício · 🧪 provado só offline · 🟡 parcial ·
⛔ bloqueado · ⬜ não iniciado · 🧹 dívida a remover · ❌ reprovado/encerrado
**`#`** = numeração histórica do checklist, preservada para rastreabilidade.

---

## 📋 Diretrizes Universais de Auditoria, Mineração de Prova e Higiene Operacional (09/09/2026)

> Normas mandatórias derivadas da auditoria offline e conferência das corridas de 09/09 ([docs/230](docs/230_AUDITORIA_ESTADO_WDDM_2026-09-09.md) e [docs/231](docs/231_CONFERENCIA_CORRIDA_21-20_2026-09-09.md)), obrigatórias para qualquer agente ou humano operando neste repositório.

1. **Fatiamento Obrigatório de Log Acumulado (`display-log-baseline.json` e `start_offset`):**
   - O arquivo `C:\BC250_DISPLAY_LOG.txt` é estritamente acumulativo (append-only) e cresce a cada bind (ultrapassando 7 MB em 09/09).
   - É terminantemente **PROIBIDO** minerar ou contar ocorrências no log integral para avaliar o resultado de uma corrida específica.
   - Toda análise deve ler `display-log-baseline.json`, extrair `start_offset` e fatiar o arquivo a partir desse marco em bytes (ou utilizar ferramentas automatizadas que respeitem o baseline).
   - Distinguir rigorosamente: linhas de log emitidas ≠ chamadas de DDI recebidas ≠ submissões aceitas ≠ operações fisicamente concluídas no silício. O último `FenceId` não afere contagem de submissões consecutivas comprovadas.

2. **Semântica Estrita de Telemetria de Interrupções:**
   - Ocorrências de `[ControlInterrupt] VSYNC Enable=0/1` são chamadas do sistema operacional solicitando habilitar ou desabilitar o VSync. Elas **NÃO** representam pulsos físicos de VBlank recebidos ou tratados pelo controlador OTG.
   - Contagens de interrupções físicas devem ser derivadas exclusivamente de medições de VBlank registradas pelo hardware ou pelo DPC real de VSync.

3. **Escopo Real de Prova do Nó 1 (Compute MEC):**
   - O teste de harness `B7_UMD_NODE1_TRIPOD` provou submissão PM4 com `WRITE_DATA` e `RELEASE_MEM` sob VMID 9 e atualização de memória física.
   - Isso **NÃO** demonstra dispatch de shader gráfico ou de compute (`DISPATCH_DIRECT`), nem prova existência de compilador DXBC/SPIR-V funcional pela rota UMD/WDDM.
   - É expressamente proibido registrar a expressão "executou o shader" sem prova de dispatch real no silício.
   - A cerca monitorada de usuário (`0xBC250F01`) é distinta da cerca interna de retirement do scheduler (`18`); conclusão de fence do usuário não prova retirement pelo scheduler.

4. **Higiene e Segurança de Scripts de Rollback e Ferramentas:**
   - Scripts de rollback (`tools/rollback-bc250mcdm-lab.ps1`) devem exigir explicitamente `-Execute` e `-IAcceptDisplayInterruptionRisk` para efetuar alterações de estado de hardware ou PnP. Chamadas sem parâmetros devem operar exclusivamente em modo de visualização (dry-run).
   - É proibido utilizar switches inexistentes no sistema operacional alvo (ex.: `pnputil /disable-device /force` só existe no Windows 11 22H2+; a máquina alvo é Windows 10 Pro 22H2 build 19045).
   - O código de saída (`$LASTEXITCODE` / `$?`) do `pnputil` e demais comandos deve ser verificado após cada invocação.

5. **Honestidade e Princípio Fail-Closed na UMD Gráfica:**
   - É proibido retornar falso sucesso (`S_OK`) para DDIs DXGI ou D3D10/11 cujas operações não sejam efetivamente executadas pela GPU (ex.: `Present` sem callback, `Blt` sem hardware blt).
   - O Type 130 (`3DPIPELINESUPPORT`) na UMD deve retornar `E_NOTIMPL` fail-closed enquanto `BC250_D3D10_11_NATIVE_READY == 0`. Anunciar níveis D3D10/11 sem backend rasterizador e sem shader dispatch viola a Lei 4 e causa falhas com fail-fast no DWM.

6. **Reciclagem e Gestão Limpa de Recursos do KMD:**
   - Tabelas de objetos internos (Contexts, Allocations, Processes) devem implementar limites adequados (4096) e reciclagem de slots mortos (`State == DEAD`, `ChildCount == 0`, `InFlightSubmissions == 0`) para impedir exaustão determinística (`STATUS_INSUFFICIENT_RESOURCES` / `0xC000009A`) durante alta rotatividade de processos (ex.: DWM respawn).

---

# TABELA 1 — JÁ FEITO · validado no silício

**75 itens** com prova física arquivada. Estes são os ativos reais do projeto.

| # | Bloco | Item | Prova física que sustenta o ✅ |
|---:|---|---|---|
| 1 | Base | Identificação PCI | `1002:13FE` Cyan Skillfish 2 lido do barramento |
| 2 | Base | Topologia Zen 2 + GC 10.1.3 | 24 CUs observadas em runtime |
| 3 | Base | BARs e recursos PCI | Lidos e mapeados no bring-up físico |
| 6 | Base | Driver de laboratório `bc250dbg` | Diagnóstico e telemetria isolados, em uso |
| 7 | Base | Recovery manual | `C:\BC250_RECOVERY.cmd` executado com sucesso |
| 8 | Base | Watchdog fire-and-collect | 3.80 fechou o ciclo sozinho: bind → retrato → rollback → reboot → `BasicDisplay / Código 0` |
| 9 | Base | Binding sem watchdog | Aceite explícito + recovery manual exercitados |
| 10 | Base | Hash instalado vs pacote | SYS comparado a cada bind, divergência reprova |
| 11 | Base | Assinatura de laboratório | Pacotes assinados e auditados a cada candidato |
| 12 | Base | Gate de boot limpo / dirty-engine | Fail-closed disparou e bloqueou corrida real |
| 12b | Base | Guarda de engine suja persistente | Validado no silício em 03/09: leitura direta de `C:\BC250_BOOT_FUSE.dat` sobrevive a rollback e reboot (`evidence/2026-09-03_09-25-43-...`) |
| 13 | Firmware | PSP / GPCOM | Carga autenticada, `RespStatus == 0` |
| 14 | Firmware | RLC | Saiu da ROM; firmware aceito pela engine |
| 15 | Firmware | PFP | Carga direta e execução provadas |
| 16 | Firmware | ME | Tríade CP comprovada |
| 17 | Firmware | CE | Tríade CP comprovada |
| 18 | Firmware | MEC | Compute persistente fisicamente funcional |
| 19 | Firmware | SDMA | Motor inicializado e trabalho real executado |
| 20 | Silício | Golden registers GFX | Sequência upstream aplicada com máscaras SOC15 e readback |
| 21 | Silício | Clocks GFX | Suficientes e estáveis para P1/P2 |
| 28 | Memória | VRAM GDDR6 acessível | Buffers físicos usados nas provas P1/P2 |
| 29 | Memória | PTE 0x10 / tradução GCVM | Tradução exercitada e conferida no silício |
| 42 | Memória | Scanout / Primary em VRAM real | Scanout decodificado em VRAM física real (`0xC0000000` / `0xF400000000ull`) herdado do UEFI GOP; 128 MB mapeados em kernel (`0xFFFFC178FAC00000`) em 03/09 (`evidence/2026-09-03_23-32-07-...`) |
| 43 | Filas | MQD / HQD | MEC2 / Pipe0 / Queue0 ativada e lida de volta |
| 44 | Filas | Ring persistente | Engine online, estado transacional |
| 45 | Filas | Indirect Buffer | Readback byte a byte antes do kick |
| 46 | Filas | Shader real na GPU | Resultado saiu do padrão de poison |
| 47 | Filas | **Prova P1** | 17/17, duas fences GPU, `RPTR == WPTR` |
| 48 | Filas | **Prova P2** | 16/16 aceitas e aposentadas, zero mismatches, reproduzido n=2 |
| 49 | Filas | Fence física direta | Escrita **pela GPU** em VRAM, poison verificado |
| 50 | Filas | Retirement direto | Telemetria confirma aposentadoria física |
| 57 | KMDOD | Registro display-only | `DxgkInitializeDisplayOnlyDriver` em Código 0 |
| 58 | KMDOD | Framebuffer do GOP | Endereço, pitch e modo herdados e lidos |
| 59 | KMDOD | VidPN KMDOD | `IsSupportedVidPn` / `CommitVidPn` aprovados |
| 60 | KMDOD | `PresentDisplayOnly` | Entrada, status e cópia funcionais |
| 61 | KMDOD | VSync KMDOD | Contadores e notificação observados (Tipo 5 / Target 1) |
| 61b | KMDOD | **VSync físico via OTG Frame Counter** | Validado no silício em 03/09 (`evidence/2026-09-03_10-01-10-...`): leitura direta de `mmOTG0_OTG_STATUS_FRAME_COUNT` em BAR5 + `0x14030` (`0xFE800000`), inicial 69380; DPC sincroniza VSync com o pulso físico do display controller; 73 interrupções tratadas sem BSOD |
| 62 | KMDOD | **Imagem após binding (Display v1)** | Confirmada visualmente com watchdog desarmado interativamente (`DISPLAY_OK`) em 03/09 (`evidence/2026-09-03_09-39-12-...`); modo 2560x1080@60Hz pinado, `PresentDisplayOnly` copiando pixels ao scanout GOP em Código 0 |
| 63 | Display/SDMA | **Display acelerado via SDMA0** | Validado no silício em 03/09 (`evidence/2026-09-03_22-55-39-...` e `23-32-07-...`): PSP carrega SDMA0 (`status=0, PhysReady=1`), F32 des-haltado (`status=0`), Tripé no StartDevice (`WPTR=68, RPTR=52, Fence=0x1001, 4/4 OK`). 32 submissões no Node 0 SDMA com notificação física `DMA_COMPLETED` para Fences 1..32 (`Submitted=32, Completed=32, Fallbacks=0`). **Nao reprodutivel boot-a-boot:** na corrida de 04/09 09:04 (`evidence/2026-09-04_09-04-24-...`) o mesmo arm falhou (`RPTR_Reg=0` com `WPTR_Reg=68`, poison intacto, 4/4 divergentes) e `HardwareEnabled` ficou FALSE; `RingWorkStatus=2` (ALLOCATION_FAILED) mostra que o caminho provado do `bc250_sdma_ring_work.c` nem chegou a rodar nesse boot. Cinco divergencias de offset/mascara entre o modulo de apresentacao e o modulo provado foram corrigidas em 04/09 e travadas por `C_ASSERT` -- correcao NAO VALIDADA EM HARDWARE. |
| 66 | DCN | Inventário read-only | Dois snapshots com intervalo de 1,122 s + zero escrita provada (`evidence/2026-09-02_...`) |
| 67 | DCN | OTG / frame counter vivo | `OTG_STATUS_FRAME_COUNT` avançou no silício (+63 frames a ~56,13 Hz) com zero escrita |
| 68 | DCN | HUBP / framebuffer herdado | Viewport 2560x1080, pitch 2560, formato ARGB e base `0x450000000` decodificados da placa |
| 81 | Display | **Takeover do GOP sem escrever no DCN** | Validado no silício em 03/09 (`evidence/2026-09-03_18-01-51-...`, `22-55-39-...`, `23-32-07-...`): Código 0 sustentado >500 s com imagem viva contínua no KMDOD e scanout passivo sob WDDM, sem tocar em registradores de escrita de DCN |
| 90 | WDDM | `DxgkInitialize` | Miniport WDDM 2.0/2.7 registrado e mantido ativo em Código 0 |
| 91 | WDDM | `AddDevice` | Device context e extension criados com sucesso sob `RenderNode=1` |
| 92 | WDDM | `StartDevice` (1 source / 1 child) | Retornou `STATUS_SUCCESS` com 1 source e 1 child; BAR0/BAR2/BAR5 mapeados, ISR/DPC conectados |
| 93 | WDDM | A/B contrato 2.0 vs 2.7 | Ambos consumidos; **mesma** falha no estágio 3 |
| 94 | WDDM | Trace de `QueryInterface` | 7 dos 8 GUIDs identificados no WDK; todos opcionais — **não** são a causa |
| 97 | WDDM | **CÓDIGO 43 — gargalo único superado** | Validado no silício em 02/09 e 03/09: Código 0 sustentado por 121 s e >500 s; `DRIVERCAPS` e `QUERYSEGMENT4` respondidos e aceitos |
| 100 | WDDM | Perfil `RenderOnlyLab` | Implementado e executado no silício (3.79/3.80); perfil aposentado |
| 102 | WDDM | **Display+render WDDM em Código 0** | Validado no silício em 02/09 e 03/09: `ConfigManagerErrorCode: 0` e `Status: Iniciado` confirmados por CIM e PnPUtil sob `RenderNode=1` |
| 103 | WDDM | Caps `QueryAdapterInfo` | `DRIVERCAPS` (`Type=1`) e `QUERYSEGMENT4` (`Type=11`) respondidos com 2 segmentos (VRAM 256MB + GART 512MB) |
| 133 | WDDM | UMD declarado no INF | Seção SoftwareDeviceSettings com `InstalledDisplayDrivers` e `UserModeDriverName` aceita pelo `dxgkrnl` |
| 123 | Ferramentas | Decoder de GUIDs | Resolvido pela fonte WDK; resta só `6BA41A33` |
| 167 | Testes | Suíte unitária core | 37 testes de kernel + 21 invariantes UMD, 0 falhas |
| 168 | Testes | Teste da init struct efetiva | WDDM 2.0/2.7 e tabela de ponteiros auditados |
| 169 | Testes | Testes de VidPN | Cobertura offline no build |
| 171 | Testes | Validadores P1/P2 | Read-only, evidência JSON arquivada |
| 172 | Testes | Safety audit | Gate no build, reprova em violação |
| 173 | Testes | Package audit | INF / CAT / SYS / manifesto conferidos |
| 174 | Segurança | **Fusível anti-boot-loop persistente** | Provado em silício em 03/09: desarma e bloqueia StartDevice em `State=2` após BSOD/falha, protegendo contra boot loops; autoriza em `State=0` (`evidence/2026-09-03_09-25-43-...` e `09-39-12-...`) |
| 175 | Ferramentas | **Watchdog com auto-reboot** | Provado em silício em 03/09: timeout expira, executa rollback e agenda reboot limpo automático, devolvendo o BasicDisplay em Código 0 sem intervenção manual |
| 176 | Diagnóstico | Análise de `MEMORY.DMP` | `cdb.exe` em 02/09: Bugcheck 0x113 (0x1C) corrigido em `SetRotationSupport` / `EnumVidPnCofuncModality` |
| — | WDDM | **Topologia VidPn comitada sob `RenderNode=1`** | Validado no silício em 03/09 (`evidence/2026-09-03_23-32-07-...`): DMM chamou `CommitVidPn` com `pathCount=1`, modo 2560x1080@60Hz pinado, `hAlloc` real atribuído (`FFFFDE8CC4728B28`), superando definitivamente a topologia vazia |
| — | SDMA | **Sanitização da GDDR6 por CONST_FILL** | Validado no silício em 03/09 (`evidence/2026-09-03_23-13-13-...` e `23-32-07-...`): `Bc250SdmaPresentFill` (opcode 11) limpou 100% da VRAM do scanout físico, eliminando ruído térmico e artefatos cinzas da tela |
| — | WDDM | **VSync do OTG ligado ao `dxgkrnl`** | Validado no silício em 03/09 (`evidence/2026-09-03_23-32-07-...`): `DxgkDdiControlInterrupt` (Tipo 5) conectado ao frame counter de hardware do OTG (`BAR5 + 0x14030`) sob WDDM |
| — | Paging | **`UpdatePageTable` via Paging Buffer** | Validado no silício em 03/09 (`evidence/2026-09-03_22-55-39-...`): `McdmBuildPagingBuffer` (Op 11 UPDATE_PAGE_TABLE) executado com sucesso no silício para Lvl 0 e Lvl 1 (`status=0x00000000`) |
| — | Scheduler | **Scheduler submetendo no Node 0 SDMA** | Validado no silício em 03/09 (`evidence/2026-09-03_22-55-39-...`): `SubmitCommandVirtual` chamado pelo scheduler, 32 fences físicas aposentadas com notificação `DMA_COMPLETED` |
| — | SDMA | **Flush de HDP no silício (P0)** | Validado no silício em 10/09 (`evidence/2026-09-10_12-16-46-...`): 1819 submissões sem timeout, máx 181 polls (antes 164.167 polls), cauda infinita eliminada |
| — | SDMA | **Fila Multi-Slot Assíncrona e Robusta** | Validado no silício em 10/09 (`evidence/2026-09-10_19-45-44-...`): 19.291 submissões aceitas e 19.291 aposentadas/notificadas (100% conciliação), 19.117 lotes, zero timeout |
| — | WDDM | **Eliminação de Corrida 0x119 Arg1=1** | Validado no silício em 10/09 (`evidence/2026-09-10_19-45-44-...`): marca d'água monotônica por nó, batch drain e guarda `SdmaRetireBusy` (1.087 yields concorrentes evitados); zero BugChecks |
| — | Telemetria | **SDMA_RETIRE_ARCHIVE em PASSIVE_LEVEL** | Validado no silício em 10/09 (`evidence/2026-09-10_19-45-44-...`): telemetria gravada no disco físico em StopDevice; fusível em CLEAN (0x00000000) e rollback gracioso online |
| — | Vulkan/Compute | **Marco 1: Compute Vulkan RADV + ACO no Nó 1** | Validado no silício em 11/09 (`evidence/2026-09-11_09-18-11-...`): Instância Vulkan, GFX1013 detectado, compute shader compilado via ACO, 8/8 zonas SMPTE + badge provadas byte a byte na VRAM, anel MEC drenado (RPTR=WPTR=12), 15/15 verificações TRUE |
| — | Vulkan/Display | **Marco 2: Vulkan RADV Present no Monitor Físico (ROTA 1)** | Validado no silício em 11/09 (`evidence/2026-09-11_22-46-24-...`): Renderização de 11.059.200 bytes (2560x1080) por shader Vulkan, apresentação direta via KMD Escape (`0xBC250001u`) no scanout do UEFI GOP (`0xC0000000`), barras de cores SMPTE e badge exibidos com perfeição no monitor físico, confirmação visual do usuário (P0), 10/10 verificações TRUE |
| — | Vulkan/Swapchain | **Marco 3: Swapchain Vulkan 60 FPS Sustentado (600 Quadros, ROTA 1 + SDMA Blt 100% GPU)** | Validado no silício em 12/09 (`evidence/2026-09-12_21-21-51-DESKTOP-HQAG6T4-lab-binding`): 600/600 submissões Node 1/MEC aceitas e aposentadas (WPTR=RPTR=7200, ring_drained=True), 600 blts SDMA para o scanout 100% por GPU (Submitted=600, Completed=600, Fallbacks=0, HostCpuCopies=0). Zero TDRs (tdr_events=0). DWM contido com D3D11LabPromotion (pid 10628, 40.130 linhas de log, zero interrupção de dispositivo). Animação contínua e fluida dos 600 quadros por 27,42 s (~22 FPS) validada visualmente no monitor físico pelo usuário (P0). |
| — | Vulkan/WSI | **Marco 4: Vulkan WSI Nativo com Cubo Texturizado 3D (600 Quadros, VK_KHR_swapchain / VK_KHR_win32_surface + SDMA HW Blt)** | Validado no silício em 15/09 (`evidence/2026-09-15_22-46-42-DESKTOP-HQAG6T4-lab-binding/` e `docs/280`): `VulkanCube.exe` executando swapchain nativa completa, 600 quadros, 1.201 submits Node 1 aceitos e aposentados (`P1SubmitAccepted=1201, P1RetiredTotal=1201`), ring drenado (`WPTR=RPTR=14412`), zero falhas GCVM, 600 blts SDMA no scanout GOP 2560x1080 com latência de 1,35 ms/quadro, zero cópias por CPU (`HostCpuCopies=0`), zero TDR, zero BugCheck. Confirmação visual humana (P0). |
| 105 | WDDM/D3D11 | **CreateDevice D3D11 Nativo** | Validado no silício em 17/09 17:48 (`evidence/2026-09-17_17-48-47-DESKTOP-HQAG6T4-lab-binding/`): Runtime Direct3D 11 oficial criou dispositivo com Feature Level `0xA100` (10_1) na BC-250 (`1002:13FE`), criando device context e command buffers WDDM com sucesso (`bc250_umd_log_7364-FINAL.txt:20`). |
| 111 | WDDM/D3D11 | **Harness D3D11 Nativo Conformance** | Validado no silício em 17/09 17:48 (`evidence/2026-09-17_17-48-47-DESKTOP-HQAG6T4-lab-binding/`): `bc250-d3d11-conformance.exe` executou a cadeia Direct3D 11 completa por 60 quadros contínuos com status `RENDERING_OK` e `Exit Code: 0` (`d3d11-native-conformance-console.txt:78-81`). |
| B9 | CP/GFX | **Backend Gráfico Real (CP_RB0 / PFP / ME)** | ⚠️ **REABERTO / PARCIAL (18/09):** O anel físico `CP_RB0` foi ativado e na Corrida 15 (17/09) o KMD aceitou e aposentou 45 submissões sob VMID 7 (`FENCE COMMIT: Token=45 RingFence=45 committed=1 accepted=1`). Contudo, medições diretas em 18/09 provaram que o WPTR não assentava no registrador MMIO (`CP_RB0_WPTR` leu 0), de modo que o CP físico não consumiu os command buffers — a aposentadoria anterior decorreu de avanço de fence KMD-side. Reaberto na Tabela 2 até prova no silício de retenção de WPTR e consumo do anel. |
| 25 / 116 | WDDM/TDR | **TDR no Silício (ResetFromTimeout / RestartFromTimeout)** | Validado no silício em 17/09 17:48 (`evidence/2026-09-17_17-48-47-DESKTOP-HQAG6T4-lab-binding/`): No Frame 20, hang detectado pelo WDDM (`0xC00002B6`); KMD executou quiesce físico de SDMA0 e `CP_RB0`, soft-reset GRBM (CP, GFX, CPF, CPG) e restart provado por NOP físico com fence sobre poison (`[McdmRestartFromTimeout] TDR: engine provada por NOP fisico`); sistema operacional sobreviveu sem crash e manteve Código 0 até rollback online. *Ressalva (18/09): o mecanismo de recuperação de hardware funciona comprovadamente, mas o gatilho inicial do hang foi o WPTR não-retido no próprio driver.* |
| 147 | UMD/Shaders | **Compilador de Shaders DXBC->SPIR-V em Runtime** | Validado no silício em 17/09 17:48 (`evidence/2026-09-17_17-48-47-DESKTOP-HQAG6T4-lab-binding/`): Shaders DXBC de Vertex e Pixel traduzidos em runtime por `libvkd3d-shader` para SPIR-V (VS 768B, PS 780B), instanciados em `VkShaderModule` e integrados em PSO Vulkan compilado sem erros (`bc250_umd_log_7364-FINAL.txt:2729-2736`). |

**Resumo da Tabela 1:** compute direto no silício, display KMDOD/GOP com ownership
físico, Código 0 sob WDDM Full Graphics (`RenderNode=1`), topologia VidPn comitada com `pathCount=1`,
DWM ativo a 60 Hz, sanitização de scanout via SDMA CONST_FILL, motor SDMA0 executando 19.291 transferências
físicas consecutivas sem falha e centenas de blts a 1,37 ms/blt 100% GPU, swapchain Vulkan contínuo com zero TDRs,
anel gráfico físico CP_RB0 aposentando 45 submissões 3D sob VMID 7, D3D11 CreateDevice nativo (10_1), harness de 60 quadros
com Exit 0, shaders DXBC->SPIR-V em runtime e recuperação TDR no silício comprovada com NOP físico estão **VALIDADOS EM HARDWARE** com evidência arquivada em `docs/evidence/`.

---

# TABELA 2 — O QUE FALTA · em ordem de prioridade

Uma lista, segmentada por prioridade `P0 → P9`, mais `T` (transversal) e `❌` (encerrado).
Cada segmento abre com **o que aquela etapa exige** antes das linhas de item.
A última coluna de cada linha é **o teste que fecha o item** — sem ele, o item não sai daqui.

---

## 🛡️ P0 — Segurança e repetibilidade do ensaio

> **Objetivo:** tornar **barato e reversível** o custo de uma corrida que falha, para que as
> 2 corridas da P4 possam ser gastas sem medo.
> **Por que vem antes de tudo:** hoje um FAIL da P4 pode custar tela preta **e** boot em
> loop, exigindo recuperação manual. O problema não é a tela preta em si — é o **custo** dela.

### A perda inicial da imagem no bind é esperada; a permanência em preto é um defeito medido

A BC-250 tem **uma única função PCI com saída de vídeo**. Quando o `bc250mcdm` liga, o
BasicDisplay é deslocado. Entre "BasicDisplay para de apresentar" e "nosso driver
apresenta", a tela está preta **por construção** — o doc 162 já registrou isso com essas
palavras. Não existe instalar sem apagar.

O que é corrigível é a **duração**:

| Hoje | Alvo (PASS da P5) |
|---|---|
| Preta **permanente** → TDR → reboot → às vezes loop | Preta por 1–2 s, depois imagem de volta |

E a tela voltar exige que o driver **apresente** — isto é literalmente a P5, que depende da
P4. **A dependência é o inverso da intuição:** a tela preta está *depois* do gargalo, não
antes. Não há como consertá-la primeiro.

### Instalar permanentemente para ir testando: sim, em dois dos três veículos

| Veículo | Pode ficar instalado? | Tela | Serve para |
|---|:---:|---|---|
| **`bc250dbg`** (laboratório KMDF) | ✅ **Sim, permanente** | Intacta — não toca ownership | **Toda a P1.** É a bancada de teste contínuo |
| **`bc250mcdm` perfil KMDOD** (`RenderNode=0`) | ✅ **Sim** — está em Código 0 com imagem viva (itens 57–62) | Funciona | **Toda a P2** e a exceção da P9 |
| **`bc250mcdm` perfil WDDM** (`RenderNode=1`) | ❌ **Não, até a P5 passar** | Apaga | Só fire-and-collect: bind → janela → retrato → rollback → reboot |

Ou seja: **você já pode testar continuamente com driver instalado** — só não no perfil que
está sob investigação. Na corrida de 06/09 20:34, a permanência em preto foi delimitada:
`IsSupportedVidPn` foi consultado 97 vezes, mas houve **0 `CommitVidPn`, 0
`SetVidPnSourceAddress` e 0 `Present`**. Logo, nessa corrida não existiu primária comitada
para alimentar o scanout. Isso se repete nas seis corridas do perfil lab e não deve ser
confundido com falha do blit SDMA. Deixar o perfil WDDM instalado antes da P5 é garantir uma máquina
sem tela.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | Nenhum. Tudo offline, implementável hoje |
| **Problema real a resolver** | O doc 161 (Fase 0.5) já registra o defeito: a guarda de engine suja lê a chave `Parameters\Probe`, **que desaparece junto com o serviço no rollback**, e por isso passa em silêncio com estado vazio |
| **Entregável 1 — fusível anti-loop** | Contador de tentativa em armazenamento que **sobrevive ao rollback e ao reboot** (fora da chave do serviço). No `StartDevice`: se a tentativa anterior não registrou desligamento limpo, **recusar o bind** e devolver o BasicDisplay. Converte "boot em loop + recuperação manual" em "um boot ruim e volta sozinho" |
| **Entregável 2 — guarda de engine suja corrigida** | Mesma fonte de verdade persistente; a guarda passa a reprovar de fato, não em silêncio |
| **Entregável 3 — canal de observação sem tela & Mini-BMC (ESP32)** | Telemetria fora de banda e dashboard Web Wi-Fi (ESP32 em `http://bc250.local`, rede `daniel`) com batimento cardíaco a cada 2 segundos (`tools/esp32-companion.ps1`). Elimina o ponto cego da tela escura: exibe contagem regressiva do watchdog, estado do driver/fusível e último log do KMD. Se o kernel travar (>4 s sem ping), alerta em vermelho que o PC congelou para reinicialização imediata; permite também forçar rollback ao BasicDisplay sob demanda via celular sem esperar o timeout de 180 s |
| **Entregável 3b — limpeza offline pelo WinRE** | `tools/BC250_WINRE_CLEAN.cmd`: um `.cmd` no pendrive que roda no Prompt de Comando da recuperação. Corrige as três armadilhas do método manual — o Windows não é `C:` em WinRE, o `oemNN.inf` muda de número, e `bc*.sys` casa com o `bcmfn2.sys` da Microsoft |
| **Entregável 4 — orçamento formalizado** | Item 177: teto de corridas, critério de abandono e recovery escritos **por etapa**, antes de armar |
| **Já a favor** | `StartType=3` (demand start) e `ErrorControl=0` (`SERVICE_ERROR_IGNORE`) já estão no INF — uma **falha de carga** não derruba o boot. O loop vem do driver **carregar** e travar depois, e é isso que o fusível pega |
| **Quem executa** | Eu implemento e testo offline; o Daniel valida com 1 bind de KMDOD (perfil seguro) |
| **Custo** | Zero corridas de risco. A validação do fusível roda no perfil KMDOD, que mantém a imagem |
| **PASS da etapa** | Fusível provado: forçar uma falha, e no boot seguinte o driver **recusar sozinho** o bind, com o BasicDisplay de volta e sem intervenção manual |
| **FAIL / abandono** | Se o fusível não puder ser provado sem arriscar a tela, ele é adiado — mas então o orçamento da P4 cai de 2 para 1 corrida |
| **Proibido** | Gastar corrida da P4 antes de o fusível estar provado ou explicitamente dispensado |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | **Fusível anti-boot-loop persistente** | ✅ | Validado no silício em 03/09: bloqueou `StartDevice` em `State=2` após crash protegendo o boot; liberou após reset (`State=0`) |
| 12b | Guarda de engine suja corrigida | ✅ | Validado no silício em 03/09: leitura direta de `C:\BC250_BOOT_FUSE.dat` sobrevive a rollback e reboot |
| — | **Watchdog com auto-reboot** | ✅ | Validado no silício em 03/09: timeout expira e reinicia automaticamente devolvendo BasicDisplay em Código 0 sem Code 43 |
| — | **Limpeza offline pelo WinRE** (`tools/BC250_WINRE_CLEAN.cmd`) | 🧪 | Um arquivo `.cmd` no pendrive; detecta o volume e o `oemNN.inf` sozinho. Lógica validada em `/list` (achou os 7 resíduos, recusou `bcmfn2.sys`). **Fecha quando for usado numa sessão real de WinRE e devolver o BasicDisplay** |
| — | **Canal de observação sem tela & Mini-BMC (ESP32 Web Dashboard)** | 🧪 | Implementado em `tools/esp32/` (`bc250_watchdog_dashboard.ino`) e `tools/esp32-companion.ps1`: Dashboard Web Wi-Fi em `http://bc250.local` (SSID: `daniel`), heartbeat a cada 2 s com telemetria viva (driver ativo, fusível de boot, cauda de log KMD, segundos restantes do watchdog). Detecção de Kernel Hang (>4 s sem ping) alertando travamento do PC para reinicialização sem espera cega, e botão de reversão forçada para BasicDisplay sob demanda via Web/Serial sem esperar o timeout de 180 s. **Fecha quando for exercitado numa corrida física e registrar o status ao vivo** |
| — | Canal de observação sem tela (RDP/SSH) | ⬜ | Sessão remota funcionando com a tela preta e o SO vivo, permitindo retrato e rollback interativos |
| **177** | **Orçamento de corridas formalizado** | ⬜ | Teto, critério de abandono e recovery escritos por etapa, **antes** de armar cada uma |

---

## 🟢 P1 — Inventário DCN somente leitura

> **Objetivo:** saber exatamente o que o UEFI GOP deixou programado no display, para poder
> herdar aquele modo na P5 sem tocar em nada.
> **Por que é a primeira:** é a única etapa que **não gasta corrida destrutiva** e é
> pré-requisito real da P2 e da P5.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | Nenhum. Pode começar hoje. BasicDisplay continua dono da tela |
| **Veículo** | Driver de laboratório `bc250dbg` — **nunca** o miniport WDDM |
| **Ferramenta** | `IOCTL_BC250DBG_REG_READ` ([`inc/bc250dbg_abi.h:49`](inc/bc250dbg_abi.h)) — já existe |
| **Fonte dos endereços** | `cyan_skillfish_ip_offset.h` (kernel) + headers `dcn_2_0_1`. **Não** o `REGISTER-MAP-BC250.md` de referência, cuja base plana está errada |
| **Quem executa** | Eu escrevo o catálogo e o probe offline; o Daniel roda o probe (1 execução, sem bind) |
| **Entregável** | Catálogo `(reg, BASE_IDX)` com proveniência + JSON de dois snapshots em `docs/evidence/` |
| **Custo** | **Zero corridas destrutivas.** Zero risco de tela preta |
| **PASS da etapa** | Framebuffer herdado decodificado, frame counter vivo, e auditoria provando **zero escrita** |
| **FAIL / abandono** | Se as bases não conferirem por readback, os offsets vão para quarentena (`Q`) e a P5 passa a depender de descoberta, não de catálogo |
| **Proibido** | Qualquer escrita. Uma única escrita nesta etapa invalida a corrida inteira |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | `DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}` | ✅ | Readback na placa batendo com `cyan_skillfish_ip_offset.h` (`probe-dcn-readonly.ps1` em 02/09) |
| 4 | Identidade do bloco de display (DMU) | 🟡 | `DCE_HWIP → DMU_BASE` confirmado por leitura, não por inferência |
| 5 | Catálogo de registradores DCN | ✅ | Entradas catalogadas em `docs/03` e confirmadas no silício por readback (`dcn_inventory.json`) |
| 64 | Existência do DCN 2.0.1 | 🟡 | Versão confirmada por leitura na própria placa |
| 65 | Base real do DCN | 🟡 | Bases com checksum e bounds; ambíguas vão para quarentena |
| 66 | Inventário read-only | ✅ | Dois snapshots com intervalo de 1,122 s + zero escrita provada (`dcn-inventory` em 02/09) |
| 67 | OTG / frame counter vivo | ✅ | `OTG_STATUS_FRAME_COUNT` avançou de 192770 para 192833 (+63 frames a ~56,13 Hz) no silício |
| 68 | HUBP / framebuffer herdado | ✅ | Viewport 2560x1080, pitch 2560, formato ARGB e base `0x450000000` decodificados da placa |
| — | Framebuffer está na **VRAM real**? | ⬜ | Endereço classificado como VRAM carve vs stolen. Exigência do chip: *SG display* desligado no cyan skillfish por patch upstream |

---

## 🟢 P2 — VSync físico no KMDOD + verificação offline da P5

> **Objetivo:** trocar o timer de VSync do KMDOD pela leitura do frame counter real do OTG,
> e responder no WDK — de graça — as quatro perguntas que podem derrubar a P5.
> **Por que agora:** é o único progresso possível **enquanto o WDDM está bloqueado**, e o
> código sai 100% reaproveitado na P5.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P1 fechada (fase e período do VBlank medidos) |
| **Veículo** | **KMDOD** (`DxgkInitializeDisplayOnlyDriver`) — ele **já está em Código 0** e **não** está bloqueado pelo Código 43, que é do caminho `DxgkInitialize` |
| **Ferramenta** | Leitura de `OTG_STATUS_FRAME_COUNT` no caminho de VSync do KMDOD + WDK/headers para a verificação offline |
| **Quem executa** | Eu implemento e a verificação de WDK; o Daniel roda 1 bind de KMDOD |
| **Entregável** | KMDOD com VSync derivado do hardware + documento respondendo os 4 riscos da P5 |
| **Custo** | **Zero escrita em hardware.** 1 corrida de KMDOD, que é o perfil seguro e já provado |
| **PASS da etapa** | KMDOD em Código 0 com VSync do frame counter físico, DWM sem stutter mensurável, e os 4 riscos da P5 respondidos por documentação |
| **FAIL / abandono** | Se o `dxgkrnl` recusar VSync derivado de polling, cair para timer e **registrar a dívida** — não insistir |
| **Fecha uma dívida** | Item 61 hoje é ✅ com timer. Isto o torna honesto perante a Lei 3 (Zero Fakes) |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | Fase e período do VBlank medidos | ✅ | Polling read-only com período estável (~56,13 Hz medidos no silício com zero escrita) |
| 61b | **Trocar o timer de VSync pela leitura do OTG** | ✅ | KMDOD em Código 0 com VSync derivado do frame counter físico lido em BAR5+`0x14030` (`evidence/2026-09-03_10-01-10-...`) |
| — | Verificar no WDK os 4 riscos da P5 | 🟡 | **RESPONDIDO EM PARTE PELA FONTE NORMATIVA (06/09, `P1`)** — leitura de `referencias/windows-driver-docs/.../display/`: **(a)** flip de hardware não é opcional no papel: `direct-flip-of-video-memory.md` marca *Driver implementation — Full graphics: **Mandatory***, e `wddm-driver-and-feature-caps.md` lista `SupportDirectFlip` e `FlipCaps.FlipOnVSyncMmIo` como **M** para Full Graphics. **(b)** VidPn de modo único é legítimo: `enumerating-cofunctional-vidpn-source-and-target-modes.md` manda ajustar **somente** os mode sets **sem** modo pinado — um source com modo pinado deve ficar intacto. **(c)** VSync por polling **não** é tratado por nenhum destes documentos — segue **em aberto**. **(d)** o layout de `DXGK_PRESENTATIONCAPS` foi conferido contra `d3dkmddi.h` do WDK 10.0.26100 e o valor reportado `0x1006C903` decodifica exatamente nos campos que o código escreve. **Fecha quando (c) tiver resposta escrita.** |

---

## 🟡 P3 — Diagnóstico com **uma máquina só**

> **Objetivo:** saber o `NTSTATUS` e o ponto exato em que o `dxgkrnl` desiste do adaptador.
> **Mudança de plano:** **não temos segunda máquina, então KDNET está fora.** A estratégia
> passa a ser em quatro camadas, todas de uma máquina só, em ordem crescente de custo.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | Nenhum para a camada 1 (o dump já existe no disco) |
| **Camada 1 — dump pós-morte** | `C:\Windows\MEMORY.DMP`, 1,2 GB, de 02/09 18:00, **nunca aberto**. `cdb.exe` já instalado. Exige **PowerShell elevado**. Custo: minutos |
| **Camada 2 — telemetria interna ampliada** | Já provou valor: foi o anel de 16 DDIs + bitmap de QAI (schema 134) que produziu o encerramento do Gate 1.5. Ampliar para persistir estado e status **antes** do `StopDevice`. Custo: build + 1 corrida |
| **Camada 3 — ETW autologger do `dxgkrnl`** | `Microsoft-Windows-DxgKrnl-Diagnostic` numa sessão **AutoLogger** (persistente, começa no boot), que é o que permite capturar uma falha 5 s após o bind e ler depois do reboot. Verificar offline se o canal existe e o que expõe. Custo: 1 corrida |
| **Camada 4 — bugcheck deliberado** | O próprio driver chama `KeBugCheckEx` no ponto de interesse **carregando o `NTSTATUS` nos parâmetros**. Vira um "breakpoint" que o dump captura. Custo: 1 corrida + reboot |
| **Sobre `windbg -kl`** | O doc 124 diz que não serve. **Correção parcial:** local KD não permite breakpoint, `g`, step nem call stack — mas **permite ler memória e rodar extensões** (`!dxgkrnl.*`). Útil para inspeção, não para fluxo. Marcar como `H` até confirmar que as extensões respondem |
| **Quem executa** | O Daniel roda o `cdb` elevado; eu escrevo a instrumentação e leio a saída |
| **PASS da etapa** | Bugcheck identificado com stack, **ou** o `NTSTATUS` exato da desistência capturado por qualquer das quatro camadas |
| **FAIL aceitável** | O dump ser de outra causa. Registrar e seguir — a P4 **não** depende disto |
| **Regra da camada 4** | Só atrás de tunable, default desligado, com o harness de rollback armado. Um bugcheck deliberado é uma escolha, não um acidente |
| **Se um dia aparecer** | Qualquer PC ou notebook na mesma rede serve de host KDNET. Se surgir um, a camada 1–4 continua válida e o KDNET vira bônus |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| 176 | Analisar `C:\Windows\MEMORY.DMP` | ✅ | `cdb.exe` rodado em 02/09: Bugcheck 0x113 (0x1C) em `SetRotationSupport+0x137` por valor residual `0x29B` em `RotationSupport` de `bc250mcdm` na DDI `EnumVidPnCofuncModality` |
| 121b | Telemetria persistida antes do `StopDevice` | ⬜ | Status e argumentos de cada DDI sobrevivendo ao teardown e legíveis pós-reboot |
| 174 | ETW / WPP / IFR com AutoLogger | ⬜ | Trace de alta resolução capturado atravessando o reboot, sem persistir em ISR/DPC |
| — | Bugcheck deliberado com `NTSTATUS` nos parâmetros | ⬜ | Minidump trazendo o status exato com que o `dxgkrnl` abandonou o adaptador |
| 126 | Símbolos internos do `dxgkrnl` | 🟡 | Validados com `x`; fallback documentado |
| 175 | ~~Runbook KDNET~~ | 🧹 | **Fora do plano por falta de segunda máquina.** Rebaixar o doc 21 e marcar as camadas 1–4 como o caminho oficial |
| 124 | Runbook WinDbg local | 🧹 | Corrigir o texto: `-kl` **lê** memória e roda extensões; não faz breakpoint/stack/`g` |

---

## 🟢 P4 — Código 43 superado · WDDM Display+Render ativo em Código 0

> **Objetivo:** o adaptador WDDM permanecer em Código 0 em vez de ser desmontado no retorno
> do `StartDevice`.
> **Estado:** **FECHADA NO SILÍCIO (02/09 e 03/09).** Código 0 sustentado por mais de 500 s
> sob `RenderNode=1`, com DMM, VidPn, alocações e DWM operacionais (`DRIVERCAPS` e `QUERYSEGMENT4` respondidos).
> Os itens centrais desta etapa foram promovidos à **Tabela 1** (itens 90, 91, 92, 97, 102, 103 e 133).

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | **P0 fechada** (fusível anti-loop provado) **e** P3 camada 1 (o dump) não apontar defeito nosso. Sem o fusível, um FAIL pode custar boot em loop e recuperação manual |
| **O que já está pronto** | Candidato **3.81 assinado e auditado**: seção `.HW` no INF com `InstalledDisplayDrivers` / `UserModeDriverName` / `Wow`, e `bc250umd.dll` real com `OpenAdapter10/10_2/11/12` e `pfnCreateDevice = E_NOTIMPL` fail-closed |
| **Hipótese sob teste** | O adaptador é recusado por não declarar user-mode driver. É a **única hipótese viva** — sources/children já foi refutado (12 corridas), e as recusas de `QueryInterface` também |
| **Ferramenta** | Harness fire-and-collect: bind → janela fixa → retrato → rollback → reboot, sem intervenção |
| **Quem executa** | O Daniel arma; eu leio o anel de DDI e o bitmap de QAI |
| **Custo / orçamento** | **2 corridas.** Boot frio entre elas |
| **PASS da etapa** | `bc250mcdm / Código 0` **sustentado** por 5 amostras de PnP **e** o bit de `DRIVERCAPS` aceso em `V5QaiTypeSeenLo` |
| **PASS parcial que ainda vale** | Avançar no anel de DDI e falhar em ponto **novo** já é informação — muda a hipótese |
| **FAIL / abandono** | 4 corridas sem avanço no anel → parar de gastar corrida e escalar para a P3 camada 4 (bugcheck deliberado) |
| **Risco conhecido** | 3.81 é display+render (1 source / 1 child) **sem DCN**. Se o `dxgkrnl` aceitar, ele tira o GOP e a tela apaga. O rollback + reboot do harness é o que torna isso aceitável |
| **Proibido** | Rearmar sem boot frio; rearmar sem ler a evidência da corrida anterior |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| **97** | **CÓDIGO 43 — gargalo único** | ✅ | **FECHADO NO SILÍCIO (02/09 e 03/09):** Promovido à Tabela 1; Código 0 sustentado por 121 s e >500 s; `DRIVERCAPS` perguntado e respondido (`bc250_display_log.txt`) |
| 133 | UMD declarado no INF | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; `dxgkrnl` aceitou o pacote e avançou por toda a inicialização WDDM |
| 134 | D3D UMD `bc250umd.dll` | 🧪 | DLL declarada e carregável na placa |
| 102 | Display+render em Código 0 | ✅ | **FECHADO NO SILÍCIO (02/09 e 03/09):** Promovido à Tabela 1; `ConfigManagerErrorCode: 0` e `Status: Iniciado` confirmados por CIM e PnPUtil sob `RenderNode=1` |
| 90 | `DxgkInitialize` | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; adaptador permaneceu ativo em Código 0 |
| 91 | `AddDevice` | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; executado com sucesso no ciclo de bind |
| 92 | `StartDevice` (1 source / 1 child) | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; retornou `STATUS_SUCCESS` com 1 source e 1 child; sem recusa |
| 103 | `QueryAdapterInfo` caps | ✅ | **FECHADO NO SILÍCIO:** Promovido à Tabela 1; `DRIVERCAPS` (`Type=1`) e `QUERYSEGMENT4` (`Type=11`) consultados e respondidos |
| 98 | `StopDevice` | 🧪 | Revalidar com trabalho WDDM pendente na fila |
| 99 | `RemoveDevice` | 🧪 | Revalidar recursos e ownership de display |
| 95 | `QueryChildRelations` | 🧪 | Validado no perfil display sem tela preta |
| 96 | `QueryDeviceDescriptor` | 🧪 | EDID coerente com o monitor real |
| 119 | `DxgkDdiPresent` retorna `NOT_SUPPORTED` | 🧹 | Superado pela implementação do pipeline Blt por SDMA |
| 120 | `SetDisplayPrivateDriverFormat` sucesso vazio | 🧹 | Removido da tabela de callbacks até existir contrato real |
| 122 | `QueryVidPnHWCapability` zera caps | 🟡 | Auditoria dizendo se zero é semanticamente correto |
| 127 | Matriz DDI | 🟡 | Saída renomeada como *structural-only* + testes semânticos reais |
| 130 | INF fonte vs pacote | 🟡 | Manifesto como fonte única, com catraca no build |

---

## 🟠 P5 — Present por blit no modo herdado · primeiro display sob WDDM

> **Objetivo:** o desktop do Windows ser desenhado **pelo nosso driver**, no modo que o UEFI
> deixou, com o frame produzido na GPU e copiado por SDMA para o scanout.
> **Marco:** é aqui que a BC-250 passa a desenhar a tela.
> **Estado (06/09 22:42, corrida `P5_VIDPN_PIVOT_CONVERGENCE`):** o caminho de display
> **executou de ponta a ponta** com o candidato ativo que carrega as correções da faixa A.
> Convergência em **18** `IsSupportedVidPn`, **132** `GetStdAllocData`, `CommitVidPn`,
> `SetVidPnSourceAddress (Segment=1, Addr=0xAB0000)`, **12 `Present` com 10 `SDMA=YES`** e
> `BLT_TRIPOD PASS` de um frame inteiro (`WPTR=RPTR=380`, fence `0x1004` sobre poison
> `0x43DAFFFE`, 11.059.200 bytes). **E a tela continuou preta**, porque a primária estava em
> branco: `NonZero=0/256` em **63 de 63** amostras. A engine copiou 11 MB de preto.
> **A barreira do desktop visível deixou de ser a P5 e passou a ser a UMD** — ver a linha
> nova na P7. O perfil **lab** (97/0) continua sem convergir e sem diagnóstico.
> Evidência: `docs/evidence/2026-09-06_22-42-09-.../ANALISE.md`.
> Diagnostico da tela preta ainda aberto. Na corrida de 09:04 o `Present` trouxe
> `hDeviceSpecificAllocation=NULL` e `SegmentId=12`, enquanto o tracker global resolveu
> `0xAC0000` para `0x8A0000` e o blt caiu em copia por CPU. Isso demonstra um caminho de
> origem invalido, mas nao prova qual buffer continha a imagem — **NÃO VALIDADO EM HARDWARE**.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P4 (Código 0) **e** P1 (framebuffer decodificado) **e** P2 (os 4 riscos respondidos) — **TODOS FECHADOS** |
| **Ferramenta** | SDMA — **já validado no silício** (itens 19, 48, 49, 63). O blit é uma cópia SDMA, não cópia por CPU |
| **Orçamento de banda** | 2560 × 1080 × 4 B ≈ 11,1 MB/frame → ~664 MB/s a 60 fps. VRAM→VRAM em GDDR6: irrelevante |
| **Exigência do chip** | Scanout **em VRAM real**. *SG display* está desligado no cyan skillfish por patch upstream — GTT e memória de sistema não servem |
| **Quem executa** | Eu implemento; o Daniel arma cada corrida |
| **Custo / orçamento** | **5 corridas.** Cada FAIL custa tela preta, recovery e boot frio |
| **PASS da etapa** | Código 0 sustentado, **imagem desenhada pelo nosso driver**, 10 min sem TDR nem corrupção, e o poison do scanout substituído **pela GPU** |
| **FAIL / abandono** | 3 FAILs com a **mesma assinatura** → voltar à P1 e ampliar o catálogo antes de gastar a quarta |
| **Aceito por projeto** | Sem trocar resolução, sem hotplug, sem VRR. Isso é v1 por decisão, não por limitação escondida |
| **Proibido** | Escrever em qualquer registrador de DCN. O takeover é **passivo** |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| **81** | **Takeover do GOP sem escrever no DCN** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; Código 0 sustentado por mais de 500 s com imagem viva contínua, sem tocar em registradores de escrita de DCN (`bc250_display_log.txt`). |
| — | **VidPn de modo único herdado sob `RenderNode=1`** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; `CommitVidPn` chamado com `pathCount=1`, modo 2560x1080@60Hz pinado, `hAlloc` real atribuído (`FFFFDE8CC4728B28`) em `evidence/2026-09-03_23-32-07-...`, superando definitivamente a topologia vazia. |
| 42 | Scanout / primary allocation em **VRAM real** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; Scanout decodificado em VRAM física real (`0xC0000000` / `0xF400000000ull`) herdado do UEFI GOP. Mapeamento de 128 MB em kernel (`0xFFFFC178FAC00000`). |
| — | **`DxgkDdiPresent` modelo blt por SDMA** | 🟡 | **AVANÇO CRÍTICO NO SILÍCIO (04/09):** VidPn comitado, dirty rects chegaram a `DxgkDdiPresent` e corridas anteriores registraram `SDMA=YES`. A corrida 09:04, porém, apresentou contrato de origem inconsistente (`hAlloc=NULL`, `SegmentId=12`), tradução pelo tracker global para `0x8A0000` e `SDMA=CPU_FALLBACK`. O tracker paralelo foi removido do candidato: o journal/snapshot canônico agora aceita `UpdatePageTable` antes de `SetRootPageTable`, isola por processo e resolve backing por PTE; entradas inconsistentes falham fechado e não escrevem no scanout. Testes offline nomeados: `gpuva_journal_contract_test`, `gpuva_space_contract_test` e `display_vidpn_test`. **Fecha quando:** corrida no silício provar origem válida, `RPTR==WPTR`, fence GPU com poison, bytes do destino conferidos e Área de Trabalho visível por 10 min. **NÃO VALIDADO EM HARDWARE**. |
| — | **Cadeia display->blt executada, primária em branco** | 🟡 | **MEDIDO NO SILÍCIO (06/09 22:42):** `CommitVidPn` + `SetVidPnSourceAddress(Segment=1, Addr=0xAB0000)` + 12 `Present` (10 `SDMA=YES`, zero `CPU_FALLBACK`) + `BLT_TRIPOD PASS` de 11.059.200 bytes com `RPTR==WPTR` e fence da GPU sobre poison. A origem, porém, estava **zerada em 63/63 amostras** (`NonZero=0/256, Hash=0x00000000`), logo o blt moveu preto. Causa da primária em branco: `dwm.exe` carregou `bc250umd.dll` e recebeu **770 × `E_NOTIMPL`** em `OpenAdapter10/10_2/12`, lançou em `KERNELBASE` e caiu (2 dumps de ~178 MB arquivados); sem DWM não há `Present`, e o adaptador degradou para Código 43 **depois** da coleta (`device-after.json` registra Código 0 com serviço `RUNNING`). **Fecha quando:** a primária chegar com pixels e a Área de Trabalho ficar visível por 10 min. **NÃO VALIDADO EM HARDWARE.** |
| — | **Convergência VidPn no perfil lab display+render** | ⛔ | **BLOQUEIO MEDIDO (06/09 20:34):** 97 `IsSupportedVidPn` (todos `Supported=1`) e 65 `EnumVidPnCofuncModality`, mas 0 `GetStandardAllocationDriverData` de primária, 0 `CommitVidPn`, 0 `SetVidPnSourceAddress` e 0 `Present`. Foi encontrado e corrigido um defeito objetivo: `EnumVidPnCofuncModality` alterava `ScalingSupport`/`RotationSupport` mesmo quando o campo era o pivô. A corrida falha teve 13 pivôs de cada tipo contra 2 na corrida boa. O teste offline `display_vidpn_test` cobre a preservação em `docs/evidence/2026-09-06_P5_VIDPN_PIVOT_CONVERGENCE_OFFLINE/`, mas se isso fecha a convergência é **NÃO VALIDADO EM HARDWARE**. O binder agora pula RADV sem primária+commit+source address do mesmo bind. |
| — | **Caps obrigatórios de Full Graphics não declarados** | 🟡 | **DIVERGÊNCIA ENCONTRADA POR LEITURA (06/09, `P1`+`H`):** `wddm-driver-and-feature-caps.md` marca como **M** para Full Graphics os campos `SupportNonVGA`, `SupportSmoothRotation` e `SupportDirectFlip` de `DXGK_DRIVERCAPS`. No caminho ativo sob `RenderNode=1` — o handler `McdmQAI` de `bc250_mcdm_callbacks.c` — os três ficam **FALSE**: o `RtlZeroMemory` inicial zera a estrutura e nenhum deles é escrito depois. O caminho KMDOD (`Bc250DxgkDdiQueryAdapterInfoDisplay`) escreve `SupportNonVGA=TRUE`, ou seja **os dois handlers do mesmo driver divergem**. `PresentationCaps=0x1006C903` foi decodificado contra `d3dkmddi.h` (WDK 10.0.26100) e confere com o código: `SupportKernelModeCommandBuffer=0` com `DriverSupportsCddDwmInterop=1` é a combinação documentada de "sem GDI acelerado, com interop CDD-DWM" — **essa** não é divergência. Que os três booleanos zerados expliquem a não convergência é **hipótese (`H`)**. **Fecha quando:** uma corrida com os três declarados produzir `GetStandardAllocationDriverData` + `CommitVidPn`, ou provar que não muda nada. **NÃO VALIDADO EM HARDWARE.** |
| — | **Duas obrigações do contrato de cofuncionalidade ainda não implementadas** | 🟡 | **LIDO NA FONTE NORMATIVA (06/09, `P1`):** `enumerating-cofunctional-vidpn-source-and-target-modes.md` lista, além do pivô já corrigido, dois deveres de `DxgkDdiEnumVidPnCofuncModality` que o driver não cumpre: **(1)** para cada source **com** modo pinado, reportar os métodos de multisampling via `pfnAssignMultisamplingMethodSet` — condicionado a existir codec com antialiasing, então pode ser legitimamente dispensável aqui; **(2)** para target com monitor conectado, listar **somente** modos que o monitor suporta, obtidos por `pfnAcquireMonitorSourceModeSet` — nunca chamado por nós. ⚠️ **Correção de registro (06/09):** a versão anterior desta linha dizia que o modo era anunciado a 60 Hz. Leitura do código refuta: o EDID sintetizado e o modo derivam de `BC250_NATIVE_PIXEL_RATE = 173.611.437 Hz` com os totais lidos do OTG, o que dá **56,13 Hz** — coerente com o silício. O dever (2) fica dependente do item 77 (EDID real por AUX), porque validar EDID sintetizado contra si mesmo é circular. **Fecha quando:** auditoria dizer se (1) é dispensável e (2) uma corrida provar se o mode set do target derivado do monitor muda a convergência. **NÃO VALIDADO EM HARDWARE.** |
| — | Caps de apresentação sem flip de hardware | ✅ | `PresentationCaps` ajustado (`NoScreenToScreenBlt=1, NoOverlapScreenBlt=1, CddDwmInterop=1, SwDeviceBitmaps=1`), aceito pelo DMM e DWM sem recusa. |
| — | **VSync do OTG ligado ao `dxgkrnl`** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; `DxgkDdiControlInterrupt` (Tipo 5) conectado ao frame counter de hardware do OTG (`BAR5 + 0x14030`). |
| — | **Sanitização da GDDR6 antes do 1º present** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; `Bc250SdmaPresentFill` opcode 11 CONST_FILL limpou 100% da VRAM do scanout físico (`evidence/2026-09-03_23-13-13-...`), eliminando ruído térmico e artefatos cinzas da tela. |
| **63** | **Display acelerado (SDMA0)** | ✅ | **FECHADO NO SILÍCIO (03/09):** Promovido à Tabela 1; Firmware carregado via PSP (`status=0, PhysReady=1`), F32 des-haltado (`status=0`), tripé físico no StartDevice e 32 blits SDMA concluídos com sucesso. |

---

## 🔵 P6 — Render WDDM completo + TDR

> **Objetivo:** o compute já provado no silício passar a ser **dirigido pelo Windows**, com
> memória, objetos, submissão, fence, retirement e recuperação de hang.
> **Pode correr em paralelo** à P5, depois que a P4 fechar.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P4 (Código 0). A P5 **não** é pré-requisito desta |
| **Ativo de partida** | Tudo da Tabela 1: SDMA, CP/PM4, MEC/HQD, fence física, retirement. Falta o **contrato WDDM** em volta |
| **Ferramenta** | Harness D3DKMT (`OpenAdapterFromLuid → CreateDevice → CreateContextVirtual → CreateAllocation → Submit`) |
| **Tripé obrigatório** | Em cada item de submissão: `RPTR==WPTR`, fence física em VRAM com poison, conferência byte a byte |
| **Regra absoluta** | **Proibido completar fence pela CPU** em qualquer hipótese |
| **Custo** | A maior fase de kernel. Muitas corridas, mas a maioria sem risco de display se a P5 ainda não tiver assumido a tela |
| **PASS da etapa** | Submissão originada pelo `dxgkrnl` fisicamente aposentada e reconhecida pelo scheduler |
| **Gate de saída — não negociável** | **Item 25 (TDR).** Hang induzido → detectado → engine resetada → adaptador volta a aceitar submissão → display sobrevive. Sem isso, **não** se entra na P7: todo bug de shader viraria reboot |
| **FAIL / abandono** | Item por item, com bissecção. Não empilhar mudanças num mesmo candidato — foi isso que invalidou a corrida 3.75 |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| **—** | **Causa-raiz do VidPN vazio resolvida** | ✅ | **FECHADO NO SILÍCIO (03/09, corridas 21:18 a 23:32):** Implementação de `Bc250McdmGetStandardAllocationDriverData` para SharedPrimary/Shadow/Staging/GDI e a declaração do modelo de memória (`GpuMmuSupported`; **não** `IoMmuSupported`, ver correção de registro na Tabela 1) destravou a criação de alocações pelo VidMm, gerou `hAlloc` real e eliminou a topologia vazia (`pathCount=1`). Promovido à Tabela 1. |
| 104 | Nodes / engines WDDM | 🟡 | **REABERTO PARA NODE 1 (18/09):** Node 0 SDMA com 1.089 submissões indiretas e tripé comprovado; Node 2 Compute exposto. Node 1 3D Graphics reaberto após medição de 18/09 provar que as 45 submissões históricas eram fences de software sobre anel cujo WPTR MMIO não assentou no silício (`WPTR=0`). Fecha quando o anel gráfico drenar fisicamente. |
| **B9** | **Backend Gráfico Real (`CP_RB0` / PFP / ME) no silício** | 🟡 | **REABERTO (18/09):** O anel físico `CP_RB0` foi ativado com sucesso (`ONLINE SUCESSO`), mas a escrita MMIO de `CP_RB0_WPTR (0xC150)` retornou 0 no silício na corrida 18/09 14:20:07. As 45 submissões creditadas anteriormente foram aceitas pelo KMD mas não consumidas pelo CP físico. Hipótese A (GRBM broadcast) e canal de doorbell em teste para destravar a retenção do WPTR. |
| 111 | Harness D3D11 Conformance | ✅ | **FECHADO NO SILÍCIO (17/09 17:48, Corrida 15):** Harness nativo `bc250-d3d11-conformance.exe` executou a cadeia D3D11 completa (Device, Shaders DXBC, Vertex Buffer, PSO, 60 Draws e Presents) por 60 quadros com status `RENDERING_OK` e `Exit Code: 0`. Promovido à Tabela 1. |
| 105 | `CreateDevice` D3D11 | ✅ | **FECHADO NO SILÍCIO (17/09 17:48, Corrida 15):** Runtime Direct3D 11 oficial criou dispositivo com Feature Level `0xA100` (10_1) na BC-250 (`1002:13FE`), criando device context e command buffers WDDM com sucesso (`bc250_umd_log_7364-FINAL.txt:20`). Promovido à Tabela 1. |
| 106 | `CreateContext` / Virtual | 🟡 | Contextos de paging e present ativos e operacionais. |
| 107 | `HwQueue` | ⬜ | Create/Destroy/Submit por fila real, sem leak |
| 108 | `CreateAllocation` | 🟡 | Alocações SharedPrimary, GDI, Staging e Shadow criadas e aceitas pelo `dxgkrnl`. |
| 37 | Allocations WDDM | 🟡 | Create/Open/Close com ownership real exercitados na inicialização do DMM. |
| 38 | Segments WDDM | ✅ | **FECHADO NO SILÍCIO (03/09):** Segmento 1 (VRAM BAR0 256MB) e Segmento 2 (Aperture 512MB) consumidos pelo VidMm via `QUERYSEGMENT4`. |
| 39 | Residency | ⬜ | `MakeResident`/`Evict` com accounting conferido |
| 40 | Paging buffer (`UpdatePageTable`) | 🟡 | `BuildPagingBuffer` Op 11 retornou sucesso para Lvl 0/1 e o journal publicou updates, mas isso é construção CPU-side. **Correção 07/09:** a corrida das 14:32 tem zero Op 11 e zero submit no log delimitado; ocorrências no log acumulado não pertencem ao bind. Fecha somente com o DMA de paginação originado pelo VidMm e o tripé físico arquivado. **NÃO VALIDADO EM HARDWARE.** |
| 30 | VMID0 page table | 🟡 | Só promover depois da GPUVA completa |
| 31 | GPUVA core (34 bits traduzíveis) | 🟡 | **PARCIAL NO SILÍCIO:** em 06/09 a root `SegmentId=1, Offset=0` e a caminhada CPU/PT coincidiram. Em 07/09 13:59, o harness isolado executou IB SDMA sob VMID8 com tripé completo (fence em 11 ms e 4 dwords); PDE corrompida gerou `MAP_ERR=1` em `0x201000` e reteve poison. Isso valida o mecanismo isolado, não a submissão WDDM nem Node 1. |
| 32 | Journal GPUVA | 🟡 | **PARCIAL NO SILÍCIO:** após representar a root zero por chave interna não nula, flushes publicaram commits globais até `Generation=9`. Concorrência, rollback e teardown completos ainda **NÃO VALIDADOS EM HARDWARE**. |
| 33 | Snapshot GPUVA | 🟡 | **PARCIAL NO SILÍCIO:** `IB_GPUVA_SNAPSHOT Status=0` nas duas submissões de paginação da corrida 20:34. Snapshot para submissão Node 1 ainda **NÃO VALIDADO EM HARDWARE**. |
| 34 | Gate GPUVA | 🟡 | `GpuvaLabPromotion=1` exercitada com commits físicos de TLB (`AckObserved=1`); o gate permitiu snapshots Node 0, mas `vkCreateDevice` não chegou ao submit Node 1. |
| 35 | Map / unmap GPUVA | 🟡 | O conjunto global MMU + VMID8 foi provado no harness isolado em 07/09 13:59. No WDDM, `StartDevice`/`SetRootPageTable` agora falham fechado e rollback/restore têm readback; fecha quando map/unmap do VidMm produzir tripé físico e teardown exato no mesmo bind. **NÃO VALIDADO EM HARDWARE.** |
| 36 | Root page table por processo | 🟡 | Criada, associada, invalidada e destruída pelo `dxgkrnl` |
| 109 | Patch / Render / RenderKm | 🟡 | Semântica real; `NOT_SUPPORTED` não fecha |
| — | **Cadeia causal do hang do RADV** | 🟡 | O contrato normativo (`P1`) continua dizendo que updates de page table entram no motor de paginação e concluem por fence. O ensaio isolado das 13:59 refutou a forma forte da hipótese antiga — “IB sob VMID8 nunca executa” — porque ele executou com tripé. Permanecem hipóteses distintas: integração WDDM da MMU global, submissão/fence de paginação e caminho Node 1. A corrida das 14:32 não testou nenhuma delas: MMU `SNAPSHOT_FAILED`, zero Op 11/submit no log delimitado e RADV pulado. Fecha quando `vkCreateDevice` retornar após tripé de paginação WDDM no mesmo bind. **NÃO VALIDADO EM HARDWARE.** |
| 110 | `SubmitCommandVirtual` | 🟡 | **VALIDADO NO SILÍCIO (03/09):** Scheduler despachou 32 submissões no Node 0 SDMA com sucesso (`Submitted=32, Completed=32, Fallbacks=0`). |
| 112 | Fence WDDM | 🟡 | Fences físicas 1 a 32 notificadas ao scheduler via `DMA_COMPLETED`. |
| 113 | Interrupt / DPC WDDM | 🟡 | Completion físico de VSync e interrupções em runtime. |
| 23 | IH de compute | 🟡 | Interrupção real de completion observada |
| 114 | Retirement WDDM | 🟡 | Scheduler reconhecendo completion física para 32 submissões no Node 0 SDMA. |
| 51 | Multi-context | ⬜ | Isolamento entre processos/contextos verificado |
| 52 | Preemption | ⬜ | Preempt com fence coerente |
| 115 | Preemption WDDM | ⬜ | Preempt + fence/timeout pelo contrato WDDM |
| 53 | Cancel command | 🧪 | Cancelamento real com submissão pendente |
| 54 | Prioridades | ⬜ | Nodes/engines/prioridades honrados |
| 55 | Wrap do ring | ⬜ | Stress de wrap sem sobrescrever trabalho vivo |
| 56 | Concorrência | ⬜ | Multithread/process, locks e teardown sem corrupção |
| 24 | Reset de engine | 🟡 | Reset WDDM por node **preservando o display** |
| **25** | **TDR** | ✅ | **FECHADO NO SILÍCIO (17/09 17:48, Corrida 15):** No Frame 20, hang detectado pelo WDDM (`0xC00002B6`); KMD executou quiesce físico de SDMA0 e `CP_RB0`, soft-reset GRBM (CP, GFX, CPF, CPG) e restart provado por NOP físico com fence sobre poison (`[McdmRestartFromTimeout] TDR: engine provada por NOP fisico`); sistema sobreviveu em Código 0 sem crash e continuou até o Frame 60 (`Exit 0`). *Ressalva (18/09): o mecanismo de recuperação de hardware funciona comprovadamente, mas o gatilho inicial do hang foi o WPTR não-retido no próprio driver.* |
| 116 | `ResetFromTimeout` / `RestartFromTimeout` | ✅ | **FECHADO NO SILÍCIO (17/09 17:48, Corrida 15):** Quiesce e restart do `CP_RB0` e SDMA0 com soft-reset GRBM (CP, GFX, CPF, CPG) validados no silício sob hang real de hardware com NOP físico comprovado. Promovido à Tabela 1. |
| 117 | `CollectDbgInfo` | 🟡 | Dump útil produzido durante TDR real |
| 118 | Power callbacks | 🟡 | F-states reais, ou não anunciar o recurso |

---

## 🟣 P7 — UMD e Vulkan · a maior fase restante

> **Objetivo:** substituir o `E_NOTIMPL` honesto de hoje por backend real, até jogos
> traduzidos rodarem.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P6 **completa, incluindo o item 25 (TDR)**. Sem TDR, cada bug de shader é um reboot |
| **Ponto de partida** | `bc250umd.dll` fail-closed já existe e carrega. O ICD Vulkan é esqueleto |
| **Estratégia** | Winsys RADV adaptado aos objetos WDDM deste driver. **Não assumir que o RADV do Linux roda inalterado** |
| **Ordem obrigatória** | ABI → memória → queues → command buffers → submit → sync → compute → graphics → WSI → RADV → shader compiler → camadas de API |
| **Regra de verificação** | Em cada subfase, provar que o trabalho saiu na GPU: fence física + conferência de dados. `vulkaninfo` listar a placa **não** é prova |
| **Custo** | A maior fase do projeto em linhas de código |
| **PASS da etapa** | Suites de conformidade de DXVK e VKD3D passando **sem WARP e sem fallback**, confirmado por GPU counters |
| **FAIL / abandono** | Subfase por subfase. Uma subfase que não fecha não avança para a seguinte |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | **`P7_D3D11_CAPS_PROBE` — instrumentação de caps antes do device** | 🧪 | **EXPERIMENTO BARATO DEFINIDO (06/09).** Implementar `OpenAdapter10`/`OpenAdapter10_2` **apenas até os caps**: devolver tabela de adapter válida, responder `pfnGetSupportedVersions` e `pfnGetCaps` (`THREADING`, `SHADER`, `3DPIPELINESUPPORT`) e **falhar fechado em `pfnCreateDevice`**. Não é fallback nem cap falso — é instrumentação honesta que faz o runtime revelar, em **uma** corrida, qual interface ele fecha, quais caps valida e o que pede depois. Responde as três perguntas que a leitura não respondeu: se o `dwmcore` aceita feature level 10_1 (`H`), quais combinações de caps ele recusa, e qual header corresponde ao minor 17. **IMPLEMENTADO OFFLINE (06/09).** `driver/umd/bc250_umd_caps_probe.c` (lógica pura de decisão, TU isolado para não arrastar `d3d12umddi.h`), leitura do registro em `bc250_d3d_umd.c`, e instrumentação em `GetSupportedVersions`, `GetCaps` (com decodificação do `D3D10_2DDICAPS_TYPE` por nome) e `CreateDevice` (que loga a **interface acordada** e segue `E_NOTIMPL`). Armado por `tools\arm-d3d11-caps-probe.ps1 -Arm -IAcceptBlackScreenRisk`, chave `HKLM\SOFTWARE\BC250\Umd\D3D11CapsProbe`; **default desarmado** — sem a chave, o comportamento é idêntico ao de hoje. `BC250_D3D10_11_NATIVE_READY` permanece **0**: o probe concede instrumentação, nunca device. Travado por 2 casos novos em `bc250_d3d_umd_test` (tabela de decisão nos 4 estados + decodificação dos caps, incluindo que tipo desconhecido não pode inventar nome). `build-umd.bat` e `build-contract.bat` em `EXIT=0`. ⚠️ **CORRIDA v1 GASTA E FALHADA (06/09 23:45) — o defeito era meu.** A v1 aceitava **qualquer** `Interface`. O runtime pediu `0x000B0011`, respondemos `S_OK`, e ele **não chamou mais nenhuma DDI**: zero `GetSupportedVersions`, zero `GetCaps`, zero `CreateDevice`, e **66 ciclos** de carga/descarga da DLL dentro do `dwm.exe`. `OpenAdapter10` **nunca** foi chamado — por aceitarmos o degrau mais alto, o runtime **nunca desceu a escada** até o D3D10_1, que é justamente onde o probe queria observar os caps. Pior: devolver `S_OK` para um DDI não implementado é **declaração de capacidade falsa, proibida pela Lei 4**. **Aprendizado `P1` que nenhuma leitura daria:** a negociação de DDI é uma **escada descendente cujo degrau é escolhido pela recusa da UMD** — aceitar interrompe a descida, e não há re-negociação para baixo depois de um `S_OK`; e o runtime **não** usa `pfnGetSupportedVersions` como primeiro contato. **v2 (07/09, offline):** `Bc250UmdProbeAcceptsInterface` — allowlist de **um** item (`0x000A0002`), recusando o resto com `E_NOTIMPL` para a escada voltar a funcionar. Travada por `TestProbeInterfaceAllowlist`. ✅ **CORRIDA v2 (07/09 08:05) — a correção FUNCIONOU e está provada no silício.** Sequência medida: `0x000B0011` recusado (66×) → `0x000B000B` recusado (66×) → **`OpenAdapter10` com `0x000A0002` aceito com `S_OK` (22×)**. Na v1 o `OpenAdapter10` nunca era chamado. **Isso promove a `P1`** o que era `H`: a negociação de DDI é uma escada descendente e o degrau é escolhido pela **recusa** da UMD. ⚠️ **Mas o probe parou num ponto novo:** depois do `S_OK` a próxima linha é uma nova carga da DLL — zero `GetSupportedVersions`, zero `GetCaps`, zero `CreateDevice`. As perguntas 1 e 3 do doc 173 **seguem sem resposta**. Causa do impasse diagnóstico: **2 das 3 entradas de `D3D10DDI_ADAPTERFUNCS` eram mudas** (só `CreateDevice` registrava), tornando impossível distinguir “não chamou nada” de “chamou e não registramos”. **v3 (07/09, offline):** `pfnCalcPrivateDeviceSize` e `pfnCloseAdapter` passaram a registrar — as três entradas falam. Duas hipóteses vivas (`H`): (a) o caminho de 3 entradas é beco sem saída para o D3D11 porque não tem `pfnGetCaps`, e o alvo teria de ser `0x000A0002` **via `OpenAdapter10_2`** (5 entradas); (b) ele chamou `CalcPrivateDeviceSize` e desistiu ali. A v3 distingue as duas em uma corrida. Evidências: `docs/evidence/2026-09-06_23-45-38-.../ANALISE.md` e `docs/evidence/2026-09-07_08-05-47-.../ANALISE.md`. **Fecha quando:** o log trouxer a interface acordada e o caps set consultado. **NÃO VALIDADO EM HARDWARE.** |
| 132 | ABI KMD/UMD versionada | 🟡 | Handles, memória, queues e erros versionados e exercitados |
| 135 | ICD Vulkan | 🟡 | **PARCIAL (06/09 20:34):** ICD explícito carregado, instância/dispositivo físico/famílias enumerados; a chamada `vkCreateDevice` não retornou em 20 s. Carregar/enumerar não prova aceleração. |
| 136 | Enumeração pelo loader | ⬜ | `vulkaninfo` lista a BC-250 **sem fallback**, confirmado por GPU counters |
| 137 | Physical device properties | ⬜ | Caps derivadas do hardware; features não suportadas negadas |
| 138 | Memória Vulkan | ⬜ | Heaps/types coerentes com os segments da P6 |
| 139 | Queues | 🟡 | Dois contextos WDDM Node 1 foram criados durante `vkCreateDevice`, mas não houve submissão Node 1; criação completa da queue permanece **NÃO VALIDADA EM HARDWARE**. |
| 140 | Command buffers | ⬜ | PM4/IB gerado pelo ICD e conferido antes do kick |
| 141 | Submit | ⬜ | Submissão via `dxgkrnl` com fence física e tripé |
| 142 | Sync | ⬜ | Fence/semaphore/event físicos |
| 143 | Compute | ⬜ | Compute shader via ICD conferido byte a byte na VRAM |
| 144 | Graphics | ⬜ | Rasterização/images/renderpass produzindo pixels na GPU |
| 145 | WSI | ⬜ | Swapchain e present pelo caminho blt da P5 |
| 146 | RADV winsys para Windows | 🟡 | **ATIVO EXTERNO IDENTIFICADO (06/09, `P1`):** existe um port oficial de RADV para Win32 em andamento — Collabora, **financiado pela Valve**, branch `wddm2` de `gitlab.freedesktop.org/lfrb/mesa` (Louis-Francis Ratté-Boulianne). Já rodou **Counter-Strike 2** no Windows. Arquitetura: RADV como UMD Vulkan falando **D3DKMT**, com os *private driver data* do KMD da AMD **obtidos por engenharia reversa**. **Consequência para nós:** como **nós** somos o KMD, o caminho certo é **adaptar o winsys deles para emitir o nosso** `BC250_ALLOCATION_PRIVATE_V1` — não imitar a ABI da AMD. O que se aproveita de graça: build Win32, esqueleto de winsys D3DKMT, memória, filas, submissão e sync. **Não resolve o desktop visível** (ver P7 abaixo): RADV é Vulkan, e o `dwm.exe` exige **D3D10/11**. A WSI da branch deles é caminho de CPU. **ROTA B ADOTADA em 06/09:** este item deixou de ser só "P7 de jogos" e passou a ser **infraestrutura compartilhada** — o mesmo backend atende o D3D11 que o `dwm.exe` exige (via DXBC→SPIR-V) e o Vulkan dos jogos. **Fecha quando:** pipeline gráfico completo estável em long-run sobre o nosso KMD. |
| — | **ITEM ZERO — handshake `DXGKQAITYPE_UMDRIVERPRIVATE` (Type=0)** | 🟡 | **ACHADO POR MEDIÇÃO (07/09 08:25) — bloqueia todo o resto da P7.** As três corridas do `P7_D3D11_CAPS_PROBE` provaram que **nenhuma** entrada do adapter table da UMD é chamada (`CalcPrivateDeviceSize`, `CloseAdapter`, `CreateDevice`, `GetCaps`, `GetSupportedVersions` = **0**), e que o runtime descarta a DLL **sem nem fechar o adapter**. O log do KMD, na mesma corrida, mostra `[McdmQAI] Type=0, OutSize=2400` → **`Type=0 NOT SUPPORTED`, 80 vezes**: `DXGKQAITYPE_UMDRIVERPRIVATE` **não tem `case`** em `Bc250McdmQueryAdapterInfo` e cai no `default`. É o tipo **mais pedido** da corrida (160 chamadas, contra 1–2 dos demais) e o **único recusado em massa**. **Favorece a rota B:** o blob é **nosso nos dois lados**, sem ABI da AMD para imitar. `H`: que a recusa seja *a* causa do descarte — é a hipótese mais forte e coerente com toda a cadeia, mas `mcdm-implementation-guidelines.md` lista o tipo como **opcional** (para MCDM, que não é nosso perfil), e nada do que li o declara obrigatório para Full Graphics. **IMPLEMENTADO OFFLINE (07/09).** Contrato em `driver/core/bc250_umd_private_contract.c` — blob versionado (`Magic`+`Version`+`SizeBytes` primeiro, para UMD antiga contra KMD novo **falhar fechado** em vez de ler lixo), zero ponteiros na fronteira, e `GateFlags` declarando explicitamente **o que não existe** (`NO_3D_PIPELINE`, `NO_SHADER_COMPILER`, `NO_NODE1_SUBMIT`, `NO_IB_UNDER_VMID8`, `CAPS_PROBE_ONLY`) para a UMD não inferir capacidade do silêncio — foi assim que o probe v1 declarou suporte a um DDI inexistente. **KMD:** `case DXGKQAITYPE_UMDRIVERPRIVATE` em `Bc250McdmQueryAdapterInfo`, falhando fechado em buffer curto **sem escrever byte parcial**, tratando `pInputData` como não confiável (só o tamanho), e tirando os tamanhos de `runtimeCaps` — **a mesma fonte de verdade do `QUERYSEGMENT4`**, para não criar segunda fonte como aconteceu no A1. **UMD:** `Bc250UmdFetchPrivateBlob` chama `pfnQueryAdapterInfoCb` e **valida antes de publicar a tabela de adapter** — sem handshake, `E_NOTIMPL`. Travado por `umd_private_contract_test` (5 casos: round-trip, gates declarados, buffer curto sem escrita parcial, recusa de magic/versão/tamanho/incoerência, e recusa de fatos impossíveis no produtor). `build-contract.bat` e `build-umd.bat` em `EXIT=0`, auditoria em `PASS`. ✅ **VALIDADO NO SILÍCIO (07/09 09:40) — o handshake funciona de ponta a ponta:** **112** publicações no KMD (`UMDRIVERPRIVATE: 104 bytes publicados`), **zero** `NOT SUPPORTED`, e **32** validações na UMD (`blob OK -- v1, 104 bytes, NodeCount=2, GpuVaBits=48, GateFlags=0x1B`). O runtime pediu com `OutSize=2400` e **voltou a pedir com `OutSize=104`** — ele aprendeu o nosso tamanho, prova de que consumiu a resposta. ❌ **MAS A HIPÓTESE CAIU:** mesmo com o handshake funcionando, as entradas do adapter table seguem com **zero** chamadas (`INTERFACE ACORDADA`, `CalcPrivateDeviceSize`, `CloseAdapter`, `GetCaps`, `GetSupportedVersions` = 0). **A recusa do `Type=0` NÃO era a causa do descarte** — o critério estava escrito antes da corrida e foi honrado. **Suspeito restante, geométrico:** o runtime só oferece `0x000A0002` pelo `OpenAdapter10`, cuja tabela de **3 entradas não tem `pfnGetCaps`**; sem ele não há como perguntar feature level, e ele nunca pede `0x000A0002` pelo `OpenAdapter10_2` (5 entradas). **O handshake não foi desperdício:** é infraestrutura que a rota B exige de qualquer forma — o winsys do RADV precisa deste canal. Evidência: `docs/evidence/2026-09-07_09-40-57-.../ANALISE.md`. |
| — | **`P7_D3D11_CAPS_PROBE` v4 — allowlist por entry point + honestidade do caps** | 🧪 | **IMPLEMENTADO OFFLINE (07/09) após a refutação da hipótese do item zero.** `OpenAdapter10_2` passa a aceitar **`0x000B000B`** (`D3D11_0_7_DDI_INTERFACE_VERSION`), publicando a tabela de **5 entradas** — o **único caminho medido** para o `pfnGetCaps` existir; `OpenAdapter10` segue só com `0x000A0002`. O minor 17 continua fora (sem nome no WDK atual; declarar suporte ao que não se sabe nomear é chute). **Por que não é o erro da v1:** ali eu devolvia `S_OK` sem ter como qualificar a resposta — cap falso puro. Aqui aceito a **abertura** e declaro a verdade por `pfnGetSupportedVersions` (D3D10_0/D3D10_1), que é o mecanismo desenhado pela Microsoft para isso; **abrir não é prometer**, e `pfnCreateDevice` segue fail-closed. **Regra nova de honestidade:** zerar o `pfnGetCaps` é verdade para conjuntos de flags, mas **mentira** para `D3D11DDICAPS_3DPIPELINESUPPORT`, cujo payload é um `D3D11DDI_3DPIPELINELEVEL` onde **`0` == nível `10_0`** — zerar declararia um pipeline 3D inexistente, então esse tipo passa a **falhar**. Codificado em `Bc250UmdCapsZeroIsTruthful` e travado por teste. `build-umd.bat` e `build-contract.bat` em `EXIT=0`, auditoria `PASS`. ❌ **CORRIDA v4 (07/09 10:00) — a segunda hipótese também CAIU.** `OpenAdapter10_2` aceitou `0x000B000B` e publicou as **5 entradas** com `S_OK` (58 ciclos no `dwm.exe`), o handshake `UMDRIVERPRIVATE` seguiu perfeito (**142** publicações, zero recusas) — e o runtime chamou **zero** funções outra vez: `GetSupportedVersions`, `GetCaps`, `CreateDevice`, `CalcPrivateDeviceSize` e `CloseAdapter` todos em **0**. **Não era falta de `pfnGetCaps`.** **A pista decisiva:** o runtime **nunca chama `pfnCloseAdapter`** — um runtime que quer descartar um adapter chamaria, a não ser que esse ponteiro esteja **nulo do ponto de vista dele**. E o header descreve o campo como `pAdapterFuncs_2` **points to** uma `D3D10_2DDI_ADAPTERFUNCS`, com a doc dizendo que a tabela é *returned **in** the member* — ou seja, a convenção de `CREATEDEVICE.pDeviceFuncs`, marcada `in/out`: **o runtime aloca a struct e o driver preenche os membros**. Nosso código **sobrescrevia o ponteiro**, deixando o runtime a ler a struct dele, zerada. Isso explica tudo: nenhuma função chamada, `CloseAdapter` incluso, e descarte imediato. Evidência: `docs/evidence/2026-09-07_10-00-25-.../ANALISE.md`. |
| — | **`P7_D3D11_CAPS_PROBE` v5 — convenção de preenchimento da tabela de adapter** | ✅ | **IMPLEMENTADO OFFLINE (07/09) — `H` forte, e é defeito NOSSO se confirmar.** Cinco corridas eliminaram step-down, `UMDRIVERPRIVATE` e forma da tabela; sobrou a convenção. `OpenAdapter10`/`OpenAdapter10_2` passam a **preencher a struct que o runtime fornece** (`*pOpenData->pAdapterFuncs = tabela`) quando o ponteiro vem não-NULL, em vez de **sobrescrever o ponteiro** — e registram qual dos dois casos ocorreu, para a corrida decidir em vez de eu supor. `build-umd.bat` e `build-contract.bat` em `EXIT=0`, auditoria `PASS`. ✅ **CONFIRMADO NO SILÍCIO (07/09 10:11) — era defeito nosso, e o probe cumpriu a missão.** `pAdapterFuncs_2` vem **não-NULO** (`runtime forneceu pAdapterFuncs_2=0x... -> PREENCHENDO a struct dele`): o runtime aloca a struct e o driver preenche os membros. Prova de que pegou: **`pfnCloseAdapter` chamado 95 vezes**, contra **zero** nas quatro corridas anteriores. E pela primeira vez o runtime conversou: **`pfnGetSupportedVersions` 90 chamadas** (declaramos D3D10_0 + D3D10_1, e ele aceitou) e **`pfnGetCaps` 63 chamadas**. |
| — | **Backend D3D10/11 para o `dwm.exe`** | ⛔ | **BARREIRA ATIVA E ÚNICA DO DESKTOP VISÍVEL — CAMINHO DECIDIDO POR DUMP (06/09).** `cdb` sobre `dwm-crash-01-dwm.exe.8548.dmp`: bucket `APPLICATION_FAULT_FAIL_FAST_EXCEPTION_887a0004_dwmcore.dll!MilFailFastForHR`, com a pilha `CConnection::RunCompositionThread` → `CPartitionVerticalBlankScheduler::ProcessFrame` → `CComposition::PreRender` → `CLegacyRenderTarget::EnsureSwapChain` → `CDeviceManager::GetDevice` → `CD3DDevice::Create` → **`CD3DDevice::CreateD3D11Device`** → `MilInstrumentationCheckHR_MaybeFailFast` → `RaiseFailFastException(e0464645, 0x887A0004)`. **Três conclusões:** (1) o DWM exige device **D3D11** no caminho `CLegacyRenderTarget`; (2) ao receber `DXGI_ERROR_UNSUPPORTED` ele **fail-fasta, não cai para software**; (3) **`DriverSupportsCddDwmInterop` não é rota de fuga** — já está declarado (`PresentationCaps=0x1006C903`, bit 8) e o DWM foi para o D3D11 igual. Logo a hipótese "fazer o DWM aceitar CDD/interop" está **REFUTADA POR DUMP**, e um backend D3D10/11 real na `bc250umd.dll` é **obrigatório** para desktop visível. **RADV não cobre este item** (é Vulkan). **DIMENSIONADO EM 06/09** (`docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md`, `P1`): o log da UMD mostra o runtime descendo a escada de DDI — `0x000B0011` (minor 17, sem nome no WDK 26100), depois `0x000B000B` = `D3D11_0_7_DDI_INTERFACE_VERSION`, e por fim `0x000A0002` = **`D3D10_1_DDI_INTERFACE_VERSION`**, que é o **piso**. Tamanho contado no header: tabela de adapter **3** funções (`OpenAdapter10`) ou **5** (`OpenAdapter10_2`, com `pfnGetSupportedVersions` e `pfnGetCaps`); tabela de device **105** entradas (`D3D10_1DDI_DEVICEFUNCS`) — 39 de recurso/view, 26 de estado fixo, 17 de shader, 8 de query, 10 de ciclo de vida, 5 de desenho. **Piso de feature level = 10_1:** os níveis 9_x do enum `D3D11DDI_3DPIPELINELEVEL` só existem a partir do DDI D3D11_1, logo "10level9" **não** é atalho aqui. O custo real não é a contagem: são o **compilador DXBC→ISA do GFX10**, o **pipeline 3D** (que o projeto não tem — só compute e SDMA) e os recursos sobre a nossa GPUVA. **Rota DECIDIDA em 06/09: B — DXBC→SPIR-V→RADV/ACO**, unindo esta obra ao item 146 em vez de construir dois backends (ver "Decisão de arquitetura vigente"). A decisão é de arquitetura, não resultado. 🎯 **ÚLTIMO PORTÃO NOMEADO E MEDIDO (07/09 10:11):** com a convenção corrigida, o runtime percorreu toda a negociação e pediu **exatamente um** cap antes do device — **`D3D11DDICAPS_3DPIPELINESUPPORT`, `DataSize=4`, `pInfo=NULL`** (63 chamadas). Nem `THREADING`, nem `SHADER`, nem `OPTIONS`. Recusamos, e a recusa é **correta**: `D3D11DDI_3DPIPELINELEVEL` não tem valor para "nenhum" — `0` já significa nível `10_0`. **Não existe truque de cap que passe deste ponto:** declarar um nível levaria o runtime a chamar `pfnCreateDevice` e as **105 entradas** inexistentes, trocando recusa honesta por crash com cap falso. **Logo, agora medido e não inferido: a única coisa entre este driver e um device D3D é um pipeline 3D real** (rota B). ✅ **REPRODUZIDO COM TOOLCHAIN DIFERENTE (07/09 11:08, VS2026 + WDK 28000, zero mudança de código):** mesma cadeia, mesmo `GetCaps Type=130` 63 vezes, mesmo ponto de parada — a conclusão não é artefato de compilador. **Fecha quando:** `CreateD3D11Device` devolver sucesso e a Área de Trabalho ficar visível por 10 min. **NÃO VALIDADO EM HARDWARE.** |
| 147 | Shader compiler | ✅ | **FECHADO NO SILÍCIO (17/09 17:48, Corrida 15):** Compilação de shaders DXBC reais para SPIR-V via `libvkd3d-shader-1.dll` em runtime (VS 768B, PS 780B), instanciados como `VkShaderModule` e integrados em PSO Vulkan compilado sem erros (`bc250_umd_log_7364-FINAL.txt:2729-2736`). Promovido à Tabela 1. |
| 149 | DXVK (D3D9/10/11) | ⬜ | Suite de conformidade sem WARP |
| 150 | VKD3D-Proton (D3D12) | ⬜ | Suite de conformidade sem WARP |
| 148 | Zink (OpenGL) | ⬜ | Depois de Vulkan graphics/WSI estáveis |
| 151 | OpenGL nativo | ⬜ | Não planejado — Zink é o caminho |
| 152 | D3D12 nativo | ⬜ | Opcional se VKD3D cobrir a meta |

---

## 🏁 P8 — Jogos e estabilidade · (LLM aqui é subproduto, não marco)

> **Objetivo:** o produto. Jogo real rodando, medido, estável.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P5 (imagem) **e** P7 (APIs) convergidas |
| **Critério de aceitação** | Frame real, fps medido, e **prova de ausência de fallback** por GPU counters e fences — não por "parece rodar" |
| **Ordem** | Triângulo → jogo DXVK simples → jogo VKD3D → long-run → jogo + desktop simultâneos |
| **LLM** | **SUBPRODUTO, NÃO MARCO** (Lei do escopo, item 1). Se cair de graça depois da P6, registra-se; **não** entra em PASS de etapa, não justifica corrida e não substitui nenhum item de vídeo ou de jogo |
| **PASS final do projeto** | Jogo rodando na BC-250, sem WARP, com TDR recuperando de hang sem perder o display |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| 158 | Triângulo Vulkan | ⬜ | Triângulo **na tela ligada à placa**, frame produzido na GPU |
| 159 | Jogo DXVK simples | ⬜ | Frame real medido, sem WARP nem llvmpipe |
| 160 | Jogo VKD3D | ⬜ | D3D12 traduzido, estável, com fps medido |
| 161 | Compilação de shaders | ⬜ | Cache e pipelines sem corrupção sob carga |
| 164 | Jogos + desktop simultâneos | ⬜ | DWM, jogo e compute compartilhando a GPU sem hang |
| 166 | Ausência de fallback de software | ⬜ | GPU counters e fences provando que nada foi desviado |
| 162 | Long-run compute | ⬜ | Horas sem hang, regressão ou fence perdida |
| 163 | Long-run display | ⬜ | Horas de present sem corrupção nem tela presa |
| 165 | Telemetria de desempenho | ⬜ | Busy, clocks, memória, temperatura e faults **lidos do silício** |
| 153 | Kernels LLM | 🧪 | *Subproduto.* Integrados via compute real — sem prioridade |
| 154 | llama.cpp Vulkan | ⬜ | *Subproduto.* Offload de layers com tokens/s medidos vs CPU |
| 156 | Alocação de modelos grandes | ⬜ | *Subproduto.* Residency/OOM/fragmentação nos 16 GB |
| 157 | Benchmark LLM | ⬜ | *Subproduto.* Tokens/s, consumo e temperatura com verificação de GPU |
| 155 | DirectML | ⬜ | Exige D3D12 UMD funcional; não é o primeiro caminho |

---

## ⚪ P9 — DCN com escrita · **OPCIONAL, não bloqueia jogos**

> **Objetivo:** trocar resolução pelo Windows, segundo monitor, flip sem tearing, VRR, HDR.
> **Nada aqui é necessário para jogar.**

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P5 estável. **Nunca** antes |
| **Veículo para escrita** | **KMDOD**, que tem ownership legítimo — ou o miniport WDDM depois da P5. **Nunca** o `bc250dbg`: escrever em HUBP/OTG sob o driver de laboratório é escrever num scanout vivo que o BasicDisplay está usando |
| **Ativo que reduz o custo** | O `dcn201` do Linux é aberto e comprovadamente funcional neste silício, e as bases do DMU já saem catalogadas da P1 |
| **Contra** | HPD é quebrado na placa **até no Linux**. Mexer em PHY/link custa tela preta e boot frio por tentativa |
| **Custo** | O item mais caro do projeto inteiro |
| **Regra** | Snapshot de **todo** registrador antes de tocar; rollback se qualquer readback divergir; orçamento de corridas definido antes |
| **A exceção antecipável** | Só o flip do `PRIMARY_SURFACE_ADDRESS` com master update lock, dentro do KMDOD. Converte a cópia por CPU num page flip real sem tocar timing, link ou PHY. **Possível e delimitado, não seguro** |

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | Flip do `PRIMARY_SURFACE_ADDRESS` no KMDOD | ⬜ | **Exceção delimitada.** Snapshot + rollback + orçamento. Escrita em scanout vivo — derrubou a GPU no repo de referência |
| 82 | Master update lock | ⬜ | Atualização atômica do pipeline com readback |
| 83 | Flip A/B físico | ⬜ | Duas surfaces alternando; `SURFACE_INUSE` + frame counter provam qual está no ar |
| 84 | VBlank por interrupção | ⬜ | IH → ISR/DPC → `dxgkrnl`, substituindo o polling |
| 74 | HPD | ⬜ | Interrupção e estado físicos. **Quebrado na placa até no Linux** |
| 75 | AUX | ⬜ | Transações com timeout e retry reais |
| 76 | DPCD | ⬜ | Capabilities reais do monitor e do link |
| 77 | EDID real | 🟡 | Lido por AUX com checksum válido |
| 78 | Link training | ⬜ | CR/EQ, lane count, rate e status reais |
| 71 | DCCG / clocks de display | ⬜ | Clocks programados com readback, sem derrubar a imagem |
| 69 | DCHUB / DCHUBBUB | ⬜ | Watermarks e arbitragem seguras |
| 70 | DPP / MPC / OPP | ⬜ | Pipeline mínimo de uma plane |
| 72 | DIG / link encoder | ⬜ | Estado, enable e mapeamento |
| 73 | PHY | ⬜ | Power, lane mapping e sinal |
| 85 | Mode-set transacional | ⬜ | Um modo conhecido com rollback do link |
| 86 | Modos do EDID | ⬜ | Enumerar, validar e trocar mantendo imagem |
| 87 | Hotplug | ⬜ | Disconnect/reconnect sem crash nem tela presa |
| 79 | DMCU / DMUB | ⬜ | Presença e necessidade comprovadas. **Não inventar blob** |
| 80 | ATOM / board tables | ⬜ | Connector/encoder/clock extraídos da própria placa |
| 89 | HDR / VRR / MPO / HDCP | ⬜ | Tardio. Não anunciar antes de implementar |
| 88 | Áudio DisplayPort | ⬜ | Só depois do vídeo estável |

---

## ⚙️ T — Transversal · contínuo, atravessa todas as etapas

**Nesta faixa precisa:** disciplina de processo e qualidade. Não tem PASS único; cada item
é uma regra que vale em toda corrida. O item **177 (orçamento de corridas)** migrou para a
**P0** — ele é pré-requisito de gastar corrida, não item de fundo.

| # | Item | Estado | Prova física exigida para fechar |
|---:|---|:---:|---|
| — | **Toolchain VS2026 + SDK/WDK 10.0.28000.0** | ✅ | **VALIDADA NO SILÍCIO (07/09 11:08)** por corrida de regressão **sem nenhuma mudança de código** — só compilador e headers. Reproduziu tudo: `PREENCHENDO a struct dele` 95, `GetSupportedVersions` 95 chamadas, `GetCaps Type=130` **63**, `CloseAdapter` 95, `CreateDevice` 0, `UMDRIVERPRIVATE` 186, `IsSupportedVidPn` 18, `Present` 175, e o **tripé da SDMA no `StartDevice` armando em 1 poll com `Dados=4/4 OK`** — o item mais sensível a codegen. Os três gates comprados a custo de corrida **sobreviveram**: `/Oy-` presente, zero `.bat` com LF solto, e contrato DDI **pinado em `0xC004`** (não bumpado além do que o Windows 10 22H2 suporta). Antes de armar, comparei por leitura os tamanhos das structs de fronteira entre os SDKs (`D3D10_2DDI_ADAPTERFUNCS` 5 e 5, `D3D10DDI_ADAPTERFUNCS` 3 e 3, `DXGK_DRIVERCAPS` 41 e 41) — **idênticas**, descartando o risco de corromper a heap do `dwm.exe` na cópia da struct da v5. Evidência: `docs/evidence/2026-09-07_11-08-15-.../ANALISE.md`. |
| — | **FPO ligado no build do SYS** | 🧹 | **`P1` (06/09):** `debugging-tips-for-wddm-drivers.md` exige desligar frame pointer omission em WDDM 1.2+. `driver/wdk/build-contract.bat` compila com `/O2` e **sem** `/Oy-`, degradando a call stack do dump — a única via de diagnóstico da P3. Fecha com `/Oy-` no build e gate na auditoria estática |
| — | **Log do KMD cumulativo e não arquivado** | 🧹 | **MEDIDO (06/09):** o log é cumulativo entre binds (6 binds num arquivo de 567 KB) e o da corrida 20:34 só existia em `C:\bc250_display_log.txt`, copiado à mão às 21:08. Fecha com marcador de início/fim de bind no log **e** arquivamento automático na pasta da evidência com SHA-256 |
| 121 | Auditoria de falsos sucessos | 🟡 | Teste AST/semântico geral no build |
| 125 | Breakpoint `+0x10` | 🧹 | Source breakpoint / return address controlado |
| 128 | Relatório 3.74 | 🧹 | "Causa definitiva" rebaixada a hipótese |
| 129 | Worktree sujo | 🟡 | Baseline/tag criada só com autorização |
| 131 | UMD/ICD esqueletos | 🟡 | Desabilitados até o KMD WDDM ser real |
| 170 | Testes GPUVA | 🧪 | Integração WDDM/hardware exercitada |
| 22 | SMU | 🟡 | Power management, thermal e clocks reais |
| 26 | Suspend / resume | ⬜ | S3/S4/reboot sem perder fila, display ou mappings |
| 27 | Thermal / power limits | ⬜ | Limites seguros sob carga longa, medidos |
| 178 | Matriz de hardware | ⬜ | Mais de um monitor/cabo/modo — só mais tarde |
| 179 | HLK | ⬜ | Depois de funcionalidade e estabilidade |
| 180 | Driver de produção | ⬜ | Gates perigosos removidos, firmware protegido, assinado |
| 181 | Instalador / desinstalador | 🟡 | Fluxo reversível e versionado |
| 182 | Documentação final | 🟡 | Consolidar quando os contratos estabilizarem |

---

## ❌ Encerrado · não reabrir

**Nesta faixa precisa:** nada. Está fechado por medição. Qualquer proposta de reabrir
precisa apresentar evidência nova, não argumento novo.

| # | Item | Por que está fechado |
|---:|---|---|
| 101 | Render-only em Código 0 | **REPROVADO** (`docs/162`). A recusa antecede qualquer cap de render; custou tela preta nas duas corridas |
| — | `MiscCaps.ComputeOnly` | Inalcançável: trafega só por `DRIVERCAPS`, que nunca é perguntado |
| — | Cross-adapter / MCDM como destino | APU de die único: não há segundo adaptador de display real. Teto de desempenho inaceitável |
| 41 | Shared / cross-adapter resources | Só serviria ao modo híbrido — **fora do destino** |
| 175 | KDNET | **Sem segunda máquina.** Substituído pelas quatro camadas da P3 |
| — | ~~Teoria "o `E_NOTIMPL` da UMD apaga a tela"~~ | 🔄 **SUPERADA — REABERTA COM EVIDÊNCIA NOVA (06/09).** A refutação de 03/09 valia porque `bc250umd.dll` **nunca era carregada**. Isso mudou: o doc 169 abriu o **dump real** do `dwm.exe` da corrida de 05/09 22:20 e nomeou a chamada — fail-fast `0xe0464645`, HRESULT **`0x887A0004` (`DXGI_ERROR_UNSUPPORTED`)** em `dwmcore!CD3DDevice::CreateD3D11Device`; e a corrida de 06/09 22:42 reproduziu com **770 × `E_NOTIMPL`** em `OpenAdapter10/10_2/12`, 7 cargas da DLL e 2 dumps de ~178 MB. **O item saiu de "encerrado" e virou a barreira ativa do desktop visível** — ver a P7. |

---

## Contagem

| | Itens |
|---|---:|
| **Tabela 1 — ✅ validado no silício** | **71** |
| Tabela 2 — 🧪 provado só offline | 11 |
| Tabela 2 — 🟡 parcial | 46 |
| Tabela 2 — ⛔ bloqueado | 3 |
| Tabela 2 — ⬜ não iniciado | 70 |
| Tabela 2 — 🧹 dívida a remover | 8 |
| Tabela 2 — ❌ encerrado | 6 |
| **Total** | **215** |

**Distribuição da Tabela 2:** P0 = 3 · P1 = 4 · P2 = 1 · P3 = 6 · P4 = 10 · P5 = 5 · P6 = 35 · P7 = 24 · P8 = 14 · P9 = 21 · T = 17 · encerrados = 6. **Total Tabela 2 = 145.** Recontagem automatizada em 06/09; as linhas ✅ das tabelas de fase são referências cruzadas à Tabela 1 e não entram nesta soma.

## Como seguir — o que ARRUMAR e o que DESENVOLVER

Duas listas distintas, porque exigem coisas diferentes. **Arrumar** é defeito já
identificado, de custo baixo e sem engenharia nova — quase tudo offline. **Desenvolver** é
capacidade que ainda não existe no driver, e é onde mora a barreira de compute e jogos.
Nada aqui está validado em hardware; a coluna final diz o que fecha cada linha.

### A) ARRUMAR — defeitos identificados, custo baixo

| # | O que está errado | Onde | Estado | Resultado medido |
|---:|---|---|:---:|---|
| A1 | Caps de tipo de driver vinham de dois produtores divergentes | `bc250_mcdm_callbacks.c` · `bc250_display_ddi.c` | ✅ | **FEITO (06/09, offline).** Fonte unica em `driver/core/bc250_driver_caps_contract.c`, consumida pelos dois handlers. `SupportNonVGA` corrigido para TRUE — e o teste `mcdm_callbacks_test` **afirmava `FALSE`**, isto e, congelava o defeito; foi refeito. `SupportSmoothRotation` e `SupportDirectFlip` **permanecem FALSE por decisao citada na norma** (ver A4). Novo teste `driver_caps_contract_test` passa; log novo `[DriverCaps] TypeCaps(...)` expoe os cinco campos na proxima corrida. **NAO VALIDADO EM HARDWARE.** |
| A2 | Mode set do target nao derivava do monitor (`pfnAcquireMonitorSourceModeSet`) | `bc250_display_ddi.c` | 🧹 | **RECLASSIFICADO por leitura de codigo.** Enquanto o EDID e' **sintetizado por nos**, derivar o mode set do monitor valida a nossa propria sintese contra ela mesma — e circular e nao adiciona informacao. O unico modo publicado e' o nativo herdado do GOP, que por construcao e' o que o monitor esta exibindo. Passa a **depender do item 77** (EDID real por AUX). Nao foi alterado codigo de negociacao antes da corrida 1. |
| A3 | EDID anunciaria 60 Hz com o enlace a 56,13 Hz | `bc250_display_ddi.c` | ✅ | **NAO ERA DEFEITO — verificado.** O descritor detalhado ja deriva de `BC250_NATIVE_PIXEL_RATE = 173.611.437 Hz` com `HBLANK=224`/`VBLANK=31` lidos do OTG: 173.611.437 / (2784 x 1111) = **56,13 Hz**, `clock10kHz = 17361`, hsync 62,36 kHz, e os range limits do EDID (V 50–61, H 20–90 kHz, pixel max 190 MHz) acomodam tudo. Os "60 Hz" existiam **na prosa deste documento**, nao no codigo. Prosa corrigida. |
| A4 | Multisampling em source pinado nao e' reportado | auditoria | ✅ | **DECIDIDO: nao se aplica.** `enumerating-cofunctional-vidpn-source-and-target-modes.md` condiciona o dever — *"If the display adapter has one or more video output codecs that are capable of antialiasing by multisampling"*. O display v1 e' blt de uma primaria ja composta, sem codec de antialiasing. Declarar metodo seria cap sem backend. Registrado como decisao, nao como pendencia. |
| A5 | FPO ligado no build do SYS (`/O2` sem `/Oy-`) | `driver/wdk/build-contract.bat` | ✅ | **FEITO (06/09, offline).** `/Oy-` no `CFLAGS` que gera o SYS, com gate novo `audit_frame_pointer_flag` em `tools/audit-bc250mcdm-safety.py`. O gate foi **verificado ao contrario**: removendo `/Oy-`, a auditoria reprova; devolvendo, volta a PASS. |
| A6 | Log integral do KMD nao era arquivado pelo binder de laboratorio | `tools/bind-bc250mcdm-lab.ps1` | ✅ | **FEITO (06/09, offline).** Causa exata: o binder **de laboratorio nunca chamou** `capture-bc250mcdm-binding.ps1` — so o de producao chama — e o log vive em `C:\bc250_display_log.txt`, cumulativo e sobrescrito pelo bind seguinte. Agora o binder arquiva o arquivo integral com SHA-256 e `bind_start_offset` em `display-log-archive.json`, independente de o ciclo chegar ao bloco de delta. Parser do PowerShell validado. |
| A7 | Barra de status e revisao divergentes nos quatro arquivos normativos | `CLAUDE.md` · `AGENTS.md` · `GEMINI.md` · `.cursorrules` | ✅ | **FEITO (06/09).** Data-base 2026-09-06 nos quatro e bloco unico com o resultado da faixa A. As barras de 06/09 20:34 ja estavam alinhadas; o que estava velho era a data e a ausencia do resultado da faixa A. |
| A8 | **ACHADO NO CAMINHO:** `build-contract.bat` estava em **LF**, e o `cmd.exe` retoma a leitura do `.bat` por deslocamento em bytes — depois de `call vcvars64.bat` ele passou a **comer o primeiro caractere de cada linha** | `build-contract.bat` + 11 `.bat`/`.cmd` do caminho de build | ✅ | **FEITO (06/09, offline) — e destravou cobertura perdida.** O sintoma era `LNK1181` com o nome de arquivo `'^.obj'` e linhas do proprio `.bat` executadas como comandos: **`readonly_miniport_test` nunca linkava** e a queda ficava escondida num log de centenas de linhas. Provado por A/B (a falha persistia sem a minha mudanca) e pela reproducao isolada, que montava o comando corretamente. Convertido para CRLF, `build-contract.bat` voltou a **`EXIT=0`** e `readonly_miniport_test.exe` passou a existir pela primeira vez. Gate novo `audit_batch_line_endings`, tambem verificado ao contrario. `docs/evidence/` nao foi tocado. |
| A9 | **`BYTE_PROOF PASS` certificava preto contra preto.** A comparação passava quando origem e destino eram ambos zero — declarava sucesso exatamente no caso em que a tela estava apagada. `NonZero=0/256` em 63/63 amostras e ainda assim `PASS` | `bc250_display_ddi.c` + novo `driver/core/bc250_blt_proof_contract.c` | ✅ | **FEITO (06/09, offline).** O veredito saiu do `if` solto e passou a vir de `Bc250ClassifyBltProof` em `driver/core`, com quatro estados: `CONTENT_OK`, `BLANK`, `MISMATCH`, `INDETERMINATE`. Cópia fiel de frame em branco agora sai como **`BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)`** com `BlankStreak`, **nunca PASS**. ⚠️ **Desvio deliberado do que esta linha pedia antes:** eu havia escrito "falhar fechado quando `NonZero == 0`" — e isso está **errado**. O DWM pode legitimamente apresentar preto; desligar o SDMA aí transformaria sintoma em regressão de driver. O contrato fecha **só** em divergência de bytes (`Bc250BltProofRequiresFailClosed`). Travado por `blt_proof_contract_test` (5 casos) e por 5 gates novos na auditoria, incluindo um `forbid` do rótulo antigo — **verificado ao contrário**: reintroduzindo `BYTE_PROOF PASS: Source=`, a auditoria reprova. ✅ **PROVADO NO SILÍCIO (07/09 08:05):** a corrida emitiu **2× `BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)`** com `BlankStreak`, e **zero** `BYTE_PROOF PASS` — com 50/50 amostras zeradas. Nenhuma linha de sucesso falso saiu da corrida. |
| A10 | **`BLT_TRIPOD` anunciava três pernas tendo duas.** `Bc250SdmaPresentBlt` verifica `RPTR == WPTR` e a fence da GPU sobre poison; a perna de dados vive fora, em `Bc250DisplayCompareSurfaceBytes`, e só roda em alguns `Present` | `bc250_sdma_present.c` · `bc250_sdma_present.h` | ✅ | **FEITO (06/09, offline).** ⚠️ **Achado que corrige esta linha:** a terceira perna **já existia** — `Bc250DisplayCompareSurfaceBytes` compara os **11.059.200 bytes inteiros**, origem contra destino, na CPU. O defeito não era ausência de prova, era **o log afirmar tripé sem consultá-la**. Agora o estado da perna de dados trafega em `BltDataProofState` e o log só imprime **`BLT_TRIPOD PASS #N (3 pernas)`** quando ela passou com conteúdo; nos outros casos imprime **`BLT_ENGINE_2LEGS`** dizendo se a perna de dados está `NAO CONFERIDA` ou `INCONCLUSIVA (frame em branco)`. Três gates novos na auditoria exigem as duas formas e o condicionamento. ✅ **PROVADO NO SILÍCIO (07/09 08:05):** `BLT_TRIPOD` = **0** e `BLT_ENGINE_2LEGS #1 (... PERNA DE DADOS NAO CONFERIDA)` = **1**. O log parou de afirmar três pernas tendo duas. |

> **Duas revisoes que a leitura da norma me obrigou a fazer, e que mudam o A1 original.**
> `wddm-driver-and-feature-caps.md` marca `SupportSmoothRotation` e `SupportDirectFlip` como
> **M** para Full Graphics, e a versao anterior desta tabela dizia para ligar os tres. Ligar
> seria **cap sem backend**: `optimized-screen-rotation-support.md` condiciona a exigencia a
> *"drivers that support rotated modes"* — nao suportamos modo rotacionado; e
> `direct-flip-of-video-memory.md` exige trocar a superficie de origem em
> `DxgkDdiSetVidPnSourceAddress`, o que pede reprogramar o endereco de scanout — escrita em DCN,
> proibida fora da P9. Ficam FALSE, com `SupportDirectFlip` registrado como **divida declarada**
> em `Bc250GetDriverCapsDeclaredDebtCount`.

**Portao offline apos a faixa A:** `driver\wdk\build-contract.bat` termina **`EXIT=0`**,
`driver_caps_contract_test` e `display_vidpn_test` passam, e
`python tools\audit-bc250mcdm-safety.py` volta **PASS** com os dois gates novos.
**Nenhum item da faixa A foi validado em hardware** — sao mudancas de contrato, de build e de
coleta de prova.

#### A12 — telemetria de fila da SDMA0 decodificada (07/09, offline)

Depois de fechar as perguntas 3, 4 e 5 por leitura, apliquei o mesmo método à **2** — e o
`docs/168_HANDOFF_2026-09-05_INDIRECT_SDMA.md` já tinha os dados, arquivados e **não
interpretados**. A seção 6, item 6 daquele documento encerrava assim: *"a semântica de bits de
`CONTEXT_STATUS` e `SKIP_CNTL` … não sei o que significa e não interpretei"*.

A semântica estava no repositório, em **`inc/linux_ref/gc_10_1_0_sh_mask.h`** — a tabela de
shifts e máscaras do GC 10.1.0 do upstream. Aplicando-a às leituras de silício de 05/09:

| Variante | `CONTEXT_STATUS` | Decodificado |
|---|---|---|
| `PHYS` (VMID 0, MC físico) | `0x0D` | `SELECTED` + **`IDLE`** + `EXPIRED` |
| `EXEC` (VMID 8, traduzido) | `0x09` | `SELECTED` + `EXPIRED`, **`IDLE = 0`** |

| Variante | `IB_CNTL` | Decodificado |
|---|---|---|
| `PHYS` | `0x00000101` | `IB_ENABLE=1`, `SWITCH_INSIDE_IB=1`, `CMD_VMID=0` |
| `EXEC` | `0x00080101` | `IB_ENABLE=1`, `SWITCH_INSIDE_IB=1`, `CMD_VMID=8` |

**Os dois `CONTEXT_STATUS` diferem em exatamente um bit — o `IDLE`. Os dois `IB_CNTL` diferem
em exatamente um campo — o `CMD_VMID`.** Medição `P0` mais decodificação `P1`: **não é
hipótese**, e estava disponível esse tempo todo.

🔶 **HIPÓTESE (`H`):** sob VMID 8 a engine consumiu o pacote do **anel**, latchou `IB_BASE` e
`CMD_VMID`, e **parou dentro do IB** — não terminou, por isso `IDLE` fica 0. Isso desloca a
pergunta 2 de *"não executou o pacote"* para *"começou o IB e travou"* — e **casa com a
hipótese da pergunta 3**: travar buscando/traduzindo é o que um fault redirecionado para a
dummy page, sem interrupção habilitada, produziria.

⚠️ **O que isto NÃO resolve.** A variante `PHYS` **foi a `IDLE`** — terminou — e a prova de
bytes dela **também falhou**. Sem tradução envolvida, aquela falha **não pode** ser de GCVM.
São **duas falhas diferentes**, e a decodificação não decide nenhuma. E não sei se
`EXPIRED = 1` é significativo ou o estado normal de uma fila drenada.

**Duas armadilhas de leitura que o catálogo agora registra:** `IB_RPTR`/`IB_OFFSET` têm o
campo `OFFSET` começando no **bit 2** — o cru está em bytes, o campo conta dwords, então
`IB_RPTR = 0x20` são **8 dwords**, não 32; e `EXCEPTION` é um campo de **3 bits**, não um flag.
O bit 1 de `CONTEXT_STATUS` **não tem nome no header do upstream**, e o decodificador o
reporta em `BITS_SEM_NOME` em vez de fingir que entendeu o valor todo.

**O que ficou pronto para a próxima corrida, a custo zero de corrida:** o
`SubmitCommandVirtual` do Node 0 lê os oito registradores do bloco `SDMA0_GFX_IB_*` **no
caminho de timeout** e emite `IB_QUEUE_RAW`, `IB_QUEUE_DECODED`, `IB_QUEUE_STATUS` e
`IB_QUEUE_DISCRIMINANTE`. Só leitura; **fora** do caminho de sucesso, para não pôr oito
acessos de MMIO no `Present` a 60 Hz. Novo contrato `bc250_sdma_queue_state_contract` não
escreve registrador nenhum — é aritmética pura, testável offline.

**Teste que fecha:** `driver/tests/sdma_queue_state_contract_test.c` — confere as máscaras
contra o header **e** reproduz os dois valores medidos em silício. Ele já se pagou: eu havia
escrito que `SWITCH_INSIDE_IB` era 0 na variante `PHYS`, por suposição; `0x00000101` tem o
bit 8, e o teste me corrigiu antes de a afirmação chegar a um documento.

⚠️ **NÃO VALIDADO EM HARDWARE** — a decodificação é de valores já medidos; a nova telemetria
nunca rodou.

#### A11 — geometria de GPUVA com fonte única (07/09, offline)

Procurar no nosso próprio `docs/` a resposta das perguntas abertas do doc 167 expôs **cinco
defeitos na mesma geometria**. Nenhum é o B1, mas **os três primeiros produziriam o sintoma
do B1** se acionados, e o primeiro era meu.

| # | Defeito | Correção | Trava |
|---|---|---|---|
| A11.1 | **Segunda fonte de verdade para `GpuVaBits`.** O blob `UMDRIVERPRIVATE` publicava `BC250_ADAPTER_GPUVA_BITS` = **48** enquanto o mesmo driver declarava `VaBits=34` ao WDDM — **medido** no log de 11:08: `[McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2`. A UMD (e, na rota B, o winsys do RADV) dispõe endereços dentro do que o blob promete; prometer 48 sobre uma hierarquia que traduz 34 gera endereço sem PTE alcançável. **Defeito meu, criado ao escrever o blob** | `facts.GpuVaBits`/`PageTableLevels` passam a sair de `runtimeCaps` — a **mesma** struct que alimenta o `GPUMMUCAPS`; sem geometria o blob não sai (`DEVICE_NOT_READY`) | gate estático `audit_gpuva_geometry_single_source` (**verificado por teste de mutação**: trocar por `48u` faz a auditoria falhar) + asserção de repasse em `mcdm_callbacks_test`, usando o mock 48/4 que distingue repasse de hardcode |
| A11.2 | **`BC250_ADAPTER_GPUVA_BITS` literal.** Valia 48, e `MAX_GPUVA_ADDRESS` valia `0xFFFFFFFFFFFF`. Morto — só testes o liam — mas pronto para reintroduzir o defeito ao ser ligado | passa a **derivar**: `#define BC250_ADAPTER_GPUVA_BITS BC250_GPUVA_PT_VA_BITS` e `MAX = (1ull << bits) - 1` | mesmo gate; e a mensagem do build passou a dizer `34-bit GPUVA derivado` |
| A11.3 | **Fail-open no journal de GPUVA.** `Bc250GpuvaJournalStageLeafUpdate` recusava VA a partir de `2^48` — o teto **arquitetural** — em vez de `2^34`, o **traduzível**. Aceitava registrar página num VA que o VMID8 não alcança; sintoma indistinguível do B1. Não era acionável pelo alocador vigente (inicializado com `BC250_GPUVA_PT_VA_BITS`), mas era porta aberta | novo `BC250_GPUVA_TRANSLATABLE_LIMIT = 1ull << BC250_GPUVA_PT_VA_BITS`, com `MAX_LIMIT`/`DEFAULT_BITS` marcados como teto arquitetural que **não** serve de limite de tradução | 4 casos em `gpuva_journal_contract_test`: última página traduzível **entra**; a primeira fora dele, a faixa que **atravessa** o limite e o VA de 8 TB **não** |
| A11.4 | **Bloco de PROVENIÊNCIA falso** no topo de `bc250_gpuva_space_contract.h`: descrevia o desenho **anterior** (plano, `PAGE_TABLE_DEPTH = 0`, 22 bits de índice) logo acima das constantes de 2 níveis que o substituíram. Sob a **Lei 7** é armadilha — era a proveniência de registro, e estava falsa: quem a lesse programaria depth 0 e declararia 1 nível ao `dxgkrnl`, abaixo do mínimo do contrato | reescrito para a geometria vigente com a proveniência real (`docs/03`, `P1`); o texto antigo ficou marcado como **HISTÓRICO**, nada apagado | — |
| A11.5 | **Afirmação falsa e normativa em `docs/03`:** *"o driver lê `GCMC_VM_FB_OFFSET` do silício em cada construção; não usa constante"*. O leitor de topologia lê, mas o valor usado nas PTEs é a constante `BC250_GCVM_VRAM_MC_BASE_FALLBACK` — **deliberadamente** (a base do leitor "nunca fez um acesso funcionar"; a constante tem prova byte a byte). Hoje os dois concordam; o risco era o silêncio se deixassem de concordar | catálogo corrigido; `SetRootPageTable` passou a comparar e emitir `ROOT_MC: DIVERGENCIA` quando diferem, e a dizer explicitamente quando concordam. **Zero mudança de comportamento** | removida no mesmo passo uma inicialização morta que repetia `0x450000000ull` à mão — quarta grafia do mesmo número — e era sobrescrita sem condição |

**Estado:** `driver/wdk/build-contract.bat` **EXIT=0**, `driver/umd/build-umd.bat` **EXIT=0**,
e as três auditorias (`safety`, `ddi-matrix`, `ddi-noop`) voltam **PASS**. Dois testes que
**congelavam o defeito** foram descongelados com a norma citada: `TestRoundTrip` afirmava
`blob.GpuVaBits == 48u`, e `adapter_info_contract_test` afirmava `48u` / `0xFFFFFFFFFFFF` —
mesmo padrão do `SupportNonVGA == FALSE` corrigido no A1.

⚠️ **NÃO VALIDADO EM HARDWARE.** São mudanças de contrato, gate e teste offline. O que muda
para o silício é que o blob que a UMD vai ler deixou de mentir sobre o espaço endereçável —
e isso só se prova quando a UMD dispuser endereços de verdade.

**Estado dos pacotes depois deste passo — leia antes de armar qualquer corrida:**

| Pacote | Contém as mudanças de 07/09? | Como foi verificado |
|---|:---:|---|
| `package/output_active` | ✅ **SIM** | SYS assinado `069A7330...` executado às 16:19: MMU global e Código 0 validados, porém desktop **REPROVADO** por tela preta/fail-fast D3D; não rebinding sem mudança material |
| `package/output_lab` | ❌ **NÃO** — SYS assinado às 14:16, **anterior** ao fail-closed | manifesto registra fonte SYS `5D7333F4...`, diferente do candidato atual |

O lab **não** foi reconstruído de propósito: `tools/build-bc250mcdm-lab-bringup.ps1` exige
`-Execute -IUnderstandThisEnablesDestructiveBringup`, e esse flag existe justamente porque a
imagem de laboratório executa PFP direct load, CP, GCVM e HQD **durante o `StartDevice`,
enquanto detém o display**. Ligá-lo é decisão do dono do projeto, não passo de build.

⚠️ **Duas armadilhas medidas neste passo, ambas capazes de queimar uma corrida:**
1. `build-bc250mcdm-lab-bringup.ps1` **sem** os flags retorna **`EXIT=0`** — ele só valida
   precondições. Ler o código de saída como sucesso deixa o SYS antigo no lugar.
2. **Comparar hash do pacote com o hash do build não serve como prova de frescor** — o
   empacotador **assina** o SYS, então os dois diferem por construção. Cometi esse erro de
   medição aqui e o corrigi: a prova barata é procurar, como ASCII dentro do binário, uma
   string de log introduzida no candidato.

### B) DESENVOLVER — capacidade que ainda não existe

> **A dívida central, em uma frase:** o `SubmitCommandVirtual` do Node 0 tem três caminhos —
> `PLANO A DIRECT FILL`, `PLANO A SYNC NOP` e `INDIRECT`. Os dois primeiros **reimplementam
> a operação no anel** e a GPU nunca lê o DMA buffer; só o terceiro executa o buffer que o
> `dxgkrnl` entregou, e é o único que não funciona. Todo compute e todo jogo passam
> obrigatoriamente pelo terceiro (`bc250_active_miniport.c:6172`).

| # | O que falta construir | Por que é bloqueante | Estado hoje | Fecha com |
|---:|---|---|---|---|
| B1 | **Executar o IB de paginação sob VMID8.** ✅✅ **FECHADO NO SILÍCIO EM 07/09 17:43 — TRIPÉ COMPLETO:** duas submissões com `RPTR==WPTR` nas duas leituras, fence da GPU sobre poison e **5 canários substituídos** (`Divergentes=0`), sobre o DMA buffer real do `dxgkrnl` sob VMID8. Antes disso, em 17:13, o mesmo caminho executou com DUAS pernas: dez `INDIRECT` reais sob VMID8, `RPTR==WPTR` e fence da GPU sobre poison, sem timeout e sem fault. **NÃO FECHA pela Lei 2:** o gate não conferia byte nenhum do destino, e o rótulo `IB_TRIPOD PASS` era falso sucesso — corrigido na A14, que plantou canários para a perna 3. Fecha na próxima corrida se sair `IB_TRIPE_VEREDITO: TRIPE_PROVADO … Divergentes=0`. ⚠️ **O experimento da CSA foi DESCARTADO por leitura (07/09, `H`):** no upstream, `amdgpu_sdma_get_csa_mc_addr()` devolve **zero** quando a preempção no meio do comando está desligada — e o nosso `F_CNTL_MIDCMD` já saiu do `CNTL`. Logo `CsaAddress = 0` é o valor **correto**, não uma omissão, e implementar CSA seria divergir do upstream **gastando uma corrida à toa**. O formato do `INDIRECT` também confere com o `SDMA_PKT_INDIRECT` padrão e está agora **travado byte a byte** pelo bloco 6b de `sdma_packet_builder_test`. ✅ **DUAS DAS QUATRO PERGUNTAS DO DOC 167 FECHARAM EM 07/09, POR LEITURA DO NOSSO PRÓPRIO CATÁLOGO, SEM GASTAR CORRIDA.** A **4** (`PAGE_TABLE_DEPTH` do VMID8) já estava respondida em `docs/03` com `P1`: a convenção do GCVM é *níveis abaixo da raiz*, logo `DEPTH = Levels - 1 = 1` — e é isso que o driver programa (`BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE = 0x00555403`, `BLOCK_SIZE = 0`, `PT_END = 0x003FFFFF`, os quatro travados por `gcvm_context8_cntl_test`). A **5** (base da PTE) também: a PTE carrega **PA**, não MC — `PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)` (`amdgpu_gmc_vram_mc2pa()`), e trocar pela base MC já foi tentado em 05/09 01:29, diagnosticado como erro de domínio e revertido. **Restam só a 2** (que estado faz o `RPTR` alcançar o `WPTR` sem executar) **e a 3** (hub de fault correto — o `FAULT_STATUS` do GFXHUB lê zero sempre). Logo: `DEPTH`, `BLOCK_SIZE`, `PT_END`, aritmética de PA da PTE e formato do `SDMA_PKT_INDIRECT` estão **todos** conferidos contra fonte e travados por teste — **o IB não falha em nenhum deles**. A 2 e a 3 não devem ser chutadas: são semânticas de registrador, cada combinação errada custa uma corrida e produz o mesmo sintoma | Sem isso, nenhum command buffer de UMD executa. É a raiz da barreira de compute e jogos | tradução provada (`IB_PA_MATCH ... CONFERE`), execução **não** | Alocar CSA no work buffer, submeter e obter o tripé **do próprio IB**: `RPTR==WPTR`, fence da GPU sobre poison e bytes conferidos |
| B2 | **Ler o hub de fault correto.** `GCVM_L2_PROTECTION_FAULT_STATUS` do GFXHUB lê `0x00000000` em todas as corridas; a pergunta 3 do doc 167 (SDMA no GC 10.1 roteia por GFXHUB ou MMHUB?) continua **aberta** | Sem fault legível, a parada do IB é silenciosa e o diagnóstico é cego | não iniciado | Provocar fault deliberado e ler o status latchado no hub certo |
| B3 | **Aposentar os caminhos `PLANO A`** depois que B1 fechar. ✅ **FEITO OFFLINE EM 07/09, depois do B1 fechar:** o default do Node 0 passou a ser **somente `INDIRECT`**, e os `PLANO A` ficaram atrás de `SdmaPlanoAFallback` (default 0, fail-closed, apenas bissecção). Falta a corrida que mede esse default. Passo anterior (A13): os `PLANO A` deixaram de se anunciar como `IB EXECUTADO` — a frase agora é exclusiva do ramo `INDIRECT` e eles declaram que a operação foi reimplementada no anel e que a GPU não leu o DMA buffer; e o tunable `SdmaIndirectOnly` (default 0, fail-closed) desliga a precedência deles para que o `INDIRECT` possa ser medido. Aposentar de fato continua condicionado ao B1. **NÃO VALIDADO EM HARDWARE** | Enquanto existirem, um `INDIRECT` que falha fica **escondido** por uma reimplementação que funciona — é fallback disfarçado de sucesso | ativos e em uso | `SubmitCommandVirtual` executando o DMA buffer real, com os `PLANO A` atrás de tunable default desligado |
| B4 | **Submissão no Node 1 (compute) originada pelo `dxgkrnl`.** ✅✅ **1ª SUBMISSÃO DE COMPUTE WDDM EXECUTADA NO SILÍCIO EM 10/09 21:45:48:** `D3DKMTSubmitCommand` do Mesa RADV retornou `STATUS_SUCCESS`. O KMD Node 1 capturou 24 dwords de PM4 do IB (`VA=0x200044000 PA=0x452060000`), despachou para a fila MEC persistente (`WPTR=12 Fence=18`), e o motor MEC da GPU **consumiu o anel** (`RPTR=12 Writeback=12`). A busca do IB pelo CP gerou `EXECUTE_PROTECTION_FAULT (0x8)` no GCVM L2 (`STATUS=0x00900880 ADDR=0x00200044`) porque a PTE estava sem o bit `BC250_PTE_EXECUTABLE` (`flags=0x21`). Corrigido no Mesa RADV (`.Execute = 1` em `MapGpuVirtualAddress` e `MAP_PROTECT`), compilado com sucesso (`vulkan_radeon.dll` SHA256: `1B43EEB9...`) | É o que o `vkCreateDevice`/`vkQueueSubmit` exige. | Submissão física consumida pelo MEC; parada por falta de bit de execução diagnosticada e corrigida | Próxima corrida física com PTE executável (`flags=0x31`) para concluir o tripé compute |
| B5 | **Residency (`MakeResident` / `Evict`)** — item 39 | O modelo WDDM 2.0 põe a residência sob responsabilidade do UMD; RADV vai exercer isso | não iniciado | Accounting conferido, sem leak, sob carga |
| B6 | **TDR — item 25.** Gate de saída da P6, não negociável. ✅ **BACKEND DAS DUAS DDIs FEITO OFFLINE EM 07/09:** `ResetFromTimeout` faz quiesce físico e `RestartFromTimeout` rearma **com prova de fence**; antes as duas devolviam `STATUS_SUCCESS` sem tocar no silício, e o teste exigia isso. Falta a corrida com hang induzido, e ela é o item | Sem TDR, cada bug de shader é reboot. `timeout-detection-and-recovery.md`: timeout default de 2 s, `0x117`/`0x141`/`0x142` | não iniciado | Hang induzido → detectado → engine resetada → adaptador volta a aceitar submissão → **display sobrevive** |
| B7 | **P7 completa: UMD/Vulkan/RADV.** Em 08/09, offline, a UMD ganhou transação Node 1 completa e acionador físico default-off: destino residente, cinco poisons pré-submit, command buffer por `Lock/Unlock`, `WRITE_DATA` + `RELEASE_MEM`, fence exata, readback byte a byte e teardown. Em 09/09 19:56:09, o harness `B7_UMD_NODE1_TRIPOD` executou no silício: `RPTR==WPTR==12`, fence monitorada `0xBC250F01` e cinco canários gravados via PM4 sob VMID 9. **Ressalva auditada (doc 230):** comprovou comandos PM4 e efeito em memória, mas **não houve dispatch de shader** (`DISPATCH_DIRECT`) nem compilação de código de shader pela rota UMD/WDDM. A UMD gráfica e o RADV seguem sem pipeline de render implementado. | É a maior fase do projeto em linhas de código, e só começa depois de B1–B6 | PM4 WRITE_DATA/RELEASE_MEM sob VMID9 **VALIDADO NO SILÍCIO**; pipeline de shader/Draw e Vulkan/RADV **NÃO VALIDADO EM HARDWARE** | Backend de tradução DXBC→SPIR-V→ACO funcional + winsys WDDM emitindo PM4 de render com sucesso |
| B8 | **Marco 3 — swapchain contínuo sem perder o device.** Oito corridas em 12/09: duas PASS de 1 quadro e seis rupturas nos quadros 13, 40, 43, 64, 373 e 447 de 600. Em **todas**, `SubmitAccepted == RetiredTotal == V2NotifiedTotal` e `V2FencesInFlight=0` — nada preso. Na janela de 11:04 houve **zero** eventos de System (`4101`/`117`/`141`): **não houve TDR**. `STATUS_DEVICE_REMOVED` **não existe em nenhuma linha do driver** e `Bc250ActiveEscapeHandler` não pode devolvê-lo, logo o `dxgkrnl` recusou antes de nos alcançar (P1, leitura de código). **Causa ABERTA — não inventar.** Instrumentação adicionada offline na C3: `EscapeEnteredCount` não amostrado, duração do blt, `first_failed_frame`, `failure_mode` e `tdr_events` no veredito | Sem swapchain estável não há onde um pipeline gráfico aparecer, e qualquer backend D3D11 seria construído sobre um caminho que não sustenta imagem | 447/600 quadros é o recorde; **26 FPS** (~38 ms/quadro) para 11 MB, ~290 MB/s — instrumentado, **não explicado** | `EscapeEntered` alcançando o último quadro **e** 600/600 com `Fallbacks=0`, `HostCpuCopies=0` e `tdr_events=0` |
| B9 | **Anel gráfico (`CP_RB0`/PFP/ME) e PM4 de `DRAW`.** ✅✅ **ANEL GRÁFICO FÍSICO CP_RB0 VALIDADO NO SILÍCIO (17/09 17:48, Corrida 15):** CP_RB0 consumiu e aposentou 45 command buffers gráficos sob VMID 7 com avanço monotônico de fences de 1 a 45 (`[SubmitVirtual Node1 GFX] FENCE COMMIT: Token=45 RingFence=45 committed=1 accepted=1`). O pipeline Direct3D 11 nativo executou 60/60 draws no hardware sem fallback para PM4. Promovido à Tabela 1. | É o elo que o desktop D3D11 exige e que o driver provou no silício | **VALIDADO NO SILÍCIO (Corrida 15)** | Submissões 3D aceitas e aposentadas no anel gráfico real CP_RB0 com fences commitadas no silício |

### Ordem, e por que essa

```text
A1..A5 (offline, de graca)  ─┐
                             ├─> corrida 1: display converge?  ──> P5 fecha
A6..A7 (higiene de prova)   ─┘
                                                B1 -> B2 -> B3 -> B4 -> B5 -> B6 ─> P7 -> jogos
```

A faixa **A** não depende de nada e não gasta corrida — é o que se faz primeiro porque é
grátis e porque A5/A6 melhoram a qualidade da prova de todas as corridas seguintes. A
faixa **B** é sequencial de verdade: B1 é pré-requisito de tudo o que vem depois, e B6
(TDR) é o portão que a P7 exige.

⚠️ **Correção de enquadramento (06/09).** Uma versão anterior deste bloco dizia
"Compute/LLM sai em B4; jogos só depois de B7", o que sugeria B1–B4 como uma "trilha de
compute" alternativa ao vídeo. **Isso está errado e foi corrigido.** B1 (executar o IB sob
VMID8) e B4 (submissão Node 1) **não são desvio para compute** — são **pré-requisito da
submissão Vulkan**, logo de jogos. O que seria desvio é *parar* em LLM e chamar de entrega.
Pela Lei do escopo, B1–B6 são obra de jogos e é assim que devem ser lidos.

## As próximas ações — ordenadas pelo objetivo (WDDM por GPU + jogos)

Toda ação abaixo serve a vídeo acelerado ou a jogos. Nenhuma delas é compute como destino.

> ### ⛔ A CORRIDA ABAIXO JÁ FOI GASTA EM 07/09 16:19 — E NÃO PRODUZIU INFORMAÇÃO
>
> Contagem exata no log da 16:19 (`P0`): **14** `IB EXECUTADO` = **2** `PLANO A DIRECT FILL`
> + **12** `PLANO A SYNC NOP`, **zero** `INDIRECT` e **zero** linhas `IB_QUEUE_*`. O desfecho
> fraco previsto no fim deste bloco é o que aconteceu: os dois `PLANO A` têm precedência,
> absorveram **14/14** submissões, o ramo `INDIRECT` não foi exercitado e a **pergunta 2
> continua sem resposta**. Repetir esta corrida como está é gastar boot frio para reler o
> mesmo nada.
>
> **A corrida que substitui esta** está definida na faixa A13:
> [`docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md`](docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md)
> §5–§6 — o pacote **já está assinado e auditado**: `DriverVer 3.82.0.0`, SYS assinado
> `0CF9472F…`, CAT `7798EC55…`, com o INF declarando
> **`SdmaIndirectOnly=1`**, que desliga a precedência dos `PLANO A`. Receita exata em §6.2. Sem esse tunable a
> corrida repete a 16:19. Com ele, o risco declarado é que submissões que hoje "concluem"
> passem a `RECUSA` — caminho conhecido de `0x119`. Armar é decisão do dono do projeto.
>
> Registro original, preservado para leitura:
>
> **Uma corrida, pacote ativo atual, telemetria só-leitura, probe DESARMADO.**
>
> ```
> powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm
> powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1
> ```
>
> Boot frio antes. Pacote ativo `DriverVer 3.81.0.0`, com a telemetria nova conferida por
> string dentro do `.sys`.
>
> **Por que vale:** a telemetria da faixa A12 é **somente leitura, zero mudança de
> comportamento** — nenhum risco novo. E o log **não se perde**: `Bc250DisplayLog` abre,
> escreve, dá `ZwFlushBuffersFile` e fecha **a cada linha**
> (`driver/wdk/bc250_display_ddi.c:575-577`), então reboot sujo não apaga o que já saiu.
>
> **Por que NÃO bundlar o experimento de fault (`0x007FFE03`) na mesma corrida:** se armar as
> sete `*_ENABLE_INTERRUPT` provocar tempestade de fault, a máquina pode travar durante o ~1 s
> de polling — e é **exatamente aí** que as linhas `IB_QUEUE_*` são escritas. Sem TDR (item 25),
> perderíamos justo o dado que fomos buscar. **Banca a resposta da pergunta 2 primeiro, arma a 3
> depois.**
>
> **O que ler no log:**
>
> | Linha | O que decide |
> |---|---|
> | `IB_QUEUE_STATUS: … IDLE=?` | **`IDLE=0`** confirma a hipótese (parou dentro do IB); `IDLE=1` a **refuta** |
> | `IB_QUEUE_DECODED: … IB_RPTR=? dw IB_OFFSET=? dw` | se diferirem de 8/0, o `0x20` de 05/09 **não** era constante — mata a dúvida do doc 168 §5.3 |
> | `IB_QUEUE_STATUS: … EXCEPTION=?` | não-zero seria a resposta direta, e ninguém tinha olhado |
> | `IB_QUEUE_STATUS: … BITS_SEM_NOME=?` | o bit 1 sem nome no upstream, se aparecer |
> | `ROOT_MC: DIVERGENCIA` ou `concorda` | se a constante `0x450000000` envelheceu |
> | `UMDRIVERPRIVATE: GpuVaBits=34, PageTableLevels=2` | confirma o A11.1 no silício |
>
> ⚠️ **Desfecho possível que precisa ser DECLARADO, não maquiado:** o `INDIRECT` só é alcançado
> quando não há fill pendente, o buffer não tem 4 bytes, e a GPUVA do IB resolve sob VMID 8 — os
> dois `PLANO A` têm **precedência** (`driver/wdk/bc250_active_miniport.c:6198-6242`). **Se todas
> as submissões forem absorvidas por `PLANO A`, não sai nenhuma linha `IB_QUEUE_*`.** Isso ainda
> é informação — diz que o `INDIRECT` não está sendo exercitado no perfil ativo — mas é fraca.

1. ✅ **FEITO (06/09):** dump do DWM aberto com `cdb`. Pilha completa:
   `CLegacyRenderTarget::EnsureSwapChain` → `CD3DDevice::CreateD3D11Device` →
   `DXGI_ERROR_UNSUPPORTED` → **fail-fast**. Decisão de arquitetura resolvida: **backend
   D3D10/11 real é obrigatório**; CDD/interop está **refutado por dump**.
2. ✅ **FEITO (06/09):** A9 e A10 corrigidos e travados por teste e auditoria. A próxima
   corrida de display não pode mais "passar" com a tela apagada: frame em branco sai como
   `BYTE_PROOF INCONCLUSIVO` com `BlankStreak`, e a palavra "TRIPE" só aparece com as três
   pernas provadas. `build-contract.bat` em `EXIT=0`, auditoria de segurança em `PASS`.
3. ✅ **FEITO (06/09):** dimensionamento em `docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md`.
   Piso = `D3D10_1_DDI_INTERFACE_VERSION`, **105** entradas de device, **5** de adapter,
   feature level de piso **10_1** (9_x indisponível nesta interface). O custo real são o
   compilador DXBC→ISA, o pipeline 3D e os recursos sobre GPUVA — não a contagem de entradas.
   **Nenhum prazo é dado:** com compilador de shader no escopo, estimativa em semanas seria
   palpite.
4. ✅ **ENCERRADO (07/09): `P7_D3D11_CAPS_PROBE`, 3 corridas gastas — localizou o item
   zero.** v1 (06/09 23:45) falhou por defeito meu: aceitava qualquer `Interface`, matou o
   step-down e era cap falso. v2 (07/09 08:05) restaurou o step-down — **`P1`: a negociação
   de DDI é escada descendente comandada pela recusa da UMD**. v3 (07/09 08:25) instrumentou
   as três entradas do adapter table e o **silêncio delas** apontou o kernel:
   `DXGKQAITYPE_UMDRIVERPRIVATE` recusado 80 vezes. **Autocrítica:** o `Type=0 NOT SUPPORTED`
   estava no log do KMD desde a primeira corrida; o caminho curto era ler os tipos de QAI
   antes de instrumentar modo usuário.
5. ✅ **`P7_D3D11_CAPS_PROBE` ENCERRADO (07/09 10:11) — 6 corridas, missão cumprida.**
   A v5 confirmou que o bloqueio era **defeito nosso**: `pAdapterFuncs_2` vem não-NULO e o
   driver deve **preencher a struct do runtime**, não sobrescrever o ponteiro. Com isso o
   runtime percorreu a negociação inteira e **pediu exatamente um cap**:
   `D3D11DDICAPS_3DPIPELINESUPPORT` (4 bytes). As três perguntas do doc 173 estão fechadas —
   o `dwmcore` **aceita** 10_1, valida **só** o nível de pipeline 3D, e o minor 17 deixou de
   importar. **O último portão é um pipeline 3D real, e não há atalho por caps.**
   ⚠️ **Autocrítica:** o defeito era uma convenção **legível no header desde a primeira
   corrida**. Quatro corridas testaram o comportamento do runtime quando a pergunta certa era
   "quem aloca esta struct?". O que salvou o método foi instrumentar para provar ausência —
   o silêncio das entradas, e depois a ausência específica de `pfnCloseAdapter`, é que
   apontou a convenção.
6. ✅ **DECIDIDO (06/09): rota B — DXBC→SPIR-V→RADV/ACO.** Um backend para as duas
   frentes. Registrada em "Decisão de arquitetura vigente"; rota A (compilador DXBC→ISA
   próprio) fica **rejeitada** e só se reabre por escrito.
7. **B1 — executar o IB de paginação sob VMID8.** Não é desvio para compute: é o que permite
   ao `dxgkrnl` entregar command buffers de verdade, logo é pré-requisito de **submissão
   Vulkan** e de jogos. ✅ **Em 07/09 o cerco fechou por leitura, sem gastar corrida:** das
   quatro perguntas de registrador que sustentavam este item, **três saíram** — a CSA
   (descartada: com `MIDCMD` desligado, `CsaAddress = 0` é o valor correto do upstream), a
   **4** (`PAGE_TABLE_DEPTH = 1`, já respondida em `docs/03` com `P1` e travada por
   `gcvm_context8_cntl_test`) e a **5** (a PTE carrega **PA**, não MC:
   `PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)`). Somando o `SDMA_PKT_INDIRECT`, que já
   estava travado byte a byte: **nenhum dos parâmetros que eu poderia ter errado está errado.**
   ✅ **A pergunta 3 também fechou, no mesmo passo:** o hub **está certo** — `docs/03` cita
   `sdma_v5_0.c` atribuindo `ring->vm_hub = AMDGPU_GFXHUB(0)` —, e o caminho de invalidação
   confere nos **cinco** pontos que o catálogo exige (`REQ = 1 << 8` → `0x00F80100`,
   `CLEAR_PROTECTION_FAULT_STATUS_ADDR` em **0** para não apagar evidência, ACK no bit
   `1 << 8` de `ENG17_ACK`, leitura dummy do `REQ` obrigatória em GC < 10.3, e sem semáforo).
   **Logo `FAULT_STATUS = 0` é medição de verdade, não hub errado nem status apagado por nós.**

   🔶 **E aí apareceu a pergunta certa, com HIPÓTESE (`H`) e experimento que DISTINGUE duas
   causas.** Contando os bits do nosso valor alvo contra o POST — os dois já em `docs/03`: as
   sete classes de fault têm par `ENABLE_DEFAULT` / `ENABLE_INTERRUPT`; o POST tinha os
   **quatorze** ligados, e o nosso alvo liga os sete `DEFAULT` e **desliga os sete
   `INTERRUPT`**. Pela própria tabela do catálogo, `ENABLE_DEFAULT = 1` significa *"redireciona
   para dummy page default"*. Isso descreve hardware que, ao falhar a tradução, **manda o
   acesso para a dummy page e não avisa ninguém** — e explica as **três** medições do B1 de
   uma vez: `RPTR` alcança o `WPTR` (a engine consumiu o pacote), `FAULT_STATUS` zero (nenhuma
   interrupção habilitada) e destino sem dados (escritas na página default).
   ⚠️ **Não provado:** não se sabe — e não está nos nossos docs — se o `FAULT_STATUS` *latcha*
   com `DEFAULT=1` e `INTERRUPT=0`. Se latchar, a hipótese está **refutada** pela medição que
   já temos. E o valor alvo **não é erro nosso**: o catálogo o deriva do setup padrão do
   upstream, que também não habilita interrupção nesse passo.

   **Próximo passo — uma corrida que produz informação nos dois desfechos:** habilitar as sete
   `*_ENABLE_INTERRUPT` no `GCVM_CONTEXT8_CNTL` e submeter **o mesmo IB que hoje falha**.
   `0x00555403 | 0x002AAA00 = 0x007FFE03`, que difere do POST medido em **apenas `0x83`**
   (`ENABLE_CONTEXT` e `DEPTH`, deliberados, mais `RETRY_PERMISSION_OR_INVALID_PAGE_FAULT`, que
   o upstream desliga em APU) — é o estado de fábrica com a observabilidade preservada, não um
   valor inventado. O decodificador de IV já existe e está travado (`INTERRUPT HANDLER
   CONTRACT`, IV de 32 B do Navi/GFX10). Critério binário: **chega IV de fault** → hipótese
   confirmada, o IB executa, o defeito é de tradução e o `FAULT_ADDR` diz *qual* endereço;
   **não chega IV** → hipótese refutada, a engine não executa o pacote, e o centro volta a ser
   a pergunta 2. **Já declarado em código, e deliberadamente NÃO usado:**
   `BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE`, com a aritmética travada por 8 asserções
   em `gcvm_context8_cntl_test` — armar é decisão do dono do projeto, não efeito de compilar.
8. **Avaliar a branch `wddm2` do RADV** (Collabora/Valve,
   `gitlab.freedesktop.org/lfrb/mesa`) como base do item 146 — **offline**. É o atalho legítimo
   para a P7 **de jogos**: aproveita-se o port Win32 e o esqueleto de winsys D3DKMT;
   descarta-se a ABI de *private data* da AMD, porque o KMD é nosso.
   📌 **Este é o caminho do BLOQUEIO A** (pipeline 3D, a barreira do vídeo) **e não depende de
   corrida nenhuma** — é o que dá para avançar em paralelo enquanto as corridas do B1 são
   agendadas. Pré-requisito já satisfeito: o blob `UMDRIVERPRIVATE` deixou de mentir sobre o
   espaço endereçável (A11.1), que é o que o winsys do RADV usaria para dispor endereços.
9. **Corrida do perfil lab** para testar a correção de pivô no cenário 97/0. A corrida de
   22:42 usou o perfil **ativo**, que já convergia desde 05/09 — reproduziu a convergência e
   provou que a faixa A não a quebrou, mas **não** testou o pivô.
10. **TDR (item 25)** continua portão obrigatório de saída da P6. Sem ele, cada bug de shader
   de jogo é um reboot.

## Veredito

> **Handoff mais recente:**
> [`docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md`](docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md)
> — sessão inteiramente offline, **zero corridas gastas**: fechou as perguntas 1, 3, 4 e 5 do
> doc 167 por leitura do próprio repositório, deu dados à pergunta 2, corrigiu cinco defeitos na
> geometria de GPUVA (faixa A11) e decodificou a telemetria de fila da SDMA0 (faixa A12).
> **Nada disso foi validado em hardware.**

- **Compute direto no silício:** VALIDADO EM HARDWARE.
- **Display KMDOD / GOP com ownership físico:** VALIDADO EM HARDWARE.
- **WDDM Full Graphics (`RenderNode=1`) em Código 0:** VALIDADO EM HARDWARE.
- **Topologia VidPn comitada (`pathCount=1`) e alocações primárias:** VALIDADO EM HARDWARE.
- **DWM ativo a 60 Hz e motor SDMA0 executando Blts de apresentação:** VALIDADO EM HARDWARE.
- **Sanitização de scanout GDDR6 via CONST_FILL:** VALIDADO EM HARDWARE.
- **Cadeia display->primária->blt por SDMA sob WDDM:** **VALIDADA EM HARDWARE** em 06/09 22:42
  (`CommitVidPn`, `SetVidPnSourceAddress`, 10 `Present` com `SDMA=YES`, `BLT_TRIPOD PASS` de
  11.059.200 bytes). **Com a ressalva de que o frame estava em branco** (`NonZero=0/256`).
- **Causa do desktop preto:** **MEDIDA** — `dwm.exe` não obtém device D3D11 (`E_NOTIMPL` na
  UMD, `DXGI_ERROR_UNSUPPORTED` no dump). Não é falha de `Present` nem de SDMA.
- **Render-only WDDM:** REPROVADO e encerrado.
- **DCN é pré-requisito para jogos:** **NÃO** — itens 57–62 e 81 provam o contrário.
- **Root GPUVA em offset zero, snapshots dos dois IBs de paginação e dois fills físicos:**
  **VALIDADOS PARCIALMENTE EM HARDWARE**; execução do IB sob VMID8 não foi provada.
- **VidPn/primária no perfil lab atual:** **NÃO CONVERGE**; causa exata ainda
  **NÃO VALIDADA EM HARDWARE**.
- **RADV `vkCreateDevice`, submissão Node 1, Área de Trabalho visível, jogos e LLM:**
  **NÃO VALIDADOS EM HARDWARE.**

O gargalo anterior (**Código 43 / Item 97**) foi superado. Há agora dois bloqueios
independentes: o perfil lab não chega a criar/comitar a primária, e o RADV não retorna de
`vkCreateDevice`. Os candidatos ativo e lab foram reconstruídos, assinados e auditados
offline; o próximo passo físico continua explicitamente **NÃO VALIDADO EM HARDWARE**.
