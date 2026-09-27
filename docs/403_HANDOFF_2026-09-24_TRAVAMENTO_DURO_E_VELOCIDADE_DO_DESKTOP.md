# Passagem de bastão: travamento duro e velocidade do desktop (24/09/2026)

> **Para a próxima IA.** Leia isto inteiro antes de mexer em código ou propor corrida.
> **Autor:** Claude (sessão de 23–24/09/2026, Corridas 93 a 100).
> **Nada desta sessão foi commitado.** O Daniel pediu explicitamente para não commitar ("continue entao nao precisa commit"). A árvore de trabalho tem ~44 arquivos modificados e ~100 novos sobre o commit `52f49e4`. Não faça `git checkout`/`reset`/`stash` sem pedir.

## 0. Regras que não mudam

**Fontes:** leia primeiro [`CLAUDE.md`](../CLAUDE.md) (leis 1–10), [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) (entradas de 24/09 no topo) e [`DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md`](../DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md).

**Quem faz o quê:**
- O **Daniel executa** as corridas no hardware.
- A IA compila, analisa e prepara.
- **Desligamento completo** (boot frio) entre binds.

**Antes de cada corrida:** critério escrito (Lei 10), com:
- assinatura e implicação em campos separados;
- desfecho **E — fora do mapa**;
- "desfecho parcial não é desfecho".

**Depois de cada corrida, ao Daniel:**
- as 5 seções (avanço, sucessos, falhas, veredito, recuperação/pendências);
- doc de resultado;
- entrada no topo da BASE;
- a **tabela-checklist ✅/🟡/❌/🔜** do desktop mínimo funcional, com "caminho mais curto".

**Proibições e hábitos:**
- Classificar toda afirmação: P0/P1/P2/H.
- Sem WARP, sem fallback de CPU, sem escrita em DCN.
- **Uma variável comportamental por corrida.** O Daniel já autorizou quebrar isso uma vez (DX.54); não generalize.
- Todo portão novo de auditoria é **verificado por mutação** (script `tools/test-dxNN-*-mutations.py`).
- Nunca `-Force` ao criar arquivo.
- Pronomes neutros para pessoas.
- Idioma com o Daniel: português, direto, sem jargão desnecessário.

## 1. Onde estamos (resumo de uma linha)

O **desktop do Windows aparece pela nossa placa, desenhado pela GPU** (D3D11 → Vulkan/RADV → WDDM, Present por SDMA) e é interativo. Dois problemas bloqueiam o "mínimo funcional":
- **(1)** ele **trava a máquina inteira** entre ~40 s e ~6 min em todas as corridas desde a 94;
- **(2)** ele é **lento**: ~3 FPS no melhor caso medido.

## 2. O travamento duro (bloqueador nº 1)

### 2.1 Assinatura (igual em 88, 92, 94–97, 99, 100)

- **Evento:** Kernel-Power 41, **sem BugCheck** e sem dump. O mouse para; às vezes o desktop fica congelado na tela.
- **Log de eventos:** vazio entre o bind e o reboot. O travamento leva o que não estava em disco.
- **Depois do reboot:** o fusível recusa o StartDevice seguinte (`[FUSE] STARTDEVICE RECUSADO … State=2`) e o guardião faz recovery online **depois** do reboot. Com o travamento, o guardião não age na janela.
- **Log do KMD:** termina **no meio de uma submissão SDMA de paginação**.
  - **Isso não localiza o travamento:** linhas emitidas acima de PASSIVE ou durante paginação vão para um **anel em RAM de 128 KB** que morre com a máquina.
  - Zero TDR, zero `IB_TRIPOD TIMEOUT`, zero `GFXHUB_FAULT`, zero `DEVICE_REMOVED`.

### 2.2 Tabela de corridas

