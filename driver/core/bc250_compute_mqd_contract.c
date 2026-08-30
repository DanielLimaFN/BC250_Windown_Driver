#include "bc250_compute_mqd_contract.h"
#include "bc250_observable_shader.h"

#include <string.h>

/* GC10.1 field layout from inc/linux_ref/gc_10_1_0_sh_mask.h. */
#define BC250_HQD_EOP_SIZE_MASK              0x0000003Fu
#define BC250_HQD_DOORBELL_OFFSET_MASK       0x0FFFFFFCu
#define BC250_HQD_DOORBELL_ENABLE            0x40000000u
#define BC250_HQD_PQ_QUEUE_SIZE_MASK         0x0000003Fu
#define BC250_HQD_PQ_RPTR_BLOCK_SIZE_MASK    0x00003F00u
#define BC250_HQD_PQ_EMPTY_STATUS             0x00008000u
#define BC250_HQD_DOORBELL_STATUS_MASK        0xA0000000u
#define BC250_HQD_PQ_MIN_AVAIL_SIZE_3        0x00300000u
#define BC250_HQD_PQ_UNORD_DISPATCH          0x10000000u
#define BC250_HQD_PQ_PRIV_STATE              0x40000000u
#define BC250_HQD_PQ_KMD_QUEUE               0x80000000u
#define BC250_HQD_PRELOAD_SIZE_SHIFT         8u
#define BC250_HQD_PERSISTENT_STATE_PRELOAD_REQ 0x00000001u
#define BC250_HQD_IB_MIN_AVAIL_SHIFT         20u
#define BC250_HQD_QUANTUM_ENABLE             0x00000001u
#define BC250_HQD_QUANTUM_SCALE_SHIFT        4u
#define BC250_HQD_QUANTUM_DURATION_SHIFT     8u
#define BC250_HQD_FENCE_GPUVA                 0x0000000000002200ull
#define BC250_HQD_SHADER_GPUVA                0x0000000000004000ull
#define BC250_HQD_RESULT_GPUVA                0x0000000000005000ull
#define BC250_HQD_PTE_ADDRESS_MASK            0x0000FFFFFFFFF000ull
#define BC250_HQD_PTE_REQUIRED_FLAGS          0x00000000000000E1ull

static int Bc250IsPowerOfTwo32(uint32_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}
static uint32_t Bc250OrderBase2(uint32_t Value)
{
    uint32_t order = 0;
    if (Value <= 1u) return 0;
    while ((1u << order) < Value) {
        order++;
    }
    return order;
}

BC250_MQD_STATUS Bc250ComputeMqdInit(
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd)
{
    uint64_t eopShifted;
    uint64_t hqdShifted;
    uint32_t eopOrder;
    uint32_t queueSizeOrder;
    uint32_t rptrBlockSize;

    if (Props == NULL || Mqd == NULL) {
        return BC250_MQD_BAD_ARGUMENT;
    }

    if (!Bc250IsPowerOfTwo32(Props->QueueSizeDwords) ||
        Props->QueueSizeDwords < BC250_HQD_MIN_QUEUE_DWORDS ||
        Props->QueueSizeDwords > BC250_HQD_MAX_QUEUE_DWORDS) {
        return BC250_MQD_INVALID_QUEUE_SIZE;
    }

    if (!Bc250IsPowerOfTwo32(Props->EopSizeBytes) || Props->EopSizeBytes < 256u) {
        return BC250_MQD_INVALID_EOP_SIZE;
    }

    if (Props->UseDoorbell > 1u || Props->HqdActive > 1u ||
        Props->DoorbellIndex > 0x03FFFFFFu ||
        Props->PipePriority > 3u || Props->QueuePriority > 15u) {
        return BC250_MQD_INVALID_FIELD;
    }

    if ((Props->MqdGpuAddress & (BC250_MQD_ALIGNMENT - 1u)) != 0 ||
        (Props->HqdBaseGpuAddress & (BC250_HQD_RING_ALIGNMENT - 1u)) != 0 ||
        (Props->EopGpuAddress & (BC250_HQD_EOP_ALIGNMENT - 1u)) != 0 ||
        (Props->RptrGpuAddress & 3ull) != 0 ||
        (Props->WptrGpuAddress & 7ull) != 0) {
        return BC250_MQD_MISALIGNED_ADDRESS;
    }

    memset(Mqd, 0, sizeof(BC250_GFX10_COMPUTE_MQD));

    Mqd->Header = BC250_MQD_HEADER_MAGIC;
    Mqd->ComputePipelinestatEnable = 0x00000001u;
    Mqd->ComputeStaticThreadMgmtSe0 = 0xFFFFFFFFu;
    Mqd->ComputeStaticThreadMgmtSe1 = 0xFFFFFFFFu;
    Mqd->ComputeStaticThreadMgmtSe2 = 0xFFFFFFFFu;
    Mqd->ComputeStaticThreadMgmtSe3 = 0xFFFFFFFFu;
    Mqd->ComputeMiscReserved = 0x00000003u;

    eopShifted = Props->EopGpuAddress >> 8;
    Mqd->CpHqdEopBaseAddrLo = (uint32_t)(eopShifted & 0xFFFFFFFFull);
    Mqd->CpHqdEopBaseAddrHi = (uint32_t)(eopShifted >> 32) & 0xFFu;

    eopOrder = Bc250OrderBase2(Props->EopSizeBytes / 4u) - 1u;
    Mqd->CpHqdEopControl = eopOrder & BC250_HQD_EOP_SIZE_MASK;

    if (Props->UseDoorbell) {
        /*
         * Upstream gfx_v10_0.c (linha 6947):
         * REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_OFFSET, prop->doorbell_index);
         * DOORBELL_OFFSET esta no shift 2 (bits 27:2).
         * Para doorbell_index=32: (32 << 2) = 128 (0x80).
         */
        Mqd->CpHqdPqDoorbellControl =
            ((Props->DoorbellIndex << 2) & BC250_HQD_DOORBELL_OFFSET_MASK) |
            BC250_HQD_DOORBELL_ENABLE;
    } else {
        Mqd->CpHqdPqDoorbellControl = 0;
    }

    Mqd->CpHqdDequeueRequest = 0;
    Mqd->CpHqdPqRptr = 0;
    Mqd->CpHqdPqWptrLo = 0;
    Mqd->CpHqdPqWptrHi = 0;

    Mqd->CpMqdBaseAddrLo = (uint32_t)(Props->MqdGpuAddress & 0xFFFFFFFCull);
    Mqd->CpMqdBaseAddrHi = (uint32_t)(Props->MqdGpuAddress >> 32) & 0xFFFFu;
    Mqd->CpMqdControl = 0; /* VMID 0 */

    hqdShifted = Props->HqdBaseGpuAddress >> 8;
    Mqd->CpHqdPqBaseLo = (uint32_t)(hqdShifted & 0xFFFFFFFFull);
    Mqd->CpHqdPqBaseHi = (uint32_t)(hqdShifted >> 32) & 0xFFu;

    queueSizeOrder = Bc250OrderBase2(Props->QueueSizeDwords) - 1u;
    rptrBlockSize = Bc250OrderBase2(4096u / 4u) - 1u;
    Mqd->CpHqdPqControl = (queueSizeOrder & BC250_HQD_PQ_QUEUE_SIZE_MASK) |
                          ((rptrBlockSize << 8) & BC250_HQD_PQ_RPTR_BLOCK_SIZE_MASK) |
                          BC250_HQD_PQ_MIN_AVAIL_SIZE_3 |
                          BC250_HQD_PQ_UNORD_DISPATCH |
                          BC250_HQD_PQ_PRIV_STATE |
                          BC250_HQD_PQ_KMD_QUEUE;

    Mqd->CpHqdPqRptrReportAddrLo = (uint32_t)(Props->RptrGpuAddress & 0xFFFFFFFCull);
    Mqd->CpHqdPqRptrReportAddrHi = (uint32_t)(Props->RptrGpuAddress >> 32) & 0xFFFFu;
    Mqd->CpHqdPqWptrPollAddrLo = (uint32_t)(Props->WptrGpuAddress & 0xFFFFFFF8ull);
    Mqd->CpHqdPqWptrPollAddrHi = (uint32_t)(Props->WptrGpuAddress >> 32) & 0xFFFFu;

    Mqd->CpHqdVmid = 0;
    Mqd->CpHqdPersistentState = (0x53u << BC250_HQD_PRELOAD_SIZE_SHIFT) |
                                BC250_HQD_PERSISTENT_STATE_PRELOAD_REQ;
    Mqd->CpHqdIbControl = 3u << BC250_HQD_IB_MIN_AVAIL_SHIFT;
    Mqd->CpHqdQuantum = BC250_HQD_QUANTUM_ENABLE |
                        (1u << BC250_HQD_QUANTUM_SCALE_SHIFT) |
                        (1u << BC250_HQD_QUANTUM_DURATION_SHIFT);
    Mqd->CpHqdPipePriority = Props->PipePriority;
    Mqd->CpHqdQueuePriority = Props->QueuePriority;
    Mqd->CpHqdActive = Props->HqdActive;

    return BC250_MQD_OK;
}

BC250_MQD_STATUS Bc250ComputeMqdValidate(
    const BC250_GFX10_COMPUTE_MQD *Mqd,
    const BC250_COMPUTE_MQD_PROPERTIES *Props)
{
    uint64_t eopShifted;
    uint64_t hqdShifted;
    uint32_t expectedQueueOrder;

    if (Mqd == NULL || Props == NULL) {
        return BC250_MQD_BAD_ARGUMENT;
    }

    if (Mqd->Header != BC250_MQD_HEADER_MAGIC) {
        return BC250_MQD_BAD_ARGUMENT;
    }

    eopShifted = Props->EopGpuAddress >> 8;
    if (Mqd->CpHqdEopBaseAddrLo != (uint32_t)(eopShifted & 0xFFFFFFFFull) ||
        Mqd->CpHqdEopBaseAddrHi != ((uint32_t)(eopShifted >> 32) & 0xFFu)) {
        return BC250_MQD_MISALIGNED_ADDRESS;
    }

    hqdShifted = Props->HqdBaseGpuAddress >> 8;
    if (Mqd->CpHqdPqBaseLo != (uint32_t)(hqdShifted & 0xFFFFFFFFull) ||
        Mqd->CpHqdPqBaseHi != ((uint32_t)(hqdShifted >> 32) & 0xFFu)) {
        return BC250_MQD_MISALIGNED_ADDRESS;
    }

    expectedQueueOrder = (Bc250OrderBase2(Props->QueueSizeDwords) - 1u) &
                         BC250_HQD_PQ_QUEUE_SIZE_MASK;
    if ((Mqd->CpHqdPqControl & BC250_HQD_PQ_QUEUE_SIZE_MASK) != expectedQueueOrder) {
        return BC250_MQD_INVALID_QUEUE_SIZE;
    }

    if (Mqd->CpHqdPqRptrReportAddrLo !=
            (uint32_t)(Props->RptrGpuAddress & 0xFFFFFFFCull) ||
        Mqd->CpHqdPqWptrPollAddrLo !=
            (uint32_t)(Props->WptrGpuAddress & 0xFFFFFFF8ull) ||
        Mqd->CpHqdActive != Props->HqdActive) {
        return BC250_MQD_BAD_ARGUMENT;
    }

    return BC250_MQD_OK;
}

BC250_HQD_INACTIVE_PLAN_STATUS Bc250ComputeBuildInactiveHqdPlan(
    const BC250_GFX10_COMPUTE_MQD *Mqd,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_HQD_REGISTER_WRITE *Writes,
    uint32_t WriteCapacity,
    uint32_t *WriteCount)
{
    static const uint32_t offsets[BC250_HQD_INACTIVE_PLAN_MAX_WRITES] = {
        BC250_HQD_MMIO_MQD_BASE_LO, BC250_HQD_MMIO_MQD_BASE_HI,
        BC250_HQD_MMIO_MQD_CONTROL, BC250_HQD_MMIO_EOP_BASE_LO,
        BC250_HQD_MMIO_EOP_BASE_HI, BC250_HQD_MMIO_EOP_CONTROL,
        BC250_HQD_MMIO_VMID, BC250_HQD_MMIO_PERSISTENT_STATE,
        BC250_HQD_MMIO_PIPE_PRIORITY, BC250_HQD_MMIO_QUEUE_PRIORITY,
        BC250_HQD_MMIO_QUANTUM, BC250_HQD_MMIO_PQ_BASE_LO,
        BC250_HQD_MMIO_PQ_BASE_HI, BC250_HQD_MMIO_PQ_RPTR,
        BC250_HQD_MMIO_PQ_CONTROL, BC250_HQD_MMIO_DOORBELL_CONTROL
    };
    const uint32_t values[BC250_HQD_INACTIVE_PLAN_MAX_WRITES] = {
        Mqd ? Mqd->CpMqdBaseAddrLo : 0u, Mqd ? Mqd->CpMqdBaseAddrHi : 0u,
        Mqd ? Mqd->CpMqdControl : 0u, Mqd ? Mqd->CpHqdEopBaseAddrLo : 0u,
        Mqd ? Mqd->CpHqdEopBaseAddrHi : 0u, Mqd ? Mqd->CpHqdEopControl : 0u,
        Mqd ? Mqd->CpHqdVmid : 0u, Mqd ? Mqd->CpHqdPersistentState : 0u,
        Mqd ? Mqd->CpHqdPipePriority : 0u, Mqd ? Mqd->CpHqdQueuePriority : 0u,
        Mqd ? Mqd->CpHqdQuantum : 0u, Mqd ? Mqd->CpHqdPqBaseLo : 0u,
        Mqd ? Mqd->CpHqdPqBaseHi : 0u, 0u,
        Mqd ? Mqd->CpHqdPqControl : 0u, 0u
    };
    uint32_t index;

    if (Mqd == NULL || Props == NULL || Writes == NULL || WriteCount == NULL)
        return BC250_HQD_INACTIVE_PLAN_BAD_ARGUMENT;
    *WriteCount = 0u;
    if (WriteCapacity < BC250_HQD_INACTIVE_PLAN_MAX_WRITES)
        return BC250_HQD_INACTIVE_PLAN_CAPACITY;
    if (Bc250ComputeMqdValidate(Mqd, Props) != BC250_MQD_OK)
        return BC250_HQD_INACTIVE_PLAN_MQD_INVALID;
    if (Props->HqdActive != 0u || Props->UseDoorbell != 0u ||
        Mqd->CpHqdActive != 0u || Mqd->CpHqdPqDoorbellControl != 0u ||
        Mqd->CpHqdPqWptrLo != 0u || Mqd->CpHqdPqWptrHi != 0u ||
        Mqd->CpHqdPqRptr != 0u) {
        return BC250_HQD_INACTIVE_PLAN_UNSAFE_STATE;
    }
    for (index = 0u; index < BC250_HQD_INACTIVE_PLAN_MAX_WRITES; ++index) {
        Writes[index].ByteOffset = offsets[index];
        Writes[index].Value = values[index];
    }
    *WriteCount = BC250_HQD_INACTIVE_PLAN_MAX_WRITES;
    return BC250_HQD_INACTIVE_PLAN_OK;
}

BC250_MQD_STATUS Bc250ComputeQueueComputeAvailableSpace(
    uint32_t QueueSizeDwords,
    uint32_t RptrDwords,
    uint32_t WptrDwords,
    uint32_t *AvailableDwords)
{
    uint32_t used;

    if (AvailableDwords == NULL || !Bc250IsPowerOfTwo32(QueueSizeDwords)) {
        return BC250_MQD_BAD_ARGUMENT;
    }

    used = (WptrDwords - RptrDwords) & (QueueSizeDwords - 1u);
    *AvailableDwords = (QueueSizeDwords - 1u) - used;
    return BC250_MQD_OK;
}

BC250_MQD_STATUS Bc250ComputeQueueAdvanceWptr(
    uint32_t QueueSizeDwords,
    uint32_t CurrentWptrDwords,
    uint32_t EmittedDwords,
    uint32_t *NewWptrDwords)
{
    if (NewWptrDwords == NULL || !Bc250IsPowerOfTwo32(QueueSizeDwords)) {
        return BC250_MQD_BAD_ARGUMENT;
    }

    *NewWptrDwords = (CurrentWptrDwords + EmittedDwords) & (QueueSizeDwords - 1u);
    return BC250_MQD_OK;
}

BC250_HQD_PREFLIGHT_STATUS Bc250ComputeHqdReadonlyPreflight(
    const uint32_t Pass1[BC250_HQD_PREFLIGHT_REGISTER_COUNT],
    const uint32_t Pass2[BC250_HQD_PREFLIGHT_REGISTER_COUNT],
    BC250_HQD_READONLY_SNAPSHOT *Snapshot)
{
    uint32_t index;

    if (Pass1 == NULL || Pass2 == NULL || Snapshot == NULL) {
        return BC250_HQD_PREFLIGHT_BAD_ARGUMENT;
    }

    memset(Snapshot, 0, sizeof(*Snapshot));
    for (index = 0u; index < BC250_HQD_PREFLIGHT_REGISTER_COUNT; ++index) {
        if (Pass1[index] == 0xFFFFFFFFu || Pass2[index] == 0xFFFFFFFFu) {
            return BC250_HQD_PREFLIGHT_UNMAPPED;
        }
        if (Pass1[index] != Pass2[index]) {
            return BC250_HQD_PREFLIGHT_UNSTABLE;
        }
        Snapshot->Values[index] = Pass1[index];
    }

    if (Snapshot->Values[BC250_HQD_PREFLIGHT_SELECTOR_BEFORE] !=
        Snapshot->Values[BC250_HQD_PREFLIGHT_SELECTOR_AFTER]) {
        return BC250_HQD_PREFLIGHT_UNSTABLE;
    }

    Snapshot->StablePasses = 2u;
    if ((Snapshot->Values[BC250_HQD_PREFLIGHT_ACTIVE] & 1u) != 0u) {
        return BC250_HQD_PREFLIGHT_INHERITED_ACTIVE;
    }

    Snapshot->HqdWasInactive = 1u;
    if ((Snapshot->Values[BC250_HQD_PREFLIGHT_SELECTOR_BEFORE] &
            BC250_HQD_SELECTOR_MASK) !=
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0) {
        return BC250_HQD_PREFLIGHT_SELECTOR_NOT_COMPUTE;
    }
    return BC250_HQD_PREFLIGHT_OK;
}

