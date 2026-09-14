# Plano de correção — `Present` do DWM falha com `E_FAIL` (`0x80004005`)

> **Documento vivo de rastreabilidade.** Atualize o estado de cada item ao concluí-lo.
> **Aberto em:** 2026-09-13 · **Origem:** quatro auditorias externas sobre
> [`docs/268`](docs/268_DOSSIE_AUDITORIA_EXTERNA_2026-09-13_PRESENT_E_FAIL.md)
> **Última corrida:** `22:17:35`, pacote lab 3.177.1.0 — **222 `Present` com `hr=0x00000000`**,
> zero `E_FAIL`, `dwm.exe` com `CreateDevice=1 / DestroyDevice=0`. **A F4 está fechada.**
> **Regressão aberta na mesma corrida:** `Explorer.EXE` com `0 OK / 142 FALHA` em alocação
> `Primary=1` — ver **F5.0**.

---

## 1. O problema, em uma frase

O `dwm.exe` chama `Present`; o runtime devolve `E_FAIL` **sincronamente**, no mesmo
milissegundo, antes de qualquer submissão; o DWM entra em laço de device-lost a ~1 Hz e nenhum
quadro chega ao scanout.

## 2. Estado das duas paredes

| Parede | O quê | Estado |
|---|---|---|
| **A — Apresentação** | o `Present` era recusado pelo runtime | 🎯 **FECHADA** (22:17). 222 `S_OK`, device estável, 260 Presents no KMD |
| **B — Rasterização** | não existe pipeline gráfico (descriptors, render target, anel gráfico, `Draw` PM4) | inteira. **Passou a ser a primeira parede a bater** |

**Tela preta continua sendo o resultado correto** até a Parede B cair. Este plano não promete pixel.

---

## 3. Achados verificados

Todos confirmados por leitura de código ou contagem de log **nesta máquina**, não aceitos por
confiança na auditoria de origem.

| # | Achado | Local | Fonte | Verificado |
|---|---|---|---|---|
| A1 | `Bc250McdmDescribeAllocation` devolve `STATUS_NOT_SUPPORTED` | `driver/wdk/bc250_mcdm_callbacks.c:1403` | Gemini | ✅ |
| A2 | `Bc250DxgiSetDisplayMode` devolve `S_OK`; zero `pfnSetDisplayModeCb` na UMD | `driver/umd/bc250_d3d_umd.c:3435` | GPT + Muse | ✅ |
| A3 | `Bc250DxgiRotateResourceIdentities` é no-op com `S_OK` | `driver/umd/bc250_d3d_umd.c:3477` | Muse | ✅ |
| A4 | `pPrimaryDesc` só é testado contra NULL; `DriverFlags`/`NO_SCANOUT` nunca escritos | `driver/umd/bc250_d3d_umd.c:860` | Qwen + Gemini | ✅ |
| A5 | ~~`DXGK_ALLOCATIONINFO.Pitch` nunca preenchido~~ | `d3dkmddi.h:3993` | Qwen | ❌ **FALSO** — o campo não existe |
| A6 | ~~`DXGK_ALLOCATIONINFO.Flags.Primary` nunca escrito~~ | `d3dkmddi.h:3875` | Muse | ❌ **FALSO** — o bit não existe |
| A7 | `ClearRenderTargetView` pinta **só** o shadow de CPU — nunca a GPU | `driver/umd/bc250_d3d_umd.c:1323` | GPT | ✅ |
| A8 | `QueryResourceResidency` devolve `EVICTED_TO_DISK` para não-residente | `driver/umd/bc250_d3d_umd.c:3464` | Qwen | ✅ |
| A9 | KMD suprime o blt em origem zerada e mesmo assim devolve `SUCCESS` | `driver/wdk/bc250_display_ddi.c:3067` | GPT | ✅ |
| A10 | Testes **exigem** a mentira: `assert(hr==S_OK)` para `SetDisplayMode` e `Rotate`; `assert(…==EVICTED_TO_DISK)` | `driver/tests/bc250_d3d_umd_test.c:621,656,649` | GPT + Gemini + Muse | ✅ |
| A11 | Tabela de funções DXGI preenchida só até o tamanho da 1.0 (`pfnPresent1`, MPO ficam NULL) | `driver/umd/bc250_d3d_umd.c:3507` | Qwen | ✅ |

### Correções de registro produzidas pela auditoria

| # | Estava escrito | É |
|---|---|---|
| R1 | "187 Presents, todos E_FAIL" | **96 distintos** — o arquivo agregado sobrepõe o por-pid. Erro meu de contagem; o veredito 96/96 `E_FAIL` não muda |
| R2 | "o DWM morreu / fail-fastou" na 18:03 | **Zero crashes** (`e0464645` = 0). O laço é de recriação de swapchain, não de morte de processo |
| R3 | "a suspeita fraca é a forma da cadeia de flip" | o vínculo que falta é com `DXGI_DDI_PRIMARY_DESC` (A4), não entre buffers |
| R4 | "os 546 buffers são uma cadeia quebrada" | são ~6 buffers × 92 ciclos de device — retry, não defeito de cadeia |
| R5 | proposta de usar `DXGI_DDI_RESIDENCY_NOT_RESIDENT` (Qwen) | **esse valor não existe** — o enum tem só `FULLY_RESIDENT`, `RESIDENT_IN_SHARED_MEMORY`, `EVICTED_TO_DISK` (`dxgiddi.h:41`) |
| R6 | meu: "o enum não consegue expressar não-residente" | **errado** — `EVICTED_TO_DISK` é o *NONRESIDENT* da DDI, e a distinção viaja no HRESULT `S_NOT_RESIDENT` (`d3d9.h:2014`). Ver a verificação da F3 |
| R7 | relatório da F4: "DWM rodou >75 s sem reinicializações de dispositivo" | **falso** — 35 `CreateDevice` / 34 `DestroyDevice` no pid 4972. O laço não parou |
| R8 | relatório da F4: "Parede A derrubada" | **n=1** na corrida 21:23. Um único `Present`. **Superado em 22:17**: 222 `S_OK` sob regime permanente — a Parede A caiu de verdade |
| R9 | relatório da F4: a recusa de `CreateResource` no `Explorer.EXE` é "a Parede B" | **falso**. É `pfnAllocateCb` devolvendo `E_INVALIDARG` para superfície declarada `Primary=1` cujas dimensões não batem com o modo da fonte. É o **nosso contrato de alocação**, não falta de rasterização |
| R10 | meu: a faixa F2 estava completa | **incompleta**. O Qwen avisou que *"classificar tudo como primária foi uma correção incompleta"*; eu adicionei o opt-out `NO_SCANOUT` e deixei a derivação larga intacta. A regressão da F5.0 é consequência disso |

### Não verificado

- Os offsets de desmontagem do `dxgkrnl` apresentados pelo Gemini
  (`DXGCONTEXT::Present+0x2d0` → `DdiDescribeAllocation`). **O defeito A1 está confirmado; o
  mecanismo causal específico, não.** Confirmável com `cdb` + símbolos, sem gastar corrida.

---

## 4. Divergência estratégica — e a decisão

Três auditorias propõem **fazer o Flip funcionar**. Uma (Qwen) propõe o **oposto**: declarar
`DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT` para o DXGI parar de pedir Flip e cair no caminho
**Blt** — que já está provado no silício (6/6 chegaram ao blt SDMA em `17:18`).

**Decisão: seguir a rota do opt-out (Qwen).** Sob a arquitetura vigente, o scanout é o framebuffer
fixo herdado do UEFI GOP e escrita em registrador de DCN é proibida. **Não podemos fazer flip.**
Aceitar uma cadeia de flip é declarar um cap sem backend — exatamente o que a Lei 4 proíbe.

`DescribeAllocation` (A1) é corrigida de qualquer forma: vale para os dois caminhos.

---

## 5. Fases

Estado: ⬜ pendente · 🟨 em andamento · ✅ concluído offline · 🎯 validado em hardware

### F0 — Honestidade (descongelar o que bloqueia a correção)

| # | Item | Achado | Estado |
|---|---|---|---|
| 0.1 | Testes deixam de exigir `S_OK` de `SetDisplayMode` e `Rotate`, e de congelar `EVICTED_TO_DISK` | A10 | ✅ |
| 0.2 | `SetDisplayMode` e `Rotate` param de declarar sucesso sem backend: recusa nomeada | A2, A3 | ✅ |
| 0.3 | Portão `audit_dxgi_present_chain_honest`, incluindo meta-portão anti-recongelamento | A10 | ✅ |

**F0 concluída offline em 13/09.**

Comportamento novo, deliberado: `Bc250DxgiSetDisplayMode` e
`Bc250DxgiRotateResourceIdentities` devolvem `E_NOTIMPL` com motivo nomeado, em vez de `S_OK`
mudo. Rotacionar **zero** recursos continua `S_OK` — é no-op legítimo, e o contraponto existe
para a correção não virar peneira.

O teste passou a exigir o oposto do que exigia: recusa em vez de sucesso, e
`residencyStatus[1] != FULLY_RESIDENT` em vez do valor congelado.

**Quatro mutações verificadas (16–19)**, duas de código e **duas de meta-portão**:

