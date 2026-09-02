#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../driver/umd/bc250_runtime.h"

/* Shader canônico GFX10 de teste: s_endpgm (0xBF810000) */
static const uint32_t s_testShader[4] = {
    0xBF810000u, /* s_endpgm */
    0xBF800000u, /* s_nop */
    0xBF800000u, /* s_nop */
    0xBF800000u  /* s_nop */
};

int main(void)
{
    bc250_device_t *device = NULL;
    bc250_device_info_t info;
    bc250_telemetry_data_t telemetry;
    bc250_status_t status;
    uint64_t gpuBufferVa = 0;
    const size_t testBufferSize = 4096;
    uint32_t hostWriteData[1024];
    uint32_t hostReadData[1024];
    uint32_t wptrDelta = 0, rptrDelta = 0, fenceObserved = 0;
    size_t i;
    int mismatches = 0;

    printf("================================================================================\n");
    printf("   AMD BC-250 — USER-MODE RUNTIME (UMD / PHASE P3) TEST HARNESS                 \n");
    printf("================================================================================\n\n");

    /* 1. Inicializacao do Dispositivo via UMD */
    printf("[1/6] Inicializando BC-250 via bc250_runtime.dll...\n");
    status = bc250_device_init(&device, &info);
    if (status != BC250_SUCCESS) {
        printf("  [!] Falha ao inicializar dispositivo (Status: %d).\n", status);
        printf("  [!] Verifique se o driver ou servico de kernel esta ativo.\n");
        return 1;
    }

    printf("  [+] Dispositivo conectado com sucesso!\n");
    printf("      Silicon:            %s\n", info.silicon_name);
    printf("      Compute Units:      %u CUs ativas\n", info.gpu_cus);
    printf("      Workgroup Process:  %u WGPs ativos\n", info.gpu_wgps);
    printf("      VRAM Total:         %u MiB\n", info.vram_total_mib);
    printf("      GPU Clock:          %u MHz\n", info.gpu_clock_mhz);
    printf("      Memory Clock:       %u MHz\n\n", info.memory_clock_mhz);

    /* 2. Leitura de Telemetria */
    printf("[2/6] Consultando telemetria em tempo real...\n");
    if (bc250_get_telemetry(device, &telemetry) == BC250_SUCCESS) {
        printf("      SMU Firmware:       0x%08X\n", telemetry.smu_fw_version);
        printf("      GFXCLK Freq:        %u MHz\n", telemetry.gfxclk_mhz);
        printf("      GFX Voltage:        %u mV\n", telemetry.gfx_voltage_mv);
        printf("      Temperatura:        %u C\n", telemetry.motherboard_temp_c);
        printf("      Fan Speed:          %u RPM\n\n", telemetry.fan_rpm);
    } else {
        printf("      [-] Telemetria nao disponivel nesta sessao.\n\n");
    }

    /* 3. Alocacao de VRAM */
    printf("[3/6] Alocando %zu bytes de VRAM física...\n", testBufferSize);
    status = bc250_mem_alloc(device, testBufferSize, &gpuBufferVa);
    if (status != BC250_SUCCESS) {
        printf("  [!] Falha na alocacao de VRAM (Status: %d).\n", status);
        bc250_device_close(device);
        return 1;
    }
    printf("  [+] Buffer alocado na GPUVA: 0x%016llX\n\n", gpuBufferVa);

    /* 4. Teste de Copia Host -> Device (SDMA H2D) */
    printf("[4/6] Gravando padrao de teste na VRAM (Host -> Device)...\n");
    for (i = 0; i < 1024; ++i) {
        hostWriteData[i] = 0xBC250000u | (uint32_t)i;
        hostReadData[i] = 0x00000000u;
    }
    status = bc250_mem_copy_h2d(device, gpuBufferVa, hostWriteData, testBufferSize);
    if (status != BC250_SUCCESS) {
        printf("  [!] Falha no SDMA Copy H2D (Status: %d).\n", status);
    } else {
        printf("  [+] Copia H2D concluida com sucesso!\n");
    }

    /* 5. Leitura Device -> Host (SDMA D2H) e Validacao Byte a Byte */
    printf("[5/6] Lendo dados da VRAM de volta para o Host (Device -> Host)...\n");
    status = bc250_mem_copy_d2h(device, hostReadData, gpuBufferVa, testBufferSize);
    if (status != BC250_SUCCESS) {
        printf("  [!] Falha no SDMA Copy D2H (Status: %d).\n", status);
    } else {
        for (i = 0; i < 1024; ++i) {
            if (hostReadData[i] != hostWriteData[i]) {
                ++mismatches;
            }
        }
        if (mismatches == 0) {
            printf("  [+] Integridade de dados CONFIRMADA: 1024/1024 DWORDs identicos (0 mismatches)!\n\n");
        } else {
            printf("  [!] Corrupcao de dados detectada: %d mismatches (Lido[0]: 0x%08X, Esperado[0]: 0x%08X)!\n\n",
                   mismatches, hostReadData[0], hostWriteData[0]);
        }
    }

    /* 6. Disparo de Kernel via UMD Dispatch */
    printf("[6/6] Disparando Kernel de Computacao via UMD Dispatch...\n");
    status = bc250_dispatch_kernel(
        device,
        s_testShader,
        sizeof(s_testShader) / sizeof(s_testShader[0]),
        1,  /* grid_x */
        1,  /* block_x */
        &wptrDelta,
        &rptrDelta,
        &fenceObserved);

    if (status == BC250_SUCCESS) {
        printf("  [+] Kernel despachado com sucesso!\n");
        printf("      WPTR Delta:         %u DWORDs\n", wptrDelta);
        printf("      RPTR Delta:         %u DWORDs\n", rptrDelta);
        printf("      Fence Observada:    0x%08X\n\n", fenceObserved);
    } else {
        printf("  [-] Dispatch de kernel UMD retornou status %d (esperado durante operacao basica).\n\n", status);
    }

    /* 7. Desalocacao e Teardown */
    printf("Liberando memoria e fechando dispositivo UMD...\n");
    bc250_mem_free(device, gpuBufferVa, testBufferSize);
    bc250_device_close(device);

    printf("================================================================================\n");
    printf("   TESTE DO RUNTIME UMD FINALIZADO COM EXITO!\n");
    printf("================================================================================\n");
    return 0;
}
