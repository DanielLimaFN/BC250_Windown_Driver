#include "bc250_gcvm_contract.h"

#include <string.h>

#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)
#define BC250_PTE_ADDR_MASK    0x0000FFFFFFFFF000ull
#define BC250_PTE_PRT_BIT      (1ull << 54)

BC250_GCVM_STATUS Bc250GcvmEncodePte(
    uint64_t PhysicalAddress,
    uint32_t Flags,
    uint64_t *PteOut)
{
    uint64_t pte;

    if (PteOut == NULL) return BC250_GCVM_BAD_ARGUMENT;
    *PteOut = 0;
    if ((PhysicalAddress & BC250_PAGE_MASK_4K) != 0) {
        return BC250_GCVM_MISALIGNED;
    }
    if ((PhysicalAddress & ~BC250_PTE_ADDR_MASK) != 0) {
        return BC250_GCVM_OUT_OF_RANGE;
    }

    pte = PhysicalAddress & BC250_PTE_ADDR_MASK;
    if (Flags & BC250_PTE_VALID)      pte |= (1ull << 0);
    if (Flags & BC250_PTE_SYSTEM)     pte |= (1ull << 1);
    if (Flags & BC250_PTE_SNOOPED)    pte |= (1ull << 2);
    if (Flags & BC250_PTE_TMZ)        pte |= (1ull << 3);
    if (Flags & BC250_PTE_READABLE)   pte |= (1ull << 5);
    if (Flags & BC250_PTE_WRITEABLE)  pte |= (1ull << 6);
    if (Flags & BC250_PTE_EXECUTABLE) pte |= (1ull << 7);
    if (Flags & BC250_PTE_PRT)        pte |= BC250_PTE_PRT_BIT;

    *PteOut = pte;
    return BC250_GCVM_OK;
}

BC250_GCVM_STATUS Bc250GcvmDecodePte(
    uint64_t Pte,
    uint64_t *PhysicalAddressOut,
    uint32_t *FlagsOut)
{
    uint32_t flags = 0;

    if (PhysicalAddressOut == NULL || FlagsOut == NULL) {
        return BC250_GCVM_BAD_ARGUMENT;
    }

    *PhysicalAddressOut = Pte & BC250_PTE_ADDR_MASK;
    if (Pte & (1ull << 0)) flags |= BC250_PTE_VALID;
    if (Pte & (1ull << 1)) flags |= BC250_PTE_SYSTEM;
    if (Pte & (1ull << 2)) flags |= BC250_PTE_SNOOPED;
    if (Pte & (1ull << 3)) flags |= BC250_PTE_TMZ;
    if (Pte & (1ull << 5)) flags |= BC250_PTE_READABLE;
    if (Pte & (1ull << 6)) flags |= BC250_PTE_WRITEABLE;
    if (Pte & (1ull << 7)) flags |= BC250_PTE_EXECUTABLE;
    if (Pte & BC250_PTE_PRT_BIT) flags |= BC250_PTE_PRT;

    *FlagsOut = flags;
    return BC250_GCVM_OK;
}

BC250_GCVM_STATUS Bc250GcvmBuildPageTablePlan(
    uint64_t VaStart,
    uint64_t PhysicalStart,
    uint64_t Length,
    uint32_t PteFlags,
    BC250_GCVM_PAGE_TABLE_PLAN *Plan)
{
    uint64_t pageCount64;

    if (Plan != NULL) memset(Plan, 0, sizeof(*Plan));
    if (Plan == NULL || Length == 0) return BC250_GCVM_BAD_ARGUMENT;
    if ((VaStart & BC250_PAGE_MASK_4K) != 0 ||
        (PhysicalStart & BC250_PAGE_MASK_4K) != 0 ||
        (Length & BC250_PAGE_MASK_4K) != 0) {
        return BC250_GCVM_MISALIGNED;
    }
    if (VaStart > BC250_UINT64_MAX_VALUE - (Length - 1ull) ||
        PhysicalStart > BC250_UINT64_MAX_VALUE - (Length - 1ull)) {
        return BC250_GCVM_RANGE_OVERFLOW;
    }

    pageCount64 = Length / BC250_PAGE_SIZE_4K;
    if (pageCount64 == 0) return BC250_GCVM_PAGE_COUNT_ZERO;
    if (pageCount64 > (BC250_UINT64_MAX_VALUE / sizeof(uint64_t)) ||
        pageCount64 > (uint64_t)MAXULONG) {
        return BC250_GCVM_OUT_OF_RANGE;
    }

    Plan->VaStart = VaStart;
    Plan->VaEnd = VaStart + Length - 1ull;
    Plan->PhysicalStart = PhysicalStart;
    Plan->Length = Length;
    Plan->PageCount = (uint32_t)pageCount64;
    Plan->TableSizeBytes = Plan->PageCount * (uint32_t)sizeof(uint64_t);
    Plan->TableAlignment = (uint32_t)BC250_PAGE_SIZE_4K;
    Plan->PteFlags = PteFlags;
    return BC250_GCVM_OK;
}

