# Plano de correção — `Present` do DWM falha com `E_FAIL` (`0x80004005`)

> **Documento vivo de rastreabilidade.** Atualize o estado de cada item ao concluí-lo.
> **Aberto em:** 2026-09-13 · **Origem:** quatro auditorias externas sobre
> [`docs/268`](docs/268_DOSSIE_AUDITORIA_EXTERNA_2026-09-13_PRESENT_E_FAIL.md)
> **Última corrida:** `18:03:43`, pacote lab 3.174.1.0 — 96/96 `Present` com `hr=0x80004005`.

---

## 1. O problema, em uma frase

O `dwm.exe` chama `Present`; o runtime devolve `E_FAIL` **sincronamente**, no mesmo
milissegundo, antes de qualquer submissão; o DWM entra em laço de device-lost a ~1 Hz e nenhum
quadro chega ao scanout.

## 2. Estado das duas paredes

| Parede | O quê | Estado |
|---|---|---|
| **A — Apresentação** | o `Present` é recusado pelo runtime | **em ataque, este plano** |
| **B — Rasterização** | não existe pipeline gráfico (descriptors, render target, anel gráfico, `Draw` PM4) | inteira; obra longa, em paralelo |

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
| 3.1 | `Clear` escreve na GPU ou recusa honestamente | A7 | ⬜ |
| 3.2 | KMD deixa de devolver `SUCCESS` quando suprime o blt | A9 | ⬜ |
| 3.3 | Residência honesta; resolver a lacuna do enum (ver R5) e propagar falha de `MakeResident` | A8 | ⬜ |

> **Por que antes da corrida:** com `Clear` só no shadow, **toda** prova de dado nasce
> zero-contra-zero. Sem 3.1 a corrida não prova pixel nem se o `Present` passar.

### F4 — Corrida única, com atribuição por defeito

| Sinal | Onde | O que diz |
|---|---|---|
| `DXGI Present … Flags=` muda de `0x2` para `0x1` | UMD | `NO_SCANOUT` (2.2) foi respeitado — o DXGI desistiu do Flip |
| `[DescribeAlloc] ENTER` aparece | KMD | a DDI **era** chamada; o `LEAVE` diz se a recusa era o portão (2.1) |
| `SetDisplayMode LEAVE cb: hr=` | UMD | (2.3) foi exercitado, e com que resultado |
| `DXGI Present LEAVE cb: hr=` ≠ `0x80004005` | UMD | o `E_FAIL` caiu — e os três acima dizem por quê |

**Se o `E_FAIL` persistir idêntico**, os três primeiros sinais ainda eliminam ou confirmam cada
candidato **separadamente**. Nenhuma corrida é desperdiçada.

### F5 — Depois

| # | Item | Achado | Estado |
|---|---|---|---|
| 5.1 | Tabela DXGI 1.6 (`pfnPresent1`, MPO) — mina terrestre, não causa atual | A11 | ⬜ |
| 5.2 | Parede B: anel gráfico + `Draw` PM4 | — | ⬜ |

---

## 6. Riscos declarados

1. **Nenhum dos três candidatos pode ser a causa.** `0x80004005` é genérico e nenhuma das três
   corridas o determinou. A F4 é desenhada para informar em qualquer desfecho, não para vencer.
2. **A F0 deixa duas DDIs recusando** até a F2 aterrissar. É deliberado — falhar visível é melhor
   que suceder por reimplementação (precedente B3). Não há corrida entre F0 e F4.
3. **Corrigir a Parede A não acende a tela.** A Parede B segue inteira.

## 7. Histórico

| Data | Evento |
|---|---|
| 2026-09-13 16:28 | baseline; handoff anterior refutado — ver [`docs/266`](docs/266_AUDITORIA_2026-09-13_A_PAREDE_E_A_PRIMARIA_NAO_O_SCANOUT.md) |
| 2026-09-13 17:18 | vínculo de recurso + I1/I2/I3; `DdiPresent` alcançada 6× — ver [`docs/267`](docs/267_CORRIDA_2026-09-13_17-18_PRIMARIA_OK_E_O_CONTEXTO_DE_APRESENTACAO.md) |
| 2026-09-13 18:03 | contexto de apresentação no Node 0; contexto **eliminado** como suspeito |
| 2026-09-13 | dossiê para auditoria externa — [`docs/268`](docs/268_DOSSIE_AUDITORIA_EXTERNA_2026-09-13_PRESENT_E_FAIL.md) |
| 2026-09-13 | quatro auditorias externas; achados verificados; este plano aberto |
