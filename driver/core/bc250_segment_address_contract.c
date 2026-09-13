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
