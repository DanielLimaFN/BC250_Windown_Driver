#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <powrprof.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include "../../driver/wdk/bc250_compute_service.h"

#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

// PROCESSOR_POWER_INFORMATION for CallNtPowerInformation
typedef struct _PROCESSOR_POWER_INFORMATION {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
} PROCESSOR_POWER_INFORMATION, *PPROCESSOR_POWER_INFORMATION;

// SMU Mailbox Queues
#define Q0_CMD 0x03B10A08u
#define Q0_RSP 0x03B10A68u
#define Q0_ARG 0x03B10A48u

#define Q3_CMD 0x03B10A20u
#define Q3_RSP 0x03B10A80u
#define Q3_ARG 0x03B10A88u

// SMN Registers
#define BC250_SMN_TEMP_EDGE     0x03B10000u
#define BC250_SMN_TEMP_JUNCTION 0x03B10020u
#define BC250_SMN_TEMP_MEM      0x03B10028u
#define BC250_SMN_FAN_RPM       0x03B10064u
#define BC250_SMN_FAN_PWM       0x03B10068u
#define BC250_SMN_CPU_CORES     0x0115A870u

// Zen 2 SMN TSI Diodo Termico (Family 17h/19h APU)
#define BC250_SMN_ZEN2_TSI      0x00059800u

// MMIO Registers
#define BC250_REG_GRBM_STATUS   0x08010u

static HANDLE g_hDevice = INVALID_HANDLE_VALUE;

BOOL DrvW32(uint32_t offset, uint32_t val) {
    if (g_hDevice == INVALID_HANDLE_VALUE) return FALSE;
    BC250_MMIO_REG_REQ req;
    DWORD bytesReturned = 0;
    req.Offset = offset;
    req.Value = val;
    req.Status = 0;
    return DeviceIoControl(g_hDevice, IOCTL_BC250_WRITE_MMIO_REG,
                           &req, sizeof(req), &req, sizeof(req), &bytesReturned, NULL);
}

uint32_t DrvR32(uint32_t offset) {
    if (g_hDevice == INVALID_HANDLE_VALUE) return 0xFFFFFFFFu;
    BC250_MMIO_REG_REQ req;
    DWORD bytesReturned = 0;
    req.Offset = offset;
    req.Value = 0;
    req.Status = 0;
    if (DeviceIoControl(g_hDevice, IOCTL_BC250_READ_MMIO_REG,
                        &req, sizeof(req), &req, sizeof(req), &bytesReturned, NULL)) {
        return req.Value;
    }
    return 0xFFFFFFFFu;
}

// SMN transport via BAR5 0x38 (Index) / 0x3C (Data)
static void smnW(uint32_t addr, uint32_t val) {
    DrvW32(0x38, addr);
    DrvW32(0x3C, val);
}

static uint32_t smnR(uint32_t addr) {
    DrvW32(0x38, addr);
    DrvR32(0x38); // posting flush
    return DrvR32(0x3C);
}

// SMU Queue 0 (GPU Core / DPM)
static int q0(uint32_t msg, uint32_t arg) {
    if (g_hDevice == INVALID_HANDLE_VALUE) return -100;
    smnW(Q0_RSP, 0);
    smnW(Q0_ARG, arg);
    smnW(Q0_CMD, msg);
    for (int i = 0; i < 200; i++) {
        uint32_t st = smnR(Q0_RSP);
        if (st == 1) return 1;       // OK
        if (st == 0xFF) return -1;   // FAIL
        if (st == 0xFE) return -2;   // UNKNOWN
        if (st == 0xFD) return -3;   // REJECTED
        if (st == 0xFC) return -4;   // BUSY
        Sleep(1);
    }
    return -100; // TIMEOUT
}

static uint32_t q0_arg(void) {
    return smnR(Q0_ARG);
}

// SMU Queue 3 (CPU / VRM / System)
static int q3(uint32_t msg, uint32_t arg) {
    if (g_hDevice == INVALID_HANDLE_VALUE) return -100;
    smnW(Q3_RSP, 0);
    smnW(Q3_ARG, arg);
    smnW(Q3_CMD, msg);
    for (int i = 0; i < 200; i++) {
        uint32_t st = smnR(Q3_RSP);
        if (st == 1) return 1;
        if (st == 0xFF) return -1;
        if (st == 0xFE) return -2;
        if (st == 0xFD) return -3;
        if (st == 0xFC) return -4;
        Sleep(1);
    }
    return -100;
}

