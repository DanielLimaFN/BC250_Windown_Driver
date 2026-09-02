# Handoff — Gate 1.5 / `RenderOnlyLab`

**Data:** 2026-09-01
**Candidato:** 3.75.0.0 — telemetria schema **132**, empacotado e assinado
**Estado:** **NAO VALIDADO EM HARDWARE**
**Predecessor:** 3.74.0.0 (schema 131), tela preta nao diagnosticada

---

## 0. Identidade do candidato

| | |
|---|---|
| `DriverVer` | `09/01/2026,3.75.0.0` |
| `TelemetrySchemaVersion` | 132 |
| `SourceSysSha256` | `3413709BB5F8DAA14BD0F70424B570A2F07CA0A59BB083CE17DD32D49C98C5EC` |
| `PackageSysSha256` | `E9949CAAD8A2EBE09D141680417DCB4675AFF2B999FAC63FE2512D724B23687F` |
| `PackageInfSha256` | `10A6524EFD64EF8A3B614535DBF1967008A1B4BF6F9296E9E0B97D94A9ABD27A` |
| `PackageCatSha256` | `06C064C031E063B9D71A84A68B431822222F4A818ED3E93AEDB021E560B09E50` |
| Signer | `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B` |

`SourceSysSha256` do 3.74 era `9867D1C5...` — mudou, entao o link pegou o codigo novo. A receita de laboratorio e' **identica** a do 3.74 (`ShaderDispatch=1`, `CpSoftReset=1`, `PersistentSubmission=1`, `SdmaAutoCtxSw=0`), de modo que `RenderOnlyLab` seja a unica variavel entre os dois candidatos.

`PackageSysSha256` ficou identico entre dois empacotamentos sucessivos do mesmo `.sys`, o que confirma o determinismo do `/Brepro`.

**Nada foi instalado.**

---

## 1. A hipotese que o proximo boot testa

Uma unica pergunta, e ela nao depende de DCN:

> **Um adaptador WDDM registrado por `DxgkInitialize`, que anuncia a tabela render/base completa e NENHUMA superficie de display, sobrevive em Codigo 0 e avanca alem de `V5PostStartStage=3`?**

O que essa resposta separa:

| Observado no boot | Conclusao |
|---|---|
| Codigo 43 no estagio 3, igual ao display+render | O bloqueio **nao** vem de display nem de `Present`. Investigar o contrato base/render. |
| Avanca nas caps e so' entao Codigo 43 | Estagio novo identificado; o display era parte do problema, mas nao todo ele. |
| Codigo 0 estavel | Tabela render/base correta. O display passa a ser o proximo bloqueio, isolado. |
| Chega a D3DKMT/submissao | Vulkan compute pode ser desenvolvido **antes** do DCN. |

O que este boot **nao** responde: se `DxgkDdiPresent` foi a funcao que apagou a tela no 3.74. Essa continua sendo uma pergunta de KDNET, nao deste ensaio.

---

## 2. O defeito estatico que motivou o gate

Confirmado por leitura de codigo, sem gastar corrida:

- `DriverEntry` bifurca em dois contratos. `RenderNode=0` registra por `DxgkInitializeDisplayOnlyDriver` e usa `DxgkDdiPresentDisplayOnly`, que **e** implementada.
- `RenderNode=1` registra por `DxgkInitialize`, e naquela tabela o unico caminho de apresentacao era `Bc250DxgkDdiPresent` — `STATUS_NOT_SUPPORTED` incondicional. `PresentDisplayOnly` nao existe nessa estrutura.
- Ao mesmo tempo, `StartDevice` devolvia 1 source e 1 child, e `QueryChildRelations` descrevia um DisplayPort externo.

**Um adaptador anunciava saida de video fisica cujo unico caminho de apresentacao recusava toda apresentacao.**

### Por que a correcao nao foi "tirar o `Present`"

Remover a DDI mantendo source/child so' trocaria a tela preta por rejeicao de tabela, Codigo 43 mais cedo ou falha de criacao da primary. E implementar um `Present` "minimo" sem allocations, contextos, primary surface e backend fisico produziria outro falso sucesso.

A correcao e' de **perfil**: ou o adaptador anuncia o conjunto inteiro de display, ou nao anuncia display nenhum.

---

## 3. Os tres perfis

