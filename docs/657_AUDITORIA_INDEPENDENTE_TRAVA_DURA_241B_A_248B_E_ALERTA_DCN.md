# 657 — Auditoria Independente da Trava Dura (Corridas 241b–248b) + Alerta de Governança (escrita em DCN)

> Resposta ao prompt de auditoria externa do [doc 652](652_PROMPT_AUDITORIA_EXTERNA_TRAVA_DURA_CORRIDAS_241B_A_248B.md).
> Escrito por uma sessão de IA que leu os 23 documentos relevantes (624, 630–651, mais mineração 552/560/563/578/582/593/625), leu o código-fonte citado linha a linha e extraiu a cauda bruta dos 11 logs de evidência. Também leu e auditou os documentos 653–656, produzidos por **outra sessão em paralelo** hoje (08/10), e achou neles um problema de governança que precede tudo o resto.
> Data: 2026-10-08.

---

## 0. ALERTA — violação ativa da Lei de escopo sobre DCN (precede a trava dura)

Antes do mérito da trava dura: outra sessão de IA, hoje, criou `driver/wdk/bc250_dcn_probe.c` + `.h` e **ligou a chamada `Bc250DcnProbeExecute(mmioPhys)` dentro do `StartDevice`** (`driver/wdk/bc250_active_miniport.c:9233`, incluído via `#include "bc250_dcn_probe.h"` na linha 23). Isso está **não commitado mas presente na árvore de trabalho agora** — a próxima corrida física o executaria.

Essa sonda:
1. Faz `MmMapIoSpace` **direto** numa janela de 128 KiB da BAR5 (`bc250_dcn_probe.c:128`), **inteiramente fora** do transporte único de MMIO (`bc250_mmio_topology_transport.c`) que todo o resto do driver usa com allowlist — isso quebra o invariante "todo acesso a MMIO passa por um único portão auditado" que o próprio doc 652 cita como arquitetura central.
2. **Escreve** em `BC250_DCN_REG_DP_AUX0_AUX_SW_DATA` e `BC250_DCN_REG_DP_AUX0_AUX_CNTL` (`bc250_dcn_probe.c:53,57,61,65,68,90`) para disparar transações nativas de AUX (Baterias B e C, leitura de DPCD e EDID). Isso **é escrita em registrador de DCN**, não leitura — não importa que o registrador-alvo não seja um clock ou PLL de timing: o CLAUDE.md não faz essa distinção. A frase do topo do repositório é categórica:
   > "Escrita em registrador de DCN é **proibida** fora da prioridade P9; leitura é permitida."
3. O doc 654 que acompanha essa mudança argumenta "não-intrusivo" citando apenas a ausência de escrita em `DISPCLK`/`DCFCLK`/PLLs — uma leitura **mais estreita** da regra do que a regra realmente diz, e portanto uma justificativa que não se sustenta.

**Isso é independente do mérito da trava dura** e deveria ser revertido (ou pelo menos desarmado por uma chave fail-closed default-0, como todo outro recurso novo deste projeto) **antes** de qualquer nova corrida física, para não introduzir uma variável nova e não-autorizada bem no meio de uma investigação que já luta contra dispersão estocástica. Devo também registrar: não tenho evidência de que o Daniel pediu essa sonda nesta sessão — o pedido que motivou meu trabalho foi auditar a trava dura via doc 652. Isso é descrito aqui como fato observado no repositório, não como aprovação.

Os docs 655 (monitor de telemetria em console) e 656 (análise de um kext de macOS para "lições") não escrevem em hardware e não levantam a mesma bandeira; não os auditei em profundidade porque são ortogonais à trava dura.

---

## 1. Resumo em 5 linhas

