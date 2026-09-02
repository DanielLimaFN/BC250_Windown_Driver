#ifndef BC250_RUNTIME_EXPORTS
#define BC250_RUNTIME_EXPORTS
#endif

#include "bc250_runtime.h"
#include "../wdk/bc250_compute_service.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct bc250_device {
    HANDLE handle;
    bc250_device_info_t info;
    uint32_t fence_counter;
    CRITICAL_SECTION lock;
};

BC250_API bc250_status_t bc250_device_init(bc250_device_t **device, bc250_device_info_t *info)
{
    HANDLE hDevice;
    BC250_HARDWARE_INFO hwInfo;
    DWORD bytesReturned = 0;
    bc250_device_t *dev;

    if (device == NULL) return BC250_ERROR_INVALID_ARGUMENT;
    *device = NULL;

    hDevice = CreateFileW(
        L"\\\\.\\Bc250Compute",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        return BC250_ERROR_DEVICE_NOT_FOUND;
    }

    ZeroMemory(&hwInfo, sizeof(hwInfo));
    if (!DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_GET_HARDWARE_INFO, NULL, 0,
                         &hwInfo, sizeof(hwInfo), &bytesReturned, NULL)) {
        CloseHandle(hDevice);
        return BC250_ERROR_DEVICE_NOT_FOUND;
    }

    dev = (bc250_device_t *)malloc(sizeof(bc250_device_t));
    if (dev == NULL) {
        CloseHandle(hDevice);
        return BC250_ERROR_OUT_OF_MEMORY;
    }

    InitializeCriticalSection(&dev->lock);
    dev->handle = hDevice;
    dev->fence_counter = 1;
    strncpy_s(dev->info.silicon_name, sizeof(dev->info.silicon_name), hwInfo.SiliconName, _TRUNCATE);
    dev->info.cpu_cores = hwInfo.CpuCoreCount;
    dev->info.gpu_cus = hwInfo.GpuCuCount;       /* 24 CUs ativas */
    dev->info.gpu_wgps = hwInfo.GpuWgpCount;     /* 12 WGPs */
    dev->info.vram_total_mib = hwInfo.VramTotalMiB;
    dev->info.cpu_visible_mib = hwInfo.CpuVisibleMiB;
    dev->info.rebar_size_mib = hwInfo.RebarSelectedMiB;
    dev->info.gpu_clock_mhz = hwInfo.GpuClockMhz;
    dev->info.memory_clock_mhz = hwInfo.MemoryClockMhz;
    dev->info.gfxoff_state = hwInfo.GfxoffState;

    if (info != NULL) {
        *info = dev->info;
    }

    *device = dev;
    return BC250_SUCCESS;
}

BC250_API void bc250_device_close(bc250_device_t *device)
{
    if (device != NULL) {
        EnterCriticalSection(&device->lock);
        if (device->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(device->handle);
            device->handle = INVALID_HANDLE_VALUE;
        }
        LeaveCriticalSection(&device->lock);
        DeleteCriticalSection(&device->lock);
        free(device);
    }
}

