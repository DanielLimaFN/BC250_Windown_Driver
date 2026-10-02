# IDEIAS DE AVANÇO — auditoria completa e planos A / B / E por etapa

> **Data:** 25/09/2026. **Autor:** Claude (sessão remota, somente análise offline).
> **Base:** commit `fa7f695` do ramo `p6-rendernode-wddm` (Corridas até a **129**, candidato DX.82 / lab 3.287.1.0;
> handoffs [docs/438](docs/438_HANDOFF_2026-09-25_CORRIDAS_111_A_118_VISUAL_VELOCIDADE_E_TRAVAMENTO.md) e
> [docs/459](docs/459_CORRIDA129_DX82_TRAVAMENTO_PRECOCE_ANALISE.md)). A primeira versão deste texto foi escrita sobre o
> commit `8a73051` (Corrida 100); todos os achados abaixo foram **reconferidos no código da DX.82** e as linhas citadas
> são as dele.
> **Natureza:** auditoria de código + plano. **Nenhuma corrida foi feita.** Tudo que depende de silício
> está marcado **NÃO VALIDADO EM HARDWARE**. Vocabulário de confiança: **P0** medido no silício ·
> **P1** lido no código/WDK/upstream · **H** hipótese.
>
> **Como ler os planos:** para cada etapa há
> - **Plano A:** o caminho principal recomendado.
> - **Plano B:** a alternativa, se o A falhar ou for refutado.
> - **Plano E:** saída de emergência / "fora do mapa" (Lei 10): o que fazer se A e B falharem ou o
>   resultado não couber em nenhum dos dois. **Nunca** é WARP, fallback de CPU ou troca de meta.

---

## 0. Resumo executivo

**Onde estamos (P0, docs/387–459):** o desktop do Windows aparece pela BC-250, desenhado pela GPU
(D3D11 → nossa UMD → Vulkan/RADV → WDDM → Present por SDMA), é interativo, e o harness D3D11 fecha
60/60 com pixels conferidos em janelas de 180 s (Corridas 118, 126). A Corrida 127 chegou a 600 s sem
travar, mas com 23 TDRs e o DWM em laço (docs/457). Três bloqueadores:

1. **Travamento duro** (sem BugCheck espontâneo) de ~90 s a ~8 min: 88, 92, 94–97, 99, 100, 108–112,
   115, 128 (~214 s) e 129 (~90 s).
2. **Lentidão:** DWM ~2,7–5,5 Presents/s, harness ~1 FPS; `pfnPresentCb` p50 ~443 ms contra 0,75 ms
   dentro da nossa DDI de Present (docs/437).
3. **Memória e SDMA sob carga:** `VK_ERROR_OUT_OF_DEVICE_MEMORY` e `MakeResident 0x8007000E` no DWM
   (Corrida 129), `IB_TRIPOD TIMEOUT` + `Blt Timeout` dentro do escape e 23 TDRs (Corrida 127),
   367 `Journal NO_SPACE` / 28.534 `PUBLICACAO_FALHA` de GPUVA.

**Os três achados desta auditoria que mais mudam o plano (todos por leitura de código, P1; a
ligação com o travamento é H):**

| # | Achado | Por que importa |
|---|---|---|
| **A1** | O log do KMD faz **abre/grava/flush/fecha arquivo em disco a cada linha**, de dentro das DDIs, em qualquer thread PASSIVE — inclusive nas threads de trabalho do VidSch/VidMm e **com o `ObjectLock` do adaptador segurado** (ex.: `CreateAllocation`). O `FLUSH_TLB` da paginação precisa desse mesmo `ObjectLock`. Não há checagem de APC desabilitada; o logger não usa thread própria (a DX.56 criou uma, mas só para o rastro de paginação). | Casa com a Corrida 129 ponto a ponto: `Allocate` de 4 KiB levou 6,8 s dentro do callback, o Explorer ficou em `WaitPaging` sem retorno e o rastro DX.56 mostra `FLUSH_TLB` em `STATUS_PENDING` com o escritor vivo por mais 12 s (kernel vivo, paginação parada). Também explica perda de log (enquanto há paginação ativa tudo vai para o anel em RAM) e parte da lentidão. **H** até um dump. |
| **A3** | Toda composição do DWM passa por **`pfnEscapeCb`** e o KMD faz o blt SDMA **síncrono** dentro do escape, fora do modelo de residência do WDDM (sem lista de alocações, o VidMm não sabe que a GPU está lendo/escrevendo aquelas páginas). A DX.81 trocou `HardwareAccess` para 0 na ponte, mas `bc250_umd_blt.inl:81` ainda usa 1. | A Corrida 127 mostrou o preço: `IB_TRIPOD TIMEOUT` seguido de `Blt Timeout` **dentro** do escape e 23 TDRs (docs/457). Uma cópia de GPU feita "por fora" do escalonador é o ponto onde memória em movimento e fila travada se encontram. |
| **A6** | Para o travamento, **nenhum dump foi obtido até hoje**. O atalho Ctrl direito + Scroll Lock ×2 está armado desde a 116 (`tools/arm-crash-dump-on-hang.ps1`), mas os resultados das 128/129 não registram se foi tentado nem se o teclado respondeu. As migalhas em VRAM (DX.66) e o rastro de paginação (DX.56) já existem e dizem "a paginação parou", não **quem** a segurou. | O próximo salto de diagnóstico não é mais log: é a pilha de todas as threads (`!locks`, `!stacks`). Se o atalho não funciona, o passo seguinte é depurador de kernel pela rede (KDNET). |

**Recomendação de ordem:** (1) offline, a faixa "logger assíncrono" (A1, reaproveitando a thread da
DX.56) — é correção de defeito certo e também ganho de velocidade; (2) corrida com **só** essa mudança
sobre a DX.82, com o protocolo de dump de §5 **obrigatório e registrado**; (3) depois, uma variável
por vez: A2, memória (A5), e a saída do escape (Etapa 5).

---

## 1. Estado atual medido (P0)

| | Parte | Situação | Fonte |
|---|---|---|---|
| ✅ | Adaptador WDDM 2.7, VidPn, primária, Present por SDMA | estável até o travamento | docs/387, 402 |
| ✅ | D3D11 → Vulkan (harness 60/60, pixel central e canto conferidos nos dois lados da cópia) | P0 nas 118, 126, 128 | docs/437, 454, 457 |
| ✅ | 600 s sem travar (uma vez) | P0 na 127, **com 23 TDRs** | docs/457 |
| ✅ | Anel gráfico GFX (CP_RB0) inteiro, preempção concluída | P0 nas 90–92 | docs/381, 385 |
| ✅ | TDR: detecção → reset → restart → readmissão | P0 (07/09) | docs/188 |
| ✅ | Pool de descritores persistente | 14 ms → 0,25 ms/draw | docs/402 |
| 🟡 | Velocidade | DWM 2,7–5,5 Presents/s; harness ~1 FPS; `pfnPresentCb` p50 ~443 ms | docs/437, 450 |
| ❌ | Estabilidade | trava de ~90 s a ~8 min (até a 129) | docs/459 |
| ❌ | Memória | `OUT_OF_DEVICE_MEMORY` / `0x8007000E` no DWM; GPUVA `NO_SPACE` | docs/457, 459 |
| ❌ | Laço do Explorer | `pfnAllocateCb` → `E_INVALIDARG` na textura SHARED+PRESENT (conferir se persiste depois da DX.64) | docs/396 |
| 🟡 | Visual | triângulo ausente na 117, presente nas 118/126; intermitência sem causa | docs/435, 437 |
| ❌ | TDR com display sobrevivendo | NÃO VALIDADO EM HARDWARE | docs/188 |
| ❌ | Jogos (Vulkan ou D3D11) | NÃO VALIDADO EM HARDWARE | — |

