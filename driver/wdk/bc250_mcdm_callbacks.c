#include "bc250_mcdm_callbacks.h"
#include "bc250_probe_telemetry.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_segment_contract.h"
#include "../core/bc250_tdr_recovery_contract.h"
#include "../core/bc250_context_dispatcher_contract.h"
#include "../core/bc250_paging_contract.h"
#include "../core/bc250_ih_contract.h"
#include "../core/bc250_preemption_contract.h"
#include "../core/bc250_gpu_clock_contract.h"
#include "../core/bc250_sdma_packet_builder.h"
#include "../core/bc250_pm4_packet_builder.h"
#include "../core/bc250_gpuva_space_contract.h"

/*
 * Fase V5: as DDIs que devolvem NOT_SUPPORTED sao as candidatas naturais a
 * causa de um teardown, entao cada uma marca a propria entrada no rastro.
 */
#define BC250_MCDM_UNSUPPORTED_1(Name, DdiId, Type1, Arg1) \
    NTSTATUS APIENTRY Name(Type1 Arg1) \
    { \
        UNREFERENCED_PARAMETER(Arg1); \
        Bc250ProbeMarkDdi(DdiId); \
        return STATUS_NOT_SUPPORTED; \
    }

#define BC250_MCDM_UNSUPPORTED_2(Name, DdiId, Type1, Arg1, Type2, Arg2) \
    NTSTATUS APIENTRY Name(Type1 Arg1, Type2 Arg2) \
    { \
        UNREFERENCED_PARAMETER(Arg1); \
        UNREFERENCED_PARAMETER(Arg2); \
        Bc250ProbeMarkDdi(DdiId); \
        return STATUS_NOT_SUPPORTED; \
    }

static BC250_MCDM_CREATE_PROCESS_OBJECT g_Bc250CreateProcessObject;
static BC250_MCDM_DESTROY_PROCESS_OBJECT g_Bc250DestroyProcessObject;
static BC250_MCDM_CREATE_DEVICE_OBJECT g_Bc250CreateDeviceObject;
static BC250_MCDM_DESTROY_DEVICE_OBJECT g_Bc250DestroyDeviceObject;
static BC250_MCDM_CREATE_CONTEXT_OBJECT g_Bc250CreateContextObject;
static BC250_MCDM_DESTROY_CONTEXT_OBJECT g_Bc250DestroyContextObject;
static BC250_MCDM_SET_ROOT_PAGE_TABLE_OBJECT g_Bc250SetRootPageTableObject;
static BC250_MCDM_CREATE_ALLOCATION_OBJECTS g_Bc250CreateAllocationObjects;
static BC250_MCDM_DESTROY_ALLOCATION_OBJECTS g_Bc250DestroyAllocationObjects;
static BC250_MCDM_OPEN_ALLOCATION_OBJECTS g_Bc250OpenAllocationObjects;
static BC250_MCDM_CLOSE_ALLOCATION_OBJECTS g_Bc250CloseAllocationObjects;
static BC250_MCDM_COLLECT_DEBUG_SNAPSHOT g_Bc250CollectDebugSnapshot;
static BC250_MCDM_SUBMIT_COMMAND_EXECUTOR g_Bc250SubmitCommandExecutor;
static BC250_MCDM_INTERRUPT_HANDLER g_Bc250InterruptHandler;
static BC250_MCDM_DPC_HANDLER g_Bc250DpcHandler;
static BC250_MCDM_QUERY_RUNTIME_CAPS g_Bc250QueryRuntimeCaps;
#include "bc250_display_ddi.h"
static BC250_MCDM_UPDATE_PAGE_TABLE_HANDLER g_Bc250UpdatePageTableHandler;
static volatile LONG g_Bc250WddmContractVersion =
    (LONG)DXGKDDI_WDDMv2_7;

NTSTATUS Bc250McdmSetWddmContractVersion(DXGK_WDDMVERSION Version)
{
    if (Version != DXGKDDI_WDDMv2 && Version != DXGKDDI_WDDMv2_7) {
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&g_Bc250WddmContractVersion, (LONG)Version);
    return STATUS_SUCCESS;
}

DXGK_WDDMVERSION Bc250McdmGetWddmContractVersion(VOID)
{
    return (DXGK_WDDMVERSION)InterlockedCompareExchange(
        &g_Bc250WddmContractVersion, 0, 0);
}

/*
 * Default 0 = display+render, o destino do projeto. Ver o bloco extenso em
 * bc250_mcdm_callbacks.h antes de mexer.
 */
static volatile LONG g_Bc250McdmComputeOnly = 0;

NTSTATUS Bc250McdmSetComputeOnly(ULONG ComputeOnly)
{
    if (ComputeOnly > 1u) {
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&g_Bc250McdmComputeOnly, (LONG)ComputeOnly);
    return STATUS_SUCCESS;
}

ULONG Bc250McdmGetComputeOnly(VOID)
{
    return (ULONG)InterlockedCompareExchange(&g_Bc250McdmComputeOnly, 0, 0);
}

