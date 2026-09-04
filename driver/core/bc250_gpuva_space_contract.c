#include "bc250_gpuva_space_contract.h"
#include <string.h>

static BC250_GPUVA_STATUS Bc250GpuvaValidateSpace(const BC250_GPUVA_SPACE *Space)
{
    if (Space == NULL) return BC250_GPUVA_BAD_ARGUMENT;
    if (Space->Signature != BC250_GPUVA_SPACE_SIGNATURE) {
        return BC250_GPUVA_BAD_STATE;
    }
    return BC250_GPUVA_OK;
}

static void Bc250GpuvaCopySpace(
    BC250_GPUVA_SPACE *Destination,
    const BC250_GPUVA_SPACE *Source)
{
    volatile uint8_t *destination = (volatile uint8_t *)Destination;
    const volatile uint8_t *source = (const volatile uint8_t *)Source;
    size_t index;
    for (index = 0u; index < sizeof(*Destination); ++index) {
        destination[index] = source[index];
    }
}

BC250_GPUVA_STATUS Bc250GpuvaSpaceInitialize(
    BC250_GPUVA_SPACE *Space,
    uint32_t AddressBits)
{
    if (Space == NULL) return BC250_GPUVA_BAD_ARGUMENT;
    if (AddressBits == 0u || AddressBits > BC250_GPUVA_DEFAULT_BITS) {
        AddressBits = BC250_GPUVA_DEFAULT_BITS;
    }
    memset(Space, 0, sizeof(*Space));
    Space->Signature = BC250_GPUVA_SPACE_SIGNATURE;
    Space->AddressBits = AddressBits;
    Space->MaxAddress = (AddressBits >= 64u) ? (uint64_t)~0ull : (1ull << AddressBits);
    Space->PageTableEpoch = 1u;
    return BC250_GPUVA_OK;
}

BC250_GPUVA_STATUS Bc250GpuvaSpaceInsertRange(
    BC250_GPUVA_SPACE *Space,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint64_t *RangeId)
{
    BC250_GPUVA_STATUS status;
    uint32_t i;
    int freeSlot = -1;
    uint64_t endGpuVa;

    if (RangeId != NULL) *RangeId = 0ull;
    status = Bc250GpuvaValidateSpace(Space);
    if (status != BC250_GPUVA_OK) return status;
    if (Param == NULL || RangeId == NULL) return BC250_GPUVA_BAD_ARGUMENT;
    if (Space->Teardown != 0u) return BC250_GPUVA_IN_TEARDOWN;

    if (Param->BaseGpuVa == 0u || Param->Size == 0u || Param->OwnerProcess == NULL) {
        return BC250_GPUVA_BAD_ARGUMENT;
    }

    if ((Param->BaseGpuVa & BC250_GPUVA_PAGE_MASK) != 0u ||
        (Param->Size & BC250_GPUVA_PAGE_MASK) != 0u) {
        return BC250_GPUVA_MISALIGNED;
    }

    endGpuVa = Param->BaseGpuVa + Param->Size;
    if (endGpuVa < Param->BaseGpuVa) {
        return BC250_GPUVA_RANGE_OVERFLOW;
    }
    if (endGpuVa > Space->MaxAddress) {
        return BC250_GPUVA_OUT_OF_BOUNDS;
    }

    /* Check for invalid overlaps with existing active ranges for the same process */
    for (i = 0u; i < BC250_GPUVA_SPACE_MAX_RANGES; ++i) {
        const BC250_GPUVA_RANGE_RECORD *existing = &Space->Ranges[i];
        if (existing->State == BC250_GPUVA_RANGE_MAPPED &&
            existing->OwnerProcess == Param->OwnerProcess) {
            /* Check if intervals intersect: max(start1, start2) < min(end1, end2) */
            uint64_t maxStart = (Param->BaseGpuVa > existing->BaseGpuVa) ?
                Param->BaseGpuVa : existing->BaseGpuVa;
            uint64_t minEnd = (endGpuVa < existing->EndGpuVa) ?
                endGpuVa : existing->EndGpuVa;
            if (maxStart < minEnd) {
                return BC250_GPUVA_INVALID_OVERLAP;
            }
        }
        if (freeSlot < 0 && (existing->State == BC250_GPUVA_RANGE_FREE ||
                             existing->State == BC250_GPUVA_RANGE_UNMAPPED)) {
            freeSlot = (int)i;
        }
    }

    if (freeSlot < 0) {
        return BC250_GPUVA_NO_SPACE;
    }

    {
        BC250_GPUVA_RANGE_RECORD *slot = &Space->Ranges[freeSlot];
        uint32_t insertPos;

        memset(slot, 0, sizeof(*slot));
        slot->State = BC250_GPUVA_RANGE_MAPPED;
        slot->Generation = ++Space->NextGeneration;
        slot->RangeId = ++Space->NextRangeId;
        slot->BaseGpuVa = Param->BaseGpuVa;
        slot->Size = Param->Size;
        slot->EndGpuVa = endGpuVa;
        slot->OwnerProcess = Param->OwnerProcess;
        slot->OwnerContext = Param->OwnerContext;
        slot->Allocation = Param->Allocation;
        slot->OpenAllocation = Param->OpenAllocation;
        slot->BackingSegmentId = Param->BackingSegmentId;
        slot->BackingSegmentOffset = Param->BackingSegmentOffset;
        slot->Permissions = Param->Permissions ? Param->Permissions :
            (BC250_PTE_READABLE | BC250_PTE_WRITEABLE);
        slot->IsResident = 1u;
        slot->PageTableEpoch = Space->PageTableEpoch;
        slot->TeardownState = 0u;

        /* Inserção ordenada em SortedIndices por BaseGpuVa */
        insertPos = Space->SortedCount;
        for (i = 0u; i < Space->SortedCount; ++i) {
            uint32_t idx = Space->SortedIndices[i];
            if (Space->Ranges[idx].BaseGpuVa > slot->BaseGpuVa) {
                insertPos = i;
                break;
            }
        }
        for (i = Space->SortedCount; i > insertPos; --i) {
            Space->SortedIndices[i] = Space->SortedIndices[i - 1u];
        }
        Space->SortedIndices[insertPos] = (uint32_t)freeSlot;
        ++Space->SortedCount;

        ++Space->LiveCount;
        *RangeId = slot->RangeId;
    }

    return BC250_GPUVA_OK;
}

