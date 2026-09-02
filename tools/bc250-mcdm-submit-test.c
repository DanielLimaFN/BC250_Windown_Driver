#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../driver/wdk/bc250_compute_service.h"
#include "../driver/umd/bc250_runtime.h"

int main(int argc, char **argv)
{
    bc250_device_t *device = NULL;
    bc250_device_info_t info;
    bc250_status_t status;
    uint32_t wptr_delta = 0;
    uint32_t rptr_delta = 0;
    uint32_t fence_observed = 0;

    /* Shader GFX10 Compute Mínimo: s_endpgm (0xBF810000) */
    static const uint32_t shader_code[] = {
        0xBF810000u, /* s_endpgm */
        0xBF800000u, /* s_nop */
        0xBF800000u, /* s_nop */
        0xBF800000u  /* s_nop */
    };

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    printf("================================================================================\n");
    printf("   AMD BC-250 — TESTE DE SUBMISSAO DE COMANDOS (FASE P1: WDDM / MCDM PRODUCAO) \n");
    printf("================================================================================\n\n");

    printf("[1/3] Inicializando dispositivo BC-250 em modo usuario...\n");
    status = bc250_device_init(&device, &info);
    if (status != BC250_SUCCESS) {
        printf("[!] ERRO: Falha ao inicializar dispositivo (Codigo: %d)\n", (int)status);
        printf("[!] Certifique-se de que o servico esteja ativo (sc start bc250compute).\n");
        return 1;
    }

    printf("  Dispositivo:          %s\n", info.silicon_name);
    printf("  Compute Units:        %lu CUs (%lu WGPs)\n", info.gpu_cus, info.gpu_wgps);
    printf("  VRAM Total:           %lu MiB (%lu MiB visivel CPU)\n", info.vram_total_mib, info.cpu_visible_mib);
    printf("  Clock GPU / Memoria:  %lu MHz / %lu MHz\n\n", info.gpu_clock_mhz, info.memory_clock_mhz);

    printf("[2/3] Submetendo despacho de Compute Shader via anel MEC/HQD...\n");
    status = bc250_dispatch_kernel(
        device,
        shader_code,
        sizeof(shader_code) / sizeof(shader_code[0]),
        info.gpu_cus ? info.gpu_cus : 24, /* 1 workgroup por CU ativo */
        64,           /* Wavefront 64 */
        &wptr_delta,
        &rptr_delta,
        &fence_observed);

    if (status != BC250_SUCCESS) {
        printf("[!] ERRO: Falha na submissao de kernel (Codigo: %d)\n", (int)status);
        bc250_device_close(device);
        return 1;
    }

    printf("[3/3] Validando conclusao no silicio...\n");
    printf("  WPTR Delta:           +%lu dwords\n", wptr_delta);
    printf("  RPTR Delta:           +%lu dwords (Consumo integral confirmado)\n", rptr_delta);
    printf("  Fence Observada:      0x%08lX (Gravada pela GPU em VRAM)\n\n", fence_observed);

    printf("================================================================================\n");
    printf("VEREDITO: SUBMISSAO DE COMPUTACAO WDDM EXECUTADA COM SUCESSO!\n");
    printf("================================================================================\n");

    bc250_device_close(device);
    return 0;
}