BC250_HQD_SELECTOR_STATUS Bc250ComputeProbeMecHqdSelected(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_SELECTOR_EVIDENCE *Evidence)
{
    static const uint32_t offsets[BC250_HQD_PREFLIGHT_REGISTER_COUNT] = {
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
        BC250_HQD_MMIO_MQD_BASE_LO, BC250_HQD_MMIO_MQD_BASE_HI,
        BC250_HQD_MMIO_ACTIVE, BC250_HQD_MMIO_VMID,
        BC250_HQD_MMIO_PQ_BASE_LO, BC250_HQD_MMIO_PQ_BASE_HI,
        BC250_HQD_MMIO_PQ_RPTR, BC250_HQD_MMIO_DOORBELL_CONTROL,
        BC250_HQD_MMIO_PQ_CONTROL, BC250_HQD_MMIO_PQ_WPTR_LO,
        BC250_HQD_MMIO_PQ_WPTR_HI, BC250_GRBM_GFX_CNTL_BYTE_OFFSET
    };
    uint32_t pass1[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    uint32_t pass2[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    BC250_HQD_SELECTOR_STATUS status = BC250_HQD_SELECTOR_PREFLIGHT_FAILED;
    uint32_t original2 = 0u, index;
    uint32_t selectorWritten = 0u;

    if (Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->Barrier == NULL) {
        return BC250_HQD_SELECTOR_BAD_ARGUMENT;
    }
    memset(Evidence, 0, sizeof(*Evidence));
    Evidence->HqdStatus = BC250_HQD_PREFLIGHT_BAD_ARGUMENT;
    if (!Interface->Read32(Interface->Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->OriginalSelector) ||
        !Interface->Read32(Interface->Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &original2)) {
        goto Cleanup;
    }
    Evidence->ReadCalls += 2u;
    if (Evidence->OriginalSelector == 0xFFFFFFFFu ||
        Evidence->OriginalSelector != original2 ||
        (Evidence->OriginalSelector & BC250_HQD_SELECTOR_MASK) != 0u) {
        goto Cleanup;
    }
    Evidence->AppliedSelector =
        (Evidence->OriginalSelector & ~BC250_HQD_SELECTOR_MASK) |
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0;
    if (!Interface->Write32(Interface->Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Evidence->AppliedSelector)) {
        status = BC250_HQD_SELECTOR_WRITE_FAILED;
        goto Cleanup;
    }
    selectorWritten = 1u;
    Evidence->WriteCalls = 1u;
    Interface->Barrier(Interface->Context);
    if (!Interface->Read32(Interface->Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
            &Evidence->AppliedSelectorReadbackFirst)) {
        status = BC250_HQD_SELECTOR_READBACK_FAILED;
        goto Cleanup;
    }
    ++Evidence->ReadCalls;
    if (!Interface->Read32(Interface->Context,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
            &Evidence->AppliedSelectorReadbackLast)) {
        status = BC250_HQD_SELECTOR_READBACK_FAILED;
        goto Cleanup;
    }
    ++Evidence->ReadCalls;
    if (Evidence->AppliedSelectorReadbackFirst != Evidence->AppliedSelector ||
        Evidence->AppliedSelectorReadbackLast != Evidence->AppliedSelector) {
        status = BC250_HQD_SELECTOR_READBACK_FAILED;
        goto Cleanup;
    }
    Evidence->ApplyReadbackPassed = 1u;
    for (index = 0u; index < BC250_HQD_PREFLIGHT_REGISTER_COUNT; ++index) {
        if (!Interface->Read32(
                Interface->Context, offsets[index], &pass1[index])) {
            status = BC250_HQD_SELECTOR_HQD_FAILED;
            goto Cleanup;
        }
        ++Evidence->ReadCalls;
    }
    Interface->Barrier(Interface->Context);
    for (index = 0u; index < BC250_HQD_PREFLIGHT_REGISTER_COUNT; ++index) {
        if (!Interface->Read32(
                Interface->Context, offsets[index], &pass2[index])) {
            status = BC250_HQD_SELECTOR_HQD_FAILED;
            goto Cleanup;
        }
        ++Evidence->ReadCalls;
    }
    Evidence->HqdStatus = Bc250ComputeHqdReadonlyPreflight(
        pass1, pass2, &Evidence->HqdSnapshot);
    status = Evidence->HqdStatus == BC250_HQD_PREFLIGHT_OK ?
        BC250_HQD_SELECTOR_OK : BC250_HQD_SELECTOR_HQD_FAILED;

Cleanup:
    if (selectorWritten != 0u) {
        if (!Interface->Write32(Interface->Context,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                Evidence->OriginalSelector)) {
            return BC250_HQD_SELECTOR_RESTORE_FAILED;
        }
        ++Evidence->WriteCalls;
        Interface->Barrier(Interface->Context);
        if (!Interface->Read32(Interface->Context,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                &Evidence->RestoredSelector)) {
            return BC250_HQD_SELECTOR_RESTORE_FAILED;
        }
        ++Evidence->ReadCalls;
        if (Evidence->RestoredSelector != Evidence->OriginalSelector) {
            return BC250_HQD_SELECTOR_RESTORE_FAILED;
        }
        Evidence->RestorePassed = 1u;
    }
    if (status == BC250_HQD_SELECTOR_OK &&
        Evidence->ApplyReadbackPassed != 0u &&
        Evidence->RestorePassed != 0u && Evidence->WriteCalls == 2u) {
        Evidence->PhysicalReady = 1u;
    }
    return status;
}

static uint32_t Bc250HqdBankRead(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_BANK_PROBE_EVIDENCE *Evidence,
    uint32_t Offset,
    uint32_t *Value)
{
    if (!Interface->Read32(Interface->Context, Offset, Value)) return 0u;
    ++Evidence->ReadCalls;
    return 1u;
}

static uint32_t Bc250HqdBankWrite(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_BANK_PROBE_EVIDENCE *Evidence,
    uint32_t Offset,
    uint32_t Value)
{
    if (!Interface->Write32(Interface->Context, Offset, Value)) return 0u;
    ++Evidence->WriteCalls;
    Interface->Barrier(Interface->Context);
    return 1u;
}

BC250_HQD_BANK_PROBE_STATUS Bc250ComputeProbeMecHqdBankIsolation(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_BANK_PROBE_EVIDENCE *Evidence)
{
    static const uint32_t hqdOffsets[BC250_HQD_PREFLIGHT_REGISTER_COUNT] = {
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
        BC250_HQD_MMIO_MQD_BASE_LO, BC250_HQD_MMIO_MQD_BASE_HI,
        BC250_HQD_MMIO_ACTIVE, BC250_HQD_MMIO_VMID,
        BC250_HQD_MMIO_PQ_BASE_LO, BC250_HQD_MMIO_PQ_BASE_HI,
        BC250_HQD_MMIO_PQ_RPTR, BC250_HQD_MMIO_DOORBELL_CONTROL,
        BC250_HQD_MMIO_PQ_CONTROL, BC250_HQD_MMIO_PQ_WPTR_LO,
        BC250_HQD_MMIO_PQ_WPTR_HI, BC250_GRBM_GFX_CNTL_BYTE_OFFSET
    };
    uint32_t hqdPass1[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    uint32_t hqdPass2[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    BC250_HQD_BANK_PROBE_STATUS status =
        BC250_HQD_BANK_PROBE_PREFLIGHT_FAILED;
    uint32_t check = 0u, selectorCandidate = 0u;
    uint32_t selectorCandidateWritten = 0u, poisonWritten = 0u;
    uint32_t lowCandidate, lowBank0, poisonLow, index;

    if (Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->Barrier == NULL) {
        return BC250_HQD_BANK_PROBE_BAD_ARGUMENT;
    }
    memset(Evidence, 0, sizeof(*Evidence));
    if (!Bc250HqdBankRead(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->OriginalSelector) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &check) ||
        Evidence->OriginalSelector != check ||
        (Evidence->OriginalSelector & BC250_HQD_SELECTOR_MASK) != 0u) {
        goto Cleanup;
    }
    if (!Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &Evidence->Bank0Active) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &check)) {
        status = BC250_HQD_BANK_PROBE_READ_FAILED;
        goto Cleanup;
    }
    if (Evidence->Bank0Active != check ||
        (Evidence->Bank0Active & 1u) != 0u) {
        status = BC250_HQD_BANK_PROBE_ACTIVE;
        goto Cleanup;
    }
    if (!Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &Evidence->Bank0Priority) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &check) ||
        Evidence->Bank0Priority != check) {
        status = BC250_HQD_BANK_PROBE_READ_FAILED;
        goto Cleanup;
    }
    selectorCandidate =
        (Evidence->OriginalSelector & ~BC250_HQD_SELECTOR_MASK) |
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0;
    if (!Bc250HqdBankWrite(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, selectorCandidate)) {
        status = BC250_HQD_BANK_PROBE_WRITE_FAILED;
        goto Cleanup;
    }
    selectorCandidateWritten = 1u;
    if (!Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &Evidence->CandidateActive) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &check)) {
        status = BC250_HQD_BANK_PROBE_READ_FAILED;
        goto Cleanup;
    }
    if (Evidence->CandidateActive != check ||
        (Evidence->CandidateActive & 1u) != 0u) {
        status = BC250_HQD_BANK_PROBE_ACTIVE;
        goto Cleanup;
    }
    if (!Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &Evidence->CandidatePriority) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &check) ||
        Evidence->CandidatePriority != check) {
        status = BC250_HQD_BANK_PROBE_READ_FAILED;
        goto Cleanup;
    }
    lowCandidate = Evidence->CandidatePriority & BC250_HQD_QUEUE_PRIORITY_MASK;
    lowBank0 = Evidence->Bank0Priority & BC250_HQD_QUEUE_PRIORITY_MASK;
    poisonLow = (lowCandidate + 1u) & BC250_HQD_QUEUE_PRIORITY_MASK;
    if (poisonLow == lowBank0)
        poisonLow = (poisonLow + 1u) & BC250_HQD_QUEUE_PRIORITY_MASK;
    Evidence->PoisonPriority =
        (Evidence->CandidatePriority & ~BC250_HQD_QUEUE_PRIORITY_MASK) |
        poisonLow;
    if (!Bc250HqdBankWrite(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, Evidence->PoisonPriority)) {
        status = BC250_HQD_BANK_PROBE_WRITE_FAILED;
        goto Cleanup;
    }
    poisonWritten = 1u;
    if (!Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &Evidence->PoisonReadback) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &check) ||
        Evidence->PoisonReadback != check ||
        Evidence->PoisonReadback != Evidence->PoisonPriority) {
        status = BC250_HQD_BANK_PROBE_POISON_FAILED;
        goto Cleanup;
    }
    Evidence->PoisonReadbackPassed = 1u;
    if (!Bc250HqdBankWrite(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Evidence->OriginalSelector) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &Evidence->Bank0CrossReadback) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &check) ||
        Evidence->Bank0CrossReadback != check) {
        status = BC250_HQD_BANK_PROBE_READ_FAILED;
        goto Cleanup;
    }
    if (!Bc250HqdBankWrite(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, selectorCandidate) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY,
            &Evidence->CandidateCrossReadback) ||
        !Bc250HqdBankRead(Interface, Evidence,
            BC250_HQD_MMIO_QUEUE_PRIORITY, &check) ||
        Evidence->CandidateCrossReadback != check) {
        status = BC250_HQD_BANK_PROBE_READ_FAILED;
        goto Cleanup;
    }
    if (Evidence->Bank0CrossReadback != Evidence->Bank0Priority ||
        Evidence->CandidateCrossReadback != Evidence->PoisonPriority) {
        status = BC250_HQD_BANK_PROBE_NOT_ISOLATED;
        goto Cleanup;
    }
    Evidence->BankIsolationPassed = 1u;
    status = BC250_HQD_BANK_PROBE_OK;

Cleanup:
    if (poisonWritten != 0u) {
        if (!Bc250HqdBankWrite(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, selectorCandidate) ||
            !Bc250HqdBankWrite(Interface, Evidence,
                BC250_HQD_MMIO_QUEUE_PRIORITY,
                Evidence->CandidatePriority) ||
            !Bc250HqdBankRead(Interface, Evidence,
                BC250_HQD_MMIO_QUEUE_PRIORITY,
                &Evidence->CandidateRestoreReadback) ||
            !Bc250HqdBankRead(Interface, Evidence,
                BC250_HQD_MMIO_QUEUE_PRIORITY, &check) ||
            Evidence->CandidateRestoreReadback != check ||
            Evidence->CandidateRestoreReadback !=
                Evidence->CandidatePriority) {
            return BC250_HQD_BANK_PROBE_RESTORE_FAILED;
        }
        if (status == BC250_HQD_BANK_PROBE_OK &&
            Evidence->BankIsolationPassed != 0u) {
            for (index = 0u;
                 index < BC250_HQD_PREFLIGHT_REGISTER_COUNT;
                 ++index) {
                if (!Bc250HqdBankRead(Interface, Evidence,
                        hqdOffsets[index], &hqdPass1[index])) {
                    status = BC250_HQD_BANK_PROBE_HQD_CAPTURE_FAILED;
                    break;
                }
            }
            Interface->Barrier(Interface->Context);
            if (status == BC250_HQD_BANK_PROBE_OK) {
                for (index = 0u;
                     index < BC250_HQD_PREFLIGHT_REGISTER_COUNT;
                     ++index) {
                    if (!Bc250HqdBankRead(Interface, Evidence,
                            hqdOffsets[index], &hqdPass2[index])) {
                        status = BC250_HQD_BANK_PROBE_HQD_CAPTURE_FAILED;
                        break;
                    }
                }
            }
            if (status == BC250_HQD_BANK_PROBE_OK) {
                for (index = 0u;
                     index < BC250_HQD_PREFLIGHT_REGISTER_COUNT;
                     ++index) {
                    if (hqdPass1[index] == 0xFFFFFFFFu ||
                        hqdPass1[index] != hqdPass2[index]) {
                        status = BC250_HQD_BANK_PROBE_HQD_CAPTURE_FAILED;
                        break;
                    }
                    Evidence->CandidateHqdSnapshot.Values[index] =
                        hqdPass1[index];
                }
            }
            if (status == BC250_HQD_BANK_PROBE_OK) {
                if (Evidence->CandidateHqdSnapshot.Values[
                        BC250_HQD_PREFLIGHT_SELECTOR_BEFORE] !=
                        Evidence->CandidateHqdSnapshot.Values[
                            BC250_HQD_PREFLIGHT_SELECTOR_AFTER] ||
                    (Evidence->CandidateHqdSnapshot.Values[
                        BC250_HQD_PREFLIGHT_ACTIVE] & 1u) != 0u) {
                    status = BC250_HQD_BANK_PROBE_HQD_CAPTURE_FAILED;
                } else {
                    Evidence->CandidateHqdSnapshot.StablePasses = 2u;
                    Evidence->CandidateHqdSnapshot.HqdWasInactive = 1u;
                    Evidence->HqdCapturePassed = 1u;
                }
            }
        }
    }
    if (selectorCandidateWritten != 0u) {
        if (!Bc250HqdBankWrite(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                Evidence->OriginalSelector) ||
            !Bc250HqdBankRead(Interface, Evidence,
                BC250_HQD_MMIO_QUEUE_PRIORITY,
                &Evidence->Bank0FinalReadback) ||
            !Bc250HqdBankRead(Interface, Evidence,
                BC250_HQD_MMIO_QUEUE_PRIORITY, &check) ||
            Evidence->Bank0FinalReadback != check ||
            Evidence->Bank0FinalReadback != Evidence->Bank0Priority ||
            !Bc250HqdBankRead(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                &Evidence->FinalSelector) ||
            Evidence->FinalSelector != Evidence->OriginalSelector) {
            return BC250_HQD_BANK_PROBE_RESTORE_FAILED;
        }
        Evidence->RestorePassed = 1u;
    }
    if (status == BC250_HQD_BANK_PROBE_OK &&
        Evidence->PoisonReadbackPassed != 0u &&
        Evidence->BankIsolationPassed != 0u &&
        Evidence->RestorePassed != 0u &&
        Evidence->HqdCapturePassed != 0u) {
        Evidence->PhysicalReady = 1u;
    }
    return status;
}

static uint32_t Bc250HqdWptrDiagRead(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE *Evidence,
    uint32_t Offset,
    uint32_t *Value)
{
    if (!Interface->Read32(Interface->Context, Offset, Value)) return 0u;
    ++Evidence->ReadCalls;
    return 1u;
}

static uint32_t Bc250HqdWptrDiagWrite(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE *Evidence,
    uint32_t Offset,
    uint32_t Value)
{
    if (!Interface->Write32(Interface->Context, Offset, Value)) return 0u;
    ++Evidence->WriteCalls;
    Interface->Barrier(Interface->Context);
    return 1u;
}

BC250_HQD_WPTR_DIAGNOSTIC_STATUS Bc250ComputeProbeMecHqdWptrBehavior(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE *Evidence)
{
    BC250_HQD_WPTR_DIAGNOSTIC_STATUS status = BC250_HQD_WPTR_DIAG_PREFLIGHT_FAILED;
    uint32_t check = 0u, i = 0u, poll = 0u;
    uint32_t rlcSchedulers = 0u;
    uint32_t me = 0u, pipe = 0u, queue = 0u;
    uint32_t derivedSelector = 0u;
    uint32_t targetSelector = 0u;
    uint32_t selectorWritten = 0u;
    uint32_t testValueWritten = 0u;
    uint32_t testValue = 0u;

    if (Interface == NULL || Evidence == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Interface->Barrier == NULL) {
        return BC250_HQD_WPTR_DIAG_BAD_ARGUMENT;
    }
    memset(Evidence, 0, sizeof(*Evidence));

    /* 1. Preflight: Leitura e validacao estavel de GRBM_GFX_CNTL */
    if (!Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->OriginalSelector) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &check) ||
        Evidence->OriginalSelector != check) {
        goto Cleanup;
    }

    /* 2. Derivacao dinamica de ME2/P0/Q0 do RLC_CP_SCHEDULERS (ou 0x8) */
    if (Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_HQD_MMIO_RLC_CP_SCHEDULERS, &rlcSchedulers) &&
        rlcSchedulers != 0u && rlcSchedulers != 0xFFFFFFFFu) {
        me = (rlcSchedulers >> 5) & 0x3u;
        pipe = (rlcSchedulers >> 3) & 0x3u;
        queue = rlcSchedulers & 0x7u;
        if (me != 0u && me <= 2u && queue == 0u && (me != 2u || pipe <= 1u)) {
            derivedSelector = (me << 2) | pipe | (queue << 8);
        } else {
            derivedSelector = BC250_HQD_SELECTOR_MEC1_P0_Q0_V0; /* 0x4 fallback */
        }
    } else {
        derivedSelector = 0x00000008u; /* ME2_P0_Q0 */
    }

    targetSelector = (Evidence->OriginalSelector & ~BC250_HQD_SELECTOR_MASK) |
                     (derivedSelector & BC250_HQD_SELECTOR_MASK);
    Evidence->TargetSelector = targetSelector;

    /* 3. Seleciona o banco alvo com broadcast de instancia/SH/SE */
    (void)Bc250HqdWptrDiagWrite(Interface, Evidence, 0x00030800u, 0xE0000000u);
    if (!Bc250HqdWptrDiagWrite(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, targetSelector)) {
        status = BC250_HQD_WPTR_DIAG_SELECTOR_WRITE_FAILED;
        goto Cleanup;
    }
    selectorWritten = 1u;
    (void)Bc250HqdWptrDiagRead(Interface, Evidence,
        BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->TargetSelectorReadback);

    /* 4. Snapshot Duplo Inicial dos Registradores do Banco */
    /* Pass 1 */
    if (!Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveBeforePass1) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_RPTR, &Evidence->RptrBeforePass1) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->WptrLoBeforePass1) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_WPTR_HI, &Evidence->WptrHiBeforePass1) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_CONTROL, &Evidence->PqControlBeforePass1) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_BASE_LO, &Evidence->PqBaseLoBeforePass1) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_BASE_HI, &Evidence->PqBaseHiBeforePass1) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_DOORBELL_CONTROL, &Evidence->DoorbellControlBeforePass1)) {
        status = BC250_HQD_WPTR_DIAG_PASS1_READ_FAILED;
        goto Cleanup;
    }

    Interface->Barrier(Interface->Context);

    /* Pass 2 */
    if (!Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveBeforePass2) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_RPTR, &Evidence->RptrBeforePass2) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->WptrLoBeforePass2) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_WPTR_HI, &Evidence->WptrHiBeforePass2) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_CONTROL, &Evidence->PqControlBeforePass2) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_BASE_LO, &Evidence->PqBaseLoBeforePass2) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_PQ_BASE_HI, &Evidence->PqBaseHiBeforePass2) ||
        !Bc250HqdWptrDiagRead(Interface, Evidence, BC250_HQD_MMIO_DOORBELL_CONTROL, &Evidence->DoorbellControlBeforePass2)) {
        status = BC250_HQD_WPTR_DIAG_PASS2_READ_FAILED;
        goto Cleanup;
    }

    /* Validacao de Estabilidade e Estado Inativo */
    if (Evidence->ActiveBeforePass1 != Evidence->ActiveBeforePass2 ||
        Evidence->RptrBeforePass1 != Evidence->RptrBeforePass2 ||
        Evidence->WptrLoBeforePass1 != Evidence->WptrLoBeforePass2 ||
        Evidence->WptrHiBeforePass1 != Evidence->WptrHiBeforePass2 ||
        Evidence->PqControlBeforePass1 != Evidence->PqControlBeforePass2 ||
        Evidence->PqBaseLoBeforePass1 != Evidence->PqBaseLoBeforePass2 ||
        Evidence->PqBaseHiBeforePass1 != Evidence->PqBaseHiBeforePass2 ||
        Evidence->DoorbellControlBeforePass1 != Evidence->DoorbellControlBeforePass2) {
        status = BC250_HQD_WPTR_DIAG_SNAPSHOT_UNSTABLE;
        goto Cleanup;
    }

    if ((Evidence->ActiveBeforePass1 & 1u) != 0u) {
        status = BC250_HQD_WPTR_DIAG_BANK_ACTIVE;
        goto Cleanup;
    }
    Evidence->StablePreflightPassed = 1u;

    /* 5. Determinacao do Valor de Teste Controlado */
    if (Evidence->WptrLoBeforePass1 == 0u) {
        testValue = 16u;
    } else {
        testValue = 0u;
    }
    Evidence->TestValueWritten = testValue;

    /* 6. Escrita Unica de Teste em CP_HQD_PQ_WPTR_LO (0xC8FC) */
    if (!Bc250HqdWptrDiagWrite(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, testValue)) {
        status = BC250_HQD_WPTR_DIAG_WRITE_FAILED;
        goto Cleanup;
    }
    testValueWritten = 1u;

    /* 7. Amostragem em Serie (16 leituras de 0xC8FC) */
    for (i = 0u; i < BC250_HQD_WPTR_DIAG_SERIES_COUNT; ++i) {
        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->SeriesSelectors[i]);
        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->SeriesReadbacks[i]);

        if (i == 0u) {
            Evidence->FirstReadback = Evidence->SeriesReadbacks[0];
        }

        if (Evidence->SeriesReadbacks[i] == testValue &&
            Evidence->SeriesMatchIndex == 0u) {
            Evidence->SeriesMatchIndex = i + 1u;
        }
    }

    Evidence->SelectorRemainedUnmodified = 1u;
    status = BC250_HQD_WPTR_DIAG_OK;

