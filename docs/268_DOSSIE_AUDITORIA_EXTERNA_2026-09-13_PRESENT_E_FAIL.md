# Dossiê para auditoria externa — por que o `Present` do DWM falha com `E_FAIL`

> **Data:** 2026-09-13 · **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)
> **Plataforma:** Windows 10 Pro 22H2 build 19045 · WDDM 2.7 declarado
> **Repositório:** `C:\Users\Daniel\Desktop\NamixWDDM`
>
> **Este documento é autocontido.** Você não participou da sessão que o gerou. Leia-o inteiro
> antes de opinar, e trate a análise da seção 7 como **a tese a ser atacada**, não como verdade.

---

## 1. O que estamos construindo

Um driver WDDM **completo** (display + render) para a AMD BC-250, escrito do zero, para rodar
jogos no Windows 10 com aceleração por GPU real — sem WARP, sem fallback de software, com o
monitor ligado ao DisplayPort da própria placa.

Não existe driver do fabricante para esta placa nesta plataforma. Tudo — bring-up de firmware,
MMU, filas, submissão, display — foi implementado a partir de documentação pública, do catálogo
de registradores derivado do upstream amdgpu, e de medição no silício.

**O display é herdado do UEFI GOP.** Não programamos o DCN (bloco de display) — escrita em
registrador de DCN é proibida no escopo atual. O scanout é o framebuffer que o firmware deixou
ligado, e nós copiamos pixels para ele via SDMA.

---

## 2. As leis que governam este repositório

Elas mudam o que conta como resposta aceitável. **Respeite-as na sua auditoria.**

| Lei | Conteúdo |
|---|---|
| **1 — Sinceridade** | Reporte o que a medição diz. Prova ausente → escreva `NÃO VALIDADO EM HARDWARE`. Hipótese não é causa. |
| **2 — Tripé no silício** | Sucesso exige as três provas: `RPTR == WPTR`; fence escrita **pela GPU** sobre padrão de poison; dados destino conferidos **byte a byte**. |
| **3 — Teste nomeado** | Todo item fecha com um teste nomeado e evidência arquivada. |
| **4 — Zero fake, zero fallback de software** | Nunca incrementar fence pela CPU. Nunca `Sleep`/delay simulando execução. Nunca `STATUS_SUCCESS` em DDI sem backend. Compilar, assinar, carregar DLL ou abrir swapchain **não é** prova de aceleração. |
| **5 — Ciclo transacional** | PREFLIGHT → SNAPSHOT → APPLY → READBACK → PROVA → ROLLBACK, falhando fechado. |
| **7 — Endereços com procedência** | Só offsets catalogados com fonte citada. |
| **8 — Vocabulário de confiança** | **P0** provado no silício · **P1** derivado do upstream/WDK · **P2** parcialmente observado · **H** hipótese · **Q** proibido. |
| **9 — Economia de corridas** | A corrida física é o recurso escasso. Responda por leitura de código e mineração de evidência antes de propor ensaio. Critério de aceitação **escrito antes** da corrida. |

**Contexto operacional:** quem executa as corridas é o dono do projeto, numa máquina física, com
watchdog que faz rollback automático. Cada corrida custa um boot frio e ~5 minutos de risco de
tela preta/BugCheck. Propostas que gastem corrida sem critério falsificável são rejeitadas.

---

## 3. Onde estamos: o mapa das duas paredes

### Está provado no silício (P0)

| Camada | Prova |
|---|---|
| Adaptador WDDM em Código 0 | sustentado, reproduzido em dezenas de corridas |
| Firmware (PSP, RLC, PFP, ME, CE, MEC, SDMA) | carregado e autenticado |
| GCVM / MMU sob VMID8 | page table de 2 níveis, `SetRootPageTable` com readback |
| SDMA — IB indireto real do `dxgkrnl` | tripé completo (RPTR==WPTR + fence sobre poison + 5 canários substituídos) |
| TDR | detecção → quiesce → reset → restart → readmissão de submissão |
| Compute Vulkan (RADV) no Node 1 / MEC | 600 quadros consecutivos |
| Present por GPU até o scanout GOP | 584 blts consecutivos, byte a byte, zero cópia de CPU — **pelo harness** |
| D3D11 — `bc250umd.dll` no `dwm.exe` | `OpenAdapter10_2 → S_OK (BC250 Native)`, `CreateDevice` OK |
| Tradução de shader | DXBC → SPIR-V via vkd3d-shader |

