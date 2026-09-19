# 203 — Dossiê para auditoria externa · sessão de 2026-09-08

> **Propósito:** este documento existe para ser auditado por terceiro. Ele descreve
> o que foi afirmado, qual medição sustenta cada afirmação, o que continua sem
> prova, e o que vem a seguir.
> **Leitor esperado:** alguém sem contexto prévio do projeto.
> **Pedido explícito ao auditor:** procurar afirmação sem evidência, teste que
> não testa, e portão que não pega o defeito que diz pegar.

---

## 0. Contexto mínimo

**Projeto:** driver WDDM para AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)
em **Windows 10 Pro 22H2 build 19045**. Objetivo final: rodar jogos via D3D12/Vulkan
com o monitor no DisplayPort da própria placa, sem WARP e sem fallback de software.

**Regras que o projeto se impõe** (relevantes para a auditoria):

- **Lei 1 — sinceridade:** sem prova, escreve-se literalmente `NÃO VALIDADO EM HARDWARE`.
- **Lei 2 — tripé no silício:** aceleração só se declara com (a) `RPTR == WPTR`,
  (b) fence gravada **pela GPU** sobre poison, (c) dados conferidos byte a byte.
- **Lei 3 — todo item fecha com um teste nomeado.**
- **Lei 4 — zero fakes:** nunca `STATUS_SUCCESS` em DDI sem backend real; nunca
  fence incrementada por CPU; nunca tratar compilação/enumeração como prova.
- **Lei 8 — vocabulário de confiança:** `P0` provado no silício · `P1` derivado do
  upstream · `P2` parcialmente observado · `H` hipótese · `Q` proibido.
- **Lei 9 — economia de corridas:** a corrida física é o recurso escasso; o dono
  do projeto é quem executa. O agente compila, analisa e prepara.

**Duas imagens diferentes do mesmo driver:**

| imagem | macro | caminho no `StartDevice` |
|---|---|---|
| produção | *sem* `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP` | herda o scanout do GOP e retorna |
| laboratório | *com* a macro | executa bring-up físico (PSP, SDMA, MMU, CP, HQD) |

Boa parte dos defeitos desta sessão vem exatamente dessa bifurcação.

---

## 1. Ponto de partida da sessão

O adaptador chegava a **Código 43** (falha do PnP) no perfil de laboratório, e a
tela ficava preta. Havia duas explicações circulando no repositório, ambas
registradas na barra de status:

1. "o VidPn do perfil lab **não converge**";
2. "a parede é o backend D3D10/11 da `bc250umd.dll`".

**As duas estavam parcialmente erradas.** A auditoria pode conferir isso pelos
logs citados adiante.

---

## 2. As seis corridas do dia, com números medidos

Evidência em `docs/evidence/<timestamp>-lab-binding/`, contendo
`bc250_display_log-full.txt` (cumulativo, com `display-log-baseline.json`
indicando o offset do bind) e `probe-registry-at-bind.json`.

| hora | pacote / SYS | resultado |
|---|---|---|
| 09:50 | anterior | Código 0; 98 `IsSupportedVidPn`, 0 `CommitVidPn` |
| 11:48 | 3.90.1.0 | **Código 43**, `StartDevice = 0xC00000BB` |
| 12:36 | 3.92.1.0 `A1973547` | **Código 43**, `RuntimeCapsStatus = 0xC0000182` |
| 13:00 | 3.92.1.0 `7B34393F` | **Código 0**, segmento validado, tela preta |
| 14:06 | 3.93.1.0 `EF91F17F` | Código 0, instrumentação respondeu 2 perguntas |
| 14:26 | 3.94.1.0 `8CA949DC` | Código 0, **`CommitVidPn` pela 1ª vez**, tela preta |

*(Nota de registro que o auditor deve conhecer: `3.92.1.0` designa **dois**
artefatos distintos — SYS `A1973547` e `7B34393F` — porque o INF fonte não foi
incrementado antes do segundo empacotamento. Desambiguável pelo hash, que o
binder imprime e o manifesto grava. Corrigido a partir de 3.93.)*

---

## 3. Defeitos encontrados e corrigidos

### 3.1 Ordem do segundo preflight (corrida 11:48)