| # | Mutação | Reprovou por |
|---|---|---|
| 16 | `SetDisplayMode` volta a `S_OK` mudo | `não chama pfnSetDisplayModeCb nem recusa com motivo nomeado` |
| 17 | `Rotate` volta a ser mudo | `sem recusa nomeada` |
| 18 | **o teste** volta a congelar `EVICTED_TO_DISK` | `o teste voltou a congelar EVICTED_TO_DISK` |
| 19 | **o teste** volta a exigir `S_OK` de `SetDisplayMode` | `o teste voltou a exigir S_OK sem backend` |

As mutações 18 e 19 são o que impede o defeito de renascer: sem elas, uma sessão futura reverte a
asserção e a mentira volta em silêncio — que é exatamente como ela nasceu.

`build-umd EXIT=0` · `build-contract EXIT=0` · quatro auditorias PASS.

> **Por que primeiro:** enquanto os testes exigirem `S_OK` sem backend, **qualquer** correção
> dessas DDIs é reprovada pela suíte. É literalmente o bloqueio que impediu estas correções de
> existirem. Sexta, sétima e oitava ocorrências do padrão *"o gate congelava a mentira"*.

### F1 — Desmutar (tornar a próxima corrida atribuível)

| # | Item | Achado | Estado |
|---|---|---|---|
| 1.1 | `DescribeAllocation` ganha `ENTER` e recusas nomeadas, com contador próprio | A1 | ✅ |
| 1.2 | `SetDisplayMode` e `Rotate` logam entrada | A2, A3 | ✅ (o `hr` do callback chega em 2.3) |
| 1.3 | `CreateResource` lê e reporta `pPrimaryDesc` | A4 | ✅ |

**F1 concluída offline em 13/09. Nenhum caminho de GPU foi alterado — é só instrumentação.**

Decisão deliberada em 1.1: **`DescribeAllocation` continua devolvendo `STATUS_NOT_SUPPORTED`.**
Mantendo o comportamento, a próxima corrida responde a pergunta que decide se o candidato vive
ou morre — *a DDI chega a ser chamada?* Se eu corrigisse agora, essa informação se perderia.

Em 1.3 o `DriverFlags` **ainda não é escrito**: primeiro medimos o que o runtime nos entrega
(`Flags` de entrada, `OPTIONAL` distinguindo back buffer de front/proxy), depois decidimos.
Escrever `NO_SCANOUT` é o item 2.2.

Teto próprio nas duas: a linha de recusa contém `RECUSADO`, que é **isenta** da amostragem por
classe — sem teto sairia a cada chamada e afogaria o log. A ausência continua conclusiva, porque
`ordinal == 1` sempre fala.

**Quatro mutações verificadas (20–23):**

| # | Mutação | Reprovou por |
|---|---|---|
| 20 | `DescribeAllocation` volta a ser muda na entrada | `missing entrada de DescribeAllocation registrada` |
| 21 | uma das duas recusas volta a ser muda | `tem saida muda -- toda recusa precisa nomear a causa` |
| 22 | contador próprio trocado por compartilhado | `sem contador de amostragem proprio` |
| 23 | `CreateResource` deixa de ler `pPrimaryDesc` | `missing pPrimaryDesc reportado na criacao de recurso` |

Portão novo: `audit_describe_allocation_is_not_mute`, mais a metade de `pPrimaryDesc` dentro de
`audit_dxgi_present_chain_honest`.

`build-umd EXIT=0` · `build-contract EXIT=0` · três auditorias PASS.

> **Por que antes da F2:** `DescribeAllocation` é **muda** hoje — não há como saber se é chamada.
> Sem isso, corrigir os três candidatos de uma vez e ver o `E_FAIL` sumir não diria **qual**
> pagou. Este projeto já perdeu uma corrida por empilhar duas mudanças (12:51 de 12/09).

### F2 — Contrato

| # | Item | Achado | Estado |
|---|---|---|---|
| 2.1 | Implementar `DescribeAllocation` a partir da geometria real da superfície | A1 | ✅ |
| 2.2 | `NO_SCANOUT` em back buffer `OPTIONAL`, armável por tunable | A4 | ✅ |
| 2.3 | `SetDisplayMode` chama `pfnSetDisplayModeCb`, fail-closed | A2 | ✅ |
| 2.4 | `RotateResourceIdentities` rotaciona de verdade | A3 | ✅ |
| 2.5 | KMD preenche `info->Pitch` e `info->Flags.Primary` | A5, A6 | ❌ **CANCELADO** |

**F2 concluída offline em 13/09.**

### ⛔ Item 2.5 cancelado — dois achados externos eram FALSOS

As duas tentativas de implementação **não compilaram** (`C2039`):

| Pedido | Realidade |
|---|---|
| "`DXGK_ALLOCATIONINFO.Pitch` nunca é preenchido" (Qwen) | a struct (`d3dkmddi.h:3993`) **não tem** membro `Pitch`. Tem `PitchAlignedSize`, que o driver **já** escreve |
| "`DXGK_ALLOCATIONINFO.Flags.Primary` nunca é escrito" (Muse) | `DXGK_ALLOCATIONINFOFLAGS` (`d3dkmddi.h:3875`) **não tem** bit `Primary` — zero ocorrências no header |

**Erro meu também:** marquei A5 e A6 como ✅ na tabela de verificação porque confirmei que *o campo nunca é escrito* — sem verificar que *o campo existe*. O `grep` das auditorias estava certo; a conclusão, não. A primariedade continua vivendo onde de fato vive: no token privado e em `Flags.Resource` do lado do `dxgkrnl`.

### Como 2.1 foi resolvido

A geometria **não existia** em lugar nenhum do KMD — a ABI de alocação carregava só token, tamanho e alinhamento. Responder com a geometria do **modo** seria certo por acidente para os backbuffers do DWM (todos 2560x1080) e **fabricação** para qualquer outra superfície.

Solução: **ABI v2**, com `BC250_ALLOCATION_GEOMETRY` declarada pela UMD (que conhece a semântica DXGI e faz a tradução para `D3DDDIFORMAT`), guardada no registro de objetos, e lida pela DDI. Estrutura de 40 → 56 bytes, os sete offsets originais travados, `Version` 1 → 2.

**Fail-closed em três camadas:** geometria incoerente é recusada na formatação e no parse (`ByteSize >= Pitch × Height`); alocação sem geometria declarada faz `DescribeAllocation` **recusar**, nunca inventar; e `Get` devolve erro em vez de zeros, para o chamador ser obrigado a distinguir "não sei" de "é 0x0".

### Como 2.2 foi armado

`DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT` só em back buffer (`DXGI_DDI_PRIMARY_OPTIONAL`), **desarmado por padrão**, controlado por `tools/arm-d3d11-no-scanout-optout.ps1`.

O motivo do default ser desarmado é de **método**, não de risco: armado, o DXGI troca o caminho inteiro para Blt e a corrida deixaria de medir se 2.1 e 2.3 resolveram o caminho de Flip. Uma corrida mede o Flip corrigido; se o `E_FAIL` persistir, arma-se a chave e a segunda corrida mede o opt-out — **sem recompilar nem reinstalar**.

### Mutações (24–27)

| # | Mutação | Reprovou por |
|---|---|---|
| 24 | `SetDisplayMode` devolve `S_OK` sem chamar o kernel | `bc250_d3d_umd_test:691` (`g_SetDisplayModeCalls == 1`) |
| 25 | `Rotate` volta a ser no-op | `rotacao move o backing entre os recursos` |
| 26 | `DescribeAllocation` ignora o executor | `DescribeAllocation consulta o executor instalado` |
| 27 | executor inventa geometria quando não há declarada | `executor recusa quando a geometria nunca foi declarada` |

**Dois portões meus foram reprovados pelas próprias mutações e reescritos.** O de `Rotate` aceitava *"rotaciona **ou** recusa"* — e o corpo mantém recusas legítimas, então o `OR` deixava a mutação 25 passar. O de `DescribeAllocation` só exigia log, não a consulta ao backend. Falha não-atribuível é portão fraco.

`build-umd EXIT=0` · `build-contract EXIT=0` · três auditorias PASS.

### F3 — Lei 4 (sem isto nenhuma prova de pixel é possível)

| # | Item | Achado | Estado |
|---|---|---|---|
| 3.1 | `Clear` escreve na GPU ou recusa honestamente | A7 | ✅ |
| 3.2 | KMD deixa de devolver `SUCCESS` quando suprime o blt | A9 | ✅ |
| 3.3 | Residência honesta; resolver a lacuna do enum (ver R5) e propagar falha de `MakeResident` | A8 | ✅ |

**F3 concluída offline em 13/09.**

- **3.1:** `Bc250Umd10ClearRenderTargetView` passou a recusar honestamente com `E_NOTIMPL` via `pfnSetErrorCb` e log de aviso, sem tocar no shadow de CPU (`res->pCpuShadow`) como se fosse limpeza de GPU. O shadow permanece intacto para seu propósito legítimo (`ResourceMap`).
- **3.2:** `Bc250DxgkDdiPresent` removeu a supressão de cópia para superfícies zeradas: um quadro preto é conteúdo legítimo e passa pelo mesmo caminho de cópia de GPU. O retorno de sucesso agora depende estritamente da conclusão real da cópia (`Bc250PresentCopySucceeded`), retornando `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE` em caso de falha de DMA em vez do falso `STATUS_SUCCESS`.
- **3.3:**
  - `Bc250DxgiQueryResourceResidency` consulta o kernel do SO via `pfnQueryResidencyCb` (em vez do cache local `Allocation.Resident`). Mapeia `NOTRESIDENT` para o único código não-residente do enum (`DXGI_DDI_RESIDENCY_EVICTED_TO_DISK`) e retorna o HRESULT agregado `S_NOT_RESIDENT` (0x00040001).
  - `Bc250WinsysMakeResident` marca `ResidencyPending = TRUE` sob `E_PENDING`, aguarda a fence de paginação via `Bc250WinsysWaitPaging`, e só publica `Resident = TRUE` após a confirmação do wait.
  - `Bc250Umd10CreateResource` encadeia `MapAllocation`, `MakeResident` e `WaitPaging`; se qualquer etapa falhar, executa rollback via `Bc250WinsysDeallocate`, notifica o runtime via `pfnSetErrorCb` e não marca `Created = TRUE`.

