# 293 — Critério e resultado da Corrida 39 — A textura chega ao shader?

> **Escrito em 19/09/2026, ANTES do ensaio.** Candidato lab **3.197.1.0**, sonda de onda **ARMADA**.
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3) | Windows 10 Pro 22H2 build 19045
> **Pergunta única:** *o `dwm.exe` consegue vincular seus descriptors e completar um draw com textura?*
> **Anterior:** [docs/290](290_CRITERIO_CORRIDA_36_ONDE_A_ONDA_PARA.md) — Corrida 38 fechou com o anel
> concluindo submissão pela primeira vez, mas com os draws recusados no `binding index=2`.

---

## 1. De onde viemos

A Corrida 38 (15:35:31, lab 3.196.1.0) deu o primeiro avanço funcional da sequência: **o anel
gráfico concluiu submissão**, três vezes, com `RPTR == WPTR` e fence escrita pela GPU sobre poison,
e **zero TDR** — contra `Perna1=NAO_CONVERGIU` (RPTR travado em 24 de 32) e um a cinco TDRs em
todas as corridas de 34 a 37.

Mas não foi cura: os draws do `dwm.exe` foram **recusados antes de chegar à GPU**
(`BOUND=0 RECUSADO=2 draws=0`), no `binding index=2`. O hang foi evitado, não corrigido.

**Tela preta continua sendo o resultado esperado.** O harness não lê pixel de volta (Lei 2), e
nada nesta faixa prova rasterização.

---

## 2. O que mudou nesta faixa

| # | Mudança | Onde |
|---|---|---|
| 1 | 17 recusas de descriptor passam a **nomear** a condição | `bc250_umd_descriptors.c` |
| 2 | Upload de pixel para `VkImage` — `vkCmdCopyBufferToImage` **nunca era chamado** | `bc250_umd_resource_vk.c` |
| 3 | `memcpy` selvagem de 1,4 GB fechado na cópia entre recursos | `bc250_d3d_umd.c` |

### 2.1 O buraco da textura, medido

`vkCmdCopyBufferToImage` não existia em nenhum ponto da UMD. A `VkImage` nascia vazia e o
`VkLayout` ficava preso em `UNDEFINED`, porque só os caminhos de render target o alteravam — e o
descriptor recusa `UNDEFINED`. Essa era a causa do `binding index=2`.

Corrigir só o layout seria pior que não corrigir: o descriptor aceitaria a imagem e o shader
amostraria memória não inicializada. Por isso o conteúdo vem primeiro, e **"tem conteúdo" é
derivado do próprio `VkLayout`** — uma fonte de verdade, não duas que podem divergir:

- `SHADER_READ_ONLY_OPTIMAL` — só `Bc250UmdVkUploadImageData` põe, e só **depois da fence**;
- `COLOR_ATTACHMENT_OPTIMAL` — só quem desenhou no alvo põe;
- qualquer outro — recusa nomeada.

**Escopo declarado:** subrecurso 0 (mip 0, camada 0), e só quando o D3D entregou `pInitialDataUP`
com `SysMemPitch` não-zero. View que peça mip ou camada além disso é recusada por
`SRV_ALEM_DO_SUBRECURSO_CARREGADO`. Pitch zero recusa em vez de adivinhar o tamanho da origem.

### 2.2 O `memcpy` de 1,4 GB, que não estava no plano

`Bc250Umd10ResourceCopyRegion` lia `TotalByteSize` e `pCpuShadow` sem nunca conferir se o recurso
existia. Medido com `cdb`:

```
rcx = rdx = 0x00007ffd58bb5b40    (origem e destino iguais)
r8  = 0x58bb34e0                  (1,4 GB)
```

O comprimento carregava o padrão alto do próprio ponteiro — campo lido de memória não
inicializada. A origem era um `BC250_UMD_RESOURCE` de pilha nunca inicializado, entregue pelo
teste a `ResourceResolveSubresource`.

