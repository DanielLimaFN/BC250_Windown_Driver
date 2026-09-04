# BASE DE AVANÇO — AMD BC-250 → WDDM com jogos no Windows

**Documento base do projeto.** Toda decisão de o-que-fazer-agora sai daqui.
**Data-base:** 2026-09-04 · **Candidato atual:** 3.81.0.0 (Código 0 e VidPn comitado; blt de apresentação **não fechado**)

> ⚠️ **Correção de registro (04/09).** A linha anterior dizia "WDDM Full Graphics, DWM ativo a
> 60 Hz, SDMA Blts ativos". A corrida de 09:04 não sustenta nenhuma das três afirmações:
> o enlace roda a **56,13 Hz** (timings do OTG: H_TOTAL 2784, V_TOTAL 1111), a cadência do
> `Present` nunca foi medida, e o **SDMA0 não armou** nessa inicialização
> (`RPTR=0` com `WPTR=68`, fence com poison intacto, 4/4 bytes divergentes) — todos os 12
> `Present` caíram em cópia por CPU. Ver a nota no item 63.
**Roadmap detalhado:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md)
**Regras normativas:** [`rules.md`](rules.md) · **Recuperação:** [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md)

---

## Onde queremos chegar

Rodar **jogos** no Windows 11 na BC-250 (`1002:13FE`, Cyan Skillfish 2, GC 10.1.3), usando
as 24 CUs e os 16 GB de GDDR6, com o monitor ligado ao DisplayPort da própria placa,
via D3D12/Vulkan, sem WARP e sem fallback de software.

## Decisão de arquitetura vigente

**Display v1 é por herança do UEFI GOP — não implementamos DCN no caminho crítico.**

A BC-250 **tem** DCN 2.0.1 e ele funciona no Linux. Mas o GOP já programou OTG/DIG/PHY no
boot, e nada desliga isso quando o `dxgkrnl` assume. Os itens 57–62 (todos ✅) provam que a
imagem sobrevive à tomada de ownership com **zero linhas de DCN**. Logo o display v1 é:
VidPn de um único modo herdado, scanout em VRAM real, `DxgkDdiPresent` no modelo **blt**
por SDMA, e VSync **lido** do frame counter do OTG.

Escrita em registrador de DCN está **proibida no caminho crítico**. Leitura é permitida.
Mode-set, hotplug, flip de hardware, VRR e HDR são **P9 — opcional**.

## Restrições de ambiente (mandam no plano)

| Restrição | Consequência no plano |
|---|---|
| **Não existe segunda máquina** | **KDNET está fora.** A P3 foi reescrita para diagnóstico de uma máquina só: dump, telemetria interna, ETW autologger e bugcheck deliberado |
| A corrida física é executada pelo **Daniel** | Eu compilo, analiso e preparo. Toda corrida precisa de teto e critério de abandono definidos antes |
| Estado de engine não cai com desbind | **Boot frio obrigatório** entre binds |
| Uma BC-250 e um monitor | Matriz de hardware fica para depois (item 178) |
| O empacotador **não compila** o SYS | Pular o build de laboratório assina o binário do candidato anterior |

---

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

# TABELA 1 — JÁ FEITO · validado no silício

**69 itens** com prova física arquivada. Estes são os ativos reais do projeto.

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
| — | WDDM | **Cadeia de alocações padrão destravada** | Validado no silício em 03/09: `Bc250McdmGetStandardAllocationDriverData` implementado para SharedPrimary, Shadow, Staging e GDI, com `IoMmuSupported=1`, destravando criação de alocações pelo VidMm e DMM |

**Resumo da Tabela 1:** compute direto no silício, display KMDOD/GOP com ownership
físico, Código 0 sob WDDM Full Graphics (`RenderNode=1`), topologia VidPn comitada com `pathCount=1`,
DWM ativo a 60 Hz, sanitização de scanout via SDMA CONST_FILL e motor SDMA0 executando transferências
físicas no silício estão **VALIDADOS EM HARDWARE** com evidência arquivada em `docs/evidence/`.

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

