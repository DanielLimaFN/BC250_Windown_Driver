/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_framework.h

Abstract:
    Framework unificado de testes de silicio e comunicacao WDDM D3DKMT.
    Implementa a metodologia de medicao honesta com 3 estados:
      - PROVADO: Todas as condicoes fisicas atendidas no silicio.
      - NAO VALIDADO EM HARDWARE: Executou sem confirmacao no silicio.
      - BLOQUEADO: Pre-requisito de hardware/firmware ausente.
      - ERRO: Falha de comunicacao com o driver WDDM.

--*/

#ifndef _TEST_FRAMEWORK_H_
#define _TEST_FRAMEWORK_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

/* =========================================================================
   D3DKMT Direct Types & Definitions
   ========================================================================= */

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

/* =========================================================================
   BC250 Driver Escape Command Codes & Constants
   ========================================================================= */

#define BC250_ESCAPE_CMD_SUBMIT_PM4         0x01u
#define BC250_ESCAPE_CMD_READ_REG           0x02u
#define BC250_ESCAPE_CMD_WRITE_REG          0x03u
#define BC250_ESCAPE_CMD_QUERY_INFO         0x04u
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
#define BC250_ESCAPE_SET_POWER_PROFILE      0xBD23u
#define BC250_ESCAPE_PSP_RAW_CMD            0xBD30u
#define BC250_ESCAPE_QUERY_RING_STATUS      0xBD40u
#define BC250_ESCAPE_QUERY_MEMORY_MAP       0xBD41u

#define BC250_VRAM_GPU_BASE                 0x000000F400000000ULL

/* Registradores Fisicos RDNA1 (Cyan Skillfish / Oberon) */
#define REG_CP_SCRATCH                      0x032D4u
#define REG_CP_SCRATCH2                     0x032D8u
#define REG_CP_SCRATCH3                     0x030E0u
#define REG_GRBM_STATUS                     0x03260u
#define REG_GRBM_STATUS2                    0x0326Cu
#define REG_GRBM_GFX_INDEX                  0x034D0u
#define REG_SPI_PG_STATIC_WGP               0x05C3Cu
#define REG_SPI_PG_MASK                     0x034FCu
#define REG_CC_GC_SHADER_ARRAY              0x09C1Cu
#define REG_RLC_PG_ALWAYS_ON                0x03D64u
#define REG_HDP_READ_CACHE_INVALIDATE       0x03FC4u
#define REG_HDP_MEM_COHERENCY_FLUSH_CNTL    0x04A80u

#define REG_CP_ME_CNTL                      0x086D8u
#define REG_CP_RB0_BASE                     0x0C100u
#define REG_CP_RB0_CNTL                     0x0C104u
#define REG_CP_RB0_RPTR_ADDR                0x0C10Cu
#define REG_CP_RB0_RPTR_ADDR_HI             0x0C110u
#define REG_CP_RB0_WPTR                     0x0C150u
#define REG_CP_RING0_BASE_LO                0x0DA60u
#define REG_CP_RING0_RPTR                   0x0DA6Cu
#define REG_CP_RING0_WPTR                   0x0DA78u

#define REG_PSP_C2PMSG_35                   0x5818Cu
#define REG_PSP_C2PMSG_36                   0x58190u
#define REG_PSP_C2PMSG_37                   0x58194u
#define REG_PSP_C2PMSG_64                   0x58200u
#define REG_PSP_C2PMSG_81                   0x58244u
#define REG_COMPUTE_DISPATCH_INITIATOR      0x080E0u

/* SMU Mailboxes SMN */
#define SMN_SMU_Q0_CMD                      0x03B10A08u
#define SMN_SMU_Q0_ARG                      0x03B10A48u
#define SMN_SMU_Q0_RSP                      0x03B10A68u
#define SMN_SMU_Q2_CMD                      0x03B10528u
#define SMN_SMU_Q2_RSP                      0x03B10564u
#define SMN_SMU_Q2_ARG                      0x03B10998u

/* Offsets invalidos conhecidos (Fill pattern / desmapeado) */
#define REG_CP_STAT_INVALID                 0x08684u
#define REG_CP_RB0_RPTR_SUSPECT             0x08700u

