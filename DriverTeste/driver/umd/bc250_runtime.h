#ifndef BC250_RUNTIME_H
#define BC250_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bc250_status {
    BC250_SUCCESS = 0,
    BC250_ERROR_DEVICE_NOT_FOUND,
    BC250_ERROR_INVALID_ARGUMENT,
    BC250_ERROR_OUT_OF_MEMORY,
    BC250_ERROR_EXECUTION_FAILED,
    BC250_ERROR_ISA_VALIDATION_FAILED
} bc250_status_t;

typedef struct bc250_device_info {
    char     silicon_name[32];
    uint32_t cpu_cores;
    uint32_t gpu_cus;           /* Contagem real dinâmica de CUs ativas */
    uint32_t gpu_wgps;          /* Contagem real dinâmica de WGPs ativos */
    uint32_t vram_total_mib;
    uint32_t cpu_visible_mib;
    uint32_t rebar_size_mib;
    uint32_t gpu_clock_mhz;
    uint32_t memory_clock_mhz;
    uint32_t gfxoff_state;
} bc250_device_info_t;

typedef struct bc250_device bc250_device_t;

bc250_status_t bc250_device_init(bc250_device_t **device, bc250_device_info_t *info);
void bc250_device_close(bc250_device_t *device);

bc250_status_t bc250_mem_alloc(bc250_device_t *device, size_t bytes, uint64_t *gpu_va);
bc250_status_t bc250_mem_free(bc250_device_t *device, uint64_t gpu_va, size_t bytes);

bc250_status_t bc250_mem_copy_h2d(bc250_device_t *device, uint64_t dst_gpu_va, const void *src_host, size_t bytes);
bc250_status_t bc250_mem_copy_d2h(bc250_device_t *device, void *dst_host, uint64_t src_gpu_va, size_t bytes);

bc250_status_t bc250_dispatch_kernel(
    bc250_device_t *device,
    const uint32_t *shader_dwords,
    size_t dword_count,
    uint32_t grid_x,
    uint32_t block_x,
    uint32_t *wptr_delta,
    uint32_t *rptr_delta,
    uint32_t *fence_observed);

#ifdef __cplusplus
}
#endif

#endif
