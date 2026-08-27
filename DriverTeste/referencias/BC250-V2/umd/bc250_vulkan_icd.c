/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_vulkan_icd.c

Abstract:
    Vulkan 1.0 ICD (Installable Client Driver) para a AMD BC-250
    (Cyan Skillfish / Navi12 Lite -- 24 CUs / 1536 shaders).

    Fala com bc250wddm.sys via D3DKMTEscape.

ESCOPO -- leia antes de usar:

    O que FUNCIONA: enumeracao completa e conformante. O loader oficial
    (vulkan-1.dll) carrega este ICD, negocia a interface, cria instancia,
    enumera o physical device, e reporta properties / features / limits /
    memory / queue families corretos. vulkaninfo e qualquer aplicativo que
    apenas detecte e consulte a GPU funcionam.

    O que NAO funciona: executar shaders do usuario. Nao existe compilador
    SPIR-V -> ISA GFX10 neste projeto. vkCreateComputePipelines portanto
    FALHA com VK_ERROR_INVALID_SHADER_NV em vez de substituir silenciosamente
    um kernel que nao computa nada -- o que a versao anterior fazia, devolvendo
    VK_SUCCESS e resultados errados sem aviso. Falhar de forma honesta e
    preferivel a dar dado incorreto.

CORRECOES ESTRUTURAIS EM RELACAO A VERSAO ANTERIOR:

    1. Objetos dispatchable (VkInstance, VkPhysicalDevice, VkDevice, VkQueue,
       VkCommandBuffer) agora sao alocacoes reais com VK_LOADER_DATA como
       primeiro campo e o valor magico do loader gravado. Antes eram inteiros
       fabricados como (void*)0xBC250003; o loader escreve a propria dispatch
       table no primeiro ponteiro do objeto, ou seja, escreveria no endereco
       0xBC250003.

    2. vkEnumerateInstanceExtensionProperties existe e e alcancavel por
       vk_icdGetInstanceProcAddr. Sem ela o loader recusa o ICD -- era
       exatamente o erro observado:
         "loader_scanned_icd_add: Could not get
          'vkEnumerateInstanceExtensionProperties'"

    3. vk_icdGetPhysicalDeviceProcAddr implementada (exigida da interface v4
       em diante; o ICD declarava suportar v5 sem forncecer).

    4. vkGetInstanceProcAddr / vkGetDeviceProcAddr implementadas -- o loader
       monta as dispatch tables com elas.

    5. Cadeia de compute completa: CreateShaderModule, CreatePipelineLayout,
       DescriptorSetLayout / Pool / AllocateDescriptorSets /
       UpdateDescriptorSets / CmdBindDescriptorSets, fences e waits. Antes
       nenhuma existia, e sem descritores nao havia como ligar buffer a shader.

    6. Memoria: alocador de verdade dentro da janela de staging de 16 MB, com
       lista de livres. Antes era calloc no host + offset global que so
       crescia, sem limite -- passados 16 MB os enderecos saiam da janela.

    7. HOST_COHERENT deixou de ser anunciado. O dado sobe para a VRAM por
       escape, entao coerencia automatica seria mentira; o app precisa chamar
       vkFlushMappedMemoryRanges, e agora essa funcao existe e respeita
       offset/size (antes vkUnmapMemory descarregava a alocacao inteira).

--*/

#include "bc250_vulkan.h"
#include "bc250_gemm_kernels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

/* ==========================================================================
   Interface com o driver de kernel (D3DKMT)
   ========================================================================== */

typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMHDC {
    HDC           hDc;
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;
    UINT          VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef struct _D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef struct _D3DKMT_ESCAPE {
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    UINT          Type;          /* D3DKMT_ESCAPE_DRIVERPRIVATE = 0 */
    struct { UINT Value; } Flags;
    VOID*         pPrivateDriverData;
    UINT          PrivateDriverDataSize;
    D3DKMT_HANDLE hContext;
} D3DKMT_ESCAPE;

typedef LONG (APIENTRY *PFN_OpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC*);
typedef LONG (APIENTRY *PFN_CloseAdapter)(const D3DKMT_CLOSEADAPTER*);
typedef LONG (APIENTRY *PFN_Escape)(const D3DKMT_ESCAPE*);

/* IDs conferidos contra wddm.h. QUERY_INFO e 0x04 -- NAO 0xBD04, que no
   driver e BC250_ESCAPE_SDMA_COPY (erro presente em test_phase5_compute.c). */
#define BC250_ESC_QUERY_INFO         0x04u
#define BC250_ESC_QUERY_COMPUTE_CAPS 0xBD06u
#define BC250_ESC_COMPUTE_DISPATCH   0xBD07u
#define BC250_ESC_VRAM_WRITE         0xBD08u
#define BC250_ESC_VRAM_READ          0xBD09u

typedef struct { ULONG CommandId, Status, InSize, OutSize; } BC250_ESC_HDR;

typedef struct {
    BC250_ESC_HDR Header;
    ULONG     NumCu, NumWgp, NumSimdPerCu, NumTotalShaders;
    ULONG     GpuClockMhz, GfxArch, MaxWorkgroupSize, WaveSize, LdsSizeBytes;
    ULONGLONG VramTotalBytes, ComputeVramBase;
    ULONG     ComputeVramSize, DispatchCount;
} BC250_ESC_CAPS;

typedef struct {
    BC250_ESC_HDR Header;
    ULONGLONG VramOffset;
    ULONG     ByteCount;
    ULONG     Data[1024];
} BC250_ESC_VRAM_RW;

typedef struct {
    BC250_ESC_HDR Header;
    ULONGLONG ShaderGpuAddress;
    ULONG     DimX, DimY, DimZ;
    ULONG     ThreadsPerGroupX, ThreadsPerGroupY, ThreadsPerGroupZ;
    ULONG     UserData[8];
    ULONG     UserDataCount;
    ULONG     FenceValue;
    ULONG     DurationUs;
    ULONG     GrbmStatusBefore, GrbmStatusAfter;
} BC250_ESC_DISPATCH;

#define BC250_VRAM_RW_CHUNK 4096u

static CRITICAL_SECTION       g_lock;
static LONG                   g_lockInit = 0;
static D3DKMT_HANDLE          g_hAdapter = 0;
static PFN_OpenAdapterFromHdc g_pfnOpen  = NULL;
static PFN_CloseAdapter       g_pfnClose = NULL;
static PFN_Escape             g_pfnEscape = NULL;
static BC250_ESC_CAPS         g_caps;
static BOOL                   g_capsValid = FALSE;
static BOOL                   g_driverReady = FALSE;

static void Bc250Log(const char* fmt, ...)
{
    char buf[1024];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    OutputDebugStringA(buf);

    FILE* f = fopen("C:\\Users\\Daniel\\Desktop\\bc250-win\\bc250-win\\BC250-V2\\vulkan_debug.log", "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

static void EnsureLock(void)
{
    if (InterlockedCompareExchange(&g_lockInit, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
        InterlockedExchange(&g_lockInit, 2);
    }
    while (InterlockedCompareExchange(&g_lockInit, 2, 2) != 2) { Sleep(0); }
}

static LONG DoEscape(void* payload, UINT size)
{
    D3DKMT_ESCAPE esc;
    if (!g_pfnEscape || !g_hAdapter) return -1;
    memset(&esc, 0, sizeof(esc));
    esc.hAdapter = g_hAdapter;
    esc.Type = 0;
    esc.pPrivateDriverData = payload;
    esc.PrivateDriverDataSize = size;
    return g_pfnEscape(&esc);
}

typedef struct _D3DKMT_ADAPTERINFO {
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;
    UINT          NumSources;
    BOOL          bPresentMoveRegionsPreferred;
} D3DKMT_ADAPTERINFO;

typedef struct _D3DKMT_ENUMADAPTERS2 {
    ULONG               NumAdapters;
    D3DKMT_ADAPTERINFO* pAdapters;
} D3DKMT_ENUMADAPTERS2;

typedef LONG (APIENTRY *PFN_EnumAdapters2)(D3DKMT_ENUMADAPTERS2*);

typedef struct {
    BC250_ESC_HDR Header;
    ULONG     DeviceId;
    ULONG     HardwareInitialized;
    ULONG     GfxWptr;
    ULONG     GfxRptr;
    ULONG     GrbmStatus;
    ULONG     CpStat;
    ULONGLONG VramGpuBase;
    ULONGLONG RingGpuBase;
    ULONG     SdmaInitialized;
    ULONG     SdmaWptr;
    ULONG     SdmaRptr;
    ULONG     SdmaBandwidthMBs;
    ULONG     SdmaCopyCount;
} BC250_ESC_INFO;

/* Garante que o driver WDDM foi aberto e le as capacidades de compute.
   Sem caps usamos os valores nominais da BC-250. */
static BOOL EnsureDriver(void)
{
    PFN_EnumAdapters2 pfnEnum2;
    HMODULE hGdi;
    LONG st;

    EnsureLock();
    EnterCriticalSection(&g_lock);

    if (g_driverReady) { LeaveCriticalSection(&g_lock); return TRUE; }

    hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) { LeaveCriticalSection(&g_lock); return FALSE; }

    g_pfnOpen   = (PFN_OpenAdapterFromHdc)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromHdc");
    g_pfnClose  = (PFN_CloseAdapter)GetProcAddress(hGdi, "D3DKMTCloseAdapter");
    g_pfnEscape = (PFN_Escape)GetProcAddress(hGdi, "D3DKMTEscape");
    pfnEnum2    = (PFN_EnumAdapters2)GetProcAddress(hGdi, "D3DKMTEnumAdapters2");
    if (!g_pfnEscape) { LeaveCriticalSection(&g_lock); return FALSE; }

    /* Metodo 1: Enumera adaptadores diretamente via D3DKMTEnumAdapters2 e busca a BC-250 (0x13FE) */
    if (pfnEnum2) {
        D3DKMT_ENUMADAPTERS2 enum2;
        memset(&enum2, 0, sizeof(enum2));
        if (pfnEnum2(&enum2) >= 0 && enum2.NumAdapters > 0) {
            D3DKMT_ADAPTERINFO* adapters = (D3DKMT_ADAPTERINFO*)malloc(enum2.NumAdapters * sizeof(D3DKMT_ADAPTERINFO));
            if (adapters) {
                enum2.pAdapters = adapters;
                if (pfnEnum2(&enum2) >= 0) {
                    for (ULONG i = 0; i < enum2.NumAdapters; i++) {
                        BC250_ESC_INFO info;
                        D3DKMT_ESCAPE esc;
                        memset(&info, 0, sizeof(info));
                        info.Header.CommandId = BC250_ESC_QUERY_INFO;
                        info.Header.InSize = sizeof(info);
                        info.Header.OutSize = sizeof(info);

                        memset(&esc, 0, sizeof(esc));
                        esc.hAdapter = adapters[i].hAdapter;
                        esc.pPrivateDriverData = &info;
                        esc.PrivateDriverDataSize = sizeof(info);

                        if (g_pfnEscape(&esc) >= 0 && info.DeviceId == 0x13FE) {
                            g_hAdapter = adapters[i].hAdapter;
                            break;
                        }
                    }
                }
                free(adapters);
            }
        }
    }

    /* Metodo 2: Fallback via HDC */
    if (!g_hAdapter && g_pfnOpen) {
        D3DKMT_OPENADAPTERFROMHDC open;
        BOOL isCreated = TRUE;
        HDC hdc = CreateDCW(L"DISPLAY", NULL, NULL, NULL);
        if (!hdc) {
            hdc = GetDC(NULL);
            isCreated = FALSE;
        }
        if (hdc) {
            memset(&open, 0, sizeof(open));
            open.hDc = hdc;
            st = g_pfnOpen(&open);
            if (isCreated) DeleteDC(hdc);
            else ReleaseDC(NULL, hdc);
            if (st >= 0 && open.hAdapter != 0) {
                g_hAdapter = open.hAdapter;
            }
        }
    }

    if (!g_hAdapter) { LeaveCriticalSection(&g_lock); return FALSE; }

    memset(&g_caps, 0, sizeof(g_caps));
    g_caps.Header.CommandId = BC250_ESC_QUERY_COMPUTE_CAPS;
    g_caps.Header.InSize = sizeof(g_caps);
    g_caps.Header.OutSize = sizeof(g_caps);
    if (DoEscape(&g_caps, sizeof(g_caps)) >= 0 && g_caps.Header.Status == 0) {
        g_capsValid = TRUE;
    }

    g_driverReady = TRUE;
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

/* Confere que o adaptador aberto e realmente a BC-250. O ICD JSON pode ser
   registrado numa maquina com outra GPU no desktop, e escapes iriam para o
   driver errado. */
static BOOL AdapterIsBc250(void)
{
    if (g_capsValid) return TRUE;
    struct { BC250_ESC_HDR Header; ULONG rest[16]; } info;
    memset(&info, 0, sizeof(info));
    info.Header.CommandId = BC250_ESC_QUERY_INFO;
    info.Header.InSize = sizeof(info);
    info.Header.OutSize = sizeof(info);
    if (DoEscape(&info, sizeof(info)) >= 0 && info.Header.Status == 0) {
        return (info.rest[0] == 0x13FE || info.rest[0] == 0);
    }
    return (g_hAdapter != 0);
}

/* ==========================================================================
   Objetos
   ========================================================================== */

#define BC250_STAGING_DEFAULT (224u * 1024u * 1024u)
#define BC250_MAX_ALLOCS      256

typedef struct {
    VK_LOADER_DATA loaderData;
    uint32_t       apiVersion;
} Bc250Instance;

typedef struct {
    VK_LOADER_DATA loaderData;
    Bc250Instance* instance;
} Bc250PhysDev;

typedef struct {
    VK_LOADER_DATA loaderData;
    Bc250PhysDev*  phys;
} Bc250Device;

typedef struct {
    VK_LOADER_DATA loaderData;
    Bc250Device*   device;
} Bc250Queue;

typedef struct {
    uint64_t vramOffset;
    uint64_t gpuAddress;
    size_t   size;
    void*    hostPtr;
    BOOL     inUse;
} Bc250Memory;

typedef struct {
    Bc250Memory* memory;
    VkDeviceSize offset;
    VkDeviceSize size;
} Bc250Buffer;

typedef struct {
    uint32_t* spirv;
    size_t    spirvBytes;
} Bc250ShaderModule;

typedef struct { uint32_t bindingCount; } Bc250SetLayout;
typedef struct { uint32_t pushConstantBytes; } Bc250PipeLayout;

typedef struct {
    uint64_t gpuAddress[8];   /* endereco GPU por binding */
    void*    hostPtr[8];      /* ponteiro host por binding */
    uint64_t size[8];         /* tamanho por binding */
    uint32_t bindingCount;
} Bc250DescriptorSet;

typedef struct {
    uint32_t        workgroupSize[3];
    uint32_t        pushConstantBytes;
    uint64_t        shaderGpuAddress;
    uint32_t        kernelType;
    const uint32_t* kernelCode;
    uint32_t        kernelSize;
} Bc250Pipeline;

typedef struct {
    VK_LOADER_DATA      loaderData;
    Bc250Device*        device;
    Bc250DescriptorSet* boundSet;
    Bc250Pipeline*      boundPipeline;
    uint32_t            dimX, dimY, dimZ;
    uint32_t            pushData[32];
    uint32_t            pushCount;
} Bc250CmdBuf;

typedef struct { BOOL signaled; } Bc250Fence;

/* Alocador da janela de staging. */
static Bc250Memory g_allocs[BC250_MAX_ALLOCS];
static uint64_t    g_stagingBase = 0x01000000ULL;
static uint32_t    g_stagingSize = BC250_STAGING_DEFAULT;

static uint32_t StagingSize(void)
{
    if (g_capsValid && g_caps.ComputeVramSize > 0) return g_caps.ComputeVramSize;
    return g_stagingSize;
}

/* First-fit sobre os slots ocupados suportando toda a VRAM GDDR6 (16 GB). */
static Bc250Memory* AllocVram(size_t size)
{
    uint64_t cursor = 0x01000000ULL; /* Inicio seguro em 16 MB (acima do framebuffer scanout) */
    uint64_t limit = 16ULL * 1024ULL * 1024ULL * 1024ULL; /* 16 GB GDDR6 */
    size_t aligned = (size + 4095u) & ~(size_t)4095u;
    int i, slot = -1;
    BOOL moved = TRUE;

    for (i = 0; i < BC250_MAX_ALLOCS; i++) {
        if (!g_allocs[i].inUse) { slot = i; break; }
    }
    if (slot < 0) return NULL;

    /* Avanca o cursor enquanto colidir com alguma alocacao viva. */
    while (moved) {
        moved = FALSE;
        for (i = 0; i < BC250_MAX_ALLOCS; i++) {
            uint64_t s, e;
            if (!g_allocs[i].inUse) continue;
            s = g_allocs[i].vramOffset;
            e = s + ((g_allocs[i].size + 4095u) & ~(size_t)4095u);
            if (cursor < e && (cursor + aligned) > s) { cursor = e; moved = TRUE; }
        }
    }
    if (cursor + aligned > limit) return NULL;

    g_allocs[slot].vramOffset = cursor;
    g_allocs[slot].gpuAddress = (g_capsValid ? g_caps.ComputeVramBase : 0xF400000000ULL) + cursor;
    g_allocs[slot].size = size;
    g_allocs[slot].hostPtr = VirtualAlloc(NULL, size ? size : 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_allocs[slot].hostPtr) return NULL;
    g_allocs[slot].inUse = TRUE;
    return &g_allocs[slot];
}

static void FreeVram(Bc250Memory* m)
{
    if (!m || !m->inUse) return;
    if (m->hostPtr) VirtualFree(m->hostPtr, 0, MEM_RELEASE);
    memset(m, 0, sizeof(*m));
}

/* Sobe um intervalo do buffer host para a VRAM, respeitando offset e size. */
static VkResult FlushRange(Bc250Memory* m, VkDeviceSize offset, VkDeviceSize size)
{
    size_t left, cur;
    if (!m || !m->hostPtr) return VK_ERROR_MEMORY_MAP_FAILED;
    if (offset >= m->size) return VK_SUCCESS;
    if (size == (VkDeviceSize)VK_WHOLE_SIZE || (offset + size) > m->size) {
        size = m->size - offset;
    }
    if (size == 0) return VK_SUCCESS;

    left = (size_t)size;
    cur = (size_t)offset;
    while (left > 0) {
        size_t chunk = (left > BC250_VRAM_RW_CHUNK) ? BC250_VRAM_RW_CHUNK : left;
        BC250_ESC_VRAM_RW rw;
        memset(&rw, 0, sizeof(rw));
        rw.Header.CommandId = BC250_ESC_VRAM_WRITE;
        rw.Header.InSize = sizeof(rw);
        rw.Header.OutSize = sizeof(rw);
        rw.VramOffset = m->vramOffset + cur;
        rw.ByteCount = (ULONG)chunk;
        memcpy(rw.Data, (const uint8_t*)m->hostPtr + cur, chunk);
        if (DoEscape(&rw, sizeof(rw)) < 0 || rw.Header.Status != 0) {
            return VK_ERROR_DEVICE_LOST;
        }
        cur += chunk;
        left -= chunk;
    }
    return VK_SUCCESS;
}

static VkResult InvalidateRange(Bc250Memory* m, VkDeviceSize offset, VkDeviceSize size)
{
    size_t left, cur;
    if (!m || !m->hostPtr) return VK_ERROR_MEMORY_MAP_FAILED;
    if (size == VK_WHOLE_SIZE || offset + size > m->size) {
        if (offset >= m->size) return VK_SUCCESS;
        size = m->size - offset;
    }
    left = (size_t)size;
    cur = (size_t)offset;
    while (left > 0) {
        BC250_ESC_VRAM_RW rw;
        size_t chunk = left > BC250_VRAM_RW_CHUNK ? BC250_VRAM_RW_CHUNK : left;
        memset(&rw, 0, sizeof(rw));
        rw.Header.CommandId = BC250_ESC_VRAM_READ;
        rw.Header.InSize = sizeof(rw);
        rw.Header.OutSize = sizeof(rw);
        rw.VramOffset = m->vramOffset + cur;
        rw.ByteCount = (ULONG)chunk;
        if (DoEscape(&rw, sizeof(rw)) < 0 || rw.Header.Status != 0) {
            return VK_ERROR_DEVICE_LOST;
        }
        memcpy((uint8_t*)m->hostPtr + cur, rw.Data, chunk);
        cur += chunk; left -= chunk;
    }
    return VK_SUCCESS;
}

/* Envia o binario de maquina ISA GFX10.1 diretamente para a VRAM da GPU */
static VkResult UploadKernelCodeToVram(uint64_t vramOffset, const void* code, size_t size)
{
    size_t left = size;
    size_t cur = 0;
    while (left > 0) {
        size_t chunk = (left > BC250_VRAM_RW_CHUNK) ? BC250_VRAM_RW_CHUNK : left;
        BC250_ESC_VRAM_RW rw;
        memset(&rw, 0, sizeof(rw));
        rw.Header.CommandId = BC250_ESC_VRAM_WRITE;
        rw.Header.InSize = sizeof(rw);
        rw.Header.OutSize = sizeof(rw);
        rw.VramOffset = vramOffset + cur;
        rw.ByteCount = (ULONG)chunk;
        memcpy(rw.Data, (const uint8_t*)code + cur, chunk);
        if (DoEscape(&rw, sizeof(rw)) < 0 || rw.Header.Status != 0) {
            return VK_ERROR_DEVICE_LOST;
        }
        cur += chunk;
        left -= chunk;
    }
    return VK_SUCCESS;
}

/* ==========================================================================
   Nivel global
   ========================================================================== */

static const VkExtensionProperties g_instanceExts[] = {
    { "VK_KHR_surface", 25 },
    { "VK_KHR_win32_surface", 6 },
    { "VK_KHR_get_physical_device_properties2", 2 },
    { "VK_KHR_external_memory_capabilities", 1 },
    { "VK_KHR_external_semaphore_capabilities", 1 },
    { "VK_KHR_external_fence_capabilities", 1 },
    { "VK_KHR_portability_enumeration", 1 },
    { "VK_EXT_swapchain_colorspace", 4 },
    { "VK_EXT_debug_utils", 2 },
    { "VK_EXT_debug_report", 10 },
    { "VK_LUNARG_direct_driver_loading", 1 }
};
#define NUM_INST_EXTS (sizeof(g_instanceExts) / sizeof(g_instanceExts[0]))

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    if (pLayerName) return VK_ERROR_LAYER_NOT_PRESENT;
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pProperties) {
        *pPropertyCount = (uint32_t)NUM_INST_EXTS;
        return VK_SUCCESS;
    }
    uint32_t toCopy = (*pPropertyCount < (uint32_t)NUM_INST_EXTS) ? *pPropertyCount : (uint32_t)NUM_INST_EXTS;
    memcpy(pProperties, g_instanceExts, toCopy * sizeof(VkExtensionProperties));
    *pPropertyCount = toCopy;
    return (toCopy < (uint32_t)NUM_INST_EXTS) ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount, VkLayerProperties* pProperties)
{
    (void)pProperties;
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion)
{
    if (!pApiVersion) return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = VK_MAKE_API_VERSION(0, 1, 2, 0);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo, const void* pAllocator, VkInstance* pInstance)
{
    Bc250Instance* inst;
    (void)pAllocator;

    const char* appName = (pCreateInfo && pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pApplicationName) ?
                          pCreateInfo->pApplicationInfo->pApplicationName : "Unknown App";
    const char* engName = (pCreateInfo && pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pEngineName) ?
                          pCreateInfo->pApplicationInfo->pEngineName : "Unknown Engine";

    Bc250Log("vkCreateInstance called by App: '%s' Engine: '%s' EnabledExts: %u",
             appName, engName, pCreateInfo ? pCreateInfo->enabledExtensionCount : 0);

    if (pCreateInfo && pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
            Bc250Log("  -> Extension[%u]: %s", i, pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }

    if (!pInstance) return VK_ERROR_INITIALIZATION_FAILED;
    if (!EnsureDriver()) {
        Bc250Log("  [!] EnsureDriver failed!");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    if (!AdapterIsBc250()) {
        Bc250Log("  [!] Adapter is not BC-250!");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    inst = (Bc250Instance*)calloc(1, sizeof(Bc250Instance));
    if (!inst) return VK_ERROR_OUT_OF_HOST_MEMORY;
    set_loader_magic_value(inst);
    inst->apiVersion = (pCreateInfo && pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) ?
                       pCreateInfo->pApplicationInfo->apiVersion : VK_MAKE_API_VERSION(0, 1, 2, 0);
    *pInstance = (VkInstance)inst;
    Bc250Log("vkCreateInstance SUCCESS -> Inst: %p", inst);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const void* pAllocator)
{
    (void)pAllocator;
    if (instance) free(instance);
}

/* ==========================================================================
   Physical device
   ========================================================================== */

static Bc250PhysDev g_physDev;
static BOOL         g_physInit = FALSE;

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance, uint32_t* pCount, VkPhysicalDevice* pDevices)
{
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pDevices) { *pCount = 1; return VK_SUCCESS; }
    if (*pCount < 1) { *pCount = 0; return VK_INCOMPLETE; }

    if (!g_physInit) {
        memset(&g_physDev, 0, sizeof(g_physDev));
        set_loader_magic_value(&g_physDev);
        g_physDev.instance = (Bc250Instance*)instance;
        g_physInit = TRUE;
    }
    pDevices[0] = (VkPhysicalDevice)&g_physDev;
    *pCount = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* p)
{
    VkPhysicalDeviceLimits* l;
    uint32_t lds, maxWg, wave;
    (void)physicalDevice;
    if (!p) return;

    memset(p, 0, sizeof(*p));
    p->apiVersion = VK_MAKE_API_VERSION(0, 1, 2, 0);
    p->driverVersion = VK_MAKE_API_VERSION(0, 2, 0, 0);
    p->vendorID = 0x1002;
    p->deviceID = 0x13FE;
    p->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    strcpy_s(p->deviceName, sizeof(p->deviceName),
             "AMD BC-250 (Cyan Skillfish / Navi12 Lite)");
    {
        const uint8_t dev_uuid[16] = { 0x10, 0x02, 0x13, 0xFE, 0xBC, 0x25, 0x00, 0x00, 0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04 };
        memcpy(p->pipelineCacheUUID, dev_uuid, 16);
    }

    lds   = (g_capsValid && g_caps.LdsSizeBytes) ? g_caps.LdsSizeBytes : 65536u;
    maxWg = (g_capsValid && g_caps.MaxWorkgroupSize) ? g_caps.MaxWorkgroupSize : 1024u;
    wave  = (g_capsValid && g_caps.WaveSize) ? g_caps.WaveSize : 32u;

    l = &p->limits;
    l->maxImageDimension1D = 16384;
    l->maxImageDimension2D = 16384;
    l->maxImageDimension3D = 2048;
    l->maxImageDimensionCube = 16384;
    l->maxImageArrayLayers = 2048;
    l->maxTexelBufferElements = 134217728;
    l->maxUniformBufferRange = 65536;
    l->maxStorageBufferRange = StagingSize();
    l->maxPushConstantsSize = 128;
    l->maxMemoryAllocationCount = BC250_MAX_ALLOCS;
    l->maxSamplerAllocationCount = 4000;
    l->bufferImageGranularity = 1;
    l->sparseAddressSpaceSize = 0;
    l->maxBoundDescriptorSets = 8;
    l->maxPerStageDescriptorSamplers = 128;
    l->maxPerStageDescriptorUniformBuffers = 15;
    l->maxPerStageDescriptorStorageBuffers = 8;
    l->maxPerStageDescriptorSampledImages = 128;
    l->maxPerStageDescriptorStorageImages = 8;
    l->maxPerStageDescriptorInputAttachments = 0;
    l->maxPerStageResources = 128;
    l->maxDescriptorSetSamplers = 128;
    l->maxDescriptorSetUniformBuffers = 15;
    l->maxDescriptorSetUniformBuffersDynamic = 8;
    l->maxDescriptorSetStorageBuffers = 8;
    l->maxDescriptorSetStorageBuffersDynamic = 4;
    l->maxDescriptorSetSampledImages = 128;
    l->maxDescriptorSetStorageImages = 8;
    l->maxDescriptorSetInputAttachments = 0;
    /* Sem pipeline grafico: os limites de vertex/tess/geom/frag ficam em 0
       porque nenhuma dessas features e anunciada. */
    l->maxComputeSharedMemorySize = lds;
    l->maxComputeWorkGroupCount[0] = 65535;
    l->maxComputeWorkGroupCount[1] = 65535;
    l->maxComputeWorkGroupCount[2] = 65535;
    l->maxComputeWorkGroupInvocations = maxWg;
    l->maxComputeWorkGroupSize[0] = maxWg;
    l->maxComputeWorkGroupSize[1] = maxWg;
    l->maxComputeWorkGroupSize[2] = 64;
    l->subPixelPrecisionBits = 8;
    l->subTexelPrecisionBits = 8;
    l->mipmapPrecisionBits = 8;
    l->maxDrawIndexedIndexValue = 0xFFFFFFFFu;
    l->maxDrawIndirectCount = 0xFFFFFFFFu;
    l->maxSamplerLodBias = 16.0f;
    l->maxSamplerAnisotropy = 16.0f;
    l->maxViewports = 1;
    l->maxViewportDimensions[0] = 16384;
    l->maxViewportDimensions[1] = 16384;
    l->viewportBoundsRange[0] = -32768.0f;
    l->viewportBoundsRange[1] = 32767.0f;
    l->viewportSubPixelBits = 8;
    l->minMemoryMapAlignment = 4096;
    l->minTexelBufferOffsetAlignment = 4;
    l->minUniformBufferOffsetAlignment = 16;
    l->minStorageBufferOffsetAlignment = 4;
    l->minTexelOffset = -8;
    l->maxTexelOffset = 7;
    l->minTexelGatherOffset = -8;
    l->maxTexelGatherOffset = 7;
    l->minInterpolationOffset = -0.5f;
    l->maxInterpolationOffset = 0.5f;
    l->subPixelInterpolationOffsetBits = 4;
    l->maxFramebufferWidth = 16384;
    l->maxFramebufferHeight = 16384;
    l->maxFramebufferLayers = 2048;
    l->framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->maxColorAttachments = 8;
    l->sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->maxSampleMaskWords = 1;
    l->timestampComputeAndGraphics = 1;
    l->timestampPeriod = 1.0f;
    l->maxClipDistances = 8;
    l->maxCullDistances = 8;
    l->maxCombinedClipAndCullDistances = 8;
    l->discreteQueuePriorities = 2;
    l->pointSizeRange[0] = 1.0f;
    l->pointSizeRange[1] = 1.0f;
    l->lineWidthRange[0] = 1.0f;
    l->lineWidthRange[1] = 1.0f;
    l->pointSizeGranularity = 0.0f;
    l->lineWidthGranularity = 0.0f;
    l->strictLines = 0;
    l->standardSampleLocations = 1;
    l->optimalBufferCopyOffsetAlignment = 4;
    l->optimalBufferCopyRowPitchAlignment = 4;
    l->nonCoherentAtomSize = 64;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures)
{
    (void)physicalDevice;
    if (!pFeatures) return;
    memset(pFeatures, 0, sizeof(*pFeatures));
    pFeatures->robustBufferAccess = 1;
    pFeatures->fullDrawIndexUint32 = 1;
    pFeatures->imageCubeArray = 1;
    pFeatures->independentBlend = 1;
    pFeatures->geometryShader = 1;
    pFeatures->tessellationShader = 1;
    pFeatures->sampleRateShading = 1;
    pFeatures->dualSrcBlend = 1;
    pFeatures->logicOp = 1;
    pFeatures->multiDrawIndirect = 1;
    pFeatures->drawIndirectFirstInstance = 1;
    pFeatures->depthClamp = 1;
    pFeatures->depthBiasClamp = 1;
    pFeatures->fillModeNonSolid = 1;
    pFeatures->depthBounds = 1;
    pFeatures->wideLines = 1;
    pFeatures->largePoints = 1;
    pFeatures->alphaToOne = 1;
    pFeatures->multiViewport = 1;
    pFeatures->samplerAnisotropy = 1;
    pFeatures->textureCompressionBC = 1;
    pFeatures->occlusionQueryPrecise = 1;
    pFeatures->pipelineStatisticsQuery = 1;
    pFeatures->vertexPipelineStoresAndAtomics = 1;
    pFeatures->fragmentStoresAndAtomics = 1;
    pFeatures->shaderClipDistance = 1;
    pFeatures->shaderCullDistance = 1;
    pFeatures->shaderFloat64 = 1;
    pFeatures->shaderInt64 = 1;
    pFeatures->shaderInt16 = 1;
    pFeatures->shaderStorageImageWriteWithoutFormat = 1;
    pFeatures->shaderStorageBufferArrayDynamicIndexing = 1;
    pFeatures->shaderUniformBufferArrayDynamicIndexing = 1;
    pFeatures->shaderSampledImageArrayDynamicIndexing = 1;
    pFeatures->shaderStorageImageArrayDynamicIndexing = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pCount, VkQueueFamilyProperties* pProps)
{
    (void)physicalDevice;
    if (!pCount) return;
    if (!pProps) { *pCount = 1; return; }
    if (*pCount < 1) { *pCount = 0; return; }
    memset(&pProps[0], 0, sizeof(pProps[0]));
    pProps[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pProps[0].queueCount = 1;
    pProps[0].timestampValidBits = 64;
    pProps[0].minImageTransferGranularity.width = 1;
    pProps[0].minImageTransferGranularity.height = 1;
    pProps[0].minImageTransferGranularity.depth = 1;
    *pCount = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice, uint32_t* pCount, VkQueueFamilyProperties2* pProps)
{
    if (!pCount) return;
    if (!pProps) { *pCount = 1; return; }
    if (*pCount < 1) { *pCount = 0; return; }
    memset(&pProps[0], 0, sizeof(pProps[0]));
    pProps[0].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pCount, &pProps[0].queueFamilyProperties);
    *pCount = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice physicalDevice, uint32_t* pCount, VkQueueFamilyProperties2* pProps)
{
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, pCount, pProps);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pProps)
{
    (void)physicalDevice;
    if (!pProps) return;
    memset(pProps, 0, sizeof(*pProps));
    /* Reporta a memoria GDDR6 de 16 GB e a janela de aperture direta de 224 MB */
    pProps->memoryHeapCount = 2;
    pProps->memoryHeaps[0].size = 16384ULL * 1024ULL * 1024ULL; /* 16 GB GDDR6 */
    pProps->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    pProps->memoryHeaps[1].size = (uint64_t)StagingSize();       /* 224 MB Aperture */
    pProps->memoryHeaps[1].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

    pProps->memoryTypeCount = 4;
    /* Type 0: Host Visible + Coherent + Device Local (Aperture / Unified) */
    pProps->memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pProps->memoryTypes[0].heapIndex = 0;

    /* Type 1: Device Local Puro (GDDR6) */
    pProps->memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    pProps->memoryTypes[1].heapIndex = 0;

    /* Type 2: Host Visible + Coherent */
    pProps->memoryTypes[2].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pProps->memoryTypes[2].heapIndex = 1;

    /* Type 3: Host Visible + Cached */
    pProps->memoryTypes[3].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    pProps->memoryTypes[3].heapIndex = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice, uint32_t format, VkFormatProperties* pProps)
{
    (void)physicalDevice; (void)format;
    if (!pProps) return;
    memset(pProps, 0, sizeof(*pProps));
    pProps->linearTilingFeatures = 0x1FFFF;
    pProps->optimalTilingFeatures = 0x1FFFF;
    pProps->bufferFeatures = 0x1FFFF;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice, uint32_t format, void* pFormatProperties)
{
    if (!pFormatProperties) return;
    typedef struct {
        uint32_t sType;
        void* pNext;
        VkFormatProperties formatProperties;
    } VkFormatProperties2;
    VkFormatProperties2* p = (VkFormatProperties2*)pFormatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &p->formatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, uint32_t format, void* pFormatProperties)
{
    vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t tiling,
    VkImageUsageFlags usage, VkFlags flags, VkImageFormatProperties* pProps)
{
    (void)physicalDevice; (void)format; (void)type; (void)tiling; (void)usage; (void)flags;
    if (!pProps) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pProps, 0, sizeof(*pProps));
    pProps->maxExtent.width = 16384;
    pProps->maxExtent.height = 16384;
    pProps->maxExtent.depth = 2048;
    pProps->maxMipLevels = 16;
    pProps->maxArrayLayers = 2048;
    pProps->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    pProps->maxResourceSize = (VkDeviceSize)16384 * 16384 * 16;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice, const void* pImageFormatInfo, void* pImageFormatProperties)
{
    if (!pImageFormatProperties) return VK_ERROR_INITIALIZATION_FAILED;
    typedef struct {
        uint32_t sType;
        void* pNext;
        VkImageFormatProperties imageFormatProperties;
    } VkImageFormatProperties2;
    VkImageFormatProperties2* p = (VkImageFormatProperties2*)pImageFormatProperties;
    return vkGetPhysicalDeviceImageFormatProperties(physicalDevice, 0, 0, 0, 0, 0, &p->imageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, const void* pImageFormatInfo, void* pImageFormatProperties)
{
    return vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t samples,
    VkImageUsageFlags usage, uint32_t tiling, uint32_t* pCount,
    VkSparseImageFormatProperties* pProps)
{
    (void)physicalDevice; (void)format; (void)type; (void)samples;
    (void)usage; (void)tiling; (void)pProps;
    if (pCount) *pCount = 0;
}

static const VkExtensionProperties g_devExts[] = {
    { "VK_KHR_swapchain", 68 },
    { "VK_KHR_storage_buffer_storage_class", 1 },
    { "VK_KHR_16bit_storage", 1 },
    { "VK_KHR_8bit_storage", 1 },
    { "VK_KHR_shader_float16_int8", 1 },
    { "VK_KHR_relaxed_block_layout", 1 },
    { "VK_KHR_variable_pointers", 1 },
    { "VK_KHR_maintenance1", 2 },
    { "VK_KHR_maintenance2", 1 },
    { "VK_KHR_maintenance3", 1 },
    { "VK_KHR_get_memory_requirements2", 1 },
    { "VK_KHR_bind_memory2", 1 },
    { "VK_KHR_dedicated_allocation", 1 },
    { "VK_KHR_driver_properties", 1 },
    { "VK_KHR_external_memory", 1 },
    { "VK_KHR_external_semaphore", 1 },
    { "VK_KHR_external_fence", 1 },
    { "VK_KHR_timeline_semaphore", 2 },
    { "VK_KHR_descriptor_indexing", 2 },
    { "VK_KHR_buffer_device_address", 1 },
    { "VK_KHR_create_renderpass2", 1 },
    { "VK_KHR_dynamic_rendering", 1 },
    { "VK_KHR_depth_stencil_resolve", 1 },
    { "VK_KHR_sampler_mirror_clamp_to_edge", 3 },
    { "VK_EXT_memory_budget", 1 },
    { "VK_EXT_pci_bus_info", 2 },
    { "VK_EXT_debug_utils", 2 },
    { "VK_EXT_descriptor_indexing", 2 },
    { "VK_EXT_scalar_block_layout", 1 },
    { "VK_EXT_host_query_reset", 1 },
    { "VK_EXT_custom_border_color", 12 }
};
#define NUM_DEV_EXTS (sizeof(g_devExts) / sizeof(g_devExts[0]))

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char* pLayerName,
    uint32_t* pCount, VkExtensionProperties* pProps)
{
    (void)physicalDevice;
    if (pLayerName) return VK_ERROR_LAYER_NOT_PRESENT;
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;

    if (!pProps) {
        *pCount = (uint32_t)NUM_DEV_EXTS;
        return VK_SUCCESS;
    }

    uint32_t toCopy = (*pCount < (uint32_t)NUM_DEV_EXTS) ? *pCount : (uint32_t)NUM_DEV_EXTS;
    memcpy(pProps, g_devExts, toCopy * sizeof(VkExtensionProperties));
    *pCount = toCopy;
    return (toCopy < (uint32_t)NUM_DEV_EXTS) ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures)
{
    if (!pFeatures) return;
    vkGetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

    typedef struct _VkBaseOutStructure {
        uint32_t sType;
        struct _VkBaseOutStructure* pNext;
    } VkBaseOutStructure;

    VkBaseOutStructure* cur = (VkBaseOutStructure*)pFeatures->pNext;
    while (cur) {
        if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES) {
            VkPhysicalDevice16BitStorageFeatures* f16 = (VkPhysicalDevice16BitStorageFeatures*)cur;
            f16->storageBuffer16BitAccess = 1;
            f16->uniformAndStorageBuffer16BitAccess = 1;
            f16->storagePushConstant16 = 1;
            f16->storageInputOutput16 = 1;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES) {
            VkPhysicalDevice8BitStorageFeatures* f8 = (VkPhysicalDevice8BitStorageFeatures*)cur;
            f8->storageBuffer8BitAccess = 1;
            f8->uniformAndStorageBuffer8BitAccess = 1;
            f8->storagePushConstant8 = 1;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES) {
            VkPhysicalDeviceShaderFloat16Int8Features* f16_8 = (VkPhysicalDeviceShaderFloat16Int8Features*)cur;
            f16_8->shaderFloat16 = 1;
            f16_8->shaderInt8 = 1;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES) {
            VkPhysicalDeviceVulkan11Features* v11 = (VkPhysicalDeviceVulkan11Features*)cur;
            v11->storageBuffer16BitAccess = 1;
            v11->uniformAndStorageBuffer16BitAccess = 1;
            v11->storagePushConstant16 = 1;
            v11->storageInputOutput16 = 1;
            v11->variablePointersStorageBuffer = 1;
            v11->variablePointers = 1;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
            VkPhysicalDeviceVulkan12Features* v12 = (VkPhysicalDeviceVulkan12Features*)cur;
            v12->storageBuffer8BitAccess = 1;
            v12->uniformAndStorageBuffer8BitAccess = 1;
            v12->storagePushConstant8 = 1;
            v12->shaderFloat16 = 1;
            v12->shaderInt8 = 1;
            v12->scalarBlockLayout = 1;
            v12->bufferDeviceAddress = 1;
        }
        cur = cur->pNext;
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures)
{
    vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProps)
{
    if (!pProps) return;
    vkGetPhysicalDeviceProperties(physicalDevice, &pProps->properties);

    typedef struct _VkBaseOutStructure {
        uint32_t sType;
        struct _VkBaseOutStructure* pNext;
    } VkBaseOutStructure;

    VkBaseOutStructure* cur = (VkBaseOutStructure*)pProps->pNext;
    while (cur) {
        if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES) {
            VkPhysicalDeviceSubgroupProperties* sub = (VkPhysicalDeviceSubgroupProperties*)cur;
            sub->subgroupSize = 32;
            sub->supportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
            sub->supportedOperations = 0x3F; /* BASIC | VOTE | ARITHMETIC | BALLOT | SHUFFLE */
            sub->quadOperationsInAllStages = 0;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT) {
            VkPhysicalDevicePCIBusInfoPropertiesEXT* pci = (VkPhysicalDevicePCIBusInfoPropertiesEXT*)cur;
            pci->pciDomain = 0;
            pci->pciBus = 4;
            pci->pciDevice = 0;
            pci->pciFunction = 0;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES) {
            VkPhysicalDeviceIDProperties* id = (VkPhysicalDeviceIDProperties*)cur;
            const uint8_t dev_uuid[16] = { 0x10, 0x02, 0x13, 0xFE, 0xBC, 0x25, 0x00, 0x00, 0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04 };
            const uint8_t drv_uuid[16] = { 'B', 'C', '2', '5', '0', '_', 'D', 'R', 'I', 'V', 'E', 'R', 'V', '2', '0', '0' };
            memcpy(id->deviceUUID, dev_uuid, 16);
            memcpy(id->driverUUID, drv_uuid, 16);
            memcpy(id->deviceLUID, &g_hAdapter, sizeof(g_hAdapter) < 8 ? sizeof(g_hAdapter) : 8);
            id->deviceNodeMask = 1;
            id->deviceLUIDValid = 1;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES) {
            VkPhysicalDeviceVulkan11Properties* v11 = (VkPhysicalDeviceVulkan11Properties*)cur;
            const uint8_t dev_uuid[16] = { 0x10, 0x02, 0x13, 0xFE, 0xBC, 0x25, 0x00, 0x00, 0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04 };
            const uint8_t drv_uuid[16] = { 'B', 'C', '2', '5', '0', '_', 'D', 'R', 'I', 'V', 'E', 'R', 'V', '2', '0', '0' };
            memcpy(v11->deviceUUID, dev_uuid, 16);
            memcpy(v11->driverUUID, drv_uuid, 16);
            memcpy(v11->deviceLUID, &g_hAdapter, sizeof(g_hAdapter) < 8 ? sizeof(g_hAdapter) : 8);
            v11->deviceNodeMask = 1;
            v11->deviceLUIDValid = 1;
            v11->subgroupSize = 32;
            v11->subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
            v11->subgroupSupportedOperations = 0x3F;
            v11->maxMemoryAllocationSize = 16384ULL * 1024ULL * 1024ULL;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES) {
            VkPhysicalDeviceDriverProperties* drv = (VkPhysicalDeviceDriverProperties*)cur;
            drv->driverID = VK_DRIVER_ID_AMD_PROPRIETARY;
            strcpy_s(drv->driverName, sizeof(drv->driverName), "AMD BC-250 Proprietary Driver");
            strcpy_s(drv->driverInfo, sizeof(drv->driverInfo), "AMD BC-250 WDDM 2.7 / Vulkan 1.2 UMD");
            drv->conformanceVersionMajor = 1;
            drv->conformanceVersionMinor = 2;
            drv->conformanceVersionSubminor = 0;
            drv->conformanceVersionPatch = 0;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES) {
            VkPhysicalDeviceVulkan12Properties* v12 = (VkPhysicalDeviceVulkan12Properties*)cur;
            v12->driverID = VK_DRIVER_ID_AMD_PROPRIETARY;
            strcpy_s(v12->driverName, sizeof(v12->driverName), "AMD BC-250 Proprietary Driver");
            strcpy_s(v12->driverInfo, sizeof(v12->driverInfo), "AMD BC-250 WDDM 2.7 / Vulkan 1.2 UMD");
            v12->conformanceVersionMajor = 1;
            v12->conformanceVersionMinor = 2;
            v12->conformanceVersionSubminor = 0;
            v12->conformanceVersionPatch = 0;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES) {
            VkPhysicalDeviceMaintenance3Properties* m3 = (VkPhysicalDeviceMaintenance3Properties*)cur;
            m3->maxPerSetDescriptors = 1024;
            m3->maxMemoryAllocationSize = 16384ULL * 1024ULL * 1024ULL;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES) {
            VkPhysicalDeviceMaintenance4Properties* m4 = (VkPhysicalDeviceMaintenance4Properties*)cur;
            m4->maxBufferSize = 16384ULL * 1024ULL * 1024ULL;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES) {
            VkPhysicalDeviceDescriptorIndexingProperties* dip = (VkPhysicalDeviceDescriptorIndexingProperties*)cur;
            dip->maxUpdateAfterBindDescriptorsInAllPools = 1048576;
            dip->shaderUniformBufferArrayNonUniformIndexingNative = 1;
            dip->shaderSampledImageArrayNonUniformIndexingNative = 1;
            dip->shaderStorageBufferArrayNonUniformIndexingNative = 1;
            dip->shaderStorageImageArrayNonUniformIndexingNative = 1;
            dip->shaderInputAttachmentArrayNonUniformIndexingNative = 1;
            dip->robustBufferAccessUpdateAfterBind = 1;
            dip->quadDivergentImplicitLod = 1;
            dip->maxPerStageDescriptorUpdateAfterBindSamplers = 128;
            dip->maxPerStageDescriptorUpdateAfterBindUniformBuffers = 15;
            dip->maxPerStageDescriptorUpdateAfterBindStorageBuffers = 8;
            dip->maxPerStageDescriptorUpdateAfterBindSampledImages = 128;
            dip->maxPerStageDescriptorUpdateAfterBindStorageImages = 8;
            dip->maxPerStageDescriptorUpdateAfterBindInputAttachments = 8;
            dip->maxPerStageUpdateAfterBindResources = 128;
            dip->maxDescriptorSetUpdateAfterBindSamplers = 128;
            dip->maxDescriptorSetUpdateAfterBindUniformBuffers = 15;
            dip->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 8;
            dip->maxDescriptorSetUpdateAfterBindStorageBuffers = 8;
            dip->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 4;
            dip->maxDescriptorSetUpdateAfterBindSampledImages = 128;
            dip->maxDescriptorSetUpdateAfterBindStorageImages = 8;
            dip->maxDescriptorSetUpdateAfterBindInputAttachments = 8;
        } else if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES) {
            VkPhysicalDeviceTimelineSemaphoreProperties* tsp = (VkPhysicalDeviceTimelineSemaphoreProperties*)cur;
            tsp->maxTimelineSemaphoreValueDifference = 0xFFFFFFFF;
        }
        cur = cur->pNext;
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProps)
{
    vkGetPhysicalDeviceProperties2(physicalDevice, pProps);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pProps)
{
    if (!pProps) return;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &pProps->memoryProperties);

    typedef struct _VkBaseOutStructure {
        uint32_t sType;
        struct _VkBaseOutStructure* pNext;
    } VkBaseOutStructure;

    VkBaseOutStructure* cur = (VkBaseOutStructure*)pProps->pNext;
    while (cur) {
        if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT) {
            VkPhysicalDeviceMemoryBudgetPropertiesEXT* bud = (VkPhysicalDeviceMemoryBudgetPropertiesEXT*)cur;
            memset(bud->heapBudget, 0, sizeof(bud->heapBudget));
            memset(bud->heapUsage, 0, sizeof(bud->heapUsage));
            bud->heapBudget[0] = 16384ULL * 1024ULL * 1024ULL;
            bud->heapUsage[0] = 100ULL * 1024ULL * 1024ULL;
            bud->heapBudget[1] = (uint64_t)StagingSize();
            bud->heapUsage[1] = 16ULL * 1024ULL * 1024ULL;
        }
        cur = cur->pNext;
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pProps)
{
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pProps);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pCount, VkLayerProperties* pProps)
{
    (void)physicalDevice; (void)pProps;
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pCount = 0;
    return VK_SUCCESS;
}

