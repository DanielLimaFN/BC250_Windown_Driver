#include "bc250_cmos_vram_contract.h"
#include <string.h>

BC250_CMOS_VRAM_STATUS Bc250ReadCmosUmaSize(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t *OutUmaSizeMb,
    uint64_t *OutVramBytes)
{
    uint8_t raw[BC250_CMOS_APCB_SIZE_BYTES];
    uint32_t sig;
    uint16_t umaSizeMb;
    uint32_t i;

    if (OutUmaSizeMb != 0) {
        *OutUmaSizeMb = 0;
    }
    if (OutVramBytes != 0) {
        *OutVramBytes = 0;
    }

    if (Transport == 0 || Transport->InByte == 0 || Transport->OutByte == 0 ||
        OutUmaSizeMb == 0 || OutVramBytes == 0) {
        return BC250_CMOS_VRAM_BAD_ARGUMENT;
    }

    memset(raw, 0, sizeof(raw));

    /* Le os 32 bytes da tabela APCB no CMOS (offsets 0x90 a 0xAF) */
    for (i = 0; i < BC250_CMOS_APCB_SIZE_BYTES; ++i) {
        Transport->OutByte(Transport->Context, BC250_CMOS_INDEX_PORT, (uint8_t)(BC250_CMOS_APCB_BASE_OFFSET + i));
        raw[i] = Transport->InByte(Transport->Context, BC250_CMOS_DATA_PORT);
    }

    /* Valida assinatura 'APCB' (Little Endian: 0x41, 0x50, 0x43, 0x42 -> 0x42435041) */
    sig = (uint32_t)raw[0] |
          ((uint32_t)raw[1] << 8) |
          ((uint32_t)raw[2] << 16) |
          ((uint32_t)raw[3] << 24);

    if (sig != BC250_CMOS_APCB_SIGNATURE) {
        return BC250_CMOS_VRAM_BAD_SIGNATURE;
    }

    /* Extrai o campo UMA_SIZE em MB (offset 0x1A correspondente a 0xAA no CMOS) */
    umaSizeMb = (uint16_t)raw[BC250_CMOS_APCB_UMA_SIZE_OFFSET] |
                ((uint16_t)raw[BC250_CMOS_APCB_UMA_SIZE_OFFSET + 1] << 8);

    /* Validacao de sanidade: faixa entre 256MB e 16384MB (16GB), multiplo de 16MB */
    if (umaSizeMb < BC250_CMOS_UMA_MIN_MB ||
        umaSizeMb > BC250_CMOS_UMA_MAX_MB ||
        (umaSizeMb & 0x0Fu) != 0) {
        return BC250_CMOS_VRAM_OUT_OF_RANGE;
    }

    *OutUmaSizeMb = umaSizeMb;
    *OutVramBytes = (uint64_t)umaSizeMb * 1024ULL * 1024ULL;

    return BC250_CMOS_VRAM_OK;
}

const char *Bc250CmosVramStatusName(BC250_CMOS_VRAM_STATUS Status)
{
    switch (Status) {
    case BC250_CMOS_VRAM_OK:
        return "BC250_CMOS_VRAM_OK";
    case BC250_CMOS_VRAM_BAD_ARGUMENT:
        return "BC250_CMOS_VRAM_BAD_ARGUMENT";
    case BC250_CMOS_VRAM_BAD_SIGNATURE:
        return "BC250_CMOS_VRAM_BAD_SIGNATURE";
    case BC250_CMOS_VRAM_OUT_OF_RANGE:
        return "BC250_CMOS_VRAM_OUT_OF_RANGE";
    default:
        return "BC250_CMOS_VRAM_UNKNOWN";
    }
}
