#include "bc250_runtime_inventory.h"

#include <string.h>

#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)

static int Bc250RangesOverlap(
    uint64_t A, uint64_t ALength,
    uint64_t B, uint64_t BLength)
{
    uint64_t aEnd, bEnd;
    if (ALength == 0 || BLength == 0) return 0;
    if (A > BC250_UINT64_MAX_VALUE - ALength ||
        B > BC250_UINT64_MAX_VALUE - BLength) return 1;
    aEnd = A + ALength;
    bEnd = B + BLength;
    return A < bEnd && B < aEnd;
}

BC250_RUNTIME_INVENTORY_STATUS Bc250BuildRuntimeVramInventory(
    const BC250_RUNTIME_INVENTORY_INPUT *Input,
    uint64_t VramTotalBytes,
    uint64_t CpuVisibleBytes,
    uint32_t SnapshotGeneration,
    BC250_RUNTIME_INVENTORY_REPORT *Report)
{
    BC250_RUNTIME_INVENTORY_REPORT candidate;
    uint32_t count = 0;
    uint32_t i, j;
    uint64_t totalReserved = 0;
    uint64_t cpuReserved = 0;

    if (Report != NULL) memset(Report, 0, sizeof(*Report));
    if (Input == NULL || Report == NULL || VramTotalBytes == 0 ||
        CpuVisibleBytes == 0 || CpuVisibleBytes > VramTotalBytes ||
        SnapshotGeneration == 0) {
        return BC250_RUNTIME_INVENTORY_BAD_ARGUMENT;
    }
    if (Input->PlatformCarveoutCount > BC250_VRAM_MAX_REGIONS) {
        return BC250_RUNTIME_INVENTORY_BAD_ARGUMENT;
    }

    memset(&candidate, 0, sizeof(candidate));

    if (Input->HasScanout != 0) {
        const BC250_INHERITED_VRAM_RESERVATION *scanout =
            &Input->ScanoutReservation;
        if (scanout->Length == 0 ||
            scanout->Role != BC250_VRAM_ROLE_SCANOUT ||
            (scanout->Flags & BC250_VRAM_REGION_PERSISTENT) == 0 ||
            scanout->Offset > BC250_UINT64_MAX_VALUE - scanout->Length ||
            scanout->Offset + scanout->Length > CpuVisibleBytes) {
            return BC250_RUNTIME_INVENTORY_SCANOUT_INVALID;
        }
        if (count >= BC250_VRAM_MAX_REGIONS) {
            return BC250_RUNTIME_INVENTORY_TABLE_FULL;
        }
        candidate.Reservations[count] = *scanout;
        candidate.Reservations[count].Flags |= BC250_VRAM_ALLOC_CPU_VISIBLE;
        ++count;
    }

    if (Input->HasDiscoveryTmr != 0) {
        const BC250_DISCOVERY_TMR_EVIDENCE *discovery =
            &Input->DiscoveryTmrEvidence;
        BC250_INHERITED_VRAM_RESERVATION *res;
        if (discovery->Length == 0 ||
            discovery->Offset > BC250_UINT64_MAX_VALUE - discovery->Length ||
            discovery->Offset + discovery->Length > VramTotalBytes) {
            return BC250_RUNTIME_INVENTORY_DISCOVERY_TMR_INVALID;
        }
        if (count >= BC250_VRAM_MAX_REGIONS) {
            return BC250_RUNTIME_INVENTORY_TABLE_FULL;
        }
        res = &candidate.Reservations[count];
        res->Offset = discovery->Offset;
        res->Length = discovery->Length;
        res->Alignment = 0x1000u;
        res->Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
        res->Flags = BC250_VRAM_REGION_PERSISTENT;
        if (res->Offset + res->Length <= CpuVisibleBytes) {
            res->Flags |= BC250_VRAM_ALLOC_CPU_VISIBLE;
        }
        ++count;
    }

    for (i = 0; i < Input->PlatformCarveoutCount; ++i) {
        const BC250_INHERITED_VRAM_RESERVATION *carveout =
            &Input->PlatformCarveouts[i];
        BC250_INHERITED_VRAM_RESERVATION *res;
        if (carveout->Length == 0 ||
            carveout->Role != BC250_VRAM_ROLE_PLATFORM_RESERVED ||
            (carveout->Flags & BC250_VRAM_REGION_PERSISTENT) == 0 ||
            carveout->Offset > BC250_UINT64_MAX_VALUE - carveout->Length ||
            carveout->Offset + carveout->Length > VramTotalBytes) {
            return BC250_RUNTIME_INVENTORY_CARVEOUT_INVALID;
        }
        if (count >= BC250_VRAM_MAX_REGIONS) {
            return BC250_RUNTIME_INVENTORY_TABLE_FULL;
        }
        res = &candidate.Reservations[count];
        *res = *carveout;
        if (res->Offset + res->Length <= CpuVisibleBytes) {
            res->Flags |= BC250_VRAM_ALLOC_CPU_VISIBLE;
        }
        ++count;
    }

    for (i = 0; i < count; ++i) {
        for (j = i + 1; j < count; ++j) {
            if (Bc250RangesOverlap(
                    candidate.Reservations[i].Offset,
                    candidate.Reservations[i].Length,
                    candidate.Reservations[j].Offset,
                    candidate.Reservations[j].Length)) {
                return BC250_RUNTIME_INVENTORY_OVERLAP;
            }
        }
    }

    for (i = 0; i < count; ++i) {
        const BC250_INHERITED_VRAM_RESERVATION *res = &candidate.Reservations[i];
        totalReserved += res->Length;
        if (res->Offset + res->Length <= CpuVisibleBytes) {
            cpuReserved += res->Length;
        } else if (res->Offset < CpuVisibleBytes) {
            cpuReserved += (CpuVisibleBytes - res->Offset);
        }
    }

    if (totalReserved > VramTotalBytes || cpuReserved > CpuVisibleBytes) {
        return BC250_RUNTIME_INVENTORY_OUT_OF_RANGE;
    }

    candidate.SnapshotGeneration = SnapshotGeneration;
    candidate.ReservationCount = count;
    candidate.VramTotalBytes = VramTotalBytes;
    candidate.CpuVisibleBytes = CpuVisibleBytes;
    candidate.TotalReservedBytes = totalReserved;
    candidate.TotalFreeBytes = VramTotalBytes - totalReserved;
    candidate.CpuVisibleReservedBytes = cpuReserved;
    candidate.CpuVisibleFreeBytes = CpuVisibleBytes - cpuReserved;
    candidate.OwnershipEvidenceFlags =
        BC250_OWNERSHIP_EXCLUSIVE_ADAPTER |
        BC250_OWNERSHIP_INVENTORY_COMPLETE;
    candidate.StagingEvidenceFlags = BC250_STAGING_EVIDENCE_REQUIRED;

    *Report = candidate;
    return BC250_RUNTIME_INVENTORY_OK;
}

const char *Bc250RuntimeInventoryStatusName(
    BC250_RUNTIME_INVENTORY_STATUS Status)
{
    switch (Status) {
    case BC250_RUNTIME_INVENTORY_OK: return "ok";
    case BC250_RUNTIME_INVENTORY_BAD_ARGUMENT: return "bad_argument";
    case BC250_RUNTIME_INVENTORY_SCANOUT_INVALID:
        return "scanout_invalid";
    case BC250_RUNTIME_INVENTORY_DISCOVERY_TMR_INVALID:
        return "discovery_tmr_invalid";
    case BC250_RUNTIME_INVENTORY_CARVEOUT_INVALID:
        return "carveout_invalid";
    case BC250_RUNTIME_INVENTORY_OVERLAP: return "overlap";
    case BC250_RUNTIME_INVENTORY_OUT_OF_RANGE: return "out_of_range";
    case BC250_RUNTIME_INVENTORY_TABLE_FULL: return "table_full";
    default: return "unknown";
    }
}
