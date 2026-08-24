/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    common.h

Abstract:
    Common header for AMD BC-250 Low-Level CLI Tools & Control Center Studio.
    Provides D3DKMT private escape interface, SMN definitions, and hardware structs.

--*/

#ifndef _BC250_TOOLS_COMMON_H_
#define _BC250_TOOLS_COMMON_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
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

/* Escapes */
#define BC250_ESCAPE_CMD_SUBMIT_PM4         0x01
#define BC250_ESCAPE_CMD_READ_REG           0x02
#define BC250_ESCAPE_CMD_WRITE_REG          0x03
#define BC250_ESCAPE_CMD_QUERY_INFO         0x04
#define BC250_ESCAPE_SDMA_COPY              0xBD04u
#define BC250_ESCAPE_SDMA_BENCHMARK         0xBD05u
#define BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS 0xBD06u
#define BC250_ESCAPE_CMD_COMPUTE_DISPATCH   0xBD07u
#define BC250_ESCAPE_CMD_VRAM_WRITE         0xBD08u
#define BC250_ESCAPE_CMD_VRAM_READ          0xBD09u
#define BC250_ESCAPE_CMD_SUBMIT_IB          0xBD0Bu
#define BC250_ESCAPE_CMD_QUERY_TELEMETRY    0xBD10u
#define BC250_ESCAPE_SMN_READ               0xBD20u
#define BC250_ESCAPE_SMN_WRITE              0xBD21u
#define BC250_ESCAPE_SMU_SEND_MSG           0xBD22u
#define BC250_ESCAPE_PSP_RAW_CMD            0xBD30u
#define BC250_ESCAPE_QUERY_RING_STATUS      0xBD40u
#define BC250_ESCAPE_QUERY_MEMORY_MAP       0xBD41u
#define BC250_ESCAPE_QUERY_EVENT_LOG        0xBD42u

typedef struct _BC250_ESCAPE_HEADER {
    ULONG CommandId;
    ULONG Status;
    ULONG InSize;
    ULONG OutSize;
} BC250_ESCAPE_HEADER;

typedef struct _BC250_WDDM_ESCAPE_SMN_RW {
    BC250_ESCAPE_HEADER Header;
    ULONG     Address;
    ULONG     Value;
} BC250_WDDM_ESCAPE_SMN_RW;

typedef struct _BC250_WDDM_ESCAPE_SMU_MSG {
    BC250_ESCAPE_HEADER Header;
    ULONG     QueueId;
    ULONG     MsgId;
    ULONG     Param;
    ULONG     SmuStatus;
    ULONG     ArgReadback;
} BC250_WDDM_ESCAPE_SMU_MSG;

typedef struct _BC250_WDDM_ESCAPE_PSP_RAW {
    BC250_ESCAPE_HEADER Header;
    ULONG     Opcode;
    ULONG     SubOp;
    ULONG     BufSizeBytes;
    ULONG     Data[128];
    ULONG     RespStatus;
    ULONG     RespSession;
    ULONG     FenceValue;
    ULONG     DurationUs;
} BC250_WDDM_ESCAPE_PSP_RAW;

typedef struct _BC250_WDDM_ESCAPE_RING_STATUS {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG CpRingGpuBase;
    ULONG     CpRingSizeBytes;
    ULONG     CpRingWptr;
    ULONG     CpRingRptr;
    ULONG     CpRingOccupancyBytes;
    ULONG     CpRingSubmitCount;
    ULONGLONG SdmaRingGpuBase;
    ULONG     SdmaRingSizeBytes;
    ULONG     SdmaRingWptr;
    ULONG     SdmaRingRptr;
    ULONG     SdmaRingOccupancyBytes;
    ULONG     SdmaCopyCount;
    ULONG     SdmaBandwidthMBs;
    ULONGLONG PspRingGpuBase;
    ULONG     PspRingSizeBytes;
    ULONG     PspRingWptr;
    ULONG     PspFenceValue;
    ULONG     PspCmdCount;
    ULONG     GrbmStatus;
    ULONG     CpStat;
    LARGE_INTEGER TimestampQpc;
    LARGE_INTEGER QpcFrequency;
} BC250_WDDM_ESCAPE_RING_STATUS;

typedef struct _BC250_WDDM_ESCAPE_MEMORY_MAP {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG TotalPhysicalBytes;
    ULONGLONG Bar0PhysicalBase;
    ULONGLONG Bar0ApertureBytes;
    ULONGLONG GopFbBase;
    ULONG     GopFbSizeBytes;
    ULONGLONG StagingComputeBase;
    ULONG     StagingComputeBytes;
    ULONGLONG TmrBase;
    ULONG     TmrSizeBytes;
    ULONGLONG SdmaRingBase;
    ULONG     SdmaRingSizeBytes;
    ULONGLONG CpRingBase;
    ULONG     CpRingSizeBytes;
    ULONGLONG DynamicPoolBase;
    ULONGLONG DynamicPoolBytes;
    ULONG     ScreenWidth;
    ULONG     ScreenHeight;
    ULONG     ScreenPitch;
} BC250_WDDM_ESCAPE_MEMORY_MAP;

#define BC250_EVENT_LOG_MAX 32

typedef struct _BC250_EVENT_ENTRY {
    ULONG         EventId;
    ULONG         Param1;
    ULONG         Param2;
    LARGE_INTEGER Timestamp;
} BC250_EVENT_ENTRY;

typedef struct _BC250_WDDM_ESCAPE_EVENT_LOG {
    BC250_ESCAPE_HEADER Header;
    ULONG             TotalEventsCount;
    ULONG             EntryCount;
    ULONG             PresentCount;
    ULONG             VsyncCount;
    ULONG             IbSubmitCount;
    ULONG             ComputeDispatchCount;
    ULONG             SdmaCopyCount;
    BC250_EVENT_ENTRY Entries[BC250_EVENT_LOG_MAX];
} BC250_WDDM_ESCAPE_EVENT_LOG;

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

/* Open Adapter Helper */
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

static inline BOOL CallBc250Escape(D3DKMT_HANDLE hAdapter, VOID* pData, UINT size)
{
    LONG status = CallBc250EscapeStatus(hAdapter, pData, size);
    return (status >= 0);
}

#endif /* _BC250_TOOLS_COMMON_H_ */