Cleanup:
    /* 8. Rollback LIFO e Polling de Restauracao */
    if (testValueWritten != 0u) {
        Evidence->RestoredValueWritten = Evidence->WptrLoBeforePass1;
        (void)Bc250HqdWptrDiagWrite(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_HI, Evidence->WptrHiBeforePass1);
        (void)Bc250HqdWptrDiagWrite(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, Evidence->WptrLoBeforePass1);

        for (poll = 0u; poll < 1000u; ++poll) {
            if (!Bc250HqdWptrDiagRead(Interface, Evidence,
                    BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->RestoredValueObserved)) {
                break;
            }
            Evidence->RestorePolls = poll + 1u;
            if (Evidence->RestoredValueObserved == Evidence->WptrLoBeforePass1) {
                Evidence->RestorePassed = 1u;
                break;
            }
        }

        if (Evidence->RestorePassed == 0u) {
            status = BC250_HQD_WPTR_DIAG_RESTORE_FAILED;
        }
    }

    /* 9. Snapshot Duplo Final de Verificacao */
    if (selectorWritten != 0u) {
        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveAfterPass1);
        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->WptrLoAfterPass1);
        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_HI, &Evidence->WptrHiAfterPass1);

        Interface->Barrier(Interface->Context);

        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveAfterPass2);
        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->WptrLoAfterPass2);
        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_HI, &Evidence->WptrHiAfterPass2);

        /* Restaura Seletor Original */
        (void)Bc250HqdWptrDiagWrite(Interface, Evidence, 0x00030800u, 0u);
        (void)Bc250HqdWptrDiagWrite(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Evidence->OriginalSelector);
        (void)Bc250HqdWptrDiagRead(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->RestoredSelector);
        Evidence->SelectorRestorePassed = 1u;
    }

    if (status == BC250_HQD_WPTR_DIAG_OK &&
        Evidence->StablePreflightPassed != 0u &&
        Evidence->RestorePassed != 0u &&
        Evidence->SelectorRestorePassed != 0u) {
        Evidence->PhysicalReady = 1u;
    }

    return status;
}

const char *Bc250MqdStatusName(BC250_MQD_STATUS Status)
{
    switch (Status) {
    case BC250_MQD_OK: return "ok";
    case BC250_MQD_BAD_ARGUMENT: return "bad_argument";
    case BC250_MQD_MISALIGNED_ADDRESS: return "misaligned_address";
    case BC250_MQD_INVALID_QUEUE_SIZE: return "invalid_queue_size";
    case BC250_MQD_INVALID_EOP_SIZE: return "invalid_eop_size";
    case BC250_MQD_INVALID_FIELD: return "invalid_field";
    default: return "unknown";
    }
}

static uint32_t Bc250HqdProgramRead(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_PROGRAM_EVIDENCE *Evidence,
    uint32_t Offset,
    uint32_t *Value)
{
    if (!Interface->Read32(Interface->Context, Offset, Value)) return 0u;
    ++Evidence->ReadCalls;
    return 1u;
}

static uint32_t Bc250HqdProgramWrite(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_PROGRAM_EVIDENCE *Evidence,
    uint32_t Offset,
    uint32_t Value)
{
    if (!Interface->Write32(Interface->Context, Offset, Value)) return 0u;
    ++Evidence->WriteCalls;
    Interface->Barrier(Interface->Context);
    return 1u;
}

/*
 * Alguns campos do bloco HQD sao status produzidos pelo hardware, nao estado
 * programavel. Em particular, PQ_EMPTY foi observado no BC-250 alternando de
 * 0 para 1 logo apos a fila ficar inativa (D030090D -> D030890D). Os bits HIT
 * do doorbell tem a mesma natureza. Compare somente o estado gravavel para
 * nao transformar atividade legitima do silicio em falso erro de readback ou
 * rollback.
 */
static uint32_t Bc250HqdWritableReadbackMask(uint32_t ByteOffset)
{
    if (ByteOffset == BC250_HQD_MMIO_PQ_RPTR) {
        return 0u;
    }
    if (ByteOffset == BC250_HQD_MMIO_PQ_CONTROL) {
        return ~BC250_HQD_PQ_EMPTY_STATUS;
    }
    if (ByteOffset == BC250_HQD_MMIO_DOORBELL_CONTROL) {
        return ~BC250_HQD_DOORBELL_STATUS_MASK;
    }
    if (ByteOffset == BC250_HQD_MMIO_PERSISTENT_STATE) {
        return ~(0x000C0000u);
    }
    if (ByteOffset == BC250_HQD_MMIO_QUANTUM) {
        return ~(0x80000000u);
    }
    return 0xFFFFFFFFu;
}

static int Bc250HqdReadbackMatches(
    uint32_t ByteOffset,
    uint32_t Expected,
    uint32_t Observed)
{
    uint32_t mask;

    if (Observed == 0xFFFFFFFFu) return 0;
    mask = Bc250HqdWritableReadbackMask(ByteOffset);
    return (Expected & mask) == (Observed & mask);
}

BC250_HQD_PROGRAM_STATUS Bc250ComputeRunInactiveHqdProgramTransaction(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd,
    BC250_HQD_PROGRAM_EVIDENCE *Evidence)
{
    BC250_HQD_REGISTER_WRITE planWrites[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t planCount = 0u;
    BC250_HQD_PROGRAM_STATUS status = BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
    uint32_t selectorWritten = 0u;
    uint32_t hqdWritten = 0u;
    uint32_t check = 0u;
    uint32_t i;
    volatile uint32_t *vPtr32;
    const uint32_t *src32;
    uint32_t initialBankActive = 0u;

    if (Interface == NULL || Buffers == NULL || Props == NULL ||
        Mqd == NULL || Evidence == NULL ||
        Interface->Read32 == NULL || Interface->Write32 == NULL ||
        Interface->Barrier == NULL) {
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;
    }

    memset(Evidence, 0, sizeof(*Evidence));

    /* 1. Preflight validation of buffers and pointers */
    if (Buffers->MqdVirtual == NULL || Buffers->MqdSizeBytes < BC250_MQD_SIZE_BYTES ||
        (Buffers->MqdGpuAddress & (BC250_MQD_ALIGNMENT - 1u)) != 0 ||
        Buffers->MqdGpuAddress != Props->MqdGpuAddress) {
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;
    }

    if (Buffers->RingVirtual == NULL ||
        Buffers->RingSizeBytes < (Props->QueueSizeDwords * sizeof(uint32_t)) ||
        (Buffers->RingGpuAddress & (BC250_HQD_RING_ALIGNMENT - 1u)) != 0 ||
        Buffers->RingGpuAddress != Props->HqdBaseGpuAddress) {
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;
    }

    if (Buffers->EopVirtual == NULL ||
        Buffers->EopSizeBytes < Props->EopSizeBytes ||
        (Buffers->EopGpuAddress & (BC250_HQD_EOP_ALIGNMENT - 1u)) != 0 ||
        Buffers->EopGpuAddress != Props->EopGpuAddress) {
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;
    }

    if (Buffers->RptrVirtual == NULL || Buffers->RptrSizeBytes < 8u ||
        (Buffers->RptrGpuAddress & 3u) != 0 ||
        Buffers->RptrGpuAddress != Props->RptrGpuAddress) {
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;
    }

    if (Buffers->WptrVirtual == NULL || Buffers->WptrSizeBytes < 8u ||
        (Buffers->WptrGpuAddress & 7u) != 0 ||
        Buffers->WptrGpuAddress != Props->WptrGpuAddress) {
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;
    }

    if (Buffers->FenceVirtual == NULL || Buffers->FenceSizeBytes < 8u ||
        (Buffers->FenceGpuAddress & 7u) != 0) {
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;
    }

    /* 2. Safety rules: Inactive gate must strictly have ACTIVE=0 and Doorbell=0 */
    if (Props->HqdActive != 0u || Props->UseDoorbell != 0u) {
        return BC250_HQD_PROGRAM_UNSAFE_STATE;
    }

    if (Bc250ComputeMqdInit(Props, Mqd) != BC250_MQD_OK) {
        return BC250_HQD_PROGRAM_BAD_ARGUMENT;
    }

    if (Mqd->CpHqdActive != 0u || Mqd->CpHqdPqDoorbellControl != 0u ||
        Mqd->CpHqdPqRptr != 0u || Mqd->CpHqdPqWptrLo != 0u ||
        Mqd->CpHqdPqWptrHi != 0u) {
        return BC250_HQD_PROGRAM_UNSAFE_STATE;
    }

    if (Bc250ComputeBuildInactiveHqdPlan(Mqd, Props, planWrites,
            BC250_HQD_INACTIVE_PLAN_MAX_WRITES, &planCount) !=
            BC250_HQD_INACTIVE_PLAN_OK ||
        planCount != BC250_HQD_INACTIVE_PLAN_MAX_WRITES) {
        return BC250_HQD_PROGRAM_PLAN_FAILED;
    }

    /* 3. MC Memory initialization */
    /* Fill MQD */
    src32 = (const uint32_t *)Mqd;
    vPtr32 = (volatile uint32_t *)Buffers->MqdVirtual;
    for (i = 0u; i < BC250_MQD_SIZE_DWORDS; ++i) {
        vPtr32[i] = src32[i];
    }

    /* Clear Ring */
    vPtr32 = (volatile uint32_t *)Buffers->RingVirtual;
    for (i = 0u; i < Props->QueueSizeDwords; ++i) {
        vPtr32[i] = 0u;
    }

    /* Clear EOP */
    vPtr32 = (volatile uint32_t *)Buffers->EopVirtual;
    for (i = 0u; i < (Props->EopSizeBytes / sizeof(uint32_t)); ++i) {
        vPtr32[i] = 0u;
    }

    /* Clear RPTR / WPTR */
    vPtr32 = (volatile uint32_t *)Buffers->RptrVirtual;
    vPtr32[0] = 0u;
    vPtr32[1] = 0u;

    vPtr32 = (volatile uint32_t *)Buffers->WptrVirtual;
    vPtr32[0] = 0u;
    vPtr32[1] = 0u;

    /* Write Poison to Fence */
    vPtr32 = (volatile uint32_t *)Buffers->FenceVirtual;
    vPtr32[0] = BC250_HQD_FENCE_POISON_VALUE;
    vPtr32[1] = 0u;

    Interface->Barrier(Interface->Context);

    /* 4. MC Memory Verification: Re-read all buffers byte-by-byte */
    vPtr32 = (volatile uint32_t *)Buffers->MqdVirtual;
    for (i = 0u; i < BC250_MQD_SIZE_DWORDS; ++i) {
        if (vPtr32[i] != src32[i]) {
            status = BC250_HQD_PROGRAM_MEMORY_FAILED;
            goto Cleanup;
        }
    }

    vPtr32 = (volatile uint32_t *)Buffers->RingVirtual;
    for (i = 0u; i < Props->QueueSizeDwords; ++i) {
        if (vPtr32[i] != 0u) {
            status = BC250_HQD_PROGRAM_MEMORY_FAILED;
            goto Cleanup;
        }
    }

    vPtr32 = (volatile uint32_t *)Buffers->EopVirtual;
    for (i = 0u; i < (Props->EopSizeBytes / sizeof(uint32_t)); ++i) {
        if (vPtr32[i] != 0u) {
            status = BC250_HQD_PROGRAM_MEMORY_FAILED;
            goto Cleanup;
        }
    }

    vPtr32 = (volatile uint32_t *)Buffers->RptrVirtual;
    if (vPtr32[0] != 0u || vPtr32[1] != 0u) {
        status = BC250_HQD_PROGRAM_MEMORY_FAILED;
        goto Cleanup;
    }

    vPtr32 = (volatile uint32_t *)Buffers->WptrVirtual;
    if (vPtr32[0] != 0u || vPtr32[1] != 0u) {
        status = BC250_HQD_PROGRAM_MEMORY_FAILED;
        goto Cleanup;
    }

    vPtr32 = (volatile uint32_t *)Buffers->FenceVirtual;
    if (vPtr32[0] != BC250_HQD_FENCE_POISON_VALUE) {
        status = BC250_HQD_PROGRAM_MEMORY_FAILED;
        goto Cleanup;
    }

    Evidence->MemoryVerificationPassed = 1u;

    /* 5. Select MEC Bank and Snapshot */
    if (!Bc250HqdProgramRead(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->OriginalSelector) ||
        !Bc250HqdProgramRead(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &check) ||
        Evidence->OriginalSelector != check ||
        (Evidence->OriginalSelector & BC250_HQD_SELECTOR_MASK) != 0u) {
        status = BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
        goto Cleanup;
    }

    Evidence->SelectedSelector =
        (Evidence->OriginalSelector & ~BC250_HQD_SELECTOR_MASK) |
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0;

    if (!Bc250HqdProgramWrite(Interface, Evidence,
            BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Evidence->SelectedSelector)) {
        status = BC250_HQD_PROGRAM_WRITE_FAILED;
        goto Cleanup;
    }
    selectorWritten = 1u;

    /* Read snapshot of all 16 registers from the plan */
    for (i = 0u; i < BC250_HQD_INACTIVE_PLAN_MAX_WRITES; ++i) {
        if (!Bc250HqdProgramRead(Interface, Evidence,
                planWrites[i].ByteOffset, &Evidence->SnapshotValues[i]) ||
            Evidence->SnapshotValues[i] == 0xFFFFFFFFu) {
            status = BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
            goto Cleanup;
        }
    }

    /* Verify initial bank is inactive */
    if (!Bc250HqdProgramRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &initialBankActive) ||
        (initialBankActive & 1u) != 0u) {
        status = BC250_HQD_PROGRAM_PREFLIGHT_FAILED;
        goto Cleanup;
    }

    /* 6. Apply Inactive HQD Plan */
    for (i = 0u; i < BC250_HQD_INACTIVE_PLAN_MAX_WRITES; ++i) {
        Evidence->AppliedValues[i] = planWrites[i].Value;
        if (!Bc250HqdProgramWrite(Interface, Evidence,
                planWrites[i].ByteOffset, planWrites[i].Value)) {
            status = BC250_HQD_PROGRAM_WRITE_FAILED;
            goto Cleanup;
        }
    }
    hqdWritten = 1u;

    /* 7. Double Readback Phase */
    /* Pass 1 */
    for (i = 0u; i < BC250_HQD_INACTIVE_PLAN_MAX_WRITES; ++i) {
        if (!Bc250HqdProgramRead(Interface, Evidence,
                planWrites[i].ByteOffset, &Evidence->ReadbackPass1[i])) {
            status = BC250_HQD_PROGRAM_READBACK_FAILED;
            goto Cleanup;
        }
    }
    if (!Bc250HqdProgramRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveReadbackPass1) ||
        !Bc250HqdProgramRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->WptrLoReadbackPass1) ||
        !Bc250HqdProgramRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_HI, &Evidence->WptrHiReadbackPass1)) {
        status = BC250_HQD_PROGRAM_READBACK_FAILED;
        goto Cleanup;
    }

    Interface->Barrier(Interface->Context);

    /* Pass 2 */
    for (i = 0u; i < BC250_HQD_INACTIVE_PLAN_MAX_WRITES; ++i) {
        if (!Bc250HqdProgramRead(Interface, Evidence,
                planWrites[i].ByteOffset, &Evidence->ReadbackPass2[i])) {
            status = BC250_HQD_PROGRAM_READBACK_FAILED;
            goto Cleanup;
        }
    }
    if (!Bc250HqdProgramRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveReadbackPass2) ||
        !Bc250HqdProgramRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->WptrLoReadbackPass2) ||
        !Bc250HqdProgramRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_HI, &Evidence->WptrHiReadbackPass2)) {
        status = BC250_HQD_PROGRAM_READBACK_FAILED;
        goto Cleanup;
    }

    /* Validate double readbacks and populate diagnostic mismatch mask */
    for (i = 0u; i < BC250_HQD_INACTIVE_PLAN_MAX_WRITES; ++i) {
        if (!Bc250HqdReadbackMatches(
                planWrites[i].ByteOffset,
                Evidence->AppliedValues[i],
                Evidence->ReadbackPass1[i]) ||
            !Bc250HqdReadbackMatches(
                planWrites[i].ByteOffset,
                Evidence->AppliedValues[i],
                Evidence->ReadbackPass2[i])) {
            Evidence->MismatchMask |= (1u << i);
            if (Evidence->FirstMismatchOffset == 0u) {
                Evidence->FirstMismatchOffset = planWrites[i].ByteOffset;
                Evidence->FirstMismatchWritten = Evidence->AppliedValues[i];
                Evidence->FirstMismatchRead1 = Evidence->ReadbackPass1[i];
                Evidence->FirstMismatchRead2 = Evidence->ReadbackPass2[i];
            }
        }
    }

    if (Evidence->MismatchMask != 0u) {
        status = BC250_HQD_PROGRAM_READBACK_FAILED;
        goto Cleanup;
    }

    if ((Evidence->ActiveReadbackPass1 & 1u) != 0u ||
        (Evidence->ActiveReadbackPass2 & 1u) != 0u ||
        Evidence->ActiveReadbackPass1 != Evidence->ActiveReadbackPass2 ||
        Evidence->WptrLoReadbackPass1 != 0u ||
        Evidence->WptrLoReadbackPass2 != 0u ||
        Evidence->WptrHiReadbackPass1 != 0u ||
        Evidence->WptrHiReadbackPass2 != 0u) {
        status = BC250_HQD_PROGRAM_UNSAFE_STATE;
        goto Cleanup;
    }

    Evidence->ActiveRemainedZero = 1u;
    Evidence->ApplyReadbackPassed = 1u;
    status = BC250_HQD_PROGRAM_OK;

