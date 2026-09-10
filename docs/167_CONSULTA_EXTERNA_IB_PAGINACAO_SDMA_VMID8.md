# Consulta técnica externa — IB de paginação não executa na SDMA0 sob VMID8

> **Alvo:** AMD BC-250 (`PCI 1002:13FE`, APU Cyan Skillfish 2, GC 10.1.3 / RDNA1, 24 CUs, 16 GB GDDR6)
> **Sistema:** Windows 11 Pro x64 · driver WDDM 2.0 próprio (`bc250mcdm.sys`), escrito do zero
> **Repositório:** `C:\Users\Daniel\Desktop\NamixWDDM`
> **Data:** 2026-09-05
> **O que peço:** ideias, hipóteses e caminhos de investigação. **Não modifique arquivos** — apenas leia e opine.

---

## 0. Como me ajudar

Estou preso há seis corridas de hardware num ponto muito específico. Você pode ler todo o
código do repositório. **Não altere nada.** O que eu quero é:

1. Hipóteses que eu não considerei para a falha da Seção 4.
2. Erros no meu raciocínio ou nas minhas conclusões — inclusive nas que eu declarei
   "provadas".
3. Conhecimento de SDMA 5.0 / GCVM do GC 10.1 que contradiga o que estou fazendo,
   preferencialmente com referência ao `amdgpu` upstream.
4. Qual seria **a próxima corrida** que você faria, e qual medição ela produziria.

Cada corrida no hardware custa uma tela preta, um reboot e uma recuperação manual. Por
isso o critério é: **uma corrida por hipótese, com medição nomeada antes de armar.**

---

## 1. Regras do projeto (relevantes para a sua resposta)

O repositório opera sob leis rígidas. As que importam aqui:

- **Sinceridade absoluta.** Se algo não tem prova física, escreve-se literalmente
  `NÃO VALIDADO EM HARDWARE`. Não arredondar para cima.
- **Tripé de validação no silício.** Só se declara sucesso com as três provas:
  (1) `RPTR == WPTR` na engine; (2) fence física gravada **pela GPU** na VRAM,
  substituindo um valor *poison* previamente escrito pela CPU; (3) dados de destino
  conferidos **byte a byte** contra gabarito da CPU.
- **Zero fakes.** Nunca incrementar fence pela CPU, nunca `Sleep` simulando execução,
  nunca `STATUS_SUCCESS` em DDI sem backend real.
- **Endereços só com proveniência.** Offsets de registrador só do catálogo, com fonte
  citada (normalmente `amdgpu` upstream + headers `gc_10_1_0_*`).
- **Vocabulário de confiança:** `P0` provado no silício · `P1` derivado do upstream ·
  `P2` parcialmente observado · `H` hipótese · `Q` quarentena (proibido usar).

Se você propuser algo, por favor classifique com esse vocabulário.

---

## 2. Arquitetura em uma tela

- O driver **herda o framebuffer do UEFI GOP** (2560x1080 @ 56,13 Hz). Não implementamos
  DCN; escrita em registrador DCN é proibida nesta fase. O scanout fica no **offset 0 da
  VRAM**, e essa região é reservada (fica fora do segmento que entregamos ao VidMm).
- `DxgkDdiPresent` faz **BLT por SDMA0** da alocação primária para o scanout.
- Dois nós de submissão: **Node 0 = SDMA (cópia/paginação)**, Node 1 = compute.
- **GPU VA de 34 bits, 2 níveis** (`GPUMMUCAPS`: `VirtualAddressBitCount=34`,
  `PageTableLevelCount=2`). Root com 13 bits de índice (8192 entradas, 64 KiB), folha com
  9 bits (512 entradas, 4 KiB), página de 4 KiB.
- Paginação usa **VMID 8**, com a root page table do processo instalada pela GCVM.

### Arquivos centrais

| Papel | Arquivo |
|---|---|
| Miniport ativo, submissão, gate de tradução | `driver/wdk/bc250_active_miniport.c` |
| Callbacks MCDM, `BuildPagingBuffer`, encoder de PTE | `driver/wdk/bc250_mcdm_callbacks.c` |
| Anel SDMA0, tripé, `ExecuteIndirect` | `driver/wdk/bc250_sdma_present.c` |
| Construtor de pacotes SDMA | `driver/core/bc250_sdma_packet_builder.c` |
| Instalação do VMID 8 na GCVM | `driver/core/bc250_gcvm_vmid8_transaction.c` |
| Codificação de PTE / flags | `driver/core/bc250_gcvm_contract.c/.h` |
| Geometria de page table | `driver/core/bc250_gpuva_space_contract.h` |

---

## 3. O que já está provado no silício (P0)

Isto **não** é hipótese. Cada item tem evidência arquivada em `docs/evidence/`.

1. **Boot tripod da SDMA0.** A engine é armada, consome 68 bytes de pacotes do anel e
   grava a fence física na VRAM, substituindo o poison `0x43DAFFFE` por `0x00001001`,
   **em 1 poll**. Isso acontece em **toda** corrida, inclusive nas que falham depois.
   ```
   [SdmaPresent] Post-Kick SDMA0: WPTR_Reg=68, RPTR_Reg=0, STATUS=46DC7042
   [SdmaPresent] TRIPÉ NO SILÍCIO COMPROVADO! WPTR=68 RPTR=52 Fence=00001001 Dados=4/4 OK em 1 polls!
   ```
   > Observação: o tripé de boot passa com `RPTR=52 != WPTR=68`. A fence e os dados é que
   > o validam.