static uint32_t q3_arg(void) {
    return smnR(Q3_ARG);
}

static int vid_to_mv(int vid) {
    if (vid <= 0 || vid > 255) return 0;
    return (int)((-vid * 0.00625 + 1.55) * 1000 + 0.5);
}

static int popcount32(uint32_t v) {
    int c = 0;
    while (v) {
        c += v & 1;
        v >>= 1;
    }
    return c;
}

BOOL InitDevice(void) {
    if (g_hDevice != INVALID_HANDLE_VALUE) {
        return TRUE;
    }
    g_hDevice = CreateFileW(L"\\\\.\\Bc250Compute", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    return (g_hDevice != INVALID_HANDLE_VALUE);
}

void CloseDevice(void) {
    if (g_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hDevice);
        g_hDevice = INVALID_HANDLE_VALUE;
    }
}

// 1. Coleta CPU: Media de clock de todos os nucleos
BOOL GetCpuMetrics(double *pAvgMhz, ULONG *pMinMhz, ULONG *pMaxMhz, DWORD *pNumThreads) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD numThreads = sysInfo.dwNumberOfProcessors;
    if (pNumThreads) *pNumThreads = numThreads;
    if (numThreads == 0) return FALSE;

    PROCESSOR_POWER_INFORMATION *ppi = (PROCESSOR_POWER_INFORMATION *)malloc(sizeof(PROCESSOR_POWER_INFORMATION) * numThreads);
    if (!ppi) return FALSE;

    LONG st = CallNtPowerInformation(ProcessorInformation, NULL, 0, ppi, sizeof(PROCESSOR_POWER_INFORMATION) * numThreads);
    if (st != 0) {
        free(ppi);
        return FALSE;
    }

    ULONG sum = 0;
    ULONG minMhz = 0xFFFFFFFF;
    ULONG maxMhz = 0;
    for (DWORD i = 0; i < numThreads; i++) {
        sum += ppi[i].CurrentMhz;
        if (ppi[i].CurrentMhz < minMhz) minMhz = ppi[i].CurrentMhz;
        if (ppi[i].CurrentMhz > maxMhz) maxMhz = ppi[i].CurrentMhz;
    }

    if (pAvgMhz) *pAvgMhz = (double)sum / (double)numThreads;
    if (pMinMhz) *pMinMhz = minMhz;
    if (pMaxMhz) *pMaxMhz = maxMhz;

    free(ppi);
    return TRUE;
}