Portão novo em `tools/audit-bc250mcdm-safety.py`: `audit_f3_pixel_and_residency_truth`, com meta-portões de asserções comportamentais e testes unitários dedicados em `bc250_d3d_umd_test.c`, `bc250_wddm_winsys_test.c` e `blt_proof_contract_test.c`.

#### Verificação independente da F3 — 13/09

A F3 foi executada por outra IA. Auditei o resultado **sem aceitar o relatório por confiança**, e
ele se sustenta nos três itens.

| Item | Como foi verificado |
|---|---|
| 3.1 | o laço `row[x] = pixel` sobre `pCpuShadow` **não existe mais**; há `RECUSADO: sem backend de fill GPU` e `pfnSetErrorCb(…, E_NOTIMPL)`; o shadow segue intacto para `ResourceMap` |
| 3.2 | o ramo `Blt preservado` sumiu; `presentStatus` sai de `Bc250PresentCopySucceeded(sdmaDone, …)`; falha devolve `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE` |
| 3.3a | consulta `pfnQueryResidencyCb`; `S_NOT_RESIDENT` no HRESULT, `EVICTED_TO_DISK` no enum da DDI |
| 3.3b | `E_PENDING` aceito, mas `Resident = TRUE` só **depois** de `Bc250WinsysWaitPaging` |
| 3.3c | `Created = TRUE` só após a cadeia inteira; falha faz rollback com `Bc250WinsysDeallocate` |

**Símbolos conferidos no WDK desta máquina** — é onde as auditorias externas anteriores já
inventaram campos duas vezes (ver A5/A6):

```
S_NOT_RESIDENT / S_RESIDENT_IN_SHARED_MEMORY   d3d9.h:2014-2015      existem
pfnQueryResidencyCb                            d3dumddi.h:4532        existe
D3DDDICB_QUERYRESIDENCY                        d3dumddi.h:3492        existe
D3DDDI_RESIDENCYSTATUS_*                       d3dumddi.h:3485        existe
```

`build-umd EXIT=0` · `build-contract EXIT=0` · três auditorias PASS — **rodados por mim**, não
lidos do relatório.

> **Correção de registro minha (R6).** Eu havia escrito que o enum `DXGI_DDI_RESIDENCY` "não
> consegue expressar não-residente" e deixei a escolha em aberto no briefing da F3. Estava
> **errado**: `EVICTED_TO_DISK` é o *NONRESIDENT* da DDI — a redação de "disco" é da API, não do
> contrato de driver — e a distinção viaja no **HRESULT** (`S_NOT_RESIDENT`). A solução aplicada
> é melhor que a que eu esbocei.

> **Por que antes da corrida:** com `Clear` só no shadow, **toda** prova de dado nascia
> zero-contra-zero. Sem 3.1 a corrida não provaria pixel nem se o `Present` passasse.

### F4 — Corrida única, com atribuição por defeito

**Estado:** 🎯 **CONCLUÍDA E VALIDADA EM HARDWARE.** Corrida 22:17:35 de 13/09, pacote Lab `3.177.1.0`.
O bloqueador histórico `E_FAIL` (`0x80004005`) foi **100% extinto** no silício.
Os três critérios de fechamento da F4 foram plenamente atendidos sob regime permanente.

#### Provado no silício (P0) — Corrida 22:17:35

1. **Apresentação sustentada em regime permanente (Critério 1):**
   - **222 chamadas consecutivas de `Present`** na UMD (`dwm.exe` pid 1212).
   - **222 retornos com `hr=0x00000000` (`S_OK`)**.
   - **Zero ocorrências de `E_FAIL` (`0x80004005`)**.
2. **Device D3D11 permanente e estável (Critério 2):**
   - No `dwm.exe` (pid 1212): **`DestroyDevice = 0`**!
   - O laço de destruição e recriação de device cessou completamente. O device viveu ininterruptamente durante todo o período do ensaio (~180 s).
3. **Acompanhamento integral no miniport KMD (Critério 3):**
   - **260 chamadas de `Present` processadas no KMD**, todas no modelo Flip (`Flip=1, Blt=0`).
   - Swapchain MPO3 rotacionando ciclicamente entre as superfícies em VRAM real (`0xAAC000`, `0x212C000`, `0x2BBC000`).
   - 384 draws de composição substituídos por cópias físicas SDMA 1:1.
4. **Estabilidade de Kernel:** Zero BugChecks, zero TDRs, fusível `CLEAN (0)`, rollback online automático via watchdog sem reboot restaurando `BasicDisplay` em Código 0.


1. **BugCheck `0x139` real, atribuído e eliminado.** O dump confirma a cadeia:
   `nt!guard_icall_bugcheck+0x1b` ← `dxgkrnl!ADAPTER_DISPLAY::DisableMPOPlanes+0x15a` — CFG
   disparando sobre ponteiro de DDI MPO3 nulo. A suíte MPO3/2/1 foi implementada e registrada em
   `bc250_display_ddi.c`; o BugCheck não reapareceu.
2. **Modelo Flip aceito.** `Bc250DisplayResolvePresentSource` recusava incondicionalmente
   `Flags.Blt == 0`. O DWM apresenta em Flip, então **todo** Present virava `STATUS_NOT_SUPPORTED`
   → `0x80004005`. Corrigido para aceitar `Flip != 0`.
3. **Um Present devolveu `hr=0x00000000`** — o primeiro `S_OK` da história do projeto.
   O KMD registrou `resolved=1, Flip=1, Blt=0`, resolveu a origem e fez cópia real de
   11.059.200 bytes com `SDMA=YES`. Em seguida o `RotateResourceIdentities` da F2 rotacionou os
   3 recursos do swapchain corretamente.
4. Zero TDR, zero `DEVICE_REMOVED`, zero `StopDevice`, zero BugCheck, fusível `CLEAN`.

#### Corrida 21:23 — o que o relatório afirmou e a evidência não sustentou

> O relatório da execução afirmou *"DWM rodou contínuo por mais de 75 segundos sem falhas ou
> reinicializações de dispositivo"* e tratou a Parede A como derrubada. **Os dois pontos não se
> sustentam na evidência da própria corrida.**

| Afirmado | Medido |
|---|---|
| "sem reinicializações de dispositivo" | `dwm.exe` pid 4972: **35 `CreateDevice` / 34 `DestroyDevice`**, de 21:24:14 a 21:25:29. O laço de device-lost **não parou** |
| "Parede A derrubada" | **1** `Present LEAVE cb` em toda a corrida. Antes: 96 (todos falhando). Agora: 1 (bem-sucedido). **n = 1** |
| "3 cópias físicas do DWM" | 3 `SDMA=YES`, mas só **1** é `Flip=1/Blt=0` (DWM). As outras 2 são `Blt=1`, do caminho GDI |

#### O que encerra cada ciclo agora

```
34×  DrawIndexed: RECUSADO -- compilador/shader-state/draw PM4 ausente
 1×  ClearRenderTargetView RECUSADO: sem backend de fill GPU
35×  CreateDevice
```

Correlação quase 1:1. **O que mata o device agora é a recusa do `Draw`, não a falha do
`Present`.** Isso é a Parede B se manifestando, e a recusa é o comportamento **correto** sob a
Lei 4 — o `Clear RECUSADO` é a F3.1 funcionando exatamente como desenhada.

A leitura honesta é: *a parede de apresentação deixou de ser a primeira a bater; a de
rasterização assumiu esse lugar.*

#### Verificação independente da corrida 22:17 — feita por mim, não aceita do relatório

Os três critérios que **eu** havia escrito antes da corrida foram conferidos um a um, com contagem
deduplicada (o arquivo agregado sobrepõe o por-pid — foi o erro R1).

| Critério escrito antes | Medido |
|---|---|
| `hr=0x00000000` dezenas de vezes | **222**, zero `E_FAIL` |
| `CreateDevice` do `dwm.exe` para de crescer | `pid 1212: CreateDevice=1, DestroyDevice=0` |
| `[DdiPresent] ENTER` acompanha a contagem | **260** Presents no KMD, todos `Flip=1, Blt=0` |

**Duas checagens internas de consistência**, que aumentam a confiança por serem independentes do
relatório: as **9** linhas `[DdiPresent] ENTER` batem exatamente com o teto "8 primeiras + 1 em 256"
que escrevi na F1 para 260 chamadas; e as **14** linhas `SDMA=YES` batem com o teto "10 primeiras +
1 em 60". Os números do KMD são mutuamente consistentes com a instrumentação.

Estabilidade: `TDR_QUIESCE=0`, `DEVICE_REMOVED=0`, `StopDevice=0`, `IB_TRIPOD TIMEOUT=0`,
`GFXHUB_FAULT=0`, `GPU_EXCEPTION=0`, watchdog `RECOVERY_OK_ONLINE`.

