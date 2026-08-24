/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_common.h

Abstract:
    Common header for AMD BC-250 (BC250-V3) test suite.
    Provides low-level D3DKMT escape interface helpers.

--*/

#ifndef _TEST_COMMON_H_
#define _TEST_COMMON_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

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

/* Escape Command IDs */
#define BC250_ESCAPE_CMD_SUBMIT_PM4         0x01
#define BC250_ESCAPE_CMD_READ_REG           0x02
#define BC250_ESCAPE_CMD_WRITE_REG          0x03
#define BC250_ESCAPE_CMD_QUERY_INFO         0x04
#define BC250_ESCAPE_SDMA_COPY              0xBD04u
#define BC250_ESCAPE_SDMA_BENCHMARK         0xBD05u
#define BC250_ESCAPE_SDMA_FILL              0xBD0Cu
#define BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS 0xBD06u
#define BC250_ESCAPE_CMD_COMPUTE_DISPATCH   0xBD07u
#define BC250_ESCAPE_CMD_VRAM_WRITE         0xBD08u
#define BC250_ESCAPE_CMD_VRAM_READ          0xBD09u
#define BC250_ESCAPE_CMD_SUBMIT_IB          0xBD0Bu
#define BC250_ESCAPE_CMD_QUERY_TELEMETRY    0xBD10u
#define BC250_ESCAPE_SET_POWER_PROFILE      0xBD23u

typedef struct _BC250_ESCAPE_HEADER {
    ULONG CommandId;
    ULONG Status;
    ULONG InSize;
    ULONG OutSize;
} BC250_ESCAPE_HEADER;

typedef struct _BC250_WDDM_ESCAPE_INFO {
    BC250_ESCAPE_HEADER Header;
    ULONG DeviceId;
    ULONG HardwareInitialized;
    ULONG GfxWptr;
    ULONG GfxRptr;
    ULONG GrbmStatus;
    ULONG CpStat;
    ULONGLONG VramGpuBase;
    ULONGLONG RingGpuBase;
    ULONG SdmaInitialized;
    ULONG SdmaWptr;
    ULONG SdmaRptr;
    ULONG SdmaBandwidthMBs;
    ULONG SdmaCopyCount;
} BC250_WDDM_ESCAPE_INFO;

typedef struct _BC250_WDDM_ESCAPE_TELEMETRY {
    BC250_ESCAPE_HEADER Header;
    ULONG     GfxTemperature;
    ULONG     GfxTemperatureC;
    ULONG     SocTemperatureC;
    ULONG     EdgeTemperatureC;
    ULONG     HotspotTemperatureC;
    ULONG     GfxVoltageMv;
    ULONG     SocVoltageMv;
    ULONG     GfxClockMhz;
    ULONG     SocClockMhz;
    ULONG     MemClockMhz;
    ULONG     PowerMilliwatts;
    ULONG     PowerWatts;
    ULONG     SmuStatus;
    ULONG     Flags;
} BC250_WDDM_ESCAPE_TELEMETRY;

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_CAPS {
    BC250_ESCAPE_HEADER Header;
    ULONG     NumCu;
    ULONG     NumWgp;
    ULONG     NumSimdPerCu;
    ULONG     NumTotalShaders;
    ULONG     GpuClockMhz;
    ULONG     GfxArch;
    ULONG     MaxWorkgroupSize;
    ULONG     WaveSize;
    ULONG     LdsSizeBytes;
    ULONGLONG VramTotalBytes;
    ULONGLONG ComputeVramBase;
    ULONG     ComputeVramSize;
    ULONG     DispatchCount;
    /* SchemaVersion != 0 significa que este KMD preenche os campos de
       procedencia abaixo. Zero = driver anterior a' Fase 0: os campos sao
       lixo/zero e NAO podem ser interpretados como "detectado". */
    ULONG     SchemaVersion;
    /* Procedencia dos numeros (Fase 0 e Fase 1): 0 = DETECTADO, 1 = ASSUMIDO. */
    ULONG     CuCountSource;
    ULONG     VramSizeSource;
    ULONG     WgpMaskRaw;
    ULONG     FirmwareReady;
    /* Fase 1: GFXOFF, CPU cores e ReBAR */
    ULONG     GfxOffDisabled;   /* SEMPRE 0: GFXOFF nao se aplica a este ASIC */
    ULONG     GfxOffApplicable; /* 0 = nao aplicavel neste ASIC */
    ULONG     CpuCoresActive;
    ULONG     ReBarActive;
    ULONGLONG Bar0SizeBytes;
    ULONGLONG GttSizeBytes;
    ULONG     GttSizeSource;
} BC250_WDDM_ESCAPE_COMPUTE_CAPS;

typedef struct _BC250_WDDM_ESCAPE_VRAM_RW {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG VramOffset;
    ULONG     ByteCount;
    ULONG     Data[1024];
} BC250_WDDM_ESCAPE_VRAM_RW;

