# 168 — Handoff técnico: o `INDIRECT` da SDMA0 e o bloqueio da paginação

> **Data:** 2026-09-05
> **Target:** AMD BC-250 (`1002:13FE`, Cyan Skillfish 2, GC 10.1.3 / RDNA1, 24 CUs, 16 GB GDDR6)
> **Sistema:** Windows 11 Pro x64 · driver WDDM 2.0 próprio (`bc250mcdm.sys`)
> **Repositório:** `C:\Users\Daniel\Desktop\NamixWDDM`
> **Para:** a próxima IA que assumir este trabalho

---

## 0. Leia isto primeiro

Este documento cobre **um dia de trabalho**: 7 corridas do driver de produção (cada
uma com tela preta, BugCheck e reboot) e 13 sessões do driver de laboratório.

**Resultado em uma frase:** nenhuma imagem na tela, mas o alvo mudou — o bloqueio
não é a tradução de memória como o handoff anterior supunha; é o pacote
`INDIRECT` da SDMA0, que não produz efeito nem com tradução nem sem.

**Antes de mais nada, leia a Seção 8.** Ela lista instrumentos de medição que
MENTEM neste projeto. Eu perdi corridas inteiras confiando neles, e três revisões
externas independentes tiveram que me corrigir.

Governança obrigatória: `AGENTS.md`, `BASE_DE_AVANCO.md`, `rules.md`,
`docs/164_ROADMAP_WDDM_JOGOS.md`. As leis não são decorativas — a Lei 2 (tripé no
silício) e a Lei 7 (endereços só com proveniência) foram violadas por mim neste
dia e produziram conclusões erradas.

---

## 1. Onde queremos chegar

Rodar jogos no Windows 11 na BC-250 usando as 24 CUs e os 16 GB de GDDR6, com o
monitor no DisplayPort da própria placa, via D3D12/Vulkan, **sem WARP e sem
fallback de software**.

Display v1 é por **herança do UEFI GOP** (2560x1080 @ 56,13 Hz). O scanout fica na
VRAM física em offset `0x0`. `DxgkDdiPresent` faz BLT por SDMA0. DCN está fora do
caminho crítico e não recebe escrita antes da prioridade P9.

---

## 2. Estado no início do dia

O handoff anterior (`docs/HANDOFF_2026-09-04_VMID.md`, `docs/166...`) dizia:

> falta executar o IB de paginação sob VMID 8; a instalação do VMID está feita,
> a primeira IB retirou em 22:16.

**Esse diagnóstico estava errado.** A "IB que retirou" em 22:16 provou apenas que a
fence **do anel** executou — e a fence do anel roda sob o VMID do anel, físico, sem
tradução. Nunca houve prova de que o conteúdo do IB executou. Ver Seção 8.2.

### Provado no silício, e que continua valendo

| Item | Prova |
|---|---|
| Boot tripod da SDMA0 | fence `0x00001001` substituindo poison `0x43DAFFFE` em 1 poll, dados 4/4 |
| BLT de apresentação | 30 transferências de 11.059.200 bytes, `BYTE_PROOF PASS`, monitor acendeu |
| Negociação VidPn/GOP | `CommitVidPn`, `SetVidPnSourceAddress`, 30 `DxgkDdiPresent` |
| `UpdatePageTable` / `FlushTlb` | 141 + 3 por corrida, `AckObserved=1` |
| Anel da SDMA0 no laboratório | `WRITE_LINEAR`, `CONST_FILL` e `COPY_LINEAR` com leitura de volta conferida |

> Os 30 BLTs usam **endereçamento físico MC**, sem tradução: origem em
> `0xF400AD0000`. Eles provam a engine e o caminho de escrita, **não** a GCVM.

---

## 3. As 7 corridas de produção

Todas terminaram no mesmo ponto: o primeiro `FillVirtual`. Zero `CommitVidPn`,
zero `Present`, BugCheck e reboot em todas.

