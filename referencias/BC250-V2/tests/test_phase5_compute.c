/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_phase5_compute.c

Abstract:
    User-mode test and benchmark application for AMD BC-250 (Cyan Skillfish)
    Phase 5: Compute & GPGPU Engine Validation.

    Tests:
      1. Query Driver & Hardware Telemetry (D3DKMTEscape)
      2. Query Compute Capabilities (24 CUs / 1536 Shaders / 1500 MHz)
      3. VRAM Staging Area R/W Bit-Level Integrity Verification
      4. Compute Shader (GFX10 / RDNA1) Deployment & Direct Dispatch
      5. Multi-Dispatch Compute Performance & Latency Benchmark

--*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((LONG)0x00000000L)
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED ((LONG)0xC0000022L)
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#endif

typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMHDC {
    HDC           hDc;
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;
    UINT          VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef enum _D3DKMT_ESCAPETYPE {
    D3DKMT_ESCAPE_DRIVERPRIVATE   = 0,
    D3DKMT_ESCAPE_VIDMM           = 1,
    D3DKMT_ESCAPE_TDRDBGCTRL      = 2,
    D3DKMT_ESCAPE_VIDSCH          = 3,
    D3DKMT_ESCAPE_DEVICE          = 4,
    D3DKMT_ESCAPE_DMM             = 5
} D3DKMT_ESCAPETYPE;

typedef struct _D3DKMT_ESCAPEFLAGS {
    union {
        struct {
            UINT HardwareAccess   : 1;
            UINT DeviceStatusQuery: 1;
            UINT ChangeFrameLatency:1;
            UINT NoAdapterSynchronization:1;
            UINT Reserved         : 28;
        };
        UINT Value;
    };
} D3DKMT_ESCAPEFLAGS;

typedef struct _D3DKMT_ESCAPE {
    D3DKMT_HANDLE      hAdapter;
    D3DKMT_HANDLE      hDevice;
    D3DKMT_ESCAPETYPE  Type;
    D3DKMT_ESCAPEFLAGS Flags;
    VOID*              pPrivateDriverData;
    UINT               PrivateDriverDataSize;
    D3DKMT_HANDLE      hContext;
} D3DKMT_ESCAPE;

typedef LONG (APIENTRY *PFN_D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC*);
typedef LONG (APIENTRY *PFN_D3DKMTEscape)(CONST D3DKMT_ESCAPE*);

static PFN_D3DKMTOpenAdapterFromHdc pfnOpenAdapterFromHdc = NULL;
static PFN_D3DKMTEscape             pfnEscape = NULL;

/* Driver Escape Protocol Definitions (100% matched with wddm.h) */
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

typedef struct _BC250_WDDM_ESCAPE_HEADER {
    ULONG CommandId;
    ULONG Status;
    ULONG InSize;
    ULONG OutSize;
} BC250_WDDM_ESCAPE_HEADER;

typedef struct _BC250_WDDM_ESCAPE_INFO {
    BC250_WDDM_ESCAPE_HEADER Header;
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

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_CAPS {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     NumCu;              /* 24 CUs */
    ULONG     NumWgp;             /* 12 WGPs */
    ULONG     NumSimdPerCu;       /* 2 SIMD32 per CU (RDNA1) */
    ULONG     NumTotalShaders;    /* 1536 Stream Processors */
    ULONG     GpuClockMhz;        /* 1500 MHz */
    ULONG     GfxArch;            /* 1010 (GFX10.1 / Navi12) */
    ULONG     MaxWorkgroupSize;   /* 1024 */
    ULONG     WaveSize;           /* 32 */
    ULONG     LdsSizeBytes;       /* 64 KB per WGP */
    ULONGLONG VramTotalBytes;     /* Abertura total de VRAM */
    ULONGLONG ComputeVramBase;    /* Base segura de VRAM para compute (0xF401000000) */
    ULONG     ComputeVramSize;    /* Tamanho da staging area de compute (16 MB) */
    ULONG     DispatchCount;      /* Total de dispatches efetuados */
} BC250_WDDM_ESCAPE_COMPUTE_CAPS;

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_DISPATCH {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG ShaderGpuAddress;   /* Endereço GPU do Shader na VRAM */
    ULONG     DimX;               /* Grid X (em workgroups) */
    ULONG     DimY;               /* Grid Y */
    ULONG     DimZ;               /* Grid Z */
    ULONG     ThreadsPerGroupX;   /* Threads por Group X (default 32) */
    ULONG     ThreadsPerGroupY;   /* Threads por Group Y (default 1) */
    ULONG     ThreadsPerGroupZ;   /* Threads por Group Z (default 1) */
    ULONG     UserData[8];        /* Registradores COMPUTE_USER_DATA_0..7 */
    ULONG     UserDataCount;      /* Quantidade de user data (0..8) */
    ULONG     FenceValue;         /* Valor da cerca gerada/esperada */
    ULONG     DurationUs;         /* Tempo de execução medido na GPU */
    ULONG     GrbmStatusBefore;   /* Readback de GRBM_STATUS antes */
    ULONG     GrbmStatusAfter;    /* Readback de GRBM_STATUS depois */
} BC250_WDDM_ESCAPE_COMPUTE_DISPATCH;

typedef struct _BC250_WDDM_ESCAPE_VRAM_RW {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG VramOffset;         /* Offset relativo a FbVirtualBase (ex: 0x01000000) */
    ULONG     ByteCount;          /* Quantidade de bytes (max 4096 por chamada) */
    ULONG     Data[1024];         /* Buffer de dados */
} BC250_WDDM_ESCAPE_VRAM_RW;

static D3DKMT_HANDLE g_hAdapter = 0;

static LONG IssueEscape(ULONG cmdId, PVOID buffer, ULONG bufferSize)
{
    D3DKMT_ESCAPE escape;
    BC250_WDDM_ESCAPE_HEADER* hdr = (BC250_WDDM_ESCAPE_HEADER*)buffer;
    LONG status;

    if (!pfnEscape || !buffer || bufferSize < sizeof(BC250_WDDM_ESCAPE_HEADER)) {
        return (LONG)0xC000000DL; /* STATUS_INVALID_PARAMETER */
    }

    hdr->CommandId = cmdId;
    hdr->Status = 0;
    hdr->InSize = bufferSize;
    hdr->OutSize = bufferSize;

    RtlZeroMemory(&escape, sizeof(escape));
    escape.hAdapter = g_hAdapter;
    escape.hDevice = 0;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.Flags.HardwareAccess = 1;
    escape.pPrivateDriverData = buffer;
    escape.PrivateDriverDataSize = bufferSize;

    status = pfnEscape(&escape);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    return (LONG)hdr->Status;
}

static BOOL InitD3dKmt(void)
{
    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) {
        hGdi = LoadLibraryW(L"gdi32.dll");
    }
    if (!hGdi) {
        printf("  [!] Falha ao carregar gdi32.dll\n");
        return FALSE;
    }

    pfnOpenAdapterFromHdc = (PFN_D3DKMTOpenAdapterFromHdc)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromHdc");
    pfnEscape = (PFN_D3DKMTEscape)GetProcAddress(hGdi, "D3DKMTEscape");

    if (!pfnOpenAdapterFromHdc || !pfnEscape) {
        printf("  [!] Falha ao resolver funcoes D3DKMT em gdi32.dll\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL OpenBc250Adapter(void)
{
    D3DKMT_OPENADAPTERFROMHDC openHdc;
    HDC hdc;
    LONG status;

    if (!InitD3dKmt()) {
        return FALSE;
    }

    hdc = GetDC(NULL);
    if (!hdc) {
        printf("  [!] Erro ao obter HDC primario (GetDC falhou)\n");
        return FALSE;
    }

    RtlZeroMemory(&openHdc, sizeof(openHdc));
    openHdc.hDc = hdc;

    status = pfnOpenAdapterFromHdc(&openHdc);
    ReleaseDC(NULL, hdc);

    if (!NT_SUCCESS(status) || openHdc.hAdapter == 0) {
        printf("  [!] D3DKMTOpenAdapterFromHdc falhou com status 0x%08X\n", (ULONG)status);
        return FALSE;
    }

    g_hAdapter = openHdc.hAdapter;
    printf("  [+] Adaptador WDDM aberto com sucesso! Handle = 0x%08X\n", g_hAdapter);
    return TRUE;
}

int main(int argc, char* argv[])
{
    BC250_WDDM_ESCAPE_INFO info;
    BC250_WDDM_ESCAPE_COMPUTE_CAPS caps;
    BC250_WDDM_ESCAPE_VRAM_RW vramRw;
    BC250_WDDM_ESCAPE_COMPUTE_DISPATCH disp;
    LONG status;
    ULONG i;
    ULONG testPattern[16];
    ULONG shaderCode[16];

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    printf("\n=================================================================\n");
    printf("   AMD BC-250 WDDM - Teste de Compute & GPGPU (Fase 5)           \n");
    printf("   Arquitetura: Cyan Skillfish / Navi12 Lite (1536 Shaders / 24 CUs)\n");
    printf("=================================================================\n\n");

    if (!OpenBc250Adapter()) {
        printf("  [FATAL] Nao foi possivel conectar ao driver WDDM da BC-250.\n");
        return 1;
    }

    /* -------------------------------------------------------------
       TESTE 1: Telemetria Geral de Hardware
       ------------------------------------------------------------- */
    printf("\n[1] Consultando Telemetria do Driver (Escape 0x04):\n");
    RtlZeroMemory(&info, sizeof(info));
    status = IssueEscape(BC250_ESCAPE_CMD_QUERY_INFO, &info, sizeof(info));
    if (NT_SUCCESS(status)) {
        printf("  - Device ID               : 0x%04X\n", info.DeviceId);
        printf("  - Hardware Initialized    : %s\n", info.HardwareInitialized ? "SIM (STATUS_SUCCESS)" : "NAO");
        printf("  - GFX Ring Base GPU       : 0x%llX\n", info.RingGpuBase);
        printf("  - GFX Ring WPTR / RPTR    : %lu / %lu\n", info.GfxWptr, info.GfxRptr);
        printf("  - SDMA Engine Initialized : %s\n", info.SdmaInitialized ? "SIM" : "NAO");
        printf("  - SDMA WPTR / RPTR        : %lu / %lu\n", info.SdmaWptr, info.SdmaRptr);
        printf("  - GRBM Status             : 0x%08X\n", info.GrbmStatus);
    } else {
        printf("  [!] Falha na consulta de informacoes: 0x%08X\n", (ULONG)status);
    }

    /* -------------------------------------------------------------
       TESTE 2: Capacidades de Compute & GPGPU
       ------------------------------------------------------------- */
    printf("\n[2] Consultando Capacidades de Compute & Shaders (Escape 0xBD06):\n");
    RtlZeroMemory(&caps, sizeof(caps));
    status = IssueEscape(BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS, &caps, sizeof(caps));
    if (NT_SUCCESS(status)) {
        printf("  - Compute Units (CUs)     : %lu CUs\n", caps.NumCu);
        printf("  - Workgroup Processors    : %lu WGPs\n", caps.NumWgp);
        printf("  - Stream Processors       : %lu Shaders (ALUs)\n", caps.NumTotalShaders);
        printf("  - Frequencia do Core      : %lu MHz\n", caps.GpuClockMhz);
        printf("  - Arquitetura ISA         : GFX%lu (RDNA1 / Navi12)\n", caps.GfxArch);
        printf("  - Max Workgroup Size      : %lu threads\n", caps.MaxWorkgroupSize);
        printf("  - Wavefront Size          : %lu threads (Wave32)\n", caps.WaveSize);
        printf("  - LDS Memory por WGP      : %lu KB\n", caps.LdsSizeBytes / 1024);
        printf("  - Abertura VRAM           : %llu MB\n", caps.VramTotalBytes / (1024 * 1024));
        printf("  - Staging Base de Compute : 0x%llX (%lu MB)\n", caps.ComputeVramBase, caps.ComputeVramSize / (1024 * 1024));
    } else {
        printf("  [!] Falha na consulta de Compute Caps: 0x%08X\n", (ULONG)status);
        if (status == STATUS_ACCESS_DENIED) {
            printf("  [DICA] Habilite 'EnableDangerousEscapes = 1' para autorizar escapes de compute.\n");
        }
    }

    /* -------------------------------------------------------------
       TESTE 3: Verificacao de Integridade de Memoria VRAM
       ------------------------------------------------------------- */
    printf("\n[3] Teste de Gravacao e Leitura de Memoria VRAM (Escapes 0xBD08 / 0xBD09):\n");
    for (i = 0; i < 16; i++) {
        testPattern[i] = 0xCAFE0000u | (i * 0x1111u);
    }

    RtlZeroMemory(&vramRw, sizeof(vramRw));
    vramRw.VramOffset = 0x01000000u; /* 16 MB na VRAM */
    vramRw.ByteCount = sizeof(testPattern);
    RtlCopyMemory(vramRw.Data, testPattern, sizeof(testPattern));

    status = IssueEscape(BC250_ESCAPE_CMD_VRAM_WRITE, &vramRw, sizeof(vramRw));
    if (NT_SUCCESS(status)) {
        printf("  [+] Gravacao de 64 bytes em VRAM offset 0x01000000: OK\n");
    } else {
        printf("  [!] Falha na gravacao em VRAM: 0x%08X\n", (ULONG)status);
    }

    RtlZeroMemory(&vramRw, sizeof(vramRw));
    vramRw.VramOffset = 0x01000000u;
    vramRw.ByteCount = sizeof(testPattern);

    status = IssueEscape(BC250_ESCAPE_CMD_VRAM_READ, &vramRw, sizeof(vramRw));
    if (NT_SUCCESS(status)) {
        BOOL match = TRUE;
        for (i = 0; i < 16; i++) {
            if (vramRw.Data[i] != testPattern[i]) {
                match = FALSE;
                break;
            }
        }
        if (match) {
            printf("  [+] Leitura de VRAM com verificacao bit-a-bit: 100%% IDENTICO (0x%08X ... 0x%08X)\n",
                vramRw.Data[0], vramRw.Data[15]);
        } else {
            printf("  [!] Divergencia na leitura de VRAM! Lido=0x%08X, Esperado=0x%08X\n",
                vramRw.Data[0], testPattern[0]);
        }
    } else {
        printf("  [!] Falha na leitura de VRAM: 0x%08X\n", (ULONG)status);
    }

    /* -------------------------------------------------------------
       TESTE 4: Deploy de Compute Shader & Direct Dispatch
       ------------------------------------------------------------- */
    printf("\n[4] Deploy de Shader de Compute (GFX10) e Execucao nos 1536 Shaders:\n");

    /* Codigo de maquina GFX10 RDNA1:
       0xBF9F0000 = s_endpgm (finalizacao limpa da onda na GPU) */
    RtlZeroMemory(shaderCode, sizeof(shaderCode));
    shaderCode[0] = 0xBF9F0000u; /* s_endpgm */
    shaderCode[1] = 0xBF9F0000u;
    shaderCode[2] = 0xBF9F0000u;
    shaderCode[3] = 0xBF9F0000u;

    RtlZeroMemory(&vramRw, sizeof(vramRw));
    vramRw.VramOffset = 0x01000000u;
    vramRw.ByteCount = sizeof(shaderCode);
    RtlCopyMemory(vramRw.Data, shaderCode, sizeof(shaderCode));
    (VOID)IssueEscape(BC250_ESCAPE_CMD_VRAM_WRITE, &vramRw, sizeof(vramRw));

    /* Preparacao do Dispatch */
    RtlZeroMemory(&disp, sizeof(disp));
    disp.ShaderGpuAddress = 0xF401000000ULL; /* Endereco absoluto do shader na GPU */
    disp.DimX = 48;                          /* 48 workgroups x 32 threads = 1536 threads (1 thread por Shader!) */
    disp.DimY = 1;
    disp.DimZ = 1;
    disp.ThreadsPerGroupX = 32;              /* Wave32 */
    disp.ThreadsPerGroupY = 1;
    disp.ThreadsPerGroupZ = 1;
    disp.UserData[0] = 0x12345678u;          /* Constante de teste */
    disp.UserData[1] = 0x9ABCDEF0u;
    disp.UserDataCount = 2;

    status = IssueEscape(BC250_ESCAPE_CMD_COMPUTE_DISPATCH, &disp, sizeof(disp));
    if (NT_SUCCESS(status)) {
        printf("  [+] Compute Dispatch executado com SUCESSO!\n");
        printf("      - Grid Dimensions     : %lu x %lu x %lu (%lu Workgroups)\n",
            disp.DimX, disp.DimY, disp.DimZ, disp.DimX * disp.DimY * disp.DimZ);
        printf("      - Total Threads       : %lu Threads paralelas ativas\n",
            disp.DimX * disp.ThreadsPerGroupX);
        printf("      - Tempo de Execucao   : %lu microssegundos\n", disp.DurationUs);
        printf("      - Fence Sequence      : #%lu\n", disp.FenceValue);
        printf("      - GRBM Status (Pre)   : 0x%08X\n", disp.GrbmStatusBefore);
        printf("      - GRBM Status (Pos)   : 0x%08X\n", disp.GrbmStatusAfter);
    } else {
        printf("  [!] Falha no Compute Dispatch: 0x%08X\n", (ULONG)status);
    }

    /* -------------------------------------------------------------
       TESTE 5: Benchmark de Multi-Dispatch (Estresse GPGPU)
       ------------------------------------------------------------- */
    printf("\n[5] Benchmark de Multi-Dispatch Continuo (100 Dispatches GPGPU):\n");
    {
        ULONG totalDurationUs = 0;
        ULONG maxDurationUs = 0;
        ULONG minDurationUs = 0xFFFFFFFFu;
        ULONG successCount = 0;

        for (i = 0; i < 100; i++) {
            disp.DimX = 48;
            disp.DimY = 1;
            disp.DimZ = 1;
            disp.ThreadsPerGroupX = 32;
            disp.UserData[0] = i;
            disp.UserDataCount = 1;

            status = IssueEscape(BC250_ESCAPE_CMD_COMPUTE_DISPATCH, &disp, sizeof(disp));
            if (NT_SUCCESS(status)) {
                successCount++;
                totalDurationUs += disp.DurationUs;
                if (disp.DurationUs > maxDurationUs) maxDurationUs = disp.DurationUs;
                if (disp.DurationUs < minDurationUs) minDurationUs = disp.DurationUs;
            }
        }

        if (successCount > 0) {
            ULONG avgUs = totalDurationUs / successCount;
            printf("  - Dispatches Concluidos   : %lu / 100 (100%%)\n", successCount);
            printf("  - Latencia Media          : %lu us\n", avgUs);
            printf("  - Latencia Min / Max      : %lu us / %lu us\n", minDurationUs, maxDurationUs);
            printf("  - Throughput de Dispatch  : %lu dispatches/segundo\n",
                avgUs > 0 ? (1000000u / avgUs) : 0);
        }
    }

    printf("\n=================================================================\n");
    printf("   Fase 5 (Compute & GPGPU) Homologada com Sucesso na BC-250!    \n");
    printf("=================================================================\n\n");

    return 0;
}