BC250_GPUVA_STATUS Bc250GpuvaSpaceRemoveRange(
    BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    uint64_t BaseGpuVa,
    uint64_t Size)
{
    BC250_GPUVA_STATUS status;
    uint32_t i;

    status = Bc250GpuvaValidateSpace(Space);
    if (status != BC250_GPUVA_OK) return status;
    if (OwnerProcess == NULL || BaseGpuVa == 0u || Size == 0u) {
        return BC250_GPUVA_BAD_ARGUMENT;
    }

    for (i = 0u; i < BC250_GPUVA_SPACE_MAX_RANGES; ++i) {
        BC250_GPUVA_RANGE_RECORD *record = &Space->Ranges[i];
        if (record->State == BC250_GPUVA_RANGE_MAPPED &&
            record->OwnerProcess == OwnerProcess &&
            record->BaseGpuVa == BaseGpuVa &&
            record->Size == Size) {
            record->State = BC250_GPUVA_RANGE_UNMAPPED;
            record->IsResident = 0u;
            if (Space->LiveCount > 0u) --Space->LiveCount;
            ++Space->PageTableEpoch;

            /* Remover de SortedIndices */
            {
                uint32_t s;
                for (s = 0u; s < Space->SortedCount; ++s) {
                    if (Space->SortedIndices[s] == (uint32_t)i) {
                        uint32_t j;
                        for (j = s; j + 1u < Space->SortedCount; ++j) {
                            Space->SortedIndices[j] = Space->SortedIndices[j + 1u];
                        }
                        --Space->SortedCount;
                        break;
                    }
                }
            }

            return BC250_GPUVA_OK;
        }
    }

    return BC250_GPUVA_NOT_FOUND;
}

