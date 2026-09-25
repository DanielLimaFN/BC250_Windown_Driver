# Roadmap 164 — WDDM com jogos na BC-250, **sem** implementar DCN no caminho crítico

> **Estado operacional em 25/09/2026:** [handoff 438](438_HANDOFF_2026-09-25_CORRIDAS_111_A_118_VISUAL_VELOCIDADE_E_TRAVAMENTO.md) e [base de avanço](../BASE_DE_AVANCO.md). A Corrida 118 validou desktop visual por 180 s, 60/60 apresentações e rollback no mesmo boot; taxa ~0,9 FPS no harness, visual intermitente entre corridas e travamentos em janelas longas mantêm a meta de desktop rápido/estável e jogos aberta. As etapas e percentuais antigos deste roadmap são planejamento/histórico, não veredito atual.

> **Escopo de produto atualizado em 17/09/2026:** ver [plano de desktop e jogos transparentes](281_PLANO_PRODUTO_DESKTOP_E_JOGOS_TRANSPARENTES.md) e [handoff doc 284](284_HANDOFF_2026-09-17_15-12_D3D11_NATIVO_GFX_RB0_E_ISOLAMENTO_SCANOUT.md). A meta inclui desktop D3D11 acelerado, aplicações não modificadas e integração Direct3D ao driver. DCN básico em P9 integra a conclusão desse produto, embora não seja pré-requisito do primeiro desktop em modo GOP. As menções históricas abaixo a DCN totalmente opcional valem para a demonstração em modo fixo, não para esse escopo ampliado. Os marcos antigos abaixo não representam o status atual; consultar BASE_DE_AVANCO.md e o plano.

**Data-base:** 2026-09-02 · **Revisão 2** (substitui a revisão 1, que colocava DCN como pré-requisito)
**Meta:** BC-250 rodando jogos no Windows (D3D12/Vulkan) com as 24 CUs e 16 GB GDDR6, com
o monitor ligado ao DisplayPort da própria placa.
**Substitui:** `DocsObsoletos/raiz-superseded/ROADMAP_BC250_WDDM_DISPLAY_RENDER_DCN.md`

> Compilar, assinar ou retornar `STATUS_SUCCESS` não equivale a funcionar.
> Onde não existir prova física, consta **NÃO VALIDADO EM HARDWARE**.

---

## 1. A pergunta do DCN, respondida com fonte

Você pediu para checar se a BC-250 tem DCN no Linux. Verifiquei. **A resposta é sim, e
com folga** — mas a conclusão prática é a que você intuiu: **não precisamos implementar
DCN para ter jogos.** As duas coisas não se contradizem, e a distinção é o ponto central
desta revisão.

### 1.1 A BC-250 tem DCN, e o Linux o usa

| Fato | Fonte |
|---|---|
| O display da Cyan Skillfish é **DCN 2.0.1 ("dcn201")**, uma variante distinta do DCN2 | AMD publicou o suporte em set/2021, ~34 mil linhas (a maior parte headers gerados) |
| O suporte inicial da Cyan Skillfish entrou no Linux 5.15 **sem** display; o display veio em série separada | Série "cyan skillfish display support", amd-gfx |
| O bloco de display é IP de primeira classe neste chip | `cyan_skillfish_reg_init.c` mapeia `adev->reg_offset[DCE_HWIP][i] = &DMU_BASE.instance[i]` |
| A saída DisplayPort **funciona** no Linux via amdgpu/DC | Documentação da comunidade BC-250 e SkillFishOS |

Ou seja: o silício tem display engine completa, a programação dela está documentada em
código aberto e funciona hoje em outro sistema operacional. Isso **de-risca** um eventual
trabalho de DCN — mas não o torna necessário agora.

### 1.2 Dois erros corrigidos por essa checagem

**Erro 1 — a base do DCN que estava no repositório está errada.** O
`REGISTER-MAP-BC250.md` de referência listava `DCN base 0x5000` e `MMHUB base 0x5000` —
colisão. O valor autoritativo, do header do kernel `cyan_skillfish_ip_offset.h`:

```c
#define MAX_SEGMENT 5
static const struct IP_BASE DMU_BASE   = {{{{ 0x00000012, 0x000000C0, 0x000034C0, 0x00009000, 0 }}, ...
static const struct IP_BASE GC_BASE    = {{{{ 0x00001260, 0x0000A000, 0, 0, 0 }}, ...
static const struct IP_BASE MMHUB_BASE = {{{{ 0x0001A000, 0,        0, 0, 0 }}, ...
```

O DMU tem **quatro segmentos**, e cada registrador de DCN carrega seu `BASE_IDX` — o
offset é `DMU_BASE.seg[BASE_IDX] + reg`. **Não existe base plana.** Os offsets
`0x5080/0x6000/0x7000` do repositório de referência são hipótese a reconciliar, não
catálogo.

**Erro 2 — meu próprio.** Na revisão 1 deste roadmap eu mandei derivar o DMU do IP
Discovery. Está errado para este chip: o Linux **hardcoda** as bases da Cyan Skillfish
(`cyan_skillfish_reg_base_init`), não usa discovery, e `IP_VERSION(2,0,1)` nem aparece na
lista de `amdgpu_discovery_set_display_ip_blocks`. A fonte correta é o header acima; o
papel da placa é **confirmar por readback**, não descobrir.

### 1.3 Restrição de hardware que herdamos do Linux — e que importa muito

Existe um patch upstream **"disable SG displays on cyan skillfish"**: *scatter-gather
display* fica desligado por padrão neste chip. Consequência direta:

> **O framebuffer de scanout tem que estar na VRAM real (o carve-out do BIOS), não em
> memória de sistema / GTT.**

Isso é acionável no Windows: a primary/scanout allocation precisa ser VRAM, e o split de
VRAM configurado no BIOS passa a ser parte da receita. Também explica os relatos de "pin
failed" no Linux quando o heap local enche.

Nota de campo, também do Linux: **a detecção de monitor (HPD) do DisplayPort da BC-250 é
quebrada** — a comunidade força com `video=DP-1:e` e um daemon de hotswap. Isto é um
argumento *a favor* de não perseguir hotplug: nem no Linux ele funciona direito.

### 1.4 Por que **não** precisamos de DCN — e a prova já está neste repositório

O que mantém sua tela acesa hoje não é o BasicDisplay: é o **UEFI GOP**, que no boot
programou OTG, DIG/PHY e fez link training. Nada desliga isso. A transferência de
ownership para o `dxgkrnl` **não reprograma nem desabilita a display engine** — o
pipeline continua fazendo scanout do endereço em que o HUBP já aponta.

E isso não é teoria. Está medido, no seu próprio checklist:

| Item | Estado | O que prova |
|---|:---:|---|
| 57 | ✅ | Registro KMDOD em Código 0 |
| 58 | ✅ | Framebuffer do GOP herdado: endereço, pitch e modo |
| 60 | ✅ | `PresentDisplayOnly` copiando de verdade |
| 61 | ✅ | VSync contando e notificando |
| **62** | ✅ | **Imagem após o binding, confirmada várias vezes** |
| doc 26 | ✅ | Post display ownership **físico** |

**Zero linhas de DCN no repositório** — `grep dcn` = 0 — e a imagem sobrevive à tomada de
ownership. Essa é a prova de que o caminho sem DCN existe.

O que falta não é DCN: é fazer isso pelo contrato **full WDDM** (`DxgkInitialize`) em vez
do contrato display-only (`DxgkInitializeDisplayOnlyDriver`), para que o adaptador também
tenha render.

### 1.5 O que se ganha e o que se perde

| Sem DCN (v1) | Com DCN (v2) |
|---|---|
| ✅ 24 CUs e 16 GB usados de verdade | idem |
| ✅ D3D12 / Vulkan / jogos | idem |
| ✅ DWM composto pela GPU | idem |
| ✅ Modo herdado do UEFI (ex. 2560x1080@60) | Qualquer modo do EDID |
| ❌ Trocar resolução pelo Windows | ✅ |
| ❌ Hotplug (quebrado na placa até no Linux) | ✅ com esforço |
| ❌ Flip de hardware → present por blit, tearing possível | ✅ flip real, sem tearing |
| ❌ VRR / HDR / MPO / multi-monitor | ✅ |

