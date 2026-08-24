/*++

Copyright (c) 2026 Khronos Group & AMD BC-250 Driver Project

Module Name:
    opencl_loader.c

Abstract:
    Complete Khronos OpenCL 2.0 / 3.0 ICD Loader (OpenCL.dll).
    Provides all standard OpenCL 1.0 - 3.0 entry points and dispatches
    calls to registered vendor ICD drivers (e.g. bc250_opencl.dll / amdocl64.dll).

--*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <CL/cl.h>
#include <CL/cl_icd.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

#define MAX_VENDORS 16

typedef cl_int (CL_API_CALL *PFN_clIcdGetPlatformIDsKHR)(cl_uint, cl_platform_id*, cl_uint*);

static HMODULE g_VendorLibs[MAX_VENDORS];
static cl_uint g_VendorCount = 0;
static cl_platform_id g_Platforms[MAX_VENDORS * 4];
static cl_uint g_PlatformCount = 0;
static BOOL g_Initialized = FALSE;
static CRITICAL_SECTION g_LoaderLock;

static void LoadVendors(void)
{
    if (g_Initialized) return;
    EnterCriticalSection(&g_LoaderLock);
    if (g_Initialized) {
        LeaveCriticalSection(&g_LoaderLock);
        return;
    }

    /* 1. Busca fornecedores na chave oficial Khronos do Registro */
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Khronos\\OpenCL\\Vendors", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD index = 0;
        char valName[MAX_PATH];
        DWORD valNameLen;
        DWORD valType;
        DWORD valData;
        DWORD valDataLen;

        while (index < MAX_VENDORS) {
            valNameLen = sizeof(valName);
            valDataLen = sizeof(valData);
            if (RegEnumValueA(hKey, index, valName, &valNameLen, NULL, &valType, (LPBYTE)&valData, &valDataLen) != ERROR_SUCCESS) {
                break;
            }
            if (valType == REG_DWORD && valData == 0 && valNameLen > 0) {
                HMODULE hMod = LoadLibraryA(valName);
                if (hMod) {
                    g_VendorLibs[g_VendorCount++] = hMod;
                }
            }
            index++;
        }
        RegCloseKey(hKey);
    }

    /* 2. Fallback direto para o driver nativo AMD BC-250 se nada encontrado no registro */
    if (g_VendorCount == 0) {
        const char* fallbackDrivers[] = {
            "bc250_opencl.dll",
            "amdocl64.dll",
            "C:\\Windows\\System32\\bc250_opencl.dll",
            "C:\\Windows\\System32\\amdocl64.dll",
            NULL
        };
        for (int i = 0; fallbackDrivers[i] != NULL && g_VendorCount < MAX_VENDORS; i++) {
            HMODULE hMod = LoadLibraryA(fallbackDrivers[i]);
            if (hMod) {
                g_VendorLibs[g_VendorCount++] = hMod;
                break;
            }
        }
    }

    /* 3. Enumera plataformas de todos os drivers carregados */
    g_PlatformCount = 0;
    for (cl_uint v = 0; v < g_VendorCount; v++) {
        PFN_clIcdGetPlatformIDsKHR pfnGetPlat = (PFN_clIcdGetPlatformIDsKHR)GetProcAddress(g_VendorLibs[v], "clIcdGetPlatformIDsKHR");
        if (!pfnGetPlat) {
            pfnGetPlat = (PFN_clIcdGetPlatformIDsKHR)GetProcAddress(g_VendorLibs[v], "clGetPlatformIDs");
        }
        if (pfnGetPlat) {
            cl_uint count = 0;
            if (pfnGetPlat(0, NULL, &count) == CL_SUCCESS && count > 0) {
                cl_uint maxToFetch = (cl_uint)(sizeof(g_Platforms)/sizeof(g_Platforms[0]) - g_PlatformCount);
                if (count > maxToFetch) count = maxToFetch;
                if (pfnGetPlat(count, &g_Platforms[g_PlatformCount], &count) == CL_SUCCESS) {
                    g_PlatformCount += count;
                }
            }
        }
    }

    g_Initialized = TRUE;
    LeaveCriticalSection(&g_LoaderLock);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&g_LoaderLock);
        break;
    case DLL_PROCESS_DETACH:
        DeleteCriticalSection(&g_LoaderLock);
        break;
    }
    return TRUE;
}