**Sintoma:** `StartDevice` devolveu `0xC00000BB` sem uma linha de bring-up físico.

**Causa:** um bloco no `StartDevice` mapeia `Core.MmioTransport` para programar a
MMU global e seguia com ele **mapeado** até o segundo preflight readonly.
`Bc250DxgkDdiStartDeviceReadonly` recusa transporte não-vazio com
`STATUS_INVALID_PARAMETER` na **guarda de entrada** — antes de escrever `LastStage`.

**Como isso foi provado (P1, não hipótese):** `SecondPreflightStage = 3`
(`RUNTIME_MISSING`) é **resíduo do primeiro preflight**. Qualquer caminho que
passe da guarda escreve `INTERFACE_ACCEPTED (1)` no mínimo. Logo o 3 prova que a
segunda chamada saiu antes disso. Contraste: no bind 09:50, mesmo perfil, o valor
era 8 e o gate 1.

**Segundo defeito, que escondia o primeiro:** com o gate fechado, todo o bring-up
foi pulado, `persistentStatus` ficou no **inicializador** (`STATUS_NOT_SUPPORTED`),
e o rodapé do `StartDevice` publicou esse inicializador como veredito porque
`PersistentSubmission=1` e `persistentReady=FALSE`. **O `0xC00000BB` não foi
produzido por medição nenhuma.**

**Correções** (`driver/wdk/bc250_active_miniport.c`):
- transporte volta a vazio antes do segundo preflight, só no caminho de
  laboratório, com falha fechada se o unmap não fechar;
- probe novo `BC250_PROBE_SECOND_PREFLIGHT_STATUS` com o `NTSTATUS` **cru** — o
  estágio sozinho é ambíguo **por construção**;
- `persistentAttempted`: o rodapé só troca o status pelo do ensaio de submissão
  persistente se esse ensaio **tiver acontecido**.

**Travas:** teste em `driver/tests/dxgk_readonly_ddi_test.c` (runtime staged +
transporte mapeado → `INVALID_PARAMETER`, `LastStage` intocado; e o contraponto
com transporte vazio → estágio 8, gate 1). Gate
`audit_second_preflight_transport_empty`.

**Validado em hardware:** SIM, corrida 12:36 — `stage=8`, `gate=1`,
`V5StartDeviceStatus=0`.

---

### 3.2 Segmento de VRAM no ramo *sealed* (corrida 12:36)

**Sintoma:** Código 43 com `RuntimeCapsStatus = 0xC0000182`.

**Por que só apareceu agora:** o ramo *sealed* de `Bc250ActiveQueryRuntimeCaps`
exige `SecondPreflightStage == PREFLIGHT_READY`. Até a correção 3.1 destravar o
segundo preflight, **esse ramo nunca havia executado num bind real**. O defeito é
**anterior** à correção; ela apenas o tornou alcançável.

**A conta, com os números medidos:**

```
janela de CPU ........... 268.435.456
localEnd (firmware) ..... 268.161.024
reservas:
  scanout .............. offset           0, len  11.059.200
  dummy page da MMU .... offset 268.431.360, len       4.096
  carveout opaco ....... offset 268.435.456, len 268.435.456  (pulado)
                          soma = 279.498.752 = RuntimeInventoryReservedLow32 ✔
```

A dummy page é alocada `TOP_DOWN | CPU_VISIBLE` (`DummyPA=0x45FFFF000` menos base
`0x450000000` = 268.431.360) e termina no topo da janela. O laço tratava **toda**
reserva como *prefixo a pular*, então `localStart = 268.435.456 ≥ localEnd` e o
segmento colapsava.

Agravante: a dummy **já era tratada corretamente** ~60 linhas abaixo, truncando
`localEnd`. Era contada duas vezes, e prevalecia a contabilidade errada.

**Correção:** prefixo empurra o início; reserva alta trunca o fim; o passe de
prefixo repete até estabilizar porque a lista não é ordenada.

**Trava:** `Bc250RunSealedSegmentReservationTest` em
`driver/tests/active_miniport_test.c`, com os números da corrida **sem
arredondar**, em três casos — incluindo um negativo, para a asserção não virar
carimbo.

