# docs/250 — Handoff: a parede é `BC250_GPUVA_NO_SPACE` no rebuild transacional, e ela termina em DMA PAGE FAULT

**Data:** 2026-09-12 (sessão inteira)
**Última corrida física:** `2026-09-12_17-45-46-DESKTOP-HQAG6T4-lab-binding`, candidato **3.156.1.0**
**Estado histórico ao escrever este handoff:** fusível **TRIPPED** (Boots=2, LastStatus=`0xC0000001`), dispositivo em **Código 43** (`CM_PROB_FAILED_POST_START`) com `bc250mcdm` preso. **Estado revalidado após a recuperação, antes da C3.14:** fusível CLEAN, BasicDisplay Código 0, nenhum serviço/arquivo/pacote experimental instalado; preflight LAB PASS em 12/09.
**Veredito físico da faixa C3:** **NÃO VALIDADO EM HARDWARE** exceto onde explicitamente marcado P0.

> **CORREÇÃO DE REGISTRO C3.14 (auditoria da IA seguinte, 12/09, OFFLINE):** a
> conclusão desta passagem de bastão de que o estouro era apenas do `Scratch`,
> eliminável por replace in-place, está **REFUTADA pela aritmética do código**.
> `Scratch` é inicializado vazio e recebe somente as faixas do estado final;
> portanto, se ele chega a 256, o estado final também não cabe em 256 slots.
> A linha reproduzida abaixo também está transcrita com os campos trocados: o
> log integral contém `Mapped=73 Reusable=183`, não `Mapped=183 Reusable=73`, e
> mediu picos de `Mapped=252`. As falhas com 65--83 faixas ativas são coerentes
> com commits contendo centenas de runs esparsos. A opção A não resolve a
> capacidade. A C3.14 elevou o teto para 1024 e acrescentou replay offline de
> **252 faixas existentes + 256 mudanças = 508 faixas publicadas**, com portão
> de auditoria provado por mutação. Build completo: `EXIT=0`. Isso ainda é
> **NÃO VALIDADO EM HARDWARE**. A reação ao erro da seção 3.4 não foi alterada,
> preservando uma variável por candidato.

---

## 0. Para a próxima IA — leia isto primeiro

Três coisas que mudam o que você vai fazer:

1. **O `NO_SPACE` está medido, mas o enquadramento inicial do `Scratch` foi corrigido acima.** Não repita a opção A como conserto de capacidade; a C3.14 adotou o teto 1024 com replay offline.
2. **Duas hipóteses minhas foram refutadas durante esta sessão** (contenção leitor/publicador; vazamento de slots). As duas estão documentadas com o dado que as derrubou, para você não repeti-las.
3. **O ciclo de corrida está caro e frágil.** Leia a seção 6 (armadilhas) antes de pedir hardware ao Daniel. Quatro corridas desta sessão foram gastas em defeito de host, não de silício — duas delas minhas.

---

## 1. O que ficou PROVADO NO SILÍCIO hoje (P0)

Estes são ganhos reais, com evidência arquivada. Não os re-litigue.

| Fato | Evidência |
|---|---|
| **Present 100% por GPU com imagem correta no monitor** | 13:01 — `Submitted=584 Completed=584 Fallbacks=0 HostCpuCopies=0`, 584 quadros consecutivos, confirmação visual |
| **Causa raiz da tela preta fechada** | faltava `LocalSegmentBaseOffset` na conversão offset-de-segmento → MC. `SrcMc` passou de `0xF401560000` para `0xF401FEC000`, delta `0xA8C000` |
| **`D3D11LabPromotion` mantém o `dwm.exe` vivo** | 13:29 — 3 min 13 s, `dwm_crash_events: 0`, contra 7 crashes em 18 s sem ele |
| **Compute e desktop coexistiram** | 16:09 — gate do Node 1 aberto **com o DWM vivo**, shader executado, byte proof PASS, present por GPU |
| **A C3.9 (gate por processo) funciona** | 16:09 — `SlotMask=0x6 Count=2` (dois slots de outro processo pendentes) e mesmo assim `JOURNAL=0` |

---

## 2. A PAREDE ATUAL — cadeia causal completa

Medida em 16:31 e confirmada em 17:45. **Só acontece com o `dwm.exe` vivo.**