| Corrida | Config relevante | Carga (contextos/min) | Resultado |
|---|---|---|---|
| 88, 92 | antes da DX.49 | — | **travaram** ([docs/387](387_CORRIDA93_DX48_RESULTADO_DESKTOP_INTERATIVO_LENTO.md)) |
| 93 | DX.48 | ~100 | **300 s limpos**, ~0,6 FPS |
| 94 | DX.49 (staging persistente, lote 16 MB, dirty rects) | 290–390 | travou |
| 95 | + DX.49b (trace durável) | ~ | travou ~1,5 min |
| 96 | dirty rects off (DX.50) | ~ | travou ~4,4 min, piscando |
| 97 | lote off (DX.51) | ~290 | travou ~4,2 min |
| 98R | + DX.53 (VMID desarmado antes de devolver) | ~290 | **5 min limpos** (acabou a janela) |
| 99 | + DX.54 (pool persistente, lote 4 MB, inspeção amostrada) | ~ | travou ~39 s |
| 100 | DX.55 = DX.54 com lote off | ~345 | travou ~6 min 02 s |

### 2.3 O que já foi descartado como causa suficiente (P0 de corrida)

- **Dirty rects:** a 96 travou sem eles.
- **Lote de desenhos:** a 97 e a 100 travaram sem ele.
- **Janela de reuso de VMID** (achado da auditoria externa do ChatGPT, corrigido na DX.53): a 100 travou com a correção. A DX.53 continua correta como código (removeu uma corrida de dados real) e fica.
- **Staging persistente e trace durável como causa única:** a 88 e a 92 travaram antes deles.

### 2.4 Hipóteses vivas (H)

1. **Rotatividade de contextos.** O Explorer destrói e recria o device inteiro a cada ~1,5 s (laço, §4). Com o driver mais rápido, o laço gira mais: ~100/min na 93 contra 290–390/min desde a 94. O travamento parece vir mais cedo com mais carga.
2. **Submissões GFX concorrentes de processos diferentes.** Na 99, dwm.exe e harness estavam os dois dentro de `FlushCommands` sem retorno no instante final, e o `SubmitUs` do harness chegou a 1,06 s logo antes.
3. **`Radeonsoftware.exe`** (AMD Radeon Software) carregou a nossa UMD 47 s antes do travamento da 100. Uma amostra só.

### 2.5 Recomendação para o próximo passo (minha; o Daniel ainda não escolheu)

Opções apresentadas ao Daniel no fim da Corrida 100 ([docs/402 §6](402_CORRIDA100_DX55_RESULTADO.md)):

1. **Staging persistente desligado** (bissecção barata; fraca, por causa de 88/92).
2. **Atacar o laço do Explorer** (tira o confundidor de carga; causa não identificada offline).
3. **Recomendado: trilha que sobreviva ao reset.**
   - Depois de 7 travamentos iguais, a última linha útil sempre se perde.
   - Ideias:
     - (a) gravar "migalhas" (estágio + contador + FenceId + VMID + DDI corrente) num bloco reservado de **VRAM** e lê-lo no StartDevice seguinte, **antes** de reinicializar. Precisa verificar se a VRAM sobrevive ao reset do botão; se não sobreviver, testar RAM de sistema reservada;
     - (b) reduzir o que vai ao anel em RAM e escrever as linhas de PASSIVE que antecedem o travamento de forma síncrona.
   - Na (a), respeitar a Lei 5 (transacional) e **nada em DCN**.

## 3. Velocidade (bloqueador nº 2)

**Medido na 98R, pelo log do dwm.exe (P0 de tempo):**
- intervalo entre Presents p50 391 ms;
- `FlushCommands` ~14–16 ms com espera de fence de só ~0,2 ms, ou seja, custo de CPU (fim do CB + `vkQueueSubmit` no RADV/WDDM);
- ~14 ms por desenho criando um `VkDescriptorPool` novo (no RADV = BO, ida ao kernel);
- ~29 ms por Present lendo a grade do buffer linear pela CPU.

**Corrigido (DX.54, P0):**
- pool de descritores persistente: `DescUs` de ~14 ms para **32 µs–253 µs**;
- inspeção do Present amostrada.

**Resultado na 100:**
- Present/min 151–178 (98R: 87–120);
- p50 294 ms;
- `SubmitUs` mediano 2,1 ms e `EsperaUs` 1,0 ms no dwm.exe.

