# 174 — Handoff de 2026-09-07 · GPUVA com fonte única, e a telemetria da SDMA0 decodificada

> **Para quem assume o trabalho.** Este documento é auto-suficiente: o que foi feito, o que
> está provado, o que é hipótese, o que **não** foi validado em hardware, e qual é o próximo
> passo com o teste que o fecha.
>
> **Sessão inteiramente OFFLINE. Nenhuma corrida física foi gasta.** Todo achado aqui vem de
> leitura de fonte, de aritmética sobre medições já arquivadas, ou de teste offline.
>
> Leia antes: [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) (Tabela 1 = feito com prova física,
> Tabela 2 = o que falta em ordem de prioridade) e [`CLAUDE.md`](../CLAUDE.md) (leis
> inegociáveis e barra de status).

---

## 0. Onde queremos chegar — não mudou, e não deve mudar

**Rodar jogos no Windows 10 22H2 na BC-250**, usando as 24 CUs e os 16 GB de GDDR6, monitor no
DisplayPort da própria placa, via D3D12/Vulkan, **sem WARP e sem fallback de software**.

**Lei do escopo (decisão do dono do projeto, 06/09):**

1. O objetivo é **WDDM acelerado por GPU e jogos com Vulkan**. Nada mais.
2. **LLM é subproduto.** Não é marco, não entra em PASS de etapa, não aparece em veredito.
3. **Compute isolado não é entregável.** Vale como pré-requisito de jogos, não como destino.
4. **É proibido propor compute ou LLM como "caminho mais rápido" quando o item de vídeo estiver
   bloqueado.** Item travado se resolve, se declara travado, ou se declara
   `NÃO VALIDADO EM HARDWARE`.
5. **Demorar é aceito.** Custo em tempo não é argumento para trocar o objetivo.
6. Critério final: **jogo real na tela ligada à placa, sem WARP, com TDR recuperando de hang
   sem perder o display.**

---

## 1. O contexto herdado — os dois bloqueios que já eram conhecidos

### Bloqueio A — vídeo: falta pipeline 3D real na UMD

Medido em 07/09 10:11 e **reproduzido em 11:08 com toolchain diferente** (VS2026 + WDK 28000,
zero mudança de código, logo **não é artefato de compilador**):

```text
OpenAdapter10_2 -> S_OK -> GetSupportedVersions -> OK -> GetCaps(3DPIPELINESUPPORT) -> RECUSAMOS
                                                                                        ^^^^^^^^^
                                                                              e é só aqui que para
```

O runtime pede **exatamente um** cap antes de criar o device: `D3D11DDICAPS_3DPIPELINESUPPORT`
(type 130, `DataSize=4`). Nós recusamos, e a recusa é correta — o payload
`D3D11DDI_3DPIPELINELEVEL` tem **`0 == level 10_0`**, então zerar seria declarar capacidade
falsa (Lei 4).

O `dwmcore` exige device **D3D11** em `CLegacyRenderTarget::EnsureSwapChain` →
`CD3DDevice::CreateD3D11Device`, e ao receber `DXGI_ERROR_UNSUPPORTED` ele **fail-fasta**
(`0xe0464645`) em vez de cair para software. `DriverSupportsCddDwmInterop` **não** é rota de
fuga — já está declarado e não mudou nada.

**Conclusão medida, não inferida: não há truque de caps que passe deste ponto. A única coisa
entre este driver e um device D3D é um pipeline 3D real.** Decisão de arquitetura vigente:
**rota B — DXBC → SPIR-V → RADV/ACO** (um backend para D3D e Vulkan). Rota A (compilador
DXBC→ISA próprio) está **rejeitada** e só reabre por escrito.

### Bloqueio B — render: o IB de paginação não executa sob VMID 8 (item B1)

`RPTR` alcança o `WPTR`, `GCVM_L2_PROTECTION_FAULT_STATUS` lê zero, e o destino não tem os
dados. Isso é o item **B1**, e é pré-requisito de submissão Vulkan e de jogos: sem ele o
`dxgkrnl` não consegue entregar command buffer de verdade.

---

## 2. O que esta sessão fez — resumo executivo

