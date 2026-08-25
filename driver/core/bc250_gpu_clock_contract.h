#ifndef BC250_GPU_CLOCK_CONTRACT_H
#define BC250_GPU_CLOCK_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_adapter_info_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GPU_CLOCK_FREQUENCY_HZ 100000000ull /* 100 MHz reference timestamp counter */

typedef enum BC250_GPU_CLOCK_STATUS {
    BC250_GPU_CLOCK_OK = 0,
    BC250_GPU_CLOCK_BAD_ARGUMENT,
    BC250_GPU_CLOCK_INVALID_NODE,
    BC250_GPU_CLOCK_READ_GLITCH
} BC250_GPU_CLOCK_STATUS;

typedef struct BC250_GPU_CLOCK_CALIBRATION {
    uint32_t NodeIndex;
    uint32_t EngineIndex;
    uint64_t GpuTimestamp;
    uint64_t CpuTimestamp;
    uint64_t GpuFrequencyHz;
    uint64_t CpuFrequencyHz;
    uint32_t JitterDelta;
    uint32_t IsValid;
} BC250_GPU_CLOCK_CALIBRATION;

BC250_GPU_CLOCK_STATUS Bc250CalibrateGpuClock(
    uint32_t NodeIndex,
    uint32_t EngineIndex,
    uint32_t RawGpuLo,
    uint32_t RawGpuHi,
    uint64_t RawCpuCounter,
    uint64_t CpuFreqHz,
    BC250_GPU_CLOCK_CALIBRATION *Calibration);

const char *Bc250GpuClockStatusName(BC250_GPU_CLOCK_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