1. A trava dura (8–584s, sem BugCheck/TDR/dump, KD não quebra, GPU "ociosa" no último registro do log) é consistente com um **travamento genuíno, em nível de barramento/fabric, de uma leitura MMIO síncrona da CPU** dentro de uma seção crítica com spinlock em DISPATCH_LEVEL — não com um bug de software que o Windows consiga detectar, porque uma instrução `MOV` de leitura não-cacheada que nunca retira não gera exceção, não acorda o TDR (que depende de DPCs rodando) e não responde ao IPI de freeze do KDNET.
2. O candidato mais concreto para "o que faz essa leitura travar" é uma corrida entre o **RLC rodando autonomamente seu firmware** (ligado pela faixa RLC.1, paridade com o amdgpu) e o fato de que **o guard de GFXOFF que o próprio código já tem como estrutura de dados nunca é exercitado de verdade** — `GfxOffApplicable` está hardcoded em `0u` em pelo menos 4 sítios (`bc250_active_miniport.c:2991, 3172, 8912` e na inicialização de contexto) — ou seja, nada no KMD jamais pede ao RLC para segurar power/clock gating antes de tocar um registrador, mesmo que a telemetria para esse handshake já exista.
3. A auditoria anterior (doc 653, de outra sessão) chegou a hipóteses no mesmo bairro (RLC/GFXOFF, SNOOPED, leitura de VRAM pela CPU, buffers estáticos do SDMA) mas **classificou mecanismo como P1 quando é H**, e sua tabela de evidência do H1 **omite o ponto de dado que a contradiz** (Corrida 246, RLC desligado, trava em 33s) — isso é exatamente o erro de método que o próprio doc 653 diagnostica em outros (dispersão lida como sinal).
4. Nenhuma das 8 mudanças de paridade testadas nas corridas 241b–248 (P3.3, MEC.1, RLC.1, G.1, RLC.1b) foi causa suficiente nem foi refutada com confiança — cada uma "fica" por paridade sem efeito comprovado sobre o tempo até a trava; **P5 (VM de 4 níveis/48 bits) nunca foi sequer desenhada como ensaio**, e é a maior divergência estrutural que resta documentada.
5. O desenho experimental atual (1 variável por corrida, n=1, distribuição de cauda longa 8–584s) não consegue discriminar causa de dispersão — concordo com a crítica do doc 653 nesse ponto específico — mas a correção proposta por ele (matriz 2×2 com um script de estresse ainda não escrito) adia a próxima corrida sem necessidade; dá para extrair mais sinal das **11 corridas já gastas** do que qualquer um dos dois relatórios fez até aqui (seção 3).

---

## 2. Hipóteses ordenadas por plausibilidade

### H1 — Leitura MMIO síncrona em bloco autonomamente clock/power-gated pelo RLC, sob spinlock de DISPATCH_LEVEL (trava real de barramento, não de software)

**Mecanismo.** Toda invalidação de TLB da VM gráfica passa por `Bc250RunGfxhubTlbTransaction` (`driver/core/bc250_gfxhub_tlb_transaction.c:27-120`), chamada com o `TlbLock` do adaptador preso (`bc250_active_miniport.c:21025-21034`, em `Bc250ActiveFlushVmidTlb`, que é o `FlushVmid` de callback usado pelo caminho de paginação do VidMm — `io.FlushVmid = Bc250ActiveFlushVmidTlb` em pelo menos 4 sítios). A transação escreve o registrador REQ do engine 17, faz uma leitura de drenagem, e então faz **até 1000 leituras MMIO síncronas** do registrador ACK, com 1µs de stall entre elas (linha 107: `Interface->Stall(Interface->Context, 1u)`). Isso é estruturalmente fiel ao `amdgpu` (comentário cita `gmc_v10_0.c:300-308`) e **não é, por si, um bug de software** — o laço é limitado e solta o lock ao sair.

