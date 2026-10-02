#include "bc250_submission_engine.h"
#include "bc250_active_miniport.h"
#include "bc250_display_ddi.h"
#include "bc250_mmio_topology_transport.h"
#include "bc250_probe_telemetry.h"
#include "bc250_observable_shader.h"
#include "bc250_sdma_present.h"

/*
 * Fase P1 — implementacao do motor de submissao persistente.
 *
 * O bloco de VRAM segue exatamente o mapa provado no candidato 3.43, para que
 * a fila persistente veja a mesma topologia de memoria que o tripe fisico
 * validou: MQD em +0x0000, EOP em +0x1000, RPTR em +0x2000, WPTR em +0x2100,
 * fence em +0x2200, tabela de paginas do Contexto 0 em +0x3000 e o anel em
 * +0x10000, alinhado a 64 KiB.
 */

#define BC250_SUBMISSION_MQD_OFFSET     0x0000u
#define BC250_SUBMISSION_EOP_OFFSET     0x1000u
#define BC250_SUBMISSION_RPTR_OFFSET    0x2000u
#define BC250_SUBMISSION_WPTR_OFFSET    0x2100u
#define BC250_SUBMISSION_FENCE_OFFSET   0x2200u
#define BC250_SUBMISSION_PT_OFFSET      0x3000u
#define BC250_SUBMISSION_SHADER_OFFSET  0x4000u
#define BC250_SUBMISSION_RESULT_OFFSET  0x5000u

#define BC250_SUBMISSION_PT_ENTRIES     512u
#define BC250_SUBMISSION_PTE_FLAGS      0xE1ull
#define BC250_SUBMISSION_DB_OFFSET_MASK 0x0FFFFFFCu
#define BC250_SUBMISSION_DB_ENABLE      0x40000000u

static VOID Bc250SubmissionBarrier(void *Context)
{
    UNREFERENCED_PARAMETER(Context);
    KeMemoryBarrier();
}

static uint32_t Bc250SubmissionRead32(
    BC250_SUBMISSION_ENGINE *Engine,
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint32_t ByteOffset,
    uint32_t *Value)
{
    UNREFERENCED_PARAMETER(Engine);
    return Bc250MmioTopologyRead32(
        &Context->Core.MmioTransport, ByteOffset, Value);
}

static uint32_t Bc250SubmissionWrite32(
    BC250_SUBMISSION_ENGINE *Engine,
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint32_t ByteOffset,
    uint32_t Value)
{
    UNREFERENCED_PARAMETER(Engine);
    return Bc250MmioTopologyWrite32(
        &Context->Core.MmioTransport, ByteOffset, Value);
}

/*
 * Fotografia global, deliberadamente tirada sem selecionar uma HQD. Alem de
 * ampliar o diagnostico com CP_STALLED_STAT2/CP_BUSY_STAT, isto permite
 * distinguir um stall que ja existia de um que aparece durante as leituras
 * indexadas usadas pela propria telemetria da fila.
 */
VOID Bc250SubmissionEngineCaptureRciuStage(
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    const char *Tag)
{
    BC250_SUBMISSION_ENGINE *engine;
    uint32_t valid = 0u;
    uint32_t cpcStatus = 0u, cpcBusy = 0u, cpcStalled = 0u;
    uint32_t cpfStatus = 0u, cpfBusy = 0u, cpfStalled = 0u;
    uint32_t cpStalled1 = 0u, cpStalled2 = 0u, cpBusy = 0u, cpStat = 0u;
    uint32_t mec2Pntr = 0u;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        Tag == NULL) return;
    engine = &Context->SubmissionEngine;
    if (engine->MmioMapped == 0u ||
        Context->Core.MmioTransport.State != BC250_MMIO_TOPOLOGY_MAPPED) return;

    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPC_STATUS, &cpcStatus)) valid |= 1u << 0;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &cpcBusy)) valid |= 1u << 1;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, &cpcStalled)) valid |= 1u << 2;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPF_STATUS, &cpfStatus)) valid |= 1u << 3;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &cpfBusy)) valid |= 1u << 4;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &cpfStalled)) valid |= 1u << 5;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_STALLED_STAT1, &cpStalled1)) valid |= 1u << 6;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_STALLED_STAT2, &cpStalled2)) valid |= 1u << 7;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_BUSY_STAT, &cpBusy)) valid |= 1u << 8;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_STAT, &cpStat)) valid |= 1u << 9;
    if (Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR, &mec2Pntr)) valid |= 1u << 10;

    Bc250DisplayLog("[MEC_RCIU_STAGE %s] Valid=0x%X CpcStat=%08X CpcBusy=%08X CpcStalled=%08X CpfStat=%08X CpfBusy=%08X CpfStalled=%08X CpStalled1=%08X CpStalled2=%08X CpBusy=%08X CpStat=%08X Mec2Pntr=%08X RciuBusy=%u/%u/%u RciuPath=%u/%u\n",
        Tag, valid, cpcStatus, cpcBusy, cpcStalled, cpfStatus, cpfBusy,
        cpfStalled, cpStalled1, cpStalled2, cpBusy, cpStat, mec2Pntr,
        (cpcStatus & 0x00000010u) != 0u ? 1u : 0u,
        (cpcStatus & 0x00000020u) != 0u ? 1u : 0u,
        (cpcStatus & 0x00040000u) != 0u ? 1u : 0u,
        (cpBusy & 0x00000200u) != 0u ? 1u : 0u,
        (cpBusy & 0x00000400u) != 0u ? 1u : 0u);
}

/*
 * Fotografa os registradores da fila selecionada e devolve o seletor original.
 * Somente leitura: nenhuma destas chamadas altera estado da engine.
 */
static VOID Bc250SubmissionCaptureQueue(
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SUBMISSION_ENGINE *Engine,
    BC250_SUBMISSION_QUEUE_SNAPSHOT *Snapshot)
{
    if (Snapshot == NULL) return;
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    if (Engine->MmioMapped == 0u) return;

    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->SelectedSelector);

    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_ACTIVE, &Snapshot->Active);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_HQ_STATUS0, &Snapshot->HqStatus0);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_PQ_CONTROL, &Snapshot->PqControl);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_DOORBELL_CONTROL, &Snapshot->DoorbellControl);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_HQD_ERROR, &Snapshot->HqdError);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_PQ_WPTR_LO, &Snapshot->PqWptrLo);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_PQ_RPTR, &Snapshot->PqRptr);

    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->OriginalSelector);

    /* Registradores globais, fora do banco indexado. */
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_CP_PQ_STATUS, &Snapshot->CpPqStatus);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_CP_CPC_STATUS, &Snapshot->CpcStatus);
    (void)Bc250SubmissionRead32(Engine, Context,
        0x00008234u, &Snapshot->CpMecCntl);
    (void)Bc250SubmissionRead32(Engine, Context,
        0x000086D8u, &Snapshot->CpMeCntl);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_RLC_CP_SCHEDULERS, &Snapshot->RlcCpSchedulers);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL, &Snapshot->WptrPollCntl);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, &Snapshot->WptrPollCntl1);
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP, &Snapshot->MecMe2HeaderDump);
}

/*
 * Libera tudo o que o motor possui.  Chamada tanto pelo teardown normal
 * quanto pelos caminhos de falha do Online: nunca se devolve motor parcial.
 */
static VOID Bc250SubmissionEngineReleaseResources(
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SUBMISSION_ENGINE *Engine)
{
    /* Core owns this shared mapping. StopDevice still needs it to restore
     * the global MMU after the HQD has been deactivated. The readonly core
     * releases it last, including on failed startup. */
    Engine->MmioMapped = 0u;
    if (Engine->DoorbellVirtual != NULL) {
        (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle,
            (PVOID)Engine->DoorbellVirtual);
        Engine->DoorbellVirtual = NULL;
    }
    if (Engine->Mapped != NULL) {
        (void)Context->Core.DxgkInterface.DxgkCbUnmapMemory(
            Context->Core.DxgkInterface.DeviceHandle,
            (PVOID)Engine->Mapped);
        Engine->Mapped = NULL;
    }
    if (Engine->TokenValid != 0u) {
        (void)Bc250VramRelease(
            &Context->Core.StartDevice.Owned.Bootstrap.Allocator,
            Engine->Token);
        Engine->TokenValid = 0u;
    }
    Engine->ComputeRingVirtual = NULL;
    Engine->ComputeFenceVirtual = NULL;
    Engine->ComputeRptrReportVirtual = NULL;
    Engine->ComputeWptrShadowVirtual = NULL;
    Engine->DoorbellEnabled = 0u;
}

_Check_return_
NTSTATUS Bc250SubmissionEngineOnline(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_SUBMISSION_ENGINE *engine;
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_VRAM_REGION region;
    BC250_VRAM_STATUS vramStatus;
    BC250_MMIO_TOPOLOGY_STATUS mmioStatus;
    BC250_HQD_ACTIVE_STATUS hqdStatus;
    BC250_PRING_STATUS ringStatus;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *mapped = NULL;
    NTSTATUS ntStatus;
    uint64_t basePhysical;
    size_t matchCount = 0;
    uint32_t i;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    engine = &Context->SubmissionEngine;
    if (InterlockedCompareExchange(&engine->State,
            BC250_SUBMISSION_ENGINE_OFFLINE,
            BC250_SUBMISSION_ENGINE_OFFLINE) !=
        BC250_SUBMISSION_ENGINE_OFFLINE) {
        return STATUS_ALREADY_INITIALIZED;
    }

    RtlZeroMemory(engine, sizeof(*engine));
    engine->Signature = BC250_SUBMISSION_ENGINE_SIGNATURE;
    KeInitializeSpinLock(&engine->Lock);
    /*
     * O RtlZeroMemory acima tambem zera o rastreador, e um rastreador zerado
     * tem AcceptingWork == 0 — ou seja, recusaria toda reserva. O reset
     * explicito e' obrigatorio: inicializa o lock e reabre o portao.
     */
    Bc250WddmFenceTrackerReset(&engine->WddmFences);

    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->TotalBytes == 0u || addressSpace->VramMcStart == 0u) {
        return STATUS_DEVICE_NOT_READY;
    }

    for (i = 0; i < Context->Core.ResolvedBarCount; ++i) {
        if (Context->Core.ResolvedBars[i].Index ==
                BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            bar = &Context->Core.ResolvedBars[i];
            ++matchCount;
        }
    }
    if (matchCount != 1 || bar == NULL) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlZeroMemory(&region, sizeof(region));
    vramStatus = Bc250VramAllocate(allocator,
        BC250_SUBMISSION_BLOCK_BYTES, 65536u,
        BC250_VRAM_ROLE_MQD,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE,
        &engine->Token);
    if (vramStatus != BC250_VRAM_OK) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    engine->TokenValid = 1u;
    if (Bc250VramGetRegion(allocator, engine->Token, &region) != BC250_VRAM_OK) {
        Bc250SubmissionEngineReleaseResources(Context, engine);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    physical.QuadPart = (LONGLONG)(bar->Start + region.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle,
        physical, (ULONG)BC250_SUBMISSION_BLOCK_BYTES,
        FALSE, FALSE, MmNonCached, (PVOID *)&mapped);
    if (!NT_SUCCESS(ntStatus) || mapped == NULL) {
        Bc250SubmissionEngineReleaseResources(Context, engine);
        return NT_SUCCESS(ntStatus) ? STATUS_UNSUCCESSFUL : ntStatus;
    }
    engine->Mapped = mapped;
    engine->MappedBytes = BC250_SUBMISSION_BLOCK_BYTES;
    Bc250LogUcVramMapping("MEC_SUBMISSION_BLOCK", region.Offset,
        BC250_SUBMISSION_BLOCK_BYTES);

    basePhysical = addressSpace->VramMcStart + region.Offset;
    engine->BasePhysical = basePhysical;

    engine->Buffers.MqdVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_MQD_OFFSET);
    engine->Buffers.MqdGpuAddress = basePhysical + BC250_SUBMISSION_MQD_OFFSET;
    engine->Buffers.MqdSizeBytes = BC250_MQD_SIZE_BYTES;

    engine->Buffers.EopVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_EOP_OFFSET);
    engine->Buffers.EopGpuAddress = basePhysical + BC250_SUBMISSION_EOP_OFFSET;
    engine->Buffers.EopSizeBytes = 4096u;

    engine->Buffers.RptrVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_RPTR_OFFSET);
    engine->Buffers.RptrGpuAddress = basePhysical + BC250_SUBMISSION_RPTR_OFFSET;
    engine->Buffers.RptrSizeBytes = 256u;

    engine->Buffers.WptrVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_WPTR_OFFSET);
    engine->Buffers.WptrGpuAddress = basePhysical + BC250_SUBMISSION_WPTR_OFFSET;
    engine->Buffers.WptrSizeBytes = 256u;

    engine->Buffers.FenceVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_FENCE_OFFSET);
    engine->Buffers.FenceGpuAddress =
        basePhysical + BC250_SUBMISSION_FENCE_OFFSET;
    engine->Buffers.FenceSizeBytes = 256u;

    engine->Buffers.ShaderVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_SHADER_OFFSET);
    engine->Buffers.ShaderGpuAddress =
        basePhysical + BC250_SUBMISSION_SHADER_OFFSET;
    engine->Buffers.ShaderSizeBytes = 256u;

    engine->Buffers.ResultVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_RESULT_OFFSET);
    engine->Buffers.ResultGpuAddress =
        basePhysical + BC250_SUBMISSION_RESULT_OFFSET;
    engine->Buffers.ResultSizeBytes = 256u;

    engine->Buffers.PtVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_PT_OFFSET);
    engine->Buffers.PtGpuAddress = basePhysical + BC250_SUBMISSION_PT_OFFSET;
    engine->Buffers.PtSizeBytes = 4096u;

    {
        volatile uint64_t *pt = (volatile uint64_t *)engine->Buffers.PtVirtual;
        for (i = 0u; i < BC250_SUBMISSION_PT_ENTRIES; ++i) {
            uint64_t pagePhysical = basePhysical + ((uint64_t)i * 4096ull);
            pt[i] = (pagePhysical & 0x0000FFFFFFFFF000ull) |
                BC250_SUBMISSION_PTE_FLAGS;
        }
        KeMemoryBarrier();
    }

    engine->Buffers.RingVirtual =
        (volatile void *)((PUCHAR)mapped + BC250_SUBMISSION_RING_OFFSET);
    engine->Buffers.RingGpuAddress =
        basePhysical + BC250_SUBMISSION_RING_OFFSET;
    engine->Buffers.RingSizeBytes = BC250_SUBMISSION_RING_BYTES;

    engine->ComputeRingVirtual =
        (volatile uint32_t *)engine->Buffers.RingVirtual;
    engine->ComputeFenceVirtual =
        (volatile uint32_t *)engine->Buffers.FenceVirtual;
    engine->ComputeRptrReportVirtual =
        (volatile uint32_t *)engine->Buffers.RptrVirtual;
    engine->ComputeWptrShadowVirtual =
        (volatile uint32_t *)engine->Buffers.WptrVirtual;

    engine->Props.MqdGpuAddress = engine->Buffers.MqdGpuAddress;
    engine->Props.HqdBaseGpuAddress = engine->Buffers.RingGpuAddress;
    engine->Props.QueueSizeDwords = BC250_SUBMISSION_RING_DWORDS;
    engine->Props.EopGpuAddress = engine->Buffers.EopGpuAddress;
    engine->Props.EopSizeBytes = 4096u;
    engine->Props.RptrGpuAddress = engine->Buffers.RptrGpuAddress;
    engine->Props.WptrGpuAddress = engine->Buffers.WptrGpuAddress;
    engine->Props.UseDoorbell = 0u;
    engine->Props.DoorbellIndex = 0u;
    engine->Props.SchedulerMe = 2u;
    engine->Props.SchedulerPipe = 0u;
    engine->Props.SchedulerQueue = 0u;
    engine->Props.PipePriority = 0u;
    engine->Props.QueuePriority = 0u;
    engine->Props.HqdActive = 1u;

    {
        uint32_t barIdx;
        const BC250_RESOLVED_BAR_RESOURCE *doorbellBar = NULL;

        for (barIdx = 0u; barIdx < Context->Core.ResolvedBarCount; ++barIdx) {
            if (Context->Core.ResolvedBars[barIdx].Index ==
                BC250_ADAPTER_DOORBELL_BAR_INDEX) {
                doorbellBar = &Context->Core.ResolvedBars[barIdx];
                break;
            }
        }
        if (doorbellBar != NULL && doorbellBar->Length >= 4096u) {
            PHYSICAL_ADDRESS doorbellPhysical;
            volatile void *doorbellMapped = NULL;

            doorbellPhysical.QuadPart = (LONGLONG)doorbellBar->Start;
            ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
                Context->Core.DxgkInterface.DeviceHandle,
                doorbellPhysical,
                (ULONG)doorbellBar->Length,
                FALSE, FALSE, MmNonCached, (PVOID *)&doorbellMapped);
            if (NT_SUCCESS(ntStatus) && doorbellMapped != NULL) {
                engine->DoorbellVirtual = doorbellMapped;
                engine->Buffers.DoorbellVirtual = doorbellMapped;
                engine->Buffers.DoorbellSizeBytes = (uint32_t)doorbellBar->Length;
                engine->Props.UseDoorbell = 1u;
            }
        }
    }

    mmioStatus = Bc250MmioTopologyMapReadonly(
        &Context->Core.MmioTransport, &Context->Core.DxgkInterface,
        Context->Core.ResolvedBars, Context->Core.ResolvedBarCount,
        BC250_PCI_MMIO_BAR_INDEX);
    if (mmioStatus != BC250_MMIO_TOPOLOGY_OK) {
        Bc250SubmissionEngineReleaseResources(Context, engine);
        return STATUS_DEVICE_NOT_READY;
    }
    engine->MmioMapped = 1u;

    RtlZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.Read32 = Bc250MmioTopologyRead32;
    interfaceData.Write32 = Bc250MmioTopologyWrite32;
    interfaceData.Barrier = Bc250SubmissionBarrier;
    interfaceData.Context = &Context->Core.MmioTransport;

    hqdStatus = Bc250ComputeActivatePersistentHqd(
        &interfaceData, &engine->Buffers, &engine->Props, &engine->Mqd,
        &engine->ActivateEvidence, &engine->HqdState);
    engine->ActivateStatus = (uint32_t)hqdStatus;
    if (hqdStatus != BC250_HQD_ACTIVE_OK) {
        if (engine->ActivateEvidence.FirstRestoreFailureOffset != 0u) {
            engine->ActivateStatus = engine->ActivateEvidence.FirstRestoreFailureOffset;
        }
        Bc250SubmissionEngineReleaseResources(Context, engine);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    engine->SelectedSelector = engine->ActivateEvidence.SelectedSelector;
    engine->OriginalSelector = engine->ActivateEvidence.OriginalSelector;
    engine->DoorbellOffsetBytes = engine->ActivateEvidence.DoorbellOffset;
    engine->DoorbellEnabled = engine->Props.UseDoorbell;

    if (engine->Buffers.MqdVirtual != NULL) {
        RtlCopyMemory((PVOID)engine->Buffers.MqdVirtual, &engine->Mqd, sizeof(engine->Mqd));
        KeMemoryBarrier();
    }

    /*
     * Baseline da fence do anel.
     *
     * O 3.44 nao fazia isto e pagou caro: a fence em +0x2200 entrou com
     * memoria velha (0x43DAFFFE), o retire comparou contra a janela em voo,
     * viu delta absurdo e marcou o motor como FAULTED. Zerar aqui e' o unico
     * ponto legitimo para tocar nesse endereco — depois da submissao, quem
     * escreve e' exclusivamente a GPU.
     */
    if (engine->ComputeFenceVirtual != NULL) {
        engine->ComputeFenceVirtual[0] = 0u;
        engine->ComputeFenceVirtual[1] = 0u;
        KeMemoryBarrier();
        engine->RingFenceBaselineReadback = engine->ComputeFenceVirtual[0];
        engine->RingFenceBaselineWritten =
            (engine->RingFenceBaselineReadback == 0u) ? 1u : 0u;
    }

    Bc250SubmissionCaptureQueue(Context, engine, &engine->SnapshotAfterActivate);

    if (engine->RingFenceBaselineWritten == 0u) {
        /* Sem baseline confiavel nao ha como aposentar submissao nenhuma. */
        BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE teardown;
        RtlZeroMemory(&teardown, sizeof(teardown));
        teardown.SelectedSelector = engine->SelectedSelector != 0u ?
            engine->SelectedSelector : 0x00000008u;
        teardown.OriginalSelector = engine->OriginalSelector;
        teardown.ActiveConfirmed = 1u;

        (void)Bc250ComputeDeactivatePersistentHqd(
            &interfaceData, &engine->Buffers, &teardown, &engine->HqdState);
        Bc250SubmissionEngineReleaseResources(Context, engine);
        return STATUS_DEVICE_DATA_ERROR;
    }

    /*
     * No modo Host PA (Contexto 0 mapeando a VRAM fisica), CP_HQD_PQ_BASE
     * recebe o endereco fisico do anel: o anel persistente usa exatamente o
     * mesmo endereco que a fila esta lendo.
     */
    ringStatus = Bc250PersistentRingInit(&engine->ComputeRing,
        (uint32_t)BC250_ENGINE_TYPE_COMPUTE,
        BC250_SUBMISSION_NODE_COMPUTE,
        engine->Buffers.RingGpuAddress,
        BC250_SUBMISSION_RING_DWORDS,
        engine->Buffers.FenceGpuAddress,
        0ull, 0u,
        (uint32_t)BC250_PRING_COMPLETION_POLL);
    if (ringStatus != BC250_PRING_OK) {
        BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE teardown;
        RtlZeroMemory(&teardown, sizeof(teardown));
        teardown.SelectedSelector = engine->SelectedSelector != 0u ?
            engine->SelectedSelector : 0x00000008u;
        teardown.OriginalSelector = engine->OriginalSelector;
        teardown.ActiveConfirmed = 1u;

        (void)Bc250ComputeDeactivatePersistentHqd(
            &interfaceData, &engine->Buffers, &teardown, &engine->HqdState);
        Bc250SubmissionEngineReleaseResources(Context, engine);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (engine->Props.InitialWptrDwords != 0u) {
        engine->ComputeRing.WptrDwords = engine->Props.InitialWptrDwords;
        engine->ComputeRing.LastSubmittedFence = 1ull;
        engine->ComputeRing.SubmissionCount = 1u;
        engine->LastSubmittedFence = 1ull;
        engine->LastWptrPublished = engine->Props.InitialWptrDwords;
        engine->LastWptrShadowReadback = engine->Props.InitialWptrDwords;
        engine->LastWptrMmioReadback = engine->Props.InitialWptrDwords;
    }

    InterlockedExchange(&engine->State, BC250_SUBMISSION_ENGINE_ONLINE);
    return STATUS_SUCCESS;
}

/*
 * Publica o cursor.  Tres caminhos independentes, todos telemetrados
 * separadamente para que o ensaio fisico consiga dizer QUAL deles moveu a
 * engine — o 3.43 mostrou que o WPTR nao assenta por escrita direta antes de
 * ACTIVE e que o CP o busca no shadow via QUEUE_MASK.
 */
static NTSTATUS Bc250SubmissionApplyCoherencyLocked(
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SUBMISSION_ENGINE *Engine)
{
    uint32_t l2Before = 0u;
    uint32_t l2Applied;
    const uint32_t l2OrValue = BC250_HQD_GCVM_L2_INVALIDATE_MASK;
    uint32_t requestReadback = 0u;
    uint32_t poll;
    uint32_t reqValue = BC250_HQD_GCVM_INVALIDATE_REQ_VMID0;
    uint32_t ackMask = BC250_HQD_GCVM_INVALIDATE_VMID0;

    Engine->CoherHdpApplied = 0u;
    Engine->CoherL2Readback = 0u;
    Engine->CoherInvalidateAck = 0u;
    Engine->CoherInvalidatePolls = 0u;
    Engine->CoherencyPassed = 0u;

    if (Engine->ComputeRing.Vmid != 0u && Engine->ComputeRing.Vmid < 16u) {
        reqValue |= (1u << Engine->ComputeRing.Vmid);
        ackMask |= (1u << Engine->ComputeRing.Vmid);
    }

    /* PREFLIGHT/SNAPSHOT: nunca componha uma escrita mascarada sem a leitura. */
    if (!Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_GCVM_L2_CNTL2, &l2Before)) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    /*
     * HDP Write Flush & Read Invalidate:
     * Garante que todas as escritas da CPU no anel persistente em VRAM
     * sejam completamente drenadas pela FIFO PCIe do HDP antes que o MEC/HQD
     * tente ler o pacote de comandos no anel.
     * Como o upstream (amdgpu_hdp_generic_flush + hdp_v5_0_invalidate_hdp):
     *   0. barreira de CPU (mb()) para drenar write-combining da BAR;
     *   1. Escrita 0u no flush de ESCRITA real (0x0385C) e leitura de
     *      confirmacao em RCC_CONFIG_MEMSIZE (0x0378C);
     *   2. Escrita 1u em HDP_READ_CACHE_INVALIDATE (0x03FC4) e readback
     * HDP.1 (28/09, doc 519): o passo 1 escrevia 0 em 0x04A80, que e'
     * SDMA0_UTCL1_INV0 e nao flush de HDP -- a drenagem nunca acontecia.
     * HDP_MISC (0x03FCC) NAO e tocado durante submissao ativa para evitar
     * bloqueio da interface RCIU no MEC2 (CP_CPC_STALLED_STAT1 = 0x00040000).
     */
    uint32_t hdpDummy = 0u;

    _mm_mfence();
    if (!Bc250SubmissionWrite32(Engine, Context,
            BC250_HQD_MMIO_HDP_FLUSH, 0u) ||
        !Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_HDP_FLUSH_POSTING_READ, &hdpDummy) ||
        !Bc250SubmissionWrite32(Engine, Context,
            BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE, 1u) ||
        !Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE, &hdpDummy)) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    Engine->CoherHdpApplied = 1u;

    l2Applied =
        (l2Before & ~BC250_HQD_GCVM_L2_INVALIDATE_MASK) |
        (l2OrValue & BC250_HQD_GCVM_L2_INVALIDATE_MASK);
    if (!Bc250SubmissionWrite32(Engine, Context,
            BC250_HQD_MMIO_GCVM_L2_CNTL2, l2Applied) ||
        !Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_GCVM_L2_CNTL2, &Engine->CoherL2Readback)) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    if ((Engine->CoherL2Readback &
            ~BC250_HQD_GCVM_L2_INVALIDATE_MASK) !=
            (l2Before & ~BC250_HQD_GCVM_L2_INVALIDATE_MASK) ||
        (Engine->CoherL2Readback &
            BC250_HQD_GCVM_L2_INVALIDATE_MASK) != 0u) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    if (!Bc250SubmissionWrite32(Engine, Context,
            BC250_HQD_MMIO_GCVM_INVALIDATE_REQ,
            reqValue)) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    /* GC10.1: o dummy-read do REQ deve preceder o poll do ACK. */
    if (!Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_GCVM_INVALIDATE_REQ, &requestReadback) ||
        requestReadback != reqValue) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    for (poll = 0u; poll < 1000u; ++poll) {
        if (!Bc250SubmissionRead32(Engine, Context,
                BC250_HQD_MMIO_GCVM_INVALIDATE_ACK,
                &Engine->CoherInvalidateAck)) {
            return STATUS_DEVICE_HARDWARE_ERROR;
        }
        if ((Engine->CoherInvalidateAck & ackMask) == ackMask) {
            break;
        }
    }
    Engine->CoherInvalidatePolls = poll + (poll < 1000u ? 1u : 0u);
    if ((Engine->CoherInvalidateAck & ackMask) != ackMask) {
        return STATUS_IO_TIMEOUT;
    }

    Engine->CoherencyPassed = 1u;
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250SubmissionApplyCoherency(
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SUBMISSION_ENGINE *Engine)
{
    NTSTATUS status;
    KIRQL irql;
    /* Lock order: engine Lock -> adapter TlbLock. VMID8/9 invalidations
     * share engine17 REQ/ACK with this VMID0 operation. All exits from the
     * transaction must release the same lock, including readback failure. */
    KeAcquireSpinLock(&Context->TlbLock, &irql);
    status = Bc250SubmissionApplyCoherencyLocked(Context, Engine);
    KeReleaseSpinLock(&Context->TlbLock, irql);
    return status;
}