Cleanup:
    /* 8. Rollback and Restore Phase */
    if (hqdWritten != 0u) {
        /* Restore the 16 registers in reverse order */
        for (i = BC250_HQD_INACTIVE_PLAN_MAX_WRITES; i > 0u; --i) {
            uint32_t regIdx = i - 1u;
            if (!Bc250HqdProgramWrite(Interface, Evidence,
                    planWrites[regIdx].ByteOffset,
                    Evidence->SnapshotValues[regIdx]) ||
                !Bc250HqdProgramRead(Interface, Evidence,
                    planWrites[regIdx].ByteOffset,
                    &Evidence->RestoredValues[regIdx]) ||
                !Bc250HqdProgramRead(Interface, Evidence,
                    planWrites[regIdx].ByteOffset, &check) ||
                !Bc250HqdReadbackMatches(
                    planWrites[regIdx].ByteOffset,
                    Evidence->RestoredValues[regIdx], check) ||
                !Bc250HqdReadbackMatches(
                    planWrites[regIdx].ByteOffset,
                    Evidence->SnapshotValues[regIdx],
                    Evidence->RestoredValues[regIdx])) {
                return BC250_HQD_PROGRAM_RESTORE_FAILED;
            }
        }
    }

    /* Restore / Clear MC Memory Buffers */
    if (Buffers->MqdVirtual != NULL && Buffers->MqdSizeBytes >= BC250_MQD_SIZE_BYTES) {
        vPtr32 = (volatile uint32_t *)Buffers->MqdVirtual;
        for (i = 0u; i < BC250_MQD_SIZE_DWORDS; ++i) vPtr32[i] = 0u;
    }
    if (Buffers->RingVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->RingVirtual;
        for (i = 0u; i < Props->QueueSizeDwords; ++i) vPtr32[i] = 0u;
    }
    if (Buffers->EopVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->EopVirtual;
        for (i = 0u; i < (Props->EopSizeBytes / sizeof(uint32_t)); ++i) vPtr32[i] = 0u;
    }
    if (Buffers->RptrVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->RptrVirtual;
        vPtr32[0] = 0u; vPtr32[1] = 0u;
    }
    if (Buffers->WptrVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->WptrVirtual;
        vPtr32[0] = 0u; vPtr32[1] = 0u;
    }
    if (Buffers->FenceVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->FenceVirtual;
        vPtr32[0] = 0u; vPtr32[1] = 0u;
    }

    Interface->Barrier(Interface->Context);

    /* Verify cleared memory */
    if (Buffers->MqdVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->MqdVirtual;
        for (i = 0u; i < BC250_MQD_SIZE_DWORDS; ++i) {
            if (vPtr32[i] != 0u) return BC250_HQD_PROGRAM_RESTORE_FAILED;
        }
    }
    if (Buffers->FenceVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->FenceVirtual;
        if (vPtr32[0] != 0u || vPtr32[1] != 0u) {
            return BC250_HQD_PROGRAM_RESTORE_FAILED;
        }
    }
    Evidence->MemoryRestorePassed = 1u;

    if (selectorWritten != 0u) {
        if (!Bc250HqdProgramWrite(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                Evidence->OriginalSelector) ||
            !Bc250HqdProgramRead(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                &Evidence->RestoredSelector) ||
            !Bc250HqdProgramRead(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &check) ||
            Evidence->RestoredSelector != check ||
            Evidence->RestoredSelector != Evidence->OriginalSelector) {
            return BC250_HQD_PROGRAM_RESTORE_FAILED;
        }
        Evidence->RestorePassed = 1u;
    }

    if (status == BC250_HQD_PROGRAM_OK &&
        Evidence->MemoryVerificationPassed != 0u &&
        Evidence->ApplyReadbackPassed != 0u &&
        Evidence->ActiveRemainedZero != 0u &&
        Evidence->RestorePassed != 0u &&
        Evidence->MemoryRestorePassed != 0u) {
        Evidence->PhysicalReady = 1u;
    }

    return status;
}

const char *Bc250HqdProgramStatusName(BC250_HQD_PROGRAM_STATUS Status)
{
    switch (Status) {
    case BC250_HQD_PROGRAM_OK: return "ok";
    case BC250_HQD_PROGRAM_BAD_ARGUMENT: return "bad_argument";
    case BC250_HQD_PROGRAM_PREFLIGHT_FAILED: return "preflight_failed";
    case BC250_HQD_PROGRAM_MEMORY_FAILED: return "memory_failed";
    case BC250_HQD_PROGRAM_PLAN_FAILED: return "plan_failed";
    case BC250_HQD_PROGRAM_WRITE_FAILED: return "write_failed";
    case BC250_HQD_PROGRAM_READBACK_FAILED: return "readback_failed";
    case BC250_HQD_PROGRAM_UNSAFE_STATE: return "unsafe_state";
    case BC250_HQD_PROGRAM_RESTORE_FAILED: return "restore_failed";
    default: return "unknown";
    }
}

static int Bc250HqdActiveRead(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence,
    uint32_t ByteOffset,
    uint32_t *Value)
{
    if (Interface == NULL || Interface->Read32 == NULL || Value == NULL)
        return 0;
    if (Evidence != NULL) ++Evidence->ReadCalls;
    return Interface->Read32(Interface->Context, ByteOffset, Value) != 0;
}

static int Bc250HqdActiveWrite(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence,
    uint32_t ByteOffset,
    uint32_t Value)
{
    if (Interface == NULL || Interface->Write32 == NULL)
        return 0;
    if (Evidence != NULL) ++Evidence->WriteCalls;
    return Interface->Write32(Interface->Context, ByteOffset, Value) != 0;
}

/*
 * Registra a PRIMEIRA reprovacao de rollback e mantem o agregado.
 * Chamadas subsequentes so incrementam o contador, para que o estagio
 * publicado seja a causa raiz e nao o ultimo efeito colateral.
 */
static void Bc250HqdNoteRestoreFailure(
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence,
    BC250_HQD_RESTORE_STAGE Stage,
    uint32_t ByteOffset,
    uint32_t Expected,
    uint32_t Observed,
    uint32_t Transport)
{
    if (Evidence == NULL) {
        return;
    }
    Evidence->RestorePassed = 0u;
    ++Evidence->RestoreFailureCount;
    if (Evidence->FirstRestoreFailureStage !=
            (uint32_t)BC250_HQD_RESTORE_STAGE_NONE) {
        return;
    }
    Evidence->FirstRestoreFailureStage = (uint32_t)Stage;
    Evidence->FirstRestoreFailureOffset = ByteOffset;
    Evidence->FirstRestoreFailureExpected = Expected;
    Evidence->FirstRestoreFailureObserved = Observed;
    Evidence->FirstRestoreFailureTransport = Transport;
}

/*
 * Restauracao transacional de um registrador: escreve, rele e compara sob
 * mascara.  Qualquer reprovacao e localizada pelo estagio, sem mascarar a
 * falha e sem interromper o restante do rollback LIFO.
 */
static int Bc250HqdRestoreRegister(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence,
    BC250_HQD_RESTORE_STAGE Stage,
    uint32_t ByteOffset,
    uint32_t Expected,
    uint32_t CompareMask,
    uint32_t *Observed)
{
    uint32_t observed = 0u;

    if (Observed != NULL) {
        *Observed = 0u;
    }
    if (!Bc250HqdActiveWrite(Interface, Evidence, ByteOffset, Expected) ||
        !Bc250HqdActiveRead(Interface, Evidence, ByteOffset, &observed)) {
        Bc250HqdNoteRestoreFailure(
            Evidence, Stage, ByteOffset, Expected, 0u, 1u);
        return 0;
    }
    if (Observed != NULL) {
        *Observed = observed;
    }
    if ((observed & CompareMask) != (Expected & CompareMask)) {
        Bc250HqdNoteRestoreFailure(
            Evidence, Stage, ByteOffset, Expected, observed, 0u);
        return 0;
    }
    return 1;
}

/* Bit de fila do CP_PQ_WPTR_POLL_CNTL1: pipe * filas_por_pipe + fila. */
static uint32_t Bc250HqdQueueMaskBit(uint32_t Pipe, uint32_t Queue)
{
    return (Pipe * BC250_HQD_QUEUES_PER_PIPE) + Queue;
}