BC250_GPUVA_STATUS Bc250GpuvaSpaceResolveAddress(
    const BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    uint64_t GpuVa,
    uint32_t RequiredPermissions,
    uint32_t *BackingSegmentId,
    uint64_t *BackingSegmentOffset)
{
    BC250_GPUVA_STATUS status;
    uint32_t i;
    uint32_t addressOwnedByAnotherProcess = 0u;

    if (BackingSegmentId != NULL) *BackingSegmentId = 0u;
    if (BackingSegmentOffset != NULL) *BackingSegmentOffset = 0ull;
    status = Bc250GpuvaValidateSpace(Space);
    if (status != BC250_GPUVA_OK) return status;
    if (OwnerProcess == NULL || GpuVa == 0u || BackingSegmentId == NULL ||
        BackingSegmentOffset == NULL) {
        return BC250_GPUVA_BAD_ARGUMENT;
    }
    if (Space->Teardown != 0u) return BC250_GPUVA_IN_TEARDOWN;
    if (GpuVa >= Space->MaxAddress) return BC250_GPUVA_OUT_OF_BOUNDS;

    for (i = 0u; i < Space->SortedCount; ++i) {
        const BC250_GPUVA_RANGE_RECORD *record =
            &Space->Ranges[Space->SortedIndices[i]];
        uint64_t withinRange;

        if (record->BaseGpuVa > GpuVa) break;
        if (GpuVa >= record->EndGpuVa) continue;
        if (record->OwnerProcess != OwnerProcess) {
            addressOwnedByAnotherProcess = 1u;
            continue;
        }
        if (record->State == BC250_GPUVA_RANGE_TEARDOWN ||
            record->TeardownState != 0u) {
            return BC250_GPUVA_IN_TEARDOWN;
        }
        if (record->State != BC250_GPUVA_RANGE_MAPPED ||
            record->IsResident == 0u) {
            return BC250_GPUVA_NOT_RESIDENT;
        }
        if ((record->Permissions & RequiredPermissions) != RequiredPermissions) {
            return BC250_GPUVA_PERMISSION_DENIED;
        }
        withinRange = GpuVa - record->BaseGpuVa;
        if (record->BackingSegmentOffset > (uint64_t)~0ull - withinRange) {
            return BC250_GPUVA_RANGE_OVERFLOW;
        }
        *BackingSegmentId = record->BackingSegmentId;
        *BackingSegmentOffset = record->BackingSegmentOffset + withinRange;
        return BC250_GPUVA_OK;
    }

    return addressOwnedByAnotherProcess != 0u ?
        BC250_GPUVA_OWNER_MISMATCH : BC250_GPUVA_NOT_FOUND;
}

BC250_GPUVA_STATUS Bc250GpuvaSpaceReplaceRangeTransactional(
    BC250_GPUVA_SPACE *Space,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint32_t MapRange)
{
    BC250_GPUVA_STATUS status;
    uint64_t replaceEnd;
    uint32_t i;

    status = Bc250GpuvaValidateSpace(Space);
    if (status != BC250_GPUVA_OK) return status;
    if (Scratch == NULL || Scratch == Space || Param == NULL ||
        Param->OwnerProcess == NULL || Param->BaseGpuVa == 0u ||
        Param->Size == 0u || MapRange > 1u) {
        return BC250_GPUVA_BAD_ARGUMENT;
    }
    if ((Param->BaseGpuVa & BC250_GPUVA_PAGE_MASK) != 0u ||
        (Param->Size & BC250_GPUVA_PAGE_MASK) != 0u) {
        return BC250_GPUVA_MISALIGNED;
    }
    replaceEnd = Param->BaseGpuVa + Param->Size;
    if (replaceEnd < Param->BaseGpuVa) return BC250_GPUVA_RANGE_OVERFLOW;
    if (replaceEnd > Space->MaxAddress) return BC250_GPUVA_OUT_OF_BOUNDS;
    if (MapRange != 0u && Param->Permissions == 0u) {
        return BC250_GPUVA_PERMISSION_DENIED;
    }

    status = Bc250GpuvaSpaceInitialize(Scratch, Space->AddressBits);
    if (status != BC250_GPUVA_OK) return status;
    Scratch->NextGeneration = Space->NextGeneration;
    Scratch->NextRangeId = Space->NextRangeId;
    Scratch->PageTableEpoch = Space->PageTableEpoch + 1u;
    if (Scratch->PageTableEpoch == 0u) Scratch->PageTableEpoch = 1u;

    for (i = 0u; i < Space->SortedCount; ++i) {
        const BC250_GPUVA_RANGE_RECORD *source =
            &Space->Ranges[Space->SortedIndices[i]];
        BC250_GPUVA_RANGE_RECORD fragment;
        uint64_t ignoredId;
        int overlaps;

        if (source->State != BC250_GPUVA_RANGE_MAPPED) continue;
        overlaps = source->OwnerProcess == Param->OwnerProcess &&
            source->BaseGpuVa < replaceEnd &&
            Param->BaseGpuVa < source->EndGpuVa;
        if (!overlaps) {
            fragment = *source;
            fragment.PageTableEpoch = Scratch->PageTableEpoch;
            status = Bc250GpuvaSpaceInsertRange(Scratch, &fragment, &ignoredId);
            if (status != BC250_GPUVA_OK) return status;
            continue;
        }

        if (source->BaseGpuVa < Param->BaseGpuVa) {
            fragment = *source;
            fragment.Size = Param->BaseGpuVa - source->BaseGpuVa;
            fragment.EndGpuVa = 0u;
            fragment.PageTableEpoch = Scratch->PageTableEpoch;
            status = Bc250GpuvaSpaceInsertRange(Scratch, &fragment, &ignoredId);
            if (status != BC250_GPUVA_OK) return status;
        }
        if (source->EndGpuVa > replaceEnd) {
            fragment = *source;
            fragment.BaseGpuVa = replaceEnd;
            fragment.Size = source->EndGpuVa - replaceEnd;
            fragment.EndGpuVa = 0u;
            if (fragment.BackingSegmentOffset <=
                (uint64_t)~0ull - (replaceEnd - source->BaseGpuVa)) {
                fragment.BackingSegmentOffset +=
                    replaceEnd - source->BaseGpuVa;
            } else {
                return BC250_GPUVA_RANGE_OVERFLOW;
            }
            fragment.PageTableEpoch = Scratch->PageTableEpoch;
            status = Bc250GpuvaSpaceInsertRange(Scratch, &fragment, &ignoredId);
            if (status != BC250_GPUVA_OK) return status;
        }
    }

    if (MapRange != 0u) {
        BC250_GPUVA_RANGE_RECORD mapped = *Param;
        uint64_t ignoredId;
        mapped.PageTableEpoch = Scratch->PageTableEpoch;
        status = Bc250GpuvaSpaceInsertRange(Scratch, &mapped, &ignoredId);
        if (status != BC250_GPUVA_OK) return status;
    }

    /* Publicação permitida somente no snapshot inativo, conforme contrato. */
    Bc250GpuvaCopySpace(Space, Scratch);
    return BC250_GPUVA_OK;
}