**Para jogar, a coluna da esquerda basta.** É exatamente o modelo dos drivers WDDM
sintéticos de máquina virtual: sem display engine própria, present por cópia, e ainda
assim desktop acelerado e jogos.

### 1.6 O custo do "sem flip" é quase zero

Present por blit não significa cópia por CPU. O **SDMA está validado no silício**
(itens 19, 48, 49). O present vira uma cópia SDMA da surface renderizada para a surface
de scanout herdada:

```
2560 × 1080 × 4 B ≈ 11,1 MB por frame
11,1 MB × 60 fps ≈ 664 MB/s
```

VRAM→VRAM em GDDR6 — irrelevante. E como o SG display está desligado neste chip, as duas
surfaces já precisam estar em VRAM de qualquer forma, o que é justamente o caso favorável.

### 1.7 A tensão com a Lei 3 (Zero Fakes), resolvida

Sem interrupção de DCN, de onde vem o VSync?

| Opção | Veredito |
|---|---|
| Timer de kernel a ~60 Hz (o que o KMDOD faz hoje) | Funciona, mas é sinal **sintetizado**. Fallback, não escolha |
| **Ler o frame counter / scanline real do OTG (somente leitura)** | **Recomendado.** Leitura não é escrita: sem risco de tela preta, e a fase do VBlank vem do hardware de verdade |

A segunda opção mantém a Lei 3 intacta — o VSync é **medido**, não inventado. E custa
apenas o inventário read-only da Fase C, não o DCN inteiro.

### 1.8 Então por que houve tela preta e reboot?

Não por falta de DCN. As corridas que apagaram a tela anunciavam uma saída de vídeo
física cujo **único caminho de apresentação retornava `STATUS_NOT_SUPPORTED`**
(item 119), e/ou executavam bring-up de hardware dentro do `StartDevice`. O bugcheck
exato continua **não identificado** — só o dump responde (Fase A). Qualquer afirmação de
`0x116` antes disso é palpite.

---

## 2. Magnitude, agora honesta

A revisão 1 dizia "equivale a escrever um driver de display AMD mais um driver Vulkan".
Com o DCN fora do caminho crítico, **cai a primeira metade**. O que resta:

1. **Render WDDM** (memória, objetos, submissão, fence, TDR) — grande, e inevitável em
   qualquer arquitetura.
2. **UMD / Vulkan** (ICD, winsys RADV, DXVK/VKD3D) — a maior fase restante.
3. **Display por herança** — pequeno: uma VidPn de um modo, uma allocation em VRAM, um
   blit por SDMA e um VSync lido do OTG.

A fase mais cara do projeto (mode-set, AUX/DPCD, link training, PHY, DCCG, DPP/MPC/OPP)
sai do caminho crítico e vira **v2 opcional**.

---

## 3. Regras de execução (valem em todas as fases)

1. **Offline antes de silício.** Corrida física é o recurso escasso.
2. **Mineração de fonte antes de corrida.** Esta revisão nasceu de leitura de header do
   kernel, não de ensaio — e corrigiu dois erros de endereço sem gastar nada.
3. **Fire-and-collect.** Janela fixa, retrato, rollback e reboot incondicionais.
4. **Boot frio entre binds.** Estado de engine não cai com desbind.
5. **Orçamento por gate** definido *antes* de armar: teto de corridas e critério de abandono.
6. **Tripé no silício** em toda prova: `RPTR==WPTR`, fence física em VRAM com poison,
   conferência byte a byte.
7. **Nenhuma escrita em registrador de DCN em nenhuma fase do caminho crítico.**
   Leitura é permitida; escrita é v2.

---

## 4. Fases

### Fase 0 — Segurança do ensaio · offline · antes de gastar corrida

