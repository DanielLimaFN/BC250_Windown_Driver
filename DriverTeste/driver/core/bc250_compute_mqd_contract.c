#include "bc250_compute_mqd_contract.h"

#include <string.h>

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

    if ((Props->MqdGpuAddress & (BC250_MQD_ALIGNMENT - 1u)) != 0 ||
        (Props->HqdBaseGpuAddress & (BC250_HQD_RING_ALIGNMENT - 1u)) != 0 ||
        (Props->EopGpuAddress & (BC250_HQD_EOP_ALIGNMENT - 1u)) != 0) {
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
    Mqd->CpHqdEopBaseAddrHi = (uint32_t)(eopShifted >> 32);

    eopOrder = Bc250OrderBase2(Props->EopSizeBytes / 4u) - 1u;
    Mqd->CpHqdEopControl = eopOrder & 0x1Fu;

    if (Props->UseDoorbell) {
        Mqd->CpHqdPqDoorbellControl = (Props->DoorbellIndex & 0x3FFFFFFFu) | (1u << 31);
    } else {
        Mqd->CpHqdPqDoorbellControl = 0;
    }

    Mqd->CpHqdDequeueRequest = 0;
    Mqd->CpHqdPqRptr = 0;
    Mqd->CpHqdPqWptrLo = 0;
    Mqd->CpHqdPqWptrHi = 0;

    Mqd->CpMqdBaseAddrLo = (uint32_t)(Props->MqdGpuAddress & 0xFFFFFFFCull);
    Mqd->CpMqdBaseAddrHi = (uint32_t)(Props->MqdGpuAddress >> 32);
    Mqd->CpMqdControl = 0; /* VMID 0 */

    hqdShifted = Props->HqdBaseGpuAddress >> 8;
    Mqd->CpHqdPqBaseLo = (uint32_t)(hqdShifted & 0xFFFFFFFFull);
    Mqd->CpHqdPqBaseHi = (uint32_t)(hqdShifted >> 32);

    queueSizeOrder = Bc250OrderBase2(Props->QueueSizeDwords) - 1u;
    rptrBlockSize = Bc250OrderBase2(4096u / 4u) - 1u;
    Mqd->CpHqdPqControl = (queueSizeOrder & 0x1Fu) |
                          ((rptrBlockSize & 0x1Fu) << 8) |
                          (1u << 26) /* unord_dispatch */ |
                          (1u << 28) /* priv_state */ |
                          (1u << 29); /* kmd_queue */

    Mqd->CpHqdPqRptrReportAddrLo = (uint32_t)(Props->RptrGpuAddress & 0xFFFFFFFCull);
    Mqd->CpHqdPqRptrReportAddrHi = (uint32_t)(Props->RptrGpuAddress >> 32);
    Mqd->CpHqdPqWptrPollAddrLo = (uint32_t)(Props->WptrGpuAddress & 0xFFFFFFFCull);
    Mqd->CpHqdPqWptrPollAddrHi = (uint32_t)(Props->WptrGpuAddress >> 32);

    Mqd->CpHqdVmid = 0;
    Mqd->CpHqdPersistentState = 0x53u & 0x7Fu;
    Mqd->CpHqdIbControl = 3u;
    Mqd->CpHqdQuantum = (1u << 0) | (1u << 1) | (1u << 16);
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
        Mqd->CpHqdEopBaseAddrHi != (uint32_t)(eopShifted >> 32)) {
        return BC250_MQD_MISALIGNED_ADDRESS;
    }

    hqdShifted = Props->HqdBaseGpuAddress >> 8;
    if (Mqd->CpHqdPqBaseLo != (uint32_t)(hqdShifted & 0xFFFFFFFFull) ||
        Mqd->CpHqdPqBaseHi != (uint32_t)(hqdShifted >> 32)) {
        return BC250_MQD_MISALIGNED_ADDRESS;
    }

    expectedQueueOrder = (Bc250OrderBase2(Props->QueueSizeDwords) - 1u) & 0x1Fu;
    if ((Mqd->CpHqdPqControl & 0x1Fu) != expectedQueueOrder) {
        return BC250_MQD_INVALID_QUEUE_SIZE;
    }

    return BC250_MQD_OK;
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

const char *Bc250MqdStatusName(BC250_MQD_STATUS Status)
{
    switch (Status) {
    case BC250_MQD_OK: return "ok";
    case BC250_MQD_BAD_ARGUMENT: return "bad_argument";
    case BC250_MQD_MISALIGNED_ADDRESS: return "misaligned_address";
    case BC250_MQD_INVALID_QUEUE_SIZE: return "invalid_queue_size";
    case BC250_MQD_INVALID_EOP_SIZE: return "invalid_eop_size";
    default: return "unknown";
    }
}