BC250_GPUVA_STATUS Bc250GpuvaSpaceInvalidateForObject(
    BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE Context,
    BC250_MCDM_HANDLE Allocation)
{
    BC250_GPUVA_STATUS status;
    uint32_t i;
    uint32_t invalidated = 0u;

    status = Bc250GpuvaValidateSpace(Space);
    if (status != BC250_GPUVA_OK) return status;

    for (i = 0u; i < BC250_GPUVA_SPACE_MAX_RANGES; ++i) {
        BC250_GPUVA_RANGE_RECORD *record = &Space->Ranges[i];
        if (record->State == BC250_GPUVA_RANGE_MAPPED) {
            int match = 0;
            if (OwnerProcess != NULL && record->OwnerProcess == OwnerProcess) {
                if (Context == NULL && Allocation == NULL) {
                    match = 1;
                } else if (Context != NULL && record->OwnerContext == Context) {
                    match = 1;
                } else if (Allocation != NULL && (record->Allocation == Allocation || record->OpenAllocation == Allocation)) {
                    match = 1;
                }
            } else if (OwnerProcess == NULL) {
                if (Context != NULL && record->OwnerContext == Context) {
                    match = 1;
                } else if (Allocation != NULL && (record->Allocation == Allocation || record->OpenAllocation == Allocation)) {
                    match = 1;
                }
            }
            if (match) {
                record->State = BC250_GPUVA_RANGE_TEARDOWN;
                record->IsResident = 0u;
                record->TeardownState = 1u;
                if (Space->LiveCount > 0u) --Space->LiveCount;
                ++invalidated;
            }
        }
    }

    if (invalidated > 0u) {
        ++Space->PageTableEpoch;
    }
    return BC250_GPUVA_OK;
}