**A mina já estava armada há tempo.** Só foi pisada porque acrescentar um campo à struct mudou
*qual* lixo caía em `TotalByteSize`. Fechado nas duas pontas: o produto exige `Created` dos dois
lados e recusa falando; o teste para de fabricar a entrada inválida.

---

## 3. Critério, escrito antes — quatro desfechos

### Desfecho A — A TEXTURA CHEGA E O DRAW COMPLETA

```
[VK_RES] UploadImageData SUCESSO: WxH, N bytes em mip0/camada0
[VK_DESCRIPTOR] BOUND: bindings=N (CB=.. SRV=.. SAMPLER=..)
```

e no KMD, submissões GFX **maiores** que as 592–608 DW da Corrida 38, com `Perna1=REGISTRADOR`.
O draw com textura chegou à GPU e voltou. **É o melhor desfecho possível sem pixel conferido**, e
ainda assim não prova rasterização — prova que o trabalho completa.

### Desfecho B — RECUSA COM NOME (o mais provável, e é progresso)

```
[VK_DESCRIPTOR] RECUSADO: binding index=N motivo=<NOME>
```

Agora o motivo é uma condição, não um agregado de 17. O nome diz para onde ir:

| Motivo | Significa |
|---|---|
| `SRV_SEM_CONTEUDO_NUNCA_ESCRITO` | a textura não recebeu upload — ver se o DWM passa `pInitialDataUP` |
| `SRV_ALEM_DO_SUBRECURSO_CARREGADO` | o DWM quer mip/array; o escopo de subrecurso 0 é estreito demais |
| `SRV_FORMATO_OU_SUBRECURSO_INVALIDO` | formato DXGI sem mapeamento Vulkan |
| `SAMPLER_MODO_NAO_SUPORTADO` | filtro/anisotropia/address fora do implementado |
| `SRV_EM_FEEDBACK_LOOP_COM_RTV` | o DWM amostra o próprio alvo — exige outra estratégia |
| `CB_*` | regressão: o constant buffer já funcionava na Corrida 38 |

### Desfecho C — VOLTA O TDR

Se o draw com textura chegar à GPU e o anel travar de novo, a sonda de onda está **armada** e
responde onde: `[TDR_GFX_WAVEPC]` com `PC`, `IB_STS`, `INST_DW0`, `TRAPSTS` e `ValidMask`.
Na Corrida 37 esse caminho já entregou `FATAL_HALT` + `XNACK_ERROR` + `EXCP` bit 8.

### Desfecho D — NADA MUDA

`RECUSADO` com o mesmo perfil da Corrida 38 e nenhum `UploadImageData`. Então o DWM não cria
textura por `pInitialDataUP`, e o caminho de upload precisa cobrir `UpdateSubresource`/`Map`.

---

## 4. Medições de controle

| Controle | Valor da Corrida 38 |
|---|---|
| Submissões GFX concluídas | 3/3, `Perna1=REGISTRADOR` |
| TDR | zero |
| `GFXHUB_FAULT` | zero |
| BugCheck | zero |
| Node 0 SDMA | 217 `TRIPE_PROVADO` |

**Regressão em qualquer um manda no veredito** — em especial, se as submissões GFX voltarem a
`NAO_CONVERGIU`, esta faixa piorou algo.

---

## 5. O que esta corrida NÃO responde

- **Não acende a tela.** O harness não lê pixel de volta; conta Presents aceitos (Lei 2).
- **Não prova que a textura tem o conteúdo certo.** Prova que *algum* conteúdo foi copiado e que a
  fence voltou. Conferir os texels exige leitura de volta, que não existe.
- **Não cobre mip chain nem array.** Escopo declarado; view além disso é recusada por nome.
- **Não cobre `UpdateSubresource`.** Só `pInitialDataUP` na criação.

---

## 6. Pacote — 3.197.1.0, assinado, auditado, NÃO INSTALADO

