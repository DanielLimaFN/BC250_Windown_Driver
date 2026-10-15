/* ===========================================================================
 * bc250_dcn_probe_contract.c — Implementação Pura do Contrato DCN 2.0.1
 * =========================================================================== */

#include "bc250_dcn_probe_contract.h"
#include <string.h>

bool Bc250DcnDecodeBatteryA(
    uint32_t OtgControl,
    uint32_t OtgStatus,
    uint32_t OtgPosition,
    uint32_t OtgFrameCount,
    uint32_t OtgHTotal,
    uint32_t OtgHBlank,
    uint32_t OtgVTotal,
    uint32_t OtgVBlank,
    uint32_t HubpViewportDim,
    uint32_t HubpSurfacePitch,
    uint32_t HubpSurfaceAddrLo,
    uint32_t HubpSurfaceAddrHi,
    uint32_t HubpSurfaceInUseLo,
    uint32_t HubpSurfaceInUseHi,
    BC250_DCN_BATTERY_A_RESULT *OutResult)
{
    uint64_t pixelRate;

    if (OutResult == NULL) {
        return false;
    }
    memset(OutResult, 0, sizeof(*OutResult));

    OutResult->PrimarySurfaceAddress = ((uint64_t)HubpSurfaceAddrHi << 32) | HubpSurfaceAddrLo;
    OutResult->SurfaceInUseAddress = ((uint64_t)HubpSurfaceInUseHi << 32) | HubpSurfaceInUseLo;

    /* Viewport: largura nos bits [14:0], altura nos bits [30:16] */
    OutResult->ViewportWidth = HubpViewportDim & 0x7FFFu;
    OutResult->ViewportHeight = (HubpViewportDim >> 16) & 0x7FFFu;
    OutResult->SurfacePitch = HubpSurfacePitch & 0x3FFFFu;

    OutResult->OtgEnabled = (OtgControl & 1u) != 0;
    OutResult->FrameCount = OtgFrameCount;

    /* Posição: pixel nos bits [14:0], linha nos bits [30:16] */
    OutResult->CurrentPixel = OtgPosition & 0x7FFFu;
    OutResult->CurrentLine = (OtgPosition >> 16) & 0x7FFFu;

    /* OTG registers armazenam Total - 1 */
    OutResult->HTotal = (OtgHTotal & 0x7FFFu) != 0 ? (OtgHTotal & 0x7FFFu) + 1u : 0u;
    OutResult->VTotal = (OtgVTotal & 0x7FFFu) != 0 ? (OtgVTotal & 0x7FFFu) + 1u : 0u;

    OutResult->HBlankStart = OtgHBlank & 0x7FFFu;
    OutResult->HBlankEnd = (OtgHBlank >> 16) & 0x7FFFu;
    OutResult->VBlankStart = OtgVBlank & 0x7FFFu;
    OutResult->VBlankEnd = (OtgVBlank >> 16) & 0x7FFFu;

    /* Pixel rate de referência da Cyan Skillfish (173.611.437 Hz) */
    pixelRate = 173611437ULL;
    OutResult->CalculatedPixelRateHz = pixelRate;

    if (OutResult->HTotal > 0u && OutResult->VTotal > 0u) {
        uint64_t totalPixelsPerFrame = (uint64_t)OutResult->HTotal * OutResult->VTotal;
        OutResult->CalculatedRefreshHz100 = (uint32_t)(((pixelRate * 100ULL) + (totalPixelsPerFrame / 2ULL)) / totalPixelsPerFrame);
    }

    OutResult->Valid = (OutResult->ViewportWidth > 0u && OutResult->ViewportHeight > 0u);
    (void)OtgStatus;
    return OutResult->Valid;
}