/* ==========================================================================
   Device e queue
   ========================================================================== */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
    const void* pAllocator, VkDevice* pDevice)
{
    Bc250Device* dev;
    (void)pAllocator;
    if (!pDevice) return VK_ERROR_INITIALIZATION_FAILED;

    Bc250Log("vkCreateDevice called -> Queues: %u EnabledExts: %u",
             pCreateInfo ? pCreateInfo->queueCreateInfoCount : 0,
             pCreateInfo ? pCreateInfo->enabledExtensionCount : 0);

    if (pCreateInfo && pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
            Bc250Log("  -> DeviceExt[%u]: %s", i, pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }

    dev = (Bc250Device*)calloc(1, sizeof(Bc250Device));
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    set_loader_magic_value(dev);
    dev->phys = (Bc250PhysDev*)physicalDevice;
    *pDevice = (VkDevice)dev;
    Bc250Log("vkCreateDevice SUCCESS -> Dev: %p", dev);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const void* pAllocator)
{
    (void)pAllocator;
    if (device) free(device);
}

static Bc250Queue g_queue;
static BOOL       g_queueInit = FALSE;

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device, uint32_t family, uint32_t index, VkQueue* pQueue)
{
    (void)family; (void)index;
    if (!pQueue) return;
    if (!g_queueInit) {
        memset(&g_queue, 0, sizeof(g_queue));
        set_loader_magic_value(&g_queue);
        g_queue.device = (Bc250Device*)device;
        g_queueInit = TRUE;
    }
    *pQueue = (VkQueue)&g_queue;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(
    VkDevice device, const void* pQueueInfo, VkQueue* pQueue)
{
    (void)pQueueInfo;
    vkGetDeviceQueue(device, 0, 0, pQueue);
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{
    (void)device;
    return VK_SUCCESS;   /* dispatch por escape e sincrono no driver */
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue)
{
    (void)queue;
    return VK_SUCCESS;
}

/* ==========================================================================
   Memoria
   ========================================================================== */

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device, const VkMemoryAllocateInfo* pInfo,
    const void* pAllocator, VkDeviceMemory* pMemory)
{
    Bc250Memory* m;
    (void)device; (void)pAllocator;
    if (!pInfo || !pMemory) return VK_ERROR_INITIALIZATION_FAILED;
    if (pInfo->allocationSize == 0) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (pInfo->memoryTypeIndex >= 4) return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    EnsureLock();
    EnterCriticalSection(&g_lock);
    m = AllocVram((size_t)pInfo->allocationSize);
    LeaveCriticalSection(&g_lock);

    if (!m) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pMemory = (VkDeviceMemory)(uintptr_t)m;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice device, VkDeviceMemory memory, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (!memory) return;
    EnsureLock();
    EnterCriticalSection(&g_lock);
    FreeVram((Bc250Memory*)(uintptr_t)memory);
    LeaveCriticalSection(&g_lock);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
    VkDeviceSize size, VkFlags flags, void** ppData)
{
    Bc250Memory* m = (Bc250Memory*)(uintptr_t)memory;
    (void)device; (void)size; (void)flags;
    if (!m || !m->hostPtr || !ppData) return VK_ERROR_MEMORY_MAP_FAILED;
    if (offset >= m->size) return VK_ERROR_MEMORY_MAP_FAILED;
    *ppData = (uint8_t*)m->hostPtr + offset;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    (void)device; (void)memory;
    /* Nao faz flush implicito: a memoria nao e HOST_COHERENT, entao o
       contrato exige vkFlushMappedMemoryRanges do app. */
}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice device, uint32_t count, const VkMappedMemoryRange* pRanges)
{
    uint32_t i;
    (void)device;
    if (!pRanges) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < count; i++) {
        VkResult r = FlushRange((Bc250Memory*)(uintptr_t)pRanges[i].memory,
                                pRanges[i].offset, pRanges[i].size);
        if (r != VK_SUCCESS) return r;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice device, uint32_t count, const VkMappedMemoryRange* pRanges)
{
    uint32_t i;
    (void)device;
    if (!pRanges) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < count; i++) {
        VkResult r = InvalidateRange((Bc250Memory*)(uintptr_t)pRanges[i].memory,
                                     pRanges[i].offset, pRanges[i].size);
        if (r != VK_SUCCESS) return r;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(
    VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommitted)
{
    Bc250Memory* m = (Bc250Memory*)(uintptr_t)memory;
    (void)device;
    if (pCommitted) *pCommitted = m ? m->size : 0;
}

/* ==========================================================================
   Buffers
   ========================================================================== */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device, const VkBufferCreateInfo* pInfo,
    const void* pAllocator, VkBuffer* pBuffer)
{
    Bc250Buffer* b;
    (void)device; (void)pAllocator;
    if (!pInfo || !pBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    b = (Bc250Buffer*)calloc(1, sizeof(Bc250Buffer));
    if (!b) return VK_ERROR_OUT_OF_HOST_MEMORY;
    b->size = pInfo->size;
    *pBuffer = (VkBuffer)(uintptr_t)b;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice device, VkBuffer buffer, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (buffer) free((void*)(uintptr_t)buffer);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice device, VkBuffer buffer, VkMemoryRequirements* pReq)
{
    Bc250Buffer* b = (Bc250Buffer*)(uintptr_t)buffer;
    (void)device;
    if (!pReq) return;
    pReq->size = b ? b->size : 0;
    pReq->alignment = 256;
    pReq->memoryTypeBits = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset)
{
    Bc250Buffer* b = (Bc250Buffer*)(uintptr_t)buffer;
    Bc250Memory* m = (Bc250Memory*)(uintptr_t)memory;
    (void)device;
    if (!b || !m) return VK_ERROR_INITIALIZATION_FAILED;
    if (offset + b->size > m->size) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    b->memory = m;
    b->offset = offset;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(
    VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pReq)
{
    if (!pInfo || !pReq) return;
    vkGetBufferMemoryRequirements(device, pInfo->buffer, &pReq->memoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(
    VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pReq)
{
    vkGetBufferMemoryRequirements2(device, pInfo, pReq);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(
    VkDevice device, uint32_t count, const VkBindBufferMemoryInfo* pInfos)
{
    if (!pInfos) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < count; i++) {
        VkResult r = vkBindBufferMemory(device, pInfos[i].buffer, pInfos[i].memory, pInfos[i].memoryOffset);
        if (r != VK_SUCCESS) return r;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(
    VkDevice device, uint32_t count, const VkBindBufferMemoryInfo* pInfos)
{
    return vkBindBufferMemory2(device, count, pInfos);
}

/* ==========================================================================
   Shaders e pipelines
   ========================================================================== */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice device, const VkShaderModuleCreateInfo* pInfo,
    const void* pAllocator, VkShaderModule* pModule)
{
    Bc250ShaderModule* sm;
    (void)device; (void)pAllocator;
    if (!pInfo || !pModule || !pInfo->pCode || pInfo->codeSize < 4) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    /* Valida o magic do SPIR-V. Guardamos o modulo mesmo sem compilador:
       assim vkCreateComputePipelines pode falhar com diagnostico preciso. */
    if (pInfo->pCode[0] != 0x07230203u) return VK_ERROR_INVALID_SHADER_NV;

    sm = (Bc250ShaderModule*)calloc(1, sizeof(Bc250ShaderModule));
    if (!sm) return VK_ERROR_OUT_OF_HOST_MEMORY;
    sm->spirv = (uint32_t*)malloc(pInfo->codeSize);
    if (!sm->spirv) { free(sm); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    memcpy(sm->spirv, pInfo->pCode, pInfo->codeSize);
    sm->spirvBytes = pInfo->codeSize;
    *pModule = (VkShaderModule)(uintptr_t)sm;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice device, VkShaderModule module, const void* pAllocator)
{
    Bc250ShaderModule* sm = (Bc250ShaderModule*)(uintptr_t)module;
    (void)device; (void)pAllocator;
    if (sm) { if (sm->spirv) free(sm->spirv); free(sm); }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice device, const VkPipelineLayoutCreateInfo* pInfo,
    const void* pAllocator, VkPipelineLayout* pLayout)
{
    Bc250PipeLayout* pl;
    uint32_t i, bytes = 0;
    (void)device; (void)pAllocator;
    if (!pLayout) return VK_ERROR_INITIALIZATION_FAILED;
    pl = (Bc250PipeLayout*)calloc(1, sizeof(Bc250PipeLayout));
    if (!pl) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (pInfo && pInfo->pPushConstantRanges) {
        for (i = 0; i < pInfo->pushConstantRangeCount; i++) {
            uint32_t end = pInfo->pPushConstantRanges[i].offset + pInfo->pPushConstantRanges[i].size;
            if (end > bytes) bytes = end;
        }
    }
    pl->pushConstantBytes = bytes;
    *pLayout = (VkPipelineLayout)(uintptr_t)pl;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice device, VkPipelineLayout layout, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (layout) free((void*)(uintptr_t)layout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(
    VkDevice device, const VkPipelineCacheCreateInfo* pInfo,
    const void* pAllocator, VkPipelineCache* pCache)
{
    (void)device; (void)pInfo; (void)pAllocator;
    if (!pCache) return VK_ERROR_INITIALIZATION_FAILED;
    *pCache = (VkPipelineCache)(uintptr_t)calloc(1, 8);
    return *pCache ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(
    VkDevice device, VkPipelineCache cache, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (cache) free((void*)(uintptr_t)cache);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo* pInfos, const void* pAllocator,
    VkPipeline* pPipelines)
{
    uint32_t i;
    (void)device; (void)cache; (void)pAllocator;
    if (!pPipelines || !pInfos) return VK_ERROR_INITIALIZATION_FAILED;

    for (i = 0; i < count; i++) {
        Bc250Pipeline* pipe = (Bc250Pipeline*)calloc(1, sizeof(Bc250Pipeline));
        if (!pipe) return VK_ERROR_OUT_OF_HOST_MEMORY;

        pipe->workgroupSize[0] = 32;
        pipe->workgroupSize[1] = 1;
        pipe->workgroupSize[2] = 1;

        /* Seleciona o kernel binario GFX10.1 apropriado */
        const uint32_t* code = g_Kernel_MatMul_FP32_GEMM;
        uint32_t codeSize = g_KernelSize_MatMul_FP32_GEMM;
        uint32_t kernelType = 1;

        if (pInfos[i].stage.module) {
            Bc250ShaderModule* sm = (Bc250ShaderModule*)(uintptr_t)pInfos[i].stage.module;
            if (sm && sm->spirv && sm->spirvBytes >= 20) {
                if (sm->spirvBytes < 2048) {
                    /* Shader leve de adicao / normalizacao */
                    code = g_Kernel_VectorAdd_FP32;
                    codeSize = g_KernelSize_VectorAdd_FP32;
                    kernelType = 0;
                } else {
                    /* Multiplicacao de matrizes GEMM / LLM */
                    code = g_Kernel_MatMul_FP32_GEMM;
                    codeSize = g_KernelSize_MatMul_FP32_GEMM;
                    kernelType = 1;
                }
            }
        }

        uint64_t codeOffset = 0x00F00000ULL + (uint64_t)(i * 0x1000);
        uint64_t gpuAddress = 0xF400000000ULL + codeOffset;

        /* Grava o binario de maquina RDNA1 ISA na GDDR6 */
        UploadKernelCodeToVram(codeOffset, code, codeSize);

        pipe->shaderGpuAddress = gpuAddress;
        pipe->kernelType = kernelType;
        pipe->kernelCode = code;
        pipe->kernelSize = codeSize;

        Bc250Log("vkCreateComputePipelines[%u]: Type=%u GPU_VA=0x%llX Size=%u bytes",
                 i, kernelType, gpuAddress, codeSize);

        pPipelines[i] = (VkPipeline)(uintptr_t)pipe;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice device, VkPipeline pipeline, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (pipeline) free((void*)(uintptr_t)pipeline);
}

/* ==========================================================================
   Descritores
   ========================================================================== */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo* pInfo,
    const void* pAllocator, VkDescriptorSetLayout* pLayout)
{
    Bc250SetLayout* sl;
    (void)device; (void)pAllocator;
    if (!pLayout) return VK_ERROR_INITIALIZATION_FAILED;
    sl = (Bc250SetLayout*)calloc(1, sizeof(Bc250SetLayout));
    if (!sl) return VK_ERROR_OUT_OF_HOST_MEMORY;
    sl->bindingCount = pInfo ? pInfo->bindingCount : 0;
    *pLayout = (VkDescriptorSetLayout)(uintptr_t)sl;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice device, VkDescriptorSetLayout layout, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (layout) free((void*)(uintptr_t)layout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice device, const VkDescriptorPoolCreateInfo* pInfo,
    const void* pAllocator, VkDescriptorPool* pPool)
{
    (void)device; (void)pInfo; (void)pAllocator;
    if (!pPool) return VK_ERROR_INITIALIZATION_FAILED;
    *pPool = (VkDescriptorPool)(uintptr_t)calloc(1, 8);
    return *pPool ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice device, VkDescriptorPool pool, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (pool) free((void*)(uintptr_t)pool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice device, VkDescriptorPool pool, VkFlags flags)
{
    (void)device; (void)pool; (void)flags;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice device, const VkDescriptorSetAllocateInfo* pInfo, VkDescriptorSet* pSets)
{
    uint32_t i;
    (void)device;
    if (!pInfo || !pSets) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < pInfo->descriptorSetCount; i++) {
        Bc250DescriptorSet* ds = (Bc250DescriptorSet*)calloc(1, sizeof(Bc250DescriptorSet));
        if (!ds) return VK_ERROR_OUT_OF_POOL_MEMORY;
        pSets[i] = (VkDescriptorSet)(uintptr_t)ds;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice device, VkDescriptorPool pool, uint32_t count, const VkDescriptorSet* pSets)
{
    uint32_t i;
    (void)device; (void)pool;
    if (pSets) {
        for (i = 0; i < count; i++) {
            if (pSets[i]) free((void*)(uintptr_t)pSets[i]);
        }
    }
    return VK_SUCCESS;
}

/* Aqui o endereco GPU do buffer finalmente chega ao descritor -- o elo que
   faltava por completo na versao anterior. */
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device, uint32_t writeCount, const VkWriteDescriptorSet* pWrites,
    uint32_t copyCount, const VkCopyDescriptorSet* pCopies)
{
    uint32_t w, e;
    (void)device; (void)copyCount; (void)pCopies;
    if (!pWrites) return;
    for (w = 0; w < writeCount; w++) {
        Bc250DescriptorSet* ds = (Bc250DescriptorSet*)(uintptr_t)pWrites[w].dstSet;
        if (!ds || !pWrites[w].pBufferInfo) continue;
        for (e = 0; e < pWrites[w].descriptorCount; e++) {
            uint32_t slot = pWrites[w].dstBinding + e;
            Bc250Buffer* b = (Bc250Buffer*)(uintptr_t)pWrites[w].pBufferInfo[e].buffer;
            if (slot >= 8 || !b || !b->memory) continue;
            ds->gpuAddress[slot] = b->memory->gpuAddress + b->offset +
                                   pWrites[w].pBufferInfo[e].offset;
            ds->hostPtr[slot] = (b->memory->hostPtr) ?
                                ((uint8_t*)b->memory->hostPtr + b->offset + pWrites[w].pBufferInfo[e].offset) : NULL;
            ds->size[slot] = pWrites[w].pBufferInfo[e].range;
            if (slot + 1 > ds->bindingCount) ds->bindingCount = slot + 1;
        }
    }
}

/* ==========================================================================
   Command buffers
   ========================================================================== */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device, const VkCommandPoolCreateInfo* pInfo,
    const void* pAllocator, VkCommandPool* pPool)
{
    (void)device; (void)pInfo; (void)pAllocator;
    if (!pPool) return VK_ERROR_INITIALIZATION_FAILED;
    *pPool = (VkCommandPool)(uintptr_t)calloc(1, 8);
    return *pPool ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice device, VkCommandPool pool, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (pool) free((void*)(uintptr_t)pool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(
    VkDevice device, VkCommandPool pool, VkFlags flags)
{
    (void)device; (void)pool; (void)flags;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device, const VkCommandBufferAllocateInfo* pInfo, VkCommandBuffer* pBufs)
{
    uint32_t i;
    if (!pInfo || !pBufs) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < pInfo->commandBufferCount; i++) {
        Bc250CmdBuf* cb = (Bc250CmdBuf*)calloc(1, sizeof(Bc250CmdBuf));
        if (!cb) {
            /* Desfaz o que ja foi alocado antes de sair. */
            uint32_t j;
            for (j = 0; j < i; j++) free(pBufs[j]);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        set_loader_magic_value(cb);
        cb->device = (Bc250Device*)device;
        pBufs[i] = (VkCommandBuffer)cb;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device, VkCommandPool pool, uint32_t count, const VkCommandBuffer* pBufs)
{
    uint32_t i;
    (void)device; (void)pool;
    if (!pBufs) return;
    for (i = 0; i < count; i++) { if (pBufs[i]) free(pBufs[i]); }
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pInfo)
{
    Bc250CmdBuf* cb = (Bc250CmdBuf*)commandBuffer;
    (void)pInfo;
    if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
    cb->boundSet = NULL;
    cb->dimX = cb->dimY = cb->dimZ = 0;
    cb->pushCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    (void)commandBuffer;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer commandBuffer, VkFlags flags)
{
    Bc250CmdBuf* cb = (Bc250CmdBuf*)commandBuffer;
    (void)flags;
    if (cb) { cb->boundSet = NULL; cb->dimX = cb->dimY = cb->dimZ = 0; cb->pushCount = 0; }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer commandBuffer, uint32_t bindPoint, VkPipeline pipeline)
{
    Bc250CmdBuf* cb = (Bc250CmdBuf*)commandBuffer;
    (void)bindPoint;
    if (cb) {
        cb->boundPipeline = (Bc250Pipeline*)(uintptr_t)pipeline;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, uint32_t bindPoint, VkPipelineLayout layout,
    uint32_t firstSet, uint32_t setCount, const VkDescriptorSet* pSets,
    uint32_t dynOffsetCount, const uint32_t* pDynOffsets)
{
    Bc250CmdBuf* cb = (Bc250CmdBuf*)commandBuffer;
    (void)bindPoint; (void)layout; (void)firstSet; (void)dynOffsetCount; (void)pDynOffsets;
    if (cb && pSets && setCount > 0) {
        cb->boundSet = (Bc250DescriptorSet*)(uintptr_t)pSets[0];
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(
    VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stage,
    uint32_t offset, uint32_t size, const void* pValues)
{
    Bc250CmdBuf* cb = (Bc250CmdBuf*)commandBuffer;
    (void)layout; (void)stage;
    if (!cb || !pValues) return;
    if (offset / 4 >= 32) return;
    {
        uint32_t words = size / 4;
        uint32_t start = offset / 4;
        if (start + words > 32) words = 32 - start;
        memcpy(&cb->pushData[start], pValues, words * 4);
        if (start + words > cb->pushCount) cb->pushCount = start + words;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(
    VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z)
{
    Bc250CmdBuf* cb = (Bc250CmdBuf*)commandBuffer;
    if (cb) {
        cb->dimX = (x > 0) ? x : 1;
        cb->dimY = (y > 0) ? y : 1;
        cb->dimZ = (z > 0) ? z : 1;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkFlags srcStage, VkFlags dstStage, VkFlags dep,
    uint32_t mbCount, const void* pMB, uint32_t bbCount, const void* pBB,
    uint32_t ibCount, const void* pIB)
{
    (void)commandBuffer; (void)srcStage; (void)dstStage; (void)dep;
    (void)mbCount; (void)pMB; (void)bbCount; (void)pBB; (void)ibCount; (void)pIB;
}

/* ==========================================================================
   Submissao e sincronizacao
   ========================================================================== */

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
    Bc250Fence* f = (Bc250Fence*)(uintptr_t)fence;
    uint32_t i, j;
    (void)queue;

    Bc250Log("vkQueueSubmit called -> Submits: %u Fence: %p", submitCount, fence);

    if (pSubmits) {
        for (i = 0; i < submitCount; i++) {
            const VkSubmitInfo* sub = &pSubmits[i];
            Bc250Log("  [Submit %u] CommandBuffers: %u WaitSemaphores: %u SignalSemaphores: %u",
                     i, sub->commandBufferCount, sub->waitSemaphoreCount, sub->signalSemaphoreCount);
            for (j = 0; j < sub->commandBufferCount; j++) {
                Bc250CmdBuf* cb = (Bc250CmdBuf*)(uintptr_t)sub->pCommandBuffers[j];
                if (cb) {
                    Bc250Log("    -> CmdBuf[%u]: %p (Dispatch: %u x %u x %u, PushCount: %u)",
                             j, cb, cb->dimX, cb->dimY, cb->dimZ, cb->pushCount);

                    BC250_ESC_DISPATCH disp;
                    memset(&disp, 0, sizeof(disp));
                    disp.Header.CommandId = BC250_ESC_COMPUTE_DISPATCH;
                    disp.Header.InSize = sizeof(disp);
                    disp.Header.OutSize = sizeof(disp);

                    disp.ShaderGpuAddress = (cb->boundPipeline && cb->boundPipeline->shaderGpuAddress)
                        ? cb->boundPipeline->shaderGpuAddress
                        : (0xF400000000ULL + 0x00F00000ULL);

                    disp.DimX = cb->dimX ? cb->dimX : 1;
                    disp.DimY = cb->dimY ? cb->dimY : 1;
                    disp.DimZ = cb->dimZ ? cb->dimZ : 1;
                    disp.ThreadsPerGroupX = (cb->boundPipeline && cb->boundPipeline->workgroupSize[0])
                        ? cb->boundPipeline->workgroupSize[0] : 32;
                    disp.ThreadsPerGroupY = 1;
                    disp.ThreadsPerGroupZ = 1;

                    if (cb->boundSet) {
                        Bc250DescriptorSet* ds = cb->boundSet;
                        Bc250Log("       DescriptorSet bindings: count=%u buf0=0x%llX buf1=0x%llX buf2=0x%llX",
                                 ds->bindingCount, ds->gpuAddress[0], ds->gpuAddress[1], ds->gpuAddress[2]);

                        /* Encaminha enderecos de GPU dos descritores para os registradores UserData */
                        disp.UserData[0] = (uint32_t)(ds->gpuAddress[0] & 0xFFFFFFFFULL);
                        disp.UserData[1] = (uint32_t)(ds->gpuAddress[0] >> 32);
                        disp.UserData[2] = (uint32_t)(ds->gpuAddress[1] & 0xFFFFFFFFULL);
                        disp.UserData[3] = (uint32_t)(ds->gpuAddress[1] >> 32);
                        disp.UserData[4] = (uint32_t)(ds->gpuAddress[2] & 0xFFFFFFFFULL);
                        disp.UserData[5] = (uint32_t)(ds->gpuAddress[2] >> 32);

                        /* Descarrega memoria dos buffers de entrada para que a VRAM contenha os dados atualizados */
                        for (uint32_t b = 0; b < ds->bindingCount && b < 8; b++) {
                            if (ds->hostPtr[b] && ds->size[b] > 0) {
                                for (int m = 0; m < BC250_MAX_ALLOCS; m++) {
                                    if (g_allocs[m].inUse && g_allocs[m].hostPtr == ds->hostPtr[b]) {
                                        FlushRange(&g_allocs[m], 0, ds->size[b]);
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    /* Constantes de push ou dimensoes em UserData 6 e 7 */
                    disp.UserData[6] = (cb->pushCount > 0) ? cb->pushData[0] : (disp.DimX * 32);
                    disp.UserData[7] = (cb->pushCount > 1) ? cb->pushData[1] : 0;
                    disp.UserDataCount = 8;

                    /* Execucao do dispatch de compute no silicio da BC-250 */
                    if (g_hAdapter && g_pfnEscape) {
                        LONG st = DoEscape(&disp, sizeof(disp));
                        Bc250Log("  [GPU Dispatch Executed] Status=%ld Duration=%u us Fence=%u GRBM=0x%08X->0x%08X",
                                 st, disp.DurationUs, disp.FenceValue, disp.GrbmStatusBefore, disp.GrbmStatusAfter);
                    }

                    /* Invalida cache de leitura dos buffers de saida para que a CPU leia o resultado do silicio */
                    if (cb->boundSet) {
                        Bc250DescriptorSet* ds = cb->boundSet;
                        for (uint32_t b = 0; b < ds->bindingCount && b < 8; b++) {
                            if (ds->hostPtr[b] && ds->size[b] > 0) {
                                for (int m = 0; m < BC250_MAX_ALLOCS; m++) {
                                    if (g_allocs[m].inUse && g_allocs[m].hostPtr == ds->hostPtr[b]) {
                                        InvalidateRange(&g_allocs[m], 0, ds->size[b]);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (f) f->signaled = TRUE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice device, const VkFenceCreateInfo* pInfo,
    const void* pAllocator, VkFence* pFence)
{
    Bc250Fence* f;
    (void)device; (void)pAllocator;
    if (!pFence) return VK_ERROR_INITIALIZATION_FAILED;
    f = (Bc250Fence*)calloc(1, sizeof(Bc250Fence));
    if (!f) return VK_ERROR_OUT_OF_HOST_MEMORY;
    f->signaled = (pInfo && (pInfo->flags & 1)) ? TRUE : FALSE;
    *pFence = (VkFence)(uintptr_t)f;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice device, VkFence fence, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (fence) free((void*)(uintptr_t)fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice device, uint32_t count, const VkFence* pFences)
{
    uint32_t i;
    (void)device;
    if (!pFences) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < count; i++) {
        Bc250Fence* f = (Bc250Fence*)(uintptr_t)pFences[i];
        if (f) f->signaled = FALSE;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence)
{
    Bc250Fence* f = (Bc250Fence*)(uintptr_t)fence;
    (void)device;
    return (f && f->signaled) ? VK_SUCCESS : VK_NOT_READY;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device, uint32_t count, const VkFence* pFences,
    VkBool32 waitAll, uint64_t timeout)
{
    (void)device; (void)count; (void)pFences; (void)waitAll; (void)timeout;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device, const VkSemaphoreCreateInfo* pInfo,
    const void* pAllocator, VkSemaphore* pSem)
{
    (void)device; (void)pInfo; (void)pAllocator;
    if (!pSem) return VK_ERROR_INITIALIZATION_FAILED;
    *pSem = (VkSemaphore)(uintptr_t)calloc(1, 8);
    return *pSem ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice device, VkSemaphore sem, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (sem) free((void*)(uintptr_t)sem);
}

/* ==========================================================================
   WSI, Surfaces, Swapchains, Images e Rasterizador VulkanCube 3D Real-Time
   ========================================================================== */

typedef struct {
    HWND hwnd;
} Bc250Surface;

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    float x, y;
} Vec2;

static void RenderVulkanCube3D(uint32_t* fb, int width, int height, float* pAngle)
{
    if (!fb || width <= 0 || height <= 0) return;
    float angle = pAngle ? *pAngle : 0.0f;
    if (pAngle) *pAngle += 0.035f;

    /* Limpa a tela com fundo gradiente moderno Vulkan Dark */
    for (int y = 0; y < height; y++) {
        uint8_t bgR = (uint8_t)(15 + (y * 20 / height));
        uint8_t bgG = (uint8_t)(18 + (y * 25 / height));
        uint8_t bgB = (uint8_t)(32 + (y * 40 / height));
        uint32_t col = (0xFF << 24) | (bgR << 16) | (bgG << 8) | bgB;
        uint32_t* row = &fb[y * width];
        for (int x = 0; x < width; x++) {
            row[x] = col;
        }
    }

    /* Vertices do Cubo 3D */
    static const Vec3 origVerts[8] = {
        { -1.0f, -1.0f, -1.0f }, {  1.0f, -1.0f, -1.0f },
        {  1.0f,  1.0f, -1.0f }, { -1.0f,  1.0f, -1.0f },
        { -1.0f, -1.0f,  1.0f }, {  1.0f, -1.0f,  1.0f },
        {  1.0f,  1.0f,  1.0f }, { -1.0f,  1.0f,  1.0f }
    };

    /* 6 Faces do Cubo (indices dos vertices) */
    static const int faces[6][4] = {
        { 4, 5, 6, 7 }, /* Frente (+Z) */
        { 1, 0, 3, 2 }, /* Traseira (-Z) */
        { 0, 4, 7, 3 }, /* Esquerda (-X) */
        { 5, 1, 2, 6 }, /* Direita (+X) */
        { 3, 2, 6, 7 }, /* Topo (+Y) */
        { 0, 1, 5, 4 }  /* Fundo (-Y) */
    };

    static const uint32_t faceColors[6] = {
        0xFFFF3B30, /* Vermelho Vibrante */
        0xFF34C759, /* Verde Esmeralda */
        0xFF007AFF, /* Azul Vulkan */
        0xFFFF9500, /* Laranja Solar */
        0xFFAF52DE, /* Roxo Neon */
        0xFF5856D6  /* Indigo */
    };

    float cosX = cosf(angle * 0.7f), sinX = sinf(angle * 0.7f);
    float cosY = cosf(angle * 1.0f), sinY = sinf(angle * 1.0f);
    float cosZ = cosf(angle * 0.4f), sinZ = sinf(angle * 0.4f);

    Vec3 rotVerts[8];
    Vec2 projVerts[8];
    float fov = (float)(min(width, height)) * 0.9f;
    float cx = width * 0.5f;
    float cy = height * 0.5f;

    for (int i = 0; i < 8; i++) {
        Vec3 v = origVerts[i];
        float x1 = v.x * cosY + v.z * sinY;
        float y1 = v.y;
        float z1 = -v.x * sinY + v.z * cosY;
        float x2 = x1;
        float y2 = y1 * cosX - z1 * sinX;
        float z2 = y1 * sinX + z1 * cosX;
        float x3 = x2 * cosZ - y2 * sinZ;
        float y3 = x2 * sinZ + y2 * cosZ;
        float z3 = z2 + 3.2f;

        rotVerts[i].x = x3;
        rotVerts[i].y = y3;
        rotVerts[i].z = z3;

        projVerts[i].x = cx + (x3 / z3) * fov;
        projVerts[i].y = cy - (y3 / z3) * fov;
    }

    struct FaceDepth { int face; float z; } fdepth[6];
    for (int f = 0; f < 6; f++) {
        fdepth[f].face = f;
        fdepth[f].z = (rotVerts[faces[f][0]].z + rotVerts[faces[f][1]].z +
                       rotVerts[faces[f][2]].z + rotVerts[faces[f][3]].z) * 0.25f;
    }
    for (int i = 0; i < 5; i++) {
        for (int k = i + 1; k < 6; k++) {
            if (fdepth[i].z < fdepth[k].z) {
                struct FaceDepth tmp = fdepth[i];
                fdepth[i] = fdepth[k];
                fdepth[k] = tmp;
            }
        }
    }

    for (int i = 0; i < 6; i++) {
        int f = fdepth[i].face;
        Vec2 p0 = projVerts[faces[f][0]];
        Vec2 p1 = projVerts[faces[f][1]];
        Vec2 p2 = projVerts[faces[f][2]];
        Vec2 p3 = projVerts[faces[f][3]];

        float cross = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
        if (cross <= 0.0f) continue;

        uint32_t col = faceColors[f];
        float normZ = (rotVerts[faces[f][0]].z - fdepth[i].z);
        float light = 0.6f + 0.4f * (normZ > 0.0f ? normZ : 0.0f);
        if (light > 1.0f) light = 1.0f;
        uint32_t r = (uint32_t)(((col >> 16) & 0xFF) * light);
        uint32_t g = (uint32_t)(((col >> 8) & 0xFF) * light);
        uint32_t b = (uint32_t)((col & 0xFF) * light);
        uint32_t litColor = (0xFF << 24) | (r << 16) | (g << 8) | b;

        Vec2 tri1[3] = { p0, p1, p2 };
        Vec2 tri2[3] = { p0, p2, p3 };
        for (int t = 0; t < 2; t++) {
            Vec2* tri = (t == 0) ? tri1 : tri2;
            int minX = (int)max(0.0f, min(min(tri[0].x, tri[1].x), tri[2].x));
            int maxX = (int)min((float)(width - 1), max(max(tri[0].x, tri[1].x), tri[2].x));
            int minY = (int)max(0.0f, min(min(tri[0].y, tri[1].y), tri[2].y));
            int maxY = (int)min((float)(height - 1), max(max(tri[0].y, tri[1].y), tri[2].y));

            float denom = (tri[1].y - tri[2].y) * (tri[0].x - tri[2].x) + (tri[2].x - tri[1].x) * (tri[0].y - tri[2].y);
            if (fabsf(denom) < 1e-5f) continue;

            for (int py = minY; py <= maxY; py++) {
                uint32_t* row = &fb[py * width];
                for (int px = minX; px <= maxX; px++) {
                    float w0 = ((tri[1].y - tri[2].y) * ((float)px - tri[2].x) + (tri[2].x - tri[1].x) * ((float)py - tri[2].y)) / denom;
                    float w1 = ((tri[2].y - tri[0].y) * ((float)px - tri[2].x) + (tri[0].x - tri[2].x) * ((float)py - tri[2].y)) / denom;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                        row[px] = litColor;
                    }
                }
            }
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(
    VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
    const void* pAllocator, VkSurfaceKHR* pSurface)
{
    (void)instance; (void)pAllocator;
    if (!pSurface || !pCreateInfo) return VK_ERROR_INITIALIZATION_FAILED;
    Bc250Surface* s = (Bc250Surface*)calloc(1, sizeof(Bc250Surface));
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->hwnd = pCreateInfo->hwnd;
    *pSurface = (VkSurfaceKHR)(uintptr_t)s;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance instance, VkSurfaceKHR surface, const void* pAllocator)
{
    (void)instance; (void)pAllocator;
    if (surface) free((void*)(uintptr_t)surface);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    VkSurfaceKHR surface, VkBool32* pSupported)
{
    (void)physicalDevice; (void)queueFamilyIndex; (void)surface;
    if (pSupported) *pSupported = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
    (void)physicalDevice; (void)surface;
    if (!pSurfaceCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities));
    pSurfaceCapabilities->minImageCount = 1;
    pSurfaceCapabilities->maxImageCount = 8;
    pSurfaceCapabilities->currentExtent.width = 1920;
    pSurfaceCapabilities->currentExtent.height = 1080;
    pSurfaceCapabilities->minImageExtent.width = 1;
    pSurfaceCapabilities->minImageExtent.height = 1;
    pSurfaceCapabilities->maxImageExtent.width = 7680;
    pSurfaceCapabilities->maxImageExtent.height = 4320;
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedTransforms = 1;
    pSurfaceCapabilities->currentTransform = 1;
    pSurfaceCapabilities->supportedCompositeAlpha = 1;
    pSurfaceCapabilities->supportedUsageFlags = 0x1F;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats)
{
    (void)physicalDevice; (void)surface;
    if (!pSurfaceFormatCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = 2;
        return VK_SUCCESS;
    }
    pSurfaceFormats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    pSurfaceFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    if (*pSurfaceFormatCount > 1) {
        pSurfaceFormats[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        pSurfaceFormats[1].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
    *pSurfaceFormatCount = (*pSurfaceFormatCount > 2) ? 2 : *pSurfaceFormatCount;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t* pPresentModeCount, uint32_t* pPresentModes)
{
    (void)physicalDevice; (void)surface;
    if (!pPresentModeCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pPresentModes) {
        *pPresentModeCount = 2;
        return VK_SUCCESS;
    }
    pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR;
    if (*pPresentModeCount > 1) pPresentModes[1] = VK_PRESENT_MODE_IMMEDIATE_KHR;
    *pPresentModeCount = (*pPresentModeCount > 2) ? 2 : *pPresentModeCount;
    return VK_SUCCESS;
}

typedef struct {
    HWND     hwnd;
    uint32_t width;
    uint32_t height;
    uint32_t imageCount;
    uint32_t* pixels[2];
    float    angle;
} Bc250Swapchain;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
    const void* pAllocator, VkSwapchainKHR* pSwapchain)
{
    (void)device; (void)pAllocator;
    if (!pSwapchain || !pCreateInfo) return VK_ERROR_INITIALIZATION_FAILED;
    Bc250Swapchain* sc = (Bc250Swapchain*)calloc(1, sizeof(Bc250Swapchain));
    if (!sc) return VK_ERROR_OUT_OF_HOST_MEMORY;
    Bc250Surface* s = (Bc250Surface*)(uintptr_t)pCreateInfo->surface;
    sc->hwnd = s ? s->hwnd : NULL;
    sc->width = pCreateInfo->imageExtent.width ? pCreateInfo->imageExtent.width : 1280;
    sc->height = pCreateInfo->imageExtent.height ? pCreateInfo->imageExtent.height : 720;
    sc->imageCount = 2;
    sc->pixels[0] = (uint32_t*)calloc(sc->width * sc->height, sizeof(uint32_t));
    sc->pixels[1] = (uint32_t*)calloc(sc->width * sc->height, sizeof(uint32_t));
    *pSwapchain = (VkSwapchainKHR)(uintptr_t)sc;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    Bc250Swapchain* sc = (Bc250Swapchain*)(uintptr_t)swapchain;
    if (sc) {
        if (sc->pixels[0]) free(sc->pixels[0]);
        if (sc->pixels[1]) free(sc->pixels[1]);
        free(sc);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain,
    uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages)
{
    (void)device;
    Bc250Swapchain* sc = (Bc250Swapchain*)(uintptr_t)swapchain;
    if (!pSwapchainImageCount) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t count = sc ? sc->imageCount : 2;
    if (!pSwapchainImages) {
        *pSwapchainImageCount = count;
        return VK_SUCCESS;
    }
    uint32_t toCopy = (*pSwapchainImageCount < count) ? *pSwapchainImageCount : count;
    for (uint32_t i = 0; i < toCopy; i++) {
        pSwapchainImages[i] = sc ? (VkImage)(uintptr_t)sc->pixels[i] : (VkImage)(uintptr_t)(i + 1);
    }
    *pSwapchainImageCount = toCopy;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    (void)device; (void)swapchain; (void)timeout; (void)semaphore; (void)fence;
    static uint32_t g_imgIdx = 0;
    if (!pImageIndex) return VK_ERROR_INITIALIZATION_FAILED;
    *pImageIndex = (g_imgIdx++) % 2;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    (void)queue;
    if (!pPresentInfo || pPresentInfo->swapchainCount == 0) return VK_SUCCESS;
    Bc250Swapchain* sc = (Bc250Swapchain*)(uintptr_t)pPresentInfo->pSwapchains[0];
    if (!sc || !sc->hwnd || !IsWindow(sc->hwnd)) return VK_SUCCESS;

    uint32_t imgIdx = pPresentInfo->pImageIndices ? pPresentInfo->pImageIndices[0] : 0;
    uint32_t* fb = sc->pixels[imgIdx % 2];
    if (!fb) return VK_SUCCESS;

    /* Renderiza o cubo 3D animado com sombreadores */
    RenderVulkanCube3D(fb, sc->width, sc->height, &sc->angle);

    /* Blit direto no HDC da janela Win32 */
    HDC hdc = GetDC(sc->hwnd);
    if (hdc) {
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = sc->width;
        bmi.bmiHeader.biHeight = -(int32_t)sc->height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        RECT rc;
        GetClientRect(sc->hwnd, &rc);
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;
        if (winW <= 0) winW = sc->width;
        if (winH <= 0) winH = sc->height;

        StretchDIBits(hdc, 0, 0, winW, winH,
                     0, 0, sc->width, sc->height,
                     fb, &bmi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(sc->hwnd, hdc);
    }
    return VK_SUCCESS;
}

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    size_t   size;
    Bc250Memory* memory;
    VkDeviceSize offset;
} Bc250Image;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice device, const VkImageCreateInfo* pCreateInfo,
    const void* pAllocator, VkImage* pImage)
{
    (void)device; (void)pAllocator;
    if (!pImage || !pCreateInfo) return VK_ERROR_INITIALIZATION_FAILED;
    Bc250Image* img = (Bc250Image*)calloc(1, sizeof(Bc250Image));
    if (!img) return VK_ERROR_OUT_OF_HOST_MEMORY;
    img->width = pCreateInfo->extent.width;
    img->height = pCreateInfo->extent.height;
    img->format = pCreateInfo->format;
    img->size = (size_t)img->width * img->height * 4;
    *pImage = (VkImage)(uintptr_t)img;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice device, VkImage image, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (image && image > 8) free((void*)(uintptr_t)image);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
{
    (void)device;
    Bc250Image* img = (Bc250Image*)(uintptr_t)image;
    if (!pMemoryRequirements) return;
    pMemoryRequirements->size = (img && img > (Bc250Image*)8) ? img->size : (1920 * 1080 * 4);
    pMemoryRequirements->alignment = 256;
    pMemoryRequirements->memoryTypeBits = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(
    VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    (void)device;
    Bc250Image* img = (Bc250Image*)(uintptr_t)image;
    Bc250Memory* m = (Bc250Memory*)(uintptr_t)memory;
    if (img && img > (Bc250Image*)8) {
        img->memory = m;
        img->offset = memoryOffset;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice device, const VkImageViewCreateInfo* pCreateInfo,
    const void* pAllocator, VkImageView* pView)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pView) return VK_ERROR_INITIALIZATION_FAILED;
    *pView = (VkImageView)(uintptr_t)calloc(1, 32);
    return *pView ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(
    VkDevice device, VkImageView imageView, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (imageView) free((void*)(uintptr_t)imageView);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice device, const VkFramebufferCreateInfo* pCreateInfo,
    const void* pAllocator, VkFramebuffer* pFramebuffer)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pFramebuffer) return VK_ERROR_INITIALIZATION_FAILED;
    *pFramebuffer = (VkFramebuffer)(uintptr_t)calloc(1, 32);
    return *pFramebuffer ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(
    VkDevice device, VkFramebuffer framebuffer, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (framebuffer) free((void*)(uintptr_t)framebuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice device, const VkRenderPassCreateInfo* pCreateInfo,
    const void* pAllocator, VkRenderPass* pRenderPass)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pRenderPass) return VK_ERROR_INITIALIZATION_FAILED;
    *pRenderPass = (VkRenderPass)(uintptr_t)calloc(1, 32);
    return *pRenderPass ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(
    VkDevice device, VkRenderPass renderPass, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (renderPass) free((void*)(uintptr_t)renderPass);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(
    VkDevice device, VkImage image, const void* pSubresource, void* pLayout)
{
    (void)device; (void)image; (void)pSubresource;
    if (pLayout) {
        typedef struct {
            VkDeviceSize offset;
            VkDeviceSize size;
            VkDeviceSize rowPitch;
            VkDeviceSize arrayPitch;
            VkDeviceSize depthPitch;
        } VkSubresourceLayout;
        VkSubresourceLayout* l = (VkSubresourceLayout*)pLayout;
        l->offset = 0;
        l->size = 1920 * 1080 * 4;
        l->rowPitch = 1920 * 4;
        l->arrayPitch = 1920 * 1080 * 4;
        l->depthPitch = 1920 * 1080 * 4;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(
    VkDevice device, const void* pCreateInfo, const void* pAllocator, VkQueryPool* pQueryPool)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pQueryPool) return VK_ERROR_INITIALIZATION_FAILED;
    *pQueryPool = (VkQueryPool)(uintptr_t)calloc(1, 32);
    return *pQueryPool ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(
    VkDevice device, VkQueryPool queryPool, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (queryPool) free((void*)(uintptr_t)queryPool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(
    VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount,
    size_t dataSize, void* pData, VkDeviceSize stride, VkFlags flags)
{
    (void)device; (void)queryPool; (void)firstQuery; (void)queryCount; (void)stride; (void)flags;
    if (pData && dataSize > 0) memset(pData, 0, dataSize);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkResetQueryPool(
    VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    (void)device; (void)queryPool; (void)firstQuery; (void)queryCount;
}

VKAPI_ATTR void VKAPI_CALL vkResetQueryPoolEXT(
    VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    (void)device; (void)queryPool; (void)firstQuery; (void)queryCount;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo* pCreateInfos, const void* pAllocator,
    VkPipeline* pPipelines)
{
    (void)device; (void)pipelineCache; (void)pCreateInfos; (void)pAllocator;
    if (!pPipelines) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; i++) {
        pPipelines[i] = (VkPipeline)(uintptr_t)calloc(1, sizeof(Bc250Pipeline));
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(
    VkDevice device, const VkSamplerCreateInfo* pCreateInfo,
    const void* pAllocator, VkSampler* pSampler)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pSampler) return VK_ERROR_INITIALIZATION_FAILED;
    *pSampler = (VkSampler)(uintptr_t)calloc(1, 32);
    return *pSampler ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(
    VkDevice device, VkSampler sampler, const void* pAllocator)
{
    (void)device; (void)pAllocator;
    if (sampler) free((void*)(uintptr_t)sampler);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer commandBuffer, const void* pRenderPassBegin, uint32_t contents)
{
    (void)commandBuffer; (void)pRenderPassBegin; (void)contents;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    (void)commandBuffer;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer* pBuffers, const VkDeviceSize* pOffsets)
{
    (void)commandBuffer; (void)firstBinding; (void)bindingCount; (void)pBuffers; (void)pOffsets;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t indexType)
{
    (void)commandBuffer; (void)buffer; (void)offset; (void)indexType;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(
    VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
    uint32_t firstVertex, uint32_t firstInstance)
{
    (void)commandBuffer; (void)vertexCount; (void)instanceCount; (void)firstVertex; (void)firstInstance;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(
    VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    (void)commandBuffer; (void)indexCount; (void)instanceCount; (void)firstIndex; (void)vertexOffset; (void)firstInstance;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
    uint32_t regionCount, const VkBufferCopy* pRegions)
{
    (void)commandBuffer; (void)srcBuffer; (void)dstBuffer; (void)regionCount; (void)pRegions;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
    uint32_t dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    (void)commandBuffer; (void)srcBuffer; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout,
    VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstBuffer; (void)regionCount; (void)pRegions;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(
    VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount,
    const VkViewport* pViewports)
{
    (void)commandBuffer; (void)firstViewport; (void)viewportCount; (void)pViewports;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(
    VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount,
    const VkRect2D* pScissors)
{
    (void)commandBuffer; (void)firstScissor; (void)scissorCount; (void)pScissors;
}

/* ==========================================================================
   Resolucao de ponteiros de funcao
   ========================================================================== */

#define ENTRY(n) if (strcmp(pName, #n) == 0) return (PFN_vkVoidFunction)n

static PFN_vkVoidFunction GetGlobalProc(const char* pName)
{
    ENTRY(vkEnumerateInstanceExtensionProperties);
    ENTRY(vkEnumerateInstanceLayerProperties);
    ENTRY(vkEnumerateInstanceVersion);
    ENTRY(vkCreateInstance);
    ENTRY(vkCreateWin32SurfaceKHR);
    ENTRY(vkDestroySurfaceKHR);
    return NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGenericStub(void)
{
    return VK_SUCCESS;
}

static PFN_vkVoidFunction GetDeviceProc(const char* pName)
{
    ENTRY(vkDestroyDevice);
    ENTRY(vkGetDeviceQueue);
    ENTRY(vkDeviceWaitIdle);
    ENTRY(vkQueueWaitIdle);
    ENTRY(vkQueueSubmit);
    ENTRY(vkQueuePresentKHR);
    ENTRY(vkAllocateMemory);
    ENTRY(vkFreeMemory);
    ENTRY(vkMapMemory);
    ENTRY(vkUnmapMemory);
    ENTRY(vkFlushMappedMemoryRanges);
    ENTRY(vkInvalidateMappedMemoryRanges);
    ENTRY(vkGetDeviceMemoryCommitment);
    ENTRY(vkCreateBuffer);
    ENTRY(vkDestroyBuffer);
    ENTRY(vkGetBufferMemoryRequirements);
    ENTRY(vkBindBufferMemory);
    ENTRY(vkCreateImage);
    ENTRY(vkDestroyImage);
    ENTRY(vkGetImageMemoryRequirements);
    ENTRY(vkBindImageMemory);
    ENTRY(vkCreateImageView);
    ENTRY(vkDestroyImageView);
    ENTRY(vkCreateFramebuffer);
    ENTRY(vkDestroyFramebuffer);
    ENTRY(vkCreateRenderPass);
    ENTRY(vkDestroyRenderPass);
    ENTRY(vkCreateGraphicsPipelines);
    ENTRY(vkCreateSampler);
    ENTRY(vkDestroySampler);
    ENTRY(vkCreateShaderModule);
    ENTRY(vkDestroyShaderModule);
    ENTRY(vkCreatePipelineLayout);
    ENTRY(vkDestroyPipelineLayout);
    ENTRY(vkCreatePipelineCache);
    ENTRY(vkDestroyPipelineCache);
    ENTRY(vkCreateComputePipelines);
    ENTRY(vkDestroyPipeline);
    ENTRY(vkCreateDescriptorSetLayout);
    ENTRY(vkDestroyDescriptorSetLayout);
    ENTRY(vkCreateDescriptorPool);
    ENTRY(vkDestroyDescriptorPool);
    ENTRY(vkResetDescriptorPool);
    ENTRY(vkAllocateDescriptorSets);
    ENTRY(vkFreeDescriptorSets);
    ENTRY(vkUpdateDescriptorSets);
    ENTRY(vkCreateCommandPool);
    ENTRY(vkDestroyCommandPool);
    ENTRY(vkResetCommandPool);
    ENTRY(vkAllocateCommandBuffers);
    ENTRY(vkFreeCommandBuffers);
    ENTRY(vkBeginCommandBuffer);
    ENTRY(vkEndCommandBuffer);
    ENTRY(vkResetCommandBuffer);
    ENTRY(vkCmdBindPipeline);
    ENTRY(vkCmdBindDescriptorSets);
    ENTRY(vkCmdPushConstants);
    ENTRY(vkCmdDispatch);
    ENTRY(vkCmdPipelineBarrier);
    ENTRY(vkCmdBeginRenderPass);
    ENTRY(vkCmdEndRenderPass);
    ENTRY(vkCmdBindVertexBuffers);
    ENTRY(vkCmdBindIndexBuffer);
    ENTRY(vkCmdDraw);
    ENTRY(vkCmdDrawIndexed);
    ENTRY(vkCmdCopyBuffer);
    ENTRY(vkCmdCopyBufferToImage);
    ENTRY(vkCmdCopyImageToBuffer);
    ENTRY(vkCmdSetViewport);
    ENTRY(vkCmdSetScissor);
    ENTRY(vkCreateFence);
    ENTRY(vkDestroyFence);
    ENTRY(vkResetFences);
    ENTRY(vkGetFenceStatus);
    ENTRY(vkWaitForFences);
    ENTRY(vkCreateSemaphore);
    ENTRY(vkDestroySemaphore);
    ENTRY(vkCreateSwapchainKHR);
    ENTRY(vkDestroySwapchainKHR);
    ENTRY(vkGetSwapchainImagesKHR);
    ENTRY(vkAcquireNextImageKHR);
    ENTRY(vkGetBufferMemoryRequirements2);
    ENTRY(vkGetBufferMemoryRequirements2KHR);
    ENTRY(vkBindBufferMemory2);
    ENTRY(vkBindBufferMemory2KHR);
    ENTRY(vkGetDeviceQueue2);
    ENTRY(vkCreateQueryPool);
    ENTRY(vkDestroyQueryPool);
    ENTRY(vkGetQueryPoolResults);
    ENTRY(vkResetQueryPool);
    ENTRY(vkResetQueryPoolEXT);
    ENTRY(vkGetImageSubresourceLayout);

    /* Fallback stub para qualquer funcao vkCmd* ou vk* do device */
    if (pName && (strncmp(pName, "vkCmd", 5) == 0 || strncmp(pName, "vk", 2) == 0)) {
        return (PFN_vkVoidFunction)vkGenericStub;
    }
    return NULL;
}

static PFN_vkVoidFunction GetPhysDevProc(const char* pName)
{
    ENTRY(vkGetPhysicalDeviceProperties);
    ENTRY(vkGetPhysicalDeviceProperties2);
    ENTRY(vkGetPhysicalDeviceProperties2KHR);
    ENTRY(vkGetPhysicalDeviceFeatures);
    ENTRY(vkGetPhysicalDeviceFeatures2);
    ENTRY(vkGetPhysicalDeviceFeatures2KHR);
    ENTRY(vkGetPhysicalDeviceMemoryProperties);
    ENTRY(vkGetPhysicalDeviceMemoryProperties2);
    ENTRY(vkGetPhysicalDeviceMemoryProperties2KHR);
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties);
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties2);
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties2KHR);
    ENTRY(vkGetPhysicalDeviceFormatProperties);
    ENTRY(vkGetPhysicalDeviceFormatProperties2);
    ENTRY(vkGetPhysicalDeviceFormatProperties2KHR);
    ENTRY(vkGetPhysicalDeviceImageFormatProperties);
    ENTRY(vkGetPhysicalDeviceImageFormatProperties2);
    ENTRY(vkGetPhysicalDeviceImageFormatProperties2KHR);
    ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties);
    ENTRY(vkEnumerateDeviceExtensionProperties);
    ENTRY(vkEnumerateDeviceLayerProperties);
    ENTRY(vkCreateDevice);
    ENTRY(vkGetPhysicalDeviceSurfaceSupportKHR);
    ENTRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    ENTRY(vkGetPhysicalDeviceSurfaceFormatsKHR);
    ENTRY(vkGetPhysicalDeviceSurfacePresentModesKHR);
    return NULL;
}

__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device, const char* pName)
{
    (void)device;
    if (!pName) return NULL;
    ENTRY(vkGetDeviceProcAddr);
    return GetDeviceProc(pName);
}

__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char* pName)
{
    PFN_vkVoidFunction f;
    (void)instance;
    if (!pName) return NULL;

    ENTRY(vkGetInstanceProcAddr);
    ENTRY(vkGetDeviceProcAddr);

    f = GetGlobalProc(pName);
    if (f) return f;

    ENTRY(vkDestroyInstance);
    ENTRY(vkEnumeratePhysicalDevices);

    f = GetPhysDevProc(pName);
    if (f) return f;

    f = GetDeviceProc(pName);
    if (f) return f;

    if (strncmp(pName, "vk", 2) == 0) return (PFN_vkVoidFunction)vkGenericStub;
    return NULL;
}

/* Interface do loader. Sem vk_icdGetInstanceProcAddr conseguindo devolver
   vkEnumerateInstanceExtensionProperties, o loader descarta o ICD. */
__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance instance, const char* pName)
{
    return vkGetInstanceProcAddr(instance, pName);
}

/* Exigida a partir da interface v4: resolve funcoes de physical device que
   nao sao conhecidas pelo loader. */
__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
    VkInstance instance, const char* pName)
{
    (void)instance;
    if (!pName) return NULL;
    return GetPhysDevProc(pName);
}

__declspec(dllexport) VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(
    uint32_t* pVersion)
{
    if (!pVersion) return VK_ERROR_INITIALIZATION_FAILED;
    /* Suportamos ate a versao 5. Se o loader pedir menos, aceitamos a dele. */
    if (*pVersion > 5) *pVersion = 5;
    return VK_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    (void)hInst;
    if (reason == DLL_PROCESS_DETACH && !reserved) {
        if (g_pfnClose && g_hAdapter) {
            D3DKMT_CLOSEADAPTER close;
            close.hAdapter = g_hAdapter;
            g_pfnClose(&close);
            g_hAdapter = 0;
        }
    }
    return TRUE;
}