2. **BLT de apresentação com byte proof.** 30 transferências de 11.059.200 bytes da
   primária para o scanout, cada uma com `RPTR==WPTR`, fence gravada e verificação
   **byte a byte**. O monitor acendeu fisicamente. Isso usa **endereçamento físico MC,
   sem tradução**: origem em `0xF400AD0000`, ou seja, base MC `0xF400000000` + offset de
   VRAM `0xAD0000`.

3. **Negociação VidPn/GOP, `CommitVidPn`, `SetVidPnSourceAddress`, 30 `DxgkDdiPresent`.**

4. **Instalação do VMID 8 com readback.** Os 6 registradores de endereço e o `CNTL`
   são escritos e relidos com sucesso, e a TLB é invalidada com `AckObserved=1`.

5. **`UpdatePageTable` e `FlushTlb`.** 141 operações `UpdatePageTable` e 3 `FlushTlb`
   por corrida, todas retornando sucesso, com ack de flush observado em 1 poll.

---

## 4. O problema

**Um IB (indirect buffer) de paginação submetido à SDMA0 sob VMID 8 nunca executa.**

O VidMm pede um `DXGK_OPERATION_VIRTUAL_FILL` para zerar a alocação primária
(11.075.584 bytes). Nós codificamos o DMA buffer, o VidMm chama
`DxgkDdiSubmitCommandVirtual`, e nós publicamos um pacote `INDIRECT` no anel da SDMA0
apontando para a GPUVA daquele buffer, com VMID=8.

**A fence que vem depois do `INDIRECT`, no próprio anel, nunca é gravada.**

### 4.1 Layout exato do anel (aritmética conferida)

O boot tripod deixa o cursor em **DW 17** (68 bytes). Então `ExecuteIndirect` emite:

| Conteúdo | DW | Observação |
|---|---|---|
| NOP de pré-alinhamento | 17 | `pad = (2 - 17) & 7 = 1` |
| `INDIRECT` (6 DW) | 18–23 | termina em **DW 24**, e `24 % 8 == 0` ✅ |
| `GCR` + `FENCE` (9 DW) | 24–32 | |
| **WPTR final** | **DW 33 = 132 bytes** | confere com `TargetWPTR=132` medido |

A regra de alinhamento vem do upstream (`sdma_v5_0_ring_emit_ib`: o `INDIRECT` de 6 DW
precisa terminar em fronteira de 8 DW; a solução é inserir `(2 - wptr_dw) & 7` NOPs).

### 4.2 O IB, byte a byte, como a GPU o vê

Despejado pela CPU imediatamente antes do disparo:

```
IB_DUMP DW00: 0000020B 00260000 00000000 00000000
IB_DUMP DW04: 003FFFFF 0000020B 00660000 00000000
IB_DUMP DW08: 00000000 003FFFFF 0000020B 00A60000
IB_DUMP DW12: 00000000 00000000 0028FFFF 00000000
IB_SHAPE: UsefulDW=15 PadDW=1 PublishedDW=16 PublishedBytes=64
```

Decodificação: três `CONST_FILL` (opcode `0x0B`), destinos `0x260000` → `0x660000` →
`0xA60000`, tamanhos `0x400000 + 0x400000 + 0x290000 = 11.075.584` — exatamente o
`FillSize` pedido. Seguidos de um NOP de cauda (`0x00000000`).

> O NOP de cauda existe só para o **tamanho publicado ao VidMm** ser múltiplo de 8 DW,
> de modo que a GPUVA do *próximo* paging buffer caia em fronteira de 32 bytes (exigência
> do campo base do `INDIRECT`). O campo `IB_SIZE` do `INDIRECT` recebe **15**, não 16 —
> os NOPs de cauda não precisam ser buscados. Ver Seção 5, corrida A/B.

### 4.3 Estado da tradução, conferido pela CPU antes de submeter

Implementei uma caminhada de page table na CPU que lê **as mesmas estruturas que a GPU
lê** (`Bc250ActiveWalkGpuVa` em `bc250_active_miniport.c`). Última corrida:

```
RootMC=0xF400A90000  VramMcBase=0xF400000000  LocalSegBase=0xA90000

IB   L1 idx=1   VRAM+0xA90008  PDE=000000F400A91061 -> leafMC=0xF400A91000  flags=0x61 valid=1
IB   L0 idx=79  VRAM+0xA91278  PTE=00000002474570E7 -> pageMC=0x247457000   flags=0xE7 valid=1
IB   RESOLVIDO: VA=0x24F000 -> 0x247457000

DEST faixa de escrita [0x260000, 0xCF0000)
DEST L0 idx=96  PTE=000000F400AD0061 -> pageMC=0xF400AD0000  valid=1   (VA=0x260000)
DEST L0 idx=424 PTE=000000F401018061 -> pageMC=0xF401018000  valid=1   (VA=0x7A8000)
DEST L0 idx=239 PTE=000000F40155F061 -> pageMC=0xF40155F000  valid=1   (VA=0xCEF000)
```