static NTSTATUS Bc250SubmissionPublishWptr(
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SUBMISSION_ENGINE *Engine,
    uint32_t NewWptrDwords)
{
    NTSTATUS status;
    uint32_t readback = 0u;
    uint32_t doorbellControl = 0u;

    Engine->PublishFailStep = 0u;
    Engine->PublishHqdActive = 0u;
    Engine->PublishHqdRptr = 0u;
    Engine->PublishHqdError = 0u;
    Engine->PublishReadbackDoorbellControl = 0u;
    Engine->PublishTargetWptr = NewWptrDwords;

    Bc250DisplayLog("[SubmissionPublishWptr] ENTER: NewWptr=%u EngineState=%d RingWptr=%u ShadowVirt=%p MmioWPTR=%u\n",
        NewWptrDwords, (int)Engine->State, Engine->ComputeRing.WptrDwords,
        Engine->ComputeWptrShadowVirtual, Engine->LastWptrMmioReadback);

    /* 1. Shadow em memoria: a fonte que o CP le por polling. */
    if (Engine->ComputeWptrShadowVirtual == NULL) {
        Bc250DisplayLog("[SubmissionPublishWptr] RECUSA: ShadowVirtual nulo\n");
        return STATUS_DEVICE_NOT_READY;
    }
    Engine->ComputeWptrShadowVirtual[0] = NewWptrDwords;
    Engine->ComputeWptrShadowVirtual[1] = 0u;
    KeMemoryBarrier();
    Engine->LastWptrShadowReadback = Engine->ComputeWptrShadowVirtual[0];
    if (Engine->LastWptrShadowReadback != NewWptrDwords) {
        Bc250DisplayLog("[SubmissionPublishWptr] RECUSA: Shadow readback mismatch (exp=%u obs=%u)\n",
            NewWptrDwords, Engine->LastWptrShadowReadback);
        return STATUS_DEVICE_DATA_ERROR;
    }

    /* Atualiza a estrutura MQD fisica em VRAM com o novo cursor */
    Engine->Mqd.CpHqdPqWptrLo = NewWptrDwords;
    Engine->Mqd.CpHqdPqWptrHi = 0u;
    Engine->Mqd.CpHqdPersistentState = (0x53u << 8) | 1u;
    if (Engine->Buffers.MqdVirtual != NULL) {
        RtlCopyMemory((PVOID)Engine->Buffers.MqdVirtual, &Engine->Mqd, sizeof(Engine->Mqd));
        KeMemoryBarrier();
    }

    /*
     * 2. Torna anel, IB, MQD e shadow visiveis ao MEC. Uma barreira de CPU nao
     * invalida o HDP read cache nem a GL2 da GPU. Se qualquer passo falhar,
     * nao toque doorbell/WPTR: a submissao falha fechada.
     */
    Bc250SubmissionEngineCaptureRciuStage(Context, "PUBLISH_BEFORE_COHERENCY");
    status = Bc250SubmissionApplyCoherency(Context, Engine);
    if (!NT_SUCCESS(status)) {
        Engine->PublishFailStep = 1u;
        Bc250DisplayLog("[SubmissionPublishWptr] RECUSA: ApplyCoherency falhou (status=0x%08X)\n",
            (uint32_t)status);
        return status;
    }
    Bc250SubmissionEngineCaptureRciuStage(Context, "PUBLISH_AFTER_COHERENCY");

    /*
     * 3. Publicacao direta no registrador CP_HQD_PQ_WPTR_LO com a fila ATIVA.
     *
     * O motor permanece continuamente ATIVO. A escrita em CP_HQD_PQ_WPTR_LO
     * avança o cursor no silício exatamente como comprovado na Fase P1.
     */
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->SelectedSelector);

    if (Bc250SubmissionWrite32(Engine, Context,
            BC250_HQD_MMIO_PQ_WPTR_LO, NewWptrDwords) != 0u &&
        Bc250SubmissionWrite32(Engine, Context,
            BC250_HQD_MMIO_PQ_WPTR_HI, 0u) != 0u) {
        ++Engine->DirectWptrWrites;
    }
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_HQD_MMIO_PQ_WPTR_LO, &readback);
    Engine->LastWptrMmioReadback = readback;

    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->OriginalSelector);

    Bc250DisplayLog("[SubmissionPublishWptr] Step 3 MMIO WPTR: Write=%u Readback=%u (DirWrites=%u)\n",
        NewWptrDwords, readback, Engine->DirectWptrWrites);
    Bc250SubmissionEngineCaptureRciuStage(Context, "PUBLISH_AFTER_MMIO_WPTR");

    /*
     * 4. Doorbell canonica da HQD na abertura BAR2.
     *
     * O firmware desta BC-250 restringe o MEC a 0x110..0x110 e o HQD
     * confirma DOORBELL_CONTROL=0x40000110.  Uma doorbell GFX10 e' uma unica
     * escrita de 64 bits.  Nao emita primeiro um store de 32 bits: isso cria
     * duas transacoes PCIe para o mesmo slot e pode consumir a borda na
     * primeira metade, antes do WPTR completo existir no hub.
     *
     * Nao ha fallback por WPTR MMIO: se a doorbell atribuida nao estiver
     * mapeada, alinhada e habilitada, a submissao falha fechada.
     */
    Bc250DisplayLog("[SubmissionPublishWptr] Step 4 Precheck: Enabled=%u Virt=%p Offset=0x%X (align8=%u) Size=0x%X (Req=0x%X)\n",
        Engine->DoorbellEnabled, Engine->DoorbellVirtual,
        Engine->DoorbellOffsetBytes, (uint32_t)(Engine->DoorbellOffsetBytes & (sizeof(uint64_t) - 1u)),
        Engine->Buffers.DoorbellSizeBytes,
        (uint32_t)(Engine->DoorbellOffsetBytes + sizeof(uint64_t)));

    if (Engine->DoorbellEnabled == 0u || Engine->DoorbellVirtual == NULL ||
        (Engine->DoorbellOffsetBytes & (sizeof(uint64_t) - 1u)) != 0u ||
        Engine->Buffers.DoorbellSizeBytes <
            Engine->DoorbellOffsetBytes + sizeof(uint64_t)) {
        ++Engine->DoorbellWriteFailures;
        Engine->PublishFailStep = 2u;
        Bc250DisplayLog("[SubmissionPublishWptr] RECUSA Check A: Precheck falhou (Enabled=%u Virt=%p Offset=0x%X Size=0x%X Failures=%u)\n",
            Engine->DoorbellEnabled, Engine->DoorbellVirtual,
            Engine->DoorbellOffsetBytes, Engine->Buffers.DoorbellSizeBytes,
            Engine->DoorbellWriteFailures);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->SelectedSelector);
    uint32_t selReadbackQueueBank = 0u;
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &selReadbackQueueBank);

    {
        uint32_t activeVal = 0u;
        uint32_t rptrVal = 0u;
        uint32_t hqdErrorVal = 0u;

        (void)Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_ACTIVE, &activeVal);
        (void)Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_PQ_RPTR, &rptrVal);
        (void)Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_HQD_ERROR, &hqdErrorVal);

        Engine->PublishHqdActive = activeVal;
        Engine->PublishHqdRptr = rptrVal;
        Engine->PublishHqdError = hqdErrorVal;

        if (Bc250SubmissionRead32(Engine, Context,
                BC250_HQD_MMIO_DOORBELL_CONTROL, &doorbellControl) == 0u) {
            (void)Bc250SubmissionWrite32(Engine, Context,
                BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
            (void)Bc250SubmissionWrite32(Engine, Context,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->OriginalSelector);
            ++Engine->DoorbellWriteFailures;
            Engine->PublishFailStep = 3u;
            Bc250DisplayLog("[SubmissionPublishWptr] RECUSA: Read32(DOORBELL_CONTROL) falhou (Active=%u Rptr=%u Error=0x%08X)\n",
                activeVal, rptrVal, hqdErrorVal);
            return STATUS_DEVICE_HARDWARE_ERROR;
        }

        Engine->PublishReadbackDoorbellControl = doorbellControl;

        uint32_t hqdVmidVal = 0u;
        (void)Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_VMID, &hqdVmidVal);
        uint32_t expectedHqdVmid = ((uint32_t)Engine->ComputeRing.Vmid << 8);
        if (hqdVmidVal != expectedHqdVmid) {
            (void)Bc250SubmissionWrite32(Engine, Context,
                BC250_HQD_MMIO_VMID, expectedHqdVmid);
            (void)Bc250SubmissionRead32(Engine, Context,
                BC250_HQD_MMIO_VMID, &hqdVmidVal);
        }

        Bc250DisplayLog("[SubmissionPublishWptr] Step 4 QueueBank: Active=%u Rptr=%u DbCtrl=0x%08X Error=0x%08X HqdVmid=0x%08X (Selector=0x%08X Readback=0x%08X)\n",
            activeVal, rptrVal, doorbellControl, hqdErrorVal, hqdVmidVal, Engine->SelectedSelector, selReadbackQueueBank);
    }

    Engine->DoorbellControlBefore = doorbellControl;
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->OriginalSelector);

    Bc250DisplayLog("[SubmissionPublishWptr] Step 4 Check B: DbCtrl=0x%08X (EnableBit=%u Offset=0x%X ExpOffset=0x%X)\n",
        doorbellControl,
        (doorbellControl & BC250_SUBMISSION_DB_ENABLE) != 0u ? 1u : 0u,
        doorbellControl & BC250_SUBMISSION_DB_OFFSET_MASK,
        Engine->DoorbellOffsetBytes);

    {
        BOOLEAN dbMatched = FALSE;
        if ((doorbellControl & BC250_SUBMISSION_DB_ENABLE) != 0u) {
            if ((doorbellControl & BC250_SUBMISSION_DB_OFFSET_MASK) ==
                Engine->DoorbellOffsetBytes) {
                dbMatched = TRUE;
            }
        } else if (Engine->ActivateEvidence.DoorbellControlAfterKick ==
                   (BC250_SUBMISSION_DB_ENABLE | Engine->DoorbellOffsetBytes) ||
                   Engine->Mqd.CpHqdPqDoorbellControl ==
                   (BC250_SUBMISSION_DB_ENABLE | Engine->DoorbellOffsetBytes)) {
            /*
             * Leitura dinamica de DOORBELL_CONTROL via MMIO retornou 0 (comportamento
             * do GC 10.1 com fila ativa em runtime). A evidencia fisica
             * de ativacao (Fases P1/P2) ja comprovou que a HQD foi inicializada com
             * DOORBELL_CONTROL = 0x40000110. O precheck Check A ja validou que a
             * abertura BAR2 esta mapeada, alinhada e nos limites.
             */
            dbMatched = TRUE;
            Bc250DisplayLog("[SubmissionPublishWptr] Step 4 DbCtrl dinamico=0x0; validado via evidencia de ativacao (0x%08X)\n",
                Engine->ActivateEvidence.DoorbellControlAfterKick);
        }

        if (!dbMatched) {
            ++Engine->DoorbellWriteFailures;
            Engine->PublishFailStep = 4u;
            Bc250DisplayLog("[SubmissionPublishWptr] RECUSA Check B: DbCtrl mismatch (DbCtrl=0x%08X ExpOffset=0x%X En=%u Failures=%u)\n",
                doorbellControl, Engine->DoorbellOffsetBytes,
                (doorbellControl & BC250_SUBMISSION_DB_ENABLE) != 0u ? 1u : 0u,
                Engine->DoorbellWriteFailures);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    Engine->DoorbellLastValueLow = NewWptrDwords;
    Engine->DoorbellLastValueHigh = 0u;
    KeMemoryBarrier();
    *(volatile uint64_t *)((uintptr_t)Engine->DoorbellVirtual +
        Engine->DoorbellOffsetBytes) = (uint64_t)NewWptrDwords;
    KeMemoryBarrier();
    ++Engine->DoorbellKicks;

    Bc250DisplayLog("[SubmissionPublishWptr] Step 4 DbKick: Virt=%p Offset=0x%X Val64=%u Kicks=%u\n",
        Engine->DoorbellVirtual, Engine->DoorbellOffsetBytes,
        NewWptrDwords, Engine->DoorbellKicks);

    /* Readback imediato do HIT; e' observabilidade, nao substitui o tripe. */
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->SelectedSelector);
    uint32_t selReadbackDbHit = 0u;
    (void)Bc250SubmissionRead32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &selReadbackDbHit);
    if (Bc250SubmissionRead32(Engine, Context,
            BC250_HQD_MMIO_DOORBELL_CONTROL, &doorbellControl) != 0u) {
        Engine->DoorbellControlAfter = doorbellControl;
        if ((doorbellControl & BC250_HQD_DOORBELL_HIT_MASK) != 0u) {
            Engine->DoorbellHitObserved = 1u;
        }
    }
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(Engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->OriginalSelector);

    Bc250DisplayLog("[SubmissionPublishWptr] Step 4 DbHit: After=0x%08X HitObserved=%u Kicks=%u (Selector=0x%08X Readback=0x%08X)\n",
        Engine->DoorbellControlAfter, Engine->DoorbellHitObserved,
        Engine->DoorbellKicks, Engine->SelectedSelector, selReadbackDbHit);
    Bc250SubmissionEngineCaptureRciuStage(Context, "PUBLISH_AFTER_DOORBELL");

    Engine->LastWptrPublished = NewWptrDwords;
    Engine->PublishFailStep = 0u;
    Bc250DisplayLog("[SubmissionPublishWptr] SUCESSO: PublishedWptr=%u\n", NewWptrDwords);
    return STATUS_SUCCESS;
}