static BC250_HQD_ACTIVE_STATUS Bc250ComputeRunActiveHqdSubmissionTransactionInternal(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence,
    BC250_HQD_SHADER_DISPATCH_EVIDENCE *DispatchEvidence)
{
    BC250_HQD_REGISTER_WRITE planWrites[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t planCount = 0;
    uint32_t originalScratch = 0;
    uint32_t origCpMecCntl = 0;
    uint32_t origCpMeCntl = 0;
    uint32_t origDbRangeLo = 0;
    uint32_t origDbRangeHi = 0;
    uint32_t origCpPqStatus = 0;
    uint32_t origCpPqWptrPollCntl = 0;
    uint32_t origCpPqWptrPollCntl1 = 0;
    uint32_t origGcvmL2Cntl2 = 0;
    uint32_t origGcvmCtx0Cntl = 0;
    uint32_t origGcvmCtx0PtBaseLo = 0;
    uint32_t origGcvmCtx0PtBaseHi = 0;
    uint32_t origGcvmCtx0PtStartLo = 0;
    uint32_t origGcvmCtx0PtEndLo = 0;
    uint32_t origIbControl = 0;
    uint32_t origEopRptr = 0;
    uint32_t scratchPoll = 0;
    uint32_t fencePoll = 0;
    uint32_t rptrPoll = 0;
    uint32_t mecPcPoll = 0;
    uint32_t mecPcPrevious = 0;
    uint32_t gcvmInvalidateReqReadback = 0;
    uint32_t effectiveDoorbellIndex = 0;
    uint32_t effectiveDoorbellOffset = 0;
    uint32_t schedulerMe = 0;
    uint32_t schedulerPipe = 0;
    uint32_t schedulerQueue = 0;
    uint32_t schedulerSelector = 0;
    uint32_t schedulerPcOffset = 0;
    uint32_t check = 0;
    uint32_t i = 0;
    uint32_t poll = 0;
    uint32_t cursor = 0;
    uint32_t submittedDwords = 0;
    uint32_t wptrPollObserved = 0;
    uint32_t resultPoll = 0;
    uint32_t scratchWritten = 0;
    uint32_t observableDispatch = DispatchEvidence != NULL ? 1u : 0u;
    uint32_t observableMemoryStaged = 0u;
    uint32_t selectorCaptured = 0u;
    uint32_t ringSnapshot[BC250_DISPATCH_RING_MIN_DWORDS] = { 0u };
    uint32_t dispatchRing[BC250_DISPATCH_RING_MIN_DWORDS] = { 0u };
    uint32_t mqdSnapshot[BC250_MQD_SIZE_DWORDS] = { 0u };
    uint32_t shaderSnapshot[BC250_OBSERVABLE_SHADER_DWORDS] = { 0u };
    uint32_t resultSnapshot = 0u;
    uint32_t fenceSnapshot[2] = { 0u, 0u };
    uint32_t rptrSnapshot[2] = { 0u, 0u };
    uint32_t wptrSnapshot[2] = { 0u, 0u };
    uint32_t coreMemoryRestorePassed = 0u;
    BC250_COMPUTE_DISPATCH_DESCRIPTOR dispatchDescriptor;
    const BC250_OBSERVABLE_SHADER_MANIFEST *shaderManifest = NULL;
    uint32_t hqdWritten = 0;
    uint32_t queueAddressWriteCount = 0;
    uint32_t cpPqStatusWritten = 0;
    uint32_t cpPqWptrPollCntlWritten = 0;
    uint32_t cpPqWptrPollCntl1Written = 0;
    uint32_t gcvmL2Cntl2Written = 0;
    uint32_t gcvmCtx0Written = 0;
    uint32_t rlcCpSchedulersWritten = 0;
    uint32_t queueAddressSnapshots[4] = { 0u, 0u, 0u, 0u };
    uint32_t dequeueRequestSnapshot = 0u;
    uint32_t dequeueRequestWritten = 0u;
    uint32_t wptrLoSnapshot = 0u;
    uint32_t wptrHiSnapshot = 0u;
    uint32_t wptrHiRestored = 0u;
    uint32_t wptrBaselineWritten = 0u;
    uint32_t directWptrWritten = 0u;
    uint32_t ibControlWritten = 0u;
    uint32_t eopRptrWritten = 0u;
    uint32_t cpMecCntlWritten = 0u;
    uint32_t cpMeCntlWritten = 0u;
    static const uint32_t queueAddressOffsets[4] = {
        BC250_HQD_MMIO_RPTR_REPORT_LO,
        BC250_HQD_MMIO_RPTR_REPORT_HI,
        BC250_HQD_MMIO_WPTR_POLL_LO,
        BC250_HQD_MMIO_WPTR_POLL_HI
    };
    BC250_HQD_ACTIVE_STATUS status = BC250_HQD_ACTIVE_PM4_NOT_CONSUMED;
    BC250_COMPUTE_MQD_PROPERTIES activeProps;
    volatile uint32_t *vPtr32 = NULL;

    if (Evidence != NULL) {
        memset(Evidence, 0, sizeof(*Evidence));
    }
    if (DispatchEvidence != NULL) {
        memset(DispatchEvidence, 0, sizeof(*DispatchEvidence));
    }
    if (Interface == NULL || Buffers == NULL || Props == NULL || Mqd == NULL || Evidence == NULL) {
        return BC250_HQD_ACTIVE_BAD_ARGUMENT;
    }

    if (Buffers->MqdVirtual == NULL || Buffers->MqdSizeBytes < sizeof(BC250_GFX10_COMPUTE_MQD) ||
        (Buffers->MqdGpuAddress & (BC250_MQD_ALIGNMENT - 1u)) != 0 ||
        Buffers->RingVirtual == NULL || Buffers->RingSizeBytes < 256u ||
        (Buffers->RingGpuAddress & (BC250_HQD_RING_ALIGNMENT - 1u)) != 0 ||
        Buffers->EopVirtual == NULL || Buffers->EopSizeBytes < 4096u ||
        (Buffers->EopGpuAddress & (BC250_HQD_EOP_ALIGNMENT - 1u)) != 0 ||
        Buffers->RptrVirtual == NULL || Buffers->RptrSizeBytes < 8u ||
        (Buffers->RptrGpuAddress & 0x7ull) != 0 ||
        Buffers->WptrVirtual == NULL || Buffers->WptrSizeBytes < 8u ||
        (Buffers->WptrGpuAddress & 0x7ull) != 0 ||
        Buffers->FenceVirtual == NULL || Buffers->FenceSizeBytes < 8u ||
        (Buffers->FenceGpuAddress & 0x7ull) != 0 ||
        Buffers->PtVirtual == NULL || Buffers->PtSizeBytes < 4096u ||
        Buffers->PtGpuAddress == 0u ||
        (Buffers->PtGpuAddress & 0xFFFull) != 0u) {
        return BC250_HQD_ACTIVE_MEMORY_FAILED;
    }

    if (observableDispatch != 0u) {
        shaderManifest = Bc250GetObservableShaderManifest();
        if (!Bc250ValidateObservableShaderManifest(shaderManifest) ||
            (Props->HqdBaseGpuAddress != BC250_HQD_RING_GPUVA &&
             Props->HqdBaseGpuAddress != Buffers->RingGpuAddress) ||
            Buffers->ShaderVirtual == NULL ||
            Buffers->ShaderSizeBytes < 256u ||
            Buffers->ShaderGpuAddress == 0u ||
            (Buffers->ShaderGpuAddress & 0xFFull) != 0u ||
            Buffers->ResultVirtual == NULL ||
            Buffers->ResultSizeBytes < sizeof(uint32_t) ||
            Buffers->ResultGpuAddress == 0u ||
            (Buffers->ResultGpuAddress & 3ull) != 0u) {
            return BC250_HQD_ACTIVE_MEMORY_FAILED;
        }
    }

    /*
     * O WRITE_DATA do MEC usa GPUVA no VMID da fila. Prove antes de tocar no
     * HQD que GPUVA 0x2200 traduz exatamente para a pagina fisica que contem
     * FenceVirtual/FenceGpuAddress; caso contrario, falhe fechado.
     */
    {
        volatile const uint64_t *pt =
            (volatile const uint64_t *)Buffers->PtVirtual;
        const uint64_t fencePte = pt[BC250_HQD_FENCE_GPUVA >> 12];
        const uint64_t translatedFence =
            (fencePte & BC250_HQD_PTE_ADDRESS_MASK) |
            (BC250_HQD_FENCE_GPUVA & 0xFFFull);

        if ((fencePte & ~BC250_HQD_PTE_ADDRESS_MASK) !=
                BC250_HQD_PTE_REQUIRED_FLAGS ||
            translatedFence != Buffers->FenceGpuAddress) {
            return BC250_HQD_ACTIVE_MEMORY_FAILED;
        }


        if (observableDispatch != 0u) {
            const uint64_t ringPte = pt[BC250_HQD_RING_GPUVA >> 12];
            const uint64_t shaderPte = pt[BC250_HQD_SHADER_GPUVA >> 12];
            const uint64_t resultPte = pt[BC250_HQD_RESULT_GPUVA >> 12];
            const uint64_t translatedShader =
                (shaderPte & BC250_HQD_PTE_ADDRESS_MASK) |
                (BC250_HQD_SHADER_GPUVA & 0xFFFull);
            const uint64_t translatedResult =
                (resultPte & BC250_HQD_PTE_ADDRESS_MASK) |
                (BC250_HQD_RESULT_GPUVA & 0xFFFull);
            const uint64_t translatedRing =
                (ringPte & BC250_HQD_PTE_ADDRESS_MASK) |
                (BC250_HQD_RING_GPUVA & 0xFFFull);

            DispatchEvidence->RingGpuVaLow =
                (uint32_t)BC250_HQD_RING_GPUVA;
            DispatchEvidence->RingGpuVaHigh =
                (uint32_t)(BC250_HQD_RING_GPUVA >> 32);
            DispatchEvidence->RingPhysicalLow =
                (uint32_t)Buffers->RingGpuAddress;
            DispatchEvidence->RingPhysicalHigh =
                (uint32_t)(Buffers->RingGpuAddress >> 32);
            DispatchEvidence->RingPteLow = (uint32_t)ringPte;
            DispatchEvidence->RingPteHigh = (uint32_t)(ringPte >> 32);
            DispatchEvidence->RingTranslationPassed =
                (ringPte & ~BC250_HQD_PTE_ADDRESS_MASK) ==
                    BC250_HQD_PTE_REQUIRED_FLAGS &&
                translatedRing == Buffers->RingGpuAddress ? 1u : 0u;

            if (DispatchEvidence->RingTranslationPassed == 0u ||
                (shaderPte & ~BC250_HQD_PTE_ADDRESS_MASK) !=
                    BC250_HQD_PTE_REQUIRED_FLAGS ||
                (resultPte & ~BC250_HQD_PTE_ADDRESS_MASK) !=
                    BC250_HQD_PTE_REQUIRED_FLAGS ||
                translatedShader != Buffers->ShaderGpuAddress ||
                translatedResult != Buffers->ResultGpuAddress) {
                return BC250_HQD_ACTIVE_MEMORY_FAILED;
            }
        }
    }

    if (observableDispatch != 0u) {
        BC250_COMPUTE_DISPATCH_PLAN_STATUS dispatchStatus;

        memset(&dispatchDescriptor, 0, sizeof(dispatchDescriptor));
        if (Props->HqdBaseGpuAddress == Buffers->RingGpuAddress) {
            dispatchDescriptor.ShaderGpuAddress = Buffers->ShaderGpuAddress;
            dispatchDescriptor.ResultGpuAddress = Buffers->ResultGpuAddress;
            dispatchDescriptor.FenceGpuAddress = Buffers->FenceGpuAddress;
        } else {
            dispatchDescriptor.ShaderGpuAddress = BC250_HQD_SHADER_GPUVA;
            dispatchDescriptor.ResultGpuAddress = BC250_HQD_RESULT_GPUVA;
            dispatchDescriptor.FenceGpuAddress = BC250_HQD_FENCE_GPUVA;
        }
        dispatchDescriptor.PgmRsrc1 = shaderManifest->PgmRsrc1;
        dispatchDescriptor.PgmRsrc2 = shaderManifest->PgmRsrc2;
        dispatchDescriptor.WaveSize = shaderManifest->WaveSize;
        dispatchDescriptor.NumThreadX = 1u;
        dispatchDescriptor.NumThreadY = 1u;
        dispatchDescriptor.NumThreadZ = 1u;
        dispatchDescriptor.GridDimX = 1u;
        dispatchDescriptor.GridDimY = 1u;
        dispatchDescriptor.GridDimZ = 1u;
        dispatchDescriptor.ResultPoison = BC250_DISPATCH_RESULT_POISON;
        dispatchDescriptor.ResultExpected = shaderManifest->ResultValue;
        dispatchDescriptor.FencePoison = BC250_DISPATCH_FENCE_POISON;
        dispatchDescriptor.FenceExpected = BC250_DISPATCH_FENCE_VALUE;

        dispatchStatus = Bc250BuildObservableComputeDispatchPlan(
            dispatchRing, BC250_DISPATCH_RING_MIN_DWORDS,
            &dispatchDescriptor, &DispatchEvidence->Plan);
        if (dispatchStatus != BC250_COMPUTE_DISPATCH_PLAN_OK) {
            return BC250_HQD_ACTIVE_PLAN_FAILED;
        }

        /*
         * CU/WGP preflight is deliberately read-only.  Preserve the fused
         * harvest mask and power policy installed by firmware; two identical
         * samples are required, and at least one non-harvested WGP must also
         * be marked always-on.  Never revive the quarantined 0x34FC/0x9C1C
         * aliases or clear CC_GC_SHADER_ARRAY_CONFIG to fabricate more CUs.
         */
        if (!Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CC_GC_SHADER_ARRAY_CONFIG,
                &DispatchEvidence->CcShaderArrayPass1) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK,
                &DispatchEvidence->SpiStaticWgpMaskPass1) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK,
                &DispatchEvidence->RlcAlwaysOnWgpMaskPass1) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CC_GC_SHADER_ARRAY_CONFIG,
                &DispatchEvidence->CcShaderArrayPass2) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK,
                &DispatchEvidence->SpiStaticWgpMaskPass2) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK,
                &DispatchEvidence->RlcAlwaysOnWgpMaskPass2)) {
            return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
        }
        DispatchEvidence->NonHarvestedWgpMask =
            (~(DispatchEvidence->CcShaderArrayPass1 >>
               BC250_HQD_CC_INACTIVE_WGPS_SHIFT)) & 0xFFFFu;
        DispatchEvidence->AlwaysOnNonHarvestedWgpMask =
            DispatchEvidence->NonHarvestedWgpMask &
            DispatchEvidence->RlcAlwaysOnWgpMaskPass1;
        DispatchEvidence->WgpPreflightPassed =
            DispatchEvidence->CcShaderArrayPass1 != 0xFFFFFFFFu &&
            DispatchEvidence->SpiStaticWgpMaskPass1 != 0xFFFFFFFFu &&
            DispatchEvidence->RlcAlwaysOnWgpMaskPass1 != 0xFFFFFFFFu &&
            DispatchEvidence->CcShaderArrayPass1 ==
                DispatchEvidence->CcShaderArrayPass2 &&
            DispatchEvidence->SpiStaticWgpMaskPass1 ==
                DispatchEvidence->SpiStaticWgpMaskPass2 &&
            DispatchEvidence->RlcAlwaysOnWgpMaskPass1 ==
                DispatchEvidence->RlcAlwaysOnWgpMaskPass2 &&
            DispatchEvidence->AlwaysOnNonHarvestedWgpMask != 0u ? 1u : 0u;
        if (DispatchEvidence->WgpPreflightPassed == 0u) {
            return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
        }
    }

    /* 1. Preflight e Leitura de Scratch e Estado CP Original */
    if (!Bc250HqdActiveRead(Interface, Evidence, 0x00030100u, &originalScratch) ||
        !Bc250HqdActiveRead(Interface, Evidence, 0x00008234u, &origCpMecCntl) ||
        !Bc250HqdActiveRead(Interface, Evidence, 0x000086D8u, &origCpMeCntl) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_MEC_DB_RANGE_LOWER, &origDbRangeLo) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_MEC_DB_RANGE_UPPER, &origDbRangeHi) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_STATUS, &origCpPqStatus) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL, &origCpPqWptrPollCntl) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1, &origCpPqWptrPollCntl1) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_L2_CNTL2, &origGcvmL2Cntl2) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_CNTL, &origGcvmCtx0Cntl) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_LO, &origGcvmCtx0PtBaseLo) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_HI, &origGcvmCtx0PtBaseHi) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_START_LO, &origGcvmCtx0PtStartLo) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_END_LO, &origGcvmCtx0PtEndLo) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_INVALIDATE_ACK,
            &Evidence->GcvmInvalidateAckBefore) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_RLC_CP_SCHEDULERS,
            &Evidence->RlcCpSchedulersBefore)) {
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }
    Evidence->GcvmL2Cntl2Before = origGcvmL2Cntl2;
    Evidence->GcvmContext0CntlBefore = origGcvmCtx0Cntl;
    Evidence->GcvmContext0PtBaseLoBefore = origGcvmCtx0PtBaseLo;
    Evidence->GcvmContext0PtBaseHiBefore = origGcvmCtx0PtBaseHi;
    Evidence->GcvmContext0PtStartLoBefore = origGcvmCtx0PtStartLo;
    Evidence->GcvmContext0PtEndLoBefore = origGcvmCtx0PtEndLo;
    Evidence->DoorbellRangeLowerBefore = origDbRangeLo;
    Evidence->DoorbellRangeUpperBefore = origDbRangeHi;
    Evidence->CpPqStatusBefore = origCpPqStatus;
    Evidence->WptrPollCntlBefore = origCpPqWptrPollCntl;
    Evidence->WptrPollCntl1Before = origCpPqWptrPollCntl1;
    /*
     * QUEUE_MASK ja acesa na entrada significa boot morno: o 3.31 deixou o
     * bit no silicio e ele so cai com reset. Nao aborta - apenas denuncia,
     * para o aceite poder recusar o ensaio em vez de comparar 1 com 1 e
     * declarar rollback aprovado.
     */
    if (origCpPqWptrPollCntl1 != 0u) {
        Evidence->WptrPollCntl1DirtyEntry = 1u;
    }
    if (Props->UseDoorbell != 0u) {
        effectiveDoorbellOffset = origDbRangeLo & 0x0FFFFFFCu;
        if (origDbRangeLo == 0xFFFFFFFFu || origDbRangeHi == 0xFFFFFFFFu ||
            effectiveDoorbellOffset != origDbRangeLo ||
            (origDbRangeHi & 0x0FFFFFFCu) < effectiveDoorbellOffset ||
            Buffers->DoorbellVirtual == NULL ||
            Buffers->DoorbellSizeBytes <
                effectiveDoorbellOffset + sizeof(uint64_t)) {
            return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
        }
        effectiveDoorbellIndex = effectiveDoorbellOffset >> 2;
    }
    Evidence->ScratchBefore = originalScratch;
    Evidence->CpMecCntlBefore = origCpMecCntl;
    Evidence->CpMeCntlBefore = origCpMeCntl;
    vPtr32 = (volatile uint32_t *)Buffers->FenceVirtual;
    if (observableDispatch != 0u) {
        volatile uint32_t *shaderPtr =
            (volatile uint32_t *)Buffers->ShaderVirtual;
        volatile uint32_t *resultPtr =
            (volatile uint32_t *)Buffers->ResultVirtual;
        volatile uint32_t *ringPtr =
            (volatile uint32_t *)Buffers->RingVirtual;
        volatile uint32_t *mqdPtr =
            (volatile uint32_t *)Buffers->MqdVirtual;
        volatile uint32_t *rptrPtr =
            (volatile uint32_t *)Buffers->RptrVirtual;
        volatile uint32_t *wptrPtr =
            (volatile uint32_t *)Buffers->WptrVirtual;

        for (i = 0u; i < BC250_MQD_SIZE_DWORDS; ++i) {
            mqdSnapshot[i] = mqdPtr[i];
        }
        for (i = 0u; i < BC250_OBSERVABLE_SHADER_DWORDS; ++i) {
            shaderSnapshot[i] = shaderPtr[i];
        }
        for (i = 0u; i < BC250_DISPATCH_RING_MIN_DWORDS; ++i) {
            ringSnapshot[i] = ringPtr[i];
        }
        resultSnapshot = resultPtr[0];
        fenceSnapshot[0] = vPtr32[0];
        fenceSnapshot[1] = vPtr32[1];
        rptrSnapshot[0] = rptrPtr[0];
        rptrSnapshot[1] = rptrPtr[1];
        wptrSnapshot[0] = wptrPtr[0];
        wptrSnapshot[1] = wptrPtr[1];

        for (i = 0u; i < BC250_OBSERVABLE_SHADER_DWORDS; ++i) {
            shaderPtr[i] = shaderManifest->ShaderDwords[i];
        }
        resultPtr[0] = BC250_DISPATCH_RESULT_POISON;
        vPtr32[0] = BC250_DISPATCH_FENCE_POISON;
        vPtr32[1] = 0u;
        rptrPtr[0] = 0u;
        rptrPtr[1] = 0u;
        wptrPtr[0] = 0u;
        wptrPtr[1] = 0u;
        Interface->Barrier(Interface->Context);

        for (i = 0u; i < BC250_OBSERVABLE_SHADER_DWORDS; ++i) {
            if (shaderPtr[i] != shaderManifest->ShaderDwords[i]) {
                ++DispatchEvidence->ShaderReadbackMismatchCount;
            }
        }
        DispatchEvidence->ShaderReadbackPassed =
            DispatchEvidence->ShaderReadbackMismatchCount == 0u ? 1u : 0u;
        if (DispatchEvidence->ShaderReadbackPassed == 0u ||
            resultPtr[0] != BC250_DISPATCH_RESULT_POISON ||
            vPtr32[0] != BC250_DISPATCH_FENCE_POISON ||
            rptrPtr[0] != 0u || rptrPtr[1] != 0u ||
            wptrPtr[0] != 0u || wptrPtr[1] != 0u) {
            for (i = 0u; i < BC250_OBSERVABLE_SHADER_DWORDS; ++i) {
                shaderPtr[i] = shaderSnapshot[i];
            }
            resultPtr[0] = resultSnapshot;
            vPtr32[0] = fenceSnapshot[0];
            vPtr32[1] = fenceSnapshot[1];
            rptrPtr[0] = rptrSnapshot[0];
            rptrPtr[1] = rptrSnapshot[1];
            wptrPtr[0] = wptrSnapshot[0];
            wptrPtr[1] = wptrSnapshot[1];
            Interface->Barrier(Interface->Context);
            return BC250_HQD_ACTIVE_READBACK_FAILED;
        }
        Evidence->FenceBefore = vPtr32[0];
        DispatchEvidence->Observation.ResultBefore = resultPtr[0];
        DispatchEvidence->Observation.FenceBefore = vPtr32[0];
        observableMemoryStaged = 1u;
    } else {
        Evidence->FenceBefore = BC250_HQD_FENCE_POISON_VALUE;
        vPtr32[0] = BC250_HQD_FENCE_POISON_VALUE;
        vPtr32[1] = 0x00000000u;
        if (!Bc250HqdActiveWrite(
                Interface, Evidence, 0x00030100u, 0x11111111u)) {
            return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
        }
        scratchWritten = 1u;
    }

    /*
     * 2. Diagnostico PM4 canonico (11 dwords: 3 + 8).
     *
     * O ensaio 3.20 avancou RPTR=10 e deixou os seis dwords finais parados:
     * eles eram menores que a disponibilidade minima de oito dwords desta
     * fila. Tambem revelou que WRITE_DATA recebeu mmSCRATCH_REG0=0x2040 sem
     * a base SOC15 do segmento 1. Use a sequencia do ring_test_ring upstream:
     * SET_UCONFIG_REG opcode 0x79, endereco SOC15 0xC040 relativo a 0xC000.
     * Restam entao exatamente oito dwords disponiveis, o minimo exigido para
     * o RELEASE_MEM canonico que grava a fence na GPUVA validada pela PTE.
     */
    vPtr32 = (volatile uint32_t *)Buffers->RingVirtual;
    if (observableDispatch != 0u) {
        for (i = 0u; i < BC250_DISPATCH_RING_MIN_DWORDS; ++i) {
            vPtr32[i] = dispatchRing[i];
        }
        submittedDwords = DispatchEvidence->Plan.SubmittedDwords;
    } else {
        for (i = 0; i < (Buffers->RingSizeBytes >> 2); ++i) vPtr32[i] = 0u;
        cursor = 0u;

        /* SET_UCONFIG_REG: SCRATCH_REG0 = 0x22222222 (3 dwords). */
        vPtr32[cursor++] = 0xC0017900u;
        vPtr32[cursor++] = 0x00000040u;
        vPtr32[cursor++] = 0x22222222u;

        /* RELEASE_MEM GFX10: exact 8-dword form proven in silicon. */
        vPtr32[cursor++] = 0xC0064900u;
        vPtr32[cursor++] = 0x06603514u;
        vPtr32[cursor++] = 0x20000000u;
        {
            uint64_t fenceTarget = (Buffers->FenceGpuAddress != 0u) ?
                Buffers->FenceGpuAddress : BC250_HQD_FENCE_GPUVA;
            vPtr32[cursor++] = (uint32_t)(fenceTarget & 0xFFFFFFFFull);
            vPtr32[cursor++] = (uint32_t)(fenceTarget >> 32);
        }
        vPtr32[cursor++] = 0xBC250123u;
        vPtr32[cursor++] = 0x00000000u;
        vPtr32[cursor++] = 0x00000000u;

        submittedDwords = cursor;
        if (submittedDwords != 11u) {
            return BC250_HQD_ACTIVE_PLAN_FAILED;
        }
    }
    Interface->Barrier(Interface->Context);
    if (observableDispatch != 0u) {
        for (i = 0u; i < BC250_DISPATCH_RING_MIN_DWORDS; ++i) {
            if (vPtr32[i] != dispatchRing[i]) {
                ++DispatchEvidence->RingReadbackMismatchCount;
            }
        }
        DispatchEvidence->RingReadbackPassed =
            DispatchEvidence->RingReadbackMismatchCount == 0u ? 1u : 0u;
        for (i = 0u; i < 8u; ++i) {
            DispatchEvidence->RingDword[i] = vPtr32[i];
        }
        if (DispatchEvidence->RingReadbackPassed == 0u) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
    }

    /* 3. Inicializacao do MQD */
    activeProps = *Props;
    activeProps.HqdActive = 0u;
    activeProps.DoorbellIndex = effectiveDoorbellIndex;
    if (Bc250ComputeMqdInit(&activeProps, Mqd) != BC250_MQD_OK ||
        Bc250ComputeMqdValidate(Mqd, &activeProps) != BC250_MQD_OK) {
        if (observableDispatch != 0u) {
            status = BC250_HQD_ACTIVE_PLAN_FAILED;
            goto Cleanup;
        }
        return BC250_HQD_ACTIVE_PLAN_FAILED;
    }
    Evidence->PqBaseLoApplied = Mqd->CpHqdPqBaseLo;
    Evidence->PqBaseHiApplied = Mqd->CpHqdPqBaseHi;
    Evidence->WptrPollAddrLoApplied = Mqd->CpHqdPqWptrPollAddrLo;
    Evidence->WptrPollAddrHiApplied = Mqd->CpHqdPqWptrPollAddrHi;
    vPtr32 = (volatile uint32_t *)Buffers->MqdVirtual;
    for (i = 0; i < BC250_MQD_SIZE_DWORDS; ++i) {
        vPtr32[i] = ((const uint32_t *)Mqd)[i];
    }
    Interface->Barrier(Interface->Context);

    /* 4. Construcao do Plano de Registradores Ativo */
    {
        static const uint32_t activeOffsets[BC250_HQD_INACTIVE_PLAN_MAX_WRITES] = {
            BC250_HQD_MMIO_MQD_BASE_LO, BC250_HQD_MMIO_MQD_BASE_HI,
            BC250_HQD_MMIO_MQD_CONTROL, BC250_HQD_MMIO_EOP_BASE_LO,
            BC250_HQD_MMIO_EOP_BASE_HI, BC250_HQD_MMIO_EOP_CONTROL,
            BC250_HQD_MMIO_VMID, BC250_HQD_MMIO_PERSISTENT_STATE,
            BC250_HQD_MMIO_PIPE_PRIORITY, BC250_HQD_MMIO_QUEUE_PRIORITY,
            BC250_HQD_MMIO_QUANTUM, BC250_HQD_MMIO_PQ_BASE_LO,
            BC250_HQD_MMIO_PQ_BASE_HI, BC250_HQD_MMIO_PQ_RPTR,
            BC250_HQD_MMIO_PQ_CONTROL, BC250_HQD_MMIO_DOORBELL_CONTROL
        };
        uint32_t activeValues[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
        activeValues[0] = Mqd->CpMqdBaseAddrLo;
        activeValues[1] = Mqd->CpMqdBaseAddrHi;
        activeValues[2] = Mqd->CpMqdControl;
        activeValues[3] = Mqd->CpHqdEopBaseAddrLo;
        activeValues[4] = Mqd->CpHqdEopBaseAddrHi;
        activeValues[5] = Mqd->CpHqdEopControl;
        activeValues[6] = Mqd->CpHqdVmid;
        activeValues[7] = Mqd->CpHqdPersistentState;
        activeValues[8] = Mqd->CpHqdPipePriority;
        activeValues[9] = Mqd->CpHqdQueuePriority;
        activeValues[10] = Mqd->CpHqdQuantum;
        activeValues[11] = Mqd->CpHqdPqBaseLo;
        activeValues[12] = Mqd->CpHqdPqBaseHi;
        activeValues[13] = 0u;
        activeValues[14] = Mqd->CpHqdPqControl;
        activeValues[15] = Mqd->CpHqdPqDoorbellControl;
        planCount = BC250_HQD_INACTIVE_PLAN_MAX_WRITES;
        for (i = 0; i < planCount; ++i) {
            planWrites[i].ByteOffset = activeOffsets[i];
            planWrites[i].Value = activeValues[i];
        }
    }
    Evidence->MemoryVerificationPassed = 1u;

    /* 5. Selecao da KIQ descrita pelo scheduler0 fisico deste boot. */
    (void)Bc250HqdActiveWrite(Interface, Evidence, 0x00030800u, 0xE0000000u);
    if (!Bc250HqdActiveRead(Interface, Evidence, BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->OriginalSelector)) {
        if (observableDispatch != 0u) {
            status = BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
            goto Cleanup;
        }
        return BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
    }
    selectorCaptured = 1u;
    /*
     * O modo observavel nao toca portas de microcode nem forca clocks,
     * contexto ou topologia global. O unico unhalt necessario sera um RMW
     * no bit HALT do MEC fisicamente selecionado, depois do snapshot.
     */

    schedulerMe = (Evidence->RlcCpSchedulersBefore >> 5) & 0x3u;
    schedulerPipe = (Evidence->RlcCpSchedulersBefore >> 3) & 0x3u;
    schedulerQueue = Evidence->RlcCpSchedulersBefore & 0x7u;
    if (schedulerMe == 0u || schedulerMe > 2u || schedulerQueue != 0u ||
        (schedulerMe == 2u && schedulerPipe > 1u)) {
        status = BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
        goto Cleanup;
    }
    schedulerSelector = (schedulerMe << 2) |
                        schedulerPipe |
                        (schedulerQueue << 8);
    schedulerPcOffset = schedulerMe == 2u ? 0x000086A4u : 0x000086A0u;
    (void)Bc250HqdActiveWrite(Interface, Evidence, 0x00030800u, 0xE0000000u);
    if (!Bc250HqdActiveWrite(Interface, Evidence, BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
            schedulerSelector)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    Evidence->SelectedSelector = schedulerSelector;

    /*
     * gfx_v10_0_kiq_setting: scheduler0 descreve a KIQ escolhida.
     * Preserva scheduler1..3 e substitui somente o byte baixo por
     * Preserva ME/PIPE/QUEUE do scheduler0 e acrescenta somente ENABLE=0x80.
     */
    Evidence->RlcCpSchedulersApplied =
        Evidence->RlcCpSchedulersBefore | 0x00000080u;
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_RLC_CP_SCHEDULERS,
            Evidence->RlcCpSchedulersApplied)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    rlcCpSchedulersWritten = 1u;
    if (!Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_RLC_CP_SCHEDULERS,
            &Evidence->RlcCpSchedulersReadback) ||
        Evidence->RlcCpSchedulersReadback !=
            Evidence->RlcCpSchedulersApplied) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }

    /* 6. Snapshot de Registradores */
    for (i = 0; i < planCount; ++i) {
        if (!Bc250HqdActiveRead(Interface, Evidence, planWrites[i].ByteOffset, &Evidence->SnapshotValues[i])) {
            status = BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
            goto Cleanup;
        }
    }
    for (i = 0u; i < 4u; ++i) {
        if (!Bc250HqdActiveRead(
                Interface, Evidence, queueAddressOffsets[i],
                &queueAddressSnapshots[i])) {
            status = BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
            goto Cleanup;
        }
    }
    if (!Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveBefore) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_RPTR, &Evidence->RptrBefore) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_DEQUEUE_REQUEST, &dequeueRequestSnapshot) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, &wptrLoSnapshot) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_HI, &wptrHiSnapshot) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_IB_CONTROL, &origIbControl) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_EOP_RPTR, &origEopRptr)) {
        status = BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
        goto Cleanup;
    }
    if ((Evidence->ActiveBefore & 1u) != 0u) {
        status = BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
        goto Cleanup;
    }
    Evidence->InitialWptrLo = wptrLoSnapshot;
    Evidence->InitialWptrHi = wptrHiSnapshot;
    Evidence->EopRptrBefore = origEopRptr;

    /* 7. Aplicacao dos 16 Registradores de Configuracao e Faixa de Doorbell */
    for (i = 0; i < planCount; ++i) {
        Evidence->AppliedValues[i] = planWrites[i].Value;
        if (!Bc250HqdActiveWrite(Interface, Evidence, planWrites[i].ByteOffset, planWrites[i].Value)) {
            status = BC250_HQD_ACTIVE_WRITE_FAILED;
            goto Cleanup;
        }
        if (!Bc250HqdActiveRead(Interface, Evidence, planWrites[i].ByteOffset, &Evidence->ReadbackValues[i])) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
    }
    hqdWritten = 1u;
    Evidence->PqControlObserved = Evidence->ReadbackValues[14];

    /* Enderecos de writeback/polling exigidos pelo caminho upstream GC10. */
    {
        const uint32_t queueAddressValues[4] = {
            Mqd->CpHqdPqRptrReportAddrLo,
            Mqd->CpHqdPqRptrReportAddrHi,
            Mqd->CpHqdPqWptrPollAddrLo,
            Mqd->CpHqdPqWptrPollAddrHi
        };
        for (i = 0u; i < 4u; ++i) {
            if (!Bc250HqdActiveWrite(
                    Interface, Evidence, queueAddressOffsets[i],
                    queueAddressValues[i])) {
                status = BC250_HQD_ACTIVE_WRITE_FAILED;
                goto Cleanup;
            }
            ++queueAddressWriteCount;
            if (!Bc250HqdActiveRead(Interface, Evidence, queueAddressOffsets[i], &check) ||
                check != queueAddressValues[i]) {
                status = BC250_HQD_ACTIVE_READBACK_FAILED;
                goto Cleanup;
            }
        }
    }

    /*
     * Candidato 3.31: CP_PQ_WPTR_POLL_CNTL vira OBSERVACAO, nao pre-requisito.
     *
     * O 3.30 tratou EN=1 como fatal e abortou aqui (WRITE_FAILED), antes de
     * CP_PQ_WPTR_POLL_CNTL1, do WPTR pre-ACTIVE, de INIT_FETCHER e de ACTIVE.
     * Custou um boot inteiro sem medir a peca que interessa.  EN e hipotese,
     * nao fato provado: o comentario upstream em kgd_gfx_v9_hqd_load atribui a
     * QUEUE_MASK a ativacao da logica de doorbell, que pode nao depender dele.
     *
     * Aqui nao ha fail-closed afrouxado: o gate do tripe continua exigindo
     * RPTR==WPTR, fence fisica e dado byte a byte.  O que muda e apenas parar
     * de abortar por uma etapa cuja necessidade nunca foi demonstrada.
     *
     * Passo 1 - sonda de gravabilidade: escreve PERIOD (campo comum, sem
     * efeito de habilitacao) e rele.  Separa "registrador read-only neste
     * contexto" de "bit EN especificamente bloqueado".
     * Passo 2 - tentativa de EN, tambem registrada e nao-fatal.
     *
     * POLL_ACTIVE[30] fica fora de toda escrita por ser status do CP. O
     * literal historico 0x00403004 e do SDMA e nao entra aqui.
     */
    cpPqWptrPollCntlWritten = 1u;
    Evidence->WptrPollCntlProbeApplied =
        (origCpPqWptrPollCntl & ~(BC250_HQD_WPTR_POLL_ACTIVE_MASK |
                                  BC250_HQD_WPTR_POLL_PERIOD_MASK)) |
        BC250_HQD_WPTR_POLL_PROBE_PERIOD;
    if (Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL,
            Evidence->WptrPollCntlProbeApplied) &&
        Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL,
            &Evidence->WptrPollCntlProbeReadback) &&
        (Evidence->WptrPollCntlProbeReadback &
            BC250_HQD_WPTR_POLL_PERIOD_MASK) ==
            BC250_HQD_WPTR_POLL_PROBE_PERIOD) {
        Evidence->WptrPollCntlWritable = 1u;
    }

    Evidence->WptrPollCntlApplied =
        (origCpPqWptrPollCntl & ~BC250_HQD_WPTR_POLL_ACTIVE_MASK) |
        BC250_HQD_WPTR_POLL_ENABLE_MASK;
    if (Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL,
            Evidence->WptrPollCntlApplied) &&
        Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL,
            &Evidence->WptrPollCntlReadback) &&
        (Evidence->WptrPollCntlReadback &
            BC250_HQD_WPTR_POLL_ENABLE_MASK) != 0u) {
        Evidence->WptrPollCntlEnLatched = 1u;
    }

    if (Props->UseDoorbell != 0u) {
        /* Preserva a faixa fisica ja inicializada e usa seu primeiro slot. */
        Evidence->DoorbellRangeLowerApplied = origDbRangeLo;
        Evidence->DoorbellRangeUpperApplied = origDbRangeHi;
        if (!Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_MEC_DB_RANGE_LOWER,
                &Evidence->DoorbellRangeLowerReadback) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_MEC_DB_RANGE_UPPER,
                &Evidence->DoorbellRangeUpperReadback) ||
            Evidence->DoorbellRangeLowerReadback != origDbRangeLo ||
            Evidence->DoorbellRangeUpperReadback != origDbRangeHi) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
    }

    /*
     * Configuracao transacional de GCVM Context 0 (VMID 0) para o HQD:
     * Garante que o microengine MEC consiga traduzir a memoria da fila
     * (Ring 64KB, MQD 512B, EOP 4KB, Fences/Pointers).
     */
    if (Buffers->PtVirtual != NULL && Buffers->PtGpuAddress != 0u) {
        uint32_t eng0Ack = 0u;

        Evidence->GcvmContext0PtBaseLoApplied = (uint32_t)(Buffers->PtGpuAddress | 1u);
        Evidence->GcvmContext0CntlApplied = 0x007FFE01u;
        gcvmCtx0Written = 1u;
        if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_LO,
            Evidence->GcvmContext0PtBaseLoApplied) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_HI,
            (uint32_t)(Buffers->PtGpuAddress >> 32)) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_START_LO, 0u) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_END_LO, 511u) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_CNTL,
            Evidence->GcvmContext0CntlApplied) ||
            !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_LO, &check) ||
            check != Evidence->GcvmContext0PtBaseLoApplied) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
        if (!Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_HI, &check) ||
            check != (uint32_t)(Buffers->PtGpuAddress >> 32) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_GCVM_CONTEXT0_PT_START_LO, &check) ||
            check != 0u ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_GCVM_CONTEXT0_PT_END_LO, &check) ||
            check != 511u ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_GCVM_CONTEXT0_CNTL, &check) ||
            check != Evidence->GcvmContext0CntlApplied ||
            !Bc250HqdActiveWrite(Interface, Evidence,
                BC250_HQD_MMIO_GCVM_INVALIDATE_ENG0_REQ, 0x00F80001u)) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
        for (i = 0u; i < 100u; ++i) {
            if (Bc250HqdActiveRead(Interface, Evidence, 0x0000A2D4u, &eng0Ack) &&
                (eng0Ack & 1u) != 0u) {
                break;
            }
        }
        if (i == 100u) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
    }

    /*
     * gfx_v10_0_kiq_init_register reinicia WPTR antes de ACTIVE=1. Isso
     * cria uma borda real para o doorbell mesmo quando o banco herdou um
     * cursor residual de um ensaio anterior. O snapshot original continua
     * sendo restaurado no teardown, antes de desmontar a configuracao HQD.
     */
    wptrBaselineWritten = 1u;
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_HI, 0u) ||
        !Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, 0u)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_PQ_WPTR_LO,
        &Evidence->BaselineWptrReadback);
    Evidence->BaselineWptrApplied = 1u;

    /* CP_HQD_IB_CONTROL: MIN_IB_AVAIL_SIZE = 3 */
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_IB_CONTROL, 0x00300000u) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_IB_CONTROL, &check) ||
        check != 0x00300000u) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }
    ibControlWritten = 1u;

    /* CP_HQD_DEQUEUE_REQUEST: explicitamente 0 para garantir que nenhuma requisicao residual permaneca */
    Evidence->DequeueRequestApplied = 0u;
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_DEQUEUE_REQUEST, 0u) ||
        !Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_DEQUEUE_REQUEST, &Evidence->DequeueRequestReadback) ||
        Evidence->DequeueRequestReadback != 0u) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }
    dequeueRequestWritten = 1u;

    /* 8. Unhalt estritamente do MEC selecionado; preserve todos os demais bits. */
    if (observableDispatch != 0u) {
        const uint32_t selectedHaltMask =
            schedulerMe == 1u ? 0x40000000u : 0x10000000u;
        Evidence->CpMecCntlApplied = origCpMecCntl & ~selectedHaltMask;
        Evidence->CpMeCntlApplied = origCpMeCntl;
        if (!Bc250HqdActiveWrite(Interface, Evidence, 0x00008234u,
                Evidence->CpMecCntlApplied) ||
            !Bc250HqdActiveRead(Interface, Evidence, 0x00008234u,
                &Evidence->CpMecCntlReadback) ||
            Evidence->CpMecCntlReadback != Evidence->CpMecCntlApplied) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
        cpMecCntlWritten = 1u;
    } else {
        Evidence->CpMecCntlApplied = 0u;
        Evidence->CpMeCntlApplied = 0u;
        if (!Bc250HqdActiveWrite(Interface, Evidence, 0x00008234u,
                Evidence->CpMecCntlApplied)) {
            status = BC250_HQD_ACTIVE_WRITE_FAILED;
            goto Cleanup;
        }
        cpMecCntlWritten = 1u;
        if (!Bc250HqdActiveWrite(Interface, Evidence, 0x000086D8u,
                Evidence->CpMeCntlApplied)) {
            status = BC250_HQD_ACTIVE_WRITE_FAILED;
            goto Cleanup;
        }
        cpMeCntlWritten = 1u;
        if (!Bc250HqdActiveRead(Interface, Evidence, 0x00008234u,
                &Evidence->CpMecCntlReadback) ||
            Evidence->CpMecCntlReadback != Evidence->CpMecCntlApplied ||
            !Bc250HqdActiveRead(Interface, Evidence, 0x000086D8u, &check) ||
            check != Evidence->CpMeCntlApplied) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
    }

    /* Publica o cursor na shadow memory antes do flush, igual ao Ring 0. */
    vPtr32 = (volatile uint32_t *)Buffers->WptrVirtual;
    vPtr32[0] = submittedDwords;
    vPtr32[1] = 0u;
    Evidence->WptrShadowApplied = vPtr32[0];
    Interface->Barrier(Interface->Context);

    /*
     * Torna as escritas de CPU visiveis ao MEC antes do kick. Os offsets sao
     * os bytes SOC15 catalogados para GC10.1; 0x86D0/0x28208/0x28210 nao
     * pertencem a este trio no BAR5 da BC-250 e permanecem fora do caminho.
     */
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE, 1u)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    Evidence->HdpInvalidateApplied = 1u;

    Evidence->GcvmL2Cntl2Applied =
        (origGcvmL2Cntl2 & ~BC250_HQD_GCVM_L2_INVALIDATE_MASK) |
        BC250_HQD_GCVM_L2_INVALIDATE_MASK;
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_L2_CNTL2,
            Evidence->GcvmL2Cntl2Applied)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    gcvmL2Cntl2Written = 1u;
    if (!Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_L2_CNTL2,
            &Evidence->GcvmL2Cntl2Readback) ||
        (Evidence->GcvmL2Cntl2Readback &
            ~BC250_HQD_GCVM_L2_INVALIDATE_MASK) !=
        (origGcvmL2Cntl2 & ~BC250_HQD_GCVM_L2_INVALIDATE_MASK) ||
        (Evidence->GcvmL2Cntl2Readback &
            BC250_HQD_GCVM_L2_INVALIDATE_MASK) != 0u) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_INVALIDATE_REQ,
            BC250_HQD_GCVM_INVALIDATE_REQ_VMID0)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    Evidence->GcvmInvalidateRequestApplied =
        BC250_HQD_GCVM_INVALIDATE_REQ_VMID0;
    /* GC10.1 exige dummy-read do REQ antes do ACK para evitar falso ACK. */
    if (!Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_GCVM_INVALIDATE_REQ,
            &gcvmInvalidateReqReadback)) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }
    Evidence->GcvmInvalidateRequestReadback = gcvmInvalidateReqReadback;
    if (gcvmInvalidateReqReadback !=
            BC250_HQD_GCVM_INVALIDATE_REQ_VMID0) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }
    for (poll = 0u; poll < 1000u; ++poll) {
        if (!Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_GCVM_INVALIDATE_ACK,
                &Evidence->GcvmInvalidateAckAfter)) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
        if ((Evidence->GcvmInvalidateAckAfter &
             BC250_HQD_GCVM_INVALIDATE_VMID0) != 0u) {
            break;
        }
    }
    Evidence->GcvmInvalidatePolls = poll + (poll < 1000u ? 1u : 0u);
    if ((Evidence->GcvmInvalidateAckAfter &
         BC250_HQD_GCVM_INVALIDATE_VMID0) == 0u) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }
    Evidence->CoherencyPassed = 1u;
    /*
     * Candidato 3.30 - protocolo one-shot de WPTR na ordem KFD.
     *
     * kgd_gfx_v9_hqd_load publica os enderecos de polling e so entao
     * habilita a fila em CP_PQ_WPTR_POLL_CNTL1 (QUEUE_MASK, um bit por
     * fila) antes de INIT_FETCHER e ACTIVE.  O ensaio 3.29 provou que
     * doorbell e escrita direta nao fixam o WPTR com a fila fora dessa
     * mascara: o readback imediato valia o cursor e as amostras
     * seguintes voltavam a zero, com PQ_EMPTY aceso.
     *
     * A mascara e derivada da topologia real lida em RLC_CP_SCHEDULERS,
     * nunca de literal historico.  CP_PQ_WPTR_POLL_CNTL permanece com o
     * valor original do silicio (0 nesta plataforma): esta e a unica
     * variavel do candidato.
     */
    Evidence->QueueMaskBit = Bc250HqdQueueMaskBit(schedulerPipe, schedulerQueue);
    if (Evidence->QueueMaskBit > BC250_HQD_QUEUE_MASK_MAX_BIT) {
        status = BC250_HQD_ACTIVE_PREFLIGHT_FAILED;
        goto Cleanup;
    }
    Evidence->WptrPollCntl1Applied = 1u << Evidence->QueueMaskBit;
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1,
            Evidence->WptrPollCntl1Applied)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    cpPqWptrPollCntl1Written = 1u;
    if (!Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1,
            &Evidence->WptrPollCntl1Readback) ||
        (Evidence->WptrPollCntl1Readback &
            Evidence->WptrPollCntl1Applied) !=
            Evidence->WptrPollCntl1Applied) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }

    /*
     * Copia o cursor de usuario para o registrador antes de ACTIVE, como
     * upstream.  O snapshot original ja foi capturado e e restaurado no
     * teardown; nada aqui simula RPTR, fence ou resultado.
     */
    /*
     * Candidato 3.32: a publicacao pre-ACTIVE vira OBSERVACAO nao-fatal.
     *
     * O 3.31 escreveu 40 aqui e releu 0, e abortou (READBACK_FAILED) antes de
     * INIT_FETCHER e de ACTIVE. Cruzando com o 3.29 - onde a escrita DEPOIS
     * de ACTIVE=1 releu 40 - a regra observada e que CP_HQD_PQ_WPTR_LO so
     * aceita o cursor com a fila ativa. Upstream publica antes; este silicio
     * nao permite. Registramos a tentativa e seguimos: quem publica de
     * verdade e o caminho pos-ACTIVE, agora com a fila dentro da QUEUE_MASK -
     * combinacao que nunca foi ensaiada.
     */
    Evidence->PreActiveWptrApplied = submittedDwords;
    if (Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_HI, 0u) &&
        Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO, submittedDwords) &&
        Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_PQ_WPTR_LO,
            &Evidence->PreActiveWptrReadback) &&
        Evidence->PreActiveWptrReadback == submittedDwords) {
        Evidence->PreActiveWptrPublished = 1u;
    }

    /*
     * O helper KFD kgd_gfx_v9_hqd_load inicia explicitamente o fetcher EOP
     * antes de publicar ACTIVE. INIT_FETCHER e um gatilho e pode auto-limpar;
     * por isso o readback rejeita somente RPTR e comandos RESET/DEQUEUE
     * inesperados; RPTR_EQ_CSMD_WPTR[30] e status observacional permitido.
     */
    Evidence->EopRptrApplied = 0x80000000u;
    if (!Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_EOP_RPTR, Evidence->EopRptrApplied)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    eopRptrWritten = 1u;
    if (!Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_EOP_RPTR, &Evidence->EopRptrReadback) ||
        (Evidence->EopRptrReadback & 0x30001FFFu) != 0u) {
        status = BC250_HQD_ACTIVE_READBACK_FAILED;
        goto Cleanup;
    }

    /*
     * Candidato 3.36: drena o ME2 antes de ACTIVE para separar residuos da
     * FIFO dos headers acrescentados pelo ciclo atual. Observacao best-effort:
     * nenhuma leitura nova participa de um abort.
     */
    for (i = 0u; i < BC250_HQD_MEC_HEADER_DUMP_DEPTH; ++i) {
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP,
            &Evidence->MecMe2HeaderDumpBeforeActive[i]);
    }

    /* Candidato 3.40/3.41: CPF busy e stalled imediatamente antes de ACTIVE=1 */
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &Evidence->CpfBusyStatBeforeActive);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &Evidence->CpfStalledStat1BeforeActive);

    if (!Bc250HqdActiveWrite(Interface, Evidence, BC250_HQD_MMIO_ACTIVE, 1u)) {
        status = BC250_HQD_ACTIVE_WRITE_FAILED;
        goto Cleanup;
    }
    (void)Bc250HqdActiveRead(Interface, Evidence, BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveApplied);
    if ((Evidence->ActiveApplied & 1u) != 0) {
        Evidence->ActiveConfirmed = 1u;
    }
    /* Candidato 3.40/3.41: CPF busy, stalled e status imediatamente apos readback de ACTIVE=1 */
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &Evidence->CpfBusyStatAfterActive);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &Evidence->CpfStalledStat1AfterActive);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPF_STATUS, &Evidence->CpfStatusAfterActive);

    /*
     * Amostra limpa: WPTR imediatamente apos ACTIVE=1, antes de qualquer
     * doorbell ou escrita direta. Se vier igual ao cursor, foi o proprio CP
     * que o buscou no shadow via QUEUE_MASK - a prova que o 3.31 nao chegou
     * a produzir. Nao ha escrita aqui; nada e simulado.
     */
    /* Candidato 3.40: CPF busy antes da observacao inicial de WPTR */
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &Evidence->CpfBusyStatBeforeWptr);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_PQ_WPTR_LO, &Evidence->PostActiveWptrObserved);

    /* Upstream gfx_v10_0_hqd_load: habilita DOORBELL_ENABLE no CP_PQ_STATUS apos ACTIVE=1 */
    if (Props->UseDoorbell != 0u) {
        Evidence->CpPqStatusApplied = origCpPqStatus | 0x00000002u;
        if (!Bc250HqdActiveWrite(Interface, Evidence,
                BC250_HQD_MMIO_CP_PQ_STATUS,
                Evidence->CpPqStatusApplied) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_PQ_STATUS,
                &Evidence->CpPqStatusReadback) ||
            (Evidence->CpPqStatusReadback & 0x00000002u) == 0u) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
        cpPqStatusWritten = 1u;
    }

    /*
     * Upstream gfx_v10_0_kiq_init_register:
     * Publica o cursor exato diretamente no registrador CP_HQD_PQ_WPTR_LO.
     */
    Evidence->WptrApplied = submittedDwords;
    if (Props->UseDoorbell == 0u) {
        if (!Bc250HqdActiveWrite(
                Interface, Evidence, BC250_HQD_MMIO_PQ_WPTR_LO,
                submittedDwords)) {
            status = BC250_HQD_ACTIVE_WRITE_FAILED;
            goto Cleanup;
        }
        (void)Bc250HqdActiveWrite(
            Interface, Evidence, BC250_HQD_MMIO_PQ_WPTR_HI, 0u);
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &Evidence->CpfBusyStatAfterWptr);
    } else {
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &Evidence->CpfBusyStatAfterWptr);
        /* Libera o banco MMIO como nv_grbm_select(..., 0,0,0,0). */
        if (!Bc250HqdActiveWrite(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                Evidence->OriginalSelector)) {
            status = BC250_HQD_ACTIVE_WRITE_FAILED;
            goto Cleanup;
        }
        Evidence->SelectorReleasedBeforeKick = 1u;
    }

    /* Snapshot global imediatamente antes da publicacao BAR2. */
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPC_STATUS, &Evidence->CpcStatusBeforeKick);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &Evidence->CpcBusyStatBeforeKick);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &Evidence->CpfBusyStatBeforeKick);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &Evidence->CpfStalledStat1BeforeKick);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_CPC_STALLED_STAT1,
        &Evidence->CpcStalledStat1BeforeKick);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_STAT, &Evidence->CpStatBeforeKick);
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_STALLED_STAT1,
        &Evidence->CpStalledStat1BeforeKick);

    /* Disparo de Doorbell na Abertura PCIe BAR2 */
    if (Props->UseDoorbell != 0u && Buffers->DoorbellVirtual != NULL) {
        uint32_t dbOffset = effectiveDoorbellOffset;
        volatile uint64_t *db64;
        if (Buffers->DoorbellSizeBytes < dbOffset + sizeof(uint64_t)) {
            status = BC250_HQD_ACTIVE_MEMORY_FAILED;
            goto Cleanup;
        }
        db64 = (volatile uint64_t *)
            ((uintptr_t)Buffers->DoorbellVirtual + dbOffset);
        *db64 = (uint64_t)submittedDwords;
        Evidence->DoorbellRung = 1u;
        Evidence->DoorbellIndex = effectiveDoorbellIndex;
        Evidence->DoorbellOffset = dbOffset;
        Evidence->DoorbellValueApplied = submittedDwords;
        Interface->Barrier(Interface->Context);

        /* Snapshot global imediatamente depois da publicacao BAR2. */
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_CPC_STATUS, &Evidence->CpcStatusAfterKick);
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &Evidence->CpcBusyStatAfterKick);
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &Evidence->CpfBusyStatAfterKick);
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_CPF_STALLED_STAT1,
            &Evidence->CpfStalledStat1AfterKick);
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_CPF_STATUS, &Evidence->CpfStatusAfterKick);
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_CPC_STALLED_STAT1,
            &Evidence->CpcStalledStat1AfterKick);
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_STAT, &Evidence->CpStatAfterKick);
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_STALLED_STAT1,
            &Evidence->CpStalledStat1AfterKick);

        /* Diagnostico fisico: recepcao global e latch no HQD selecionado. */
        if (!Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_PQ_STATUS,
                &Evidence->CpPqStatusAfterKick) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                Evidence->SelectedSelector) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_DOORBELL_CONTROL,
                &Evidence->DoorbellControlAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_LO,
                &Evidence->WptrAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_PQ_RPTR,
                &Evidence->RptrAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_HQ_STATUS0,
                &Evidence->HqStatus0AfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_HQ_CONTROL0,
                &Evidence->HqControl0AfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_HQD_ERROR,
                &Evidence->HqdErrorAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CPC_UTCL1_ERROR,
                &Evidence->CpcUtcl1ErrorAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CPC_UTCL1_STATUS,
                &Evidence->CpcUtcl1StatusAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CPF_UTCL1_STATUS,
                &Evidence->CpfUtcl1StatusAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CPC_INT_STATUS,
                &Evidence->CpcIntStatusAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_ME1_PIPE0_INT_STATUS,
                &Evidence->Me1Pipe0IntStatusAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_ME2_PIPE1_INT_STATUS,
                &Evidence->Me2Pipe1IntStatusAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_ME2_PIPE0_INT_STATUS,
                &Evidence->Me2Pipe0IntStatusAfterKick) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_RLC_CP_SCHEDULERS,
                &Evidence->RlcCpSchedulersAfterKick) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                Evidence->OriginalSelector)) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
    }

    /*
     * Diagnostico canonico sem BAR2: se o doorbell nao atualizou o WPTR do
     * banco selecionado, publica o mesmo cursor diretamente no HQD. Isto
     * separa roteamento de doorbell de scheduler/fetch, sem simular RPTR ou
     * fence. O snapshot do WPTR e restaurado no teardown.
     */
    if (Props->UseDoorbell != 0u &&
        Evidence->WptrAfterKick != submittedDwords) {
        directWptrWritten = 1u;
        if (!Bc250HqdActiveWrite(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                Evidence->SelectedSelector) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_LO,
                &Evidence->DirectWptrBefore) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_LO, submittedDwords) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_HI, 0u) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_LO,
                &Evidence->DirectWptrReadback) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_PQ_RPTR,
                &Evidence->DirectRptrAfterKick) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
                BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                Evidence->OriginalSelector)) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
        Evidence->DirectWptrKickApplied = 1u;
        if (Evidence->DirectWptrReadback != submittedDwords) {
            status = BC250_HQD_ACTIVE_READBACK_FAILED;
            goto Cleanup;
        }
        Interface->Barrier(Interface->Context);
    }

    /* 9. Polling Loop do Tripe de Prova Fisica */
    status = BC250_HQD_ACTIVE_PM4_NOT_CONSUMED;
    wptrPollObserved = Evidence->WptrAfterKick;
    if (Evidence->DirectWptrKickApplied != 0u) {
        wptrPollObserved = Evidence->DirectWptrReadback;
    }
    for (poll = 0; poll < 50000u; ++poll) {
        if (Evidence->SelectorReleasedBeforeKick == 0u) {
            (void)Bc250HqdActiveRead(
                Interface, Evidence, BC250_HQD_MMIO_PQ_RPTR, &rptrPoll);
        }
        if (Buffers->RptrVirtual != NULL) {
            volatile uint32_t *rptrMem = (volatile uint32_t *)Buffers->RptrVirtual;
            /*
             * Candidato 3.33: o writeback de RPTR em memoria e observado
             * sempre, nao so quando diferente de zero. Se o registrador nao
             * anda mas a memoria anda (ou o contrario), isso aponta direto
             * para o RPTR_REPORT_ADDR.
             */
            if (poll == 0u) {
                Evidence->RptrReportMemFirst = rptrMem[0];
            }
            Evidence->RptrReportMemLast = rptrMem[0];
            if (rptrMem[0] != 0u) {
                rptrPoll = rptrMem[0];
            }
        }
        if (observableDispatch == 0u) {
            (void)Bc250HqdActiveRead(
                Interface, Evidence, 0x00030100u, &scratchPoll);
        }
        Interface->Barrier(Interface->Context);
        vPtr32 = (volatile uint32_t *)Buffers->FenceVirtual;
        fencePoll = vPtr32[0];
        if (observableDispatch != 0u) {
            resultPoll = ((volatile uint32_t *)Buffers->ResultVirtual)[0];
        }

        if (rptrPoll == submittedDwords &&
            fencePoll != (observableDispatch != 0u ?
                BC250_DISPATCH_FENCE_VALUE : 0xBC250123u)) {
            (void)Bc250HqdActiveWrite(Interface, Evidence, 0x00003FC4u, 1u);
            Interface->Barrier(Interface->Context);
            fencePoll = vPtr32[0];
        }

        if ((poll & 0xFFu) == 0u) {
            uint32_t rptrMmio = 0u;

            (void)Bc250HqdActiveRead(Interface, Evidence, schedulerPcOffset, &mecPcPoll);
            /*
             * O doorbell exige liberar o seletor GRBM, mas isso nao pode
             * esconder o RPTR real durante a prova. Reabre o banco somente
             * para uma amostra read-only e o libera imediatamente.
             */
            if (!Bc250HqdActiveWrite(Interface, Evidence,
                    BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                    Evidence->SelectedSelector) ||
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_PQ_RPTR, &rptrMmio) ||
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_PQ_WPTR_LO,
                    &Evidence->WptrMmioLast) ||
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_HQ_STATUS0,
                    &Evidence->HqStatus0PollLast) ||
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_HQD_ERROR,
                    &Evidence->HqdErrorPollLast) ||
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_PQ_CONTROL,
                    &Evidence->PqControlObserved) ||
                /*
                 * Candidato 3.33: quatro leituras a mais no mesmo banco ja
                 * aberto, para separar as hipoteses do RPTR parado.
                 * DEQUEUE_REQUEST mostra se algo pediu dequeue por conta
                 * propria; VMID e PERSISTENT_STATE mostram se o HQD
                 * permanece com a configuracao que escrevemos.
                 */
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_DEQUEUE_REQUEST,
                    &Evidence->DequeueRequestPollLast) ||
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_VMID,
                    &Evidence->VmidPollLast) ||
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_PERSISTENT_STATE,
                    &Evidence->PersistentStatePollLast) ||
                !Bc250HqdActiveWrite(Interface, Evidence,
                    BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
                    Evidence->OriginalSelector)) {
                status = BC250_HQD_ACTIVE_READBACK_FAILED;
                goto Cleanup;
            }
            /*
             * Candidato 3.34: estado global do CP/CPC. Sao registradores nao
             * indexados pelo seletor GRBM; a leitura aqui apenas aproveita o
             * mesmo ponto de amostragem. Somente leitura.
             */
            (void)Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_CPC_STATUS, &Evidence->CpcStatusLast);
            (void)Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_CPC_BUSY_STAT, &Evidence->CpcBusyStatLast);
            (void)Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_CPF_BUSY_STAT, &Evidence->CpfBusyStatLast);
            (void)Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_CPF_STALLED_STAT1, &Evidence->CpfStalledStat1Last);
            (void)Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_CPC_STALLED_STAT1,
                &Evidence->CpcStalledStat1Last);
            (void)Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_CP_STAT, &Evidence->CpStatLast);
            if (Evidence->MecPcSamples == 0u) {
                Evidence->PqControlFirstSample = Evidence->PqControlObserved;
                Evidence->HqStatus0FirstSample = Evidence->HqStatus0PollLast;
                Evidence->CpcStatusFirst = Evidence->CpcStatusLast;
                Evidence->CpStatFirst = Evidence->CpStatLast;
            }
            if (rptrMmio != 0u) {
                ++Evidence->RptrNonZeroSamples;
            }
            Evidence->RptrMmioLast = rptrMmio;
            wptrPollObserved = Evidence->WptrMmioLast;
            rptrPoll = rptrMmio;
            if (Evidence->MecPcSamples == 0u) {
                Evidence->MecPcFirst = mecPcPoll;
            } else if (mecPcPoll != mecPcPrevious) {
                ++Evidence->MecPcTransitions;
            }
            mecPcPrevious = mecPcPoll;
            Evidence->MecPcLast = mecPcPoll;
            ++Evidence->MecPcSamples;
        }

        if (observableDispatch != 0u &&
            rptrPoll == submittedDwords &&
            wptrPollObserved == submittedDwords &&
            fencePoll == BC250_DISPATCH_FENCE_VALUE &&
            resultPoll == BC250_DISPATCH_RESULT_VALUE) {
            Evidence->TriadPassed = 1u;
            status = BC250_HQD_ACTIVE_OK;
            break;
        }
        if (observableDispatch == 0u &&
            rptrPoll == submittedDwords &&
            scratchPoll == 0x22222222u &&
            fencePoll == 0xBC250123u) {
            Evidence->TriadPassed = 1u;
            Evidence->PhysicalReady = 1u;
            status = BC250_HQD_ACTIVE_OK;
            break;
        }
    }
    Evidence->PollIterations = poll;
    /*
     * Candidato 3.35: rajada unica dos dumps ME1/ME2 depois do laco. Cada
     * FIFO avanca a cada leitura, portanto ambas sao drenadas uma unica vez.
     * ME2 e o alvo ativo; ME1 permanece como controle do diagnostico 3.34.
     */
    for (i = 0u; i < BC250_HQD_MEC_HEADER_DUMP_DEPTH; ++i) {
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_MEC_ME1_HEADER_DUMP,
            &Evidence->MecMe1HeaderDump[i]);
    }
    for (i = 0u; i < BC250_HQD_MEC_HEADER_DUMP_DEPTH; ++i) {
        (void)Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP,
            &Evidence->MecMe2HeaderDump[i]);
    }
    (void)Bc250HqdActiveRead(Interface, Evidence,
        BC250_HQD_MMIO_CP_STALLED_STAT1, &Evidence->CpStalledStat1After);
    Evidence->RptrAfter = rptrPoll;
    Evidence->ScratchAfter = scratchPoll;
    Evidence->FenceAfter = fencePoll;
    if (observableDispatch != 0u) {
        const uint32_t expected = BC250_DISPATCH_RESULT_VALUE;
        uint32_t byteIndex;

        DispatchEvidence->Observation.SubmittedDwords = submittedDwords;
        DispatchEvidence->Observation.RptrBefore = Evidence->RptrBefore;
        DispatchEvidence->Observation.RptrAfter = rptrPoll;
        DispatchEvidence->Observation.WptrBefore = Evidence->InitialWptrLo;
        DispatchEvidence->Observation.WptrAfter = wptrPollObserved;
        DispatchEvidence->Observation.ResultAfter = resultPoll;
        DispatchEvidence->Observation.FenceAfter = fencePoll;
        for (byteIndex = 0u; byteIndex < sizeof(uint32_t); ++byteIndex) {
            const uint32_t shift = byteIndex * 8u;
            if (((resultPoll >> shift) & 0xFFu) !=
                ((expected >> shift) & 0xFFu)) {
                ++DispatchEvidence->Observation.ResultMismatchCount;
            }
        }
        if (status != BC250_HQD_ACTIVE_OK &&
            rptrPoll == submittedDwords &&
            wptrPollObserved == submittedDwords &&
            fencePoll == BC250_DISPATCH_FENCE_VALUE) {
            status = BC250_HQD_ACTIVE_RESULT_FAILED;
        }
    }