**Custos ainda abertos:**
- **Um flush por desenho:** o lote está desligado por suspeita, e a suspeita caiu com a 100. Religar é candidato, mas a 96 piscou com ele.
- **Trace durável com write-through:** ~0,2 ms por linha e ~25 linhas por desenho.
- **KMD com ~243 invalidações de TLB por segundo** (`FLUSH_POOL_VMIDS`), compatível com mapeia/desmapeia por desenho somado ao laço do Explorer.
- **Log de fluxo do RADV** (`bc250_diag_printf`) abre e fecha arquivo por linha até 4 MB por processo. Está no repositório do Mesa, fora deste.

## 4. O laço do Explorer

**O recurso recusado** (P0 da descrição, pela DX.52), sempre o mesmo:

`CreateResource RECUSADO desc: Dim=3 1024x832 Fmt=87 Bind=0xA8 Misc=0x2 TotalSize=3407872 hr=0x80070057`

- **Decodificado:** Texture2D BGRA com SRV|RT|**PRESENT** e **MISC_SHARED** (P1).
- **A recusa:** `E_INVALIDARG` do `pfnAllocateCb` com `hRTResource` preenchido. O Explorer destrói o device inteiro e recria.
- **P0 (doc 396):** para o mesmo token, o KMD **aceitou** (`CreateAllocation Result=0`, `ActiveOpenAlloc result=0`). Quem recusa é o **dxgkrnl, depois do KMD**.
- **As DDIs que o dxgkrnl chama depois caem na amostragem do log de display:** `[CreateAllocation]`, `[ActiveOpenAlloc]` e Destroy/Close ficam invisíveis nas corridas longas.
- **Refutado:** a tese do Gemini "hResource deve ser NULL" (928 alocações com `hRTResource` passaram). O mesmo audit citou linhas de log que não existem; não confiar nele.
- **Candidatos não testados (H):**
  - dados privados no nível do recurso (`D3DDDICB_ALLOCATE.pPrivateDriverData`, hoje sempre NULL);
  - `CpuVisible=1` em alocação compartilhada (a classe RESOURCE sempre pede CPU_VISIBLE);
  - o KMD não devolver `DXGKARG_CREATEALLOCATION.hResource`.
- **Próximo passo razoável:** tirar essas DDIs da amostragem só para `Flags.Resource=1` com tamanho 3.407.872, ou coletar ETW do DxgKrnl.

## 5. Mapa do código das faixas DX.49–DX.55 (UMD, `driver/umd/`)

| Peça | Onde | Interruptor (hoje) |
|---|---|---|
| Staging persistente de textura | `bc250_umd_resource_vk.c` (`Bc250UmdVkUploadImageData`), `bc250_umd_vulkan_bridge.c` (`Bc250UmdVkEnsureUploadStaging`) | sem interruptor, **ligado** |
| Lote de desenhos + anel (4 MB, limiar de 32 desenhos) | `bc250_d3d_umd.c` (`Bc250UmdExecuteVkDraw`, `s_Bc250Dx51DrawBatchEnabled`), `bc250_umd_vulkan_bridge.c` (`DrawRing*`) | **FALSE** |
| Drenos antes de destruir objetos | resource_vk, pipeline_cache, `bc250_umd_blt.inl`, `Bc250UmdBridgeLinearToWddm` | ativo |
| Trace durável (write-through) | `bc250_wddm_winsys.c` (`Bc250WinsysTraceRaw`) | ligado |
| Pool de descritores persistente + cache de samplers | `bc250_umd_descriptors.c`, `Bc250UmdVkAllocPersistentSet`/`Bc250UmdVkCachedSampler` na ponte | `s_Bc250Dx54PersistentDescriptors = TRUE` |
| Inspeção do Present amostrada | `bc250_d3d_umd.c` (Present, `PresentInspectCount`) | `s_Bc250Dx54SampledPresentInspection = TRUE` |
| Tempos do flush `[DX54_TEMPO]` | `Bc250UmdVkFlushCommands` | medição |
| Descrição do recurso recusado (DX.52) | `bc250_d3d_umd.c`, `bc250_wddm_winsys.c` | medição |

**KMD (`driver/wdk/`):**
- `bc250_display_ddi.c`: dirty rects desligados, `s_Bc250Dx50DirtyRectsEnabled = FALSE`.
- `bc250_active_miniport.c`:
  - `[CREATE_ALLOC_RECUSADO]` (DX.52);
  - **DX.53** no `DestroyContext`: Lookup → desarme com slot reservado → Release só com desarme OK, quarentena na falha;
  - `Bc250ActiveDx53PendingFences` sincronizado com a ISR.