BC250_API bc250_status_t bc250_mem_alloc(bc250_device_t *device, size_t bytes, uint64_t *gpu_va)
{
    if (device == NULL || gpu_va == NULL || bytes == 0) return BC250_ERROR_INVALID_ARGUMENT;
    *gpu_va = 0;
    /* The legacy service is a wrapping bump allocator and FREE_VRAM does not
     * own or release a lease.  Do not expose it as production allocation. */
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_mem_free(bc250_device_t *device, uint64_t gpu_va, size_t bytes)
{
    if (device == NULL || gpu_va == 0) return BC250_ERROR_INVALID_ARGUMENT;
    UNREFERENCED_PARAMETER(bytes);
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_mem_copy_h2d(bc250_device_t *device, uint64_t dst_gpu_va, const void *src_host, size_t bytes)
{
    if (device == NULL || src_host == NULL || dst_gpu_va == 0 || bytes == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    /* Fail closed: the legacy IOCTL treats a user pointer as a GPU address and
     * completes H2D with a kernel CPU copy.  It is not a pinned/mapped SDMA
     * transfer and must not be exposed as GPU acceleration. */
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_mem_copy_d2h(bc250_device_t *device, void *dst_host, uint64_t src_gpu_va, size_t bytes)
{
    if (device == NULL || dst_host == NULL || src_gpu_va == 0 || bytes == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_mem_copy_d2d(bc250_device_t *device, uint64_t dst_gpu_va, uint64_t src_gpu_va, size_t bytes)
{
    if (device == NULL || dst_gpu_va == 0 || src_gpu_va == 0 || bytes == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    /* The service currently implements this branch with CPU MMIO mappings,
     * not a submitted SDMA packet. */
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_dispatch_kernel(
    bc250_device_t *device,
    const uint32_t *shader_dwords,
    size_t dword_count,
    uint32_t grid_x,
    uint32_t block_x,
    uint32_t *wptr_delta,
    uint32_t *rptr_delta,
    uint32_t *fence_observed)
{
    BC250_COMPUTE_DISPATCH_REQ req;
    DWORD bytesReturned = 0;

    if (device == NULL || shader_dwords == NULL || dword_count == 0 || dword_count > 256) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }

    ZeroMemory(&req, sizeof(req));
    req.ShaderCodeSize = (ULONG)dword_count;
    memcpy(req.ShaderCode, shader_dwords, dword_count * sizeof(uint32_t));
    req.WorkgroupSizeX = block_x;
    req.GridDimX = grid_x;
    req.GridDimY = 1;
    req.GridDimZ = 1;

    EnterCriticalSection(&device->lock);
    req.FenceValue = ++device->fence_counter;

    if (!DeviceIoControl(device->handle, (DWORD)IOCTL_BC250_DISPATCH_COMPUTE, &req, sizeof(req),
                         &req, sizeof(req), &bytesReturned, NULL)) {
        LeaveCriticalSection(&device->lock);
        return BC250_ERROR_EXECUTION_FAILED;
    }

    if (req.IsaValidationPassed == 0) {
        LeaveCriticalSection(&device->lock);
        return BC250_ERROR_ISA_VALIDATION_FAILED;
    }

    if (wptr_delta) *wptr_delta = req.WptrAfter - req.WptrBefore;
    if (rptr_delta) *rptr_delta = req.RptrAfter - req.RptrBefore;
    if (fence_observed) *fence_observed = req.FenceObserved;

    LeaveCriticalSection(&device->lock);
    return (req.ExecutionSuccess != 0) ? BC250_SUCCESS : BC250_ERROR_EXECUTION_FAILED;
}

BC250_API bc250_status_t bc250_submit_ib(
    bc250_device_t *device,
    uint64_t ib_gpu_va,
    uint32_t ib_dwords,
    uint32_t *fence_assigned,
    uint32_t *fence_retired)
{
    if (device == NULL || ib_gpu_va == 0 || ib_dwords == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    UNREFERENCED_PARAMETER(fence_assigned);
    UNREFERENCED_PARAMETER(fence_retired);
    /* BC250_COMPUTE_DISPATCH_REQ has no IB GPUVA field.  The old code silently
     * discarded ib_gpu_va and submitted zero-filled shader storage. */
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_poll_fence(
    bc250_device_t *device,
    uint32_t fence_expected,
    uint32_t timeout_ms)
{
    if (device == NULL) return BC250_ERROR_INVALID_ARGUMENT;
    UNREFERENCED_PARAMETER(fence_expected);
    UNREFERENCED_PARAMETER(timeout_ms);
    /* A fence query must never reuse the dispatch IOCTL: that can submit work.
     * This becomes supported with the monitored-fence ABI. */
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_get_telemetry(
    bc250_device_t *device,
    bc250_telemetry_data_t *telemetry)
{
    BC250_PLATFORM_TELEMETRY_SNAPSHOT snapshot;
    DWORD bytesReturned = 0;

    if (device == NULL || telemetry == NULL) return BC250_ERROR_INVALID_ARGUMENT;

    ZeroMemory(&snapshot, sizeof(snapshot));
    EnterCriticalSection(&device->lock);
    if (DeviceIoControl(device->handle, (DWORD)IOCTL_BC250_GET_TELEMETRY, NULL, 0,
                        &snapshot, sizeof(snapshot), &bytesReturned, NULL)) {
        telemetry->smu_fw_version = snapshot.GpuMetrics.SmuFirmwareVersion;
        telemetry->gfxclk_mhz = snapshot.GpuMetrics.GfxclkFrequencyMhz;
        telemetry->gfx_voltage_mv = snapshot.GpuMetrics.GfxVoltageMv;
        telemetry->motherboard_temp_c = (uint32_t)(snapshot.MotherboardMetrics.TempCpuC > 0 ? snapshot.MotherboardMetrics.TempCpuC : 0);
        telemetry->fan_rpm = (uint32_t)snapshot.MotherboardMetrics.FanRpm[0];
        telemetry->valid = snapshot.TelemetryHealthy;
        LeaveCriticalSection(&device->lock);
        return BC250_SUCCESS;
    }
    LeaveCriticalSection(&device->lock);
    return BC250_ERROR_EXECUTION_FAILED;
}