| # | Hora | Mudança | `IB_SIZE` | `RPTR` | Fence |
|---|---|---|---|---|---|
| A | 22:27 | padding do `INDIRECT` + NOP de cauda buscado | 16 | 96 | poison |
| B | 23:23 | `IB_SIZE` exclui o NOP de cauda | 15 | 132 | poison |
| C | 00:55 | root corrigida (`+LocalSegmentBaseOffset`) | 15 | 132 | poison |
| D | 01:12 | encoder soma base MC nos endereços de VRAM | 15 | 96 | poison |
| E | 01:21 | valida o destino; teto 50 ms → 1 s | 15 | 132 | poison |
| F | 01:29 | base MC trocada para `0xF400000000` | 15 | 132 | poison |
| G | 09:07 | formato de PDE, bit de exec, base PA | 15 | 132 | poison |

### O BugCheck é determinístico

```
bc250mcdm!Bc250McdmSubmitCommandVirtual -> return STATUS_IO_DEVICE_ERROR
  -> BugCheck 0x119 VIDEO_SCHEDULER_INTERNAL_ERROR, Arg1=2, Arg2=0xC0000185
```

`0xC0000185` **é** o `STATUS_IO_DEVICE_ERROR` que devolvemos. **Não existe NTSTATUS
de erro aceitável em `DxgkDdiSubmitCommandVirtual`** — o video scheduler derruba a
máquina em qualquer um.

Tentei reportar por `DXGK_INTERRUPT_DMA_FAULTED`. Também derruba, com outro
subtipo:

```
dxgkrnl!DxgNotifyInterruptCB -> dxgmms2!VidSchDdiNotifyInterruptWorker
  -> watchdog!WdCriticalError
BugCheck 0x119, Arg1=0xD, Arg2=0, Arg3=0, Arg4=1 (o FenceId)
```

A submissão vinha de `dxgmms2!VidSchiSubmitPagingCommand` — é a fila de paginação.
**Revertido.** O motivo está registrado no ponto de falha em
`bc250_active_miniport.c` para não ser reintroduzido.

---

## 4. Defeitos reais encontrados e corrigidos

Todos verificados por leitura de fonte contra o upstream, todos com teste offline
nomeado. **Nenhum deles resolveu o bloqueio**, mas todos eram defeitos genuínos.

### 4.1 Ponteiro de root sem o offset do segmento — `P0`

`Bc250McdmSetRootPageTable` fazia `ptPhysical = vramBaseOffset + Address`, mas
`Address` é o offset **dentro do segmento 1**, e o segmento 1 começa em
`LocalSegmentBaseOffset`. Medido: `0x20000000 - 0x1F570000 = 0xA90000`, que é
`2560*1080*4` arredondado para 64 KiB — a reserva do scanout.

O ponteiro apontava 0xA90000 bytes antes da tabela real, dentro do framebuffer.
Corrigido para `vramBaseOffset + LocalSegmentBaseOffset + Address`.

### 4.2 Formato de PDE — `P1`, achado por revisão externa

Layout do Navi 10 (`gmc_v10_0.c`): PDE tem endereço em **`[47:6]`** e **`[5:3]`
reservados**. Gravávamos `VALID|READABLE|WRITEABLE = 0x61`. O **bit 6 não é
"writeable" num PDE — é o bit menos significativo do endereço**. O hardware lia a
tabela folha 64 bytes adiante.

Pior: `Bc250GcvmDecodePte` mascara com 4 KiB, então a caminhada pela CPU
descartava justamente esses bits e reportava o PDE como limpo. **O falso positivo
durou seis corridas.** Corrigido com par próprio `Bc250GcvmEncodePde`/`DecodePde`.

### 4.3 `EXECUTABLE` no bit errado — `P1`, achado por revisão externa

Layout do Navi 10: `11:7 fragment | 6 write | 5 read | 4 exe | 3 Z | 2 snooped |
1 system | 0 valid`.

`BC250_PTE_EXECUTABLE` era `0x80`, codificado em `(1ull << 7)` — ou seja, ligava
**`FRAG[0]`** e deixava a permissão de execução **sempre desligada**. A PTE do IB
saía `0xE7` em vez de `0x77`. Corrigido para o bit 4.