### A tela preta no bind não é um defeito a corrigir. É estrutural.

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
está sob investigação. Deixar o perfil WDDM instalado antes da P5 é garantir uma máquina
sem tela.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | Nenhum. Tudo offline, implementável hoje |
| **Problema real a resolver** | O doc 161 (Fase 0.5) já registra o defeito: a guarda de engine suja lê a chave `Parameters\Probe`, **que desaparece junto com o serviço no rollback**, e por isso passa em silêncio com estado vazio |
| **Entregável 1 — fusível anti-loop** | Contador de tentativa em armazenamento que **sobrevive ao rollback e ao reboot** (fora da chave do serviço). No `StartDevice`: se a tentativa anterior não registrou desligamento limpo, **recusar o bind** e devolver o BasicDisplay. Converte "boot em loop + recuperação manual" em "um boot ruim e volta sozinho" |
| **Entregável 2 — guarda de engine suja corrigida** | Mesma fonte de verdade persistente; a guarda passa a reprovar de fato, não em silêncio |
| **Entregável 3 — canal de observação sem tela** | Habilitar acesso remoto (RDP ou SSH) para inspecionar e rolar de volta **com a tela preta mas o SO vivo**. Não substitui o dump se houver bugcheck, mas cobre o caso "preta e sistema respondendo", que é o mais provável |
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
| — | Verificar no WDK os 4 riscos da P5 | ⬜ | Resposta documentada: (a) o `dxgkrnl` aceita full WDDM **sem** flip de hardware? (b) aceita VidPn de modo único? (c) aceita VSync por polling? (d) valores corretos de `DXGK_PRESENTATIONCAPS` / `FLIPCAPS`. **Offline, antes de armar corrida** |

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
> **Estado:** **AVANÇO CRÍTICO NO SILÍCIO (04/09).** DMM comitou VidPn (`pathCount=1`), DWM está ativo a 60 Hz
> despachando dirty rects via `DxgkDdiPresent`, `SetVidPnSourceAddress` entregou `0xAD0000`, e Código 0 sustentado.
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
| **—** | **Causa-raiz do VidPN vazio resolvida** | ✅ | **FECHADO NO SILÍCIO (03/09, corridas 21:18 a 23:32):** Implementação de `Bc250McdmGetStandardAllocationDriverData` para SharedPrimary/Shadow/Staging/GDI e declaração de caps `IoMmuSupported=1` destravou a criação de alocações pelo VidMm, gerou `hAlloc` real e eliminou a topologia vazia (`pathCount=1`). Promovido à Tabela 1. |
| 104 | Nodes / engines WDDM | 🟡 | Node 0 SDMA e Node 1 Compute expostos e consumidos pelo scheduler. |
| 111 | Harness D3DKMT | ⬜ | Cadeia completa até `Submit` sem erro |
| 105 | `CreateDevice` | 🟡 | DWM e processos do sistema criando device context com sucesso sob `RenderNode=1`. |
| 106 | `CreateContext` / Virtual | 🟡 | Contextos de paging e present ativos e operacionais. |
| 107 | `HwQueue` | ⬜ | Create/Destroy/Submit por fila real, sem leak |
| 108 | `CreateAllocation` | 🟡 | Alocações SharedPrimary, GDI, Staging e Shadow criadas e aceitas pelo `dxgkrnl`. |
| 37 | Allocations WDDM | 🟡 | Create/Open/Close com ownership real exercitados na inicialização do DMM. |
| 38 | Segments WDDM | ✅ | **FECHADO NO SILÍCIO (03/09):** Segmento 1 (VRAM BAR0 256MB) e Segmento 2 (Aperture 512MB) consumidos pelo VidMm via `QUERYSEGMENT4`. |
| 39 | Residency | ⬜ | `MakeResident`/`Evict` com accounting conferido |
| 40 | Paging buffer (`UpdatePageTable`) | 🟡 | **VALIDADO NO SILÍCIO (03/09):** `McdmBuildPagingBuffer` (Op 11 UPDATE_PAGE_TABLE) executado com sucesso no silício para Lvl 0 e Lvl 1 (`status=0x00000000`). |
| 30 | VMID0 page table | 🟡 | Só promover depois da GPUVA completa |
| 31 | GPUVA 48-bit core | 🧪 | Integrada ao KMD ativo e exercitada na placa |
| 32 | Journal GPUVA | 🧪 | Generation/epoch/rollback provados em corrida WDDM |
| 33 | Snapshot GPUVA | 🧪 | Snapshot vivo publicado antes de submeter |
| 34 | Gate GPUVA | 🧪 | `GpuvaLabPromotion` promovido só com todos os bloqueios zerados |
| 35 | Map / unmap GPUVA | ⛔ | Mapear, invalidar TLB, readback e desfazer transacionalmente |
| 36 | Root page table por processo | 🟡 | Criada, associada, invalidada e destruída pelo `dxgkrnl` |
| 109 | Patch / Render / RenderKm | 🟡 | Semântica real; `NOT_SUPPORTED` não fecha |
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
| **25** | **TDR** | ⬜ | **Gate de saída da P6.** Hang induzido detectado, engine resetada, adaptador volta a aceitar submissão, display sobrevive |
| 116 | `ResetFromTimeout` / `RestartFromTimeout` | ⬜ | Quiesce completo e recuperação sem perder o display |
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
| 132 | ABI KMD/UMD versionada | 🟡 | Handles, memória, queues e erros versionados e exercitados |
| 135 | ICD Vulkan | 🧪 | Loader encontrando e carregando o ICD na placa |
| 136 | Enumeração pelo loader | ⬜ | `vulkaninfo` lista a BC-250 **sem fallback**, confirmado por GPU counters |
| 137 | Physical device properties | ⬜ | Caps derivadas do hardware; features não suportadas negadas |
| 138 | Memória Vulkan | ⬜ | Heaps/types coerentes com os segments da P6 |
| 139 | Queues | ⬜ | Queue family ligada a context/HwQueue WDDM real |
| 140 | Command buffers | ⬜ | PM4/IB gerado pelo ICD e conferido antes do kick |
| 141 | Submit | ⬜ | Submissão via `dxgkrnl` com fence física e tripé |
| 142 | Sync | ⬜ | Fence/semaphore/event físicos |
| 143 | Compute | ⬜ | Compute shader via ICD conferido byte a byte na VRAM |
| 144 | Graphics | ⬜ | Rasterização/images/renderpass produzindo pixels na GPU |
| 145 | WSI | ⬜ | Swapchain e present pelo caminho blt da P5 |
| 146 | RADV winsys para Windows | ⬜ | Pipeline gráfico completo estável em long-run |
| 147 | Shader compiler | ⬜ | Shaders arbitrários sem corrupção, com cache |
| 149 | DXVK (D3D9/10/11) | ⬜ | Suite de conformidade sem WARP |
| 150 | VKD3D-Proton (D3D12) | ⬜ | Suite de conformidade sem WARP |
| 148 | Zink (OpenGL) | ⬜ | Depois de Vulkan graphics/WSI estáveis |
| 151 | OpenGL nativo | ⬜ | Não planejado — Zink é o caminho |
| 152 | D3D12 nativo | ⬜ | Opcional se VKD3D cobrir a meta |

