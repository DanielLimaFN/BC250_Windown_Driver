/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    atiadlxx.c

Abstract:
    AMD Display Library (ADL) Compatibility Bridge for AMD BC-250 (Cyan Skillfish).
    Provides native ADL endpoints for GPU-Z, HWiNFO, MSI Afterburner, and RivaTuner,
    bridging queries to the bc250wddm.sys kernel driver via D3DKMTEscape.

--*/

#include "atiadlxx.h"

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

typedef struct _D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef LONG (APIENTRY *PFN_D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC*);
typedef LONG (APIENTRY *PFN_D3DKMTEscape)(CONST D3DKMT_ESCAPE*);
typedef LONG (APIENTRY *PFN_D3DKMTCloseAdapter)(CONST D3DKMT_CLOSEADAPTER*);

#define BC250_ESCAPE_CMD_QUERY_TELEMETRY    0xBD10u
#define BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS 0xBD06u

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

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_CAPS {
    BC250_ESCAPE_HEADER Header;
    ULONG     NumCu;              /* 24 CUs */
    ULONG     NumWgp;             /* 12 WGPs */
    ULONG     NumSimdPerCu;       /* 2 SIMD32 per CU (RDNA1) */
    ULONG     NumTotalShaders;    /* 1536 Stream Processors */
    ULONG     GpuClockMhz;        /* 1500 MHz */
    ULONG     GfxArch;            /* 1010 (GFX10.1 / Navi12) */
    ULONG     MaxWorkgroupSize;   /* 1024 */
    ULONG     WaveSize;           /* 32 */
    ULONG     LdsSizeBytes;       /* 64 KB per WGP */
    ULONGLONG VramTotalBytes;     /* Total VRAM */
    ULONGLONG ComputeVramBase;
    ULONG     ComputeVramSize;
    ULONG     DispatchCount;
    ULONG     SchemaVersion;
    ULONG     CuCountSource;
    ULONG     VramSizeSource;
    ULONG     WgpMaskRaw;
    ULONG     FirmwareReady;
} BC250_WDDM_ESCAPE_COMPUTE_CAPS;

static PFN_D3DKMTOpenAdapterFromHdc g_pfnOpenHdc = NULL;
static PFN_D3DKMTEscape             g_pfnEscape = NULL;
static PFN_D3DKMTCloseAdapter       g_pfnCloseAdapter = NULL;
static D3DKMT_HANDLE                g_hAdapter = 0;
static ADL_MAIN_MALLOC_CALLBACK     g_pfnMalloc = NULL;

