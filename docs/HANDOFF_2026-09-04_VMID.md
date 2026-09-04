# Handoff — Instalação de VMID e execução real de DMA buffer

> **Para a IA que assume.** Branch `p6-rendernode-wddm`, HEAD `342ae70`.
> Alvo: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2), Windows 11 x64.
> Data deste handoff: **2026-09-04, 19h50**.

```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ██████████████████▋░  93%   Estavel e Isolado
SDMA (DMA Engine & Robustez)     ████████████████████ 100%   31 blts de 11.059.200 bytes com tripe completo
GCVM & Traducao de Memoria       ██████████████████░░  90%   Snapshot GPUVA correto; NENHUM VMID instalado no silicio
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     ████████████████████ 100%   Log sobrevive a BugCheck e a hard reset
Compute & Async Queues (MEC/MQD) ██████████████████░░  90%   P1 17/17 e P2 16/16 provados
Miniport WDDM Display+Render     ███████████████████░  95%   P5 VALIDADA EM HARDWARE (tripe + BYTE_PROOF)
=============================================================================================
Veredito Global: em 04/09 19:07 a GPU escreveu no scanout GOP pela primeira vez, com tripe
completo e prova byte a byte de 11.059.200 bytes. O conteudo exibido ainda NAO e o desktop:
nada escreve na primary, porque nenhum DMA buffer e executado. Execucao de DMA buffer e
instalacao de VMID — NAO VALIDADO EM HARDWARE.
=============================================================================================
```

---

## 1. Leis que você tem que seguir (não são sugestões)

Estão em [`CLAUDE.md`](../CLAUDE.md) e [`rules.md`](../rules.md). As quatro que mais importam
para esta tarefa:

- **Lei 1 — Sinceridade.** Reporte o que a medição diz. Se não houver prova, escreva
  literalmente **`NÃO VALIDADO EM HARDWARE`**.
- **Lei 2 — Tripé no silício.** Não declare sucesso sem as três provas: `RPTR == WPTR`,
  fence gravada **pela GPU** com poison substituído, e dados conferidos byte a byte.
- **Lei 4 — Zero fakes.** Nada de fence incrementada pela CPU, nada de `STATUS_SUCCESS` em
  DDI sem backend. **Esta tarefa nasceu de uma violação dessa lei** — veja a seção 4.
- **Lei 7 — Endereços só com proveniência.** Só offsets e campos catalogados em
  [`docs/03_CATALOGO_DE_REGISTRADORES.md`](03_CATALOGO_DE_REGISTRADORES.md), com fonte
  citada. **É exatamente esta lei que bloqueia o passo 1 e é por isso que ele existe.**

E a Lei 9: **o Daniel é quem roda no hardware.** Você compila, analisa e prepara. Corrida
física é recurso escasso — defina teto e critério de abandono antes de armar.

---

## 2. Onde queremos chegar

Rodar jogos no Windows 11 na BC-250 usando as 24 CUs e os 16 GB de GDDR6, com o monitor no
DisplayPort da própria placa, via D3D12/Vulkan, **sem WARP e sem fallback de software**.

O passo imediato é menor e concreto: **fazer o desktop aparecer na tela**. Hoje a tela acende,
mas mostra VRAM velha.

---

## 3. O que foi feito em 04/09 (tudo com evidência arquivada)

Cinco paredes caíram, cada uma diagnosticada por log ou dump e corrigida com portão offline.
Commits, do mais antigo para o mais novo:

| Commit | O que corrigiu | Como foi descoberto |
|---|---|---|
| `e458b8e` | `GetStandardAllocationDriverData` devolvia `AllocationPrivateDriverDataSize=0`, então o VidMm nunca chamava `CreateAllocation` e a primary não existia | Mineração do corpus: a corrida 12:23 tinha 50 `CreateAllocation`, todas as seguintes tinham 0 |
| `fb92131` | `VIRTUAL_FILL` rejeitava pedidos acima de 4 MiB com `STATUS_INVALID_PARAMETER`, um código que essa DDI não pode devolver → BugCheck `0x10E` | `!analyze -v` do `MEMORY.DMP`: `Arg1: 0xB, Driver returned an invalid error code from BuildPagingBuffer` |
| `d466f0a` | O encoder SDMA exige `RingDwords` potência de dois e recebia `DmaSize/4` cru | Log do driver, que sobrevive a BugCheck: `[FillVirtual] SDMA encode failed: status=1` |
| `dee893e` | `DXGKARG_PRESENT` tem união `pAllocationList` / `pAllocationInfo`; em WDDM 2.0 GpuMmu o dxgkrnl preenche `pAllocationInfo` (32 bytes/entrada) e líamos `pAllocationList` (24 bytes) | Offsets do log batendo só com passo de 32: `Seg=16`, `hDev=NULL`, ponteiro de kernel no slot 2 |
| `0879d01` | O journal GPUVA era único no adaptador; dois processos intercalando abortavam a transação um do outro e o range `0xAC0000` da primary nunca chegava ao snapshot | Linha do tempo por número de linha no log, mais teste de controle que **reproduz o defeito offline** |