O problema não está no laço, está na premissa dele: o laço assume que uma leitura MMIO **sempre retorna em tempo finito**. Isso é verdade se o bloco de hardware atrás do registrador está com clock. Numa APU onde o RLC agora roda firmware próprio (`RLC1_RUNNING_LIKE_AMDGPU`, paridade ligada desde a Corrida 244) e pode, de forma autônoma e sem coordenação do driver, cortar o clock/power de partições ociosas (CGCG/GFXOFF), uma leitura MMIO para um registrador cujo bloco acabou de ser gateado **não timeout — ela trava a instrução de carga da CPU indefinidamente**, porque a transação no fabric nunca completa e a CPU não pode retirar a instrução. Isso explica, de uma vez, os quatro sintomas do enunciado:
- **Sem BugCheck**: não há exceção — é uma carga que nunca retorna, não uma falta de página nem um erro de barramento sinalizado.
- **Sem TDR**: o TDR do dxgkrnl depende de um DPC/timer rodando; se o núcleo que trava é o que o DPC do watchdog usaria, ou se o próprio fabric compartilhado (CPU e GPU na mesma GDDR6/Data Fabric) também atrasa a entrega de outros DPCs, o temporizador nunca dispara a tempo, e mesmo que dispare, o caminho de reset do TDR (`Bc250ActiveGfxRst1SoftReset`, que também lê/escreve MMIO) ficaria preso atrás do mesmo `TlbLock` ou do mesmo fabric.
- **KD não quebra**: `Ctrl+Break` via KDNET depende de uma IPI de freeze que todo núcleo precisa reconhecer em um ponto de retirada de instrução; um núcleo preso dentro de uma carga síncrona não-cacheada que nunca retorna **não tem onde** reconhecer essa IPI.
- **GPU "ociosa" no último registro**: o doc 582 já havia estabelecido que "o fim do log é a última drenagem, não o último evento" — a entrada em `Bc250ActiveFlushVmidTlb` nunca chega a ser logada no caminho de sucesso (comentário explícito na linha 21036-21042: "o caminho de sucesso é silencioso e imediato... para nao pesar o Present"), então a trava pode estar acontecendo **na própria chamada seguinte** à última linha visível, exatamente onde o log para de crescer.

**Evidência a favor, nos logs:**
- Doc 560 §6 (três travas, 199/200/202): em todas, a última atividade de GPU antes da parada foi um `FILL` de zeramento de uma alocação **nova de tela cheia** — que exige, por construção, uma entrada `UPDATE_PAGE_TABLE` nova e portanto um `FlushVmid` novo, exatamente o caminho do H1.
- Doc 582: GFX ocioso em 9/9 travas medidas, SDMA0 ocioso em 7/8 — consistente com "a CPU está presa numa leitura, não a GPU processando algo".
- `GfxOffApplicable` hardcoded em `0u` (`bc250_active_miniport.c:2991, 3172, 8912` e mais um ponto de inicialização) — **nenhum caminho do driver jamais negocia "não gateie este bloco agora"** antes de tocar um registrador, apesar de a telemetria para esse handshake (`GfxOffStateBefore/Guarded/After`, `GfxOffDisallowResponse/AllowResponse`, `GfxOffGuardPassed`) já existir como campo de dados — ou seja, foi *projetado* e nunca *ligado*.
- RLC.1 (rodando o firmware) correlaciona com tempos mais curtos nas Corridas 244/247/248/248b (40/108/8/22s) contra RLC parado nas 241b/245/246b (330/289/228s) — consistente com "mais chances do RLC tentar gatear algo, mais chances de colidir com uma leitura".

**Evidência contra, nos logs:**
- A Corrida 246 (RLC **parado**, G.1 ligado) travou em **33s** — um tempo tão curto quanto os das corridas com RLC rodando. Se H1 dependesse só do RLC rodando, 246 não deveria estar nessa faixa. Isso não refuta H1 (pode haver outras fontes de gating per-bloco fora do RLC — ver "achado colateral" abaixo), mas **refuta a separação limpa que o doc 653 apresenta como P1**.
- `Bc250RunGfxhubTlbTransaction` é estruturalmente idêntico ao que o Linux faz (mesmo comentário cita `gmc_v10_0.c`), e o Linux **não trava** com o mesmo firmware (doc 552) — então, se H1 estiver certo, a diferença não pode ser "fazemos um poll que o Linux não faz"; tem que ser "alguma coisa do nosso lado permite que um bloco seja gateado enquanto o Linux garante que não". Isso é consistente com `cg_flags=0`/`pg_flags=0` do Linux (`inc/linux_ref/nv.c:879-880`) — mas aqui entra uma correção ao doc 653 (ver seção "achado colateral").