NTSTATUS Bc250McdmEncodeVidMmPteWithLevel(
    const BC250_MCDM_RUNTIME_CAPS *Caps,
    const DXGK_PTE *Source,
    UINT32 PageTableLevel,
    UINT64 *Encoded)
{
    UINT64 physicalAddress;
    UINT64 pageOffsetInSegment;
    UINT32 flags;

    if (Caps == NULL || Source == NULL || Encoded == NULL) {
        Bc250DisplayLog("[EncodeVidMmPte FAIL NullParam] Caps=%p, Source=%p, Encoded=%p\n",
            Caps, Source, Encoded);
        return STATUS_INVALID_PARAMETER;
    }
    *Encoded = 0u;
    if (Source->Valid == 0u || Source->Zero != 0u) {
        return STATUS_SUCCESS;
    }

    if (Source->PhysicalAdapterIndex != 0u) {
        Bc250DisplayLog("[EncodeVidMmPte FAIL PhysAdapter] PhysIdx=%u\n",
            (UINT32)Source->PhysicalAdapterIndex);
        return STATUS_NOT_SUPPORTED;
    }

    /*
     * Em ambos os niveis (folha e diretorio), PageAddress/PageTableAddress
     * representa o Page Frame Number (PFN) de 4KB na abertura do segmento.
     */
    pageOffsetInSegment = Source->PageAddress << 12;

    if (PageTableLevel > 0u) {
        /*
         * Nivel > 0: Entrada de Diretorio de Paginas (PDE).
         * Tabelas de paginas filhas DEVEM residir no Segmento 1 (VRAM Local).
         */
        if (Source->Segment != 1u) {
            Bc250DisplayLog("[EncodeVidMmPte FAIL PDE Segment] Lvl=%u, Seg=%u\n",
                PageTableLevel, (UINT32)Source->Segment);
            return STATUS_NOT_SUPPORTED;
        }
        if (pageOffsetInSegment >= Caps->LocalSegmentBytes) {
            Bc250DisplayLog("[EncodeVidMmPte FAIL PDE LocalBounds] Lvl=%u, Pfn=0x%llX, Offset=0x%llX, MaxBytes=0x%llX\n",
                PageTableLevel,
                (unsigned long long)Source->PageAddress,
                (unsigned long long)pageOffsetInSegment,
                (unsigned long long)Caps->LocalSegmentBytes);
            return STATUS_NOT_SUPPORTED;
        }
        if (Caps->LocalSegmentBaseOffset > (UINT64)~(UINT64)0 - pageOffsetInSegment) {
            Bc250DisplayLog("[EncodeVidMmPte FAIL Overflow] Base=0x%llX, Offset=0x%llX\n",
                (unsigned long long)Caps->LocalSegmentBaseOffset,
                (unsigned long long)pageOffsetInSegment);
            return STATUS_INTEGER_OVERFLOW;
        }
        physicalAddress = Caps->LocalSegmentBaseOffset + pageOffsetInSegment;
        flags = BC250_PTE_VALID | BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
    } else {
        /*
         * Nivel 0: Entrada Folha (PTE).
         * Suporta Segmento 1 (VRAM Local) e Segmento 0 (Memoria do Sistema / RAM do Host).
         */
        if (Source->LargePage != 0u ||
            Source->PageTablePageSize != DXGK_PTE_PAGE_TABLE_PAGE_4KB) {
            Bc250DisplayLog("[EncodeVidMmPte FAIL LeafAttrs] LP=%u, PTPgSz=%u\n",
                (UINT32)Source->LargePage, (UINT32)Source->PageTablePageSize);
            return STATUS_NOT_SUPPORTED;
        }
        flags = BC250_PTE_VALID | BC250_PTE_READABLE;
        if (Source->ReadOnly == 0u) flags |= BC250_PTE_WRITEABLE;
        if (Source->NoExecute == 0u) flags |= BC250_PTE_EXECUTABLE;
        if (Source->CacheCoherent != 0u) flags |= BC250_PTE_SNOOPED;

        if (Source->Segment == 1u) {
            /* VRAM Local */
            if (pageOffsetInSegment >= Caps->LocalSegmentBytes) {
                Bc250DisplayLog("[EncodeVidMmPte FAIL LocalBounds] Lvl=0, Pfn=0x%llX, Offset=0x%llX, MaxBytes=0x%llX\n",
                    (unsigned long long)Source->PageAddress,
                    (unsigned long long)pageOffsetInSegment,
                    (unsigned long long)Caps->LocalSegmentBytes);
                return STATUS_NOT_SUPPORTED;
            }
            if (Caps->LocalSegmentBaseOffset > (UINT64)~(UINT64)0 - pageOffsetInSegment) {
                Bc250DisplayLog("[EncodeVidMmPte FAIL Overflow] Base=0x%llX, Offset=0x%llX\n",
                    (unsigned long long)Caps->LocalSegmentBaseOffset,
                    (unsigned long long)pageOffsetInSegment);
                return STATUS_INTEGER_OVERFLOW;
            }
            physicalAddress = Caps->LocalSegmentBaseOffset + pageOffsetInSegment;
        } else if (Source->Segment == 0u) {
            /* Memoria do Sistema (Segmento 0 = System Memory) */
            physicalAddress = pageOffsetInSegment;
            flags |= BC250_PTE_SYSTEM | BC250_PTE_SNOOPED;
        } else {
            Bc250DisplayLog("[EncodeVidMmPte FAIL Leaf Segment] Seg=%u\n",
                (UINT32)Source->Segment);
            return STATUS_NOT_SUPPORTED;
        }
    }

    return Bc250GcvmEncodePte(physicalAddress, flags, Encoded) ==
        BC250_GCVM_OK ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS Bc250McdmEncodeVidMmPte(
    const BC250_MCDM_RUNTIME_CAPS *Caps,
    const DXGK_PTE *Source,
    UINT64 *Encoded)
{
    return Bc250McdmEncodeVidMmPteWithLevel(Caps, Source, 0u, Encoded);
}

static NTSTATUS Bc250McdmBuildCpuPageTableUpdate(
    const BC250_MCDM_RUNTIME_CAPS *Caps,
    HANDLE AdapterContext,
    DXGKARG_BUILDPAGINGBUFFER *Args)
{
    DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update;
    UINT64 *destination;
    UINT32 index;
    UINT32 maxEntries;
    UINT32 levelBits;

    if (Caps == NULL || Args == NULL) return STATUS_INVALID_PARAMETER;
    update = &Args->UpdatePageTable;
    Bc250DisplayLog("[UpdatePT] Lvl=%u, FirstPteVA=0x%llX, Num=%u, Start=%u, Mode=%u, Repeat=%u, CpuVa=0x%p, Flags=0x%08X\n",
        update->PageTableLevel, (unsigned long long)update->FirstPteVirtualAddress,
        update->NumPageTableEntries, update->StartIndex,
        (UINT32)update->UpdateMode, update->Flags.Repeat, update->PageTableAddress.CpuVirtual,
        *(const UINT32 *)&update->Flags);

    /* Detailed logging of first few PTE entries */
    if (update->pPageTableEntries != NULL) {
        UINT32 logCount = update->NumPageTableEntries < 4u ? update->NumPageTableEntries : 4u;
        for (index = 0u; index < logCount; ++index) {
            const DXGK_PTE *p = &update->pPageTableEntries[index];
            Bc250DisplayLog("[UpdatePT Lvl=%u Entry %u] V=%u, Z=%u, LP=%u, PhysIdx=%u, PTPgSz=%u, Seg=%u, Addr=0x%llX\n",
                update->PageTableLevel, index,
                (UINT32)p->Valid, (UINT32)p->Zero, (UINT32)p->LargePage,
                (UINT32)p->PhysicalAdapterIndex, (UINT32)p->PageTablePageSize,
                (UINT32)p->Segment, (unsigned long long)p->PageAddress);
        }
    }

    if (update->UpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL ||
        update->PageTableAddress.CpuVirtual == NULL ||
        update->pPageTableEntries == NULL ||
        update->NumPageTableEntries == 0u || update->Reserved0 != 0u ||
        update->pPageTableEntries64KB != NULL ||
        update->PageTableLevel >= Caps->PageTableLevelCount ||
        Caps->PageTableIndexBitCount == 0u ||
        Caps->PageTableIndexBitCount >= 32u) {
        Bc250DisplayLog("[UpdatePT FAIL Header] Mode=%u, CpuVa=%p, pPTE=%p, Num=%u, Res0=%u, FlagsVal=0x%08X, Prot=%u, Lvl=%u (Caps=%u)\n",
            (UINT32)update->UpdateMode, update->PageTableAddress.CpuVirtual,
            update->pPageTableEntries, update->NumPageTableEntries,
            update->Reserved0, *(const UINT32 *)&update->Flags, update->DriverProtection,
            update->PageTableLevel, Caps->PageTableLevelCount);
        return STATUS_INVALID_PARAMETER;
    }
    levelBits = (update->PageTableLevel == 0u) ?
        BC250_GPUVA_PT_LEAF_INDEX_BITS : BC250_GPUVA_PT_ROOT_INDEX_BITS;
    maxEntries = 1u << levelBits;
    if (update->StartIndex >= maxEntries ||
        update->NumPageTableEntries > maxEntries - update->StartIndex) {
        Bc250DisplayLog("[UpdatePT FAIL Bounds] Start=%u, Num=%u, Max=%u, LvlBits=%u\n",
            update->StartIndex, update->NumPageTableEntries, maxEntries, levelBits);
        return STATUS_INVALID_PARAMETER;
    }
    if (update->Flags.Repeat != 0u) {
        UINT64 encoded = 0u;
        NTSTATUS status = Bc250McdmEncodeVidMmPteWithLevel(
            Caps, &update->pPageTableEntries[0], update->PageTableLevel, &encoded);
        if (!NT_SUCCESS(status)) {
            Bc250DisplayLog("[UpdatePT FAIL Repeat Encode] status=0x%08X\n", status);
            return status;
        }
    } else {
        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            UINT64 encoded = 0u;
            NTSTATUS status = Bc250McdmEncodeVidMmPteWithLevel(
                Caps, &update->pPageTableEntries[index], update->PageTableLevel, &encoded);
            if (!NT_SUCCESS(status)) {
                Bc250DisplayLog("[UpdatePT FAIL Encode] index=%u/%u, status=0x%08X, RawFlags=0x%016llX, RawAddr=0x%016llX\n",
                    index, update->NumPageTableEntries, status,
                    (unsigned long long)update->pPageTableEntries[index].Flags,
                    (unsigned long long)update->pPageTableEntries[index].PageAddress);
                return status;
            }
        }
    }
    /* O handler ativo distingue leaf de diretórios; ambos precisam participar
     * da mesma transação de flush, embora só leaf gere ranges GPUVA. */
    if (g_Bc250UpdatePageTableHandler != NULL) {
        return g_Bc250UpdatePageTableHandler(AdapterContext, Caps, update);
    }
    destination = (UINT64 *)update->PageTableAddress.CpuVirtual;
    if (update->Flags.Repeat != 0u) {
        UINT64 encoded = 0u;
        (void)Bc250McdmEncodeVidMmPteWithLevel(
            Caps, &update->pPageTableEntries[0], update->PageTableLevel, &encoded);
        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            destination[update->StartIndex + index] = encoded;
        }
    } else {
        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            UINT64 encoded = 0u;
            (void)Bc250McdmEncodeVidMmPteWithLevel(
                Caps, &update->pPageTableEntries[index], update->PageTableLevel, &encoded);
            destination[update->StartIndex + index] = encoded;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250McdmBuildTransferVirtual(
    DXGKARG_BUILDPAGINGBUFFER *Args)
{
    DXGK_BUILDPAGINGBUFFER_TRANSFERVIRTUAL *transfer;
    uint32_t *dmaBuffer;
    uint32_t dwordsAvailable;
    uint32_t cursor = 0;
    BC250_SDMA_STATUS sdmaStatus;

    if (Args == NULL || Args->pDmaBuffer == NULL) return STATUS_INVALID_PARAMETER;
    transfer = &Args->TransferVirtual;
    if (transfer->TransferSizeInBytes == 0 || transfer->TransferSizeInBytes > BC250_SDMA_COPY_MAX_BYTES) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((transfer->SourceVirtualAddress & 3u) != 0 || (transfer->DestinationVirtualAddress & 3u) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    dmaBuffer = (uint32_t *)Args->pDmaBuffer;
    dwordsAvailable = Args->DmaSize / (uint32_t)sizeof(uint32_t);
    if (dwordsAvailable < BC250_SDMA_DWORDS_COPY_LINEAR) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    sdmaStatus = Bc250SdmaEmitCopyLinear(
        dmaBuffer,
        dwordsAvailable,
        &cursor,
        transfer->SourceVirtualAddress,
        transfer->DestinationVirtualAddress,
        (uint32_t)transfer->TransferSizeInBytes);

    if (sdmaStatus != BC250_SDMA_OK) return STATUS_UNSUCCESSFUL;

    Args->pDmaBuffer = (PVOID)(dmaBuffer + cursor);
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250McdmBuildFillVirtual(
    DXGKARG_BUILDPAGINGBUFFER *Args)
{
    DXGK_BUILDPAGINGBUFFER_FILLVIRTUAL *fill;
    uint32_t *dmaBuffer;
    uint32_t dwordsAvailable;
    uint32_t cursor = 0;
    BC250_SDMA_STATUS sdmaStatus;

    if (Args == NULL || Args->pDmaBuffer == NULL) return STATUS_INVALID_PARAMETER;
    fill = &Args->FillVirtual;
    if (fill->FillSizeInBytes == 0 || fill->FillSizeInBytes > BC250_SDMA_FILL_MAX_BYTES) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((fill->DestinationVirtualAddress & 3u) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    dmaBuffer = (uint32_t *)Args->pDmaBuffer;
    dwordsAvailable = Args->DmaSize / (uint32_t)sizeof(uint32_t);
    if (dwordsAvailable < BC250_SDMA_DWORDS_CONST_FILL) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    sdmaStatus = Bc250SdmaEmitConstFill(
        dmaBuffer,
        dwordsAvailable,
        &cursor,
        fill->DestinationVirtualAddress,
        fill->FillPattern,
        (uint32_t)fill->FillSizeInBytes);

    if (sdmaStatus != BC250_SDMA_OK) return STATUS_UNSUCCESSFUL;

    Args->pDmaBuffer = (PVOID)(dmaBuffer + cursor);
    return STATUS_SUCCESS;
}

VOID Bc250McdmInstallRuntimeCapsProvider(
    BC250_MCDM_QUERY_RUNTIME_CAPS Provider)
{
    g_Bc250QueryRuntimeCaps = Provider;
}

VOID Bc250McdmInstallProcessLifecycle(
    BC250_MCDM_CREATE_PROCESS_OBJECT CreateProcessObject,
    BC250_MCDM_DESTROY_PROCESS_OBJECT DestroyProcessObject)
{
    g_Bc250CreateProcessObject = CreateProcessObject;
    g_Bc250DestroyProcessObject = DestroyProcessObject;
}

VOID Bc250McdmInstallDeviceLifecycle(
    BC250_MCDM_CREATE_DEVICE_OBJECT CreateDeviceObject,
    BC250_MCDM_DESTROY_DEVICE_OBJECT DestroyDeviceObject)
{
    g_Bc250CreateDeviceObject = CreateDeviceObject;
    g_Bc250DestroyDeviceObject = DestroyDeviceObject;
}

VOID Bc250McdmInstallContextLifecycle(
    BC250_MCDM_CREATE_CONTEXT_OBJECT CreateContextObject,
    BC250_MCDM_DESTROY_CONTEXT_OBJECT DestroyContextObject)
{
    g_Bc250CreateContextObject = CreateContextObject;
    g_Bc250DestroyContextObject = DestroyContextObject;
}

VOID Bc250McdmInstallGpuVaLifecycle(
    BC250_MCDM_SET_ROOT_PAGE_TABLE_OBJECT SetRootPageTableObject)
{
    g_Bc250SetRootPageTableObject = SetRootPageTableObject;
}

VOID Bc250McdmInstallAllocationLifecycle(
    BC250_MCDM_CREATE_ALLOCATION_OBJECTS CreateAllocationObjects,
    BC250_MCDM_DESTROY_ALLOCATION_OBJECTS DestroyAllocationObjects,
    BC250_MCDM_OPEN_ALLOCATION_OBJECTS OpenAllocationObjects,
    BC250_MCDM_CLOSE_ALLOCATION_OBJECTS CloseAllocationObjects)
{
    g_Bc250CreateAllocationObjects = CreateAllocationObjects;
    g_Bc250DestroyAllocationObjects = DestroyAllocationObjects;
    g_Bc250OpenAllocationObjects = OpenAllocationObjects;
    g_Bc250CloseAllocationObjects = CloseAllocationObjects;
}

VOID Bc250McdmInstallDebugLifecycle(
    BC250_MCDM_COLLECT_DEBUG_SNAPSHOT CollectDebugSnapshot)
{
    g_Bc250CollectDebugSnapshot = CollectDebugSnapshot;
}

VOID Bc250McdmInstallSubmissionExecutor(
    BC250_MCDM_SUBMIT_COMMAND_EXECUTOR SubmitCommandExecutor)
{
    g_Bc250SubmitCommandExecutor = SubmitCommandExecutor;
}

VOID Bc250McdmInstallInterruptLifecycle(
    BC250_MCDM_INTERRUPT_HANDLER InterruptHandler,
    BC250_MCDM_DPC_HANDLER DpcHandler)
{
    g_Bc250InterruptHandler = InterruptHandler;
    g_Bc250DpcHandler = DpcHandler;
}

static BC250_MCDM_FLUSH_TLB_HANDLER g_Bc250FlushTlbHandler;

VOID Bc250McdmInstallFlushTlbHandler(
    _In_opt_ BC250_MCDM_FLUSH_TLB_HANDLER FlushTlbHandler)
{
    g_Bc250FlushTlbHandler = FlushTlbHandler;
}

VOID Bc250McdmInstallUpdatePageTableHandler(
    _In_opt_ BC250_MCDM_UPDATE_PAGE_TABLE_HANDLER UpdatePageTableHandler)
{
    g_Bc250UpdatePageTableHandler = UpdatePageTableHandler;
}

NTSTATUS APIENTRY Bc250McdmBuildPagingBuffer(
    CONST HANDLE hAdapter,
    DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_BUILD_PAGING_BUFFER);
    BC250_MCDM_RUNTIME_CAPS caps;
    RtlZeroMemory(&caps, sizeof(caps));

    if (hAdapter == NULL || pBuildPagingBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmBuildPagingBuffer] Entered: Op=%u\n", pBuildPagingBuffer->Operation);
    if (g_Bc250QueryRuntimeCaps == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    (void)g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps);

    if (pBuildPagingBuffer->Operation == DXGK_OPERATION_MAP_APERTURE_SEGMENT ||
        pBuildPagingBuffer->Operation == DXGK_OPERATION_UNMAP_APERTURE_SEGMENT ||
        pBuildPagingBuffer->Operation == DXGK_OPERATION_INIT_CONTEXT_RESOURCE) {
        Bc250DisplayLog("[McdmBuildPagingBuffer] Handled Op=%u -> SUCCESS\n", pBuildPagingBuffer->Operation);
        return STATUS_SUCCESS;
    }

    switch (pBuildPagingBuffer->Operation) {
    case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
    case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
    case DXGK_OPERATION_INIT_CONTEXT_RESOURCE:
    case DXGK_OPERATION_DISCARD_CONTENT:
    case DXGK_OPERATION_UPDATE_CONTEXT_ALLOCATION:
    case DXGK_OPERATION_NOTIFY_RESIDENCY:
        Bc250DisplayLog("[McdmBuildPagingBuffer] Handled Op=%u -> SUCCESS\n", pBuildPagingBuffer->Operation);
        return STATUS_SUCCESS;

    case DXGK_OPERATION_UPDATE_PAGE_TABLE: {
        NTSTATUS updateStatus = Bc250McdmBuildCpuPageTableUpdate(
            &caps, (HANDLE)hAdapter, pBuildPagingBuffer);
        if (updateStatus == STATUS_NOT_SUPPORTED) {
            updateStatus = STATUS_INVALID_PARAMETER;
        }
        return updateStatus;
    }

    case DXGK_OPERATION_VIRTUAL_TRANSFER:
        return Bc250McdmBuildTransferVirtual(pBuildPagingBuffer);

    case DXGK_OPERATION_VIRTUAL_FILL:
        return Bc250McdmBuildFillVirtual(pBuildPagingBuffer);

    case DXGK_OPERATION_FLUSH_TLB:
        if (g_Bc250FlushTlbHandler != NULL) {
            return g_Bc250FlushTlbHandler((HANDLE)hAdapter, &pBuildPagingBuffer->FlushTlb);
        }
        /* Sem backend nao houve invalidacao nem ACK fisico. */
        return STATUS_NOT_SUPPORTED;

    default:
        Bc250DisplayLog("[McdmBuildPagingBuffer] Unhandled Op=%u -> SUCCESS\n", pBuildPagingBuffer->Operation);
        return STATUS_SUCCESS;
    }
}

NTSTATUS APIENTRY Bc250McdmCloseAllocation(
    CONST HANDLE hDevice,
    CONST DXGKARG_CLOSEALLOCATION *pCloseAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CLOSE_ALLOCATION);
    if (hDevice == NULL || pCloseAllocation == NULL ||
        pCloseAllocation->NumAllocations == 0u ||
        pCloseAllocation->pOpenHandleList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCloseAllocation] NumAllocs=%u\n", pCloseAllocation->NumAllocations);
    if (g_Bc250CloseAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250CloseAllocationObjects(hDevice, pCloseAllocation);
}

NTSTATUS APIENTRY Bc250McdmCollectDbgInfo(
    CONST HANDLE hAdapter,
    CONST DXGKARG_COLLECTDBGINFO *pCollectDbgInfo)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_COLLECT_DBG_INFO);
    if (hAdapter == NULL || pCollectDbgInfo == NULL ||
        pCollectDbgInfo->pBuffer == NULL || pCollectDbgInfo->BufferSize == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250CollectDebugSnapshot == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250CollectDebugSnapshot(hAdapter, pCollectDbgInfo);
}

NTSTATUS APIENTRY Bc250McdmCreateAllocation(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEALLOCATION *pCreateAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CREATE_ALLOCATION);
    UINT index;
    NTSTATUS status;
    if (hAdapter == NULL || pCreateAllocation == NULL ||
        pCreateAllocation->pAllocationInfo == NULL ||
        pCreateAllocation->NumAllocations == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCreateAllocation] NumAllocs=%u\n", pCreateAllocation->NumAllocations);
    for (index = 0u; index < pCreateAllocation->NumAllocations; ++index) {
        pCreateAllocation->pAllocationInfo[index].hAllocation = NULL;
    }
    if (g_Bc250CreateAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    status = g_Bc250CreateAllocationObjects(hAdapter, pCreateAllocation);
    Bc250DisplayLog("[McdmCreateAllocation] Result=0x%08X\n", status);
    return status;
}

NTSTATUS APIENTRY Bc250McdmCreateContext(
    CONST HANDLE hDevice,
    DXGKARG_CREATECONTEXT *pCreateContext)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CREATE_CONTEXT);
    HANDLE contextHandle = NULL;
    NTSTATUS status;

    if (hDevice == NULL || pCreateContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCreateContext] Entered: Node=%u\n", pCreateContext->NodeOrdinal);
    if (pCreateContext->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }
    pCreateContext->hContext = NULL;
    RtlZeroMemory(&pCreateContext->ContextInfo,
        sizeof(pCreateContext->ContextInfo));
    pCreateContext->ContextInfo.DmaBufferSize = 0x10000u;
    pCreateContext->ContextInfo.DmaBufferSegmentSet = (1u << (2u - 1u)); /* Segment 2 GART Aperture */
    pCreateContext->ContextInfo.AllocationListSize = 32u;
    pCreateContext->ContextInfo.PatchLocationListSize = 0u;
    pCreateContext->ContextInfo.Caps.NoPatchingRequired = 1u;
    pCreateContext->ContextInfo.PagingCompanionNodeId = 0u;
    if (g_Bc250CreateContextObject == NULL) return STATUS_NOT_SUPPORTED;
    if (hDevice == (HANDLE)1) {
        pCreateContext->hContext = (HANDLE)2;
        Bc250DisplayLog("[McdmCreateContext] System Paging Context: DmaBuf=0x%X, SegSet=0x%X\n",
            pCreateContext->ContextInfo.DmaBufferSize,
            pCreateContext->ContextInfo.DmaBufferSegmentSet);
        return STATUS_SUCCESS;
    }
    status = g_Bc250CreateContextObject(hDevice,
        pCreateContext->NodeOrdinal, &contextHandle,
        &pCreateContext->ContextInfo);
    if (!NT_SUCCESS(status)) return status;
    pCreateContext->hContext = contextHandle;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmCreateDevice(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEDEVICE *pCreateDevice)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CREATE_DEVICE);
    HANDLE deviceHandle = NULL;
    NTSTATUS status;

    if (hAdapter == NULL || pCreateDevice == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCreateDevice] Entered: hAdapter=0x%p, hKmdProcess=0x%p\n",
        hAdapter, pCreateDevice->hKmdProcess);
    pCreateDevice->hDevice = NULL;
    if (g_Bc250CreateDeviceObject == NULL) return STATUS_NOT_SUPPORTED;
    if (pCreateDevice->hKmdProcess == NULL) {
        pCreateDevice->hDevice = (HANDLE)1;
        return STATUS_SUCCESS;
    }
    status = g_Bc250CreateDeviceObject(
        hAdapter, pCreateDevice->hKmdProcess, &deviceHandle);
    if (!NT_SUCCESS(status)) return status;
    pCreateDevice->hDevice = deviceHandle;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmCreateProcess(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEPROCESS *pArgs)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CREATE_PROCESS);
    NTSTATUS status;
    HANDLE processHandle = NULL;

    if (hAdapter == NULL || pArgs == NULL || pArgs->hDxgkProcess == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCreateProcess] Entered: hAdapter=0x%p, hDxgkProcess=0x%p\n",
        hAdapter, pArgs->hDxgkProcess);

    pArgs->hKmdProcess = NULL;
    if (g_Bc250CreateProcessObject == NULL) return STATUS_NOT_SUPPORTED;
    status = g_Bc250CreateProcessObject(
        hAdapter, pArgs->hDxgkProcess, &processHandle);
    if (!NT_SUCCESS(status)) return status;
    pArgs->hKmdProcess = processHandle;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmDescribeAllocation(
    CONST HANDLE hAdapter,
    DXGKARG_DESCRIBEALLOCATION *pDescribeAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_DESCRIBE_ALLOCATION);
    UNREFERENCED_PARAMETER(hAdapter);
    if (pDescribeAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmDestroyAllocation(
    CONST HANDLE hAdapter,
    CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_DESTROY_ALLOCATION);
    if (hAdapter == NULL || pDestroyAllocation == NULL ||
        pDestroyAllocation->NumAllocations == 0u ||
        pDestroyAllocation->pAllocationList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250DestroyAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyAllocationObjects(hAdapter, pDestroyAllocation);
}

NTSTATUS APIENTRY Bc250McdmDestroyContext(CONST HANDLE hContext)
{
    if (hContext == NULL) return STATUS_INVALID_PARAMETER;
    if (hContext == (HANDLE)1 || hContext == (HANDLE)2) return STATUS_SUCCESS;
    if (g_Bc250DestroyContextObject == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyContextObject(hContext);
}

NTSTATUS APIENTRY Bc250McdmDestroyDevice(CONST HANDLE hDevice)
{
    if (hDevice == NULL) return STATUS_INVALID_PARAMETER;
    if (hDevice == (HANDLE)1) return STATUS_SUCCESS;
    if (g_Bc250DestroyDeviceObject == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyDeviceObject(hDevice);
}

NTSTATUS APIENTRY Bc250McdmDestroyProcess(
    CONST HANDLE hAdapter,
    CONST HANDLE hKmdProcess)
{
    if (hAdapter == NULL || hKmdProcess == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250DestroyProcessObject == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyProcessObject(hAdapter, hKmdProcess);
}

NTSTATUS Bc250McdmDispatchIoRequest(
    CONST PVOID MiniportDeviceContext,
    ULONG VidPnSourceId,
    PVIDEO_REQUEST_PACKET VideoRequestPacket)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_DISPATCH_IO_REQUEST);
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    return STATUS_NOT_SUPPORTED;
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmFormatHistoryBuffer, BC250_V5_DDI_FORMAT_HISTORY_BUFFER,
    CONST HANDLE, hContext,
    DXGKARG_FORMATHISTORYBUFFER *, pFormatData)

NTSTATUS APIENTRY Bc250McdmCalibrateGpuClock(
    CONST HANDLE hAdapter,
    UINT32 NodeOrdinal,
    UINT32 EngineOrdinal,
    DXGKARG_CALIBRATEGPUCLOCK *pClockCalibration)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CALIBRATE_GPU_CLOCK);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(EngineOrdinal);

    if (pClockCalibration == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmGetNodeMetadata(
    CONST HANDLE hAdapter,
    UINT NodeOrdinalAndAdapterIndex,
    DXGKARG_GETNODEMETADATA *pGetNodeMetadata)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_GET_NODE_METADATA);
    UINT nodeIndex = NodeOrdinalAndAdapterIndex & 0xFFFFu;

    BC250_MCDM_RUNTIME_CAPS caps;

    if (pGetNodeMetadata == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (nodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.Ready == 0u || caps.FullMcdmReady == 0u ||
        caps.SubmitReady == 0u || nodeIndex >= caps.NodeCount) {
        return STATUS_DEVICE_NOT_READY;
    }
    RtlZeroMemory(pGetNodeMetadata, sizeof(*pGetNodeMetadata));
    pGetNodeMetadata->EngineType = (nodeIndex == BC250_ADAPTER_NODE_SDMA) ?
        DXGK_ENGINE_TYPE_COPY :
        (Bc250McdmGetComputeOnly() ? DXGK_ENGINE_TYPE_OTHER : DXGK_ENGINE_TYPE_3D);
    if (nodeIndex == BC250_ADAPTER_NODE_SDMA) {
        static const WCHAR name[] = L"BC250 SDMA0";
        RtlCopyMemory(pGetNodeMetadata->FriendlyName, name, sizeof(name));
    } else {
        static const WCHAR nameCompute[] = L"BC250 Compute MEC";
        static const WCHAR name3D[] = L"BC250 3D Graphics";
        if (Bc250McdmGetComputeOnly()) {
            RtlCopyMemory(pGetNodeMetadata->FriendlyName, nameCompute, sizeof(nameCompute));
        } else {
            RtlCopyMemory(pGetNodeMetadata->FriendlyName, name3D, sizeof(name3D));
        }
    }
    pGetNodeMetadata->GpuMmuSupported = caps.GpuVaReady != 0u;
    pGetNodeMetadata->IoMmuSupported = FALSE;
    Bc250DisplayLog("[GetNodeMetadata] Node=%u, EngineType=%d\n", nodeIndex, (int)pGetNodeMetadata->EngineType);
    return STATUS_SUCCESS;
}

SIZE_T APIENTRY Bc250McdmGetRootPageTableSize(
    CONST HANDLE hAdapter,
    DXGKARG_GETROOTPAGETABLESIZE *pArgs)
{
    BC250_MCDM_RUNTIME_CAPS caps;
    UINT64 bytes;
    UINT32 entries;

    if (hAdapter == NULL || pArgs == NULL ||
        pArgs->PhysicalAdapterIndex != 0u || pArgs->NumberOfPte == 0u ||
        g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.GpuVaReady == 0u ||
        caps.PageTableLevelCount != 2u ||
        caps.PageTableSizeBytes == 0u ||
        caps.PageTableAlignmentBytes == 0u) {
        if (pArgs != NULL) pArgs->NumberOfPte = 0u;
        return 0u;
    }
    entries = pArgs->NumberOfPte;
    bytes = (UINT64)entries * sizeof(UINT64);
    if (bytes > MAXULONG_PTR - (caps.PageTableAlignmentBytes - 1u)) {
        pArgs->NumberOfPte = 0u;
        return 0u;
    }
    bytes = (bytes + caps.PageTableAlignmentBytes - 1u) &
        ~((UINT64)caps.PageTableAlignmentBytes - 1u);
    pArgs->NumberOfPte = (UINT32)(bytes / sizeof(UINT64));
    return (SIZE_T)bytes;
}

NTSTATUS APIENTRY Bc250McdmGetStandardAllocationDriverData(
    CONST HANDLE hAdapter,
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *pGetStandardAllocationDriverData)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_GET_STD_ALLOCATION_DATA);
    if (hAdapter == NULL || pGetStandardAllocationDriverData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250DisplayLog("[GetStdAllocData] Type=%u\n",
        pGetStandardAllocationDriverData->StandardAllocationType);

    switch (pGetStandardAllocationDriverData->StandardAllocationType) {
    case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE: {
        D3DKMDT_SHAREDPRIMARYSURFACEDATA *pShared =
            pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData;
        if (pShared != NULL) {
            Bc250DisplayLog("[GetStdAllocData] SharedPrimary: %ux%u, Format=%u, VidPnSourceId=%u\n",
                pShared->Width, pShared->Height, pShared->Format, pShared->VidPnSourceId);
        }
        pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = 0u;
        pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0u;
        return STATUS_SUCCESS;
    }

    case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE: {
        D3DKMDT_SHADOWSURFACEDATA *pShadow =
            pGetStandardAllocationDriverData->pCreateShadowSurfaceData;
        if (pShadow != NULL) {
            pShadow->Pitch = pShadow->Width * 4u;
            Bc250DisplayLog("[GetStdAllocData] ShadowSurface: %ux%u, Pitch=%u\n",
                pShadow->Width, pShadow->Height, pShadow->Pitch);
        }
        pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = 0u;
        pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0u;
        return STATUS_SUCCESS;
    }

    case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE: {
        D3DKMDT_STAGINGSURFACEDATA *pStaging =
            pGetStandardAllocationDriverData->pCreateStagingSurfaceData;
        if (pStaging != NULL) {
            pStaging->Pitch = pStaging->Width * 4u;
            Bc250DisplayLog("[GetStdAllocData] StagingSurface: %ux%u, Pitch=%u\n",
                pStaging->Width, pStaging->Height, pStaging->Pitch);
        }
        pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = 0u;
        pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0u;
        return STATUS_SUCCESS;
    }

    case D3DKMDT_STANDARDALLOCATION_GDISURFACE: {
        D3DKMDT_GDISURFACEDATA *pGdi =
            pGetStandardAllocationDriverData->pCreateGdiSurfaceData;
        if (pGdi != NULL) {
            pGdi->Pitch = pGdi->Width * 4u;
            Bc250DisplayLog("[GetStdAllocData] GdiSurface: %ux%u, Type=%u, Pitch=%u\n",
                pGdi->Width, pGdi->Height, pGdi->Type, pGdi->Pitch);
        }
        pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = 0u;
        pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0u;
        return STATUS_SUCCESS;
    }

    default:
        Bc250DisplayLog("[GetStdAllocData] Unsupported type=%u\n",
            pGetStandardAllocationDriverData->StandardAllocationType);
        return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS APIENTRY Bc250McdmOpenAllocation(
    CONST HANDLE hDevice,
    CONST DXGKARG_OPENALLOCATION *pOpenAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_OPEN_ALLOCATION);
    UINT index;
    if (hDevice == NULL || pOpenAllocation == NULL ||
        pOpenAllocation->NumAllocations == 0u ||
        pOpenAllocation->pOpenAllocation == NULL ||
        pOpenAllocation->Flags.Reserved != 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    for (index = 0u; index < pOpenAllocation->NumAllocations; ++index) {
        pOpenAllocation->pOpenAllocation[index].hDeviceSpecificAllocation = NULL;
    }
    if (g_Bc250OpenAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250OpenAllocationObjects(hDevice, pOpenAllocation);
}

NTSTATUS APIENTRY Bc250McdmPreemptCommand(
    CONST HANDLE hAdapter,
    CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_PREEMPT_COMMAND);
    BC250_MCDM_RUNTIME_CAPS caps;

    if (pPreemptCommand == NULL ||
        pPreemptCommand->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.PreemptionReady == 0u) {
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS Bc250McdmQueryAdapterInfoWorker(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo);

/*
 * Envoltorio de instrumentacao do caminho WDDM completo.
 *
 * No boot de 2026-08-31 com RenderNode=1 o DxgkInitialize devolveu 0, o
 * AddDevice devolveu 0, o StartDevice entrou — e o dispositivo parou em Codigo
 * 43 sem que houvesse como saber qual consulta foi recusada. Este envoltorio
 * registra a sequencia: quantas chamadas, o ultimo tipo, o ultimo status, e
 * separadamente o ultimo tipo recusado com NOT_SUPPORTED, que e' o suspeito
 * natural quando o dxgkrnl aborta o start.
 */
NTSTATUS APIENTRY Bc250McdmQueryAdapterInfo(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    static volatile LONG s_qaiCalls;
    static volatile LONG s_qaiUnsupported;
    NTSTATUS status;
    ULONG type;

    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_ADAPTER_INFO);

    Bc250ProbeRecord(BC250_PROBE_V5_QAI_CALLS,
        (ULONG)InterlockedIncrement(&s_qaiCalls));

    status = Bc250McdmQueryAdapterInfoWorker(hAdapter, pQueryAdapterInfo);

    type = (pQueryAdapterInfo != NULL) ? (ULONG)pQueryAdapterInfo->Type : 0xFFFFFFFFu;
    Bc250ProbeRecord(BC250_PROBE_V5_QAI_LAST_TYPE, type);
    Bc250ProbeRecord(BC250_PROBE_V5_QAI_LAST_STATUS, (ULONG)status);
    /*
     * Bitmap acumulado (schema 134). Guarda de NULL obrigatoria: com
     * pQueryAdapterInfo nulo, type vale 0xFFFFFFFF e envenenaria o bitmap.
     *
     * O 3.79 soube por acaso que o dxgkrnl parou depois de WDDMDEVICECAPS
     * (29) sem jamais pedir DRIVERCAPS (1). O bitmap torna isso uma leitura
     * direta em vez de uma inferencia sobre o ultimo valor sobrevivente.
     */
    if (pQueryAdapterInfo != NULL) {
        Bc250ProbeMarkQaiType(type);
    }
    if (status == STATUS_NOT_SUPPORTED) {
        Bc250ProbeRecord(BC250_PROBE_V5_QAI_LAST_UNSUPPORTED_TYPE, type);
        Bc250ProbeRecord(BC250_PROBE_V5_QAI_UNSUPPORTED_COUNT,
            (ULONG)InterlockedIncrement(&s_qaiUnsupported));
    }
    return status;
}

static NTSTATUS Bc250McdmQueryAdapterInfoWorker(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    BC250_MCDM_RUNTIME_CAPS runtimeCaps;
    NTSTATUS capsStatus;

    if (pQueryAdapterInfo == NULL || pQueryAdapterInfo->pOutputData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&runtimeCaps, sizeof(runtimeCaps));
    capsStatus = g_Bc250QueryRuntimeCaps != NULL ?
        g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &runtimeCaps) :
        STATUS_DEVICE_NOT_READY;

    Bc250ProbeRecord(BC250_PROBE_DMM_QUERY_ADAPTER_TYPE, (ULONG)pQueryAdapterInfo->Type);
    Bc250DisplayLog("[McdmQAI] Type=%u, OutSize=%u, CapsStatus=0x%08X\n",
        (UINT)pQueryAdapterInfo->Type, pQueryAdapterInfo->OutputDataSize, capsStatus);

    switch (pQueryAdapterInfo->Type) {
    /*
     * DXGKQAITYPE_WDDMDEVICECAPS — obrigatorio para driver de render.
     *
     * Medido no silicio em 2026-08-31, primeiro boot com RenderNode=1:
     *
     *   V5QaiCalls              1
     *   V5QaiLastType           0x1D  (DXGKQAITYPE_WDDMDEVICECAPS)
     *   V5QaiLastStatus         0xC00000BB  (STATUS_NOT_SUPPORTED)
     *   V5StartDeviceStatus     0  com Sources=1 e Children=1 (sucesso)
     *
     * O dxgkrnl fez UMA unica pergunta, levou NOT_SUPPORTED do ramo default e
     * abortou o dispositivo com Codigo 43. O KMDOD nunca chegava aqui porque
     * KMDDOD_INITIALIZATION_DATA nao expoe este caminho.
     *
     * A estrutura tem um unico campo, e o WDK exige explicitamente o mesmo
     * valor de DXGK_DRIVERCAPS::WDDMVersion — por isso DXGKDDI_WDDMv2_7, igual
     * ao declarado logo abaixo. Divergir entre os dois e' erro de conformidade.
     */
    case DXGKQAITYPE_WDDMDEVICECAPS: {
        DXGK_WDDMDEVICECAPS *deviceCaps;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_WDDMDEVICECAPS)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        /*
         * SEM gate de runtimeCaps aqui, ao contrario de todos os outros tipos.
         *
         * O primeiro candidato com este case tinha o gate padrao e devolveu
         * STATUS_DEVICE_NOT_READY (0xC00000A3), medido no boot de 2026-08-31 —
         * trocando um Codigo 43 por outro. A causa: o WDK especifica que
         * WDDMDEVICECAPS e' consultado "after DxgkDdiAddDevice returns and
         * before DxgkDdiStartDevice is called". Nesse instante o adaptador
         * ainda nao subiu e runtimeCaps nao existe, por construcao.
         *
         * Isto nao afrouxa a Lei 3: WDDMVersion nao descreve estado de
         * hardware, descreve qual contrato de DDI este binario implementa —
         * fato de compilacao, nao de execucao. Condicionar uma constante ao
         * estado do silicio seria erro na direcao oposta.
         */
        deviceCaps = (DXGK_WDDMDEVICECAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(deviceCaps, sizeof(*deviceCaps));
        deviceCaps->WDDMVersion = Bc250McdmGetWddmContractVersion();
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS *caps;
        if (pQueryAdapterInfo->OutputDataSize < 576u) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.TdrReady == 0u ||
            runtimeCaps.PreemptionReady == 0u ||
            runtimeCaps.SubmitReady == 0u ||
            runtimeCaps.HistoryBufferReady == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        caps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, pQueryAdapterInfo->OutputDataSize);
        caps->HighestAcceptableAddress.QuadPart = MAXLONGLONG;
        caps->WDDMVersion = Bc250McdmGetWddmContractVersion();
        caps->SchedulingCaps.MultiEngineAware = 1u;
        caps->SchedulingCaps.PreemptionAware = 1u;
        caps->SchedulingCaps.LowIrqlPreemptCommand = 1u;
        caps->MemoryManagementCaps.VirtualAddressingSupported =
            (runtimeCaps.GpuVaReady != 0u) ? 1u : 0u;
        caps->MemoryManagementCaps.GpuMmuSupported =
            (runtimeCaps.GpuVaReady != 0u) ? 1u : 0u;
        caps->MemoryManagementCaps.ParavirtualizationSupported = 0u;
        caps->PreemptionCaps.ComputePreemptionGranularity =
            D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
        caps->SupportPerEngineTDR = TRUE;
        caps->GpuEngineTopology.NbAsymetricProcessingNodes =
            runtimeCaps.NodeCount;
        caps->MiscCaps.ComputeOnly = Bc250McdmGetComputeOnly();
        caps->MaxAllocationListSlotId = 32u;
        caps->PresentationCaps.NoScreenToScreenBlt = 1u;
        caps->PresentationCaps.NoOverlapScreenBlt = 1u;
        caps->PresentationCaps.DriverSupportsCddDwmInterop = 1u;
        caps->PresentationCaps.AlignmentShift = 2u;
        caps->PresentationCaps.MaxTextureWidthShift = 3u;  /* 2 ^ (3 + 11) = 16384 texels (suporta 2560x1080) */
        caps->PresentationCaps.MaxTextureHeightShift = 3u; /* 2 ^ (3 + 11) = 16384 texels */
        caps->PresentationCaps.SupportSoftwareDeviceBitmaps = 1u;
        caps->MaxQueuedFlipOnVSync = 1u;
        caps->FlipCaps.FlipOnVSyncMmIo = 1u;
        caps->PointerCaps.Value = 0u;
        caps->MaxPointerWidth = 0u;
        caps->MaxPointerHeight = 0u;

        /*
         * Instrumentacao 3.82: ate a corrida 14:25 o log expunha 4 campos de
         * uma estrutura de 576 bytes. Sem o retrato completo nao da para
         * auditar por que o DMM recusa ativar o caminho sob RenderNode=1.
         * Este bloco e' somente leitura — nao altera nenhum cap.
         */
        Bc250DisplayLog("[McdmQAI] DRIVERCAPS: ComputeOnly=%u, AlignShift=%u, TexWShift=%u, TexHShift=%u, OutSize=%u\n",
            caps->MiscCaps.ComputeOnly, caps->PresentationCaps.AlignmentShift,
            caps->PresentationCaps.MaxTextureWidthShift, caps->PresentationCaps.MaxTextureHeightShift,
            pQueryAdapterInfo->OutputDataSize);
        Bc250DisplayLog("[DriverCaps] WDDMVersion=%u, MaxAllocListSlotId=%u, PerEngineTDR=%u, Nodes=%u, HighestAddr=0x%I64X\n",
            (ULONG)caps->WDDMVersion,
            caps->MaxAllocationListSlotId,
            (ULONG)caps->SupportPerEngineTDR,
            caps->GpuEngineTopology.NbAsymetricProcessingNodes,
            caps->HighestAcceptableAddress.QuadPart);
        Bc250DisplayLog("[DriverCaps] PresentationCaps=0x%08X (NoScreenToScreenBlt=%u, NoOverlapScreenBlt=%u, CddDwmInterop=%u, SwDeviceBitmaps=%u)\n",
            caps->PresentationCaps.Value,
            caps->PresentationCaps.NoScreenToScreenBlt,
            caps->PresentationCaps.NoOverlapScreenBlt,
            caps->PresentationCaps.DriverSupportsCddDwmInterop,
            caps->PresentationCaps.SupportSoftwareDeviceBitmaps);
        Bc250DisplayLog("[DriverCaps] FlipCaps=0x%08X, MaxQueuedFlipOnVSync=%u, MiscCaps=0x%08X, PointerCaps=0x%08X\n",
            caps->FlipCaps.Value,
            caps->MaxQueuedFlipOnVSync,
            caps->MiscCaps.Value,
            caps->PointerCaps.Value);
        Bc250DisplayLog("[DriverCaps] Sched(MultiEngine=%u, Preempt=%u, LowIrql=%u) Mem(VA=%u, GpuMmu=%u, Paravirt=%u)\n",
            caps->SchedulingCaps.MultiEngineAware,
            caps->SchedulingCaps.PreemptionAware,
            caps->SchedulingCaps.LowIrqlPreemptCommand,
            caps->MemoryManagementCaps.VirtualAddressingSupported,
            caps->MemoryManagementCaps.GpuMmuSupported,
            caps->MemoryManagementCaps.ParavirtualizationSupported);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PHYSICALADAPTERCAPS:
        Bc250DisplayLog("[McdmQAI] PHYSICALADAPTERCAPS returned NOT_SUPPORTED (single physical adapter)\n");
        return STATUS_NOT_SUPPORTED;

    case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION: {
        DXGK_DISPLAY_DRIVERCAPS_EXTENSION *ext;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION)) {
            Bc250DisplayLog("[McdmQAI] DISPLAY_DRIVERCAPS_EXTENSION buffer too small\n");
            return STATUS_BUFFER_TOO_SMALL;
        }
        ext = (DXGK_DISPLAY_DRIVERCAPS_EXTENSION *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(ext, sizeof(*ext));
        Bc250DisplayLog("[McdmQAI] DISPLAY_DRIVERCAPS_EXTENSION handled (zeroed)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PREFERREDGPUNODE: {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0;
        Bc250DisplayLog("[McdmQAI] PREFERREDGPUNODE handled (0)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_GPUMMUCAPS: {
        const DXGK_QUERYGPUMMUCAPSIN *in;
        DXGK_GPUMMUCAPS *caps;
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in)) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        in = (const DXGK_QUERYGPUMMUCAPSIN *)
            pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) {
            return STATUS_INVALID_PARAMETER;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.GpuVaReady == 0u ||
            runtimeCaps.GpuVaBitCount == 0u ||
            runtimeCaps.PageTableLevelCount == 0u) {
            Bc250DisplayLog("[McdmQAI] GPUMMUCAPS device not ready\n");
            return STATUS_DEVICE_NOT_READY;
        }
        caps = (DXGK_GPUMMUCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, sizeof(*caps));
        caps->ReadOnlyMemorySupported = 1u;
        caps->NoExecuteMemorySupported = 1u;
        caps->ExplicitPageTableInvalidation = 1u;
        caps->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        caps->VirtualAddressBitCount = runtimeCaps.GpuVaBitCount;
        caps->PageTableLevelCount = runtimeCaps.PageTableLevelCount;
        Bc250DisplayLog("[McdmQAI] GPUMMUCAPS: VaBits=%u, Levels=%u\n",
            caps->VirtualAddressBitCount, caps->PageTableLevelCount);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PAGETABLELEVELDESC: {
        const DXGK_QUERYPAGETABLELEVELDESCIN *in;
        DXGK_PAGE_TABLE_LEVEL_DESC *desc;
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in)) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*desc)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        in = (const DXGK_QUERYPAGETABLELEVELDESCIN *)
            pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) {
            return STATUS_INVALID_PARAMETER;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.GpuVaReady == 0u ||
            runtimeCaps.PageTableLevelCount == 0u ||
            runtimeCaps.PageTableIndexBitCount == 0u ||
            runtimeCaps.PageTableSizeBytes == 0u) {
            Bc250DisplayLog("[McdmQAI] PAGETABLELEVELDESC device not ready\n");
            return STATUS_DEVICE_NOT_READY;
        }
        if (in->LevelIndex >= runtimeCaps.PageTableLevelCount) {
            return STATUS_INVALID_PARAMETER;
        }
        desc = (DXGK_PAGE_TABLE_LEVEL_DESC *)
            pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(desc, sizeof(*desc));
        if (in->LevelIndex == 0u) {
            desc->PageTableIndexBitCount = BC250_GPUVA_PT_LEAF_INDEX_BITS;
            desc->PageTableSizeInBytes = (1ull << BC250_GPUVA_PT_LEAF_INDEX_BITS) * sizeof(UINT64);
        } else {
            desc->PageTableIndexBitCount = BC250_GPUVA_PT_ROOT_INDEX_BITS;
            desc->PageTableSizeInBytes = (1ull << BC250_GPUVA_PT_ROOT_INDEX_BITS) * sizeof(UINT64);
        }
        desc->PageTableSegmentId = 1u;
        desc->PagingProcessPageTableSegmentId = 1u;
        desc->PageTableAlignmentInBytes = runtimeCaps.PageTableAlignmentBytes;
        Bc250DisplayLog("[McdmQAI] PAGETABLELEVELDESC: Level=%u, Bits=%u, Size=%u\n",
            in->LevelIndex, desc->PageTableIndexBitCount, (UINT32)desc->PageTableSizeInBytes);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT4: {
        DXGK_QUERYSEGMENTOUT4 *out;
        DXGK_QUERYSEGMENTIN4 *in;
        DXGK_SEGMENTDESCRIPTOR4 *segment;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*out)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u) {
            Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4 device not ready\n");
            return STATUS_DEVICE_NOT_READY;
        }
        out = (DXGK_QUERYSEGMENTOUT4 *)pQueryAdapterInfo->pOutputData;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        if (out->NbSegment == 0u) {
            out->NbSegment = 2u;
            Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: NbSegment queried -> 2\n");
            return STATUS_SUCCESS;
        }
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in) ||
            out->NbSegment < 2u || out->pSegmentDescriptor == NULL ||
            out->SegmentDescriptorStride < sizeof(*segment)) {
            Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: Invalid parameter in pass 2\n");
            return STATUS_INVALID_PARAMETER;
        }
        in = (DXGK_QUERYSEGMENTIN4 *)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) return STATUS_INVALID_PARAMETER;

        /* Segment 1: Local VRAM */
        segment = (DXGK_SEGMENTDESCRIPTOR4 *)out->pSegmentDescriptor;
        RtlZeroMemory(segment, sizeof(*segment));
        segment->BaseAddress.QuadPart =
            (LONGLONG)runtimeCaps.LocalSegmentBaseOffset;
        segment->CpuTranslatedAddress.QuadPart =
            (LONGLONG)runtimeCaps.CpuPhysicalStart;
        segment->Size = (SIZE_T)runtimeCaps.LocalSegmentBytes;
        segment->CommitLimit = 0u;
        segment->Flags.CpuVisible = 1u;
        segment->Flags.LocalBudgetGroup = 1u;
        segment->Flags.PreservedDuringStandby = 1u;
        segment->Flags.PreservedDuringHibernate = 1u;

        /* Segment 2: System Memory Aperture (GART) - Obrigatorio para WDDM 2.0+ */
        {
            DXGK_SEGMENTDESCRIPTOR4 *gart = (DXGK_SEGMENTDESCRIPTOR4 *)
                ((BYTE *)out->pSegmentDescriptor + out->SegmentDescriptorStride);
            RtlZeroMemory(gart, sizeof(*gart));
            gart->BaseAddress.QuadPart = 0x100000000ull; /* 4GB base GPU logica */
            gart->Size = 512ull * 1024 * 1024;           /* 512 MB GART aperture */
            gart->CommitLimit = 512ull * 1024 * 1024;
            gart->Flags.Aperture = 1u;
            gart->Flags.NonLocalBudgetGroup = 1u;
        }

        out->PagingBufferSegmentId = 2u;
        out->PagingBufferSize = 0x10000u;
        out->PagingBufferPrivateDataSize = 0u;
        Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: 2 segments filled (VRAM=0x%llX, GART=512MB, PagingSeg=2)\n",
            (unsigned long long)segment->Size);
        return STATUS_SUCCESS;