### As duas paredes que restam

**Parede A — apresentação.** O `dwm.exe` chama `Present`, e o `pfnPresentCb` do runtime devolve
`E_FAIL` (`0x80004005`) de forma **síncrona**, no mesmo milissegundo. O DWM então entra em laço
de device-lost (destrói tudo, recria o device) a ~1 Hz, indefinidamente. **Causa não identificada.**

**Parede B — rasterização.** Não existe pipeline gráfico: sem descriptors, sem render target
binding, sem anel gráfico, sem `Draw` PM4. O próprio log declara isso. O `Draw` é honestamente
recusado, ou (com um tunable armado) substituído por cópia 1:1 no SDMA — que **não é**
rasterização e está rotulada como tal.

**Consequência:** mesmo que a Parede A caia, a tela continua preta, porque ninguém produz pixel.
Isso é esperado e correto. **Tela preta é o resultado certo nas corridas atuais.**

---

## 4. O que foi feito nesta sessão

O ponto de partida era um handoff de outra IA que afirmava: *"o DWM compôs milhares de quadros no
backbuffer offscreen; falta transferi-los para o scanout do GOP"*.

### 4.1 Auditoria que refutou o handoff (offline)

Mineração da corrida `16:28:27` (pacote 3.170.1.0), evidência em
`docs/evidence/2026-09-13_16-28-27-*/`:

| Achado | Medição |
|---|---|
| `Bc250DxgkDdiPresent` nunca foi chamada | **0** linhas `[Present #N]` em 5,4 MB de log |
| Não existe quadro composto | **1787/1787** `COMPOSITE_BLT` com `Prova=ZERO_INCONCLUSIVO` e a linha literal `zero contra zero NAO prova pixel` |
| Nenhum destino foi o scanout | 7 `DstMc` distintos, todos backbuffers; scanout é `0xF400000000` |
| O DWM estava em laço de device-lost | 90 ciclos `Present → DestroyDevice → CreateDevice` |
| O Present pedido era **Flip**, não Blt | `Flags=0x00000002` (`dxgiddi.h:71`) |

**Conclusão:** o caminho de scanout era **código morto** naquela corrida, e não havia pixel para
transferir. O handoff propunha consertar código que nunca executou.

### 4.2 Faixa 1 — vínculo de recurso na alocação

**Defeito encontrado por leitura de código.** A UMD e o KMD discordavam do contrato próprio:

- `Bc250WinsysAllocate` fazia `memset(&args,0,…)` em `D3DDDICB_ALLOCATE` e **nunca** atribuía
  `hResource` (`d3dumddi.h:3457`), `D3DDDI_ALLOCATIONINFO2.Flags.Primary` nem `VidPnSourceId`
  (`d3dukmdt.h:554` e `:549`). A primariedade existia só num token privado nosso.
- O `dxgkrnl` só liga `DXGKARG_CREATEALLOCATION.Flags.Resource` quando a UMD passa `hResource`.
- O ramo do KMD que marca primária ([`bc250_active_miniport.c:2767`](../driver/wdk/bc250_active_miniport.c)) está atrás
  desse bit — logo era **inalcançável** para toda alocação nossa.

Confirmado no log da 16:28: todo `[CreateAllocation] ENTER` de origem UMD com `Resource=0` →
`Non-resource allocation`.

**Correção:** `Bc250WinsysAllocateForResource`
([`bc250_wddm_winsys.c:313`](../driver/umd/bc250_wddm_winsys.c)) com
`BC250_WINSYS_RESOURCE_BINDING`; `Bc250Umd10CreateResource` passa `hRTResource.handle`
([`bc250_d3d_umd.c:887`](../driver/umd/bc250_d3d_umd.c)). Fonte única de primariedade (a classe do
token manda; vínculo divergente falha fechado); primária sem `hRTResource` é **recusada antes do
callback**; desalocação simétrica por recurso.

### 4.3 Faixa 2 — instrumentação (I1, I2, I3)

Três defeitos de observabilidade que tinham custado corridas inteiras:

| | Defeito | Correção |
|---|---|---|
| **I1** | `Bc250DxgiPresent` fazia `return callback(...)` direto — o `hr` de 90 Presents nunca foi gravado | `ENTER cb` + `LEAVE cb: hr=` ([`bc250_d3d_umd.c:3406`](../driver/umd/bc250_d3d_umd.c)) |
| **I2** | `Bc250DxgkDdiPresent` tinha **3** saídas mudas (`hContext` NULL, `pPresent` NULL, e todo o corpo sob `if (FrameBufferVirtual != NULL)`) | entrada **incondicional** anterior a toda guarda ([`bc250_display_ddi.c:2711`](../driver/wdk/bc250_display_ddi.c)) + recusas nomeadas |
| **I3** | `[McdmQAI]`, `[McdmCreateDevice]` e `[McdmCreateContext]` dividiam **um** contador de amostragem; a rajada de QAI no bind esgotava o orçamento e 90 criações de device ficavam invisíveis | contadores independentes |

### 4.4 Faixa 3 — contexto de apresentação dedicado

Depois da corrida `17:18:35` (ver 5.2), a UMD passou a criar um **segundo** contexto, no **Node
0**, exclusivo para o `Present`
([`bc250_wddm_winsys.c:168`](../driver/umd/bc250_wddm_winsys.c),
[`bc250_d3d_umd.c:2302`](../driver/umd/bc250_d3d_umd.c),
[`bc250_d3d_umd.c:3361`](../driver/umd/bc250_d3d_umd.c)), mantendo o de trabalho no Node 1.

**Sem fallback:** ausência do contexto → recusa com motivo nomeado, para que "E_FAIL de novo"
signifique inequivocamente "falhou pelo Node 0".

Também: a classe 9 do amostrador (`[CreateAllocation]` + `[ActiveOpenAlloc]` +
`[McdmCreateAllocation]` + `[McdmCloseAllocation]`) foi dividida — tinha o mesmo defeito do I3.

### 4.5 Disciplina aplicada

- `driver/umd/build-umd.bat` → `EXIT=0`; `driver/wdk/build-contract.bat` → `EXIT=0`
- `tools/audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py`,
  `audit-bc250mcdm-lab-package.ps1` → todos PASS
- Dois portões de auditoria novos: `audit_primary_allocation_is_declared_to_the_kernel` e
  `audit_present_path_is_not_mute`
- **15 mutações verificadas** — cada correção foi quebrada de propósito para confirmar que algum
  teste ou portão reprova, e reprova **pelo invariante certo**

---

## 5. O que as corridas mediram

### 5.1 Corrida `16:28:27` — pacote 3.170.1.0 (antes das faixas)

Baseline. 0 `DdiPresent`, 1787/1787 provas de dado inconclusivas por zero, 90 ciclos de
device-lost, estabilidade boa.

### 5.2 Corrida `17:18:35` — pacote 3.173.1.0 (faixas 1 e 2)

**Evidência:** `docs/evidence/2026-09-13_17-18-35-*/`

| Medição | 16:28 | 17:18 |
|---|---|---|
| `CreateAllocation` de UMD | `Resource=0` | **7/7 `Resource=1`** |
| Lado da UMD | não instrumentado | `SrcPrimary=1 SrcDeclaredPrimary=1 SrcRTResource=0000000006010000` |
| `SetVidPnSourceAddress` | 1× (`0xAAC000`, GOP do boot) | **3×** — GOP, depois `0x4B5C000`/`hAlloc=0xA8`, depois `0x43BC000`/`hAlloc=0x6F5` |
| `[DdiPresent] ENTER` | 0 | **6** — primeira vez na história do projeto |
| `pfnPresentCb` | não medido | **84/84 `hr=0x80004005`** |

Os 6 Presents fecharam até o silício: origem resolvida (`SegOff=0x4B60000 → VramOff=0x55EC000`),
blt de 11.059.200 bytes, `SDMA=YES`, DMA buffer submetido e executado (`FenceId=833`).
**Todos com origem toda zerada** (`NonZero=0/256`) — nada para mostrar.

Dado que reposicionou tudo, decodificando `DXGK_CREATECONTEXTFLAGS` (`d3dkmddi.h:1518`, P1 —
bit0 `SystemContext`, bit1 `GdiContext`, bit2 `VirtualAddressing`):

| Contexto | Node | Flags | Decodificado | Apresentou? |
|---|---|---|---|---|
| `0x04` | 0 | `0x05` | SystemContext + VirtualAddressing | — |
| `0x07` | 0 | `0x06` | **GdiContext** + VirtualAddressing | **sim, os 6** |
| todos os nossos | 1 | `0x04` | VirtualAddressing apenas | **nunca** |

E o `GetNodeMetadata` da própria corrida: **Node 0 = `DXGK_ENGINE_TYPE_COPY` (6), Node 1 =
`DXGK_ENGINE_TYPE_3D` (1)**.