/*
 * A corrida 3.115 mediu o primeiro submit Node 1 com dominio logico em 204,
 * mas a HQD fisica drenada em RPTR=0/WPTR=0. Publicar diretamente 216 mistura
 * duas epocas da fila. Reconciliamos somente quando a propria contabilidade
 * prova zero fence em voo e as duas leituras fisicas coincidem.
 *
 * Esta funcao roda sob Engine->Lock. Ela NAO escreve CP_HQD_PQ_RPTR.
 */
static NTSTATUS Bc250SubmissionReconcileCursorLocked(
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SUBMISSION_ENGINE *Engine)
{
    uint32_t physicalRptr = 0u;
    uint32_t physicalWptr = 0u;
    uint32_t logicalBefore = 0u;
    uint32_t reconciled = 0u;
    uint32_t selected = 0u;
    uint32_t readsValid = 0u;
    uint32_t restored = 0u;
    BC250_PRING_STATUS ringStatus;

    if (Engine->ComputeRing.LastSubmittedFence !=
        Engine->ComputeRing.LastRetiredFence) {
        return STATUS_SUCCESS;
    }

    ++Engine->CursorReconcileAttempts;
    Engine->CursorReconcilePassed = 0u;
    Engine->CursorLogicalBefore = Engine->ComputeRing.WptrDwords;

    if (Engine->MmioMapped == 0u) {
        ++Engine->CursorReconcileRejected;
        Engine->CursorReconcileStatus = (uint32_t)BC250_PRING_BAD_ARGUMENT;
        return STATUS_DEVICE_NOT_READY;
    }
    if (Bc250SubmissionWrite32(Engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET,
            BC250_GRBM_GFX_INDEX_BROADCAST) &&
        Bc250SubmissionWrite32(Engine, Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->SelectedSelector)) {
        selected = 1u;
        if (Bc250SubmissionRead32(Engine, Context,
                BC250_HQD_MMIO_PQ_RPTR, &physicalRptr) &&
            Bc250SubmissionRead32(Engine, Context,
                BC250_HQD_MMIO_PQ_WPTR_LO, &physicalWptr)) {
            readsValid = 1u;
        }
    }

    if (selected != 0u &&
        Bc250SubmissionWrite32(Engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET,
            BC250_GRBM_GFX_INDEX_BROADCAST) &&
        Bc250SubmissionWrite32(Engine, Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Engine->OriginalSelector)) {
        restored = 1u;
    }

    Engine->CursorLogicalBefore = Engine->ComputeRing.WptrDwords;
    Engine->CursorPhysicalRptrRaw = physicalRptr;
    Engine->CursorPhysicalWptrRaw = physicalWptr;

    if (readsValid == 0u || restored == 0u ||
        Engine->ComputeWptrShadowVirtual == NULL) {
        ++Engine->CursorReconcileRejected;
        Engine->CursorReconcileStatus = (uint32_t)BC250_PRING_BAD_ARGUMENT;
        Bc250DisplayLog("[MEC_CURSOR_RECONCILE] RECUSA: Logical=%u PhysicalRptr=%u PhysicalWptr=%u Submitted=%llu Retired=%llu Status=%u Restored=%u (RPTR nunca escrito)\n",
            Engine->ComputeRing.WptrDwords, physicalRptr, physicalWptr,
            (unsigned long long)Engine->ComputeRing.LastSubmittedFence,
            (unsigned long long)Engine->ComputeRing.LastRetiredFence,
            (uint32_t)BC250_PRING_BAD_ARGUMENT, restored);
        return STATUS_DEVICE_NOT_READY;
    }

    ringStatus = Bc250PersistentRingReconcileDrainedCursor(
        &Engine->ComputeRing, physicalRptr, physicalWptr,
        &logicalBefore, &reconciled);
    Engine->CursorLogicalBefore = logicalBefore;
    Engine->CursorReconciled = reconciled;
    Engine->CursorReconcileStatus = (uint32_t)ringStatus;
    if (ringStatus != BC250_PRING_OK) {
        ++Engine->CursorReconcileRejected;
        Bc250DisplayLog("[MEC_CURSOR_RECONCILE] RECUSA: Logical=%u PhysicalRptr=%u PhysicalWptr=%u Submitted=%llu Retired=%llu Status=%u Restored=%u (RPTR nunca escrito)\n",
            logicalBefore, physicalRptr, physicalWptr,
            (unsigned long long)Engine->ComputeRing.LastSubmittedFence,
            (unsigned long long)Engine->ComputeRing.LastRetiredFence,
            (uint32_t)ringStatus, restored);
        return ringStatus == BC250_PRING_CURSOR_BUSY ?
            STATUS_GRAPHICS_ALLOCATION_BUSY : STATUS_DEVICE_NOT_READY;
    }
    Engine->ComputeWptrShadowVirtual[0] = reconciled;
    Engine->ComputeWptrShadowVirtual[1] = 0u;
    KeMemoryBarrier();
    Engine->Mqd.CpHqdPqWptrLo = reconciled;
    Engine->LastWptrPublished = reconciled;
    Engine->LastWptrShadowReadback = Engine->ComputeWptrShadowVirtual[0];
    Engine->LastWptrMmioReadback = physicalWptr;
    Engine->LastRptrMmio = physicalRptr;
    if (Engine->LastWptrShadowReadback != reconciled) {
        ++Engine->CursorReconcileRejected;
        Engine->CursorReconcileStatus = (uint32_t)BC250_PRING_BAD_ARGUMENT;
        return STATUS_DEVICE_DATA_ERROR;
    }

    Engine->CursorReconcilePassed = 1u;
    Bc250DisplayLog("[MEC_CURSOR_RECONCILE] PASS: Logical=%u PhysicalRptr=%u PhysicalWptr=%u Rebased=%u Submitted=%llu Retired=%llu (drained, RPTR nunca escrito)\n",
        logicalBefore, physicalRptr, physicalWptr, reconciled,
        (unsigned long long)Engine->ComputeRing.LastSubmittedFence,
        (unsigned long long)Engine->ComputeRing.LastRetiredFence);
    return STATUS_SUCCESS;
}

_Check_return_
NTSTATUS Bc250SubmissionEngineRetargetDrainedIbVmid(
    struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint32_t TargetVmid)
{
    BC250_SUBMISSION_ENGINE *engine;
    BC250_PRING_STATUS ringStatus;
    KIRQL irql;
    NTSTATUS status = STATUS_SUCCESS;
    uint32_t selected = 0u, restored = 0u;
    uint32_t active = 0u, rptr = 0u, wptr = 0u, vmid = 0u;
    uint32_t queueMask = 0u, queueMaskReadback = 0u;
    uint32_t logicalBefore = 0u, reconciled = 0u;
    uint32_t targetValue, poll;
    uint32_t deactivated = 0u;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        TargetVmid >= 16u) return STATUS_INVALID_PARAMETER;
    engine = &Context->SubmissionEngine;
    if (InterlockedCompareExchange(&engine->State,
            BC250_SUBMISSION_ENGINE_ONLINE,
            BC250_SUBMISSION_ENGINE_ONLINE) !=
        BC250_SUBMISSION_ENGINE_ONLINE) return STATUS_DEVICE_NOT_READY;

    Bc250SubmissionEngineCaptureRciuStage(Context, "RETARGET_ENTRY");
    KeAcquireSpinLock(&engine->Lock, &irql);
    ++engine->VmidRetargetAttempts;
    targetValue = TargetVmid << 8;

    if (engine->ComputeRing.LastSubmittedFence !=
            engine->ComputeRing.LastRetiredFence ||
        engine->ActivateEvidence.QueueMaskBit >= 32u) {
        status = STATUS_GRAPHICS_ALLOCATION_BUSY;
        goto RetargetDone;
    }
    if (Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET,
            BC250_GRBM_GFX_INDEX_BROADCAST) &&
        Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->SelectedSelector)) {
        selected = 1u;
    }
    if (selected == 0u ||
        !Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_ACTIVE, &active) ||
        !Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_PQ_RPTR, &rptr) ||
        !Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_PQ_WPTR_LO, &wptr) ||
        !Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_VMID, &vmid)) {
        status = STATUS_DEVICE_HARDWARE_ERROR;
        goto RetargetDone;
    }

    engine->VmidRetargetBefore = vmid;
    engine->VmidRetargetActiveBefore = active;
    engine->VmidRetargetRptr = rptr;
    engine->VmidRetargetWptr = wptr;

    ringStatus = Bc250PersistentRingReconcileDrainedCursor(
        &engine->ComputeRing, rptr, wptr, &logicalBefore, &reconciled);
    if (ringStatus != BC250_PRING_OK) {
        status = ringStatus == BC250_PRING_CURSOR_BUSY ?
            STATUS_GRAPHICS_ALLOCATION_BUSY : STATUS_DEVICE_NOT_READY;
        goto RetargetDone;
    }

    /* Reescrever o mesmo VMID seria perturbacao sem informacao. */
    if (vmid == targetValue) {
        engine->ComputeRing.Vmid = TargetVmid;
        engine->Mqd.CpHqdVmid = targetValue;
        engine->VmidRetargetAfter = vmid;
        engine->VmidRetargetActiveAfter = active;
        ++engine->VmidRetargetPassed;
        goto RetargetDone;
    }

    /* O load upstream programa VMID antes de ACTIVE. Retarget vivo e' proibido. */
    if ((active & 1u) != 0u) {
        /*
         * Upstream kgd_gfx_v10_hqd_destroy: requisita dequeue para drenar o
         * pipeline MEC antes de zerar ACTIVE, prevenindo MEC2_WAIT_ON_RCIU_READ.
         */
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_HQD_MMIO_DEQUEUE_REQUEST, 1u);
        for (poll = 0u; poll < BC250_SUBMISSION_ACTIVE_POLLS; ++poll) {
            if (!Bc250SubmissionRead32(engine, Context,
                    BC250_HQD_MMIO_ACTIVE, &active)) {
                status = STATUS_DEVICE_HARDWARE_ERROR;
                goto RetargetDone;
            }
            if ((active & 1u) == 0u) break;
        }
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_HQD_MMIO_DEQUEUE_REQUEST, 0u);
        if (!Bc250SubmissionWrite32(engine, Context,
                BC250_HQD_MMIO_ACTIVE, 0u)) {
            status = STATUS_DEVICE_HARDWARE_ERROR;
            goto RetargetDone;
        }
        for (poll = 0u; poll < BC250_SUBMISSION_ACTIVE_POLLS; ++poll) {
            if (!Bc250SubmissionRead32(engine, Context,
                    BC250_HQD_MMIO_ACTIVE, &active)) {
                status = STATUS_DEVICE_HARDWARE_ERROR;
                goto RetargetDone;
            }
            if ((active & 1u) == 0u) break;
        }
        if ((active & 1u) != 0u) {
            status = STATUS_IO_TIMEOUT;
            goto RetargetDone;
        }
        deactivated = 1u;
    }

    /* O shadow antigo nao pode ser reamostrado ao reativar a fila. */
    if (engine->ComputeWptrShadowVirtual == NULL) {
        status = STATUS_DEVICE_NOT_READY;
        goto RetargetDone;
    }
    engine->ComputeWptrShadowVirtual[0] = reconciled;
    engine->ComputeWptrShadowVirtual[1] = 0u;
    engine->Mqd.CpHqdPqWptrLo = reconciled;
    engine->Mqd.CpHqdPqWptrHi = 0u;
    engine->Mqd.CpHqdVmid = targetValue;
    if (engine->Buffers.MqdVirtual != NULL) {
        RtlCopyMemory((PVOID)engine->Buffers.MqdVirtual, &engine->Mqd, sizeof(engine->Mqd));
        KeMemoryBarrier();
    }
    KeMemoryBarrier();

    if (!Bc250SubmissionWrite32(engine, Context,
            BC250_HQD_MMIO_PQ_WPTR_LO, reconciled) ||
        !Bc250SubmissionWrite32(engine, Context,
            BC250_HQD_MMIO_PQ_WPTR_HI, 0u) ||
        !Bc250SubmissionWrite32(engine, Context,
            BC250_HQD_MMIO_VMID, targetValue) ||
        !Bc250SubmissionRead32(engine, Context,
            BC250_HQD_MMIO_VMID, &vmid) ||
        vmid != targetValue) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto RetargetDone;
    }

    queueMask = 1u << engine->ActivateEvidence.QueueMaskBit;
    engine->VmidRetargetQueueMask = queueMask;
    if (!Bc250SubmissionWrite32(engine, Context,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, queueMask) ||
        !Bc250SubmissionRead32(engine, Context,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, &queueMaskReadback) ||
        (queueMaskReadback & queueMask) != queueMask ||
        !Bc250SubmissionWrite32(engine, Context,
            BC250_HQD_MMIO_ACTIVE, 1u) ||
        !Bc250SubmissionRead32(engine, Context,
            BC250_HQD_MMIO_ACTIVE, &active) ||
        (active & 1u) == 0u) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto RetargetDone;
    }

    /* Upstream gfx_v10_0_hqd_load: rearmar DOORBELL_ENABLE no CP_PQ_STATUS apos reativacao */
    if (engine->Props.UseDoorbell != 0u) {
        uint32_t pqStatus = 0u;
        if (Bc250SubmissionRead32(engine, Context,
                BC250_HQD_MMIO_CP_PQ_STATUS, &pqStatus)) {
            (void)Bc250SubmissionWrite32(engine, Context,
                BC250_HQD_MMIO_CP_PQ_STATUS, pqStatus | 0x00000002u);
        }
    }

    /* Invalida cache de leitura HDP para que o MEC leia o MQD atualizado em VRAM */
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE, 1u);

    engine->ComputeRing.Vmid = TargetVmid;
    engine->Mqd.CpHqdVmid = targetValue;
    engine->VmidRetargetAfter = vmid;
    engine->VmidRetargetActiveAfter = active;
    ++engine->VmidRetargetPassed;