Tudo resolve. O IB está em memória de sistema (`flags=0xE7`, bit `SYSTEM` ligado —
endereço de sistema corretamente **sem** base de VRAM). O destino está em VRAM
(`flags=0x61`), e `VA=0x260000 -> 0xF400AD0000` bate exatamente com o endereço MC da
primária que os blts da Seção 3.2 provaram byte a byte.

### 4.4 O resultado

```
IB_ALIGN: StartDW=17 PadDW=1 IndirectEndDW=24 EndModulo8=0 TargetWPTR=132 IbSizeDW=15 PublishedDW=16
IB_TRIPOD TIMEOUT: TargetWPTR=132, WPTR=132, RPTR=132, STATUS=46DEE056, ERR_LOG=0000000F,
                   FenceExp=00001001, FenceObs=43DAFFFE
RPTR_SAMPLES (n=8, stride=25000 polls): 132 132 132 132 132 132 132 132
GFXHUB_FAULT_SNAPSHOT: STATUS=00000000 ADDR=00000000:00000000
```

- `RPTR` alcança `WPTR`.
- **A fence continua no poison.** `0x43DAFFFE` intacto.
- Nenhum fault latchado no GFXHUB.
- `RPTR` já estava em 132 na primeira amostra (~125 ms), então **não é lentidão**.

A fence é o **último pacote do anel**, executada sob o VMID do anel (físico, sem
tradução), escrevendo num endereço MC fixo. É exatamente o mesmo tipo de pacote, para o
mesmo endereço, que o boot tripod grava com sucesso em 1 poll na mesma corrida.

---

## 5. Histórico completo das corridas

Todas na mesma noite, boot frio entre cada uma, uma variável por corrida.

| # | Hora | Mudança | `IB_SIZE` | Estado das page tables | `RPTR` | Fence |
|---|---|---|---|---|---|---|
| A | 22:27 | padding do `INDIRECT` + NOP de cauda buscado | 16 | root apontava 0xA90000 antes da tabela | **96** | poison |
| B | 23:23 | `IB_SIZE` passa a excluir o NOP de cauda | 15 | idem | **132** | poison |
| C | 00:55 | root corrigida (`+LocalSegmentBaseOffset`) | 15 | PDE com offset **cru**, folha inalcançável | **132** | poison |
| D | 01:12 | encoder soma base MC nos endereços de VRAM | 15 | root=MC, folha=MC, base `0x450000000` | **96** | poison |
| E | 01:21 | valida também o destino; teto 50 ms → 1 s | 15 | idem D, 3 páginas do destino OK | **132** | poison |
| F | 01:29 | base MC trocada para `0xF400000000` | 15 | idem, no espaço provado por byte proof | **132** | poison |

Cada correção intermediária foi um bug real, confirmado por medição:

- **A→B:** com `IB_SIZE=16` (NOP de cauda sendo buscado) a engine parava em `RPTR=96`,
  exatamente o fim do `INDIRECT`. Com 15, o anel passou a drenar.
- **C:** a root era programada como `vramBaseOffset + Address`, mas `Address` é o offset
  **dentro do segmento 1**, e o segmento 1 começa em `LocalSegmentBaseOffset = 0xA90000`
  (a reserva do scanout: `2560*1080*4` arredondado para 64 KiB). O ponteiro apontava
  0xA90000 bytes antes da tabela real, dentro do framebuffer.
- **D:** o codificador de PTE gravava `LocalSegmentBaseOffset + pageOffsetInSegment`, um
  **offset cru de VRAM**, sem a base MC. A GPU caminha em MC, então lia o PDE
  `0x00A91000` e ia buscar a folha no MC `0x0000A91000`, que não é VRAM.
- **F:** troquei a base de `0x450000000` (leitor de topologia, nunca validado por acesso)
  para `0xF400000000` (provado por byte proof nos blts). Sem efeito.

### 5.1 O invariante

**Em nenhuma das seis corridas a fence pós-`INDIRECT` foi gravada** — nem quando a
tradução estava comprovadamente quebrada (C), nem quando estava comprovadamente coerente
(E, F). E o boot tripod, que **não tem `INDIRECT`**, grava a fence em 1 poll em todas.

`RPTR` alternando entre 96 e 132 entre corridas com o mesmo binário lógico (D vs E)
sugere não-determinismo.

### 5.2 O que NÃO é diagnóstico

Os deltas de UTCL1 vieram **byte a byte idênticos** entre as corridas E e F, que tinham
page tables completamente diferentes (`0x450...` vs `0xF400...`):

```
UTCL1_DELTA: RD_STAT=000C0000 WR_STAT=00000000 RD_XNACK=0045FC00/00181A80 WR_XNACK=00000000/00343400
```

Conclusão: esses registradores não estão refletindo a nossa submissão. Eu os interpretei
como evidência de atividade de tradução em análises anteriores — **isso foi leitura
excessiva do dado.** Não os use como sinal.

---

## 6. Minha hipótese atual (H, não causa)

`SDMA0_CNTL` é programado com `MIDCMD_PREEMPT_ENABLE` ligado
(`bc250_sdma_present.c:300`):

```c
value | F_CNTL_UTC_L1 | F_CNTL_MIDCMD
```

E todo `INDIRECT` que emitimos passa **CSA = 0**
(`Bc250SdmaEmitIndirect` em `bc250_sdma_packet_builder.c`):

```c
Bc250SdmaEmitIndirect(packets, 64u, &cursor, DmaBufferGpuVa, ibSizeDwords,
                      0ull /* CsaAddress */, Vmid);
```