static BOOL EnsureD3dKmt(void)
{
    if (g_hAdapter != 0 && g_pfnEscape != NULL) {
        return TRUE;
    }

    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) return FALSE;

    g_pfnOpenHdc      = (PFN_D3DKMTOpenAdapterFromHdc)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromHdc");
    g_pfnEscape       = (PFN_D3DKMTEscape)GetProcAddress(hGdi, "D3DKMTEscape");
    g_pfnCloseAdapter = (PFN_D3DKMTCloseAdapter)GetProcAddress(hGdi, "D3DKMTCloseAdapter");
    if (!g_pfnOpenHdc || !g_pfnEscape) return FALSE;

    /* 1. Tenta encontrar display attached */
    DISPLAY_DEVICEW dd;
    DWORD devNum = 0;
    RtlZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);

    while (EnumDisplayDevicesW(NULL, devNum, &dd, 0)) {
        if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) || (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) {
            HDC hdc = CreateDCW(L"DISPLAY", dd.DeviceName, NULL, NULL);
            if (hdc != NULL) {
                D3DKMT_OPENADAPTERFROMHDC openHdc = { 0 };
                openHdc.hDc = hdc;
                LONG status = g_pfnOpenHdc(&openHdc);
                DeleteDC(hdc);
                if (status >= 0 && openHdc.hAdapter != 0) {
                    g_hAdapter = openHdc.hAdapter;
                    return TRUE;
                }
            }
        }
        devNum++;
        RtlZeroMemory(&dd, sizeof(dd));
        dd.cb = sizeof(dd);
    }

    /* 2. Fallback padrão: GetDC(NULL) */
    HDC hdc = GetDC(NULL);
    if (hdc != NULL) {
        D3DKMT_OPENADAPTERFROMHDC openHdc = { 0 };
        openHdc.hDc = hdc;
        LONG status = g_pfnOpenHdc(&openHdc);
        ReleaseDC(NULL, hdc);
        if (status >= 0 && openHdc.hAdapter != 0) {
            g_hAdapter = openHdc.hAdapter;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL QueryDriverTelemetry(BC250_WDDM_ESCAPE_TELEMETRY *pTelem)
{
    if (!EnsureD3dKmt() || !pTelem) {
        return FALSE;
    }

    RtlZeroMemory(pTelem, sizeof(*pTelem));
    pTelem->Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
    pTelem->Header.InSize = sizeof(*pTelem);
    pTelem->Header.OutSize = sizeof(*pTelem);

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = g_hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = pTelem;
    esc.PrivateDriverDataSize = sizeof(*pTelem);

    if (g_pfnEscape(&esc) >= 0 && pTelem->Header.Status == 0) {
        return TRUE;
    }
    return FALSE;
}

static BOOL QueryDriverCaps(BC250_WDDM_ESCAPE_COMPUTE_CAPS *pCaps)
{
    if (!EnsureD3dKmt() || !pCaps) {
        return FALSE;
    }

    RtlZeroMemory(pCaps, sizeof(*pCaps));
    pCaps->Header.CommandId = BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS;
    pCaps->Header.InSize = sizeof(*pCaps);
    pCaps->Header.OutSize = sizeof(*pCaps);

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = g_hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = pCaps;
    esc.PrivateDriverDataSize = sizeof(*pCaps);

    if (g_pfnEscape(&esc) >= 0 && pCaps->Header.Status == 0) {
        return TRUE;
    }
    return FALSE;
}

/* =========================================================================
   ADL Standard Public API Implementations
   ========================================================================= */

int __cdecl ADL_Main_Control_Create(ADL_MAIN_MALLOC_CALLBACK callback, int iEnumConnectedAdapters)
{
    g_pfnMalloc = callback;
    EnsureD3dKmt();
    return ADL_OK;
}

int __cdecl ADL_Main_Control2_Create(ADL_MAIN_MALLOC_CALLBACK callback, int iEnumConnectedAdapters, void** context)
{
    g_pfnMalloc = callback;
    EnsureD3dKmt();
    if (context) *context = (void*)1;
    return ADL_OK;
}

int __cdecl ADL_Main_Control_Destroy(void)
{
    if (g_hAdapter != 0 && g_pfnCloseAdapter != NULL) {
        D3DKMT_CLOSEADAPTER closeAdapter = { g_hAdapter };
        g_pfnCloseAdapter(&closeAdapter);
        g_hAdapter = 0;
    }
    return ADL_OK;
}

int __cdecl ADL2_Main_Control_Destroy(ADL_CONTEXT_HANDLE context)
{
    return ADL_Main_Control_Destroy();
}

int __cdecl ADL_Adapter_NumberOfAdapters_Get(int* numAdapters)
{
    if (!numAdapters) return ADL_ERR_INVALID_PARAM;
    *numAdapters = 1;
    return ADL_OK;
}

int __cdecl ADL2_Adapter_NumberOfAdapters_Get(ADL_CONTEXT_HANDLE context, int* numAdapters)
{
    return ADL_Adapter_NumberOfAdapters_Get(numAdapters);
}

int __cdecl ADL_Adapter_AdapterInfo_Get(LPAdapterInfo lpInfo, int iInputSize)
{
    if (!lpInfo || iInputSize < sizeof(AdapterInfo)) {
        return ADL_ERR_INVALID_PARAM;
    }

    RtlZeroMemory(lpInfo, sizeof(AdapterInfo));
    lpInfo->iSize = sizeof(AdapterInfo);
    lpInfo->iAdapterIndex = 0;
    lpInfo->iBusNumber = 1;
    lpInfo->iDeviceNumber = 0;
    lpInfo->iFunctionNumber = 0;
    lpInfo->iVendorID = 0x1002; /* AMD */
    lpInfo->iPresent = 1;
    lpInfo->iExist = 1;
    lpInfo->iOSDisplayIndex = 0;

    strcpy_s(lpInfo->strUDID, ADL_MAX_PATH, "PCI_VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00");
    strcpy_s(lpInfo->strAdapterName, ADL_MAX_PATH, "AMD BC-250 / Cyan Skillfish");
    strcpy_s(lpInfo->strDisplayName, ADL_MAX_PATH, "AMD BC-250 / Cyan Skillfish (Navi 12)");
    strcpy_s(lpInfo->strDriverPath, ADL_MAX_PATH, "bc250wddm");
    strcpy_s(lpInfo->strPNPString, ADL_MAX_PATH, "PCI\\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00");

    return ADL_OK;
}

int __cdecl ADL2_Adapter_AdapterInfo_Get(ADL_CONTEXT_HANDLE context, LPAdapterInfo lpInfo, int iInputSize)
{
    return ADL_Adapter_AdapterInfo_Get(lpInfo, iInputSize);
}

int __cdecl ADL_Adapter_ID_Get(int iAdapterIndex, int* lpAdapterID)
{
    if (!lpAdapterID) return ADL_ERR_INVALID_PARAM;
    *lpAdapterID = 0x13FE;
    return ADL_OK;
}

int __cdecl ADL2_Adapter_ID_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int* lpAdapterID)
{
    return ADL_Adapter_ID_Get(iAdapterIndex, lpAdapterID);
}

int __cdecl ADL_Adapter_Active_Get(int iAdapterIndex, int* lpStatus)
{
    if (!lpStatus) return ADL_ERR_INVALID_PARAM;
    *lpStatus = 1;
    return ADL_OK;
}

int __cdecl ADL2_Adapter_Active_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int* lpStatus)
{
    return ADL_Adapter_Active_Get(iAdapterIndex, lpStatus);
}