| Frente | Resultado |
|---|---|
| **Perguntas abertas do doc 167** | Das quatro que sustentavam o B1, **três fecharam** (4, 5 e 3) e a quarta (2) **ganhou dados**. Tudo por leitura do **nosso próprio repositório** |
| **Defeitos encontrados e corrigidos** | **Cinco** na geometria de GPUVA (faixa A11). Os três primeiros produziriam o sintoma do B1 se acionados. O primeiro era meu |
| **Capacidade nova** | Telemetria de fila da SDMA0 **decodificada** (faixa A12) — a próxima corrida devolve estado interpretado em vez de oito palavras hexadecimais |
| **Corridas gastas** | **Zero** |

⚠️ **NADA DESTA SESSÃO FOI VALIDADO EM HARDWARE.** São mudanças de contrato, de gate, de teste
offline e de telemetria. A telemetria nova **nunca rodou**.

---

## 3. A lição de processo — e ela é a parte mais reaproveitável

**Três das quatro perguntas de registrador estavam respondidas no repositório, com
proveniência `P1`, e ficaram listadas como "sem resposta" por dias.**

| Pergunta do doc 167 | Onde a resposta já estava |
|---|---|
| **Q4** — `PAGE_TABLE_DEPTH` do VMID 8 | `docs/03`, seção "Derivação Matemática da Geometria (34 bits, 2 níveis)" |
| **Q5** — qual base aparece numa PTE | `docs/03`, tabela de flags de PTE + `amdgpu_gmc_vram_mc2pa()` |
| **Q3** — qual é o hub de fault correto | `docs/03`, seção "E a SDMA traduz por qual hub?" |
| **Q2** — que estado faz o `RPTR` alcançar o `WPTR` sem executar | dados em `docs/168` §5.3; semântica em `inc/linux_ref/gc_10_1_0_sh_mask.h` |

**Não eram lacunas de conhecimento; eram lacunas de busca.** Como a corrida é o recurso escasso
(Lei 9) e é o Daniel que a executa, cada pergunta que o catálogo já responde e que se escala é
ensaio desperdiçado.

### Regra de classificação que ficou estabelecida

Eu havia escrito no doc 167 que *"este repositório não tem cópia local do amdgpu"*. **Era
falso**, e a distinção correta importa:

| Tipo de pergunta | Fonte local | Classe alcançável |
|---|---|---|
| *"quais são os bits do campo X?"* | `inc/linux_ref/gc_10_1_0_sh_mask.h` | **`P1`** |
| *"qual offset tem o registrador X?"* | `docs/03` | **`P1`** |
| *"o que a função `foo()` do amdgpu decide?"* | nenhuma — não há `.c` do amdgpu | **`H`** |

**Como aplicar:** grepar o catálogo pelo nome do campo **e** pela palavra da operação
("fault", "invalidate", "depth", "aperture"), e **ler as seções em prosa** — as respostas de
Q3/Q4/Q5 estavam em texto corrido e em tabelas de derivação, não onde um grep pelo offset
cairia. Depois **conferir se o código faz o que o catálogo derivou**: foi assim que apareceu o
A11.5 (o catálogo afirmava algo falso sobre o próprio driver), e a divergência valia mais que a
resposta.

---

## 4. Perguntas fechadas (com proveniência)

### Q4 — `PAGE_TABLE_DEPTH` do VMID 8 = **1** · `P1`

A convenção do GCVM é *níveis abaixo da raiz*:

| `PAGE_TABLE_DEPTH` | Hierarquia |
|---:|---|
| `0` | 1 nível — tabela plana / GART |
| `1` | **2 níveis — raiz PDE0 → folha PTE** ← nosso caso |
| `2` | 3 níveis |
| `3` | 4 níveis |

Nossa hierarquia é de 2 níveis (12 bits de offset + 9 de folha + 13 de raiz = **34 bits**,
16 GiB), logo `DEPTH = Levels - 1 = 1`. **E é isso que o driver programa:**

- `BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE = 0x00555403`;
- `PAGE_TABLE_BLOCK_SIZE = block_size - 9 = 0`;
- `BC250_GCVM_CONTEXT8_PT_END_LO_VALUE = 0x003FFFFF` (`2^22 - 1`);
- os quatro travados por `driver/tests/gcvm_context8_cntl_test.c`.

**Não é aqui que o IB falha.** Gastar corrida variando `PAGE_TABLE_DEPTH` seria testar o que a
nossa própria fonte já derivou.

### Q5 — a PTE carrega **PA**, não MC · `P1`

| O quê | Valor | Fonte |
|---|---|---|
| PA de página de VRAM | `MC - vram_start + vram_base_offset` | `amdgpu_gmc_vram_mc2pa()` |
| `vram_base_offset` | `GCMC_VM_FB_OFFSET << 24` | `gfxhub_v2_0.c:117`, `get_mc_fb_offset()` |
| medido nesta placa | `GCMC_VM_FB_OFFSET = 0x450` → `0x450000000` | baseline de 2026-08-23 |