Os dois últimos DWORDs do `INDIRECT` do SDMA 5.0 são o endereço da Context Save Area.
No upstream, `amdgpu_sdma_get_csa_mc_addr()` só devolve 0 quando a preempção no meio do
comando está desabilitada ou o VMID é 0. Nós temos preempção **ligada** e VMID **8**.

Isso explicaria o invariante da Seção 5.1: o defeito estaria preso ao pacote `INDIRECT`,
não ao conteúdo do IB nem às page tables — e o boot tripod funciona justamente por não
emitir `INDIRECT`.

**Próximo passo planejado:** desligar `F_CNTL_MIDCMD` (uma variável, reversível numa
linha, e preempção não é necessária para paginação). Se não resolver, alocar uma CSA real
no work buffer.

---

## 7. Perguntas específicas

1. **CSA:** a hipótese da Seção 6 procede para SDMA 5.0 no GC 10.1? Com
   `MIDCMD_PREEMPT_ENABLE=1`, a engine escreve na CSA ao entrar num IB de VMID != 0?
   Um CSA nulo faz a engine parar silenciosamente, sem latchar fault?

2. **A fence que não grava:** que estado da SDMA 5.0 faz `RPTR` alcançar `WPTR` **sem**
   executar os pacotes intermediários? Existe um modo halt/skip em que o fetch pointer
   avança e a execução não? Que registrador exporia isso? Nosso `STATUS` lido é
   `0x46DEE056` no caso de drenagem e `0x46DE6756` no caso de parada em 96 (o valor no
   boot, antes do kick, é `0x46DEE557`).

3. **Fault silencioso:** `GCVM_L2_PROTECTION_FAULT_STATUS` do GFXHUB lê `0x00000000` em
   todas as corridas. Estamos lendo o hub errado (MMHUB em vez de GFXHUB)? A SDMA no
   GC 10.1 roteia por qual hub? Existe registrador de fault específico da SDMA que
   deveríamos ler em vez desse?

4. **Geometria do VMID 8:** programamos `GCVM_CONTEXT8_CNTL = 0x00555403`, que decodifica
   como `ENABLE_CONTEXT=1`, `PAGE_TABLE_DEPTH=1`, `PAGE_TABLE_BLOCK_SIZE=0` (= 9 bits),
   mais os defaults de fault `0x00555400`. `PAGE_TABLE_START_ADDR = 0` e
   `PAGE_TABLE_END_ADDR = 0x3FFFFF` (interpretados como PFN de 4 KiB, cobrindo os 34
   bits). Isso está correto para root de 13 bits + folha de 9 bits? Em particular:
   `PAGE_TABLE_DEPTH` deve ser 1 ou 2 nessa convenção?

5. **Bases MC:** o leitor de topologia devolve `VramBaseOffset = 0x450000000`, mas o anel
   da SDMA0 e os blts provados por byte proof usam `0xF400000000`. Ambas podem ser
   aperturas legítimas? Qual delas deve aparecer numa **entrada de page table** do GC
   10.1? (Testei as duas; nenhuma mudou o resultado.)

6. **Formato do `INDIRECT`:** nosso pacote é
   `header = OP_INDIRECT | (vmid << 16)`, seguido de base_lo (mascarado com
   `0xFFFFFFE0`), base_hi, `ib_size` em dwords, csa_lo, csa_hi. Está correto para
   SDMA 5.0? O campo `vmid` fica mesmo no bit 16 do header?

7. **Alguma coisa que eu declarei "provada" e não está?** Especialmente: eu trato o
   boot tripod e os 30 blts como P0. Se houver furo nesses, quero saber.

---

## 8. Como reproduzir a leitura

O log da última corrida está em `C:\BC250_DISPLAY_LOG.txt` (~90 KB, 1157 linhas). Filtro
útil:

```powershell
Select-String -LiteralPath C:\BC250_DISPLAY_LOG.txt -Pattern `
  'PT_WALK|ROOT_MC|IB_DUMP|IB_SHAPE|IB_ALIGN|IB_TRIPOD|RPTR_SAMPLES|UTCL1_|GFXHUB_FAULT|IB NAO EXECUTADO'