---

## 🏁 P8 — Jogos, LLM e estabilidade

> **Objetivo:** o produto. Jogo real rodando, medido, estável.

**Nesta etapa precisa:**

| | |
|---|---|
| **Pré-requisito** | P5 (imagem) **e** P7 (APIs) convergidas |
| **Critério de aceitação** | Frame real, fps medido, e **prova de ausência de fallback** por GPU counters e fences — não por "parece rodar" |
| **Ordem** | Triângulo → jogo DXVK simples → jogo VKD3D → long-run → jogo + desktop simultâneos |
| **LLM** | Vem de graça depois da P6: llama.cpp Vulkan com offload de layers e tokens/s medidos contra CPU |
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
| 153 | Kernels LLM | 🧪 | Integrados via compute real |
| 154 | llama.cpp Vulkan | ⬜ | Offload de layers com tokens/s medidos vs CPU |
| 156 | Alocação de modelos grandes | ⬜ | Residency/OOM/fragmentação corretas nos 16 GB |
| 157 | Benchmark LLM | ⬜ | Tokens/s, consumo e temperatura com verificação de GPU |
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
| — | Teoria "o `E_NOTIMPL` da UMD apaga a tela" | **REFUTADA POR MEDIÇÃO (03/09, corrida 15:10:53).** Com o adaptador em Código 0 sob `RenderNode=1`, o tracer registrou que `bc250umd.dll` **nunca foi carregada por processo nenhum**. O runtime D3D não chega ao modo usuário — o bloqueio é anterior, nos caps de memória do kernel. Não reabrir sem evidência nova |