/* =========================================================================
   Escape Data Structures
   ========================================================================= */

typedef struct _BC250_ESCAPE_HEADER {
    ULONG CommandId;
    ULONG Status;
    ULONG InSize;
    ULONG OutSize;
} BC250_ESCAPE_HEADER;

typedef struct _BC250_ESC_REG {
    BC250_ESCAPE_HEADER Header;
    ULONG Offset;
    ULONG Value;
} BC250_ESC_REG;

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
    ULONG GfxTemperature;
    ULONG GfxTemperatureC;
    ULONG SocTemperatureC;
    ULONG EdgeTemperatureC;
    ULONG HotspotTemperatureC;
    ULONG GfxVoltageMv;
    ULONG SocVoltageMv;
    ULONG GfxClockMhz;
    ULONG SocClockMhz;
    ULONG MemClockMhz;
    ULONG PowerMilliwatts;
    ULONG PowerWatts;
    ULONG SmuStatus;
    ULONG Flags;
} BC250_WDDM_ESCAPE_TELEMETRY;

typedef struct _BC250_WDDM_ESCAPE_SDMA_COPY {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG SrcGpuAddress;
    ULONGLONG DstGpuAddress;
    ULONG     SizeBytes;
    ULONG     FenceValue;
    ULONG     DurationUs;
    ULONG     BandwidthMBs;
} BC250_WDDM_ESCAPE_SDMA_COPY;

typedef struct _BC250_WDDM_ESCAPE_SUBMIT {
    BC250_ESCAPE_HEADER Header;
    ULONG DwordCount;
    ULONG RptrBefore;
    ULONG RptrAfter;
    ULONG Data[256];
} BC250_WDDM_ESCAPE_SUBMIT;

typedef struct _BC250_WDDM_ESCAPE_VRAM_RW {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG VramOffset;
    ULONG     ByteCount;
    ULONG     Data[1024];
} BC250_WDDM_ESCAPE_VRAM_RW;

typedef struct _BC250_WDDM_ESCAPE_SMN_RW {
    BC250_ESCAPE_HEADER Header;
    ULONG Address;
    ULONG Value;
} BC250_WDDM_ESCAPE_SMN_RW;

typedef struct _BC250_WDDM_ESCAPE_SMU_MSG {
    BC250_ESCAPE_HEADER Header;
    ULONG QueueId;
    ULONG MsgId;
    ULONG Param;
    ULONG SmuStatus;
    ULONG ArgReadback;
} BC250_WDDM_ESCAPE_SMU_MSG;

/* =========================================================================
   Verdict Types and Test Reporting Framework
   ========================================================================= */

typedef enum _TEST_VERDICT_TYPE {
    VERDICT_PROVED = 0,             /* PROVADO NO SILICIO */
    VERDICT_NOT_VALIDATED = 1,      /* NAO VALIDADO EM HARDWARE */
    VERDICT_BLOCKED = 2,            /* BLOQUEADO (PRE-REQUISITO AUSENTE) */
    VERDICT_ERROR = 3               /* ERRO DE COMUNICACAO / DRIVER */
} TEST_VERDICT_TYPE;

typedef struct _TEST_RESULT {
    const char* TestId;
    const char* TestTitle;
    TEST_VERDICT_TYPE Verdict;
    char Description[1024];
    char Criterion[512];
    char Measured[1024];
    char Details[1024];
} TEST_RESULT;

static inline const char* VerdictToString(TEST_VERDICT_TYPE v)
{
    switch (v) {
    case VERDICT_PROVED:        return "PROVADO";
    case VERDICT_NOT_VALIDATED: return "NAO VALIDADO EM HARDWARE";
    case VERDICT_BLOCKED:       return "BLOQUEADO";
    case VERDICT_ERROR:         return "ERRO";
    default:                    return "DESCONHECIDO";
    }
}

static inline void PrintTestHeader(const char* id, const char* title, const char* description)
{
    printf("===============================================================================\n");
    printf(" [%s] %s\n", id, title);
    printf("-------------------------------------------------------------------------------\n");
    printf(" [DESCRICAO DETALHADA]\n %s\n", description);
    printf("===============================================================================\n");
}