```

Evidências arquivadas:

```
docs/evidence/2026-09-04_P6_PAGING_IB_SHAPE_PUBLICATION_OFFLINE/README.md
docs/evidence/2026-09-05_P6_PT_WALK_GATE_OFFLINE/README.md
docs/evidence/2026-09-05_P6_PTE_VRAM_MC_BASE_OFFLINE/README.md
docs/166_ANALISE_E_HANDOFF_ARTEFATOS_TELA.md
```

---

## 8b. FECHAMENTO PARCIAL POR LEITURA (07/09) — perguntas 1 e 6

Feito **offline, sem corrida**, relendo o nosso próprio código contra o comportamento
conhecido do upstream. Classificação: **`H`**, não `P1` — o que segue sobre a *lógica* do
amdgpu é upstream **recordado**, não lido.

> ⚠️ **CORREÇÃO (07/09), e ela importa para classificar as próximas respostas.** Escrevi
> acima que *"este repositório não tem cópia local do amdgpu"*. **Falso, e a distinção é
> útil:** existe [`inc/linux_ref/gc_10_1_0_sh_mask.h`](../inc/linux_ref/gc_10_1_0_sh_mask.h),
> que é a tabela de **shifts e máscaras** do GC 10.1.0 do upstream, presente no repositório.
>
> Logo, a regra correta de classificação é:
>
> | Tipo de pergunta | Fonte local | Classe alcançável |
> |---|---|---|
> | *"quais são os bits do campo X?"* | `gc_10_1_0_sh_mask.h` | **`P1`** |
> | *"qual offset tem o registrador X?"* | `docs/03` | **`P1`** |
> | *"o que a função `foo()` do amdgpu decide?"* | nenhuma — não há `.c` do amdgpu | **`H`** |
>
> As respostas das perguntas 1 e 6 abaixo são sobre **lógica** (`amdgpu_sdma_get_csa_mc_addr()`,
> `sdma_v5_0_ring_emit_ib`), então seguem `H`. Mas foi por causa dessa correção que a
> semântica de `CONTEXT_STATUS` e `SKIP_CNTL` — pendente desde `docs/168` — fechou como `P1`
> em vez de continuar aberta. Ver `docs/03`, "Campos do bloco — decodificados em 2026-09-07".

### Pergunta 1 (CSA) — a hipótese da seção 6 está muito provavelmente ERRADA, e o
### experimento planejado NÃO deve ser gasto numa corrida

A seção 6 propôs: *"desligar `F_CNTL_MIDCMD`; se não resolver, alocar uma CSA real"*.

Duas coisas mudaram desde então:

1. **`F_CNTL_MIDCMD` já saiu do `CNTL`.** Hoje `bc250_sdma_present.c` só faz
   `value | F_CNTL_UTC_L1` — o `#define F_CNTL_MIDCMD` ficou órfão no arquivo. A preempção no
   meio do comando está **desligada**.
2. No upstream, `amdgpu_sdma_get_csa_mc_addr()` devolve **zero** quando a preempção no meio
   do comando não está habilitada (além dos casos de SR-IOV e `vmid == 0`).

**Consequência:** com MIDCMD desligado, `CsaAddress = 0` é o valor **correto**, e não uma
omissão. Alocar uma CSA real seria divergir do upstream sem motivo, e gastaria uma corrida
física para testar algo que a leitura já indica ser desnecessário.

**Recomendação:** **não** implementar a CSA. A pergunta 1 deixa de ser candidata a
experimento até que alguém religue MIDCMD por uma razão declarada.

### Pergunta 6 (formato do `INDIRECT`) — confere com o layout padrão

Nosso `Bc250SdmaEmitIndirect` emite exatamente os seis dwords de `SDMA_PKT_INDIRECT`:

```text
[0] header  = OP_INDIRECT(4) | (vmid << 16)
[1] base_lo = IbAddress[31:0] & 0xFFFFFFE0     (alinhamento de 32 B)
[2] base_hi = IbAddress[63:32]
[3] ib_size = tamanho em DWORDS
[4] csa_lo  = 0
[5] csa_hi  = 0
```

Constantes locais conferidas: `BC250_SDMA_OP_INDIRECT = 4`,
`BC250_SDMA_IB_ALIGNMENT_BYTES = 32`, `BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS = 8`. E o
pré-pad `(2 - wptr) & 7` corresponde ao `amdgpu_ring_insert_nop(ring, (2 - wptr) & 7)` do
upstream — o pacote de 6 dwords termina em fronteira de 8.

**Isto agora está travado por teste offline**, no bloco 6b de
`driver/tests/sdma_packet_builder_test.c`: os seis dwords são conferidos valor a valor para
uma entrada conhecida, mais as recusas de VMID fora de faixa, tamanho zero, tamanho absurdo,
IB desalinhada e CSA desalinhado. **O pacote não pode mais mudar em silêncio.**

### Pergunta 4 (`PAGE_TABLE_DEPTH` do VMID 8) — JÁ ESTAVA RESPONDIDA NO NOSSO PRÓPRIO CATÁLOGO

Esta pergunta não precisava de consulta externa nem de corrida: a resposta está em
[`docs/03_CATALOGO_DE_REGISTRADORES.md`](03_CATALOGO_DE_REGISTRADORES.md), seção
**"Derivação Matemática da Geometria (34 bits, 2 níveis)"**, com classificação **`P1`** e
proveniência citada (`gfxhub_v2_0_setup_vmid_config()`, máscaras de `gc_10_1_0_sh_mask.h`).

A convenção do GCVM é *níveis abaixo da raiz*:

| `PAGE_TABLE_DEPTH` | Hierarquia |
|---:|---|
| `0` | 1 nível — tabela plana / GART, PTE direta |
| `1` | 2 níveis — raiz PDE0 → folha PTE |
| `2` | 3 níveis |
| `3` | 4 níveis |

Nossa hierarquia é de 2 níveis, logo **`PAGE_TABLE_DEPTH = Levels - 1 = 1`** — e é
exatamente isso que o driver programa. **Confirmado por leitura de código em 07/09:**

- `BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE = 0x00555403` (`bc250_gcvm_vmid8_transaction.h:72`)
  — os bits `[2:1]` valem 1;
- `PAGE_TABLE_BLOCK_SIZE = block_size - 9 = 0`, porque a folha tem 9 bits de índice;
- `BC250_GCVM_CONTEXT8_PT_END_LO_VALUE = 0x003FFFFF` = `2^22 - 1`;
- os quatro estão travados por `driver/tests/gcvm_context8_cntl_test.c`.

