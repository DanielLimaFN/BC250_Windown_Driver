#include "bc250_segment_address_contract.h"

BC250_SEGMENT_ADDRESS_STATUS Bc250SegmentOffsetToVramOffset(
    uint64_t LocalSegmentBaseOffset,
    uint64_t LocalSegmentBytes,
    uint32_t SegmentId,
    uint64_t SegmentOffset,
    uint64_t SizeBytes,
    uint64_t *VramOffset)
{
    if (VramOffset == NULL) {
        return BC250_SEGMENT_ADDRESS_BAD_ARGUMENT;
    }
    *VramOffset = 0ull;

    if (SegmentId != BC250_SEGMENT_ADDRESS_LOCAL_SEGMENT_ID) {
        return BC250_SEGMENT_ADDRESS_NOT_LOCAL_SEGMENT;
    }
    if (LocalSegmentBytes == 0ull) {
        return BC250_SEGMENT_ADDRESS_BAD_ARGUMENT;
    }

    /*
     * A faixa pedida tem de caber inteira no segmento. Sem isto, uma origem
     * ligeiramente grande demais leria memoria vizinha e ainda assim
     * concluiria -- indistinguivel de sucesso.
     */
    if (SegmentOffset >= LocalSegmentBytes) {
        return BC250_SEGMENT_ADDRESS_OUT_OF_SEGMENT;
    }
    if (SizeBytes != 0ull && SizeBytes > LocalSegmentBytes - SegmentOffset) {
        return BC250_SEGMENT_ADDRESS_OUT_OF_SEGMENT;
    }
    if (LocalSegmentBaseOffset > (uint64_t)~0ull - SegmentOffset) {
        return BC250_SEGMENT_ADDRESS_OVERFLOW;
    }

    *VramOffset = LocalSegmentBaseOffset + SegmentOffset;
    return BC250_SEGMENT_ADDRESS_OK;
}

BC250_SEGMENT_ADDRESS_STATUS Bc250SegmentOffsetToMc(
    uint64_t VramMcBase,
    uint64_t LocalSegmentBaseOffset,
    uint64_t LocalSegmentBytes,
    uint32_t SegmentId,
    uint64_t SegmentOffset,
    uint64_t SizeBytes,
    uint64_t *McAddress)
{
    uint64_t vramOffset = 0ull;
    BC250_SEGMENT_ADDRESS_STATUS status;

    if (McAddress == NULL) {
        return BC250_SEGMENT_ADDRESS_BAD_ARGUMENT;
    }
    *McAddress = 0ull;

    if (VramMcBase == 0ull) {
        return BC250_SEGMENT_ADDRESS_BAD_ARGUMENT;
    }

    /* Vista unica: a conta e as guardas de faixa moram na funcao acima. */
    status = Bc250SegmentOffsetToVramOffset(LocalSegmentBaseOffset,
        LocalSegmentBytes, SegmentId, SegmentOffset, SizeBytes, &vramOffset);
    if (status != BC250_SEGMENT_ADDRESS_OK) {
        return status;
    }

    if (VramMcBase > (uint64_t)~0ull - vramOffset) {
        return BC250_SEGMENT_ADDRESS_OVERFLOW;
    }

    *McAddress = VramMcBase + vramOffset;
    return BC250_SEGMENT_ADDRESS_OK;
}

static uint64_t Bc250HiddenAlignUp(uint64_t Value, uint64_t Alignment)
{
    if (Value > (uint64_t)~0ull - (Alignment - 1ull)) return (uint64_t)~0ull & ~(Alignment - 1ull);
    return (Value + Alignment - 1ull) & ~(Alignment - 1ull);
}

BC250_HIDDEN_SEGMENT_STATUS Bc250ComputeHiddenSegment(
    uint64_t VramTotalBytes,
    uint64_t CpuVisibleBytes,
    uint64_t TopGuardBytes,
    const uint64_t *BusyOffsets,
    const uint64_t *BusyLengths,
    uint32_t BusyCount,
    uint64_t *BaseOffset,
    uint64_t *Bytes)
{
    const uint64_t align = BC250_HIDDEN_SEGMENT_ALIGNMENT;
    uint64_t start;
    uint64_t end;
    uint32_t pass;
    uint32_t i;

    if (BaseOffset == NULL || Bytes == NULL) return BC250_HIDDEN_SEGMENT_BAD_ARGUMENT;
    *BaseOffset = 0ull;
    *Bytes = 0ull;
    if (VramTotalBytes == 0ull || CpuVisibleBytes == 0ull ||
        (BusyCount != 0u && (BusyOffsets == NULL || BusyLengths == NULL))) {
        return BC250_HIDDEN_SEGMENT_BAD_ARGUMENT;
    }
    if (CpuVisibleBytes >= VramTotalBytes ||
        TopGuardBytes >= VramTotalBytes - CpuVisibleBytes) {
        return BC250_HIDDEN_SEGMENT_NO_HIDDEN_VRAM;
    }

    start = Bc250HiddenAlignUp(CpuVisibleBytes, align);
    end = (VramTotalBytes - TopGuardBytes) & ~(align - 1ull);

    /* Prefixo empurra o inicio; repete ate estabilizar (lista nao ordenada). */
    for (pass = 0u; pass <= BusyCount; ++pass) {
        uint64_t before = start;
        for (i = 0u; i < BusyCount; ++i) {
            uint64_t busyEnd;
            if (BusyLengths[i] == 0ull) continue;
            if (BusyOffsets[i] > (uint64_t)~0ull - BusyLengths[i]) return BC250_HIDDEN_SEGMENT_BAD_ARGUMENT;
            busyEnd = BusyOffsets[i] + BusyLengths[i];
            if (BusyOffsets[i] <= start && busyEnd > start) {
                start = Bc250HiddenAlignUp(busyEnd, align);
            }
        }
        if (start == before) break;
    }
    /* Regiao acima do inicio trunca o fim. */
    for (i = 0u; i < BusyCount; ++i) {
        if (BusyLengths[i] == 0ull) continue;
        if (BusyOffsets[i] > start && BusyOffsets[i] < end) {
            end = BusyOffsets[i] & ~(align - 1ull);
        }
    }
    if (end <= start || end - start < BC250_HIDDEN_SEGMENT_MIN_BYTES) {
        return BC250_HIDDEN_SEGMENT_TOO_SMALL;
    }
    *BaseOffset = start;
    *Bytes = end - start;
    return BC250_HIDDEN_SEGMENT_OK;
}

const char *Bc250HiddenSegmentStatusName(BC250_HIDDEN_SEGMENT_STATUS Status)
{
    switch (Status) {
    case BC250_HIDDEN_SEGMENT_OK: return "ok";
    case BC250_HIDDEN_SEGMENT_BAD_ARGUMENT: return "bad_argument";
    case BC250_HIDDEN_SEGMENT_NO_HIDDEN_VRAM: return "sem_vram_alem_da_janela_de_cpu";
    case BC250_HIDDEN_SEGMENT_TOO_SMALL: return "pequeno_demais_apos_reservas";
    default: return "desconhecido";
    }
}