# 239 — Consulta Técnica Externa: Corrida das 18:34, BugCheck 0x119 Arg1=0x1 e Condição de Corrida na Notificação de Fences

> **Destinatários:** Claude, DeepSeek, Sparke e revisores de arquitetura WDDM.  
> **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
> **Base desta análise:** Medição física direta no silício na corrida de 10/09/2026 18:34–18:37 (Pacote de Laboratório `3.133.1.0`).  
> **Evidências físicas arquivadas:**  
> - Log vivo cumulativo: `C:\BC250_DISPLAY_LOG.txt` (43.541.515 bytes, 493.369 linhas; trecho da corrida: linhas 448.847 a 493.369 = 44.522 linhas).  
> - Pasta da corrida: `docs/evidence/2026-09-10_18-34-36-DESKTOP-HQAG6T4-lab-binding/`.  
> - Log da UMD: `bc250_umd_log_1200.txt` (469 KB) e `bc250_umd_log.txt` (1.046 KB).  
> - Log de eventos do Windows: Evento 1001 WER — BugCheck `0x00000119` `(0x0000000000000001, 0x0000000000002331, 0x0000000000002332, 0xffffc4847faec000)` às 18:37:51.  
> - Fusível anti-boot-loop: `C:\BC250_BOOT_FUSE.dat` disparado (`State=2`, `Boots=4`, `LastStatus=0xC0000001`).  

---

## 1. Contexto: As correções do Pacote 3.133.1.0 (Baseadas no Consenso do Doc 238)

Seguindo a orientação unânime da consulta anterior ([`docs/238`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/238_RESPOSTA_CONSULTA_0x10E_E_APOSENTADORIA_MULTI_SLOT.md)):
1. **Aposentadoria canônica por fence:** Removemos `Bc250SdmaRptrMatches` (`RPTR == TargetWptr`) do portão de aposentadoria da fila assíncrona multi-slot (`InFlightQueue[32]`). O critério único passou a ser `fenceObserved == nextFence` sobre o poison `0x43DAFFFE`.
2. **Flush de HDP incondicional:** Executado antes de re-ler a fence em memória de trabalho do SDMA.
3. **Timer de retaguarda one-shot:** `Period = 0`, evitando reentrâncias e consumo de CPU com a fila ociosa.
4. **Notificação incondicional ao VidSch:** Desacoplada de checagens de canários de auditoria.

### O Resultado Físico Imediato: O BugCheck 0x10E foi 100% EXTINTO
Nas corridas anteriores das 18:20 e 18:26 com o pacote `3.133.1.0`:
- **`Submitted=31, Completed=31, Fallbacks=0, NotExecuted=0`**!
- ZERO travamentos de máquina, ZERO BugChecks `0x10E`, ZERO TDRs!
- 195 Presents executados ao longo de 180 segundos!
- A inanição da cabeça da fila (*head-of-line starvation*) que causava `0x10E` Arg1=4 foi **comprovadamente eliminada no silício**.

---

## 2. A Corrida das 18:34 (D3D11 e DRAW_BLT_SUBSTITUTION Armados)

Para medir a geração de quadros da área de trabalho, armamos a UMD com:
`-EnableRenderNode -EnableD3D11LabPromotion -EnableD3D11DrawBltSubstitution`.

### 2.1 O Marco Histórico no Silício: 9.010 submissões em 3 minutos!
O driver não morreu no início. Ele sustentou execução contínua por **mais de 3 minutos** (18:34:38 até 18:37:51).
Nesse intervalo:
- A fila multi-slot processou e concluiu com perfeição **9.010 submissões de buffers DMA** (`FenceId=1` até `FenceId=9010`)!
- Foram executados mais de 30 Presents e centenas de alocações/desalocações de superfícies DWM.
- O hardware SDMA operou com zero erros de paginação GFXHUB (`STATUS=00000000`).

---

## 3. O Incidente: BugCheck 0x119 Arg1=0x1 às 18:37:51

Aos 3 minutos e 13 segundos de corrida, o Windows apresentou tela azul.

### 3.1 O Registro Oficial do Windows (WER Event 1001)
```text
O computador foi reinicializado apos uma verificacao de erro. Essa verificacao foi:
0x00000119 (0x0000000000000001, 0x0000000000002331, 0x0000000000002332, 0xffffc4847faec000)
```