**A tela preta no bind não é um defeito a corrigir — é estrutural.** A BC-250 tem uma
única função PCI com saída de vídeo; quando o `bc250mcdm` liga, o BasicDisplay é
deslocado, e entre um e outro a tela apaga por construção (doc 162). O que é corrigível é a
**duração**: hoje preta permanente → TDR → reboot; o alvo é preta por 1–2 s e imagem de
volta — e isso exige que o driver **apresente**, o que é a Fase D. **A tela preta está
depois do gargalo, não antes.**

O que dá para atacar agora é o **custo** de um FAIL:

| # | Tarefa | PASS |
|---|---|---|
| 0.1 | **Fusível anti-boot-loop persistente** | Falha forçada → no boot seguinte o driver recusa o bind sozinho → BasicDisplay de volta, sem intervenção manual |
| 0.2 | Guarda de engine suja corrigida (doc 161, Fase 0.5) | Fonte de verdade sobrevive ao rollback; a guarda reprova de fato, não em silêncio |
| 0.3 | Canal de observação sem tela (RDP/SSH) | Sessão remota viva com a tela preta, permitindo retrato e rollback interativos |
| 0.4 | Orçamento de corridas formalizado (item 177) | Teto, critério de abandono e recovery escritos **por etapa** |

**Veículos que podem ficar instalados permanentemente:**

| Veículo | Instalado? | Tela | Serve para |
|---|:---:|---|---|
| `bc250dbg` (laboratório KMDF) | ✅ permanente | intacta | toda a Fase C |
| `bc250mcdm` perfil **KMDOD** (`RenderNode=0`) | ✅ sim, Código 0 com imagem viva | funciona | Fase C′ (VSync) e a exceção da Fase H |
| `bc250mcdm` perfil **WDDM** (`RenderNode=1`) | ❌ não, até a Fase D passar | apaga | só fire-and-collect |

**Já a favor:** `StartType=3` (demand start) e `ErrorControl=0` (`SERVICE_ERROR_IGNORE`)
já estão no INF — uma **falha de carga** não derruba o boot. O loop vem do driver
**carregar** e travar depois; é isso que o fusível pega.

**Regra:** não gastar corrida da Fase B antes de o fusível estar provado ou explicitamente
dispensado. Se dispensado, o orçamento da Fase B cai de 2 para 1 corrida.

---

### Fase A — Instrumentar a falha · offline · custo zero de corrida

| # | Tarefa | PASS |
|---|---|---|
| A.1 | Analisar `C:\Windows\MEMORY.DMP` com `cdb` **elevado** | Bugcheck real, stack e DDI de origem. Encerra a especulação |
| A.2 | `lm m bc250*` + frame do miniport | Se o crash for nosso: arquivo e linha |
| A.3 | **Telemetria persistida antes do `StopDevice`** | Status e argumentos de cada DDI sobrevivendo ao teardown e legíveis pós-reboot. Foi o anel de DDI que fechou o Gate 1.5 — ampliar o que já funciona |
| A.4 | **ETW `Microsoft-Windows-DxgKrnl-Diagnostic` em sessão AutoLogger** | Trace atravessando o reboot (a falha ocorre 5 s após o bind). Verificar offline se o canal existe e o que expõe |
| A.5 | **Bugcheck deliberado com o `NTSTATUS` nos parâmetros** | `KeBugCheckEx` no ponto de interesse, atrás de tunable com default desligado. Vira um "breakpoint" que o minidump captura |

> ⚠️ **KDNET está fora do plano: não existe segunda máquina.** O doc
> [`21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md`](21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md) fica
> como referência para o dia em que houver um host, mas o caminho oficial são as camadas
> A.1–A.5, todas de uma máquina só.
>
> **Correção ao doc 124:** `windbg -kl` (local kernel debugging) não permite breakpoint,
> `g`, step nem call stack — isso está certo. Mas **permite ler memória do kernel e rodar
> extensões** (`!dxgkrnl.*`). Serve para inspeção, não para controle de fluxo. Classificado
> como **H** até confirmar que as extensões respondem nesta máquina.

