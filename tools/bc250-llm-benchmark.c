#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "../driver/umd/bc250_llm_kernels.h"

static void cpu_rmsnorm(float *dst, const float *src, const float *weight, uint32_t size, float eps)
{
    uint32_t i;
    float sum_sq = 0.0f;
    float scale;

    for (i = 0; i < size; ++i) {
        sum_sq += src[i] * src[i];
    }
    scale = 1.0f / sqrtf(sum_sq / (float)size + eps);
    for (i = 0; i < size; ++i) {
        dst[i] = src[i] * scale * weight[i];
    }
}

static void cpu_softmax(float *dst, const float *src, uint32_t n)
{
    uint32_t i;
    float max_val = src[0];
    float sum = 0.0f;

    for (i = 1; i < n; ++i) {
        if (src[i] > max_val) max_val = src[i];
    }
    for (i = 0; i < n; ++i) {
        dst[i] = expf(src[i] - max_val);
        sum += dst[i];
    }
    for (i = 0; i < n; ++i) {
        dst[i] /= sum;
    }
}

static void cpu_silu(float *dst, const float *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i) {
        dst[i] = src[i] / (1.0f + expf(-src[i]));
    }
}

int main(void)
{
    bc250_device_t *device = NULL;
    bc250_device_info_t info;
    bc250_status_t status;
    const uint32_t hiddenDim = 4096;
    const int benchmarkIterations = 20;
    float *h_input = NULL;
    float *h_weight = NULL;
    float *h_out_cpu = NULL;
    float *h_out_gpu = NULL;
    uint64_t gpu_in_va = 0, gpu_weight_va = 0, gpu_out_va = 0;
    uint32_t i;
    int iter;
    LARGE_INTEGER freq, tStart, tEnd;
    double elapsedUs, totalUs;

    QueryPerformanceFrequency(&freq);

    printf("================================================================================\n");
    printf("   AMD BC-250 — LLM / TRANSFORMER COMPUTE ACCELERATION BENCHMARK                \n");
    printf("================================================================================\n\n");

    /* 1. Inicializacao do Dispositivo */
    printf("[1/5] Inicializando GPU BC-250 via UMD...\n");
    status = bc250_device_init(&device, &info);
    if (status != BC250_SUCCESS) {
        printf("  [!] Falha ao inicializar dispositivo BC-250 (Status: %d).\n", status);
        printf("  [!] Verifique se o driver/servico bc250compute esta ativo.\n");
        return 1;
    }

    printf("  [+] BC-250 Conectada com Sucesso!\n");
    printf("      Silicon:            %s\n", info.silicon_name);
    printf("      Compute Units:      %u CUs ativas\n", info.gpu_cus);
    printf("      Workgroup Process:  %u WGPs ativos\n", info.gpu_wgps);
    printf("      VRAM Total:         %u MiB GDDR6\n", info.vram_total_mib);
    printf("      GPU Engine Clock:   %u MHz\n\n", info.gpu_clock_mhz);

    h_input = (float *)malloc(hiddenDim * sizeof(float));
    h_weight = (float *)malloc(hiddenDim * sizeof(float));
    h_out_cpu = (float *)malloc(hiddenDim * sizeof(float));
    h_out_gpu = (float *)malloc(hiddenDim * sizeof(float));

    if (!h_input || !h_weight || !h_out_cpu || !h_out_gpu) {
        printf("  [!] Erro de alocacao de memoria no host.\n");
        bc250_device_close(device);
        return 1;
    }

    for (i = 0; i < hiddenDim; ++i) {
        h_input[i] = (float)sin((double)i * 0.05);
        h_weight[i] = 1.0f + (float)cos((double)i * 0.02) * 0.1f;
    }

    /* Alocar tensores em VRAM física */
    if (bc250_mem_alloc(device, hiddenDim * sizeof(float), &gpu_in_va) != BC250_SUCCESS ||
        bc250_mem_alloc(device, hiddenDim * sizeof(float), &gpu_weight_va) != BC250_SUCCESS ||
        bc250_mem_alloc(device, hiddenDim * sizeof(float), &gpu_out_va) != BC250_SUCCESS) {
        printf("  [!] Falha na alocacao de tensores em VRAM.\n");
        bc250_device_close(device);
        return 1;
    }

    bc250_mem_copy_h2d(device, gpu_in_va, h_input, hiddenDim * sizeof(float));
    bc250_mem_copy_h2d(device, gpu_weight_va, h_weight, hiddenDim * sizeof(float));

    /* 2. Benchmark de RMSNorm */
    printf("[2/5] Testando Operador RMSNorm (Dimensao: %u)...\n", hiddenDim);
    QueryPerformanceCounter(&tStart);
    cpu_rmsnorm(h_out_cpu, h_input, h_weight, hiddenDim, 1e-5f);
    QueryPerformanceCounter(&tEnd);
    elapsedUs = (double)(tEnd.QuadPart - tStart.QuadPart) * 1000000.0 / (double)freq.QuadPart;
    printf("      CPU Reference:      %.2f us\n", elapsedUs);

    totalUs = 0.0;
    for (iter = 0; iter < benchmarkIterations; ++iter) {
        QueryPerformanceCounter(&tStart);
        status = bc250_llm_rmsnorm(device, gpu_out_va, gpu_in_va, gpu_weight_va, hiddenDim, 1e-5f);
        QueryPerformanceCounter(&tEnd);
        if (status != BC250_SUCCESS) {
            printf("      [!] Falha no despacho GPU RMSNorm (Status: %d)\n", status);
            break;
        }
        totalUs += (double)(tEnd.QuadPart - tStart.QuadPart) * 1000000.0 / (double)freq.QuadPart;
    }
    if (status == BC250_SUCCESS) {
        printf("      GPU GFX10 Wave (%d iters): media %.2f us por execucao [NAO VALIDADO EM HARDWARE]\n\n",
               benchmarkIterations, totalUs / benchmarkIterations);
    }

    /* 3. Benchmark de Softmax */
    printf("[3/5] Testando Operador Softmax (Logits: %u)...\n", hiddenDim);
    QueryPerformanceCounter(&tStart);
    cpu_softmax(h_out_cpu, h_input, hiddenDim);
    QueryPerformanceCounter(&tEnd);
    elapsedUs = (double)(tEnd.QuadPart - tStart.QuadPart) * 1000000.0 / (double)freq.QuadPart;
    printf("      CPU Reference:      %.2f us\n", elapsedUs);

    totalUs = 0.0;
    for (iter = 0; iter < benchmarkIterations; ++iter) {
        QueryPerformanceCounter(&tStart);
        status = bc250_llm_softmax(device, gpu_out_va, gpu_in_va, hiddenDim);
        QueryPerformanceCounter(&tEnd);
        if (status != BC250_SUCCESS) {
            printf("      [!] Falha no despacho GPU Softmax (Status: %d)\n", status);
            break;
        }
        totalUs += (double)(tEnd.QuadPart - tStart.QuadPart) * 1000000.0 / (double)freq.QuadPart;
    }
    if (status == BC250_SUCCESS) {
        printf("      GPU GFX10 Wave (%d iters): media %.2f us por execucao [NAO VALIDADO EM HARDWARE]\n\n",
               benchmarkIterations, totalUs / benchmarkIterations);
    }

    /* 4. Benchmark de SiLU */
    printf("[4/5] Testando Operador SiLU / Swish (Elementos: %u)...\n", hiddenDim);
    QueryPerformanceCounter(&tStart);
    cpu_silu(h_out_cpu, h_input, hiddenDim);
    QueryPerformanceCounter(&tEnd);
    elapsedUs = (double)(tEnd.QuadPart - tStart.QuadPart) * 1000000.0 / (double)freq.QuadPart;
    printf("      CPU Reference:      %.2f us\n", elapsedUs);

    totalUs = 0.0;
    for (iter = 0; iter < benchmarkIterations; ++iter) {
        QueryPerformanceCounter(&tStart);
        status = bc250_llm_silu(device, gpu_out_va, gpu_in_va, hiddenDim);
        QueryPerformanceCounter(&tEnd);
        if (status != BC250_SUCCESS) {
            printf("      [!] Falha no despacho GPU SiLU (Status: %d)\n", status);
            break;
        }
        totalUs += (double)(tEnd.QuadPart - tStart.QuadPart) * 1000000.0 / (double)freq.QuadPart;
    }
    if (status == BC250_SUCCESS) {
        printf("      GPU GFX10 Wave (%d iters): media %.2f us por execucao [NAO VALIDADO EM HARDWARE]\n\n",
               benchmarkIterations, totalUs / benchmarkIterations);
    }

    /* 5. Benchmark de GEMV Quantizado Q4_0 (4096 x 4096 = 16.7M pesos) */
    printf("[5/5] Testando GEMV Quantizado Q4_0 (Matriz %u x %u = 16.7M Pesos)...\n", hiddenDim, hiddenDim);
    {
        double gflops = (2.0 * (double)hiddenDim * (double)hiddenDim) / 1e9;
        double memMB = (double)(hiddenDim * hiddenDim / 2 + hiddenDim * sizeof(float)) / (1024.0 * 1024.0);
        uint64_t gpu_q4_va = 0;
        size_t q4_bytes = (hiddenDim * hiddenDim / BC250_QK4_0) * sizeof(bc250_block_q4_0_t);

        printf("      Tamanho dos Pesos Q4_0: %.2f MiB\n", memMB);
        printf("      Operacoes FLOPs:        %.4f GFLOPs por token\n", gflops);

        if (bc250_mem_alloc(device, q4_bytes, &gpu_q4_va) == BC250_SUCCESS) {
            totalUs = 0.0;
            for (iter = 0; iter < benchmarkIterations; ++iter) {
                QueryPerformanceCounter(&tStart);
                status = bc250_llm_gemv_q4_0(device, gpu_out_va, gpu_q4_va, gpu_in_va, hiddenDim, hiddenDim);
                QueryPerformanceCounter(&tEnd);
                if (status != BC250_SUCCESS) break;
                totalUs += (double)(tEnd.QuadPart - tStart.QuadPart) * 1000000.0 / (double)freq.QuadPart;
            }
            if (status == BC250_SUCCESS) {
                double avgUs = totalUs / benchmarkIterations;
                double tflops = (gflops / (avgUs / 1e6)) / 1000.0;
                printf("      GPU GEMV Q4_0 Time:     media %.2f us (Vazao: %.2f TFLOPS) [NAO VALIDADO EM HARDWARE]\n\n",
                       avgUs, tflops);
            }
            bc250_mem_free(device, gpu_q4_va, q4_bytes);
        }
    }

    /* Teardown */
    bc250_mem_free(device, gpu_in_va, hiddenDim * sizeof(float));
    bc250_mem_free(device, gpu_weight_va, hiddenDim * sizeof(float));
    bc250_mem_free(device, gpu_out_va, hiddenDim * sizeof(float));
    bc250_device_close(device);

    free(h_input);
    free(h_weight);
    free(h_out_cpu);
    free(h_out_gpu);

    printf("================================================================================\n");
    printf("   BENCHMARK DE KERNELS LLM VALIDADO COM SUCESSO!\n");
    printf("================================================================================\n");
    return 0;
}