### 4.4 Domínio da base de endereço — `P1`

Page tables usam base **PA** (`GCMC_VM_FB_OFFSET << 24` = `0x450000000`), não a
base MC de `FB_LOCATION_BASE` (`0xF400000000`). O catálogo do próprio repositório
já dizia isso na linha 640; eu troquei para a base MC na corrida F argumentando com
o byte proof dos blts — prova que vale para acesso **físico**, não para tradução.
Erro meu, revertido.

### 4.5 Cabeçalho do `CONST_FILL` — `P1`, ainda **NÃO CORRIGIDO** na produção

O caminho do laboratório que passa com prova byte a byte monta
(`dbg/hw.c`, `BC250DBG_SDMA_OP_FILL`):

```c
SDMA_PKT_HEADER(op, subop) = (op & 0xFF) | ((subop & 0xFF) << 8)
pkt = SDMA_PKT_HEADER(CONST_FILL, 0) | ((FillSize & 3) << 30)
```

`sub_op = 0`, `FILLSIZE` nos **bits 31:30**. Fill de dword = **`0x8000000B`**.

`bc250_sdma_packet_builder.c:98` emite `0x0000020B`, que põe o `2` em `sub_op` e
deixa `FILLSIZE = 0` — e com 0 a SDMA replica apenas o **byte baixo** do padrão.

> **Tarefa aberta.** Os 30 BLTs validam `COPY_LINEAR`, nunca `CONST_FILL`.

### 4.6 `IB_ALIGN` emitido sob spinlock — `P0`

`Bc250DisplayLog` (`bc250_display_ddi.c:536`) só escreve em arquivo em
`PASSIVE_LEVEL`. O `IB_ALIGN` era emitido com `RingLock` adquirido, em
`DISPATCH_LEVEL`, e sumia do log. Corrigido: capturar em locais, logar após o
`KeReleaseSpinLock`.

---

## 5. O achado central: o `INDIRECT` não produz efeito

Reproduzido no laboratório, com controle positivo na mesma sessão.

### 5.1 O ensaio

`tools/test-vmid8-ib.ps1`. Ele monta root + folha de 2 níveis na VRAM, programa o
VMID 8, emite o `INDIRECT` e confere os bytes. Tudo por `reg w` / `vram w` /
`sdma raw`, **sem recompilar kernel**.

Ordem obrigatória, e **não reordene sem evidência** (Seção 8.5):

```
arm write -> sdma f32 -> sdma init -> CONTROLE POSITIVO -> gcvm dump
          -> variantes -> PARSETEST (por ultimo) -> sdma rollback
```

### 5.2 As variantes

| Variante | O que muda | Resultado |
|---|---|---|
| `PHYS` | VMID 0, IB no MC físico `0xF40F020000`, sem tradução | byte proof falhou |
| `EXEC` | VMID 8, PDE limpo, PTE do IB com bit 4 | byte proof falhou |
| `NOEXEC` | VMID 8, PTE do IB sem bit 4 | byte proof falhou |
| `DIRTY` | VMID 8, PDE `0x61` (defeito reproduzido de propósito) | byte proof falhou |

E o **controle positivo passou em toda sessão de boot frio**: `WRITE_LINEAR` com
fence gravada pela GPU e 4 dwords conferidos.

### 5.3 A telemetria que fecha o quadro

Bloco `SDMA0_GFX_IB_*` — registradores que **o software nunca escreve**; o
microcódigo do F32 é que os carrega ao consumir o `INDIRECT`:

```
PHYS   IB_BASE = 0x000000F40F020000   IB_CNTL = 0x00000101  -> CMD_VMID = 0
EXEC   IB_BASE = 0x0000000000201000   IB_CNTL = 0x00080101  -> CMD_VMID = 8
todas  IB_RPTR = 0x20   IB_SIZE = 0   CONTEXT_STATUS = 0x09 (VMID8) / 0x0D (PHYS)
```

