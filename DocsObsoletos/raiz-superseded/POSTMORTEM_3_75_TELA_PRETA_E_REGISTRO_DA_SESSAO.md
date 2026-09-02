# Post-mortem 3.75 — tela preta no bind KMDOD, e registro integral da sessao

**Data:** 2026-09-01
**Candidato:** 3.75.0.0, schema 132, `PackageSysSha256 E9949CAA...`
**Resultado do ensaio:** tela preta; dispositivo em **Codigo 39** (`CM_PROB_DRIVER_FAILED_LOAD`)
**Veredito:** **NAO VALIDADO EM HARDWARE**

---

## 1. Estado atual e recuperacao

| | |
|---|---|
| Dispositivo | `BC-250 MCDM Fail-Closed Development Adapter`, service `bc250mcdm` |
| `ConfigManagerErrorCode` | **39** — driver falhou ao carregar |
| Ultimo boot | 2026-09-01 18:51:42 |
| Binding | `2026-09-01_18-40-20-...-lab-binding`, `oem20.inf` |
| Watchdog | **ARMADO** (`watchdog_token e8b66fc2...`, `watchdog_disabled: false`) |
| `C:\BC250_TEST_OEMINF.txt` | PRESENTE — marcador de binding ainda no lugar |

**Acao pendente do operador:** `C:\BC250_RECOVERY.cmd`, para restaurar `BasicDisplay / Codigo 0` antes de qualquer novo ensaio.

---

## 2. O que a telemetria mostra

O `.sys` gravou schema 132 no registro, entao esta evidencia e' do candidato 3.75.

### 2.1 O registro e o perfil funcionaram exatamente como projetados

| Campo | Valor | Leitura |
|---|---|---|
| `DriverEntryReached` | 1 | DriverEntry executou |
| `V5ProfileResolveStatus` | 0 | perfil resolvido sem erro |
| `V5AdapterProfile` | 0 | `DISPLAY_ONLY` — correto para `RenderNode=0` |
| `V5RenderOnlyLabTunablePresent` | 1 | o tunable novo foi lido do registro |
| `V5RegistrationMode` | 0 | caminho KMDOD — correto |
| `InitializationDataReady` | 1 | tabela construida |
| `DxgkInitializeStatus` | **0** | **`DxgkInitializeDisplayOnlyDriver` ACEITOU a tabela** |
| `AddDeviceStatus` | 0 | AddDevice bem-sucedido |
| `V5StartDeviceStatus` | **0** | **StartDevice devolveu SUCESSO** |
| `V5StartDeviceSources` / `Children` | 1 / 1 | correto para `DISPLAY_ONLY` |
| `V5DisplaySurfaceAnnounced` | 1 | correto |
| `V5ChildEnumerationEnabled` | 1 | correto |
| `PostDisplayStatus` | 0 | post-display ownership adquirido |
| `DmmDisplayMapped` | 1 | `Bc250DisplayInit` executou |

**Isto refuta a primeira hipotese que levantei.** Eu suspeitei que remover `DxgkDdiSetPalette` do perfil KMDOD tivesse feito o `dxgkrnl` rejeitar a tabela. Nao fez: `DxgkInitializeStatus = 0`.

### 2.2 O bring-up de hardware passou por completo

Todos os marcos fisicos ficaram prontos nesta corrida:

| Bloco | Evidencia |
|---|---|
| PFP direct load | `PfpDirectPhysicalReady=1`, `PfpDirectCpSoftReset=1`, `PfpDirectStagedDwords=65792` |
| Shader | `ShaderPhysicalReady=1`, `ShaderTriadPassed=1` |
| P1 (anel persistente) | `P1EngineState=1`, `P1SelfTestPhysicalReady=1`, `P1SelfTestTriadPassed=1`, 17 submissoes aceitas e retiradas |
| P2 (streaming) | `P2PhysicalReady=1`, `P2TriadPassed=1`, 16 aceitas / 16 retiradas |
| HQD | `HqdActivePhysicalReady=1`, `HqdActiveTriadPassed=1` |
| SDMA | `SdmaRingPhysicalReady=1`, `SdmaF32PhysicalReady=1` |
| GCVM/TLB | `GcvmVmid8TlbPassed=1`, `TlbAckObserved=1` |