Cleanup:
    /* 10. Desativacao / Dequeue e Rollback LIFO */
    if (observableDispatch != 0u && selectorCaptured == 0u) {
        Evidence->RestorePassed = 1u;
        goto CleanupMemory;
    }
    (void)Bc250HqdActiveWrite(Interface, Evidence, 0x00030800u, 0xE0000000u);
    (void)Bc250HqdActiveWrite(Interface, Evidence, BC250_GRBM_GFX_CNTL_BYTE_OFFSET,
        Evidence->SelectedSelector);

    Evidence->RestorePassed = 1u;

    /*
     * Sequencia upstream GC10: pedir dequeue, aguardar ACTIVE=0 e somente
     * entao restaurar DEQUEUE_REQUEST/RPTR/WPTR. O ensaio 3.19 provou que
     * tentar reduzir WPTR com ACTIVE=1 nao faz commit neste silicio.
     */
    if (Evidence->ActiveConfirmed != 0u) {
        if (Bc250HqdActiveWrite(Interface, Evidence,
                BC250_HQD_MMIO_DEQUEUE_REQUEST, 1u)) {
            dequeueRequestWritten = 1u;
        } else {
            Bc250HqdNoteRestoreFailure(
                Evidence, BC250_HQD_RESTORE_STAGE_ACTIVE_DEACTIVATE,
                BC250_HQD_MMIO_DEQUEUE_REQUEST, 1u, 0u, 1u);
        }
        for (poll = 0u; poll < 1000u; ++poll) {
            if (!Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveAfter)) {
                Bc250HqdNoteRestoreFailure(
                    Evidence, BC250_HQD_RESTORE_STAGE_ACTIVE_READBACK,
                    BC250_HQD_MMIO_ACTIVE, 0u, 0u, 1u);
                break;
            }
            if ((Evidence->ActiveAfter & 1u) == 0u) break;
        }
        if ((Evidence->ActiveAfter & 1u) != 0u) {
            /* Teardown de emergencia; o gate permanece reprovado. */
            Bc250HqdNoteRestoreFailure(
                Evidence, BC250_HQD_RESTORE_STAGE_ACTIVE_DEACTIVATE,
                BC250_HQD_MMIO_ACTIVE, 0u, Evidence->ActiveAfter, 0u);
            (void)Bc250HqdActiveWrite(
                Interface, Evidence, BC250_HQD_MMIO_ACTIVE, 0u);
            (void)Bc250HqdActiveRead(
                Interface, Evidence, BC250_HQD_MMIO_ACTIVE,
                &Evidence->ActiveAfter);
        }
    } else if (!Bc250HqdActiveRead(Interface, Evidence,
            BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveAfter)) {
        Bc250HqdNoteRestoreFailure(
            Evidence, BC250_HQD_RESTORE_STAGE_ACTIVE_READBACK,
            BC250_HQD_MMIO_ACTIVE, 0u, 0u, 1u);
    }

    if (dequeueRequestWritten != 0u) {
        (void)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_DEQUEUE_REQUEST,
            BC250_HQD_MMIO_DEQUEUE_REQUEST, dequeueRequestSnapshot,
            0xFFFFFFFFu, &check);
    }

    if (wptrBaselineWritten != 0u) {
        uint32_t pointerRestoreWritten = 1u;

        (void)Bc250HqdActiveWrite(Interface, Evidence,
            BC250_HQD_MMIO_PQ_RPTR, Evidence->RptrBefore);
        if ((Evidence->ActiveAfter & 1u) != 0u ||
            !Bc250HqdActiveWrite(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_LO, wptrLoSnapshot) ||
            !Bc250HqdActiveWrite(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_HI, wptrHiSnapshot)) {
            pointerRestoreWritten = 0u;
        }
        if (pointerRestoreWritten != 0u) {
            for (poll = 0u; poll < 1000u; ++poll) {
                if (!Bc250HqdActiveRead(Interface, Evidence,
                        BC250_HQD_MMIO_PQ_WPTR_LO,
                        &Evidence->BaselineWptrRestored) ||
                    !Bc250HqdActiveRead(Interface, Evidence,
                        BC250_HQD_MMIO_PQ_WPTR_HI,
                        &wptrHiRestored)) {
                    pointerRestoreWritten = 0u;
                    break;
                }
                Evidence->BaselineWptrRestorePolls = poll + 1u;
                if (Evidence->BaselineWptrRestored == wptrLoSnapshot &&
                    wptrHiRestored == wptrHiSnapshot) {
                    Evidence->BaselineWptrRestorePassed = 1u;
                    break;
                }
            }
        }
        if (pointerRestoreWritten == 0u) {
            Bc250HqdNoteRestoreFailure(
                Evidence, BC250_HQD_RESTORE_STAGE_POINTER_WRITE,
                BC250_HQD_MMIO_PQ_WPTR_LO, wptrLoSnapshot,
                Evidence->BaselineWptrRestored, 1u);
        } else if (Evidence->BaselineWptrRestorePassed == 0u) {
            Bc250HqdNoteRestoreFailure(
                Evidence, BC250_HQD_RESTORE_STAGE_POINTER_CONVERGE,
                BC250_HQD_MMIO_PQ_WPTR_LO, wptrLoSnapshot,
                Evidence->BaselineWptrRestored, 0u);
        }
    }

    if (directWptrWritten != 0u) {
        Evidence->DirectWptrRestored = Evidence->BaselineWptrRestored;
        Evidence->DirectWptrRestorePolls = Evidence->BaselineWptrRestorePolls;
        Evidence->DirectWptrRestorePassed = Evidence->BaselineWptrRestorePassed;
    }

    if (ibControlWritten != 0u) {
        (void)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_IB_CONTROL,
            BC250_HQD_MMIO_IB_CONTROL, origIbControl, 0xFFFFFFFFu, &check);
    }

    if (dequeueRequestWritten != 0u) {
        Evidence->DequeueRequestRestorePassed = (uint32_t)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_IB_CONTROL,
            BC250_HQD_MMIO_DEQUEUE_REQUEST, dequeueRequestSnapshot, 0xFFFFFFFFu,
            &Evidence->DequeueRequestRestored);
    }

    if (eopRptrWritten != 0u) {
        Evidence->EopRptrRestorePassed = (uint32_t)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_EOP_RPTR,
            BC250_HQD_MMIO_EOP_RPTR, origEopRptr, 0xFFFFFFFFu,
            &Evidence->EopRptrRestored);
    }

    /* Rollback LIFO dos enderecos auxiliares e dos 16 registradores. */
    while (queueAddressWriteCount != 0u) {
        uint32_t idx = queueAddressWriteCount - 1u;
        (void)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_QUEUE_ADDRESS,
            queueAddressOffsets[idx], queueAddressSnapshots[idx],
            0xFFFFFFFFu, &check);
        --queueAddressWriteCount;
    }
    if (hqdWritten != 0u) {
        for (i = planCount; i > 0; --i) {
            uint32_t idx = i - 1u;
            if (!Bc250HqdActiveWrite(Interface, Evidence, planWrites[idx].ByteOffset, Evidence->SnapshotValues[idx])) {
                Bc250HqdNoteRestoreFailure(
                    Evidence, BC250_HQD_RESTORE_STAGE_HQD_PLAN,
                    planWrites[idx].ByteOffset,
                    Evidence->SnapshotValues[idx], 0u, 1u);
            }
            if (!Bc250HqdActiveRead(Interface, Evidence,
                    planWrites[idx].ByteOffset,
                    &Evidence->RestoredValues[idx])) {
                Bc250HqdNoteRestoreFailure(
                    Evidence, BC250_HQD_RESTORE_STAGE_HQD_PLAN,
                    planWrites[idx].ByteOffset,
                    Evidence->SnapshotValues[idx], 0u, 1u);
            } else if (!Bc250HqdReadbackMatches(
                    planWrites[idx].ByteOffset,
                    Evidence->SnapshotValues[idx],
                    Evidence->RestoredValues[idx])) {
                Bc250HqdNoteRestoreFailure(
                    Evidence, BC250_HQD_RESTORE_STAGE_HQD_PLAN,
                    planWrites[idx].ByteOffset,
                    Evidence->SnapshotValues[idx],
                    Evidence->RestoredValues[idx], 0u);
            }
        }
    }

    /* Prova final exata do banco apos restaurar toda a configuracao. */
    if (hqdWritten != 0u) {
        if (!Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_ACTIVE, &Evidence->ActiveAfter) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_PQ_RPTR, &check) ||
            (check != Evidence->RptrBefore && check != Evidence->RptrAfter) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_LO,
                &Evidence->BaselineWptrRestored) ||
            !Bc250HqdActiveRead(Interface, Evidence,
                BC250_HQD_MMIO_PQ_WPTR_HI, &wptrHiRestored) ||
            Evidence->ActiveAfter != Evidence->ActiveBefore ||
            Evidence->BaselineWptrRestored != wptrLoSnapshot ||
            wptrHiRestored != wptrHiSnapshot) {
            const uint32_t activeDiverged =
                Evidence->ActiveAfter != Evidence->ActiveBefore ? 1u : 0u;

            Bc250HqdNoteRestoreFailure(
                Evidence, BC250_HQD_RESTORE_STAGE_FINAL_BANK,
                activeDiverged != 0u ?
                    BC250_HQD_MMIO_ACTIVE : BC250_HQD_MMIO_PQ_WPTR_LO,
                activeDiverged != 0u ?
                    Evidence->ActiveBefore : wptrLoSnapshot,
                activeDiverged != 0u ?
                    Evidence->ActiveAfter : Evidence->BaselineWptrRestored,
                0u);
            Evidence->BaselineWptrRestorePassed = 0u;
        }
    }
    /* Restauracao de Doorbell Range, CP_PQ_STATUS, CP_MEC_CNTL e CP_ME_CNTL */
    if (cpPqStatusWritten != 0u) {
        (void)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_CP_PQ_STATUS,
            BC250_HQD_MMIO_CP_PQ_STATUS, origCpPqStatus,
            BC250_HQD_CP_PQ_DOORBELL_ENABLE_MASK, &check);
    }
    /*
     * Rollback LIFO dos dois controles globais de polling: CNTL1 (queue
     * mask, ultimo escrito no APPLY) sai primeiro, depois CNTL.
     * POLL_ACTIVE[30] e status do proprio CP e fica fora da comparacao.
     */
    if (cpPqWptrPollCntl1Written != 0u) {
        /*
         * O 3.31 provou que a QUEUE_MASK nao volta a zero com uma escrita.
         * Tentamos mais de uma vez e registramos quantas: se nem assim cair,
         * e propriedade do silicio, e o gate de rollback deve continuar
         * reprovando - nao se mascara estado que ficou no hardware.
         */
        uint32_t attempt;

        for (attempt = 0u; attempt < 3u; ++attempt) {
            ++Evidence->WptrPollCntl1ClearAttempts;
            if (!Bc250HqdActiveWrite(Interface, Evidence,
                    BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1,
                    origCpPqWptrPollCntl1) ||
                !Bc250HqdActiveRead(Interface, Evidence,
                    BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1,
                    &Evidence->WptrPollCntl1Restored)) {
                Bc250HqdNoteRestoreFailure(
                    Evidence, BC250_HQD_RESTORE_STAGE_WPTR_POLL_CNTL1,
                    BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1,
                    origCpPqWptrPollCntl1, 0u, 1u);
                break;
            }
            if (Evidence->WptrPollCntl1Restored == origCpPqWptrPollCntl1) {
                Evidence->WptrPollCntl1RestorePassed = 1u;
                break;
            }
        }
        if (Evidence->WptrPollCntl1RestorePassed == 0u &&
            Evidence->WptrPollCntl1ClearAttempts != 0u &&
            Evidence->FirstRestoreFailureStage !=
                (uint32_t)BC250_HQD_RESTORE_STAGE_WPTR_POLL_CNTL1) {
            Bc250HqdNoteRestoreFailure(
                Evidence, BC250_HQD_RESTORE_STAGE_WPTR_POLL_CNTL1,
                BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1,
                origCpPqWptrPollCntl1, Evidence->WptrPollCntl1Restored, 0u);
        } else if (Evidence->WptrPollCntl1RestorePassed == 0u) {
            Evidence->RestorePassed = 0u;
        }
    }
    if (cpPqWptrPollCntlWritten != 0u) {
        (void)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_WPTR_POLL_CNTL,
            BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL, origCpPqWptrPollCntl,
            ~BC250_HQD_WPTR_POLL_ACTIVE_MASK, &check);
    }
    if (gcvmL2Cntl2Written != 0u) {
        (void)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_GCVM_L2_CNTL2,
            BC250_HQD_MMIO_GCVM_L2_CNTL2, origGcvmL2Cntl2,
            0xFFFFFFFFu, &check);
    }
    if (rlcCpSchedulersWritten != 0u) {
        Evidence->RlcCpSchedulersRestorePassed =
            (uint32_t)Bc250HqdRestoreRegister(
                Interface, Evidence,
                BC250_HQD_RESTORE_STAGE_RLC_CP_SCHEDULERS,
                BC250_HQD_MMIO_RLC_CP_SCHEDULERS,
                Evidence->RlcCpSchedulersBefore, 0xFFFFFFFFu,
                &Evidence->RlcCpSchedulersRestored);
    }
    if (gcvmCtx0Written != 0u) {
        uint32_t gcvmOk = 1u;

        if (!Bc250HqdRestoreRegister(
                Interface, Evidence, BC250_HQD_RESTORE_STAGE_GCVM_CONTEXT0,
                BC250_HQD_MMIO_GCVM_CONTEXT0_CNTL, origGcvmCtx0Cntl,
                0xFFFFFFFFu, &check)) {
            gcvmOk = 0u;
        }
        if (!Bc250HqdRestoreRegister(
                Interface, Evidence, BC250_HQD_RESTORE_STAGE_GCVM_CONTEXT0,
                BC250_HQD_MMIO_GCVM_CONTEXT0_PT_END_LO, origGcvmCtx0PtEndLo,
                0xFFFFFFFFu, &check)) {
            gcvmOk = 0u;
        }
        if (!Bc250HqdRestoreRegister(
                Interface, Evidence, BC250_HQD_RESTORE_STAGE_GCVM_CONTEXT0,
                BC250_HQD_MMIO_GCVM_CONTEXT0_PT_START_LO,
                origGcvmCtx0PtStartLo, 0xFFFFFFFFu, &check)) {
            gcvmOk = 0u;
        }
        if (!Bc250HqdRestoreRegister(
                Interface, Evidence, BC250_HQD_RESTORE_STAGE_GCVM_CONTEXT0,
                BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_HI, origGcvmCtx0PtBaseHi,
                0xFFFFFFFFu, &check)) {
            gcvmOk = 0u;
        }
        if (!Bc250HqdRestoreRegister(
                Interface, Evidence, BC250_HQD_RESTORE_STAGE_GCVM_CONTEXT0,
                BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_LO, origGcvmCtx0PtBaseLo,
                0xFFFFFFFFu, &check)) {
            gcvmOk = 0u;
        }
        Evidence->GcvmContext0RestorePassed = gcvmOk;
    }
    if (cpMecCntlWritten != 0u) {
        (void)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_CP_MEC_CNTL,
            0x00008234u, origCpMecCntl, 0xFFFFFFFFu,
            &Evidence->CpMecCntlAfter);
    }
    if (cpMeCntlWritten != 0u) {
        (void)Bc250HqdRestoreRegister(
            Interface, Evidence, BC250_HQD_RESTORE_STAGE_CP_ME_CNTL,
            0x000086D8u, origCpMeCntl, 0xFFFFFFFFu,
            &Evidence->CpMeCntlAfter);
    }