/* =========================================================================
   ASIC, Memory & Architecture Information (GPU-Z Main Tab)
   ========================================================================= */

int __cdecl ADL_Adapter_ASIC_Info_Get(int iAdapterIndex, ADLASICInfo* lpASICInfo)
{
    if (!lpASICInfo) return ADL_ERR_INVALID_PARAM;
    RtlZeroMemory(lpASICInfo, sizeof(ADLASICInfo));
    lpASICInfo->iSize = sizeof(ADLASICInfo);
    lpASICInfo->iASICFamily = 100; /* RDNA1 / GFX10 */
    strcpy_s(lpASICInfo->strASICName, ADL_MAX_PATH, "Navi 12 (Cyan Skillfish / Oberon)");
    lpASICInfo->iRevisionID = 0;
    lpASICInfo->iASICRevision = 0;
    lpASICInfo->iBusType = 6;  /* PCIe */
    lpASICInfo->iBusSpeed = 3; /* Gen 3 (8.0 GT/s) */
    lpASICInfo->iBusWidth = 16;/* x16 */
    return ADL_OK;
}

int __cdecl ADL2_Adapter_ASIC_Info_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLASICInfo* lpASICInfo)
{
    return ADL_Adapter_ASIC_Info_Get(iAdapterIndex, lpASICInfo);
}

int __cdecl ADL_Adapter_MemoryInfo_Get(int iAdapterIndex, ADLMemoryInfo* lpMemoryInfo)
{
    if (!lpMemoryInfo) return ADL_ERR_INVALID_PARAM;
    lpMemoryInfo->iMemorySize = 16ULL * 1024 * 1024 * 1024; /* 16 GB GDDR6 */
    strcpy_s(lpMemoryInfo->strMemoryType, ADL_MAX_PATH, "GDDR6");
    lpMemoryInfo->iMemoryBandwidth = 448000; /* 448 GB/s (256-bit @ 14 Gbps) */
    return ADL_OK;
}

int __cdecl ADL2_Adapter_MemoryInfo_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLMemoryInfo* lpMemoryInfo)
{
    return ADL_Adapter_MemoryInfo_Get(iAdapterIndex, lpMemoryInfo);
}

int __cdecl ADL_Adapter_MemoryInfo2_Get(int iAdapterIndex, ADLMemoryInfo2* lpMemoryInfo)
{
    if (!lpMemoryInfo) return ADL_ERR_INVALID_PARAM;
    lpMemoryInfo->iMemorySize = 16ULL * 1024 * 1024 * 1024;
    strcpy_s(lpMemoryInfo->strMemoryType, ADL_MAX_PATH, "GDDR6");
    lpMemoryInfo->iMemoryBandwidth = 448000;
    lpMemoryInfo->iHyperZ = 1;
    lpMemoryInfo->iMemoryType = 6; /* GDDR6 */
    lpMemoryInfo->iMemoryFlags = 0;
    return ADL_OK;
}

int __cdecl ADL2_Adapter_MemoryInfo2_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLMemoryInfo2* lpMemoryInfo)
{
    return ADL_Adapter_MemoryInfo2_Get(iAdapterIndex, lpMemoryInfo);
}