### O resultado: P5 fechada em hardware

Corrida das **19:07**, evidência em
`docs/evidence/2026-09-04_19-07-10-DESKTOP-HQAG6T4-mcdm-display-binding/`:

```text
[SdmaPresent] BLT_TRIPOD PASS #1: WPTR=188 RPTR=188 Fence=00001001 Poison=43DAFFFE Bytes=11059200
[Present #1] BYTE_PROOF PASS: Source=0xAD0000 Dest=0x0 Bytes=11059200 (byte-a-byte)
[Present #1] Blt 11059200 bytes (src 0xAD0000 -> scanout 0x0): SDMA=YES
[SdmaPresent] Teardown Complete. TripodValidated=1, Submitted=31, Completed=31, Fallbacks=0
```

Tripé completo. 31 blts, 31 completos, 0 fallbacks. Sem BugCheck, sem TDR. E o Daniel **viu
imagem na tela pela primeira vez**: cinza com linhas finas em vermelho, verde e azul.

Comparação com a corrida anterior, que mede o efeito de `0879d01`:

| | 18:25 | 19:07 |
|---|---|---|
| `Aborting orphaned journal` | 7 | **0** |
| `REJECT source contract` | 30 | **0** |
| `SpaceResolveAddress failed` | 30 | **0** |
| `ResolveGpuVa SUCCESS` | 0 | **97** |

---

## 4. O problema que sobrou, e por que ele existe

**A imagem está errada porque nada escreve na primary.** O blt está correto e provado; ele
copia fielmente o que existe em `0xAD0000`, e o que existe lá é VRAM velha.

Prova independente disso: `Spread PRIMARY @ 0xAD0000` tem `Xor=E854A5FB` — **o mesmo valor nas
corridas 18:10 e 19:07, boots diferentes**. Conteúdo estático. Nada escreveu ali.

Por que nada escreve: `Bc250DxgkDdiSubmitCommandVirtual` no node 0
([`bc250_active_miniport.c`](../driver/wdk/bc250_active_miniport.c), procure por
`NAO EXECUTADO`) **não executa o DMA buffer**. Ele apenas notifica `DMA_COMPLETED` para o
scheduler avançar. Consequência medida: `[FillVirtual] Completo: 11075584 bytes` "completa"
apenas no **encoder**; o preenchimento nunca roda na GPU.

Isso era sucesso silencioso sem backend, uma violação da Lei 4. O commit `342ae70` **não
corrigiu** o problema — ele o tornou explícito e contado, que é o mínimo que a lei exige
enquanto a correção real não vem:

```text
[SubmitVirtual Node0 SDMA] NAO EXECUTADO #N: DMA buffer de %u bytes em VA=0x%llX
descartado; DMA_COMPLETED notificado para FenceId=%llu
```

### Por que executar não é "só ligar o fio"

Os pacotes dentro do DMA buffer referenciam **GPUVA** (`DestinationVirtualAddress`).
Executá-los exige um **VMID com a root page table do processo instalada**. Hoje nenhum VMID
está instalado:

- [`Bc250ActiveSetRootPageTableObject`](../driver/wdk/bc250_active_miniport.c) (linha ~2008)
  apenas **registra** a root page table no object store. Não escreve em registrador nenhum.
- [`Bc250RunGcvmVmid8InertTransaction`](../driver/core/bc250_gcvm_vmid8_transaction.c) é
  **inerte por construção**: programa, confere por readback, invalida a TLB e **restaura** os
  valores originais. Prova que os registradores respondem; não deixa nada instalado.

O blt de apresentação funciona **porque usa endereço físico MC**
(`BC250_SDMA_VRAM_MC_BASE + offset`) e não passa por tradução alguma. É por isso que a P5
fechou enquanto o resto continua parado. Entenda essa distinção antes de mexer em qualquer
coisa.

---

## 5. O trabalho, em quatro passos

Siga nesta ordem. Cada passo tem um teste nomeado (Lei 3). **Não pule para o passo 3.**

### Passo 1 — Catalogar `GCVM_CONTEXT8_CNTL` com proveniência ← **comece aqui**

**Custo de hardware: zero.** É leitura de fonte.

