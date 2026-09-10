#include "bc250_allocation_abi_contract.h"

#include <string.h>

BC250_ALLOCATION_ABI_STATUS Bc250AllocationAbiFormat(
    uint64_t AllocationToken,
    uint64_t ByteSize,
    uint64_t Alignment,
    uint32_t Flags,
    BC250_ALLOCATION_PRIVATE_V1 *Output)
{
    if (Output != NULL) {
        memset(Output, 0, sizeof(*Output));
    }
    if (Output == NULL) {
        return BC250_ALLOCATION_ABI_BAD_ARGUMENT;
    }
    if (AllocationToken == 0u || ByteSize == 0u) {
        return BC250_ALLOCATION_ABI_BAD_SIZE;
    }
    if (Flags != 0u) {
        return BC250_ALLOCATION_ABI_BAD_FLAGS;
    }
    if (Alignment < BC250_ALLOCATION_MIN_ALIGNMENT ||
        Alignment > BC250_ALLOCATION_MAX_ALIGNMENT ||
        (Alignment & (Alignment - 1u)) != 0u ||
        (ByteSize & (Alignment - 1u)) != 0u) {
        return BC250_ALLOCATION_ABI_BAD_ALIGNMENT;
    }
    Output->Magic = BC250_ALLOCATION_PRIVATE_MAGIC;
    Output->Version = BC250_ALLOCATION_PRIVATE_VERSION;
    Output->StructSize = (uint32_t)sizeof(*Output);
    Output->Flags = Flags;
    Output->AllocationToken = AllocationToken;
    Output->ByteSize = ByteSize;
    Output->Alignment = Alignment;
    return BC250_ALLOCATION_ABI_OK;
}

BC250_ALLOCATION_ABI_STATUS Bc250AllocationAbiParse(
    const void *PrivateData,
    uint32_t PrivateDataSize,
    BC250_ALLOCATION_PRIVATE_V1 *Request)
{
    BC250_ALLOCATION_PRIVATE_V1 candidate;

    if (Request != NULL) memset(Request, 0, sizeof(*Request));
    if (PrivateData == NULL || Request == NULL) {
        return BC250_ALLOCATION_ABI_BAD_ARGUMENT;
    }
    if (PrivateDataSize != sizeof(candidate)) {
        return BC250_ALLOCATION_ABI_BAD_SIZE;
    }
    memcpy(&candidate, PrivateData, sizeof(candidate));
    if (candidate.Magic != BC250_ALLOCATION_PRIVATE_MAGIC) {
        return BC250_ALLOCATION_ABI_BAD_MAGIC;
    }
    if (candidate.Version != BC250_ALLOCATION_PRIVATE_VERSION ||
        candidate.StructSize != sizeof(candidate)) {
        return BC250_ALLOCATION_ABI_BAD_VERSION;
    }
    if (candidate.Flags != 0u || candidate.AllocationToken == 0u ||
        candidate.ByteSize == 0u) {
        return candidate.Flags != 0u ? BC250_ALLOCATION_ABI_BAD_FLAGS :
            BC250_ALLOCATION_ABI_BAD_SIZE;
    }
    if (candidate.Alignment < BC250_ALLOCATION_MIN_ALIGNMENT ||
        candidate.Alignment > BC250_ALLOCATION_MAX_ALIGNMENT ||
        (candidate.Alignment & (candidate.Alignment - 1u)) != 0u ||
        (candidate.ByteSize & (candidate.Alignment - 1u)) != 0u) {
        return BC250_ALLOCATION_ABI_BAD_ALIGNMENT;
    }
    *Request = candidate;
    return BC250_ALLOCATION_ABI_OK;
}

int Bc250AllocationIsPrimary(
    const BC250_ALLOCATION_PRIVATE_V1 *Request)
{
    return Request != NULL &&
        BC250_ALLOCATION_TOKEN_CLASS(Request->AllocationToken) ==
            BC250_ALLOCATION_TOKEN_CLASS_PRIMARY;
}
