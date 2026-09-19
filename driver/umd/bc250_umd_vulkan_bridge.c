#include "bc250_umd_vulkan_bridge.h"
#include "bc250_wddm_winsys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static void Bc250VkTrace(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    Bc250WinsysTraceRaw(buf);
}

#define LOAD_VK_INST(Name) do { \
    pBridge->vk.Name = (PFN_##Name)getInstanceProcAddr(pBridge->Instance, #Name); \
    if (pBridge->vk.Name == NULL) { \
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: Funcao de instancia ausente: %s", #Name); \
        return FALSE; \
    } \
} while (0)

#define LOAD_VK_DEV(Name) do { \
    pBridge->vk.Name = (PFN_##Name)pBridge->vk.vkGetDeviceProcAddr(pBridge->Device, #Name); \
    if (pBridge->vk.Name == NULL) { \
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: Funcao de dispositivo ausente: %s", #Name); \
        return FALSE; \
    } \
} while (0)

static HMODULE Bc250LocateAndLoadIcd(void)
{
    char path[MAX_PATH];
    HMODULE mod = NULL;
    DWORD len = 0;

    Bc250VkTrace("[VK_BRIDGE] Localizando ICD Vulkan (vulkan_radeon.dll)...");

    /* 1. Check registry override if configured */
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\BC250\\Umd", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = sizeof(path);
        if (RegQueryValueExA(key, "IcdPath", NULL, &type, (LPBYTE)path, &size) == ERROR_SUCCESS && type == REG_SZ) {
            Bc250VkTrace("[VK_BRIDGE] Tentando IcdPath do registro: '%s'", path);
            mod = LoadLibraryExA(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (mod != NULL) {
                Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado via registro: %p", mod);
                RegCloseKey(key);
                return mod;
            } else {
                Bc250VkTrace("[VK_BRIDGE] AVISO: Falha ao carregar IcdPath do registro (err=%lu)", GetLastError());
            }
        }
        RegCloseKey(key);
    }

    /* 2. Check directory of current module (bc250umd.dll) */
    HMODULE currentModule = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)Bc250LocateAndLoadIcd, &currentModule);
    if (currentModule != NULL) {
        len = GetModuleFileNameA(currentModule, path, ARRAYSIZE(path));
        if (len > 0 && len < ARRAYSIZE(path)) {
            char *lastSlash = strrchr(path, '\\');
            if (lastSlash != NULL) {
                *(lastSlash + 1) = '\0';
                strcat_s(path, sizeof(path), "vulkan_radeon.dll");
                Bc250VkTrace("[VK_BRIDGE] Tentando caminho local do modulo: '%s'", path);
                mod = LoadLibraryExA(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (mod != NULL) {
                    Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado do diretorio local: %p", mod);
                    return mod;
                }
            }
        }
    }

    /* 3. Check development default build path */
    const char *devPath = "C:\\Users\\Daniel\\Desktop\\bc250-win\\bc250-win\\mesa-radv-wddm\\build\\src\\amd\\vulkan\\vulkan_radeon.dll";
    Bc250VkTrace("[VK_BRIDGE] Tentando caminho de desenvolvimento: '%s'", devPath);
    mod = LoadLibraryExA(devPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (mod != NULL) {
        Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado do caminho de desenvolvimento: %p", mod);
        return mod;
    }

    /* 3b. Check C:\BC250_LIB system installation directory */
    const char *libPath = "C:\\BC250_LIB\\vulkan_radeon.dll";
    Bc250VkTrace("[VK_BRIDGE] Tentando caminho C:\\BC250_LIB: '%s'", libPath);
    mod = LoadLibraryExA(libPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (mod != NULL) {
        Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado de C:\\BC250_LIB: %p", mod);
        return mod;
    }

    /* 4. Fallback to standard DLL search */
    Bc250VkTrace("[VK_BRIDGE] Tentando busca padrao no sistema para 'vulkan_radeon.dll'...");
    mod = LoadLibraryA("vulkan_radeon.dll");
    if (mod != NULL) {
        Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado da busca padrao: %p", mod);
        return mod;
    }

    Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: Nao foi possivel carregar vulkan_radeon.dll por nenhum caminho! (LastErr=%lu)", GetLastError());
    return NULL;
}

static BOOL Bc250UmdVkBridgeInitInternal(_Out_ BC250_UMD_VK_BRIDGE *pBridge)
{
    Bc250VkTrace("[VK_BRIDGE] Bc250UmdVkBridgeInitInternal: iniciando ponte Vulkan RADV...");

    pBridge->hIcdModule = Bc250LocateAndLoadIcd();
    if (pBridge->hIcdModule == NULL) {
        Bc250VkTrace("[VK_BRIDGE] RECUSA: modulo ICD nao carregado");
        return FALSE;
    }

    PFN_vk_icdNegotiateLoaderICDInterfaceVersion negotiate =
        (PFN_vk_icdNegotiateLoaderICDInterfaceVersion)(void *)
        GetProcAddress(pBridge->hIcdModule, "vk_icdNegotiateLoaderICDInterfaceVersion");
    PFN_vk_icdGetInstanceProcAddr getInstanceProcAddr =
        (PFN_vk_icdGetInstanceProcAddr)(void *)
        GetProcAddress(pBridge->hIcdModule, "vk_icdGetInstanceProcAddr");

    if (negotiate == NULL || getInstanceProcAddr == NULL) {
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: Exportacoes de loader ICD ausentes (negotiate=%p, getInstance=%p)",
            (void*)negotiate, (void*)getInstanceProcAddr);
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
        return FALSE;
    }

    uint32_t icdVersion = CURRENT_LOADER_ICD_INTERFACE_VERSION;
    VkResult negRes = negotiate(&icdVersion);
    Bc250VkTrace("[VK_BRIDGE] vk_icdNegotiateLoaderICDInterfaceVersion: res=%d, versao negociada=%u",
        negRes, icdVersion);
    if (negRes != VK_SUCCESS || icdVersion < 2u) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: versao negociada insuficiente (< 2) ou falha");
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
        return FALSE;
    }

    pBridge->vk.vkCreateInstance = (PFN_vkCreateInstance)
        getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (pBridge->vk.vkCreateInstance == NULL) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: vkCreateInstance nao encontrado via getInstanceProcAddr");
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
        return FALSE;
    }

    VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "BC250_UMD_D3D11_TRANSLATOR";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "BC250_D3D11_TO_VK";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instInfo.pApplicationInfo = &appInfo;

    VkResult res = pBridge->vk.vkCreateInstance(&instInfo, NULL, &pBridge->Instance);
    Bc250VkTrace("[VK_BRIDGE] vkCreateInstance: res=%d, Instance=%p", res, (void*)pBridge->Instance);
    if (res != VK_SUCCESS || pBridge->Instance == VK_NULL_HANDLE) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: falha ao criar VkInstance (res=%d)", res);
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
        return FALSE;
    }

    LOAD_VK_INST(vkDestroyInstance);
    LOAD_VK_INST(vkEnumeratePhysicalDevices);
    LOAD_VK_INST(vkGetPhysicalDeviceProperties);
    LOAD_VK_INST(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_VK_INST(vkGetPhysicalDeviceMemoryProperties);
    LOAD_VK_INST(vkCreateDevice);
    LOAD_VK_INST(vkGetDeviceProcAddr);

    uint32_t physCount = 0;
    res = pBridge->vk.vkEnumeratePhysicalDevices(pBridge->Instance, &physCount, NULL);
    Bc250VkTrace("[VK_BRIDGE] vkEnumeratePhysicalDevices: res=%d, physCount=%u", res, physCount);
    if (res != VK_SUCCESS || physCount == 0) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: nenhuma GPU fisica encontrada via Vulkan ICD");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    VkPhysicalDevice *physList = (VkPhysicalDevice *)calloc(physCount, sizeof(VkPhysicalDevice));
    if (physList == NULL) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: falha de memoria ao alocar lista de GPUs");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    pBridge->vk.vkEnumeratePhysicalDevices(pBridge->Instance, &physCount, physList);
    pBridge->PhysicalDevice = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < physCount; ++i) {
        VkPhysicalDeviceProperties props;
        pBridge->vk.vkGetPhysicalDeviceProperties(physList[i], &props);
        Bc250VkTrace("[VK_BRIDGE] GPU [%u]: Vendor=0x%04X, Device=0x%04X, Nome='%s', DriverVer=0x%08X",
            i, props.vendorID, props.deviceID, props.deviceName, props.driverVersion);
        if (props.vendorID == BC250_VENDOR_ID && props.deviceID == BC250_DEVICE_ID) {
            pBridge->PhysicalDevice = physList[i];
            Bc250VkTrace("[VK_BRIDGE] -> SELECIONADO: Adaptador BC-250 (1002:13FE) no indice %u", i);
            break;
        }
    }
    free(physList);

    if (pBridge->PhysicalDevice == VK_NULL_HANDLE) {
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: GPU BC-250 (1002:13FE) nao encontrada entre as %u GPUs!", physCount);
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    uint32_t queueCount = 0;
    pBridge->vk.vkGetPhysicalDeviceQueueFamilyProperties(pBridge->PhysicalDevice, &queueCount, NULL);
    Bc250VkTrace("[VK_BRIDGE] Familias de filas: count=%u", queueCount);
    if (queueCount == 0) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: nenhuma familia de fila encontrada na GPU");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    VkQueueFamilyProperties *queues = (VkQueueFamilyProperties *)calloc(queueCount, sizeof(VkQueueFamilyProperties));
    if (queues == NULL) {
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    pBridge->vk.vkGetPhysicalDeviceQueueFamilyProperties(pBridge->PhysicalDevice, &queueCount, queues);
    pBridge->GraphicsQueueFamily = UINT32_MAX;
    for (uint32_t q = 0; q < queueCount; ++q) {
        Bc250VkTrace("[VK_BRIDGE] Fila [%u]: Flags=0x%08X (Graphics=%d, Compute=%d, Transfer=%d) Count=%u",
            q, queues[q].queueFlags,
            (queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? 1 : 0,
            (queues[q].queueFlags & VK_QUEUE_COMPUTE_BIT) ? 1 : 0,
            (queues[q].queueFlags & VK_QUEUE_TRANSFER_BIT) ? 1 : 0,
            queues[q].queueCount);
        if ((queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && queues[q].queueCount > 0) {
            pBridge->GraphicsQueueFamily = q;
            Bc250VkTrace("[VK_BRIDGE] -> Fila grafica selecionada: index=%u", q);
            break;
        }
    }
    free(queues);

    if (pBridge->GraphicsQueueFamily == UINT32_MAX) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: nenhuma fila com suporte GRAPHICS encontrada");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    pBridge->vk.vkGetPhysicalDeviceMemoryProperties(pBridge->PhysicalDevice, &pBridge->MemProperties);
    Bc250VkTrace("[VK_BRIDGE] Memoria GPU: %u tipos de memoria, %u heaps",
        pBridge->MemProperties.memoryTypeCount, pBridge->MemProperties.memoryHeapCount);

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = pBridge->GraphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    res = pBridge->vk.vkCreateDevice(pBridge->PhysicalDevice, &deviceCreateInfo, NULL, &pBridge->Device);
    Bc250VkTrace("[VK_BRIDGE] vkCreateDevice: res=%d, Device=%p", res, (void*)pBridge->Device);
    if (res != VK_SUCCESS || pBridge->Device == VK_NULL_HANDLE) {
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: vkCreateDevice falhou (res=%d)", res);
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    LOAD_VK_DEV(vkDestroyDevice);
    LOAD_VK_DEV(vkGetDeviceQueue);
    LOAD_VK_DEV(vkCreateBuffer);
    LOAD_VK_DEV(vkDestroyBuffer);
    LOAD_VK_DEV(vkGetBufferMemoryRequirements);
    LOAD_VK_DEV(vkCreateImage);
    LOAD_VK_DEV(vkDestroyImage);
    LOAD_VK_DEV(vkGetImageMemoryRequirements);
    LOAD_VK_DEV(vkCreateImageView);
    LOAD_VK_DEV(vkDestroyImageView);
    LOAD_VK_DEV(vkCreateSampler);
    LOAD_VK_DEV(vkDestroySampler);
    LOAD_VK_DEV(vkAllocateMemory);
    LOAD_VK_DEV(vkFreeMemory);
    LOAD_VK_DEV(vkBindBufferMemory);
    LOAD_VK_DEV(vkBindImageMemory);
    LOAD_VK_DEV(vkMapMemory);
    LOAD_VK_DEV(vkUnmapMemory);
    LOAD_VK_DEV(vkInvalidateMappedMemoryRanges);
    LOAD_VK_DEV(vkFlushMappedMemoryRanges);
    LOAD_VK_DEV(vkCreateRenderPass);
    LOAD_VK_DEV(vkDestroyRenderPass);
    LOAD_VK_DEV(vkCreateFramebuffer);
    LOAD_VK_DEV(vkDestroyFramebuffer);
    LOAD_VK_DEV(vkCreateDescriptorSetLayout);
    LOAD_VK_DEV(vkDestroyDescriptorSetLayout);
    LOAD_VK_DEV(vkCreateDescriptorPool);
    LOAD_VK_DEV(vkDestroyDescriptorPool);
    LOAD_VK_DEV(vkAllocateDescriptorSets);
    LOAD_VK_DEV(vkUpdateDescriptorSets);
    LOAD_VK_DEV(vkCreatePipelineLayout);
    LOAD_VK_DEV(vkDestroyPipelineLayout);
    LOAD_VK_DEV(vkCreateShaderModule);
    LOAD_VK_DEV(vkDestroyShaderModule);
    LOAD_VK_DEV(vkCreateGraphicsPipelines);
    LOAD_VK_DEV(vkDestroyPipeline);
    LOAD_VK_DEV(vkCreateCommandPool);
    LOAD_VK_DEV(vkDestroyCommandPool);
    LOAD_VK_DEV(vkAllocateCommandBuffers);
    LOAD_VK_DEV(vkBeginCommandBuffer);
    LOAD_VK_DEV(vkEndCommandBuffer);
    LOAD_VK_DEV(vkCmdBeginRenderPass);
    LOAD_VK_DEV(vkCmdEndRenderPass);
    LOAD_VK_DEV(vkCmdBindPipeline);
    LOAD_VK_DEV(vkCmdBindVertexBuffers);
    LOAD_VK_DEV(vkCmdBindIndexBuffer);
    LOAD_VK_DEV(vkCmdBindDescriptorSets);
    LOAD_VK_DEV(vkCmdSetViewport);
    LOAD_VK_DEV(vkCmdSetScissor);
    LOAD_VK_DEV(vkCmdPushConstants);
    LOAD_VK_DEV(vkCmdDraw);
    LOAD_VK_DEV(vkCmdDrawIndexed);
    LOAD_VK_DEV(vkCmdPipelineBarrier);
    LOAD_VK_DEV(vkCmdCopyBufferToImage);
    LOAD_VK_DEV(vkCmdCopyImageToBuffer);
    LOAD_VK_DEV(vkCmdCopyImage);
    LOAD_VK_DEV(vkCmdCopyBuffer);
    LOAD_VK_DEV(vkCmdClearAttachments);
    LOAD_VK_DEV(vkCmdClearColorImage);
    LOAD_VK_DEV(vkCreateFence);
    LOAD_VK_DEV(vkDestroyFence);
    LOAD_VK_DEV(vkResetFences);
    LOAD_VK_DEV(vkResetCommandBuffer);
    LOAD_VK_DEV(vkQueueSubmit);
    LOAD_VK_DEV(vkWaitForFences);
    LOAD_VK_DEV(vkDeviceWaitIdle);

    pBridge->vk.vkGetDeviceQueue(pBridge->Device, pBridge->GraphicsQueueFamily, 0, &pBridge->GraphicsQueue);
    Bc250VkTrace("[VK_BRIDGE] vkGetDeviceQueue: GraphicsQueue=%p", (void*)pBridge->GraphicsQueue);

    VkCommandPoolCreateInfo cmdPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmdPoolInfo.queueFamilyIndex = pBridge->GraphicsQueueFamily;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    res = pBridge->vk.vkCreateCommandPool(pBridge->Device, &cmdPoolInfo, NULL, &pBridge->CommandPool);
    Bc250VkTrace("[VK_BRIDGE] vkCreateCommandPool: res=%d, CommandPool=%p", res, (void*)pBridge->CommandPool);
    if (res != VK_SUCCESS) {
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    VkCommandBufferAllocateInfo cmdAlloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAlloc.commandPool = pBridge->CommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    res = pBridge->vk.vkAllocateCommandBuffers(pBridge->Device, &cmdAlloc, &pBridge->CommandBuffer);
    Bc250VkTrace("[VK_BRIDGE] vkAllocateCommandBuffers: res=%d, CommandBuffer=%p", res, (void*)pBridge->CommandBuffer);
    if (res != VK_SUCCESS) {
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    res = pBridge->vk.vkCreateFence(pBridge->Device, &fenceInfo, NULL, &pBridge->Fence);
    Bc250VkTrace("[VK_BRIDGE] vkCreateFence: res=%d, Fence=%p", res, (void*)pBridge->Fence);
    if (res != VK_SUCCESS) {
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    /* Opcional: checar presenca de radv_bc250_present_scanout exportado pelo ICD */
    pBridge->pfnPresentScanout = (PFN_radv_bc250_present_scanout)(void*)
        GetProcAddress(pBridge->hIcdModule, "radv_bc250_present_scanout");
    Bc250VkTrace("[VK_BRIDGE] radv_bc250_present_scanout: %s (%p)",
        pBridge->pfnPresentScanout ? "PRESENTE" : "AUSENTE",
        (void*)pBridge->pfnPresentScanout);

    /* Checar presenca de radv_bc250_get_memory_gpu_va exportado pelo ICD */
    pBridge->pfnGetMemoryGpuVa = (PFN_radv_bc250_get_memory_gpu_va)(void*)
        GetProcAddress(pBridge->hIcdModule, "radv_bc250_get_memory_gpu_va");
    Bc250VkTrace("[VK_BRIDGE] radv_bc250_get_memory_gpu_va: %s (%p)",
        pBridge->pfnGetMemoryGpuVa ? "PRESENTE" : "AUSENTE",
        (void*)pBridge->pfnGetMemoryGpuVa);

    pBridge->Initialized = TRUE;
    pBridge->DeviceActive = TRUE;
    pBridge->RecordingActive = FALSE;
    Bc250VkTrace("[VK_BRIDGE] Bc250UmdVkBridgeInitInternal: CONCLUIDO COM SUCESSO!");
    return TRUE;
}

VOID Bc250UmdVkReleaseDescriptorBatches(BC250_UMD_VK_BRIDGE *pBridge)
{
    while (pBridge->DescriptorBatches) {
        BC250_VK_DESCRIPTOR_BATCH *batch = pBridge->DescriptorBatches;
        pBridge->DescriptorBatches = batch->Next;
        if (batch->Pool) pBridge->vk.vkDestroyDescriptorPool(pBridge->Device, batch->Pool, NULL);
        for (uint32_t i = 0; i < 316; ++i) {
            if (batch->Views[i]) pBridge->vk.vkDestroyImageView(pBridge->Device, batch->Views[i], NULL);
            if (batch->Samplers[i]) pBridge->vk.vkDestroySampler(pBridge->Device, batch->Samplers[i], NULL);
        }
        free(batch);
    }
}

VOID Bc250UmdVkBridgeShutdown(_Inout_ BC250_UMD_VK_BRIDGE *pBridge)
{
    if (pBridge == NULL) return;
    Bc250VkTrace("[VK_BRIDGE] Bc250UmdVkBridgeShutdown ENTER: DeviceActive=%d", pBridge->DeviceActive);

    if (pBridge->Device != VK_NULL_HANDLE) {
        if (pBridge->vk.vkDeviceWaitIdle != NULL) {
            pBridge->vk.vkDeviceWaitIdle(pBridge->Device);
        }
        Bc250UmdVkReleaseDescriptorBatches(pBridge);
        if (pBridge->LinearPresentBuf != VK_NULL_HANDLE && pBridge->vk.vkDestroyBuffer != NULL) {
            pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->LinearPresentBuf, NULL);
            pBridge->LinearPresentBuf = VK_NULL_HANDLE;
        }
        if (pBridge->LinearPresentMem != VK_NULL_HANDLE && pBridge->vk.vkFreeMemory != NULL) {
            pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->LinearPresentMem, NULL);
            pBridge->LinearPresentMem = VK_NULL_HANDLE;
        }
        pBridge->LinearPresentCreated = FALSE;
        pBridge->LinearPresentSize = 0;

        if (pBridge->Fence != VK_NULL_HANDLE && pBridge->vk.vkDestroyFence != NULL) {
            pBridge->vk.vkDestroyFence(pBridge->Device, pBridge->Fence, NULL);
            pBridge->Fence = VK_NULL_HANDLE;
        }
        if (pBridge->CommandPool != VK_NULL_HANDLE && pBridge->vk.vkDestroyCommandPool != NULL) {
            pBridge->vk.vkDestroyCommandPool(pBridge->Device, pBridge->CommandPool, NULL);
            pBridge->CommandPool = VK_NULL_HANDLE;
        }
        if (pBridge->vk.vkDestroyDevice != NULL) {
            pBridge->vk.vkDestroyDevice(pBridge->Device, NULL);
            pBridge->Device = VK_NULL_HANDLE;
        }
    }

    if (pBridge->Instance != VK_NULL_HANDLE) {
        if (pBridge->vk.vkDestroyInstance != NULL) {
            pBridge->vk.vkDestroyInstance(pBridge->Instance, NULL);
            pBridge->Instance = VK_NULL_HANDLE;
        }
    }

    if (pBridge->hIcdModule != NULL) {
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
    }

    pBridge->pfnPresentScanout = NULL;
    pBridge->pfnGetMemoryGpuVa = NULL;
    pBridge->Initialized = FALSE;
    pBridge->DeviceActive = FALSE;
    pBridge->RecordingActive = FALSE;
    Bc250VkTrace("[VK_BRIDGE] Bc250UmdVkBridgeShutdown LEAVE: Desmontado com sucesso");
}

BOOL Bc250UmdVkBridgeInit(_Out_ BC250_UMD_VK_BRIDGE *pBridge)
{
    char envBuf[16];
    if (pBridge == NULL) return FALSE;
    memset(pBridge, 0, sizeof(*pBridge));

    if (GetEnvironmentVariableA("BC250_UMD_DISABLE_VK_BRIDGE", envBuf, sizeof(envBuf)) > 0 &&
        envBuf[0] == '1') {
        Bc250VkTrace("[VK_BRIDGE] Desabilitado via variavel de ambiente BC250_UMD_DISABLE_VK_BRIDGE=1");
        return FALSE;
    }

    __try {
        return Bc250UmdVkBridgeInitInternal(pBridge);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Bc250VkTrace("[VK_BRIDGE] EXCECAO SEH CAPTURADA em Bc250UmdVkBridgeInit! Executando shutdown seguro.");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }
}

uint32_t Bc250UmdVkFindMemoryType(
    _In_ const BC250_UMD_VK_BRIDGE *pBridge,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties)
{
    if (pBridge == NULL) return UINT32_MAX;
    for (uint32_t i = 0; i < pBridge->MemProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0u &&
            (pBridge->MemProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkCommandBuffer Bc250UmdVkGetCommandBuffer(_Inout_ BC250_UMD_VK_BRIDGE *pBridge)
{
    if (pBridge == NULL || !pBridge->DeviceActive || pBridge->SubmissionFailed) return VK_NULL_HANDLE;

    if (!pBridge->RecordingActive) {
        if (pBridge->vk.vkResetCommandBuffer(pBridge->CommandBuffer, 0) != VK_SUCCESS) {
            pBridge->SubmissionFailed = TRUE;
            return VK_NULL_HANDLE;
        }
        VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (pBridge->vk.vkBeginCommandBuffer(pBridge->CommandBuffer, &beginInfo) == VK_SUCCESS) {
            pBridge->RecordingActive = TRUE;
            Bc250VkTrace("[VK_BRIDGE] BeginCommandBuffer: gravacao iniciada (cmd=%p)", (void*)pBridge->CommandBuffer);
        } else {
            Bc250VkTrace("[VK_BRIDGE] ERRO: vkBeginCommandBuffer falhou");
            return VK_NULL_HANDLE;
        }
    }
    return pBridge->CommandBuffer;
}

VkResult Bc250UmdVkFlushCommands(_Inout_ BC250_UMD_VK_BRIDGE *pBridge)
{
    if (pBridge == NULL || !pBridge->DeviceActive || pBridge->SubmissionFailed) return VK_ERROR_DEVICE_LOST;
    if (!pBridge->RecordingActive) return VK_SUCCESS;

    Bc250VkTrace("[VK_BRIDGE] FlushCommands ENTER: gravacao ativa, finalizando (cmd=%p)...", (void*)pBridge->CommandBuffer);

    VkResult res = pBridge->vk.vkEndCommandBuffer(pBridge->CommandBuffer);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] FlushCommands: vkEndCommandBuffer falhou: res=%d", res);
        pBridge->SubmissionFailed = TRUE;
        return res;
    }

    res = pBridge->vk.vkResetFences(pBridge->Device, 1, &pBridge->Fence);
    if (res != VK_SUCCESS) {
        pBridge->SubmissionFailed = TRUE;
        return res;
    }

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &pBridge->CommandBuffer;

    res = pBridge->vk.vkQueueSubmit(pBridge->GraphicsQueue, 1, &submitInfo, pBridge->Fence);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] FlushCommands: vkQueueSubmit falhou: res=%d", res);
        pBridge->SubmissionFailed = TRUE;
        return res;
    }

    res = pBridge->vk.vkWaitForFences(pBridge->Device, 1, &pBridge->Fence, VK_TRUE, 2000000000ull);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] FlushCommands: vkWaitForFences retorno nao-sucesso: res=%d (TIMEOUT ou ERRO)", res);
        pBridge->SubmissionFailed = TRUE;
    } else {
        Bc250UmdVkReleaseDescriptorBatches(pBridge);
        Bc250VkTrace("[VK_BRIDGE] FlushCommands: Execucao e Fence em hardware CONCLUIDOS com SUCESSO!");
    }
    pBridge->RecordingActive = FALSE;
    return res;
}

VkFormat Bc250UmdVkFormatFromDxgi(uint32_t dxgiFormat)
{
    switch (dxgiFormat) {
    case 2:  /* DXGI_FORMAT_R32G32B32A32_FLOAT */
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case 3:  /* DXGI_FORMAT_R32G32B32A32_UINT */
        return VK_FORMAT_R32G32B32A32_UINT;
    case 4:  /* DXGI_FORMAT_R32G32B32A32_SINT */
        return VK_FORMAT_R32G32B32A32_SINT;
    case 6:  /* DXGI_FORMAT_R32G32B32_FLOAT */
        return VK_FORMAT_R32G32B32_SFLOAT;
    case 7:  /* DXGI_FORMAT_R32G32B32_UINT */
        return VK_FORMAT_R32G32B32_UINT;
    case 8:  /* DXGI_FORMAT_R32G32B32_SINT */
        return VK_FORMAT_R32G32B32_SINT;
    case 10: /* DXGI_FORMAT_R16G16B16A16_FLOAT */
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case 11: /* DXGI_FORMAT_R16G16B16A16_UNORM */
        return VK_FORMAT_R16G16B16A16_UNORM;
    case 12: /* DXGI_FORMAT_R16G16B16A16_UINT */
        return VK_FORMAT_R16G16B16A16_UINT;
    case 13: /* DXGI_FORMAT_R16G16B16A16_SNORM */
        return VK_FORMAT_R16G16B16A16_SNORM;
    case 14: /* DXGI_FORMAT_R16G16B16A16_SINT */
        return VK_FORMAT_R16G16B16A16_SINT;
    case 16: /* DXGI_FORMAT_R32G32_FLOAT */
        return VK_FORMAT_R32G32_SFLOAT;
    case 17: /* DXGI_FORMAT_R32G32_UINT */
        return VK_FORMAT_R32G32_UINT;
    case 18: /* DXGI_FORMAT_R32G32_SINT */
        return VK_FORMAT_R32G32_SINT;
    case 28: /* DXGI_FORMAT_R8G8B8A8_UNORM */
    case 29: /* DXGI_FORMAT_R8G8B8A8_UNORM_SRGB */
        return VK_FORMAT_R8G8B8A8_UNORM;
    case 30: /* DXGI_FORMAT_R8G8B8A8_UINT */
        return VK_FORMAT_R8G8B8A8_UINT;
    case 31: /* DXGI_FORMAT_R8G8B8A8_SNORM */
        return VK_FORMAT_R8G8B8A8_SNORM;
    case 32: /* DXGI_FORMAT_R8G8B8A8_SINT */
        return VK_FORMAT_R8G8B8A8_SINT;
    case 34: /* DXGI_FORMAT_R16G16_FLOAT */
        return VK_FORMAT_R16G16_SFLOAT;
    case 35: /* DXGI_FORMAT_R16G16_UNORM */
        return VK_FORMAT_R16G16_UNORM;
    case 36: /* DXGI_FORMAT_R16G16_UINT */
        return VK_FORMAT_R16G16_UINT;
    case 37: /* DXGI_FORMAT_R16G16_SNORM */
        return VK_FORMAT_R16G16_SNORM;
    case 38: /* DXGI_FORMAT_R16G16_SINT */
        return VK_FORMAT_R16G16_SINT;
    case 40: /* DXGI_FORMAT_D32_FLOAT */
    case 41: /* DXGI_FORMAT_R32_FLOAT */
        return VK_FORMAT_R32_SFLOAT;
    case 42: /* DXGI_FORMAT_R32_UINT */
        return VK_FORMAT_R32_UINT;
    case 43: /* DXGI_FORMAT_R32_SINT */
        return VK_FORMAT_R32_SINT;
    case 45: /* DXGI_FORMAT_D24_UNORM_S8_UINT */
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case 49: /* DXGI_FORMAT_R8G8_UNORM */
        return VK_FORMAT_R8G8_UNORM;
    case 50: /* DXGI_FORMAT_R8G8_UINT */
        return VK_FORMAT_R8G8_UINT;
    case 51: /* DXGI_FORMAT_R8G8_SNORM */
        return VK_FORMAT_R8G8_SNORM;
    case 52: /* DXGI_FORMAT_R8G8_SINT */
        return VK_FORMAT_R8G8_SINT;
    case 54: /* DXGI_FORMAT_R16_FLOAT */
        return VK_FORMAT_R16_SFLOAT;
    case 56: /* DXGI_FORMAT_R16_UNORM */
        return VK_FORMAT_R16_UNORM;
    case 57: /* DXGI_FORMAT_R16_UINT */
        return VK_FORMAT_R16_UINT;
    case 58: /* DXGI_FORMAT_R16_SNORM */
        return VK_FORMAT_R16_SNORM;
    case 59: /* DXGI_FORMAT_R16_SINT */
        return VK_FORMAT_R16_SINT;
    case 61: /* DXGI_FORMAT_R8_UNORM */
        return VK_FORMAT_R8_UNORM;
    case 62: /* DXGI_FORMAT_R8_UINT */
        return VK_FORMAT_R8_UINT;
    case 63: /* DXGI_FORMAT_R8_SNORM */
        return VK_FORMAT_R8_SNORM;
    case 64: /* DXGI_FORMAT_R8_SINT */
        return VK_FORMAT_R8_SINT;
    case 65: /* DXGI_FORMAT_A8_UNORM */
        return VK_FORMAT_R8_UNORM;
    case 85: /* DXGI_FORMAT_B5G6R5_UNORM */
        return VK_FORMAT_B5G6R5_UNORM_PACK16;
    case 86: /* DXGI_FORMAT_B5G5R5A1_UNORM */
        return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
    case 87: /* DXGI_FORMAT_B8G8R8A8_UNORM */
    case 88: /* DXGI_FORMAT_B8G8R8A8_UNORM_SRGB */
    case 90: /* DXGI_FORMAT_B8G8R8X8_UNORM */
    case 91: /* DXGI_FORMAT_B8G8R8X8_UNORM_SRGB */
        return VK_FORMAT_B8G8R8A8_UNORM;
    default:
        return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

BOOL Bc250UmdVkEnsureLinearPresentBuf(
    _Inout_ BC250_UMD_VK_BRIDGE *pBridge,
    uint32_t width,
    uint32_t height)
{
    if (pBridge == NULL || !pBridge->DeviceActive || pBridge->Device == VK_NULL_HANDLE) {
        return FALSE;
    }

    VkDeviceSize requiredSize = (VkDeviceSize)width * height * 4;
    if (requiredSize == 0) requiredSize = (VkDeviceSize)2560 * 1080 * 4;

    if (pBridge->LinearPresentCreated && pBridge->LinearPresentBuf != VK_NULL_HANDLE) {
        if (pBridge->LinearPresentSize >= requiredSize) {
            return TRUE;
        }
        Bc250VkTrace("[VK_BRIDGE] Redimensionando LinearPresentBuf: atual=%llu, necessario=%llu",
            (unsigned long long)pBridge->LinearPresentSize, (unsigned long long)requiredSize);
        if (pBridge->vk.vkDeviceWaitIdle != NULL) {
            pBridge->vk.vkDeviceWaitIdle(pBridge->Device);
        }
        pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->LinearPresentBuf, NULL);
        pBridge->LinearPresentBuf = VK_NULL_HANDLE;
        pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->LinearPresentMem, NULL);
        pBridge->LinearPresentMem = VK_NULL_HANDLE;
        pBridge->LinearPresentCreated = FALSE;
    }

    VkBufferCreateInfo linearBufInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    linearBufInfo.size = requiredSize;
    linearBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    linearBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = pBridge->vk.vkCreateBuffer(pBridge->Device, &linearBufInfo, NULL, &pBridge->LinearPresentBuf);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] AVISO: vkCreateBuffer falhou para LinearPresentBuf: %d", res);
        return FALSE;
    }

    VkMemoryRequirements memReqs = {0};
    pBridge->vk.vkGetBufferMemoryRequirements(pBridge->Device, pBridge->LinearPresentBuf, &memReqs);

    /*
     * ESCOLHA DE MEMORIA -- e por que o rebaixamento nao pode mais ser silencioso.
     *
     * Medido na corrida 2026-09-18 13:12: o buffer saiu com memType=2 e a
     * inspecao de pixel falhou 128 vezes com "vkMapMemory retornou -5"
     * (VK_ERROR_MEMORY_MAP_FAILED). A mensagem culpava o mapeamento, mas a causa
     * estava AQUI: a primeira opcao (HOST_VISIBLE|HOST_COHERENT) nao foi
     * atendida e a cadeia caiu para DEVICE_LOCAL, que nao e' mapeavel pela CPU.
     * O resultado pratico foi perder a Perna 3 do tripe -- a unica que confere
     * bytes -- sem que nada no log dissesse que a decisao de alocacao tinha sido
     * rebaixada.
     *
     * A cadeia de fallback CONTINUA, de proposito: recusar a alocacao mataria o
     * caminho de apresentacao inteiro, o que e' pior do que apresentar sem poder
     * inspecionar. O que muda e' que o nivel escolhido passa a ser declarado, e
     * a perda de inspecionabilidade e' anunciada no momento em que acontece.
     */
    uint32_t memType = Bc250UmdVkFindMemoryType(
        pBridge, memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const char *memTier = "HOST_VISIBLE|HOST_COHERENT";
    BOOL memHostVisible = TRUE;
    if (memType == UINT32_MAX) {
        memType = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        memTier = "HOST_VISIBLE (sem HOST_COHERENT)";
    }
    if (memType == UINT32_MAX) {
        memType = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        memTier = "DEVICE_LOCAL -- NAO MAPEAVEL PELA CPU";
        memHostVisible = FALSE;
    }
    if (memType == UINT32_MAX) {
        memType = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits, 0);
        memTier = "QUALQUER TIPO -- inspecionabilidade DESCONHECIDA";
        memHostVisible = FALSE;
    }

    VkMemoryAllocateInfo memAlloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = (memType != UINT32_MAX) ? memType : 0;

    res = pBridge->vk.vkAllocateMemory(pBridge->Device, &memAlloc, NULL, &pBridge->LinearPresentMem);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] AVISO: vkAllocateMemory falhou para LinearPresentMem: %d", res);
        pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->LinearPresentBuf, NULL);
        pBridge->LinearPresentBuf = VK_NULL_HANDLE;
        return FALSE;
    }

    res = pBridge->vk.vkBindBufferMemory(pBridge->Device, pBridge->LinearPresentBuf, pBridge->LinearPresentMem, 0);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] AVISO: vkBindBufferMemory falhou para LinearPresentBuf: %d", res);
        pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->LinearPresentMem, NULL);
        pBridge->LinearPresentMem = VK_NULL_HANDLE;
        pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->LinearPresentBuf, NULL);
        pBridge->LinearPresentBuf = VK_NULL_HANDLE;
        return FALSE;
    }

    pBridge->LinearPresentSize = memReqs.size;
    pBridge->LinearPresentCreated = TRUE;
    Bc250VkTrace("[VK_BRIDGE] LinearPresentBuf LAZY criado com sucesso (%llu bytes, mem=%p, memType=%u, nivel=%s)",
        (unsigned long long)memReqs.size, (void*)pBridge->LinearPresentMem,
        memAlloc.memoryTypeIndex, memTier);
    if (!memHostVisible) {
        Bc250VkTrace("[VK_BRIDGE] AVISO LinearPresentBuf: memoria NAO e' host-visible (nivel=%s). "
                     "A INSPECAO LINEAR_BUF vai falhar com VK_ERROR_MEMORY_MAP_FAILED (-5) por DECISAO DE ALOCACAO, "
                     "nao por defeito de mapeamento. A Perna 3 do tripe fica sem medicao nesta sessao.",
            memTier);
    }
    return TRUE;
}