RetargetDone:
    if (selected != 0u &&
        Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET,
            BC250_GRBM_GFX_INDEX_BROADCAST) &&
        Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->OriginalSelector)) {
        restored = 1u;
    }
    if (selected != 0u && restored == 0u && NT_SUCCESS(status)) {
        status = STATUS_DEVICE_HARDWARE_ERROR;
    }
    if (!NT_SUCCESS(status)) {
        ++engine->VmidRetargetRejected;
        /* Uma falha depois de desligar a HQD deixa o backend indisponivel. */
        if (deactivated != 0u &&
            engine->VmidRetargetActiveAfter == 0u) {
            InterlockedExchange(&engine->State, BC250_SUBMISSION_ENGINE_FAULTED);
        }
    }
    KeReleaseSpinLock(&engine->Lock, irql);
    Bc250DisplayLog("[MEC_VMID_RETARGET] Status=0x%08X Before=%08X Target=%08X After=%08X Active=%u->%u RPTR=%u WPTR=%u Rebased=%u QueueMask=%08X Selected=%u Restored=%u Sub=%llu Ret=%llu (RPTR nunca escrito)\n",
        (uint32_t)status, engine->VmidRetargetBefore, targetValue,
        engine->VmidRetargetAfter, engine->VmidRetargetActiveBefore,
        engine->VmidRetargetActiveAfter, rptr, wptr, reconciled, queueMask,
        selected, restored,
        (unsigned long long)engine->ComputeRing.LastSubmittedFence,
        (unsigned long long)engine->ComputeRing.LastRetiredFence);
    Bc250SubmissionEngineCaptureRciuStage(Context, "RETARGET_EXIT");
    return status;
}

_Check_return_
NTSTATUS Bc250SubmissionEngineSubmit(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t NodeOrdinal,
    _In_ uint64_t IbGpuVirtualAddress,
    _In_ uint32_t IbSizeDwords,
    _Out_ uint64_t *AssignedFence)
{
    BC250_SUBMISSION_ENGINE *engine;
    BC250_PRING_SUBMISSION_PLAN plan;
    BC250_PRING_STATUS ringStatus;
    NTSTATUS status = STATUS_SUCCESS;
    KIRQL irql;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        AssignedFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *AssignedFence = 0ull;

    engine = &Context->SubmissionEngine;
    if (InterlockedCompareExchange(&engine->State,
            BC250_SUBMISSION_ENGINE_ONLINE,
            BC250_SUBMISSION_ENGINE_ONLINE) !=
        BC250_SUBMISSION_ENGINE_ONLINE) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeAcquireSpinLock(&engine->Lock, &irql);
    ++engine->SubmitCalls;

    /*
     * O no SDMA ainda nao tem anel persistente.  Recusar e' a resposta
     * honesta: devolver STATUS_SUCCESS sem backend e' exatamente o que a
     * Lei 3 do CLAUDE.md proibe.
     */
    if (NodeOrdinal != BC250_SUBMISSION_NODE_COMPUTE) {
        ++engine->SubmitRejectedNode;
        engine->LastSubmitStatus = (uint32_t)BC250_PRING_INVALID_ENGINE;
        KeReleaseSpinLock(&engine->Lock, irql);
        return STATUS_NOT_SUPPORTED;
    }

    status = Bc250SubmissionReconcileCursorLocked(Context, engine);
    if (!NT_SUCCESS(status)) {
        ++engine->SubmitRejectedPlan;
        engine->LastSubmitStatus = (uint32_t)engine->CursorReconcileStatus;
        KeReleaseSpinLock(&engine->Lock, irql);
        return status;
    }

    ringStatus = Bc250PersistentRingPlanSubmission(&engine->ComputeRing,
        IbGpuVirtualAddress, IbSizeDwords,
        engine->ComputeRing.LastSubmittedFence + 1ull, &plan);
    engine->LastSubmitStatus = (uint32_t)ringStatus;
    if (ringStatus != BC250_PRING_OK) {
        ++engine->SubmitRejectedPlan;
        if (ringStatus == BC250_PRING_RING_FULL) {
            ++engine->RingFullCount;
            status = STATUS_GRAPHICS_ALLOCATION_BUSY;
        } else if (ringStatus == BC250_PRING_FENCE_REGRESSION) {
            ++engine->FenceRegressionCount;
            status = STATUS_INVALID_DEVICE_STATE;
        } else {
            status = STATUS_INVALID_PARAMETER;
        }
        KeReleaseSpinLock(&engine->Lock, irql);
        return status;
    }

    ringStatus = Bc250PersistentRingEmitSubmission(&engine->ComputeRing, &plan,
        (uint32_t *)(ULONG_PTR)engine->ComputeRingVirtual,
        BC250_SUBMISSION_RING_DWORDS);
    if (ringStatus != BC250_PRING_OK) {
        ++engine->SubmitRejectedEmit;
        engine->LastSubmitStatus = (uint32_t)ringStatus;
        KeReleaseSpinLock(&engine->Lock, irql);
        return STATUS_UNSUCCESSFUL;
    }
    KeMemoryBarrier();

    if (engine->ComputeRing.Vmid == BC250_GCVM_COMPUTE_VMID) {
        uint32_t start = (plan.NewWptrDwords - 12u) &
            (BC250_SUBMISSION_RING_DWORDS - 1u);
        uint32_t word;
        for (word = 0u; word < 4u; ++word) {
            engine->DiagnosticDescriptor[word] = engine->ComputeRingVirtual[
                (start + word) & (BC250_SUBMISSION_RING_DWORDS - 1u)];
        }
        Bc250DisplayLog("[MEC_IB_DESCRIPTOR] Vmid=%u StartDW=%u Raw=%08X,%08X,%08X,%08X FenceAddress=0x%llX\n",
            engine->ComputeRing.Vmid, start,
            engine->ComputeRingVirtual[start],
            engine->ComputeRingVirtual[(start + 1u) & (BC250_SUBMISSION_RING_DWORDS - 1u)],
            engine->ComputeRingVirtual[(start + 2u) & (BC250_SUBMISSION_RING_DWORDS - 1u)],
            engine->ComputeRingVirtual[(start + 3u) & (BC250_SUBMISSION_RING_DWORDS - 1u)],
            (unsigned long long)engine->ComputeRing.FenceGpuAddress);
    }

    ringStatus = Bc250PersistentRingCommit(&engine->ComputeRing, &plan);
    if (ringStatus != BC250_PRING_OK) {
        ++engine->SubmitRejectedEmit;
        engine->LastSubmitStatus = (uint32_t)ringStatus;
        KeReleaseSpinLock(&engine->Lock, irql);
        return STATUS_UNSUCCESSFUL;
    }

    KeMemoryBarrier();
    status = Bc250SubmissionPublishWptr(
        Context, engine, plan.NewWptrDwords);
    if (!NT_SUCCESS(status)) {
        /*
         * O commit ja reservou o cursor/fence no estado do anel. Sem kick,
         * nao existe rollback seguro desse lote. Isole o motor ate teardown
         * em vez de aceitar trabalho sobre estado parcialmente publicado.
         */
        engine->LastSubmitStatus = (uint32_t)status;
        InterlockedExchange(&engine->State, BC250_SUBMISSION_ENGINE_FAULTED);
        KeReleaseSpinLock(&engine->Lock, irql);

        Bc250DisplayLog("[SubmissionEngineSubmit] RECUSA: Bc250SubmissionPublishWptr falhou (status=0x%08X NewWptr=%u Step=%u DbCtrl=0x%08X ExpOff=0x%X Act=%u Rptr=%u Err=0x%08X Virt=%p Size=0x%X Failures=%u)\n",
            (uint32_t)status, plan.NewWptrDwords,
            engine->PublishFailStep,
            engine->PublishReadbackDoorbellControl,
            engine->DoorbellOffsetBytes,
            engine->PublishHqdActive,
            engine->PublishHqdRptr,
            engine->PublishHqdError,
            engine->DoorbellVirtual,
            engine->Buffers.DoorbellSizeBytes,
            engine->DoorbellWriteFailures);
        return status;
    }

    engine->LastPlanPadDwords = plan.PadDwords;
    engine->LastSubmittedFence = engine->ComputeRing.LastSubmittedFence;
    ++engine->SubmitAccepted;
    *AssignedFence = engine->ComputeRing.LastSubmittedFence;
    engine->DiagnosticSubmitTime = KeQueryInterruptTime();

    KeReleaseSpinLock(&engine->Lock, irql);

    Bc250DisplayLog("[SubmissionEngineSubmit] SUCESSO: NewWptr=%u Fence=%llu Pad=%u Accepted=%u Kicks=%u\n",
        plan.NewWptrDwords, (unsigned long long)engine->ComputeRing.LastSubmittedFence,
        plan.PadDwords, engine->SubmitAccepted, engine->DoorbellKicks);

    return STATUS_SUCCESS;
}

_Check_return_
NTSTATUS Bc250SubmissionEngineSubmitDirect(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t NodeOrdinal,
    _In_ const uint32_t *Dwords,
    _In_ uint32_t DwordCount,
    _Out_ uint64_t *AssignedFence)
{
    BC250_SUBMISSION_ENGINE *engine;
    uint32_t startWptr;
    uint32_t totalDwords;
    uint32_t newWptr;
    uint32_t cursor;
    uint32_t i;
    uint64_t assignedFenceVal;
    NTSTATUS status;
    KIRQL irql;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE ||
        Dwords == NULL || DwordCount == 0u || AssignedFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *AssignedFence = 0ull;

    engine = &Context->SubmissionEngine;
    if (InterlockedCompareExchange(&engine->State,
            BC250_SUBMISSION_ENGINE_ONLINE,
            BC250_SUBMISSION_ENGINE_ONLINE) !=
        BC250_SUBMISSION_ENGINE_ONLINE) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (NodeOrdinal != BC250_SUBMISSION_NODE_COMPUTE) {
        return STATUS_NOT_SUPPORTED;
    }

    KeAcquireSpinLock(&engine->Lock, &irql);
    ++engine->SubmitCalls;

    assignedFenceVal = engine->ComputeRing.LastSubmittedFence + 1ull;
    startWptr = engine->ComputeRing.WptrDwords;
    totalDwords = DwordCount + 8u; /* Dwords + 8 dwords RELEASE_MEM */

    if (startWptr + totalDwords > BC250_SUBMISSION_RING_DWORDS) {
        /* Wraparound: preenche o restante com PACKET2 NOP */
        for (i = startWptr; i < BC250_SUBMISSION_RING_DWORDS; ++i) {
            engine->ComputeRingVirtual[i] = BC250_PM4_PACKET2_NOP;
        }
        startWptr = 0u;
        ++engine->ComputeRing.WrapCount;
    }

    /* Copia os dwords de comando diretamente para o anel */
    for (i = 0u; i < DwordCount; ++i) {
        engine->ComputeRingVirtual[startWptr + i] = Dwords[i];
    }

    /* Emite o RELEASE_MEM de sequencia do anel */
    cursor = startWptr + DwordCount;
    (void)Bc250Pm4EmitReleaseMemEop32(
        (uint32_t *)(ULONG_PTR)engine->ComputeRingVirtual,
        BC250_SUBMISSION_RING_DWORDS,
        &cursor,
        BC250_HQD_FENCE_GPUVA,
        (uint32_t)assignedFenceVal,
        BC250_PM4_RELEASE_MEM_INT_SEL_NONE);

    newWptr = startWptr + totalDwords;
    engine->ComputeRing.WptrDwords = newWptr;
    engine->ComputeRing.LastSubmittedFence = assignedFenceVal;
    engine->LastSubmittedFence = assignedFenceVal;
    *AssignedFence = assignedFenceVal;
    ++engine->ComputeRing.SubmissionCount;
    ++engine->SubmitAccepted;

    KeMemoryBarrier();
    status = Bc250SubmissionPublishWptr(Context, engine, newWptr);
    if (!NT_SUCCESS(status)) {
        engine->LastSubmitStatus = (uint32_t)status;
        InterlockedExchange(&engine->State, BC250_SUBMISSION_ENGINE_FAULTED);
        KeReleaseSpinLock(&engine->Lock, irql);
        return status;
    }

    KeReleaseSpinLock(&engine->Lock, irql);
    return STATUS_SUCCESS;
}

_Check_return_
NTSTATUS Bc250SubmissionEnginePoll(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    _Out_opt_ uint32_t *RetiredNow)
{
    BC250_SUBMISSION_ENGINE *engine;
    BC250_PRING_STATUS ringStatus;
    uint32_t observedFence = 0u;
    uint32_t rptrReport = 0u;
    uint32_t rptrMmio = 0u;
    uint32_t retired = 0u;
    uint32_t diagnostic = 0u, validReads = 0u;
    uint32_t queueRaw[6] = {0u};
    uint32_t cpfStalled = 0u, cpcStalled = 0u;
    uint32_t index;
    uint64_t pendingFence = 0ull;
    static const uint32_t queueRegisters[6] = {
        BC250_HQD_MMIO_ACTIVE, BC250_HQD_MMIO_HQD_ERROR,
        BC250_HQD_MMIO_HQ_STATUS0, BC250_HQD_MMIO_PQ_WPTR_LO,
        BC250_HQD_MMIO_VMID, BC250_HQD_MMIO_IB_CONTROL
    };
    KIRQL irql;

    if (RetiredNow != NULL) *RetiredNow = 0u;
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    engine = &Context->SubmissionEngine;
    if (InterlockedCompareExchange(&engine->State,
            BC250_SUBMISSION_ENGINE_ONLINE,
            BC250_SUBMISSION_ENGINE_ONLINE) !=
        BC250_SUBMISSION_ENGINE_ONLINE) {
        return STATUS_DEVICE_NOT_READY;
    }

    KeAcquireSpinLock(&engine->Lock, &irql);
    ++engine->PollCalls;

    /*
     * Dreno e coerencia HDP (PCIe) antes de ler a fence e o cursor de VRAM.
     * Sem o dreno de HDP, leituras de CPU sobre PCIe BAR 0 podem retornar
     * valores desatualizados (stale) cacheados na FIFO PCIe, ocasionando
     * falsos timeouts no motor MEC.
     */
    Bc250SdmaPresentFlushHdp();

    /* Leitura pura: nada aqui escreve fence ou cursor. */
    if (engine->ComputeFenceVirtual != NULL) {
        observedFence = engine->ComputeFenceVirtual[0];
    }
    if (engine->ComputeRptrReportVirtual != NULL) {
        rptrReport = engine->ComputeRptrReportVirtual[0];
    }
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->SelectedSelector);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_PQ_RPTR, &rptrMmio);

    pendingFence = engine->ComputeRing.LastSubmittedFence;
    if (engine->ComputeRing.Vmid == BC250_GCVM_COMPUTE_VMID &&
        pendingFence > engine->ComputeRing.LastRetiredFence &&
        observedFence != (uint32_t)pendingFence) {
        /* Re-drena HDP e tenta releitura caso a escrita da GPU estivesse transitando na FIFO */
        Bc250SdmaPresentFlushHdp();
        if (engine->ComputeFenceVirtual != NULL) {
            observedFence = engine->ComputeFenceVirtual[0];
        }
        if (engine->ComputeRptrReportVirtual != NULL) {
            rptrReport = engine->ComputeRptrReportVirtual[0];
        }
    }

    /* Capture the first pending observation before Retire can fault the
     * engine. The 3.105 time gate could miss failures before 100 ms.
     * This is not a hang verdict. Failed register reads stay distinguishable. */
    if (engine->ComputeRing.Vmid == BC250_GCVM_COMPUTE_VMID &&
        pendingFence > engine->ComputeRing.LastRetiredFence &&
        observedFence != (uint32_t)pendingFence &&
        engine->DiagnosticPendingFence != pendingFence) {
        diagnostic = 1u;
        engine->DiagnosticPendingFence = pendingFence;
        for (index = 0u; index < 4u; ++index) {
            engine->DiagnosticPendingDescriptor[index] = engine->DiagnosticDescriptor[index];
        }
        for (index = 0u; index < 6u; ++index) {
            if (Bc250SubmissionRead32(engine, Context,
                    queueRegisters[index], &queueRaw[index])) {
                validReads |= 1u << index;
            }
            engine->DiagnosticQueueRaw[index] = queueRaw[index];
        }
        engine->DiagnosticValidReads = validReads;
        engine->DiagnosticObservedFence = observedFence;
        engine->DiagnosticRptr = rptrMmio;
        engine->DiagnosticWriteback = rptrReport;
        (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &cpfStalled);
        (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, &cpcStalled);
    }
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->OriginalSelector);

    engine->LastFenceObserved = observedFence;
    engine->LastRptrReportMem = rptrReport;
    engine->LastRptrMmio = rptrMmio;

    ringStatus = Bc250PersistentRingRetire(&engine->ComputeRing,
        observedFence,
        rptrReport & (BC250_SUBMISSION_RING_DWORDS - 1u),
        &retired);

    if (retired > 0u || ringStatus != BC250_PRING_OK || engine->State != BC250_SUBMISSION_ENGINE_ONLINE) {
        Bc250DisplayLog("[SubmissionPoll] State=%d ObservedFence=%u RptrReport=%u RptrMmio=%u Sub=%llu Ret=%llu RingStatus=%u Retired=%u\n",
            (int)engine->State, observedFence, rptrReport, rptrMmio,
            (unsigned long long)engine->ComputeRing.LastSubmittedFence,
            (unsigned long long)engine->ComputeRing.LastRetiredFence,
            (uint32_t)ringStatus, retired);
    }

    if (ringStatus == BC250_PRING_FENCE_REGRESSION) {
        /*
         * A GPU gravou um valor fora da janela em voo.  Isso e' corrupcao,
         * nao progresso: o motor e' marcado com falta e nada e' aposentado.
         */
        ++engine->FenceRegressionCount;
        InterlockedExchange(&engine->State, BC250_SUBMISSION_ENGINE_FAULTED);
        Bc250DisplayLog("[SubmissionPoll] FENCE_REGRESSION: ObservedFence=%u LastRetired=%llu LastSub=%llu InFlight=%llu -> motor FAULTED\n",
            observedFence,
            (unsigned long long)engine->ComputeRing.LastRetiredFence,
            (unsigned long long)engine->ComputeRing.LastSubmittedFence,
            (unsigned long long)(engine->ComputeRing.LastSubmittedFence > engine->ComputeRing.LastRetiredFence ?
                engine->ComputeRing.LastSubmittedFence - engine->ComputeRing.LastRetiredFence : 0ull));
        KeReleaseSpinLock(&engine->Lock, irql);
        return STATUS_DEVICE_DATA_ERROR;
    }
    if (ringStatus != BC250_PRING_OK) {
        Bc250DisplayLog("[SubmissionPoll] RECUSA ringStatus=%u (ObservedFence=%u LastRetired=%llu LastSub=%llu)\n",
            (uint32_t)ringStatus, observedFence,
            (unsigned long long)engine->ComputeRing.LastRetiredFence,
            (unsigned long long)engine->ComputeRing.LastSubmittedFence);
        KeReleaseSpinLock(&engine->Lock, irql);
        return STATUS_UNSUCCESSFUL;
    }

    engine->RetiredTotal += retired;
    engine->LastRetiredFence = engine->ComputeRing.LastRetiredFence;
    if (RetiredNow != NULL) *RetiredNow = retired;

    KeReleaseSpinLock(&engine->Lock, irql);
    if (diagnostic) {
        Bc250DisplayLog("[MEC_PENDING] Fence=%llu Observed=%u RPTR=%u Writeback=%u ValidReads=0x%X Active=%08X Error=%08X HqStatus=%08X WPTR=%u Vmid=%08X IbControl=%08X\n",
            (unsigned long long)pendingFence, observedFence, rptrMmio,
            rptrReport, validReads, queueRaw[0], queueRaw[1], queueRaw[2],
            queueRaw[3], queueRaw[4], queueRaw[5]);
        Bc250DisplayLog("[MEC_PENDING_STALL] CpfStalled=%08X CpcStalled=%08X\n",
            cpfStalled, cpcStalled);
    }
    return STATUS_SUCCESS;
}