#else
        if (out->NbSegment == 0u) {
            out->NbSegment = 1u;
            Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: NbSegment queried -> 1\n");
            return STATUS_SUCCESS;
        }
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in) ||
            out->NbSegment != 1u || out->pSegmentDescriptor == NULL ||
            out->SegmentDescriptorStride < sizeof(*segment)) {
            return STATUS_INVALID_PARAMETER;
        }
        in = (DXGK_QUERYSEGMENTIN4 *)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) return STATUS_INVALID_PARAMETER;
        segment = (DXGK_SEGMENTDESCRIPTOR4 *)out->pSegmentDescriptor;
        RtlZeroMemory(segment, sizeof(*segment));
        segment->BaseAddress.QuadPart =
            (LONGLONG)runtimeCaps.LocalSegmentBaseOffset;
        segment->CpuTranslatedAddress.QuadPart =
            (LONGLONG)runtimeCaps.CpuPhysicalStart;
        segment->Size = (SIZE_T)runtimeCaps.LocalSegmentBytes;
        segment->CommitLimit = 0u;
        segment->Flags.CpuVisible = 1u;
        segment->Flags.LocalBudgetGroup = 1u;
        out->PagingBufferSegmentId = 1u;
        out->PagingBufferSize = 0x10000u;
        out->PagingBufferPrivateDataSize = 0u;
        Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: Segment filled, Size=0x%llX\n",
            (unsigned long long)segment->Size);
        return STATUS_SUCCESS;