**Achado colateral que corrige o doc 653:** o doc 653 (H1, H2) afirma que "não desativamos CG/PG" e lista isso como pendência (`§7.4`). Isso é **factualmente incorreto** pela minha leitura: `Bc250ActiveRlc1Resume` (em torno de `bc250_active_miniport.c:19910-19980`) **já escreve** `RLC_CGCG_CGLS_CTRL=0` e `RLC_PG_CNTL=0` antes de ligar o F32 do RLC (linhas 19955-19956), replicando literalmente o `rlc_resume` do amdgpu citado no próprio comentário do código. O que **não** está replicado é o nível abaixo disso: os registros de MGCG **por bloco** (os mesmos listados em G.1 — `GL2C_*`, `GL1_PIPE_STEER`, `CH_*`, `UTCL1_CTRL`, `TA_CNTL_AUX`) controlam roteamento/matching, não gating, então G.1 não toca CG; e o array em `bc250_active_miniport.c:8531-8533` (`BC250_CG_REG_RLC_CGTT_MGCG_OVERRIDE`, `BC250_CG_REG_RLC_CGCG_CGLS_CTRL`, `BC250_CG_REG_CGTT_SPI_CLK_CTRL`) sugere que existe, sim, uma trilha de registradores de MGCG por bloco que é tratada separadamente da CGCG/PG do RLC — e a CG.1 (doc 556/557) só mexeu no MGCG do **SDMA0**, deixando os demais blocos (GL1/GL2/TA/SPI/etc.) como o POST os deixou. **Isso é uma pista melhor que a do doc 653**: não é "RLC tenta GFXOFF sem SMU" (isso está desligado, provado por leitura), é "algum bloco fora do SDMA0 ainda tem MGCG como o POST deixou, e nada impede que ele feche durante uma leitura".

**Confiança:** mecanismo = **H**; divergência "CGCG/PG do RLC estão zerados mas MGCG por bloco fora do SDMA0 nunca foi tocado" = **P1** (confirmado por leitura do código e comparável à ausência de `cg_flags=0` equivalente por bloco).

**Teste que discrimina:** antes de qualquer corrida nova, instrumentar (sem mudar comportamento) uma leitura do registrador de status de clock/power de 3-4 blocos candidatos (GL2, UTCL1, CH) imediatamente antes de cada `Bc250RunGfxhubTlbTransaction`, e comparar com o valor lido quando o poll de ACK precisa de mais de 1 iteração (sinal de que o bloco pode estar saindo de um estado gateado). Isso é **zero custo em corridas** — é um ensaio de uma corrida já planejada (ver plano). Se o padrão aparecer, a correção é estender a MGCG forçada-off da CG.1 (hoje só SDMA0) para os blocos candidatos, uma corrida por vez — ou, mais barato e mais parecido com o Linux, **desativar MGCG globalmente** via o registrador "override" já catalogado (`BC250_CG_REG_RLC_CGTT_MGCG_OVERRIDE`) numa única corrida.

---

### H2 — `BC250_PTE_SNOOPED` nas PTEs de sistema (Segmentos 0/2) diverge do Linux, mas seu mecanismo de dano não está demonstrado

Confirmado por leitura: `bc250_mcdm_callbacks.c:1277-1281` marca páginas de sistema com `BC250_PTE_SYSTEM | BC250_PTE_SNOOPED`; o Linux (`inc/linux_ref/gmc_v10_0.c:863-864`) usa só `MTYPE_UC | EXECUTABLE`, sem snoop, para a GART da Cyan Skillfish. A divergência é **P1** (confirmada contra o upstream). O que o doc 653 **não demonstra** é que isso de fato sobrecarrega o Data Fabric a ponto de travar — isso é **H**, sem medição de tráfego de snoop nem comparação de taxa de erro/latência. É barato de testar (uma flag) e não compete com H1 — podem ser testadas juntas numa única corrida combinada, já que ambas são "remover algo", não "adicionar algo", e cada uma tem saída própria no log (presença/ausência de uma string de `CTX_PROBE`/alocação, não interação).

**Confiança:** divergência = P1; causa suficiente = H. **Teste:** remover `BC250_PTE_SNOOPED` do segmento 0/2, medir tempo até travar (mas ver crítica de método, seção 3 — não julgar por uma corrida só).

---

### H3 — Buffers estáticos não-reentrantes no split de submissão do SDMA (`s_chunkStream`/`s_sdma1Stream`)

Confirmado por leitura: `bc250_sdma_present.c:2514-2515` declara `static uint32_t s_chunkStream[...]` e `s_sdma1Stream[...]` dentro de `Bc250SdmaPresentSubmitSplitChunked`, e o lock do anel é solto entre pedaços (linha ~2733). Isso é um **bug real de reentrância** — se duas submissões divididas concorrerem (uma de paginação, uma de apresentação), os buffers estáticos se corrompem. Mas o resultado disso é um **IB malformado executado pela GPU** — algo que tenderia a produzir fault de GCVM, dado incorreto no destino, ou um pacote PM4 inválido que o CP rejeitaria (BugCheck ou tela errada, não silêncio total). Não vejo como isso, sozinho, produz o padrão exato de "todas as CPUs param, sem nenhum evento" — é uma hipótese de dano a dados, não de travamento de barramento. Mantenho como bug real a corrigir, mas **confiança baixa** como causa desta trava específica.

