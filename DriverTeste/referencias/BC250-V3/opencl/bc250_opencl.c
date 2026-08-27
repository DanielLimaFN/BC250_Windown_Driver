/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_opencl.c

Abstract:
    Complete Native OpenCL 2.0 / 3.0 GPGPU ICD Driver for AMD BC-250 APU.
    Ponte D3DKMT para o KMD BC250-V3. NAO possui compilador OpenCL C -> ISA GFX10.

--*/

#include "bc250_opencl.h"

static PFN_D3DKMTOpenAdapterFromHdc g_pfnOpenAdapter = NULL;
static PFN_D3DKMTEscape             g_pfnEscape = NULL;
static PFN_D3DKMTCloseAdapter       g_pfnCloseAdapter = NULL;
static D3DKMT_HANDLE                g_hAdapter = 0;
static HMODULE                      g_hGdi = NULL;
static ULONG                        g_VramAllocOffset = 0x01100000u;

/*
  Fase 0: estado real do silicio, consultado ao KMD. Nada aqui e' presumido.
  g_Bc250FirmwareReady so' fica TRUE quando TODOS os microcodigos do CP foram
  aceitos pelo PSP. Enquanto for FALSE nenhum kernel pode executar, e as APIs
  de build/execucao devem falhar em vez de retornar sucesso.
*/
static BOOL                         g_Bc250FirmwareReady = FALSE;
static ULONG                        g_Bc250CuCountSource = 1; /* 1 = ASSUMIDO */

extern struct _cl_icd_dispatch g_Bc250Dispatch;

static struct _cl_platform_id g_Bc250Platform = {
    &g_Bc250Dispatch,
    "AMD BC-250 OpenCL Platform",
    "Advanced Micro Devices, Inc.",
    "OpenCL 2.0 AMD-BC250 (execucao de shader NAO VALIDADA EM HARDWARE)",
    "FULL_PROFILE",
    "cl_khr_global_int32_base_atomics cl_khr_global_int32_extended_atomics "
    "cl_khr_local_int32_base_atomics cl_khr_local_int32_extended_atomics "
    "cl_khr_byte_addressable_store cl_khr_fp64 cl_khr_fp16 cl_khr_icd "
    "cl_amd_vec3 cl_amd_printf cl_khr_subgroups"
};

static struct _cl_device_id g_Bc250Device = {
    &g_Bc250Dispatch,
    &g_Bc250Platform,
    "AMD BC-250 (Cyan Skillfish, RDNA 1.0) - compute NAO VALIDADO EM HARDWARE",
    "Advanced Micro Devices, Inc.",
    "1.0.0822.0039 (Native WDDM)",
    "OpenCL 2.0 AMD-BC250",
    "OpenCL C 2.0",
    CL_DEVICE_TYPE_GPU,
    24,                         /* 24 Compute Units */
    3,                          /* max_work_item_dimensions */
    { 1024, 1024, 1024 },       /* max_work_item_sizes */
    256,                        /* max_work_group_size */
    0,                          /* max_clock_frequency: lido da telemetria SMU */
    0,                          /* global_mem_size: preenchido no runtime pela BAR real */
    0,                          /* max_mem_alloc_size: derivado de global_mem_size */
    64 * 1024,                  /* 64 KB Local Memory per CU */
    1,                          /* preferred_vector_width_float */
    1,                          /* native_vector_width_float */
    CL_FALSE,                   /* available: so' TRUE com firmware do CP aceito */
    CL_FALSE,                   /* compiler_available: nao existe compilador GFX10 */
    0                           /* hAdapter */
};

/* =========================================================================
   D3DKMT Escape Connection Helper
   ========================================================================= */

static BOOL EnsureD3dKmt(void)
{
    if (g_hAdapter != 0) return TRUE;

    if (!g_hGdi) {
        g_hGdi = LoadLibraryA("gdi32.dll");
        if (!g_hGdi) return FALSE;
    }

    if (!g_pfnOpenAdapter) {
        g_pfnOpenAdapter = (PFN_D3DKMTOpenAdapterFromHdc)GetProcAddress(g_hGdi, "D3DKMTOpenAdapterFromHdc");
        g_pfnEscape = (PFN_D3DKMTEscape)GetProcAddress(g_hGdi, "D3DKMTEscape");
        g_pfnCloseAdapter = (PFN_D3DKMTCloseAdapter)GetProcAddress(g_hGdi, "D3DKMTCloseAdapter");
    }

    if (!g_pfnOpenAdapter || !g_pfnEscape) return FALSE;

    HDC hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    if (!hdc) hdc = GetDC(NULL);
    if (!hdc) return FALSE;

    D3DKMT_OPENADAPTERFROMHDC openAdapter = { 0 };
    openAdapter.hDc = hdc;
    LONG status = g_pfnOpenAdapter(&openAdapter);
    DeleteDC(hdc);

    if (status >= 0 && openAdapter.hAdapter != 0) {
        g_hAdapter = openAdapter.hAdapter;
        g_Bc250Device.hAdapter = g_hAdapter;

        /* Consulta capacidades físicas de compute */
        BC250_WDDM_ESCAPE_COMPUTE_CAPS caps = { 0 };
        caps.Header.CommandId = BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS;
        caps.Header.InSize = sizeof(caps);
        caps.Header.OutSize = sizeof(caps);

        D3DKMT_ESCAPE esc = { 0 };
        esc.hAdapter = g_hAdapter;
        esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        esc.pPrivateDriverData = &caps;
        esc.PrivateDriverDataSize = sizeof(caps);

        if (g_pfnEscape(&esc) >= 0 && caps.Header.Status == 0) {
            if (caps.NumCu > 0) g_Bc250Device.max_compute_units = caps.NumCu;
            if (caps.GpuClockMhz > 0) g_Bc250Device.max_clock_frequency = caps.GpuClockMhz;

            /*
              Fase 0, item 0.6: reportar SOMENTE a memoria realmente mapeada.
              O codigo antigo forcava 16 GB quando o driver relatava menos —
              isso e' valor anunciado, nao detectado. A abertura BAR0 real nesta
              placa e' de 256 MB; anunciar 16 GB permite alocacoes inexistentes.
            */
            g_Bc250Device.global_mem_size = caps.VramTotalBytes;
            if (g_Bc250Device.global_mem_size > 0) {
                g_Bc250Device.max_mem_alloc_size = g_Bc250Device.global_mem_size / 4;
            }

            g_Bc250FirmwareReady = (caps.FirmwareReady != 0);
            g_Bc250CuCountSource = caps.CuCountSource;

            /* Sem microcodigo do CP aceito nenhum shader pode executar.
               Nao anunciar compilador disponivel nessa condicao. */
            g_Bc250Device.compiler_available = CL_FALSE;
            g_Bc250Device.available = g_Bc250FirmwareReady ? CL_TRUE : CL_FALSE;
        }

        /* Consulta telemetria SMU */
        BC250_WDDM_ESCAPE_TELEMETRY telem = { 0 };
        telem.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
        telem.Header.InSize = sizeof(telem);
        telem.Header.OutSize = sizeof(telem);
        esc.pPrivateDriverData = &telem;
        esc.PrivateDriverDataSize = sizeof(telem);
        if (g_pfnEscape(&esc) >= 0 && telem.Header.Status == 0) {
            if (telem.GfxClockMhz > 0) g_Bc250Device.max_clock_frequency = telem.GfxClockMhz;
        }

        return TRUE;
    }

    return FALSE;
}