Ou seja `PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)`. A base **MC** (`0xF400000000`, de
`GCMC_VM_FB_LOCATION_BASE`) é de **outro domínio** — anel e blts físicos. Trocar uma pela outra
já foi tentado: a corrida de 05/09 01:29 fez essa troca por causa da prova byte a byte dos
blts, foi **erro de domínio**, não mudou nada, e foi revertida. O comentário de domínio está
preservado em `bc250_gcvm_contract.h:12-29`.

### Q3 — o hub **está certo** · `P1`

> *"E a SDMA traduz por qual hub? Pelo **GFXHUB**. Não é dedução: `sdma_v5_0.c` atribui
> literalmente `ring->vm_hub = AMDGPU_GFXHUB(0);`"* — `docs/03`

E o caminho de invalidação foi conferido linha a linha, **correto nos cinco pontos**:

| O que o catálogo exige | O que `Bc250PagingBuildGfxhubTlbFlushPlan` faz |
|---|---|
| `PER_VMID_INVALIDATE_REQ = 1 << vmid` | `1u << 8` → `RequestValue = 0x00F80100` ✅ |
| `CLEAR_PROTECTION_FAULT_STATUS_ADDR = 0` (não apagar evidência) | bit 24 em **0** ✅ |
| ACK no bit `1 << vmid` de `ENG17_ACK` | `EngineId = 17`, `AckMask = 1u << 8`, `0xA318` ✅ |
| leitura dummy do `REQ` obrigatória em GC < 10.3 | `RequiresDummyRequestRead = 1` ✅ |
| GFXHUB **não** usa semáforo | `UsesSemaphore = 0` ✅ |

**Logo `FAULT_STATUS = 0` é medição de verdade — não hub errado, nem status apagado por nós.**

### Q1 (CSA) — descartada · `H`

No upstream, `amdgpu_sdma_get_csa_mc_addr()` devolve **zero** quando a preempção no meio do
comando está desligada — e o nosso `F_CNTL_MIDCMD` já saiu do `CNTL` (o `#define` ficou órfão).
Logo `CsaAddress = 0` é o valor **correto**, não uma omissão. Implementar CSA seria divergir do
upstream **gastando uma corrida à toa**. O formato do `SDMA_PKT_INDIRECT` também confere e está
travado byte a byte pelo bloco 6b de `sdma_packet_builder_test`.

---

## 5. A pergunta 2 — deixou de ser vazia (o achado central da sessão)

`docs/168_HANDOFF_2026-09-05_INDIRECT_SDMA.md` §5.3 arquivou leituras de silício do bloco
`SDMA0_GFX_IB_*` e a §6 item 6 encerrou assim:

> *"Registradores ainda não lidos: `SDMA0_GFX_IB_SUB_REMAIN`, e a semântica de bits de
> `CONTEXT_STATUS` e `SKIP_CNTL` … **não sei o que significa e não interpretei**."*

A semântica estava em `inc/linux_ref/gc_10_1_0_sh_mask.h`. Aplicada aos valores medidos:

| Variante | `CONTEXT_STATUS` | Decodificado |
|---|---|---|
| `PHYS` (VMID 0, MC físico) | `0x0D` | `SELECTED` + **`IDLE`** + `EXPIRED` |
| `EXEC` (VMID 8, traduzido) | `0x09` | `SELECTED` + `EXPIRED`, **`IDLE = 0`** |

| Variante | `IB_CNTL` | Decodificado |
|---|---|---|
| `PHYS` | `0x00000101` | `IB_ENABLE=1`, `SWITCH_INSIDE_IB=1`, `CMD_VMID=0` |
| `EXEC` | `0x00080101` | `IB_ENABLE=1`, `SWITCH_INSIDE_IB=1`, `CMD_VMID=8` |

**Os dois `CONTEXT_STATUS` diferem em exatamente um bit — o `IDLE`. Os dois `IB_CNTL` diferem
em exatamente um campo — o `CMD_VMID`.** Medição `P0` (doc 168) somada a decodificação `P1`
(header local): **não é hipótese**. A engine tratou o pacote **igual** nas duas variantes, e a
única diferença de desfecho é que a variante traduzida **não voltou a ficar ociosa**.

