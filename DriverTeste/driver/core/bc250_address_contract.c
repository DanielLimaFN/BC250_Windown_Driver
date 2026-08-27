#include "bc250_address_contract.h"

#include <string.h>

#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)
#define BC250_UINT32_MAX_VALUE ((uint32_t)~(uint32_t)0)

static int Bc250AddressRangeEnd(
    uint64_t Start, uint64_t Length, uint64_t *End)
{
    if (Length == 0 || Start > BC250_UINT64_MAX_VALUE - (Length - 1u)) {
        return 0;
    }
    *End = Start + Length - 1u;
    return 1;
}

static int Bc250AddressIsPowerOfTwo(uint64_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

BC250_ADDRESS_STATUS
Bc250BindVramRegion(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    BC250_VRAM_ADDRESS_BINDING *Binding)
{
    BC250_VRAM_REGION region;
    uint64_t expectedMcEnd, regionEnd, mcEnd, paEnd;
    BC250_VRAM_STATUS allocatorStatus;

    if (Binding != NULL) memset(Binding, 0, sizeof(*Binding));
    if (Allocator == NULL || AddressSpace == NULL || Binding == NULL ||
        AddressSpace->SnapshotGeneration == 0) {
        return BC250_ADDRESS_BAD_ARGUMENT;
    }
    if ((AddressSpace->EvidenceFlags & BC250_ADDR_EVIDENCE_REQUIRED) !=
        BC250_ADDR_EVIDENCE_REQUIRED) {
        return BC250_ADDRESS_MISSING_EVIDENCE;
    }
    if (AddressSpace->TotalBytes == 0 ||
        AddressSpace->TotalBytes != Allocator->TotalBytes ||
        AddressSpace->CpuVisibleBytes != Allocator->CpuVisibleBytes ||
        AddressSpace->CpuVisibleBytes > AddressSpace->TotalBytes ||
        !Bc250AddressRangeEnd(AddressSpace->VramMcStart,
                              AddressSpace->TotalBytes, &expectedMcEnd) ||
        expectedMcEnd != AddressSpace->VramMcEnd ||
        !Bc250AddressRangeEnd(AddressSpace->VramBaseOffset,
                              AddressSpace->TotalBytes, &paEnd)) {
        return BC250_ADDRESS_TOPOLOGY_MISMATCH;
    }

    allocatorStatus = Bc250VramGetRegion(Allocator, Token, &region);
    if (allocatorStatus == BC250_VRAM_STALE_TOKEN) {
        return BC250_ADDRESS_STALE_TOKEN;
    }
    if (allocatorStatus != BC250_VRAM_OK) {
        return BC250_ADDRESS_BAD_ARGUMENT;
    }
    if (!Bc250AddressRangeEnd(region.Offset, region.Length, &regionEnd) ||
        regionEnd >= AddressSpace->TotalBytes ||
        AddressSpace->VramMcStart > BC250_UINT64_MAX_VALUE - region.Offset ||
        AddressSpace->VramBaseOffset > BC250_UINT64_MAX_VALUE - region.Offset) {
        return BC250_ADDRESS_RANGE_OVERFLOW;
    }

    Binding->Token = Token;
    Binding->Offset = region.Offset;
    Binding->Length = region.Length;
    Binding->McAddress = AddressSpace->VramMcStart + region.Offset;
    Binding->SystemPhysicalAddress =
        AddressSpace->VramBaseOffset + region.Offset;
    Binding->Alignment = region.Alignment;
    Binding->Role = region.Role;
    Binding->RegionFlags = region.Flags;
    Binding->CpuVisible = regionEnd < AddressSpace->CpuVisibleBytes ? 1u : 0u;
    Binding->NaturallyAligned =
        (region.Offset % region.Length) == 0 ? 1u : 0u;
    Binding->SnapshotGeneration = AddressSpace->SnapshotGeneration;

    if (!Bc250AddressRangeEnd(Binding->McAddress, Binding->Length, &mcEnd) ||
        !Bc250AddressRangeEnd(Binding->SystemPhysicalAddress,
                              Binding->Length, &paEnd) ||
        mcEnd > AddressSpace->VramMcEnd ||
        Binding->McAddress - AddressSpace->VramMcStart != region.Offset ||
        Binding->SystemPhysicalAddress - AddressSpace->VramBaseOffset !=
            region.Offset) {
        memset(Binding, 0, sizeof(*Binding));
        return BC250_ADDRESS_RANGE_OVERFLOW;
    }
    return BC250_ADDRESS_OK;
}

BC250_ADDRESS_STATUS
Bc250BuildPspTmrPlan(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    uint64_t RequiredSize,
    uint64_t RequiredAlignment,
    BC250_PSP_TMR_PLAN *Plan)
{
    BC250_ADDRESS_STATUS status;

    if (Plan != NULL) memset(Plan, 0, sizeof(*Plan));
    if (Plan == NULL || RequiredSize == 0 ||
        RequiredSize > BC250_UINT32_MAX_VALUE ||
        !Bc250AddressIsPowerOfTwo(RequiredAlignment)) {
        return BC250_ADDRESS_BAD_ARGUMENT;
    }

    status = Bc250BindVramRegion(
        Allocator, Token, AddressSpace, &Plan->Binding);
    if (status != BC250_ADDRESS_OK) return status;
    if (Plan->Binding.Role != BC250_VRAM_ROLE_TMR) {
        memset(Plan, 0, sizeof(*Plan));
        return BC250_ADDRESS_WRONG_ROLE;
    }
    if ((Plan->Binding.RegionFlags & BC250_VRAM_REGION_PERSISTENT) == 0) {
        memset(Plan, 0, sizeof(*Plan));
        return BC250_ADDRESS_NOT_PERSISTENT;
    }
    if (Plan->Binding.Length != RequiredSize) {
        memset(Plan, 0, sizeof(*Plan));
        return BC250_ADDRESS_SIZE_MISMATCH;
    }
    if (Plan->Binding.Alignment < RequiredAlignment ||
        (Plan->Binding.Offset & (RequiredAlignment - 1u)) != 0) {
        memset(Plan, 0, sizeof(*Plan));
        return BC250_ADDRESS_MISALIGNED;
    }

    Plan->BufferSize = (uint32_t)RequiredSize;
    Plan->TmrFlags = BC250_PSP_TMR_VIRT_PHY_ADDR;
    Plan->RequiredAlignment = RequiredAlignment;
    return BC250_ADDRESS_OK;
}

const char *Bc250AddressStatusName(BC250_ADDRESS_STATUS Status)
{
    switch (Status) {
    case BC250_ADDRESS_OK: return "ok";
    case BC250_ADDRESS_BAD_ARGUMENT: return "bad_argument";
    case BC250_ADDRESS_MISSING_EVIDENCE: return "missing_evidence";
    case BC250_ADDRESS_TOPOLOGY_MISMATCH: return "topology_mismatch";
    case BC250_ADDRESS_RANGE_OVERFLOW: return "range_overflow";
    case BC250_ADDRESS_STALE_TOKEN: return "stale_token";
    case BC250_ADDRESS_WRONG_ROLE: return "wrong_role";
    case BC250_ADDRESS_NOT_PERSISTENT: return "not_persistent";
    case BC250_ADDRESS_SIZE_MISMATCH: return "size_mismatch";
    case BC250_ADDRESS_MISALIGNED: return "misaligned";
    default: return "unknown";
    }
}