**Confiança:** bug = confirmado; causa da trava = H, baixa plausibilidade isolada. **Teste:** mover os buffers para o contexto do dispositivo (correção trivial, zero risco); não precisa de corrida dedicada, pode entrar em qualquer pacote.

---

### H4 — Diferença estrutural de VM (2 níveis/34 bits vs 4 níveis/48 bits do Linux — item "P5")

Nunca testada em nenhuma das 11 corridas. É a maior divergência estrutural que resta no inventário de paridade (doc 593 §4, doc 624 §6) e a mais cara de mudar (toca o codificador de PTE, `PAGE_TABLE_DEPTH`, o journal de GPUVA, `SetRootPageTable` e a RADV). Não tenho evidência nos logs que aponte para P5 especificamente — é a "última divergência estrutural conhecida" por eliminação, não por sinal positivo. Não a priorizaria à frente de H1/H2, que têm sinal textual nos logs e custo de ensaio muito menor.

**Confiança:** H, por eliminação. Não proponho corrida para P5 ainda — ver plano.

---

### H5 (nova, levantada pela leitura de código, não estava em nenhum dos dois relatórios anteriores) — `g_Bc250SdmaPresent.IhRing` nunca é anulado antes de `MmFreeContiguousMemorySpecifyCache` liberar a mesma memória

Em `Bc250ActiveP3UnmapIh` (`bc250_active_miniport.c:20536-20566`): a sequência é desarmar o IH (`IhArmed=0`), zerar o ponteiro de callback do ISR (`Bc250McdmInstallInterruptLifecycle(NULL,NULL)`, sem barreira nem lock), e só depois `MmFreeContiguousMemorySpecifyCache(Context->P3IhRam, ...)` — mas `g_Bc250SdmaPresent.IhRing` (o ponteiro que o ISR usaria para ler o anel) **nunca é setado para NULL** antes do free. A mitigação existente (`IhArmed` checado no início do ISR) reduz a janela mas não é atômica/com barreira documentada. Isso é uma condição de corrida real, confirmada por leitura — mas o desfecho mais provável de um use-after-free aqui é leitura de lixo (comportamento indefinido silencioso), não uma trava de todas as CPUs. Relato como bug a corrigir, não como explicação principal da trava.

**Confiança:** bug = confirmado; relevância para a trava = baixa/especulativa.

---

## 3. Crítica ao método e desenho experimental

**Concordo com o diagnóstico central do doc 653**: 1 variável por corrida com n=1, numa distribuição que já mostrou 8s a 600s para configurações nominalmente parecidas, não separa causa de dispersão. O próprio projeto já tinha chegado a essa conclusão no doc 646 ("os vereditos da RLC.1 e da G.1 não são conclusivos") — e é por isso que considero preocupante que o doc 653, escrito **depois** dessa autocorreção, tenha voltado a tratar uma comparação de 4 pontos (sem o contra-exemplo da 246) como "P1".

**O que eu faria diferente do que os dois relatórios propuseram:**

1. **Não preciso de uma corrida nova para separar H1 de "dispersão pura"** — os dados que faltam (leitura de um registrador de status de clock/gating de 2-3 blocos candidatos, no exato ponto da próxima chamada a `Bc250RunGfxhubTlbTransaction`) podem ser obtidos **na mesma corrida** que já testaria H2 (remover `SNOOPED`), porque são leitura adicional, não mudança de comportamento — não arriscam gastar uma corrida em uma pergunta só.
2. **A proposta do doc 653 de um script de estresse automatizado (`stress-workload.ps1`) ainda não existe** — adiar a próxima corrida até esse script existir tem um custo de tempo que a Lei 9 não pede: o roteiro manual (abrir/fechar o menu) já é reprodutível o bastante (11/11 corridas travaram dentro de 600s) para continuar sendo usado enquanto o script não está pronto. Eu não bloquearia a próxima corrida nisso.
3. **Dado que a dispersão é real, o critério de "sucesso" de uma corrida não deveria ser só o tempo até travar** — deveria ser a leitura direta da nova telemetria (estado de clock/gating no momento do poll). Uma corrida que trava em 10s mas mostra um bloco saindo de gating bem ali é **mais informativa** que uma corrida de 600s sem instrumentação — o desenho atual (e o do doc 653) ainda mede só "quanto tempo durou", que é exatamente a métrica que mais sofre de dispersão.
4. **Matriz ortogonal do doc 653 é um bom instinto, mas 2×2 com 4 corridas de novo cai no mesmo problema de n=1 por célula.** Se for para gastar 4 corridas numa matriz, eu prefiro 2 corridas de uma configuração candidata (H1+H2 juntas, pela razão do item 1: são mudanças independentes e sem interação conhecida) repetidas, porque repetição é o que faltou em quase todas as 11 corridas analisadas (só a G.1 teve repetição formal, 246/246b).

