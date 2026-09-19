# 243 — Consulta Técnica Externa: Marco 2 RADV Present, BugCheck 0x10E Arg1=0x4 e Inanição na Fila de Paginação SDMA

> **Destinatários:** DeepSeek, Claude, Sparke e revisores de arquitetura WDDM.  
> **Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Base desta análise:** Medição física direta no silício na corrida de 11/09/2026 10:35:34–10:36:56 (Pacote `3.134.1.0` / DriverVer `09/10/2026,3.134.0.0` + Mesa RADV ACO WDDM).  
> **Evidências físicas arquivadas:**  
> - Pasta da corrida: `docs/evidence/2026-09-11_10-35-34-DESKTOP-HQAG6T4-lab-binding/`  
> - Log vivo cumulativo: `C:\BC250_DISPLAY_LOG.txt` (54.513.106 bytes, janela de análise: linhas 614.094 a 615.300).  
> - Diário do harness: `radv-present-tripod-2026-09-11_10-35-41-305/harness-stage-journal.txt`.  
> - Log do Sistema Windows: Evento 1001 WER — BugCheck `0x0000010E (0x0000000000000004, 0x0, 0x000000000000000a, 0x0)` às 10:36:56.  
> - Despejo de memória: `C:\Windows\MEMORY.DMP` (821.799.548 bytes, gerado às 10:36:49).  
> - Fusível anti-boot-loop: `C:\BC250_BOOT_FUSE.dat` disparado (`State=2`, `Boots=12`, `LastStatus=0xC0000001`).  

---

## 0. Resumo Executivo & O que se Pretendia

Na sessão de hoje (11/09), após a confirmação do **Marco 1** (Compute Shader Vulkan compilado pelo Valve ACO executado no silício físico via anel MEC/MQD no Node 1, arquivado em `docs/241`), partimos para o **Marco 2**:
Apresentação direta na tela física via anel SDMA (Node 0) para o scanout herdado do UEFI GOP (`0xC0000000`, 2560×1080 @ 56.13 Hz), ignorando completamente o compositor D3D11 fechado do DWM.

Ao executar o ensaio às 10:35:34 com o pacote `3.134.1.0`:
1. O driver WDDM subiu em Código 0.
2. O display GOP convergiu com sucesso (`CommitVidPn` e `SetVidPnSourceAddress` para `0xAAC000`).
3. O harness Vulkan `bc250-radv-present-gate.exe` inicializou com sucesso via ICD explícito, enumerou o adaptador físico como `"AMD BC-250 Graphics"` (diário foi até `after vkEnumeratePhysicalDevices`).
4. O anel MEC (Node 1) recebeu submissão de 192 bytes (`FenceId=1`, anel MEC `AssignedFence=18`, consumido com `RPTR=12 WPTR=12`).
5. O agendador de memória de vídeo (`dxgmms2!VidMm`) despachou comandos de paginação no Node 0 (SDMA).
6. A tela permaneceu preta por ~15 segundos com 52 quadros em branco reportados via Present, e o Windows reiniciou com **BugCheck `0x10E` Arg1=`0x4`**.

---

## 1. O Que o Silício Físico Mediu