VOID Bc250SubmissionEngineArchiveDiagnostics(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_SUBMISSION_ENGINE *engine;
    uint32_t raw[8] = {0u};
    uint32_t valid = 0u, i;
    uint32_t fence = 0u, fenceMapped = 0u;
    uint32_t ibBaseLo = 0u, ibBaseHi = 0u, ibRptr = 0u, dequeueReq = 0u;
    uint32_t cpfStatus = 0u, cpfBusy = 0u, cpfStalled = 0u;
    uint32_t cpcStatus = 0u, cpcBusy = 0u, cpcStalled = 0u;
    uint32_t mecMe1Hdr = 0u, mecMe2Hdr = 0u;
    uint32_t mec1InstrPntr = 0u, mec2InstrPntr = 0u;
    uint32_t cpStalled1 = 0u, cpStat = 0u;
    uint32_t selReadback = 0u, restReadback = 0u;
    KIRQL irql;
    static const uint32_t registers[8] = {
        BC250_HQD_MMIO_ACTIVE, BC250_HQD_MMIO_HQD_ERROR,
        BC250_HQD_MMIO_HQ_STATUS0, BC250_HQD_MMIO_PQ_WPTR_LO,
        BC250_HQD_MMIO_VMID, BC250_HQD_MMIO_IB_CONTROL,
        BC250_HQD_MMIO_PQ_RPTR, BC250_HQD_MMIO_PQ_CONTROL
    };
    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) return;
    engine = &Context->SubmissionEngine;
    if (engine->Signature != BC250_SUBMISSION_ENGINE_SIGNATURE) {
        Bc250DisplayLog("[MEC_STOP_STATE] UNAVAILABLE: engine was not initialized; MmioState=%u\n",
            Context->Core.MmioTransport.State);
        return;
    }
    /* No ONLINE/VMID/fence/time gate: those are precisely the unknowns.
     * StopDevice has drained callers; use the lock for MMIO bank selection. */
    KeAcquireSpinLock(&engine->Lock, &irql);
    if (engine->ComputeFenceVirtual != NULL) {
        fence = engine->ComputeFenceVirtual[0];
        fenceMapped = 1u;
    }
    if (Context->Core.MmioTransport.State == BC250_MMIO_TOPOLOGY_MAPPED) {
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
        if (Bc250SubmissionWrite32(engine, Context,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->SelectedSelector)) {
            (void)Bc250SubmissionRead32(engine, Context,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &selReadback);
            for (i = 0u; i < 8u; ++i) {
                if (Bc250SubmissionRead32(engine, Context, registers[i], &raw[i]))
                    valid |= 1u << i;
            }
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_IB_BASE_LO, &ibBaseLo);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_IB_BASE_HI, &ibBaseHi);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_IB_RPTR, &ibRptr);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_DEQUEUE_REQUEST, &dequeueReq);
            (void)Bc250SubmissionWrite32(engine, Context,
                BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
            (void)Bc250SubmissionWrite32(engine, Context,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->OriginalSelector);
            (void)Bc250SubmissionRead32(engine, Context,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &restReadback);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPF_STATUS, &cpfStatus);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &cpfBusy);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &cpfStalled);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPC_STATUS, &cpcStatus);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &cpcBusy);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, &cpcStalled);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_MEC_ME1_HEADER_DUMP, &mecMe1Hdr);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP, &mecMe2Hdr);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_MEC1_INSTR_PNTR, &mec1InstrPntr);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR, &mec2InstrPntr);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_STALLED_STAT1, &cpStalled1);
            (void)Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_CP_STAT, &cpStat);
        }
    }
    KeReleaseSpinLock(&engine->Lock, irql);
    Bc250DisplayLog("[MEC_STOP_STATE] Engine=%ld Vmid=%u Sub=%llu Ret=%llu Polls=%u PollFailures=%u DpcRuns=%u TimerInitialized=%ld LastSubmit=%08X CapturedFence=%llu FenceMapped=%u FenceNow=%u MmioState=%u\n",
        engine->State, engine->ComputeRing.Vmid,
        (unsigned long long)engine->ComputeRing.LastSubmittedFence,
        (unsigned long long)engine->ComputeRing.LastRetiredFence,
        engine->PollCalls, Context->RetirementPollFailures,
        Context->RetirementDpcRuns, Context->RetirementTimerInitialized,
        engine->LastSubmitStatus, (unsigned long long)engine->DiagnosticPendingFence,
        fenceMapped, fence, Context->Core.MmioTransport.State);
    Bc250DisplayLog("[MEC_STOP_QUEUE] Selector=0x%08X (Req=0x%08X) RestoredSel=0x%08X ValidReads=0x%X Active=%08X Error=%08X HqStatus=%08X WPTR=%u Vmid=%08X IbControl=%08X RPTR=%u PqControl=%08X\n",
        selReadback, engine->SelectedSelector, restReadback, valid, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);
    Bc250DisplayLog("[MEC_STOP_CPF] CpfStat=%08X CpfBusy=%08X CpfStalled=%08X CpcStat=%08X CpcBusy=%08X CpcStalled=%08X CpStalled1=%08X CpStat=%08X\n",
        cpfStatus, cpfBusy, cpfStalled, cpcStatus, cpcBusy, cpcStalled, cpStalled1, cpStat);
    Bc250DisplayLog("[MEC_STOP_EXEC] Mec1Hdr=%08X Mec2Hdr=%08X Mec1Pntr=%08X Mec2Pntr=%08X IbBase=%08X:%08X IbRptr=%08X Dequeue=%08X\n",
        mecMe1Hdr, mecMe2Hdr, mec1InstrPntr, mec2InstrPntr, ibBaseHi, ibBaseLo, ibRptr, dequeueReq);
    Bc250DisplayLog("[MEC_LAST_DESCRIPTOR] Raw=%08X,%08X,%08X,%08X SubmitTime100ns=%llu\n",
        engine->DiagnosticDescriptor[0], engine->DiagnosticDescriptor[1],
        engine->DiagnosticDescriptor[2], engine->DiagnosticDescriptor[3],
        (unsigned long long)engine->DiagnosticSubmitTime);
}

/* ==========================================================================
   Fase V2 — rastreador de fences WDDM.

   FIFO puro, sem hardware.  Desde o 3.67 a serializacao e' propria: cada
   operacao publica adquire o lock do rastreador.  DxgkCbSynchronizeExecution
   continua envolvendo os caminhos quentes, mas nao e' mais a unica garantia —
   foi a dependencia exclusiva dele que produziu o STATUS_DEVICE_BUSY falso do
   ensaio 3.66.  Nenhuma destas funcoes escreve fence: elas apenas
   correlacionam a fence do anel, que a GPU gravou, com o identificador que o
   dxgkrnl reconhece.
   ========================================================================== */

/* --------------------------------------------------------------------------
   Lock proprio do rastreador (3.67).

   Aquisicao valida em PASSIVE_LEVEL, DISPATCH_LEVEL e DIRQL.  Abaixo de
   DISPATCH o IRQL sobe para DISPATCH antes do giro, de modo que o DPC de
   retirement nao possa preemptar o portador no mesmo processador.  A partir de
   DISPATCH o IRQL nao e' tocado: girar e' suficiente e subir seria incorreto.

   Limite de giro: ver BC250_FENCE_LOCK_SPIN_LIMIT no header.  Estourar devolve
   FALSE em vez de travar o processador.
   -------------------------------------------------------------------------- */

static BOOLEAN Bc250FenceLockAcquire(
    _Inout_ BC250_FENCE_LOCK *Lock,
    _Out_ KIRQL *OldIrql)
{
    uint32_t spins = 0u;
    BOOLEAN contended = FALSE;

    *OldIrql = PASSIVE_LEVEL;
    if (Lock == NULL) {
        return FALSE;
    }
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    {
        KIRQL current = KeGetCurrentIrql();
        if (current < DISPATCH_LEVEL) {
            KeRaiseIrql(DISPATCH_LEVEL, OldIrql);
        } else {
            *OldIrql = current;
        }
    }
#endif
    while (InterlockedCompareExchange(&Lock->Owned, 1, 0) != 0) {
        contended = TRUE;
        ++spins;
        if (spins >= BC250_FENCE_LOCK_SPIN_LIMIT) {
            (void)InterlockedIncrement(&Lock->AcquireFailures);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
            if (*OldIrql < DISPATCH_LEVEL) {
                KeLowerIrql(*OldIrql);
            }
#endif
            return FALSE;
        }
        YieldProcessor();
    }
    if (contended) {
        (void)InterlockedIncrement(&Lock->Contentions);
    }
    return TRUE;
}

static void Bc250FenceLockRelease(
    _Inout_ BC250_FENCE_LOCK *Lock,
    _In_ KIRQL OldIrql)
{
    if (Lock == NULL) {
        return;
    }
    InterlockedExchange(&Lock->Owned, 0);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (OldIrql < DISPATCH_LEVEL) {
        KeLowerIrql(OldIrql);
    }
#else
    UNREFERENCED_PARAMETER(OldIrql);
#endif
}

/* Corpos puros. Assumem o lock ja tomado; nunca chamar de fora deste bloco. */
static BOOLEAN Bc250WddmFenceTrackerReserveUnlocked(
    BC250_WDDM_FENCE_TRACKER *Tracker,
    uint32_t SubmissionFenceId,
    uint32_t NodeOrdinal,
    uint32_t EngineOrdinal,
    uint32_t *ReservationToken);

static BOOLEAN Bc250WddmFenceTrackerCancelUnlocked(
    BC250_WDDM_FENCE_TRACKER *Tracker,
    uint32_t ReservationToken);

static BOOLEAN Bc250WddmFenceTrackerCommitUnlocked(
    BC250_WDDM_FENCE_TRACKER *Tracker,
    uint32_t ReservationToken,
    uint64_t RingFence);

void Bc250WddmFenceTrackerReset(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker)
{
    if (Tracker == NULL) {
        return;
    }
    /*
     * O lock sobrevive ao reset. Zerar a estrutura inteira apagaria o campo
     * Owned de um lock possivelmente tomado, o que trocaria um FIFO sujo por
     * corrupcao silenciosa. Contadores de diagnostico do lock tambem ficam.
     */
    RtlZeroMemory(Tracker->Entries, sizeof(Tracker->Entries));
    RtlZeroMemory(Tracker->Reservations, sizeof(Tracker->Reservations));
    Tracker->Head = 0u;
    Tracker->Count = 0u;
    Tracker->ReservedCount = 0u;
    Tracker->NextReservationToken = 0u;
    Tracker->PushedTotal = 0u;
    Tracker->NotifiedTotal = 0u;
    Tracker->OverflowCount = 0u;
    Tracker->OutOfOrderCount = 0u;
    Tracker->LastPushedRingFence = 0ull;
    Tracker->LastPushedRingFenceByNode[0] = 0ull;
    Tracker->LastPushedRingFenceByNode[1] = 0ull;
    Tracker->LastNotifiedFenceId = 0u;
    Tracker->LastNotifiedFenceIdByNode[0] = 0u;
    Tracker->LastNotifiedFenceIdByNode[1] = 0u;
    Tracker->RejectedWhileStopping = 0u;
    InterlockedExchange(&Tracker->Lock.Initialized, 1);
    InterlockedExchange(&Tracker->AcceptingWork, 1);
}

void Bc250WddmFenceTrackerSetAcceptingWork(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ BOOLEAN Accepting)
{
    if (Tracker == NULL) {
        return;
    }
    InterlockedExchange(&Tracker->AcceptingWork, Accepting ? 1 : 0);
}

_Check_return_
BOOLEAN Bc250WddmFenceTrackerSnapshotCounts(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _Out_ uint32_t *PendingCount,
    _Out_ uint32_t *ReservedCount)
{
    KIRQL irql;

    if (PendingCount != NULL) *PendingCount = 0u;
    if (ReservedCount != NULL) *ReservedCount = 0u;
    if (Tracker == NULL || PendingCount == NULL || ReservedCount == NULL) {
        return FALSE;
    }
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }
    *PendingCount = Tracker->Count;
    *ReservedCount = Tracker->ReservedCount;
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return TRUE;
}

/*
 * DX.47: estado de UM no para concluir a preempcao. Em voo = entradas
 * empurradas e ainda nao notificadas; reservas = submissoes entre reserve e
 * commit. LastNotified e' a ultima SubmissionFenceId ja reportada como
 * DMA_COMPLETED naquele no -- o LastCompletedFenceId do DMA_PREEMPTED.
 */
_Check_return_
BOOLEAN Bc250WddmFenceTrackerNodeState(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t NodeOrdinal,
    _Out_ uint32_t *InFlight,
    _Out_ uint32_t *Reserved,
    _Out_ uint32_t *LastNotified)
{
    KIRQL irql;
    uint32_t i;
    uint32_t inFlight = 0u;
    uint32_t reserved = 0u;

    if (InFlight != NULL) *InFlight = 0u;
    if (Reserved != NULL) *Reserved = 0u;
    if (LastNotified != NULL) *LastNotified = 0u;
    if (Tracker == NULL || InFlight == NULL || Reserved == NULL ||
        LastNotified == NULL || NodeOrdinal > BC250_SUBMISSION_NODE_COMPUTE) {
        return FALSE;
    }
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }
    for (i = 0u; i < Tracker->Count; ++i) {
        const BC250_WDDM_FENCE_ENTRY *entry =
            &Tracker->Entries[(Tracker->Head + i) % BC250_WDDM_FENCE_TRACK_DEPTH];
        if (entry->NodeOrdinal == NodeOrdinal) ++inFlight;
    }
    for (i = 0u; i < BC250_WDDM_FENCE_TRACK_DEPTH; ++i) {
        if (Tracker->Reservations[i].Active != 0u &&
            Tracker->Reservations[i].NodeOrdinal == NodeOrdinal) {
            ++reserved;
        }
    }
    *InFlight = inFlight;
    *Reserved = reserved;
    *LastNotified = Tracker->LastNotifiedFenceIdByNode[NodeOrdinal];
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return TRUE;
}

_Check_return_
BOOLEAN Bc250WddmFenceTrackerAbortPending(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _Out_writes_to_(Max, *AbortedCount) BC250_WDDM_FENCE_ENTRY *Out,
    _In_ uint32_t Max,
    _Out_ uint32_t *AbortedCount,
    _Out_ uint32_t *CommittedCount,
    _Out_ uint32_t *ReservedCount)
{
    KIRQL irql;
    uint32_t produced = 0u;
    uint32_t committed;
    uint32_t reserved;
    uint32_t i;

    if (AbortedCount != NULL) *AbortedCount = 0u;
    if (CommittedCount != NULL) *CommittedCount = 0u;
    if (ReservedCount != NULL) *ReservedCount = 0u;
    if (Tracker == NULL || Out == NULL || Max < BC250_WDDM_FENCE_TRACK_DEPTH ||
        AbortedCount == NULL || CommittedCount == NULL || ReservedCount == NULL) {
        return FALSE;
    }
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }

    committed = Tracker->Count;
    reserved = Tracker->ReservedCount;
    if ((committed + reserved) > Max) {
        Bc250FenceLockRelease(&Tracker->Lock, irql);
        return FALSE;
    }

    while (Tracker->Count > 0u) {
        Out[produced++] = Tracker->Entries[Tracker->Head];
        RtlZeroMemory(&Tracker->Entries[Tracker->Head],
            sizeof(Tracker->Entries[Tracker->Head]));
        Tracker->Head = (Tracker->Head + 1u) % BC250_WDDM_FENCE_TRACK_DEPTH;
        --Tracker->Count;
    }
    for (i = 0u; i < BC250_WDDM_FENCE_TRACK_DEPTH; ++i) {
        BC250_WDDM_FENCE_RESERVATION *res = &Tracker->Reservations[i];
        if (res->Active != 0u) {
            BC250_WDDM_FENCE_ENTRY *entry = &Out[produced++];
            RtlZeroMemory(entry, sizeof(*entry));
            entry->SubmissionFenceId = res->SubmissionFenceId;
            entry->NodeOrdinal = res->NodeOrdinal;
            entry->EngineOrdinal = res->EngineOrdinal;
            entry->GpuvaSlot = res->GpuvaSlot;
            entry->GpuvaGeneration = res->GpuvaGeneration;
            entry->GpuvaPinValid = res->GpuvaPinValid;
        }
        RtlZeroMemory(res, sizeof(*res));
    }
    Tracker->Head = 0u;
    Tracker->ReservedCount = 0u;

    *AbortedCount = produced;
    *CommittedCount = committed;
    *ReservedCount = reserved;
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return TRUE;
}

_Check_return_
BOOLEAN Bc250WddmFenceTrackerReserve(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t SubmissionFenceId,
    _In_ uint32_t NodeOrdinal,
    _In_ uint32_t EngineOrdinal,
    _Out_ uint32_t *ReservationToken)
{
    KIRQL irql;
    BOOLEAN ok;

    if (ReservationToken != NULL) *ReservationToken = 0u;
    if (Tracker == NULL || ReservationToken == NULL) {
        return FALSE;
    }
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }
    ok = Bc250WddmFenceTrackerReserveUnlocked(
        Tracker, SubmissionFenceId, NodeOrdinal, EngineOrdinal,
        ReservationToken);
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return ok;
}

_Check_return_
BOOLEAN Bc250WddmFenceTrackerCancel(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t ReservationToken)
{
    KIRQL irql;
    BOOLEAN ok;

    if (Tracker == NULL) {
        return FALSE;
    }
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }
    ok = Bc250WddmFenceTrackerCancelUnlocked(Tracker, ReservationToken);
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return ok;
}

_Check_return_
BOOLEAN Bc250WddmFenceTrackerCommit(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t ReservationToken,
    _In_ uint64_t RingFence)
{
    KIRQL irql;
    BOOLEAN ok;

    if (Tracker == NULL) {
        return FALSE;
    }
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }
    ok = Bc250WddmFenceTrackerCommitUnlocked(Tracker, ReservationToken, RingFence);
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return ok;
}