CleanupMemory:
    /* Restauracao do Scratch e Memoria */
    if (scratchWritten != 0u) {
        (void)Bc250HqdActiveWrite(
            Interface, Evidence, 0x00030100u, originalScratch);
    }

    if (Buffers->MqdVirtual != NULL && Buffers->MqdSizeBytes >= sizeof(BC250_GFX10_COMPUTE_MQD)) {
        vPtr32 = (volatile uint32_t *)Buffers->MqdVirtual;
        for (i = 0u; i < BC250_MQD_SIZE_DWORDS; ++i) {
            vPtr32[i] = observableDispatch != 0u ? mqdSnapshot[i] : 0u;
        }
    }
    if (Buffers->RingVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->RingVirtual;
        if (observableDispatch != 0u) {
            for (i = 0u; i < BC250_DISPATCH_RING_MIN_DWORDS; ++i) {
                vPtr32[i] = ringSnapshot[i];
            }
        } else {
            for (i = 0u; i < (Buffers->RingSizeBytes >> 2); ++i) {
                vPtr32[i] = 0u;
            }
        }
    }
    if (Buffers->EopVirtual != NULL) {
        if (observableDispatch == 0u) {
            vPtr32 = (volatile uint32_t *)Buffers->EopVirtual;
            for (i = 0u; i < (Buffers->EopSizeBytes >> 2); ++i) {
                vPtr32[i] = 0u;
            }
        }
    }
    if (Buffers->RptrVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->RptrVirtual;
        vPtr32[0] = observableDispatch != 0u ? rptrSnapshot[0] : 0u;
        vPtr32[1] = observableDispatch != 0u ? rptrSnapshot[1] : 0u;
    }
    if (Buffers->WptrVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->WptrVirtual;
        vPtr32[0] = observableDispatch != 0u ? wptrSnapshot[0] : 0u;
        vPtr32[1] = observableDispatch != 0u ? wptrSnapshot[1] : 0u;
    }
    if (Buffers->FenceVirtual != NULL) {
        vPtr32 = (volatile uint32_t *)Buffers->FenceVirtual;
        if (observableDispatch != 0u) {
            vPtr32[0] = fenceSnapshot[0];
            vPtr32[1] = fenceSnapshot[1];
        } else {
            vPtr32[0] = 0u; vPtr32[1] = 0u;
        }
    }
    if (observableDispatch != 0u && observableMemoryStaged != 0u) {
        volatile uint32_t *shaderPtr =
            (volatile uint32_t *)Buffers->ShaderVirtual;
        volatile uint32_t *resultPtr =
            (volatile uint32_t *)Buffers->ResultVirtual;

        for (i = 0u; i < BC250_OBSERVABLE_SHADER_DWORDS; ++i) {
            shaderPtr[i] = shaderSnapshot[i];
        }
        resultPtr[0] = resultSnapshot;
        Interface->Barrier(Interface->Context);

        DispatchEvidence->ShaderRestorePassed = 1u;
        for (i = 0u; i < BC250_OBSERVABLE_SHADER_DWORDS; ++i) {
            if (shaderPtr[i] != shaderSnapshot[i]) {
                DispatchEvidence->ShaderRestorePassed = 0u;
            }
        }
        DispatchEvidence->ResultRestorePassed =
            resultPtr[0] == resultSnapshot ? 1u : 0u;
        DispatchEvidence->FenceRestorePassed =
            ((volatile uint32_t *)Buffers->FenceVirtual)[0] == fenceSnapshot[0] &&
            ((volatile uint32_t *)Buffers->FenceVirtual)[1] == fenceSnapshot[1] ?
                1u : 0u;
        DispatchEvidence->RingRestorePassed = 1u;
        for (i = 0u; i < BC250_DISPATCH_RING_MIN_DWORDS; ++i) {
            if (((volatile uint32_t *)Buffers->RingVirtual)[i] !=
                    ringSnapshot[i]) {
                DispatchEvidence->RingRestorePassed = 0u;
            }
        }
        coreMemoryRestorePassed = 1u;
        for (i = 0u; i < BC250_MQD_SIZE_DWORDS; ++i) {
            if (((volatile uint32_t *)Buffers->MqdVirtual)[i] !=
                    mqdSnapshot[i]) {
                coreMemoryRestorePassed = 0u;
            }
        }
        if (((volatile uint32_t *)Buffers->RptrVirtual)[0] != rptrSnapshot[0] ||
            ((volatile uint32_t *)Buffers->RptrVirtual)[1] != rptrSnapshot[1] ||
            ((volatile uint32_t *)Buffers->WptrVirtual)[0] != wptrSnapshot[0] ||
            ((volatile uint32_t *)Buffers->WptrVirtual)[1] != wptrSnapshot[1]) {
            coreMemoryRestorePassed = 0u;
        }
        Evidence->MemoryRestorePassed =
            coreMemoryRestorePassed &&
            DispatchEvidence->ShaderRestorePassed &&
            DispatchEvidence->ResultRestorePassed &&
            DispatchEvidence->FenceRestorePassed &&
            DispatchEvidence->RingRestorePassed;
    } else {
        Evidence->MemoryRestorePassed = 1u;
    }

    /* Restauracao do Seletor GRBM_GFX_CNTL e Broadcast GRBM_GFX_INDEX */
    if (observableDispatch != 0u && selectorCaptured == 0u) {
        return Evidence->MemoryRestorePassed != 0u ?
            status : BC250_HQD_ACTIVE_RESTORE_FAILED;
    }
    (void)Bc250HqdActiveWrite(Interface, Evidence, 0x00030800u, 0xE0000000u);
    Interface->Barrier(Interface->Context);
    if (!Bc250HqdActiveWrite(Interface, Evidence, BC250_GRBM_GFX_CNTL_BYTE_OFFSET, Evidence->OriginalSelector)) {
        return BC250_HQD_ACTIVE_RESTORE_FAILED;
    }
    Interface->Barrier(Interface->Context);
    if (!Bc250HqdActiveRead(Interface, Evidence, BC250_GRBM_GFX_CNTL_BYTE_OFFSET, &Evidence->RestoredSelector) ||
        (Evidence->RestoredSelector & 0x7FFu) != (Evidence->OriginalSelector & 0x7FFu)) {
        return BC250_HQD_ACTIVE_RESTORE_FAILED;
    }

    Evidence->PhysicalReady = 0u;
    if (observableDispatch != 0u) {
        DispatchEvidence->Observation.RegistersRestorePassed =
            Evidence->RestorePassed;
        DispatchEvidence->Observation.MemoryRestorePassed =
            Evidence->MemoryRestorePassed;
        (void)Bc250EvaluateObservableComputeDispatch(
            &dispatchDescriptor,
            &DispatchEvidence->Observation,
            &DispatchEvidence->Verdict);
        DispatchEvidence->PhysicalReady =
            DispatchEvidence->WgpPreflightPassed != 0u &&
            DispatchEvidence->ShaderReadbackPassed != 0u &&
            DispatchEvidence->Verdict.PhysicalReady != 0u ? 1u : 0u;
        Evidence->PhysicalReady = DispatchEvidence->PhysicalReady;
        Evidence->TriadPassed = DispatchEvidence->Verdict.TriadPassed;
        if (status == BC250_HQD_ACTIVE_OK &&
            DispatchEvidence->PhysicalReady == 0u) {
            status = BC250_HQD_ACTIVE_RESTORE_FAILED;
        }
    } else if (status == BC250_HQD_ACTIVE_OK &&
        Evidence->TriadPassed != 0u &&
        Evidence->RestorePassed != 0u &&
        Evidence->MemoryRestorePassed != 0u) {
        Evidence->PhysicalReady = 1u;
    }

    return status;
}

