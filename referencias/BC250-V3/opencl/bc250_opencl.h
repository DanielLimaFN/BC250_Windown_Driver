/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_opencl.h

Abstract:
    AMD BC-250 (Cyan Skillfish / Oberon) Native OpenCL 2.0 / GPGPU ICD Driver.
    Provides Khronos OpenCL platform, device, context, memory, and dispatch
    management backed by real silicon execution on 24 CUs / 1536 Shaders.

--*/

#ifndef _BC250_OPENCL_H_
#define _BC250_OPENCL_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "CL/cl.h"
#include "CL/cl_icd.h"
#include "CL/cl_ext.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

/* D3DKMT Types */
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

/* BC-250 Escape Command IDs */
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
#define BC250_ESCAPE_SET_POWER_PROFILE      0xBD23u

#define BC250_VRAM_GPU_BASE                 0x00000000ULL
#define BC250_STAGING_COMPUTE_BASE          0x01000000ULL /* 16 MB Offset */

typedef struct _BC250_ESCAPE_HEADER {
    ULONG CommandId;
    ULONG Status;
    ULONG InSize;
    ULONG OutSize;
} BC250_ESCAPE_HEADER;

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
    /* Procedencia dos numeros (Fase 0): 0 = DETECTADO, 1 = ASSUMIDO. */
    ULONG     CuCountSource;
    ULONG     VramSizeSource;
    ULONG     WgpMaskRaw;
    ULONG     FirmwareReady;
} BC250_WDDM_ESCAPE_COMPUTE_CAPS;

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

typedef struct _BC250_WDDM_ESCAPE_VRAM_RW {
    BC250_ESCAPE_HEADER Header;
    ULONGLONG VramOffset;
    ULONG     ByteCount;
    ULONG     Data[1024];
} BC250_WDDM_ESCAPE_VRAM_RW;

/* Internal OpenCL Object Definitions with Khronos ICD Dispatch Headers */

struct _cl_platform_id {
    struct _cl_icd_dispatch *dispatch;
    char platform_name[128];
    char vendor_name[128];
    char version[128];
    char profile[64];
    char extensions[512];
};

struct _cl_device_id {
    struct _cl_icd_dispatch *dispatch;
    cl_platform_id platform;
    char device_name[128];
    char vendor_name[128];
    char driver_version[64];
    char device_version[64];
    char opencl_c_version[64];
    cl_device_type device_type;
    cl_uint max_compute_units;
    cl_uint max_work_item_dimensions;
    size_t max_work_item_sizes[3];
    size_t max_work_group_size;
    cl_uint max_clock_frequency;
    cl_ulong global_mem_size;
    cl_ulong max_mem_alloc_size;
    cl_ulong local_mem_size;
    cl_uint preferred_vector_width_float;
    cl_uint native_vector_width_float;
    cl_bool available;
    cl_bool compiler_available;
    D3DKMT_HANDLE hAdapter;
};

struct _cl_context {
    struct _cl_icd_dispatch *dispatch;
    cl_device_id device;
    volatile LONG ref_count;
};

struct _cl_command_queue {
    struct _cl_icd_dispatch *dispatch;
    cl_context context;
    cl_device_id device;
    cl_command_queue_properties properties;
    volatile LONG ref_count;
};

struct _cl_mem {
    struct _cl_icd_dispatch *dispatch;
    cl_context context;
    cl_mem_flags flags;
    size_t size;
    void *host_ptr;
    ULONGLONG vram_gpu_address;
    ULONG vram_offset;
    volatile LONG ref_count;
};

struct _cl_program {
    struct _cl_icd_dispatch *dispatch;
    cl_context context;
    char *source;
    size_t source_len;
    unsigned char *binary;
    size_t binary_len;
    cl_build_status build_status;
    char build_log[2048];
    volatile LONG ref_count;
};

struct _cl_kernel {
    struct _cl_icd_dispatch *dispatch;
    cl_program program;
    char kernel_name[128];
    ULONG num_args;
    struct {
        size_t arg_size;
        cl_mem mem_obj;
        ULONG scalar_val;
        ULONG is_mem;
    } args[16];
    volatile LONG ref_count;
};

struct _cl_event {
    struct _cl_icd_dispatch *dispatch;
    cl_context context;
    cl_command_queue queue;
    cl_int execution_status;
    ULONG fence_value;
    volatile LONG ref_count;
};

#endif /* _BC250_OPENCL_H_ */
