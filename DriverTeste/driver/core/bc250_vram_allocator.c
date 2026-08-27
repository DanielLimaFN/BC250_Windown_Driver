#include "bc250_vram_allocator.h"

#include <string.h>

#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)

static int Bc250IsPowerOfTwo(uint64_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

static int Bc250RangeEndExclusive(uint64_t Offset, uint64_t Length, uint64_t *End)
{
    if (Length == 0 || Offset > BC250_UINT64_MAX_VALUE - Length) return 0;
    *End = Offset + Length;
    return 1;
}

static int Bc250RangesOverlap(uint64_t A, uint64_t ALength,
                              uint64_t B, uint64_t BLength)
{
    uint64_t aEnd, bEnd;
    if (!Bc250RangeEndExclusive(A, ALength, &aEnd) ||
        !Bc250RangeEndExclusive(B, BLength, &bEnd)) return 1;
    return A < bEnd && B < aEnd;
}

static uint64_t Bc250AlignDown(uint64_t Value, uint64_t Alignment)
{
    return Value & ~(Alignment - 1u);
}

static int Bc250AlignUp(uint64_t Value, uint64_t Alignment, uint64_t *Result)
{
    uint64_t mask = Alignment - 1u;
    if (Value > BC250_UINT64_MAX_VALUE - mask) return 0;
    *Result = (Value + mask) & ~mask;
    return 1;
}

static uint32_t Bc250NextGeneration(BC250_VRAM_ALLOCATOR *Allocator)
{
    ++Allocator->NextGeneration;
    if (Allocator->NextGeneration == 0) ++Allocator->NextGeneration;
    return Allocator->NextGeneration;
}

static BC250_VRAM_STATUS
Bc250Insert(BC250_VRAM_ALLOCATOR *Allocator,
            uint64_t Offset, uint64_t Length, uint64_t Alignment,
            BC250_VRAM_ROLE Role, uint32_t Flags, BC250_VRAM_TOKEN *Token)
{
    size_t i;
    uint64_t end;

    if (Allocator == NULL || Token == NULL ||
        !Bc250IsPowerOfTwo(Alignment) || Role == BC250_VRAM_ROLE_UNKNOWN ||
        Role > BC250_VRAM_ROLE_MAX ||
        (Flags & ~(BC250_VRAM_ALLOC_TOP_DOWN |
                   BC250_VRAM_ALLOC_CPU_VISIBLE |
                   BC250_VRAM_REGION_PERSISTENT)) != 0) {
        return BC250_VRAM_BAD_ARGUMENT;
    }
    if (!Bc250RangeEndExclusive(Offset, Length, &end)) {
        return BC250_VRAM_RANGE_OVERFLOW;
    }
    if (end > Allocator->TotalBytes) return BC250_VRAM_OUT_OF_RANGE;
    if ((Flags & BC250_VRAM_ALLOC_CPU_VISIBLE) && end > Allocator->CpuVisibleBytes) {
        return BC250_VRAM_OUT_OF_RANGE;
    }
    if ((Offset & (Alignment - 1u)) != 0) return BC250_VRAM_MISALIGNED;

    for (i = 0; i < BC250_VRAM_MAX_REGIONS; ++i) {
        if (Allocator->Regions[i].InUse &&
            Bc250RangesOverlap(Offset, Length,
                               Allocator->Regions[i].Offset,
                               Allocator->Regions[i].Length)) {
            return BC250_VRAM_OVERLAP;
        }
    }
    for (i = 0; i < BC250_VRAM_MAX_REGIONS; ++i) {
        BC250_VRAM_REGION *region;
        if (Allocator->Regions[i].InUse) continue;
        region = &Allocator->Regions[i];
        memset(region, 0, sizeof(*region));
        region->Offset = Offset;
        region->Length = Length;
        region->Alignment = Alignment;
        region->Role = (uint32_t)Role;
        region->Flags = Flags;
        region->Generation = Bc250NextGeneration(Allocator);
        region->InUse = 1;
        ++Allocator->RegionCount;
        Token->Slot = (uint32_t)i;
        Token->Generation = region->Generation;
        return BC250_VRAM_OK;
    }
    return BC250_VRAM_TABLE_FULL;
}

BC250_VRAM_STATUS
Bc250VramAllocatorInit(BC250_VRAM_ALLOCATOR *Allocator,
                       uint64_t TotalBytes, uint64_t CpuVisibleBytes)
{
    if (Allocator == NULL || TotalBytes == 0 || CpuVisibleBytes > TotalBytes) {
        return BC250_VRAM_BAD_ARGUMENT;
    }
    memset(Allocator, 0, sizeof(*Allocator));
    Allocator->TotalBytes = TotalBytes;
    Allocator->CpuVisibleBytes = CpuVisibleBytes;
    return BC250_VRAM_OK;
}

BC250_VRAM_STATUS
Bc250VramReserveFixed(BC250_VRAM_ALLOCATOR *Allocator,
                      uint64_t Offset, uint64_t Length, uint64_t Alignment,
                      BC250_VRAM_ROLE Role, uint32_t Flags,
                      BC250_VRAM_TOKEN *Token)
{
    if (Token != NULL) memset(Token, 0, sizeof(*Token));
    if (Flags & BC250_VRAM_ALLOC_TOP_DOWN) return BC250_VRAM_BAD_ARGUMENT;
    return Bc250Insert(Allocator, Offset, Length, Alignment, Role, Flags, Token);
}

BC250_VRAM_STATUS
Bc250VramAllocate(BC250_VRAM_ALLOCATOR *Allocator,
                  uint64_t Length, uint64_t Alignment,
                  BC250_VRAM_ROLE Role, uint32_t Flags,
                  BC250_VRAM_TOKEN *Token)
{
    uint64_t limit, cursor, candidate;
    size_t attempts;

    if (Token != NULL) memset(Token, 0, sizeof(*Token));
    if (Allocator == NULL || Token == NULL || Length == 0 ||
        !Bc250IsPowerOfTwo(Alignment) || Role == BC250_VRAM_ROLE_UNKNOWN ||
        Role > BC250_VRAM_ROLE_MAX ||
        (Flags & ~(BC250_VRAM_ALLOC_TOP_DOWN |
                   BC250_VRAM_ALLOC_CPU_VISIBLE |
                   BC250_VRAM_REGION_PERSISTENT)) != 0) {
        return BC250_VRAM_BAD_ARGUMENT;
    }
    limit = (Flags & BC250_VRAM_ALLOC_CPU_VISIBLE)
          ? Allocator->CpuVisibleBytes : Allocator->TotalBytes;
    if (Length > limit) return BC250_VRAM_NO_SPACE;

    if (Flags & BC250_VRAM_ALLOC_TOP_DOWN) {
        cursor = limit;
        for (attempts = 0; attempts <= BC250_VRAM_MAX_REGIONS; ++attempts) {
            uint64_t nextCursor = cursor;
            size_t i;
            int overlap = 0;

            if (Length > cursor) return BC250_VRAM_NO_SPACE;
            candidate = Bc250AlignDown(cursor - Length, Alignment);
            for (i = 0; i < BC250_VRAM_MAX_REGIONS; ++i) {
                if (!Allocator->Regions[i].InUse) continue;
                if (Bc250RangesOverlap(candidate, Length,
                                       Allocator->Regions[i].Offset,
                                       Allocator->Regions[i].Length)) {
                    overlap = 1;
                    if (Allocator->Regions[i].Offset < nextCursor) {
                        nextCursor = Allocator->Regions[i].Offset;
                    }
                }
            }
            if (!overlap) {
                return Bc250Insert(Allocator, candidate, Length, Alignment,
                                   Role, Flags, Token);
            }
            if (nextCursor >= cursor) return BC250_VRAM_NO_SPACE;
            cursor = nextCursor;
        }
    } else {
        cursor = 0;
        for (attempts = 0; attempts <= BC250_VRAM_MAX_REGIONS; ++attempts) {
            uint64_t nextCursor = cursor;
            uint64_t end;
            size_t i;
            int overlap = 0;

            if (!Bc250AlignUp(cursor, Alignment, &candidate) ||
                !Bc250RangeEndExclusive(candidate, Length, &end) || end > limit) {
                return BC250_VRAM_NO_SPACE;
            }
            for (i = 0; i < BC250_VRAM_MAX_REGIONS; ++i) {
                uint64_t regionEnd;
                if (!Allocator->Regions[i].InUse) continue;
                if (Bc250RangesOverlap(candidate, Length,
                                       Allocator->Regions[i].Offset,
                                       Allocator->Regions[i].Length)) {
                    overlap = 1;
                    (void)Bc250RangeEndExclusive(Allocator->Regions[i].Offset,
                                                 Allocator->Regions[i].Length,
                                                 &regionEnd);
                    if (regionEnd > nextCursor) nextCursor = regionEnd;
                }
            }
            if (!overlap) {
                return Bc250Insert(Allocator, candidate, Length, Alignment,
                                   Role, Flags, Token);
            }
            if (nextCursor <= cursor) return BC250_VRAM_NO_SPACE;
            cursor = nextCursor;
        }
    }
    return BC250_VRAM_NO_SPACE;
}

BC250_VRAM_STATUS
Bc250VramGetRegion(const BC250_VRAM_ALLOCATOR *Allocator,
                   BC250_VRAM_TOKEN Token, BC250_VRAM_REGION *Region)
{
    const BC250_VRAM_REGION *found;
    if (Allocator == NULL || Region == NULL || Token.Slot >= BC250_VRAM_MAX_REGIONS) {
        return BC250_VRAM_BAD_ARGUMENT;
    }
    found = &Allocator->Regions[Token.Slot];
    if (!found->InUse || found->Generation != Token.Generation) {
        return BC250_VRAM_STALE_TOKEN;
    }
    *Region = *found;
    return BC250_VRAM_OK;
}

BC250_VRAM_STATUS
Bc250VramRelease(BC250_VRAM_ALLOCATOR *Allocator, BC250_VRAM_TOKEN Token)
{
    BC250_VRAM_REGION *region;
    if (Allocator == NULL || Token.Slot >= BC250_VRAM_MAX_REGIONS) {
        return BC250_VRAM_BAD_ARGUMENT;
    }
    region = &Allocator->Regions[Token.Slot];
    if (!region->InUse || region->Generation != Token.Generation) {
        return BC250_VRAM_STALE_TOKEN;
    }
    if (region->Flags & BC250_VRAM_REGION_PERSISTENT) {
        return BC250_VRAM_PROTECTED;
    }
    memset(region, 0, sizeof(*region));
    --Allocator->RegionCount;
    return BC250_VRAM_OK;
}

BC250_VRAM_STATUS Bc250VramValidate(const BC250_VRAM_ALLOCATOR *Allocator)
{
    size_t i, j;
    uint32_t count = 0;
    if (Allocator == NULL || Allocator->TotalBytes == 0 ||
        Allocator->CpuVisibleBytes > Allocator->TotalBytes) {
        return BC250_VRAM_BAD_ARGUMENT;
    }
    for (i = 0; i < BC250_VRAM_MAX_REGIONS; ++i) {
        uint64_t end;
        const BC250_VRAM_REGION *a = &Allocator->Regions[i];
        if (!a->InUse) continue;
        ++count;
        if (!Bc250IsPowerOfTwo(a->Alignment) ||
            (a->Offset & (a->Alignment - 1u)) != 0) return BC250_VRAM_MISALIGNED;
        if (!Bc250RangeEndExclusive(a->Offset, a->Length, &end)) {
            return BC250_VRAM_RANGE_OVERFLOW;
        }
        if (end > Allocator->TotalBytes) return BC250_VRAM_OUT_OF_RANGE;
        for (j = i + 1; j < BC250_VRAM_MAX_REGIONS; ++j) {
            const BC250_VRAM_REGION *b = &Allocator->Regions[j];
            if (b->InUse && Bc250RangesOverlap(a->Offset, a->Length,
                                               b->Offset, b->Length)) {
                return BC250_VRAM_OVERLAP;
            }
        }
    }
    return count == Allocator->RegionCount ? BC250_VRAM_OK : BC250_VRAM_BAD_ARGUMENT;
}

const char *Bc250VramStatusName(BC250_VRAM_STATUS Status)
{
    switch (Status) {
    case BC250_VRAM_OK: return "ok";
    case BC250_VRAM_BAD_ARGUMENT: return "bad_argument";
    case BC250_VRAM_RANGE_OVERFLOW: return "range_overflow";
    case BC250_VRAM_OUT_OF_RANGE: return "out_of_range";
    case BC250_VRAM_MISALIGNED: return "misaligned";
    case BC250_VRAM_OVERLAP: return "overlap";
    case BC250_VRAM_NO_SPACE: return "no_space";
    case BC250_VRAM_TABLE_FULL: return "table_full";
    case BC250_VRAM_STALE_TOKEN: return "stale_token";
    case BC250_VRAM_PROTECTED: return "protected";
    default: return "unknown";
    }
}