uint32_t Bc250UmdVkInspectLinearPresentBuf(
    _Inout_ BC250_UMD_VK_BRIDGE *pBridge,
    uint32_t width,
    uint32_t height)
{
    uint32_t nonZeroSamples = 0;
    if (pBridge == NULL || !pBridge->DeviceActive || pBridge->LinearPresentMem == VK_NULL_HANDLE ||
        pBridge->vk.vkMapMemory == NULL || pBridge->vk.vkUnmapMemory == NULL) {
        return 0;
    }

    void *mapped = NULL;
    VkDeviceSize checkSize = (VkDeviceSize)width * height * 4;
    if (checkSize == 0 || checkSize > pBridge->LinearPresentSize) checkSize = pBridge->LinearPresentSize;

    VkResult res = pBridge->vk.vkMapMemory(pBridge->Device, pBridge->LinearPresentMem, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (res == VK_SUCCESS && mapped != NULL) {
        if (pBridge->vk.vkInvalidateMappedMemoryRanges != NULL) {
            VkMappedMemoryRange range;
            memset(&range, 0, sizeof(range));
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = pBridge->LinearPresentMem;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            pBridge->vk.vkInvalidateMappedMemoryRanges(pBridge->Device, 1, &range);
        }
        const uint32_t *pWords = (const uint32_t *)mapped;
        uint32_t centerIdx = (height > 0 && width > 0) ? ((height / 2) * width + (width / 2)) : 0;
        uint32_t s0 = pWords[0];
        uint32_t s1 = (checkSize >= 8) ? pWords[1] : 0;
        uint32_t s2 = (checkSize >= 12) ? pWords[2] : 0;
        uint32_t s3 = (checkSize >= 16) ? pWords[3] : 0;
        uint32_t sc = (centerIdx < (checkSize / 4)) ? pWords[centerIdx] : 0;
        nonZeroSamples = (s0 != 0) + (s1 != 0) + (s2 != 0) + (s3 != 0) + (sc != 0);

        Bc250VkTrace("[VK_BRIDGE] INSPECAO LINEAR_BUF: Px0..3=[0x%08X 0x%08X 0x%08X 0x%08X] Center(%u,%u)=0x%08X NonZeroSamples=%u/5",
            s0, s1, s2, s3, width / 2, height / 2, sc, nonZeroSamples);

        pBridge->vk.vkUnmapMemory(pBridge->Device, pBridge->LinearPresentMem);
    } else {
        Bc250VkTrace("[VK_BRIDGE] INSPECAO LINEAR_BUF: vkMapMemory retornou %d (memoria nao host-visible ou ocupada)", res);
    }
    return nonZeroSamples;
}


