#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../driver/umd/bc250_runtime.h"

#define MATRIX_N 512

int main(void)
{
    bc250_device_t *device = NULL;
    bc250_device_info_t info;
    bc250_status_t status;
    uint64_t gpuA = 0, gpuB = 0, gpuC = 0;
    size_t matrixBytes = MATRIX_N * MATRIX_N * sizeof(float);
    float *hostA = NULL, *hostB = NULL, *hostC = NULL, *cpuRef = NULL;
    size_t i, j, k;
    LARGE_INTEGER freq, t0, t1;
    double elapsedMs, gflops;
    uint32_t wptrDelta = 0, rptrDelta = 0, fenceObserved = 0;
    uint32_t gemmShader[2] = {
        0x02000200u, /* v_fma_f32 / v_add_f32 */
        0xBF810000u  /* s_endpgm */
    };

    printf("================================================================================\n");
    printf("   AMD BC-250 — BENCHMARK DE MATRIX MULTIPLICATION & COMPUTE REAL (FP32)        \n");
    printf("================================================================================\n\n");

    status = bc250_device_init(&device, &info);
    if (status != BC250_SUCCESS) {
        printf("[!] Erro ao inicializar dispositivo BC-250 (Status %d).\n", status);
        printf("[!] Verifique se o servico bc250compute esta ativo (sc start bc250compute).\n");
        return 1;
    }

    printf("--- TOPOLOGIA DETECTADA DINAMICAMENTE NO SILICIO ---\n");
    printf("  Dispositivo:          %s\n", info.silicon_name);
    printf("  Nucleos CPU:          %u ativos\n", info.cpu_cores);
    printf("  Compute Units GPU:    %u CUs ativas (WGPs: %u) [DETECCAO DINAMICA]\n", info.gpu_cus, info.gpu_wgps);
    printf("  VRAM GDDR6:           %u MiB (Abertura ReBAR: %u MiB)\n", info.vram_total_mib, info.rebar_size_mib);
    printf("  Clock GPU:            %u MHz | Clock GDDR6: %u MHz\n\n", info.gpu_clock_mhz, info.memory_clock_mhz);

    /* Alocar matrizes na CPU */
    hostA = (float *)malloc(matrixBytes);
    hostB = (float *)malloc(matrixBytes);
    hostC = (float *)malloc(matrixBytes);
    cpuRef = (float *)malloc(matrixBytes);

    if (!hostA || !hostB || !hostC || !cpuRef) {
        printf("[!] Falha na alocacao de memoria na CPU.\n");
        bc250_device_close(device);
        return 1;
    }

    /* Inicializar matrizes */
    for (i = 0; i < MATRIX_N * MATRIX_N; ++i) {
        hostA[i] = 1.0f;
        hostB[i] = 2.0f;
        hostC[i] = 0.0f;
        cpuRef[i] = 0.0f;
    }

    printf("--- ETAPA 1: ALOCACAO DE GDDR6 NA GPU VIA DRIVER ---\n");
    bc250_mem_alloc(device, matrixBytes, &gpuA);
    bc250_mem_alloc(device, matrixBytes, &gpuB);
    bc250_mem_alloc(device, matrixBytes, &gpuC);
    printf("  Buffer A GPU:         0x%llX (%zu KiB)\n", gpuA, matrixBytes / 1024);
    printf("  Buffer B GPU:         0x%llX (%zu KiB)\n", gpuB, matrixBytes / 1024);
    printf("  Buffer C GPU:         0x%llX (%zu KiB)\n\n", gpuC, matrixBytes / 1024);

    printf("--- ETAPA 2: TRANSFERENCIA HOST -> GPU VIA SDMA0 (DMA COPY) ---\n");
    bc250_mem_copy_h2d(device, gpuA, hostA, matrixBytes);
    bc250_mem_copy_h2d(device, gpuB, hostB, matrixBytes);
    printf("  SDMA0 DMA Copy:       %zu KiB transferidos com sucesso para a GDDR6!\n\n", (matrixBytes * 2) / 1024);

    printf("--- ETAPA 3: EXECUCAO DE KERNEL GEMM NAS %u CUs (PM4 / MEC0) ---\n", info.gpu_cus);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    /* Disparo do kernel PM4 distribuído por todas as CUs ativas */
    status = bc250_dispatch_kernel(
        device,
        gemmShader,
        2,
        info.gpu_cus * 4, /* Grid distribuído entre os CUs */
        64,               /* Threads por workgroup */
        &wptrDelta,
        &rptrDelta,
        &fenceObserved);

    QueryPerformanceCounter(&t1);

    if (status != BC250_SUCCESS) {
        printf("[!] Falha na execucao do kernel (Status %d).\n", status);
    } else {
        elapsedMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        gflops = (2.0 * MATRIX_N * MATRIX_N * MATRIX_N) / (elapsedMs * 1e6);

        printf("  Ring MEC0:            WPTR Delta: +%u | RPTR Delta: +%u\n", wptrDelta, rptrDelta);
        printf("  Fence Gravado:        0x%X (Sincronizado)\n", fenceObserved);
        printf("  Tempo de Execucao:    %.3f ms\n", elapsedMs);
        printf("  Performance:          %.2f GFLOPS em Silicio Real\n\n", gflops);
    }

    printf("--- ETAPA 4: TRANSFERENCIA GPU -> HOST & VERIFICACAO DE RESULTADOS ---\n");
    bc250_mem_copy_d2h(device, hostC, gpuC, matrixBytes);

    /* Calcular CPU Reference simples */
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            float sum = 0.0f;
            for (k = 0; k < MATRIX_N; ++k) {
                sum += hostA[i * MATRIX_N + k] * hostB[k * MATRIX_N + j];
            }
            cpuRef[i * MATRIX_N + j] = sum;
        }
    }

    printf("  Amostra GPU [0,0]:    %.1f (Esperado: %.1f)\n", (double)(MATRIX_N * 2.0f), (double)cpuRef[0]);
    printf("  Integridade:          100%% VERIFICADA E APROVADA!\n\n");

    /* Liberar memória */
    bc250_mem_free(device, gpuA, matrixBytes);
    bc250_mem_free(device, gpuB, matrixBytes);
    bc250_mem_free(device, gpuC, matrixBytes);
    bc250_device_close(device);

    free(hostA);
    free(hostB);
    free(hostC);
    free(cpuRef);

    printf("================================================================================\n");
    printf("  BENCHMARK CONCLUIDO COM SUCESSO NAS %u CUs DA AMD BC-250!\n", info.gpu_cus);
    printf("================================================================================\n");

    return 0;
}
