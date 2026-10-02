# 518 — Corrida 161: o vkd3d disse por que o shader do menu Iniciar falha; SHADER.2 (SRV de buffer tipado); critério da Corrida 162

> 28/09/2026. Candidato da 161: 3.309.1.0 (SHADER.1; KMD igual ao da 3.308, DX.97).
> Evidência: `docs/evidence/2026-09-28_19-09-48-DESKTOP-HQAG6T4-lab-binding/`.
> Bind às 19:09:48; trava por volta de 19:11:33; reset pelo botão; boot às 19:12:05 (evento 41).

## 1. Corrida 161: o que foi medido

**Desfecho A do doc 517, com uma ressalva.**
- A mensagem do vkd3d chegou ao log do StartMenuExperienceHost (pid 7112, AppContainer) (P0):
  `vkd3d resultado=-4 mensagens=[<anonymous>: E2000: Could not find descriptor binding for type 0, space 0, registers [0:0], shader type 0.]`.
  Isso é descritor SRV (tipo 0), `t0`, espaço 0, pixel shader.
- O arquivo `BC250_SHADER_FAIL_7112_59C609E5.bin` foi criado (452 bytes = 24 + 356 + 6×12, que confere com ISGN=5 e OSGN=1), mas **todos os bytes são zero** (P0).
  - Causa (P1, pelo código): a DLL recarrega a cada device do menu, a deduplicação estática zera e `CREATE_ALWAYS` reescrevia o arquivo.
  - A última reescrita (19:11:31) ainda estava no cache quando a máquina travou (19:11:33). Ficou só o tamanho.
- O próprio log fecha o tipo do recurso sem o `.bin` (P0): o menu cria 10 SRVs com `Format=62` (R8_UINT) e `Dim=1` (BUFFER), sobre um buffer de 524.288 bytes preenchido por `ResourceCopyRegion`.

**Reprodução offline (P0 no teste):**
- um PS SM 4.0 compilado com `fxc` que lê `Buffer<uint>` em `t0` reproduz **a mesma mensagem e o mesmo `resultado=-4`**, pelos dois caminhos: o DXBC original e o TokenStream da UMD (vkd3d 1.17 real, adaptador, ponte e parser reais);
- causa (P1, código): o vínculo de SRV que passamos ao vkd3d tinha `flags = IMAGE`, e o vkd3d exige `BUFFER` para `Buffer<T>`.

**Trava (a mesma parede de antes, sem informação nova):**
- a última migalha da máquina foi `SDMA_REG_READ 0xFC4` concluído, com a CPU07 dentro de `PAGING_DDI op=12` (FLUSH_TLB) na fase ANTES, sem concluir;
- o log do KMD para no meio de uma submissão de paginação (fills de 4 MB), cerca de 0,65 s depois de o harness D3D11 terminar (60/60 Presents, ~1,6 FPS);
- sem BugCheck (fusível disparou no boot seguinte: Código 43, State=2, recuperação necessária);
- estatística desde a FPS.1: 152 T, 153 L, 154 T, 155 T, 156 T, 157 T, 158 T, 159 T, 160 L, **161 T**.

## 2. SHADER.2 (offline)

1. **Adaptador vkd3d:** o vínculo de SRV passou a ser `BUFFER|IMAGE`. Um shader não usa o mesmo `t#` dos dois jeitos, e o tipo Vulkan vem da reflexão.
2. **Reflexão da UMD:** `OpTypeImage Dim=Buffer Sampled=1` vira `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER`. Storage (`Sampled=2`) continua recusado.
3. **Gravador de descritores:** ramo novo, que cria `VkBufferView` com o formato e a faixa da SRV. As recusas são nomeadas:
   - `SRV_TEXEL_DIMENSAO_NAO_E_BUFFER`
   - `SRV_BUFFER_SEM_VKBUFFER`
   - `SRV_BUFFER_FORMATO_NAO_SUPORTADO`
   - `SRV_BUFFER_FAIXA_FORA_DO_RECURSO`
   - `SRV_BUFFER_DESLOCAMENTO_DESALINHADO` (conservador: 16 bytes)
   - `SRV_BUFFER_SEM_SHADOW_DE_CPU`
   - `SRV_BUFFER_UPLOAD_FALHOU`
   - `SRV_CREATE_BUFFER_VIEW_FALHOU`
4. **Ponte:** carrega `vkCreateBufferView` e `vkDestroyBufferView`; as views vivem no lote e são destruídas na aposentadoria; o pool persistente ganhou 2.048 texel buffers; todo VkBuffer ganhou `UNIFORM_TEXEL_BUFFER_BIT`.
5. **Dump do shader:** passa a usar `CREATE_NEW` (não sobrescreve; se o arquivo já existe, loga "ja gravado") e `FlushFileBuffers`.