`MCDM_LAB_BRINGUP` · `DISPLAY_RENDER_LAB` · WDDM 2.7 · P1 PERSISTENT SUBMISSION · schema 135 ·
**GfxWaveProbe=1 (ARMADO)**

| Artefato | SHA-256 |
|---|---|
| SYS (pacote) | `749BA458F5F235B702C25204E17DF0DC85E98C9D6F0B5DD1F8E88926A7875041` |
| UMD (pacote) | `F75DD6DE81364B121C2EA59AA476F8AD7831AFC748EEA370112B251171A3EE78` |
| INF | `36AFA568038663DC77414EE31EB17C48CA975334FE098B12F6BD89924428C06F` |
| CAT | `AF9C96CFC8C7CD6E1F2AC586E0C4EE539E714370C6D3095785F2582A77A972BA` |
| `vulkan_radeon.dll` | `7837c96027e5e56233fa16735cbe22a301a62f61d1e0075ad833efddeefe5868` |

**O SYS é byte a byte o mesmo do 3.196.1.0, e isso é correto:** nenhuma fonte do KMD mudou nesta
faixa. Toda a mudança está na UMD, cujo hash saiu de `F82141CA…` para `F75DD6DE…`. Por isso o
bring-up destrutivo não precisou ser refeito — verificado por `find -newer` contra a imagem de
laboratório de 15:28.

**Conferido por string na UMD assinada:** `motivo=%s`, `SRV_SEM_CONTEUDO_NUNCA_ESCRITO`,
`SRV_ALEM_DO_SUBRECURSO_CARREGADO`, `SAMPLER_MODO_NAO_SUPORTADO`, `UploadImageData SUCESSO`,
`RowPitch=0`, `ResourceCopyRegion RECUSADO: recurso nao criado`, `VK_DESCRIPTOR] BOUND`.
No SYS, `TDR_GFX_WAVEPC` e `reposto em broadcast antes do soft-reset` seguem presentes.

`audit-bc250mcdm-lab-package.ps1`: **PASS**.

Antes do bind: `tools\inspect-boot-fuse.ps1` (e `-Reset` se `TRIPPED`) e **boot frio obrigatório**.
Recuperação: `tools\BC250_RECOVERY.cmd` elevado.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**

---

## 7. RESULTADO — Corrida 39 (19/09 16:20:14, lab 3.197.1.0, SYS `749BA458`)

**Desfecho B**, pela assinatura, sem ambiguidade:

```
[VK_DESCRIPTOR] RECUSADO: binding index=2 motivo=SRV_AUSENTE_OU_SEM_IMAGEM
```

**Evidência:** `docs/evidence/2026-09-19_16-20-14-DESKTOP-HQAG6T4-lab-binding/`.
A pasta declarou-se incompleta (`complete_at_capture: false`, 868.164 de 869.150 bytes) e a cauda
foi resgatada pelo offset do baseline: `display-log-this-bind-RECUPERADO.txt`, 884.034 bytes,
sha256 `7BAF856D78F0052DEEAA6A3A11DCD19ACA877F03C756B773DBDC116440718ECB`.

### 7.1 O que a corrida entregou (P0)

**Estabilidade, e é o maior resultado da série:**

| | Corrida 38 | Corrida 39 |
|---|---|---|
| Submissões GFX | 3 | **118** |
| Maior IB | 608 DW | **20.208 DW** (~80 KB), 65 vezes |
| `Perna1=NAO_CONVERGIU` | 0 | **0** |
| TDR | 0 | **0** |

O IB que travava nas Corridas 34–37 tinha 1.872 DW. Aqui passaram 65 IBs **dez vezes maiores**,
todos com `Perna1=REGISTRADOR` e fence escrita pela GPU. Zero `GFXHUB_FAULT`, zero BugCheck,
461 `TRIPE_PROVADO` no Node 0, rollback `RECOVERY_OK_ONLINE`.