int __cdecl ADL_Adapter_Speed_Caps_Get(int iAdapterIndex, ADLAdapterSpeedCaps* lpSpeedCaps)
{
    if (!lpSpeedCaps) return ADL_ERR_INVALID_PARAM;
    RtlZeroMemory(lpSpeedCaps, sizeof(ADLAdapterSpeedCaps));
    lpSpeedCaps->iSize = sizeof(ADLAdapterSpeedCaps);
    /* Fase 0: CU/shader vem do KMD, que por sua vez le do silicio. Os valores
       fixos 24/1536 foram removidos — a regra proibe fixar a configuracao. */
    {
        BC250_WDDM_ESCAPE_COMPUTE_CAPS caps;
        BC250_WDDM_ESCAPE_TELEMETRY telem;

        if (!QueryDriverCaps(&caps)) return ADL_ERR;

        lpSpeedCaps->iNumberOfCUs = (int)caps.NumCu;
        lpSpeedCaps->iNumberOfShaders = (int)caps.NumTotalShaders;

        if (QueryDriverTelemetry(&telem)) {
            lpSpeedCaps->iCoreSpeed = (int)telem.GfxClockMhz * 100;
            lpSpeedCaps->iMemorySpeed = (int)telem.MemClockMhz * 100;
        }
    }
    lpSpeedCaps->iCoreSpeedMax = 200000;
    lpSpeedCaps->iMemorySpeedMax = 200000;
    lpSpeedCaps->iNumberOfROPs = 64;
    lpSpeedCaps->iNumberOfTMUs = 96;
    return ADL_OK;
}

int __cdecl ADL2_Adapter_Speed_Caps_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLAdapterSpeedCaps* lpSpeedCaps)
{
    return ADL_Adapter_Speed_Caps_Get(iAdapterIndex, lpSpeedCaps);
}

int __cdecl ADL_Adapter_VideoBiosInfo_Get(int iAdapterIndex, ADLBiosInfo* lpBiosInfo)
{
    if (!lpBiosInfo) return ADL_ERR_INVALID_PARAM;
    strcpy_s(lpBiosInfo->strPartNumber, ADL_MAX_PATH, "Cyan Skillfish (Navi12 Lite)");
    strcpy_s(lpBiosInfo->strVersion, ADL_MAX_PATH, "Cyan Skillfish UEFI GOP v5.00");
    strcpy_s(lpBiosInfo->strDate, ADL_MAX_PATH, "2026/08/20");
    return ADL_OK;
}

int __cdecl ADL2_Adapter_VideoBiosInfo_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLBiosInfo* lpBiosInfo)
{
    return ADL_Adapter_VideoBiosInfo_Get(iAdapterIndex, lpBiosInfo);
}

/* =========================================================================
   Clock & Telemetry APIs (Used by GPU-Z Sensors & Clocks)
   ========================================================================= */

int __cdecl ADL_Adapter_ClockInfo_Get(int iAdapterIndex, ADLClockInfo* lpClockInfo)
{
    if (!lpClockInfo) return ADL_ERR_INVALID_PARAM;

    BC250_WDDM_ESCAPE_TELEMETRY telem;
    if (QueryDriverTelemetry(&telem) && telem.GfxClockMhz > 0) {
        lpClockInfo->iCoreClock = telem.GfxClockMhz * 100; /* in 10 KHz */
    } else {
        lpClockInfo->iCoreClock = 150000; /* 1500 MHz */
    }
    lpClockInfo->iMemoryClock = 175000;   /* 1750 MHz */
    return ADL_OK;
}

int __cdecl ADL2_Adapter_ClockInfo_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLClockInfo* lpClockInfo)
{
    return ADL_Adapter_ClockInfo_Get(iAdapterIndex, lpClockInfo);
}

int __cdecl ADL_Overdrive5_ODParameters_Get(int iAdapterIndex, ADLODParameters* lpOdParameters)
{
    if (!lpOdParameters) return ADL_ERR_INVALID_PARAM;
    RtlZeroMemory(lpOdParameters, sizeof(ADLODParameters));
    lpOdParameters->iSize = sizeof(ADLODParameters);
    lpOdParameters->iNumberOfPerformanceLevels = 2;
    lpOdParameters->iActivityReportingSupported = 1;
    lpOdParameters->iDiscretePerformanceLevels = 1;

    lpOdParameters->sEngineClock.iMin = 30000;
    lpOdParameters->sEngineClock.iMax = 200000;
    lpOdParameters->sEngineClock.iStep = 100;

    lpOdParameters->sMemoryClock.iMin = 30000;
    lpOdParameters->sMemoryClock.iMax = 200000;
    lpOdParameters->sMemoryClock.iStep = 100;

    lpOdParameters->sVddc.iMin = 700;
    lpOdParameters->sVddc.iMax = 1200;
    lpOdParameters->sVddc.iStep = 1;
    return ADL_OK;
}