---

## 2. Auditoria — achados por severidade

Cada achado: **onde**, **o que está errado**, **confiança**, **efeito provável**, **correção** e
**teste que fecha** (Lei 3).

### 🔴 Críticos (podem explicar o travamento ou corromper estado)

#### A1 — Log do KMD faz I/O síncrono de disco por linha dentro das DDIs
- **Onde:** `driver/wdk/bc250_display_ddi.c:1217–1297` (`Bc250DisplayLog`).
- **O que está errado (P1):**
  - para cada linha em PASSIVE: `ZwCreateFile` → drena o anel → `ZwWriteFile` → `ZwFlushBuffersFile` → `ZwClose`;
  - a única proteção é `g_Bc250PagingActiveCount` (só o `BuildPagingBuffer`, `bc250_mcdm_callbacks.c:1315`) e `IRQL > PASSIVE`;
  - **não protege** as outras DDIs chamadas pelas threads de trabalho do dxgkrnl (VidSch chama `SubmitCommandVirtual` da paginação; VidMm chama Create/Open/Destroy/Close de alocação, `UpdatePageTable` etc.);
  - é chamado com os **nossos** locks segurados — por exemplo `KeEnterCriticalRegion` + `ExAcquirePushLockExclusive(&context->ObjectLock)` no `CreateAllocation` (`bc250_active_miniport.c:5072–5082` em diante; varredura desta auditoria: ao menos 5 regiões e ~24 chamadas de log sob `ObjectLock`);
  - o `FLUSH_TLB` da paginação (`Bc250ActiveFlushTlbHandler`, `bc250_active_miniport.c:14202`, lock em `:14234`) espera **o mesmo** `ObjectLock` exclusivo: enquanto uma DDI qualquer faz flush de disco segurando o lock, a paginação para;
  - não existe `KeAreAllApcsDisabled()`; a única thread de sistema do KMD é a do rastro DX.56 (`bc250_mcdm_callbacks.c:275`), que não é usada pelo logger;
  - usa 512 + 4096 bytes de pilha antes de entrar no NTFS (pilha de kernel é limitada; `ZwCreateFile` desce por filtros e storport).
- **Efeito provável (H forte):**
  - **Corrida 129, lida com este achado:** `Allocate` de 4 KiB ficou 6,8 s dentro do callback (P0; que o tempo tenha ido em flush de disco sob `ObjectLock` é H — o `CreateAllocation` segura esse lock e loga), o Explorer entrou em `WaitPaging` fence 7004 sem retorno, e o rastro DX.56 terminou com `FLUSH_TLB` em `STATUS_PENDING` e o escritor ainda vivo por 12,3 s — isto é, kernel vivo e paginação esperando. É o desenho que A1 prevê; não é prova (falta a pilha);
  - thread do escalonador presa em I/O de disco → nenhum progresso de GPU → sem TDR (quem detecta TDR é a `VidSchiWorkerThread`, vista nas pilhas dos 0x116 de 12–13/09) → tela e mouse congelados;
  - com NTFS/flush envolvido, o disco pode travar junto → log de eventos vazio, guardião sem agir, logs da UMD sem cópia: a assinatura repetida de 88 a 129;
  - "o log termina no meio de uma submissão SDMA de paginação" é o que se espera se a última escrita síncrona for a que travou;
  - aparece mais cedo com mais carga (mais linhas por segundo).
- **Efeito certo (P1):** custo de latência em toda DDI que loga (open+flush+close por linha); com ~848 sítios de log, é um freio de vazão do desktop.
- **Correção (faixa proposta, nome sugerido "DX.83 — logger assíncrono"):**
  1. `Bc250DisplayLog` passa a **só** formatar e copiar para um anel em memória não paginada (spinlock curto), em qualquer IRQL ≤ DISPATCH;
  2. uma **thread de sistema** drena o anel para o arquivo a cada ~50–100 ms, com arquivo aberto uma vez — reaproveitar o padrão já provado do escritor DX.56 (`bc250_mcdm_callbacks.c:134–300`, arranque em `bc250_active_miniport.c:16452`), sem segunda infraestrutura;
  3. o drenador ignora nada: se o anel encher, grava marcador `[LOG_OVERRUN n linhas]` (perda declarada, Lei 1);
  4. linhas "críticas" continuam síncronas **somente** em DDIs sabidamente fora do escalonador (Add/Start/Stop/RemoveDevice) — lista explícita, com portão de auditoria;
  5. teardown: `StopDevice` sinaliza, espera a thread e fecha o arquivo.
- **Teste que fecha:** Corrida com o mesmo candidato + só esta mudança. Critério: janela inteira (10 min) sem travamento, **ou** travamento com dump 0xE2 mostrando outra pilha. Portão novo `audit_kmd_log_never_does_io_in_ddi` com mutação (reintroduzir `ZwWriteFile` no caminho da DDI reprova).

#### A2 — Log em DIRQL dentro do callback sincronizado de VSync
- **Onde:** `driver/wdk/bc250_display_ddi.c:1436` dentro de `Bc250SynchronizedVsyncCallback` (`:1397`), que roda sob `DxgkCbSynchronizeExecution` (IRQL do dispositivo, com o spinlock de interrupção segurado).
- **O que está errado (P1):** `Bc250DisplayLog` → `KeAcquireSpinLock(&g_Bc250PagingLogLock)` em IRQL **acima** de DISPATCH. `KeAcquireSpinLock` só é válido em IRQL ≤ DISPATCH: em build livre ele **baixa** o IRQL para DISPATCH enquanto o spinlock de interrupção está segurado, e o `Release` volta ao DIRQL. Driver Verifier acusaria na primeira execução. Executa a cada 60 callbacks (≈ 0,25–1 s), a corrida inteira.
- **Efeito provável (H):** janela para a mesma CPU reentrar no ISR do dispositivo com o spinlock de interrupção já segurado → deadlock em DIRQL = travamento sem BugCheck. É menos provável que A1 (depende de interrupção real chegar), mas é defeito certo.
- **Correção:** tirar o log de dentro do callback; o callback só grava contadores (`InterlockedIncrement`), e a linha `[Vsync #n]` sai do `Bc250VsyncDpcRoutine` **depois** de `DxgkCbSynchronizeExecution` retornar (em DISPATCH, para o anel). Portão `audit_no_log_inside_synchronize_callback` (varre as funções passadas a `DxgkCbSynchronizeExecution`: `Bc250SynchronizedVsyncCallback` e `Bc250ActiveSynchronizedFenceCallback`).
- **Teste que fecha:** offline, portão + mutação; em hardware, rodar uma vez com **Driver Verifier** (`verifier /standard /driver bc250mcdm.sys`) — ver Etapa 1, Plano B.

#### A3 — Composição por escape: blt síncrono fora do modelo de residência
- **Onde:** `driver/umd/bc250_d3d_umd.c:5093` (`Bc250UmdBridgeLinearToWddm`, `HardwareAccess=0` desde a DX.81 em `:5142`) e `driver/umd/bc250_umd_blt.inl:81` (**ainda** `HardwareAccess=1`); handler `driver/wdk/bc250_active_miniport.c:15519` em diante (resolve GPUVA→MC e chama `Bc250SdmaPresentBlt`, que espera a conclusão dentro do escape).
- **O que está errado (P1 no código; efeito H):**
  - o escape é uma cópia de GPU **fora** do modelo de residência: nenhuma lista de alocações, nenhuma referência para o VidMm. Com `HardwareAccess=1` a paginação ficava congelada durante a cópia (caro); com `HardwareAccess=0` (DX.81) ela **não** fica — a alocação pode ser movida entre a resolução GPUVA→MC e a execução do SDMA. Os `RECUSADO (resolucao) status=9` da Corrida 100 e as recusas por geometria da 129 mostram que as alocações se movem (P0);
  - a cópia entra no **mesmo** anel SDMA das submissões do escalonador, mas por fora dele: um timeout na fila (127: `IB_TRIPOD TIMEOUT`) deixa o escape esperando dentro da DDI, e o escalonador não sabe que existe trabalho ali;
  - `bc250_umd_blt.inl:81` ainda pede `HardwareAccess=1` — dois comportamentos para o mesmo tipo de escape.