```
[1] DWM vivo -> muitas faixas GPUVA. Pico medido: Mapped=183 de Cap=256.

[2] Bc250GpuvaSpaceReplaceRangeTransactional RECONSTRÓI o espaço inteiro no
    Scratch: insere TODAS as faixas MAPPED uma a uma, MAIS os fragmentos gerados
    pelos recortes. Com 183 mapeadas + fragmentação, a reconstrução estoura 256.
    -> BC250_GPUVA_NO_SPACE                    (bc250_gpuva_space_contract.c:135)

[3] Bc250GpuvaSnapshotPublishBatch devolve o erro
    -> journal PUBLISH_FAILED (status 7)

[4] ActiveFlushTlb devolve STATUS_IO_DEVICE_ERROR **e a page table NÃO é
    publicada**. GpuvaMappingsOperational só reabre dentro do ramo
    "journalStatus == OK" (bc250_active_miniport.c:8699), então a admissão do
    Node 1 também não volta.

[5] A GPU acessa uma VA cuja tradução nunca foi comitada
    -> D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT   (ExecutionState=7,
                                                    P1, d3dkmthk.h:4451)

[6] dxgkrnl marca o device em erro
    -> STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE (0xC01E0200) no submit seguinte
    -> vkQueueSubmit / vkWaitForFences = VK_ERROR_DEVICE_LOST
```

### Medições que sustentam cada elo

Da corrida 17:45 (`display-log-this-bind.txt`):

```
[ActiveFlushTlb] OCUPACAO slot=0 st=0 Cap=256 Mapped=183 Teardown=0 Reusable=73 Live=183
[ActiveFlushTlb] ... (PublishStatus=3 Attempts=1 Busy=0)     x7
[RADV_WDDM2] check_device_status: NON-ACTIVE ExecutionState=7
vkWaitForFences failed on frame 3: -4
```

Da corrida 16:31:
```
... (PublishStatus=3 Attempts=1 Busy=0)
BC-250 SubmitCommand #10: status=0xC01E0200
```

---

## 3. O QUE FAZER — o conserto, em ordem de preferência

**`Teardown=0` e `Reusable=73` provam que NÃO é vazamento e NÃO é o espaço vivo cheio.** O estouro é no **Scratch**, durante o rebuild. Isso muda o conserto:

### 3.1 — Opção A: não reconstruir o espaço inteiro (preferida)

`Bc250GpuvaSpaceReplaceRangeTransactional` (`bc250_gpuva_space_contract.c:289`) faz rebuild total por transacionalidade. Substituir uma faixa não deveria exigir re-inserir as outras 182. Um `replace` in-place com rollback pontual elimina o problema na raiz e reduz custo por operação — que também importa, porque este caminho roda a cada paginação do DWM.

**Risco:** mexe na garantia transacional. Exige teste que prove rollback correto em falha no meio.

### 3.2 — Opção B: aumentar `BC250_GPUVA_SPACE_MAX_RANGES`

`#define BC250_GPUVA_SPACE_MAX_RANGES 256u` (`bc250_gpuva_space_contract.h:26`).

**Custo direto, medido:** `sizeof(BC250_GPUVA_SPACE)` = **27.696 bytes** e `sizeof(BC250_GPUVA_SNAPSHOT_STORE)` = **55.448 bytes** (dois slots). O store vive dentro de `BC250_DXGK_ACTIVE_CONTEXT`, alocado de pool não paginado. Dobrar para 512 leva o store a ~110 KB; 1024 a ~220 KB.

**Ressalva:** isso trata o sintoma. Se a fragmentação crescer com a carga do desktop, 512 também estoura. Meça o pico real antes de escolher o número — a linha `OCUPACAO` já dá isso.

### 3.3 — Opção C: coalescer faixas adjacentes do mesmo processo

Reduz a contagem na origem. Mais trabalho, melhor resultado a longo prazo.

### 3.4 — Independente da opção: a reação ao `NO_SPACE` está errada

Mesmo consertando a capacidade, **`ActiveFlushTlb` devolvendo `STATUS_IO_DEVICE_ERROR` sem publicar a page table é o que produz o DMA page fault.** Falhar a publicação e deixar a GPU acessar traduções não comitadas é pior que falhar a submissão. Precisa haver um caminho que **não deixe a tradução inconsistente** — recusar a operação antes, ou reverter.

Isso é a mesma classe da C3.7 (a reação era pior que a falha) e merece o mesmo tratamento.

---

## 4. HIPÓTESES JÁ REFUTADAS — não repita

