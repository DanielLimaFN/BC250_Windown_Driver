# ROADMAP VULKAN 100% — BC-250 sem mentiras

> **Escopo exclusivo:** levar a APU AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)
> a executar software Vulkan de terceiros (`llama.cpp`, `Vulkan-Samples`, CTS) no
> Windows 11 x64 com resultado **conferido**, nunca declarado.
>
> **Criado em:** 2026-08-31 · **Documento vivo** · Ver também
> [`STATUS_E_ROADMAP_CONSOLIDADO.md`](STATUS_E_ROADMAP_CONSOLIDADO.md) para o projeto inteiro.

---

## 0. Barra de status deste roadmap

```text
================================== VULKAN BC-250 ============================================
Higiene do ambiente (mentiras)   ████████████████████ 100%   HKLM e HKCU limpos e conferidos
ICD proprio — fail-closed        ████████████████████ 100%   Provado: loader recebe -9 e pula
ICD proprio — entry points       ██████████▊░░░░░░░░░  54%   51/95 resolvidos
ICD proprio — execucao real       ░░░░░░░░░░░░░░░░░░░   0%   Sem SPIR-V->ISA, sem descritores
Miniport — fence ao dxgkrnl      ██████████████░░░░░░  70%   Codigo pronto; NAO validado em HW
Miniport — GPUVA dinamica         ░░░░░░░░░░░░░░░░░░░   0%
RADV — 3 defeitos localizados    ████░░░░░░░░░░░░░░░░  20%   EM ANDAMENTO: nao submete e e gfx1010
No de render (DOD -> WDDM)       ██████░░░░░░░░░░░░░░  30%   Interruptor escrito, desligado
Gate de aceitacao por operador    ░░░░░░░░░░░░░░░░░░░   0%   test-backend-ops nao compilado
=============================================================================================
Veredito: VULKAN NAO VALIDADO EM HARDWARE
=============================================================================================
```

---

## 0.1 Quadro consolidado — feito x falta

Legenda: **✅** provado no silício · **🟡** escrito e verificado só offline · **❌** não existe

### Kernel — miniport WDDM

| Item | Estado | Evidência / lacuna |
|---|---|---|
| `Class=Display`, INF assinado, bind com watchdog | ✅ | `ConfigManagerErrorCode 0`, rollback provado |
| KMDOD via `DxgkInitializeDisplayOnlyDriver` | ✅ | WDDM v1.2 |
| VidPn: IsSupported, Recommend, EnumCofuncModality, Commit | ✅ | `DmmCommitStatus 0`, `DmmCommitHasPath 1` |
| `SetVidPnSourceAddress` / `SourceVisibility` | ✅ | — |
| `PresentDisplayOnly` (blit por CPU) | ✅ | `DmmPresentEntered 1` |
| VSync por timer + `NotifyInterrupt` | ✅ | `DmmVsyncCount` avançando |
| Ponteiro, RecommendMonitorModes, VidPnTopology, QueryVidPnHWCapability | 🟡 | Escritos, não exercitados |
| Interruptor `RenderNode` para `DxgkInitialize` | 🟡 | Escrito e compilando; **desligado** — ausente do registro |
| Telemetria `V5RegistrationMode` / `RenderNodeRequested` / `TunablePresent` | 🟡 | No código; não no binário instalado |
| **Nó de render efetivamente ativo** | ❌ | Nunca registrado por `DxgkInitialize` no silício |
| `DxgkDdiQueryCurrentFence` | ❌ | Obrigatório para render |
| `UpdatePageTable` / `UpdatePageDirectory` / `MovePageDirectory` / `DescribePageTable` | ❌ | Obrigatórias com GPUVA |
| `CreateDevice/Context/Allocation`, `BuildPagingBuffer` | 🟡 órfãos | O KMDOD nunca os entrega ao dxgkrnl |
| `DxgkDdiPresent` (render) | 🟡 stub honesto | Devolve `STATUS_NOT_SUPPORTED` |
| GPUVA dinâmica por processo | ❌ | Bloqueia multiprocesso e render |
| TDR / preempção provados | 🟡 | `TdrReady=1` declarado, sem prova física |
| MMIO/SMN por IOCTL de user mode | ❌ risco | Furo de segurança no binário assinado |

### Motor de submissão e compute

| Item | Estado | Evidência / lacuna |
|---|---|---|
| SDMA H2D/D2H byte a byte | ✅ | 1024/1024 DWORDs |
| CP/PM4, fence física em VRAM | ✅ | Tripé fechado |
| MEC2, MQD, doorbell, anel persistente | ✅ | Fase P1 |
| P2 multi-submissão 16/16 | ✅ | `P2TriadPassed 1` |
| Rastreador de fences WDDM (FIFO 64) | 🟡 | Contrato offline aprovado |
| `DXGK_INTERRUPT_DMA_COMPLETED` + varredura 1 ms | 🟡 | **Inalcançável** sem nó de render |
| Contrapressão antes de submeter | 🟡 | idem |
| 12 campos `V2*` de telemetria | ✅ legíveis | 834 → 846 valores |
| Anel/fence por contexto | ❌ | Hardcoded `0xF400000000` / `0xF400002200` |
| Interrupção real do MEC (anel IH) | ❌ | Hoje é varredura por timer |

### Vulkan ICD próprio

| Item | Estado | Evidência / lacuna |
|---|---|---|
| Fail-closed no loader | ✅ | Retorna −9, `vulkaninfo` exit 0 |
| Não executa mais SPIR-V como ISA | ✅ | — |
| Fences honestas | ✅ | `VK_TIMEOUT` / `VK_NOT_READY` reais |
| `HOST_COHERENT` falso removido | ✅ | + `Flush`/`Invalidate` por SDMA |
| `apiVersion` honesto (1.0) | ✅ | — |
| Entry points | 🟡 51/95 | Faltam 44 |
| Descritores / pipeline layout | ❌ 0/8 | Bloqueante absoluto |
| Barreiras, copy, semáforos, query pool | ❌ | — |
| SPIR-V → ISA GFX10 | ❌ | Virá do ACO, não daqui |