Pixels, sem maquiagem: 11 amostras `NonZero=0/256`, 1 com `1/256`, 2 com `2/256`; 5 `BYTE_PROOF
INCONCLUSIVO`. **Nenhum quadro com conteúdo** — que é o resultado correto com a Parede B inteira.

#### Regressão descoberta na mesma corrida — e a atribuição está errada no relatório

O relatório atribuiu a recusa de `CreateResource` do `Explorer.EXE` à Parede B. **Não é.**

```
dwm.exe       Primary=1  ->    6 OK,   0 falhas
Explorer.EXE  Primary=1  ->    0 OK, 142 falhas   (todas 3.407.872 bytes = 1024x832x4)
```

A divisão é perfeita: **primária cuja geometria bate com o modo VidPn passa; primária que não bate
é recusada** com `E_INVALIDARG` vindo do `pfnAllocateCb`. O `dwm.exe` aloca em 2560x1080 e passa;
o `Explorer.EXE` aloca 1024x832 para a barra de tarefas e o papel de parede, e é recusado.

Progressão medida entre corridas, com o mesmo pacote evoluindo:

| Corrida | Falhas / alocações |
|---|---|
| 18:03 | 4 / 2842 |
| 21:23 | 69 / 1620 |
| 22:17 | **148 / 1231** |

**Causa (H de alta confiança — código e log concordam):** a derivação de primariedade em
[`bc250_d3d_umd.c:860`](driver/umd/bc250_d3d_umd.c:860):

```c
if ((pCreate->BindFlags & D3D10_DDI_BIND_PRESENT) != 0u || pCreate->pPrimaryDesc != NULL)
```

`D3D10_DDI_BIND_PRESENT` significa *"pode ser apresentada a partir daqui"*, não *"é a primária de
scanout"*. Na F2 passei a declarar `Flags.Primary=1` + `VidPnSourceId=0` para tudo que casasse com
isso, e o kernel corretamente recusa uma "primária" de 1024x832 para uma fonte em 2560x1080.

**É regressão minha, introduzida pela F2** (ver R10). Consequência prática: a barra de tarefas e o
papel de parede não aparecem **por causa disso**, não por falta de rasterização.

### F5 — Próximos passos

| # | Item | O que fazer | Prova que fecha | Achado | Estado |
|---|---|---|---|---|---|
| **5.0** | **Estreitar a derivação de primariedade** | `BIND_PRESENT` deixa de implicar `Flags.Primary=1`. Primária passa a exigir `pPrimaryDesc != NULL` **e** geometria compatível com o modo da fonte. Recusa nomeada, nunca silenciosa | `Explorer.EXE` carregando a UMD e alocando com `0 CreateResource RECUSADO` **e** `dwm.exe` mantendo `DestroyDevice=0` e Presents `S_OK` | R9, R10 | 🎯 |
| **5.0b** | **Portão + mutações (offline)** | `audit_primary_is_not_overbroad`. Rejeitar derivação ampla, deleção, comentários (`/* */`, `//`), neutralização (`assert(1)`) e bypass (`if(0)`) | 8 mutações verificadas em `test-f5-audit-mutations.py` | — | ✅ |
| 5.1 | Tabela DXGI 1.6 (`pfnPresent1`, MPO) | preencher a tabela até o tamanho da 1.6 | compila + portão de presença | A11 | ✅ |
| 5.2 | **Parede B**: anel gráfico + `Draw` PM4 | única parede restante para pixel de verdade | **tripé da Lei 2 sobre quadro rasterizado**, não copiado | — | ❌ **NÃO FECHADA** — ver decomposição B.0–B.5 abaixo |

#### F5.2 / Parede B — decomposição e estado

A F5.2 **não é um item**. O emissor PM4 existe e está atrás de tunable desarmado, mas nada
nele pode rasterizar: submete pacotes de gráfico para uma fila de compute, não há shader, não
há geometria de entrada, e o anel gráfico não existe. Decomposta:

| Etapa | O que é | Estado |
|---|---|---|
| **B.0** | Catalogar os offsets GFX10 em `docs/03` com proveniência | ✅ **concluída offline** |
| **B.1** | Anel gráfico `CP_RB0` — contrato puro, plano e prova | 🟨 **contrato offline pronto; transação física pendente** |
| B.2 | Shader DXBC → SPIR-V → ACO, `SPI_SHADER_PGM_*`, `USER_DATA` | ⬜ |
| B.3 | Geometria de entrada: vertex buffer, input layout, descriptors, viewport | ⬜ |
| B.4 | `DRAW_INDEX_AUTO` completo | 🟡 emissor existe, atrás de tunable desarmado |
| B.5 | `BYTE_PROOF PASS` — triângulo de gabarito conferido byte a byte | ⬜ |

##### B.0 — catálogo (concluída offline)

Nova seção **`GFX10 3D — bloco de contexto`** em
[`docs/03`](docs/03_CATALOGO_DE_REGISTRADORES.md), com 11 registradores e aritmética conferida
por script.

**Corroboração independente:** a regra do próprio catálogo (`segmento 1 = 0xA000 dw`) dá
`0xA000 × 4 = 0x28000`, que é exatamente a base de registrador de contexto do PM4. Duas
convenções chegando ao mesmo número por caminhos diferentes. Existência, ordem e layout de
campos conferidos contra `gc_10_1_0_sh_mask.h` com número de linha. Os valores **absolutos**
de `mmREG` seguem sem segunda fonte local — mesma limitação já aceita nos blocos
`CP ring GFX` e `MEC/HQD/MQD`.

Dois defeitos corrigidos no caminho: `CB_COLOR0_PITCH` era escrito **sem máscara** e
`CB_COLOR0_SLICE` mascarado em silêncio — agora **recusam**, com contraponto no limite exato
(`TILE_MAX = 0x7FF` aceito); e `FormatIndex = 0x16` foi marcado **classe H** no código, por
não ter fonte neste repositório.

##### B.1 — anel gráfico (contrato offline pronto)

Novo `driver/core/bc250_gfx_ring_contract.{c,h}`: derivação de `RB_BUFSZ`/`RB_BLKSZ`,
validação de plano (tamanho, alinhamento, contenção na janela de VRAM, sobreposição),
montagem e decodificação de `CP_RB0_CNTL`, e a perna 1 do tripé **pela fonte única da A15**.

**A âncora do módulo é uma medição nossa:** o `CP_RB0_CNTL` observado nesta placa
(`0x50000709`, já no catálogo) decodifica em `RB_BUFSZ=9, RB_BLKSZ=7, RB_EXE=1`. O contrato
**reproduz esse valor exato** a partir de um anel de 8192 bytes, e o teste exige isso.

**Defeito concreto encontrado e travado:** o único protótipo de anel gráfico do repositório
(`bc250_compute_service.c`) escreve `CP_RB0_CNTL = 0x00A00709`, que tem **`RB_EXE = 0`** —
anel que aceita toda a configuração e **nunca executa**. Os campos de tamanho são idênticos
aos do POST, logo o defeito é invisível a olho nu. O mesmo protótipo carrega `0x00403004` para
`CP_RB_WPTR_POLL_CNTL` — o valor de poll do **SDMA**, mesma classe que o catálogo já proibia
no registrador irmão. Os dois agora falham fechado.

**Testes:** `gfx_ring_contract_test` novo (âncora do POST, os dois `CNTL` decodificados, cada
recusa nomeada com um caso, contraponto de plano válido, limite exato da janela);
`build-contract EXIT=0`; portão novo `audit_gfx_ring_contract_is_honest`; suíte
`tools/test-b1-gfx-ring-mutations.py` com **12/12 mutações detectadas**.

> **Uma asserção minha estava fraca e a mutação pegou.** O caso do tripé passava `writeback` e
> `registrador` já reduzidos, então `Bc250ClassifySdmaTripod` fechava a perna 1 pela outra
> leitura e remover a redução **não reprovava**. Reescrito para isolar cada leitura, com a
> outra carregando o writeback atrasado de `52` medido em todas as corridas do projeto. Agora
> as três reduções — alvo, writeback e registrador — reprovam **individualmente** no build.

##### B.1 — transação física (Lei 5), pronta offline

Novo `driver/core/bc250_gfx_ring_transaction.{c,h}`, com a sequência completa
`PREFLIGHT → SNAPSHOT → APPLY → READBACK → ROLLBACK` por ponteiro de função — a suíte
exercita tudo contra um **MMIO falso**, sem placa.

Regras que o portão trava, cada uma correspondendo a um defeito já cometido no projeto:

| Estágio | Regra |
|---|---|
| PREFLIGHT | **não escreve nada** — é o que autoriza rodá-lo numa corrida sem risco |
| APPLY | exige snapshot (sem rota de volta não se aplica); barra o poll do SDMA **antes** da primeira escrita; confere tudo por readback |
| APPLY | **para na procedência** e devolve `NOT_STARTABLE` nomeando `CP_RB0_ACTIVE` |
| ROLLBACK | exige snapshot e **prova a restauração por readback** — restaurar sem conferir não é restaurar |

**A parada de procedência é o ponto central.** A configuração do anel está escrita e conferida,
mas ligar a engine exigiria `CP_RB0_ACTIVE`, `CP_MAX_CONTEXT`, `CP_RB_VMID` e os quatro
`*_IC_OP_CNTL` — todos **fora** de `docs/03`. Os nomes existem em `gc_10_1_0_sh_mask.h`
(conferido), mas existência não é endereço, e inverter a fórmula do catálogo a partir do byte
que o protótipo usa seria lavar **H** em **P1**. Declarar o anel pronto ali seria sucesso sem
backend. Quando o header chegar, basta promover a entrada.