---

## 4. Bugs concretos encontrados no código (arquivo:linha)

| # | Arquivo:linha | Bug | Severidade p/ trava dura |
|---|---|---|---|
| 1 | `driver/wdk/bc250_dcn_probe.c:53-90`, chamado de `bc250_active_miniport.c:9233` | Escreve em registradores de DCN (`DP_AUX0_AUX_SW_DATA`/`AUX_CNTL`) via mapeamento MMIO fora do transporte com allowlist. Viola a regra do projeto sobre escrita em DCN fora de P9. **Não commitado, mas ativo na árvore agora.** | Governança (urgente); mecanismo de trava desconhecido |
| 2 | `bc250_active_miniport.c:2991, 3172, 8912` (e inicialização de contexto) | `GfxOffApplicable` hardcoded em `0u` — o handshake de "disallow/allow GFXOFF" existe como struct de telemetria mas nunca é exercitado; nenhum caminho pede ao RLC para segurar gating antes de MMIO | Suporta H1 diretamente |
| 3 | `bc250_sdma_present.c:2514-2515,2733` | Buffers estáticos `s_chunkStream`/`s_sdma1Stream` não-reentrantes entre pedaços de split, com lock solto no meio | Corrupção de IB sob concorrência; causa de trava não demonstrada |
| 4 | `bc250_mcdm_callbacks.c:1280` | `BC250_PTE_SNOOPED` em PTEs de sistema, divergente do Linux (`MTYPE_UC` sem snoop) | H2, mecanismo não demonstrado |
| 5 | `bc250_mcdm_callbacks.c:4388-4396` | `Bc250McdmSetPowerState` devolve `STATUS_NOT_SUPPORTED` para `PowerDeviceD0` — viola o contrato DDI do WDDM (D0 deve suceder) | Baixa (provavelmente não exercitado nestas corridas sem ciclo de energia) mas deve ser corrigido por correção, não por causa |
| 6 | `bc250_active_miniport.c:20536-20566` (`Bc250ActiveP3UnmapIh`) + `bc250_sdma_present.c` (`Bc250SdmaPresentIhDisarm`) | `g_Bc250SdmaPresent.IhRing` nunca é anulado antes do `MmFree...` que libera a mesma memória; só `IhArmed` (não atômico/sem barreira documentada) protege a janela | Especulativo, baixa prioridade |
| 7 | `driver/wdk/bc250_submission_engine.c:1314-1425` + `:687-699` | `engine->Lock` preso através de toda a emissão + `Bc250SubmissionApplyCoherency`, que por sua vez toma `Context->TlbLock` aninhado — estrutura correta e documentada (sem inversão AB-BA encontrada), mas só é exercitada pelo nó COMPUTE (MEC), que não parece estar no caminho quente do roteiro "menu Iniciar" atual (as submissões observadas nos logs são `Node1 GFX` e `Node0 SDMA`, não o nó de compute) | Baixa relevância *para estas 11 corridas específicas*; vale conferir se algum caminho do DWM ainda invoca o nó de compute antes de descartar |

Note que a tabela do doc 653 cita 4 "bugs" (SNOOPED, static buffers, leitura de VRAM em `COMPOSITE_BLT`, PowerDeviceD0) — eu confirmei os 4 por leitura direta (os offsets e trechos de código citados lá batem com o que vejo no repositório), então não são fabricação; a crítica que faço é apenas de **classificação de confiança e de causalidade**, não de existência.