### RADV / Mesa — branch `bc250-wddm-diag`

| Item | Estado | Evidência / lacuna |
|---|---|---|
| Árvore + toolchain na máquina | ✅ | `lfrb/mesa` wddm2, meson/ninja |
| Winsys WDDM2 existe | ✅ | `D3DKMTSubmitCommand` implementado |
| Alvo de ISA gfx1013 | ✅ verificado | `chip_external_rev 1 → 0x82`; `RADV GFX1013` |
| Status de submissão não engolido | 🟡 | Compila e linka; falta a corrida |
| WARP enumerado como BC-250 | ❌ | `deviceID 0x008c` |
| Orçamento de memória real | ❌ | 16384 MiB fixos |
| UMA anunciada | ❌ | `uma: 0` numa APU unificada |
| Subgroup coerente | ❌ | Driver 64, ggml 32 |
| WSI Win32 | ❌ | Sem prova |

### Aplicação

| Item | Estado | Evidência / lacuna |
|---|---|---|
| V1 respondida: o RADV não submete | ✅ | Delta zero + 115.504 t/s (~60x o pico) |
| Hipóteses Q4_K / FP16 / barreira | ✅ descartadas | Não há execução para explicar |
| Kernels LLM fail-closed | ✅ | Os 6 eram `s_endpgm` |
| `[PROVADO NO SILICIO]` removido | ✅ | 4 ocorrências no benchmark |
| Kernels LLM reais | ❌ | Nenhum existe |
| `test-backend-ops` como gate | ❌ | Não compilado |
| Token real gerado na GPU | ❌ | — |

### Deriva de binário — o que está no silício agora

| Artefato | SHA-256 (16) | Contém |
|---|---|---|
| `output/bc250mcdm.sys` (produção, 13:04) | `E113AFEE6C9179AE` | V2 + interruptor `RenderNode` |
| `output_lab/bc250mcdm.sys` (11:55) | `D2315A012B4E8008` | V2 apenas |
| Pacote assinado | `38B56891B736E48C` | V2 apenas |
| **Instalado em System32** | `38B56891B736E48C` | **V2 apenas** |

O interruptor `RenderNode` está no build de produção mas **não** no binário que está
rodando. Para exercitá-lo é preciso refazer a cadeia inteira, nesta ordem:
`build-contract.bat` → `build-bc250mcdm-lab-bringup.ps1 -Execute ...` →
`package-bc250mcdm-lab.ps1` → `bind` → **boot** (porque `RenderNode` é lido em
`DriverEntry`, não em `StartDevice`).

---

### Ferramentas e higiene

| Item | Estado |
|---|---|
| Overrides do Mesa fora de HKLM e HKCU | ✅ |
| `clean-mesa-overrides.ps1` com `-Restore` | ✅ |
| `audit-vulkan-icd-entrypoints.py` | ✅ |
| `test-v2-wddm-submission.ps1` (+ `-VulkanDriverFiles`) | ✅ |
| Guard-rails de build e de engine suja no bind | ✅ |
| Backup/rollback do registro | ✅ |
| `-EnableRenderNode` no bind, com aviso de que só vale no próximo boot | ✅ |

---

## 1. Diagnóstico que originou este roadmap

Auditoria de 2026-08-31, com testes executados em modo de usuário (nenhuma escrita no silício).

### 1.1 Evidência bruta coletada

| # | Teste | Resultado |
|---|---|---|
| T1 | Sonda de entry points (`tools\audit-vulkan-icd-entrypoints.py`) | **47/95** resolvidos; 0/8 descritores; 0/3 query pool |
| T2 | `vulkaninfo --summary` só com o ICD BC-250 | **`0xC0000005` ACCESS_VIOLATION** — o ICD derrubava o processo |
| T3 | `vulkaninfo --summary` só com RADV | `deviceName = ... (RADV NAVI10)`, `deviceID = 0x008c` (não `0x13FE`), `deviceUUID` degenerado, **WARP aceito como device RADV**, adaptador duplicado |
| T4 | `llama-bench --list-devices` | `uma: 0`, `warp size: 32` vs `subgroupSize = 64` do driver, `16384 MiB free` fixo |
| T5 | Ambiente e binário Mesa | `MESA_VK_VERSION_OVERRIDE = 1.3` persistido em HKLM **e** HKCU; `D3DKMTSubmitCommand`/`CreateAllocation`/`MapGpuVirtualAddress` presentes na DLL |
| T6 | Leitura do miniport | `DxgkCbNotifyInterrupt` existe **apenas** em `driver/wdk/bc250_display_ddi.c`; o caminho de compute nunca sinaliza `DXGK_INTERRUPT_DMA_COMPLETED` |

### 1.2 Causas raiz

1. **ICD próprio entregava palavras SPIR-V como ISA GFX10** ao CP, sem vincular descritor
   nenhum, e marcava a fence como sinalizada pela CPU logo em seguida.
2. **RADV compila para gfx1010 (NAVI10)**, não gfx1013. ISA errada para este silício.
3. **O miniport nunca retira fence WDDM.** Sem isso nenhum caminho WDDM pode funcionar.
4. **`MESA_VK_VERSION_OVERRIDE=1.3` persistido** — override ativo na máquina de ensaio.
   Removido na V0; medição posterior mostrou que ele **não** inflava a versão de Vulkan
   (o RADV realmente reporta 1.4.348). Descartado como causa.

A saída corrompida do `llama.cpp` (`CC0,1*.C4...`) **não** é bug de desquantização `Q4_K_M`.
Nenhuma hipótese numérica (`v_bfe_u32`, denormal FP16, barreiras) é testável enquanto não
houver execução comprovada para observar.

---

## 2. Leis deste roadmap

Além das quatro leis do [`CLAUDE.md`](CLAUDE.md), valem aqui:

- **L5 — Nenhum entry point Vulkan devolve `VK_SUCCESS` sem backend.** Falta de suporte é
  `VK_ERROR_*`, nunca sucesso silencioso.
