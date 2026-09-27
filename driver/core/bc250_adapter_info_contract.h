#ifndef BC250_ADAPTER_INFO_CONTRACT_H
#define BC250_ADAPTER_INFO_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_context_dispatcher_contract.h"
#include "bc250_gpuva_space_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_ADAPTER_NODE_COUNT          2u
#define BC250_ADAPTER_NODE_SDMA           0u
#define BC250_ADAPTER_NODE_COMPUTE        1u

/*
 * FONTE UNICA (07/09). Estes dois nomes valiam 48 e 0xFFFFFFFFFFFF, numeros que
 * o silicio nao tem: a hierarquia de 2 niveis em uso cobre 12 + 9 + 13 = 34 bits
 * (16 GiB), conforme derivado em docs/03 e em bc250_gpuva_space_contract.h. O que
 * o driver declara ao WDDM em GPUMMUCAPS ja era BC250_GPUVA_PT_VA_BITS; estes
 * defines eram uma segunda fonte de verdade -- morta, mas pronta para reintroduzir
 * o defeito ao ser ligada. Agora derivam, e nao podem mais divergir.
 */
#define BC250_ADAPTER_GPUVA_BITS          BC250_GPUVA_PT_VA_BITS
#define BC250_ADAPTER_MAX_GPUVA_ADDRESS   ((1ull << BC250_GPUVA_PT_VA_BITS) - 1ull)
#define BC250_ADAPTER_PAGE_SIZE_BYTES     4096u

#define BC250_ADAPTER_VRAM_BAR_INDEX      0u
#define BC250_ADAPTER_DOORBELL_BAR_INDEX  2u
#define BC250_ADAPTER_MMIO_BAR_INDEX      5u

typedef enum BC250_ADAPTER_INFO_STATUS {
    BC250_ADAPTER_INFO_OK = 0,
    BC250_ADAPTER_INFO_BAD_ARGUMENT,
    BC250_ADAPTER_INFO_INVALID_NODE
} BC250_ADAPTER_INFO_STATUS;

typedef struct BC250_NODE_INFO {
    uint32_t NodeIndex;
    BC250_ENGINE_TYPE EngineType;
    char FriendlyName[64];
    uint32_t SupportsGpuVa;
    uint32_t SupportsIoMmu;
    uint32_t ContextStatusReporting;
    uint32_t MaxSubmissionDwords;
} BC250_NODE_INFO;

typedef struct BC250_ADAPTER_CAPABILITIES {
    uint32_t NodeCount;
    uint32_t GpuVaBits;
    uint64_t MaxVaAddress;
    uint32_t PageSizeBytes;
    uint32_t VramBarIndex;
    uint32_t DoorbellBarIndex;
    uint32_t MmioBarIndex;
    uint32_t SdmaEngineCount;
    uint32_t ComputeEngineCount;
} BC250_ADAPTER_CAPABILITIES;

BC250_ADAPTER_INFO_STATUS Bc250GetAdapterCapabilities(
    BC250_ADAPTER_CAPABILITIES *Caps);

BC250_ADAPTER_INFO_STATUS Bc250GetNodeCount(
    uint32_t *NodeCount);

BC250_ADAPTER_INFO_STATUS Bc250GetNodeInfo(
    uint32_t NodeIndex,
    BC250_NODE_INFO *NodeInfo);

BC250_ADAPTER_INFO_STATUS Bc250QueryEngineStatus(
    uint32_t NodeIndex,
    uint32_t Rptr,
    uint32_t Wptr,
    uint32_t *IsBusy,
    uint32_t *PendingDwords);

const char *Bc250AdapterInfoStatusName(BC250_ADAPTER_INFO_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