`P1LastRptrMmio = P1LastWptrPublished = P1RingWptr = 204` — RPTR==WPTR. Poison `0x43DAFFFE` presente em `P1SelfTestResultBefore`, `ShaderResultBefore` e `SdmaRingFencePoison`.

**Ressalva obrigatoria sobre esta evidencia:** `P1*` e `P2*` estao dentro do intervalo que `Bc250ProbeResetSubmissionEvidence` zera a cada DriverEntry, portanto sao desta corrida. `ShaderPhysicalReady` (enum 754) e `HqdActive*` (444) estao **fora** desse intervalo e podem ter sobrevivido de um boot anterior — e' a lacuna de reset que eu ja' havia documentado. Os blocos de shader e HQD nao podem ser creditados a esta corrida sem confirmacao.

### 2.3 Onde a corrida morreu

| Campo | Valor | Leitura |
|---|---|---|
| `V5PostStartStage` | **0** | **nada aconteceu depois de StartDevice retornar** |
| `V5LastDdi` / `PrevDdi` / `Prev2Ddi` | 0 / 0 / 0 | nenhuma DDI marcada apos o start |
| `DmmCommitStatus` | 0 | `CommitVidPn` nunca reportou status |
| `DmmPresentStatus` | 0 | nenhum present |
| `DmmVsyncCount` | 0 | nenhum vsync |
| `StopDeviceEntered` | 0 | sem teardown limpo |
| `RemoveDeviceEntered` | 0 | idem |

Esses quatro primeiros campos estao na lista de reset de `Bc250ProbeResetRenderTrace`, entao sao confiaveis para esta corrida.

**Nao confiar em `DmmChildRelationsCalled=56`, `DmmIsSupportedCount=41`, `DmmDescriptorCalled=1`, `DmmQaiOutSize=544`.** Esses campos `Dmm*` **nao** estao em nenhuma lista de reset e carregam valores de boots anteriores — a mesma contaminacao cruzada que o comentario no `DriverEntry` documenta como incidente historico. Eles nao provam atividade nesta corrida.

### 2.4 Sintese

O driver carregou, registrou, `AddDevice` passou, `StartDevice` executou o bring-up destrutivo **completo** com o dispositivo detendo o display, devolveu `STATUS_SUCCESS` — e a partir dali o `dxgkrnl` nao levou o adaptador a lugar nenhum. Sem `CommitVidPn`, sem present, sem vsync. Tela preta.

---

## 3. Analise de causa

> **CORRECAO — 2026-09-01, apos coleta de eventos Kernel-PnP.**
>
> A secao 3.1 abaixo atribuia a tela preta ao bring-up destrutivo com
> confianca que a evidencia nao sustenta. Duas descobertas posteriores a
> derrubam:
>
> 1. O evento Kernel-PnP 219 `\Driver\bc250mcdm falhou ao carregar` aparece em
>    **17:31:29 e 17:31:31**, no boot do **3.74** — com a tabela KMDOD antiga,
>    sem perfis, sem schema 132, sem nenhuma alteracao do 3.75. O Codigo 39
>    **nao e' novidade do 3.75**, e a premissa "nao aparecia nas corridas KMDOD
>    normais" e' falsa.
> 2. O mesmo bring-up executou com imagem visivel em candidatos anteriores,
>    incluindo a baseline 3.71.
>
> **O CP soft reset e' uma hipotese, nao a causa.** Ler 3.1 como
> "mecanismo plausivel", nunca como conclusao. A causa da falha de carga
> permanece **nao diagnosticada**; o candidato 3.76 acrescenta os campos que a
> tornam diagnosticavel (`V5UnsafeBringupCompiled`, `V5UnsafeBringupEntered`,
> `V5PostDisplayAttempted`, `V5DisplayInitEntered`).
>
> Ver `HANDOFF_3_76_RENDERONLY_SAFE.md`, secao 1.

### 3.1 Uma hipotese plausivel — nao a causa