typedef struct _BC250_WDDM_ESCAPE_SDMA_COPY {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG SrcGpuAddress;
    ULONGLONG DstGpuAddress;
    ULONG     SizeBytes;
    ULONG     FenceValue;          /* valor ESPERADO da fence */
    ULONG     DurationUs;
    ULONG     BandwidthMBs;
    /* --- Evidencia fisica (Fase 0, item 0.5). ---
       O loop de conclusao aceitava RPTR==WPTR OU fence. Isso confunde
       "o motor consumiu o frame" com "o motor executou a copia": e' possivel
       o RPTR alcancar o WPTR e a copia nunca acontecer. Estes campos separam
       as duas coisas. */
    ULONG     FenceObserved;       /* valor REALMENTE lido da fence em VRAM */
    ULONGLONG FenceGpuAddress;
    ULONG     RptrBefore;
    ULONG     RptrAfter;
    ULONG     WptrTarget;
    ULONG     FenceWritten;        /* 1 = a GPU escreveu a fence esperada */
    ULONG     CompletedByRptrOnly; /* 1 = RPTR alcancou WPTR mas a fence NAO */
} BC250_WDDM_ESCAPE_SDMA_COPY;

typedef struct _BC250_WDDM_ESCAPE_SDMA_FILL {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG DstGpuAddress;
    ULONG     SizeBytes;
    ULONG     Pattern;
} BC250_WDDM_ESCAPE_SDMA_FILL;

typedef struct _BC250_WDDM_ESCAPE_SDMA_BENCHMARK {
    BC250_ESCAPE_HEADER Header;
    ULONG     Iterations;
    ULONG     BlockSizeBytes;
    ULONG     AvgBandwidthMBs;
    ULONG     PeakBandwidthMBs;
} BC250_WDDM_ESCAPE_SDMA_BENCHMARK;

typedef struct _BC250_WDDM_ESCAPE_SUBMIT {
    BC250_ESCAPE_HEADER Header;
    ULONG DwordCount;
    ULONG RptrBefore;
    ULONG RptrAfter;
    ULONG Data[256];
} BC250_WDDM_ESCAPE_SUBMIT;

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_DISPATCH {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG ShaderGpuAddress;
    ULONG     DimX;
    ULONG     DimY;
    ULONG     DimZ;
    ULONG     ThreadsPerGroupX;
    ULONG     ThreadsPerGroupY;
    ULONG     ThreadsPerGroupZ;
    ULONG     UserData[8];
    ULONG     UserDataCount;
    ULONG     FenceValue;
    ULONG     DurationUs;
    ULONG     GrbmStatusBefore;
    ULONG     GrbmStatusAfter;
    /* Evidencia fisica (Fase 0) */
    ULONG     FenceObserved;
    ULONGLONG FenceGpuAddress;
    ULONG     ExecutionProven;
    ULONG     GrbmChanged;
} BC250_WDDM_ESCAPE_COMPUTE_DISPATCH;

typedef struct _BC250_WDDM_ESCAPE_POWER_PROFILE {
    BC250_ESCAPE_HEADER Header;
    ULONG     TargetFreqMhz;   /* 400 - 2350 MHz, or 0 for Unforce */
    ULONG     TargetVoltageMv; /* 700 - 1125 mV, or 0 for Unforce */
    ULONG     PerfProfile;     /* 1 (Eco/Idle) or 3 (Performance/IF) */
    ULONG     MaxTemperatureC; /* 60 - 85 C, default 85 */
    ULONG     Flags;           /* 0 = Apply Profile, 1 = Unforce/Restore Default */
    ULONG     ResultStatus;    /* Out: 0 = OK, error code */
    ULONG     CurrentFreqMhz;  /* Out: Readback frequency */
    ULONG     CurrentVid;      /* Out: Readback VID */
    ULONG     CurrentVoltageMv;/* Out: Readback Voltage in mV */
} BC250_WDDM_ESCAPE_POWER_PROFILE;

static inline D3DKMT_HANDLE OpenBc250Adapter(void)
{
    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) return 0;

    PFN_D3DKMTOpenAdapterFromHdc pfnOpen = (PFN_D3DKMTOpenAdapterFromHdc)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromHdc");
    if (!pfnOpen) return 0;

    HDC hdc = GetDC(NULL);
    if (!hdc) return 0;

    D3DKMT_OPENADAPTERFROMHDC openData = { 0 };
    openData.hDc = hdc;
    LONG status = pfnOpen(&openData);
    ReleaseDC(NULL, hdc);

    if (status >= 0 && openData.hAdapter != 0) {
        return openData.hAdapter;
    }
    return 0;
}

static inline LONG CallBc250EscapeStatus(D3DKMT_HANDLE hAdapter, VOID* pData, UINT size)
{
    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) return -1;

    PFN_D3DKMTEscape pfnEscape = (PFN_D3DKMTEscape)GetProcAddress(hGdi, "D3DKMTEscape");
    if (!pfnEscape) return -2;

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = pData;
    esc.PrivateDriverDataSize = size;

    return pfnEscape(&esc);
}

typedef struct _D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;
typedef LONG (APIENTRY *PFN_D3DKMTCloseAdapter)(const D3DKMT_CLOSEADAPTER*);

static inline VOID CloseBc250Adapter(D3DKMT_HANDLE hAdapter)
{
    if (!hAdapter) return;
    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (hGdi) {
        PFN_D3DKMTCloseAdapter pfnClose = (PFN_D3DKMTCloseAdapter)GetProcAddress(hGdi, "D3DKMTCloseAdapter");
        if (pfnClose) {
            D3DKMT_CLOSEADAPTER closeData = { hAdapter };
            pfnClose(&closeData);
        }
    }
}

static inline BOOL CallBc250Escape(D3DKMT_HANDLE hAdapter, VOID* pData, UINT size)
{
    LONG status = CallBc250EscapeStatus(hAdapter, pData, size);
    return (status >= 0);
}

#endif /* _TEST_COMMON_H_ */