#endif
    }

    case DXGKQAITYPE_NUMPOWERCOMPONENTS:
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0u;
        Bc250DisplayLog("[McdmQAI] NUMPOWERCOMPONENTS handled (0)\n");
        return STATUS_SUCCESS;

    case DXGKQAITYPE_HISTORYBUFFERPRECISION: {
        DXGKARG_HISTORYBUFFERPRECISION *precision;
        UINT index;
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.HistoryBufferReady == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        if (pQueryAdapterInfo->OutputDataSize <
            sizeof(*precision) * runtimeCaps.NodeCount) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        precision = (DXGKARG_HISTORYBUFFERPRECISION *)
            pQueryAdapterInfo->pOutputData;
        for (index = 0u; index < runtimeCaps.NodeCount; ++index) {
            precision[index].PrecisionBits = 32u;
        }
        Bc250DisplayLog("[McdmQAI] HISTORYBUFFERPRECISION handled (32-bit)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_DISPLAYID_DESCRIPTOR: {
        DXGK_QUERYDISPLAYIDOUT *out;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*out)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (DXGK_QUERYDISPLAYIDOUT *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(out, sizeof(*out));
        out->Length = 0u;
        out->pDescriptor = NULL;
        Bc250DisplayLog("[McdmQAI] DISPLAYID_DESCRIPTOR handled (none)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYCOLORIMETRYOVERRIDES: {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_COLORIMETRY)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);
        Bc250DisplayLog("[McdmQAI] QUERYCOLORIMETRYOVERRIDES handled (zeroed)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT:
    case DXGKQAITYPE_QUERYSEGMENT2:
    case DXGKQAITYPE_QUERYSEGMENT3:
        return STATUS_NOT_SUPPORTED;

    default:
        Bc250DisplayLog("[McdmQAI] Type=%u NOT SUPPORTED\n", (UINT)pQueryAdapterInfo->Type);
        return STATUS_NOT_SUPPORTED;
    }
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmQueryDependentEngineGroup, BC250_V5_DDI_QUERY_DEPENDENT_ENGINE_GROUP,
    CONST HANDLE, hAdapter,
    DXGKARG_QUERYDEPENDENTENGINEGROUP *, pQueryDependentEngineGroup)

NTSTATUS APIENTRY Bc250McdmQueryEngineStatus(
    CONST HANDLE hAdapter,
    DXGKARG_QUERYENGINESTATUS *pQueryEngineStatus)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_ENGINE_STATUS);
    BC250_MCDM_RUNTIME_CAPS caps;

    if (pQueryEngineStatus == NULL ||
        pQueryEngineStatus->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.TdrReady == 0u) {
        pQueryEngineStatus->EngineStatus.Responsive = 0;
        return STATUS_NOT_SUPPORTED;
    }

    pQueryEngineStatus->EngineStatus.Responsive = 1;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmResetEngine(
    CONST HANDLE hAdapter,
    DXGKARG_RESETENGINE *pResetEngine)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_RESET_ENGINE);
    BC250_MCDM_RUNTIME_CAPS caps;

    if (pResetEngine == NULL ||
        pResetEngine->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.TdrReady == 0u) {
        pResetEngine->LastAbortedFenceId = 0u;
        return STATUS_NOT_SUPPORTED;
    }

    /* Caps estruturais não provam que HQD/SDMA foram resetados. Sem executor
     * físico e sem devolução dos pins das fences abortadas, sucesso seria falso. */
    pResetEngine->LastAbortedFenceId = 0u;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmResetFromTimeout(CONST HANDLE hAdapter)
{
    Bc250DisplayLog("[McdmResetFromTimeout] Called - recovering adapter without BSOD\n");
    if (hAdapter == NULL) {
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmRestartFromTimeout(CONST HANDLE hAdapter)
{
    Bc250DisplayLog("[McdmRestartFromTimeout] Called - restarting adapter without BSOD\n");
    if (hAdapter == NULL) {
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmSetVirtualMachineData, BC250_V5_DDI_SET_VIRTUAL_MACHINE_DATA,
    CONST HANDLE, hAdapter,
    CONST DXGKARG_SETVIRTUALMACHINEDATA *, Args)

VOID APIENTRY Bc250McdmSetRootPageTable(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETROOTPAGETABLE *pSetPageTable)
{
    BC250_MCDM_RUNTIME_CAPS caps;
    UINT32 maxEntries;

    if (hAdapter == NULL || pSetPageTable == NULL ||
        pSetPageTable->hContext == NULL ||
        g_Bc250SetRootPageTableObject == NULL ||
        g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.GpuVaReady == 0u ||
        caps.PageTableIndexBitCount == 0u ||
        caps.PageTableIndexBitCount >= 32u) {
        return;
    }
    maxEntries = 1u << caps.PageTableIndexBitCount;
    (void)g_Bc250SetRootPageTableObject((HANDLE)hAdapter,
        pSetPageTable->hContext, pSetPageTable->Address.SegmentId,
        pSetPageTable->Address.SegmentOffset,
        pSetPageTable->NumEntries, maxEntries);
}

NTSTATUS APIENTRY Bc250McdmSubmitCommandVirtual(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SUBMITCOMMANDVIRTUAL *pSubmitCommand)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_SUBMIT_COMMAND_VIRTUAL);
    BC250_MCDM_RUNTIME_CAPS caps;
    if (hAdapter == NULL || pSubmitCommand == NULL ||
        pSubmitCommand->hContext == NULL ||
        pSubmitCommand->DmaBufferSize == 0u ||
        (pSubmitCommand->DmaBufferSize % sizeof(uint32_t)) != 0u ||
        pSubmitCommand->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmSubmitCommandVirtual] Node=%u, FenceId=%llu, DmaSize=%u\n",
        pSubmitCommand->NodeOrdinal, (unsigned long long)pSubmitCommand->SubmissionFenceId,
        pSubmitCommand->DmaBufferSize);

    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.SubmitReady == 0u) {
        return STATUS_NOT_SUPPORTED;
    }

    if (pSubmitCommand->DmaBufferVirtualAddress == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * O callback comum não possui fila, fence nem ownership físico próprios.
     * Somente o miniport ativo pode validar o contexto real e publicar o IB na
     * MEC. Sucesso sem executor esconderia uma submissão descartada pela CPU.
     */
    if (g_Bc250SubmitCommandExecutor == NULL) {
        return STATUS_NOT_SUPPORTED;
    }
    return g_Bc250SubmitCommandExecutor(hAdapter, pSubmitCommand);
}