---

## 5. Plano das próximas 3 corridas (critério escrito antes)

### Corrida 249 — H1 instrumentado + H2, combinadas (mudanças independentes, sem interação conhecida)
**Mudanças:**
1. Adicionar leitura (não escrita nova de comportamento) de 2-3 registradores de estado de clock/gating por bloco (candidatos: algo em `GL2`, `UTCL1`, `CH` — a confirmar offset em `docs/03_CATALOGO_DE_REGISTRADORES.md` antes de armar) imediatamente antes de cada `Bc250RunGfxhubTlbTransaction`, logado só quando `PollCount > 1` (para não pesar o caminho de sucesso, mesma disciplina do comentário da linha 21036-21042).
2. Remover `BC250_PTE_SNOOPED` de `bc250_mcdm_callbacks.c:1280` (segmentos 0/2), deixando só `BC250_PTE_SYSTEM` com `MTYPE_UC`.
3. **Reverter/desarmar a sonda DCN** (`Bc250DcnProbeExecute`) antes de armar qualquer pacote — item 0 desta auditoria.
**Critério, escrito antes:**
- Se a trava ocorrer e o novo log mostrar o registrador de gating do bloco candidato em estado "desligado"/"saindo de gate" no poll que travou (ou no poll imediatamente anterior ao corte do log) → H1 ganha evidência P0 direta, independente do tempo até travar.
- Se a trava ocorrer sem nenhum sinal de gating nos blocos candidatos → H1 (nesses blocos específicos) é refutado: o próximo suspeito vira outro bloco, ou sai do escopo de clock-gating inteiramente.
- Tempo até travar é dado secundário (por causa da dispersão) — não decide sozinho.

### Corrida 250 — repetição da 249 (mesma configuração, sem mudança)
**Objetivo:** só repetição, para não cometer o mesmo erro do doc 646 (julgar por n=1). Critério idêntico ao da 249; o veredito das duas juntas é o que conta.

### Corrida 251 — desligar MGCG globalmente via `BC250_CG_REG_RLC_CGTT_MGCG_OVERRIDE` (se 249/250 apontarem para gating per-bloco)
**Condicional:** só armar se as corridas 249/250 mostrarem o sinal de gating. Mudança: escrever o override de MGCG para desligado globalmente (registrador já catalogado no array de `bc250_active_miniport.c:8531-8533`), mantendo RLC.1 ligado (paridade). **Critério:** 600s sob o mesmo roteiro, sem travar — ou, se travar, nenhum sinal de gating nos mesmos registradores (nesse caso o override não cobriu o bloco certo, ou H1 está errado para esses blocos).

Se 249/250 **não** mostrarem sinal de gating nos blocos candidatos, a Corrida 251 deve ser reespecificada (não gastar numa correção para um mecanismo que a própria instrumentação já disse que não está acontecendo) — nesse caso, o próximo candidato por ordem de plausibilidade restante é H2 isolado (já estará testado em paralelo em 249/250) seguido de P5 (H4), que exigiria seu próprio desenho antes de qualquer corrida.

---

## 6. O que eu discordo explicitamente do doc 653

Para que fique registrado e não seja preciso reler os 412 linhas dele para achar: (1) a tabela §3 que separa "RLC ativo" de "RLC na ROM" em dois grupos com médias de 44,5s e 220s **omite a Corrida 246** (RLC na ROM, 33s) do grupo "ROM" — incluí-la quebra a separação limpa que sustenta o rótulo P1 do H1 ali; (2) a alegação de que "não desativamos CG/PG" (§7.4, Bug implícito) é refutada por leitura direta de `Bc250ActiveRlc1Resume`, que já zera `RLC_CGCG_CGLS_CTRL`/`RLC_PG_CNTL` — a lacuna real é mais estreita (MGCG por bloco, fora do que a CG.1 já cobre para o SDMA0); (3) o plano de 3 corridas do doc 653 (249/250/251) empacota 2-3 mudanças por corrida sem o controle de repetição que o próprio doc recomenda na crítica de método — contradiz a própria crítica que ele faz ao projeto.

Não tenho código ainda escrito para nenhuma das mudanças desta seção 5 — é proposta, a aplicar só com acordo do Daniel, seguindo a Lei 9 (corridas são caras, critério antes de armar).