static BOOLEAN Bc250WddmFenceTrackerReserveUnlocked(
    BC250_WDDM_FENCE_TRACKER *Tracker,
    uint32_t SubmissionFenceId,
    uint32_t NodeOrdinal,
    uint32_t EngineOrdinal,
    uint32_t *ReservationToken)
{
    uint32_t i;

    if (ReservationToken != NULL) *ReservationToken = 0u;
    if (Tracker == NULL || ReservationToken == NULL ||
        NodeOrdinal > BC250_SUBMISSION_NODE_COMPUTE) {
        return FALSE;
    }
    /*
     * Portao de admissao. Fechado no passo 1 da quiescencia do StopDevice;
     * a partir dai nenhuma reserva nova entra, e por isso a fotografia
     * posterior das contagens nao pode mais crescer pelas costas.
     */
    if (InterlockedCompareExchange(&Tracker->AcceptingWork, 1, 1) != 1) {
        ++Tracker->RejectedWhileStopping;
        return FALSE;
    }
    if ((Tracker->Count + Tracker->ReservedCount) >= BC250_WDDM_FENCE_TRACK_DEPTH) {
        ++Tracker->OverflowCount;
        return FALSE;
    }

    for (i = 0u; i < BC250_WDDM_FENCE_TRACK_DEPTH; ++i) {
        BC250_WDDM_FENCE_RESERVATION *res = &Tracker->Reservations[i];
        if (res->Active == 0u) {
            ++Tracker->NextReservationToken;
            if (Tracker->NextReservationToken == 0u) {
                ++Tracker->NextReservationToken;
            }
            res->Token = Tracker->NextReservationToken;
            res->SubmissionFenceId = SubmissionFenceId;
            res->NodeOrdinal = NodeOrdinal;
            res->EngineOrdinal = EngineOrdinal;
            res->GpuvaSlot = 0u;
            res->GpuvaGeneration = 0u;
            res->GpuvaPinValid = 0u;
            res->Active = 1u;
            ++Tracker->ReservedCount;
            *ReservationToken = res->Token;
            return TRUE;
        }
    }

    ++Tracker->OverflowCount;
    return FALSE;
}

static BOOLEAN Bc250WddmFenceTrackerCancelUnlocked(
    BC250_WDDM_FENCE_TRACKER *Tracker,
    uint32_t ReservationToken)
{
    uint32_t i;

    if (Tracker == NULL || ReservationToken == 0u) {
        return FALSE;
    }

    for (i = 0u; i < BC250_WDDM_FENCE_TRACK_DEPTH; ++i) {
        BC250_WDDM_FENCE_RESERVATION *res = &Tracker->Reservations[i];
        if (res->Active != 0u && res->Token == ReservationToken) {
            res->Active = 0u;
            res->Token = 0u;
            res->GpuvaPinValid = 0u;
            if (Tracker->ReservedCount > 0u) {
                --Tracker->ReservedCount;
            }
            return TRUE;
        }
    }

    return FALSE;
}

BOOLEAN Bc250WddmFenceTrackerAttachGpuvaPin(
    BC250_WDDM_FENCE_TRACKER *Tracker,
    uint32_t ReservationToken,
    uint32_t GpuvaSlot,
    uint32_t GpuvaGeneration)
{
    KIRQL irql;
    uint32_t i;
    BOOLEAN ok = FALSE;

    if (Tracker == NULL || ReservationToken == 0u || GpuvaSlot > 1u ||
        GpuvaGeneration == 0u) return FALSE;
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }
    for (i = 0u; i < BC250_WDDM_FENCE_TRACK_DEPTH; ++i) {
        BC250_WDDM_FENCE_RESERVATION *reservation = &Tracker->Reservations[i];
        if (reservation->Active != 0u && reservation->Token == ReservationToken) {
            if (reservation->GpuvaPinValid == 0u) {
                reservation->GpuvaSlot = GpuvaSlot;
                reservation->GpuvaGeneration = GpuvaGeneration;
                reservation->GpuvaPinValid = 1u;
                ok = TRUE;
            }
            break;
        }
    }
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return ok;
}

static BOOLEAN Bc250WddmFenceTrackerCommitUnlocked(
    BC250_WDDM_FENCE_TRACKER *Tracker,
    uint32_t ReservationToken,
    uint64_t RingFence)
{
    uint32_t i;
    uint32_t slot;

    if (Tracker == NULL || ReservationToken == 0u || RingFence == 0ull) {
        return FALSE;
    }

    for (i = 0u; i < BC250_WDDM_FENCE_TRACK_DEPTH; ++i) {
        BC250_WDDM_FENCE_RESERVATION *res = &Tracker->Reservations[i];
        if (res->Active != 0u && res->Token == ReservationToken) {
            if (Tracker->Count >= BC250_WDDM_FENCE_TRACK_DEPTH) {
                ++Tracker->OverflowCount;
                res->Active = 0u;
                if (Tracker->ReservedCount > 0u) --Tracker->ReservedCount;
                return FALSE;
            }
            uint32_t nodeIdx = (res->NodeOrdinal < 2u) ? res->NodeOrdinal : 0u;
            if (Tracker->LastPushedRingFenceByNode[nodeIdx] != 0ull &&
                RingFence <= Tracker->LastPushedRingFenceByNode[nodeIdx]) {
                ++Tracker->OutOfOrderCount;
                res->Active = 0u;
                if (Tracker->ReservedCount > 0u) --Tracker->ReservedCount;
                return FALSE;
            }

            slot = (Tracker->Head + Tracker->Count) % BC250_WDDM_FENCE_TRACK_DEPTH;
            Tracker->Entries[slot].RingFence = RingFence;
            Tracker->Entries[slot].SubmissionFenceId = res->SubmissionFenceId;
            Tracker->Entries[slot].NodeOrdinal = res->NodeOrdinal;
            Tracker->Entries[slot].EngineOrdinal = res->EngineOrdinal;
            Tracker->Entries[slot].GpuvaSlot = res->GpuvaSlot;
            Tracker->Entries[slot].GpuvaGeneration = res->GpuvaGeneration;
            Tracker->Entries[slot].GpuvaPinValid = res->GpuvaPinValid;

            res->Active = 0u;
            res->Token = 0u;
            res->GpuvaPinValid = 0u;
            if (Tracker->ReservedCount > 0u) --Tracker->ReservedCount;

            ++Tracker->Count;
            ++Tracker->PushedTotal;
            Tracker->LastPushedRingFence = RingFence;
            Tracker->LastPushedRingFenceByNode[nodeIdx] = RingFence;
            return TRUE;
        }
    }

    return FALSE;
}

_Check_return_
BOOLEAN Bc250WddmFenceTrackerPush(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint64_t RingFence,
    _In_ uint32_t SubmissionFenceId,
    _In_ uint32_t NodeOrdinal,
    _In_ uint32_t EngineOrdinal)
{
    KIRQL irql;
    uint32_t token = 0u;
    BOOLEAN ok;

    if (Tracker == NULL) {
        return FALSE;
    }
    /*
     * Um unico ciclo de lock para reserve+commit. Chamar os involucros
     * publicos em sequencia deixaria uma janela entre a reserva e o commit na
     * qual outro contexto poderia comitar uma fence maior e disparar o gate de
     * ordem — atalho nao pode ser menos atomico que o caminho longo.
     */
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }
    ok = Bc250WddmFenceTrackerReserveUnlocked(
        Tracker, SubmissionFenceId, NodeOrdinal, EngineOrdinal, &token);
    if (ok) {
        ok = Bc250WddmFenceTrackerCommitUnlocked(Tracker, token, RingFence);
        if (!ok) {
            (void)Bc250WddmFenceTrackerCancelUnlocked(Tracker, token);
        }
    }
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return ok;
}

_Check_return_
uint32_t Bc250WddmFenceTrackerDrainEx(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint64_t RetiredMecFence,
    _In_ uint64_t RetiredGfxFence,
    _Out_writes_to_(Max, return) BC250_WDDM_FENCE_ENTRY *Out,
    _In_ uint32_t Max)
{
    KIRQL irql;
    uint32_t produced = 0u;

    if (Tracker == NULL || Out == NULL || Max == 0u) {
        return 0u;
    }
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return 0u;
    }

    while (produced < Max && Tracker->Count > 0u) {
        BC250_WDDM_FENCE_ENTRY *entry = &Tracker->Entries[Tracker->Head];
        uint64_t retired = (entry->NodeOrdinal == 1u) ? RetiredGfxFence : RetiredMecFence;

        if (entry->RingFence > retired) {
            break;
        }
        Out[produced] = *entry;
        ++produced;
        Tracker->Head = (Tracker->Head + 1u) % BC250_WDDM_FENCE_TRACK_DEPTH;
        --Tracker->Count;
        ++Tracker->NotifiedTotal;
        Tracker->LastNotifiedFenceId = entry->SubmissionFenceId;
        if (entry->NodeOrdinal <= BC250_SUBMISSION_NODE_COMPUTE) {
            Tracker->LastNotifiedFenceIdByNode[entry->NodeOrdinal] =
                entry->SubmissionFenceId;
        }
    }
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return produced;
}

uint32_t Bc250WddmFenceTrackerDrain(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint64_t RetiredRingFence,
    _Out_writes_to_(Max, return) BC250_WDDM_FENCE_ENTRY *Out,
    _In_ uint32_t Max)
{
    return Bc250WddmFenceTrackerDrainEx(Tracker, RetiredRingFence, RetiredRingFence, Out, Max);
}

_Check_return_
BOOLEAN Bc250WddmFenceTrackerQueryCurrent(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t NodeOrdinal,
    _Out_ uint32_t *SubmissionFenceId)
{
    KIRQL irql;

    if (SubmissionFenceId != NULL) *SubmissionFenceId = 0u;
    if (Tracker == NULL || SubmissionFenceId == NULL ||
        NodeOrdinal > BC250_SUBMISSION_NODE_COMPUTE) {
        return FALSE;
    }
    if (!Bc250FenceLockAcquire(&Tracker->Lock, &irql)) {
        return FALSE;
    }
    *SubmissionFenceId = Tracker->LastNotifiedFenceIdByNode[NodeOrdinal];
    Bc250FenceLockRelease(&Tracker->Lock, irql);
    return TRUE;
}

NTSTATUS Bc250SubmissionEngineOffline(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_SUBMISSION_ENGINE *engine;
    BC250_HQD_SELECTOR_INTERFACE interfaceData;
    BC250_HQD_ACTIVE_STATUS hqdStatus = BC250_HQD_ACTIVE_OK;
    LONG previous;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    engine = &Context->SubmissionEngine;

    previous = InterlockedExchange(&engine->State,
        BC250_SUBMISSION_ENGINE_OFFLINE);
    if (previous == BC250_SUBMISSION_ENGINE_OFFLINE &&
        engine->Signature != BC250_SUBMISSION_ENGINE_SIGNATURE) {
        return STATUS_SUCCESS;
    }

    if (engine->HqdState.QueueIsActive != 0u && engine->MmioMapped != 0u) {
        RtlZeroMemory(&interfaceData, sizeof(interfaceData));
        interfaceData.Read32 = Bc250MmioTopologyRead32;
        interfaceData.Write32 = Bc250MmioTopologyWrite32;
        interfaceData.Barrier = Bc250SubmissionBarrier;
        interfaceData.Context = &Context->Core.MmioTransport;

        RtlZeroMemory(&engine->TeardownEvidence, sizeof(engine->TeardownEvidence));
        engine->TeardownEvidence.SelectedSelector = engine->SelectedSelector != 0u ?
            engine->SelectedSelector : 0x00000008u;
        engine->TeardownEvidence.OriginalSelector = engine->OriginalSelector;
        engine->TeardownEvidence.ActiveConfirmed = 1u;

        hqdStatus = Bc250ComputeDeactivatePersistentHqd(
            &interfaceData, &engine->Buffers, &engine->TeardownEvidence,
            &engine->HqdState);
    }
    engine->TeardownStatus = (uint32_t)hqdStatus;

    Bc250SubmissionEngineReleaseResources(Context, engine);
    return hqdStatus == BC250_HQD_ACTIVE_OK ?
        STATUS_SUCCESS : STATUS_DEVICE_HARDWARE_ERROR;
}