⚠️ **A pergunta 4 está FECHADA e sai da lista.** O que ela cobra do B1 já está implementado
e travado por teste — **não é aqui que o IB falha**. Gastar corrida variando
`PAGE_TABLE_DEPTH` seria testar o que a nossa própria fonte já derivou.

> **Lição de processo, não técnica:** a pergunta ficou aberta neste documento por dias
> enquanto a resposta estava catalogada com `P1` no repositório. Antes de escalar pergunta
> de registrador para fora, **procure em `docs/03` primeiro** — é o que a Lei 9 manda.

### Achados colaterais desta leitura (07/09, offline, sem corrida)

Procurar a resposta da pergunta 4 no repositório expôs **dois defeitos nossos** na mesma
geometria. Nenhum é o B1, mas os dois produziriam o sintoma do B1 se acionados:

1. **Segunda fonte de verdade para `GpuVaBits`.** O blob `DXGKQAITYPE_UMDRIVERPRIVATE`
   publicava `BC250_ADAPTER_GPUVA_BITS` (= **48**) enquanto o mesmo driver declarava
   `VaBits=34` ao WDDM — medido no log da corrida 11:08:
   `[McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2`. A UMD (e, na rota B, o winsys do RADV) dispõe
   endereços dentro do que o blob promete; prometer 48 bits sobre uma hierarquia que traduz
   34 produz endereço sem PTE alcançável. **Corrigido:** o blob passou a derivar de
   `runtimeCaps`, a mesma estrutura que alimenta o `GPUMMUCAPS`, e
   `BC250_ADAPTER_GPUVA_BITS` deixou de ser literal — agora é `BC250_GPUVA_PT_VA_BITS`.
   Travado por gate estático (`audit_gpuva_geometry_single_source`, com teste de mutação) e
   por asserção de repasse em `mcdm_callbacks_test`.

2. **Teto errado no journal de GPUVA — fail-open.** `Bc250GpuvaJournalStageLeafUpdate`
   recusava VA a partir de `BC250_GPUVA_MAX_LIMIT` = `2^48`, o teto **arquitetural**, não o
   **traduzível** (`2^34`). Aceitava registrar página num VA que o contexto VMID8 não
   alcança. Não era acionável pelo alocador vigente — `Bc250GpuvaSpaceInitialize` é chamado
   com `BC250_GPUVA_PT_VA_BITS` — mas era porta aberta, e o sintoma seria indistinguível do
   B1. **Corrigido** com `BC250_GPUVA_TRANSLATABLE_LIMIT` e travado por teste (última página
   traduzível entra; a primeira fora, a faixa que atravessa o limite e o VA de 8 TB não).

Também reescrito: o bloco de PROVENIÊNCIA no topo de `bc250_gpuva_space_contract.h` ainda
descrevia o desenho **anterior** (plano, `PAGE_TABLE_DEPTH = 0`, 22 bits de índice) logo
acima das constantes de 2 níveis que o substituíram. Sob a Lei 7 isso é armadilha: era a
proveniência de registro, e estava falsa. O texto antigo ficou marcado como histórico.

⚠️ **Nada disto foi validado em hardware.** São mudanças de contrato, gate e teste offline.

### Pergunta 5 (qual base deve aparecer numa PTE do GC 10.1) — TAMBÉM RESPONDIDA NO CATÁLOGO

A pista acima se confirmou. `docs/03` responde a pergunta 5 com `P1`, e a pergunta está mal
formulada no seu próprio enunciado: **não é base "MC" nenhuma.**

| O quê | Valor | Fonte |
|---|---|---|
| PA de página de VRAM | `MC - vram_start + vram_base_offset` | `amdgpu_gmc_vram_mc2pa()` |
| `vram_base_offset` | `GCMC_VM_FB_OFFSET << 24` | `gfxhub_v2_0.c:117`, `get_mc_fb_offset()` |
| medido nesta placa | `GCMC_VM_FB_OFFSET = 0x450` → `0x450000000` | baseline de 2026-08-23 |

Ou seja: a PTE carrega **PA**, e a conta é `PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24)`.
A base **MC** (`0xF400000000`, de `GCMC_VM_FB_LOCATION_BASE`) é de **outro domínio** — anel e
blts físicos —, e trocar uma pela outra já foi tentado: a corrida de 05/09 01:29 fez essa
troca por causa da prova byte a byte dos blts, o que foi **erro de domínio**, não mudou nada,
e foi revertido. O defeito real daquela corrida estava nas flags do PDE.

O código já implementa exatamente isso, com o comentário de domínio preservado em
`bc250_gcvm_contract.h:13-28`. **A pergunta 5 está FECHADA e sai da lista.**

Achado colateral: o catálogo afirmava que *"o driver lê `GCMC_VM_FB_OFFSET` do silício em cada
construção; não usa constante"* — **falso**, e normativo. O leitor de topologia lê, mas o
valor usado é a constante `BC250_GCVM_VRAM_MC_BASE_FALLBACK`, deliberadamente (a base do
leitor "nunca fez um acesso funcionar"; a constante tem prova byte a byte). Corrigido no
catálogo, e o `SetRootPageTable` passou a **logar `ROOT_MC: DIVERGENCIA`** se o valor lido do
silício deixar de concordar com a constante — sem mudar comportamento.