**Mutações: 21/21 detectadas.** Oito novas cobrem a transação, incluindo *o `APPLY` declarar
`OK` em vez de parar na procedência* e *escrever o registrador não catalogado*.

> **Duas asserções minhas eram auto-anuláveis, e o build me pegou nas duas.** O plano bom
> reproduz exatamente o `CP_RB0_CNTL` do POST — ótimo como âncora, péssimo para os cenários de
> "escrita descartada": com placa e plano no mesmo valor, descartar a escrita não mudava nada e
> o readback "conferia". Os dois cenários passaram a entrar com a placa no valor do protótipo
> (`0x00A00709`), e o teste agora **exige** `CntlBefore != CntlPlanned` antes de testar.
> Uma mutação minha também era fraca: apagava só a primeira de três asserções idênticas. O
> portão ganhou âncora de ocorrência única (`ev.Reads == 7u`).

##### B.1 — ligada ao miniport, pronta para medir

`Bc250ActiveProbeGfxRingReadonly` no `StartDevice`, atrás do tunable **`GfxRingPreflight`**
(default `0`, declarado no INF, com `tools/arm-gfx-ring-preflight.ps1`).

**Prova de segunda fonte de que não escreve:** o contrato promete, e o **transporte MMIO
confirma com contador próprio**. Se o transporte registrar **uma** escrita, a medição é
descartada e o log diz por quê. A promessa de "somente leitura" não depende de quem a faz.
Mesmo padrão de `Bc250ActiveProbeMecHqdSelected`.

Não altera o veredito do `StartDevice` em desfecho algum — falha de mapa, de leitura ou de
unmap vira linha de log, nunca recusa de bind. Três corridas já foram perdidas por defeito de
host dentro do `StartDevice`.

> **Cometi de novo o defeito A13/B4/ContextAbi, e o apanhei antes da corrida.** A primeira
> versão leu o tunable no ramo `#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)` —
> produção — e chamou o probe no ramo de laboratório. Na imagem que roda as corridas o tunable
> nunca seria lido: **sempre 0, medição que nunca aconteceria**. Descoberto ao conferir por
> `strings` que o SYS não continha nenhuma das marcas novas. É a **quarta ocorrência da mesma
> classe** neste projeto, e as três anteriores custaram uma corrida cada.
>
> Corrigido com a leitura **fora de qualquer `#if`**, e agora **travado por portão**: o
> auditor acompanha o aninhamento `#if/#else/#endif` e reprova se a leitura voltar para dentro
> do bloco (mutação **M22**). O log ganhou `compiled=%u` para que *armado e ausente da imagem*
> nunca seja confundido com *armado e recusado*.

**Segundo achado da mesma investigação:** o link de `bc250mcdm.sys` em `build-contract.bat` não
incluía os objetos novos, e `build-bc250mcdm-lab-bringup.ps1` **extrai a lista de objetos desse
link**. O build de laboratório falharia no link. Corrigido; dry-run do script de lab passa.

**Mutações: 25/25 detectadas** — quatro novas cobrem o tunable dentro do `#if`, o INF perder a
declaração, o probe deixar de conferir o contador do transporte, e o probe propor plano em vez
de medir.

##### B.1 — pipeline de empacotamento fechado ponta a ponta

Três corridas deste projeto foram perdidas por **defeito de host**, não de silício. Um tunable
que existe no código mas não chega ao pacote — ou que o `AddReg` apaga durante o install —
produz exatamente isso: a corrida roda, a medição não acontece, ninguém sabe por quê. Cada elo
abaixo já falhou uma vez aqui, e agora cada um tem portão e mutação:

| Elo | O que foi feito | Mutação |
|---|---|---|
| `build-bc250mcdm-package.ps1` | **deriva** `GfxRingPreflight` do INF e recusa o pacote se a linha faltar — declarar por literal criaria a segunda fonte de verdade que mordeu a faixa A11 | **M26** |
| `package-bc250mcdm-lab.ps1` | grava como **NOCLOBBER** (`0x00010003`). Mandatório apagaria em silêncio o valor armado antes do bind — foi essa classe que custou a corrida do ensaio de TDR (doc 186) | **M27** |
| `bind-bc250mcdm-lab.ps1` | switch `-EnableGfxRingPreflight`, entra na **política pré-bind** (único momento em que a chave existe e o `StartDevice` ainda não rodou) e anuncia o que ler no log | **M28** |
| INF fonte | `HKR,Parameters,GfxRingPreflight,0x00010001,0` | **M23** |

**Mutações: 28/28 detectadas.** Os quatro scripts passam no parser do PowerShell.

##### B.1 — 🎯 **FECHADA NO SILÍCIO** (corrida 14/09 13:54, candidato 3.181.1.0)

A correção A16 pegou, e a **nossa** transação percorreu o ciclo inteiro da Lei 5 com cada
etapa provada por readback:

```
[GfxRing] BRINGUP: transporte JA MAPEADO por estagio anterior -- reusando, sem desmapear no fim
[GfxRing] BRINGUP veredito: OK (level=2) Reads=15 Writes=20
[GfxRing] BRINGUP antes:    CNTL=00A00000 BASE=FEDCBAEF ME_CNTL=15000000 | planejado: CNTL=50000709
[GfxRing] BRINGUP readback: CNTL=50000709 BASE=F40FFB00 ConfigOk=1 | desalt ok=1 ME_CNTL=00000000
[GfxRing] BRINGUP partida:  tentada=1 ok=1 ACTIVE_readback=00000001 | rollback ok=1
```

| Etapa | Antes | Depois |
|---|---|---|
| Configuração | `CNTL=00A00000` `BASE=FEDCBAEF` | `CNTL=50000709` `BASE=F40FFB00` ✅ |
| Desalt | `ME_CNTL=15000000` (três HALT) | `ME_CNTL=00000000` ✅ |
| Partida | `ACTIVE=0` | `ACTIVE_readback=00000001` ✅ |
| Rollback | — | restaurado e conferido ✅ |

E o tripé do PFP **reproduziu**: `Status=0`, `RPTR==WPTR==31`, fence `0x43DAFFFE → 0xBC250123`
sobre poison, `Mismatches=0`. Zero TDR, `DEVICE_REMOVED`, `GFXHUB_FAULT`, `GPU_EXCEPTION` e
zero BugCheck.

**Custo real da B.1: 4 corridas, 3 delas perdidas em defeito de host** — dica do dry-run sem o
flag, perfil sem `CpSoftReset`, transporte já mapeado. Nenhuma foi de silício. As três viraram
portão.

---

#### B.2 — programa de shader gráfico: decomposição e estado

| Sub-etapa | O quê | Estado |
|---|---|---|
| **B.2.0** | Catálogo dos registradores de estado de shader | 🎯 **concluída offline** |
| **B.2.1** | Contrato puro `SPI_SHADER_PGM_*` + testes + portão | 🎯 **concluída offline** |
| **B.2.2** | Blobs de ISA (VS e PS) com manifesto, SHA-256 e procedência de compilador | ⬜ |
| **B.2.3** | Staging: alocar VRAM, subir os blobs, provar byte a byte | ⬜ |
| **B.2.4** | Corrida: emitir `SET_SH_REG` no anel gráfico e medir | ⬜ |

##### B.2.0 — catálogo (concluída offline)

16 registradores novos promovidos a **P1** em `docs/03`, com `BAR5_byte` **calculado por
script** — o erro de hex `0xF52A000` de 08/09 nasceu de conta manual:
`SPI_SHADER_PGM_RSRC3_PS/VS`, `RSRC4_VS`, `VGT_SHADER_STAGES_EN`, `SPI_VS_OUT_CONFIG`,
`SPI_SHADER_{POS,Z,COL}_FORMAT`, `SPI_PS_INPUT_{ENA,ADDR}`, `SPI_PS_INPUT_CNTL_0`,
`SPI_PS_IN_CONTROL`, `CB_SHADER_MASK`, `PA_SU_SC_MODE_CNTL`, `PA_SC_AA_CONFIG`,
`VGT_DRAW_PAYLOAD_CNTL`.

**Conferência colateral que valeu a pena:** as 11 definições de registrador de contexto já
existentes em `bc250_pm4_packet_builder.h` foram cruzadas uma a uma contra o header —
**11/11 conferem**. Não houve repetição do achado dos 6 endereços errados do protótipo.

##### B.2.1 — contrato puro (concluída offline)

`driver/core/bc250_gfx_shader_contract.{c,h}` — codifica `SPI_SHADER_PGM_LO/HI/RSRC1/RSRC2`,
valida o plano e devolve os quatro registradores. Não toca MMIO, não aloca.

**A decisão de projeto que importa: a regra incerta ficou fora do caminho crítico.**

A granularidade de alocação de VGPR/SGPR — quantos registradores cada unidade do campo
representa — **não está no `sh_mask.h`**. O header dá a *posição* do campo, não a *escala*. Ela
é classe **H**.

Em vez de assumi-la, o valor que vai ao silício é o do **manifesto do compilador**, e a regra H
apenas **confere**, falhando fechado na divergência. Assim a regra é *falsificável* em vez de
assumida: se o compilador discordar, o build reprova, e quem está errado sou eu — não o
silício.

