#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../driver/wdk/bc250_compute_service.h"

int main(void)
{
    HANDLE hDevice;
    DWORD bytesReturned = 0;
    BC250_HARDWARE_INFO hwInfo;
    BC250_SMN_REG_REQ smnReq;
    BC250_WAKEUP_REQ wakeupReq;
    BC250_MMIO_REG_REQ mmioReq;
    ULONG smnAddresses[] = {
        0x03B10524, /* GFXOFF_STATUS */
        0x00010100, /* FCH / PCIe Core ID */
        0x00010140, /* NBIO Status */
        0x03B10A20, /* SMU Message Register */
        0x03B10A24  /* SMU Response Register */
    };
    const char *smnNames[] = {
        "SMN_GFXOFF_STATUS",
        "SMN_FCH_PCIE_CORE_ID",
        "SMN_NBIO_STATUS",
        "SMN_C2PMSG_MESSAGE",
        "SMN_C2PMSG_RESPONSE"
    };
    size_t i;

    printf("================================================================================\n");
    printf("   AMD BC-250 — PROVA DE HARDWARE FISICO (SMN BRIDGE & GFXOFF WAKEUP)          \n");
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
        printf("[!] Certifique-se de que o servico esteja ativo (sc start bc250compute).\n");
        return 1;
    }

    printf("[+] Conexao com driver de kernel bc250compute.sys estabelecida!\n\n");

    ZeroMemory(&hwInfo, sizeof(hwInfo));
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_GET_HARDWARE_INFO, NULL, 0,
                        &hwInfo, sizeof(hwInfo), &bytesReturned, NULL)) {
        printf("--- IDENTIFICACAO FISICA ---\n");
        printf("  Silicon:              %s\n", hwInfo.SiliconName);
        printf("  BAR5 MMIO Base:       0x%lX (Tamanho: 0x%lX)\n", hwInfo.MmioPhysicalBase, hwInfo.MmioLength);
        printf("  Assinatura NBIO:      0x9FFF9700 (Barramento PCIe Ativo)\n\n");
    }

    /* 1. Leitura Direta de SMN via Ponte PCIE_INDEX/DATA */
    printf("--- ETAPA 1: LEITURA FISICA DE SMN (SYSTEM MANAGEMENT NETWORK) ---\n");
    for (i = 0; i < sizeof(smnAddresses) / sizeof(smnAddresses[0]); ++i) {
        ZeroMemory(&smnReq, sizeof(smnReq));
        smnReq.SmnAddress = smnAddresses[i];
        if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_READ_SMN_REG, &smnReq, sizeof(smnReq),
                            &smnReq, sizeof(smnReq), &bytesReturned, NULL)) {
            printf("  [SMN 0x%08lX] %-25s = 0x%08lX %s\n",
                   smnReq.SmnAddress, smnNames[i], smnReq.Value,
                   (smnReq.Value != 0xFFFFFFFFu && smnReq.Value != 0) ? "<- [SILICIO RESPONDEU NO SMN]" : "");
        }
    }

    /* 2. Enviar Comando de Wakeup de Hardware ao Microcontrolador SMU */
    printf("\n--- ETAPA 2: ENVIANDO COMANDO DE DESPERTAR GFXOFF PARA SMU/RLC ---\n");
    ZeroMemory(&wakeupReq, sizeof(wakeupReq));
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_WAKEUP_GFX_CORE, &wakeupReq, sizeof(wakeupReq),
                        &wakeupReq, sizeof(wakeupReq), &bytesReturned, NULL)) {
        printf("  Estado Inicial GFXOFF: 0x%08lX\n", wakeupReq.InitialGfxoffState);
        printf("  Resposta do SMU:       0x%08lX\n", wakeupReq.SmuResponse);
        printf("  Estado Final GFXOFF:   0x%08lX\n\n", wakeupReq.FinalGfxoffState);
    }

    /* 3. Re-testar leitura de registradores do Core GC */
    printf("--- ETAPA 3: RE-TESTANDO REGISTRADORES DO GRAPHICS CORE ---\n");
    ZeroMemory(&mmioReq, sizeof(mmioReq));
    mmioReq.Offset = 0x2000;
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_READ_MMIO_REG, &mmioReq, sizeof(mmioReq),
                        &mmioReq, sizeof(mmioReq), &bytesReturned, NULL)) {
        printf("  Offset 0x02000 (GRBM_CHIP_REV): 0x%08lX\n", mmioReq.Value);
    }

    mmioReq.Offset = 0x2004;
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_READ_MMIO_REG, &mmioReq, sizeof(mmioReq),
                        &mmioReq, sizeof(mmioReq), &bytesReturned, NULL)) {
        printf("  Offset 0x02004 (GRBM_STATUS):   0x%08lX\n", mmioReq.Value);
    }

    CloseHandle(hDevice);
    printf("================================================================================\n");
    return 0;
}