BC250_GPUVA_STATUS Bc250GpuvaSpaceValidateRangeCoverage(
    const BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE Context,
    uint64_t BaseGpuVa,
    uint64_t Size,
    uint32_t RequiredPermissions,
    uint32_t ExpectedPageTableEpoch)
{
    BC250_GPUVA_STATUS status;
    uint64_t cursor;
    uint64_t targetEnd;

    status = Bc250GpuvaValidateSpace(Space);
    if (status != BC250_GPUVA_OK) return status;
    if (Space->Teardown != 0u) return BC250_GPUVA_IN_TEARDOWN;
    if (OwnerProcess == NULL || BaseGpuVa == 0u || Size == 0u) {
        return BC250_GPUVA_BAD_ARGUMENT;
    }

    if ((BaseGpuVa & 3u) != 0u || (Size % sizeof(uint32_t)) != 0u) {
        return BC250_GPUVA_MISALIGNED;
    }

    targetEnd = BaseGpuVa + Size;
    if (targetEnd < BaseGpuVa) {
        return BC250_GPUVA_RANGE_OVERFLOW;
    }
    if (targetEnd > Space->MaxAddress) {
        return BC250_GPUVA_OUT_OF_BOUNDS;
    }

    cursor = BaseGpuVa;
    while (cursor < targetEnd) {
        const BC250_GPUVA_RANGE_RECORD *best = NULL;
        uint32_t i;
        int left = 0;
        int right = (int)Space->SortedCount - 1;
        int startIdx = -1;

        /* Busca binária O(log N) na tabela ordenada */
        while (left <= right) {
            int mid = left + (right - left) / 2;
            uint32_t slot = Space->SortedIndices[mid];
            if (Space->Ranges[slot].BaseGpuVa <= cursor) {
                startIdx = mid;
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }

        if (startIdx >= 0 && startIdx < (int)Space->SortedCount) {
            uint32_t slot = Space->SortedIndices[startIdx];
            const BC250_GPUVA_RANGE_RECORD *candidate = &Space->Ranges[slot];
            if (candidate->State != BC250_GPUVA_RANGE_FREE &&
                candidate->OwnerProcess == OwnerProcess &&
                candidate->BaseGpuVa <= cursor &&
                candidate->EndGpuVa > cursor) {
                if (Context == NULL || candidate->OwnerContext == NULL ||
                    candidate->OwnerContext == Context) {
                    best = candidate;
                }
            }
        }

        if (best == NULL) {
            for (i = 0u; i < BC250_GPUVA_SPACE_MAX_RANGES; ++i) {
                const BC250_GPUVA_RANGE_RECORD *record = &Space->Ranges[i];
                if (record->State != BC250_GPUVA_RANGE_FREE &&
                    record->OwnerProcess == OwnerProcess &&
                    record->BaseGpuVa <= cursor &&
                    record->EndGpuVa > cursor) {
                    if (Context != NULL && record->OwnerContext != NULL &&
                        record->OwnerContext != Context) {
                        continue;
                    }
                    best = record;
                    break;
                }
            }
        }

        if (best == NULL) {
            /* Check if any range for another process overlaps */
            for (i = 0u; i < BC250_GPUVA_SPACE_MAX_RANGES; ++i) {
                const BC250_GPUVA_RANGE_RECORD *record = &Space->Ranges[i];
                if (record->State == BC250_GPUVA_RANGE_MAPPED &&
                    record->OwnerProcess != OwnerProcess &&
                    record->BaseGpuVa <= cursor &&
                    record->EndGpuVa > cursor) {
                    return BC250_GPUVA_OWNER_MISMATCH;
                }
            }
            return (cursor == BaseGpuVa) ? BC250_GPUVA_NOT_FOUND : BC250_GPUVA_GAP_DETECTED;
        }

        if (best->State == BC250_GPUVA_RANGE_TEARDOWN ||
            best->TeardownState != 0u) {
            return BC250_GPUVA_IN_TEARDOWN;
        }
        if (best->IsResident == 0u ||
            best->State != BC250_GPUVA_RANGE_MAPPED) {
            return BC250_GPUVA_NOT_RESIDENT;
        }
        if (RequiredPermissions != 0u &&
            (best->Permissions & RequiredPermissions) != RequiredPermissions) {
            return BC250_GPUVA_PERMISSION_DENIED;
        }
        if (ExpectedPageTableEpoch != 0u &&
            best->PageTableEpoch != ExpectedPageTableEpoch) {
            return BC250_GPUVA_STALE_EPOCH;
        }

        cursor = best->EndGpuVa;
    }

    return BC250_GPUVA_OK;
}

const char *Bc250GpuvaStatusName(BC250_GPUVA_STATUS Status)
{
    switch (Status) {
    case BC250_GPUVA_OK: return "ok";
    case BC250_GPUVA_BAD_ARGUMENT: return "bad_argument";
    case BC250_GPUVA_BAD_STATE: return "bad_state";
    case BC250_GPUVA_NO_SPACE: return "no_space";
    case BC250_GPUVA_INVALID_ADDRESS: return "invalid_address";
    case BC250_GPUVA_MISALIGNED: return "misaligned";
    case BC250_GPUVA_RANGE_OVERFLOW: return "range_overflow";
    case BC250_GPUVA_OUT_OF_BOUNDS: return "out_of_bounds";
    case BC250_GPUVA_INVALID_OVERLAP: return "invalid_overlap";
    case BC250_GPUVA_NOT_FOUND: return "not_found";
    case BC250_GPUVA_OWNER_MISMATCH: return "owner_mismatch";
    case BC250_GPUVA_PERMISSION_DENIED: return "permission_denied";
    case BC250_GPUVA_GAP_DETECTED: return "gap_detected";
    case BC250_GPUVA_NOT_RESIDENT: return "not_resident";
    case BC250_GPUVA_STALE_EPOCH: return "stale_epoch";
    case BC250_GPUVA_IN_TEARDOWN: return "in_teardown";
    case BC250_GPUVA_BUSY: return "busy";
    default: return "unknown";
    }
}