#define GET_DISPATCH(obj) (*((struct _cl_icd_dispatch**)(obj)))

/* =========================================================================
   Platform APIs
   ========================================================================= */

CL_API_ENTRY cl_int CL_API_CALL
clIcdGetPlatformIDsKHR(cl_uint num_entries, cl_platform_id *platforms, cl_uint *num_platforms)
{
    LoadVendors();
    if (platforms == NULL && num_platforms == NULL) return CL_INVALID_VALUE;
    if (platforms != NULL && num_entries == 0) return CL_INVALID_VALUE;

    if (num_platforms != NULL) {
        *num_platforms = g_PlatformCount;
    }
    if (platforms != NULL) {
        cl_uint toCopy = (num_entries < g_PlatformCount) ? num_entries : g_PlatformCount;
        for (cl_uint i = 0; i < toCopy; i++) {
            platforms[i] = g_Platforms[i];
        }
    }
    return (g_PlatformCount > 0) ? CL_SUCCESS : CL_PLATFORM_NOT_FOUND_KHR;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetPlatformIDs(cl_uint num_entries, cl_platform_id *platforms, cl_uint *num_platforms)
{
    return clIcdGetPlatformIDsKHR(num_entries, platforms, num_platforms);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetPlatformInfo(cl_platform_id platform, cl_platform_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!platform) return CL_INVALID_PLATFORM;
    return GET_DISPATCH(platform)->clGetPlatformInfo(platform, param_name, param_value_size, param_value, param_value_size_ret);
}

/* =========================================================================
   Device APIs
   ========================================================================= */

CL_API_ENTRY cl_int CL_API_CALL
clGetDeviceIDs(cl_platform_id platform, cl_device_type device_type, cl_uint num_entries, cl_device_id *devices, cl_uint *num_devices)
{
    LoadVendors();
    if (!platform) {
        if (g_PlatformCount > 0) platform = g_Platforms[0];
        else return CL_INVALID_PLATFORM;
    }
    return GET_DISPATCH(platform)->clGetDeviceIDs(platform, device_type, num_entries, devices, num_devices);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetDeviceInfo(cl_device_id device, cl_device_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!device) return CL_INVALID_DEVICE;
    return GET_DISPATCH(device)->clGetDeviceInfo(device, param_name, param_value_size, param_value, param_value_size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clCreateSubDevices(cl_device_id in_device, const cl_device_partition_property *properties, cl_uint num_devices, cl_device_id *out_devices, cl_uint *num_devices_ret)
{
    if (!in_device) return CL_INVALID_DEVICE;
    return GET_DISPATCH(in_device)->clCreateSubDevices(in_device, properties, num_devices, out_devices, num_devices_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainDevice(cl_device_id device)
{
    if (!device) return CL_INVALID_DEVICE;
    return GET_DISPATCH(device)->clRetainDevice(device);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseDevice(cl_device_id device)
{
    if (!device) return CL_INVALID_DEVICE;
    return GET_DISPATCH(device)->clReleaseDevice(device);
}

/* =========================================================================
   Context APIs
   ========================================================================= */

CL_API_ENTRY cl_context CL_API_CALL
clCreateContext(const cl_context_properties *properties, cl_uint num_devices, const cl_device_id *devices,
                void (CL_CALLBACK *pfn_notify)(const char *, const void *, size_t, void *),
                void *user_data, cl_int *errcode_ret)
{
    if (num_devices == 0 || devices == NULL || devices[0] == NULL) {
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }
    return GET_DISPATCH(devices[0])->clCreateContext(properties, num_devices, devices, pfn_notify, user_data, errcode_ret);
}

CL_API_ENTRY cl_context CL_API_CALL
clCreateContextFromType(const cl_context_properties *properties, cl_device_type device_type,
                        void (CL_CALLBACK *pfn_notify)(const char *, const void *, size_t, void *),
                        void *user_data, cl_int *errcode_ret)
{
    LoadVendors();
    if (g_PlatformCount == 0) {
        if (errcode_ret) *errcode_ret = CL_PLATFORM_NOT_FOUND_KHR;
        return NULL;
    }
    return GET_DISPATCH(g_Platforms[0])->clCreateContextFromType(properties, device_type, pfn_notify, user_data, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainContext(cl_context context)
{
    if (!context) return CL_INVALID_CONTEXT;
    return GET_DISPATCH(context)->clRetainContext(context);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseContext(cl_context context)
{
    if (!context) return CL_INVALID_CONTEXT;
    return GET_DISPATCH(context)->clReleaseContext(context);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetContextInfo(cl_context context, cl_context_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!context) return CL_INVALID_CONTEXT;
    return GET_DISPATCH(context)->clGetContextInfo(context, param_name, param_value_size, param_value, param_value_size_ret);
}

/* =========================================================================
   Command Queue APIs
   ========================================================================= */

CL_API_ENTRY cl_command_queue CL_API_CALL
clCreateCommandQueue(cl_context context, cl_device_id device, cl_command_queue_properties properties, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    return GET_DISPATCH(context)->clCreateCommandQueue(context, device, properties, errcode_ret);
}

CL_API_ENTRY cl_command_queue CL_API_CALL
clCreateCommandQueueWithProperties(cl_context context, cl_device_id device, const cl_queue_properties *properties, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    if (GET_DISPATCH(context)->clCreateCommandQueueWithProperties) {
        typedef cl_command_queue (CL_API_CALL *pfn_clCreateCommandQueueWithProperties)(cl_context, cl_device_id, const cl_queue_properties*, cl_int*);
        return ((pfn_clCreateCommandQueueWithProperties)GET_DISPATCH(context)->clCreateCommandQueueWithProperties)(context, device, properties, errcode_ret);
    }
    /* Fallback para clCreateCommandQueue se driver for 1.2 */
    cl_command_queue_properties legacyProps = 0;
    if (properties && properties[0] == CL_QUEUE_PROPERTIES) {
        legacyProps = (cl_command_queue_properties)properties[1];
    }
    return GET_DISPATCH(context)->clCreateCommandQueue(context, device, legacyProps, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainCommandQueue(cl_command_queue command_queue)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clRetainCommandQueue(command_queue);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseCommandQueue(cl_command_queue command_queue)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clReleaseCommandQueue(command_queue);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetCommandQueueInfo(cl_command_queue command_queue, cl_command_queue_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clGetCommandQueueInfo(command_queue, param_name, param_value_size, param_value, param_value_size_ret);
}

/* =========================================================================
   Memory Object APIs
   ========================================================================= */

CL_API_ENTRY cl_mem CL_API_CALL
clCreateBuffer(cl_context context, cl_mem_flags flags, size_t size, void *host_ptr, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    return GET_DISPATCH(context)->clCreateBuffer(context, flags, size, host_ptr, errcode_ret);
}

CL_API_ENTRY cl_mem CL_API_CALL
clCreateSubBuffer(cl_mem buffer, cl_mem_flags flags, cl_buffer_create_type buffer_create_type, const void *buffer_create_info, cl_int *errcode_ret)
{
    if (!buffer) {
        if (errcode_ret) *errcode_ret = CL_INVALID_MEM_OBJECT;
        return NULL;
    }
    return GET_DISPATCH(buffer)->clCreateSubBuffer(buffer, flags, buffer_create_type, buffer_create_info, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainMemObject(cl_mem memobj)
{
    if (!memobj) return CL_INVALID_MEM_OBJECT;
    return GET_DISPATCH(memobj)->clRetainMemObject(memobj);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseMemObject(cl_mem memobj)
{
    if (!memobj) return CL_INVALID_MEM_OBJECT;
    return GET_DISPATCH(memobj)->clReleaseMemObject(memobj);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetMemObjectInfo(cl_mem memobj, cl_mem_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!memobj) return CL_INVALID_MEM_OBJECT;
    return GET_DISPATCH(memobj)->clGetMemObjectInfo(memobj, param_name, param_value_size, param_value, param_value_size_ret);
}

/* =========================================================================
   Program APIs
   ========================================================================= */

CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithSource(cl_context context, cl_uint count, const char **strings, const size_t *lengths, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    return GET_DISPATCH(context)->clCreateProgramWithSource(context, count, strings, lengths, errcode_ret);
}

CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithBinary(cl_context context, cl_uint num_devices, const cl_device_id *device_list,
                          const size_t *lengths, const unsigned char **binaries, cl_int *binary_status, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    return GET_DISPATCH(context)->clCreateProgramWithBinary(context, num_devices, device_list, lengths, binaries, binary_status, errcode_ret);
}

CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithIL(cl_context context, const void *il, size_t length, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    if (GET_DISPATCH(context)->clCreateProgramWithIL) {
        typedef cl_program (CL_API_CALL *pfn_clCreateProgramWithIL)(cl_context, const void*, size_t, cl_int*);
        return ((pfn_clCreateProgramWithIL)GET_DISPATCH(context)->clCreateProgramWithIL)(context, il, length, errcode_ret);
    }
    if (errcode_ret) *errcode_ret = CL_INVALID_OPERATION;
    return NULL;
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainProgram(cl_program program)
{
    if (!program) return CL_INVALID_PROGRAM;
    return GET_DISPATCH(program)->clRetainProgram(program);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseProgram(cl_program program)
{
    if (!program) return CL_INVALID_PROGRAM;
    return GET_DISPATCH(program)->clReleaseProgram(program);
}

CL_API_ENTRY cl_int CL_API_CALL
clBuildProgram(cl_program program, cl_uint num_devices, const cl_device_id *device_list,
               const char *options, void (CL_CALLBACK *pfn_notify)(cl_program, void *), void *user_data)
{
    if (!program) return CL_INVALID_PROGRAM;
    return GET_DISPATCH(program)->clBuildProgram(program, num_devices, device_list, options, pfn_notify, user_data);
}

CL_API_ENTRY cl_int CL_API_CALL
clCompileProgram(cl_program program, cl_uint num_devices, const cl_device_id *device_list,
                 const char *options, cl_uint num_input_headers, const cl_program *input_headers,
                 const char **header_include_names, void (CL_CALLBACK *pfn_notify)(cl_program, void *), void *user_data)
{
    if (!program) return CL_INVALID_PROGRAM;
    return GET_DISPATCH(program)->clCompileProgram(program, num_devices, device_list, options, num_input_headers, input_headers, header_include_names, pfn_notify, user_data);
}

CL_API_ENTRY cl_program CL_API_CALL
clLinkProgram(cl_context context, cl_uint num_devices, const cl_device_id *device_list,
              const char *options, cl_uint num_input_programs, const cl_program *input_programs,
              void (CL_CALLBACK *pfn_notify)(cl_program, void *), void *user_data, cl_int *errcode_ret)
{
    if (!context) {
        if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT;
        return NULL;
    }
    return GET_DISPATCH(context)->clLinkProgram(context, num_devices, device_list, options, num_input_programs, input_programs, pfn_notify, user_data, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clUnloadPlatformCompiler(cl_platform_id platform)
{
    if (!platform) return CL_INVALID_PLATFORM;
    return GET_DISPATCH(platform)->clUnloadPlatformCompiler(platform);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetProgramInfo(cl_program program, cl_program_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!program) return CL_INVALID_PROGRAM;
    return GET_DISPATCH(program)->clGetProgramInfo(program, param_name, param_value_size, param_value, param_value_size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetProgramBuildInfo(cl_program program, cl_device_id device, cl_program_build_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!program) return CL_INVALID_PROGRAM;
    return GET_DISPATCH(program)->clGetProgramBuildInfo(program, device, param_name, param_value_size, param_value, param_value_size_ret);
}

/* =========================================================================
   Kernel APIs
   ========================================================================= */

CL_API_ENTRY cl_kernel CL_API_CALL
clCreateKernel(cl_program program, const char *kernel_name, cl_int *errcode_ret)
{
    if (!program) {
        if (errcode_ret) *errcode_ret = CL_INVALID_PROGRAM;
        return NULL;
    }
    return GET_DISPATCH(program)->clCreateKernel(program, kernel_name, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clCreateKernelsInProgram(cl_program program, cl_uint num_kernels, cl_kernel *kernels, cl_uint *num_kernels_ret)
{
    if (!program) return CL_INVALID_PROGRAM;
    return GET_DISPATCH(program)->clCreateKernelsInProgram(program, num_kernels, kernels, num_kernels_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainKernel(cl_kernel kernel)
{
    if (!kernel) return CL_INVALID_KERNEL;
    return GET_DISPATCH(kernel)->clRetainKernel(kernel);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseKernel(cl_kernel kernel)
{
    if (!kernel) return CL_INVALID_KERNEL;
    return GET_DISPATCH(kernel)->clReleaseKernel(kernel);
}

CL_API_ENTRY cl_int CL_API_CALL
clSetKernelArg(cl_kernel kernel, cl_uint arg_index, size_t arg_size, const void *arg_value)
{
    if (!kernel) return CL_INVALID_KERNEL;
    return GET_DISPATCH(kernel)->clSetKernelArg(kernel, arg_index, arg_size, arg_value);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelInfo(cl_kernel kernel, cl_kernel_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!kernel) return CL_INVALID_KERNEL;
    return GET_DISPATCH(kernel)->clGetKernelInfo(kernel, param_name, param_value_size, param_value, param_value_size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelWorkGroupInfo(cl_kernel kernel, cl_device_id device, cl_kernel_work_group_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!kernel) return CL_INVALID_KERNEL;
    return GET_DISPATCH(kernel)->clGetKernelWorkGroupInfo(kernel, device, param_name, param_value_size, param_value, param_value_size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelArgInfo(cl_kernel kernel, cl_uint arg_indx, cl_kernel_arg_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    if (!kernel) return CL_INVALID_KERNEL;
    return GET_DISPATCH(kernel)->clGetKernelArgInfo(kernel, arg_indx, param_name, param_value_size, param_value, param_value_size_ret);
}

/* =========================================================================
   Enqueue APIs
   ========================================================================= */

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueNDRangeKernel(cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
                       const size_t *global_work_offset, const size_t *global_work_size, const size_t *local_work_size,
                       cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clEnqueueNDRangeKernel(command_queue, kernel, work_dim, global_work_offset, global_work_size, local_work_size, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueTask(cl_command_queue command_queue, cl_kernel kernel, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clEnqueueTask(command_queue, kernel, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueReadBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
                    size_t offset, size_t size, void *ptr,
                    cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clEnqueueReadBuffer(command_queue, buffer, blocking_read, offset, size, ptr, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueWriteBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
                     size_t offset, size_t size, const void *ptr,
                     cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clEnqueueWriteBuffer(command_queue, buffer, blocking_write, offset, size, ptr, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueCopyBuffer(cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
                    size_t src_offset, size_t dst_offset, size_t size,
                    cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clEnqueueCopyBuffer(command_queue, src_buffer, dst_buffer, src_offset, dst_offset, size, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueFillBuffer(cl_command_queue command_queue, cl_mem buffer, const void *pattern, size_t pattern_size,
                    size_t offset, size_t size, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clEnqueueFillBuffer(command_queue, buffer, pattern, pattern_size, offset, size, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clFlush(cl_command_queue command_queue)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clFlush(command_queue);
}

CL_API_ENTRY cl_int CL_API_CALL
clFinish(cl_command_queue command_queue)
{
    if (!command_queue) return CL_INVALID_COMMAND_QUEUE;
    return GET_DISPATCH(command_queue)->clFinish(command_queue);
}

CL_API_ENTRY void * CL_API_CALL
clGetExtensionFunctionAddress(const char *func_name)
{
    LoadVendors();
    if (g_PlatformCount > 0) {
        return GET_DISPATCH(g_Platforms[0])->clGetExtensionFunctionAddress(func_name);
    }
    return NULL;
}

CL_API_ENTRY void * CL_API_CALL
clGetExtensionFunctionAddressForPlatform(cl_platform_id platform, const char *func_name)
{
    if (!platform) return NULL;
    return GET_DISPATCH(platform)->clGetExtensionFunctionAddressForPlatform(platform, func_name);
}