🔶 **HIPÓTESE (`H`):** sob VMID 8 a engine consumiu o pacote do **anel**, latchou `IB_BASE` e
`CMD_VMID`, e **parou dentro do IB** — não terminou, por isso `IDLE` continua 0. Desloca a
pergunta 2 de *"não executou o pacote"* para *"começou o IB e travou"*. **Casa com a hipótese
da Q3** (§6): travar buscando/traduzindo é o que um fault redirecionado para a dummy page, sem
interrupção habilitada, produziria.

### ⚠️ O que isto NÃO resolve — leia antes de concluir qualquer coisa

- A variante `PHYS` **foi a `IDLE`**, ou seja **terminou**, e a prova de bytes dela **também
  falhou** (doc 168 §5.2). **Sem tradução envolvida, aquela falha não pode ser de GCVM.** São
  **duas falhas diferentes**, e esta decodificação não decide nenhuma.
- Não se sabe se `EXPIRED = 1` aqui é significativo ou se é o estado normal de uma fila que
  drenou.
- `IB_RPTR = 0x20` e `IB_SIZE = 0` eram idênticos em toda variante e toda corrida. O doc 168 já
  advertiu: **pode ser valor constante**, e interpretar como "a engine leu os 8 dwords" **não
  está provado**. A telemetria nova mede isso por submissão.

### Duas armadilhas de leitura, agora catalogadas

- **`IB_RPTR`/`IB_OFFSET` têm o campo `OFFSET` começando no bit 2.** O cru está em **bytes**, o
  campo conta **dwords**. `IB_RPTR = 0x20` são **8 dwords**, não 32. Ler o cru como dwords dá
  quatro vezes o valor certo.
- **`EXCEPTION` é campo de 3 bits** (`0x00000070`), não um flag.
- **O bit 1 de `CONTEXT_STATUS` não tem nome no header do upstream.** A união dos conhecidos é
  `0x000007FD`; o decodificador reporta o resto em `BITS_SEM_NOME` em vez de fingir que
  entendeu o valor todo.

---

## 6. A hipótese da Q3 — nós desligamos a notificação de fault

Contando os bits do nosso valor alvo contra o POST medido — os dois já em `docs/03`:

| Campo (7 classes de fault) | POST `0x007FFE80` | Alvo `0x00555403` |
|---|:---:|:---:|
| `*_PROTECTION_FAULT_ENABLE_DEFAULT` | 1 (7/7) | 1 (7/7) |
| `*_PROTECTION_FAULT_ENABLE_INTERRUPT` | **1 (7/7)** | **0 (0/7)** |

A tabela do próprio catálogo diz que `ENABLE_DEFAULT = 1` significa **"redireciona para dummy
page default"**. Com `ENABLE_INTERRUPT = 0` junto, isso descreve hardware que, ao falhar a
tradução, **manda o acesso para a dummy page e não avisa ninguém**.

🔶 **HIPÓTESE (`H`)** — primeira explicação coerente com as **três** medições do B1 ao mesmo
tempo:

1. `RPTR` alcança o `WPTR` — a engine consumiu o pacote;
2. `FAULT_STATUS` lê zero — nenhuma interrupção de fault foi habilitada;
3. destino sem os dados — as escritas foram para a página default.

⚠️ **NÃO PROVADO, e não está nos nossos docs:** se `GCVM_L2_PROTECTION_FAULT_STATUS` *latcha*
quando `ENABLE_DEFAULT = 1` e `ENABLE_INTERRUPT = 0`. **Se latchar, a hipótese está refutada**
pela medição que já temos. E o valor alvo **não é erro nosso**: o catálogo o deriva do setup
padrão do upstream, que também não habilita interrupção nesse passo.

### O experimento que fecha, e produz informação nos DOIS desfechos

```text
  máscara de interrupt = bits 9,11,13,15,17,19,21 = 0x002AAA00
  0x00555403 | 0x002AAA00                         = 0x007FFE03
```

`0x007FFE03` difere do POST medido (`0x007FFE80`) em **apenas `0x83`**: bit 0
(`ENABLE_CONTEXT`, ligado de propósito), bit 1 (`PAGE_TABLE_DEPTH = 1`, idem) e bit 7
(`RETRY_PERMISSION_OR_INVALID_PAGE_FAULT`, que o upstream **desliga** em APU para suprimir
tempestade de fault, `gfxhub_v2_0.c:313`). **É o estado de fábrica com a observabilidade
preservada — não um valor inventado.**

