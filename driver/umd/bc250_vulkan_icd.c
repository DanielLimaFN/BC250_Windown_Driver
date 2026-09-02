/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_vulkan_icd.c

Abstract:
    Implementação do Vulkan 1.0/1.1 Installable Client Driver (ICD) para a
    APU AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2). Conecta o Khronos Vulkan
    Loader (vulkan-1.dll) à biblioteca de runtime UMD bc250_runtime.dll.

--*/

#include "bc250_vulkan.h"
#include "bc250_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BC250_VK_DRIVER_VERSION     VK_MAKE_API_VERSION(0, 3, 61, 0)
/*
 * Declaramos 1.0 porque nenhum entry point do core 1.1 existe aqui
 * (vkGetPhysicalDeviceProperties2, Features2, MemoryProperties2,
 * QueueFamilyProperties2, BindBufferMemory2 estao todos ausentes).
 * Anunciar 1.1 faz o Khronos Loader emular esses pontos e descartar
 * silenciosamente as cadeias pNext, devolvendo estruturas nao
 * inicializadas para a aplicacao.
 */
#define BC250_VK_API_VERSION        VK_API_VERSION_1_0
#define BC250_VK_VENDOR_ID          0x1002u
#define BC250_VK_DEVICE_ID          0x13FEu
#define BC250_VK_DEVICE_NAME        "AMD BC-250 (Cyan Skillfish 2 Compute)"

/*
 * Portao de bancada.  O ICD so se apresenta como driver utilizavel quando a
 * variavel de ambiente BC250_ICD_UNSAFE vale exatamente "1".  Fora disso ele
 * recusa vkCreateInstance com VK_ERROR_INCOMPATIBLE_DRIVER, que e a resposta
 * que o Khronos Loader espera de um ICD que nao pode servir o dispositivo.
 */
static int Bc250IcdUnsafeEnabled(void)
{
    char value[8];
    DWORD written = GetEnvironmentVariableA("BC250_ICD_UNSAFE", value, (DWORD)sizeof(value));

    if (written == 0u || written >= (DWORD)sizeof(value)) {
        return 0;
    }
    return (value[0] == '1' && value[1] == '\0') ? 1 : 0;
}

/*
 * Estruturas internas dos objetos Dispatchable (devem comecar com VK_LOADER_DATA)
 */
struct VkInstance_T {
    VK_LOADER_DATA loader_data;
    uint32_t magic;
    bc250_device_t *runtime_device;
    bc250_device_info_t hw_info;
};

struct VkPhysicalDevice_T {
    VK_LOADER_DATA loader_data;
    uint32_t magic;
    struct VkInstance_T *instance;
};

struct VkDevice_T {
    VK_LOADER_DATA loader_data;
    uint32_t magic;
    struct VkPhysicalDevice_T *physical_device;
    bc250_device_t *runtime_device;
};

struct VkQueue_T {
    VK_LOADER_DATA loader_data;
    uint32_t magic;
    struct VkDevice_T *device;
    uint32_t queue_family_index;
};

struct VkCommandBuffer_T {
    VK_LOADER_DATA loader_data;
    uint32_t magic;
    struct VkCommandPool_T *pool;
    struct VkPipeline_T *bound_pipeline;
    uint32_t grid_x;
    uint32_t grid_y;
    uint32_t grid_z;
};

/*
 * Estruturas internas dos objetos Non-Dispatchable
 */
struct VkDeviceMemory_T {
    uint32_t magic;
    uint64_t gpu_va;
    size_t size;
    uint32_t memory_type_index;
    void *host_mapped;
};

struct VkBuffer_T {
    uint32_t magic;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    struct VkDeviceMemory_T *bound_memory;
    VkDeviceSize memory_offset;
};

struct VkShaderModule_T {
    uint32_t magic;
    size_t code_size;
    uint32_t *code;
};

struct VkPipeline_T {
    uint32_t magic;
    struct VkShaderModule_T *shader_module;
};

struct VkCommandPool_T {
    uint32_t magic;
    struct VkDevice_T *device;
};

struct VkFence_T {
    uint32_t magic;
    uint32_t fence_value;
    uint32_t signaled;
};

static struct VkPhysicalDevice_T g_physical_device;
static struct VkQueue_T g_queue;

#define BC250_VK_MAGIC 0xBC250001u

/* ============================================================================
   Declarações Antecipadas das Funções Exportadas
   ============================================================================ */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory);
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **ppData);
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer);
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements);
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule);
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool);
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer);
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence);
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence);
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout);
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences);
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence);
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device);
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pVersion);

