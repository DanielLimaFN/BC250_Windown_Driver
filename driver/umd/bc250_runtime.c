#include "bc250_runtime.h"
#include "../wdk/bc250_compute_service.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct bc250_device {
    HANDLE handle;
    bc250_device_info_t info;
    uint32_t fence_counter;
};

bc250_status_t bc250_device_init(bc250_device_t **device, bc250_device_info_t *info)
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
    if (!DeviceIoControl(hDevice, IOCTL_BC250_GET_HARDWARE_INFO, NULL, 0,
                         &hwInfo, sizeof(hwInfo), &bytesReturned, NULL)) {
        CloseHandle(hDevice);
        return BC250_ERROR_DEVICE_NOT_FOUND;
    }

    dev = (bc250_device_t *)malloc(sizeof(bc250_device_t));
    if (dev == NULL) {
        CloseHandle(hDevice);
        return BC250_ERROR_OUT_OF_MEMORY;
    }

    dev->handle = hDevice;
    dev->fence_counter = 1;
    strncpy_s(dev->info.silicon_name, sizeof(dev->info.silicon_name), hwInfo.SiliconName, _TRUNCATE);
    dev->info.cpu_cores = hwInfo.CpuCoreCount;
    dev->info.gpu_cus = hwInfo.GpuCuCount;       /* Detectado dinamicamente (24/28/32/36/40) */
    dev->info.gpu_wgps = hwInfo.GpuWgpCount;
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

void bc250_device_close(bc250_device_t *device)
{
    if (device != NULL) {
        if (device->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(device->handle);
        }
        free(device);
    }
}

bc250_status_t bc250_mem_alloc(bc250_device_t *device, size_t bytes, uint64_t *gpu_va)
{
    BC250_ALLOC_VRAM_REQ req;
    DWORD bytesReturned = 0;

    if (device == NULL || gpu_va == NULL || bytes == 0) return BC250_ERROR_INVALID_ARGUMENT;

    ZeroMemory(&req, sizeof(req));
    req.SizeBytes = bytes;
    req.Alignment = 4096;

    if (DeviceIoControl(device->handle, IOCTL_BC250_ALLOC_VRAM, &req, sizeof(req),
                        &req, sizeof(req), &bytesReturned, NULL) && req.Success) {
        *gpu_va = req.AllocatedGpuVa;
        return BC250_SUCCESS;
    }
    return BC250_ERROR_OUT_OF_MEMORY;
}

bc250_status_t bc250_mem_free(bc250_device_t *device, uint64_t gpu_va, size_t bytes)
{
    BC250_FREE_VRAM_REQ req;
    DWORD bytesReturned = 0;

    if (device == NULL || gpu_va == 0) return BC250_ERROR_INVALID_ARGUMENT;

    ZeroMemory(&req, sizeof(req));
    req.GpuVa = gpu_va;
    req.SizeBytes = bytes;

    if (DeviceIoControl(device->handle, IOCTL_BC250_FREE_VRAM, &req, sizeof(req),
                        &req, sizeof(req), &bytesReturned, NULL) && req.Success) {
        return BC250_SUCCESS;
    }
    return BC250_ERROR_INVALID_ARGUMENT;
}

bc250_status_t bc250_mem_copy_h2d(bc250_device_t *device, uint64_t dst_gpu_va, const void *src_host, size_t bytes)
{
    BC250_SDMA_COPY_REQ req;
    DWORD bytesReturned = 0;

    if (device == NULL || src_host == NULL || dst_gpu_va == 0 || bytes == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }

    ZeroMemory(&req, sizeof(req));
    req.SourceGpuVa = (uint64_t)(uintptr_t)src_host;
    req.DestinationGpuVa = dst_gpu_va;
    req.ByteCount = (uint32_t)bytes;
    req.FenceValue = ++device->fence_counter;

    if (DeviceIoControl(device->handle, IOCTL_BC250_SDMA_COPY, &req, sizeof(req),
                        &req, sizeof(req), &bytesReturned, NULL) && req.ExecutionSuccess) {
        return BC250_SUCCESS;
    }
    return BC250_ERROR_EXECUTION_FAILED;
}

bc250_status_t bc250_mem_copy_d2h(bc250_device_t *device, void *dst_host, uint64_t src_gpu_va, size_t bytes)
{
    BC250_SDMA_COPY_REQ req;
    DWORD bytesReturned = 0;

    if (device == NULL || dst_host == NULL || src_gpu_va == 0 || bytes == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }

    ZeroMemory(&req, sizeof(req));
    req.SourceGpuVa = src_gpu_va;
    req.DestinationGpuVa = (uint64_t)(uintptr_t)dst_host;
    req.ByteCount = (uint32_t)bytes;
    req.FenceValue = ++device->fence_counter;

    if (DeviceIoControl(device->handle, IOCTL_BC250_SDMA_COPY, &req, sizeof(req),
                        &req, sizeof(req), &bytesReturned, NULL) && req.ExecutionSuccess) {
        return BC250_SUCCESS;
    }
    return BC250_ERROR_EXECUTION_FAILED;
}

bc250_status_t bc250_dispatch_kernel(
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
    size_t copyDwords;

    if (device == NULL || shader_dwords == NULL || dword_count == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }

    ZeroMemory(&req, sizeof(req));
    copyDwords = dword_count < 256 ? dword_count : 256;
    memcpy(req.ShaderCode, shader_dwords, copyDwords * sizeof(uint32_t));
    req.ShaderCodeSize = (uint32_t)(copyDwords * sizeof(uint32_t));
    req.WorkgroupSizeX = block_x ? block_x : 64;
    req.GridDimX = grid_x ? grid_x : device->info.gpu_cus; /* Distribui pelos CUs dinâmicos */
    req.FenceValue = ++device->fence_counter;

    if (DeviceIoControl(device->handle, IOCTL_BC250_DISPATCH_COMPUTE, &req, sizeof(req),
                        &req, sizeof(req), &bytesReturned, NULL)) {
        if (!req.IsaValidationPassed) {
            return BC250_ERROR_ISA_VALIDATION_FAILED;
        }
        if (req.ExecutionSuccess) {
            if (wptr_delta) *wptr_delta = req.WptrAfter - req.WptrBefore;
            if (rptr_delta) *rptr_delta = req.RptrAfter - req.RptrBefore;
            if (fence_observed) *fence_observed = req.FenceObserved;
            return BC250_SUCCESS;
        }
    }
    return BC250_ERROR_EXECUTION_FAILED;
}
