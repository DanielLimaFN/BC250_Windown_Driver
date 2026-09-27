# 294 — Critério da Corrida 40 — A textura do DWM chega à VkImage?

> **Escrito em 19/09/2026, ANTES do ensaio.** Candidato lab **3.198.1.0**, sonda de onda **ARMADA**.
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3) | Windows 10 Pro 22H2 build 19045
> **Pergunta única:** *a textura do `dwm.exe`, preenchida por `UpdateSubresource`/`Unmap`, chega à
> `VkImage` e deixa o descriptor vincular?*
> **Primeiro critério escrito sob a [Lei 10](../CLAUDE.md)** — assinatura e implicação em campos
> separados, desfecho `E` obrigatório, desfecho parcial não conta.

---

## 1. De onde viemos

A Corrida 39 (16:20:14, lab 3.197.1.0) fechou no **Desfecho B** e deu dois resultados:

- **118 submissões gráficas sem hang**, 65 delas de 20.208 DW (~80 KB) — dez vezes maiores que o
  IB de 1.872 DW que travava nas Corridas 34–37. Zero TDR. Esse foi o maior resultado, e caiu
  **fora** das quatro letras do critério daquela corrida; a Lei 10 nasceu disso.
- **Primeira cópia de pixel para `VkImage` no silício** (`UploadImageData SUCESSO`), 21 uploads —
  mas **zero no `dwm.exe`**, e os draws dele recusados em `binding index=2`.

O motivo da recusa era `SRV_AUSENTE_OU_SEM_IMAGEM`, que agregava sete condições. A corrida disse
onde parou, não por quê.

**Tela preta continua sendo o resultado esperado.** O harness não lê pixel de volta (Lei 2).

---

## 2. O que mudou nesta faixa

| # | Mudança | Onde |
|---|---|---|
| 1 | A recusa grossa virou **sete motivos distintos** (26 no total) | `bc250_umd_descriptors.c` |
| 2 | Textura sincroniza com `VkImage` em `UpdateSubresourceUP` **e** em `Unmap` | `bc250_d3d_umd.c` |

A #2 é a que ataca o `upload=0` do DWM. Os dois DDIs sincronizavam com Vulkan **somente quando o
recurso era BUFFER**; textura escrevia no shadow de CPU e nunca chegava à imagem. Como o DWM cria
a textura vazia e preenche depois, a faixa anterior — que só cobria a criação — não o alcançava.

Fonte única `Bc250UmdSyncTextureToVkImage`, escopo declarado em subrecurso 0. Subrecurso diferente
é **recusado com motivo**, nunca aproximado.

---

## 3. Critério — assinatura e implicação, em campos separados

> Pela Lei 10, as duas colunas valem **independentemente**. Uma assinatura pode ocorrer sem que a
> implicação se confirme, e vice-versa; quando isso acontecer, registre as duas coisas em vez de
> escolher uma.

### A — A textura do DWM chega à imagem

| | |
|---|---|
| **Assinatura** | `ResourceUpdateSubresourceUP: textura sincronizada com VkImage (WxH, N bytes, mip0/camada0)` **ou** o mesmo por `ResourceUnmap`, no log do `dwm.exe` |
| **Implicação** | O caminho de preenchimento do DWM era o que faltava. A próxima parede passa a ser o que o descriptor disser **depois** disso. |

### B — O descriptor vincula

| | |
|---|---|
| **Assinatura** | `[VK_DESCRIPTOR] BOUND: bindings=N (CB=.. SRV=.. SAMPLER=..)` no `dwm.exe` |
| **Implicação** | Descriptor set completo chega à GPU pela primeira vez. Atenção ao que vem **depois**: é aqui que o draw com textura pode reencontrar a violação de memória da Corrida 37. |

### C — Recusa, agora com um nome de sete

| | |
|---|---|
| **Assinatura** | `[VK_DESCRIPTOR] RECUSADO: binding index=N motivo=<UM DOS SETE>` |
| **Implicação** | Depende do nome, e cada um aponta um lugar distinto — ver a tabela §3.1. Isto **é progresso**, não fracasso: na Corrida 39 os sete cabiam numa frase só. |

#### 3.1 Para onde vai cada nome

| Motivo | Para onde |
|---|---|
| `SRV_RECURSO_SEM_VKIMAGE` | o recurso nem tem `VkImage` — olhar a criação, não o upload |
| `SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA` | tem imagem, nunca foi preenchida — a sincronia não disparou; ver se o DWM usa um terceiro caminho |
| `SRV_SEM_CONTEUDO_NUNCA_ESCRITO` | layout válido mas não é o de conteúdo — checar a ordem das transições |
| `SRV_DIMENSAO_NAO_E_TEXTURE2D` | o DWM usa 1D/3D/array — o escopo precisa crescer |
| `SRV_ALEM_DO_SUBRECURSO_CARREGADO` | o DWM quer mip/array; subrecurso 0 é estreito demais |
| `SRV_MULTISAMPLE_NAO_SUPORTADA` | MSAA no caminho de SRV |
| `SRV_SLOT_VAZIO` / `SRV_NAO_CRIADA` / `SRV_SEM_RECURSO_ASSOCIADO` | o problema é anterior à textura: o slot ou a view |
| `SAMPLER_*` | a textura passou; o bloqueio é o sampler |
| `CB_*` | **regressão** — o constant buffer já funcionava na Corrida 38 |