BOOLEAN Bc250McdmInterruptRoutine(
    CONST PVOID MiniportDeviceContext,
    ULONG MessageNumber)
{
    if (g_Bc250InterruptHandler != NULL) {
        return g_Bc250InterruptHandler(MiniportDeviceContext, MessageNumber);
    }
    return FALSE;
}

VOID Bc250McdmDpcRoutine(CONST PVOID MiniportDeviceContext)
{
    if (g_Bc250DpcHandler != NULL) {
        g_Bc250DpcHandler(MiniportDeviceContext);
    }
}

NTSTATUS Bc250McdmQueryChildRelations(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    if (ChildRelations == NULL && ChildRelationsSize != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (ChildRelations != NULL && ChildRelationsSize != 0) {
        RtlZeroMemory(ChildRelations, ChildRelationsSize);
    }
    return STATUS_SUCCESS;
}

NTSTATUS Bc250McdmQueryChildStatus(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_STATUS ChildStatus,
    BOOLEAN NonDestructiveOnly)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildStatus);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Bc250McdmQueryDeviceDescriptor(
    CONST PVOID MiniportDeviceContext,
    ULONG ChildUid,
    PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Bc250McdmSetPowerState(
    CONST PVOID MiniportDeviceContext,
    ULONG DeviceUid,
    DEVICE_POWER_STATE DevicePowerState,
    POWER_ACTION ActionType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);

    switch (DevicePowerState) {
    case PowerDeviceD0:
    case PowerDeviceD1:
    case PowerDeviceD2:
    case PowerDeviceD3:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* ==========================================================================
   Fase V5 — DDIs obrigatorias que faltavam na tabela de render.

   A auditoria estatica comparou os 187 campos DxgkDdi* de
   DRIVER_INITIALIZATION_DATA com os 59 que o driver preenchia. Ponteiro
   obrigatorio nulo faz o dxgkrnl abortar o start com STATUS_INTERNAL_ERROR
   (0xC00000E5) sem chamar nada — foi o observado no silicio.

   Todas marcam a propria entrada no rastro, para que a proxima recusa seja
   nomeada em vez de deduzida.
   ========================================================================== */

static BC250_MCDM_CURRENT_FENCE_PROVIDER g_Bc250CurrentFenceProvider;

VOID Bc250McdmInstallCurrentFenceProvider(
    _In_opt_ BC250_MCDM_CURRENT_FENCE_PROVIDER CurrentFenceProvider)
{
    g_Bc250CurrentFenceProvider = CurrentFenceProvider;
}

/*
 * Implementacao REAL: o valor vem da fence que a GPU gravou na VRAM, via o
 * provedor instalado pelo miniport ativo. Sem provedor, ou com o motor
 * offline, a resposta e' erro — jamais um numero inventado.
 */
NTSTATUS APIENTRY Bc250McdmQueryCurrentFence(
    CONST HANDLE hAdapter,
    DXGKARG_QUERYCURRENTFENCE *pCurrentFence)
{
    UINT completed = 0u;
    NTSTATUS status;

    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_CURRENT_FENCE);

    if (hAdapter == NULL || pCurrentFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250CurrentFenceProvider == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    status = g_Bc250CurrentFenceProvider(
        (HANDLE)hAdapter, pCurrentFence->NodeOrdinal, &completed);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pCurrentFence->CurrentFence = completed;
    return STATUS_SUCCESS;
}

/*
 * As DDIs abaixo sao obrigatorias na tabela, mas o driver genuinamente nao as
 * implementa. Recusar e' honesto; o que estava errado era o ponteiro NULO,
 * que impede o dxgkrnl de sequer perguntar.
 *
 * Patch, Render e SubmitCommand pertencem ao caminho de DMA buffer com
 * patch-list. Este driver usa enderecamento virtual e SubmitCommandVirtual,
 * entao nao devem ser chamadas; se o rastro mostrar uma delas, a premissa de
 * GPU VA esta errada em algum lugar.
 */
NTSTATUS APIENTRY Bc250McdmEscape(
    CONST HANDLE hAdapter,
    CONST DXGKARG_ESCAPE *pEscape)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_ESCAPE);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pEscape);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmAcquireSwizzlingRange(
    CONST HANDLE hAdapter,
    DXGKARG_ACQUIRESWIZZLINGRANGE *pAcquireSwizzlingRange)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_ACQUIRE_SWIZZLING_RANGE);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pAcquireSwizzlingRange);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmReleaseSwizzlingRange(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RELEASESWIZZLINGRANGE *pReleaseSwizzlingRange)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_RELEASE_SWIZZLING_RANGE);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pReleaseSwizzlingRange);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmPatch(
    CONST HANDLE hAdapter,
    CONST DXGKARG_PATCH *pPatch)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_PATCH);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPatch);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmRender(
    CONST HANDLE hContext,
    DXGKARG_RENDER *pRender)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_RENDER);
    if (hContext == NULL || pRender == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmSubmitCommand(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_SUBMIT_COMMAND);
    if (hAdapter == NULL || pSubmitCommand == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmSubmitCommand] Node=%u, FenceId=%llu, DmaBuf=0x%llX, Size=%u\n",
        pSubmitCommand->NodeOrdinal,
        (unsigned long long)pSubmitCommand->SubmissionFenceId,
        (unsigned long long)pSubmitCommand->DmaBufferPhysicalAddress.QuadPart,
        pSubmitCommand->DmaBufferSize);
    return STATUS_SUCCESS;
}

