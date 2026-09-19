# Briefing — Fase F3: três violações da Lei 4 que envenenam toda medição

> **Para quem vai executar.** Você é a próxima IA a trabalhar neste repositório.
> **Repositório:** `C:\Users\Daniel\Desktop\NamixWDDM`
> **Contexto completo:** [`PLANO_PRESENT_E_FAIL.md`](../PLANO_PRESENT_E_FAIL.md) (raiz do projeto).
> **Escopo desta tarefa:** apenas a **F3**. As fases F0, F1 e F2 já estão concluídas.
> **Modo:** OFFLINE. Não há corrida em hardware nesta tarefa.

---

## 1. O que é este projeto, em um parágrafo

Driver WDDM completo escrito do zero para a AMD BC-250, para rodar jogos no Windows 10 com GPU
real — sem WARP, sem fallback de software. O display é herdado do UEFI GOP: copiamos pixels
para o framebuffer que o firmware deixou ligado, via SDMA. Não programamos o DCN.

**A regra que governa tudo (Lei 4):** nunca declarar sucesso sem backend, nunca simular
execução pela CPU, nunca tratar "a função retornou" como "a GPU fez". Se algo não está
implementado, o driver **recusa e diz por quê** — falhar visível é melhor que suceder por
reimplementação.

---

## 2. Por que a F3 existe

Estamos caçando a causa de o `Present` do DWM falhar com `E_FAIL`. As fases anteriores
corrigiram o contrato de apresentação. **A F3 não é sobre o `Present`** — é sobre três lugares
onde o driver mente, e cujas mentiras **tornam impossível provar qualquer coisa na próxima
corrida**.

Concretamente: hoje, toda superfície que o driver examina no silício amostra **zero**. Nós
vínhamos atribuindo isso à ausência de pipeline gráfico. Uma auditoria externa mostrou que há
uma segunda causa, e ela é nossa: **o `Clear` nunca chega à GPU**. Enquanto isso for verdade,
nenhuma corrida consegue distinguir "não há rasterização" de "apagamos a evidência".

---

## 3. Os três defeitos

### 3.1 — `ClearRenderTargetView` pinta só o shadow de CPU

**Onde:** `driver/umd/bc250_d3d_umd.c`, função `Bc250Umd10ClearRenderTargetView` (~linha 1407).

**O que faz hoje:** aloca/usa `res->pCpuShadow` (memória de CPU obtida por `calloc`) e escreve
o pixel de cor linha a linha **nessa cópia**. A alocação de GPU correspondente não é tocada.
Depois a função loga e retorna.

**Por que é grave:** o DWM limpa o render target antes de compor. O KMD depois lê a superfície
**na VRAM** para provar o caminho de pixel, e encontra zeros — porque o `Clear` nunca saiu da
CPU. Todo `BYTE_PROOF` da corrida sai `INCONCLUSIVO (FRAME EM BRANCO)`.

**O que fazer — duas saídas aceitáveis, escolha com justificativa escrita:**

- **(a) Escrever de verdade na GPU.** Existe caminho: `Bc250UmdBltGpu` / o escape de composição
  do KMD (`BC250_ESCAPE_OP_COMPOSITE_BLT`) já executam no SDMA e estão provados no silício.
  Um preenchimento de cor sólida é um `fill`, não um `blt` — se não houver caminho de fill
  pronto, **diga isso** em vez de improvisar.
- **(b) Recusar honestamente.** Logar que o `Clear` não tem backend de GPU e não escrever no
  shadow como se fosse a superfície.

**O que NÃO fazer:** manter a escrita no shadow e chamá-la de `Clear`. É a definição de
fallback de software disfarçado.

**Cuidado importante:** o shadow existe para outra coisa — `ResourceMap` devolve
`res->pCpuShadow` ao runtime. Não o remova; o problema é o `Clear` afirmar ter limpado a
superfície de GPU quando limpou outra memória.

---

### 3.2 — O KMD suprime o blt e devolve `STATUS_SUCCESS` assim mesmo