A imagem de laboratorio e' compilada com `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP`, e o INF do pacote grava `SkipHwBringup=0`. A telemetria confirma que o caminho destrutivo executou: PFP direct load com **CP soft reset**, programacao de GCVM/TLB, HQD ativo, dispatch de shader e 33 submissoes P1/P2 — tudo **durante o `StartDevice`, com o driver detendo o post-display ownership herdado do GOP**.

O proprio `tools/build-bc250mcdm-lab-bringup.ps1` imprime esse aviso:

> "A imagem resultante executa PFP direct load, CP, GCVM e HQD durante o StartDevice, com o dispositivo detendo o display. **Cenario de tela preta.**"

Um CP soft reset com o scanout do GOP vivo e' um candidato plausivel para a perda de imagem.

**Mas plausivel nao e' provado, e a evidencia posterior enfraquece a hipotese:** o mesmo bring-up executou com imagem visivel em candidatos anteriores, e o Codigo 39 ja' existia no 3.74. Ver a correcao no topo desta secao.

### 3.2 O que NAO foi a causa

Nada na telemetria aponta para as minhas alteracoes de perfil:

- a resolucao de perfil devolveu sucesso e o perfil correto;
- a tabela foi aceita pelo `dxgkrnl` mesmo sem `SetPalette` e sem `SetDisplayPrivateDriverFormat`;
- sources/children e enumeracao de children saíram corretos para `DISPLAY_ONLY`;
- `StartDevice` devolveu sucesso.

**Mas nao posso declarar minhas alteracoes provadamente inocentes.** Nao existe uma corrida de referencia do 3.74 com este mesmo bind para comparar. O que posso afirmar e': todo campo que revelaria um defeito de perfil ou de tabela leu correto, e nenhum aponta para elas.

### 3.3 Por que este ensaio nao deveria ter usado esta imagem

O Gate 1.5 existe para responder uma pergunta **sem tocar em hardware**. Eu escrevi no roadmap, com estas palavras, que o perfil render-only "nao custa tela preta nem boot frio" — e entao roteei o ensaio pela unica imagem onde essa afirmacao e' falsa.

A imagem de **producao** (`tools/build-bc250mcdm-package.ps1`) e' compilada **sem** `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP`. Nela o `StartDevice` termina depois de herdar o scanout do GOP, e o proprio codigo diz por que:

> "Production display image: StartDevice ends after inheriting the GOP scanout. Compute/GCVM/CP/HQD transactions are deliberately absent from this compiled path; a registry value must never be able to turn destructive laboratory bring-up on during PnP display ownership."

**Essa e' a imagem correta para o Gate 1.5.**

---

## 4. Meus erros

Registro-os explicitamente porque sao de processo, e processo se corrige.

### 4.1 Escolhi o veiculo errado para o experimento

O Gate 1.5 nao precisa — e nao pode — de bring-up de hardware ativo. Usei a imagem de laboratorio, que faz exatamente isso. A imagem de producao entrega a mesma pergunta com uma fracao do risco.

### 4.2 Carreguei um pre-requisito sem questiona-lo, e ele forcou o boot arriscado

`tools/prepare-bc250mcdm-rendernode.ps1` exige `ShaderPhysicalReady=1`, `P1EngineState=1`, `P1SelfTestPhysicalReady=1`, `P2PhysicalReady=1` e `HqdActivePhysicalReady=1` **do boot corrente**.

Para um alvo render-only esses requisitos nao protegem nada: o perfil nao executa trabalho de hardware algum. Eu fiz **exatamente esse raciocinio** para o inspetor — relaxei as exigencias P1/P2 no modo `-RenderOnlyLab` e documentei o porque — e **falhei em carrega-lo para o `prepare`**.

Como nao carreguei, satisfazer o preflight passou a **exigir** um boot de bring-up. E foi por isso que instrui voce a vincular com `-EnableShaderDispatch -EnablePersistentSubmission`: para produzir prontidao P1/P2 que o preflight pedia. Essa instrucao maximizou a atividade de hardware durante o ownership do display.

O raciocinio estava certo como "satisfazer o portao". Estava errado como decisao de risco.