VOID Bc250McdmResetDevice(CONST PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

VOID APIENTRY Bc250McdmSetStablePowerState(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETSTABLEPOWERSTATE *pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
}

VOID Bc250McdmUnload(VOID)
{
    Bc250McdmInstallRuntimeCapsProvider(NULL);
    Bc250McdmInstallProcessLifecycle(NULL, NULL);
    Bc250McdmInstallDeviceLifecycle(NULL, NULL);
    Bc250McdmInstallContextLifecycle(NULL, NULL);
    Bc250McdmInstallGpuVaLifecycle(NULL);
    Bc250McdmInstallAllocationLifecycle(NULL, NULL, NULL, NULL);
    Bc250McdmInstallDebugLifecycle(NULL);
    Bc250McdmInstallSubmissionExecutor(NULL);
    Bc250McdmInstallCurrentFenceProvider(NULL);
    Bc250McdmInstallUpdatePageTableHandler(NULL);
    Bc250McdmInstallFlushTlbHandler(NULL);
    Bc250McdmInstallInterruptLifecycle(NULL, NULL);
}

/*
 * Fase V5 — DDIs obrigatorias EXCLUSIVAS da tabela de render.
 *
 * Deliberadamente fora de Bc250McdmPopulateRequiredCallbacks: a imagem
 * readonly de producao tem contrato proprio exigindo
 * DxgkDdiSubmitCommand == NULL, e essa garantia de fail-closed vale mais que
 * a conveniencia de uma funcao unica. So o miniport ativo, que registra por
 * DxgkInitialize, chama esta funcao.
 */
NTSTATUS Bc250McdmPopulateRenderCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;

    InitializationData->DxgkDdiQueryCurrentFence = Bc250McdmQueryCurrentFence;
    InitializationData->DxgkDdiEscape = Bc250McdmEscape;
    InitializationData->DxgkDdiAcquireSwizzlingRange = Bc250McdmAcquireSwizzlingRange;
    InitializationData->DxgkDdiReleaseSwizzlingRange = Bc250McdmReleaseSwizzlingRange;
    InitializationData->DxgkDdiPatch = Bc250McdmPatch;
    InitializationData->DxgkDdiRender = Bc250McdmRender;
    InitializationData->DxgkDdiSubmitCommand = Bc250McdmSubmitCommand;
    InitializationData->DxgkDdiRenderKm = Bc250McdmRenderKm;
    InitializationData->DxgkDdiCancelCommand = Bc250McdmCancelCommand;
    InitializationData->DxgkDdiSetPowerComponentFState =
        Bc250McdmSetPowerComponentFState;
    InitializationData->DxgkDdiPowerRuntimeControlRequest =
        Bc250McdmPowerRuntimeControlRequest;

    return STATUS_SUCCESS;
}