### D — Volta o TDR

| | |
|---|---|
| **Assinatura** | `[TDR_GFX_QUIESCE]` presente, depois de zero nas Corridas 38 e 39 |
| **Implicação** | O draw com textura chegou à GPU e travou. A sonda está **armada**: `[TDR_GFX_WAVEPC]` com `PC`, `IB_STS`, `INST_DW0`, `TRAPSTS` e `ValidMask`. Na Corrida 37 esse caminho entregou `FATAL_HALT` + `XNACK_ERROR` + `EXCP` bit 8. |

### E — Algo fora do mapa

| | |
|---|---|
| **Assinatura** | qualquer coisa que as letras acima não cubram |
| **Implicação** | **Descrever antes de julgar.** Não forçar o observado dentro de A–D. Esta letra existe porque na Corrida 39 o maior resultado — 118 submissões sem hang — não estava previsto em nenhum desfecho, e quase passou como mero controle. |

> **Desfecho parcial não é desfecho.** Se A exige a linha de sincronia no `dwm.exe` e ela aparecer
> só em outros processos, A **não** ocorreu — registre o que houve nos outros e diga que A não
> ocorreu.

---

## 4. Medições de controle

| Controle | Corrida 39 |
|---|---|
| Submissões GFX | 118, **zero** `NAO_CONVERGIU` |
| Maior IB | 20.208 DW |
| TDR | zero |
| `GFXHUB_FAULT` | zero |
| BugCheck | zero |
| Node 0 SDMA | 461 `TRIPE_PROVADO` |
| Rollback | `RECOVERY_OK_ONLINE` |

Regressão em qualquer um manda no veredito. Em especial: **se as submissões GFX voltarem a
`NAO_CONVERGIU`, esta faixa piorou algo** — a sincronia de textura submete trabalho novo à GPU.

---

## 5. O que esta corrida NÃO responde

- **Não acende a tela**, e nada aqui prova rasterização.
- **Não prova que os texels estão certos.** Prova que algum conteúdo foi copiado e que a fence
  voltou. Conferir os texels exige leitura de volta, que não existe.
- **Não cobre mip chain nem array**, nem subrecurso diferente de 0 — recusados por nome.
- **Não fecha a dívida do tamanho da struct** `BC250_UMD_RESOURCE`. A causa foi encontrada e
  corrigida (o `memcpy` selvagem em `ResourceCopyRegion`), mas ela nasceu de código que confiava
  em campo não inicializado; outros sítios podem ter o mesmo padrão e não foram varridos.

---

## 6. Pacote — 3.198.1.0, assinado, auditado, NÃO INSTALADO

`MCDM_LAB_BRINGUP` · `DISPLAY_RENDER_LAB` · WDDM 2.7 · P1 PERSISTENT SUBMISSION · schema 135 ·
**GfxWaveProbe=1 (ARMADO)**

| Artefato | SHA-256 |
|---|---|
| SYS (pacote) | `749BA458F5F235B702C25204E17DF0DC85E98C9D6F0B5DD1F8E88926A7875041` |
| UMD (pacote) | `EF5B3704F30E8157202770FE0D88A9F77AC68064AF0761BC98FE07AB81A8710B` |
| INF | `E709ADD43A337D38D8CD505A11BF92AEA0B59CF5CB1B68DEAE3853E60F2B164B` |
| CAT | `67380992B573C18965765884312B12744868C2E6CEA1761B4F6BD50724400CC1` |
| `vulkan_radeon.dll` | `7837c96027e5e56233fa16735cbe22a301a62f61d1e0075ad833efddeefe5868` |

**O SYS é o mesmo do 3.196.1.0 e do 3.197.1.0, e isso é correto:** nenhuma fonte do KMD mudou
desde a imagem de laboratório de 15:28, verificado por data de modificação. Só a UMD muda, de
`F75DD6DE…` para `EF5B3704…`.

**Conferido por string na UMD assinada:** os sete motivos novos, `textura sincronizada com
VkImage`, `fora do escopo carregado`; e `SRV_AUSENTE_OU_SEM_IMAGEM` com **zero** ocorrências.

`audit-bc250mcdm-lab-package.ps1`: **PASS**.

Antes do bind: `tools\inspect-boot-fuse.ps1` (e `-Reset` se `TRIPPED`) e **boot frio obrigatório**.
Recuperação: `tools\BC250_RECOVERY.cmd` elevado.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