### 5.3 Corrida `18:03:43` — pacote 3.174.1.0 (faixa 3)

**Evidência:** `docs/evidence/2026-09-13_18-03-43-*/`

```
187/187   DXGI Present ENTER cb: ... PresentNode=0 WorkNode=1
187/187   DXGI Present LEAVE cb: hr=0x80004005
```

Apresentamos pelo Node 0 — o mesmo nó dos contextos que o `dxgkrnl` de fato usa — e **falhou
igual**.

Lado da UMD, agora medido e não inferido:

```
546×   Allocate ENTER: Token=0xA1100001... Size=11075584 hRTResource=0000000009454660 Primary=1 VidPnSourceId=0
2821×  Allocate LEAVE: hr=0x00000000
```

Regressões/diferenças em relação à 17:18, **não atribuídas**: `[DdiPresent] ENTER` caiu de 6 para
**0**, e `SetVidPnSourceAddress` de 3 para **1** (só o do boot). Nas duas corridas: zero TDR, zero
BugCheck, zero `DEVICE_REMOVED`, rollback online limpo.

Diferença de ambiente observada: em 17:18 o `dwm.exe` **morreu e reiniciou** (3 instâncias); em
18:03 uma única instância (pid 1216) sobreviveu 92 ciclos de device.

### 5.4 Caps declarados (idênticos nas três corridas)

```
PresentationCaps=0x1006C903 (NoScreenToScreenBlt=1, NoOverlapScreenBlt=1,
                             DriverSupportsCddDwmInterop=1, SupportSoftwareDeviceBitmaps=1,
                             AlignmentShift=2, MaxTextureWidthShift=3, MaxTextureHeightShift=3)
FlipCaps=0x00000002 (FlipOnVSyncMmIo=1)   MaxQueuedFlipOnVSync=1
SupportNonVGA=1  SupportSmoothRotation=0  SupportDirectFlip=0  SupportPerEngineTDR=1
SchedulingCaps: MultiEngineAware=1 PreemptionAware=1 LowIrqlPreemptCommand=1
MemoryManagementCaps: VirtualAddressingSupported=1 GpuMmuSupported=1
MaxAllocationListSlotId=32   WDDMVersion=9984 (0x2700 = WDDM 2.7)
GPUMMUCAPS: VaBits=34, Levels=2, PageTableUpdateMode=CPU_VIRTUAL
```

Forma do Present pedido pelo DWM: `Flags=0x00000002` (**Flip**), `hDstResource=NULL`,
`FlipInterval=1`. A UMD repassa `hSrcAllocation` da superfície, `hDstAllocation=0`,
`BroadcastContextCount=0`, `PrivateDriverDataSize=0`.

Forma do swapchain: **546** `CreateResource ... Primary=1` de 2560x1080, criados **um a um**, com
GPUVAs `0xF0000`, `0xB80000`, `0x1610000`, … — sem nenhum registro de cadeia entre eles.

---

## 6. O que foi eliminado como suspeito

| Suspeito | Estado | Base |
|---|---|---|
| "Falta transferir o backbuffer para o scanout" | **refutado** | zero `DdiPresent`; provas de dado todas zero-contra-zero |
| Alocação não declarada como primária ao kernel | **corrigido, e insuficiente** | `Resource=1`, 546 primárias com `hRTResource`+`Flags.Primary`, e o E_FAIL persiste |
| Nó/handle do contexto de apresentação | **eliminado** | `PresentNode=0` em 187/187, E_FAIL igual |
| "Node 1 é compute e não apresenta" | **refutado** | `GetNodeMetadata`: Node 1 = 3D, Node 0 = COPY |
| Falha na criação do contexto no Node 0 | **eliminado** | dezenas de `PRESENT_CONTEXT: Node=0 hContext=…` com sucesso |

---

## 7. Minha análise — **ataque-a**

**O que considero P0:** tudo na seção 5 que é contagem direta de linha de log.

**O que considero H (hipótese, não provada):** nada nomeia a causa do `E_FAIL`. `0x80004005` é
genérico, é devolvido **sincronamente** pelo runtime/dxgkrnl, e nenhuma das três corridas o
determinou.

**Minha suspeita atual, fraca:** a **forma da cadeia de flip**. O DWM pede `Flip`; nós declaramos
`SupportDirectFlip=0`; os buffers do swapchain são criados um a um sem qualquer vínculo entre si;
`hDstAllocation=0`. É possível que o runtime valide a cadeia de flip antes de despachar e recuse
por não reconhecê-la. **Não tenho evidência disso** — é o próximo lugar a olhar, não uma conclusão.

