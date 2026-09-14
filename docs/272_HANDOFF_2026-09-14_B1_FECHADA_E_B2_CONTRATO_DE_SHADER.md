# Handoff — 14/09/2026 · B.1 fechada no silício, B.2.1 fechada offline

> **Para a próxima IA.** Este documento é a passagem de bastão. Leia inteiro antes de propor
> qualquer coisa — em particular a seção **"Erros que eu cometi"**, porque três das quatro
> corridas da B.1 foram perdidas por defeito meu de *host*, não de silício, e todos os três
> defeitos eram de classes que o repositório já documentava.

**Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) · Windows 10 Pro 22H2 build 19045
**Meta final:** jogo real na tela ligada à placa, sem WARP, com TDR recuperando sem perder display.

---

## 1. Onde o projeto está, em uma frase

**O motor 3D da placa está vivo e sob nosso controle** — o anel gráfico `CP_RB0` liga, executa
PM4 com o tripé completo da Lei 2, e nós o configuramos, desaltamos, ligamos e revertemos com
cada etapa provada por readback. **Falta um programa de shader, geometria e um draw.**

---

## 2. O que ficou PROVADO NO SILÍCIO (P0) na sessão de 14/09

### 2.1 — O anel gráfico executa PM4 (corrida 13:18, reproduzida em 13:54)

```
PfpDirectCpSoftReset = 1        PfpDirectStatus = 0 (OK)   PhysicalReady = 1
PfpDirectRb0WptrApplied = 31    PfpDirectRb0RptrAfter = 31     ← perna 1: RPTR == WPTR
Rb0FenceBefore = 0x43DAFFFE (poison)
PfpDirectRb0FenceAfter  = 0xBC250123 (esperado)                ← perna 2: fence da GPU
PfpDirectMismatches = 0                                        ← perna 3: bytes conferidos
```

É o **tripé da Lei 2 sobre o anel gráfico**. Reproduziu em duas corridas seguidas.

### 2.2 — A nossa transação (Lei 5) percorre o ciclo inteiro (corrida 13:54, lab `3.181.1.0`)

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

Estabilidade: **zero** TDR, `DEVICE_REMOVED`, `GFXHUB_FAULT`, `GPU_EXCEPTION`, BugCheck.
`StopDevice=4` é o rollback programado.

Evidência: `docs/evidence/2026-09-14_13-54-26-DESKTOP-HQAG6T4-lab-binding/`
SYS instalado: `5A95B9F819EEC68D3404A2B9D947EFB1C3CCA18DD5CC4F2F2E54B207AD3C787C`

### 2.3 — Medição do estado de boot do anel (corrida 11:43)

`CP_RB0_CNTL = 0x00A00000` e `CP_ME_CNTL = 0x15000000` (CE/PFP/ME **todos HALTED**).
**Isto refutou minha própria âncora:** eu havia declarado `0x50000709` como "estado observado no
POST". Era falso — `0x50000709` é o valor **de referência calculado**, nunca o medido. A
constante foi renomeada de `..._OBSERVED_POST` para `..._CONFIGURED_REFERENCE`, e há portão que
proíbe o nome antigo voltar.

---

## 3. O que foi feito OFFLINE e NÃO foi validado em hardware

### 3.1 — B.2.0: catálogo (concluída)

16 registradores promovidos a **P1** em `docs/03_CATALOGO_DE_REGISTRADORES.md`, com `BAR5_byte`
**calculado por script**, não à mão:

`SPI_SHADER_PGM_RSRC3_PS/VS`, `RSRC4_VS`, `VGT_SHADER_STAGES_EN`, `SPI_VS_OUT_CONFIG`,
`SPI_SHADER_{POS,Z,COL}_FORMAT`, `SPI_PS_INPUT_{ENA,ADDR}`, `SPI_PS_INPUT_CNTL_0`,
`SPI_PS_IN_CONTROL`, `CB_SHADER_MASK`, `PA_SU_SC_MODE_CNTL`, `PA_SC_AA_CONFIG`,
`VGT_DRAW_PAYLOAD_CNTL`.

