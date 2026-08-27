#include "bc250_compute_bootstrap.h"

#include <string.h>

#define BC250_UINT32_MAX_VALUE ((uint32_t)~(uint32_t)0)
#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)

static int Bc250PowerOfTwo(uint64_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

static uint64_t Bc250SealMix(uint64_t Seal, uint64_t Value)
{
    Seal ^= Value;
    return Seal * 0x100000001B3ull;
}

static uint64_t Bc250InventorySeal(const BC250_VRAM_ALLOCATOR *Allocator)
{
    uint64_t seal = 0xCBF29CE484222325ull;
    uint32_t i;

    seal = Bc250SealMix(seal, Allocator->TotalBytes);
    seal = Bc250SealMix(seal, Allocator->CpuVisibleBytes);
    seal = Bc250SealMix(seal, Allocator->NextGeneration);
    seal = Bc250SealMix(seal, Allocator->RegionCount);
    for (i = 0; i < BC250_VRAM_MAX_REGIONS; ++i) {
        const BC250_VRAM_REGION *region = &Allocator->Regions[i];
        seal = Bc250SealMix(seal, region->Offset);
        seal = Bc250SealMix(seal, region->Length);
        seal = Bc250SealMix(seal, region->Alignment);
        seal = Bc250SealMix(seal, region->Role);
        seal = Bc250SealMix(seal, region->Flags);
        seal = Bc250SealMix(seal, region->Generation);
        seal = Bc250SealMix(seal, region->InUse);
    }
    return seal;
}

static uint64_t Bc250SnapshotSeal(const BC250_VRAM_ADDRESS_SPACE *Space)
{
    uint64_t seal = 0xCBF29CE484222325ull;

    seal = Bc250SealMix(seal, Space->TotalBytes);
    seal = Bc250SealMix(seal, Space->CpuVisibleBytes);
    seal = Bc250SealMix(seal, Space->VramMcStart);
    seal = Bc250SealMix(seal, Space->VramMcEnd);
    seal = Bc250SealMix(seal, Space->VramBaseOffset);
    seal = Bc250SealMix(seal, Space->EvidenceFlags);
    seal = Bc250SealMix(seal, Space->SnapshotGeneration);
    return seal;
}

static int Bc250SnapshotValid(
    const BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_VRAM_ADDRESS_SPACE *Space)
{
    uint64_t expectedEnd;

    if (Space == NULL || Space->TotalBytes == 0 ||
        Space->TotalBytes != Bootstrap->Allocator.TotalBytes ||
        Space->CpuVisibleBytes != Bootstrap->Allocator.CpuVisibleBytes ||
        Space->SnapshotGeneration == 0 ||
        (Space->EvidenceFlags & BC250_ADDR_EVIDENCE_REQUIRED) !=
            BC250_ADDR_EVIDENCE_REQUIRED ||
        Space->VramMcStart > BC250_UINT64_MAX_VALUE - (Space->TotalBytes - 1u) ||
        Space->VramBaseOffset >
            BC250_UINT64_MAX_VALUE - (Space->TotalBytes - 1u)) {
        return 0;
    }
    expectedEnd = Space->VramMcStart + Space->TotalBytes - 1u;
    return Space->VramMcEnd == expectedEnd;
}

static int Bc250ManifestValid(
    const BC250_CP_FIRMWARE_ENTRY *Manifest,
    uint32_t Count,
    uint64_t *LargestPayload)
{
    static const uint32_t expected[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT] = {
        BC250_PSP_FW_CP_PFP,
        BC250_PSP_FW_CP_ME,
        BC250_PSP_FW_CP_CE,
        BC250_PSP_FW_CP_MEC,
        BC250_PSP_FW_CP_MEC_JT1
    };
    uint32_t i;
    uint64_t largest = 0;

    if (Manifest == NULL || LargestPayload == NULL ||
        Count != BC250_CP_BOOTSTRAP_FIRMWARE_COUNT) return 0;
    for (i = 0; i < Count; ++i) {
        if (Manifest[i].FirmwareType != expected[i] ||
            Manifest[i].PayloadBytes == 0) return 0;
        if ((uint64_t)Manifest[i].PayloadBytes > largest) {
            largest = Manifest[i].PayloadBytes;
        }
    }
    *LargestPayload = largest;
    return 1;
}

static int Bc250TokenEqual(BC250_VRAM_TOKEN Left, BC250_VRAM_TOKEN Right)
{
    return Left.Slot == Right.Slot && Left.Generation == Right.Generation;
}

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapInit(
    BC250_CP_BOOTSTRAP *Bootstrap,
    uint64_t TotalBytes,
    uint64_t CpuVisibleBytes,
    BC250_VRAM_STATUS *VramStatus)
{
    BC250_VRAM_STATUS status;

    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (Bootstrap == NULL) return BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    memset(Bootstrap, 0, sizeof(*Bootstrap));
    status = Bc250VramAllocatorInit(
        &Bootstrap->Allocator, TotalBytes, CpuVisibleBytes);
    if (VramStatus != NULL) *VramStatus = status;
    if (status != BC250_VRAM_OK) return BC250_CP_BOOTSTRAP_INVENTORY_REJECTED;
    Bootstrap->State = BC250_CP_BOOTSTRAP_INVENTORY_OPEN;
    return BC250_CP_BOOTSTRAP_OK;
}

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapReserveInherited(
    BC250_CP_BOOTSTRAP *Bootstrap,
    uint64_t Offset,
    uint64_t Length,
    uint64_t Alignment,
    BC250_VRAM_ROLE Role,
    uint32_t Flags,
    BC250_VRAM_TOKEN *Token,
    BC250_VRAM_STATUS *VramStatus)
{
    BC250_VRAM_STATUS status;

    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (Bootstrap == NULL || Token == NULL) {
        return BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (Bootstrap->State != BC250_CP_BOOTSTRAP_INVENTORY_OPEN) {
        return BC250_CP_BOOTSTRAP_WRONG_STATE;
    }
    status = Bc250VramReserveFixed(
        &Bootstrap->Allocator, Offset, Length, Alignment, Role, Flags, Token);
    if (VramStatus != NULL) *VramStatus = status;
    return status == BC250_VRAM_OK
        ? BC250_CP_BOOTSTRAP_OK : BC250_CP_BOOTSTRAP_INVENTORY_REJECTED;
}

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapSealInventory(
    BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    BC250_VRAM_STATUS *VramStatus)
{
    BC250_VRAM_STATUS status;

    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (Bootstrap == NULL || AddressSpace == NULL) {
        return BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (Bootstrap->State != BC250_CP_BOOTSTRAP_INVENTORY_OPEN) {
        return BC250_CP_BOOTSTRAP_WRONG_STATE;
    }
    status = Bc250VramValidate(&Bootstrap->Allocator);
    if (VramStatus != NULL) *VramStatus = status;
    if (status != BC250_VRAM_OK) return BC250_CP_BOOTSTRAP_INVENTORY_REJECTED;
    if (!Bc250SnapshotValid(Bootstrap, AddressSpace)) {
        return BC250_CP_BOOTSTRAP_SNAPSHOT_REJECTED;
    }
    Bootstrap->AddressSpace = *AddressSpace;
    Bootstrap->SealedRegionCount = Bootstrap->Allocator.RegionCount;
    Bootstrap->SealedNextGeneration = Bootstrap->Allocator.NextGeneration;
    Bootstrap->InventorySeal = Bc250InventorySeal(&Bootstrap->Allocator);
    Bootstrap->SnapshotSeal = Bc250SnapshotSeal(&Bootstrap->AddressSpace);
    Bootstrap->State = BC250_CP_BOOTSTRAP_INVENTORY_SEALED;
    return BC250_CP_BOOTSTRAP_OK;
}

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapBuildPreflight(
    BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_CP_BOOTSTRAP_POLICY *Policy,
    const BC250_CP_FIRMWARE_ENTRY *Manifest,
    uint32_t ManifestCount,
    BC250_CP_BOOTSTRAP_REPORT *Report,
    BC250_VRAM_STATUS *VramStatus,
    BC250_ADDRESS_STATUS *AddressStatus,
    BC250_PSP_CONTRACT_STATUS *PspStatus)
{
    BC250_VRAM_ALLOCATOR candidate;
    BC250_CP_BOOTSTRAP_REPORT candidateReport;
    BC250_VRAM_STATUS vramStatus;
    BC250_PSP_CONTRACT_STATUS pspStatus;
    uint64_t largestPayload;
    uint32_t i;

    if (Report != NULL) memset(Report, 0, sizeof(*Report));
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (AddressStatus != NULL) *AddressStatus = BC250_ADDRESS_BAD_ARGUMENT;
    if (PspStatus != NULL) *PspStatus = BC250_PSP_CONTRACT_BAD_ARGUMENT;
    if (Bootstrap == NULL || Policy == NULL || Report == NULL) {
        return BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (Bootstrap->State != BC250_CP_BOOTSTRAP_INVENTORY_SEALED) {
        return BC250_CP_BOOTSTRAP_WRONG_STATE;
    }
    if (Bootstrap->Allocator.RegionCount != Bootstrap->SealedRegionCount ||
        Bootstrap->Allocator.NextGeneration != Bootstrap->SealedNextGeneration ||
        Bc250VramValidate(&Bootstrap->Allocator) != BC250_VRAM_OK ||
        Bc250InventorySeal(&Bootstrap->Allocator) != Bootstrap->InventorySeal) {
        return BC250_CP_BOOTSTRAP_INVENTORY_CHANGED;
    }
    if (Bc250SnapshotSeal(&Bootstrap->AddressSpace) != Bootstrap->SnapshotSeal) {
        return BC250_CP_BOOTSTRAP_SNAPSHOT_CHANGED;
    }
    if (Policy->TmrBytes == 0 || Policy->TmrBytes > BC250_UINT32_MAX_VALUE ||
        !Bc250PowerOfTwo(Policy->TmrAlignment) ||
        !Bc250PowerOfTwo(Policy->FirmwareAlignment) ||
        !Bc250ManifestValid(Manifest, ManifestCount, &largestPayload)) {
        return BC250_CP_BOOTSTRAP_MANIFEST_REJECTED;
    }

    candidate = Bootstrap->Allocator;
    memset(&candidateReport, 0, sizeof(candidateReport));
    vramStatus = Bc250VramAllocate(
        &candidate, Policy->TmrBytes, Policy->TmrAlignment,
        BC250_VRAM_ROLE_TMR,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_REGION_PERSISTENT,
        &candidateReport.TmrToken);
    if (VramStatus != NULL) *VramStatus = vramStatus;
    if (vramStatus != BC250_VRAM_OK) {
        return BC250_CP_BOOTSTRAP_ALLOCATION_REJECTED;
    }
    vramStatus = Bc250VramAllocate(
        &candidate, largestPayload, Policy->FirmwareAlignment,
        BC250_VRAM_ROLE_FIRMWARE,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE |
            BC250_VRAM_REGION_PERSISTENT,
        &candidateReport.FirmwareStagingToken);
    if (VramStatus != NULL) *VramStatus = vramStatus;
    if (vramStatus != BC250_VRAM_OK) {
        return BC250_CP_BOOTSTRAP_ALLOCATION_REJECTED;
    }

    pspStatus = Bc250PspBuildSetupTmrPayload(
        &candidate, candidateReport.TmrToken, &Bootstrap->AddressSpace,
        Policy->TmrBytes, Policy->TmrAlignment,
        &candidateReport.TmrPlan, &candidateReport.TmrPayload, AddressStatus);
    if (PspStatus != NULL) *PspStatus = pspStatus;
    if (pspStatus != BC250_PSP_CONTRACT_OK) {
        return BC250_CP_BOOTSTRAP_PAYLOAD_REJECTED;
    }
    for (i = 0; i < ManifestCount; ++i) {
        pspStatus = Bc250PspBuildLoadFwPayload(
            &candidate, candidateReport.FirmwareStagingToken,
            &Bootstrap->AddressSpace,
            (BC250_PSP_FW_TYPE)Manifest[i].FirmwareType,
            Manifest[i].PayloadBytes,
            &candidateReport.FirmwarePlans[i],
            &candidateReport.FirmwarePayloads[i], AddressStatus);
        if (PspStatus != NULL) *PspStatus = pspStatus;
        if (pspStatus != BC250_PSP_CONTRACT_OK) {
            return BC250_CP_BOOTSTRAP_PAYLOAD_REJECTED;
        }
    }

    candidateReport.SnapshotGeneration =
        Bootstrap->AddressSpace.SnapshotGeneration;
    candidateReport.FirmwareCount = ManifestCount;
    Bootstrap->Allocator = candidate;
    Bootstrap->PreflightSeal = Bc250InventorySeal(&Bootstrap->Allocator);
    Bootstrap->State = BC250_CP_BOOTSTRAP_PREFLIGHT_READY;
    *Report = candidateReport;
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_OK;
    if (AddressStatus != NULL) *AddressStatus = BC250_ADDRESS_OK;
    if (PspStatus != NULL) *PspStatus = BC250_PSP_CONTRACT_OK;
    return BC250_CP_BOOTSTRAP_OK;
}

BC250_CP_BOOTSTRAP_STATUS Bc250CpBootstrapValidatePreflight(
    const BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_CP_BOOTSTRAP_REPORT *Report,
    BC250_VRAM_STATUS *VramStatus)
{
    BC250_VRAM_REGION tmrRegion;
    BC250_VRAM_REGION stagingRegion;
    BC250_VRAM_STATUS status;
    uint32_t i;

    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (Bootstrap == NULL || Report == NULL) {
        return BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (Bootstrap->State != BC250_CP_BOOTSTRAP_PREFLIGHT_READY) {
        return BC250_CP_BOOTSTRAP_WRONG_STATE;
    }
    status = Bc250VramValidate(&Bootstrap->Allocator);
    if (VramStatus != NULL) *VramStatus = status;
    if (status != BC250_VRAM_OK || Bootstrap->PreflightSeal == 0 ||
        Bc250InventorySeal(&Bootstrap->Allocator) != Bootstrap->PreflightSeal) {
        return BC250_CP_BOOTSTRAP_INVENTORY_CHANGED;
    }
    if (Bc250SnapshotSeal(&Bootstrap->AddressSpace) != Bootstrap->SnapshotSeal ||
        Report->SnapshotGeneration != Bootstrap->AddressSpace.SnapshotGeneration) {
        return BC250_CP_BOOTSTRAP_SNAPSHOT_CHANGED;
    }
    if (Report->FirmwareCount != BC250_CP_BOOTSTRAP_FIRMWARE_COUNT) {
        return BC250_CP_BOOTSTRAP_PAYLOAD_REJECTED;
    }
    status = Bc250VramGetRegion(
        &Bootstrap->Allocator, Report->TmrToken, &tmrRegion);
    if (status != BC250_VRAM_OK) {
        if (VramStatus != NULL) *VramStatus = status;
        return BC250_CP_BOOTSTRAP_INVENTORY_CHANGED;
    }
    status = Bc250VramGetRegion(
        &Bootstrap->Allocator, Report->FirmwareStagingToken, &stagingRegion);
    if (status != BC250_VRAM_OK) {
        if (VramStatus != NULL) *VramStatus = status;
        return BC250_CP_BOOTSTRAP_INVENTORY_CHANGED;
    }
    if (tmrRegion.Role != BC250_VRAM_ROLE_TMR ||
        stagingRegion.Role != BC250_VRAM_ROLE_FIRMWARE ||
        (tmrRegion.Flags & BC250_VRAM_REGION_PERSISTENT) == 0 ||
        (stagingRegion.Flags & (BC250_VRAM_ALLOC_CPU_VISIBLE |
                                BC250_VRAM_REGION_PERSISTENT)) !=
            (BC250_VRAM_ALLOC_CPU_VISIBLE | BC250_VRAM_REGION_PERSISTENT) ||
        !Bc250TokenEqual(Report->TmrPlan.Binding.Token, Report->TmrToken) ||
        Report->TmrPlan.Binding.Offset != tmrRegion.Offset ||
        Report->TmrPlan.Binding.Length != tmrRegion.Length) {
        return BC250_CP_BOOTSTRAP_PAYLOAD_REJECTED;
    }
    for (i = 0; i < Report->FirmwareCount; ++i) {
        const BC250_PSP_LOAD_FW_PLAN *plan = &Report->FirmwarePlans[i];
        if (!Bc250TokenEqual(plan->Binding.Token,
                             Report->FirmwareStagingToken) ||
            plan->Binding.Offset != stagingRegion.Offset ||
            plan->Binding.Length != stagingRegion.Length ||
            plan->Binding.SnapshotGeneration != Report->SnapshotGeneration ||
            plan->PayloadBytes == 0 || plan->PayloadBytes > stagingRegion.Length ||
            Report->FirmwarePayloads[i].Dwords[0] !=
                (uint32_t)plan->Binding.McAddress ||
            Report->FirmwarePayloads[i].Dwords[1] !=
                (uint32_t)(plan->Binding.McAddress >> 32) ||
            Report->FirmwarePayloads[i].Dwords[2] != plan->PayloadBytes ||
            Report->FirmwarePayloads[i].Dwords[3] != plan->FirmwareType) {
            return BC250_CP_BOOTSTRAP_PAYLOAD_REJECTED;
        }
    }
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_OK;
    return BC250_CP_BOOTSTRAP_OK;
}

const char *Bc250CpBootstrapStatusName(BC250_CP_BOOTSTRAP_STATUS Status)
{
    switch (Status) {
    case BC250_CP_BOOTSTRAP_OK: return "ok";
    case BC250_CP_BOOTSTRAP_BAD_ARGUMENT: return "bad_argument";
    case BC250_CP_BOOTSTRAP_WRONG_STATE: return "wrong_state";
    case BC250_CP_BOOTSTRAP_INVENTORY_REJECTED: return "inventory_rejected";
    case BC250_CP_BOOTSTRAP_SNAPSHOT_REJECTED: return "snapshot_rejected";
    case BC250_CP_BOOTSTRAP_INVENTORY_CHANGED: return "inventory_changed";
    case BC250_CP_BOOTSTRAP_SNAPSHOT_CHANGED: return "snapshot_changed";
    case BC250_CP_BOOTSTRAP_MANIFEST_REJECTED: return "manifest_rejected";
    case BC250_CP_BOOTSTRAP_ALLOCATION_REJECTED: return "allocation_rejected";
    case BC250_CP_BOOTSTRAP_PAYLOAD_REJECTED: return "payload_rejected";
    default: return "unknown";
    }
}