```
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z C:\Windows\MEMORY.DMP -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols" -c "!analyze -v; lm m bc250*; q" > "$env:USERPROFILE\Desktop\bugcheck.txt"
```

**FAIL aceitável:** o dump é de outra causa. Registrar e seguir — a Fase B não depende dele.

---

### Fase B — Fechar o Código 43 · o gargalo único

O candidato **3.81 está assinado e auditado**, com a seção `.HW` do INF e o
`bc250umd.dll` real. Falta a corrida.

| # | Tarefa | PASS |
|---|---|---|
| B.1 | Corrida 3.81 fire-and-collect · orçamento **2 corridas** | `bc250mcdm / Código 0` sustentado **e** o bit de `DRIVERCAPS` aceso em `V5QaiTypeSeenLo` |
| B.2 | Se FAIL: diff do anel de DDI 3.80 vs 3.81 | Avançar e falhar em ponto novo já é informação |
| B.3 | Se FAIL sem avanço: escalar para A.5 (bugcheck deliberado) | Capturar o `NTSTATUS` exato da desistência, sem depender de segunda máquina |

**Marco:** primeiro Código 0 em `DxgkInitialize`. Destrava tudo.
**Abandono:** 4 corridas sem avanço no anel → parar de gastar corrida e escalar para A.5 (bugcheck deliberado) antes da quinta.

---

### Fase C — Herança do display · **somente leitura** · risco zero

Pode começar já, em paralelo à Fase B. Leitura não toca ownership: o BasicDisplay
continua dono da tela durante todo o inventário, e `IOCTL_BC250DBG_REG_READ` já existe.

| # | Tarefa | PASS |
|---|---|---|
| C.1 | Catalogar o DCN por `(reg, BASE_IDX)` a partir de `cyan_skillfish_ip_offset.h` + headers `dcn_2_0_1` | Cada entrada com fonte citada. `DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}` como origem |
| C.2 | Confirmar as bases por **readback** na própria placa | Valores plausíveis e estáveis; ambiguidade vai para quarentena |
| C.3 | Decodificar o framebuffer herdado: `DCSURF_PRIMARY_SURFACE_ADDRESS`, pitch, formato, viewport, tiling | Endereço bate com o framebuffer do GOP; timing bate com o modo na tela |
| C.4 | Confirmar se o framebuffer está na **VRAM real** e qual é o split do BIOS | Pré-requisito da Fase D (SG display desligado neste chip) |
| C.5 | `OTG_STATUS_FRAME_COUNT` avançando entre dois snapshots | Contador vivo → fonte de VSync real por leitura |
| C.6 | Medir a fase e o período do VBlank por polling | Base para o VSync da Fase D, sem escrever nada |

**PASS:** mapa de leitura do display herdado, com framebuffer decodificado e VBlank
medido — **sem uma única escrita**.
**Custo:** zero corridas destrutivas.

---

### Fase D — Present por blit no modo herdado · primeiro display sob WDDM

Depois de B (Código 0) e C (mapa de leitura). Aqui há risco de tela preta, mas muito
menor do que na revisão 1: não programamos nada de display.

| # | Tarefa | Detalhe |
|---|---|---|
| D.1 | VidPn com **um único modo**: o herdado do GOP | `IsSupportedVidPn` aceita só ele; `RecommendFunctionalVidPn` devolve ele |
| D.2 | Primary/scanout allocation em **VRAM** | Exigência do chip: SG display desligado |
| D.3 | `DxgkDdiPresent` real, modelo **blt** | Gera cópia SDMA da surface renderizada para o scanout. Nunca `STATUS_SUCCESS` sem efeito programado |
| D.4 | Declarar caps coerentes: sem flip de hardware | `FlipCaps` desligado, `PresentationCaps` refletindo blt |
| D.5 | VSync a partir do frame counter do OTG (**leitura**) | Fallback: timer, se o `dxgkrnl` recusar |
| D.6 | `DxgkDdiControlInterrupt` honesto | Só tipos realmente suportados; resto fail-closed |
| D.7 | Poison na surface de scanout antes do primeiro present | Detecta falso sucesso |