Regra: `BAR5_byte = (base_dw[BASE_IDX] + mmREG) * 4`, com `base_dw[0]=0x1260` e
`base_dw[1]=0xA000`. Fonte: `inc/linux_ref/gc_10_1_0_offset.h`, vendorizado e conferido contra
9 âncoras confirmadas no silício por `tools/import-gc-offset-header.py`.

**Conferência colateral:** as 11 definições de registrador de contexto já existentes em
`bc250_pm4_packet_builder.h` foram cruzadas uma a uma contra o header — **11/11 conferem**.

### 3.2 — B.2.1: contrato puro de shader (concluída)

`driver/core/bc250_gfx_shader_contract.{c,h}` — codifica `SPI_SHADER_PGM_LO/HI/RSRC1/RSRC2`,
valida o plano, devolve os quatro registradores. Não toca MMIO, não aloca.

**A decisão de projeto que você precisa entender antes de mexer nisto:**

A granularidade de alocação de VGPR/SGPR — quantos registradores cada unidade do campo
representa — **NÃO está no `sh_mask.h`**. O header dá a *posição* do campo, não a *escala*.
É classe **H**.

Em vez de assumi-la, **ela está fora do caminho crítico**: o valor que vai ao silício é o
`RSRC1`/`RSRC2` do **manifesto do compilador**; a regra H apenas **confere**, e divergência é
`BC250_GFX_SHADER_MANIFEST_MISMATCH` com a saída zerada. A regra é *falsificável* em vez de
assumida.

**Âncora de realidade:** `bc250_observable_shader.c` carrega manifesto de compilador real
(AMD clang/LLVM ROCm 7.2.1, `--offload-arch=gfx1010`) cujo shader **já executou no silício**.
`COMPUTE_PGM_RSRC1` e `SPI_SHADER_PGM_RSRC1_{PS,VS}` têm `VGPRS` em `[5:0]` e `SGPRS` em `[9:6]`
— mesmas posições, mesma ISA. A suíte exige que a regra reproduza os campos emitidos.
**Verificado:** mutar `blocks - 1` → `blocks` derruba a suíte (`build-contract` EXIT=1).

> ⚠️ **Ressalva que está no próprio teste:** com `NumVgprs=2` e `NumSgprs=2`, qualquer
> granularidade ≥ 2 produz campo 0. A âncora **confirma mas não discrimina** a escala. Ela é
> fraca de propósito. **Quem a ler como prova da granularidade está lendo errado.**

Outras três armadilhas fechadas no contrato:

1. **`LO = addr>>8`, `HI = addr>>40`** — derivado de `SPI_SHADER_PGM_HI_*__MEM_BASE_MASK` ter 8
   bits: endereço cru endereçaria só 40 dos 48 bits de VA. A mesma divisão **já executou no
   silício** no caminho de compute (`bc250_compute_dispatch_contract.c:77`). Endereço não
   alinhado a 256 é **recusado**, nunca truncado.
2. **`MEM_ORDERED` fica em bit diferente por estágio** — 25 no PS (`0x02000000`), 27 no VS
   (`0x08000000`). O bit 25 cai **dentro** de `VGPR_COMP_CNT` do VS (`0x03000000`).
   `VgprCompCnt != 0` no PS é **recusado**, não ignorado.
3. **Fonte única de `S_ENDPGM`** — a checagem de ISA delega a `Bc250ValidateComputeShaderIsa`
   (`bc250_isa_validator.h`). Constante duplicada em dois módulos já custou A13, B4 e
   ContextAbi: três corridas.

**Verificação:** `build-contract` EXIT=0 · 3 auditorias PASS · portão novo
`audit_gfx_shader_contract_is_honest` · **15 mutações verificadas**
(`tools/test-b2-gfx-shader-mutations.py`, 15/15) · B.1 mutações seguem 30/30.

---

## 4. Erros que eu cometi — leia antes de propor corrida

**Três das quatro corridas da B.1 foram perdidas em defeito de host, não de silício.** Todos de
classes já documentadas no repositório.

