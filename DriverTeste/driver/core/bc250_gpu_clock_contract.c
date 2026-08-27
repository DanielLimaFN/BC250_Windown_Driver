#include "bc250_gpu_clock_contract.h"

#include <string.h>

BC250_GPU_CLOCK_STATUS Bc250CalibrateGpuClock(
    uint32_t NodeIndex,
    uint32_t EngineIndex,
    uint32_t RawGpuLo,
    uint32_t RawGpuHi,
    uint64_t RawCpuCounter,
    uint64_t CpuFreqHz,
    BC250_GPU_CLOCK_CALIBRATION *Calibration)
{
    if (Calibration == NULL) {
        return BC250_GPU_CLOCK_BAD_ARGUMENT;
    }
    if (NodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return BC250_GPU_CLOCK_INVALID_NODE;
    }
    if (RawGpuLo == 0xFFFFFFFFu && RawGpuHi == 0xFFFFFFFFu) {
        return BC250_GPU_CLOCK_READ_GLITCH;
    }

    memset(Calibration, 0, sizeof(BC250_GPU_CLOCK_CALIBRATION));
    Calibration->NodeIndex = NodeIndex;
    Calibration->EngineIndex = EngineIndex;
    Calibration->GpuTimestamp = ((uint64_t)RawGpuHi << 32) | (uint64_t)RawGpuLo;
    Calibration->CpuTimestamp = RawCpuCounter;
    Calibration->GpuFrequencyHz = BC250_GPU_CLOCK_FREQUENCY_HZ;
    Calibration->CpuFrequencyHz = (CpuFreqHz != 0) ? CpuFreqHz : 10000000ull;
    Calibration->JitterDelta = 0u;
    Calibration->IsValid = 1u;

    return BC250_GPU_CLOCK_OK;
}

const char *Bc250GpuClockStatusName(BC250_GPU_CLOCK_STATUS Status)
{
    switch (Status) {
    case BC250_GPU_CLOCK_OK: return "ok";
    case BC250_GPU_CLOCK_BAD_ARGUMENT: return "bad_argument";
    case BC250_GPU_CLOCK_INVALID_NODE: return "invalid_node";
    case BC250_GPU_CLOCK_READ_GLITCH: return "read_glitch";
    default: return "unknown";
    }
}