**PASS:** Código 0 sustentado, **imagem desenhada pelo nosso driver**, 10 minutos sem TDR
nem corrupção.
**Marco:** este é o momento em que a BC-250 desenha a tela. Sem trocar resolução, sem
hotplug — e isso é aceito por projeto.
**Orçamento:** 5 corridas. Abandono: 3 FAILs com a mesma assinatura → voltar à Fase C.

**Riscos a verificar offline ANTES de armar D:**

| Risco | Como verificar |
|---|---|
| O `dxgkrnl` aceita driver full WDDM **sem** flip de hardware? | WDK: `DXGK_PRESENTATIONCAPS` / `DXGK_FLIPCAPS`; comparar com o que os drivers WDDM sintéticos desta máquina declaram |
| Aceita VidPn de modo único, sem enumeração? | WDK + `RecommendFunctionalVidPn` |
| Aceita VSync derivado de polling? | Se não, cair para timer e registrar a dívida |
| O framebuffer do GOP é VRAM ou stolen? | Fase C.4 |

---

## 4.5 DCN antes do WDDM: o que e' possivel, e em qual veiculo

Pergunta legitima e recorrente. Resposta em tres partes, porque sao tres coisas com custos
completamente diferentes.

| O que | Possivel antes do WDDM? | Veiculo | Custo |
|---|:---:|---|---|
| **DCN somente leitura** (Fase C) | **Sim, agora** | `bc250dbg` + `IOCTL_BC250DBG_REG_READ` | Zero corridas destrutivas. BasicDisplay segue dono da tela |
| **DCN com escrita** | **Sim, mas nao sob o `bc250dbg`** | **KMDOD** — `DxgkInitializeDisplayOnlyDriver`, que **ja esta em Codigo 0** | Tela preta + boot frio por FAIL |
| **DCN completo como estrategia de sequenciamento** | Tecnicamente sim | KMDOD | **Nao recomendado** |

**Por que a escrita nao pode sair do `bc250dbg`.** Escrever em HUBP/OTG sob o driver de
laboratorio e' escrever num scanout **vivo**, que o BasicDisplay esta usando naquele
instante — duas entidades competindo pelo mesmo pipeline. Alem disso nao existe ownership
legitimo: o Windows entrega display ownership somente pelo caminho de POST device do
`dxgkrnl`.

**Por que o KMDOD e' o veiculo correto.** Ele **tem** ownership fisico (itens 57-62 e
`docs/26`), chega em Codigo 0 hoje, e **nao esta bloqueado pelo Codigo 43** — o Codigo 43
e' do caminho `DxgkInitialize`, contrato diferente. Ou seja, todo o trabalho de display
(mode-set, AUX/DPCD/EDID, flip, VBlank por interrupcao) pode ser desenvolvido dentro do
KMDOD com recovery funcionando, independentemente do bloqueio do WDDM.

**Por que nao sequenciar DCN primeiro.** Porque nao destrava nada do objetivo. Jogos
precisam das Fases B, E e F; DCN nao aparece em nenhuma delas. Fazer DCN antes significa
gastar a maior fatia de trabalho do projeto para terminar com a mesma tela de hoje — com
flip de hardware e troca de resolucao — e o Codigo 43 exatamente onde esta.

**A excecao delimitada.** Existe **uma** escrita pequena que talvez valha antecipar: o
flip do `DCSURF_PRIMARY_SURFACE_ADDRESS` com master update lock, dentro do KMDOD. Converte
a copia por CPU do `PresentDisplayOnly` num page flip real, sem tocar timing, link ou PHY.
Ressalvas honestas: e' escrita em scanout vivo, exige snapshot + rollback + orcamento de
corridas, e so' depois de a Fase C decodificar o framebuffer. O repositorio de referencia
registra que foi justo esse tipo de escrita que derrubou a GPU deles. Classificacao:
**possivel e delimitado, nao seguro.** Registrado como **P9** na `BASE_DE_AVANCO.md`.