- **Efeito provável (H):** cópia sobre memória que o VidMm já moveu (imagem errada ou pior, escrita em página reutilizada) e cadeia timeout → reset → TDR como na 127.
- **Correção:** ver Etapa 5 (Present no modelo WDDM). Imediato e barato: unificar a flag nos dois escapes e refazer a resolução GPUVA→MC **sob o lock do anel SDMA**, imediatamente antes de montar o pacote.
- **Teste que fecha:** Etapa 5.

#### A4 — Perda de `DMA_COMPLETED` quando o pino de GPUVA diverge
- **Onde:** `driver/wdk/bc250_active_miniport.c:10812–10850` (ramo de dreno de `Bc250ActiveSynchronizedFenceCallback`, `:10653`).
- **O que está errado (P1):** `Bc250WddmFenceTrackerDrainEx` (`bc250_submission_engine.c:2254`) **já removeu** `count` entradas e já avançou `LastNotifiedFenceIdByNode`. Se a entrada `i` tiver geração de pino divergente, o laço faz `return FALSE`: as entradas `i..count-1` **nunca** são notificadas ao dxgkrnl, e as `0..i-1` notificadas não são contadas (`sync->Notified` fica zero).
- **Efeito:** fence que a GPU concluiu e o Windows nunca fica sabendo → TDR mais tarde, ou espera infinita num `WaitForSynchronizationObject`. Latente (ainda não observado).
- **Correção:** a divergência de pino desliga `GpuvaMappingsOperational` (fail-closed, correto) mas **continua notificando** todas as entradas drenadas — o trabalho da GPU terminou de fato; notificar é dizer a verdade. `sync->Notified = i` no caminho de erro, se se preferir abortar.
- **Teste que fecha:** caso novo em `active_miniport_test.c` (três entradas, a do meio com geração errada → três notificações, `GpuvaMappingsOperational=0`); mutação que volta ao `return FALSE` reprova.

#### A5 — Segmento local de ~239 MB (todo CPU-visível) para 16 GB de GDDR6
- **Onde:** `driver/wdk/bc250_mcdm_callbacks.c:2709` e `:2754` (Segmento 1, `CpuVisible=1`), `:2720` (abertura de 2 GB); tamanho medido na DX.67: `SegBytes=0xEF74000` ≈ 239 MB (docs/423), com a UMA do BIOS em 512 MB (docs/421).
- **O que está errado (P1 + P0 do tamanho):** o VidMm só enxerga ~239 MB de memória local. Com DWM + Explorer + Edge abertos ele pagina o tempo todo, e já falta memória: na Corrida 129 o DWM recebeu `VK_ERROR_OUT_OF_DEVICE_MEMORY` e `MakeResident 0x8007000E` (P0, docs/459). As submissões de paginação são exatamente onde os travamentos terminam.
- **Correção possível (a medir):** (1) subir a UMA do BIOS (512 MB → 2–4 GB) e declarar a parte da reserva **além** da janela de 256 MB do BAR como **segmento local não CPU-visível** (WDDM permite; só o que precisa de `Lock` pela CPU tem de ser CPU-visível); (2) conferir se o `bc250_rebar_reader.c` vê BAR redimensionável. Cuidado já pago: o TMR e as folgas internas precisam continuar fora do segmento (DX.67).
- **Teste que fecha:** `QUERYSEGMENT4` com dois segmentos locais no log, `BuildPagingBuffer`/min caindo em pelo menos metade na mesma carga, zero `status=9` na abertura de apps.

### 🟠 Altos (quebram jogos ou a reprodutibilidade)

#### A6 — Nenhum dump de travamento até hoje
- **Onde:** `tools/arm-crash-dump-on-hang.ps1` (desde 12/09); protocolo citado nos critérios 116–127 (docs/432–455), **ausente** dos critérios 128/129 (docs/456, 458) e sem registro de tentativa nos resultados (docs/457, 459).
- **Correção:** tornar o protocolo obrigatório em todo critério e registrar sempre um de três fatos: "dump gerado", "atalho sem resposta (Num Lock também sem resposta)", "não tentado". Ver Etapa 1.

#### A7 — Tabela D3D11 da UMD só preenche o layout do D3D10.1
- **Onde:** `driver/umd/bc250_d3d_umd.c:5081–5087` (`Bc250UmdPopulateDeviceFuncs11` faz `memset` e chama o populador 10.1).
- **O que está errado (P1):** todas as entradas **exclusivas** do D3D11 ficam **NULL**: compute shader (`pfnCreateComputeShader`, `pfnCsSetShader`, `pfnDispatch`), UAV, hull/domain shader, contexto diferido, `pfnCopyStructureCount`, etc. O runtime D3D11 não valida a tabela: um jogo que usar qualquer uma delas faz access violation dentro do runtime, não recebe erro.
- **Correção:** toda entrada D3D11 aponta para uma função real; as que ainda não têm backend chamam `pfnSetErrorCb(E_NOTIMPL)` com log `RECUSADO` (Lei 4: recusa honesta, não sucesso silencioso). Portão: "nenhum ponteiro NULL na tabela 11" verificado por teste que percorre a struct inteira.
- **Teste que fecha:** teste offline em `bc250_d3d_umd_test.c` que varre `D3D11DDI_DEVICEFUNCS` (todos os ponteiros ≠ NULL).

#### A8 — `D3D10_0` anunciado, mas o `CreateDevice` escreve a tabela 10.1 no ponteiro do 10.0
- **Onde:** `driver/umd/bc250_d3d_umd.c:6581` anuncia `D3D10_0_DDI_SUPPORTED`; `:6222` ("Fallback: popula p10_1DeviceFuncs") trata qualquer outra interface como 10.1.
- **O que está errado (P1):** para um app D3D10.0 o runtime entrega `p10DeviceFuncs` (struct menor); escrever a tabela 10.1 nele sobrescreve memória do runtime. O mesmo vale para qualquer interface desconhecida.
- **Correção:** tirar `D3D10_0_DDI_SUPPORTED` da lista **ou** implementar o populador 10.0; interface desconhecida → `E_NOINTERFACE` com log. Conferir também se a versão da tabela DXGI entregue (`pDXGIDDIBaseFunctions6_1`, `:6228`) depende da interface pedida (H; conferir em `d3d10umddi.h`/`dxgiddi.h`).
- **Teste que fecha:** teste offline com `Interface = D3D10_0_DDI_INTERFACE_VERSION` e sentinela depois da struct menor.

#### A9 — Instalação não serve jogos: sem Vulkan registrado e sem UMD 32 bits
- **Onde:** `driver/wdk/package/bc250mcdm.inf:48–49`.
- **O que está errado (P1):** (a) não há `VulkanDriverName`/`VulkanDriverNameWow` — o loader Vulkan do Windows descobre o ICD pela chave do adaptador; sem ela, jogo Vulkan não acha a BC-250; (b) `UserModeDriverNameWow` aponta para a **mesma** `bc250umd.dll` de 64 bits: todo processo D3D de 32 bits (muitos jogos, launchers) falha ao carregar a UMD.
- **Correção:** Etapa 8.

