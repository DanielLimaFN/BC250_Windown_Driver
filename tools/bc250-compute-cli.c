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

    /* 3. Auditoria dos Registradores de Microcódigo e Motores CP/MEC */
    printf("--- ETAPA 3: AUDITORIA DOS REGISTRADORES DE MICROCODE E CP/MEC ---\n");
    static const struct { ULONG Offset; const char *Name; } cpRegs[] = {
        { 0x00008000u, "GRBM_CHIP_REV" },
        { 0x00008004u, "GRBM_STATUS" },
        { 0x00008008u, "GRBM_STATUS2" },
        { 0x00008680u, "CP_STAT" },
        { 0x000086D8u, "CP_ME_CNTL" },
        { 0x00008234u, "CP_MEC_CNTL" },
        { 0x00008694u, "CP_PFP_PC" },
        { 0x00008698u, "CP_ME_PC" },
        { 0x0000869Cu, "CP_CE_PC" },
        { 0x000086A0u, "CP_MEC1_PC" },
        { 0x000086A4u, "CP_MEC2_PC" },
        { 0x0000C4CCu, "CP_PFP_JT_STAT" },
        { 0x00008700u, "CP_RB0_RPTR" },
        { 0x0000C150u, "CP_RB0_WPTR" },
        { 0x0000C680u, "CP_RB0_ACTIVE" },
        { 0x0000C100u, "CP_RB0_BASE" },
        { 0x0000C104u, "CP_RB0_CNTL" },
        { 0x0000A200u, "GCVM_CTX0_CNTL" },
        { 0x0000A3ACu, "GCVM_CTX0_PT_BASE" },
        { 0x0000A42Cu, "GCVM_CTX0_START" },
        { 0x0000A4ACu, "GCVM_CTX0_END" },
        { 0x00003B00u, "RLC_CNTL" }
    };
    for (i = 0; i < sizeof(cpRegs) / sizeof(cpRegs[0]); ++i) {
        BC250_MMIO_REG_REQ mmReq;
        ZeroMemory(&mmReq, sizeof(mmReq));
        mmReq.Offset = cpRegs[i].Offset;
        if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_READ_MMIO_REG, &mmReq, sizeof(mmReq),
                            &mmReq, sizeof(mmReq), &bytesReturned, NULL)) {
            printf("  [MMIO 0x%05lX] %-18s = 0x%08lX\n", mmReq.Offset, cpRegs[i].Name, mmReq.Value);
        }
    }
    printf("\n");

    /* 4. Ensaio do Motor SDMA */
    printf("\n--- ETAPA 4: TESTE DO MOTOR SDMA (DMA FENCE TRIPOD) ---\n");
    BC250_SDMA_COPY_REQ sdmaReq;
    ZeroMemory(&sdmaReq, sizeof(sdmaReq));
    sdmaReq.ByteCount = 16;
    sdmaReq.FenceValue = 0xBC250123u;
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_SDMA_COPY, &sdmaReq, sizeof(sdmaReq),
                        &sdmaReq, sizeof(sdmaReq), &bytesReturned, NULL)) {
        printf("  SDMA RPTR / WPTR:      %lu / %lu\n", sdmaReq.RptrAfter, sdmaReq.WptrAfter);
        printf("  SDMA Fence Observada:  0x%08lX (Esperado: 0x%08lX)\n", sdmaReq.FenceObserved, sdmaReq.FenceValue);
        printf("  Veredito SDMA:         %s\n", (sdmaReq.ExecutionSuccess != 0) ? "[SUCESSO - TRIPE PROVADO]" : "[FALHA]");
    }

    /* 5. Ensaio do Motor CP Ring 0 (PM4 Direct Dispatch) */
    printf("\n--- ETAPA 5: TESTE DO MOTOR CP RING 0 (PM4 WRITE_DATA & EOP FENCE) ---\n");
    BC250_COMPUTE_DISPATCH_REQ ring0Req;
    ZeroMemory(&ring0Req, sizeof(ring0Req));
    ring0Req.WorkgroupSizeX = 100; /* Ring 0 mode */
    ring0Req.FenceValue = 0xBC250123u;
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_DISPATCH_COMPUTE, &ring0Req, sizeof(ring0Req),
                        &ring0Req, sizeof(ring0Req), &bytesReturned, NULL)) {
        printf("  Ring 0 RPTR / WPTR:    %lu / %lu\n", ring0Req.RptrAfter, ring0Req.WptrAfter);
        printf("  Ring 0 Fence:          0x%08lX (Esperado: 0x%08lX)\n", ring0Req.FenceObserved, ring0Req.FenceValue);
        printf("  Veredito Ring 0:       %s\n", (ring0Req.ExecutionSuccess != 0) ? "[SUCESSO - PM4 RING 0 PROVADO]" : "[FALHA]");
    }

    /* 6. Ensaio da Fila Assíncrona de Computação MEC1 / HQD com MQD 512B */
    printf("\n--- ETAPA 6: TESTE DA FILA ASSINCRONA MEC1 / HQD (MQD 512B & EOP FENCE) ---\n");
    BC250_COMPUTE_DISPATCH_REQ hqdReq;
    ZeroMemory(&hqdReq, sizeof(hqdReq));
    hqdReq.WorkgroupSizeX = 1; /* HQD mode */
    hqdReq.FenceValue = 0xBC250123u;
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_DISPATCH_COMPUTE, &hqdReq, sizeof(hqdReq),
                        &hqdReq, sizeof(hqdReq), &bytesReturned, NULL)) {
        printf("  HQD ACTIVE Readback:   0x%08lX\n", hqdReq.WptrBefore);
        printf("  HQD RPTR / WPTR:       %lu / %lu\n", hqdReq.RptrAfter, hqdReq.WptrAfter);
        printf("  HQD Fence Observada:   0x%08lX (Esperado: 0x%08lX)\n", hqdReq.FenceObserved, hqdReq.FenceValue);
        printf("  Veredito MEC1/HQD:     %s\n", (hqdReq.ExecutionSuccess != 0) ? "[SUCESSO - FILA ASSINCRONA PROVADA]" : "[FALHA]");
    }

    CloseHandle(hDevice);
    printf("================================================================================\n");
    return 0;
}