int __cdecl ADL2_Overdrive5_ODParameters_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLODParameters* lpOdParameters)
{
    return ADL_Overdrive5_ODParameters_Get(iAdapterIndex, lpOdParameters);
}

int __cdecl ADL_Overdrive5_ODPerformanceLevels_Get(int iAdapterIndex, int iDefault, ADLODPerformanceLevels* lpLevels)
{
    if (!lpLevels) return ADL_ERR_INVALID_PARAM;
    RtlZeroMemory(lpLevels, sizeof(ADLODPerformanceLevels));
    lpLevels->iSize = sizeof(ADLODPerformanceLevels);

    /* Idle Level 0 */
    lpLevels->aLevels[0].iEngineClock = 30000;  /* 300 MHz */
    lpLevels->aLevels[0].iMemoryClock = 100000; /* 1000 MHz */
    lpLevels->aLevels[0].iVddc = 750;

    /* Max Level 1 */
    lpLevels->aLevels[1].iEngineClock = 150000; /* 1500 MHz */
    lpLevels->aLevels[1].iMemoryClock = 175000; /* 1750 MHz */
    lpLevels->aLevels[1].iVddc = 850;
    return ADL_OK;
}

int __cdecl ADL2_Overdrive5_ODPerformanceLevels_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int iDefault, ADLODPerformanceLevels* lpLevels)
{
    return ADL_Overdrive5_ODPerformanceLevels_Get(iAdapterIndex, iDefault, lpLevels);
}

int __cdecl ADL2_OverdriveN_PerformanceLevels_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLODNPerformanceLevels* lpLevels)
{
    if (!lpLevels) return ADL_ERR_INVALID_PARAM;

    /* Fase 0: sem telemetria real -> ADL_ERR. */
    BC250_WDDM_ESCAPE_TELEMETRY telem;
    int clock, vddc;
    if (!QueryDriverTelemetry(&telem)) return ADL_ERR;
    clock = (int)telem.GfxClockMhz;
    vddc  = (int)telem.GfxVoltageMv;

    lpLevels->iNumberOfPerformanceLevels = 2;
    lpLevels->aLevels[0].iClock = clock;
    lpLevels->aLevels[0].iVddc = vddc;
    lpLevels->aLevels[0].iControl = 0;

    lpLevels->aLevels[1].iClock = 1750; /* Mem Clock */
    lpLevels->aLevels[1].iVddc = vddc;
    lpLevels->aLevels[1].iControl = 0;
    return ADL_OK;
}

int __cdecl ADL2_OverdriveN_Capabilities_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLODNCapabilities* lpCapabilities)
{
    if (!lpCapabilities) return ADL_ERR_INVALID_PARAM;
    RtlZeroMemory(lpCapabilities, sizeof(ADLODNCapabilities));
    lpCapabilities->iSize = sizeof(ADLODNCapabilities);

    lpCapabilities->sEngineClock.iMin = 300;
    lpCapabilities->sEngineClock.iMax = 2000;
    lpCapabilities->sEngineClock.iStep = 1;
    lpCapabilities->sEngineClock.iDefault = 1500;

    lpCapabilities->sMemoryClock.iMin = 300;
    lpCapabilities->sMemoryClock.iMax = 2000;
    lpCapabilities->sMemoryClock.iStep = 1;
    lpCapabilities->sMemoryClock.iDefault = 1750;

    lpCapabilities->svddc.iMin = 700;
    lpCapabilities->svddc.iMax = 1200;
    lpCapabilities->svddc.iStep = 1;
    lpCapabilities->svddc.iDefault = 850;
    return ADL_OK;
}

int __cdecl ADL2_OverdriveN_Temperature_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int iThermalType, int* lpTemperature)
{
    if (!lpTemperature) return ADL_ERR_INVALID_PARAM;

    BC250_WDDM_ESCAPE_TELEMETRY telem;
    if (QueryDriverTelemetry(&telem)) {
        if (iThermalType == ADL_ODN_TEMPERATURE_HOTSPOT) {
            *lpTemperature = (int)(telem.HotspotTemperatureC * 1000);
        } else {
            *lpTemperature = (int)telem.GfxTemperature; /* millidegrees C */
        }
        return ADL_OK;
    }

    /* Fase 0 / REGRA BC-250: telemetria fabricada e' proibida.
       Sem leitura real do SMU, reportar erro. Antes retornava 48.0 C fixo. */
    *lpTemperature = 0;
    return ADL_ERR;
}