**A engine decodifica o `INDIRECT` corretamente.** Carrega a base exata emitida e
propaga o VMID do pacote para `IB_CNTL.CMD_VMID`.

O que **não** acontece: o `CONST_FILL` dentro do IB nunca escreve, em nenhuma
variante, em nenhuma janela de leitura.

> **Cuidado:** `IB_RPTR = 0x20` e `IB_SIZE = 0` são **idênticos em toda variante e
> toda corrida**. Eu os interpretei como "a engine leu os 8 dwords". Isso **não
> está provado** — pode ser valor constante. Não repita essa inferência sem
> confirmar (ex.: emitir um IB de tamanho diferente e ver se `IB_RPTR` muda).

### 5.4 Hipóteses testadas e mortas

| Hipótese | Como morreu |
|---|---|
| `IB_ENABLE` desligado | lido em runtime: `IB_CNTL = 0x...0101`, bit 0 ligado |
| Alinhamento do `INDIRECT` | controlado e verificado: termina em fronteira de 8 DW em toda variante |
| Formato do pacote | idêntico a `sdma_v5_0_ring_emit_ib` |
| Firmware travado | F32 sadio: faixa `0x0146..0x03D7`, 18 endereços distintos |
| `UTCL1_CNTL` mal programado | `RESP_MODE=3`, `REDO_DELAY=9`, conferem com o POST |
| Coerência HDP (CPU→GPU) | `bc250ctl hdp` antes de cada submissão: **nenhuma mudança** |
| Permissão de execução | `EXEC` e `NOEXEC` deram idêntico |
| Formato do PDE | `EXEC` e `DIRTY` deram idêntico |
| Base MC vs PA | testadas as duas, idêntico |
| CSA / `MIDCMD_PREEMPT_ENABLE` | `get_csa_mc_addr()` devolve 0 para `vmid==0` por construção, então não explica a falha do `PHYS`. **Não testado com `MIDCMD` desligado** — continua candidata para VMID 8 |

---

## 6. O que ainda não foi tentado

1. **Desligar `F_CNTL_MIDCMD`** em `bc250_sdma_present.c:300` e no laboratório
   (`dbg/hw.c:1235`). Barato, uma linha, e é a última hipótese de CSA de pé.
2. **Passar uma CSA real** no `INDIRECT` em vez de 0.
3. **Reproduzir literalmente o `sdma_v5_0_ring_test_ib` do upstream**: IB com
   `WRITE_LINEAR` de 1 dword + 3 NOPs, `IB_SIZE=8`, fence pós-`INDIRECT` no anel.
4. **Variar o `IB_SIZE`** para descobrir se `IB_RPTR=0x20` é medição ou constante.
5. **`CONST_FILL` com cabeçalho `0x8000000B` dentro de um IB** — o ensaio já usa
   esse cabeçalho, mas o IB nunca executou, então nunca foi exercitado de verdade.
6. **Registradores ainda não lidos:** `SDMA0_GFX_IB_SUB_REMAIN`, e a semântica de
   bits de `CONTEXT_STATUS` e `SKIP_CNTL` (o `SKIP_CNTL` vem não-zero e variável
   nas variantes de VMID 8: `0x3CB3`, `0x3A`, `0x2A`, `0x1A` — **não sei o que
   significa e não interpretei**).

---

## 7. Os dois caminhos daqui

### (a) Consertar o `INDIRECT`

Continuar a Seção 6. Correto a longo prazo — o WDDM depende de submissão de DMA
buffer, e o `INDIRECT` é o mecanismo normal. Mas hoje **não está convergindo** e
não tem prazo previsível.

### (b) Não usar `INDIRECT` no caminho de paginação

O anel executa com prova byte a byte. Os buffers de paginação são pequenos — o
`FillVirtual` da produção tem **16 dwords**. Nada impede o driver de, em vez de
apontar um `INDIRECT` para o buffer, **emitir aqueles mesmos pacotes direto no
anel**, resolvendo as GPUVAs de destino pela caminhada de page table que já existe
(`Bc250ActiveWalkGpuVa`) — que é precisamente o que o blt de apresentação faz, com
30 provas byte a byte.