### A. O Node 1 (MEC / Compute ACO)
No log vivo `C:\BC250_DISPLAY_LOG.txt` (linhas 615163 a 615188):
```text
[McdmSubmitCommandVirtual] Node=1, FenceId=1, DmaSize=192
[SubmitVirtual Node1] GATE ABERTO: GpuvaMappingsOperational=1; publicando IB de 192 bytes em VA=0x200AC4000 no anel persistente MEC (FenceId=1)
[SubmitVirtual Node1] PrepareComputeVmid: Root=0x45FFA8000 Sub=17 Ret=17 InFlight=0
[FlushMappedGfxhubTlb] Transaction status=0, PhysReady=1, AckObserved=1, Polls=1
[SubmitVirtual Node1] VMID9_BIND: Root=0x45FFA8000 Submitted=17 Retired=17 Status=0 Ready=1
[PT_WALK NODE1_IB] L1 idx=4101 VRAM+0xFFB0028 PDE=000000045FFA1001 -> leafPA=0x45FFA1000 flags=0x1 valid=1
[PT_WALK NODE1_IB] L0 idx=196 VRAM+0xFFA1620 PTE=0000000452AF4031 -> pageMC=0x452AF4000 flags=0x31 valid=1
[PT_WALK NODE1_IB] RESOLVIDO: VA=0x200AC4000 -> 0x452AF4000 (root=MC, folha=MC)
[PT_WALK NODE1_IB] IB_CPU_PREVIEW: VA=0x200AC4000 PA=0x452AF4000 Count=32 DW0=C0017900 DW1=0000007B ...
[SubmissionEngineSubmit] SUCESSO: NewWptr=12 Fence=18 Pad=0 Accepted=18 Kicks=18
[MEC_POST_DOORBELL] Active=00000001 RPTR=12 WPTR=12 Error=00000000 HqStatus=40008040 Vmid=00000900
[SubmitVirtual Node1] PUBLICADO NO ANEL MEC: FenceId=1 AssignedFence=18 DmaVA=0x200AC4000 DmaSize=192
```
A GPU consumiu o pacote do anel MEC (`RPTR=12 WPTR=12`). Ressalva de auditoria: o diário do harness parou em `vkEnumeratePhysicalDevices`, logo esta corrida isolada não carrega probe-after/stdout de conclusão do shader; o Marco 1 permanece amparado pela corrida das 09:18 (`docs/241`).

---

## 2. O Incidente: BugCheck `0x0000010E` Arg1=`0x4`

### 2.1 Registro Oficial do Windows (WER Event 1001)
```text
TimeCreated : 11/09/2026 10:36:56
Message     : O computador foi reinicializado apos uma verificacao de erro. Essa verificacao foi: 
              0x0000010e (0x0000000000000004, 0x0000000000000000, 0x000000000000000a, 0x0000000000000000). 
              Um despejo de memoria foi salvo em: C:\Windows\MEMORY.DMP.
```

### 2.2 Definição da Microsoft
- **Código:** `0x10E` (`VIDEO_MEMORY_MANAGEMENT_INTERNAL`)
- **Arg1 = `0x4`:** `"A rotation in a must-succeed path failed."` (Uma rotação de alocação de memória de vídeo pelo VidMm falhou em um caminho de execução obrigatório que não admite falhas).
- **Arg3 = `0xA`:** Parâmetro interno do VidMm no despejo de memória.

---

## 3. A Causa Raiz: Inanição Absoluta de Fences na Fila de Paginação SDMA (Node 0)

### 3.1 O Que o Log Vivo Revelou no Node 0
Durante todo o ensaio (linhas 614314 a 615268 de `C:\BC250_DISPLAY_LOG.txt`):
- **Submissões de buffer DMA despachadas no Node 0:** **14 submissões** (`FenceId=1, 2, 8, 10, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22`).
- **Submissões aposentadas e reportadas via `DMA_COMPLETED` ao VidMm:** **ZERO!**
- **Linhas `[SdmaRetire]` no log:** **ZERO!**
- **Linhas `IB_TRIPOD TIMEOUT` no log:** **ZERO!**

Todas as 14 submissões foram aceitas e registradas:
```text
[SubmitVirtual Node0 SDMA] SUBMISSAO DESPACHADA (ASYNC INDIRECT): HwFence=4098 FenceId=1 DmaBytes=64; DPC agendado
[SubmitVirtual Node0 SDMA] SUBMISSAO DESPACHADA (ASYNC INDIRECT): HwFence=4099 FenceId=2 DmaBytes=64; DPC agendado
...
[SubmitVirtual Node0 SDMA] SUBMISSAO DESPACHADA (ASYNC INDIRECT): HwFence=4123 FenceId=22 DmaBytes=96; DPC agendado
```
Mas **nenhuma** interrupção ou DPC concluiu essas submissões perante o Windows. Quando o VidMm tentou rotacionar alocações para o processo Vulkan, ele bloqueou esperando a conclusão dos buffers de paginação pendentes. Ao esgotar o prazo interno, o kernel disparou o BugCheck `0x10E` Arg1=`0x4`.

---

## 4. Análise de Código: A Falha de Liveness no `SdmaRetireBusy`