O decodificador de IV já existe e está travado (`INTERRUPT HANDLER CONTRACT`, IV de 32 B do
Navi/GFX10). Critério **binário**:

- **chega IV de fault** → hipótese **confirmada**, o IB executa, o defeito é de tradução, e o
  `FAULT_ADDR` diz **qual** endereço;
- **não chega IV nenhum** → hipótese **refutada**, a engine não executa o pacote, e o centro
  volta a ser a pergunta 2.

**Já declarado em código e deliberadamente NÃO usado por nenhum caminho:**
`BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE`, com a aritmética travada por 8 asserções em
`gcvm_context8_cntl_test`. **Armar é decisão do dono do projeto, não efeito colateral de
compilar.**

---

## 7. Faixa A11 — cinco defeitos na geometria de GPUVA, corrigidos

Procurar as respostas do doc 167 no repositório expôs cinco defeitos na mesma geometria.
**Nenhum é o B1**, mas os três primeiros produziriam o sintoma do B1 se acionados.

| # | Defeito | Correção | Trava |
|---|---|---|---|
| **A11.1** | **Segunda fonte de verdade para `GpuVaBits`.** O blob `UMDRIVERPRIVATE` publicava `BC250_ADAPTER_GPUVA_BITS` = **48** enquanto o mesmo driver declarava `VaBits=34` ao WDDM — **medido** no log de 11:08: `[McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2`. A UMD (e, na rota B, o winsys do RADV) dispõe endereços dentro do que o blob promete; prometer 48 sobre hierarquia que traduz 34 gera endereço **sem PTE alcançável**. **Defeito meu, criado ao escrever o blob** | `facts.GpuVaBits`/`PageTableLevels` passam a sair de `runtimeCaps` — a **mesma** struct que alimenta o `GPUMMUCAPS`; sem geometria o blob não sai (`DEVICE_NOT_READY`) | gate estático `audit_gpuva_geometry_single_source`, **verificado por teste de mutação** (trocar por `48u` faz a auditoria falhar) + asserção de repasse em `mcdm_callbacks_test` usando o mock 48/4, que distingue repasse de hardcode |
| **A11.2** | `BC250_ADAPTER_GPUVA_BITS` **literal** (48) e `MAX_GPUVA_ADDRESS` = `0xFFFFFFFFFFFF`. Morto — só testes o liam — mas pronto para reintroduzir o defeito ao ser ligado | passa a **derivar**: `#define BC250_ADAPTER_GPUVA_BITS BC250_GPUVA_PT_VA_BITS`, `MAX = (1ull << bits) - 1` | mesmo gate; mensagem do build passou a dizer `34-bit GPUVA derivado` |
| **A11.3** | **Fail-open no journal de GPUVA.** `Bc250GpuvaJournalStageLeafUpdate` recusava VA a partir de `2^48` — o teto **arquitetural** — em vez de `2^34`, o **traduzível**. Aceitava registrar página num VA que o VMID 8 não alcança; **sintoma indistinguível do B1**. Não era acionável pelo alocador vigente (inicializado com `BC250_GPUVA_PT_VA_BITS`), mas era porta aberta | novo `BC250_GPUVA_TRANSLATABLE_LIMIT = 1ull << BC250_GPUVA_PT_VA_BITS`; `MAX_LIMIT`/`DEFAULT_BITS` marcados como teto arquitetural que **não** serve de limite de tradução | 4 casos em `gpuva_journal_contract_test`: última página traduzível **entra**; a primeira fora, a faixa que **atravessa** o limite e o VA de 8 TB **não** |
| **A11.4** | **Bloco de PROVENIÊNCIA falso** no topo de `bc250_gpuva_space_contract.h`: descrevia o desenho **anterior** (plano, `PAGE_TABLE_DEPTH = 0`, 22 bits de índice) logo acima das constantes de 2 níveis que o substituíram. Sob a **Lei 7** é armadilha: quem o lesse programaria depth 0 e declararia 1 nível ao `dxgkrnl`, **abaixo do mínimo do contrato** (`DXGK_MIN_PAGE_TABLE_LEVEL_COUNT = 2`) | reescrito para a geometria vigente com a proveniência real (`docs/03`, `P1`); texto antigo marcado como **HISTÓRICO**, nada apagado | — |
| **A11.5** | **Afirmação falsa e normativa em `docs/03`:** *"o driver lê `GCMC_VM_FB_OFFSET` do silício em cada construção; não usa constante"*. O leitor de topologia lê, mas o valor usado nas PTEs é a constante `BC250_GCVM_VRAM_MC_BASE_FALLBACK` — **deliberadamente** (a base do leitor "nunca fez um acesso funcionar"; a constante tem prova byte a byte). Hoje concordam; o risco era o silêncio se deixassem de concordar | catálogo corrigido; `SetRootPageTable` passou a comparar e emitir `ROOT_MC: DIVERGENCIA` quando diferem, e a dizer explicitamente quando concordam. **Zero mudança de comportamento** | removida no mesmo passo uma inicialização **morta** que repetia `0x450000000ull` à mão — quarta grafia do mesmo número — e era sobrescrita sem condição |

