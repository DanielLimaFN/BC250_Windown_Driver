#ifndef BC250_RUNTIME_H
#define BC250_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(BC250_RUNTIME_EXPORTS)
    #define BC250_API __declspec(dllexport)
  #elif defined(BC250_RUNTIME_STATIC)
    #define BC250_API
  #else
    #define BC250_API __declspec(dllimport)
  #endif
#else
  #if __GNUC__ >= 4
    #define BC250_API __attribute__((visibility("default")))
  #else
    #define BC250_API
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bc250_status {
    BC250_SUCCESS = 0,
    BC250_ERROR_DEVICE_NOT_FOUND,
    BC250_ERROR_INVALID_ARGUMENT,
    BC250_ERROR_OUT_OF_MEMORY,
    BC250_ERROR_EXECUTION_FAILED,
    BC250_ERROR_ISA_VALIDATION_FAILED,
    BC250_ERROR_TIMEOUT,
    BC250_ERROR_NOT_SUPPORTED
} bc250_status_t;

typedef struct bc250_device_info {
    char     silicon_name[32];
    uint32_t cpu_cores;
    uint32_t gpu_cus;           /* Contagem real dinâmica de CUs ativas (24) */
    uint32_t gpu_wgps;          /* Contagem real dinâmica de WGPs ativos (12) */
    uint32_t vram_total_mib;
    uint32_t cpu_visible_mib;
    uint32_t rebar_size_mib;
    uint32_t gpu_clock_mhz;
    uint32_t memory_clock_mhz;
    uint32_t gfxoff_state;
} bc250_device_info_t;

typedef struct bc250_telemetry_data {
    uint32_t smu_fw_version;
    uint32_t gfxclk_mhz;
    uint32_t gfx_voltage_mv;
    uint32_t motherboard_temp_c;
    uint32_t fan_rpm;
    uint32_t valid;
} bc250_telemetry_data_t;

typedef struct bc250_device bc250_device_t;

BC250_API bc250_status_t bc250_device_init(bc250_device_t **device, bc250_device_info_t *info);
BC250_API void bc250_device_close(bc250_device_t *device);

/* Fail-closed until allocations are owned VidMm leases with real free. */
BC250_API bc250_status_t bc250_mem_alloc(bc250_device_t *device, size_t bytes, uint64_t *gpu_va);
BC250_API bc250_status_t bc250_mem_free(bc250_device_t *device, uint64_t gpu_va, size_t bytes);

/* Fail-closed until pinned staging + a physical SDMA submission backend exist. */
BC250_API bc250_status_t bc250_mem_copy_h2d(bc250_device_t *device, uint64_t dst_gpu_va, const void *src_host, size_t bytes);
BC250_API bc250_status_t bc250_mem_copy_d2h(bc250_device_t *device, void *dst_host, uint64_t src_gpu_va, size_t bytes);
BC250_API bc250_status_t bc250_mem_copy_d2d(bc250_device_t *device, uint64_t dst_gpu_va, uint64_t src_gpu_va, size_t bytes);

BC250_API bc250_status_t bc250_dispatch_kernel(
    bc250_device_t *device,
    const uint32_t *shader_dwords,
    size_t dword_count,
    uint32_t grid_x,
    uint32_t block_x,
    uint32_t *wptr_delta,
    uint32_t *rptr_delta,
    uint32_t *fence_observed);

/* Fail-closed: the current kernel request has no field carrying ib_gpu_va. */
BC250_API bc250_status_t bc250_submit_ib(
    bc250_device_t *device,
    uint64_t ib_gpu_va,
    uint32_t ib_dwords,
    uint32_t *fence_assigned,
    uint32_t *fence_retired);

/* Fail-closed until a dedicated monitored-fence query/wait ABI exists. */
BC250_API bc250_status_t bc250_poll_fence(
    bc250_device_t *device,
    uint32_t fence_expected,
    uint32_t timeout_ms);

BC250_API bc250_status_t bc250_get_telemetry(
    bc250_device_t *device,
    bc250_telemetry_data_t *telemetry);

#ifdef __cplusplus
}
#endif

#endif /* BC250_RUNTIME_H */
