#include "bc250_resource_contract.h"

#include <string.h>

#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)

static int
Bc250RangeEnd(uint64_t Start, uint64_t Length, uint64_t *End)
{
    if (Length == 0 || Start > BC250_UINT64_MAX_VALUE - (Length - 1u)) {
        return 0;
    }
    *End = Start + Length - 1u;
    return 1;
}

BC250_RESOURCE_STATUS
Bc250ResolveTranslatedBars(
    const BC250_MEMORY_RESOURCE *MemoryResources,
    size_t MemoryResourceCount,
    const BC250_PCI_BAR_RESOURCE *Bars,
    size_t BarCount,
    BC250_RESOLVED_BAR_RESOURCE *Resolved,
    size_t ResolvedCapacity,
    size_t *ResolvedCount)
{
    size_t i, j, outputCount = 0;

    if (ResolvedCount == NULL) {
        return BC250_RESOURCE_BAD_ARGUMENT;
    }
    *ResolvedCount = 0;
    if (MemoryResources == NULL || MemoryResourceCount == 0 ||
        Bars == NULL || BarCount == 0 || Resolved == NULL) {
        return BC250_RESOURCE_BAD_ARGUMENT;
    }

    for (i = 0; i < MemoryResourceCount; ++i) {
        uint64_t ignoredEnd;
        if (!Bc250RangeEnd(MemoryResources[i].Start,
                           MemoryResources[i].Length,
                           &ignoredEnd)) {
            return BC250_RESOURCE_RANGE_OVERFLOW;
        }
    }

    for (i = 0; i < BarCount; ++i) {
        if (!Bars[i].Present) {
            continue;
        }
        if (Bars[i].Index >= BC250_RESOURCE_MAX_BARS || Bars[i].Base == 0) {
            return BC250_RESOURCE_BAD_ARGUMENT;
        }
        for (j = i + 1; j < BarCount; ++j) {
            if (Bars[j].Present &&
                (Bars[j].Index == Bars[i].Index || Bars[j].Base == Bars[i].Base)) {
                return BC250_RESOURCE_DUPLICATE_BAR;
            }
        }
        ++outputCount;
    }

    if (outputCount == 0) {
        return BC250_RESOURCE_BAD_ARGUMENT;
    }
    if (ResolvedCapacity < outputCount) {
        return BC250_RESOURCE_OUTPUT_TOO_SMALL;
    }

    outputCount = 0;
    for (i = 0; i < BarCount; ++i) {
        uint64_t selectedEnd = BC250_UINT64_MAX_VALUE;
        uint64_t exactEnd = BC250_UINT64_MAX_VALUE;
        uint64_t nextBarBase = BC250_UINT64_MAX_VALUE;
        uint32_t depth = 0;
        uint32_t exactDepth = 0;
        uint32_t capped = 0;

        if (!Bars[i].Present) {
            continue;
        }

        for (j = 0; j < MemoryResourceCount; ++j) {
            uint64_t end;
            (void)Bc250RangeEnd(MemoryResources[j].Start,
                                MemoryResources[j].Length,
                                &end);
            if (MemoryResources[j].Start <= Bars[i].Base &&
                end >= Bars[i].Base) {
                ++depth;
                if (end < selectedEnd) {
                    selectedEnd = end;
                }
                if (MemoryResources[j].Start == Bars[i].Base) {
                    ++exactDepth;
                    if (exactEnd == BC250_UINT64_MAX_VALUE) {
                        exactEnd = end;
                    } else if (exactEnd != end) {
                        return BC250_RESOURCE_AMBIGUOUS_RANGE;
                    }
                }
            }
        }
        if (depth == 0) {
            return BC250_RESOURCE_BAR_UNASSIGNED;
        }
        if (exactDepth != 0) {
            selectedEnd = exactEnd;
        }

        for (j = 0; j < BarCount; ++j) {
            if (Bars[j].Present && Bars[j].Base > Bars[i].Base &&
                Bars[j].Base < nextBarBase) {
                nextBarBase = Bars[j].Base;
            }
        }
        if (nextBarBase != BC250_UINT64_MAX_VALUE && nextBarBase - 1u < selectedEnd) {
            selectedEnd = nextBarBase - 1u;
            capped = 1;
        }
        if (selectedEnd < Bars[i].Base) {
            return BC250_RESOURCE_BAR_UNASSIGNED;
        }

        memset(&Resolved[outputCount], 0, sizeof(Resolved[outputCount]));
        Resolved[outputCount].Index = Bars[i].Index;
        Resolved[outputCount].DescriptorDepth = depth;
        Resolved[outputCount].CappedByNextBar = capped;
        Resolved[outputCount].Start = Bars[i].Base;
        Resolved[outputCount].Length = selectedEnd - Bars[i].Base + 1u;
        ++outputCount;
    }

    *ResolvedCount = outputCount;
    return BC250_RESOURCE_OK;
}

const char *
Bc250ResourceStatusName(BC250_RESOURCE_STATUS Status)
{
    switch (Status) {
    case BC250_RESOURCE_OK: return "ok";
    case BC250_RESOURCE_BAD_ARGUMENT: return "bad_argument";
    case BC250_RESOURCE_RANGE_OVERFLOW: return "range_overflow";
    case BC250_RESOURCE_DUPLICATE_BAR: return "duplicate_bar";
    case BC250_RESOURCE_AMBIGUOUS_RANGE: return "ambiguous_range";
    case BC250_RESOURCE_BAR_UNASSIGNED: return "bar_unassigned";
    case BC250_RESOURCE_OUTPUT_TOO_SMALL: return "output_too_small";
    default: return "unknown";
    }
}