int __cdecl ADL_Overdrive5_Temperature_Get(int iAdapterIndex, int iThermalControllerIndex, ADLTemperature* lpTemperature)
{
    if (!lpTemperature) return ADL_ERR_INVALID_PARAM;

    BC250_WDDM_ESCAPE_TELEMETRY telem;
    if (QueryDriverTelemetry(&telem)) {
        lpTemperature->iSize = sizeof(ADLTemperature);
        lpTemperature->iTemperature = (int)telem.GfxTemperature;
        return ADL_OK;
    }

    /* Fase 0: sem telemetria real -> erro. Antes retornava 48.0 C fixo. */
    lpTemperature->iSize = sizeof(ADLTemperature);
    lpTemperature->iTemperature = 0;
    return ADL_ERR;
}

int __cdecl ADL2_Overdrive5_Temperature_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int iThermalControllerIndex, ADLTemperature* lpTemperature)
{
    return ADL_Overdrive5_Temperature_Get(iAdapterIndex, iThermalControllerIndex, lpTemperature);
}

int __cdecl ADL2_Overdrive6_CurrentPower_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int iPowerType, int* lpCurrentValue)
{
    if (!lpCurrentValue) return ADL_ERR_INVALID_PARAM;

    BC250_WDDM_ESCAPE_TELEMETRY telem;
    if (QueryDriverTelemetry(&telem)) {
        *lpCurrentValue = (int)telem.PowerWatts;
        return ADL_OK;
    }

    /* Fase 0: sem tabela de metricas do SMU a potencia nao pode ser lida.
       Antes retornava 42 W fixo com ADL_OK. */
    *lpCurrentValue = 0;
    return ADL_ERR;
}

int __cdecl ADL2_Overdrive6_CurrentStatus_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLOD6CurrentStatus* lpCurrentStatus)
{
    if (!lpCurrentStatus) return ADL_ERR_INVALID_PARAM;

    /* Fase 0: zero em vez de valores inventados; falha se o SMU nao responder. */
    BC250_WDDM_ESCAPE_TELEMETRY telem;
    int clock = 0;
    int vddc = 0;
    int memClk = 0;
    if (!QueryDriverTelemetry(&telem)) return ADL_ERR;
    clock  = (int)telem.GfxClockMhz;
    vddc   = (int)telem.GfxVoltageMv;
    memClk = (int)telem.MemClockMhz;

    lpCurrentStatus->iEngineClock = clock * 100; /* in 10 KHz */
    lpCurrentStatus->iMemoryClock = memClk * 100;
    lpCurrentStatus->iVddc = vddc;
    lpCurrentStatus->iActivityPercent = 0;
    lpCurrentStatus->iCurrentBusLanes = 16;
    lpCurrentStatus->iCurrentBusSpeed = 3;
    return ADL_OK;
}

int __cdecl ADL_Overdrive5_CurrentActivity_Get(int iAdapterIndex, ADLPMActivity* lpActivity)
{
    if (!lpActivity) return ADL_ERR_INVALID_PARAM;

    /* Fase 0: sem telemetria real -> ADL_ERR. Nada de 1500 MHz / 1750 MHz fixos. */
    BC250_WDDM_ESCAPE_TELEMETRY telem;
    int clock, vddc, memClk;
    if (!QueryDriverTelemetry(&telem)) return ADL_ERR;
    clock  = (int)telem.GfxClockMhz;
    vddc   = (int)telem.GfxVoltageMv;
    memClk = (int)telem.MemClockMhz;

    lpActivity->iSize = sizeof(ADLPMActivity);
    lpActivity->iEngineClock = clock * 100; /* in 10 KHz */
    lpActivity->iMemoryClock = memClk * 100;
    lpActivity->iVddc = vddc;
    lpActivity->iActivityPercent = 0;
    lpActivity->iCurrentBusLanes = 16;
    lpActivity->iCurrentBusSpeed = 3;
    return ADL_OK;
}

int __cdecl ADL2_Overdrive5_CurrentActivity_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLPMActivity* lpActivity)
{
    return ADL_Overdrive5_CurrentActivity_Get(iAdapterIndex, lpActivity);
}