| Perfil | `RenderNode` | `RenderOnlyLab` | Registro | Sources | Children | Display |
|---|---|---|---|---|---|---|
| `DISPLAY_ONLY` (herdado) | 0 | 0 | `DxgkInitializeDisplayOnlyDriver` | 1 | 1 | `PresentDisplayOnly` |
| **`RENDER_ONLY_LAB`** | 1 | 1 | `DxgkInitialize` | **0** | **0** | **nenhuma DDI** |
| `DISPLAY_RENDER` (destino) | 1 | 0 | `DxgkInitialize` | 1 | 1 | conjunto completo |

Resolvido uma unica vez em `DriverEntry`, em PASSIVE_LEVEL, antes de qualquer registro. O enum amarra tres decisoes que precisam ser coerentes: rotina de registro, callbacks anunciados e sources/children devolvidos. A incoerencia entre as duas ultimas foi o defeito do 3.74.

### Fail-closed

- `RenderOnlyLab=1` com `RenderNode!=1` → `STATUS_DEVICE_CONFIGURATION_ERROR`. O caminho KMDOD e' display por definicao.
- Valores fora de `{0,1}` → recusados, nao adivinhados.
- Tunable ilegivel com valor nao-default → recusado (indistinguivel de registro corrompido).
- Perfil fora do enum → `STATUS_INVALID_PARAMETER`, estado anterior intacto.

### O que `RENDER_ONLY_LAB` nao toca

- Nao adquire post-display ownership.
- Nao chama `Bc250DisplayInit`.
- Nao reprograma modo, link, clock nem framebuffer. **O GOP continua dono do scanout.**
- Consequencia pratica: um FAIL neste gate **nao** custa tela preta nem boot frio.

### O que ele deliberadamente nao promove

`ContextAbiReady`, `AllocationAbiReady` e `RootPageTableAbiReady` ficam em **zero**, iguais ao caminho de display de producao. Promove-las anunciaria objetos WDDM sem backend fisico provado — o falso sucesso que o projeto proibe. A promocao e' decisao de um gate posterior, com evidencia propria.

`ComputeOnly` permanece **0**. Anunciar `ComputeOnly=1` exigiria antes confirmar que o contrato selecionado aceita a combinacao com zero sources; isso nao foi confirmado, entao nao foi anunciado.

---

## 4. Telemetria — schema 132

Seis campos novos. Sem eles a corrida seria ininterpretavel: um Codigo 43 nao diria se o perfil que subiu foi o pedido.

| Campo | Significado |
|---|---|
| `V5AdapterProfile` | 0 = DISPLAY_ONLY, 1 = DISPLAY_RENDER, 2 = RENDER_ONLY_LAB |
| `V5RenderOnlyLabRequested` | valor lido do tunable |
| `V5RenderOnlyLabTunablePresent` | 1 se o valor foi realmente lido do registro |
| `V5ProfileResolveStatus` | NTSTATUS da resolucao do perfil |
| `V5DisplaySurfaceAnnounced` | 1 se a tabela levou VidPn/Present |
| `V5ChildEnumerationEnabled` | 1 se `QueryChildRelations` descreve um child |

`V5RegistrationMode` ganhou o valor **2** = registrado por `DxgkInitialize` no perfil render-only.

---

## 5. Auditoria geral de falsos sucessos

`tools/audit-ddi-noop-callbacks.py`, ligada ao `build-contract.bat`. Varre a classe inteira em vez de corrigir caso a caso.

**Reprova** quando uma funcao anunciada satisfaz as tres condicoes: e' atribuida a um campo `DxgkDdi*`; todos os parametros aparecem so' dentro de `UNREFERENCED_PARAMETER`; e todo `return` devolve `STATUS_SUCCESS`. Uma funcao que ignora tudo que recebe e ainda declara sucesso nao pode ter produzido efeito verificavel.

**Avisa** quando le argumentos so' para validacao e devolve sucesso sem efeito, ou quando ignora os argumentos mas age (notificacao pode ser legitima).

### O que ela encontrou alem do caso conhecido