BC250_GCVM_STATUS Bc250GcvmPopulatePageTable(
    const BC250_GCVM_PAGE_TABLE_PLAN *Plan,
    uint64_t *PteBuffer,
    size_t BufferBytes)
{
    uint32_t i;
    BC250_GCVM_STATUS status;

    if (Plan == NULL || PteBuffer == NULL || Plan->PageCount == 0) {
        return BC250_GCVM_BAD_ARGUMENT;
    }
    if (BufferBytes < (size_t)Plan->TableSizeBytes) {
        return BC250_GCVM_BUFFER_TOO_SMALL;
    }

    for (i = 0; i < Plan->PageCount; ++i) {
        uint64_t pagePhys = Plan->PhysicalStart + ((uint64_t)i * BC250_PAGE_SIZE_4K);
        uint64_t pte = 0;

        status = Bc250GcvmEncodePte(pagePhys, Plan->PteFlags, &pte);
        if (status != BC250_GCVM_OK) return status;
        PteBuffer[i] = pte;
    }

    return BC250_GCVM_OK;
}

BC250_GCVM_STATUS Bc250GcvmTranslateVa(
    const BC250_GCVM_PAGE_TABLE_PLAN *Plan,
    const uint64_t *PteBuffer,
    uint64_t Va,
    uint64_t *PhysicalAddressOut)
{
    uint64_t pageIndex64;
    uint64_t pageOffset;
    uint64_t decodedPhys;
    uint32_t decodedFlags;
    BC250_GCVM_STATUS status;

    if (PhysicalAddressOut != NULL) *PhysicalAddressOut = 0;
    if (Plan == NULL || PteBuffer == NULL || PhysicalAddressOut == NULL ||
        Plan->PageCount == 0) {
        return BC250_GCVM_BAD_ARGUMENT;
    }
    if (Va < Plan->VaStart || Va > Plan->VaEnd) {
        return BC250_GCVM_TRANSLATION_FAULT;
    }

    pageIndex64 = (Va - Plan->VaStart) / BC250_PAGE_SIZE_4K;
    pageOffset = Va & BC250_PAGE_MASK_4K;
    if (pageIndex64 >= (uint64_t)Plan->PageCount) {
        return BC250_GCVM_TRANSLATION_FAULT;
    }

    status = Bc250GcvmDecodePte(
        PteBuffer[pageIndex64], &decodedPhys, &decodedFlags);
    if (status != BC250_GCVM_OK || !(decodedFlags & BC250_PTE_VALID)) {
        return BC250_GCVM_TRANSLATION_FAULT;
    }

    *PhysicalAddressOut = decodedPhys + pageOffset;
    return BC250_GCVM_OK;
}

const char *Bc250GcvmStatusName(BC250_GCVM_STATUS Status)
{
    switch (Status) {
    case BC250_GCVM_OK: return "ok";
    case BC250_GCVM_BAD_ARGUMENT: return "bad_argument";
    case BC250_GCVM_MISALIGNED: return "misaligned";
    case BC250_GCVM_RANGE_OVERFLOW: return "range_overflow";
    case BC250_GCVM_OUT_OF_RANGE: return "out_of_range";
    case BC250_GCVM_PAGE_COUNT_ZERO: return "page_count_zero";
    case BC250_GCVM_BUFFER_TOO_SMALL: return "buffer_too_small";
    case BC250_GCVM_TRANSLATION_FAULT: return "translation_fault";
    default: return "unknown";
    }
}