**Dois testes congelavam o defeito** e foram descongelados com a norma citada: `TestRoundTrip`
afirmava `blob.GpuVaBits == 48u`, e `adapter_info_contract_test` afirmava `48u` /
`0xFFFFFFFFFFFF`. Mesmo padrão do `SupportNonVGA == FALSE` corrigido no A1.

---

## 8. Faixa A12 — telemetria de fila da SDMA0 decodificada

**Novo contrato:** `driver/core/bc250_sdma_queue_state_contract.{c,h}` — aritmética pura,
**não escreve registrador nenhum**, testável offline.

- Offsets dos 8 registradores do bloco (`0x04BA8`..`0x04BC4`), proveniência `docs/03`;
- máscaras e shifts de `IB_CNTL`, `IB_RPTR`, `IB_OFFSET`, `IB_SIZE`, `SKIP_CNTL` e
  `CONTEXT_STATUS`, proveniência `inc/linux_ref/gc_10_1_0_sh_mask.h` com linha citada;
- `Bc250SdmaDecodeQueueState()` → struct decodificada, com `UnknownBits`;
- `Bc250SdmaClassifyQueueState()` → veredito `DRAINED` / `BUSY_OR_STALLED` / `EXCEPTION` /
  `PREEMPTED` / `IB_DISABLED`, **falhando fechado** (estado ambíguo nunca sai como `DRAINED`);
- `Bc250SdmaQueueLooksStalledInsideIb()` → o discriminante de 05/09 avaliado em tempo de
  corrida. **É rótulo, não veredito de defeito.**

**Onde entrou:** `Bc250SdmaPresentExecuteIndirect`, **no caminho de `IB_TRIPOD TIMEOUT`**,
emitindo quatro linhas novas:

```text
[SubmitVirtual Node0 SDMA] IB_QUEUE_RAW: IB_CNTL=… IB_RPTR=… IB_OFFSET=… IB_BASE=… IB_SIZE=… SKIP_CNTL=… CONTEXT_STATUS=…
[SubmitVirtual Node0 SDMA] IB_QUEUE_DECODED: veredito=… IB_ENABLE=… SWITCH_INSIDE_IB=… CMD_VMID=… IB_RPTR=… dw …
[SubmitVirtual Node0 SDMA] IB_QUEUE_STATUS: SELECTED=… IDLE=… EXPIRED=… EXCEPTION=… … BITS_SEM_NOME=…
[SubmitVirtual Node0 SDMA] IB_QUEUE_DISCRIMINANTE: anel_drenado=… parado_dentro_do_IB=…
```

**Decisões de projeto, para não serem desfeitas por acidente:**

- **Somente leitura.** Estes registradores não são escritos por software — o microcódigo do F32
  os carrega ao consumir o `INDIRECT`. É por isso que servem de diagnóstico.
- **Fora do caminho de sucesso, de propósito.** São 8 acessos de MMIO, e o `Present` roda a
  60 Hz. Instrumentar o PASS perturbaria o timing do caminho que já funciona.
- **A leitura acontece dentro do `RingLock`; o log sai depois do `KeReleaseSpinLock`.**
  `Bc250DisplayLog` só grava em `PASSIVE_LEVEL`; em 04/09 22:27 uma linha emitida com o lock
  segurado **sumiu do arquivo** e deixou a corrida cega.

**Teste que fecha:** `driver/tests/sdma_queue_state_contract_test.c` — confere as máscaras
contra o header **e** reproduz os dois valores medidos em silício. **Já se pagou:** eu havia
escrito que `SWITCH_INSIDE_IB` era 0 na variante `PHYS`, por suposição; `0x00000101` tem o bit
8, e o teste corrigiu antes de a afirmação chegar a um documento.

---

## 9. Estado de build, auditoria e pacote