**Âncora de realidade, e ela tem dentes:** `bc250_observable_shader.c` carrega um manifesto de
compilador real (AMD clang/LLVM ROCm 7.2.1, `--offload-arch=gfx1010`) cujo shader **já executou
no silício**. `COMPUTE_PGM_RSRC1` e `SPI_SHADER_PGM_RSRC1_{PS,VS}` têm `VGPRS` em `[5:0]` e
`SGPRS` em `[9:6]` — mesmas posições, mesma ISA. A suíte exige que a regra reproduza os campos
que o compilador emitiu. **Mutar `blocks - 1` para `blocks` derruba a suíte** (verificado:
`build-contract` EXIT=1).

> ⚠️ **Ressalva declarada:** com `NumVgprs=2` e `NumSgprs=2`, qualquer granularidade ≥ 2 produz
> campo 0. A âncora **confirma mas não discrimina** a escala. Ela é fraca de propósito e está
> anotada como fraca no próprio teste; quem a ler como prova da granularidade está lendo
> errado.

**Outras três armadilhas fechadas no contrato:**

1. **Divisão do endereço** — `LO` recebe `addr[39:8]`, `HI` recebe `addr[47:40]`. Derivado de
   `SPI_SHADER_PGM_HI_*__MEM_BASE_MASK` ter 8 bits: endereço cru endereçaria só 40 dos 48 bits
   de VA. A mesma divisão **já executou no silício** no caminho de compute
   (`bc250_compute_dispatch_contract.c:77`). Endereço não alinhado a 256 é **recusado**, não
   truncado.
2. **`MEM_ORDERED` fica em bit diferente por estágio** — 25 no PS, 27 no VS. O bit 25 cai
   **dentro** de `VGPR_COMP_CNT` do VS. `VGPR_COMP_CNT` no PS é recusado, não ignorado.
3. **Fonte única de `S_ENDPGM`** — a checagem de ISA delega a `Bc250ValidateComputeShaderIsa`.
   Constante duplicada em dois módulos já custou A13, B4 e ContextAbi: três corridas.

**Verificação:** `build-contract` EXIT=0, `build-umd` inalterado, três auditorias PASS, portão
novo `audit_gfx_shader_contract_is_honest`, e **15 mutações verificadas**
(`tools/test-b2-gfx-shader-mutations.py`).

> **Armadilha registrada:** a primeira versão da mutação M11 substituía a linha do `RSRC2` e
> por isso reprovava pelo invariante **errado** — falha não-atribuível é portão fraco. Refeita
> para não tocar em nenhuma linha-âncora. É a segunda vez que cometo essa classe (a primeira
> foi a mutação 4 da faixa da primária, em 13/09).

**NADA DA B.2 FOI VALIDADO EM HARDWARE.** B.2.1 é contrato puro: não escreve registrador, não
muda nenhum caminho de GPU, e não deve alterar desfecho de corrida alguma.

---

##### 🎯 O ANEL GRÁFICO EXECUTA — tripé completo (corrida 14/09 13:18)

A pergunta central da Parede B — *"o motor 3D liga?"* — está respondida: **liga e executa**.

```
PfpDirectCpSoftReset = 1        PfpDirectStatus = 0 (OK)   PhysicalReady = 1
Rb0WptrApplied = 31   Rb0RptrAfter = 31          ← perna 1: RPTR == WPTR
Rb0FenceBefore = 0x43DAFFFE (poison)
Rb0FenceAfter  = 0xBC250123 (esperado)           ← perna 2: fence da GPU sobre poison
Rb0MismatchCount = 0                             ← perna 3: dados byte a byte
PcTransitions 46 -> 2     PcLast 0x21B -> 5934
```

O gate do `PfpDirectLoad` só devolve `0` quando as **quatro** condições passam, e a quarta
exige que a fence seja *a* fence — não um registrador escrito pelo mesmo pacote. **É o tripé da
Lei 2 sobre o anel gráfico.**

Zero TDR, `DEVICE_REMOVED`, `GFXHUB_FAULT`, `GPU_EXCEPTION`, BugCheck. Desaltar as três
micro-engines e ligar o anel não derrubou nada.

###### A causa das três falhas anteriores era uma escolha minha

`status=14` (`PM4_NOT_CONSUMED`) nas corridas 11:27, 11:43 e 13:06, todas com
`CpSoftReset=0` — porque escolhi o perfil sem `-EnableShaderDispatch` para não perturbar o anel
antes de medi-lo. Certo para o nível 1; errado para o firmware.

A telemetria daquelas corridas mostrava o PFP **parado em `PC=0x21B`**, e
`tools/package-bc250mcdm-lab.ps1` linha 49 já dizia por escrito: *"sem a receita F8b
(CpSoftReset=1) a carga do PFP assenta no laço 0x218..0x21B"*. O diagnóstico estava no
repositório antes de mim.

###### O que ainda NÃO foi provado

```
[GfxRing] BRINGUP nao executou: mapa do transporte falhou (wrong_state)
```

**A nossa transação não rodou.** Com `-EnableShaderDispatch`, o caminho do PFP deixa
`Core.MmioTransport` **mapeado**, e `Bc250MmioTopologyMapReadonly` recusa transporte não-vazio.
No nível 1, sem aquele caminho, o transporte estava livre — por isso o defeito só apareceu
agora.

**Classe A16**, a mesma que custou a corrida de 08/09 11:48. Eu conhecia o precedente e escolhi
o ponto de inserção sem verificar o estado do transporte ali.

**Corrigido offline:** o ensaio **reusa** o mapeamento existente e desmapeia **somente o que ele
próprio mapeou** — desmapear o mapeamento alheio quebraria o estágio que o criou. Travado por
portão, com duas mutações verificadas (M38 desmapear o alheio, M39 não checar o estado).

###### Estado preciso da B.1

| | |
|---|---|
| O anel gráfico executa PM4, com tripé completo | 🎯 **P0** |
| Configuração + readback + rollback pela nossa transação | 🎯 P0 (nível 1, 13:06) |
| A **nossa** transação desalta e liga o anel | ❌ não rodou — corrigido, falta a corrida |

##### B.1 — 🎯 **MEDIÇÃO VALIDADA NO SILÍCIO** (corrida 14/09 11:43)

Primeira leitura do anel gráfico na história do projeto.

```
[StartDevice] GfxRingPreflight=1 (present=1 compiled=1)
[GfxRing] PREFLIGHT OK: Reads=7 Writes=0 (transporte Writes=0)
[GfxRing] CP_RB0 COMO ENCONTRADO: CNTL=00A00000 BASE=FEDCBAEF RPTR=0 WPTR=0
                                  POLL_CNTL=00400000 ME_CNTL=15000000
[GfxRing] CONTRA O CATALOGO: lido=00A00000 catalogado=50000709 -> DIVERGE
```

`Writes=0` nos **dois** contadores — o do contrato e o do transporte MMIO, que é fonte
independente. A promessa de somente-leitura está **provada**, não afirmada.

**O anel gráfico está frio:** `RB_BUFSZ=0`, `RB_EXE=0`, ponteiros zerados, `BASE` com lixo, e
`CP_ME_CNTL=0x15000000` = `CE_HALT|PFP_HALT|ME_HALT` — as três micro-engines paradas. A placa
faz boot com o framebuffer do GOP e **nunca liga o motor gráfico**.

Estabilidade: zero TDR, `DEVICE_REMOVED`, `GFXHUB_FAULT`, `GPU_EXCEPTION`, BugCheck. Código 0.

###### Duas correções de registro que a medição forçou

1. **`0x50000709` não é o estado de boot.** Eu o tratava como âncora do POST — **refutado**. O
   catálogo o registra "observado em V3", ou seja, **depois** de o anel ter sido configurado. A
   constante chamava-se `..._OBSERVED_POST`: mentira embutida no nome. Renomeada para
   `..._CONFIGURED_REFERENCE`, o valor medido virou `..._MEASURED_BOOT`, e um portão proíbe a
   redefinição do nome antigo.
2. **O defeito do protótipo ficou explicado.** `0x00A00709` = `0x00A00000` (boot) `| 0x709`
   (tamanho). Quem escreveu leu o boot e somou o tamanho, sem nunca ligar `RB_EXE`.

###### Consequência: a B.1 ficou maior, não menor

Não há anel para "ajustar" — ele tem de ser construído do zero, **e as micro-engines precisam
ser liberadas**. `CP_ME_CNTL` sai de snapshot e vira alvo de escrita; ele **está catalogado**
(`0x086D8`). A parada de procedência do `APPLY` continua no `CP_RB0_ACTIVE`, sem fonte.

Novo no contrato: `Bc250GfxRingDecodeMeCntl` e `Bc250GfxRingMicroEnginesAreHalted` (qualquer
uma halted já impede execução — exigir as três deixaria passar o caso mais difícil de
diagnosticar), com contraponto por engine no teste.

###### Custo: uma corrida perdida antes desta, por defeito meu

A corrida 11:27 rodou com o tunable **desarmado**: a dica do dry-run do binder não propagava
`-EnableGfxRingPreflight`, e o operador executou o comando sugerido. O próprio arquivo tinha um
comentário da faixa C3.9 descrevendo essa exata armadilha. Corrigido, mais um **achado latente
pré-existente** (`-EnableD3D11ShaderTranslator`, com o mesmo defeito), e agora travado por
portão genérico: todo `[switch]$Enable*` declarado tem de aparecer na dica.

###### Achado colateral: a suíte offline dependia da máquina