### Pergunta 3 (o hub de fault está certo?) — SIM, ESTÁ. E isso reabre a pergunta mais afiada

O catálogo responde direto, com fonte:

> *"E a SDMA traduz por qual hub? Pelo **GFXHUB**. Não é dedução: `sdma_v5_0.c` atribui
> literalmente `ring->vm_hub = AMDGPU_GFXHUB(0);`"*

E o caminho de invalidação foi conferido linha a linha contra o mesmo catálogo — **está
correto em todos os cinco pontos**:

| O que o catálogo exige | O que `Bc250PagingBuildGfxhubTlbFlushPlan` faz |
|---|---|
| `PER_VMID_INVALIDATE_REQ = 1 << vmid` | `1u << 8` → `RequestValue = 0x00F80100` ✅ |
| `CLEAR_PROTECTION_FAULT_STATUS_ADDR = 0` (não apagar evidência) | bit 24 em **0** ✅ |
| ACK no bit `1 << vmid` de `ENG17_ACK` | `EngineId = 17`, `AckMask = 1u << 8`, `0xA318` ✅ |
| leitura dummy do `REQ` obrigatória em GC < 10.3 | `RequiresDummyRequestRead = 1` ✅ |
| GFXHUB **não** usa semáforo | `UsesSemaphore = 0` ✅ |

**Logo o `FAULT_STATUS = 0` é medição de verdade, não artefato de hub errado nem de status
apagado por nós.** A pergunta 3, como estava escrita, **está FECHADA**.

#### E aí aparece a pergunta certa: nós DESLIGAMOS a notificação de fault

Contando os bits do nosso próprio valor alvo contra o POST medido — os dois já catalogados em
`docs/03`:

| Campo (7 classes de fault) | POST `0x007FFE80` | Alvo `0x00555403` |
|---|:---:|:---:|
| `*_PROTECTION_FAULT_ENABLE_DEFAULT` | 1 (7/7) | 1 (7/7) |
| `*_PROTECTION_FAULT_ENABLE_INTERRUPT` | **1 (7/7)** | **0 (0/7)** |

O que a própria tabela do catálogo diz que `ENABLE_DEFAULT = 1` significa:
**"Redireciona para dummy page default"**. Com `ENABLE_INTERRUPT = 0` junto, isso descreve
um hardware que, ao falhar a tradução, **redireciona o acesso para a dummy page e não avisa
ninguém**.

🔶 **HIPÓTESE (`H`) — não provada, e é a primeira explicação coerente com as três medições ao
mesmo tempo:** o IB pode estar sendo *executado* e falhando a tradução, com escritas
aterrando na dummy page. Isso produziria exatamente o tripé observado:

1. `RPTR` alcança o `WPTR` — a engine consumiu o pacote;
2. `FAULT_STATUS` lê zero — nenhuma interrupção de fault foi habilitada;
3. destino sem os dados — as escritas foram para a página default.

⚠️ **O que esta hipótese NÃO estabelece.** Não sei, e **não está nos nossos docs**, se
`GCVM_L2_PROTECTION_FAULT_STATUS` *latcha* quando `ENABLE_DEFAULT = 1` e
`ENABLE_INTERRUPT = 0`. Se latchar, a hipótese está **refutada** pela medição que já temos.
É por isso que ela é `H` e não `P1`. E note que o valor alvo **não é um erro nosso**: o
catálogo o deriva do setup padrão do upstream, onde as interrupções também não são
habilitadas nesse passo.

**O que fecha isto, e é barato:** habilitar as sete `*_ENABLE_INTERRUPT` no
`GCVM_CONTEXT8_CNTL` e submeter o mesmo IB que hoje falha. A conta, conferida:

```text
  máscara de interrupt = bits 9,11,13,15,17,19,21 = 0x002AAA00
  0x00555403 | 0x002AAA00                         = 0x007FFE03
```

Detalhe que dá confiança no valor: `0x007FFE03` difere do POST medido (`0x007FFE80`) em
**apenas `0x83`** — bit 0 (`ENABLE_CONTEXT`, que ligamos de propósito), bit 1
(`PAGE_TABLE_DEPTH = 1`, idem) e bit 7 (`RETRY_PERMISSION_OR_INVALID_PAGE_FAULT`, que o
upstream **desliga** de propósito em APU para suprimir tempestade de fault,
`gfxhub_v2_0.c:313`). Ou seja: o valor proposto é o POST do silício mais as nossas duas
decisões deliberadas, menos o no-retry que o upstream manda desligar. Não é um valor
inventado — é o estado de fábrica com a observabilidade preservada.

O decodificador de IV já existe e está travado por teste
(`INTERRUPT HANDLER CONTRACT`, IV de 32 B do Navi/GFX10). O critério é binário:

- **chega IV de fault** → a hipótese está **confirmada**, o IB executa e o defeito é de
  tradução — e o `FAULT_ADDR` diz *qual* endereço, o que aponta o defeito exato;
- **não chega IV nenhum** → a hipótese está **refutada**, a engine não executa o pacote, e o
  centro volta a ser a pergunta 2 (estado da engine).

Nos dois casos a corrida **produz informação**, o que não é verdade de nenhuma tentativa de
adivinhar valor de registrador. Custo: uma corrida. Mudança: um OR de bits num registrador já
catalogado, dentro da prioridade em que já escrevemos.