**Validado em hardware:** SIM, corrida 13:00 — `RuntimeCapsBaseLow32 = 11.059.200`
(`0xA8C000`), `RuntimeCapsLocalLow32 = 257.101.824` (`0xF531000`), previstos
**antes** da corrida; mais `QUERYSEGMENT4: VRAM=0xF531000` e
`SetRootPageTable readback OK PT_BASE=450a8c000` sob VMID8.

---

### 3.3 ABI de contexto publicada zerada (corrida 14:06)

**Como foi encontrado:** instrumentação adicionada porque o log só tinha handles
opacos. `DXGKARG_CREATEPROCESS` carrega `pProcessName` e `Flags` desde WDDM 2.4
(`d3dkmddi.h`) e o driver declara 2.7 — o nome vem do **contrato**, sem API não
documentada.

**Respostas obtidas:**

1. **quem morre:** `pProcessName` veio NULL (permitido), mas as flags disseram —
   `System=0, Gdi=1`. É o cliente **GDI**, não o `dwm.exe`.
2. **alguma DDI nossa recusou?** Não. **Zero `RECUSADO`** na corrida inteira.

**O achado:** o `OK` estava mentindo.

```
[McdmCreateContext] OK: Node=0, hContext=0x..., DmaBuf=0x0, SegSet=0x0
```

Oito vezes. Contexto com `DmaBufferSize=0` é inútil — o dxgkrnl não tem como
alocar DMA buffer.

**Causa:** `Bc250ActiveCreateContextObject` faz `*ContextInfo = context->ContextAbi`,
**sobrescrevendo** os defaults que `Bc250McdmCreateContext` acabara de escrever. E
`ContextAbi` só era populado dentro do bloco
`#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)` — **somente na imagem de
produção**. Na de laboratório, `ContextAbiReady` ia a 1 com a estrutura zerada.

**Terceira ocorrência da mesma classe:**

| faixa | campo | ramo em que existia |
|---|---|---|
| A13 | tunable de modo de submissão | só no `#else` |
| B4 | `GpuvaLabPromotion` | só no `#else` |
| agora | `ContextAbi` | só no `#if !defined` |

**Correção:** fonte única `Bc250ActiveInitializeContextAbi`, chamada nos **três**
sítios antes de declarar `ContextAbiReady`.

**Validado em hardware:** SIM, corrida 14:26 — ver seção 4.

---

## 4. O resultado da corrida 14:26 (o mais significativo da sessão)

O critério de aceitação foi escrito **antes** da corrida. Bateu inteiro:

| | 14:06 | **14:26** |
|---|---|---|
| `DmaBuf` / `SegSet` | `0x0` / `0x0` | **`0x10000` / `0x2`** |
| `CreateProcess` / `DestroyProcess` | 8 / 7 | **2 / 0** |
| convergências / reinícios do VidPn | 39 / 46 | **8 / 8** |
| `CommitVidPn` | 0 | **1** |

E, pela primeira vez no perfil de laboratório:

```
=== SetVidPnSourceAddress (SourceId=0, Segment=1, Addr=0xAAC000, Flags=0x1) ===
=== SetVidPnSourceVisibility (Visible=1) ===
60 × GetStdAllocData · 20 × CreateAllocation · 30 × OpenAllocation  (todos status 0)
367 × ControlInterrupt VSYNC
```

Sem `StopDevice`, sem BugCheck, estável até o rollback do watchdog.

**Estado novo:** o `dwm.exe` agora **roda** e fail-fasta com `0xe0464645` em
`KERNELBASE.dll+0x12d862`, oito vezes — **a mesma assinatura da corrida de
produção**. Os dois perfis convergiram: resta **uma** parede para o desktop.

---

## 5. Enquadramentos que a medição derrubou — inclusive meus

O auditor deve conferir estes especialmente, porque são autocorreções:

1. **"o VidPn do perfil lab não converge"** — FALSO. Ele converge 39 vezes na
   corrida 13:00, com `Scale=1/Rot=1`, os dois modos fixados e
   `TargetMode matches GOP -> Supported=TRUE!`, o mesmo estado que a produção
   comita. Era um **laço**, não uma não-convergência.