**O que fazer com o tempo, enquanto o WDDM esta bloqueado:** Fase C completa (P1) e depois
trocar o timer de VSync do KMDOD pela leitura do frame counter do OTG (P2). Zero escrita,
fecha a divida da Lei 3, e o codigo sai 100% reaproveitado na Fase D.

---

### Fase E — Render WDDM completo

O compute já provado no silício passa a ser dirigido pelo Windows. Pode correr em
paralelo a C/D depois da Fase B.

| # | Itens do checklist | PASS |
|---|---|---|
| E.1 | 103, 104 | `DRIVERCAPS`, segments e nodes derivados do hardware e consumidos |
| E.2 | 111, 105–108 | Harness D3DKMT: device/context/allocation com handles reais |
| E.3 | 31–38 | Segments, allocations, GPUVA 48-bit com journal, generation e rollback |
| E.4 | 39, 40 | Residency e paging por SDMA |
| E.5 | 110, 112 | `SubmitCommandVirtual` originado pelo `dxgkrnl`; fence física ligada ao scheduler |
| E.6 | 113, 114 | IH/DPC → retirement reconhecido. **Proibido** completar fence pela CPU |
| E.7 | 51, 52, 54–56, 115 | Multi-context, preemption, prioridades, wrap, concorrência |
| **E.8** | **25, 116, 24** | **TDR: `ResetEngine` por node, `ResetFromTimeout`, `RestartFromTimeout` com quiesce real** |

**E.8 não é negociável antes da Fase F.** Desenvolver UMD sobre um adaptador que não se
recupera de hang transforma todo bug de shader em reboot — provavelmente o que já
aconteceu.

---

### Fase F — UMD e Vulkan · a maior fase restante

Substituir o `E_NOTIMPL` honesto de hoje por backend real. Subfases com PASS próprio:

| # | Entrega | PASS |
|---|---|---|
| F.1 | ABI KMD/UMD versionada; loader, instance, physical device | `vulkaninfo` enumera a BC-250 com caps do hardware |
| F.2 | Heaps e memory types coerentes com os segments de E.3; command pools/buffers | Command buffer do UMD fisicamente aposentado |
| F.3 | Fences, semáforos, barreiras, pipeline de compute | Compute shader Vulkan conferido byte a byte na VRAM |
| F.4 | Graphics: rasterização, images, renderpass | Triângulo renderizado |
| F.5 | WSI: swapchain e present pelo caminho blt da Fase D | Triângulo **na tela** |
| F.6 | Winsys RADV adaptado aos objetos WDDM deste driver | Pipeline gráfico completo, estável em long-run |
| F.7 | Shader compiler (ACO/LLVM ou equivalente) | Shaders arbitrários sem corrupção |
| F.8 | DXVK (D3D9/10/11), VKD3D-Proton (D3D12), Zink (GL) | Suites de conformidade sem WARP nem fallback |

---

### Fase G — Jogos e LLM

Long-run, pressão de memória, compilação de shaders, suspend/resume, telemetria de
desempenho; llama.cpp via Vulkan.

**PASS final:** jogo real rodando na BC-250, sem WARP, com TDR recuperando.

---

### Fase H — DCN v2 · **OPCIONAL, fora do caminho crítico**

Só entra se você quiser trocar resolução pelo Windows, segundo monitor, flip sem tearing,
VRR ou HDR. Nada aqui é necessário para jogar.

`74 HPD` → `75 AUX` → `76 DPCD` → `77 EDID real` → `78 link training` → `71 DCCG` →
`69 DCHUB/DCHUBBUB` → `70 DPP/MPC/OPP` → `72 DIG` → `73 PHY` → `82 master update lock` →
`83 flip A/B` → `84 VBlank por interrupção` → `85 mode-set` → `86 modos EDID` →
`87 hotplug` → `89 HDR/VRR/MPO`

