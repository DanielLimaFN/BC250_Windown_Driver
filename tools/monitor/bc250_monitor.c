#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../driver/wdk/bc250_compute_service.h"

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
    BC250_MMIO_REG_REQ req;
    DWORD bytesReturned = 0;
    req.Offset = offset;
    req.Value = val;
    req.Status = 0;
    return DeviceIoControl(g_hDevice, IOCTL_BC250_WRITE_MMIO_REG,
                           &req, sizeof(req), &req, sizeof(req), &bytesReturned, NULL);
}

uint32_t DrvR32(uint32_t offset) {
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
    g_hDevice = CreateFileW(L"\\\\.\\Bc250Compute", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDevice == INVALID_HANDLE_VALUE) {
        printf("[-] Falha ao abrir \\\\.\\Bc250Compute. Erro: %lu\n", GetLastError());
        return FALSE;
    }
    return TRUE;
}

int main(int argc, char** argv) {
    if (!InitDevice()) {
        printf("    Execute 'tools\\start-bc250compute-service.ps1' antes de rodar o monitor.\n");
        return 1;
    }

    printf("Conectando ao silicio BC-250 (GC 10.1.3)...\n");
    Sleep(300);

    int testQ0 = q0(0x01, 41);
    uint32_t testRes = q0_arg();

    uint32_t smuver = 0, drvif = 0;
    if (q0(0x02, 0) == 1) smuver = q0_arg();
    if (q0(0x03, 0) == 1) drvif = q0_arg();

    while (1) {
        int activeHits = 0;
        uint32_t lastGrbm = 0;
        for (int s = 0; s < 50; s++) {
            lastGrbm = DrvR32(BC250_REG_GRBM_STATUS);
            int ia_busy = (lastGrbm >> 19) & 1;
            int wd_busy = (lastGrbm >> 18) & 1;
            /* Atividade real de renderizacao/computacao exige engines de shader (IA ou WD) */
            if (ia_busy || wd_busy) {
                activeHits++;
            }
            Sleep(10);
        }
        float gpuLoadPct = (float)activeHits / 50.0f * 100.0f;
        
        uint32_t tEdge = smnR(BC250_SMN_TEMP_EDGE);
        uint32_t tJun  = smnR(BC250_SMN_TEMP_JUNCTION);
        uint32_t tMem  = smnR(BC250_SMN_TEMP_MEM);
        uint32_t fanRpm= smnR(BC250_SMN_FAN_RPM);
        uint32_t fanPwm= smnR(BC250_SMN_FAN_PWM);
        uint32_t cores = smnR(BC250_SMN_CPU_CORES);

        uint32_t tsi = smnR(BC250_SMN_ZEN2_TSI);
        uint32_t tsiRaw = (tsi >> 21) & 0x7FFu;
        double cpuTempC = ((double)tsiRaw * 125.0 - 49000.0) / 1000.0;

        uint32_t freq = 0, vid = 0, wgp = 0, feat = 0;
        if (q0(0x37, 0) == 1) freq = q0_arg();
        if (q0(0x38, 0) == 1) vid  = q0_arg();
        if (q0(0x1E, 0) == 1) wgp  = q0_arg();
        if (q0(0x3D, 0) == 1) feat = q0_arg();

        uint32_t cpumv = 0, gpumv = 0;
        if (q3(0x36, 0) == 1) cpumv = q3_arg();
        if (q3(0x37, 0) == 1) gpumv = q3_arg();

        system("cls");
        printf("=================================================================\n");
        printf("       AMD BC-250 ÔÇö HARDWARE MONITOR & TELEMETRY (REAL-TIME)    \n");
        printf("=================================================================\n");

        printf("\n--- SMU Firmware & Telemetria ---\n");
        if (testQ0 == 1 && testRes == 42) {
            printf("[+] SMU Handshake : OK (TestMessage echo: 42)\n");
        } else {
            printf("[-] SMU Handshake : Status %d (Ret: %u)\n", testQ0, testRes);
        }
        printf("[+] SMU Version   : v%u.%u.%u (Driver IF: %u)\n", 
               (smuver >> 16) & 0xFF, (smuver >> 8) & 0xFF, smuver & 0xFF, drvif);
        printf("[+] CPU Zen 2     : %u Cores Ativos (Mask: 0x%02X)\n", popcount32(cores & 0xFF), cores & 0xFF);
        printf("[+] Active WGPs   : %u (24 CUs Fisicas)\n", wgp);

        printf("\n--- Frequencia e Voltagem ---\n");
        printf("[+] GPU GFX Clock : %u MHz\n", freq);
        printf("[+] GPU GFX VID   : %u (%d mV)\n", vid, vid_to_mv((int)vid));
        printf("[+] GPU Core VDD  : %u mV\n", gpumv);
        printf("[+] CPU Core VDD  : %u mV\n", cpumv);

        printf("\n--- Temperaturas Reais do Silicio ---\n");
        printf("[+] CPU Core (TSI): %.1f C  (Diodo SMN 0x59800: Raw 0x%03X)\n", cpuTempC, tsiRaw);
        printf("[+] GDDR6 / SOC   : Raw 0x%08X\n", tMem);
        printf("    |-- Byte 0 (L): %u C (0x%02X)\n", tMem & 0xFF, tMem & 0xFF);
        printf("    |-- Byte 1 (M): %u C (0x%02X)\n", (tMem >> 8) & 0xFF, (tMem >> 8) & 0xFF);
        printf("    `-- Byte 2 (H): %u C (0x%02X)\n", (tMem >> 16) & 0xFF, (tMem >> 16) & 0xFF);
        printf("[+] Junction Smn  : Raw 0x%08X (Byte0: %u | Byte1: %u)\n",
               tJun, tJun & 0xFF, (tJun >> 8) & 0xFF);
        printf("[+] Edge Sensor   : %s (Raw: 0x%08X)\n", 
               (tEdge == 0xFFFFFFFFu) ? "Desativado na VBIOS" : "Ativo", tEdge);
        printf("[+] Fan Telemetry : %u RPM (PWM: %u)\n", fanRpm, fanPwm);

        printf("\n--- Atividade da GPU no WDDM (Aceleracao de Hardware) ---\n");
        printf("[+] Uso Estimado  : [%-20s] %.1f%%\n",
               (gpuLoadPct > 50.0f ? "####################" :
                gpuLoadPct > 20.0f ? "########" :
                gpuLoadPct > 0.0f  ? "##" : ""),
               gpuLoadPct);
        printf("[+] GRBM_STATUS   : 0x%08X\n", lastGrbm);
        
        int gui_act = (lastGrbm >> 31) & 1;
        int cp_busy = (lastGrbm >> 29) & 1;
        int ia_busy = (lastGrbm >> 19) & 1;
        int wd_busy = (lastGrbm >> 18) & 1;
        int bc_busy = (lastGrbm >> 24) & 1;

        if (ia_busy || wd_busy) {
            printf("    >> [!!!] GPU ATIVA: Computacao / Renderizacao em execucao!\n");
        } else if (gui_act || cp_busy) {
            printf("    >> [!] CP EM AGUARDO: Fila pendente ou timeout de microengine (GUI=%d, CP=%d)\n", gui_act, cp_busy);
        } else {
            printf("    >> [*] GPU EM REPOUSO: GUI_ACTIVE=0 (DWM em buffer estatico)\n");
        }
        printf("    Blocos: CP_BUSY=%d | IA_BUSY=%d | WD_BUSY=%d | BC_BUSY=%d\n",
               cp_busy, ia_busy, wd_busy, bc_busy);
    }

    CloseHandle(g_hDevice);
    return 0;
}