bool Bc250DcnDecodeBatteryB(
    const uint8_t *DpcdBuffer,
    size_t DpcdLen,
    uint32_t AuxStatusRaw,
    BC250_DCN_BATTERY_B_RESULT *OutResult)
{
    if (OutResult == NULL) {
        return false;
    }
    memset(OutResult, 0, sizeof(*OutResult));
    OutResult->AuxStatusRaw = AuxStatusRaw;

    if (DpcdBuffer == NULL || DpcdLen < 3u) {
        return false;
    }

    /* DPCD 0x00000: DPCD_REV */
    OutResult->DpcdMajorRev = (DpcdBuffer[0] >> 4) & 0x0Fu;
    OutResult->DpcdMinorRev = DpcdBuffer[0] & 0x0Fu;

    /* DPCD 0x00001: MAX_LINK_RATE */
    OutResult->MaxLinkRateCode = DpcdBuffer[1];
    switch (OutResult->MaxLinkRateCode) {
    case 0x06: OutResult->MaxLinkRateMbps = 1620u; break; /* RBR  (1.62 Gbps) */
    case 0x0A: OutResult->MaxLinkRateMbps = 2700u; break; /* HBR  (2.70 Gbps) */
    case 0x14: OutResult->MaxLinkRateMbps = 5400u; break; /* HBR2 (5.40 Gbps) */
    case 0x1E: OutResult->MaxLinkRateMbps = 8100u; break; /* HBR3 (8.10 Gbps) */
    default:
        OutResult->MaxLinkRateMbps = (uint32_t)OutResult->MaxLinkRateCode * 270u;
        break;
    }

    /* DPCD 0x00002: MAX_LANE_COUNT */
    OutResult->MaxLaneCount = DpcdBuffer[2] & 0x1Fu;
    OutResult->EnhancedFramingSupported = (DpcdBuffer[2] & 0x80u) != 0;

    /* Se o buffer contém o bloco de status (0x202) */
    if (DpcdLen > 0x202u) {
        uint8_t lane01Status = DpcdBuffer[0x202];
        OutResult->CrDoneLane0 = (lane01Status & 0x01u) != 0;
        OutResult->EqDoneLane0 = (lane01Status & 0x02u) != 0;
        OutResult->CrDoneLane1 = (lane01Status & 0x10u) != 0;
        OutResult->EqDoneLane1 = (lane01Status & 0x20u) != 0;
    }

    OutResult->Valid = (OutResult->DpcdMajorRev > 0u && OutResult->MaxLaneCount > 0u);
    return OutResult->Valid;
}

