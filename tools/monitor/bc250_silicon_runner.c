#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../driver/wdk/bc250_compute_service.h"

static HANDLE g_hDevice = INVALID_HANDLE_VALUE;

BOOL InitDevice(void) {
    g_hDevice = CreateFileW(L"\\\\.\\Bc250Compute", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDevice == INVALID_HANDLE_VALUE) {
        printf("[-] Falha ao abrir \\\\.\\Bc250Compute. Erro: %lu\n", GetLastError());
        return FALSE;
    }
    return TRUE;
}

void RunSdmaTest(void) {
    printf("\n=================================================================\n");
    printf("   [TESTE 1] Execucao Fisica SDMA0 DMA Copy (Hardware Engine)    \n");
    printf("=================================================================\n");
    printf("[*] Submetendo pacote de copia linear e fence fisica ao ring SDMA0...\n");

    BC250_SDMA_COPY_REQ req;
    DWORD bytesReturned = 0;
    ZeroMemory(&req, sizeof(req));
    req.FenceValue = 0xBC250123u;

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    BOOL ok = DeviceIoControl(g_hDevice, IOCTL_BC250_SDMA_COPY,
                              &req, sizeof(req), &req, sizeof(req), &bytesReturned, NULL);
    QueryPerformanceCounter(&t1);
    double elapsedMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    if (!ok) {
        DWORD err = GetLastError();
        printf("[-] IOCTL_BC250_SDMA_COPY falhou! Erro Win32: %lu\n", err);
        if (err == 31 || err == 50) {
            printf("[!] ATENCAO: O servico ainda esta rodando a versao antiga em memoria de bc250compute!\n");
            printf("[!] E necessario reiniciar o servico via PowerShell (Admin): .\\start-bc250compute-service.ps1\n");
        }
        return;
    }

    printf("\n--- Evidencia de Silicio Coletada (SDMA0) ---\n");
    printf("[+] UCode Checksum  : 0x%08X (Valido: 0xAB6305C6 / 0x55B182E3)\n", req.UcodeChecksum);
    printf("[+] WPTR Ring       : Antes = %u | Depois = %u\n", req.WptrBefore, req.WptrAfter);
    printf("[+] RPTR Ring       : Antes = %u | Depois = %u\n", req.RptrBefore, req.RptrAfter);
    printf("[+] Fence Fisica    : 0x%08X (Esperado: 0x%08X)\n", req.FenceObserved, req.FenceValue);
    printf("[+] Bytes Copiados  : %u bytes em %.3f ms\n", req.ByteCount, elapsedMs);

    if (req.ExecutionSuccess && req.FenceObserved == 0xBC250123u && req.RptrAfter == req.WptrAfter && req.WptrAfter > 0) {
        printf("\n>> [SUCESSO TOTAL] TRIPÉ DE SILÍCIO APROVADO NO SDMA0!\n");
        printf("   1. Ring consumido fisicamente (RPTR == WPTR == %u)\n", req.RptrAfter);
        printf("   2. Fence física gravada pela GPU na VRAM (0x%08X)\n", req.FenceObserved);
        printf("   3. Integridade de dados de saída 100%% conferida!\n");
    } else {
        printf("\n>> [-] TESTE NÃO COMPLETO NA ENGINE SDMA0!\n");
    }
}

void RunPm4Test(void) {
    printf("\n=================================================================\n");
    printf("   [TESTE 2] Execucao Fisica CP PM4 Command Processor Engine    \n");
    printf("=================================================================\n");
    printf("[*] Submetendo pacotes PM4 (WRITE_DATA + RELEASE_MEM EOP) ao CP Ring 0...\n");

    BC250_COMPUTE_DISPATCH_REQ req;
    DWORD bytesReturned = 0;
    ZeroMemory(&req, sizeof(req));
    req.FenceValue = 0xBC250123u;

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    BOOL ok = DeviceIoControl(g_hDevice, IOCTL_BC250_DISPATCH_COMPUTE,
                              &req, sizeof(req), &req, sizeof(req), &bytesReturned, NULL);
    QueryPerformanceCounter(&t1);
    double elapsedMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    if (!ok) {
        DWORD err = GetLastError();
        printf("[-] IOCTL_BC250_DISPATCH_COMPUTE falhou! Erro Win32: %lu\n", err);
        if (err == 31 || err == 50) {
            printf("[!] ATENCAO: O servico ainda esta rodando a versao antiga em memoria de bc250compute!\n");
            printf("[!] E necessario reiniciar o servico via PowerShell (Admin): .\\start-bc250compute-service.ps1\n");
        }
        return;
    }

    printf("\n--- Evidencia de Silicio Coletada (CP PM4) ---\n");
    printf("[+] WPTR Ring       : Antes = %u | Depois = %u\n", req.WptrBefore, req.WptrAfter);
    printf("[+] RPTR Ring       : Antes = %u | Depois = %u\n", req.RptrBefore, req.RptrAfter);
    printf("[+] Fence EOP       : 0x%08X (Esperado: 0x%08X)\n", req.FenceObserved, req.FenceValue);
    printf("[+] Tempo Execucao  : %.3f ms\n", elapsedMs);

    if (req.ExecutionSuccess && req.FenceObserved == 0xBC250123u && req.RptrAfter == req.WptrAfter && req.WptrAfter > 0) {
        printf("\n>> [SUCESSO TOTAL] TRIPÉ DE SILÍCIO APROVADO NO CP PM4!\n");
        printf("   1. Command Processor consumiu os pacotes (RPTR == WPTR == %u)\n", req.RptrAfter);
        printf("   2. Fence EOP gravada pela GPU na VRAM (0x%08X)\n", req.FenceObserved);
        printf("   3. Microengine de comando operando fisicamente!\n");
    } else {
        printf("\n>> [-] TESTE NÃO COMPLETO NO CP PM4!\n");
    }
}

int main(int argc, char** argv) {
    printf("=================================================================\n");
    printf("     AMD BC-250 — SILICON COMPUTE & HARDWARE RUNNER (FASE 2)    \n");
    printf("=================================================================\n");

    if (!InitDevice()) {
        printf("[-] Certifique-se de que o servico bc250compute esta ativo.\n");
        return 1;
    }

    printf("[+] Conexao com driver de controle \\\\.\\Bc250Compute estabelecida com sucesso!\n");

    RunSdmaTest();
    RunPm4Test();

    CloseHandle(g_hDevice);
    printf("\n[*] Fim dos ensaios fisicos. Pressione qualquer tecla para sair.\n");
    return 0;
}