**Isto não é fake nem fallback de software.** É a GPU executando de verdade, com
fence física e conferência byte a byte. Só troca a indireção por emissão direta.

**Limitação honesta:** não escala para buffers grandes de render, quando a UMD 3D
existir. Mas o que bloqueia a tela **hoje** é a paginação, e para ela o caminho (b)
usa só peças provadas.

**Minha recomendação era (b)**, com (a) mantido como investigação barata no
laboratório. A decisão ficou com o Daniel.

---

## 8. Instrumentos que MENTEM neste projeto

Esta seção existe porque cada item aqui me custou pelo menos uma corrida.

### 8.1 `RPTR == WPTR` não prova execução

O boot tripod passa com `RPTR=52` e `WPTR=68`. E `dbg/hw.c:1672` faz:

```c
rptrAfter = (rptrWb == wptr) ? rptrWb : rptrReg;
```

Quando o writeback **não** alcança, ele troca a medição pelo registrador MMIO — o
mesmo que o comentário logo acima diz que só acompanha o WPTR. Então "RPTR alcançou
WPTR" impresso pelo `sdma raw` pode ser espelhamento.

**E atraso do writeback não é sinal de falha:** o controle que passa termina 4
dwords atrás; variantes que falham terminam 1 dword atrás.

**A prova é a fence gravada pela GPU e os dados no destino. Só.**

### 8.2 A "IB que retirou em 22:16" nunca provou execução do IB

A fence fica **no anel**, depois do `INDIRECT`, e executa sob o VMID do anel —
físico, sem tradução. Ela passar prova que a engine chegou até ali, não que o
conteúdo do IB rodou. O próprio handoff anterior admitia: *"Byte proof dos
11.075.584 bytes preenchidos: ausente."*

### 8.3 `SDMA0_STATUS2_REG` NÃO é `CMD_OP`

É o **program counter do F32**, bits `15:2`. O próprio laboratório já provava isso
(`bc250ctl sdma f32` extrai `(st2 >> 2) & 0x3FFF`, com critério de saúde medido:
sadio `0x0140..0x03E0`, travado `≤0x0010`).

O catálogo documenta `CMD_OP` com máscara `0xFF0`/shift 4 e uma revisão externa
propôs `0xFFFF0000`/shift 16. **As duas estão erradas para esta placa.**

### 8.4 O `PARSETEST` não é teste válido de decodificação

Ele conclui "a engine não decodifica", mas o controle positivo do **mesmo boot**
prova execução com fence e byte proof. E `studio/cli/audit.c:510` já declara o
critério defeituoso.

Além disso ele **deixa um pacote incompleto pendente no anel** quando o RPTR
alcança o WPTR — ele só completa os 8 dwords que faltam no ramo em que o RPTR
para. Por isso ficou por último no ensaio.

### 8.5 Estado de engine não cai com desbind

Regra do projeto, confirmada hoje com correlação de 100%:

| | Controle positivo |
|---|---|
| Sessões **com** boot frio antes | passou, todas |
| Sessões **sem** boot frio antes | falhou, todas |

Assinatura de boot frio: o ensaio imprime `F32_CNTL: 0x00000001 HALT=1` com
`UCODE_CHECKSUM 0x00000000`. Se o F32 já aparecer rodando, a máquina não foi
reiniciada.

**Não recarregue o firmware da SDMA por PSP no laboratório.** O firmware do POST
basta, e o `sdma init` levanta o F32. Recarregar não ajudou e adicionou variável.

### 8.6 PowerShell 5.1 quebra literais hexadecimais e operações de 64 bits

Medido nesta máquina:

```
0xCAFEBABE  como literal              -> -889275714   (Int32 com sinal)
0xFEEDFACE  como literal              -> -17958194
[uint64]0x45F010001 -band 0xFFFFFFFF  -> 18773770241  (INTACTO, não mascarou)
[uint64]0xFFFFFFFF                    -> erro: -1 não cabe em UInt64
```