**O que me incomoda e não sei explicar:** por que os 6 `DdiPresent` da 17:18 não reapareceram na
18:03. O caminho que os produziu é o do contexto **GDI**, que não passa pela nossa UMD, e
correlaciona com o DWM ter morrido numa corrida e não na outra — mas isso é correlação, não
mecanismo.

---

## 8. Meus erros nesta sessão — leia, isso calibra você

Estou listando porque o mesmo tipo de erro pode estar em algo que eu **ainda não percebi**.

1. **Superestimei a corrida 17:18.** Escrevi que o `SetVidPnSourceAddress` tinha ido para "uma
   alocação criada pela UMD (`hAlloc=0xA8`)". Era **inferência, não medição** — a linha
   `Primary token confirmed (UMD` nunca imprimiu. **Retirado.**

2. **Previ o suspeito errado.** Escrevi que o próximo suspeito seria "o nó do contexto (COMPUTE,
   node 1)". O `GetNodeMetadata` da própria corrida refuta: Node 1 é **3D**, Node 0 é **COPY**.

3. **Meu I3 foi incompleto duas vezes.** Corrigi a classe 12 do amostrador e deixei a classe 9 com
   o mesmo defeito — e o buraco caiu exatamente sobre a medição que eu queria. Depois "completei"
   dividindo a classe 9, e **ainda não resolveu**: são **dois portões em série** (contador próprio
   no sítio + classe do amostrador), e o orçamento é por **linha** — 24 linhas de
   `[CreateAllocation]` ≈ 8 chamadas, todas gastas pelo bind antes das 546 primárias da UMD
   chegarem. `Primary token confirmed (UMD` continua em zero **e continua não-conclusivo**.

4. **Errei a colocação de um teste.** Criei um objeto em `TestLifecycle` e o destruí em
   `TestResidencyAndPrimaryClass` — funções diferentes. A suíte pegou.

5. **Escrevi um portão fraco.** Uma mutação reprovou pelo `assert` errado (allowlist de um stub
   disparando antes do meu invariante). Falha não-atribuível é portão fraco; corrigi o stub.

**Padrão histórico deste repositório, que se repetiu 5 vezes:** *o gate congelava a mentira* — um
teste ou auditoria **exigia** o comportamento defeituoso, impedindo a correção. Já aconteceu com a
comparação crua de RPTR, com `STATUS_SUCCESS` sem executor de TDR, com um `memcpy` de CPU no
`Draw`, e com um contador cego a cópias por CPU. **Procure por isso.**

---

## 9. Perguntas abertas — o que quero de você

Responda o que puder **por leitura de código e de evidência**. Cada corrida física custa caro
(Lei 9), então proposta de ensaio precisa de critério falsificável escrito antes.

### Q1 — Por que `pfnPresentCb` devolve `E_FAIL`? *(a pergunta central)*
Sincronamente, no mesmo milissegundo, com: `Flags.Flip=1`, `hDstAllocation=0`,
`hSrcAllocation` de uma alocação primária aceita pelo kernel, contexto virtual no Node 0,
`SupportDirectFlip=0`, `FlipCaps.FlipOnVSyncMmIo=1`, `MaxQueuedFlipOnVSync=1`,
`PresentationCaps.NoScreenToScreenBlt=1`.
**Existe alguma pré-condição documentada do WDDM que estamos violando?** Cite a fonte.

### Q2 — A cadeia de flip precisa ser declarada?
546 primárias criadas uma a uma, sem vínculo. Um swapchain de flip exige que os buffers sejam
registrados como cadeia (via `hResource` comum? `pPrimaryDesc`? `D3DDDICB_CREATEALLOCATION` com
múltiplas alocações num único recurso?). **O que o contrato exige de fato?**

### Q3 — `SupportDirectFlip=0` é compatível com o DWM pedir `Flip`?
Está declarado como dívida no repositório ("não temos backend; exigiria escrita no DCN, proibida").
**Declarar 0 e ainda assim receber `Flags.Flip=1` é esperado?** O runtime deveria converter para
Blt antes de nos alcançar? (Na 17:18 o KMD recebeu `Flags=0x00000001` = Blt nos 6 que passaram —
então **alguma** conversão existe.)