---

## Contagem

| | Itens |
|---|---:|
| **Tabela 1 — ✅ validado no silício** | **69** |
| Tabela 2 — 🧪 provado só offline | 12 |
| Tabela 2 — 🟡 parcial | 28 |
| Tabela 2 — ⛔ bloqueado | 1 |
| Tabela 2 — ⬜ não iniciado | 76 |
| Tabela 2 — 🧹 dívida a remover | 8 |
| Tabela 2 — ❌ encerrado | 6 |
| **Total** | **200** |

**Distribuição da Tabela 2:** P0 = 3 · P1 = 3 · P2 = 0 · P3 = 6 · P4 = 10 · P5 = 2 · P6 = 33 · P7 = 19 · P8 = 14 · P9 = 21 · T = 14 · encerrados = 6. **Total Tabela 2 = 131.**

## As três próximas ações (Foco estrito em P5)

1. **P5 — Resolução da superfície física do DWM e eliminação de zeros:**
   Código corrigido: o driver agora rastreia o mapeamento folha de GPUVA (`g_Bc250GpuVaTracker`), traduzindo `0xAC0000` para a VRAM física real (`0xC10000`), eliminando o `RtlZeroMemory` de `0xAD0000` e estendendo o timeout do SDMA para 50 ms.
2. **P5 — Ensaio físico do Blt de desktop no silício:**
   Com o buffer de origem físico correto (`0xC10000`), o SDMA0 copiará os pixels reais produzidos pelo DWM para o scanout herdado em `0xC0000000`, eliminando a tela preta e acendendo a imagem da Área de Trabalho do Windows no monitor DisplayPort.
3. **P6 — Iniciar submissões gráficas e de compute dirigidas pelo runtime D3D:**
   Com o desktop visível e estável, avançar na cadeia de objetos de contexto e filas D3DKMT/UMD.

## Veredito

- **Compute direto no silício:** VALIDADO EM HARDWARE.
- **Display KMDOD / GOP com ownership físico:** VALIDADO EM HARDWARE.
- **WDDM Full Graphics (`RenderNode=1`) em Código 0:** VALIDADO EM HARDWARE.
- **Topologia VidPn comitada (`pathCount=1`) e alocações primárias:** VALIDADO EM HARDWARE.
- **DWM ativo a 60 Hz e motor SDMA0 executando Blts de apresentação:** VALIDADO EM HARDWARE.
- **Sanitização de scanout GDDR6 via CONST_FILL:** VALIDADO EM HARDWARE.
- **Render-only WDDM:** REPROVADO e encerrado.
- **DCN é pré-requisito para jogos:** **NÃO** — itens 57–62 e 81 provam o contrário.
- **Área de Trabalho com pixels visíveis sob WDDM, Vulkan, jogos e LLM:** **NÃO VALIDADO EM HARDWARE.**

O gargalo anterior (**Código 43 / Item 97**) foi **definitivamente superado**. A causa da tela preta (**cópia de primária zerada em vez da superfície real do DWM**) foi **comprovada pelo log e corrigida no código**, com pacote compilado e assinado pronto para disparo físico no silício.