Depois do bind, `build-umd` passou a reprovar **sem nenhuma mudança de código**.
`TestF5PmDrawEmissionOnNode1` **apagava** a variável de ambiente para exercitar o fail-closed, e
com isso a decisão caía em `HKLM` — que o bind alterou. Segunda ocorrência da classe (a faixa
C1 já a encontrara em outro ponto, docs/234).

Três defeitos no mesmo bloco, todos corrigidos: o leitor do tunable passou a tratar a variável
de ambiente como **autoritativa nos dois sentidos**; o teste **declara** `"0"` em vez de apagar;
e o bloco não definia `hRTCoreLayer`, o que só não aparecia porque a recusa nunca chegava a
acontecer. Mais duas asserções de contagem de `pfnSetErrorCb` que estavam erradas e apagavam a
diferença entre *"não recusou"* e *"recusou calado"*.

Hermeticidade verificada rodando a suíte com a variável em `"1"`, `"0"`, `"lixo"` e ausente —
`EXIT=0` nas quatro. **O braço do registro não foi testado**: escrever em `HKLM` exige elevação
que esta sessão não tem.

**O que falta na B.1:** **uma corrida**. O `PREFLIGHT` sozinho já a justifica — seria a
primeira medição do anel gráfico deste projeto. **NÃO VALIDADO EM HARDWARE.**

Comando da corrida (o build do candidato de laboratório exige aceite explícito, que é decisão
do dono do projeto):

```
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -EnableGfxRingPreflight ...
```

O critério está escrito antes da corrida, em `tools/arm-gfx-ring-preflight.ps1`, e **informa
nos dois desfechos**: se `CONTRA O CATÁLOGO` disser `CONFERE`, o `0x50000709` é estado estável
do POST e o plano do anel pode ser construído sobre ele; se disser `DIVERGE`, o estado depende
do caminho de boot e ler antes de escrever passa a ser obrigatório em toda corrida.

**Pendência que não custa corrida:** trazer `gc_10_1_0_offset.h` e `gc_10_1_0_enum.h` para
`inc/linux_ref/`. Fecha a âncora absoluta do bloco 3D, resolve o `0x16` e eleva a segunda
fonte dos blocos `CP ring GFX` e `MEC/HQD/MQD`. É download, não ensaio.

**Armadilha latente registrada:** o `CHECK` das suítes de contrato tem dois ramos, e o ramo
**offline** (`#else`) faz `return 1` na falha, que `main` converte em código de saída **0** —
isto é, falha vira sucesso. O build usa o ramo `BC250_KERNEL_MODE`, onde o comportamento está
correto, então nenhuma suíte está comprometida hoje. Mas quem compilar offline vê tudo passar.

#### Auditoria da Corrida 22:51:21 e Estado Real da F5

1. **❌ F5.0 — NÃO VALIDADA EM HARDWARE:**
   - O `Explorer.EXE` **não participou** da corrida 22:51. Os únicos processos que carregaram a UMD foram `dwm.exe` (3156), `dxgiadaptercache.exe` (5796) e `M365Copilot.exe` (9012). O volume total de alocações caiu de 1231 para 93.
   - O fato de a superfície 1024×832 do `M365Copilot.exe` ter saído `Primary=0` não discrimina a correção: na corrida 22:17, o `dwm.exe` já alocava essa mesma dimensão com `Primary=0`. As superfícies defeituosas eram exclusivamente do `Explorer.EXE`.
   - **Dívida declarada:** O ramo `PRIMARY_DESC INCOMPATIVEL RECUSADO` saiu 0 vezes. A recusa nomeada nunca foi exercida no silício $\rightarrow$ **NÃO VALIDADO EM HARDWARE**.
   - **Critério para fechar a F5.0:** Uma corrida física em que o `Explorer.EXE` carregue a UMD e tenha zero alocações recusadas.

2. **✅ F5.0b — Portão Endurecido Offline (8/8 Mutações):**
   - Corrigido o erro de categoria: portão de auditoria é estático/offline, nunca "validado no silício".
   - O portão original usava `require(test, ...)` sobre texto bruto, sendo derrotado por comentários (`/* ... */`, `// ...`) ou neutralização (`assert(1); // ...`).
   - Portão corrigido em `tools/audit-bc250mcdm-safety.py`: aplica `strip_c_comments` e restringe a verificação ao corpo balanceado de `TestF5PrimaryDerivation`, proibindo `assert(1)`, `assert(0)` e `if (0)`.
   - **8/8 mutações verificadas com sucesso em `tools/test-f5-audit-mutations.py`:**
     - M1 (religar derivação ampla no UMD) $\rightarrow$ EXIT=1 ✅
     - M2 (comentar `assert(resDwm.IsPrimary);` com `/* */`) $\rightarrow$ EXIT=1 ✅
     - M2b (apagar a asserção do DWM) $\rightarrow$ EXIT=1 ✅
     - M2c (neutralizar com `assert(1); // ...`) $\rightarrow$ EXIT=1 ✅
     - M2d (comentar com `// ...`) $\rightarrow$ EXIT=1 ✅
     - M3 (comentar asserção do Explorer com `/* */`) $\rightarrow$ EXIT=1 ✅
     - M3b (apagar asserção do Explorer) $\rightarrow$ EXIT=1 ✅
     - M4 (bypass condicional `if (0)`) $\rightarrow$ EXIT=1 ✅

3. **✅ O que a corrida 22:51 de fato fechou no silício (P0):**
   - **O Risco 7 não se materializou:** Estreitar a derivação de primariedade não quebrou a apresentação do `dwm.exe` nem reabriu a Parede A.
   - `PRIMARY_DESC VALIDO -> Primary=1`: 3 (as 3 superfícies de scanout do DWM em 2560x1080).
   - `Present LEAVE hr=0x00000000`: 127 contínuos, zero `E_FAIL`.
   - `dwm.exe` estável: `DestroyDevice = 0` durante 92 s contínuos.
   - KMD: 127 Presents no modelo Flip (`Flip=1, Blt=0`), 1:1 com a UMD, rotacionando MPO3 ciclicamente em VRAM real.
   - 217 draws atendidos por substituição SDMA 1:1.
   - Zero TDRs, zero BugChecks, zero falhas de kernel, fusível `CLEAN`.
   - Quadros pretos (`Spread NonZero=0/256`), resultado 100% coerente com a Parede B inteira.

4. **🎯 F5.0 VALIDADA NO SILÍCIO — Corrida 23:16:53 (Pacote Lab 3.177.1.0):**
   - **Participação do `Explorer.EXE`:** com o switch `-RestartExplorer` no binder, duas instâncias de `explorer.exe` carregaram a `bc250umd.dll`:
     - `explorer.exe` (pid 10700): 57 `CreateResource`, **zero recusados**, 1 superfície `1024x832` (`Size=3407872`) alocada com `Primary=0`.
     - `explorer.exe` (pid 8452, reiniciado): 233 `CreateResource`, **zero recusados**, 2 superfícies `1024x832` alocadas com `Primary=0`.
     - Total `Explorer.EXE`: **290 `CreateResource`, 0 FALHAS** (contra `0 OK / 142 FALHA` na corrida 22:17). A regressão de alocação do Explorer está **100% extinta**.
     - `explorer.exe` (pid 8452) executou **6 chamadas consecutivas de `Present`**, todas com `hr=0x00000000` (`S_OK`).
   - **Estabilidade do `dwm.exe` (pid 3156):**
     - 91 chamadas de `CreateResource`, **zero recusados**.
     - 3 superfícies de scanout em 2560x1080 (`PRIMARY_DESC VALIDO -> Primary=1`).
     - **33 Presents contínuos** com `hr=0x00000000` (`S_OK`), zero `E_FAIL`.
     - `DestroyDevice = 0` durante toda a janela de observação.
   - **Miniport KMD (`display-log-this-bind-FINAL.txt`):**
     - 33 Presents no modelo Flip (`Flip=1, Blt=0`), 1 Blt, 10 `SDMA=YES`.
     - 35 chamadas de `SetVidPnSourceAddress`, rotacionando ciclicamente entre `0xAAC000`, `0x4A1C000`, `0x54AC000`.
     - **Pixels (sem maquiagem):** distribuição física de amostras de 47× `0/256`, 4× `2/256`, 1× `3/256`, 2× `5/256`; 4 `BYTE_PROOF INCONCLUSIVO`. Quadro preto mantido, resultado 100% esperado com a Parede B inteira (`DrawIndexed: RECUSADO`).
     - > ❌ **Falso Marco 6 retratado:** A linha `[Present #5] *** DESKTOP REAL DETECTADO ***` disparou com 254 dos 256 dwords iguais a zero (2/256), `BYTE_PROOF INCONCLUSIVO` (4×), e Parede B intacta (`DrawIndexed: RECUSADO`). Trata-se de ruído em quadro preto, não de rasterização ou exibição de desktop real. Retirado do veredito.
     - Zero TDRs, zero BugChecks, fusível `CLEAN (0)`, rollback online automático do watchdog para `BasicDisplay` (Código 0).
   - **Dívida declarada mantida:** O ramo `PRIMARY_DESC INCOMPATIVEL RECUSADO` executou 0 vezes (o `Explorer.EXE` não envia `pPrimaryDesc` incompatível; simplesmente não envia descritor). Permanece **NÃO VALIDADO EM HARDWARE**.