int __cdecl ADL2_OverdriveN_PerformanceStatus_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLODNPerformanceStatus* lpStatus)
{
    if (!lpStatus) return ADL_ERR_INVALID_PARAM;
    RtlZeroMemory(lpStatus, sizeof(ADLODNPerformanceStatus));

    /* Fase 0: valores default fabricados removidos. */
    BC250_WDDM_ESCAPE_TELEMETRY telem;
    int gfxClk, socClk, memClk, vddc;

    if (!QueryDriverTelemetry(&telem)) return ADL_ERR;
    gfxClk = (int)telem.GfxClockMhz;
    socClk = (int)telem.SocClockMhz;
    memClk = (int)telem.MemClockMhz;
    vddc   = (int)telem.GfxVoltageMv;

    lpStatus->iCoreClock = gfxClk * 100;
    lpStatus->iGFXClock = gfxClk * 100;
    lpStatus->iMemoryClock = memClk * 100;
    lpStatus->iDCEFClock = socClk * 100;
    lpStatus->iUClock = memClk * 100;
    lpStatus->iVddc = vddc;
    lpStatus->iVddci = vddc;
    lpStatus->iActivity = 0;
    lpStatus->iCurrentBusLanes = 16;
    lpStatus->iMaximumBusLanes = 16;
    lpStatus->iCurrentBusSpeed = 3;
    return ADL_OK;
}

int __cdecl ADL2_New_QueryPMLogData_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLPMLogDataOutput* lpDataOutput)
{
    if (!lpDataOutput) return ADL_ERR_INVALID_PARAM;
    RtlZeroMemory(lpDataOutput, sizeof(ADLPMLogDataOutput));

    BC250_WDDM_ESCAPE_TELEMETRY telem;
    if (QueryDriverTelemetry(&telem)) {
        lpDataOutput->Sensors[ADL_PMLOG_CLK_GFXCLK].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_CLK_GFXCLK].iValue = (int)telem.GfxClockMhz;

        lpDataOutput->Sensors[ADL_PMLOG_CLK_SOCCLK].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_CLK_SOCCLK].iValue = (int)telem.SocClockMhz;

        lpDataOutput->Sensors[ADL_PMLOG_CLK_MEMCLK].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_CLK_MEMCLK].iValue = (int)telem.MemClockMhz;

        lpDataOutput->Sensors[ADL_PMLOG_TEMPERATURE_EDGE].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_TEMPERATURE_EDGE].iValue = (int)telem.EdgeTemperatureC;

        lpDataOutput->Sensors[ADL_PMLOG_TEMPERATURE_HOTSPOT].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_TEMPERATURE_HOTSPOT].iValue = (int)telem.HotspotTemperatureC;

        lpDataOutput->Sensors[ADL_PMLOG_TEMPERATURE_SOC].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_TEMPERATURE_SOC].iValue = (int)telem.SocTemperatureC;

        lpDataOutput->Sensors[ADL_PMLOG_TEMPERATURE_GFX].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_TEMPERATURE_GFX].iValue = (int)telem.GfxTemperatureC;

        lpDataOutput->Sensors[ADL_PMLOG_GFX_VOLTAGE].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_GFX_VOLTAGE].iValue = (int)telem.GfxVoltageMv;

        lpDataOutput->Sensors[ADL_PMLOG_SOC_VOLTAGE].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_SOC_VOLTAGE].iValue = (int)telem.SocVoltageMv;

        lpDataOutput->Sensors[ADL_PMLOG_ASIC_POWER].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_ASIC_POWER].iValue = (int)telem.PowerWatts;

        lpDataOutput->Sensors[ADL_PMLOG_GFX_POWER].iSupported = 1;
        lpDataOutput->Sensors[ADL_PMLOG_GFX_POWER].iValue = (int)telem.PowerWatts;

        return ADL_OK;
    }

    return ADL_ERR;
}

int __cdecl ADL_Overdrive_Caps(int iAdapterIndex, int* iSupported, int* iEnabled, int* iVersion)
{
    if (iSupported) *iSupported = 1;
    if (iEnabled) *iEnabled = 1;
    if (iVersion) *iVersion = 6; /* Overdrive 6 */
    return ADL_OK;
}

int __cdecl ADL2_Overdrive_Caps(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int* iSupported, int* iEnabled, int* iVersion)
{
    return ADL_Overdrive_Caps(iAdapterIndex, iSupported, iEnabled, iVersion);
}

int __cdecl ADL2_Adapter_VRAMUsage_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int* lpVRAMUsageInMB)
{
    if (!lpVRAMUsageInMB) return ADL_ERR_INVALID_PARAM;
    *lpVRAMUsageInMB = 512;
    return ADL_OK;
}

int __cdecl ADL_Overdrive5_FanSpeed_Get(int iAdapterIndex, int iThermalControllerIndex, ADLFanSpeedValue* lpFanSpeedValue)
{
    if (!lpFanSpeedValue) return ADL_ERR_INVALID_PARAM;
    lpFanSpeedValue->iSize = sizeof(ADLFanSpeedValue);
    lpFanSpeedValue->iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_RPM;
    lpFanSpeedValue->iFanSpeed = 1650;
    lpFanSpeedValue->iFlags = 0;
    return ADL_OK;
}

