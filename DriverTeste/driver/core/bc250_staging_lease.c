#include "bc250_staging_lease.h"

#include <string.h>

static uint64_t Bc250LeaseMix(uint64_t Seal, uint64_t Value)
{
    Seal ^= Value;
    return Seal * 0x100000001B3ull;
}

static uint64_t Bc250LeaseSeal(const BC250_FIRMWARE_STAGING_LEASE *Lease)
{
    uint64_t seal = 0xCBF29CE484222325ull;
    seal = Bc250LeaseMix(seal, Lease->Version);
    seal = Bc250LeaseMix(seal, Lease->EvidenceFlags);
    seal = Bc250LeaseMix(seal, Lease->EvidenceGeneration);
    seal = Bc250LeaseMix(seal, Lease->SnapshotGeneration);
    seal = Bc250LeaseMix(seal, Lease->Token.Slot);
    seal = Bc250LeaseMix(seal, Lease->Token.Generation);
    seal = Bc250LeaseMix(seal, Lease->Offset);
    seal = Bc250LeaseMix(seal, Lease->Length);
    seal = Bc250LeaseMix(seal, Lease->InventorySeal);
    seal = Bc250LeaseMix(seal, Lease->SnapshotSeal);
    return seal;
}

static int Bc250TokenEqual(BC250_VRAM_TOKEN Left, BC250_VRAM_TOKEN Right)
{
    return Left.Slot == Right.Slot && Left.Generation == Right.Generation;
}