| # | Erro | Classe | Como foi pego |
|---|---|---|---|
| 1 | A dica de dry-run do binder não listava `-EnableGfxRingPreflight`. O flag existia, o operador copiou a dica, e a corrida mediu nada. | C3.9 — o arquivo **já tinha comentário descrevendo essa exata armadilha** | mineração do log |
| 2 | Escolhi o perfil sem `-EnableShaderDispatch` (→ `CpSoftReset=0`) e o PFP parou em `PC=0x21B`. `package-bc250mcdm-lab.ps1` linha 49 **já dizia por escrito** que sem a receita F8b a carga do PFP assenta no laço `0x218..0x21B`. | diagnóstico já estava no repo | telemetria da própria corrida |
| 3 | O bring-up mapeava `Core.MmioTransport` que o caminho do PFP já havia mapeado → `wrong_state`. | **A16**, precedente de 08/09 11:48 | linha `BRINGUP nao executou` |

Os três viraram portão. O #1 gerou um portão **genérico**: todo `[switch]$Enable*` do binder tem
de aparecer na dica de dry-run — e ele imediatamente encontrou o mesmo defeito latente em
`-EnableD3D11ShaderTranslator`.

**Outros erros da sessão, todos corrigidos:**

- **Assertiva fraca de tripé (minha):** o teste já reduzia as duas leituras, então a mutação
  "RPTR cru" passava. Corrigido isolando cada leitura com o writeback atrasado medido (`52`).
- **Duas assertivas que se anulavam (minhas):** o plano bom reproduzia o `CNTL` do POST
  exatamente, então cenários de "escrita descartada" eram invisíveis. Corrigido começando a
  placa falsa em `0x00A00709`.
- **Mutações M19/M21 fracas (minhas):** removiam só a primeira de N assertivas idênticas.
- **`CHECK` invertido em 23 de 29 suítes:** offline devolvia `1` na falha e `main` fazia
  `return Run() ? 0 : 1` → **falha virava sucesso**. Provado compilando com um emissor
  deliberadamente quebrado (offline EXIT=0, kernel EXIT=1). Corrigido + gate
  `audit_offline_check_is_not_inverted`.
- **M11 da B.2 reprovava pelo invariante errado (minha):** a mutação destruía a linha-âncora.
  **Falha não-atribuível é portão fraco.** Refeita. Segunda vez que cometo essa classe.
- **Heredoc do Bash corrompeu `\b` e `\t`** ao editar `build-contract.bat` — exatamente o que a
  memória do projeto já registra. **Use Write/Edit ou arquivo `.py`, nunca heredoc, para
  conteúdo com backslash.**
- **PowerShell `Set-Content -Encoding UTF8` adiciona BOM.** Adicionou um em
  `bc250_gfx_shader_contract.c` durante uma mutação; removido.

---

## 5. O que fazer a seguir — B.2.2

### 5.1 — A pergunta que bloqueia, e ela é de procedência

Precisamos de dois blobs de ISA gfx1010: um **VS** que exporta posição (`exp pos0`) e um **PS**
que exporta cor (`exp mrt0`). O modelo a seguir é `driver/core/bc250_observable_shader.c`, que
carrega um blob **com SHA-256 e procedência de compilador declarada** e cujo código já rodou na
placa.

**Decida isto primeiro, e declare a classe:**

| Opção | Classe | Custo |
|---|---|---|
| Compilar com o LLVM/clang do ROCm que produziu o blob de compute | **P1** | descobrir se o toolchain ainda está na máquina |
| Escrever o ISA à mão | **H** | barato, mas H — e tem de ser **declarado como H** |

**Não pule esta decisão silenciosamente.** Um blob à mão apresentado como se tivesse procedência
é exatamente a classe de defeito que a Lei 1 proíbe.

### 5.2 — Sequência proposta

| Sub-etapa | O quê | Prova que fecha |
|---|---|---|
| **B.2.2** | Blobs VS+PS com manifesto, SHA-256 e procedência | suíte offline valida `s_endpgm`, limites e o `RSRC` do manifesto contra a regra H |
| **B.2.3** | Staging: alocar VRAM, subir os blobs | bytes conferidos **byte a byte** após o upload |
| **B.2.4** | Corrida: `SET_SH_REG` no anel gráfico | `RPTR==WPTR` + fence sobre poison + **sem `GPU_EXCEPTION`** |

Depois: **B.3** (geometria — endereços já em P1), **B.4** (`DRAW_INDEX_AUTO`, emissor já existe
atrás de tunable desarmado), **B.5** (`BYTE_PROOF PASS` em triângulo de gabarito).