bool Bc250DcnDecodeBatteryC(
    const uint8_t *EdidBuffer,
    size_t EdidLen,
    BC250_DCN_BATTERY_C_RESULT *OutResult)
{
    size_t i;
    uint32_t sum = 0u;

    if (OutResult == NULL) {
        return false;
    }
    memset(OutResult, 0, sizeof(*OutResult));

    if (EdidBuffer == NULL || EdidLen < 128u) {
        return false;
    }

    /* 1. Validação do Cabeçalho EDID: 00 FF FF FF FF FF FF 00 */
    if (EdidBuffer[0] == 0x00 && EdidBuffer[1] == 0xFF && EdidBuffer[2] == 0xFF &&
        EdidBuffer[3] == 0xFF && EdidBuffer[4] == 0xFF && EdidBuffer[5] == 0xFF &&
        EdidBuffer[6] == 0xFF && EdidBuffer[7] == 0x00) {
        OutResult->HeaderValid = true;
    } else {
        return false;
    }

    /* 2. Validação de Checksum (Soma módulo 256 deve ser zero) */
    for (i = 0; i < 128u; ++i) {
        sum += EdidBuffer[i];
    }
    OutResult->ChecksumValid = ((sum & 0xFFu) == 0u);
    if (!OutResult->ChecksumValid) {
        return false;
    }

    /* 3. Decodificação do ID do Fabricante (Bytes 8 e 9, 3 caracteres ASCII 5-bit) */
    {
        uint16_t mfgRaw = ((uint16_t)EdidBuffer[8] << 8) | EdidBuffer[9];
        OutResult->ManufacturerId[0] = (char)(((mfgRaw >> 10) & 0x1F) + 'A' - 1);
        OutResult->ManufacturerId[1] = (char)(((mfgRaw >> 5) & 0x1F) + 'A' - 1);
        OutResult->ManufacturerId[2] = (char)((mfgRaw & 0x1F) + 'A' - 1);
        OutResult->ManufacturerId[3] = '\0';
    }

    /* 4. Código do Produto e Serial */
    OutResult->ProductCode = (uint16_t)(EdidBuffer[10] | ((uint16_t)EdidBuffer[11] << 8));
    OutResult->SerialNumber = (uint32_t)(EdidBuffer[12] |
        ((uint32_t)EdidBuffer[13] << 8) |
        ((uint32_t)EdidBuffer[14] << 16) |
        ((uint32_t)EdidBuffer[15] << 24));

    OutResult->ManufactureWeek = EdidBuffer[16];
    OutResult->ManufactureYear = (uint16_t)(1990u + EdidBuffer[17]);
    OutResult->ExtensionBlockCount = EdidBuffer[126];

    /* 5. Decodificação do Detailed Timing Descriptor 1 (Bytes 54..71) */
    {
        uint16_t pixelClock10k = (uint16_t)(EdidBuffer[54] | ((uint16_t)EdidBuffer[55] << 8));
        if (pixelClock10k != 0u) {
            uint32_t hActive = (uint32_t)(EdidBuffer[56] | (((uint32_t)EdidBuffer[58] & 0xF0u) << 4));
            uint32_t hBlank  = (uint32_t)(EdidBuffer[57] | (((uint32_t)EdidBuffer[58] & 0x0Fu) << 8));
            uint32_t vActive = (uint32_t)(EdidBuffer[59] | (((uint32_t)EdidBuffer[61] & 0xF0u) << 4));
            uint32_t vBlank  = (uint32_t)(EdidBuffer[60] | (((uint32_t)EdidBuffer[61] & 0x0Fu) << 8));

            OutResult->PreferredPixelClockKhz = (uint32_t)pixelClock10k * 10u;
            OutResult->PreferredWidth = hActive;
            OutResult->PreferredHeight = vActive;

            if ((hActive + hBlank) > 0u && (vActive + vBlank) > 0u) {
                uint64_t totalPixels = (uint64_t)(hActive + hBlank) * (vActive + vBlank);
                OutResult->PreferredRefreshHz100 =
                    (uint32_t)(((uint64_t)OutResult->PreferredPixelClockKhz * 100000ULL) / totalPixels);
            }
        }
    }

    /* 6. Busca por Descritor de Nome do Monitor (Tag 0xFC nos 4 blocos de descritores) */
    {
        const size_t descriptorOffsets[4] = { 54u, 72u, 90u, 108u };
        size_t d;
        for (d = 0; d < 4u; ++d) {
            size_t off = descriptorOffsets[d];
            /* Descritor de exibição tem os primeiros dois bytes em zero e tag no byte 3 */
            if (EdidBuffer[off] == 0x00 && EdidBuffer[off + 1] == 0x00 &&
                EdidBuffer[off + 2] == 0x00 && EdidBuffer[off + 3] == 0xFC) {
                /* Tag 0xFC = Display Product Name */
                size_t c;
                size_t nameLen = 0;
                for (c = 0; c < 13u; ++c) {
                    char ch = (char)EdidBuffer[off + 5 + c];
                    if (ch == '\n' || ch == '\r' || ch == '\0') {
                        break;
                    }
                    OutResult->MonitorName[nameLen++] = ch;
                }
                /* Remove espaços à direita */
                while (nameLen > 0 && OutResult->MonitorName[nameLen - 1] == ' ') {
                    --nameLen;
                }
                OutResult->MonitorName[nameLen] = '\0';
                break;
            }
        }
    }

    OutResult->Valid = OutResult->HeaderValid && OutResult->ChecksumValid;
    return OutResult->Valid;
}