### Q4 — O que `DXGK_ENGINE_TYPE_COPY` no Node 0 implica?
Node 0 é o nó de paginação (`PagingNodeIndex=0`) **e** declarado COPY. Node 1 é 3D.
**Um contexto de UMD pode apresentar a partir de um nó COPY?** E de um nó 3D?
Qual nó o WDDM espera para o Present de um device D3D11?

### Q5 — Há algo errado na forma do `DXGIDDICB_PRESENT` que montamos?
Ver `Bc250DxgiPresent` em [`driver/umd/bc250_d3d_umd.c:3340`](../driver/umd/bc250_d3d_umd.c).
Campos não preenchidos: `BroadcastContextCount`, `BroadcastSrcAllocation`,
`BroadcastDstAllocation`, `PrivateDriverDataSize`, `pPrivateDriverData`,
`bOptimizeForComposition`, `SyncIntervalOverrideValid`. **Algum é obrigatório?**

### Q6 — A alocação primária está completa?
Ver `Bc250WinsysAllocateForResource` em
[`driver/umd/bc250_wddm_winsys.c:313`](../driver/umd/bc250_wddm_winsys.c) e o lado do KMD em
[`driver/wdk/bc250_active_miniport.c:2626`](../driver/wdk/bc250_active_miniport.c).
Passamos `hResource`, `Flags.Primary`, `VidPnSourceId`. **Falta algo?** O KMD deveria preencher
mais campos de `DXGK_ALLOCATIONINFO` para uma primária (`Flags.Primary`? `Flags.Overlay`?
`PreferredSegment`? `HintedBank`? `MaximumRenamingListLength`?).

### Q7 — Procure o padrão "o gate congela a mentira"
Ver seção 8. **Algum teste ou portão de auditoria no repositório está exigindo um comportamento
defeituoso?** Alvos prováveis: `driver/tests/`, `tools/audit-bc250mcdm-safety.py`.

### Q8 — O que a minha análise deixou passar?
Esta é a pergunta mais valiosa. **Desconfie das seções 6 e 7.** Se algum suspeito que eu declarei
"eliminado" não estiver realmente eliminado pela evidência que citei, diga.

### Q9 — Estamos no caminho certo?
Dado que a Parede B (rasterização) existe de qualquer forma, **vale insistir na Parede A agora**,
ou seria mais racional atacar a rasterização primeiro e só depois a apresentação? Argumente.

---

## 10. Como verificar por conta própria

```
Código do Present (UMD):     driver/umd/bc250_d3d_umd.c        ~linha 3340
Winsys / alocação:           driver/umd/bc250_wddm_winsys.c    ~linhas 168, 313
Present (KMD):               driver/wdk/bc250_display_ddi.c    ~linha 2690
CreateAllocation (KMD):      driver/wdk/bc250_active_miniport.c ~linha 2626
Caps / QueryAdapterInfo:     driver/wdk/bc250_mcdm_callbacks.c ~linha 1878
Amostragem do log:           driver/wdk/bc250_display_ddi.c    ~linhas 570-720
Portões de auditoria:        tools/audit-bc250mcdm-safety.py
Normas do projeto:           CLAUDE.md / AGENTS.md / rules.md
Estado e prioridades:        BASE_DE_AVANCO.md
```

**Evidência das três corridas:**
`docs/evidence/2026-09-13_16-28-27-*/`, `…_17-18-35-*/`, `…_18-03-43-*/`
— `display-log-this-bind-FINAL.txt` é o log do KMD;
`bc250_umd_log_<pid>-FINAL.txt` é o da UMD por processo.

**Histórico desta linha de investigação:** `docs/265`, `docs/266`, `docs/267`.

---

## 11. Formato de resposta que me é útil

1. **Achados**, cada um classificado **P0 / P1 / P2 / H** conforme a Lei 8, com a fonte citada
   (arquivo:linha, ou documento do WDK, ou linha de log).
2. **Erros meus** que você encontrou — especialmente afirmação minha apresentada como medição que
   na verdade é inferência.
3. **Propostas de correção**, cada uma com: o teste offline que a comprova, e o critério
   falsificável da corrida que a validaria no hardware.
4. **O que você NÃO conseguiu determinar.** Diga explicitamente. Chute rotulado como conclusão é
   pior que silêncio neste projeto.

> Não proponha WARP, GDI, llvmpipe, emulação por CPU, nem "desabilite X para funcionar".
> Não trate compilar, assinar, carregar DLL ou abrir swapchain como prova de aceleração.
> Se a resposta honesta for "não sei", escreva isso.
