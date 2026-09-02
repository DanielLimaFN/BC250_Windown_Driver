#include "bc250_ih_contract.h"

#include <string.h>

static int Bc250IsPowerOfTwo32(uint32_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

BC250_IH_STATUS Bc250IhValidateProvenance(
    const BC250_IH_PROVENANCE *Provenance)
{
    uint32_t i;
    uint32_t j;
    if (Provenance == NULL || Provenance->SnapshotGeneration == 0u ||
        Provenance->RegisterLayoutProven == 0u ||
        Provenance->ClientSourceMapProven == 0u) {
        return BC250_IH_PROVENANCE_MISSING;
    }
    if (Provenance->VectorLayoutVersion != BC250_IH_VECTOR_LAYOUT_V4 ||
        Provenance->VectorEntryDwords != BC250_IH_ENTRY_SIZE_DWORDS) {
        return BC250_IH_LAYOUT_MISMATCH;
    }
    for (i = 0u; i < 7u; ++i) {
        if ((Provenance->RegisterOffsets[i] & 3u) != 0u) {
            return BC250_IH_MISALIGNED_ADDRESS;
        }
        for (j = 0u; j < i; ++j) {
            if (Provenance->RegisterOffsets[i] == Provenance->RegisterOffsets[j]) {
                return BC250_IH_LAYOUT_MISMATCH;
            }
        }
    }
    return BC250_IH_OK;
}

BC250_IH_STATUS Bc250IhInit(
    BC250_IH_STATE *State,
    const BC250_IH_RING_PROPERTIES *Props)
{
    if (State == NULL || Props == NULL) {
        return BC250_IH_BAD_ARGUMENT;
    }
    if (!Bc250IsPowerOfTwo32(Props->RingSizeDwords) || Props->RingSizeDwords < 64u) {
        return BC250_IH_INVALID_RING_SIZE;
    }
    if ((Props->RingGpuAddress & (BC250_IH_RING_ALIGNMENT - 1u)) != 0) {
        return BC250_IH_MISALIGNED_ADDRESS;
    }

    memset(State, 0, sizeof(BC250_IH_STATE));
    State->RingSizeBytes = Props->RingSizeDwords * sizeof(uint32_t);
    return BC250_IH_OK;
}

BC250_IH_STATUS Bc250IhDecodeEntry(
    const uint32_t *RawDwords,
    BC250_IH_VECTOR_ENTRY *Entry)
{
    uint32_t dw0, dw1, dw2, dw3;

    if (RawDwords == NULL || Entry == NULL) {
        return BC250_IH_BAD_ARGUMENT;
    }

    dw0 = RawDwords[0];
    dw1 = RawDwords[1];
    dw2 = RawDwords[2];
    dw3 = RawDwords[3];

    Entry->ClientId = dw0 & 0xFFu;
    Entry->SrcId = (dw0 >> 8) & 0xFFu;
    Entry->RingId = (dw0 >> 16) & 0xFFu;
    Entry->Vmid = (dw0 >> 24) & 0x0Fu;
    Entry->VmidSource = dw0 >> 31;

    Entry->Timestamp = ((uint64_t)(dw2 & 0xFFFFu) << 32) | (uint64_t)dw1;
    Entry->TimestampSource = dw2 >> 31;

    Entry->Pasid = dw3 & 0xFFFFu;
    Entry->NodeId = (dw3 >> 16) & 0xFFu;
    Entry->SourceData[0] = RawDwords[4];
    Entry->SourceData[1] = RawDwords[5];
    Entry->SourceData[2] = RawDwords[6];
    Entry->SourceData[3] = RawDwords[7];

    return BC250_IH_OK;
}

BC250_IH_STATUS Bc250IhEncodeEntry(
    const BC250_IH_VECTOR_ENTRY *Entry,
    uint32_t *RawDwords)
{
    if (Entry == NULL || RawDwords == NULL) {
        return BC250_IH_BAD_ARGUMENT;
    }

    RawDwords[0] = (Entry->ClientId & 0xFFu) |
                   ((Entry->SrcId & 0xFFu) << 8) |
                   ((Entry->RingId & 0xFFu) << 16) |
                   ((Entry->Vmid & 0x0Fu) << 24) |
                   ((Entry->VmidSource & 0x01u) << 31);

    RawDwords[1] = (uint32_t)(Entry->Timestamp & 0xFFFFFFFFull);
    RawDwords[2] = ((uint32_t)(Entry->Timestamp >> 32) & 0xFFFFu) |
                   ((Entry->TimestampSource & 0x01u) << 31);

    RawDwords[3] = (Entry->Pasid & 0xFFFFu) |
                   ((Entry->NodeId & 0xFFu) << 16);
    RawDwords[4] = Entry->SourceData[0];
    RawDwords[5] = Entry->SourceData[1];
    RawDwords[6] = Entry->SourceData[2];
    RawDwords[7] = Entry->SourceData[3];

    return BC250_IH_OK;
}

int Bc250IhCheckInterruptNeeded(
    const BC250_IH_STATE *State,
    uint32_t CurrentWptr)
{
    if (State == NULL || State->RingSizeBytes == 0) {
        return 0;
    }

    return (State->RptrBytes != (CurrentWptr & (State->RingSizeBytes - 1u))) ? 1 : 0;
}

BC250_IH_STATUS Bc250IhProcessRing(
    BC250_IH_STATE *State,
    const uint32_t *RingBuffer,
    uint32_t CurrentWptr,
    BC250_IH_VECTOR_ENTRY *OutEntries,
    uint32_t MaxEntries,
    uint32_t *OutCount)
{
    uint32_t mask;
    uint32_t wptr;
    uint32_t rptr;
    BC250_IH_STATUS status;

    if (State == NULL || RingBuffer == NULL || OutCount == NULL) {
        return BC250_IH_BAD_ARGUMENT;
    }
    if (!Bc250IsPowerOfTwo32(State->RingSizeBytes) ||
        (CurrentWptr & (sizeof(uint32_t) - 1u)) != 0u) {
        return BC250_IH_INVALID_RING_SIZE;
    }

    *OutCount = 0;
    mask = State->RingSizeBytes - 1u;
    wptr = CurrentWptr & mask;
    State->WptrBytes = wptr;

    while (State->RptrBytes != wptr) {
        if (OutEntries != NULL && *OutCount >= MaxEntries) {
            break;
        }

        rptr = State->RptrBytes;
        if (OutEntries != NULL) {
            status = Bc250IhDecodeEntry(&RingBuffer[rptr >> 2], &OutEntries[*OutCount]);
            if (status != BC250_IH_OK) {
                return status;
            }

            if (OutEntries[*OutCount].ClientId == BC250_IH_CLIENTID_SDMA0 &&
                OutEntries[*OutCount].SrcId == BC250_IH_SRCID_SDMA_TRAP) {
                State->SdmaFenceEvents++;
            } else if (OutEntries[*OutCount].ClientId == BC250_IH_CLIENTID_GRBM_CP &&
                       OutEntries[*OutCount].SrcId == BC250_IH_SRCID_CP_EOP) {
                State->ComputeFenceEvents++;
            }

            (*OutCount)++;
        }

        State->ProcessedCount++;
        State->RptrBytes = (State->RptrBytes + BC250_IH_ENTRY_SIZE_BYTES) & mask;
    }

    return BC250_IH_OK;
}

const char *Bc250IhStatusName(BC250_IH_STATUS Status)
{
    switch (Status) {
    case BC250_IH_OK: return "ok";
    case BC250_IH_BAD_ARGUMENT: return "bad_argument";
    case BC250_IH_MISALIGNED_ADDRESS: return "misaligned_address";
    case BC250_IH_INVALID_RING_SIZE: return "invalid_ring_size";
    case BC250_IH_BUFFER_OVERFLOW: return "buffer_overflow";
    case BC250_IH_PROVENANCE_MISSING: return "provenance_missing";
    case BC250_IH_LAYOUT_MISMATCH: return "layout_mismatch";
    default: return "unknown";
    }
}
