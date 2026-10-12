/* ===========================================================================
 * bc250_dcn_probe_contract.h — Contrato de Sonda do DCN 2.0.1 (Baterias A, B e C)
 *
 * Target: AMD BC-250 APU (1002:13FE, GC 10.1.3 / Cyan Skillfish 2, DCN 2.0.1)
 *
 * Bateria A: Inspeção do Scanout e Timings Físicos do OTG0/HUBP0 (GOP Takeover)
 * Bateria B: Consulta do Link DisplayPort via DPCD por canal AUX
 * Bateria C: Leitura e Decodificação do EDID Físico Real via I2C-over-AUX (0x50)
 *
 * Todas as operações são estritamente SOMENTE LEITURA ou sideband AUX.
 * Zero escrita em registradores de clock (PLL), PHY ou timing do display.
 * =========================================================================== */

#ifndef BC250_DCN_PROBE_CONTRACT_H
#define BC250_DCN_PROBE_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#ifndef BC250_DCN_EXTRA_TYPES_DEFINED
#define BC250_DCN_EXTRA_TYPES_DEFINED
typedef UINT16 uint16_t;
typedef UINT8  uint8_t;
typedef BOOLEAN bool;
#define true 1
#define false 0
#endif
#else
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Constantes de Offset de Registradores DCN 2.0.1 (BAR5 Byte Offsets)
 * Fórmula autoritativa: (DMU_BASE.segment[2] + mmREG_dw) * 4
 * DMU_BASE.segment[2] = 0x34C0 dwords (0x0D300 bytes)
 * --------------------------------------------------------------------------- */

/* OTG0 (Output Timing Generator) */
#define BC250_DCN_REG_OTG0_H_TOTAL                  0x00013FA8u /* (0x34C0 + 0x1B2A) * 4 */
#define BC250_DCN_REG_OTG0_H_BLANK_START_END        0x00013FACu /* (0x34C0 + 0x1B2B) * 4 */
#define BC250_DCN_REG_OTG0_H_SYNC_A                 0x00013FB0u /* (0x34C0 + 0x1B2C) * 4 */
#define BC250_DCN_REG_OTG0_V_TOTAL                  0x00013FBCu /* (0x34C0 + 0x1B2F) * 4 */
#define BC250_DCN_REG_OTG0_V_BLANK_START_END        0x00013FD8u /* (0x34C0 + 0x1B36) * 4 */
#define BC250_DCN_REG_OTG0_V_SYNC_A                 0x00013FDCu /* (0x34C0 + 0x1B37) * 4 */
#define BC250_DCN_REG_OTG0_CONTROL                  0x00014004u /* (0x34C0 + 0x1B41) * 4 */
#define BC250_DCN_REG_OTG0_STATUS                   0x00014024u /* (0x34C0 + 0x1B49) * 4 */
#define BC250_DCN_REG_OTG0_STATUS_POSITION          0x00014028u /* (0x34C0 + 0x1B4A) * 4 */
#define BC250_DCN_REG_OTG0_STATUS_FRAME_COUNT       0x00014030u /* (0x34C0 + 0x1B4C) * 4 */

/* HUBP0 / HUBPREQ0 (Surface Scanout & Flip Pipe) */
#define BC250_DCN_REG_HUBP0_DCSURF_SURFACE_CONFIG   0x0000EA94u /* (0x34C0 + 0x05E5) * 4 */
#define BC250_DCN_REG_HUBP0_DCSURF_PRI_VIEWPORT_DIM 0x0000EAA8u /* (0x34C0 + 0x05EA) * 4 */
#define BC250_DCN_REG_HUBPREQ0_DCSURF_SURFACE_PITCH 0x0000EB1Cu /* (0x34C0 + 0x0607) * 4 */
#define BC250_DCN_REG_HUBPREQ0_PRIMARY_SURFACE_ADDR 0x0000EB28u /* (0x34C0 + 0x060A) * 4 */
#define BC250_DCN_REG_HUBPREQ0_PRIMARY_SURFACE_HI   0x0000EB2Cu /* (0x34C0 + 0x060B) * 4 */
#define BC250_DCN_REG_HUBPREQ0_DCSURF_SURFACE_INUSE 0x0000EB84u /* (0x34C0 + 0x0621) * 4 */
#define BC250_DCN_REG_HUBPREQ0_SURFACE_INUSE_HI     0x0000EB88u /* (0x34C0 + 0x0622) * 4 */

/* DP AUX0 Engine */
#define BC250_DCN_REG_DP_AUX0_AUX_CNTL              0x00004800u
#define BC250_DCN_REG_DP_AUX0_AUX_SW_DATA           0x00004804u
#define BC250_DCN_REG_DP_AUX0_AUX_LS_DATA           0x00004808u
#define BC250_DCN_REG_DP_AUX0_AUX_SW_STATUS         0x0000480Cu

/* ---------------------------------------------------------------------------
 * Estruturas de Resultado: Bateria A (Scanout & OTG Timings)
 * --------------------------------------------------------------------------- */
