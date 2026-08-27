#include "bc250_owned_adapter_contract.h"

#include <string.h>

#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)

static const BC250_RESOLVED_BAR_RESOURCE *Bc250FindAperture(
    const BC250_OWNED_ADAPTER_INPUT *Input)
{
    const BC250_RESOLVED_BAR_RESOURCE *found = NULL;
    size_t i;

    for (i = 0; i < Input->ResolvedBarCount; ++i) {
        const BC250_RESOLVED_BAR_RESOURCE *bar = &Input->ResolvedBars[i];
        if (bar->Index != Input->VramApertureBarIndex) continue;
        if (found != NULL) return NULL;
        found = bar;
    }
    return found;
}

static int Bc250TopologyValid(
    const BC250_OWNED_ADAPTER_INPUT *Input,
    const BC250_RESOLVED_BAR_RESOURCE *Aperture)
{
    uint64_t expectedEnd;

    if (Aperture->Length == 0 ||
        Aperture->Start > BC250_UINT64_MAX_VALUE - (Aperture->Length - 1u) ||
        Aperture->Length > Input->VramTotalBytes ||
        Input->VramTotalBytes == 0 || Input->SnapshotGeneration == 0 ||
        Input->VramMcStart >
            BC250_UINT64_MAX_VALUE - (Input->VramTotalBytes - 1u) ||
        Input->VramBaseOffset >
            BC250_UINT64_MAX_VALUE - (Input->VramTotalBytes - 1u)) {
        return 0;
    }
    expectedEnd = Input->VramMcStart + Input->VramTotalBytes - 1u;
    return Input->VramMcEnd == expectedEnd;
}

BC250_OWNED_ADAPTER_STATUS Bc250BuildOwnedAdapterBootstrap(
    const BC250_OWNED_ADAPTER_INPUT *Input,
    const BC250_INHERITED_VRAM_RESERVATION *Reservations,
    size_t ReservationCount,
    BC250_OWNED_ADAPTER_BOOTSTRAP *Owned,
    BC250_CP_BOOTSTRAP_STATUS *BootstrapStatus,
    BC250_VRAM_STATUS *VramStatus)
{
    const BC250_RESOLVED_BAR_RESOURCE *aperture;
    BC250_OWNED_ADAPTER_BOOTSTRAP candidate;
    BC250_VRAM_ADDRESS_SPACE space;
    BC250_VRAM_TOKEN token;
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;
    size_t i;

    if (Owned != NULL) memset(Owned, 0, sizeof(*Owned));
    if (BootstrapStatus != NULL) {
        *BootstrapStatus = BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (Input == NULL || Owned == NULL || Input->ResolvedBars == NULL ||
        Input->ResolvedBarCount == 0 ||
        Input->ResolvedBarCount > BC250_RESOURCE_MAX_BARS ||
        Input->VramApertureBarIndex >= BC250_RESOURCE_MAX_BARS ||
        (ReservationCount != 0 && Reservations == NULL) ||
        ReservationCount > BC250_VRAM_MAX_REGIONS) {
        return BC250_OWNED_ADAPTER_BAD_ARGUMENT;
    }
    if (Input->OwnershipEvidenceFlags != BC250_OWNERSHIP_REQUIRED) {
        return BC250_OWNED_ADAPTER_MISSING_EVIDENCE;
    }

    aperture = Bc250FindAperture(Input);
    if (aperture == NULL) {
        return BC250_OWNED_ADAPTER_APERTURE_NOT_UNIQUE;
    }
    if (!Bc250TopologyValid(Input, aperture)) {
        return BC250_OWNED_ADAPTER_TOPOLOGY_REJECTED;
    }

    memset(&candidate, 0, sizeof(candidate));
    bootstrapStatus = Bc250CpBootstrapInit(
        &candidate.Bootstrap, Input->VramTotalBytes, aperture->Length,
        VramStatus);
    if (BootstrapStatus != NULL) *BootstrapStatus = bootstrapStatus;
    if (bootstrapStatus != BC250_CP_BOOTSTRAP_OK) {
        return BC250_OWNED_ADAPTER_BOOTSTRAP_REJECTED;
    }
    for (i = 0; i < ReservationCount; ++i) {
        const BC250_INHERITED_VRAM_RESERVATION *reservation = &Reservations[i];
        if ((reservation->Flags & BC250_VRAM_REGION_PERSISTENT) == 0) {
            return BC250_OWNED_ADAPTER_RESERVATION_REJECTED;
        }
        bootstrapStatus = Bc250CpBootstrapReserveInherited(
            &candidate.Bootstrap, reservation->Offset, reservation->Length,
            reservation->Alignment, (BC250_VRAM_ROLE)reservation->Role,
            reservation->Flags, &token, VramStatus);
        if (BootstrapStatus != NULL) *BootstrapStatus = bootstrapStatus;
        if (bootstrapStatus != BC250_CP_BOOTSTRAP_OK) {
            return BC250_OWNED_ADAPTER_RESERVATION_REJECTED;
        }
    }

    memset(&space, 0, sizeof(space));
    space.TotalBytes = Input->VramTotalBytes;
    space.CpuVisibleBytes = aperture->Length;
    space.VramMcStart = Input->VramMcStart;
    space.VramMcEnd = Input->VramMcEnd;
    space.VramBaseOffset = Input->VramBaseOffset;
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;
    space.SnapshotGeneration = Input->SnapshotGeneration;
    bootstrapStatus = Bc250CpBootstrapSealInventory(
        &candidate.Bootstrap, &space, VramStatus);
    if (BootstrapStatus != NULL) *BootstrapStatus = bootstrapStatus;
    if (bootstrapStatus != BC250_CP_BOOTSTRAP_OK) {
        return BC250_OWNED_ADAPTER_BOOTSTRAP_REJECTED;
    }

    candidate.CpuAperturePhysicalStart = aperture->Start;
    candidate.CpuApertureBytes = aperture->Length;
    candidate.OwnershipEvidenceFlags = Input->OwnershipEvidenceFlags;
    candidate.InheritedReservationCount = (uint32_t)ReservationCount;
    *Owned = candidate;
    if (BootstrapStatus != NULL) *BootstrapStatus = BC250_CP_BOOTSTRAP_OK;
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_OK;
    return BC250_OWNED_ADAPTER_OK;
}

const char *Bc250OwnedAdapterStatusName(BC250_OWNED_ADAPTER_STATUS Status)
{
    switch (Status) {
    case BC250_OWNED_ADAPTER_OK: return "ok";
    case BC250_OWNED_ADAPTER_BAD_ARGUMENT: return "bad_argument";
    case BC250_OWNED_ADAPTER_MISSING_EVIDENCE: return "missing_evidence";
    case BC250_OWNED_ADAPTER_APERTURE_NOT_UNIQUE:
        return "aperture_not_unique";
    case BC250_OWNED_ADAPTER_TOPOLOGY_REJECTED: return "topology_rejected";
    case BC250_OWNED_ADAPTER_RESERVATION_REJECTED:
        return "reservation_rejected";
    case BC250_OWNED_ADAPTER_BOOTSTRAP_REJECTED:
        return "bootstrap_rejected";
    default: return "unknown";
    }
}