_Check_return_
NTSTATUS Bc250SubmissionEngineSelfTest(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_SUBMISSION_ENGINE *engine;
    BC250_SUBMISSION_SELFTEST_EVIDENCE *ev;
    BC250_COMPUTE_DISPATCH_DESCRIPTOR descriptor;
    BC250_COMPUTE_DISPATCH_PLAN_EVIDENCE planEvidence;
    BC250_COMPUTE_DISPATCH_PLAN_STATUS planStatus;
    const BC250_OBSERVABLE_SHADER_MANIFEST *manifest;
    volatile uint32_t *shaderPtr;
    volatile uint32_t *resultPtr;
    volatile uint32_t *dispatchFencePtr;
    volatile uint32_t *ibPtr;
    uint32_t ibStaging[BC250_SUBMISSION_IB_DWORDS];
    uint64_t ibGpuAddress;
    uint64_t assignedFence = 0ull;
    uint32_t index;
    uint32_t poll;
    uint32_t byteIndex;
    uint32_t retiredNow = 0u;
    NTSTATUS status;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    engine = &Context->SubmissionEngine;
    ev = &engine->SelfTest;
    RtlZeroMemory(ev, sizeof(*ev));

    if (InterlockedCompareExchange(&engine->State,
            BC250_SUBMISSION_ENGINE_ONLINE,
            BC250_SUBMISSION_ENGINE_ONLINE) !=
        BC250_SUBMISSION_ENGINE_ONLINE) {
        return STATUS_DEVICE_NOT_READY;
    }
    ev->Attempted = 1u;

    manifest = Bc250GetObservableShaderManifest();
    if (manifest == NULL ||
        Bc250ValidateObservableShaderManifest(manifest) == 0) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    shaderPtr = (volatile uint32_t *)engine->Buffers.ShaderVirtual;
    resultPtr = (volatile uint32_t *)engine->Buffers.ResultVirtual;
    dispatchFencePtr = (volatile uint32_t *)
        ((PUCHAR)engine->Mapped + BC250_SUBMISSION_DISPATCH_FENCE_OFFSET);
    ibPtr = (volatile uint32_t *)
        ((PUCHAR)engine->Mapped + BC250_SUBMISSION_IB_OFFSET);
    ibGpuAddress = engine->BasePhysical + BC250_SUBMISSION_IB_OFFSET;

    /* 1. Encena o shader allowlisted e envenena resultado e fence. */
    for (index = 0u; index < manifest->ShaderDwordCount; ++index) {
        shaderPtr[index] = manifest->ShaderDwords[index];
    }
    resultPtr[0] = BC250_DISPATCH_RESULT_POISON;
    dispatchFencePtr[0] = BC250_DISPATCH_FENCE_POISON;
    KeMemoryBarrier();

    ev->ShaderStagedPassed = 1u;
    for (index = 0u; index < manifest->ShaderDwordCount; ++index) {
        if (shaderPtr[index] != manifest->ShaderDwords[index]) {
            ev->ShaderStagedPassed = 0u;
        }
    }
    ev->ResultBefore = resultPtr[0];
    ev->DispatchFenceBefore = dispatchFencePtr[0];
    if (ev->ShaderStagedPassed == 0u) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    /* 2. Monta a sequencia de dispatch provada dentro do Indirect Buffer. */
    RtlZeroMemory(&descriptor, sizeof(descriptor));
    RtlZeroMemory(&planEvidence, sizeof(planEvidence));
    RtlZeroMemory(ibStaging, sizeof(ibStaging));

    descriptor.ShaderGpuAddress = engine->Buffers.ShaderGpuAddress;
    descriptor.ResultGpuAddress = engine->Buffers.ResultGpuAddress;
    descriptor.FenceGpuAddress = engine->BasePhysical + BC250_SUBMISSION_DISPATCH_FENCE_OFFSET;
    descriptor.PgmRsrc1 = manifest->PgmRsrc1;
    descriptor.PgmRsrc2 = manifest->PgmRsrc2;
    descriptor.WaveSize = manifest->WaveSize;
    descriptor.NumThreadX = 1u;
    descriptor.NumThreadY = 1u;
    descriptor.NumThreadZ = 1u;
    descriptor.GridDimX = 1u;
    descriptor.GridDimY = 1u;
    descriptor.GridDimZ = 1u;
    descriptor.ResultPoison = BC250_DISPATCH_RESULT_POISON;
    descriptor.ResultExpected = BC250_DISPATCH_RESULT_VALUE;
    descriptor.FencePoison = BC250_DISPATCH_FENCE_POISON;
    descriptor.FenceExpected = BC250_DISPATCH_FENCE_VALUE;

    planStatus = Bc250BuildObservableComputeDispatchPlan(
        ibStaging, BC250_SUBMISSION_IB_DWORDS, &descriptor, &planEvidence);
    ev->PlanStatus = (uint32_t)planStatus;
    if (planStatus != BC250_COMPUTE_DISPATCH_PLAN_OK ||
        planEvidence.SubmittedDwords == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    ev->IbDwords = planEvidence.SubmittedDwords;
    ev->IbGpuAddressLow = (uint32_t)(ibGpuAddress & 0xFFFFFFFFull);

    for (index = 0u; index < planEvidence.SubmittedDwords; ++index) {
        ibPtr[index] = ibStaging[index];
    }
    KeMemoryBarrier();

    ev->IbReadbackPassed = 1u;
    for (index = 0u; index < planEvidence.SubmittedDwords; ++index) {
        if (ibPtr[index] != ibStaging[index]) {
            ev->IbReadbackPassed = 0u;
        }
    }
    if (ev->IbReadbackPassed == 0u) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    /*
     * 3. Submete o IB staged pelo descritor PACKET3_INDIRECT_BUFFER.
     *
     * O candidato 3.57 copiou estes 45 dwords para o ring primario via
     * SubmitDirect. O silicio consumiu exatamente 45 dwords (RPTR=45), mas
     * nao executou o dispatch nem o RELEASE_MEM de sequencia. A P1 provada
     * usa este bloco como IB, com INDIRECT_BUFFER_VALID, VMID e tamanho no
     * descritor de quatro dwords; preserve aquela mesma fronteira.
     */
    if (engine->Props.InitialWptrDwords != 0u) {
        status = STATUS_SUCCESS;
        assignedFence = 1ull;
    } else {
        status = Bc250SubmissionEngineSubmit(Context,
            BC250_SUBMISSION_NODE_COMPUTE, ibGpuAddress,
            planEvidence.SubmittedDwords, &assignedFence);
    }
    ev->SubmitStatus = (uint32_t)status;
    ev->AssignedFence = (uint32_t)assignedFence;
    if (!NT_SUCCESS(status)) {
        return status;
    }
    ev->RingWptrAfter = engine->ComputeRing.WptrDwords;

    /*
     * Captura e auditoria dos 12 DWORDs reais do anel primario. O 3.57
     * comparava por engano o descritor do ring contra os comandos do IB;
     * aqui a expectativa volta a representar INDIRECT_BUFFER + RELEASE_MEM.
     */
    if (engine->ComputeRingVirtual != NULL) {
        for (index = 0u; index < 12u; ++index) {
            ev->RingDwordsReal[index] = engine->ComputeRingVirtual[index];
        }
    }
    ev->RingDwordsExpected[0] =
        BC250_PM4_TYPE3_HEADER(BC250_PM4_IT_INDIRECT_BUFFER, 3u);
    ev->RingDwordsExpected[1] = (uint32_t)(ibGpuAddress & 0xFFFFFFFFull);
    ev->RingDwordsExpected[2] = (uint32_t)(ibGpuAddress >> 32) & 0xFFFFu;
    ev->RingDwordsExpected[3] =
        (planEvidence.SubmittedDwords & BC250_PM4_IB_SIZE_MASK) |
        BC250_PM4_IB_VALID_BIT;
    ev->RingDwordsExpected[4] = BC250_PM4_RELEASE_MEM_HEADER_PROVEN;
    ev->RingDwordsExpected[5] = BC250_PM4_RELEASE_MEM_EVENT_PROVEN;
    ev->RingDwordsExpected[6] = BC250_PM4_RELEASE_MEM_DATA_SEL_32;
    ev->RingDwordsExpected[7] =
        (uint32_t)(engine->Buffers.FenceGpuAddress & 0xFFFFFFFFull);
    ev->RingDwordsExpected[8] =
        (uint32_t)(engine->Buffers.FenceGpuAddress >> 32);
    ev->RingDwordsExpected[9] = (uint32_t)assignedFence;
    ev->RingDwordsExpected[10] = 0u;
    ev->RingDwordsExpected[11] = 0u;
    ev->RingDwordsMismatchCount = 0u;
    for (index = 0u; index < 12u; ++index) {
        if (ev->RingDwordsReal[index] != ev->RingDwordsExpected[index]) {
            ++ev->RingDwordsMismatchCount;
        }
    }

    /* Candidato 3.53: leitura dos registradores base do HQD */
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->SelectedSelector);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_PQ_BASE_LO, &ev->HqdPqBaseLo);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_PQ_BASE_HI, &ev->HqdPqBaseHi);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_RPTR_REPORT_LO, &ev->HqdRptrReportAddrLo);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_RPTR_REPORT_HI, &ev->HqdRptrReportAddrHi);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_WPTR_POLL_LO, &ev->HqdWptrPollAddrLo);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_WPTR_POLL_HI, &ev->HqdWptrPollAddrHi);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_VMID, &ev->HqdVmid);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_MQD_CONTROL, &ev->HqdMqdControl);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_IB_CONTROL, &ev->HqdIbControl);
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
    (void)Bc250SubmissionWrite32(engine, Context,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->OriginalSelector);

    /* Candidato 3.53: estado pos-submit de microengine */
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_CP_CPC_STATUS, &ev->PostCpcStatus);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &ev->PostCpcBusyStat);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_CP_CPC_STALLED_STAT1, &ev->PostCpcStalledStat1);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_CP_CPF_STATUS, &ev->PostCpfStatus);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &ev->PostCpfBusyStat);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &ev->PostCpfStalledStat1);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_CP_STAT, &ev->PostCpStat);
    (void)Bc250SubmissionRead32(engine, Context,
        BC250_HQD_MMIO_CP_STALLED_STAT1, &ev->PostCpStalledStat1);
    (void)Bc250SubmissionRead32(engine, Context,
        0x000086A4u, &ev->PostMecPc);

    /*
     * 4. Espera a GPU.  Nenhuma escrita da CPU acontece daqui em diante: se o
     * silicio nao executar, os valores continuam envenenados e o veredito
     * reprova.  E a regra do tripe, sem atalho.
     */
    for (poll = 0u; poll < BC250_SUBMISSION_SELFTEST_POLLS; ++poll) {
        ev->Polls = poll + 1u;
        KeMemoryBarrier();
        ev->DispatchFenceAfter = dispatchFencePtr[0];
        ev->ResultAfter = resultPtr[0];
        if (ev->DispatchFenceAfter == BC250_DISPATCH_FENCE_VALUE &&
            ev->ResultAfter == BC250_DISPATCH_RESULT_VALUE) {
            break;
        }
    }
    Bc250SubmissionCaptureQueue(Context, engine, &engine->SnapshotAfterSubmit);

    /*
     * 5. Re-arme controlado — somente se a GPU nao consumiu nada.
     *
     * O 3.44 terminou aqui com WPTR=12 no registrador, doorbell tocado no
     * offset certo e RPTR parado em zero. A hipotese e' que este silicio
     * amostra o WPTR na BORDA de CP_HQD_ACTIVE: o caminho de tiro unico
     * funciona porque publica o cursor ANTES de ativar, e o polling continuo
     * (CP_PQ_WPTR_POLL_CNTL.EN) nao e' gravavel aqui.
     *
     * O 3.49 revelou a omissao no experimento: CNTL1 vale 1 no readback do
     * load e auto-limpa para 0 depois de ACTIVE. Logo um simples off/on nao
     * reproduz o one-shot vencedor. O 3.50 desativa, rearma QUEUE_MASK com
     * readback e so entao reativa a MESMA fila, com o MESMO anel e cursor.
     * Nenhuma fence ou RPTR e' escrita pela CPU.
     */
    if (ev->DispatchFenceAfter != BC250_DISPATCH_FENCE_VALUE) {
        uint32_t activeValue = 0u;

        ev->RearmAttempted = 1u;
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->SelectedSelector);
        (void)Bc250SubmissionRead32(engine, Context,
            BC250_HQD_MMIO_ACTIVE, &ev->RearmActiveBefore);

        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_HQD_MMIO_ACTIVE, 0u);
        for (poll = 0u; poll < BC250_SUBMISSION_ACTIVE_POLLS; ++poll) {
            ev->RearmDeactivatePolls = poll + 1u;
            if (!Bc250SubmissionRead32(engine, Context,
                    BC250_HQD_MMIO_ACTIVE, &activeValue)) {
                break;
            }
            if ((activeValue & 1u) == 0u) break;
        }
        ev->RearmActiveAfterOff = activeValue;

        ev->RearmQueueMaskApplied =
            1u << engine->ActivateEvidence.QueueMaskBit;
        if (Bc250SubmissionWrite32(engine, Context,
                BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1,
                ev->RearmQueueMaskApplied) != 0u &&
            Bc250SubmissionRead32(engine, Context,
                BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1,
                &ev->RearmQueueMaskReadback) != 0u &&
            (ev->RearmQueueMaskReadback & ev->RearmQueueMaskApplied) ==
                ev->RearmQueueMaskApplied) {
            ev->RearmQueueMaskPassed = 1u;
            (void)Bc250SubmissionWrite32(engine, Context,
                BC250_HQD_MMIO_ACTIVE, 1u);
            (void)Bc250SubmissionRead32(engine, Context,
                BC250_HQD_MMIO_ACTIVE, &ev->RearmActiveAfterOn);
        }
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->OriginalSelector);
        KeMemoryBarrier();

        for (poll = 0u; poll < BC250_SUBMISSION_REARM_POLLS; ++poll) {
            ev->RearmPolls = poll + 1u;
            KeMemoryBarrier();
            ev->RearmDispatchFence = dispatchFencePtr[0];
            ev->RearmResult = resultPtr[0];
            if (ev->RearmDispatchFence == BC250_DISPATCH_FENCE_VALUE &&
                ev->RearmResult == BC250_DISPATCH_RESULT_VALUE) {
                break;
            }
        }
        if (engine->ComputeFenceVirtual != NULL) {
            ev->RearmRingFence = engine->ComputeFenceVirtual[0];
        }
        if (engine->ComputeRptrReportVirtual != NULL) {
            ev->RearmRptrReport = engine->ComputeRptrReportVirtual[0];
        }
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->SelectedSelector);
        (void)Bc250SubmissionRead32(engine, Context,
            BC250_HQD_MMIO_PQ_RPTR, &ev->RearmRptrMmio);
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_INDEX_BYTE_OFFSET, BC250_GRBM_GFX_INDEX_BROADCAST);
        (void)Bc250SubmissionWrite32(engine, Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, engine->OriginalSelector);

        ev->RearmCompleted =
            (ev->RearmDispatchFence == BC250_DISPATCH_FENCE_VALUE &&
             ev->RearmResult == BC250_DISPATCH_RESULT_VALUE) ? 1u : 0u;

        Bc250SubmissionCaptureQueue(Context, engine,
            &engine->SnapshotAfterRearm);

        /* O que a GPU gravou depois do re-arme e' o resultado que vale. */
        if (ev->RearmCompleted != 0u) {
            ev->DispatchFenceAfter = ev->RearmDispatchFence;
            ev->ResultAfter = ev->RearmResult;
        }
    }

    (void)Bc250SubmissionEnginePoll(Context, &retiredNow);
    ev->RetiredCount = retiredNow;
    ev->RingFenceObserved = engine->LastFenceObserved;
    ev->RptrMmioAfter = engine->LastRptrMmio;
    ev->RptrReportAfter = engine->LastRptrReportMem;

    for (byteIndex = 0u; byteIndex < sizeof(uint32_t); ++byteIndex) {
        const uint32_t shift = byteIndex * 8u;
        if (((ev->ResultAfter >> shift) & 0xFFu) !=
            ((BC250_DISPATCH_RESULT_VALUE >> shift) & 0xFFu)) {
            ++ev->ResultMismatches;
        }
    }

    /*
     * Tripe estrito: cursor drenado, as DUAS fences gravadas pela GPU e o
     * resultado do shader conferido byte a byte.
     */
    ev->TriadPassed =
        (ev->RptrReportAfter == ev->RingWptrAfter &&
         ev->DispatchFenceBefore == BC250_DISPATCH_FENCE_POISON &&
         ev->DispatchFenceAfter == BC250_DISPATCH_FENCE_VALUE &&
         ev->RingFenceObserved == ev->AssignedFence &&
         ev->ResultBefore == BC250_DISPATCH_RESULT_POISON &&
         ev->ResultMismatches == 0u) ? 1u : 0u;
    ev->PhysicalReady =
        (ev->TriadPassed != 0u && ev->RetiredCount == 1u) ? 1u : 0u;

    return ev->PhysicalReady != 0u ?
        STATUS_SUCCESS : STATUS_DEVICE_HARDWARE_ERROR;
}

_Check_return_
NTSTATUS Bc250SubmissionEngineP2StreamTest(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    BC250_SUBMISSION_ENGINE *engine;
    BC250_SUBMISSION_P2_STREAM_EVIDENCE *ev;
    const BC250_OBSERVABLE_SHADER_MANIFEST *manifest;
    uint32_t batchIdx;
    uint32_t dwordIdx;
    uint32_t retiredNow = 0u;
    uint32_t poll;
    uint64_t assignedFence = 0ull;
    uint32_t initialRetired;
    uint32_t expectedTotalRetire;
    NTSTATUS status = STATUS_SUCCESS;

    if (Context == NULL || Context->Signature != BC250_ACTIVE_SIGNATURE) {
        return STATUS_INVALID_PARAMETER;
    }
    engine = &Context->SubmissionEngine;
    ev = &engine->P2Stream;
    RtlZeroMemory(ev, sizeof(*ev));

    if (InterlockedCompareExchange(&engine->State,
            BC250_SUBMISSION_ENGINE_ONLINE,
            BC250_SUBMISSION_ENGINE_ONLINE) !=
        BC250_SUBMISSION_ENGINE_ONLINE) {
        return STATUS_DEVICE_NOT_READY;
    }

    manifest = Bc250GetObservableShaderManifest();
    if (manifest == NULL ||
        Bc250ValidateObservableShaderManifest(manifest) == 0) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    ev->Attempted = 1u;
    ev->BatchCount = BC250_P2_STREAM_BATCH_COUNT;
    initialRetired = engine->RetiredTotal;
    expectedTotalRetire = initialRetired + BC250_P2_STREAM_BATCH_COUNT;

    /* 1. Submete os 16 lotes sequenciais pelo mesmo anel persistente */
    for (batchIdx = 0u; batchIdx < BC250_P2_STREAM_BATCH_COUNT; ++batchIdx) {
        BC250_COMPUTE_DISPATCH_DESCRIPTOR descriptor;
        BC250_COMPUTE_DISPATCH_PLAN_EVIDENCE planEvidence;
        BC250_COMPUTE_DISPATCH_PLAN_STATUS planStatus;
        uint32_t ibStaging[BC250_SUBMISSION_IB_DWORDS];
        volatile uint32_t *ibPtr;
        volatile uint32_t *resultSlotPtr;
        volatile uint32_t *dispatchFenceSlotPtr;
        uint64_t ibGpuAddress;
        uint64_t resultGpuAddress;
        uint64_t dispatchFenceGpuAddress;
        uint32_t resultPoison;
        uint32_t dispatchFencePoison;
        uint32_t ibOffset = BC250_SUBMISSION_IB_OFFSET + (batchIdx * 256u);
        uint32_t resultOffset = 0x5000u + (batchIdx * 4u);
        uint32_t dispatchFenceOffset = BC250_SUBMISSION_DISPATCH_FENCE_OFFSET + (batchIdx * 8u);

        resultPoison = 0x43DA0000u | (batchIdx + 1u);
        dispatchFencePoison = 0xA55A0000u | (batchIdx + 1u);

        resultSlotPtr = (volatile uint32_t *)((PUCHAR)engine->Mapped + resultOffset);
        dispatchFenceSlotPtr = (volatile uint32_t *)((PUCHAR)engine->Mapped + dispatchFenceOffset);
        ibPtr = (volatile uint32_t *)((PUCHAR)engine->Mapped + ibOffset);

        ibGpuAddress = engine->BasePhysical + (uint64_t)ibOffset;
        resultGpuAddress = engine->BasePhysical + (uint64_t)resultOffset;
        dispatchFenceGpuAddress = engine->BasePhysical + (uint64_t)dispatchFenceOffset;

        /* Envenena os slots deste lote */
        resultSlotPtr[0] = resultPoison;
        dispatchFenceSlotPtr[0] = dispatchFencePoison;
        KeMemoryBarrier();

        ev->ResultsBefore[batchIdx] = resultSlotPtr[0];
        ev->DispatchFencesBefore[batchIdx] = dispatchFenceSlotPtr[0];

        /* Monta o Indirect Buffer para este lote apontando para seus slots exclusivos */
        RtlZeroMemory(&descriptor, sizeof(descriptor));
        RtlZeroMemory(&planEvidence, sizeof(planEvidence));
        RtlZeroMemory(ibStaging, sizeof(ibStaging));

        descriptor.ShaderGpuAddress = engine->Buffers.ShaderGpuAddress;
        descriptor.ResultGpuAddress = resultGpuAddress;
        descriptor.FenceGpuAddress = dispatchFenceGpuAddress;
        descriptor.PgmRsrc1 = manifest->PgmRsrc1;
        descriptor.PgmRsrc2 = manifest->PgmRsrc2;
        descriptor.WaveSize = manifest->WaveSize;
        descriptor.NumThreadX = 1u;
        descriptor.NumThreadY = 1u;
        descriptor.NumThreadZ = 1u;
        descriptor.GridDimX = 1u;
        descriptor.GridDimY = 1u;
        descriptor.GridDimZ = 1u;
        descriptor.ResultPoison = resultPoison;
        descriptor.ResultExpected = BC250_DISPATCH_RESULT_VALUE;
        descriptor.FencePoison = dispatchFencePoison;
        descriptor.FenceExpected = BC250_DISPATCH_FENCE_VALUE;

        planStatus = Bc250BuildObservableComputeDispatchPlan(
            ibStaging, BC250_SUBMISSION_IB_DWORDS, &descriptor, &planEvidence);
        if (planStatus != BC250_COMPUTE_DISPATCH_PLAN_OK ||
            planEvidence.SubmittedDwords == 0u) {
            return STATUS_INVALID_PARAMETER;
        }

        for (dwordIdx = 0u; dwordIdx < planEvidence.SubmittedDwords; ++dwordIdx) {
            ibPtr[dwordIdx] = ibStaging[dwordIdx];
        }
        KeMemoryBarrier();

        status = Bc250SubmissionEngineSubmit(Context,
            BC250_SUBMISSION_NODE_COMPUTE, ibGpuAddress,
            planEvidence.SubmittedDwords, &assignedFence);
        if (!NT_SUCCESS(status)) {
            break;
        }
        ++ev->SubmissionsAccepted;
    }

    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* 2. Aguarda a GPU drenar e aposentar os 16 lotes */
    for (poll = 0u; poll < BC250_SUBMISSION_SELFTEST_POLLS; ++poll) {
        KeMemoryBarrier();
        (void)Bc250SubmissionEnginePoll(Context, &retiredNow);
        if (engine->RetiredTotal >= expectedTotalRetire) {
            break;
        }
    }

    ev->SubmissionsRetired = engine->RetiredTotal - initialRetired;
    ev->LastFenceObserved = engine->LastFenceObserved;
    ev->WrapCount = engine->ComputeRing.WrapCount;
    ev->FinalWptr = engine->ComputeRing.WptrDwords;
    ev->FinalRptrReport = engine->LastRptrReportMem;
    ev->FinalRptrMmio = engine->LastRptrMmio;

    /* 3. Confere todos os 16 resultados e dispatch fences */
    for (batchIdx = 0u; batchIdx < BC250_P2_STREAM_BATCH_COUNT; ++batchIdx) {
        uint32_t resultOffset = 0x5000u + (batchIdx * 4u);
        uint32_t dispatchFenceOffset = BC250_SUBMISSION_DISPATCH_FENCE_OFFSET + (batchIdx * 8u);
        volatile uint32_t *resultSlotPtr = (volatile uint32_t *)((PUCHAR)engine->Mapped + resultOffset);
        volatile uint32_t *dispatchFenceSlotPtr = (volatile uint32_t *)((PUCHAR)engine->Mapped + dispatchFenceOffset);

        ev->ResultsAfter[batchIdx] = resultSlotPtr[0];
        ev->DispatchFencesAfter[batchIdx] = dispatchFenceSlotPtr[0];

        if (ev->ResultsAfter[batchIdx] != BC250_DISPATCH_RESULT_VALUE) {
            ++ev->ResultMismatches;
        }
        if (ev->DispatchFencesAfter[batchIdx] != BC250_DISPATCH_FENCE_VALUE) {
            ++ev->ResultMismatches;
        }
    }

    /* 4. Valida o tripe fisico da Fase P2 */
    if (ev->SubmissionsRetired == BC250_P2_STREAM_BATCH_COUNT &&
        ev->ResultMismatches == 0u &&
        ev->FinalRptrReport == ev->FinalWptr) {
        ev->TriadPassed = 1u;
        ev->PhysicalReady = 1u;
    }

    return STATUS_SUCCESS;
}