Ativos que reduzem o custo desta fase, se um dia ela vier: o `dcn201` do Linux é código
aberto e comprovadamente funcional neste silício, e as bases do DMU já estão catalogadas
na Fase C. Contra: HPD é quebrado na placa, e mexer em PHY/link custa tela preta e boot
frio por tentativa.

---

## 5. Caminho crítico

```
0 (fusivel + orcamento) --+
                          +--> B (Codigo 0) --+--> D (present por blit) --+
A (dump / telemetria / ETW)                   |                           +--> F (UMD/Vulkan) --> G (jogos)
C (leitura do display) ----+                  |                           |
                                              +--> E (render WDDM + TDR) -+

H (DCN v2) ......... opcional, nunca bloqueia G
```

- **Fase 0 e Fase C podem andar hoje** sem gastar corrida destrutiva. A Fase 0 vem primeiro
  porque é ela que torna a corrida da Fase B barata de gastar.
- **Fase A não bloqueia a Fase B** — mas a camada 1 (o dump) deve ser lida antes de rearmar,
  porque pode revelar defeito nosso.
- Tudo depois de B está atrás de B.
- D e E podem correr em paralelo; F exige as duas convergidas.
- **H saiu do caminho crítico.** Era a fase mais cara do projeto.

---

## 6. As quatro próximas ações

1. **Fase 0.1 — fusível anti-boot-loop.** Offline. Converte um FAIL de "recuperação manual"
   em "um boot ruim e volta sozinho". É o que autoriza gastar corrida na Fase B.
2. **A.1 — `cdb` elevado no `MEMORY.DMP`.** Minutos. Encerra a especulação do bugcheck e
   pode revelar defeito nosso antes de rearmar.
3. **C.1 — catálogo do DCN por `(reg, BASE_IDX)`** a partir de
   `cyan_skillfish_ip_offset.h` e dos headers `dcn_2_0_1`. Offline, risco zero, e é o que
   habilita o VSync real e a decodificação do framebuffer na Fase D.
4. **B.1 — corrida 3.81**, orçamento de 2 corridas, **depois** da Fase 0 e de A.1 não
   apontar defeito nosso.

Em paralelo, sem custo: verificar no WDK os quatro riscos da Fase D (caps de present sem
flip, VidPn de modo único, VSync por polling, natureza do framebuffer do GOP).

---

## Veredito

- **Compute direto no silício:** VALIDADO EM HARDWARE.
- **Display KMDOD/GOP com ownership físico:** VALIDADO EM HARDWARE — e é a base do
  caminho sem DCN.
- **A BC-250 tem DCN 2.0.1 e ele funciona no Linux:** confirmado por fonte.
- **DCN é necessário para jogos no Windows:** **NÃO.** É necessário para mode-set,
  hotplug, flip de hardware, VRR e HDR.
- **Render WDDM, present por blit, Vulkan, jogos e LLM:** **NÃO VALIDADO EM HARDWARE.**

## Fontes

- [AMD Posts Code Enabling "Cyan Skillfish" Display Support Due To Different DCN2 Variant — Phoronix](https://www.phoronix.com/news/AMD-Cyan-Skillfish-DCN-2.01)
- [\[PATCH 00/02\] cyan skillfish display support — amd-gfx](https://lists.freedesktop.org/archives/amd-gfx/2021-September/069481.html)
- [\[PATCH\] drm/amd/display: disable SG displays on cyan skillfish — amd-gfx](https://www.mail-archive.com/amd-gfx@lists.freedesktop.org/msg113617.html)
- [`cyan_skillfish_ip_offset.h` (DMU_BASE) — kernel](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/include/cyan_skillfish_ip_offset.h)
- [`cyan_skillfish_reg_init.c` (DCE_HWIP → DMU_BASE) — kernel](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/amdgpu/cyan_skillfish_reg_init.c)
- [Display Issues — AMD BC250 Documentation](https://elektricm.github.io/amd-bc250-docs/troubleshooting/display/)
- [The AMD BC-250 hardware — SkillFishOS Documentation](https://www.skillfishos.com/en/docs/hardware-bc250/)