**Por quê:** para instalar o VMID é preciso escrever `GCVM_CONTEXT8_CNTL`, e esse registrador
tem campos que definem a **forma da page table**: `PAGE_TABLE_DEPTH` e
`PAGE_TABLE_BLOCK_SIZE`. Nosso espaço é de **34 bits com 2 níveis** — o driver já declara
isso ao dxgkrnl e está no log da corrida:

```text
[McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2
[McdmQAI] PAGETABLELEVELDESC: Level=0, Bits=9,  Size=4096
[McdmQAI] PAGETABLELEVELDESC: Level=1, Bits=13, Size=65536
```

O catálogo hoje só tem proveniência para `GCVM_CONTEXT0_CNTL` com `PAGE_TABLE_DEPTH=0`, que é
o **domínio de sistema, plano** — caso diferente do nosso. Ver
[`docs/03_CATALOGO_DE_REGISTRADORES.md`](03_CATALOGO_DE_REGISTRADORES.md), seção
*Estágio `context0` — `gfxhub_v2_0_enable_system_domain`*.

**Por que isso não é formalismo:** um `PAGE_TABLE_DEPTH` errado **não falha alto**. Produz
page fault ou tradução silenciosamente errada. É a mesma classe de bug que custou cinco
corridas físicas em 04/09. Escrever por inferência aqui é a pior troca possível.

**O que fazer:**

1. Minerar o upstream do amdgpu, função `gfxhub_v2_0_setup_vmid_config()` (arquivo
   `drivers/gpu/drm/amd/amdgpu/gfxhub_v2_0.c`) e o header
   `gc/gc_10_1_0_sh_mask.h` para `GCVM_CONTEXT1_CNTL` — os contextos 1..15 compartilham
   layout, então CONTEXT8 usa o mesmo.
2. Catalogar, com shift, largura, valor do POST e valor pretendido:
   `ENABLE_CONTEXT`, `PAGE_TABLE_DEPTH`, `PAGE_TABLE_BLOCK_SIZE`,
   `RETRY_PERMISSION_OR_INVALID_PAGE_FAULT` e os bits de `*_FAULT_ENABLE_DEFAULT`.
3. Derivar `PAGE_TABLE_BLOCK_SIZE` da nossa geometria e **mostrar a conta**. No upstream ele
   vem de `amdgpu_vm_get_block_size()`; documente a equivalência com `Level=1, Bits=13`.
4. Escrever no catálogo com a fonte citada, e classificar cada linha pelo vocabulário de
   confiança (**P1** derivado do upstream é o esperado aqui).

**Teste que fecha:** revisão do catálogo mais um teste de contrato offline que monta o valor
de `CNTL` a partir da geometria e compara com o esperado, campo a campo, incluindo caso
negativo. Sugestão de arquivo: `driver/tests/gcvm_context8_cntl_test.c`, integrado ao
`driver/wdk/build-contract.bat`. **Nenhuma escrita em hardware neste passo.**

### Passo 2 — Instalação persistente do VMID

**Por quê:** sem VMID instalado, nenhuma GPUVA traduz para engine nenhuma.

**O que fazer:** um `Bc250GcvmVmid8Install()` no contrato, irmão da transação inerte, com a
mesma estrutura mas **sem o restore**: programa `PT_BASE_LO/HI`, `PT_START_LO/HI`,
`PT_END_LO/HI` a partir da root page table real do processo, confere por readback, escreve
`CNTL` com os campos do passo 1, e invalida a TLB pela **engine 17** (a engine da GART; ver a
seção *A engine de invalidação* do catálogo — a engine 0 **não** é a certa).

Chame a partir de `Bc250ActiveSetRootPageTableObject`, mantendo o contrato fail-closed: se o
readback divergir, **falhe** e não deixe o VMID meio programado.

**Teste que fecha:** teste de contrato offline com interface fake (mesmo padrão de
`driver/tests/gcvm_vmid8_transaction_test.c`), cobrindo readback divergente, `CNTL` recusado e
flush falhando. Em hardware: `[Vmid8Install] readback OK` no log, sem BugCheck, com o
dispositivo em Código 0.

### Passo 3 — Executar o DMA buffer como Indirect Buffer

**Por quê:** é o que faz `[FillVirtual]` deixar de ser mentira.

**O que fazer:** `Bc250SdmaEmitIndirect()` **já existe e já é testado offline**
(`driver/core/bc250_sdma_packet_builder.c`, `BC250_SDMA_DWORDS_INDIRECT = 6`). Crie um
`Bc250SdmaPresentExecuteIndirect()` em `driver/wdk/bc250_sdma_present.c` copiando o padrão
já provado de `Bc250SdmaPresentBlt()`: emitir `INDIRECT` + `GCR` + `FENCE` no anel, envenenar
a fence, chutar o WPTR, e só devolver `TRUE` com **o tripé completo**. Depois troque, em
`SubmitCommandVirtual`, o `NAO EXECUTADO` por essa chamada.