**Portões em `tools/audit-bc250mcdm-safety.py`:**
- `audit_dx49_desktop_throughput`;
- `audit_dx50_dirty_rect_bisection`;
- `audit_dx52_create_allocation_refusal_speaks`;
- `audit_dx53_vmid_disarm_before_release`;
- `audit_dx54_desktop_speed`: exige lote **FALSE** desde a DX.55 e substituiu o antigo `audit_dx51`.

**Suítes de mutação:**
- dx49 19/19, dx50 6/6, dx52 6/6, dx53 5/5, dx54 9/9;
- `test-dx51-batch-bisection-mutations.py` está **obsoleto**.

## 6. Build, pacote e corrida

**Build e pacote (PowerShell, na raiz):**
```
cmd /c "cd /d C:\Users\Daniel\Desktop\NamixWDDM\driver\umd && call C:\Users\Daniel\Desktop\NamixWDDM\driver\umd\build-umd.bat"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr
powershell -NoProfile -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
python tools\audit-bc250mcdm-safety.py
```

- **Suba o `DriverVer` do INF fonte** (`driver/wdk/package/bc250mcdm.inf`, hoje `3.261.0.0`) antes de cada candidato. O candidato sai `x.y.1.0`; nunca repita um DriverVer já instalado.
- **O empacotador não compila, só copia.** Rode o build de laboratório antes. Depois de rodar suítes de mutação (que mexem nos mtimes), recompile também o SYS, senão o guarda de SYS velho recusa.
- **Confira as marcas novas por string dentro da DLL/SYS empacotados** (`grep -a -c`).

**Corrida:**
- Padrão: `tools/prepare-corridaNN-*.ps1` (fixa hashes KMD/UMD/RADV, roda a auditoria de segurança **e** a do pacote) + `tools/EXECUTAR_CORRIDANN_*.cmd` (define `BC250_WATCHDOG_SECONDS`, até 600, e chama `EXECUTAR_D3D11_NATIVO.cmd`).
- Modelo mais recente: `prepare-corrida100-dx55.ps1` / `EXECUTAR_CORRIDA100_DX55.cmd`.
- **Todo `.cmd` precisa de CRLF.** Gerar com Python usando `newline=''` a partir de texto LF produz LF: foi assim que o harness da Corrida 100 não rodou.

**Candidato atual em disco:**
- lab `3.261.1.0`, SYS `F643EDC9…`, UMD `51288052…` (= DX.55);
- RADV `C:\BC250_LIB\vulkan_radeon.dll` `FCAA11FE…`, fonte em `C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm`;
- **marcador de boot frio armado** (`C:\BC250_REQUIRE_CLEAN_BOOT.json`); BasicDisplay em Código 0.

## 7. Evidência: armadilhas que custaram horas

- **`C:\Windows\Temp` é ilegível nesta sessão** (não elevada), e a listagem volta **vazia sem erro**. Os logs da UMD (`BC250_UMD_LOG_<pid>.txt`) moram lá.
  - Em travamento, o binder não chega a coletar.
  - Peça ao Daniel, em PowerShell **elevado**: `powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\Daniel\Desktop\NamixWDDM\tools\copiar-logs-umd-corrida99.ps1 -Pasta <pasta da corrida>`. O script copia com hash e reseta a ACL.
  - Não passe comandos com `$var` entre aspas duplas para ele colar num PowerShell: a variável é expandida antes. Use o script.
- **O próximo bind arquiva sozinho os logs residuais** em `umd-logs-residuais-do-bind-anterior/` (copia + hash + apaga; herda a ACL).
- **Log do KMD `C:\BC250_DISPLAY_LOG.txt`:**
  - é cumulativo;
  - o início do bind está em `display-log-baseline.json` (`start_offset`);
  - em travamento, recorte do offset até o `AddDevice ENTER` do boot seguinte e arquive como `display-log-this-bind-RECUPERADO.txt` com sha256 (feito para 99 e 100).