Use sufixo `L` e cast (`[uint32]0xCAFEBABEL`), e corte 64 bits por **aritmética**
(`% 4294967296` e `-shr 32`), nunca por máscara. Três corridas foram perdidas nisso.

### 8.7 O retorno do `sdma raw` não garante fim de execução

Ele volta quando o RPTR do registrador alcança o WPTR (com o fallback de 8.1).
Marcadores escritos por uma variante apareciam na janela da variante **seguinte**,
fazendo cada uma creditar a si mesma o resultado da anterior.

**Use valor único por submissão**, para que leitura cruzada seja detectável em vez
de silenciosa. O ensaio faz isso (`0xFEED0001`, `0xFEED0002`, …).

### 8.8 `BcVramWriteRaw` não faz flush de HDP

`dbg/hw.c:349` só escreve. Quem invalida é a **leitura**
(`BcVramReadRaw` → `BcHdpReadCacheInvalidate`, que faz as duas direções).

Expus `bc250ctl hdp` para isso. **Testado: não mudou o resultado**, então não era a
causa — mas continue usando, porque a premissa "escrever pela CPU basta" é falsa.

> A produção **já** faz esse flush (`Bc250SdmaPresentHdp` antes de escrever o
> WPTR). Este defeito era do laboratório, não do driver.

---

## 9. Método que funcionou, e que eu recomendo manter

1. **Controle positivo obrigatório antes de qualquer variante.** Sem ele, "X
   falhou" é ambíguo entre "X está errado" e "nada executa". Ele já salvou uma
   sessão inteira de conclusão errada.
2. **Teste do defeito de propósito.** Rodar `DIRTY` (PDE corrompido) ao lado de
   `EXEC` transforma "mudamos e melhorou" em prova causal.
3. **Uma variável por corrida**, e reprodutibilidade como parte do critério — duas
   corridas consecutivas com entradas idênticas discordaram, e foi assim que a
   corrida do marcador apareceu.
4. **Revisão externa.** Três IAs independentes revisaram e acharam um erro que eu
   não vi (invalidação de TLB no VMID errado). O briefing usado está em
   `docs/167_CONSULTA_EXTERNA_IB_PAGINACAO_SDMA_VMID8.md`.

---

## 10. Arquivos alterados

### Driver

```
driver/core/bc250_gcvm_contract.c/.h        EncodePde/DecodePde; EXECUTABLE bit 4;
                                            base PA como fonte unica
driver/core/bc250_gcvm_vmid8_transaction.c  PT_BASE com bit VALID
driver/core/bc250_sdma_packet_builder.c/.h  padding do INDIRECT e do IB
driver/wdk/bc250_active_miniport.c/.h       root com LocalSegmentBaseOffset;
                                            Bc250ActiveWalkGpuVa; gate fail-closed;
                                            DMA_FAULTED revertido com o motivo
driver/wdk/bc250_mcdm_callbacks.c/.h        VramMcBase nos caps; PDE so VALID;
                                            IB_DUMP/IB_SHAPE; publicacao da forma
                                            do IB e do PA da CPU
driver/wdk/bc250_sdma_present.c/.h          IB_SIZE explicito; IB_ALIGN fora do
                                            spinlock; UTCL1 baseline/delta;
                                            ENGINE_STATE; FENCE_VISIBILIDADE;
                                            RPTR_SAMPLES
```

### Testes offline (todos passam, 0 falhas)

```
P6_PDE_FORMAT_OFFLINE                  formato de PDE, e o contraste com o
                                       decodificador de PTE que esconde o defeito
P6_PTE_BIT_LAYOUT_OFFLINE              EXECUTABLE no bit 4; nenhum bit de fragmento
P6_VMID8_ROOT_VALID_BIT_OFFLINE        bit VALID no ponteiro de root
P6_PAGING_IB_SHAPE_PUBLICATION_OFFLINE publicacao autovalidada e destrutiva
P6_PAGING_IB_DEST_RANGE_OFFLINE        faixa de escrita publicada pelo encoder
```

