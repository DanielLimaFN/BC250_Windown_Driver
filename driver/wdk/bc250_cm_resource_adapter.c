#include "bc250_cm_resource_adapter.h"

#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)

static BC250_CM_RESOURCE_STATUS
Bc250DecodeMemoryDescriptor(
    const CM_PARTIAL_RESOURCE_DESCRIPTOR *Descriptor,
    BC250_MEMORY_RESOURCE *MemoryResource)
{
    uint64_t length;

    if (Descriptor == NULL || MemoryResource == NULL ||
        Descriptor->u.Memory.Start.QuadPart < 0) {
        return BC250_CM_RESOURCE_BAD_MEMORY_DESCRIPTOR;
    }

    if (Descriptor->Type == CmResourceTypeMemory) {
        length = (uint64_t)Descriptor->u.Memory.Length;
    } else if (Descriptor->Type == CmResourceTypeMemoryLarge) {
        switch (Descriptor->Flags & CM_RESOURCE_MEMORY_LARGE) {
        case CM_RESOURCE_MEMORY_LARGE_40:
            length = (uint64_t)Descriptor->u.Memory40.Length40 << 8;
            break;
        case CM_RESOURCE_MEMORY_LARGE_48:
            length = (uint64_t)Descriptor->u.Memory48.Length48 << 16;
            break;
        case CM_RESOURCE_MEMORY_LARGE_64:
            length = (uint64_t)Descriptor->u.Memory64.Length64 << 32;
            break;
        default:
            return BC250_CM_RESOURCE_BAD_MEMORY_DESCRIPTOR;
        }
    } else {
        return BC250_CM_RESOURCE_BAD_MEMORY_DESCRIPTOR;
    }

    if (length == 0 ||
        (uint64_t)Descriptor->u.Memory.Start.QuadPart >
            BC250_UINT64_MAX_VALUE - (length - 1u)) {
        return BC250_CM_RESOURCE_BAD_MEMORY_DESCRIPTOR;
    }

    MemoryResource->Start = (uint64_t)Descriptor->u.Memory.Start.QuadPart;
    MemoryResource->Length = length;
    return BC250_CM_RESOURCE_OK;
}

BC250_CM_RESOURCE_STATUS
Bc250CollectTranslatedMemoryResources(
    const CM_RESOURCE_LIST *TranslatedResourceList,
    BC250_MEMORY_RESOURCE *MemoryResources,
    size_t MemoryResourceCapacity,
    size_t *MemoryResourceCount)
{
    const unsigned char *cursor;
    size_t outputCount = 0;
    ULONG fullIndex;

    if (MemoryResourceCount == NULL) {
        return BC250_CM_RESOURCE_BAD_ARGUMENT;
    }
    *MemoryResourceCount = 0;
    if (TranslatedResourceList == NULL || MemoryResources == NULL ||
        MemoryResourceCapacity == 0) {
        return BC250_CM_RESOURCE_BAD_ARGUMENT;
    }
    if (TranslatedResourceList->Count == 0 ||
        TranslatedResourceList->Count > BC250_CM_MAX_FULL_DESCRIPTORS) {
        return BC250_CM_RESOURCE_BAD_COUNT;
    }

    cursor = (const unsigned char *)&TranslatedResourceList->List[0];
    for (fullIndex = 0; fullIndex < TranslatedResourceList->Count; ++fullIndex) {
        const CM_FULL_RESOURCE_DESCRIPTOR *full;
        const CM_PARTIAL_RESOURCE_LIST *partial;
        size_t descriptorBytes;
        ULONG partialIndex;

        full = (const CM_FULL_RESOURCE_DESCRIPTOR *)cursor;
        partial = &full->PartialResourceList;
        if (partial->Count > BC250_CM_MAX_PARTIAL_DESCRIPTORS) {
            return BC250_CM_RESOURCE_BAD_COUNT;
        }

        for (partialIndex = 0; partialIndex < partial->Count; ++partialIndex) {
            const CM_PARTIAL_RESOURCE_DESCRIPTOR *descriptor;
            BC250_CM_RESOURCE_STATUS status;

            descriptor = &partial->PartialDescriptors[partialIndex];
            if (descriptor->Type == CmResourceTypeDeviceSpecific) {
                /*
                 * DataSize bytes follow this descriptor and make the next
                 * full-list offset variable. They are irrelevant to BAR
                 * identity, so refuse the layout instead of guessing its
                 * padding or trusting an unbounded byte count.
                 */
                return BC250_CM_RESOURCE_UNSUPPORTED_LAYOUT;
            }
            if (descriptor->Type != CmResourceTypeMemory &&
                descriptor->Type != CmResourceTypeMemoryLarge) {
                continue;
            }
            if (outputCount >= MemoryResourceCapacity) {
                return BC250_CM_RESOURCE_OUTPUT_TOO_SMALL;
            }
            status = Bc250DecodeMemoryDescriptor(
                descriptor, &MemoryResources[outputCount]);
            if (status != BC250_CM_RESOURCE_OK) {
                return status;
            }
            ++outputCount;
        }

        descriptorBytes = (size_t)partial->Count *
                          sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
        cursor = (const unsigned char *)&partial->PartialDescriptors[0];
        if ((ULONG_PTR)cursor > MAXULONG_PTR - descriptorBytes) {
            return BC250_CM_RESOURCE_POINTER_OVERFLOW;
        }
        cursor += descriptorBytes;
    }

    if (outputCount == 0) {
        return BC250_CM_RESOURCE_BAD_MEMORY_DESCRIPTOR;
    }
    *MemoryResourceCount = outputCount;
    return BC250_CM_RESOURCE_OK;
}