- **L6 — Nenhuma propriedade anunciada sem lastro.** Bit de capacidade (`HOST_COHERENT`,
  `apiVersion`, extensão) só é declarado se o comportamento correspondente existir.
- **L7 — Fence só é sinalizada por leitura de memória escrita pela GPU.** Nunca por
  contador de CPU.
- **L8 — Aceitação é por operador conferido, não por token gerado.** `test-backend-ops`
  antes de `llama-cli`.

---

## 3. Fases

### Fase V0 — Higiene e fail-closed · **CONCLUÍDA** · 2026-08-31

Objetivo: parar de produzir falso-positivo. Nenhuma medição valia nada antes disto.

| Item | Estado | Onde |
|---|---|---|
| Backup do registro antes de qualquer mudança | ✅ | `docs/evidence/2026-08-31_vulkan_rollback/` |
| Remover overrides do Mesa em HKCU | ✅ | `tools/clean-mesa-overrides.ps1` |
| Remover overrides do Mesa em HKLM | ✅ 2026-08-31, console elevado | idem |
| `vkCreateInstance` fail-closed (`VK_ERROR_INCOMPATIBLE_DRIVER`) atrás de `BC250_ICD_UNSAFE=1` | ✅ | `driver/umd/bc250_vulkan_icd.c` |
| Remover execução de SPIR-V como ISA em `vkQueueSubmit` | ✅ | idem — agora devolve `VK_ERROR_DEVICE_LOST` |
| `vkWaitForFences` deixa de mentir (`VK_TIMEOUT` quando não sinalizada) | ✅ | idem |
| `vkGetFenceStatus` implementado (`VK_NOT_READY` honesto) | ✅ | idem |
| Retirar bit `HOST_COHERENT` falso do tipo de memória 1 | ✅ | idem |
| Implementar `vkFlushMappedMemoryRanges` / `vkInvalidateMappedMemoryRanges` reais | ✅ | idem — o espelho de host agora é sincronizado por SDMA |
| `vkDeviceWaitIdle` implementado | ✅ | idem |
| Declarar `apiVersion 1.0` (era `1.1` sem nenhum entry point 1.1) | ✅ | `bc250_icd.json` + `BC250_VK_API_VERSION` |
| Sonda de entry points virou ferramenta do repositório | ✅ | `tools/audit-vulkan-icd-entrypoints.py` |

**Prova de V0** (executada, reproduzível):

```text
# antes
VK_DRIVER_FILES=<bc250_icd.json> vulkaninfo --summary   -> EXIT -1073741819 (0xC0000005)
vulkaninfo --summary (registro real)                    -> EXIT 5

# depois
VK_DRIVER_FILES=<bc250_icd.json> vulkaninfo --summary
  -> "terminator_CreateInstance: Received return code -9 ... Skipping this driver."
vulkaninfo --summary (registro real)                    -> EXIT 0, RADV enumera normalmente
tools\audit-vulkan-icd-entrypoints.py                   -> 51/95 (era 47/95)
build-umd.bat                                           -> limpo em /W4 /WX
```

**V0 fechada.** `MESA_VK_VERSION_OVERRIDE`, `MESA_GL_VERSION_OVERRIDE` e
`MESA_GLSL_VERSION_OVERRIDE` removidos de HKLM em console elevado; HKCU já estava limpo.
Reconferido depois: ambos os hives sem nenhuma chave `MESA*`/`RADV*`.

Observação honesta do que a limpeza **não** mudou: o RADV continua reportando
`apiVersion 1.4.348`, ou seja, o override de versão de Vulkan não estava inflando nada —
ele realmente implementa 1.4. A hipótese "o override faz o llama.cpp escolher caminho 1.3
inexistente" está **descartada**. O que permanece é `RADV NAVI10`.

Rollback disponível: `tools\clean-mesa-overrides.ps1 -Restore`.

---

### Fase V1 — Ensaio de bisseção: o RADV chega a submeter? · **CONCLUÍDA** · 2026-08-31 12:16

**Resposta: NÃO.** O RADV nunca chega a `DxgkDdiSubmitCommandVirtual`.

Ensaio: `tools/test-v2-wddm-submission.ps1`, com o driver de laboratório vinculado
(`P1EngineState = 1`), boot limpo, modelo `tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf`,
`llama-bench -ngl 99`, `exit = 0`.

Evidência 1 — nenhuma submissão WDDM:

```text
V2TrackerPushedTotal  0 (delta 0)     V2NotifiedTotal  0 (delta 0)
V2RetirementDpcRuns   0 (delta 0)     V2Backpressure   0 (delta 0)
P1SubmitCalls        17 (delta 0)  <- inalterado: as 17 sao do self-test interno
```

Evidência 2 — os números reportados são fisicamente impossíveis:

| Métrica reportada | Exigiria | Pico do silício | Fator |
|---|---|---|---|
| `pp512` 115.504 t/s | ~254 TFLOPS (2,2 GFLOP/token) | ~4,3 TFLOPS (24 CU @ ~1,4 GHz) | ~60x |
| `tg128` 1.494 t/s | ~950 GB/s (636 MiB de pesos/token) | ~224 GB/s GDDR6 | ~4x |

E a corrida inteira — carregar 636 MiB, processar 512 tokens de prompt e gerar 128 —
durou **3 segundos**.

**Conclusão:** o RADV do Windows enumera a BC-250 via DXGI/D3DKMT, aceita os despachos,
descarta e retorna imediatamente. Os buffers de saída nunca são escritos por GPU nenhuma.
A saída corrompida do `llama.cpp` é memória não inicializada.

Ficam **descartadas** as três hipóteses numéricas originais: desquantização `Q4_K_M` com
`v_bfe_u32`, denormal FP16 em subgrupos RDNA1 e falta de `vkCmdPipelineBarrier`. Não há
execução para nenhuma delas explicar.

Fica também registrado que a Fase V2 **permanece não exercitada**: sem submissão WDDM,
o rastreador de fences nunca é acionado. Ela só será validável depois da V3.