**Primeira cópia de pixel para `VkImage` no silício:**

```
UploadImageData SUCESSO: 1024x1, 4096 bytes em mip0/camada0
UploadImageData SUCESSO: 1x1, 4 bytes em mip0/camada0
```

21 uploads (11 em `dxgiadaptercache.exe`, 10 em `Explorer.EXE`). O `SUCESSO` só é impresso depois
do flush com fence, então é cópia **concluída pela GPU** — `vkCmdCopyBufferToImage` nunca havia
executado nesta UMD.

### 7.2 O que NÃO avançou

**Zero draws.** `DRAW fence concluida` = 0 em todos os processos. O caminho D3D11 de desenho segue
bloqueado no descriptor.

**As 118 submissões não são nossos draws.** Vêm dos uploads e dos flushes do bridge. A origem das
65 de 20.208 DW **não foi atribuída** — fica como NÃO ATRIBUÍDO, não como "nossas".

**`dwm.exe` teve `upload=0`.** As texturas dele não passam por `pInitialDataUP`; ele cria vazio e
preenche depois por `Map`/`UpdateSubresource`, que o upload desta faixa **não cobre**. O upload
serve aos outros processos, não a quem importa.

### 7.3 Duas falhas do CRITÉRIO, não da corrida

**(a) Assinatura e conclusão vinham acopladas.** A segunda metade da leitura de D — *"o DWM não usa
`pInitialDataUP`; falta cobrir `UpdateSubresource`"* — **confirmou-se**, mas a assinatura de D
(*"zero `UploadImageData`"*) foi **falsificada**: houve 21. A conclusão certa foi alcançada pelo
motivo nomeado de B cruzado com a contagem de upload por processo, não por D.

Um desfecho deve amarrar **uma assinatura observável** a **uma direção de investigação**, e as duas
não podem viajar juntas como pacote fechado. Daqui em diante, os critérios deste projeto separam
as colunas: *o que o log mostra* e *o que isso implica* são campos distintos, e um pode valer sem
o outro.

**(b) O maior resultado caiu fora do mapa.** As 118 submissões sem hang, com IBs dez vezes maiores
que o que travava, não estavam em nenhuma das quatro letras. O critério tratava a convergência
apenas como *controle a não regredir* (`3/3`), nunca como espaço de desfecho. Escrever só os
desfechos esperados cega para o inesperado — e neste caso o inesperado foi o avanço.

Daqui em diante todo critério deste projeto leva um desfecho final explícito:
**"E — algo fora do mapa"**, com a instrução de descrevê-lo antes de julgá-lo.

**Correção de enquadramento:** o Desfecho A ficou em 2 de 3 sinais (upload e submissões maiores
sim; `BOUND` não). Dois terços não fazem um A — o elemento que **define** A é justamente o que
faltou, e a leitura de A (*"o draw com textura foi e voltou"*) não aconteceu.

### 7.4 Limitação da própria faixa, exposta pela corrida

`SRV_AUSENTE_OU_SEM_IMAGEM` ainda agrega **sete** condições: SRV nula, `!Created`, sem
`pResource`, sem `VkImg`, dimensão ≠ TEXTURE2D, multisample, layout `UNDEFINED`. Foram nomeadas 17
recusas e justamente a que disparou ficou grossa. Meio trabalho feito.

### 7.5 Veredito

**AVANÇO FUNCIONAL EM ESTABILIDADE, MAIS UMA PRIMEIRA NO SILÍCIO; A PAREDE DO DRAW PERMANECE.**

Provado: o anel gráfico sustenta 118 submissões, algumas de 80 KB, sem hang; e a cópia de pixel
para `VkImage` funciona de ponta a ponta com fence.

**NÃO VALIDADO EM HARDWARE:** qualquer draw, qualquer pixel do nosso caminho, qualquer imagem no
monitor. Tela preta foi e continua sendo o resultado correto.