| Item | Estado |
|---|---|
| `driver/wdk/build-contract.bat` | **EXIT=0** (`/W4 /WX`, zero warning no KMD) |
| `driver/umd/build-umd.bat` | **EXIT=0** |
| `tools/audit-bc250mcdm-safety.py` | **PASS** (com o gate novo `GPUVA geometry: single source`) |
| `tools/audit-mcdm-ddi-matrix.py` | **PASS (STRUCTURAL ONLY)** |
| `tools/audit-ddi-noop-callbacks.py` | **APROVADO** (3 dívidas aceitas, 1 aviso, 0 falhas) |
| `package/output_active` | **CORRENTE** — `DriverVer 3.81.0.0`, contém a telemetria nova |
| `package/output_lab` | ❌ **ESTAGNADO** — de 10:37, **anterior** a tudo desta sessão |

Warnings remanescentes são todos em `tools/*.c` (utilitários de host, fora do `/WX`) e são
**pré-existentes**: `C4101` em `bc250-compute-cli.c`, `C4245` em `bc250-gpuva-sdma-gate.c`,
`C4996` (`fopen`) em `bc250-hardware-diag.c`.

### Por que o lab não foi reconstruído

`tools/build-bc250mcdm-lab-bringup.ps1` exige `-Execute
-IUnderstandThisEnablesDestructiveBringup`. Esse flag existe porque a imagem de laboratório
executa **PFP direct load, CP, GCVM e HQD durante o `StartDevice`, enquanto detém o display**.
**Ligá-lo é decisão do dono do projeto, não passo de build.**

### ⚠️ Duas armadilhas medidas nesta sessão — ambas capazes de queimar uma corrida

1. **`build-bc250mcdm-lab-bringup.ps1` sem os flags retorna `EXIT=0`** — ele só valida
   precondições e **não compila**. Ler o código de saída como sucesso deixa o SYS antigo no
   lugar.
2. **Comparar hash do pacote com o hash do build NÃO prova frescor** — o empacotador **assina**
   o SYS, então os dois diferem por construção. **Cometi esse erro de medição e o corrigi.** A
   prova barata é procurar, como ASCII dentro do binário, uma string de log introduzida no
   candidato:

```bash
powershell -Command "$t=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes('driver\wdk\package\output_active\bc250mcdm.sys')); $t.Contains('IB_QUEUE_DISCRIMINANTE')"
```

---

## 10. 🛑 BLOQUEADOR ATIVO — leia antes de qualquer bind

**O probe do D3D11 ficou ARMADO no registro:** `HKLM\SOFTWARE\BC250\Umd\D3D11CapsProbe = 1`,
sobra das seis corridas de 07/09.

Com ele ligado, `Bc250UmdShouldPublishAdapter(FALSE, TRUE)` devolve `TRUE`, a UMD publica a
tabela de adapter, o DWM percorre a negociação, falha no `3DPIPELINESUPPORT` e **fail-fasta** —
é a **tela preta de propósito**. O experimento `P7_D3D11_CAPS_PROBE` está **ENCERRADO**; agora
ele só adiciona ruído a qualquer corrida.

**Desarmar (exige elevação, é passo do Daniel):**

```bash
powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm
```

---

## 11. Próximo passo — a corrida recomendada, e por que nesta ordem

**Recomendação: UMA corrida com o pacote ativo atual, telemetria só-leitura, probe desarmado.**

### Por que vale agora

1. A telemetria nova é **somente leitura, zero mudança de comportamento** — nenhum risco novo.
2. **O log não se perde.** `Bc250DisplayLog` abre, escreve, dá `ZwFlushBuffersFile` e fecha **a
   cada linha** (`bc250_display_ddi.c:575-577`) — reboot sujo não apaga o que já saiu.
3. Responde a pergunta 2 **no momento real da falha**, não sobre leituras de dois dias atrás.
4. De brinde, mede o `ROOT_MC`: o `GCMC_VM_FB_OFFSET` do silício ainda concorda com a
   constante?

### Por que NÃO bundlar o experimento de fault na mesma corrida

Se armar as sete `*_ENABLE_INTERRUPT` provocar tempestade de fault, a máquina pode travar
durante o ~1 s de polling — e é **exatamente aí** que as linhas `IB_QUEUE_*` são escritas. Sem
TDR (item 25), perderíamos justo o dado que fomos buscar. **Banca a resposta da 2 primeiro,
arma a 3 depois.**

### Comandos