2. **"o `E_NOTIMPL` da nossa UMD mata o processo"** — REFUTADO na corrida 13:00:
   **nenhum** `BC250_UMD_LOG_*.txt` foi criado, isto é, `bc250umd.dll` nunca
   carregou, apesar de estar instalada em `System32`. Na corrida de **produção**
   de 08:44 a mesma DLL carregou em `dwm.exe` e recusou `OpenAdapter10/10_2`, com
   cinco logs. Eram **duas paredes distintas**.
3. **"`TelemetrySchemaVersion` prova o rebuild"** — FALSO. É constante fixa do
   empacotador gravada no INF; não deriva do enum e nunca se moveria sozinha. O
   verificador válido é `SourceSysSha256` + strings no binário assinado.
4. **`0xF52A000`** — hex errado num aviso pré-corrida. O valor decimal previsto
   (257.101.824) estava certo e foi o que apareceu (`0xF531000`).

---

## 6. Portões e testes criados — e os que reprovaram por mutação

Toda trava desta sessão foi verificada **por mutação**: introduz-se o defeito de
volta e confere-se que a suíte/auditoria reprova.

| # | Mutação | Resultado |
|---|---|---|
| 1 | remove o unmap antes do preflight | **PASSOU** → gate meu era defeituoso (ver abaixo) |
| 2 | rodapé volta a mascarar o status | FAIL, 2 erros |
| 3 | teste perde a prova de `RuntimeStaged` | FAIL |
| 4 | remove a guarda do transporte no readonly | `TESTE FALHOU (dxgk_readonly_ddi_test)` |
| 5 | remove a guarda de prefixo no segmento | `TESTE FALHOU (active_miniport_test)` |
| 6 | uma das três recusas de `CreateContext` volta a ser muda | **PASSOU** → gate meu era defeituoso |
| 7 | cópia do nome perde o `volatile` | FAIL |
| 8 | remove o helper de um dos três sítios de `ContextAbi` | FAIL |
| 9 | escreve `DmaBufferSize` fora do helper | FAIL |

**Três portões meus foram reprovados pelas próprias mutações e refeitos:**

- o gate do segundo preflight buscava `preflightStatus = Bc250DxgkDdiStartDeviceReadonly(`
  desde o início do arquivo e casava com o **primeiro** preflight; a janela saía
  invertida e o gate retornava sem examinar nada — **falso negativo silencioso**;
- o gate de recusa muda exigia apenas a **presença** da string `RECUSADO:`;
  com três recusas na mesma DDI, remover uma ainda passava. Refeito com
  invariante por-`return`;
- o gate da ABI de contexto usava janela de 12 linhas e reprovava o sítio de
  produção legítimo, que tem 19 linhas de comentário. Refeito **sem janela**.

Também: o gate do B3 pegou um comentário **meu** que citava o nome banido de um
tunable antigo — portão funcionando.

**Estado de build ao final:**

```
driver/wdk/build-contract.bat          EXIT=0
tools/audit-bc250mcdm-safety.py        PASS
tools/audit-mcdm-ddi-matrix.py         PASS (STRUCTURAL ONLY)
tools/audit-ddi-noop-callbacks.py      98 anunciadas, 0 falhas
```

---

## 7. Afirmações e confiança — o que o auditor deve questionar

| afirmação | confiança | base |
|---|---|---|
| Ordem do segundo preflight corrigida | **P0** | probes 12:36: stage 8, gate 1, status 0 |
| Segmento sealed correto | **P0** | 13:00: dois valores previstos antes, batidos exatos |
| Contexto saía com `DmaBuf=0` | **P0** | 14:06: oito linhas de log |
| Corrigi-lo destravou o `CommitVidPn` | **P0** | 14:26: critério escrito antes, batido inteiro |
| Quem repetia era o cliente GDI | **P0** | flags `Gdi=1/System=0` no log |
| A UMD não carregou nas corridas de lab | **P2** | ausência de arquivos de log; compatível com terceiro apagando |
| DWM fail-fasta por falta de D3D11 | **P2** | assinatura `e0464645` idêntica à de produção; nesta corrida **não** houve log de UMD, então se o `OpenAdapter` foi consultado antes do fail-fast é **indeterminado** |
| Subcódigo `0x193 / 804` do LiveKernelEvent | **não decodificado** | sem fonte no repositório; deliberadamente não inferido |

