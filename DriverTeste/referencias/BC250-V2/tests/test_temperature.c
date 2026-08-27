/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_temperature.c

Abstract:
    Diagnostic tool to read and monitor live temperature and telemetry on AMD BC-250 (Cyan Skillfish).
    Queries the WDDM driver escape BC250_ESCAPE_CMD_QUERY_TELEMETRY (0xBD10) via D3DKMT.

--*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMHDC {
    HDC           hDc;
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;
    UINT          VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef enum _D3DKMT_ESCAPETYPE {
    D3DKMT_ESCAPE_DRIVERPRIVATE = 0
} D3DKMT_ESCAPETYPE;

typedef struct _D3DKMT_ESCAPE {
    D3DKMT_HANDLE     hAdapter;
    D3DKMT_HANDLE     hDevice;
    D3DKMT_ESCAPETYPE Type;
    struct {
        UINT Value;
    } Flags;
    VOID*             pPrivateDriverData;
    UINT              PrivateDriverDataSize;
    D3DKMT_HANDLE     hContext;
} D3DKMT_ESCAPE;

typedef LONG (APIENTRY *PFN_D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC*);
typedef LONG (APIENTRY *PFN_D3DKMTEscape)(CONST D3DKMT_ESCAPE*);

#define BC250_ESCAPE_CMD_QUERY_TELEMETRY    0xBD10u

typedef struct _BC250_ESCAPE_HEADER {
    ULONG CommandId;
    ULONG Status;
    ULONG InSize;
    ULONG OutSize;
} BC250_ESCAPE_HEADER;

typedef struct _BC250_WDDM_ESCAPE_TELEMETRY {
    BC250_ESCAPE_HEADER Header;
    ULONG     GfxTemperature;     /* millidegrees C */
    ULONG     GfxTemperatureC;    /* degrees C */
    ULONG     SocTemperatureC;    /* degrees C */
    ULONG     EdgeTemperatureC;   /* degrees C */
    ULONG     HotspotTemperatureC;/* degrees C */
    ULONG     GfxVoltageMv;       /* mV */
    ULONG     SocVoltageMv;       /* mV */
    ULONG     GfxClockMhz;        /* MHz */
    ULONG     SocClockMhz;        /* MHz */
    ULONG     MemClockMhz;        /* MHz */
    ULONG     PowerMilliwatts;    /* mW */
    ULONG     PowerWatts;         /* W */
    ULONG     SmuStatus;          /* Status code */
    ULONG     Flags;              /* 1 = SMU Metrics, 2 = Fallback */
} BC250_WDDM_ESCAPE_TELEMETRY;

static PFN_D3DKMTOpenAdapterFromHdc g_pfnOpenHdc = NULL;
static PFN_D3DKMTEscape             g_pfnEscape = NULL;
static D3DKMT_HANDLE                g_hAdapter = 0;

static BOOL InitD3dKmt(void)
{
    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) return FALSE;

    g_pfnOpenHdc = (PFN_D3DKMTOpenAdapterFromHdc)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromHdc");
    g_pfnEscape  = (PFN_D3DKMTEscape)GetProcAddress(hGdi, "D3DKMTEscape");
    if (!g_pfnOpenHdc || !g_pfnEscape) return FALSE;

    HDC hdc = GetDC(NULL);
    if (!hdc) return FALSE;

    D3DKMT_OPENADAPTERFROMHDC openHdc = { 0 };
    openHdc.hDc = hdc;
    LONG status = g_pfnOpenHdc(&openHdc);
    ReleaseDC(NULL, hdc);

    if (status >= 0 && openHdc.hAdapter != 0) {
        g_hAdapter = openHdc.hAdapter;
        return TRUE;
    }
    return FALSE;
}

static BOOL QueryTelemetry(BC250_WDDM_ESCAPE_TELEMETRY *telem)
{
    RtlZeroMemory(telem, sizeof(*telem));
    telem->Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
    telem->Header.InSize = sizeof(*telem);
    telem->Header.OutSize = sizeof(*telem);

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = g_hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = telem;
    esc.PrivateDriverDataSize = sizeof(*telem);

    if (g_pfnEscape(&esc) >= 0 && telem->Header.Status == 0) {
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char* argv[])
{
    BOOL watchMode = FALSE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--watch") == 0) {
            watchMode = TRUE;
        }
    }

    printf("=================================================================\n");
    printf("   AMD BC-250 Live Hardware Temperature & Telemetry Monitor      \n");
    printf("=================================================================\n\n");

    if (!InitD3dKmt()) {
        printf("[-] ERRO: Nao foi possivel conectar ao driver WDDM via D3DKMT.\n");
        printf("    Certifique-se de que o driver bc250wddm esta instalado e ativo.\n");
        return 1;
    }
    printf("[+] Conectado ao adaptador WDDM (hAdapter = 0x%08X)\n\n", g_hAdapter);

    do {
        BC250_WDDM_ESCAPE_TELEMETRY telem;
        if (QueryTelemetry(&telem)) {
            if (watchMode) {
                system("cls");
                printf("=================================================================\n");
                printf("   AMD BC-250 Live Monitor (Pressione Ctrl+C para sair)          \n");
                printf("=================================================================\n\n");
            }

            printf("  [+] GPU Core / Edge Temp  : %u C (%u mC)\n", telem.GfxTemperatureC, telem.GfxTemperature);
            printf("  [+] SoC / Northbridge Temp: %u C\n", telem.SocTemperatureC);
            printf("  [+] Hotspot / Junction    : %u C\n", telem.HotspotTemperatureC);
            printf("  ---------------------------------------------------------------\n");
            printf("  [+] GPU Core Voltage      : %u mV (VDDGFX)\n", telem.GfxVoltageMv);
            printf("  [+] SoC Voltage           : %u mV (VDDNB)\n", telem.SocVoltageMv);
            printf("  [+] GPU Core Frequency    : %u MHz (GFXCLK)\n", telem.GfxClockMhz);
            printf("  [+] SoC Frequency         : %u MHz (SOCCLK)\n", telem.SocClockMhz);
            printf("  [+] Board Power Draw (PPT): %u W (%u mW)\n", telem.PowerWatts, telem.PowerMilliwatts);
            printf("  ---------------------------------------------------------------\n");
            printf("  [+] Telemetry Source      : %s (Flags: 0x%X, SmuStatus: 0x%X)\n",
                   (telem.Flags == 1) ? "SMU Metrics Table (100% Live Hardware)" : "Direct SMN/TSI Sensor",
                   telem.Flags, telem.SmuStatus);
            printf("\n");
        } else {
            printf("[-] Falha ao consultar telemetria do driver.\n");
        }

        if (watchMode) {
            Sleep(1000);
        }
    } while (watchMode);

    return 0;
}