typedef struct _BC250_DCN_BATTERY_A_RESULT {
    bool Valid;
    uint64_t PrimarySurfaceAddress; /* Endereço físico base da VRAM em GDDR6 */
    uint64_t SurfaceInUseAddress;    /* Endereço ativo de scanout no hardware */
    uint32_t ViewportWidth;         /* Largura ativa (ex.: 2560) */
    uint32_t ViewportHeight;        /* Altura ativa (ex.: 1080) */
    uint32_t SurfacePitch;          /* Pitch do scanout em pixels/bytes */
    bool OtgEnabled;                /* OTG0_CONTROL bit 0 */
    uint32_t FrameCount;            /* OTG0_STATUS_FRAME_COUNT */
    uint32_t CurrentLine;           /* Linha atual da varredura */
    uint32_t CurrentPixel;          /* Pixel atual da linha */
    uint32_t HTotal;                /* H_TOTAL (pixels ativos + blanking) */
    uint32_t VTotal;                /* V_TOTAL (linhas ativas + blanking) */
    uint32_t HBlankStart;
    uint32_t HBlankEnd;
    uint32_t VBlankStart;
    uint32_t VBlankEnd;
    uint64_t CalculatedPixelRateHz; /* Pixel clock derivado em Hz */
    uint32_t CalculatedRefreshHz100;/* Taxa de atualização (ex.: 5613 = 56.13 Hz) */
} BC250_DCN_BATTERY_A_RESULT;

/* ---------------------------------------------------------------------------
 * Estruturas de Resultado: Bateria B (DPCD Link Status)
 * --------------------------------------------------------------------------- */
typedef struct _BC250_DCN_BATTERY_B_RESULT {
    bool Valid;
    uint8_t DpcdMajorRev;           /* ex.: 1 */
    uint8_t DpcdMinorRev;           /* ex.: 4 para DP 1.4, 2 para DP 1.2 */
    uint8_t MaxLinkRateCode;        /* 0x06=1.62G, 0x0A=2.7G, 0x14=5.4G, 0x1E=8.1G */
    uint32_t MaxLinkRateMbps;       /* ex.: 5400 para 5.4 Gbps HBR2 */
    uint8_t MaxLaneCount;           /* 1, 2 ou 4 */
    bool EnhancedFramingSupported;
    bool CrDoneLane0;               /* Clock Recovery Done Lane 0 */
    bool CrDoneLane1;               /* Clock Recovery Done Lane 1 */
    bool EqDoneLane0;               /* Channel EQ Done Lane 0 */
    bool EqDoneLane1;               /* Channel EQ Done Lane 1 */
    uint32_t AuxStatusRaw;          /* Leitura crua do registrador de status AUX */
} BC250_DCN_BATTERY_B_RESULT;

/* ---------------------------------------------------------------------------
 * Estruturas de Resultado: Bateria C (EDID Real via I2C-over-AUX)
 * --------------------------------------------------------------------------- */
typedef struct _BC250_DCN_BATTERY_C_RESULT {
    bool Valid;
    bool HeaderValid;               /* 00 FF FF FF FF FF FF 00 */
    bool ChecksumValid;             /* Soma módulo 256 == 0 */
    char ManufacturerId[4];         /* 3 letras ASCII (ex.: "GSM", "AOC", "SAM") */
    uint16_t ProductCode;           /* Código do modelo em hex */
    uint32_t SerialNumber;          /* Número de série de 32 bits */
    uint8_t ManufactureWeek;
    uint16_t ManufactureYear;
    char MonitorName[14];           /* Nome extraído da tag 0xFC (ex.: "LG UltraWide") */
    uint32_t PreferredWidth;        /* Resolução horizontal preferida do DTD 1 */
    uint32_t PreferredHeight;       /* Resolução vertical preferida do DTD 1 */
    uint32_t PreferredRefreshHz100; /* Taxa de atualização preferida (ex.: 6000 = 60.00 Hz) */
    uint32_t PreferredPixelClockKhz;/* Pixel clock do descritor detalhado em kHz */
    uint8_t ExtensionBlockCount;    /* Quantidade de blocos CEA-861 adicionais */
} BC250_DCN_BATTERY_C_RESULT;

/* ---------------------------------------------------------------------------
 * Funções Puras de Decodificação (Testáveis Offline)
 * --------------------------------------------------------------------------- */

/* Decodifica os registradores lidos na Bateria A */
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
    BC250_DCN_BATTERY_A_RESULT *OutResult);

/* Decodifica os bytes crus de DPCD obtidos na Bateria B */
bool Bc250DcnDecodeBatteryB(
    const uint8_t *DpcdBuffer,
    size_t DpcdLen,
    uint32_t AuxStatusRaw,
    BC250_DCN_BATTERY_B_RESULT *OutResult);

/* Decodifica os 128 bytes crus de EDID Block 0 obtidos na Bateria C */
bool Bc250DcnDecodeBatteryC(
    const uint8_t *EdidBuffer,
    size_t EdidLen,
    BC250_DCN_BATTERY_C_RESULT *OutResult);

#ifdef __cplusplus
}
#endif

#endif /* BC250_DCN_PROBE_CONTRACT_H */