#### A10 — Caminhos de DLL fixos e graváveis dentro de todo processo D3D (dwm.exe incluído)
- **Onde:** `driver/umd/bc250_umd_vulkan_bridge.c:84–107` e `driver/umd/bc250_d3d_umd.c:444–470`.
- **O que está errado (P1):**
  - ordem de carga do RADV: registro → pasta da UMD → **`C:\Users\Daniel\Desktop\...\mesa-radv-wddm\build\...\vulkan_radeon.dll`** → `C:\BC250_LIB` → busca padrão do sistema;
  - **reprodutibilidade:** se a pasta de build existir, o RADV que roda é o de build, **não** o `C:\BC250_LIB` com hash `FCAA11FE…` registrado no handoff. Um rebuild do Mesa depois do `prepare-corridaNN` muda o RADV da corrida sem mudar o que foi auditado (conferir se o portão DX.40 cobre isso na hora da carga, não só no pré-voo);
  - **segurança:** `C:\BC250_LIB` e a pasta do Desktop herdam ACLs em que usuário comum grava (H — conferir com `icacls`); qualquer DLL plantada ali roda dentro do `dwm.exe`. `LoadLibraryA("vulkan_radeon.dll")` sem caminho usa a busca padrão.
- **Correção:** um caminho só, vindo do INF (DriverStore) e registrado pelo `IcdPath`; sem caminho de desenvolvimento no binário de pacote; recusa com log se o hash não bater (fail-closed). Para laboratório, um tunable explícito.

### 🟡 Médios