**Nota de método.** A primeira tentativa deste ensaio (12:13) produziu o veredito certo
pelo motivo errado: `$ErrorActionPreference = 'Stop'` mais `*>&1` fazem o PowerShell 5.1
transformar a primeira linha de stderr de um executável nativo em `NativeCommandError`,
e o `llama-bench` morreu em 0 s. Delta zerado com processo morto não prova nada. O script
passou a usar `Start-Process` com redirecionamento em arquivo e a recusar veredito quando
`ExitCode != 0`; as duas evidências inválidas foram apagadas.

**Evidência:** `docs/evidence/2026-08-31_12-16-14-DESKTOP-HQAG6T4-v2-wddm-submission/`

---

### Fase V2 — Miniport: fechar o ciclo de submissão WDDM · **ESCRITA, EM ESPERA** · bloqueada pela V5

Sem isto, nenhum UMD — RADV, próprio ou DirectML — pode funcionar.

> **Reclassificada em 2026-08-31.** Era "bloqueante único". O levantamento da V5
> mostrou que a V2 e' *inalcancavel* enquanto o driver registrar como display-only:
> o `DxgkDdiSubmitCommandVirtual` nunca chega ao dxgkrnl. O codigo esta escrito e com
> contrato offline aprovado; a validacao fisica espera a V5.

| Item | Situação hoje | Alvo |
|---|---|---|
| Notificação de conclusão ao `dxgkrnl` | ✅ **implementada 2026-08-31**, não validada em HW | ver V2.1 abaixo |
| Anel e slot de fence por contexto | ❌ hardcoded (`0xF400000000`, `0xF400002200`) | um anel + slot por contexto |
| Handles fake de device/context | ❌ `hDevice=(HANDLE)1`, `hContext=(HANDLE)2` | remover do binário de produção |
| `SubmitCommandVirtual` sem executor | ❌ `STATUS_SUCCESS` vazio | `STATUS_NOT_SUPPORTED` |
| GPUVA dinâmica por processo | ❌ ausente | alocador de VA + page tables por contexto + VMID |
| TDR / reset de engine provado | ⚠️ `TdrReady=1` declarado, sem prova | reset comprovado após shader travado |
| MMIO/SMN via IOCTL de user mode | ❌ furo de segurança | só no `bc250dbg.sys`, fora do binário assinado |

#### V2.1 — Retirada de fence implementada (2026-08-31)

O que foi escrito, e por quê cada peça existe:

| Peça | Arquivo | Papel |
|---|---|---|
| `BC250_WDDM_FENCE_TRACKER` — FIFO de 64 entradas | `bc250_submission_engine.h` | Correlaciona a fence de 64 bits do anel (nossa) com o `SubmissionFenceId` de 32 bits do dxgkrnl (o único que ele reconhece) |
| `Bc250WddmFenceTrackerPush/Drain/Reset` | `bc250_submission_engine.c` | Aritmética pura, sem lock e sem hardware; recusa regressão de fence e recusa transbordo |
| `Bc250ActiveSynchronizedFenceCallback` | `bc250_active_miniport.c` | Roda em DIRQL dentro de `DxgkCbSynchronizeExecution`; emite `DxgkCbNotifyInterrupt` com `DXGK_INTERRUPT_DMA_COMPLETED` |
| `Bc250ActiveRetireAndNotify` | idem | DISPATCH_LEVEL: chama `Bc250SubmissionEnginePoll` (que faz MMIO e lê a fence da VRAM), depois sincroniza e notifica |
| `Bc250ActiveRetirementTimerStart/Stop` + KTIMER/KDPC de 1 ms | idem | Gatilho de varredura, armado na primeira submissão aceita e cancelado no `StopDevice` |
| Contrapressão antes de submeter | idem | FIFO cheio → `STATUS_GRAPHICS_ALLOCATION_BUSY`; o lote nunca entra no anel sem poder ser notificado |
| 12 campos de telemetria `V2*` | `bc250_probe_telemetry.{h,c}` | `V2NotifiedTotal`, `V2FencesInFlight`, `V2TrackerOverflow`, `V2InterruptsSeen`, … todos observados |

Decisões que precisam ficar registradas porque são compromissos, não vitórias:

- **A conclusão é descoberta por varredura, não por interrupção.** O anel de IH do MEC
  nunca foi validado neste projeto, então não existe registrador de status que permita
  atribuir a interrupção a este dispositivo. A fonte da verdade continua sendo a fence que
  a GPU grava na VRAM — isso é honesto — mas a latência é a do timer (1 ms), não a de
  interrupção. Trocar por interrupção real é item posterior.
- **O ISR deixou de reivindicar a linha.** A versão anterior devolvia `TRUE`
  incondicionalmente, o que em linha compartilhada é reivindicar interrupção de terceiros.
  Agora devolve `FALSE` e apenas enfileira o DPC oportunisticamente.
- **`SubmissionFenceId` é `UINT` no próprio DDI.** O `(uint32_t)` em
  `bc250_mcdm_callbacks.c:951` está correto; a suspeita inicial de truncamento de 64 bits
  era infundada e foi retirada deste roadmap.

**Provas offline obtidas:**

```text
driver\wdkuild-contract.bat            -> EXIT 0, limpo em /W4 /WX
active_miniport_test.exe                  -> EXIT 0 (inclui o novo contrato do FIFO)
driver\wdk\outputc250mcdm.sys SHA-256  -> 569991C5F8038248900895005632C34F18BA1902C43991E4C1EC2795FCE8A845
```

O contrato novo (`Bc250RunWddmFenceTrackerTest`) prova: FIFO vazio não notifica; fence não
gravada não aposenta; aposentadoria estritamente em ordem; regressão de fence recusada;
transbordo recusado sem sobrescrever; recorte por lote sem perder o resto; índice circular
correto após esvaziar.

#### V2.2 — Primeira corrida física do binário V2 (2026-08-31 12:07)

Cadeia de artefatos conferida ponta a ponta, desta vez correta:

```text
build    driver\wdk\output_labc250mcdm.sys   D2315A012B4E8008...
pacote   assinado                                38B56891B736E48C...
instalado System32\driversc250mcdm.sys        38B56891B736E48C...
telemetria 834 -> 846 valores  (os 12 campos V2* presentes e legiveis)
```

`test-p2-multi-submission.ps1` **aprovado** em boot limpo:
`P2SubmissionsAccepted = 16`, `P2SubmissionsRetired = 16`, `P2TriadPassed = 1`,
`P1SubmitAccepted = 17`, `P1RetiredTotal = 17`.

E, no mesmo ciclo, **todos os contadores V2 em zero**:

```text
V2TrackerPushedTotal 0   V2NotifiedTotal 0   V2RetirementDpcRuns 0
V2TrackerOverflow    0   V2PushFailures  0   V2InterruptsSeen    0
```

**Achado, e ele importa:** as 17 submissoes da P1/P2 sao do **self-test interno do
motor**, que chama `Bc250SubmissionEngineSubmit` diretamente
(`bc250_submission_engine.c:1182` e `:1525`) e portanto **contorna**
`Bc250McdmSubmitCommandVirtual` — que e' onde o rastreador da V2 vive.

Consequencia: a P2, por melhor que seja, **nao exercita o caminho WDDM**. O codigo da
V2 continua sem nunca ter executado. Zero aqui significa "nao exercitado", nao
"quebrado". So um UMD real, fazendo o dxgkrnl chamar `DxgkDdiSubmitCommandVirtual`,
acende esses contadores — o que funde a V1 e a V2 numa unica corrida.

#### V2.3 — Guard-rails adicionados ao bind (2026-08-31)

Dois erros reais desta sessao viraram bloqueio em `toolsind-bc250mcdm-lab.ps1`:

| Guarda | O que bloqueia | Erro que originou |
|---|---|---|
| build == pacote | `output_labc250mcdm.sys` divergente de `manifest.SourceSysSha256` | 11:59 — o bind instalou o binario da vitoria P2 porque o empacotador nao havia rodado |
| engine limpa | `HqdActiveBefore != 0` ou `HqdActiveBaselineWptrReadback != 0` | 12:03 — rebind sem boot frio, WPTR remanescente 204, motor nao subiu, tudo zerado |

O segundo aceita `-IAcceptDirtyEngine` para casos deliberados, avisando que a telemetria
do ciclo nao vale como evidencia. Ambos conferidos contra o estado limpo atual: passam
sem falso positivo, e o segundo teria bloqueado o bind das 12:03.

**Tripé de aceitação de V2 (ainda NÃO obtido):** `RPTR == WPTR` · fence física em VRAM lida
pelo poll · `dxgkrnl` retirando a fence sem TDR, conferido em 16 submissões consecutivas,
com `V2NotifiedTotal == 16` e `V2TrackerOverflow == 0` na telemetria.

**Próximo passo de V2:** construir a imagem de laboratório. O
`build-bc250mcdm-lab-bringup.ps1` ficou em preflight de propósito — ele exige
`-Execute -IUnderstandThisEnablesDestructiveBringup` porque a imagem resultante executa
bring-up destrutivo no `StartDevice` (cenário de tela preta). Essa é uma decisão humana,
não automática.

Ordem obrigatória, sob pena de assinar o binário anterior:

```text
1) driver\wdkuild-contract.bat                      (objetos de producao)
2) toolsuild-bc250mcdm-lab-bringup.ps1 -Execute ...  (relink de laboratorio)
3) tools\package-bc250mcdm-lab.ps1                     (assinatura)
```

---

### Fase V3 — RADV: winsys e alvo de ISA · **EM ANDAMENTO**

#### V3.0 — Terreno reconhecido (2026-08-31)

A árvore do Mesa está na máquina e é a origem do binário instalado:

```text
C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm
origin  https://gitlab.freedesktop.org/lfrb/mesa.git   branch wddm2
HEAD    801c976  "HACK: vulkan/wddm2: always check status"
```

Bate com o `git-801c9763c6` que o `vulkaninfo` reporta. `meson`, `ninja`, `python` e
`git` estão instalados, e existem `build_radv.bat` e uma pasta `build`. Rebuild é viável.

O fork já tem winsys WDDM real (`src/amd/vulkan/winsys/wddm2/`), já conhece a BC-250
(bloco dedicado para `device_id == 0x13FE`) e já tem o alvo gfx1013
(`amd_family.h:112 — CHIP_GFX1013, /* AMD BC-250 */`). Ou seja: não falta escrever
winsys nem portar compilador. Faltam correções pontuais.

#### V3.1 — Três defeitos localizados

**Defeito 1 — a submissão falha em silêncio.** `radv_wddm2_cs.c`, logo após
`D3DKMTSubmitCommand` / `SubmitCommandToHwQueue`:

```c
if (!NT_SUCCESS(status)) {
   /* Non-fatal: kernel processes command asynchronously */
   status = 0;
}
```

Qualquer NTSTATUS de falha é descartado e o RADV reporta sucesso para cima. É a mesma
classe de mentira que a V0 removeu do nosso ICD, e explica a observação inteira: o
`ggml` acredita que despachou, os buffers ficam com lixo, os tokens saem como ruído e o
`llama-bench` reporta números 60x acima do pico do silício.

**Defeito 2 — DDI de submissão incompatível.** Nosso miniport registra **apenas**
`DxgkDdiSubmitCommandVirtual` (`bc250_mcdm_callbacks.c:1126`) e não implementa
`DxgkDdiCreateHwQueue` nem `DxgkDdiSubmitCommand`. Sem suporte a hardware queue,
`queue->handle` fica nulo e o RADV cai no ramo `D3DKMTSubmitCommand`. Como nenhum
contador da V2 se moveu, a chamada morre no dxgkrnl antes de chegar ao nosso DDI — e o
Defeito 1 apaga o motivo. Qual das duas pontas ceder (implementar o DDI que falta, ou
fazer o RADV criar contexto com endereçamento virtual) só se decide vendo o NTSTATUS.