> Três regressões existentes **falharam** antes de eu atualizá-las. Isso é
> cobertura real, não passagem por acaso.

### Ferramentas

```
studio/cli/bc250ctl.c        sdma raw <dw..>   injecao crua no anel (devolve F32Pc)
                             hdp               flush de escrita + invalidacao
tools/test-vmid8-ib.ps1      o ensaio de laboratorio (novo)
docs/03_CATALOGO...          bloco SDMA0_GFX_IB_* com derivacao cercada por ancoras
docs/167_CONSULTA_EXTERNA... briefing usado nas revisoes externas
```

### Pacote de produção atual

```
DriverVer:          3.81.0.0
Signed SYS SHA256:  F0A3DA060B880F3DC90B958BF32DAC9249B389CE20532B870A6ECAA59484B4AA
CAT SHA256:         E24879A486660049814D2BA43077EA68A614CDF666209100EBC122C373B41BAE
```

Contém 4.1, 4.2, 4.3, 4.4 e 4.6. **Não** contém 4.5 (cabeçalho do `CONST_FILL`).

---

## 11. Como operar

### Laboratório (barato: sem tela preta, sem fusível)

```
dbg\build.bat            (se mexer no driver de laboratorio)
studio\build.bat         (se mexer no CLI)
dbg\install.bat          elevado
dbg\start.bat            elevado
powershell -ExecutionPolicy Bypass -File tools\test-vmid8-ib.ps1 -Execute -IAcceptTheRisk
dbg\uninstall.bat
```

**Boot frio antes de cada sessão.** Verificação rápida de 10 s, se quiser evitar o
reboot:

```
bc250ctl arm write ; bc250ctl sdma init ; bc250ctl sdma verify
```

Se o `verify` passar nos três estágios, a engine está boa.

### Produção (caro: tela preta, BugCheck, reboot, recuperação)

```
driver\wdk\build-contract.bat
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-package.ps1
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-package.ps1
powershell -ExecutionPolicy Bypass -File tools\stage-bc250mcdm-package.ps1 -Execute
```

Boot frio, e então:

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1 `
    -Execute -IAcceptBlackScreenRisk -WatchdogTimeoutSeconds 60
```

Depois da tela preta:

```
tools\BC250_RECOVERY.cmd
```

Análise de dump, em PowerShell **elevado**:

```
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z C:\Windows\MEMORY.DMP `
  -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols" `
  -c "!analyze -show; kv 14; lm m bc250*; q"
```

> O empacotador **não compila o SYS** — ele copia. Pular o `build-contract.bat`
> assina o binário do candidato anterior.

### Evidência

```
docs/evidence/2026-09-05_*-vmid8-ib-lab/run.txt    transcripts do laboratorio
docs/evidence/2026-09-05_P6_*/README.md            analises das corridas de producao
C:\BC250_DISPLAY_LOG.txt                           log do driver de producao
```

---

## 12. Estado honesto final

**Provado no silício:** SDMA0 física; BLT de apresentação com byte proof; GOP e
VidPn; instalação e readback do VMID 8; `UpdatePageTable` e `FlushTlb` com ack;
anel do laboratório executando `WRITE_LINEAR`, `CONST_FILL` e `COPY_LINEAR` com
leitura de volta conferida.

**NÃO VALIDADO EM HARDWARE:** execução de conteúdo de IB (sob VMID 0 ou VMID 8);
byte proof do `FillVirtual`; tradução funcional sob VMID 8; composição real do
desktop; UMD 3D (`Bc250Umd10CreateDevice` retorna `E_NOTIMPL` em
`driver/umd/bc250_d3d_umd.c`); jogos acelerados.

**Custo do dia:** 7 corridas de produção, 13 sessões de laboratório. Sete defeitos
meus de método e de script pelo caminho — todos documentados na Seção 8, que é a
parte deste handoff que mais vale para quem vier depois.

**Não repita o erro que eu repeti a corrida inteira:** trate leitura única como
hipótese, não como medição, e nunca conclua sem controle positivo na mesma sessão.