Cuidado: o IB precisa de alinhamento de 32 bytes (`BC250_SDMA_IB_ALIGNMENT_BYTES`) e o VMID
vai no header do pacote.

**Teste que fecha:** `[SubmitVirtual Node0 SDMA] IB_TRIPOD PASS` no log, contador
`SubmissionsNotExecutedCount` em **zero**, e a primary deixando de ter `Xor=E854A5FB` — ou
seja, o `VIRTUAL_FILL` finalmente zerando o buffer de verdade. A tela deve ficar **preta**, não
cinza com ruído. Preto aqui é progresso, não regressão.

### Passo 4 — UMD

Só depois dos três acima. Antes disso qualquer coisa que o UMD submetesse seria descartada
igual. `bc250umd.dll` nunca foi carregada por processo nenhum até hoje, e `McdmRender` tem
contagem zero em todas as corridas.

---

## 6. Estado da máquina e comandos

O host está com a placa em **`BasicDisplay` Código 0**, mas o **fusível anti-boot-loop está
armado** (`C:\BC250_BOOT_FUSE.dat` presente, disparou no terceiro boot da corrida 19:07).
A próxima corrida exige recovery primeiro.

Prompt de Comando **elevado**:

```
tools\BC250_RECOVERY.cmd
```

PowerShell **elevado**:

```
powershell -ExecutionPolicy Bypass -File tools\stage-bc250mcdm-package.ps1 -Execute
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1 -Execute -IAcceptBlackScreenRisk -WatchdogTimeoutSeconds 120
```

Portões offline, todos obrigatórios antes de entregar um pacote:

```
driver\wdk\build-contract.bat
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-package.ps1
python tools\audit-bc250mcdm-safety.py
python tools\audit-mcdm-ddi-matrix.py
python tools\audit-ddi-noop-callbacks.py
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-package.ps1
```

> ⚠️ O empacotador **não compila o SYS** — ele copia. Rodar o empacotador sem rodar o
> `build-contract.bat` antes assina o binário do candidato anterior. Confira sempre o
> `PackageSysSha256` do manifesto contra o do bind.

---

## 7. Duas coisas que vão te poupar horas

**O log do driver sobrevive a BugCheck e a hard reset.** `Bc250DisplayLog` faz `ZwWriteFile` +
`ZwFlushBuffersFile` **linha a linha** em `PASSIVE_LEVEL`, direto em
`C:\BC250_DISPLAY_LOG.txt`. O coletor só o copia depois. Quando a máquina morrer antes do
coletor, **o log ainda está lá** — peça esse arquivo antes de pedir dump. Três corridas foram
diagnosticadas assim, em minutos.

**Quando houver BugCheck, o dump nomeia a função.** Precisa de PowerShell elevado:

```
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z C:\Windows\MEMORY.DMP -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols" -c "!analyze -v; lm m bc250*; q" > "$env:TEMP\bc250.txt"
```

Foi assim que o `0x10E` das 17:38 virou correção dirigida em vez de chute.

---

## 8. Erros que já cometemos — não repita

- **Não confie no membro errado de uma união.** `DXGKARG_PRESENT` e `DXGK_ALLOCATIONINFO` têm
  uniões cujo membro válido depende do modelo de memória declarado. Ler o errado não falha:
  lê lixo com passo errado. Duas corridas se perderam nisso.
- **Não devolva NTSTATUS arbitrário de `DxgkDdiBuildPagingBuffer`.** O VidMm bugcheca com
  `0x10E` subcódigo `0xB`. Só `STATUS_SUCCESS` e `STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER`
  (com progresso em `MultipassOffset`) são seguros nesse caminho.
- **Não trate "o encoder aceitou" como "a GPU executou".** Foi exatamente essa confusão que
  produziu a tela cinza.
- **Não afirme causa sem checar a ordem das linhas no log.** Eu citei um abort de journal como
  prova e estava com a ordem invertida — o abort que destruía o range era o seguinte. O Daniel
  perguntou "tem certeza?" e a releitura mostrou o erro. Reconstrua a linha do tempo por
  número de linha antes de concluir.

---

## 9. Resumo em três frases

A cadeia WDDM inteira funciona e o blt está provado no silício com tripé e prova byte a byte;
o que falta é conteúdo, e conteúdo depende de executar DMA buffers, que depende de um VMID
instalado, que depende de catalogar dois campos de registrador com proveniência. **Comece pelo
passo 1, que não custa corrida.** E enquanto não houver prova física, escreva
`NÃO VALIDADO EM HARDWARE`.