Em `driver/wdk/bc250_active_miniport.c`:
```c
static VOID Bc250ActiveSdmaRetireAndNotify(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ BOOLEAN FromDpc)
{
    ...
    if (InterlockedCompareExchange(&Context->SdmaRetireBusy, 1, 0) != 0) {
        ++Context->SdmaRetirementBusyYields;
        return; /* <-- PERIGO FATAL: descarta a execucao sem reagendar */
    }
```
**O mecanismo de perda de retentativa:**
1. A thread de submissão entra em `SubmitCommandVirtual` (em `DISPATCH_LEVEL`), chama `Bc250ActiveSdmaRetireAndNotify(context, FALSE)` e adquire `SdmaRetireBusy = 1`.
2. Em outro núcleo, o DPC do timer dispara e executa `Bc250ActiveSdmaRetirementDpcRoutine`.
3. A rotina do DPC zera o flag do timer: `InterlockedExchange(&context->SdmaRetirementTimerRunning, 0)`.
4. O DPC chama `Bc250ActiveSdmaRetireAndNotify(context, TRUE)`.
5. Como `SdmaRetireBusy` está em `1` (pela thread de submit), o DPC colide no `CompareExchange`, incrementa `BusyYields` e **retorna imediatamente**.
6. O DPC **não re-arma o timer** e não processa nada.
7. A thread de submissão termina sua passada de polling quando a GPU ainda não escreveu a fence ou a fila está incompleta, solta `SdmaRetireBusy = 0` e sai.
8. Se nenhuma submissão futura for enviada (a última foi `FenceId=22`), a fila inteira fica sem nenhum timer ou DPC ativo.
9. **Resultado:** Inanição total e permanente da fila de paginação, culminando em BugCheck `0x10E` Arg1=`0x4`.

---

## 5. Respostas da Auditoria Externa (Consenso DeepSeek / Hy 4 / Sparke)

### P1 — Voltar a polling síncrono no SubmitCommandVirtual?
**NÃO.**
- `SubmitCommandVirtual` executa em `DISPATCH_LEVEL`. Espera síncrona com loop de polling (mesmo com teto baixo de 200 polls / < 50 µs) viola o gate de arquitetura assíncrona e foi o mecanismo comprovado que gerou o BugCheck `0x133` (estouro de DPC Watchdog) na corrida das 12:16 (`docs/235`).
- O modelo assíncrono é o correto e já foi validado (1.819 submissões sem timeout na corrida das 12:16 e 19.291 na corrida das 19:45). O defeito reside exclusivamente na garantia de retentativa (*liveness*).

### P2 — Invariante Canônico de Liveness
O princípio arquitetural obrigatório: **Nenhum caminho pode sair de `Bc250ActiveSdmaRetireAndNotify` ou de sua DPC deixando trabalho pendente (`InFlightCount > 0`) sem ter uma retentativa (timer ou DPC) garantidamente agendada.**
1. No loser do `SdmaRetireBusy`:
   - Se `FromDpc == TRUE`, reagendar imediatamente via `KeInsertQueueDpc(&Context->SdmaRetirementDpc, NULL, NULL)`.
   - Se `FromDpc == FALSE` e timer não estiver rodando, agendar timer de 1 ms.
2. Na saída de `Bc250ActiveSdmaRetireAndNotify` (após soltar `SdmaRetireBusy = 0`):
   - Se `g_Bc250SdmaPresent.InFlightCount > 0u`, rearmar o timer de retaguarda se não estiver ativo.
3. Resetar `SdmaRetireBusy = 0` em `RestartFromTimeout` e `StopDevice` como defesa em profundidade.
4. Expor os contadores (`BusyYields`, `DpcRuns`, `NotifiedTotal`) nos probes de telemetria.

### P3 — Marco 1 & Marco 2
- **Marco 1**: Permanece plenamente válido com base em `docs/241` (corrida das 09:18 com byte-proof, fence 18 e anel MEC drenado).
- **Marco 2**: Não está desimpedido. O caminho de pixel nos 52 Presents da corrida das 10:35 copiou superfícies em branco (`NonZero=0/256`). A ordem obrigatória é:
  1. Aplicar o invariante de liveness e validar a robustez da fila SDMA com corrida de estresse (critério: ≥ 5.000 submissões, razão de notificação ≥ 0,99, zero BugChecks `0x10E`/`0x119`/`0x133`).
  2. Executar o teste de apresentação do Marco 2 com conteúdo real no framebuffer gerado pelo compute shader Vulkan (ACO).