| Hipótese | Como caiu |
|---|---|
| Contenção leitor/publicador no snapshot (C3.8) | `Attempts=1 Busy=0` — o publicador pegou o lock na **primeira** tentativa, sempre. A retentativa de 16 vezes que adicionei **nunca foi usada**. Não fez mal, mas não era o conserto |
| Vazamento de slots em `TEARDOWN` | `Teardown=0` em todas as amostras |
| Endereço de destino do SDMA errado | `DstMc=0xF400000000` = `VramMcStart` + offset 0 = framebuffer do GOP. Verificado por leitura, sem gastar corrida |
| `SetVidPnSourceAddress` movendo o scanout | A DDI só grava estado e devolve SUCCESS (`bc250_display_ddi.c:2245`); o OTG segue no offset 0 por herança do GOP |
| Blt síncrono explicando os 26 FPS | **1,37 ms** de média por blt num quadro de ~38 ms = **3,6%**. O custo está no Vulkan |
| Fence do MEC presa | `Sub == Ret == V2Notified` e `InFlight=0` em **todas** as corridas |

---

## 5. ESTADO DO CÓDIGO — o que entrou nesta sessão

Tudo offline, tudo com portão e mutação verificada. **Nada empacotado foi validado em hardware além do que a seção 1 lista.**

| Faixa | O que é |
|---|---|
| **C3.1** | `sdma_blt_executed` casava com a string `PRESENT_BLT` — dava PASS para present feito por CPU. Corrigido; check novo `no_host_copy_fallback`; contador `HostCpuCopyCount`; a "Garantia de Scanout GOP" por CPU foi **removida** |
| **C3.4** | Conversão offset-de-segmento → MC em fonte única (`bc250_segment_address_contract.{h,c}`), faixa conferida e **falha fechada** |
| **C3.5** | Correção do domínio de endereço: origem e destino têm de partir da **mesma base** (`VramMcStart`, não `caps.VramMcBase`) |
| **C3.6** | Mesma conversão aplicada ao `DxgkDdiPresent` — era o segundo caminho com o mesmo defeito |
| **C3.7** | Byte proof reprovado **deixou de desligar o SDMA globalmente**. O portão que **exigia** o desligamento foi invertido |
| **C3.8** | Publicador do snapshot retenta 16× (inócuo, ver seção 4) + diagnóstico `PublishStatus/Attempts/Busy` — **foi ele que refutou a própria hipótese** |
| **C3.9** | Gate do Node 1 por processo, atrás do tunable `Node1JournalGatePerProcess` (default 0). **VALIDADO em 16:09** |
| **C3.10** | `recover-bc250mcdm-lost-tail.ps1` travava o bind fatiando 201 MB como array do PowerShell. Cópia em blocos: **1,2 s** |
| **C3.12** | `Bc250GpuvaSpaceDescribeOccupancy` + linha `OCUPACAO` — **é o que produziu o dado da seção 2** |
| **C3.13** | `tools/arm-crash-dump-on-hang.ps1` — dump em travamento (já **armado** na máquina) |

**Pacote em disco, assinado e auditado:** `3.156.1.0`
- SYS fonte `24E177E8…`, assinado `4153F9A1…`
- UMD `DD4DFED4…` (inalterada desde 3.149.1.0)

**Não feito, declarado:** **C3.11** — amostrar as seis tags de log de alto volume. Ver seção 6.

---

## 6. ARMADILHAS — leia antes de gastar corrida

### 6.1 O log cresce ~200 MB por corrida com o DWM vivo
Medido: 209 MB (13:29), 207 MB (16:09), 200 MB (16:31), contra 0,5–7 MB sem `D3D11LabPromotion`. São **2,37 milhões de linhas em ~193 s**, e `Bc250DisplayLog` abre/escreve/`ZwFlushBuffersFile`/fecha **por linha** → **~12.268 ciclos por segundo** no caminho de paginação.

Seis tags são **80,3%** do volume e **nenhuma carrega veredito**: `UpdatePT`, `FlushMappedGfxhubTlb`, `PT_WALK`, `ActiveUpdatePT`, `ActiveFlushTlb`, `McdmBuildPagingBuffer`. Amostrá-las é a faixa **C3.11**, ainda não feita. Isso é efeito observador dentro da própria medição.

### 6.2 `build-contract.bat` DEVE permanecer CRLF
Em LF o `cmd.exe` come o primeiro caractere das linhas após o `vcvars` e testes deixam de existir silenciosamente (faixa A8). Qualquer script que edite esse arquivo tem de preservar CRLF.

### 6.3 Heredoc do Bash corrompe `\n` em literais C
Perdi uma compilação por isso. Use ferramentas de edição de arquivo, não heredoc, para conteúdo com backslash.