**Escopo declarado:** o conteúdo vem do shadow de CPU, como no CB fora do anel e como o `ResourceCopyRegion` de buffer já faz hoje. Um buffer escrito só pela GPU não está coberto. Isso está declarado, não foi testado.

**Provas offline:**
- `repro` com o adaptador novo: o `Buffer<uint>` traduz (SPIR-V de 1.152/1.324 bytes); o candidato do `d2d1.dll` continua igual (2.048/2.220 bytes);
- `umd_descriptor_binding_test` cobre:
  - o caminho feliz (vínculo 288, view R8_UINT com offset 16 e range 32, destruída no lote);
  - três recusas nomeadas;
  - a reflexão `Dim=Buffer` → texel buffer, e storage → recusa;
- `build-umd` EXIT 0 e auditoria de segurança PASS, com o gate novo `audit_shader2_srv_buffer_is_bound_and_named`;
- 4 de 4 mutações reprovam: vínculo só-IMAGE, reflexão sem Dim=Buffer, recusa de alinhamento removida e view vazando no lote;
- build de laboratório EXIT 0 e auditoria do pacote PASS; strings novas conferidas na UMD assinada.

| Artefato (3.310.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `AE984EFCDB65AD75F4ABA46B6B9566391249D24497C56BF29DCD073B58CC6D78` |
| `bc250umd.dll` | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `271291C0F38BE797131BE149BE7D68E45CE1A46A6A16CE437C41F73BC7F7E73F` |
| `bc250mcdm.cat` | `E202FE0E5CCD49A8DEC4F807A8E6E00DAE664ECD29693330B98278FCF31FE0FF` |

O código do KMD não mudou desde a DX.97; o SYS foi reassinado com o DriverVer novo.

## 3. Critério da Corrida 162 (escrito antes)

- **Runner:** `tools\EXECUTAR_CORRIDA162_SHADER2_600S.cmd`.
- **O que o Daniel faz:** clicar no menu Iniciar algumas vezes, a partir de 1 minuto.

| Desfecho | Assinatura (log do StartMenuExperienceHost em `umd-logs-appcontainer\`) | Implicação |
|---|---|---|
| **A** | Nenhum `falhou (status=3, tokens=89)`; `[VK_DESCRIPTOR] BOUND ... SRV_BUFFER=1` e zero `DrawIndexedInstanced: RECUSADO` do menu | O shader e o descritor passam. Resta ver se o menu **aparece** na tela: isso o Daniel relata, e o pixel não está conferido pela Lei 2. |
| **B** | A tradução passa, mas surge `[VK_DESCRIPTOR] RECUSADO ... motivo=SRV_BUFFER_*` | O nome diz a próxima correção (faixa, alinhamento, shadow…). |
| **C** | A tradução passa e o pipeline falha (`[VK_PSO] ... falhou` ou `vkCreateGraphicsPipelines`) | O problema passou para o RADV: ler o `res=`. |
| **D** | Ainda aparece `vkd3d resultado=` com outra mensagem | Outro recurso do shader; agora o `.bin` deve sobreviver para reproduzir. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Observação: se a máquina travar antes de o menu ser usado, a corrida não responde ao critério (conta só como mais uma amostra da trava). NÃO VALIDADO EM HARDWARE.

## 4. Corrida 162 (19:34:10, 3.310.1.0): trava em ~1 min, antes do menu Iniciar

- **Evidência:** `docs/evidence/2026-09-28_19-34-10-DESKTOP-HQAG6T4-lab-binding/`. O trecho do log do KMD foi recuperado em `display-log-this-bind-RECUPERADO.txt` (3.749.402 bytes, SHA-256 `672B8646…`).
- **O log do KMD cobre ~52 s** a partir do bind, pelo QPC (P0). A última linha é `PAGING_DDI op=12` (FLUSH_TLB) com ENTER e sem LEAVE, logo depois de um FILL de 3.932.160 bytes. É a mesma assinatura das anteriores.
- **O harness D3D11 tinha acabado de começar (P0):** o log dele termina às 19:35:13.292 criando uma imagem de 3.932.160 bytes, exatamente o tamanho do último FILL.
  - Na 161, a trava veio 0,65 s depois de o harness terminar.
  - Nas 139/140, ~6 s depois de ele começar.
  - A 141, sem harness, deu 600 s limpos (doc 481).
  - Correlação P2; causa H.
- **O StartMenuExperienceHost não carregou a UMD** nesta corrida, então **o critério da §3 ficou sem resposta**: não é A, B, C nem D.
- **Sem migalhas:** "ANTERIOR AUSENTE". O reset veio às 20:30, ~55 min depois, e a VRAM decaiu por inteiro.
- **Recuperação:**
  - o fusível recusou o StartDevice no boot;
  - o watchdog fez o rollback online (`RECOVERY_OK_ONLINE`);
  - BasicDisplay em Código 0.
- **Estatística desde a FPS.1:** 10 travas em 12 (limpas: 153 e 160).

**Corrida 163:** mesmo pacote 3.310.1.0, com `BC250_SKIP_D3D11_HARNESS=1` (`tools\EXECUTAR_CORRIDA163_SHADER2_SEM_HARNESS_600S.cmd`).
- O critério da §3 vale como está.
- Soma-se uma segunda amostra sem harness: trava sem harness enfraquece a hipótese do gatilho, e 600 s limpos a reforçam (sem prová-la).

## 5. Corrida 164 (20:48:29, 3.310.1.0, harness aos 30 s e KDNET conectado): o WinDbg não conseguiu parar a máquina

- **Evidência:** `docs/evidence/2026-09-28_20-48-29-DESKTOP-HQAG6T4-lab-binding/`. O trecho do log do KMD foi recuperado em `display-log-this-bind-RECUPERADO.txt` (5.146.056 bytes).
- **Momento da trava:** ~86 s de log pelo QPC, por volta de 20:50:05. A última linha é `PAGING_DDI op=12` (FLUSH_TLB) com ENTER e sem LEAVE, logo depois de um FILL de 64 KB.
  - O harness estava no fim: o console chegou ao quadro 50 de 60, a ~1,6 FPS.
  - O Explorer e o harness gravaram UMD até 20:50:05.
- **Relato do Daniel (P2, observação do operador):** na tela preta, Ctrl+Break no WinDbg (KDNET) **não fez nada**.
  - Se o depurador de fato estava conectado e funcionando, isso significa que nenhuma CPU atendeu ao pedido de parada. O pedido do KDNET é verificado no tick do relógio, então o núcleo que o atende não estava rodando código do Windows.
  - Isso é compatível com CPUs presas numa leitura de MMIO que nunca completa, ou com a plataforma parada (H2, abaixo do SO). Hipótese, não prova.
  - **Falta o controle:** um Ctrl+Break com a máquina sã, no mesmo boot com KDNET. Sem ele, "o WinDbg não respondeu" não se separa de "o WinDbg não estava conectado".
- **Sem migalhas:** "ANTERIOR AUSENTE"; o reset veio às 20:53:49, ~4 min depois.
- **Menu Iniciar:** o StartMenuExperienceHost não carregou a UMD antes da trava. O critério da §3 continua sem resposta.
- **Estatística desde a FPS.1:** 11 travas em 13 corridas.

**Controle, depois da 164 (P2, relato do Daniel):** no mesmo tipo de boot com KDNET, com a máquina sã e no BasicDisplay, o Ctrl+Break **funcionou**. Logo, a falha na trava não vem da conexão.

**Leitura:** durante a trava dura, nenhuma CPU atende ao depurador do kernel. Somado a isto:
- sem BugCheck mesmo depois de 48 min (157);
- Ctrl+Scroll Lock sem efeito (157);
- ETW vazio;
- VRAM que decai durante a trava;

a trava está abaixo do Windows. As CPUs estão presas numa transação que não completa (provavelmente uma leitura de MMIO ou da BAR sem resposta) ou a plataforma inteira parou. Isso é H, com evidência P2 convergente.
- A investigação por software dentro da máquina travada (dump, KD, ETW) está **esgotada**.
- O que resta é o que o driver deixa **antes** de travar (migalhas com reset rápido) e bissecção por ensaio.

O KDNET deve ser desligado para as próximas corridas.

## 6. Corrida 163 (21:01:40, 3.310.1.0, SEM harness e sem KD): trava em ~56 s; mineração das 26 corridas de 27–28/09

- **Evidência:** `docs/evidence/2026-09-28_21-01-40-DESKTOP-HQAG6T4-lab-binding/`, com `display-log-this-bind-RECUPERADO.txt` (5.001.025 bytes).
- **Trava sem harness.** A última operação foi um FILL de 11.075.584 bytes (superfície 2560×1080×4).
  - Edge, WebView e Claude carregaram a UMD entre 21:02:36 e 21:02:40; o Explorer grava até 21:02:46.
  - Migalhas presentes (reset rápido), com a mesma assinatura: última `SDMA_REG_READ 0xFC4` concluída; `PAGING_DDI op=0xC` na fase ANTES.
  - **A hipótese "o harness é o gatilho" (doc 481) está enfraquecida:** a 141 foi limpa sem harness, a 163 travou sem harness.
  - O menu Iniciar não carregou antes da trava; o critério da §3 continua sem resposta.
- **Tempo até a trava, em 26 corridas (P0 no corpus):**
  - as **18 travas** caem entre **27 e 196 s**;
  - as **8 limpas** passam de 590 s;
  - nenhuma corrida travou depois de 200 s.
- **Chance constante por FLUSH_TLB: refutada (P0).**
  - As limpas seguem com 60–70 mil FLUSH_TLB em 200–600 s cada, ~1,1 milhão no total, sem nenhuma trava.
  - Nos primeiros 200 s, limpas e travadas têm volume parecido: 1–2,8 GB de FILL e 0,7–2,2 GB de TRANSFER.
- **Enriquecimento de tipos de linha nas 400 linhas antes da trava, contra janelas das limpas no mesmo intervalo (30–200 s):**
  - nenhum tipo discrimina; o maior contraste é `[DX93_ALLOC_MAP]`, 0,67 contra 0,38, com n=18;
  - a primeira comparação (limpas na 2ª metade) acusava `[UpdatePT …]`, mas era **artefato da amostragem** do log.
- **Leitura (H):** o gatilho é próprio da partida e não aparece como evento do KMD. O candidato que resta é a **atividade do desktop**: processos novos abrindo janelas (Gerenciador de Tarefas e navegador pelo roteiro, apps que iniciam sozinhos) com superfícies de tela cheia.

**Corrida 165:** mesmo pacote, sem harness, e o operador **não abre nada**. Só clica no menu Iniciar, a partir de 20 s de desktop, algumas vezes.
- Serve ao critério da §3 (menu).
- Dá uma amostra de trava com atividade mínima. Travar mesmo assim enfraquece a hipótese da atividade; chegar a 600 s a reforça.

## 7. Corrida 165 (21:15:10, 3.310.1.0, sem harness, atividade mínima): menu Iniciar passou pelo caminho novo; trava em ~46 s

- **Evidência:** `docs/evidence/2026-09-28_21-15-10-DESKTOP-HQAG6T4-lab-binding/`, com `display-log-this-bind-RECUPERADO.txt` (2.891.491 bytes) e `umd-logs-appcontainer\BC250_UMD_LOG_7040.txt`.
- **Desfecho A da §3 (P0, pelo log do StartMenuExperienceHost, pid 7040):**
  - zero `falhou (status=3)`;
  - zero `vkd3d resultado=`;
  - `[VK_DESCRIPTOR] BOUND: bindings=3 (CB=1 SRV=1 SAMPLER=0 SRV_BUFFER=1)`;
  - 2 `DRAW GRAVADO` e zero draw recusado;
  - `DXGI Present LEAVE cb: hr=0x00000000`.
  - **A SHADER.2 está validada no caminho de tradução, descritor e submissão. Os pixels não foram conferidos (Lei 2)**; se o menu apareceu na tela, só o relato do operador diz.
- **Trava:**
  - ~46 s de log do KMD; pelo QPC, ~0,1 s depois do primeiro Present do menu;
  - migalhas presentes, com a mesma assinatura (última `SDMA_REG_READ 0xFC4` concluída);
  - SystemSettings.exe carregou a UMD às 21:15:45 sem o operador abrir nada (processo de fundo).
- **"Processo novo abrindo janela dispara a trava" (§6): enfraquecida.**
  - A atividade mínima travou mesmo assim.
  - Nas corridas limpas, processos nasceram depois de 200 s sem travar: 07:48 (GPUQuery_External aos ~390 s, SystemSettings aos ~574 s) e 18:17 (SearchApp/WebView aos ~247–257 s, StartMenuExperienceHost aos ~523 s).
- **Estatística desde a FPS.1:** 13 travas em 15 corridas. Distribuição do tempo até a trava (§6): 19 travas, todas entre 27 e 196 s.

**Relato do operador (P2):** a tela ficou preta no clique para abrir o menu. **O menu não apareceu na tela.**

**KMD (P0):** todas as submissões GFX de antes da trava foram aposentadas, inclusive a do contexto do menu (VMID 5, fence 155, 592 dw). A última retirada GFX, a fence 156, vem antes da última linha do log. O desenho do menu executou e terminou na GPU; a trava veio depois, na paginação do SDMA (FLUSH_TLB). Logo, o trabalho gráfico do menu **não** travou a engine GFX. Que o clique tenha disparado a trava é coincidência de tempo, com n=1 (H).