### 4.3 Repassei o aviso em vez de trata-lo como decisao

O script de build imprimiu "Cenario de tela preta" e eu o relatei a voce como uma observacao sobre o **binario**. Era uma entrada de decisao sobre **qual binario usar**. Eu li o aviso e nao o precifiquei.

### 4.4 Mudei dois eixos ao mesmo tempo, depois de dizer para nao mudar

Eu te disse para nao mexer em dois eixos numa mesma corrida. E entao coloquei, no mesmo candidato, a funcionalidade do Gate 1.5 **e** a limpeza de falsos sucessos que alterou o caminho KMDOD (`SetPalette` removida, `CollectDbgInfo` reapontada). A telemetria indica que essas mudancas nao causaram a falha — mas se tivessem causado, a atribuicao teria sido ambigua. Isso e' uma falha de metodo independentemente do resultado.

---

## 5. Registro integral do que fiz nesta sessao

### 5.1 Analise do roadmap

Verifiquei as afirmacoes do `ROADMAP_BC250_WDDM_DISPLAY_RENDER_DCN.md` contra o codigo. Confirmei o falso sucesso de `SetDisplayPrivateDriverFormat`, a ausencia de `rendernode-inspect` do 3.74, a coerencia de `ChildUid=1` e o fail-closed do `ControlInterrupt2`.

Elevei `DxgkDdiPresent` de hipotese a **defeito estatico confirmado**: no perfil `RenderNode=1` a tabela anunciava uma VidPn source e um child DisplayPort enquanto o unico caminho de apresentacao registrado devolvia `STATUS_NOT_SUPPORTED` incondicional.

### 5.2 Driver — `driver/wdk/`

**`bc250_active_miniport.h`**
- `BC250_ADAPTER_PROFILE` (`DISPLAY_ONLY`, `DISPLAY_RENDER`, `RENDER_ONLY_LAB`)
- `Bc250ActiveSetAdapterProfile`, `Bc250ActiveGetAdapterProfile`, `Bc250ActiveResolveAdapterProfile`
- `Bc250BuildActiveInitializationDataForProfile`

**`bc250_active_miniport.c`**
- global do perfil, resolvido uma unica vez em `DriverEntry` em PASSIVE_LEVEL
- fail-closed: `RenderOnlyLab=1` sem `RenderNode=1` recusado; valores fora de `{0,1}` recusados; tunable ilegivel com valor nao-default recusado
- `StartDevice`: early-return do perfil render-only **antes** do post-display e do `Bc250DisplayInit`
- selecao da rotina de registro passou a seguir o perfil, com conferencia local redundante
- `dodInit.DxgkDdiSetPalette` removida; `dodInit.DxgkDdiCollectDbgInfo` reapontada para `Bc250McdmCollectDbgInfo`

**`bc250_display_ddi.c` / `.h`**
- `Bc250DisplayPopulateBaseOnlyCallbacks` — base sem superficie de apresentacao
- `Bc250DisplaySetChildEnumeration` / `Get`, em global proprio (fora de `BC250_DISPLAY_STATE`, que `Bc250DisplayInit` zera por completo)
- `QueryChildRelations` devolve lista vazia e `QueryChildStatus` reporta desconectado quando a enumeracao esta' desligada
- `SetDisplayPrivateDriverFormat` e `SetPalette` deixaram de ser anunciadas

**`bc250_probe_telemetry.c` / `.h`** — schema 132, seis campos: `V5AdapterProfile`, `V5RenderOnlyLabRequested`, `V5RenderOnlyLabTunablePresent`, `V5ProfileResolveStatus`, `V5DisplaySurfaceAnnounced`, `V5ChildEnumerationEnabled`. `V5RegistrationMode` ganhou o valor 2.

### 5.3 Testes — `driver/tests/active_miniport_test.c`

`Bc250RunAdapterProfileTest`: resolucao de perfil em todas as combinacoes, todos os caminhos fail-closed, e comparacao campo a campo das duas tabelas — base e render identicas, superficie de display presente numa e integralmente ausente na outra.

### 5.4 Auditorias — `tools/`