**Onde:** `driver/wdk/bc250_display_ddi.c`, dentro de `Bc250DxgkDdiPresent`, ~linhas 3060–3070.

**O que faz hoje:** quando a superfície de origem amostra 100% zerada, o driver **não copia** e
preserva o scanout do GOP (log: `Blt preservado ... GOP scanout mantido intacto`). Logo abaixo,
`presentStatus = STATUS_SUCCESS`.

**Por que é grave, em dois sentidos:**

1. Declara ao `dxgkrnl` que apresentou um quadro que não apresentou.
2. **Torna impossível exibir um quadro preto legítimo.** Uma tela preta de verdade — um app que
   limpa para preto — é indistinguível de "não tenho nada para mostrar".

**O que fazer:** separar as duas situações. Preservar o scanout pode continuar sendo a decisão
(há razão histórica: evita apagar a tela durante o bring-up), mas o **status devolvido e o log
precisam dizer o que aconteceu**. Se a supressão for mantida, ela tem de ser explícita e
contável, e um quadro preto legítimo precisa de um caminho que o apresente.

**Contexto que você precisa conhecer antes de mexer aqui:** há um comentário no arquivo
explicando que devolver `STATUS_IO_DEVICE_ERROR` em `DxgkDdiPresent` no caminho do DWM já
causou `BugCheck 0x10E`. **Não troque `SUCCESS` por erro sem ler esse comentário.** A solução
provavelmente não é falhar a DDI — é parar de fingir que copiou.

---

### 3.3 — Residência mentida em dois sentidos

**(a) `QueryResourceResidency`** — `driver/umd/bc250_d3d_umd.c` ~linha 3669: devolve
`DXGI_DDI_RESIDENCY_EVICTED_TO_DISK` para qualquer alocação não-residente. Isso afirma ao
runtime que há conteúdo paginado **em disco**, algo que este driver nunca produz (não existe
caminho de offer/reclaim).

> ⚠️ **Uma das auditorias externas propôs trocar por `DXGI_DDI_RESIDENCY_NOT_RESIDENT`.
> Esse valor NÃO EXISTE.** O enum tem exatamente três membros (`dxgiddi.h:41`):
> `FULLY_RESIDENT`, `RESIDENT_IN_SHARED_MEMORY`, `EVICTED_TO_DISK`. A proposta não compila.
>
> O enum não consegue expressar "não residente". **Decida qual dos três é o menos falso para
> uma alocação nossa (que vive em VRAM, segmento 1) e escreva a justificativa no código.**
> Se a conclusão honesta for que nenhum serve, declare a dívida — é uma resposta aceitável.

**(b) `MakeResident` marca residente em `E_PENDING`** — `driver/umd/bc250_wddm_winsys.c`
~linha 843: `hr == E_PENDING` (0x8000000A) é aceito e `Allocation->Resident = TRUE` é escrito
sem esperar a paging fence. Medido na última corrida: **2992 de 2992** `MakeResident` voltaram
`E_PENDING`. "Residente" pode ser falso no microssegundo em que importa.

Aceitar `E_PENDING` está **correto** (é assíncrono por contrato, e já houve uma correção
específica para isso). O que falta é não confundir "pedido aceito" com "já residente".

**(c) `Created = TRUE` mesmo com falha** — `driver/umd/bc250_d3d_umd.c` ~linhas 1016–1018:
`Bc250WinsysMapAllocation` e `Bc250WinsysMakeResident` têm o retorno descartado com `(void)`, e
`res->Created = TRUE` é escrito em seguida. Um recurso cuja GPUVA não foi mapeada é declarado
pronto.

---

## 4. Regras de trabalho deste repositório

Elas não são opcionais. Trabalho que não as siga é rejeitado.

1. **Verifique antes de implementar.** Dois achados externos desta mesma auditoria eram
   **falsos** — pediam campos que não existem no WDK (`DXGK_ALLOCATIONINFO.Pitch` e
   `Flags.Primary`). Confira o header antes de escrever código contra uma API.