**Dívidas declaradas nesta sessão:**

- ~~o handoff da corrida 14:26 e a atualização da barra de status não foram
  escritos~~ — **quitado**: handoff em `docs/204_HANDOFF_2026-09-08_14-26_COMMITVIDPN_E_A_PAREDE_UNICA.md`,
  e a barra de status foi atualizada nos quatro arquivos normativos, incluindo a
  linha do miniport (75% → 78%, com a justificativa "duas paredes viraram uma");
- `3.92.1.0` designa dois artefatos;
- `bc250umd.dll` em `System32` está datada de 11:24 — o conteúdo é idêntico em
  todos os pacotes (`PackageUmdSha256 = 2927FCBC...`), mas vale conferir.

---

## 8. O que falta

| Prioridade | Item | Estado | Custo |
|---|---|---|---|
| **1** | **Backend D3D10/11 real na `bc250umd.dll`** | Única parede do desktop. `Bc250Umd10CreateDevice` devolve `E_NOTIMPL` porque `BC250_D3D10_11_NATIVE_READY = 0` — fail-closed deliberado pela Lei 4 | **Grande**: construção, não diagnóstico |
| 2 | **Tripé físico do Node 1 (B7)** | Runner, token e gates prontos; estava bloqueado pela falta de `CommitVidPn`, que **agora existe** | 1 corrida |
| 3 | TDR sobrevivendo com display vivo (item 25) | Detecção, reset, restart e readmissão **já validados**; falta a perna "a tela sobrevive" | depende de #1 |
| 4 | Jogos com Vulkan | Depende de #1 e #2 | — |

---

## 9. Próximas etapas, em ordem

1. ~~Escrever o handoff da 14:26 e atualizar a barra de status.~~ **Feito** —
   `docs/204`, e os quatro arquivos normativos atualizados.
2. **Corrida do Node 1 (B7).** Barata e desbloqueada: o pacote **3.94.1.0** já está
   em disco e é o mesmo que produziu o `CommitVidPn`. Boot frio e o mesmo bind
   **com** `-RunUmdNode1Tripod`. O Node 1 está **sem qualquer medição** desde o
   início do projeto; qualquer resultado é informação nova.
3. **Fase própria para o backend D3D10/11.** Não é uma corrida — é implementar o
   device D3D10/11 na UMD com pipeline real. Merece escopo, marcos e critério de
   aceitação escritos antes de começar.
4. **Defesa estrutural contra a classe de defeito recorrente.** Três vezes nesta
   sessão e nas anteriores um campo existia em **um só ramo do `#if`**. Os portões
   atuais cobrem os três casos individualmente. Vale um gate genérico que detecte
   campos de `BC250_DXGK_ACTIVE_CONTEXT` escritos em apenas um dos ramos.

---

## 10. Como o auditor pode verificar por conta própria

```bash
cd driver/wdk && ./build-contract.bat          # espera EXIT=0
python tools/audit-bc250mcdm-safety.py         # espera PASS
python tools/audit-mcdm-ddi-matrix.py
python tools/audit-ddi-noop-callbacks.py
```

Logs por corrida (o arquivo é cumulativo; usar o offset):

```python
import json, io
d = 'docs/evidence/2026-09-08_14-26-09-DESKTOP-HQAG6T4-lab-binding'
b = json.load(io.open(d + '/display-log-baseline.json', encoding='utf-8-sig'))
t = open(d + '/bc250_display_log-full.txt', 'rb').read()[b['start_offset']:]
print(t.decode('utf-8', errors='replace'))
```

Probes: `probe-registry-at-bind.json` na mesma pasta.

**Sugestão de foco para a auditoria:** os itens marcados **P2** na seção 7, os dois
portões que já reprovaram por mutação (seção 6), e se algum teste desta sessão
passa por motivo diferente do que afirma testar.

**Node 1: NÃO VALIDADO EM HARDWARE. Jogos: NÃO VALIDADOS EM HARDWARE.**