**`audit-ddi-noop-callbacks.py` (novo)** — reprova DDI anunciada que ignora todos os argumentos e devolve `STATUS_SUCCESS` sem efeito; avisa nos casos ambiguos. Ligada ao `build-contract.bat`. Encontrou `SetPalette`, `CollectDbgInfo` e o aviso sobre `NotifySurpriseRemoval` ignorar `RemovalType`.

**`audit-mcdm-ddi-matrix.py`** — `WDDM_DELIBERATELY_ABSENT`: DDIs opcionais retiradas passam a **reprovar** se voltarem a ser anunciadas sem que a justificativa saia junto.

### 5.5 Tooling de ensaio

- `prepare-bc250mcdm-rendernode.ps1`: switch `-RenderOnlyLab`, grava os dois tunables juntos com rollback conjunto
- `inspect-bc250mcdm-rendernode.ps1`: switch `-RenderOnlyLab`, expectativas por perfil, exigindo P1/P2 **zerados** no render-only
- `package-bc250mcdm-lab.ps1`: `$labSchemaVersion` unica, `DriverVer 3.75.0.0`, `RenderOnlyLab` no manifesto
- `bind-bc250mcdm-lab.ps1`: `RenderOnlyLab` na assercao de INF
- INF: `RenderOnlyLab=0`, schema 132

### 5.6 Documentos

- `ROADMAP_BC250_WDDM_DISPLAY_RENDER_DCN.md` — Gate 1.5, sub-gate 2.0 (IP Discovery/DMU), Gate 8.5 (TDR), Vulkan dividido em 9a–9f, orcamento de corridas
- `HANDOFF_GATE_1_5_RENDERONLYLAB.md`
- este post-mortem

### 5.7 Verificacao offline

`build-contract.bat` limpo em `/W4 /WX`, suite completa aprovada, tres auditorias aprovadas, pacote 3.75 assinado e auditado.

### 5.8 Defeito meu que corrigi antes do boot

O inspetor exigia P1/P2/shader prontos tambem no modo render-only. Como esse perfil sai antes do bring-up por projeto, ele teria reprovado uma corrida valida. Corrigido para exigir zero. **Nao carreguei a mesma correcao para o `prepare` — e foi essa omissao que forcou o boot arriscado.**

---

## 6. O que corrigir antes do proximo ensaio

1. **Usar a imagem de producao para o Gate 1.5.** `tools/build-bc250mcdm-package.ps1`, `SkipHwBringup=1`, sem `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP`. O `StartDevice` termina apos herdar o scanout do GOP, que e' precisamente o que o gate exige.

2. **Relaxar o preflight do `prepare` para o alvo render-only**, do mesmo modo e pelo mesmo motivo que ja' foi feito no inspetor. Sem isso, o gate obriga um boot de bring-up para satisfazer uma precondicao que o perfil nunca usa.

3. **Fechar a lacuna de reset de telemetria** — blocos de shader, HQD e `Dmm*` fora de qualquer reset. Sem isso, `ShaderPhysicalReady=1` e os contadores `Dmm*` podem estar aprovando com dados de boots anteriores. Ja' registrado como tarefa separada.

4. **Separar os eixos.** Um candidato para o Gate 1.5; outro, depois, para a limpeza de falsos sucessos que toca o caminho KMDOD.

---

## 7. O que ficou provado, e o que nao

**Provado nesta corrida:** o bring-up P1/P2 executou integralmente sobre o binario 3.75, com RPTR==WPTR e poison verificado; a resolucao de perfil e o tunable `RenderOnlyLab` funcionam no silicio; a tabela sem `SetPalette`/`SetDisplayPrivateDriverFormat` e' aceita pelo `dxgkrnl`.

**Nao provado:** que as minhas alteracoes nao contribuiram para a tela preta — nao ha' corrida de referencia; a causalidade exata da perda de imagem; prontidao de shader e HQD nesta corrida especifica (campos fora do reset); e **a hipotese do Gate 1.5, que nao chegou a ser testada** — o ensaio parou no boot preparatorio.

**Veredito: NAO VALIDADO EM HARDWARE.**