### Pergunta 2 (que estado faz o `RPTR` alcançar o `WPTR` sem executar) — deixou de ser vazia

Continua **aberta**, mas não é mais uma pergunta sem dados. Decodificando a telemetria que
`docs/168` já tinha arquivado — e que a própria seção 6 daquele documento declarou *não
interpretada* — apareceu um discriminante entre a variante que termina e a que não termina:

| Variante | `CONTEXT_STATUS` | Decodificado |
|---|---|---|
| `PHYS` (VMID 0, MC físico) | `0x0D` | `SELECTED` + **`IDLE`** + `EXPIRED` |
| `EXEC` (VMID 8, traduzido) | `0x09` | `SELECTED` + `EXPIRED`, **`IDLE = 0`** |

| Variante | `IB_CNTL` | Decodificado |
|---|---|---|
| `PHYS` | `0x00000101` | `IB_ENABLE=1`, `SWITCH_INSIDE_IB=1`, `CMD_VMID=0` |
| `EXEC` | `0x00080101` | `IB_ENABLE=1`, `SWITCH_INSIDE_IB=1`, `CMD_VMID=8` |

**Os dois `CONTEXT_STATUS` diferem em exatamente um bit — o `IDLE`. Os dois `IB_CNTL` diferem
em exatamente um campo — o `CMD_VMID`.** Medição `P0` (doc 168 §5.3) somada a decodificação
`P1` (`inc/linux_ref/gc_10_1_0_sh_mask.h`). Não é hipótese: é aritmética sobre leitura de
silício, e estava disponível esse tempo todo.

🔶 **HIPÓTESE (`H`):** sob VMID 8 a engine consumiu o pacote do **anel**, latchou `IB_BASE` e
`CMD_VMID`, e **parou dentro do IB** — não terminou, por isso `IDLE` continua 0. Isso desloca
a pergunta 2: o problema não parece ser *"não executou o pacote"*, e sim *"começou o IB e
travou"*. **E casa com a hipótese da pergunta 3** — travar buscando/traduzindo é exatamente o
que um fault redirecionado para a dummy page, sem interrupção habilitada, produziria.

⚠️ **O que isto NÃO resolve, e tem de ficar escrito:** a variante `PHYS` **foi a `IDLE`**, ou
seja **terminou**, e a prova de bytes dela **também falhou** (doc 168 §5.2). Sem tradução
envolvida, aquela falha **não pode** ser de GCVM. São **duas falhas diferentes** e esta
decodificação não decide nenhuma. Também não sei se `EXPIRED = 1` aqui é significativo ou se
é o estado normal de uma fila que drenou.

**O que ficou pronto para a próxima corrida, sem custo de corrida nenhum:** o
`SubmitCommandVirtual` do Node 0 passou a ler os oito registradores do bloco
`SDMA0_GFX_IB_*` **no caminho de timeout** e a emitir `IB_QUEUE_RAW`, `IB_QUEUE_DECODED`,
`IB_QUEUE_STATUS` e `IB_QUEUE_DISCRIMINANTE` — decodificados, com o cru ao lado, e com os
bits sem nome denunciados em vez de escondidos. Só leitura, nenhuma escrita, e fora do
caminho de sucesso para não pôr oito acessos de MMIO no `Present` a 60 Hz. Travado por
`driver/tests/sdma_queue_state_contract_test.c`, que confere as máscaras contra o header
**e** reproduz os dois valores medidos.

⚠️ **E ela não deve ser respondida por chute:** é semântica de registrador, e cada combinação
errada custa uma corrida e produz o mesmo sintoma. A diferença é que agora a corrida devolve
estado interpretado em vez de oito palavras hexadecimais.

📌 **Mas o que este passo mudou é grande:** das quatro perguntas que sustentavam o B1
(2, 3, 4, 5), **duas fecharam sem gastar corrida — e as duas fecharam com o nosso próprio
catálogo**, que já tinha as respostas com proveniência `P1`. As perguntas 4 e 5 não eram
lacunas de conhecimento; eram lacunas de **busca**. Antes de escalar pergunta de registrador
para consulta externa ou para o silício, **`docs/03` primeiro** — é literalmente o que a Lei 9
manda, e ignorá-la custou dias de "aberto" a duas perguntas já respondidas.

Consequência para o B1: `PAGE_TABLE_DEPTH`, `PAGE_TABLE_BLOCK_SIZE`, `PT_END_ADDR`, a
aritmética de PA da PTE e o formato do `SDMA_PKT_INDIRECT` estão **todos** conferidos contra
fonte e travados por teste offline. **O IB não falha em nenhum deles.** O que resta é estado
de engine (2) e observabilidade de fault (3).

---

## 9. Estado honesto

**Provado no silício:** SDMA0 física; BLT de apresentação com byte proof; GOP/VidPn;
instalação e readback do VMID 8; `UpdatePageTable`/`FlushTlb` com ack.

**NÃO VALIDADO EM HARDWARE:** execução de IB de paginação sob VMID 8; byte proof do
`FillVirtual`; composição real do desktop; UMD 3D (`Bc250Umd10CreateDevice` ainda
retorna `E_NOTIMPL`, em `driver/umd/bc250_d3d_umd.c`); jogos acelerados.

Obrigado por qualquer ideia. Mesmo "sua hipótese da Seção 6 está errada, e aqui está o
porquê" já vale uma corrida economizada.