**Defeito 3 — alvo de ISA errado, e a correção é um valor.**
`radv_wddm2_winsys.c:313` monta as propriedades da BC-250 com:

```c
props2.family_id         = FAMILY_NV;
props2.chip_external_rev = 1;
```

`ac_gpu_info.c:658` resolve `(FAMILY_NV, external_rev)` por faixa, e
`amdgpu_asic_addr.h` define:

```text
AMDGPU_NAVI10_RANGE    0x01, 0x0A     <- external_rev 1 cai aqui
AMDGPU_GFX1013_RANGE   0x82, 0x86
```

Por isso o device aparece como `RADV NAVI10` e o ACO compila gfx1010. Para gfx1013 o
`chip_external_rev` precisa estar em `0x82..0x85`.

#### V3.2 — Ordem de ataque

1. **Remover o engolidor de status** (Defeito 1) e imprimir o NTSTATUS. É a mudança mais
   barata do roadmap e transforma uma falha invisível em diagnóstico. Sem hardware.
2. **Rebuild do RADV** e nova corrida do `tools/test-v2-wddm-submission.ps1`: o NTSTATUS
   dirá se o problema é o Defeito 2 e qual é exatamente.
3. **Corrigir o alvo de ISA** (Defeito 3). Independente dos outros, mas inútil sozinho:
   com winsys morto não executa, e com ISA errada executaria lixo.
4. Só então a **Fase V2** volta a ser validável, e a **V4** (`test-backend-ops`) passa a
   fazer sentido.

---

#### V3.3 — Escopo restante do RADV (decisão de arquitetura)

Decisão de arquitetura já tomada e justificada na auditoria: **não escrevemos compilador.**
O ACO já está na máquina, maduro e conformante 1.4. Embutir ACO numa DLL própria
arrastaria `spirv_to_nir` + NIR + `radv_shader_info` (~200k LOC sem upstream). E DX12/DXC
**não** fornece backend: DXIL é IR, o tradutor DXIL→ISA é do IHV.

| Item | Alvo |
|---|---|
| ~~Família de silício~~ | **FEITO na V3.1**: `chip_external_rev = 0x82`, verificado como `RADV GFX1013` |
| Winsys | `D3DKMT` apontado ao `bc250mcdm.sys` (símbolos já presentes na DLL: `SubmitCommand`, `CreateAllocation`, `MapGpuVirtualAddress`, `CreateContext`) |
| Orçamento de memória | `D3DKMTQueryVideoMemoryInfo` real, em vez de 16384 MiB fixos |
| UMA | heap `DEVICE_LOCAL | HOST_VISIBLE` para o `ggml` parar de reportar `uma: 0` |
| Subgroup | wave32 consistente via `VK_EXT_subgroup_size_control`; hoje o driver diz 64 e o `ggml` usa 32 |
| Identidade | `deviceID` e UUID reais, não sintéticos; WARP não pode ser enumerado como RADV |

---

### Fase V5 — Nó de render: sair de Display-Only · **BLOQUEANTE DA V2, PRÉ-REQUISITO DE JOGOS**

#### O que o driver realmente é hoje

Correção de duas afirmações erradas feitas antes nesta auditoria:

| Eu havia dito | O código mostra |
|---|---|
| `Class=ComputeAccelerator`, MCDM compute-only | `Class=Display`, ClassGuid `{4d36e968-...}` (`bc250mcdm.inf:3`) |
| "não tem VidPn" | VidPn real e funcional: `IsSupportedVidPn`, `RecommendFunctionalVidPn`, `EnumVidPnCofuncModality`, `CommitVidPn`, `SetVidPnSourceAddress`, `SetVidPnSourceVisibility` em `bc250_display_ddi.c` |

O driver é um **KMDOD (Kernel Mode Display Only Driver)** WDDM v1.2. Ele registra por
`DxgkInitializeDisplayOnlyDriver` (`bc250_active_miniport.c:4687`), tem present por
`DxgkDdiPresentDisplayOnly`, e o `confirm` já prova em hardware que o caminho roda:
`DmmCommitStatus = 0`, `DmmCommitHasPath = 1`, `DmmPresentEntered = 1`, VSync contando.

#### E isto explica o Defeito 2 da V3

`KMDDOD_INITIALIZATION_DATA` (WDK 10.0.26100, `km/dispmprt.h`) **não possui nenhum
campo** de `Submit*`, `Render`, `CreateContext`, `CreateAllocation`, `Patch` ou
`BuildPagingBuffer`. Foi conferido campo a campo: a estrutura é de apresentação, e só.

Consequência direta, e é grave:

- O `DxgkDdiSubmitCommandVirtual` que preenchemos em
  `Bc250McdmPopulateRequiredCallbacks` vai para um `DRIVER_INITIALIZATION_DATA` que,
  no caminho vivo, **nunca é entregue ao dxgkrnl**. A única chamada a `DxgkInitialize`
  está atrás do sentinel `DriverObject == 0xDEADBEEF`
  (`bc250_active_miniport.c:4682`), que é gancho de teste offline e jamais é verdade
  em produção.
- Portanto o dxgkrnl não enxerga **nó de render nenhum** neste adaptador. Não há
  contexto, não há fila de comandos, não há para onde submeter.
- Logo a Fase V2 é **inalcançável por construção** neste binário: o rastreador de
  fences não está "não exercitado por falta de UMD", está atrás de um DDI que o
  sistema operacional nunca vai chamar.
- E o RADV não falha por bug dele: `D3DKMTSubmitCommand` contra um adaptador
  display-only não tem como funcionar. O Defeito 1 (status engolido) escondia
  justamente isso.

#### Por que isto converge com jogos

A migração exigida pelos dois objetivos é a mesma:

```text
DxgkInitializeDisplayOnlyDriver  ->  DxgkInitialize
        (KMDDOD)                        (WDDM display + render)
```