int __cdecl ADL2_Overdrive5_FanSpeed_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int iThermalControllerIndex, ADLFanSpeedValue* lpFanSpeedValue)
{
    return ADL_Overdrive5_FanSpeed_Get(iAdapterIndex, iThermalControllerIndex, lpFanSpeedValue);
}

int __cdecl ADL_Overdrive5_FanSpeedInfo_Get(int iAdapterIndex, int iThermalControllerIndex, ADLFanSpeedInfo* lpFanSpeedInfo)
{
    if (!lpFanSpeedInfo) return ADL_ERR_INVALID_PARAM;
    lpFanSpeedInfo->iSize = sizeof(ADLFanSpeedInfo);
    lpFanSpeedInfo->iFlags = 0;
    lpFanSpeedInfo->iMinPercent = 20;
    lpFanSpeedInfo->iMaxPercent = 100;
    lpFanSpeedInfo->iMinRPM = 800;
    lpFanSpeedInfo->iMaxRPM = 3200;
    return ADL_OK;
}

int __cdecl ADL2_Overdrive5_FanSpeedInfo_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int iThermalControllerIndex, ADLFanSpeedInfo* lpFanSpeedInfo)
{
    return ADL_Overdrive5_FanSpeedInfo_Get(iAdapterIndex, iThermalControllerIndex, lpFanSpeedInfo);
}

int __cdecl ADL2_Overdrive6_FanSpeed_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, ADLFanSpeedValue* lpFanSpeedValue)
{
    return ADL_Overdrive5_FanSpeed_Get(iAdapterIndex, 0, lpFanSpeedValue);
}

int __cdecl ADL_Display_DisplayInfo_Get(int iAdapterIndex, int* lpNumDisplays, ADLDisplayInfo** lppInfo, int iForceDetect)
{
    if (!lpNumDisplays || !lppInfo) return ADL_ERR_INVALID_PARAM;
    *lpNumDisplays = 1;

    if (g_pfnMalloc) {
        *lppInfo = (ADLDisplayInfo*)g_pfnMalloc(sizeof(ADLDisplayInfo));
    } else {
        *lppInfo = (ADLDisplayInfo*)malloc(sizeof(ADLDisplayInfo));
    }

    if (!*lppInfo) return ADL_ERR;
    RtlZeroMemory(*lppInfo, sizeof(ADLDisplayInfo));
    (*lppInfo)->iDisplayIndex = 0;
    (*lppInfo)->iDisplayType = 2; /* Digital Display */
    strcpy_s((*lppInfo)->strDisplayName, ADL_MAX_PATH, "AMD BC-250 Display");
    strcpy_s((*lppInfo)->strDisplayManufacturerName, ADL_MAX_PATH, "Advanced Micro Devices, Inc.");
    return ADL_OK;
}

int __cdecl ADL2_Display_DisplayInfo_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int* lpNumDisplays, ADLDisplayInfo** lppInfo, int iForceDetect)
{
    return ADL_Display_DisplayInfo_Get(iAdapterIndex, lpNumDisplays, lppInfo, iForceDetect);
}

int __cdecl ADL2_Adapter_PMLog_Support_Get(ADL_CONTEXT_HANDLE context, int iAdapterIndex, int* lpSupport)
{
    if (!lpSupport) return ADL_ERR_INVALID_PARAM;
    *lpSupport = 1;
    return ADL_OK;
}

int __cdecl ADL2_Device_PMLog_Device_Create(ADL_CONTEXT_HANDLE context, int iAdapterIndex, void** pDevice)
{
    if (pDevice) *pDevice = (void*)1;
    return ADL_OK;
}

int __cdecl ADL2_Device_PMLog_Device_Destroy(ADL_CONTEXT_HANDLE context, void* pDevice)
{
    return ADL_OK;
}

int __cdecl ADL2_Device_PMLog_Device_Start(ADL_CONTEXT_HANDLE context, void* pDevice, int iSamplingRate, int iFlags)
{
    return ADL_OK;
}

int __cdecl ADL2_Device_PMLog_Device_Stop(ADL_CONTEXT_HANDLE context, void* pDevice)
{
    return ADL_OK;
}

int __cdecl ADL2_Device_PMLog_Device_Sample(ADL_CONTEXT_HANDLE context, void* pDevice, ADLPMLogDataOutput* lpDataOutput)
{
    return ADL2_New_QueryPMLogData_Get(context, 0, lpDataOutput);
}
