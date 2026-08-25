#include "bc250_adapter_info_contract.h"

#include <string.h>

BC250_ADAPTER_INFO_STATUS Bc250GetAdapterCapabilities(
    BC250_ADAPTER_CAPABILITIES *Caps)
{
    if (Caps == NULL) {
        return BC250_ADAPTER_INFO_BAD_ARGUMENT;
    }

    memset(Caps, 0, sizeof(BC250_ADAPTER_CAPABILITIES));
    Caps->NodeCount = BC250_ADAPTER_NODE_COUNT;
    Caps->GpuVaBits = BC250_ADAPTER_GPUVA_BITS;
    Caps->MaxVaAddress = BC250_ADAPTER_MAX_GPUVA_ADDRESS;
    Caps->PageSizeBytes = BC250_ADAPTER_PAGE_SIZE_BYTES;
    Caps->VramBarIndex = BC250_ADAPTER_VRAM_BAR_INDEX;
    Caps->DoorbellBarIndex = BC250_ADAPTER_DOORBELL_BAR_INDEX;
    Caps->MmioBarIndex = BC250_ADAPTER_MMIO_BAR_INDEX;
    Caps->SdmaEngineCount = 1u;
    Caps->ComputeEngineCount = 1u;

    return BC250_ADAPTER_INFO_OK;
}

BC250_ADAPTER_INFO_STATUS Bc250GetNodeCount(
    uint32_t *NodeCount)
{
    if (NodeCount == NULL) {
        return BC250_ADAPTER_INFO_BAD_ARGUMENT;
    }

    *NodeCount = BC250_ADAPTER_NODE_COUNT;
    return BC250_ADAPTER_INFO_OK;
}

BC250_ADAPTER_INFO_STATUS Bc250GetNodeInfo(
    uint32_t NodeIndex,
    BC250_NODE_INFO *NodeInfo)
{
    size_t i;
    const char *name = "";

    if (NodeInfo == NULL) {
        return BC250_ADAPTER_INFO_BAD_ARGUMENT;
    }
    if (NodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return BC250_ADAPTER_INFO_INVALID_NODE;
    }

    memset(NodeInfo, 0, sizeof(BC250_NODE_INFO));
    NodeInfo->NodeIndex = NodeIndex;
    NodeInfo->SupportsGpuVa = 1u;
    NodeInfo->SupportsIoMmu = 1u;
    NodeInfo->ContextStatusReporting = 1u;

    if (NodeIndex == BC250_ADAPTER_NODE_SDMA) {
        NodeInfo->EngineType = BC250_ENGINE_TYPE_SDMA;
        name = "BC250 SDMA0 Engine";
        NodeInfo->MaxSubmissionDwords = 1024u * 1024u;
    } else {
        NodeInfo->EngineType = BC250_ENGINE_TYPE_COMPUTE;
        name = "BC250 Compute MEC Engine";
        NodeInfo->MaxSubmissionDwords = 16384u;
    }

    for (i = 0; i < sizeof(NodeInfo->FriendlyName) - 1u && name[i] != '\0'; ++i) {
        NodeInfo->FriendlyName[i] = name[i];
    }
    NodeInfo->FriendlyName[i] = '\0';

    return BC250_ADAPTER_INFO_OK;
}

BC250_ADAPTER_INFO_STATUS Bc250QueryEngineStatus(
    uint32_t NodeIndex,
    uint32_t Rptr,
    uint32_t Wptr,
    uint32_t *IsBusy,
    uint32_t *PendingDwords)
{
    if (IsBusy == NULL || PendingDwords == NULL) {
        return BC250_ADAPTER_INFO_BAD_ARGUMENT;
    }
    if (NodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return BC250_ADAPTER_INFO_INVALID_NODE;
    }

    if (Rptr == Wptr) {
        *IsBusy = 0u;
        *PendingDwords = 0u;
    } else {
        *IsBusy = 1u;
        *PendingDwords = (Wptr >= Rptr) ? (Wptr - Rptr) : (Rptr - Wptr);
    }

    return BC250_ADAPTER_INFO_OK;
}

const char *Bc250AdapterInfoStatusName(BC250_ADAPTER_INFO_STATUS Status)
{
    switch (Status) {
    case BC250_ADAPTER_INFO_OK: return "ok";
    case BC250_ADAPTER_INFO_BAD_ARGUMENT: return "bad_argument";
    case BC250_ADAPTER_INFO_INVALID_NODE: return "invalid_node";
    default: return "unknown";
    }
}