O trabalho de VidPn feito até aqui **não é descartado**: a lógica de topologia, modos e
commit migra praticamente inteira. O que entra de novo é o nó de render.

| Item | Hoje | Necessário |
|---|---|---|
| Registro no dxgkrnl | `DxgkInitializeDisplayOnlyDriver` | `DxgkInitialize` com `DRIVER_INITIALIZATION_DATA` completa |
| Nó de render / contexto | inexistente | `CreateDevice`, `CreateContext`, `CreateAllocation`, `DescribeAllocation` (já escritos, hoje órfãos) |
| Submissão | inalcançável | `SubmitCommandVirtual` (escrito) + retirada de fence da V2 (escrita) |
| Paging | `BuildPagingBuffer` existe, órfão | ligado ao VidMm, com transferências por SDMA |
| GPUVA por processo | ausente | alocador de VA + page tables por contexto (item da V2) |
| Present | `PresentDisplayOnly` (blit por CPU) | `DxgkDdiPresent` com flip e primário em VRAM |
| `DxgkDdiPatch` | ausente | necessário para render sem GPUVA; dispensável com GPUVA |
| TDR / preempção | declarado, sem prova | obrigatório — jogo trava GPU como rotina |

#### V5.1 — Levantamento da migração (2026-08-31)

Contagem real, feita sobre o código e sobre `km/dispmprt.h` do WDK 10.0.26100:

- `DRIVER_INITIALIZATION_DATA` expõe **187** campos `DxgkDdi*`. A esmagadora maioria é
  opcional (overlay, live migration, protected session, virtualização, native fence).
- O projeto já implementa **39** funções `Bc250DxgkDdi*`.
- `Bc250McdmPopulateRequiredCallbacks` já preenche **38** entradas da tabela.

A conclusão que importa: **a metade de display de um driver WDDM completo já está
escrita.** Além do VidPn provado em hardware, existem `SetPointerPosition`,
`SetPointerShape`, `RecommendMonitorModes`, `RecommendVidPnTopology`,
`QueryVidPnHWCapability`, `UpdateActiveVidPnPresentPath` e `QueryInterface`. O que falta
é o nó de render e a ponte de paginação.

**O que falta, por grupo:**

| Grupo | DDI ausente | Necessidade |
|---|---|---|
| Registro | trocar `DxgkInitializeDisplayOnlyDriver` por `DxgkInitialize` | Obrigatório — é o que cria o nó de render |
| Present | `DxgkDdiPresent` hoje devolve `STATUS_NOT_SUPPORTED` | Obrigatório para render; hoje é stub honesto |
| Fence | `DxgkDdiQueryCurrentFence` | Obrigatório |
| Paginação GPUVA | `UpdatePageTable`, `UpdatePageDirectory`, `MovePageDirectory`, `DescribePageTable` | Obrigatório com GPUVA |
| Caminho legado | `DxgkDdiPatch`, `DxgkDdiRender`, `DxgkDdiSubmitCommand` | Dispensável se ficarmos em GPUVA + `SubmitCommandVirtual` |
| Escalonamento por hardware | `CreateHwQueue`, `DestroyHwQueue`, `SubmitCommandToHwQueue` | Opcional, mas o RADV **prefere** este ramo quando existe |
| Diagnóstico | `DxgkDdiEscape` | Opcional; o fork do RADV já tem um caminho de PM4 direto por `D3DKMTEscape` atrás de `RADV_BC250_DIRECT_PM4_ESCAPE` |

**Ordem interna da V5:**

1. Trocar o registro para `DxgkInitialize` mantendo todo o resto igual, e medir: o
   adaptador ainda enumera? O display ainda acende? Este é o passo de maior risco de
   tela preta de todo o projeto.
2. `DxgkDdiQueryCurrentFence` + as quatro DDIs de página, ligando ao GCVM já provado.
3. Só então a Fase V2 vira mensurável — `V2TrackerPushedTotal` deve sair de zero.
4. `DxgkDdiPresent` real com flip, para jogos.

**Risco de tela preta:** o passo 1 muda a natureza do adaptador perante o dxgkrnl. O
watchdog de rollback e o `C:\BC250_RECOVERY.cmd` deixam de ser rede de segurança
ocasional e passam a ser infraestrutura de trabalho diária.

#### Ordem

V5 vem **depois** de V2/V3 estarem prontos no código e **antes** de qualquer um dos dois
poder ser validado em hardware. Na prática:

1. V3 termina de instrumentar o RADV (feito) e corrige o alvo de ISA (feito).
2. V5 migra o registro para `DxgkInitialize` e liga o nó de render.
3. Só então a V2 é exercitável, e o `test-v2-wddm-submission.ps1` passa a medir algo.
4. V4 (`test-backend-ops`) valida operador a operador.
5. Com render node vivo e ISA correta, jogos passam a ser questão de completar
   `DxgkDdiPresent`, flip e WSI — não mais de classe de driver.

**Risco a registrar:** sair de display-only significa que o adaptador passa a ser
candidato a primário de verdade. O cenário de tela preta deixa de ser efeito colateral do
bring-up e vira modo de falha normal do desenvolvimento. O watchdog de rollback e o
`C:\BC250_RECOVERY.cmd` passam a ser infraestrutura crítica, não rede de segurança
ocasional.

---

### Fase V4 — Gate de aceitação por operador · depende de V5 e V2

`llama-cli` **não** é instrumento de medição. O gate é `test-backend-ops` do llama.cpp,
que compara operador por operador GPU contra CPU. Não está nos binários presentes na
máquina; precisa ser compilado do fonte.

Escada de bisseção quando (e só quando) houver execução comprovada:

1. `test-backend-ops` — todos os ops falham → backend morto, volta a V2.
2. Modelo F16 → Q8_0 → Q4_0 → Q4_K_M. F16 já corrompido → não é desquantização.
3. Falham só reduções (softmax, RMSNorm, GEMV) → tamanho de subgrupo.
4. Falham só ops quantizados → `shaderInt8` / `VK_KHR_8bit_storage`.
5. `GGML_VK_DISABLE_F16=1`, `GGML_VK_DISABLE_COOPMAT=1`, `-ngl 0` como referência,
   `-ngl 1` para bissectar camada.