### 3.2 Decodificação Exata dos Parâmetros da Microsoft
- **Arg1 = `0x00000001`**: `DRIVER_REPORTED_INVALID_FENCE_ID`  
  *(The driver reported an invalid fence ID to the video scheduler).*
- **Arg2 = `0x00002331`**: Em decimal = **9009**
- **Arg3 = `0x00002332`**: Em decimal = **9010**
- **Arg4 = `0xffffc4847faec000`**: Ponteiro interno da fila do agendador (`dxgmms2!VidSch`).

---

## 4. Análise da Causa Raiz: Condição de Corrida na Notificação de Fences

### 4.1 O Log Físico no Momento do Travamento
No final do arquivo [`C:\BC250_DISPLAY_LOG.txt`](file:///C:/BC250_DISPLAY_LOG.txt) (linhas 493.364 a 493.366):
```text
[SubmitVirtual Node0 SDMA] IB_GPUVA_SNAPSHOT: Status=0 ObjStatus=0 VA=0x222F80 Size=32 -> Segment=0 Offset=0x161154F80 Epoch=0
[SubmitVirtual Node0 SDMA] FenceId=9009, DmaSize=32, DmaVA=0x222FA0
[McdmSubmitCommandVirtual] Node=0, FenceId=9010, DmaSize=32
```
A submissão `9009` foi despachada. Em seguida, o agendador entrou em `McdmSubmitCommandVirtual` para a submissão `9010`.

### 4.2 Onde o Driver Notifica o VidSch
Em `bc250_active_miniport.c`:
```c
static VOID Bc250ActiveSdmaRetireAndNotify(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ BOOLEAN FromDpc)
{
    ...
    while ((polled = Bc250SdmaPresentPollRetirement(&result)) != FALSE) {
        if (result.Completed) {
            RtlZeroMemory(&sync, sizeof(sync));
            sync.Context = Context;
            sync.Operation = BC250_ACTIVE_FENCE_SYNC_NOTIFY_SDMA;
            sync.SubmissionFenceId = (uint32_t)result.SubmissionFenceId;
            sync.NodeOrdinal = 0;

            status = Context->Core.DxgkInterface.DxgkCbSynchronizeExecution(
                Context->Core.DxgkInterface.DeviceHandle,
                Bc250ActiveSynchronizedFenceCallback,
                &sync, 0, &notified);
        }
    }
}
```
E no callback sincronizado:
```c
    if (sync->Operation == BC250_ACTIVE_FENCE_SYNC_NOTIFY_SDMA) {
        data.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
        data.DmaCompleted.SubmissionFenceId = sync->SubmissionFenceId;
        data.DmaCompleted.NodeOrdinal = sync->NodeOrdinal;
        context->Core.DxgkInterface.DxgkCbNotifyInterrupt(
            context->Core.DxgkInterface.DeviceHandle, &data);
        return TRUE;
    }
```

### 4.3 A Condição de Corrida Multithread sem Sincronização
A função `Bc250ActiveSdmaRetireAndNotify` é chamada de **dois locais simultâneos em núcleos distintos**:
1. **Núcleo 1 (Submit Thread)**: No início de `McdmSubmitCommandVirtual` (`Bc250ActiveSdmaRetireAndNotify(context, FALSE)`).
2. **Núcleo 2 (DPC Thread)**: Na rotina `Bc250ActiveSdmaRetirementDpcRoutine` (`Bc250ActiveSdmaRetireAndNotify(context, TRUE)`).

`Bc250ActiveSdmaRetireAndNotify` **não possui nenhum lock de proteção externa**. O único lock existente é o `RingLock` *dentro* de `Bc250SdmaPresentPollRetirement`, que é liberado assim que o item sai da fila circular:

1. **Tempo T0:** Núcleo 1 chama `PollRetirement` e retira o slot com `SubmissionFenceId = 9009`. Núcleo 1 solta o `RingLock` e se prepara para chamar `DxgkCbSynchronizeExecution`.
2. **Tempo T1 (microssegundos depois):** A GPU termina o pacote seguinte no anel SDMA (um NOP de 32 bytes leva < 1 µs).
3. **Tempo T2:** Núcleo 2 (DPC) roda em paralelo, adquire o `RingLock`, chama `PollRetirement`, retira o slot com `SubmissionFenceId = 9010`, solta o `RingLock` e chama `DxgkCbSynchronizeExecution` imediatamente.
4. **Tempo T3:** O agendador do Windows (`dxgmms2`) recebe a interrupção de `DMA_COMPLETED` da fence **9010**. O VidSch registra: *“Fence 9010 completada”*.
5. **Tempo T4:** Núcleo 1 finalmente executa sua chamada de `DxgkCbSynchronizeExecution` com a fence **9009**.
6. **Tempo T5:** O VidSch recebe `DMA_COMPLETED` com `SubmissionFenceId = 9009`. Mas para o VidSch, a fence 9010 **já havia sido completada**! No WDDM, reportar uma fence decrescente (`9009 < 9010`) ou fora de ordem é considerado corrupção do agendador.
7. **Tempo T6:** O `dxgmms2` aciona instantaneamente o **BugCheck `0x119` Subtipo `0x1`** com:
   - `Arg2 = 9009` (`0x2331`)
   - `Arg3 = 9010` (`0x2332`)

---

## 5. Questão Paralela: Por que a tela continuou preta na corrida?

No log da UMD ([`bc250_umd_log_1200.txt`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-10_18-34-36-DESKTOP-HQAG6T4-lab-binding/bc250_umd_log_1200.txt)):
```text
18:34:41.453 [pid=1200 dwm.exe] DRAW_BLT_SUBSTITUTION: ARMADO ...
18:34:41.453 [pid=1200 dwm.exe] DrawIndexed: RECUSADO -- compilador/shader-state/draw PM4 ausente (Rejected=1)
```
O DWM utilizou passes de `DrawIndexed` sem Shader Resource View vinculada (`PsShaderResources[0] == NULL`). A rotina `Bc250UmdSubstituteDrawWithBlt` exige uma textura de origem vinculada para realizar a substituição por cópia SDMA 1:1. Como era um desenho puramente geométrico, o draw foi rejeitado com `E_NOTIMPL`.

---

## 6. As Perguntas para Claude, DeepSeek e Sparke

Pedimos a validação e recomendações de vocês sobre as seguintes decisões de arquitetura:

### Pergunta 1 — Marca D'água Monotônica na Notificação ao VidSch
Para impedir matematicamente que qualquer chamada out-of-order atinja o `DxgkCbNotifyInterrupt`:
Podemos manter uma marca d'água atômica monotônica `LastNotifiedSubmissionFenceId`. No callback sincronizado:
```c
if (sync->SubmissionFenceId <= context->SubmissionEngine.WddmFences.LastNotifiedFenceId) {
    /* Fence ja ultrapassada por notificacao concorrente; ignora silenciosamente */
    return TRUE;
}
context->SubmissionEngine.WddmFences.LastNotifiedFenceId = sync->SubmissionFenceId;
context->Core.DxgkInterface.DxgkCbNotifyInterrupt(..., &data);
```
Essa trava de monotonicidade basta para blindar o VidSch contra `0x119` Arg1=1?

### Pergunta 2 — Drenagem em Batch com Notificação Única da Maior Fence
Conforme vocês recomendaram no Doc 238 (§3), o WDDM interpreta `DXGK_INTERRUPT_DMA_COMPLETED` com fence $N$ como: *“tudo até e inclusive $N$ neste nó foi completado”*.
Em vez de chamar `DxgkCbSynchronizeExecution` para cada slot individual aposentado dentro do loop:
Não é muito mais seguro e performático aposentar todos os slots concluídos no loop localmente e disparar **uma única chamada** de `DxgkCbSynchronizeExecution` contendo a **maior** `SubmissionFenceId` completada naquela rodada?

### Pergunta 3 — Serialização da Retirada (Submit vs DPC)
Hoje, chamamos `Bc250ActiveSdmaRetireAndNotify` tanto dentro de `McdmSubmitCommandVirtual` quanto na DPC.
Faz sentido manter a retirada no submit, ou seria mais limpo e seguro deixar a retirada **exclusivamente a cargo da DPC e do timer**, evitando qualquer contenção com a thread de submissão do agendador?

### Pergunta 4 — Tratamento de `DrawIndexed` sem SRV na UMD
Para permitir que o DWM desenhe o fundo da área de trabalho e janelas quando ele envia `DrawIndexed` sem texturas (apenas geometria ou cor sólida):
Qual é a abordagem recomendada para a UMD de laboratório sem compilador de shaders? Um preenchimento sólido (Solid Fill) via SDMA na área do Viewport/Scissor seria aceitável e coerente com a política de laboratório declarada?

---
*Aguardamos a análise de vocês para fecharmos a implementação e partirmos para o teste de validação no silício.*