2. **Todo conserto vem com teste.** Suítes em `driver/tests/`. O teste tem de **reprovar** se o
   defeito voltar.
3. **Todo conserto vem com portão de auditoria**, em `tools/audit-bc250mcdm-safety.py`.
4. **Verifique cada portão por mutação:** quebre o conserto de propósito e confirme que algo
   reprova — e que reprova **pelo invariante certo**, não por acidente. Se reprovar pelo motivo
   errado, o portão é fraco; conserte o portão.
5. **Cuidado com o padrão "o gate congelava a mentira".** Já aconteceu **oito vezes** neste
   projeto: um teste ou auditoria *exigia* o comportamento defeituoso, impedindo a correção.
   Antes de mexer, cheque se algum teste exige a mentira que você vai remover. Se exigir,
   corrija o teste primeiro e adicione um meta-portão que impeça o recongelamento.
6. **Nada de `S_OK`/`STATUS_SUCCESS` sem backend.** Recusa com motivo nomeado é sempre
   preferível.
7. **Escreva o porquê no código.** Comentários deste repositório explicam a medição que motivou
   a mudança, não o que a linha faz.

### Como validar

```
driver\umd\build-umd.bat            (precisa sair EXIT=0)
driver\wdk\build-contract.bat       (precisa sair EXIT=0)
python tools\audit-bc250mcdm-safety.py
python tools\audit-mcdm-ddi-matrix.py
python tools\audit-ddi-noop-callbacks.py
```

**Não empacote nem instale nada.** A F3 termina offline.

---

## 5. O que eu espero depois que você terminar

### Em código

| # | Resultado esperado |
|---|---|
| 3.1 | `Clear` escreve na GPU **ou** recusa com motivo nomeado. Em nenhum caso ele pinta o shadow e chama isso de limpeza da superfície. |
| 3.2 | Supressão do blt e apresentação real são **distinguíveis** no status e no log. Quadro preto legítimo tem caminho. |
| 3.3 | Residência reporta o que foi medido; `E_PENDING` não vira "residente" sem qualificação; falha de `Map`/`MakeResident` não vira `Created = TRUE`. |

### Em provas

- `build-umd EXIT=0`, `build-contract EXIT=0`, três auditorias PASS.
- Pelo menos um teste novo por item, que reprove se o defeito voltar.
- Pelo menos um portão novo, **verificado por mutação**.
- Se algum teste existente exigia a mentira: corrigido, com meta-portão anti-recongelamento.

### Na próxima corrida (não é você quem roda)

O efeito que a F3 deve destravar:

- Se o `Clear` passar a escrever na GPU, as superfícies deixam de amostrar `NonZero=0/256`, e
  `BYTE_PROOF` passa a poder dar um veredito — hoje ele só consegue dizer
  `INCONCLUSIVO (FRAME EM BRANCO)`, que é "zero contra zero não prova pixel".
- Isso **não acende a tela**, e não deve. A parede de rasterização (sem pipeline gráfico, sem
  `Draw` PM4) continua inteira. **Tela preta segue sendo o resultado correto.**
- O que muda é que passa a ser possível **distinguir** as duas causas do preto: "não há
  rasterização" e "nós apagamos a evidência". Hoje elas são indistinguíveis, e é exatamente
  isso que a F3 conserta.

### O que eu quero no seu relatório final

1. O que você mudou, por item, com arquivo e linha.
2. **Para 3.1: qual saída escolheu (escrever na GPU ou recusar) e por quê.**
3. **Para 3.3(a): qual valor de residência escolheu e a justificativa** — ou a declaração de que
   nenhum serve.
4. As mutações que você rodou e o que reprovou em cada uma.
5. **O que você não conseguiu resolver.** Diga explicitamente. Neste projeto, chute rotulado
   como conclusão é pior que silêncio.

---

## 6. Uma última coisa

Se ao ler o código você concluir que algum destes três defeitos **não é um defeito** — que a
minha leitura está errada — diga isso e mostre a evidência. Já aconteceu duas vezes nesta
mesma investigação, e das duas vezes a correção do registro valeu mais que a implementação.