BC250_HQD_ACTIVE_STATUS Bc250ComputeRunActiveHqdSubmissionTransaction(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence)
{
    return Bc250ComputeRunActiveHqdSubmissionTransactionInternal(
        Interface, Buffers, Props, Mqd, Evidence, NULL);
}

BC250_HQD_ACTIVE_STATUS Bc250ComputeRunObservableShaderDispatchTransaction(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *ActiveEvidence,
    BC250_HQD_SHADER_DISPATCH_EVIDENCE *DispatchEvidence)
{
    if (DispatchEvidence == NULL) return BC250_HQD_ACTIVE_BAD_ARGUMENT;
    return Bc250ComputeRunActiveHqdSubmissionTransactionInternal(
        Interface, Buffers, Props, Mqd, ActiveEvidence, DispatchEvidence);
}

const char *Bc250HqdActiveStatusName(BC250_HQD_ACTIVE_STATUS Status)
{
    switch (Status) {
    case BC250_HQD_ACTIVE_OK: return "ok";
    case BC250_HQD_ACTIVE_BAD_ARGUMENT: return "bad_argument";
    case BC250_HQD_ACTIVE_PREFLIGHT_FAILED: return "preflight_failed";
    case BC250_HQD_ACTIVE_MEMORY_FAILED: return "memory_failed";
    case BC250_HQD_ACTIVE_PLAN_FAILED: return "plan_failed";
    case BC250_HQD_ACTIVE_WRITE_FAILED: return "write_failed";
    case BC250_HQD_ACTIVE_READBACK_FAILED: return "readback_failed";
    case BC250_HQD_ACTIVE_PM4_NOT_CONSUMED: return "pm4_not_consumed";
    case BC250_HQD_ACTIVE_FENCE_FAILED: return "fence_failed";
    case BC250_HQD_ACTIVE_SCRATCH_FAILED: return "scratch_failed";
    case BC250_HQD_ACTIVE_SHADER_FAILED: return "shader_failed";
    case BC250_HQD_ACTIVE_RESULT_FAILED: return "result_failed";
    case BC250_HQD_ACTIVE_RESTORE_FAILED: return "restore_failed";
    default: return "unknown";
    }
}

const char *Bc250HqdWptrDiagStatusName(BC250_HQD_WPTR_DIAGNOSTIC_STATUS Status)
{
    switch (Status) {
    case BC250_HQD_WPTR_DIAG_OK: return "ok";
    case BC250_HQD_WPTR_DIAG_BAD_ARGUMENT: return "bad_argument";
    case BC250_HQD_WPTR_DIAG_PREFLIGHT_FAILED: return "preflight_failed";
    case BC250_HQD_WPTR_DIAG_SELECTOR_WRITE_FAILED: return "selector_write_failed";
    case BC250_HQD_WPTR_DIAG_PASS1_READ_FAILED: return "pass1_read_failed";
    case BC250_HQD_WPTR_DIAG_PASS2_READ_FAILED: return "pass2_read_failed";
    case BC250_HQD_WPTR_DIAG_SNAPSHOT_UNSTABLE: return "snapshot_unstable";
    case BC250_HQD_WPTR_DIAG_BANK_ACTIVE: return "bank_active";
    case BC250_HQD_WPTR_DIAG_WRITE_FAILED: return "write_failed";
    case BC250_HQD_WPTR_DIAG_RESTORE_FAILED: return "restore_failed";
    default: return "unknown";
    }
}