BC250_STAGING_LEASE_STATUS Bc250IssueFirmwareStagingLease(
    const BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_CP_BOOTSTRAP_REPORT *Report,
    const BC250_STAGING_LEASE_EVIDENCE *Evidence,
    BC250_FIRMWARE_STAGING_LEASE *Lease,
    BC250_CP_BOOTSTRAP_STATUS *BootstrapStatus,
    BC250_VRAM_STATUS *VramStatus)
{
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;
    BC250_VRAM_REGION region;
    BC250_VRAM_STATUS vramStatus;
    BC250_FIRMWARE_STAGING_LEASE candidate;

    if (Lease != NULL) memset(Lease, 0, sizeof(*Lease));
    if (BootstrapStatus != NULL) {
        *BootstrapStatus = BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (Bootstrap == NULL || Report == NULL || Evidence == NULL ||
        Lease == NULL) return BC250_STAGING_LEASE_BAD_ARGUMENT;
    if (Evidence->EvidenceFlags != BC250_STAGING_EVIDENCE_REQUIRED ||
        Evidence->EvidenceGeneration == 0 ||
        Evidence->EvidenceGeneration != Report->SnapshotGeneration) {
        return BC250_STAGING_LEASE_MISSING_EVIDENCE;
    }
    bootstrapStatus = Bc250CpBootstrapValidatePreflight(
        Bootstrap, Report, &vramStatus);
    if (BootstrapStatus != NULL) *BootstrapStatus = bootstrapStatus;
    if (VramStatus != NULL) *VramStatus = vramStatus;
    if (bootstrapStatus != BC250_CP_BOOTSTRAP_OK) {
        return BC250_STAGING_LEASE_PREFLIGHT_REJECTED;
    }
    vramStatus = Bc250VramGetRegion(
        &Bootstrap->Allocator, Report->FirmwareStagingToken, &region);
    if (VramStatus != NULL) *VramStatus = vramStatus;
    if (vramStatus != BC250_VRAM_OK ||
        region.Role != BC250_VRAM_ROLE_FIRMWARE ||
        (region.Flags & (BC250_VRAM_ALLOC_CPU_VISIBLE |
                         BC250_VRAM_REGION_PERSISTENT)) !=
            (BC250_VRAM_ALLOC_CPU_VISIBLE | BC250_VRAM_REGION_PERSISTENT)) {
        return BC250_STAGING_LEASE_REGION_REJECTED;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.Version = BC250_STAGING_LEASE_VERSION;
    candidate.EvidenceFlags = Evidence->EvidenceFlags;
    candidate.EvidenceGeneration = Evidence->EvidenceGeneration;
    candidate.SnapshotGeneration = Report->SnapshotGeneration;
    candidate.Token = Report->FirmwareStagingToken;
    candidate.Offset = region.Offset;
    candidate.Length = region.Length;
    candidate.InventorySeal = Bootstrap->PreflightSeal;
    candidate.SnapshotSeal = Bootstrap->SnapshotSeal;
    candidate.LeaseSeal = Bc250LeaseSeal(&candidate);
    *Lease = candidate;
    if (BootstrapStatus != NULL) *BootstrapStatus = BC250_CP_BOOTSTRAP_OK;
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_OK;
    return BC250_STAGING_LEASE_OK;
}

BC250_STAGING_LEASE_STATUS Bc250ValidateFirmwareStagingLease(
    const BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_CP_BOOTSTRAP_REPORT *Report,
    const BC250_FIRMWARE_STAGING_LEASE *Lease,
    const BC250_PSP_LOAD_FW_PLAN *Plan,
    BC250_CP_BOOTSTRAP_STATUS *BootstrapStatus,
    BC250_VRAM_STATUS *VramStatus)
{
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;
    uint32_t i;
    int planFound = 0;

    if (BootstrapStatus != NULL) {
        *BootstrapStatus = BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (Bootstrap == NULL || Report == NULL || Lease == NULL || Plan == NULL) {
        return BC250_STAGING_LEASE_BAD_ARGUMENT;
    }
    bootstrapStatus = Bc250CpBootstrapValidatePreflight(
        Bootstrap, Report, VramStatus);
    if (BootstrapStatus != NULL) *BootstrapStatus = bootstrapStatus;
    if (bootstrapStatus != BC250_CP_BOOTSTRAP_OK) {
        return BC250_STAGING_LEASE_PREFLIGHT_REJECTED;
    }
    if (Lease->Version != BC250_STAGING_LEASE_VERSION ||
        Lease->EvidenceFlags != BC250_STAGING_EVIDENCE_REQUIRED ||
        Lease->EvidenceGeneration != Lease->SnapshotGeneration ||
        Lease->SnapshotGeneration != Report->SnapshotGeneration ||
        Lease->InventorySeal != Bootstrap->PreflightSeal ||
        Lease->SnapshotSeal != Bootstrap->SnapshotSeal) {
        return BC250_STAGING_LEASE_STALE;
    }
    if (Lease->LeaseSeal == 0 || Lease->LeaseSeal != Bc250LeaseSeal(Lease)) {
        return BC250_STAGING_LEASE_TAMPERED;
    }
    if (!Bc250TokenEqual(Lease->Token, Report->FirmwareStagingToken) ||
        !Bc250TokenEqual(Lease->Token, Plan->Binding.Token) ||
        Lease->Offset != Plan->Binding.Offset ||
        Lease->Length != Plan->Binding.Length ||
        Plan->Binding.SnapshotGeneration != Lease->SnapshotGeneration ||
        Plan->PayloadBytes == 0 || Plan->PayloadBytes > Lease->Length) {
        return BC250_STAGING_LEASE_PLAN_MISMATCH;
    }
    for (i = 0; i < Report->FirmwareCount; ++i) {
        const BC250_PSP_LOAD_FW_PLAN *expected = &Report->FirmwarePlans[i];
        if (expected->FirmwareType == Plan->FirmwareType &&
            expected->PayloadBytes == Plan->PayloadBytes &&
            Bc250TokenEqual(expected->Binding.Token, Plan->Binding.Token) &&
            expected->Binding.Offset == Plan->Binding.Offset &&
            expected->Binding.Length == Plan->Binding.Length &&
            expected->Binding.SnapshotGeneration ==
                Plan->Binding.SnapshotGeneration) {
            planFound = 1;
            break;
        }
    }
    if (!planFound) return BC250_STAGING_LEASE_PLAN_MISMATCH;
    if (BootstrapStatus != NULL) *BootstrapStatus = BC250_CP_BOOTSTRAP_OK;
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_OK;
    return BC250_STAGING_LEASE_OK;
}

const char *Bc250StagingLeaseStatusName(BC250_STAGING_LEASE_STATUS Status)
{
    switch (Status) {
    case BC250_STAGING_LEASE_OK: return "ok";
    case BC250_STAGING_LEASE_BAD_ARGUMENT: return "bad_argument";
    case BC250_STAGING_LEASE_MISSING_EVIDENCE: return "missing_evidence";
    case BC250_STAGING_LEASE_PREFLIGHT_REJECTED:
        return "preflight_rejected";
    case BC250_STAGING_LEASE_REGION_REJECTED: return "region_rejected";
    case BC250_STAGING_LEASE_STALE: return "stale";
    case BC250_STAGING_LEASE_TAMPERED: return "tampered";
    case BC250_STAGING_LEASE_PLAN_MISMATCH: return "plan_mismatch";
    default: return "unknown";
    }
}