/* ============================================================================
   Pontos de Entrada de Negociação do Loader Khronos (vulkan-1.dll)
   ============================================================================ */

VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pVersion)
{
    if (pVersion == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    if (*pVersion > 5) *pVersion = 5; /* Negocia interface v5 do Loader */
    return VK_SUCCESS;
}

/* ============================================================================
   Implementação da API Vulkan
   ============================================================================ */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkInstance *pInstance)
{
    struct VkInstance_T *inst;
    bc250_status_t status;
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);

    if (pInstance == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    /*
     * FAIL-CLOSED (Lei 3 do CLAUDE.md — Zero Fakes).
     *
     * Este ICD ainda nao possui descritores, pipeline layout, barreiras,
     * semaforos nem tradutor de SPIR-V para ISA GFX10.  Enquanto isso for
     * verdade ele NAO pode se apresentar ao Khronos Loader como driver
     * utilizavel: um ICD que aceita vkCreateInstance e depois devolve
     * VK_SUCCESS sem backend transforma erro em resultado silenciosamente
     * corrompido.  So a bancada de laboratorio, que sabe o que esta fazendo,
     * habilita o caminho com BC250_ICD_UNSAFE=1.
     */
    if (!Bc250IcdUnsafeEnabled()) {
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    inst = (struct VkInstance_T *)malloc(sizeof(struct VkInstance_T));
    if (inst == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

    set_loader_magic_value(inst);
    inst->magic = BC250_VK_MAGIC;
    status = bc250_device_init(&inst->runtime_device, &inst->hw_info);
    if (status != BC250_SUCCESS) {
        free(inst);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    set_loader_magic_value(&g_physical_device);
    g_physical_device.magic = BC250_VK_MAGIC;
    g_physical_device.instance = inst;

    *pInstance = (VkInstance)inst;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks *pAllocator)
{
    struct VkInstance_T *inst = (struct VkInstance_T *)instance;
    UNREFERENCED_PARAMETER(pAllocator);

    if (inst != NULL) {
        if (inst->runtime_device != NULL) {
            bc250_device_close(inst->runtime_device);
        }
        free(inst);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t *pPhysicalDeviceCount,
    VkPhysicalDevice *pPhysicalDevices)
{
    UNREFERENCED_PARAMETER(instance);
    if (pPhysicalDeviceCount == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    if (pPhysicalDevices == NULL) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }

    if (*pPhysicalDeviceCount < 1) return VK_INCOMPLETE;

    *pPhysicalDeviceCount = 1;
    pPhysicalDevices[0] = (VkPhysicalDevice)&g_physical_device;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties *pProperties)
{
    struct VkPhysicalDevice_T *pDev = (struct VkPhysicalDevice_T *)physicalDevice;
    if (pProperties == NULL || pDev == NULL) return;

    memset(pProperties, 0, sizeof(*pProperties));
    pProperties->apiVersion = BC250_VK_API_VERSION;
    pProperties->driverVersion = BC250_VK_DRIVER_VERSION;
    pProperties->vendorID = BC250_VK_VENDOR_ID;
    pProperties->deviceID = BC250_VK_DEVICE_ID;
    pProperties->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    strncpy_s(pProperties->deviceName, sizeof(pProperties->deviceName), BC250_VK_DEVICE_NAME, _TRUNCATE);

    /* Limites da Arquitetura GFX10 */
    pProperties->limits.maxComputeSharedMemorySize = 65536; /* 64 KiB LDS */
    pProperties->limits.maxComputeWorkGroupCount[0] = 65535;
    pProperties->limits.maxComputeWorkGroupCount[1] = 65535;
    pProperties->limits.maxComputeWorkGroupCount[2] = 65535;
    pProperties->limits.maxComputeWorkGroupInvocations = 1024;
    pProperties->limits.maxComputeWorkGroupSize[0] = 1024;
    pProperties->limits.maxComputeWorkGroupSize[1] = 1024;
    pProperties->limits.maxComputeWorkGroupSize[2] = 1024;
    pProperties->limits.maxStorageBufferRange = 0xFFFFFFFFu;
    pProperties->limits.maxPushConstantsSize = 256;
    pProperties->limits.maxMemoryAllocationCount = 4096;
    pProperties->limits.bufferImageGranularity = 64;
    pProperties->limits.nonCoherentAtomSize = 64;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures *pFeatures)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    if (pFeatures == NULL) return;

    memset(pFeatures, 0, sizeof(*pFeatures));
    pFeatures->robustBufferAccess = VK_TRUE;
    pFeatures->shaderInt64 = VK_TRUE;
    pFeatures->shaderInt16 = VK_TRUE;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
    struct VkPhysicalDevice_T *pDev = (struct VkPhysicalDevice_T *)physicalDevice;
    uint64_t vramBytes = 16ull * 1024ull * 1024ull * 1024ull; /* 16 GB GDDR6 */
    if (pMemoryProperties == NULL) return;

    if (pDev != NULL && pDev->instance != NULL && pDev->instance->hw_info.vram_total_mib > 0) {
        vramBytes = (uint64_t)pDev->instance->hw_info.vram_total_mib * 1024ull * 1024ull;
    }

    memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));

    /* Heap 0: VRAM Unificada GDDR6 (Device Local + Host Visible via ReBAR) */
    pMemoryProperties->memoryHeapCount = 1;
    pMemoryProperties->memoryHeaps[0].size = vramBytes;
    pMemoryProperties->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

    pMemoryProperties->memoryTypeCount = 2;
    /* Tipo 0: Device Local Fast GDDR6 */
    pMemoryProperties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    pMemoryProperties->memoryTypes[0].heapIndex = 0;

    /*
     * Tipo 1: Host Visible NAO coerente.
     *
     * Lei 3 — Zero Fakes.  O bit HOST_COHERENT foi retirado porque vkMapMemory
     * nao mapeia a apertura ReBAR: ele aloca um espelho em memoria de host e
     * copia por SDMA.  Sob HOST_COHERENT a aplicacao teria o direito de
     * escrever no ponteiro mapeado e assumir que a GPU ve o dado — e o
     * llama.cpp faz exatamente isso, mantendo staging buffers mapeados de
     * forma persistente.  Sem o bit, a aplicacao e obrigada a chamar
     * vkFlushMappedMemoryRanges / vkInvalidateMappedMemoryRanges, que agora
     * existem e fazem a copia de verdade.
     */
    pMemoryProperties->memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    pMemoryProperties->memoryTypes[1].heapIndex = 0;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    if (pQueueFamilyPropertyCount == NULL) return;

    if (pQueueFamilyProperties == NULL) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }

    if (*pQueueFamilyPropertyCount < 1) return;

    *pQueueFamilyPropertyCount = 1;
    memset(pQueueFamilyProperties, 0, sizeof(*pQueueFamilyProperties));
    pQueueFamilyProperties[0].queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueCount = 1;
    pQueueFamilyProperties[0].timestampValidBits = 64;
    pQueueFamilyProperties[0].minImageTransferGranularity.width = 1;
    pQueueFamilyProperties[0].minImageTransferGranularity.height = 1;
    pQueueFamilyProperties[0].minImageTransferGranularity.depth = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties *pFormatProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(format);
    if (pFormatProperties == NULL) return;
    memset(pFormatProperties, 0, sizeof(*pFormatProperties));
    pFormatProperties->linearTilingFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
    pFormatProperties->optimalTilingFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
    pFormatProperties->bufferFeatures = VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(format);
    UNREFERENCED_PARAMETER(type);
    UNREFERENCED_PARAMETER(tiling);
    UNREFERENCED_PARAMETER(usage);
    UNREFERENCED_PARAMETER(flags);
    if (pImageFormatProperties == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pImageFormatProperties, 0, sizeof(*pImageFormatProperties));
    pImageFormatProperties->maxExtent.width = 4096;
    pImageFormatProperties->maxExtent.height = 4096;
    pImageFormatProperties->maxExtent.depth = 4096;
    pImageFormatProperties->maxMipLevels = 1;
    pImageFormatProperties->maxArrayLayers = 1;
    pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    pImageFormatProperties->maxResourceSize = 0x10000000;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkSampleCountFlags samples,
    VkImageUsageFlags usage,
    VkImageTiling tiling,
    uint32_t *pPropertyCount,
    VkSparseImageFormatProperties *pProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(format);
    UNREFERENCED_PARAMETER(type);
    UNREFERENCED_PARAMETER(samples);
    UNREFERENCED_PARAMETER(usage);
    UNREFERENCED_PARAMETER(tiling);
    UNREFERENCED_PARAMETER(pProperties);
    if (pPropertyCount != NULL) *pPropertyCount = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    VkSurfaceKHR surface,
    VkBool32 *pSupported)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(queueFamilyIndex);
    UNREFERENCED_PARAMETER(surface);
    if (pSupported != NULL) {
        *pSupported = VK_FALSE; /* Headless Compute-Only Device */
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDevice *pDevice)
{
    struct VkPhysicalDevice_T *pDev = (struct VkPhysicalDevice_T *)physicalDevice;
    struct VkDevice_T *dev;
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);

    if (pDevice == NULL || pDev == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    dev = (struct VkDevice_T *)malloc(sizeof(struct VkDevice_T));
    if (dev == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

    set_loader_magic_value(dev);
    dev->magic = BC250_VK_MAGIC;
    dev->physical_device = pDev;
    dev->runtime_device = pDev->instance->runtime_device;

    set_loader_magic_value(&g_queue);
    g_queue.magic = BC250_VK_MAGIC;
    g_queue.device = dev;
    g_queue.queue_family_index = 0;

    *pDevice = (VkDevice)dev;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks *pAllocator)
{
    struct VkDevice_T *dev = (struct VkDevice_T *)device;
    UNREFERENCED_PARAMETER(pAllocator);
    if (dev != NULL) {
        free(dev);
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue *pQueue)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(queueFamilyIndex);
    UNREFERENCED_PARAMETER(queueIndex);
    if (pQueue != NULL) {
        *pQueue = (VkQueue)&g_queue;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo *pAllocateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDeviceMemory *pMemory)
{
    struct VkDevice_T *dev = (struct VkDevice_T *)device;
    struct VkDeviceMemory_T *mem;
    bc250_status_t status;
    UNREFERENCED_PARAMETER(pAllocator);

    if (dev == NULL || pAllocateInfo == NULL || pMemory == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    mem = (struct VkDeviceMemory_T *)malloc(sizeof(struct VkDeviceMemory_T));
    if (mem == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

    mem->magic = BC250_VK_MAGIC;
    mem->size = (size_t)pAllocateInfo->allocationSize;
    mem->memory_type_index = pAllocateInfo->memoryTypeIndex;
    mem->host_mapped = NULL;

    status = bc250_mem_alloc(dev->runtime_device, mem->size, &mem->gpu_va);
    if (status != BC250_SUCCESS) {
        free(mem);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    *pMemory = (VkDeviceMemory)(uintptr_t)mem;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const VkAllocationCallbacks *pAllocator)
{
    struct VkDevice_T *dev = (struct VkDevice_T *)device;
    struct VkDeviceMemory_T *mem = (struct VkDeviceMemory_T *)(uintptr_t)memory;
    UNREFERENCED_PARAMETER(pAllocator);

    if (dev != NULL && mem != NULL) {
        if (mem->gpu_va != 0) {
            bc250_mem_free(dev->runtime_device, mem->gpu_va, mem->size);
        }
        if (mem->host_mapped != NULL) {
            free(mem->host_mapped);
        }
        free(mem);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkMemoryMapFlags flags,
    void **ppData)
{
    struct VkDevice_T *dev = (struct VkDevice_T *)device;
    struct VkDeviceMemory_T *mem = (struct VkDeviceMemory_T *)(uintptr_t)memory;
    UNREFERENCED_PARAMETER(flags);

    if (dev == NULL || mem == NULL || ppData == NULL) return VK_ERROR_MEMORY_MAP_FAILED;

    if (mem->host_mapped == NULL) {
        mem->host_mapped = malloc(mem->size);
        if (mem->host_mapped == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;
        (void)bc250_mem_copy_d2h(dev->runtime_device, mem->host_mapped, mem->gpu_va, mem->size);
    }

    *ppData = (void *)((uintptr_t)mem->host_mapped + (uintptr_t)offset);
    (void)size;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice device,
    VkDeviceMemory memory)
{
    struct VkDevice_T *dev = (struct VkDevice_T *)device;
    struct VkDeviceMemory_T *mem = (struct VkDeviceMemory_T *)(uintptr_t)memory;

    if (dev != NULL && mem != NULL && mem->host_mapped != NULL) {
        (void)bc250_mem_copy_h2d(dev->runtime_device, mem->gpu_va, mem->host_mapped, mem->size);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device,
    const VkBufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkBuffer *pBuffer)
{
    struct VkBuffer_T *buf;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);

    if (pCreateInfo == NULL || pBuffer == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    buf = (struct VkBuffer_T *)malloc(sizeof(struct VkBuffer_T));
    if (buf == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

    buf->magic = BC250_VK_MAGIC;
    buf->size = pCreateInfo->size;
    buf->usage = pCreateInfo->usage;
    buf->bound_memory = NULL;
    buf->memory_offset = 0;

    *pBuffer = (VkBuffer)(uintptr_t)buf;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice device,
    VkBuffer buffer,
    const VkAllocationCallbacks *pAllocator)
{
    struct VkBuffer_T *buf = (struct VkBuffer_T *)(uintptr_t)buffer;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (buf != NULL) {
        free(buf);
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice device,
    VkBuffer buffer,
    VkMemoryRequirements *pMemoryRequirements)
{
    struct VkBuffer_T *buf = (struct VkBuffer_T *)(uintptr_t)buffer;
    UNREFERENCED_PARAMETER(device);
    if (pMemoryRequirements == NULL || buf == NULL) return;

    memset(pMemoryRequirements, 0, sizeof(*pMemoryRequirements));
    pMemoryRequirements->size = (buf->size + 4095) & ~4095ull;
    pMemoryRequirements->alignment = 4096;
    pMemoryRequirements->memoryTypeBits = 0x3; /* Suporta tipo 0 e 1 */
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice device,
    VkBuffer buffer,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset)
{
    struct VkBuffer_T *buf = (struct VkBuffer_T *)(uintptr_t)buffer;
    struct VkDeviceMemory_T *mem = (struct VkDeviceMemory_T *)(uintptr_t)memory;
    UNREFERENCED_PARAMETER(device);

    if (buf == NULL || mem == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    buf->bound_memory = mem;
    buf->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice device,
    const VkShaderModuleCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkShaderModule *pShaderModule)
{
    struct VkShaderModule_T *sm;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);

    if (pCreateInfo == NULL || pShaderModule == NULL || pCreateInfo->pCode == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    sm = (struct VkShaderModule_T *)malloc(sizeof(struct VkShaderModule_T));
    if (sm == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

    sm->magic = BC250_VK_MAGIC;
    sm->code_size = pCreateInfo->codeSize;
    sm->code = (uint32_t *)malloc(sm->code_size);
    if (sm->code == NULL) {
        free(sm);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(sm->code, pCreateInfo->pCode, sm->code_size);

    *pShaderModule = (VkShaderModule)(uintptr_t)sm;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice device,
    VkShaderModule shaderModule,
    const VkAllocationCallbacks *pAllocator)
{
    struct VkShaderModule_T *sm = (struct VkShaderModule_T *)(uintptr_t)shaderModule;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (sm != NULL) {
        if (sm->code != NULL) free(sm->code);
        free(sm);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkComputePipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator,
    VkPipeline *pPipelines)
{
    uint32_t i;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pipelineCache);
    UNREFERENCED_PARAMETER(pAllocator);

    if (pCreateInfos == NULL || pPipelines == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    for (i = 0; i < createInfoCount; ++i) {
        struct VkPipeline_T *pipe = (struct VkPipeline_T *)malloc(sizeof(struct VkPipeline_T));
        if (pipe == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

        pipe->magic = BC250_VK_MAGIC;
        pipe->shader_module = (struct VkShaderModule_T *)(uintptr_t)pCreateInfos[i].stage.module;
        pPipelines[i] = (VkPipeline)(uintptr_t)pipe;
    }

    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice device,
    VkPipeline pipeline,
    const VkAllocationCallbacks *pAllocator)
{
    struct VkPipeline_T *pipe = (struct VkPipeline_T *)(uintptr_t)pipeline;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (pipe != NULL) {
        free(pipe);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device,
    const VkCommandPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkCommandPool *pCommandPool)
{
    struct VkDevice_T *dev = (struct VkDevice_T *)device;
    struct VkCommandPool_T *pool;
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);

    if (dev == NULL || pCommandPool == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    pool = (struct VkCommandPool_T *)malloc(sizeof(struct VkCommandPool_T));
    if (pool == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

    pool->magic = BC250_VK_MAGIC;
    pool->device = dev;

    *pCommandPool = (VkCommandPool)(uintptr_t)pool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    const VkAllocationCallbacks *pAllocator)
{
    struct VkCommandPool_T *pool = (struct VkCommandPool_T *)(uintptr_t)commandPool;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (pool != NULL) {
        free(pool);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo *pAllocateInfo,
    VkCommandBuffer *pCommandBuffers)
{
    uint32_t i;
    UNREFERENCED_PARAMETER(device);

    if (pAllocateInfo == NULL || pCommandBuffers == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    for (i = 0; i < pAllocateInfo->commandBufferCount; ++i) {
        struct VkCommandBuffer_T *cb = (struct VkCommandBuffer_T *)malloc(sizeof(struct VkCommandBuffer_T));
        if (cb == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

        set_loader_magic_value(cb);
        cb->magic = BC250_VK_MAGIC;
        cb->pool = (struct VkCommandPool_T *)(uintptr_t)pAllocateInfo->commandPool;
        cb->bound_pipeline = NULL;
        cb->grid_x = 1;
        cb->grid_y = 1;
        cb->grid_z = 1;

        pCommandBuffers[i] = (VkCommandBuffer)cb;
    }

    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers)
{
    uint32_t i;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(commandPool);

    if (pCommandBuffers != NULL) {
        for (i = 0; i < commandBufferCount; ++i) {
            free((void *)pCommandBuffers[i]);
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo *pBeginInfo)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(pBeginInfo);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer commandBuffer)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipeline pipeline)
{
    struct VkCommandBuffer_T *cb = (struct VkCommandBuffer_T *)commandBuffer;
    UNREFERENCED_PARAMETER(pipelineBindPoint);
    if (cb != NULL) {
        cb->bound_pipeline = (struct VkPipeline_T *)(uintptr_t)pipeline;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(
    VkCommandBuffer commandBuffer,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ)
{
    struct VkCommandBuffer_T *cb = (struct VkCommandBuffer_T *)commandBuffer;
    if (cb != NULL) {
        cb->grid_x = groupCountX;
        cb->grid_y = groupCountY;
        cb->grid_z = groupCountZ;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo *pSubmits,
    VkFence fence)
{
    UNREFERENCED_PARAMETER(queue);
    UNREFERENCED_PARAMETER(fence);

    if (pSubmits == NULL && submitCount > 0) return VK_ERROR_INITIALIZATION_FAILED;

    /*
     * REMOVIDO (Lei 3 do CLAUDE.md — Zero Fakes).
     *
     * A versao anterior desta funcao entregava as palavras SPIR-V do
     * VkShaderModule diretamente a bc250_dispatch_kernel como se fossem ISA
     * GFX10, sem vincular descritor nenhum (os tres ultimos argumentos eram
     * NULL).  Isso executava bytecode que nao e codigo de maquina no CP e,
     * pior, marcava a fence como sinalizada pela CPU logo depois — um duplo
     * fake que fazia qualquer aplicacao acreditar em resultados que nunca
     * foram calculados.
     *
     * Enquanto nao existir (a) tradutor SPIR-V -> ISA GFX10 e (b) camada de
     * descritores, a unica resposta honesta e recusar a submissao.  A fence
     * NAO e sinalizada: nada foi executado.
     */
    return VK_ERROR_DEVICE_LOST;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue)
{
    UNREFERENCED_PARAMETER(queue);
    /*
     * Lei 3 — Zero Fakes.  Nenhuma submissao e aceita hoje (vkQueueSubmit
     * devolve VK_ERROR_DEVICE_LOST), portanto nao existe trabalho pendente
     * para drenar.  Uma fila que nunca aceitou trabalho esta legitimamente
     * ociosa; o que seria fake e sinalizar drenagem de trabalho submetido.
     */
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{
    UNREFERENCED_PARAMETER(device);
    return VK_SUCCESS;
}

/*
 * Manutencao explicita do espelho de host criado por vkMapMemory.  Estas duas
 * funcoes sao o que torna o tipo de memoria 1 honestamente NAO coerente.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange *pMemoryRanges)
{
    struct VkDevice_T *dev = (struct VkDevice_T *)device;
    uint32_t i;

    if (dev == NULL) return VK_ERROR_DEVICE_LOST;
    if (pMemoryRanges == NULL || memoryRangeCount == 0u) return VK_SUCCESS;

    for (i = 0; i < memoryRangeCount; ++i) {
        struct VkDeviceMemory_T *mem =
            (struct VkDeviceMemory_T *)(uintptr_t)pMemoryRanges[i].memory;
        size_t offset;
        size_t bytes;

        if (mem == NULL || mem->host_mapped == NULL) continue;

        offset = (size_t)pMemoryRanges[i].offset;
        if (offset >= mem->size) continue;
        bytes = (pMemoryRanges[i].size == VK_WHOLE_SIZE)
            ? (mem->size - offset)
            : (size_t)pMemoryRanges[i].size;
        if (bytes > mem->size - offset) bytes = mem->size - offset;
        if (bytes == 0u) continue;

        if (bc250_mem_copy_h2d(dev->runtime_device, mem->gpu_va + offset,
                (const void *)((uintptr_t)mem->host_mapped + offset),
                bytes) != BC250_SUCCESS) {
            return VK_ERROR_DEVICE_LOST;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange *pMemoryRanges)
{
    struct VkDevice_T *dev = (struct VkDevice_T *)device;
    uint32_t i;

    if (dev == NULL) return VK_ERROR_DEVICE_LOST;
    if (pMemoryRanges == NULL || memoryRangeCount == 0u) return VK_SUCCESS;

    for (i = 0; i < memoryRangeCount; ++i) {
        struct VkDeviceMemory_T *mem =
            (struct VkDeviceMemory_T *)(uintptr_t)pMemoryRanges[i].memory;
        size_t offset;
        size_t bytes;

        if (mem == NULL || mem->host_mapped == NULL) continue;

        offset = (size_t)pMemoryRanges[i].offset;
        if (offset >= mem->size) continue;
        bytes = (pMemoryRanges[i].size == VK_WHOLE_SIZE)
            ? (mem->size - offset)
            : (size_t)pMemoryRanges[i].size;
        if (bytes > mem->size - offset) bytes = mem->size - offset;
        if (bytes == 0u) continue;

        if (bc250_mem_copy_d2h(dev->runtime_device,
                (void *)((uintptr_t)mem->host_mapped + offset),
                mem->gpu_va + offset, bytes) != BC250_SUCCESS) {
            return VK_ERROR_DEVICE_LOST;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice device,
    const VkFenceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkFence *pFence)
{
    struct VkFence_T *f;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);

    if (pFence == NULL) return VK_ERROR_INITIALIZATION_FAILED;

    f = (struct VkFence_T *)malloc(sizeof(struct VkFence_T));
    if (f == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;

    f->magic = BC250_VK_MAGIC;
    f->signaled = (pCreateInfo != NULL && (pCreateInfo->flags & 1)) ? 1 : 0;
    f->fence_value = 0;

    *pFence = (VkFence)(uintptr_t)f;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice device,
    VkFence fence,
    const VkAllocationCallbacks *pAllocator)
{
    struct VkFence_T *f = (struct VkFence_T *)(uintptr_t)fence;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (f != NULL) free(f);
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence *pFences,
    VkBool32 waitAll,
    uint64_t timeout)
{
    uint32_t i;
    uint32_t signaled = 0u;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(timeout);

    /*
     * Lei 3 — Zero Fakes.  A versao anterior devolvia VK_SUCCESS sem olhar
     * fence nenhuma.  Agora a resposta reflete o estado real dos objetos: se
     * a fence nao foi sinalizada por uma submissao concluida, o resultado e
     * VK_TIMEOUT.  Como vkQueueSubmit nao executa nada hoje, o unico caminho
     * para VK_SUCCESS aqui e uma fence criada com VK_FENCE_CREATE_SIGNALED.
     */
    if (pFences == NULL || fenceCount == 0u) {
        return VK_SUCCESS;
    }
    for (i = 0; i < fenceCount; ++i) {
        struct VkFence_T *f = (struct VkFence_T *)(uintptr_t)pFences[i];
        if (f != NULL && f->signaled != 0) {
            ++signaled;
        }
    }
    if (waitAll != VK_FALSE) {
        return (signaled == fenceCount) ? VK_SUCCESS : VK_TIMEOUT;
    }
    return (signaled > 0u) ? VK_SUCCESS : VK_TIMEOUT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(
    VkDevice device,
    VkFence fence)
{
    struct VkFence_T *f = (struct VkFence_T *)(uintptr_t)fence;
    UNREFERENCED_PARAMETER(device);

    if (f == NULL) return VK_ERROR_DEVICE_LOST;
    return (f->signaled != 0) ? VK_SUCCESS : VK_NOT_READY;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence *pFences)
{
    uint32_t i;
    UNREFERENCED_PARAMETER(device);

    if (pFences != NULL) {
        for (i = 0; i < fenceCount; ++i) {
            struct VkFence_T *f = (struct VkFence_T *)(uintptr_t)pFences[i];
            if (f != NULL) f->signaled = 0;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    UNREFERENCED_PARAMETER(pLayerName);
    UNREFERENCED_PARAMETER(pProperties);
    if (pPropertyCount == NULL) return VK_INCOMPLETE;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties)
{
    UNREFERENCED_PARAMETER(pProperties);
    if (pPropertyCount == NULL) return VK_INCOMPLETE;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(
    uint32_t *pApiVersion)
{
    if (pApiVersion == NULL) return VK_INCOMPLETE;
    *pApiVersion = VK_API_VERSION_1_1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(pLayerName);
    UNREFERENCED_PARAMETER(pProperties);
    if (pPropertyCount == NULL) return VK_INCOMPLETE;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(pProperties);
    if (pPropertyCount == NULL) return VK_INCOMPLETE;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

/* ============================================================================
   Tabela de Despacho (vkGetInstanceProcAddr / vkGetDeviceProcAddr)
   ============================================================================ */

#define DISPATCH_ENTRY(func) if (strcmp(pName, #func) == 0) return (PFN_vkVoidFunction)func;

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
    UNREFERENCED_PARAMETER(device);
    if (pName == NULL) return NULL;

    DISPATCH_ENTRY(vkAllocateMemory);
    DISPATCH_ENTRY(vkFreeMemory);
    DISPATCH_ENTRY(vkMapMemory);
    DISPATCH_ENTRY(vkUnmapMemory);
    DISPATCH_ENTRY(vkCreateBuffer);
    DISPATCH_ENTRY(vkDestroyBuffer);
    DISPATCH_ENTRY(vkGetBufferMemoryRequirements);
    DISPATCH_ENTRY(vkBindBufferMemory);
    DISPATCH_ENTRY(vkCreateShaderModule);
    DISPATCH_ENTRY(vkDestroyShaderModule);
    DISPATCH_ENTRY(vkCreateComputePipelines);
    DISPATCH_ENTRY(vkDestroyPipeline);
    DISPATCH_ENTRY(vkCreateCommandPool);
    DISPATCH_ENTRY(vkDestroyCommandPool);
    DISPATCH_ENTRY(vkAllocateCommandBuffers);
    DISPATCH_ENTRY(vkFreeCommandBuffers);
    DISPATCH_ENTRY(vkBeginCommandBuffer);
    DISPATCH_ENTRY(vkEndCommandBuffer);
    DISPATCH_ENTRY(vkCmdBindPipeline);
    DISPATCH_ENTRY(vkCmdDispatch);
    DISPATCH_ENTRY(vkQueueSubmit);
    DISPATCH_ENTRY(vkQueueWaitIdle);
    DISPATCH_ENTRY(vkCreateFence);
    DISPATCH_ENTRY(vkDestroyFence);
    DISPATCH_ENTRY(vkWaitForFences);
    DISPATCH_ENTRY(vkResetFences);
    DISPATCH_ENTRY(vkGetFenceStatus);
    DISPATCH_ENTRY(vkDeviceWaitIdle);
    DISPATCH_ENTRY(vkFlushMappedMemoryRanges);
    DISPATCH_ENTRY(vkInvalidateMappedMemoryRanges);
    DISPATCH_ENTRY(vkEnumerateDeviceExtensionProperties);
    DISPATCH_ENTRY(vkEnumerateDeviceLayerProperties);

    return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    UNREFERENCED_PARAMETER(instance);
    if (pName == NULL) return NULL;

    DISPATCH_ENTRY(vkCreateInstance);
    DISPATCH_ENTRY(vkDestroyInstance);
    DISPATCH_ENTRY(vkEnumeratePhysicalDevices);
    DISPATCH_ENTRY(vkGetPhysicalDeviceProperties);
    DISPATCH_ENTRY(vkGetPhysicalDeviceFeatures);
    DISPATCH_ENTRY(vkGetPhysicalDeviceMemoryProperties);
    DISPATCH_ENTRY(vkGetPhysicalDeviceQueueFamilyProperties);
    DISPATCH_ENTRY(vkGetPhysicalDeviceFormatProperties);
    DISPATCH_ENTRY(vkGetPhysicalDeviceImageFormatProperties);
    DISPATCH_ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties);
    DISPATCH_ENTRY(vkGetPhysicalDeviceSurfaceSupportKHR);
    DISPATCH_ENTRY(vkCreateDevice);
    DISPATCH_ENTRY(vkDestroyDevice);
    DISPATCH_ENTRY(vkGetDeviceQueue);
    DISPATCH_ENTRY(vkGetInstanceProcAddr);
    DISPATCH_ENTRY(vkGetDeviceProcAddr);
    DISPATCH_ENTRY(vkEnumerateInstanceExtensionProperties);
    DISPATCH_ENTRY(vkEnumerateInstanceLayerProperties);
    DISPATCH_ENTRY(vkEnumerateInstanceVersion);
    DISPATCH_ENTRY(vkEnumerateDeviceExtensionProperties);
    DISPATCH_ENTRY(vkEnumerateDeviceLayerProperties);
    DISPATCH_ENTRY(vk_icdGetInstanceProcAddr);
    DISPATCH_ENTRY(vk_icdGetPhysicalDeviceProcAddr);
    DISPATCH_ENTRY(vk_icdNegotiateLoaderICDInterfaceVersion);

    return vkGetDeviceProcAddr(NULL, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    return vkGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName)
{
    return vkGetInstanceProcAddr(instance, pName);
}