VOID Bc250SubmissionEngineRecordTelemetry(
    _In_ const struct BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    const BC250_SUBMISSION_ENGINE *engine;

    if (Context == NULL) return;
    engine = &Context->SubmissionEngine;

    /* Fase V2 — retirada de fence para o dxgkrnl. */
    Bc250ProbeRecord(BC250_PROBE_V2_RETIREMENT_DPC_RUNS,
        Context->RetirementDpcRuns);
    Bc250ProbeRecord(BC250_PROBE_V2_POLL_FAILURES,
        Context->RetirementPollFailures);
    Bc250ProbeRecord(BC250_PROBE_V2_NOTIFY_BATCHES,
        Context->RetirementNotifyBatches);
    Bc250ProbeRecord(BC250_PROBE_V2_NOTIFIED_TOTAL,
        Context->RetirementNotifiedTotal);
    Bc250ProbeRecord(BC250_PROBE_V2_PUSH_FAILURES,
        Context->RetirementPushFailures);
    Bc250ProbeRecord(BC250_PROBE_V2_BACKPRESSURE,
        Context->RetirementBackpressure);
    Bc250ProbeRecord(BC250_PROBE_V2_INTERRUPTS_SEEN,
        Context->InterruptsSeen);
    Bc250ProbeRecord(BC250_PROBE_V2_FENCES_IN_FLIGHT,
        engine->WddmFences.Count);
    Bc250ProbeRecord(BC250_PROBE_V2_TRACKER_PUSHED_TOTAL,
        engine->WddmFences.PushedTotal);
    Bc250ProbeRecord(BC250_PROBE_V2_TRACKER_OVERFLOW,
        engine->WddmFences.OverflowCount);
    Bc250ProbeRecord(BC250_PROBE_V2_TRACKER_OUT_OF_ORDER,
        engine->WddmFences.OutOfOrderCount);
    Bc250ProbeRecord(BC250_PROBE_V2_LAST_NOTIFIED_FENCE_ID,
        engine->WddmFences.LastNotifiedFenceId);

    Bc250ProbeRecord(BC250_PROBE_P1_ENGINE_STATE, (ULONG)engine->State);
    Bc250ProbeRecord(BC250_PROBE_P1_ACTIVATE_STATUS, engine->ActivateStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_TEARDOWN_STATUS, engine->TeardownStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SUBMIT_CALLS, engine->SubmitCalls);
    Bc250ProbeRecord(BC250_PROBE_P1_SUBMIT_ACCEPTED, engine->SubmitAccepted);
    Bc250ProbeRecord(BC250_PROBE_P1_SUBMIT_REJECTED_NODE,
        engine->SubmitRejectedNode);
    Bc250ProbeRecord(BC250_PROBE_P1_SUBMIT_REJECTED_PLAN,
        engine->SubmitRejectedPlan);
    Bc250ProbeRecord(BC250_PROBE_P1_SUBMIT_REJECTED_EMIT,
        engine->SubmitRejectedEmit);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_FULL_COUNT, engine->RingFullCount);
    Bc250ProbeRecord(BC250_PROBE_P1_FENCE_REGRESSION_COUNT,
        engine->FenceRegressionCount);
    Bc250ProbeRecord(BC250_PROBE_P1_DOORBELL_KICKS, engine->DoorbellKicks);
    Bc250ProbeRecord(BC250_PROBE_P1_DIRECT_WPTR_WRITES,
        engine->DirectWptrWrites);
    Bc250ProbeRecord(BC250_PROBE_P1_POLL_CALLS, engine->PollCalls);
    Bc250ProbeRecord(BC250_PROBE_P1_RETIRED_TOTAL, engine->RetiredTotal);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_PLAN_PAD_DWORDS,
        engine->LastPlanPadDwords);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_WPTR_PUBLISHED,
        engine->LastWptrPublished);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_WPTR_SHADOW_READBACK,
        engine->LastWptrShadowReadback);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_WPTR_MMIO_READBACK,
        engine->LastWptrMmioReadback);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_RPTR_MMIO, engine->LastRptrMmio);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_RPTR_REPORT_MEM,
        engine->LastRptrReportMem);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_FENCE_OBSERVED,
        engine->LastFenceObserved);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_SUBMIT_STATUS,
        engine->LastSubmitStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_SUBMITTED_FENCE,
        (ULONG)engine->LastSubmittedFence);
    Bc250ProbeRecord(BC250_PROBE_P1_LAST_RETIRED_FENCE,
        (ULONG)engine->LastRetiredFence);
    Bc250ProbeRecord(BC250_PROBE_P1_COHER_HDP_APPLIED,
        engine->CoherHdpApplied);
    Bc250ProbeRecord(BC250_PROBE_P1_COHER_L2_READBACK,
        engine->CoherL2Readback);
    Bc250ProbeRecord(BC250_PROBE_P1_COHER_INVALIDATE_ACK,
        engine->CoherInvalidateAck);
    Bc250ProbeRecord(BC250_PROBE_P1_COHER_INVALIDATE_POLLS,
        engine->CoherInvalidatePolls);
    Bc250ProbeRecord(BC250_PROBE_P1_COHERENCY_PASSED,
        engine->CoherencyPassed);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_WPTR, engine->ComputeRing.WptrDwords);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_RPTR_SHADOW,
        engine->ComputeRing.RptrShadowDwords);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_WRAP_COUNT,
        engine->ComputeRing.WrapCount);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_PAD_DWORDS_TOTAL,
        engine->ComputeRing.PadDwordsTotal);

    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_ATTEMPTED,
        engine->SelfTest.Attempted);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_PLAN_STATUS,
        engine->SelfTest.PlanStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_SUBMIT_STATUS,
        engine->SelfTest.SubmitStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_IB_DWORDS,
        engine->SelfTest.IbDwords);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_IB_GPUVA_LOW,
        engine->SelfTest.IbGpuAddressLow);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_ASSIGNED_FENCE,
        engine->SelfTest.AssignedFence);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_RING_WPTR_AFTER,
        engine->SelfTest.RingWptrAfter);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_RPTR_MMIO_AFTER,
        engine->SelfTest.RptrMmioAfter);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_RPTR_REPORT_AFTER,
        engine->SelfTest.RptrReportAfter);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_DISPATCH_FENCE_BEFORE,
        engine->SelfTest.DispatchFenceBefore);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_DISPATCH_FENCE_AFTER,
        engine->SelfTest.DispatchFenceAfter);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_RESULT_BEFORE,
        engine->SelfTest.ResultBefore);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_RESULT_AFTER,
        engine->SelfTest.ResultAfter);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_RESULT_MISMATCHES,
        engine->SelfTest.ResultMismatches);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_RING_FENCE_OBSERVED,
        engine->SelfTest.RingFenceObserved);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_RETIRED_COUNT,
        engine->SelfTest.RetiredCount);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_POLLS, engine->SelfTest.Polls);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_SHADER_STAGED,
        engine->SelfTest.ShaderStagedPassed);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_IB_READBACK,
        engine->SelfTest.IbReadbackPassed);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_TRIAD_PASSED,
        engine->SelfTest.TriadPassed);
    Bc250ProbeRecord(BC250_PROBE_P1_SELFTEST_PHYSICAL_READY,
        engine->SelfTest.PhysicalReady);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_QUEUE_MASK_APPLIED,
        engine->SelfTest.RearmQueueMaskApplied);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_QUEUE_MASK_READBACK,
        engine->SelfTest.RearmQueueMaskReadback);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_QUEUE_MASK_PASSED,
        engine->SelfTest.RearmQueueMaskPassed);
    Bc250ProbeRecord(BC250_PROBE_P1_FENCE_BASELINE_WRITTEN,
        engine->RingFenceBaselineWritten);
    Bc250ProbeRecord(BC250_PROBE_P1_FENCE_BASELINE_READBACK,
        engine->RingFenceBaselineReadback);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_ACTIVE,
        engine->SnapshotAfterActivate.Active);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_HQSTATUS0,
        engine->SnapshotAfterActivate.HqStatus0);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_PQCONTROL,
        engine->SnapshotAfterActivate.PqControl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_DOORBELLCONTROL,
        engine->SnapshotAfterActivate.DoorbellControl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_CPPQSTATUS,
        engine->SnapshotAfterActivate.CpPqStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_CPCSTATUS,
        engine->SnapshotAfterActivate.CpcStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_HQDERROR,
        engine->SnapshotAfterActivate.HqdError);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_PQWPTRLO,
        engine->SnapshotAfterActivate.PqWptrLo);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_PQRPTR,
        engine->SnapshotAfterActivate.PqRptr);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_MECME2HEADERDUMP,
        engine->SnapshotAfterActivate.MecMe2HeaderDump);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_ACTIVE,
        engine->SnapshotAfterSubmit.Active);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_HQSTATUS0,
        engine->SnapshotAfterSubmit.HqStatus0);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_PQCONTROL,
        engine->SnapshotAfterSubmit.PqControl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_DOORBELLCONTROL,
        engine->SnapshotAfterSubmit.DoorbellControl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_CPPQSTATUS,
        engine->SnapshotAfterSubmit.CpPqStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_CPCSTATUS,
        engine->SnapshotAfterSubmit.CpcStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_HQDERROR,
        engine->SnapshotAfterSubmit.HqdError);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_PQWPTRLO,
        engine->SnapshotAfterSubmit.PqWptrLo);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_PQRPTR,
        engine->SnapshotAfterSubmit.PqRptr);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_MECME2HEADERDUMP,
        engine->SnapshotAfterSubmit.MecMe2HeaderDump);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_ACTIVE,
        engine->SnapshotAfterRearm.Active);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_HQSTATUS0,
        engine->SnapshotAfterRearm.HqStatus0);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_PQCONTROL,
        engine->SnapshotAfterRearm.PqControl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_DOORBELLCONTROL,
        engine->SnapshotAfterRearm.DoorbellControl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_CPPQSTATUS,
        engine->SnapshotAfterRearm.CpPqStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_CPCSTATUS,
        engine->SnapshotAfterRearm.CpcStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_HQDERROR,
        engine->SnapshotAfterRearm.HqdError);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_PQWPTRLO,
        engine->SnapshotAfterRearm.PqWptrLo);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_PQRPTR,
        engine->SnapshotAfterRearm.PqRptr);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_MECME2HEADERDUMP,
        engine->SnapshotAfterRearm.MecMe2HeaderDump);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_ATTEMPTED,
        engine->SelfTest.RearmAttempted);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_ACTIVE_BEFORE,
        engine->SelfTest.RearmActiveBefore);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_DEACTIVATE_POLLS,
        engine->SelfTest.RearmDeactivatePolls);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_ACTIVE_AFTER_OFF,
        engine->SelfTest.RearmActiveAfterOff);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_ACTIVE_AFTER_ON,
        engine->SelfTest.RearmActiveAfterOn);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_POLLS,
        engine->SelfTest.RearmPolls);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_RPTR_MMIO,
        engine->SelfTest.RearmRptrMmio);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_RPTR_REPORT,
        engine->SelfTest.RearmRptrReport);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_DISPATCH_FENCE,
        engine->SelfTest.RearmDispatchFence);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_RESULT,
        engine->SelfTest.RearmResult);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_RING_FENCE,
        engine->SelfTest.RearmRingFence);
    Bc250ProbeRecord(BC250_PROBE_P1_REARM_COMPLETED,
        engine->SelfTest.RearmCompleted);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_CP_MEC_CNTL,
        engine->SnapshotAfterActivate.CpMecCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_CP_ME_CNTL,
        engine->SnapshotAfterActivate.CpMeCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_RLC_SCHEDULERS,
        engine->SnapshotAfterActivate.RlcCpSchedulers);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_WPTR_POLL_CNTL,
        engine->SnapshotAfterActivate.WptrPollCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_ACT_WPTR_POLL_CNTL1,
        engine->SnapshotAfterActivate.WptrPollCntl1);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_CP_MEC_CNTL,
        engine->SnapshotAfterSubmit.CpMecCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_CP_ME_CNTL,
        engine->SnapshotAfterSubmit.CpMeCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_RLC_SCHEDULERS,
        engine->SnapshotAfterSubmit.RlcCpSchedulers);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_WPTR_POLL_CNTL,
        engine->SnapshotAfterSubmit.WptrPollCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_SUB_WPTR_POLL_CNTL1,
        engine->SnapshotAfterSubmit.WptrPollCntl1);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_CP_MEC_CNTL,
        engine->SnapshotAfterRearm.CpMecCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_CP_ME_CNTL,
        engine->SnapshotAfterRearm.CpMeCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_RLC_SCHEDULERS,
        engine->SnapshotAfterRearm.RlcCpSchedulers);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_WPTR_POLL_CNTL,
        engine->SnapshotAfterRearm.WptrPollCntl);
    Bc250ProbeRecord(BC250_PROBE_P1_SNAP_RE_WPTR_POLL_CNTL1,
        engine->SnapshotAfterRearm.WptrPollCntl1);

    /* Candidato 3.53 — observabilidade aprofundada da fila persistente. */
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_0,
        engine->SelfTest.RingDwordsReal[0]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_1,
        engine->SelfTest.RingDwordsReal[1]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_2,
        engine->SelfTest.RingDwordsReal[2]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_3,
        engine->SelfTest.RingDwordsReal[3]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_4,
        engine->SelfTest.RingDwordsReal[4]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_5,
        engine->SelfTest.RingDwordsReal[5]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_6,
        engine->SelfTest.RingDwordsReal[6]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_7,
        engine->SelfTest.RingDwordsReal[7]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_8,
        engine->SelfTest.RingDwordsReal[8]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_9,
        engine->SelfTest.RingDwordsReal[9]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_10,
        engine->SelfTest.RingDwordsReal[10]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORD_11,
        engine->SelfTest.RingDwordsReal[11]);
    Bc250ProbeRecord(BC250_PROBE_P1_RING_DWORDS_MISMATCH,
        engine->SelfTest.RingDwordsMismatchCount);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_PQ_BASE_LO,
        engine->SelfTest.HqdPqBaseLo);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_PQ_BASE_HI,
        engine->SelfTest.HqdPqBaseHi);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_RPTR_REPORT_ADDR_LO,
        engine->SelfTest.HqdRptrReportAddrLo);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_RPTR_REPORT_ADDR_HI,
        engine->SelfTest.HqdRptrReportAddrHi);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_WPTR_POLL_ADDR_LO,
        engine->SelfTest.HqdWptrPollAddrLo);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_WPTR_POLL_ADDR_HI,
        engine->SelfTest.HqdWptrPollAddrHi);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_VMID,
        engine->SelfTest.HqdVmid);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_MQD_CONTROL,
        engine->SelfTest.HqdMqdControl);
    Bc250ProbeRecord(BC250_PROBE_P1_HQD_IB_CONTROL,
        engine->SelfTest.HqdIbControl);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_CPC_STATUS,
        engine->SelfTest.PostCpcStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_CPC_BUSY_STAT,
        engine->SelfTest.PostCpcBusyStat);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_CPC_STALLED_STAT1,
        engine->SelfTest.PostCpcStalledStat1);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_CPF_STATUS,
        engine->SelfTest.PostCpfStatus);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_CPF_BUSY_STAT,
        engine->SelfTest.PostCpfBusyStat);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_CPF_STALLED_STAT1,
        engine->SelfTest.PostCpfStalledStat1);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_CP_STAT,
        engine->SelfTest.PostCpStat);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_CP_STALLED_STAT1,
        engine->SelfTest.PostCpStalledStat1);
    Bc250ProbeRecord(BC250_PROBE_P1_POST_MEC_PC,
        engine->SelfTest.PostMecPc);

    /* Fase P2 — Multi-Submissao Sequencial, Pipelining e Wraparound */
    Bc250ProbeRecord(BC250_PROBE_P2_STREAM_ATTEMPTED,
        engine->P2Stream.Attempted);
    Bc250ProbeRecord(BC250_PROBE_P2_BATCH_COUNT,
        engine->P2Stream.BatchCount);
    Bc250ProbeRecord(BC250_PROBE_P2_SUBMISSIONS_ACCEPTED,
        engine->P2Stream.SubmissionsAccepted);
    Bc250ProbeRecord(BC250_PROBE_P2_SUBMISSIONS_RETIRED,
        engine->P2Stream.SubmissionsRetired);
    Bc250ProbeRecord(BC250_PROBE_P2_WRAP_COUNT,
        engine->P2Stream.WrapCount);
    Bc250ProbeRecord(BC250_PROBE_P2_RESULT_MISMATCHES,
        engine->P2Stream.ResultMismatches);
    Bc250ProbeRecord(BC250_PROBE_P2_LAST_FENCE_OBSERVED,
        engine->P2Stream.LastFenceObserved);
    Bc250ProbeRecord(BC250_PROBE_P2_FINAL_WPTR,
        engine->P2Stream.FinalWptr);
    Bc250ProbeRecord(BC250_PROBE_P2_FINAL_RPTR_MMIO,
        engine->P2Stream.FinalRptrMmio);
    Bc250ProbeRecord(BC250_PROBE_P2_FINAL_RPTR_REPORT,
        engine->P2Stream.FinalRptrReport);
    Bc250ProbeRecord(BC250_PROBE_P2_TRIAD_PASSED,
        engine->P2Stream.TriadPassed);
    Bc250ProbeRecord(BC250_PROBE_P2_PHYSICAL_READY,
        engine->P2Stream.PhysicalReady);
    Bc250ProbeRecord(BC250_PROBE_P2_DOORBELL_WRITE_FAILURES,
        engine->DoorbellWriteFailures);
    Bc250ProbeRecord(BC250_PROBE_P2_DOORBELL_LAST_VALUE_LOW,
        engine->DoorbellLastValueLow);
    Bc250ProbeRecord(BC250_PROBE_P2_DOORBELL_LAST_VALUE_HIGH,
        engine->DoorbellLastValueHigh);
    Bc250ProbeRecord(BC250_PROBE_P2_DOORBELL_CONTROL_BEFORE,
        engine->DoorbellControlBefore);
    Bc250ProbeRecord(BC250_PROBE_P2_DOORBELL_CONTROL_AFTER,
        engine->DoorbellControlAfter);
    Bc250ProbeRecord(BC250_PROBE_P2_DOORBELL_HIT_OBSERVED,
        engine->DoorbellHitObserved);
}