// 2. Coleta RAM: Uso em GB e %
BOOL GetRamMetrics(double *pUsedGb, double *pTotalGb, double *pPct) {
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) return FALSE;

    double totalGb = (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    double usedGb = (double)(mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    if (pTotalGb) *pTotalGb = totalGb;
    if (pUsedGb) *pUsedGb = usedGb;
    if (pPct) *pPct = (double)mem.dwMemoryLoad;
    return TRUE;
}

// 3. Coleta VRAM: Uso em MB e % via DXGI
BOOL GetVramMetrics(double *pUsedMb, double *pBudgetMb, double *pPct, WCHAR *pAdapterName, size_t maxNameLen) {
    IDXGIFactory4 *pFactory = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&pFactory);
    if (FAILED(hr) || !pFactory) return FALSE;

    BOOL found = FALSE;
    IDXGIAdapter1 *pAdapter = NULL;
    IDXGIAdapter1 *pTargetAdapter = NULL;

    // Procura primeiro pelo adaptador BC-250 (1002:13FE)
    for (UINT i = 0; pFactory->lpVtbl->EnumAdapters1(pFactory, i, &pAdapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->lpVtbl->GetDesc1(pAdapter, &desc);
        if (desc.VendorId == 0x1002 && desc.DeviceId == 0x13FE) {
            pTargetAdapter = pAdapter;
            if (pAdapterName) {
                wcsncpy_s(pAdapterName, maxNameLen, desc.Description, _TRUNCATE);
            }
            found = TRUE;
            break;
        }
        if (i == 0 && !pTargetAdapter) {
            pTargetAdapter = pAdapter; // Fallback para adaptador 0
            if (pAdapterName) {
                wcsncpy_s(pAdapterName, maxNameLen, desc.Description, _TRUNCATE);
            }
        } else {
            pAdapter->lpVtbl->Release(pAdapter);
        }
    }

    if (pTargetAdapter) {
        IDXGIAdapter3 *pAdapter3 = NULL;
        if (SUCCEEDED(pTargetAdapter->lpVtbl->QueryInterface(pTargetAdapter, &IID_IDXGIAdapter3, (void **)&pAdapter3))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO vramInfo;
            if (SUCCEEDED(pAdapter3->lpVtbl->QueryVideoMemoryInfo(pAdapter3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vramInfo))) {
                double usedMb = (double)vramInfo.CurrentUsage / (1024.0 * 1024.0);
                double budgetMb = (double)vramInfo.Budget / (1024.0 * 1024.0);
                if (pUsedMb) *pUsedMb = usedMb;
                if (pBudgetMb) *pBudgetMb = budgetMb;
                if (pPct) *pPct = (budgetMb > 0.0) ? ((usedMb / budgetMb) * 100.0) : 0.0;
                found = TRUE;
            }
            pAdapter3->lpVtbl->Release(pAdapter3);
        }
        pTargetAdapter->lpVtbl->Release(pTargetAdapter);
    }

    pFactory->lpVtbl->Release(pFactory);
    return found;
}

int main(int argc, char** argv) {
    bool lineMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--line") == 0 || strcmp(argv[i], "--log") == 0 || strcmp(argv[i], "-l") == 0) {
            lineMode = true;
        }
    }

    printf("Iniciando Monitor de Telemetria BC-250 (Cadencia: 2s)...\n");
    if (!InitDevice()) {
        printf("[!] Aviso: Nao foi possivel abrir \\\\.\\Bc250Compute agora.\n");
        printf("    O monitor continuara tentando reconectar a cada 2s.\n");
        printf("    (Para habilitar sensores SMU/GPU completos: rode 'tools\\start-bc250compute-service.ps1')\n\n");
    }

    // Handshake inicial SMU se conectado
    uint32_t smuver = 0, drvif = 0;
    int testQ0 = -100;
    uint32_t testRes = 0;
    if (g_hDevice != INVALID_HANDLE_VALUE) {
        testQ0 = q0(0x01, 41);
        testRes = q0_arg();
        if (q0(0x02, 0) == 1) smuver = q0_arg();
        if (q0(0x03, 0) == 1) drvif = q0_arg();
    }

    while (1) {
        // Tenta reconectar se perdeu conexao
        BOOL devOk = InitDevice();

        // 1. Amostra GRBM_STATUS durante 2 segundos (100 amostras x 20 ms = 2000 ms)
        int activeHits = 0;
        uint32_t lastGrbm = 0;
        if (devOk) {
            for (int s = 0; s < 100; s++) {
                lastGrbm = DrvR32(BC250_REG_GRBM_STATUS);
                if (lastGrbm == 0xFFFFFFFFu) {
                    // Erro de comunicacao com driver
                    CloseDevice();
                    devOk = FALSE;
                    Sleep(20 * (100 - s));
                    break;
                }
                int ia_busy = (lastGrbm >> 19) & 1;
                int wd_busy = (lastGrbm >> 18) & 1;
                if (ia_busy || wd_busy) {
                    activeHits++;
                }
                Sleep(20);
            }
        } else {
            Sleep(2000); // 2 segundos se desconectado
        }
        float gpuLoadPct = devOk ? ((float)activeHits / 100.0f * 100.0f) : 0.0f;

        // 2. Coleta CPU Clock & RAM
        double cpuAvgMhz = 0.0;
        ULONG cpuMinMhz = 0, cpuMaxMhz = 0;
        DWORD cpuThreads = 0;
        GetCpuMetrics(&cpuAvgMhz, &cpuMinMhz, &cpuMaxMhz, &cpuThreads);

        double ramUsedGb = 0.0, ramTotalGb = 0.0, ramPct = 0.0;
        GetRamMetrics(&ramUsedGb, &ramTotalGb, &ramPct);

        // 3. Coleta VRAM
        double vramUsedMb = 0.0, vramBudgetMb = 0.0, vramPct = 0.0;
        WCHAR vramAdapterName[128] = { 0 };
        GetVramMetrics(&vramUsedMb, &vramBudgetMb, &vramPct, vramAdapterName, 128);

        // 4. Coleta SMU / SMN (se conectado)
        uint32_t gpuFreqMhz = 0;
        uint32_t gpuVid = 0;
        int gpuVidMv = 0;
        uint32_t gpuCoreVddMv = 0;
        uint32_t cpuCoreVddMv = 0;
        uint32_t tJun = 0, tEdge = 0, tMem = 0, tsi = 0;
        uint32_t fanRpm = 0, fanPwm = 0, coresMask = 0, wgp = 0;
        double cpuTempC = 0.0;
        int memByte0 = 0, memByte1 = 0, memByte2 = 0;
        int junLow = 0, junHigh = 0;

        if (devOk) {
            if (q0(0x37, 0) == 1) gpuFreqMhz = q0_arg();
            if (q0(0x38, 0) == 1) {
                gpuVid = q0_arg();
                gpuVidMv = vid_to_mv((int)gpuVid);
            }
            if (q3(0x37, 0) == 1) gpuCoreVddMv = q3_arg();
            if (q3(0x36, 0) == 1) cpuCoreVddMv = q3_arg();
            if (q0(0x1E, 0) == 1) wgp = q0_arg();

            tEdge = smnR(BC250_SMN_TEMP_EDGE);
            tJun  = smnR(BC250_SMN_TEMP_JUNCTION);
            tMem  = smnR(BC250_SMN_TEMP_MEM);
            fanRpm= smnR(BC250_SMN_FAN_RPM);
            fanPwm= smnR(BC250_SMN_FAN_PWM);
            coresMask = smnR(BC250_SMN_CPU_CORES);

            tsi = smnR(BC250_SMN_ZEN2_TSI);
            uint32_t tsiRaw = (tsi >> 21) & 0x7FFu;
            if (tsiRaw > 0) {
                cpuTempC = ((double)tsiRaw * 125.0 - 49000.0) / 1000.0;
            }

            memByte0 = (int)(tMem & 0xFF);
            memByte1 = (int)((tMem >> 8) & 0xFF);
            memByte2 = (int)((tMem >> 16) & 0xFF);

            junLow  = (int)(tJun & 0xFF);
            junHigh = (int)((tJun >> 8) & 0xFF);
        }

        // Timestamp
        time_t rawtime;
        struct tm timeinfo;
        time(&rawtime);
        localtime_s(&timeinfo, &rawtime);
        char timeStr[16];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

        int bestVddMv = (gpuCoreVddMv > 0) ? (int)gpuCoreVddMv : gpuVidMv;
        int bestGpuTemp = (junLow > 0 && junLow < 130) ? junLow : (int)(tJun & 0xFF);
        int bestMemTemp = (memByte0 > 0 && memByte0 < 130) ? memByte0 : memByte1;

        if (lineMode) {
            // MODO LINHA CONTINUA (scrolling a cada 2s)
            if (devOk) {
                printf("[%s] CPU: %4.0f MHz (med) | RAM: %4.1f/%4.1f GB (%2.0f%%) | GPU: %4u MHz (%2.0f%%) | VDDGFX: %4d mV | VRAM: %4.0f MB (%2.0f%%) | Temp GPU: %2d C | Temp Mem: %2d C\n",
                       timeStr, cpuAvgMhz, ramUsedGb, ramTotalGb, ramPct,
                       gpuFreqMhz, gpuLoadPct, bestVddMv,
                       vramUsedMb, vramPct, bestGpuTemp, bestMemTemp);
            } else {
                printf("[%s] CPU: %4.0f MHz (med) | RAM: %4.1f/%4.1f GB (%2.0f%%) | VRAM: %4.0f MB (%2.0f%%) | [Aguardando driver \\\\.\\Bc250Compute...]\n",
                       timeStr, cpuAvgMhz, ramUsedGb, ramTotalGb, ramPct, vramUsedMb, vramPct);
            }
            fflush(stdout);
        } else {
            // MODO DASHBOARD FORMATADO
            system("cls");
            printf("================================================================================\n");
            printf("       AMD BC-250 (GC 10.1.3 / CYAN SKILLFISH) - TELEMETRIA (2s) [%s]    \n", timeStr);
            printf("================================================================================\n");

            // LINHA RESUMO DE 1 SEGUNDO
            if (devOk) {
                printf(" >> RESUMO: CPU: %.0f MHz | RAM: %.1f GB (%.0f%%) | GPU: %u MHz | VDDGFX: %d mV | VRAM: %.0f MB | T_GPU: %d C | T_MEM: %d C\n",
                       cpuAvgMhz, ramUsedGb, ramPct, gpuFreqMhz, bestVddMv, vramUsedMb, bestGpuTemp, bestMemTemp);
            } else {
                printf(" >> RESUMO: CPU: %.0f MHz | RAM: %.1f GB (%.0f%%) | VRAM: %.0f MB | [Driver \\\\.\\Bc250Compute Desconectado]\n",
                       cpuAvgMhz, ramUsedGb, ramPct, vramUsedMb);
            }
            printf("--------------------------------------------------------------------------------\n");

            // PAINEL CPU & RAM
            printf("[PROCESSADOR CPU]\n");
            printf("  Clock Medio   : %.1f MHz  (Min: %lu MHz | Max: %lu MHz | %lu Threads)\n",
                   cpuAvgMhz, cpuMinMhz, cpuMaxMhz, cpuThreads);
            if (devOk && cpuTempC > 0.0) {
                printf("  Temperatura   : %.1f C  (Diodo Termico Zen 2 TSI SMN 0x59800)\n", cpuTempC);
                printf("  Cores Ativos  : %u Cores Fisicos (Mascara SMN: 0x%02X)\n", popcount32(coresMask & 0xFF), coresMask & 0xFF);
            }
            printf("\n[MEMORIA RAM DO SISTEMA]\n");
            printf("  Uso de RAM    : [%-20s] %.2f GB / %.2f GB (%.1f%%)\n",
                   (ramPct > 75.0 ? "###############" : ramPct > 50.0 ? "##########" : ramPct > 25.0 ? "#####" : "#"),
                   ramUsedGb, ramTotalGb, ramPct);

            // PAINEL GPU & VRAM
            printf("\n[PLACA GRAFICA BC-250 (GPU)]\n");
            if (devOk) {
                printf("  GPU GFX Clock : %u MHz  (DPM SMU Mailbox Msg 0x37)\n", gpuFreqMhz);
                printf("  Tensao VDDGFX : %d mV  (VID: %u | Core VDD: %u mV)\n", bestVddMv, gpuVid, gpuCoreVddMv);
                printf("  Carga da GPU  : [%-20s] %.1f%%  (GRBM: 0x%08X)\n",
                       (gpuLoadPct > 50.0f ? "####################" : gpuLoadPct > 20.0f ? "########" : gpuLoadPct > 0.0f ? "##" : ""),
                       gpuLoadPct, lastGrbm);
                printf("  WGPs Ativos   : %u (24 Compute Units Fisicas)\n", wgp);
            } else {
                printf("  Status Driver : [-] \\\\.\\Bc250Compute DESCONECTADO (execute tools\\start-bc250compute-service.ps1)\n");
            }

            printf("\n[MEMORIA DE VIDEO VRAM (GDDR6)]\n");
            printf("  Uso de VRAM   : %.1f MB / %.1f MB (%.1f%%)\n", vramUsedMb, vramBudgetMb, vramPct);
            if (vramAdapterName[0] != L'\0') {
                wprintf(L"  Adaptador DXGI: %s\n", vramAdapterName);
            }

            // PAINEL TEMPERATURAS
            printf("\n[TEMPERATURAS DO SILICIO]\n");
            if (devOk) {
                printf("  GPU Junction  : %d C  (Low: %d C | High: %d C | Raw: 0x%08X)\n", bestGpuTemp, junLow, junHigh, tJun);
                printf("  GPU Edge      : %s  (Raw: 0x%08X)\n", (tEdge == 0xFFFFFFFFu) ? "Desativado na VBIOS" : "Ativo", tEdge);
                printf("  Memoria GDDR6 : %d C  (Raw: 0x%08X: B0=%d C, B1=%d C, B2=%d C)\n",
                       bestMemTemp, tMem, memByte0, memByte1, memByte2);
                printf("  Ventoinha Fan : %u RPM  (PWM: %u / 255)\n", fanRpm, fanPwm);
            } else {
                printf("  Sensores SMU  : [Aguardando conexao com driver de kernel...]\n");
            }

            printf("================================================================================\n");
            printf(" Atualizacao: a cada 2.0s | Pressione Ctrl+C para sair | Parametro: --line para log\n");
            fflush(stdout);
        }
    }

    CloseDevice();
    return 0;
}