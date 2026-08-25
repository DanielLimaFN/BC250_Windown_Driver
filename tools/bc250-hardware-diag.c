#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../driver/wdk/bc250_compute_service.h"

int main(void)
{
    HANDLE hDevice;
    DWORD bytesReturned = 0;
    BC250_HARDWARE_INFO hwInfo;
    BC250_MMIO_REG_REQ mmioReq;
    ULONG byteOffset;
    ULONG hitCount = 0;

    printf("================================================================================\n");
    printf("   AMD BC-250 — MAPEAMENTO COMPLETO DE BAR5 MMIO (512 KiB - VARREDURA REAL)   \n");
    printf("================================================================================\n\n");

    hDevice = CreateFileW(
        L"\\\\.\\Bc250Compute",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("[!] ERRO: Nao foi possivel abrir \\\\.\\Bc250Compute (Erro %lu).\n", GetLastError());
        return 1;
    }

    printf("[+] Conexao estabelecida!\n\n");

    ZeroMemory(&hwInfo, sizeof(hwInfo));
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_GET_HARDWARE_INFO, NULL, 0,
                        &hwInfo, sizeof(hwInfo), &bytesReturned, NULL)) {
        printf("  Silicon:        %s\n", hwInfo.SiliconName);
        printf("  BAR5 Base:      0x%lX (Tamanho: 0x%lX)\n\n", hwInfo.MmioPhysicalBase, hwInfo.MmioLength);
    }

    printf("--- VARRENDO TODAS AS FAIXAS DE 4 KiB DE MMIO (0x00000 A 0x7FFFF) ---\n");
    for (byteOffset = 0; byteOffset < 0x80000u; byteOffset += 0x1000u) {
        ZeroMemory(&mmioReq, sizeof(mmioReq));
        mmioReq.Offset = byteOffset;
        if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_READ_MMIO_REG, &mmioReq, sizeof(mmioReq),
                            &mmioReq, sizeof(mmioReq), &bytesReturned, NULL)) {
            if (mmioReq.Status == 0 && mmioReq.Value != 0xFFFFFFFFu && mmioReq.Value != 0) {
                printf("  [Faixa 0x%05lX] Resposta: 0x%08lX <- [IP BLOCK ATIVO]\n", byteOffset, mmioReq.Value);
                hitCount++;
            }
        }
    }

    /* Testar registradores em offset de DWORD (* 4) */
    printf("\n--- TESTANDO REGISTRADORES COM FATOR DE DWORD (*4) ---\n");
    {
        ULONG dwordTests[] = {
            0x2000 * 4, /* 0x8000: GRBM_CHIP_REV */
            0x2004 * 4, /* 0x8010: GRBM_STATUS */
            0x2280 * 4, /* 0x8A00: SCRATCH_REG0 */
            0x2440 * 4, /* 0x9100: SHADER_ARRAY_CONFIG */
            0xD000 * 4, /* 0x34000: SDMA0 */
            0xD008 * 4  /* 0x34020: SDMA0_RB_RPTR */
        };
        const char *testNames[] = {
            "DWORD 0x2000 (Byte 0x8000) GRBM_CHIP_REV",
            "DWORD 0x2004 (Byte 0x8010) GRBM_STATUS",
            "DWORD 0x2280 (Byte 0x8A00) SCRATCH_REG0",
            "DWORD 0x2440 (Byte 0x9100) SHADER_ARRAY_CONFIG",
            "DWORD 0xD000 (Byte 0x34000) SDMA0",
            "DWORD 0xD008 (Byte 0x34020) SDMA0_RB_RPTR"
        };
        size_t k;

        for (k = 0; k < sizeof(dwordTests) / sizeof(dwordTests[0]); ++k) {
            ZeroMemory(&mmioReq, sizeof(mmioReq));
            mmioReq.Offset = dwordTests[k];
            if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_READ_MMIO_REG, &mmioReq, sizeof(mmioReq),
                                &mmioReq, sizeof(mmioReq), &bytesReturned, NULL)) {
                printf("  %-45s = 0x%08lX %s\n",
                       testNames[k], mmioReq.Value,
                       (mmioReq.Value != 0xFFFFFFFFu) ? "<- [BLOCO RESPONDENDO!]" : "(Gated)");
            }
        }
    }

    CloseHandle(hDevice);
    printf("\n================================================================================\n");
    printf("  Total de Blocos de 4 KiB Ativos no Silício: %lu\n", hitCount);
    printf("================================================================================\n");

    return 0;
}