BC250_CM_RESOURCE_STATUS
Bc250ResolveCmTranslatedBars(
    const CM_RESOURCE_LIST *TranslatedResourceList,
    const BC250_PCI_BAR_RESOURCE *Bars,
    size_t BarCount,
    BC250_RESOLVED_BAR_RESOURCE *Resolved,
    size_t ResolvedCapacity,
    size_t *ResolvedCount,
    BC250_RESOURCE_STATUS *ContractStatus)
{
    BC250_MEMORY_RESOURCE memoryResources[BC250_CM_MAX_MEMORY_RESOURCES];
    BC250_CM_RESOURCE_STATUS status;
    BC250_RESOURCE_STATUS contract;
    size_t memoryResourceCount = 0;

    if (ResolvedCount == NULL) {
        return BC250_CM_RESOURCE_BAD_ARGUMENT;
    }
    *ResolvedCount = 0;
    if (ContractStatus != NULL) {
        *ContractStatus = BC250_RESOURCE_BAD_ARGUMENT;
    }

    status = Bc250CollectTranslatedMemoryResources(
        TranslatedResourceList,
        memoryResources,
        BC250_CM_MAX_MEMORY_RESOURCES,
        &memoryResourceCount);
    if (status != BC250_CM_RESOURCE_OK) {
        return status;
    }

    contract = Bc250ResolveTranslatedBars(
        memoryResources,
        memoryResourceCount,
        Bars,
        BarCount,
        Resolved,
        ResolvedCapacity,
        ResolvedCount);
    if (ContractStatus != NULL) {
        *ContractStatus = contract;
    }
    return contract == BC250_RESOURCE_OK
        ? BC250_CM_RESOURCE_OK
        : BC250_CM_RESOURCE_CONTRACT_REJECTED;
}

const char *Bc250CmResourceStatusName(BC250_CM_RESOURCE_STATUS Status)
{
    switch (Status) {
    case BC250_CM_RESOURCE_OK: return "ok";
    case BC250_CM_RESOURCE_BAD_ARGUMENT: return "bad_argument";
    case BC250_CM_RESOURCE_BAD_COUNT: return "bad_count";
    case BC250_CM_RESOURCE_BAD_MEMORY_DESCRIPTOR: return "bad_memory_descriptor";
    case BC250_CM_RESOURCE_UNSUPPORTED_LAYOUT: return "unsupported_layout";
    case BC250_CM_RESOURCE_OUTPUT_TOO_SMALL: return "output_too_small";
    case BC250_CM_RESOURCE_POINTER_OVERFLOW: return "pointer_overflow";
    case BC250_CM_RESOURCE_CONTRACT_REJECTED: return "contract_rejected";
    default: return "unknown";
    }
}