- **O log do KMD é amostrado** (8/16 primeiras + periódicas por classe): ausência de linha **não** prova ausência de evento.
- **Suítes de mutação no Windows às vezes falham ao gravar** (`OSError [Errno 22]`). Numa delas a **restauração** falhou e deixou um arquivo mutado; a auditoria pegou. Depois de qualquer erro de suíte, rode a auditoria e `git diff` do arquivo.
- **Heredoc do bash corrompe barras invertidas:** use Write/Edit para conteúdo com `\`.
- **Confira antes de afirmar.** Nesta sessão corrigi três afirmações minhas contra a medição:
  - "logs da 94 não sobreviveram";
  - "o último lote da 99 tinha desenhos agrupados";
  - "o comum que falta na 93".

## 8. Documentos desta sessão

- **Corridas 93–95:** [387](387_CORRIDA93_DX48_RESULTADO_DESKTOP_INTERATIVO_LENTO.md) C93 · [388](388_DX49_VAZAO_DO_DESKTOP_E_CRITERIO_CORRIDA94.md) DX.49 · [389](389_CORRIDA94_DX49_RESULTADO_TRAVAMENTO_E_LOG_DA_UMD_PERDIDO.md) C94 · [390](390_DX49B_LOG_DA_UMD_DURAVEL_E_CRITERIO_CORRIDA95.md) DX.49b · [391](391_CORRIDA95_DX49B_RESULTADO_MAIS_RAPIDO_E_TRAVAMENTO.md) C95.
- **Corridas 96–98:** [392](392_DX50_BISSECCAO_DIRTY_RECTS_E_CRITERIO_CORRIDA96.md) DX.50 · [393](393_CORRIDA96_RESULTADO_E_DX51_CRITERIO_CORRIDA97.md) C96/DX.51 · [394](394_CORRIDA97_RESULTADO_E_DX52_CRITERIO_CORRIDA98.md) C97/DX.52.
- **Auditoria externa:** [395](395_PROMPT_AUDITORIA_EXTERNA_CORRIDAS_93_A_98.md) prompt · [396](396_REVISAO_DAS_AUDITORIAS_EXTERNAS_CHATGPT_GEMINI.md) revisão ChatGPT/Gemini.
- **Corridas 98R–100:** [397](397_DX53_VMID_DESARMADO_ANTES_DE_DEVOLVER_E_CRITERIO_CORRIDA98R.md) DX.53 · [398](398_CORRIDA98R_DX53_RESULTADO.md) C98R · [399](399_DX54_VELOCIDADE_DO_DESKTOP_E_CRITERIO_CORRIDA99.md) DX.54 · [400](400_CORRIDA99_DX54_RESULTADO_TRAVAMENTO_CEDO.md) C99 · [401](401_DX55_BISSECCAO_LOTE_DESLIGADO_CRITERIO_CORRIDA100.md) DX.55 · [402](402_CORRIDA100_DX55_RESULTADO.md) C100.

## 9. Tabela-checklist no fim da sessão

| | Parte | Situação |
|---|---|---|
| ✅ | Adaptador WDDM, VidPn, primária, Present pelo SDMA | P0, estável até o travamento |
| ✅ | D3D11 → Vulkan (harness 60/60) | P0 na 98R; não rodou na 100 (defeito de wrapper) |
| ✅ | Descritores sem custo | 14 ms → ~0,25 ms por desenho |
| ✅ | Aplicativos abrindo device pelo nosso driver | DWM, Explorer, Edge, Claude, Radeon Software |
| 🟡 | Velocidade | ~3 FPS, 1,5× a 98R |
| ❌ | Estabilidade | trava de ~40 s a ~6 min desde a 94 (e na 88/92) |
| ❌ | Piscar da ponte (`COMPOSITE_BLT RECUSADO (resolucao)`) | aparece quando outros apps abrem, mesmo sem lote |
| ❌ | Laço do Explorer | recusa do dxgkrnl a textura SHARED+PRESENT |
| 🔜 | Escolher o próximo corte do travamento (§2.5) | com o Daniel |

**Caminho mais curto:**
1. Fazer o travamento dizer onde para.
2. Corrigir a causa.
3. Parar o laço do Explorer.
4. Religar o lote com segurança.
5. Desktop fluido.