Só depois disso um token gerado significa alguma coisa.

---

### Fase V6 — ICD próprio: bancada, não produto

O ICD próprio permanece **fail-closed** e vira instrumento de laboratório para ensaios de
caminho conhecido (ISA pré-compilada, buffer único). Reabrir para aplicações de terceiros
exigiria descritores, pipeline layout, barreiras com `ACQUIRE_MEM`, semáforos, query pool
e tradutor SPIR-V→ISA — trabalho que V3 entrega de graça via ACO.

Faltam hoje 44 entry points; os grupos ausentes estão em
`tools\audit-vulkan-icd-entrypoints.py`.

---

## 4. Ordem de execução

```text
V0 ✅ ──> V1 ✅ ──> V3 (em andamento) ──> V5 ──> V2 ──> V4 ──> jogos
                          │                │      │      │
             ISA gfx1013 ✅               │      │      └─ test-backend-ops
             status nao engolido 🟡       │      └─ V2NotifiedTotal sai de zero
                                          └─ DxgkInitialize: cria o no de render
```

**A V2 mudou de posição.** Ela estava marcada como bloqueante único e vinha antes da V3.
O levantamento da V5 mostrou que ela é *inalcançável* enquanto o driver registrar por
`DxgkInitializeDisplayOnlyDriver`: o `DxgkDdiSubmitCommandVirtual` nunca chega ao
dxgkrnl. O código da V2 está escrito e com contrato offline aprovado; ele fica em espera.

A V6 (ICD próprio) não bloqueia nada e roda em paralelo quando houver folga.

### Próximos passos, em ordem

| # | Trabalho | Desbloqueia | Custo de ensaio |
|---|---|---|---|
| 1 | Corrida com o RADV instrumentado → capturar o `NTSTATUS` | Fecha o diagnóstico por escrito | 1 corrida, sem bind novo |
| 2 | **V5.1**: `DxgkInitializeDisplayOnlyDriver` → `DxgkInitialize` | V2, V3, V4 e jogos | Alto risco de tela preta |
| 3 | `QueryCurrentFence` + 4 DDIs de página, ligadas ao GCVM | Render com GPUVA | — |
| 4 | **V2** validável: `V2NotifiedTotal == 16`, `V2TrackerOverflow == 0` | Submissão WDDM real | 1 bind + boot frio |
| 5 | **V4**: `test-backend-ops` operador a operador | Primeiro token que significa algo | — |
| 6 | `DxgkDdiPresent` com flip + WSI + TDR provado | Jogos | — |

O passo 1 é o único que ainda pode ser feito com o binário atual.

---

## 5. Comandos deste roadmap

```bash
python tools\audit-vulkan-icd-entrypoints.py
```

```bash
powershell -ExecutionPolicy Bypass -File tools\clean-mesa-overrides.ps1
```

```bash
cmd /c driver\umd\build-umd.bat
```

```bash
powershell -ExecutionPolicy Bypass -File tools\capture-baseline.ps1
```

---

## 6. Histórico

| Data | Evento |
|---|---|
| 2026-08-31 | Auditoria inicial. T1–T6 executados. Fase V0 aplicada e provada; ICD deixa de derrubar o loader (`0xC0000005` → skip limpo). Roadmap criado. |
| 2026-08-31 | V0 fechada: overrides do Mesa removidos de HKCU e HKLM. Medição pós-limpeza descartou o override como causa (RADV reporta 1.4.348 de verdade). |
| 2026-08-31 | **V5.1 escrita**: sentinel 0xDEADBEEF substituido pelo tunable RenderNode; DriverEntry passa a escolher entre KMDOD e DxgkInitialize. Tres campos de telemetria V5* e switch -EnableRenderNode no bind, com aviso de que o valor so vale no proximo boot por ser lido em DriverEntry. Build limpo, contratos offline aprovados. Interruptor DESLIGADO e ausente do binario instalado. |
| 2026-08-31 | **V5 aberta**: verificado que o driver e um KMDOD (Class=Display, DxgkInitializeDisplayOnlyDriver, VidPn real e provado em hardware), nao um MCDM compute-only. KMDDOD_INITIALIZATION_DATA nao tem campo de submissao, logo o SubmitCommandVirtual nunca chega ao dxgkrnl e a Fase V2 e inalcancavel por construcao. Migrar para DxgkInitialize desbloqueia a V2 e e o mesmo caminho exigido por jogos. |
| 2026-08-31 | **V3.0/V3.1**: arvore do Mesa (lfrb/mesa, branch wddm2, HEAD 801c976) localizada na maquina com toolchain completa. Tres defeitos localizados: o winsys engole o NTSTATUS da submissao; o miniport so registra SubmitCommandVirtual enquanto o RADV usa D3DKMTSubmitCommand; chip_external_rev=1 mapeia para NAVI10 em vez da faixa 0x82..0x85 de GFX1013. |
| 2026-08-31 | **V1 CONCLUIDA**: corrida valida do llama-bench com delta zero em todos os contadores WDDM e numeros ~60x acima do pico de compute do silicio. O RADV nao submete. Hipoteses Q4_K/FP16/barreira descartadas. Ferramenta de ensaio corrigida (NativeCommandError) e evidencias invalidas removidas. |
| 2026-08-31 | V2.2: binario V2 no silicio, cadeia de hashes conferida, P2 16/16 aprovada em boot limpo. Contadores V2 em zero — descoberto que P1/P2 contornam o DDI do WDDM, logo o caminho da V2 segue nao exercitado. Guard-rails de build e de engine suja adicionados ao bind. |
| 2026-08-31 | V2.1 implementada: rastreador de fences WDDM, notificação `DXGK_INTERRUPT_DMA_COMPLETED`, varredura por KTIMER, contrapressão e 12 campos de telemetria. Build de produção limpo, contrato offline aprovado. Imagem de laboratório **não** construída — exige decisão humana. |