static inline void PrintTestResult(const TEST_RESULT* res, bool jsonOutput)
{
    if (jsonOutput) {
        printf("{\n");
        printf("  \"id\": \"%s\",\n", res->TestId);
        printf("  \"title\": \"%s\",\n", res->TestTitle);
        printf("  \"verdict\": \"%s\",\n", VerdictToString(res->Verdict));
        printf("  \"description\": \"%s\",\n", res->Description);
        printf("  \"criterion\": \"%s\",\n", res->Criterion);
        printf("  \"measured\": \"%s\",\n", res->Measured);
        printf("  \"details\": \"%s\"\n", res->Details);
        printf("}\n");
        return;
    }

    printf("\n[CRITERIO] %s\n", res->Criterion);
    printf("[MEDIDO]   %s\n", res->Measured);
    if (strlen(res->Details) > 0) {
        printf("[DETALHES] %s\n", res->Details);
    }
    printf("[VEREDITO] ");
    switch (res->Verdict) {
    case VERDICT_PROVED:
        printf(">>> PROVADO <<<\n\n");
        break;
    case VERDICT_NOT_VALIDATED:
        printf(">>> FALHOU - NAO VALIDADO EM HARDWARE <<<\n\n");
        break;
    case VERDICT_BLOCKED:
        printf(">>> BLOQUEADO - PRE-REQUISITO AUSENTE <<<\n\n");
        break;
    case VERDICT_ERROR:
        printf(">>> ERRO - FALHA NA COMUNICACAO COM DRIVER <<<\n\n");
        break;
    }
}

/* =========================================================================
   Low-Level D3DKMT Helpers
   ========================================================================= */

static inline D3DKMT_HANDLE BcOpenAdapter(void)
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

static inline void BcCloseAdapter(D3DKMT_HANDLE hAdapter)
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

static inline bool BcEscape(D3DKMT_HANDLE hAdapter, VOID* pData, UINT size)
{
    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) return false;

    PFN_D3DKMTEscape pfnEscape = (PFN_D3DKMTEscape)GetProcAddress(hGdi, "D3DKMTEscape");
    if (!pfnEscape) return false;

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = pData;
    esc.PrivateDriverDataSize = size;

    LONG status = pfnEscape(&esc);
    return (status >= 0);
}

static inline ULONG BcRegRead(D3DKMT_HANDLE hAdapter, ULONG offset)
{
    BC250_ESC_REG r = { 0 };
    r.Header.CommandId = BC250_ESCAPE_CMD_READ_REG;
    r.Header.InSize = sizeof(r);
    r.Header.OutSize = sizeof(r);
    r.Offset = offset;
    if (!BcEscape(hAdapter, &r, sizeof(r))) {
        return 0xBADBAD00;
    }
    return r.Value;
}

static inline bool BcRegWrite(D3DKMT_HANDLE hAdapter, ULONG offset, ULONG val)
{
    BC250_ESC_REG r = { 0 };
    r.Header.CommandId = BC250_ESCAPE_CMD_WRITE_REG;
    r.Header.InSize = sizeof(r);
    r.Header.OutSize = sizeof(r);
    r.Offset = offset;
    r.Value = val;
    return BcEscape(hAdapter, &r, sizeof(r));
}

static inline ULONG BcSmnRead(D3DKMT_HANDLE hAdapter, ULONG addr)
{
    BC250_WDDM_ESCAPE_SMN_RW r = { 0 };
    r.Header.CommandId = BC250_ESCAPE_SMN_READ;
    r.Header.InSize = sizeof(r);
    r.Header.OutSize = sizeof(r);
    r.Address = addr;
    if (!BcEscape(hAdapter, &r, sizeof(r))) {
        return 0xBADBAD00;
    }
    return r.Value;
}

static inline bool BcSmnWrite(D3DKMT_HANDLE hAdapter, ULONG addr, ULONG val)
{
    BC250_WDDM_ESCAPE_SMN_RW r = { 0 };
    r.Header.CommandId = BC250_ESCAPE_SMN_WRITE;
    r.Header.InSize = sizeof(r);
    r.Header.OutSize = sizeof(r);
    r.Address = addr;
    r.Value = val;
    return BcEscape(hAdapter, &r, sizeof(r));
}

#endif /* _TEST_FRAMEWORK_H_ */
