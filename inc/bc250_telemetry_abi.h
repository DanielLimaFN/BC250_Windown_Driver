#ifndef BC250_TELEMETRY_ABI_H
#define BC250_TELEMETRY_ABI_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
typedef UINT16 uint16_t;
typedef UINT8  uint8_t;
typedef INT16  int16_t;
typedef INT8   int8_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * AMD BC-250 SMU v11.8 (Robin 5 / Cyan Skillfish 2) Telemetry ABI
 * ============================================================================
 */

/* SMU Mailbox Command / Response Register Offsets (MP1 SMN MMIO Aperture) */
#define BC250_SMU_MMIO_C2PMSG_66            0x0003B208u /* Response / Status Code */
#define BC250_SMU_MMIO_C2PMSG_67            0x0003B20Cu /* Message ID / Kick */
#define BC250_SMU_MMIO_C2PMSG_82            0x0003B248u /* Message Argument (32-bit) */

/* SMU Response Status Codes */
#define BC250_SMU_RESP_OK                   0x00000001u
#define BC250_SMU_RESP_BUSY                 0x000000FCu
#define BC250_SMU_RESP_GUARD_REJECT         0x000000FDu
#define BC250_SMU_RESP_CMD_REJECT           0x000000FEu
#define BC250_SMU_RESP_UNKNOWN              0x000000FFu

/* SMU Queue 0 Standard Message IDs (Robin 5) */
#define BC250_PPSMC_MSG_TestMessage                     0x01u
#define BC250_PPSMC_MSG_GetSmuVersion                   0x02u
#define BC250_PPSMC_MSG_GetDriverIfVersion              0x03u
#define BC250_PPSMC_MSG_SetDriverTableDramAddrHigh      0x04u
#define BC250_PPSMC_MSG_SetDriverTableDramAddrLow       0x05u
#define BC250_PPSMC_MSG_TransferTableSmu2Dram           0x06u
#define BC250_PPSMC_MSG_TransferTableDram2Smu           0x07u
#define BC250_PPSMC_MSG_RequestCorePstate               0x0Bu
#define BC250_PPSMC_MSG_QueryCorePstate                 0x0Cu
#define BC250_PPSMC_MSG_RequestGfxclk                   0x0Eu
#define BC250_PPSMC_MSG_QueryGfxclk                     0x0Fu
#define BC250_PPSMC_MSG_StartTelemetryReporting         0x1Bu
#define BC250_PPSMC_MSG_StopTelemetryReporting          0x1Cu
#define BC250_PPSMC_MSG_GetGfxFrequency                 0x37u
#define BC250_PPSMC_MSG_GetGfxVid                       0x38u
#define BC250_PPSMC_MSG_ForceGfxFreq                    0x39u
#define BC250_PPSMC_MSG_UnForceGfxFreq                  0x3Au
#define BC250_PPSMC_MSG_GetEnabledSmuFeatures           0x3Du

/* SMU Table IDs */
#define BC250_SMU_TABLE_METRICS                         0x06u

/*
 * Estrutura de Telemetria GPU consolidada (SMU)
 */
typedef struct BC250_SMU_GPU_METRICS {
    uint32_t GfxclkFrequencyMhz;
    uint32_t SocclkFrequencyMhz;
    uint32_t UclkFrequencyMhz;
    uint32_t GfxVoltageMv;
    uint32_t GfxTemperatureC;
    uint32_t SocTemperatureC;
    uint32_t GfxPowerWatts;
    uint32_t SocketPowerWatts;
    uint32_t ThrottlerStatus;
    uint32_t SmuFirmwareVersion;
    uint32_t DriverIfVersion;
    uint32_t Valid;
} BC250_SMU_GPU_METRICS;

/*
 * ============================================================================
 * Nuvoton NCT6686D Super I/O (ISA Base 0x0A20) Telemetry ABI
 * ============================================================================
 */

#define BC250_NCT6686_CONFIG_INDEX_PORT     0x002Eu
#define BC250_NCT6686_CONFIG_DATA_PORT      0x002Fu
#define BC250_NCT6686_DEFAULT_BASE_ADDR     0x0A20u
#define BC250_NCT6686_CHIP_ID_VAL           0xC451u
#define BC250_NCT6686_MAX_FAN_COUNT         8u

typedef struct BC250_SUPERIO_NCT6686_METRICS {
    uint16_t ChipId;
    uint16_t BaseAddress;
    int16_t  TempCpuC;
    int16_t  TempSystemC;
    int16_t  TempVrmMosC;        /* Sensor crucial para estabilidade do BC-250 */
    uint16_t Voltage12VMv;
    uint16_t Voltage5VMv;
    uint16_t Voltage33VMv;
    uint16_t VoltageVcoreMv;
    uint16_t VoltageSocMv;
    uint16_t FanRpm[BC250_NCT6686_MAX_FAN_COUNT];
    uint8_t  FanPwm[BC250_NCT6686_MAX_FAN_COUNT];
    uint8_t  FanCount;
    uint8_t  Valid;
} BC250_SUPERIO_NCT6686_METRICS;

/*
 * ============================================================================
 * Snapshot Consolidado de Telemetria da Plataforma BC-250
 * ============================================================================
 */
typedef struct BC250_PLATFORM_TELEMETRY_SNAPSHOT {
    uint64_t TimestampQpc;
    BC250_SMU_GPU_METRICS GpuMetrics;
    BC250_SUPERIO_NCT6686_METRICS MotherboardMetrics;
    uint32_t GfxOffStatus;
    uint32_t TelemetryHealthy;
} BC250_PLATFORM_TELEMETRY_SNAPSHOT;

#ifdef __cplusplus
}
#endif

#endif /* BC250_TELEMETRY_ABI_H */
