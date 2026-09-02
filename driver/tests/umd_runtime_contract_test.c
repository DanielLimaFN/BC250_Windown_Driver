#include "../umd/bc250_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FALHA: %s (linha %d)\n", #cond, __LINE__); \
        g_failed = 1; \
    } \
} while(0)

int main(void)
{
    bc250_device_t *device = NULL;
    bc250_device_info_t info;
    uint64_t gpuVa = 0;
    size_t devInfoSize = sizeof(bc250_device_info_t);
    size_t telemSize = sizeof(bc250_telemetry_data_t);

    printf("=== TESTE DE CONTRATO UMD RUNTIME (BC250_RUNTIME_API) ===\n");

    /* 1. Validacao de argumentos nulos (Fail-Closed) */
    CHECK(bc250_device_init(NULL, NULL) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_mem_alloc(NULL, 4096, &gpuVa) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_mem_alloc(device, 0, &gpuVa) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_mem_free(NULL, 0x1000, 4096) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_mem_copy_h2d(NULL, 0x1000, &info, 4096) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_mem_copy_d2h(NULL, &info, 0x1000, 4096) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_mem_copy_d2d(NULL, 0x2000, 0x1000, 4096) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_dispatch_kernel(NULL, NULL, 0, 0, 0, NULL, NULL, NULL) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_submit_ib(NULL, 0, 0, NULL, NULL) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_poll_fence(NULL, 1, 10) == BC250_ERROR_INVALID_ARGUMENT);
    CHECK(bc250_get_telemetry(NULL, NULL) == BC250_ERROR_INVALID_ARGUMENT);

    /* These public operations must remain fail-closed even with a non-null
     * opaque handle: their production GPU backends do not exist yet. */
    device = (bc250_device_t *)(uintptr_t)1;
    CHECK(bc250_mem_alloc(device, 4096, &gpuVa) == BC250_ERROR_NOT_SUPPORTED);
    CHECK(gpuVa == 0);
    CHECK(bc250_mem_free(device, 0xF400100000ull, 4096) == BC250_ERROR_NOT_SUPPORTED);
    CHECK(bc250_mem_copy_h2d(device, 0xF400100000ull, &info, sizeof(info)) == BC250_ERROR_NOT_SUPPORTED);
    CHECK(bc250_mem_copy_d2h(device, &info, 0xF400100000ull, sizeof(info)) == BC250_ERROR_NOT_SUPPORTED);
    CHECK(bc250_mem_copy_d2d(device, 0xF400200000ull, 0xF400100000ull, 4096) == BC250_ERROR_NOT_SUPPORTED);
    CHECK(bc250_submit_ib(device, 0x10000ull, 16, NULL, NULL) == BC250_ERROR_NOT_SUPPORTED);
    CHECK(bc250_poll_fence(device, 1, 10) == BC250_ERROR_NOT_SUPPORTED);
    device = NULL;

    /* 2. Fechamento seguro de ponteiro nulo (No-Op seguro) */
    bc250_device_close(NULL);

    /* 3. Verificacao de alinhamentos e tamanhos de ABI */
    CHECK(devInfoSize >= 48);
    CHECK(telemSize >= 20);

    if (g_failed == 0) {
        printf("UMD RUNTIME CONTRACT: OK (21/21 invariantes de seguranca validadas)\n");
        return 0;
    } else {
        printf("UMD RUNTIME CONTRACT: FALHA\n");
        return 1;
    }
}