### 5.3 — Regra que vale para a B.2.4 e para toda corrida

`SET_SH_REG` no anel **gráfico** nunca pode carregar o bit `SHADER_TYPE=COMPUTE` — com ele
ligado o pacote endereça os registradores da MEC. Há `forbid` no portão para o módulo novo;
quando a emissão for escrita, **estenda o portão ao sítio de emissão**.

---

## 6. Estado da máquina e procedimento de corrida

- BC-250: pacote lab `3.181.1.0` foi revertido pelo watchdog (rollback online).
- **Boot frio é obrigatório antes de qualquer bind** — estado de engine não cai com desbind.
- Antes do bind: `tools\inspect-boot-fuse.ps1 -Reset` se o fusível estiver `TRIPPED`;
  `tools\BC250_RECOVERY.cmd` elevado se a placa estiver em Código 43.
- **Nunca** `del C:\Windows\System32\drivers\bc*.sys` — o padrão casa com `bcmfn2.sys`, driver
  **inbox da Microsoft**.
- O empacotador **não compila o SYS**, ele copia. Pular o build de laboratório assina o binário
  do candidato anterior. **Confira os binários por `strings` depois de empacotar.**
- **Não construa o UMD depois de empacotar** — o hash diverge e a auditoria reprova.
- `3.181.1.0` **já foi instalado**: suba o `DriverVer` fonte antes de gerar outro candidato,
  senão dois artefatos diferentes carregam o mesmo `DriverVer` (erro do `3.92.1.0`).
- As suítes de mutação reescrevem fontes; elas usam `tools/_mutation_mtime.py` para preservar
  `mtime`, porque a guarda de obsolescência do pacote de laboratório compara **datas**.

**Quem executa a corrida é o Daniel.** Você compila, analisa e prepara. Defina **teto de
corridas e critério de abandono antes** de armar, e escreva o **critério de desfecho antes da
corrida** — de preferência um em que os dois desfechos produzam informação.

---

## 7. Dívidas declaradas e abertas

| | Item |
|---|---|
| ⚠️ | O binder não anuncia a variável de ambiente `D3D11ExperimentalPm4Draw` — backdoor não declarada |
| ⚠️ | 3 suítes com contrato `CHECK` indeterminado: `blt_proof_contract_test.c`, `driver_caps_contract_test.c`, `umd_private_contract_test.c` |
| ⚠️ | Rota `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE` (F3.2) nunca executada |
| ⚠️ | Laço do tripé em `DISPATCH_LEVEL` segurando spinlock (A15) — não é gargalo de FPS, mas existe |
| ⚠️ | `COPY_LINEAR_SUB_WINDOW` sem procedência local (Lei 7) — não inventar o encoding |
| ⚠️ | `GFXHUB_FAULT STATUS=00841B40 ADDR=0x82C` no IB pesado de 11 MB — sem teste desde 09/09 |

---

## 8. Leis que mandam acima de qualquer plano

1. **Lei 1** — se a prova não existe, escreva literalmente `NÃO VALIDADO EM HARDWARE`.
2. **Lei 2** — tripé: `RPTR==WPTR` + fence física da GPU sobre poison + bytes conferidos.
3. **Lei 4** — zero fakes. Nunca `STATUS_SUCCESS` sem backend, nunca fence pela CPU.
4. **Lei 7** — endereço só com procedência catalogada.
5. **Lei 8** — classifique tudo: **P0** / **P1** / **P2** / **H** / **Q**.

E a regra que este projeto aprendeu caro, **cinco vezes** (A15, `mcdm_callbacks_test`, C1,
`sdma_blt_executed`, teste do blt com 128 bytes):

> **O gate pode congelar a mentira.** Quando corrigir um defeito, verifique se o *teste* exigia
> o comportamento defeituoso — e adicione a meta-mutação que reprova se o teste voltar a exigi-lo.

---

**Estado final desta sessão:** `build-contract` EXIT=0 · `build-umd` inalterado · 3 auditorias
PASS · mutações B.1 30/30 e B.2 15/15. **Nada da B.2 foi validado em hardware.**