| DDI | Decisao |
|---|---|
| `SetDisplayPrivateDriverFormat` | Nao anunciada. Opcional; sem allocation primaria propria nao ha' formato privado a negociar. |
| `SetPalette` | Nao anunciada nos dois perfis. So' faz sentido com modos palettizados; o driver expoe apenas `D3DDDIFMT_A8R8G8B8`. |
| `CollectDbgInfo` (perfil KMDOD) | Passou a apontar para `Bc250McdmCollectDbgInfo`, que valida o buffer e devolve `STATUS_NOT_SUPPORTED` sem produtor. O stub anterior fazia o `dxgkrnl` gravar um blob vazio acreditando ser evidencia. |
| `NotifySurpriseRemoval` | **AVISO em aberto.** Ignora `RemovalType` mas produz efeito. Vale decidir se remocao permanente e temporaria deveriam divergir. |

`audit-mcdm-ddi-matrix.py` ganhou `WDDM_DELIBERATELY_ABSENT`: as duas DDIs retiradas agora **reprovam** se voltarem a ser anunciadas sem que a justificativa seja removida. Isso e' mais forte do que apenas tira-las da lista de obrigatorias — foi assim que um `STATUS_SUCCESS` vazio embarcou sem ninguem notar.

---

## 6. Sequencia fisica do ensaio

Todos os passos sao do operador. Nenhuma instalacao automatica.

1. Recuperar para `BasicDisplay / Codigo 0` e boot limpo.
2. Instalar o candidato 3.75 com `RenderNode=0` (perfil KMDOD herdado); confirmar Codigo 0.
3. Promover para o perfil do Gate 1.5:

```bash
powershell -ExecutionPolicy Bypass -File tools\prepare-bc250mcdm-rendernode.ps1 -RenderOnlyLab -Execute -IAcceptDisplayRenderBootRisk
```

4. Reboot.
5. Julgar:

```bash
powershell -ExecutionPolicy Bypass -File tools\inspect-bc250mcdm-rendernode.ps1 -RenderOnlyLab
```

O inspetor exige `V5AdapterProfile=2`, `V5RegistrationMode=2`, `V5DisplaySurfaceAnnounced=0`, `V5StartDeviceSources=0`, `V5StartDeviceChildren=0` e Codigo 0. Passar o switch errado transforma resultado valido em falha ruidosa — de proposito: o julgamento tem de declarar antes qual hipotese esta' sendo testada.

**Assimetria deliberada entre os dois modos do inspetor.** Em `-RenderOnlyLab` ele exige `P1EngineState=0`, `P1SelfTestPhysicalReady=0` e `P2PhysicalReady=0`, e nao o contrario. No perfil render-only o `StartDevice` retorna antes do bring-up de hardware por projeto; `DriverEntry` zera o bloco P1/P2 e nada o reproduz. Exigir prontidao fisica ali reprovaria uma corrida valida. Exigir zero e' a prova positiva de que o early-return executou.

Somente campos que `Bc250ProbeResetSubmissionEvidence` realmente zera entram nessa afirmacao — o intervalo `[BC250_PROBE_P1_ENGINE_STATE .. BC250_PROBE_P2_DOORBELL_HIT_OBSERVED]`.

### Nota sobre contaminacao entre boots

`ShaderPhysicalReady` (enum 754) e `HqdActivePhysicalReady` (444) estao **fora** do intervalo de reset e sobrevivem ao boot anterior. Por isso nao entram nas afirmacoes do modo render-only.

Isso e' uma lacuna pre-existente, nao introduzida aqui, e ela tem consequencia no **outro** modo: quando o inspetor exige `ShaderPhysicalReady=1` no perfil display+render, ele pode estar lendo o valor de um boot anterior. Vale estender `Bc250ProbeResetRenderTrace` ou o intervalo de `Bc250ProbeResetSubmissionEvidence` para cobrir o bloco de shader e o bloco HQD — mas isso muda o `.sys` e exige novo empacotamento, entao ficou fora deste candidato.

**Antes de interpretar qualquer outra coisa:** se `V5ProfileResolveStatus != 0` ou o perfil observado divergir do pedido, a corrida esta' invalidada. Nao interpretar o resto.

---

## 7. O que continua sem prova

- Causalidade da tela preta do 3.74.
- Qualquer inicializacao de DCN — **nao existe uma unica linha de DCN no repositorio**, e o parser de IP Discovery conhece so' `GC_HWID=11` e `OSSSYS_HWID=30`. O Gate 2 precisa do sub-gate 2.0 antes de ser executavel.
- Submissao originada pelo `dxgkrnl`, GPUVA dinamica, UMD e Vulkan.

**Veredito: NAO VALIDADO EM HARDWARE.**