```bash
powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm
```

```bash
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1
```

Boot frio antes do bind (estado de engine não cai com desbind; só o cold boot limpa).

### O que ler no log

| Linha | O que decide |
|---|---|
| `IB_QUEUE_STATUS: … IDLE=?` | **`IDLE=0`** confirma a hipótese (parou dentro do IB); `IDLE=1` a **refuta** |
| `IB_QUEUE_DECODED: … IB_RPTR=? dw IB_OFFSET=? dw` | se diferirem de 8/0, o `0x20` de 05/09 **não** era constante — mata a dúvida do doc 168 §5.3 |
| `IB_QUEUE_STATUS: … EXCEPTION=?` | não-zero seria a resposta direta, e ninguém tinha olhado |
| `IB_QUEUE_STATUS: … BITS_SEM_NOME=?` | o bit 1 sem nome no upstream, se aparecer |
| `ROOT_MC: DIVERGENCIA` ou `concorda` | se a constante `0x450000000` envelheceu |
| `UMDRIVERPRIVATE: GpuVaBits=34, PageTableLevels=2` | confirma o A11.1 no silício |

### ⚠️ Desfecho possível que precisa ser declarado, não maquiado

O `INDIRECT` só é alcançado quando **não** há fill pendente, o buffer **não** tem 4 bytes, e a
GPUVA do IB resolve sob VMID 8 — os dois `PLANO A` têm **precedência**
(`bc250_active_miniport.c:6198-6242`):

1. `PLANO A DIRECT FILL` — se há fill pendente e `destPhysStart != 0`;
2. `PLANO A SYNC NOP` — se `DmaBufferSize == 4`;
3. `INDIRECT` — só se `ibWalkOk`;
4. senão, `RECUSA` sem tocar o silício.

**Se todas as submissões forem absorvidas por `PLANO A`, não sai nenhuma linha `IB_QUEUE_*`.**
Isso ainda é informação — diz que o `INDIRECT` não está sendo exercitado no perfil ativo — mas
é fraca. **Declare isso em vez de inventar leitura.**

---

## 12. Fila de trabalho depois desta corrida

| # | Item | Natureza |
|---|---|---|
| 1 | **Analisar a corrida** e decidir Q2 (confirmada/refutada) | análise |
| 2 | **Armar `0x007FFE03`** e submeter o mesmo IB — o experimento binário da Q3 (§6) | corrida, **decisão do dono** |
| 3 | **Avaliar a branch `wddm2` do RADV** (Collabora/Valve, `gitlab.freedesktop.org/lfrb/mesa`) como base do item 146 | **offline** |
| 4 | **Corrida do perfil lab** para testar a correção de pivô no cenário 97 `IsSupportedVidPn` / 0 `CommitVidPn` — a de 22:42 usou o perfil **ativo**, que já convergia | corrida, exige rebuild do lab |
| 5 | **TDR (item 25)** — portão obrigatório de saída da P6. Sem ele, cada bug de shader de jogo é um reboot | desenvolvimento |

**O item 3 é o caminho do bloqueio A** (pipeline 3D), que é a barreira do vídeo. É offline e
não depende de nenhuma corrida — **é o que dá para fazer em paralelo** enquanto as corridas do
B1 são agendadas.

---

## 13. Veredito honesto

- **Perguntas 3, 4, 5 e 1 do doc 167:** **FECHADAS** por leitura, com proveniência. Zero
  corridas gastas.
- **Pergunta 2:** **ABERTA**, mas com dados e com hipótese `H` testável.
- **Faixas A11 e A12:** implementadas, compiladas, auditadas e travadas por teste offline.
- **Efeito de qualquer coisa desta sessão no silício:** **NÃO VALIDADO EM HARDWARE.**
- **Telemetria nova:** **NUNCA RODOU.**
- **Bloqueio A (pipeline 3D para o DWM):** intacto. Nada nesta sessão o toca.
- **Bloqueio B (IB sob VMID 8):** **nenhum dos parâmetros que poderíamos ter errado está
  errado** — `DEPTH`, `BLOCK_SIZE`, `PT_END`, aritmética de PA da PTE, hub de fault,
  invalidação e formato do `SDMA_PKT_INDIRECT` estão todos conferidos contra fonte e travados
  por teste. O que resta é estado de engine e observabilidade de fault.
- **Jogos, desktop visível, `vkCreateDevice`, submissão Node 1:** **NÃO VALIDADOS EM
  HARDWARE.**