5. **✅ F5.1 CONCLUÍDA OFFLINE — Tabela DXGI 1.6 Completa (22 Slots, 176 Bytes):**
   - Struct `DXGI1_6_1_DDI_BASE_FUNCTIONS` (22 slots, `sizeof == 176` bytes verificado).
   - Todas as DDIs estendidas implementadas com contratos fail-closed: `Bc250DxgiPresent1`, `Bc250DxgiBlt1`, `Bc250DxgiGetMultiplaneOverlayCaps` (`MaxPlanes=1`, `NumCapabilityGroups=0` — declaração honesta sem MPO de hardware fake), `Bc250DxgiReclaimResources1`, etc.
   - Conexão em `Bc250Umd10CreateDevice` via `pDXGIDDIBaseFunctions6_1`.
   - Portão `audit_dxgi_1_6_table_complete` em `tools/audit-bc250mcdm-safety.py`.
   - **7/7 mutações detectadas em `tools/test-f5-1-dxgi-mutations.py` (EXIT=0).**

6. **❌ F5.2 NÃO FECHADA OFFLINE — Parede B: Emissão PM4 3D Travada sob Tunable Fail-Closed:**
   - **Auditoria Externa:** F5.2 auditada com 4 bloqueadores fatais identificados:
     1. *Fila errada:* Pacotes de contexto de render target (`SET_CONTEXT_REG` 0x69) e `DRAW_INDEX_AUTO` (0x2D) emitidos na fila de compute (Node 1 / MEC / HQD / MQD). MEC aceita `DISPATCH_DIRECT`; não aceita pacotes gráficos do anel ME/PFP (`CP_RB0_*`). A etapa B.1 não foi feita.
     2. *Não há shader:* Zero registradores `SPI_SHADER*`, `PGM_LO/HI`, `USER_DATA`; ausência de pipeline DXBC -> SPIR-V -> ACO (etapa B.2 pendente).
     3. *Não há geometria de entrada:* Zero vertex buffer, input layout e descritores vinculados (etapa B.3 pendente).
     4. *Proveniência dos registradores:* Offsets GFX10 (`0xA318`, etc.) sem comprovação de proveniência P1 em `docs/03` e `fmtIndex=0x16` como hipótese (H) (etapa B.4 pendente).
   - **Inversão de Precedência e Trava da Lei 4:**
     - `Bc250UmdEmitPm4Draw(...) -> TRUE` causaria falso sucesso silencioso, suprimindo o log honesto `DrawIndexed: RECUSADO` sem que haja rasterização real.
     - A emissão PM4 foi isolada atrás do tunable fail-closed `D3D11ExperimentalPm4Draw` (default 0 / desarmado na imagem de produção).
     - Com o tunable desarmado, o ramo honesto `Bc250UmdRejectUnbackedDraw` (`Draw: RECUSADO: compilador/shader-state/draw PM4 ausente`) permanece como default estrito nas quatro DDIs de Draw.
     - Ferramenta de inspeção/controle criada: `tools/arm-d3d11-experimental-pm4-draw.ps1` (exige `-Arm -IAcceptPm4DrawIsExperimentalWithoutShaders`).
   - **Roadmap da Parede B (Etapas Necessárias para Fechamento Real):**
     - `B.1`: Anel gráfico real (ME/PFP, `CP_RB0_*`) ou Node 1 reconfigurado/redeclarado para suportar contexto gráfico.
     - `B.2`: Pipeline de shaders DXBC -> SPIR-V -> ACO carregados em VRAM real (`SPI_SHADER_PGM_*`).
     - `B.3`: Descritores, vertex buffer, input layout, transformações de viewport.
     - `B.4`: `DRAW_INDEX_AUTO` + catalogação de registradores em `docs/03` com proveniência P1.
     - `B.5`: Prova de bytes física (`BYTE_PROOF PASS`) de triângulo de gabarito rasterizado sobre poison.
   - **Portão e Mutações:**
     - Portão `audit_f5_2_pm4_draw_pipeline` em `tools/audit-bc250mcdm-safety.py` atualizado para exigir o tunable fail-closed e a verificação no teste de contrato.
     - **11/11 mutações detectadas em `tools/test-f5-2-pm4-draw-mutations.py` (EXIT=0).**

---

## 6. Riscos declarados

1. **Nenhum dos três candidatos pode ser a causa.** `0x80004005` é genérico e nenhuma das três
   corridas o determinou. A F4 é desenhada para informar em qualquer desfecho, não para vencer.
2. **A F0 deixa duas DDIs recusando** até a F2 aterrissar. É deliberado — falhar visível é melhor
   que suceder por reimplementação (precedente B3). Não há corrida entre F0 e F4.
3. **Corrigir a Parede A não acende a tela.** A Parede B segue inteira.
4. **A rota de falha do Present (3.2) nunca executou.** `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE`
   substituiu o `STATUS_SUCCESS` incondicional, mas nenhuma corrida exercitou esse ramo. O
   precedente do `0x10E` diz que status de erro nesta DDI é caminho sensível. **NÃO VALIDADO EM
   HARDWARE.**
5. **Se a tela acender com `-EnableD3D11DrawBltSubstitution`, isso NÃO é rasterização.** É cópia
   1:1 no SDMA, e já foi rotulada assim no código. Tratá-la como pixel de pipeline 3D seria a
   sexta ocorrência de falso marco neste projeto.
6. **A F5.0 pode não ser suficiente.** Que a geometria incompatível seja a causa do `E_INVALIDARG`
   é **H**, não P0 — sustentada pela divisão perfeita `dwm` vs `Explorer` e pela progressão
   4 -> 69 -> 148, mas o `hr` não nomeia o motivo. Se a recusa persistir com a derivação
   estreitada, o suspeito seguinte é o `VidPnSourceId` fixo em 0.
7. **Estreitar demais também é defeito.** Se a F5.0 deixar de declarar primária a superfície que o
   `dwm.exe` de fato apresenta, o `Present` volta a falhar e a Parede A reabre. Por isso o
   contraponto `dwm.exe = 6 OK` é parte do critério de fechamento, não um extra.

## 7. Histórico

| Data | Evento |
|---|---|
| 2026-09-13 16:28 | baseline; handoff anterior refutado — ver [`docs/266`](docs/266_AUDITORIA_2026-09-13_A_PAREDE_E_A_PRIMARIA_NAO_O_SCANOUT.md) |
| 2026-09-13 17:18 | vínculo de recurso + I1/I2/I3; `DdiPresent` alcançada 6× — ver [`docs/267`](docs/267_CORRIDA_2026-09-13_17-18_PRIMARIA_OK_E_O_CONTEXTO_DE_APRESENTACAO.md) |
| 2026-09-13 18:03 | contexto de apresentação no Node 0; contexto **eliminado** como suspeito |
| 2026-09-13 | dossiê para auditoria externa — [`docs/268`](docs/268_DOSSIE_AUDITORIA_EXTERNA_2026-09-13_PRESENT_E_FAIL.md) |
| 2026-09-13 | quatro auditorias externas; achados verificados; este plano aberto |
| 2026-09-13 | F0, F1, F2 concluídas offline; A5 e A6 refutados por não existirem no WDK |
| 2026-09-13 20:xx | F3 executada por outra IA; **verificada de forma independente** e confirmada |
| 2026-09-13 21:23 | F4: `0x139` eliminado e **primeiro `Present` com `S_OK`**. Enquadramento do relatório corrigido (R7, R8) — ver [`docs/270`](docs/270_RELATORIO_TECNICO_F3_F4_VALIDACAO_EXTERNA.md) |
| 2026-09-13 22:17 | F4 **CONCLUÍDA**: 222 Presents com `S_OK` na UMD, zero `E_FAIL`, device estável (`DestroyDevice = 0`), 260 Presents no KMD com rotação MPO3. **Parede A fechada no silício** |
| 2026-09-13 | verificação independente da 22:17 pelos três critérios + duas checagens de consistência; **regressão da primária larga descoberta** e atribuição do relatório corrigida (R9, R10); F5.0 e F5.0b abertas |
| 2026-09-13 22:51 | Corrida 22:51: Risco 7 refutado (Parede A segue fechada, 127 Presents `S_OK`, `DestroyDevice = 0`, 127 Flips MPO3 no KMD). **F5.0 NÃO VALIDADA EM HARDWARE**: `Explorer.EXE` ausente na corrida; ramo de recusa nomeada nunca executou. Meta-portão da F5.0b derrotado por comentários na auditoria do operador |
| 2026-09-13 | F5.0b endurecida offline: `strip_c_comments` + análise do corpo de `TestF5PrimaryDerivation`; 8/8 mutações verificadas (derivação larga, deleção, comentários `/* */` e `//`, neutralização `assert(1)` e bypass `if(0)`) |
| 2026-09-13 23:16 | Corrida 23:16:53: **F5.0 VALIDADA NO SILÍCIO**. `Explorer.EXE` reiniciado no bind (`-RestartExplorer`); 290 alocações no Explorer (3 de `1024x832` com `Primary=0`: 1 no pid 10700 e 2 no 8452), **ZERO `CreateResource RECUSADO`** (contra 142 falhas em 22:17). `Explorer.EXE` executou 6 Presents `S_OK`. `dwm.exe` com 33 Presents `S_OK`, `DestroyDevice=0`. Quadros pretos mantidos (amostras predominantemente 0/256 com Parede B inteira). Dívida mantida: `PRIMARY_DESC INCOMPATIVEL RECUSADO` (0 vezes no silício, **NÃO VALIDADO EM HARDWARE**). |