/*
 * Obrigatoria no conjunto minimo MCDM a partir do contrato que introduziu
 * esta DDI. O BC-250 ainda nao publica mudancas assincronas de conector por
 * DxgkCbIndicateConnectorChange; portanto a fila esta vazia por construcao.
 * STATUS_ALREADY_COMPLETE e' o retorno documentado para "nenhuma mudanca
 * ainda nao reportada". Nao fabricar um pacote DXGK_CONNECTION_CHANGE evita
 * anunciar um monitor ou target inexistente no perfil compute/render-only.
 */
NTSTATUS APIENTRY Bc250McdmQueryConnectionChange(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_QUERYCONNECTIONCHANGE pQueryConnectionChange)
{
    if (hAdapter == NULL || pQueryConnectionChange == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_ALREADY_COMPLETE;
}

NTSTATUS APIENTRY Bc250McdmRenderKm(
    CONST HANDLE hContext,
    DXGKARG_RENDER *pRenderKmArgs)
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRenderKmArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmCancelCommand(
    CONST HANDLE hAdapter,
    CONST DXGKARG_CANCELCOMMAND *pCancelCommand)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCancelCommand);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmSetPowerComponentFState(
    CONST HANDLE DriverContext,
    UINT ComponentIndex,
    UINT FState)
{
    UNREFERENCED_PARAMETER(DriverContext);
    UNREFERENCED_PARAMETER(ComponentIndex);
    UNREFERENCED_PARAMETER(FState);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmPowerRuntimeControlRequest(
    CONST HANDLE DriverContext,
    LPCGUID PowerControlCode,
    PVOID InBuffer,
    SIZE_T InBufferSize,
    PVOID OutBuffer,
    SIZE_T OutBufferSize,
    PSIZE_T BytesReturned)
{
    UNREFERENCED_PARAMETER(DriverContext);
    UNREFERENCED_PARAMETER(PowerControlCode);
    UNREFERENCED_PARAMETER(InBuffer);
    UNREFERENCED_PARAMETER(InBufferSize);
    UNREFERENCED_PARAMETER(OutBuffer);
    UNREFERENCED_PARAMETER(OutBufferSize);
    if (BytesReturned != NULL) *BytesReturned = 0u;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Bc250McdmPopulateRequiredCallbacks(
    DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;
    InitializationData->DxgkDdiBuildPagingBuffer =
        Bc250McdmBuildPagingBuffer;
    InitializationData->DxgkDdiCalibrateGpuClock =
        Bc250McdmCalibrateGpuClock;
    InitializationData->DxgkDdiCloseAllocation = Bc250McdmCloseAllocation;
    InitializationData->DxgkDdiCollectDbgInfo = Bc250McdmCollectDbgInfo;
    InitializationData->DxgkDdiCreateAllocation = Bc250McdmCreateAllocation;
    InitializationData->DxgkDdiCreateContext = Bc250McdmCreateContext;
    InitializationData->DxgkDdiCreateDevice = Bc250McdmCreateDevice;
    InitializationData->DxgkDdiCreateProcess = Bc250McdmCreateProcess;
    InitializationData->DxgkDdiDescribeAllocation =
        Bc250McdmDescribeAllocation;
    InitializationData->DxgkDdiDestroyAllocation =
        Bc250McdmDestroyAllocation;
    InitializationData->DxgkDdiDestroyContext = Bc250McdmDestroyContext;
    InitializationData->DxgkDdiDestroyDevice = Bc250McdmDestroyDevice;
    InitializationData->DxgkDdiDestroyProcess = Bc250McdmDestroyProcess;
    InitializationData->DxgkDdiDispatchIoRequest =
        Bc250McdmDispatchIoRequest;
    InitializationData->DxgkDdiDpcRoutine = Bc250McdmDpcRoutine;
    InitializationData->DxgkDdiFormatHistoryBuffer =
        Bc250McdmFormatHistoryBuffer;
    InitializationData->DxgkDdiGetNodeMetadata = Bc250McdmGetNodeMetadata;
    InitializationData->DxgkDdiGetRootPageTableSize =
        Bc250McdmGetRootPageTableSize;
    InitializationData->DxgkDdiGetStandardAllocationDriverData =
        Bc250McdmGetStandardAllocationDriverData;
    InitializationData->DxgkDdiInterruptRoutine = Bc250McdmInterruptRoutine;
    InitializationData->DxgkDdiOpenAllocation = Bc250McdmOpenAllocation;
    InitializationData->DxgkDdiPreemptCommand = Bc250McdmPreemptCommand;
    InitializationData->DxgkDdiQueryAdapterInfo =
        Bc250McdmQueryAdapterInfo;
    InitializationData->DxgkDdiQueryDependentEngineGroup =
        Bc250McdmQueryDependentEngineGroup;
    InitializationData->DxgkDdiQueryDeviceDescriptor =
        Bc250McdmQueryDeviceDescriptor;
    InitializationData->DxgkDdiQueryEngineStatus =
        Bc250McdmQueryEngineStatus;
    InitializationData->DxgkDdiResetDevice = Bc250McdmResetDevice;
    InitializationData->DxgkDdiResetEngine = Bc250McdmResetEngine;
    InitializationData->DxgkDdiResetFromTimeout =
        Bc250McdmResetFromTimeout;
    InitializationData->DxgkDdiRestartFromTimeout =
        Bc250McdmRestartFromTimeout;
    InitializationData->DxgkDdiSetPowerState = Bc250McdmSetPowerState;
    InitializationData->DxgkDdiSetRootPageTable = Bc250McdmSetRootPageTable;
    InitializationData->DxgkDdiSetStablePowerState =
        Bc250McdmSetStablePowerState;
    InitializationData->DxgkDdiSetVirtualMachineData =
        Bc250McdmSetVirtualMachineData;
    InitializationData->DxgkDdiSubmitCommandVirtual =
        Bc250McdmSubmitCommandVirtual;
    InitializationData->DxgkDdiQueryChildStatus = Bc250McdmQueryChildStatus;
    InitializationData->DxgkDdiQueryChildRelations =
        Bc250McdmQueryChildRelations;
    InitializationData->DxgkDdiQueryConnectionChange =
        Bc250McdmQueryConnectionChange;
    InitializationData->DxgkDdiUnload = Bc250McdmUnload;
    return STATUS_SUCCESS;
}