| # | Onde | Achado (P1) | Correção |
|---|---|---|---|
| A11 | `bc250_d3d_umd.c:444–480` | Inicialização do tradutor de shader sem exclusão: duas threads veem `-1`, as duas carregam a DLL e escrevem a mesma struct global. | `InitOnceExecuteOnce`. |
| A12 | `bc250_display_ddi.c:1230` | `_vsnprintf` devolve −1 quando a linha passa de 511 bytes e a função **descarta a linha inteira**, calada. Estimativa por formato: 8 linhas podem passar do limite (ex.: `KMD_GFX_SUBMIT`, `AVISO CONTEXT SWITCH COM IN_FLIGHT`, `bc250_active_miniport.c:12364/13303`). Ausência de linha deixa de ser conclusiva. | `RtlStringCbVPrintfA` + marcador `[TRUNCADA]`. Resolve-se junto com A1. |
| A13 | `.cmd`/`.bat` no índice do git | Estavam com **LF** no repositório (inclusive `tools/BC250_WINRE_CLEAN.cmd`, o script de emergência do WinRE). Quem baixar do GitHub para um pendrive leva um script que o cmd.exe lê errado. | **Corrigido nesta sessão:** `.gitattributes` com `eol=crlf`. A auditoria de segurança passou de FAILED (só CRLF) para PASS num checkout limpo. |
| A14 | `bc250_display_ddi.c`, `bc250_active_miniport.c` | Estado de adaptador em globais únicos (`g_Bc250DisplayState`, `g_Bc250SdmaPresent`) com diretório de adaptadores ao lado. Uma segunda instância (AddDevice #2, visto nas 88/92) divide o mesmo anel SDMA. | Mover para o contexto do adaptador na refatoração da Etapa 5. |
| A15 | `bc250_active_miniport.c` (16.962 linhas) | Arquivo único com DDIs, submissão, GPUVA, TDR, escape, diagnóstico. Difícil de auditar e de revisar mudança de uma variável. | Partir por responsabilidade (§3.4), sem mudar comportamento, com build e suítes iguais antes/depois. |
| A16 | `bc250_mcdm_callbacks.c:2718–2723` | Abertura de 2 GB em base lógica fixa `0x100000000`, sem derivar do `GCMC_VM_SYSTEM_APERTURE_*`. PTE de sistema = PFN físico com SYSTEM+SNOOPED (correto em princípio, `:303`). Não há prova de silício de acesso da GPU à memória de sistema sob carga. | Teste dirigido: alocação forçada no segmento 2, blt SDMA com as três pernas do tripé. Se falhar, a GPU pode estar escrevendo em RAM do Windows — candidato de travamento sem BugCheck (H). |

### ⚪ Higiene do repositório

- **Raiz poluída:** ~40 artefatos soltos (`dump_*.txt`, `work_*.log`, `f3-*.log`, `call_ddi*.txt`, `temp_test.c`, `comp.spv`, `tmp_evidence.txt`…). Mover para `docs/evidence/` ou `DocsObsoletos/`.
- **Binários de terceiros:** `firmware/vbios_bc250.bin` (dump de VBIOS) e blobs `cyan_skillfish2_*.bin` versionados. Os blobs do linux-firmware exigem o `LICENSE.amdgpu` junto; o VBIOS provavelmente não é redistribuível. Decidir antes de o repositório ficar público.
- **`BC-250 Adrenaline/`** é um app Node (UI de doações) dentro do repositório do driver. Separar.
- **Documentação de governança pesada:** `CLAUDE.md` carrega ~600 linhas de histórico, `BASE_DE_AVANCO.md` tem 5.058 linhas. O próprio CLAUDE.md diz que o histórico não é modelo. Proposta: mover o registro histórico para `docs/HISTORICO_STATUS.md` e deixar no CLAUDE.md só leis + diretriz vigente; na BASE, um "estado atual" de uma página no topo. Menos contexto gasto por sessão = menos erro.
- **CI:** o núcleo `driver/core/*.c` compila com gcc (medido nesta sessão, só avisos de tipos do Windows), e as auditorias Python rodam em Linux. Um workflow do GitHub Actions com as auditorias + as suítes portáveis pegaria regressão antes de empacotar.

---

## 3. Inventário módulo a módulo

Classificação: **Núcleo** (provado e em uso), **Ativo** (em uso, com dívida), **Diagnóstico**,
**Legado** (não participa do caminho atual).

### 3.1 KMD — `driver/wdk/` (miniport WDDM 2.7)

| Módulo | Linhas | Papel | Classe | Observações da auditoria |
|---|---:|---|---|---|
| `bc250_active_miniport.c/.h` | 16.962 | DDIs ativas: alocação, contexto, submissão virtual Node0/Node1, GPUVA, retirement, TDR, escape | Ativo | A3, A4, A14, A15. Blt síncrono no escape. |
| `bc250_mcdm_callbacks.c/.h` | 3.574 | Tabela DXGK, QAI (segmentos, caps), `BuildPagingBuffer`, encode de PTE, ISR/DPC | Ativo | A5, A16. ISR nunca instalado (`g_Bc250InterruptHandler` sempre NULL) — tudo por polling/timer; coerente, mas deve estar escrito como decisão. |
| `bc250_display_ddi.c/.h` | 4.664 | VidPn, Present, VSync por timer, **logger** | Ativo | **A1, A2, A12**. |
| `bc250_submission_engine.c/.h` | 3.269 | Anel MEC/GFX, rastreador de fences WDDM | Núcleo | Lock com teto de voltas (bom). |
| `bc250_sdma_present.c/.h` | 3.057 | Anel SDMA do present, poll, flush HDP | Núcleo | Poll síncrono em DISPATCH (dívida A15 de 07/09 ainda de pé). |
| `bc250_sdma_ring_work.c` | 669 | Trabalho no anel SDMA | Núcleo | — |
| `bc250_compute_service.c/.h` | 1.569 | Serviço de compute (MEC/MQD) | Núcleo | — |
| `bc250_pfp_direct_load.c` | 1.503 | Carga direta PFP/ME | Núcleo (boot) | — |
| `bc250_probe_telemetry.c/.h` | 3.415 | Sondas de registro | Diagnóstico | Grava em PASSIVE; conferir se nunca roda em thread de escalonador. |
| `bc250_boot_fuse.c/.h` | 536 | Fusível anti-loop de boot | Núcleo (segurança) | Funcionou em todas as corridas. |
| `bc250_dxgk_readonly_ddi.c`, `bc250_readonly_miniport.c` | ~1.100 | Perfil somente leitura | Legado/ativo em produção | — |
| `bc250_mmio_topology_transport.c`, `bc250_pci_bar_reader.c`, `bc250_rebar_reader.c`, `bc250_cm_resource_adapter.c`, `bc250_start_device_adapter.c` | ~1.500 | Descoberta de BAR/MMIO | Núcleo (boot) | ReBAR: usar na Etapa 4 (A5). |
| `bc250_gcvm_backing_transaction.c`, `bc250_vram_probe_transaction.c`, `bc250_post_display_reservation.c`, `bc250_cp_firmware_manifest.c` | ~1.200 | Transações de boot | Núcleo | — |

### 3.2 Núcleo portável — `driver/core/` (≈70 contratos)

Contratos puros em C, testados offline (`driver/tests/`). Grupos:

| Grupo | Arquivos | Classe | Observação |
|---|---|---|---|
| Memória/GPUVA | `gpuva_space`, `gpuva_journal`, `gpuva_snapshot`, `gpuva_sdma_*`, `gcvm_*`, `segment_*`, `vram_*`, `vmid_pool`, `staging_lease`, `address`, `resource` | Núcleo | Fonte única de geometria de VA desde A11. |
| Anéis e pacotes | `pm4_packet_builder`, `sdma_packet_builder`, `gfx_ring_*`, `persistent_ring`, `sdma_queue_state`, `sdma_f32_transaction` | Núcleo | `RB_BUFSZ` corrigido na DX.46 (P0). |
| Gráficos | `gfx_draw_initiator`, `gfx_primitive_assembler`, `gfx_viewport_scissor`, `gfx_sh_reg_packet`, `gfx_shader_*`, `gfx_observable_shader`, `gfx_ib_inspect_contract` | Ativo/Diagnóstico | Parte é de marcos próprios; o desenho real do desktop vem do RADV. Marcar o que é só prova para não confundir com backend. |
| Compute | `compute_*`, `context_dispatcher`, `observable_shader`, `isa_validator` | Núcleo | — |
| Firmware/PSP/SMU | `psp_*`, `firmware_staging`, `smu_telemetry`, `cpu_smu`, `gpu_clock`, `power`, `golden_settings`, `discovery_tmr_reader`, `ip_discovery` | Núcleo (boot) | — |
| WDDM ABI | `allocation_abi`, `umd_private`, `driver_caps`, `adapter_info`, `mcdm_object`, `escape_contract.h`, `submit_command`, `paging*`, `preemption`, `tdr_recovery`, `fault_latch`, `blt_proof` | Núcleo | O escape (`escape_contract.h`) sai do caminho quente na Etapa 5. |
| Tradução de shader | `dxbc_parser`, `dxbc_spirv_bridge` | Legado parcial | O caminho vigente usa `libvkd3d-shader` (DLL externa). Decidir se o parser próprio fica. |
| Plataforma | `superio_nct6686`, `cmos_vram`, `platform_register_snapshot`, `owned_adapter`, `runtime_inventory`, `ih_*` | Diagnóstico/boot | IH existe como contrato, mas o ISR não é instalado — ligar interrupções é pré-requisito de desempenho de longo prazo (§Etapa 4 Plano E). |

### 3.3 UMD — `driver/umd/`

| Módulo | Linhas | Papel | Classe | Observações |
|---|---:|---|---|---|
| `bc250_d3d_umd.c/.h` | ~8.100 | DDIs D3D10.1/11 + DXGI, Present, ponte | Ativo | A3, A7, A8, A11. |
| `bc250_umd_vulkan_bridge.c/.h` | ~1.520 | D3D→Vulkan (RADV), lote, staging | Ativo | A10. |
| `bc250_umd_resource_vk.c`, `bc250_umd_descriptors.c`, `bc250_umd_pipeline_cache.c`, `bc250_umd_state_translate.c` | ~2.500 | Recursos, descritores, pipelines, estado | Ativo | Pool persistente (DX.54) validado. Falta cache em disco de pipeline (latência do DWM na 1ª composição). |
| `bc250_wddm_winsys.c/.h` | 1.229 | `pfn*Cb` do runtime, trace durável | Ativo | Trace com write-through ~0,2 ms/linha, ~25 linhas/draw (custo declarado no doc 403). |
| `bc250_umd_blt.inl` | — | Blt por escape | Ativo | A3. |
| `bc250_vkd3d_shader_adapter.c` | — | Carga dinâmica do vkd3d-shader (LGPL) | Ativo | A10/A11. Três DLLs MinGW no dwm.exe (risco já declarado na BASE). |
| `bc250_vulkan_icd.c`, `bc250_vulkan.h`, `bc250_icd.json`, `bc250_runtime.c` | ~2.700 | ICD/runtime antigo próprio | Legado? | Conferir se ainda é carregado; se não, arquivar. |
| `bc250_llm_kernels.c/.h` | — | Kernels de LLM | Fora do escopo (Lei do escopo §2) | Não deve estar no pacote do driver. |
| `bc250_umd_caps_probe.c` | — | Sonda de caps | Diagnóstico | O `D3D11CapsProbe` já foi bloqueador; manter desarmado por padrão. |

### 3.4 Outros

| Pasta | Papel | Observação |
|---|---|---|
| `driver/tests/` | ~60 suítes offline | Boas; A4/A7/A8 pedem casos novos. |
| `dbg/` | Driver KMDF de laboratório (`hw.c` 5.813 linhas) | Legado do bring-up; não participa do WDDM. |
| `tools/` | 307 arquivos, 58 auditorias Python, binder, guardião, prepare/EXECUTAR por corrida | Os `EXECUTAR_CORRIDAxx`/`prepare-corridaxx` são descartáveis após a corrida: arquivar por faixa para não ter 30 scripts quase iguais. |
| `inc/linux_ref/` | Máscaras/offsets do upstream | Fonte P1 da Lei 7. |
| `docs/` | 400+ documentos | Índice por tema faz falta (o README de docs lista?). |

**Refatoração sugerida do `bc250_active_miniport.c` (sem mudar comportamento):**
`bc250_alloc_ddi.c` (Create/Open/Close/Destroy/Describe), `bc250_context_ddi.c` (Device/Context/Process),
`bc250_submit_node0.c`, `bc250_submit_node1.c`, `bc250_retirement.c` (DPCs, rastreador, notify),
`bc250_escape.c`, `bc250_gpuva_ddi.c`, `bc250_tdr.c`. Um arquivo por commit, com build e todas as
suítes iguais antes e depois.

---

## 4. Etapas seguintes — planos A / B / E

Cada etapa: **objetivo**, **critério de saída** (teste nomeado + evidência), planos, **teto de
corridas** e **critério de abandono** (Lei 9). A ordem é a do caminho mais curto para "desktop
mínimo funcional → jogo real".

### Etapa 1 — Tornar o travamento observável (zero risco, zero código novo de driver)

- **Objetivo:** saber **onde** a máquina para.
- **Critério de saída:** um dump 0xE2 do travamento arquivado em `docs/evidence/<corrida>/` com
  `!analyze -v`, `!locks`, `!stacks 2 dxgkrnl`, `!stacks 2 bc250mcdm`, `!running -ti`, `!dpcs`
  **ou** a prova de que o teclado não responde (o que por si já classifica o travamento).
- **Plano A — dump forçado por teclado:**
  1. rodar `tools\arm-crash-dump-on-hang.ps1` (sem argumento mostra o estado; `-Arm` arma), reiniciar;
  2. conferir `CrashDumpEnabled=2`, pagefile em C: ≥ tamanho do dump de kernel, espaço livre;
  3. vale para **toda** corrida a partir de agora (não gasta corrida própria): o próximo travamento, qualquer que seja o candidato, tem de chegar com um destes três registros — "dump gerado", "atalho sem resposta", "não tentado";
  4. no travamento: **antes** de qualquer coisa, apertar Num Lock (o LED muda? → o kernel responde a interrupção de teclado); depois **Ctrl direito + Scroll Lock ×2**;
  5. se aparecer a tela azul 0xE2 e o dump for gravado: análise offline (comando do §3 do CLAUDE.md + os comandos acima).
  - **Leitura dos desfechos:** dump com thread em `ZwWriteFile`/`ZwFlushBuffersFile` segurando `ObjectLock` e a thread de paginação esperando o mesmo lock → confirma A1. Dump com threads esperando dentro do escape/SDMA → A3. CPU presa em DIRQL no nosso código → A2. Threads da UMD (`vkDeviceWaitIdle`, `WaitPaging`, SRWLOCK da DX.82) esperando entre si sem nenhuma thread de kernel presa → o travamento é de user mode + GPU parada (docs/459 §2). Nenhum destes → **E**, descrever antes de julgar.
- **Plano B — o teclado não gera dump (kernel em IRQL alta ou barramento travado):**
  - rodar uma vez com **Driver Verifier** no `bc250mcdm.sys` (`/standard` + IRQL checking), que transforma A2 e violações parecidas em BugCheck com pilha imediata;
  - as migalhas em VRAM (DX.66) e o rastro de paginação (DX.56) **já existem**; o que falta nelas é o *quem*: acrescentar à migalha o TID e o nome da DDI de quem **segura** o `ObjectLock` (gravado ao adquirir, apagado ao soltar). Com isso, "FLUSH_TLB pendente" passa a vir com "à espera de `CreateAllocation` do TID x".
- **Plano E — nem teclado nem migalhas bastam:**
  - segundo computador: **KDNET** (depurador de kernel pela rede) — no travamento, `break` e `!stacks 2`, `!locks`. É o instrumento definitivo; custa configurar uma placa de rede suportada pelo KDNET;
  - dump por **NMI** (se a placa tiver botão/pino; `NMICrashDump=1`);
  - heartbeat externo: `ping -t` de outra máquina durante a corrida (pilha de rede viva = kernel vivo; na 129 o escritor DX.56 vivo já sugere isso).
- **Teto:** não consome corrida própria (vai junto de qualquer corrida). **Abandono:** se em 2 travamentos o atalho não responder, montar o KDNET antes do terceiro.

### Etapa 2 — Remover as causas prováveis do travamento (uma variável por corrida)

- **Objetivo:** 3 corridas seguidas de 10 min sem travamento, com a carga normal (DWM + Explorer + 1 app).
- **Critério de saída:** `RECOVERY_OK_ONLINE` do guardião nas três, zero Kernel-Power 41, log do KMD
  completo até o StopDevice, logs da UMD coletados.
- **Plano A — logger assíncrono (A1) + fora do DIRQL (A2):** faixa descrita em A1/A2 (sugestão de nome: DX.83). É
  correção de defeito com ou sem dump. A2 entra junto **só** se o Daniel autorizar duas variáveis
  (ambas são "o log do KMD"); senão A1 primeiro, A2 na corrida seguinte.
  - **Assinatura A-confirmada:** 10 min sem travamento. **Implicação:** I/O no escalonador era a causa.
  - **Assinatura A-refutada:** trava de novo com o log agora completo até ~100 ms antes. **Implicação:** o log passa a dizer onde parou; seguir para o B com o dado.
- **Plano B — tirar a pressão de memória (A5):** subir a UMA do BIOS e declarar o excedente como
  segmento local não CPU-visível. É a outra explicação concorrente para "a paginação parou" (VidMm
  sem espaço, `OUT_OF_DEVICE_MEMORY` na 129). Mesma regra: uma variável, e só depois de A1 — com o
  logger síncrono ainda ativo, mais paginação também significa mais flush de disco sob lock.
- **Plano E — reduzir a carga até parar de travar (bissecção de carga, não de código):**
  1. corrida sem Explorer (shell substituto: `Shell=cmd.exe` numa conta de teste) → separa "rotatividade de contextos" do resto;
  2. corrida sem `Radeonsoftware.exe` (a 109 já mostrou que ele não é condição necessária; manter desligado para reduzir ruído);
  3. corrida só com o harness D3D11 (sem DWM acelerado: `D3D11LabPromotion` desarmado).
  Cada uma responde uma pergunta; nenhuma troca a meta.
- **Teto:** 4 corridas. **Abandono:** se A e B não mudarem o tempo até o travamento, parar de mexer
  em código e voltar à Etapa 1 Plano E (KDNET).

### Etapa 3 — Laço do Explorer (textura SHARED+PRESENT recusada)

- **Primeiro:** conferir nos logs das 126–129 se o laço ainda existe depois da DX.64 (que eliminou a recriação de device na janela curta, docs/416) e da DX.82. Se sumiu, a etapa fecha por medição.
- **Objetivo:** o Explorer cria o device uma vez e não destrói em laço.
- **Critério de saída:** `KMD_CONTEXT_CREATE` < 30/min em regime; zero `Allocate RECUSADO` com
  `Misc=0x2`; taskbar/thumbnails desenhados.
- **Plano A — medir o motivo exato da recusa pelo próprio Windows (ETW):**
  `wpr -start GPU -filemode` durante 60 s de laço, `wpr -stop laco.etl`; abrir no WPA / GPUView e
  procurar os eventos de erro do provedor `Microsoft-Windows-DxgKrnl` na chamada de
  `D3DKMTCreateAllocation` do explorer.exe. Não precisa de driver novo. (Se o WPR não estiver na
  máquina: vem com o Windows ADK / Windows Performance Toolkit.)
- **Plano B — testar os três candidatos do doc 403 §4, um por corrida, na ordem de custo:**
  1. **dados privados no nível do recurso** para recursos compartilhados (`D3DDDICB_ALLOCATE.pPrivateDriverData`, hoje sempre NULL — `bc250_wddm_winsys.c:540–552` só preenche o nível da alocação) — o outro processo abre o recurso compartilhado e recebe esses dados no `OpenResource`;
  2. `CpuVisible=0` na classe RESOURCE quando `MISC_SHARED` (hoje a classe RESOURCE sempre pede CPU-visível, `bc250_wddm_winsys.c:500–506`);
  3. o KMD devolver `DXGKARG_CREATEALLOCATION.hResource` próprio para `Flags.Resource=1`.
  Antes de cada um: um app de teste mínimo (`CreateTexture2D` com `MISC_SHARED|BIND_RENDER_TARGET`,
  depois `OpenSharedResource` noutro processo) reproduz o laço sem Explorer — muito mais barato de
  iterar e de mandar para o harness.
- **Plano E — o dxgkrnl não diz nada e os três candidatos falham:** comparar com um driver WDDM de
  referência que aceite a mesma chamada — o `BasicRender`/`BasicDisplay` na mesma máquina sob o app de
  teste mínimo, capturando os `D3DDDI_ALLOCATIONINFO2` que o runtime monta (ETW `DxgKrnl` também traz
  os argumentos). A diferença de campos é a resposta.
- **Teto:** 1 corrida para o A (só ETW, driver atual) + até 3 para o B. **Abandono:** se o ETW não
  mostrar motivo, não chutar — ir direto ao E.

### Etapa 4 — Velocidade do desktop (meta: ≥ 30 FPS no DWM parado, ≥ 15 FPS arrastando janela)

- **Objetivo:** sair de ~1 FPS no harness e ~3–5 Presents/s no DWM. As faixas DX.74–80 (coalescência
  de clear/draw/damage rects, anel de staging, cache de framebuffer) já atacaram o lado da UMD; o
  maior pedaço medido continua **fora** da nossa DDI: `pfnPresentCb` p50 ~443 ms contra 0,75 ms dentro
  de `DxgkDdiPresent` (docs/437).
- **Critério de saída:** p50 entre Presents ≤ 33 ms com 1 app aberto, medido no log da UMD e
  confirmado por PresentMon (`PresentMon.exe -process_name dwm.exe`).
- **Plano A — descobrir onde estão os ~440 ms do `pfnPresentCb`, sem mexer em nada:** GPUView/WPA
  (`wpr -start GPU`) mostra a fila do VidSch, as esperas de paginação e o tempo de cada pacote por
  processo — é exatamente o instrumento para "tempo fora da DDI". Hipóteses a discriminar: (a) o
  escalonador esperando a paginação (A5), (b) DDIs do KMD lentas pelo log síncrono (A1), (c) fila
  única do Node 0 dividida entre paginação, SDMA do present e escapes.
- **Plano B — cortar os custos já conhecidos, um por vez:**
  1. logger assíncrono (Etapa 2 A);
  2. trace da UMD sem write-through por linha (anel por processo, escrita a cada 250 ms);
  3. reduzir as invalidações de TLB (`FLUSH_POOL_VMIDS`, ~243/s na 100): por faixa e em lote;
  4. segmento local maior (A5);
  5. religar o lote de desenhos só com prova de que o piscar das 94–96/99 não volta (docs/438 alerta para não religar por palpite).
- **Plano E — interrupções reais:** hoje nada usa interrupção (ISR nunca instalado; aposentadoria
  por timer de 1 ms e polling). Ligar o IH (contratos `bc250_ih_*` já existem) para EOP do GFX e
  trap do SDMA dá latência de conclusão de microssegundos em vez de milissegundos. Obra grande,
  com risco de interrupção sem ACK; só depois de o travamento estar resolvido.
- **Teto:** 1 corrida por item do A (medição de antes/depois). **Abandono:** item que não mexe o
  p50 em ≥ 10 % sai da lista.

### Etapa 5 — Present e composição no modelo WDDM (sair do escape)

- **Objetivo:** nenhum pixel do desktop passa por `pfnEscapeCb`.
- **Critério de saída:** zero `BltViaKmd`/`BRIDGE VULKAN->WDDM` no log; Present do DWM por
  `DxgkDdiPresent` (ou submissão de contexto) com lista de alocações; tripé nas três pernas no blt
  de apresentação; `COMPOSITE_BLT RECUSADO (resolucao)` = 0 com apps abrindo.
- **Plano A — Present pelo caminho do runtime:** a UMD entrega o Present com `pfnPresentCb`
  (origem/destino como alocações, não GPUVA crua) e o KMD o executa no Node 0 via
  `DxgkDdiPresent`/`DxgkDdiSubmitCommandVirtual` com DMA buffer — o VidMm garante residência e
  ordena contra a paginação. A cópia continua sendo o `Bc250SdmaPresentBlt` provado, só que como
  pacote do escalonador e **assíncrona** (a fence do DMA buffer retira, sem espera dentro da DDI).
- **Plano B — ponte Vulkan→WDDM dentro da própria fila Vulkan:** em vez de pedir ao KMD, o RADV
  copia para a alocação WDDM do backbuffer com `vkCmdCopyBuffer`/`vkCmdCopyImage` na mesma
  submissão do desenho (a alocação WDDM importada como memória Vulkan externa). Some a ponte
  inteira; a residência é do RADV/winsys (`MakeResident`).
- **Plano E — manter o escape, mas honesto:** `HardwareAccess=0`, blt assíncrono com fence
  própria, resolução refeita sob lock, e **recusa** (nunca cópia de CPU) se a alocação não estiver
  residente. Serve de ponte temporária enquanto A/B amadurecem; não fecha a etapa.
- **Teto:** 3 corridas. **Abandono:** se o A esbarrar em regra do dxgkrnl sem diagnóstico, ETW
  (como na Etapa 3 A) antes de trocar para o B.

### Etapa 6 — TDR com display sobrevivendo (fecha o item 25 e o gate de saída da P6)

- **Objetivo:** hang induzido → TDR → desktop volta sozinho, sem reboot.
- **Critério de saída:** pacote `TdrInduceHang=1` (fluxo de 07/09, docs/186–188) sobre o desktop
  acelerado: `TDR_QUIESCE` → `TDR_RESTART PROVADO` → DWM recria device → Present volta, com foto e
  log; zero BugCheck.
- **Plano A:** repetir o ensaio de 07/09 21:49 (que já validou detecção/reset/readmissão) agora com o
  DWM acelerado; o que falta é o display.
- **Plano B:** se o DWM não voltar, instrumentar `DxgkDdiResetFromTimeout`/`RestartFromTimeout` para
  o anel GFX e o VMID do DWM (hoje o reset é do SDMA0; o GFX também precisa voltar).
- **Plano E:** `DxgkDdiResetEngine` (reset por engine, hoje recusado com motivo honesto) para não
  derrubar o adaptador inteiro.
- **Teto:** 2 corridas, **só depois** da Etapa 2 (não repetir TDR com o travamento vivo — mesma regra
  que o 188 já registrou).

### Etapa 7 — D3D11 de jogo (não só do DWM)

- **Objetivo:** um jogo D3D11 real (não modificado) abre, renderiza e apresenta.
- **Critério de saída:** amostra oficial da Microsoft (DirectX-Graphics-Samples / "Direct3D Tutorial
  Win32 Sample" 07 textured cube) + um jogo leve D3D11 (lista abaixo) rodando 5 min, sem TDR, com
  captura de tela comparada por hash de região e PresentMon.
- **Plano A — completar a UMD D3D11 real, na ordem que os jogos usam:**
  1. tabela D3D11 **sem NULL** (A7) e D3D10.0 correto (A8);
  2. `pfnCreateComputeShader`/`CsSet*`/`Dispatch` + UAV (quase todo jogo moderno usa CS);
  3. deferred contexts como "não suportado" declarado via caps (`D3D11DDICAPS_THREADING`), não NULL;
  4. D3D11.1 DDI (`D3D11_1DDI_DEVICEFUNCS`) — o runtime do Windows 10 pede para `FEATURE_LEVEL_11_1`;
  5. formatos, MSAA (`CheckMultisampleQualityLevels` é chamado milhares de vezes pelo DWM — cachear);
  6. **cache em disco** de pipelines (DXBC→SPIR-V→ACO) por hash, para a latência de carga.
- **Plano B — conformidade antes do jogo:** rodar o **WHCK/HLK D3D11 conformance** parcial (ou os
  testes do `dxvk`/`vkd3d` que exercitam as mesmas DDIs) como gabarito offline+físico, e só então
  o jogo.
- **Plano E — validação cruzada por DXVK (como ferramenta, não entrega):** DXVK na pasta de um jogo
  exercita o **mesmo** RADV sobre o **mesmo** KMD sem passar pelas nossas DDIs D3D. Se o jogo roda
  com DXVK e não com a nossa UMD, o defeito está na UMD; se falha nos dois, está no RADV/KMD. A
  Diretriz de 16/09 proíbe contar isso como entrega — aqui é bisturi de diagnóstico.
- **Cargas candidatas (D3D11, sem anti-cheat de kernel):** Unigine Heaven/Valley (benchmark
  D3D11 determinístico, bom para repetir) e Dota 2 com `-dx11` (o mesmo executável roda `-vulkan`,
  o que permite comparar as duas APIs sobre o mesmo KMD). Fixar o hash do executável antes da
  corrida (lição da Diretriz sobre o Marco 4).

### Etapa 8 — Vulkan de jogo e instalação de produto

- **Objetivo:** `vulkaninfo` e um jogo Vulkan real acham a BC-250 pelo loader oficial, sem cópia de
  DLL em pasta de usuário.
- **Critério de saída:** `vkcube` oficial (Khronos, **com** `vkCmdDraw`/pipeline gráfico — Marco 4 da
  Diretriz), depois um jogo Vulkan (ex.: Dota 2 `-vulkan` ou Doom (2016) em Vulkan), com hash dos binários e foto.
- **Plano A:** INF com `VulkanDriverName`/`VulkanDriverNameWow` apontando para o JSON do ICD no
  DriverStore; RADV empacotado e assinado junto do pacote; remover os caminhos fixos (A10).
- **Plano B:** registro do ICD por `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` (método antigo, ainda
  aceito pelo loader) enquanto o INF não fecha.
- **Plano E — 32 bits:** compilar `bc250umd` e RADV para x86 (`UserModeDriverNameWow` e
  `VulkanDriverNameWow` corretos, A9). Sem isso, jogo 32 bits não funciona em nenhuma API — é
  obrigatório antes de "jogos em geral", mas pode vir depois do primeiro jogo 64 bits.

### Etapa 9 — Jogos em escala, D3D9/D3D12 e estabilidade longa

- **Objetivo:** critério final da Lei do escopo: jogo real na tela ligada à placa, sem WARP, com TDR
  recuperando.
- **Plano A — D3D12 por tradução no UMD:** avaliar (documento de decisão, não código) uma UMD D3D12
  sobre Vulkan (mesma rota B: DXIL→SPIR-V com o `vkd3d-shader`/`dxil-spirv`). É a maior obra
  restante; decidir antes de começar.
- **Plano B — D3D9:** `UserModeDriverName` D3D9 (a entrada 1 das quatro do INF) traduzida para o
  mesmo backend; muitos jogos antigos e launchers dependem disso.
- **Plano E — estabilidade longa:** soak de 2 h com PresentMon + contagem de TDR/fences, e troca de
  modo/resolução (hoje só o modo herdado do GOP).

### Etapa 10 — DCN básico (P9 da BASE, **escrita proibida até lá**)

- **Objetivo:** troca de modo, flip de hardware, hotplug, cursor de hardware.
- **Plano A:** programar só `HUBP` (endereço de superfície = flip real) com a base DMU correta
  (Lei 7), mantendo o timing do GOP. Isso já tira a cópia por quadro do scanout.
- **Plano B:** EDID real por AUX (item 77) antes de qualquer troca de modo.
- **Plano E:** permanecer no GOP e no blt (display v1) — é produto válido; P9 não bloqueia jogos.

### Transversal (em paralelo, custo de corrida zero)

| Item | Plano A | Plano B | Plano E |
|---|---|---|---|
| CI | GitHub Actions: auditorias Python + suítes portáveis de `driver/core` com gcc | runner Windows com EWDK para build completo | build noturno local com relatório |
| Driver Verifier | uma corrida por faixa grande com `/standard` no `bc250mcdm.sys` | só IRQL/pool (`/flags 0x3`) se o `/standard` for pesado demais | — |
| Documentação | `ESTADO_ATUAL.md` de 1 página + histórico fora do `CLAUDE.md` | índice de `docs/` por tema | — |
| Segurança do pacote | um caminho de DLL, vindo do DriverStore, com hash | tunable de laboratório explícito para caminho alternativo | — |

---

## 5. O que muda na próxima corrida (proposta concreta)

**Candidato proposto: DX.82 + só o logger assíncrono (A1).** Nenhuma outra mudança. Critério escrito
antes (Lei 10), assinatura e implicação separadas:

- **Preparação:** subir o DriverVer fonte; build lab; pacote; auditorias; conferir por string no SYS
  empacotado as marcas novas do logger; conferir **qual** RADV o dwm.exe carregou pela linha
  `[VK_BRIDGE] SUCESSO` (A10). Dump por teclado armado (`arm-crash-dump-on-hang.ps1` sem argumento
  mostra o estado); se possível, `ping -t` de outra máquina. Janela de 600 s, boot frio antes.
- **Desfechos:**
  - **A — 600 s sem travar.** *Assinatura:* `RECOVERY_OK_ONLINE` no mesmo boot, log do KMD contínuo até
    o StopDevice. *Implicação:* A1 sobe de H para P2 (uma corrida não fecha: a 127 também chegou a 600 s);
    repetir duas vezes antes de chamar de correção.
  - **B — trava e o dump 0xE2 é gerado.** *Assinatura:* tela azul E2 + `MEMORY.DMP` novo.
    *Implicação:* a pilha nomeia a espera; analisar antes de qualquer código. Com o logger
    assíncrono, a cauda do log do KMD também chega até ~100 ms antes.
  - **C — trava, Num Lock responde, sem dump.** *Implicação:* conferir o armamento e repetir uma vez.
  - **D — trava, Num Lock e ping sem resposta.** *Implicação:* CPU presa em IRQL alta ou barramento
    travado → Driver Verifier e A2 sobem de prioridade; KDNET antes da próxima.
  - **E — fora do mapa.** Descrever o observado antes de julgar.
- **Regra de relato:** o resultado diz sempre "dump gerado", "atalho sem resposta" ou "não tentado".

**Em paralelo, offline:** A2 (log fora do DIRQL), A4 (dreno que perde `DMA_COMPLETED`), A7/A8 (tabelas
da UMD), A11, A12 e a unificação da flag do escape (A3) entram em faixas próprias, com portão e
mutação, mas **não** no mesmo candidato de uma bissecção.

---

## 6. O que foi feito nesta sessão

- Leitura de `CLAUDE.md`, `DIRETRIZ_…`, `BASE_DE_AVANCO.md` (topo e seções normativas), docs 402,
  403, 432, 438, 457 e 459, e auditoria dirigida do KMD (logger, ISR/DPC, callbacks sincronizados, retirement, escape,
  segmentos, PTE), da UMD (tabelas de DDI, escape, carga de DLL, winsys) e do INF.
- `python3 tools/audit-bc250mcdm-safety.py`: **FAILED** num checkout limpo, só por CRLF dos `.cmd`/`.bat`
  → adicionado `.gitattributes` → **PASS**. `audit-mcdm-ddi-matrix.py`: PASS (estrutural).
  `audit-ddi-noop-callbacks.py`: PASS (1 aviso em `NotifySurpriseRemoval`).
- O ramo remoto avançou das Corridas 100 para 129 durante a análise; o texto foi refeito sobre a
  DX.82 e cada achado foi reconferido no código novo (A3 mudou em parte: a ponte já usa
  `HardwareAccess=0`; o resto se manteve).
- **Nenhum código do driver foi alterado.** Nenhuma afirmação deste documento está validada em
  hardware; os achados A1–A16 são leitura de código (P1) e a ligação deles com o travamento é H até
  a Etapa 1 responder.