### 6.4 Mutação que não se aplica não prova portão
A mutação 25 desta sessão ficou **aplicada no repositório** porque a restauração falhou por lock de arquivo e eu não conferia o restauro. O harness de mutação passou a retentar a escrita **e verificar** que o arquivo voltou. Faça o mesmo.

### 6.5 `build EXIT=1` nem sempre é teste falhando
Pode ser a **auditoria** reprovando. Eu conflatei as duas coisas e declarei no documento que não conseguiria explicar uma regressão — conseguia. Sempre rode o `.exe` do teste diretamente para separar.

### 6.6 CAS não é leitura
`InterlockedCompareExchange(&campo, 0, 0)` compila para `lock cmpxchg`, que **escreve** mesmo sem trocar valor. Usado para *amostrar* `GpuvaSnapshots.ActiveSlot`, reprovou `active_miniport_test` de forma determinística. Campo `volatile long`: **ler basta**. Há portão contra isso agora.

### 6.7 Ordem obrigatória de corrida
1. `tools\BC250_RECOVERY.cmd` (elevado) — o fusível está **TRIPPED** agora
2. `tools\inspect-boot-fuse.ps1 -Reset`
3. **Boot frio**
4. Bind

O empacotador **não compila o SYS** — ele copia. Antes de empacotar, rode `tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`, e **suba o `DriverVer` fonte** (dois artefatos com o mesmo `DriverVer` já aconteceu no 3.92.1.0).

---

## 7. O comando da corrida

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableD3D11LabPromotion -EnableNode1JournalGatePerProcess -RunRadvPresentTripod -WatchdogTimeoutSeconds 180
```

### O que ler no log

| Linha | Decide |
|---|---|
| `OCUPACAO ... Mapped=? Teardown=? Reusable=?` | pico real de faixas; escolhe o número de 3.2 |
| `PublishStatus=3` | `NO_SPACE` persiste → o conserto não pegou |
| `ExecutionState=7` no stderr | DMA page fault → tradução não comitada |
| `HostCpuCopies=0` | **obrigatório**. Acima de zero, a imagem é da CPU e não conta |
| `swapchain_completed: True` | o alvo: 600/600 quadros |

### Se travar

`CrashOnCtrlScroll` **já está armado** nos dois drivers de teclado, `CrashDumpEnabled=2`, `AlwaysKeepMemoryDump=1`.

**Segure o Ctrl DIREITO e pressione Scroll Lock duas vezes.** Gera `BugCheck 0xE2` e grava `C:\Windows\MEMORY.DMP`. **Não use o botão de reset** — foi o reset que nos deixou cegos em 16:31 e em 17:45. O dump é o dado que falta para a classe "travamento sem BugCheck", que ocorreu **duas vezes** e segue **sem atribuição**.

---

## 8. Itens abertos, em ordem

1. **`NO_SPACE` no rebuild transacional** — seção 3. É a parede.
2. **A reação ao `NO_SPACE`** deixa a tradução inconsistente e produz DMA page fault — seção 3.4.
3. **Travamento duro sem BugCheck**, 2 ocorrências, sem dump, **sem atribuição**. Dump já armado.
4. **C3.11** — amostrar as seis tags de log; 12k flushes/s no caminho de paginação é candidato plausível para o item 3.
5. **Os quatro arquivos-irmãos** (`CLAUDE.md`, `AGENTS.md`, `GEMINI.md`, `.cursorrules`) estão sincronizados entre si mas com a barra de **13:01**; falta trazer 13:11 em diante.
6. **26 FPS** no swapchain — não é o blt (3,6% do quadro). Custo está no Vulkan, não medido.

---

## 9. Leis que esta sessão confirmou na prática

- **Lei 1.** Quatro vezes escrevi uma causa raiz que a corrida seguinte refutou. Em todas, a correção no documento foi feita **no lugar**, não apagando.
- **Lei 2.** O tripé passou inteiro com a origem 11 MB errada — **nenhuma das três pernas confere de ONDE se leu**.
- **Lei 4.** A cópia por CPU produziu três "vitórias" falsas nesta sessão. O que as pegou foi contador dedicado (`HostCpuCopies`), não inspeção visual.
- **"O gate carimbava o falso sucesso"** — 5ª e 6ª ocorrências hoje: `sdma_blt_executed` aceitando `PRESENT_BLT`, e um portão que **exigia** o desligamento global do SDMA.
- **Uma variável por candidato.** Violei em 12:51 empilhando duas mudanças e custou uma corrida.

**NÃO VALIDADO EM HARDWARE** é o veredito de tudo na seção 5 que não está na seção 1.