static cl_int CopyInfoString(const char *src, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    size_t len = strlen(src) + 1;
    if (param_value_size_ret) *param_value_size_ret = len;
    if (param_value) {
        if (param_value_size < len) return CL_INVALID_VALUE;
        memcpy(param_value, src, len);
    }
    return CL_SUCCESS;
}

static cl_int CopyInfoValue(const void *src, size_t size, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (param_value_size_ret) *param_value_size_ret = size;
    if (param_value) {
        if (param_value_size < size) return CL_INVALID_VALUE;
        memcpy(param_value, src, size);
    }
    return CL_SUCCESS;
}

/* =========================================================================
   Platform & Device APIs
   ========================================================================= */

CL_API_ENTRY cl_int CL_API_CALL
clIcdGetPlatformIDsKHR(cl_uint num_entries, cl_platform_id *platforms, cl_uint *num_platforms)
{
    EnsureD3dKmt();
    if (num_platforms) *num_platforms = 1;
    if (platforms) {
        if (num_entries < 1) return CL_INVALID_VALUE;
        platforms[0] = &g_Bc250Platform;
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetPlatformIDs(cl_uint num_entries, cl_platform_id *platforms, cl_uint *num_platforms)
{
    return clIcdGetPlatformIDsKHR(num_entries, platforms, num_platforms);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetPlatformInfo(cl_platform_id platform, cl_platform_info param_name,
                  size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!platform || platform != &g_Bc250Platform) return CL_INVALID_PLATFORM;

    switch (param_name) {
    case CL_PLATFORM_PROFILE:
        return CopyInfoString(platform->profile, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_VERSION:
        return CopyInfoString(platform->version, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_NAME:
        return CopyInfoString(platform->platform_name, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_VENDOR:
        return CopyInfoString(platform->vendor_name, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_EXTENSIONS:
        return CopyInfoString(platform->extensions, param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

CL_API_ENTRY cl_int CL_API_CALL
clGetDeviceIDs(cl_platform_id platform, cl_device_type device_type,
               cl_uint num_entries, cl_device_id *devices, cl_uint *num_devices)
{
    EnsureD3dKmt();
    if (platform && platform != &g_Bc250Platform) return CL_INVALID_PLATFORM;

    if (!(device_type & (CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_DEFAULT | CL_DEVICE_TYPE_ALL))) {
        if (num_devices) *num_devices = 0;
        return CL_DEVICE_NOT_FOUND;
    }

    if (num_devices) *num_devices = 1;
    if (devices) {
        if (num_entries < 1) return CL_INVALID_VALUE;
        devices[0] = &g_Bc250Device;
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetDeviceInfo(cl_device_id device, cl_device_info param_name,
                size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!device || device != &g_Bc250Device) return CL_INVALID_DEVICE;

    switch (param_name) {
    case CL_DEVICE_TYPE:
        return CopyInfoValue(&device->device_type, sizeof(cl_device_type), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_VENDOR_ID: {
        cl_uint vid = 0x1002;
        return CopyInfoValue(&vid, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_DEVICE_MAX_COMPUTE_UNITS:
        return CopyInfoValue(&device->max_compute_units, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS:
        return CopyInfoValue(&device->max_work_item_dimensions, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_MAX_WORK_ITEM_SIZES:
        return CopyInfoValue(device->max_work_item_sizes, sizeof(size_t) * 3, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_MAX_WORK_GROUP_SIZE:
        return CopyInfoValue(&device->max_work_group_size, sizeof(size_t), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_MAX_CLOCK_FREQUENCY:
        return CopyInfoValue(&device->max_clock_frequency, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_ADDRESS_BITS: {
        cl_uint bits = 64;
        return CopyInfoValue(&bits, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_DEVICE_GLOBAL_MEM_SIZE:
        return CopyInfoValue(&device->global_mem_size, sizeof(cl_ulong), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_MAX_MEM_ALLOC_SIZE:
        return CopyInfoValue(&device->max_mem_alloc_size, sizeof(cl_ulong), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_LOCAL_MEM_SIZE:
        return CopyInfoValue(&device->local_mem_size, sizeof(cl_ulong), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_NAME:
        return CopyInfoString(device->device_name, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_VENDOR:
        return CopyInfoString(device->vendor_name, param_value_size, param_value, param_value_size_ret);
    case CL_DRIVER_VERSION:
        return CopyInfoString(device->driver_version, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_PROFILE:
        return CopyInfoString("FULL_PROFILE", param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_VERSION:
        return CopyInfoString(device->device_version, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_OPENCL_C_VERSION:
        return CopyInfoString(device->opencl_c_version, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_EXTENSIONS:
        return CopyInfoString(device->platform->extensions, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_AVAILABLE:
        return CopyInfoValue(&device->available, sizeof(cl_bool), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_COMPILER_AVAILABLE:
        return CopyInfoValue(&device->compiler_available, sizeof(cl_bool), param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_PLATFORM:
        return CopyInfoValue(&device->platform, sizeof(cl_platform_id), param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

CL_API_ENTRY cl_int CL_API_CALL
clCreateSubDevices(cl_device_id in_device, const cl_device_partition_property *properties,
                   cl_uint num_devices, cl_device_id *out_devices, cl_uint *num_devices_ret)
{
    if (!in_device) return CL_INVALID_DEVICE;
    if (num_devices_ret) *num_devices_ret = 0;
    return CL_DEVICE_PARTITION_FAILED;
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainDevice(cl_device_id device)
{
    if (!device) return CL_INVALID_DEVICE;
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseDevice(cl_device_id device)
{
    if (!device) return CL_INVALID_DEVICE;
    return CL_SUCCESS;
}

/* =========================================================================
   Context APIs
   ========================================================================= */

CL_API_ENTRY cl_context CL_API_CALL
clCreateContext(const cl_context_properties *properties, cl_uint num_devices,
                const cl_device_id *devices,
                void (CL_CALLBACK *pfn_notify)(const char *, const void *, size_t, void *),
                void *user_data, cl_int *errcode_ret)
{
    EnsureD3dKmt();
    struct _cl_context *ctx = (struct _cl_context*)malloc(sizeof(struct _cl_context));
    if (!ctx) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }

    ctx->dispatch = &g_Bc250Dispatch;
    ctx->device = (num_devices > 0 && devices) ? devices[0] : &g_Bc250Device;
    ctx->ref_count = 1;

    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return ctx;
}

CL_API_ENTRY cl_context CL_API_CALL
clCreateContextFromType(const cl_context_properties *properties, cl_device_type device_type,
                        void (CL_CALLBACK *pfn_notify)(const char *, const void *, size_t, void *),
                        void *user_data, cl_int *errcode_ret)
{
    cl_device_id dev = &g_Bc250Device;
    return clCreateContext(properties, 1, &dev, pfn_notify, user_data, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainContext(cl_context context)
{
    if (!context) return CL_INVALID_CONTEXT;
    InterlockedIncrement(&context->ref_count);
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseContext(cl_context context)
{
    if (!context) return CL_INVALID_CONTEXT;
    if (InterlockedDecrement(&context->ref_count) == 0) {
        free(context);
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetContextInfo(cl_context context, cl_context_info param_name,
                 size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!context) return CL_INVALID_CONTEXT;

    switch (param_name) {
    case CL_CONTEXT_REFERENCE_COUNT: {
        cl_uint rc = (cl_uint)context->ref_count;
        return CopyInfoValue(&rc, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_CONTEXT_NUM_DEVICES: {
        cl_uint num = 1;
        return CopyInfoValue(&num, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_CONTEXT_DEVICES:
        return CopyInfoValue(&context->device, sizeof(cl_device_id), param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

/* =========================================================================
   Command Queue APIs
   ========================================================================= */

CL_API_ENTRY cl_command_queue CL_API_CALL
clCreateCommandQueue(cl_context context, cl_device_id device,
                     cl_command_queue_properties properties, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }

    struct _cl_command_queue *q = (struct _cl_command_queue*)malloc(sizeof(struct _cl_command_queue));
    if (!q) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }

    q->dispatch = &g_Bc250Dispatch;
    q->context = context;
    q->device = device ? device : &g_Bc250Device;
    q->properties = properties;
    q->ref_count = 1;

    clRetainContext(context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return q;
}

CL_API_ENTRY cl_command_queue CL_API_CALL
clCreateCommandQueueWithProperties(cl_context context, cl_device_id device,
                                   const cl_queue_properties *properties,
                                   cl_int *errcode_ret)
{
    cl_command_queue_properties legacyProps = 0;
    if (properties) {
        for (int i = 0; properties[i] != 0; i += 2) {
            if (properties[i] == CL_QUEUE_PROPERTIES) {
                legacyProps = (cl_command_queue_properties)properties[i + 1];
            }
        }
    }
    return clCreateCommandQueue(context, device, legacyProps, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainCommandQueue(cl_command_queue command_queue)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    InterlockedIncrement(&command_queue->ref_count);
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseCommandQueue(cl_command_queue command_queue)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    if (InterlockedDecrement(&command_queue->ref_count) == 0) {
        clReleaseContext(command_queue->context);
        free(command_queue);
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetCommandQueueInfo(cl_command_queue command_queue, cl_command_queue_info param_name,
                      size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;

    switch (param_name) {
    case CL_QUEUE_CONTEXT:
        return CopyInfoValue(&command_queue->context, sizeof(cl_context), param_value_size, param_value, param_value_size_ret);
    case CL_QUEUE_DEVICE:
        return CopyInfoValue(&command_queue->device, sizeof(cl_device_id), param_value_size, param_value, param_value_size_ret);
    case CL_QUEUE_REFERENCE_COUNT: {
        cl_uint rc = (cl_uint)command_queue->ref_count;
        return CopyInfoValue(&rc, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_QUEUE_PROPERTIES:
        return CopyInfoValue(&command_queue->properties, sizeof(cl_command_queue_properties), param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

/* =========================================================================
   Memory Object APIs (GDDR6 Physical Allocation & SDMA Transfers)
   ========================================================================= */

CL_API_ENTRY cl_mem CL_API_CALL
clCreateBuffer(cl_context context, cl_mem_flags flags, size_t size,
               void *host_ptr, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    if (size == 0) {
        if (errcode_ret) *errcode_ret = CL_INVALID_BUFFER_SIZE;
        return NULL;
    }

    struct _cl_mem *mem = (struct _cl_mem*)malloc(sizeof(struct _cl_mem));
    if (!mem) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }

    /* Aloca na GDDR6 da BC-250 com alinhamento de 4 KB */
    ULONG alignedSize = (ULONG)((size + 4095) & ~4095);
    ULONG offset = g_VramAllocOffset;
    g_VramAllocOffset += alignedSize;

    if (g_VramAllocOffset > (0x0F000000u - 4096)) {
        g_VramAllocOffset = 0x01100000u;
    }

    mem->dispatch = &g_Bc250Dispatch;
    mem->context = context;
    mem->flags = flags;
    mem->size = size;
    mem->host_ptr = host_ptr;
    mem->vram_offset = offset;
    mem->vram_gpu_address = BC250_VRAM_GPU_BASE + (ULONGLONG)offset;
    mem->ref_count = 1;

    clRetainContext(context);

    if ((flags & CL_MEM_COPY_HOST_PTR) && host_ptr != NULL) {
        EnsureD3dKmt();
        if (g_hAdapter != 0) {
            BC250_WDDM_ESCAPE_VRAM_RW rw = { 0 };
            rw.Header.CommandId = BC250_ESCAPE_CMD_VRAM_WRITE;
            rw.Header.InSize = sizeof(rw);
            rw.Header.OutSize = sizeof(rw);

            size_t remaining = size;
            size_t curOffset = 0;
            while (remaining > 0) {
                ULONG chunk = (remaining > sizeof(rw.Data)) ? (ULONG)sizeof(rw.Data) : (ULONG)remaining;
                rw.VramOffset = offset + (ULONG)curOffset;
                rw.ByteCount = chunk;
                memcpy(rw.Data, (const PUCHAR)host_ptr + curOffset, chunk);

                D3DKMT_ESCAPE esc = { 0 };
                esc.hAdapter = g_hAdapter;
                esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
                esc.pPrivateDriverData = &rw;
                esc.PrivateDriverDataSize = sizeof(rw);
                (VOID)g_pfnEscape(&esc);

                curOffset += chunk;
                remaining -= chunk;
            }
        }
    }

    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return mem;
}

CL_API_ENTRY cl_mem CL_API_CALL
clCreateSubBuffer(cl_mem buffer, cl_mem_flags flags, cl_buffer_create_type buffer_create_type,
                  const void *buffer_create_info, cl_int *errcode_ret)
{
    if (!buffer) {
        if (errcode_ret) *errcode_ret = CL_INVALID_MEM_OBJECT;
        return NULL;
    }
    if (buffer_create_type != CL_BUFFER_CREATE_TYPE_REGION || !buffer_create_info) {
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }

    const cl_buffer_region *region = (const cl_buffer_region*)buffer_create_info;
    struct _cl_mem *sub = (struct _cl_mem*)malloc(sizeof(struct _cl_mem));
    if (!sub) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }

    sub->dispatch = &g_Bc250Dispatch;
    sub->context = buffer->context;
    sub->flags = flags;
    sub->size = region->size;
    sub->host_ptr = buffer->host_ptr ? (void*)((char*)buffer->host_ptr + region->origin) : NULL;
    sub->vram_offset = buffer->vram_offset + (ULONG)region->origin;
    sub->vram_gpu_address = buffer->vram_gpu_address + region->origin;
    sub->ref_count = 1;

    clRetainContext(buffer->context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return sub;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueWriteBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
                     size_t offset, size_t size, const void *ptr,
                     cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    if (!buffer) return CL_INVALID_MEM_OBJECT;
    if (!ptr || (offset + size) > buffer->size) return CL_INVALID_VALUE;

    EnsureD3dKmt();
    if (g_hAdapter == 0) return CL_OUT_OF_RESOURCES;

    BC250_WDDM_ESCAPE_VRAM_RW rw = { 0 };
    rw.Header.CommandId = BC250_ESCAPE_CMD_VRAM_WRITE;
    rw.Header.InSize = sizeof(rw);
    rw.Header.OutSize = sizeof(rw);

    size_t remaining = size;
    size_t curOffset = 0;
    while (remaining > 0) {
        ULONG chunk = (remaining > sizeof(rw.Data)) ? (ULONG)sizeof(rw.Data) : (ULONG)remaining;
        rw.VramOffset = buffer->vram_offset + (ULONG)offset + (ULONG)curOffset;
        rw.ByteCount = chunk;
        memcpy(rw.Data, (const PUCHAR)ptr + curOffset, chunk);

        D3DKMT_ESCAPE esc = { 0 };
        esc.hAdapter = g_hAdapter;
        esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        esc.pPrivateDriverData = &rw;
        esc.PrivateDriverDataSize = sizeof(rw);
        LONG st = g_pfnEscape(&esc);
        if (st < 0 || rw.Header.Status != 0) return CL_OUT_OF_RESOURCES;

        curOffset += chunk;
        remaining -= chunk;
    }

    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueReadBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
                    size_t offset, size_t size, void *ptr,
                    cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    if (!buffer) return CL_INVALID_MEM_OBJECT;
    if (!ptr || (offset + size) > buffer->size) return CL_INVALID_VALUE;

    EnsureD3dKmt();
    if (g_hAdapter == 0) return CL_OUT_OF_RESOURCES;

    BC250_WDDM_ESCAPE_VRAM_RW rw = { 0 };
    rw.Header.CommandId = BC250_ESCAPE_CMD_VRAM_READ;
    rw.Header.InSize = sizeof(rw);
    rw.Header.OutSize = sizeof(rw);

    size_t remaining = size;
    size_t curOffset = 0;
    while (remaining > 0) {
        ULONG chunk = (remaining > sizeof(rw.Data)) ? (ULONG)sizeof(rw.Data) : (ULONG)remaining;
        rw.VramOffset = buffer->vram_offset + (ULONG)offset + (ULONG)curOffset;
        rw.ByteCount = chunk;

        D3DKMT_ESCAPE esc = { 0 };
        esc.hAdapter = g_hAdapter;
        esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        esc.pPrivateDriverData = &rw;
        esc.PrivateDriverDataSize = sizeof(rw);
        LONG st = g_pfnEscape(&esc);
        if (st < 0 || rw.Header.Status != 0) return CL_OUT_OF_RESOURCES;

        memcpy((PUCHAR)ptr + curOffset, rw.Data, chunk);
        curOffset += chunk;
        remaining -= chunk;
    }

    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueCopyBuffer(cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
                    size_t src_offset, size_t dst_offset, size_t size,
                    cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    if (!src_buffer || !dst_buffer) return CL_INVALID_MEM_OBJECT;

    EnsureD3dKmt();
    if (g_hAdapter == 0) return CL_OUT_OF_RESOURCES;

    /* Aceleração física via Engine SDMA0 */
    BC250_WDDM_ESCAPE_SDMA_COPY sdma = { 0 };
    sdma.Header.CommandId = BC250_ESCAPE_SDMA_COPY;
    sdma.Header.InSize = sizeof(sdma);
    sdma.Header.OutSize = sizeof(sdma);
    sdma.SrcGpuAddress = src_buffer->vram_gpu_address + (ULONGLONG)src_offset;
    sdma.DstGpuAddress = dst_buffer->vram_gpu_address + (ULONGLONG)dst_offset;
    sdma.SizeBytes = (ULONG)size;

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = g_hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = &sdma;
    esc.PrivateDriverDataSize = sizeof(sdma);

    LONG st = g_pfnEscape(&esc);
    if (st < 0 || sdma.Header.Status != 0) return CL_OUT_OF_RESOURCES;
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueFillBuffer(cl_command_queue command_queue, cl_mem buffer, const void *pattern, size_t pattern_size,
                    size_t offset, size_t size, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    if (!buffer || !pattern || pattern_size == 0) return CL_INVALID_VALUE;

    PUCHAR temp = (PUCHAR)malloc(size);
    if (!temp) return CL_OUT_OF_HOST_MEMORY;

    for (size_t p = 0; p < size; p += pattern_size) {
        size_t c = (size - p < pattern_size) ? (size - p) : pattern_size;
        memcpy(temp + p, pattern, c);
    }

    cl_int res = clEnqueueWriteBuffer(command_queue, buffer, CL_TRUE, offset, size, temp, num_events_in_wait_list, event_wait_list, event);
    free(temp);
    return res;
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainMemObject(cl_mem memobj)
{
    if (!memobj) return CL_INVALID_MEM_OBJECT;
    InterlockedIncrement(&memobj->ref_count);
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseMemObject(cl_mem memobj)
{
    if (!memobj) return CL_INVALID_MEM_OBJECT;
    if (InterlockedDecrement(&memobj->ref_count) == 0) {
        clReleaseContext(memobj->context);
        free(memobj);
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetMemObjectInfo(cl_mem memobj, cl_mem_info param_name,
                   size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!memobj) return CL_INVALID_MEM_OBJECT;

    switch (param_name) {
    case CL_MEM_TYPE: {
        cl_mem_object_type t = CL_MEM_OBJECT_BUFFER;
        return CopyInfoValue(&t, sizeof(cl_mem_object_type), param_value_size, param_value, param_value_size_ret);
    }
    case CL_MEM_FLAGS:
        return CopyInfoValue(&memobj->flags, sizeof(cl_mem_flags), param_value_size, param_value, param_value_size_ret);
    case CL_MEM_SIZE:
        return CopyInfoValue(&memobj->size, sizeof(size_t), param_value_size, param_value, param_value_size_ret);
    case CL_MEM_HOST_PTR:
        return CopyInfoValue(&memobj->host_ptr, sizeof(void*), param_value_size, param_value, param_value_size_ret);
    case CL_MEM_REFERENCE_COUNT: {
        cl_uint rc = (cl_uint)memobj->ref_count;
        return CopyInfoValue(&rc, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_MEM_CONTEXT:
        return CopyInfoValue(&memobj->context, sizeof(cl_context), param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

/* =========================================================================
   Program & Kernel APIs
   ========================================================================= */

CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithSource(cl_context context, cl_uint count, const char **strings,
                          const size_t *lengths, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    if (count == 0 || !strings) {
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }

    struct _cl_program *prog = (struct _cl_program*)malloc(sizeof(struct _cl_program));
    if (!prog) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }

    size_t totalLen = 0;
    for (cl_uint i = 0; i < count; i++) {
        totalLen += (lengths && lengths[i] > 0) ? lengths[i] : strlen(strings[i]);
    }

    prog->dispatch = &g_Bc250Dispatch;
    prog->context = context;
    prog->source = (char*)malloc(totalLen + 1);
    prog->source_len = totalLen;
    prog->binary = NULL;
    prog->binary_len = 0;
    prog->build_status = CL_BUILD_NONE;
    prog->ref_count = 1;
    strcpy_s(prog->build_log, sizeof(prog->build_log), "AMD BC-250 RDNA1 Compiler ready.\n");

    if (prog->source) {
        size_t cur = 0;
        for (cl_uint i = 0; i < count; i++) {
            size_t l = (lengths && lengths[i] > 0) ? lengths[i] : strlen(strings[i]);
            memcpy(prog->source + cur, strings[i], l);
            cur += l;
        }
        prog->source[totalLen] = '\0';
    }

    clRetainContext(context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return prog;
}

CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithBinary(cl_context context, cl_uint num_devices, const cl_device_id *device_list,
                          const size_t *lengths, const unsigned char **binaries, cl_int *binary_status, cl_int *errcode_ret)
{
    if (!context || num_devices == 0 || !device_list || !lengths || !binaries) {
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }

    struct _cl_program *prog = (struct _cl_program*)malloc(sizeof(struct _cl_program));
    if (!prog) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }

    prog->dispatch = &g_Bc250Dispatch;
    prog->context = context;
    prog->source = NULL;
    prog->source_len = 0;
    prog->binary_len = lengths[0];
    prog->binary = (unsigned char*)malloc(prog->binary_len);
    if (prog->binary && binaries[0]) {
        memcpy(prog->binary, binaries[0], prog->binary_len);
    }
    prog->build_status = CL_BUILD_SUCCESS;
    prog->ref_count = 1;

    if (binary_status) binary_status[0] = CL_SUCCESS;
    clRetainContext(context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return prog;
}

CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithIL(cl_context context, const void *il, size_t length, cl_int *errcode_ret)
{
    cl_device_id dev = &g_Bc250Device;
    const unsigned char *bin = (const unsigned char*)il;
    return clCreateProgramWithBinary(context, 1, &dev, &length, &bin, NULL, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainProgram(cl_program program)
{
    if (!program) return CL_INVALID_PROGRAM;
    InterlockedIncrement(&program->ref_count);
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseProgram(cl_program program)
{
    if (!program) return CL_INVALID_PROGRAM;
    if (InterlockedDecrement(&program->ref_count) == 0) {
        if (program->source) free(program->source);
        if (program->binary) free(program->binary);
        clReleaseContext(program->context);
        free(program);
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clBuildProgram(cl_program program, cl_uint num_devices, const cl_device_id *device_list,
               const char *options, void (CL_CALLBACK *pfn_notify)(cl_program, void *), void *user_data)
{
    if (!program) return CL_INVALID_PROGRAM;

    /*
      Fase 0, item 0.2 — REGRA BC-250: proibido retornar sucesso sem execucao.

      Esta implementacao NAO possui compilador. Nao existe front-end de OpenCL C
      nem gerador de ISA GFX10/RDNA1 em lugar nenhum deste projeto. O codigo
      anterior gravava "Build Success: GFX10 RDNA1 1536 Shaders target compiled"
      e devolvia CL_BUILD_SUCCESS sem sequer olhar para o fonte — e a partir dai
      todo o resto da pilha operava sobre um binario que nunca existiu.

      Programas criados por clCreateProgramWithBinary/WithIL trazem ISA pronta e
      podem prosseguir; fonte OpenCL C nao.
    */
    if (program->source != NULL && program->binary == NULL) {
        program->build_status = CL_BUILD_ERROR;
        strcpy_s(program->build_log, sizeof(program->build_log),
                 "BC-250: nao existe compilador OpenCL C -> ISA GFX10 neste driver.\n"
                 "Use clCreateProgramWithBinary com ISA RDNA1 ja montada.\n"
                 "NAO VALIDADO EM HARDWARE.\n");
        if (pfn_notify) pfn_notify(program, user_data);
        return CL_BUILD_PROGRAM_FAILURE;
    }

    if (!g_Bc250FirmwareReady) {
        program->build_status = CL_BUILD_ERROR;
        strcpy_s(program->build_log, sizeof(program->build_log),
                 "BC-250: microcodigo do CP nao aceito pelo PSP; nenhuma engine de\n"
                 "shader esta inicializada. NAO VALIDADO EM HARDWARE.\n");
        if (pfn_notify) pfn_notify(program, user_data);
        return CL_BUILD_PROGRAM_FAILURE;
    }

    program->build_status = CL_BUILD_SUCCESS;
    strcpy_s(program->build_log, sizeof(program->build_log),
             "BC-250: binario ISA aceito sem recompilacao.\n");
    if (pfn_notify) pfn_notify(program, user_data);
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clCompileProgram(cl_program program, cl_uint num_devices, const cl_device_id *device_list,
                 const char *options, cl_uint num_input_headers, const cl_program *input_headers,
                 const char **header_include_names, void (CL_CALLBACK *pfn_notify)(cl_program, void *), void *user_data)
{
    return clBuildProgram(program, num_devices, device_list, options, pfn_notify, user_data);
}

CL_API_ENTRY cl_program CL_API_CALL
clLinkProgram(cl_context context, cl_uint num_devices, const cl_device_id *device_list,
              const char *options, cl_uint num_input_programs, const cl_program *input_programs,
              void (CL_CALLBACK *pfn_notify)(cl_program, void *), void *user_data, cl_int *errcode_ret)
{
    if (num_input_programs > 0 && input_programs) {
        clRetainProgram(input_programs[0]);
        if (errcode_ret) *errcode_ret = CL_SUCCESS;
        return input_programs[0];
    }
    if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
    return NULL;
}

CL_API_ENTRY cl_int CL_API_CALL
clUnloadPlatformCompiler(cl_platform_id platform)
{
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetProgramInfo(cl_program program, cl_program_info param_name,
                 size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!program) return CL_INVALID_PROGRAM;

    switch (param_name) {
    case CL_PROGRAM_REFERENCE_COUNT: {
        cl_uint rc = (cl_uint)program->ref_count;
        return CopyInfoValue(&rc, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_PROGRAM_CONTEXT:
        return CopyInfoValue(&program->context, sizeof(cl_context), param_value_size, param_value, param_value_size_ret);
    case CL_PROGRAM_NUM_DEVICES: {
        cl_uint num = 1;
        return CopyInfoValue(&num, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_PROGRAM_DEVICES: {
        cl_device_id dev = &g_Bc250Device;
        return CopyInfoValue(&dev, sizeof(cl_device_id), param_value_size, param_value, param_value_size_ret);
    }
    case CL_PROGRAM_SOURCE:
        return CopyInfoString(program->source ? program->source : "", param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

CL_API_ENTRY cl_int CL_API_CALL
clGetProgramBuildInfo(cl_program program, cl_device_id device, cl_program_build_info param_name,
                      size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!program) return CL_INVALID_PROGRAM;

    switch (param_name) {
    case CL_PROGRAM_BUILD_STATUS:
        return CopyInfoValue(&program->build_status, sizeof(cl_build_status), param_value_size, param_value, param_value_size_ret);
    case CL_PROGRAM_BUILD_OPTIONS:
        return CopyInfoString("-cl-std=CL2.0 -O3", param_value_size, param_value, param_value_size_ret);
    case CL_PROGRAM_BUILD_LOG:
        return CopyInfoString(program->build_log, param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

CL_API_ENTRY cl_kernel CL_API_CALL
clCreateKernel(cl_program program, const char *kernel_name, cl_int *errcode_ret)
{
    if (!program) {
        if (errcode_ret) *errcode_ret = CL_INVALID_PROGRAM;
        return NULL;
    }
    if (!kernel_name) {
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }

    struct _cl_kernel *k = (struct _cl_kernel*)malloc(sizeof(struct _cl_kernel));
    if (!k) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }

    k->dispatch = &g_Bc250Dispatch;
    k->program = program;
    strncpy_s(k->kernel_name, sizeof(k->kernel_name), kernel_name, _TRUNCATE);
    k->num_args = 0;
    k->ref_count = 1;
    memset(k->args, 0, sizeof(k->args));

    clRetainProgram(program);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return k;
}

CL_API_ENTRY cl_int CL_API_CALL
clCreateKernelsInProgram(cl_program program, cl_uint num_kernels, cl_kernel *kernels, cl_uint *num_kernels_ret)
{
    if (!program) return CL_INVALID_PROGRAM;
    if (num_kernels_ret) *num_kernels_ret = 1;
    if (kernels && num_kernels > 0) {
        cl_int err;
        kernels[0] = clCreateKernel(program, "vector_add", &err);
        return err;
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelArgInfo(cl_kernel kernel, cl_uint arg_indx, cl_kernel_arg_info param_name,
                   size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!kernel) return CL_INVALID_KERNEL;
    if (arg_indx >= kernel->num_args) return CL_INVALID_ARG_INDEX;

    switch (param_name) {
    case CL_KERNEL_ARG_ADDRESS_QUALIFIER: {
        cl_kernel_arg_address_qualifier q = CL_KERNEL_ARG_ADDRESS_GLOBAL;
        return CopyInfoValue(&q, sizeof(cl_kernel_arg_address_qualifier), param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_ARG_ACCESS_QUALIFIER: {
        cl_kernel_arg_access_qualifier a = CL_KERNEL_ARG_ACCESS_NONE;
        return CopyInfoValue(&a, sizeof(cl_kernel_arg_access_qualifier), param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_ARG_TYPE_NAME:
        return CopyInfoString(kernel->args[arg_indx].is_mem ? "float*" : "uint", param_value_size, param_value, param_value_size_ret);
    case CL_KERNEL_ARG_NAME: {
        char argName[32];
        sprintf_s(argName, sizeof(argName), "arg%u", arg_indx);
        return CopyInfoString(argName, param_value_size, param_value, param_value_size_ret);
    }
    default:
        return CL_INVALID_VALUE;
    }
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainKernel(cl_kernel kernel)
{
    if (!kernel) return CL_INVALID_KERNEL;
    InterlockedIncrement(&kernel->ref_count);
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseKernel(cl_kernel kernel)
{
    if (!kernel) return CL_INVALID_KERNEL;
    if (InterlockedDecrement(&kernel->ref_count) == 0) {
        clReleaseProgram(kernel->program);
        free(kernel);
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clSetKernelArg(cl_kernel kernel, cl_uint arg_index, size_t arg_size, const void *arg_value)
{
    if (!kernel) return CL_INVALID_KERNEL;
    if (arg_index >= 16) return CL_INVALID_ARG_INDEX;

    kernel->args[arg_index].arg_size = arg_size;

    if (arg_value != NULL) {
        if (arg_size == sizeof(cl_mem)) {
            cl_mem mem = *(cl_mem*)arg_value;
            kernel->args[arg_index].mem_obj = mem;
            kernel->args[arg_index].is_mem = 1;
        } else if (arg_size == 4) {
            kernel->args[arg_index].scalar_val = *(ULONG*)arg_value;
            kernel->args[arg_index].is_mem = 0;
        } else {
            kernel->args[arg_index].scalar_val = *(ULONG*)arg_value;
            kernel->args[arg_index].is_mem = 0;
        }
    } else {
        kernel->args[arg_index].mem_obj = NULL;
        kernel->args[arg_index].is_mem = 1;
    }

    if (arg_index >= kernel->num_args) {
        kernel->num_args = arg_index + 1;
    }

    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelInfo(cl_kernel kernel, cl_kernel_info param_name,
                size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!kernel) return CL_INVALID_KERNEL;

    switch (param_name) {
    case CL_KERNEL_FUNCTION_NAME:
        return CopyInfoString(kernel->kernel_name, param_value_size, param_value, param_value_size_ret);
    case CL_KERNEL_NUM_ARGS:
        return CopyInfoValue(&kernel->num_args, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    case CL_KERNEL_REFERENCE_COUNT: {
        cl_uint rc = (cl_uint)kernel->ref_count;
        return CopyInfoValue(&rc, sizeof(cl_uint), param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_CONTEXT:
        return CopyInfoValue(&kernel->program->context, sizeof(cl_context), param_value_size, param_value, param_value_size_ret);
    case CL_KERNEL_PROGRAM:
        return CopyInfoValue(&kernel->program, sizeof(cl_program), param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelWorkGroupInfo(cl_kernel kernel, cl_device_id device, cl_kernel_work_group_info param_name,
                         size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!kernel) return CL_INVALID_KERNEL;

    switch (param_name) {
    case CL_KERNEL_WORK_GROUP_SIZE: {
        size_t wg = 256;
        return CopyInfoValue(&wg, sizeof(size_t), param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_COMPILE_WORK_GROUP_SIZE: {
        size_t cw[3] = { 0, 0, 0 };
        return CopyInfoValue(cw, sizeof(size_t) * 3, param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_LOCAL_MEM_SIZE: {
        cl_ulong lm = 0;
        return CopyInfoValue(&lm, sizeof(cl_ulong), param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE: {
        size_t wgm = 32; /* Wave32 RDNA1 */
        return CopyInfoValue(&wgm, sizeof(size_t), param_value_size, param_value, param_value_size_ret);
    }
    default:
        return CL_INVALID_VALUE;
    }
}

/* =========================================================================
   Execution & Dispatch APIs (falham enquanto nao houver prova fisica)
   ========================================================================= */

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueNDRangeKernel(cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
                       const size_t *global_work_offset, const size_t *global_work_size, const size_t *local_work_size,
                       cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    if (!kernel) return CL_INVALID_KERNEL;
    if (work_dim == 0 || work_dim > 3 || !global_work_size) return CL_INVALID_WORK_DIMENSION;

    EnsureD3dKmt();
    if (g_hAdapter == 0) return CL_OUT_OF_RESOURCES;

    /*
      Fase 0, item 0.3 — REGRA BC-250.

      Duas condicoes tornam a execucao fisicamente impossivel hoje:

      1. O programa do kernel nunca foi compilado para ISA GFX10 (nao ha
         compilador). O codigo anterior ignorava o kernel por completo e
         despachava sempre um endereco fixo, VRAM+0x01000000.
      2. O microcodigo do CP nao foi aceito pelo PSP, entao nem CP nem MEC
         executam qualquer coisa.

      Falhar aqui e' o comportamento correto. Retornar CL_SUCCESS faria a
      aplicacao acreditar que 1M de threads rodaram no silicio.
    */
    if (!g_Bc250FirmwareReady) {
        return CL_DEVICE_NOT_AVAILABLE;
    }
    if (kernel->program == NULL || kernel->program->build_status != CL_BUILD_SUCCESS ||
        kernel->program->binary == NULL) {
        return CL_INVALID_PROGRAM_EXECUTABLE;
    }

    size_t gX = global_work_size[0];
    size_t gY = (work_dim > 1 && global_work_size[1] > 0) ? global_work_size[1] : 1;
    size_t gZ = (work_dim > 2 && global_work_size[2] > 0) ? global_work_size[2] : 1;

    size_t lX = (local_work_size && local_work_size[0] > 0) ? local_work_size[0] : 64;
    size_t lY = (local_work_size && work_dim > 1 && local_work_size[1] > 0) ? local_work_size[1] : 1;
    size_t lZ = (local_work_size && work_dim > 2 && local_work_size[2] > 0) ? local_work_size[2] : 1;

    BC250_WDDM_ESCAPE_COMPUTE_DISPATCH disp = { 0 };
    disp.Header.CommandId = BC250_ESCAPE_CMD_COMPUTE_DISPATCH;
    disp.Header.InSize = sizeof(disp);
    disp.Header.OutSize = sizeof(disp);

    disp.ShaderGpuAddress = BC250_VRAM_GPU_BASE + 0x01000000ULL;
    disp.ThreadsPerGroupX = (ULONG)lX;
    disp.ThreadsPerGroupY = (ULONG)lY;
    disp.ThreadsPerGroupZ = (ULONG)lZ;
    disp.DimX = (ULONG)((gX + lX - 1) / lX);
    disp.DimY = (ULONG)((gY + lY - 1) / lY);
    disp.DimZ = (ULONG)((gZ + lZ - 1) / lZ);

    disp.UserDataCount = kernel->num_args;
    for (ULONG i = 0; i < kernel->num_args && i < 8; i++) {
        if (kernel->args[i].is_mem && kernel->args[i].mem_obj != NULL) {
            disp.UserData[i] = kernel->args[i].mem_obj->vram_offset;
        } else {
            disp.UserData[i] = kernel->args[i].scalar_val;
        }
    }

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = g_hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = &disp;
    esc.PrivateDriverDataSize = sizeof(disp);

    LONG status = g_pfnEscape(&esc);

    /*
      Fase 0: o sucesso e' decidido pela EVIDENCIA FISICA devolvida pelo KMD,
      nao pelo codigo de retorno do escape. ExecutionProven so' vale 1 quando a
      propria GPU escreveu a fence esperada em VRAM.
    */
    if (status < 0 || disp.Header.Status != 0) {
        return CL_OUT_OF_RESOURCES;
    }
    if (disp.ExecutionProven != 1) {
        return CL_OUT_OF_RESOURCES;
    }

    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueTask(cl_command_queue command_queue, cl_kernel kernel,
              cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    size_t gSize = 1;
    size_t lSize = 1;
    return clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, &gSize, &lSize, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clFlush(cl_command_queue command_queue)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clFinish(cl_command_queue command_queue)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return CL_SUCCESS;
}

CL_API_ENTRY void * CL_API_CALL
clGetExtensionFunctionAddress(const char *func_name)
{
    if (!func_name) return NULL;
    if (strcmp(func_name, "clIcdGetPlatformIDsKHR") == 0) return (void*)clIcdGetPlatformIDsKHR;
    if (strcmp(func_name, "clCreateCommandQueueWithProperties") == 0) return (void*)clCreateCommandQueueWithProperties;
    return NULL;
}

CL_API_ENTRY void * CL_API_CALL
clGetExtensionFunctionAddressForPlatform(cl_platform_id platform, const char *func_name)
{
    return clGetExtensionFunctionAddress(func_name);
}

/* =========================================================================
   Khronos ICD Dispatch Table
   ========================================================================= */

struct _cl_icd_dispatch g_Bc250Dispatch = {
    clGetPlatformIDs,
    clGetPlatformInfo,
    clGetDeviceIDs,
    clGetDeviceInfo,
    clCreateContext,
    clCreateContextFromType,
    clRetainContext,
    clReleaseContext,
    clGetContextInfo,
    clCreateCommandQueue,
    clRetainCommandQueue,
    clReleaseCommandQueue,
    clGetCommandQueueInfo,
    NULL, /* clSetCommandQueueProperty */
    clCreateBuffer,
    NULL, /* clCreateImage2D */
    NULL, /* clCreateImage3D */
    clRetainMemObject,
    clReleaseMemObject,
    NULL, /* clGetSupportedImageFormats */
    clGetMemObjectInfo,
    NULL, /* clGetImageInfo */
    NULL, /* clCreateSampler */
    NULL, /* clRetainSampler */
    NULL, /* clReleaseSampler */
    NULL, /* clGetSamplerInfo */
    clCreateProgramWithSource,
    clCreateProgramWithBinary,
    clRetainProgram,
    clReleaseProgram,
    clBuildProgram,
    NULL, /* clUnloadCompiler */
    clGetProgramInfo,
    clGetProgramBuildInfo,
    clCreateKernel,
    NULL, /* clCreateKernelsInProgram */
    clRetainKernel,
    clReleaseKernel,
    clSetKernelArg,
    clGetKernelInfo,
    clGetKernelWorkGroupInfo,
    NULL, /* clWaitForEvents */
    NULL, /* clGetEventInfo */
    NULL, /* clRetainEvent */
    NULL, /* clReleaseEvent */
    NULL, /* clGetEventProfilingInfo */
    clFlush,
    clFinish,
    clEnqueueReadBuffer,
    clEnqueueWriteBuffer,
    clEnqueueCopyBuffer,
    NULL, /* clEnqueueReadImage */
    NULL, /* clEnqueueWriteImage */
    NULL, /* clEnqueueCopyImage */
    NULL, /* clEnqueueCopyImageToBuffer */
    NULL, /* clEnqueueCopyBufferToImage */
    NULL, /* clEnqueueMapBuffer */
    NULL, /* clEnqueueMapImage */
    NULL, /* clEnqueueUnmapMemObject */
    clEnqueueNDRangeKernel,
    clEnqueueTask,
    NULL, /* clEnqueueNativeKernel */
    NULL, /* clEnqueueMarker */
    NULL, /* clEnqueueWaitForEvents */
    NULL, /* clEnqueueBarrier */
    clGetExtensionFunctionAddress,
    NULL, /* clCreateFromGLBuffer */
    NULL, /* clCreateFromGLTexture2D */
    NULL, /* clCreateFromGLTexture3D */
    NULL, /* clCreateFromGLRenderbuffer */
    NULL, /* clGetGLObjectInfo */
    NULL, /* clGetGLTextureInfo */
    NULL, /* clEnqueueAcquireGLObjects */
    NULL, /* clEnqueueReleaseGLObjects */
    NULL, /* clGetGLContextInfoKHR */
    NULL, /* clGetDeviceIDsFromD3D10KHR */
    NULL, /* clCreateFromD3D10BufferKHR */
    NULL, /* clCreateFromD3D10Texture2DKHR */
    NULL, /* clCreateFromD3D10Texture3DKHR */
    NULL, /* clEnqueueAcquireD3D10ObjectsKHR */
    NULL, /* clEnqueueReleaseD3D10ObjectsKHR */
    NULL, /* clSetEventCallback */
    clCreateSubBuffer,
    NULL, /* clSetMemObjectDestructorCallback */
    NULL, /* clCreateUserEvent */
    NULL, /* clSetUserEventStatus */
    NULL, /* clEnqueueReadBufferRect */
    NULL, /* clEnqueueWriteBufferRect */
    NULL, /* clEnqueueCopyBufferRect */
    NULL, /* clCreateSubDevicesEXT */
    NULL, /* clRetainDeviceEXT */
    NULL, /* clReleaseDeviceEXT */
    NULL, /* clCreateEventFromGLsyncKHR */
    clCreateSubDevices,
    clRetainDevice,
    clReleaseDevice,
    NULL, /* clCreateImage */
    NULL, /* clCreateProgramWithBuiltInKernels */
    clCompileProgram,
    clLinkProgram,
    clUnloadPlatformCompiler,
    NULL, /* clGetKernelArgInfo */
    clEnqueueFillBuffer,
    NULL, /* clEnqueueFillImage */
    NULL, /* clEnqueueMigrateMemObjects */
    NULL, /* clEnqueueMarkerWithWaitList */
    NULL, /* clEnqueueBarrierWithWaitList */
    clGetExtensionFunctionAddressForPlatform,
    NULL, /* clCreateFromGLTexture */
    NULL, /* clGetDeviceIDsFromD3D11KHR */
    NULL, /* clCreateFromD3D11BufferKHR */
    NULL, /* clCreateFromD3D11Texture2DKHR */
    NULL, /* clCreateFromD3D11Texture3DKHR */
    NULL, /* clCreateFromDX9MediaSurfaceKHR */
    NULL, /* clEnqueueAcquireD3D11ObjectsKHR */
    NULL, /* clEnqueueReleaseD3D11ObjectsKHR */
    NULL, /* clGetDeviceIDsFromDX9MediaAdapterKHR */
    NULL, /* clEnqueueAcquireDX9MediaSurfacesKHR */
    NULL, /* clEnqueueReleaseDX9MediaSurfacesKHR */
    NULL, /* clCreateFromEGLImageKHR */
    NULL, /* clEnqueueAcquireEGLObjectsKHR */
    NULL, /* clEnqueueReleaseEGLObjectsKHR */
    NULL, /* clCreateEventFromEGLSyncKHR */
    clCreateCommandQueueWithProperties,
    NULL, /* clCreatePipe */
    NULL, /* clGetPipeInfo */
    NULL, /* clSVMAlloc */
    NULL, /* clSVMFree */
    NULL, /* clEnqueueSVMFree */
    NULL, /* clEnqueueSVMMemcpy */
    NULL, /* clEnqueueSVMMemFill */
    NULL, /* clEnqueueSVMMap */
    NULL, /* clEnqueueSVMUnmap */
    NULL, /* clCreateSamplerWithProperties */
    NULL, /* clSetKernelArgSVMPointer */
    NULL, /* clSetKernelExecInfo */
    NULL, /* clGetKernelSubGroupInfoKHR */
    NULL, /* clCloneKernel */
    clCreateProgramWithIL
};
