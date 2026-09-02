/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_vulkan.h

Abstract:
    ABI Vulkan 1.0 para o ICD da BC-250.

    ATENCAO SOBRE ABI: um ICD conversa com o loader (vulkan-1.dll) e com
    aplicativos compilados contra o vulkan.h OFICIAL. Cada struct aqui precisa
    ter layout binario IDENTICO ao oficial -- mesma ordem de campos, mesmos
    tipos, mesmo alinhamento. Nao ha SDK Vulkan nesta maquina, entao a ABI foi
    escrita a mao a partir da especificacao.

    A versao anterior deste arquivo declarava, por exemplo,
    "uint32_t limits[128]" no lugar de VkPhysicalDeviceLimits. O tamanho total
    de VkPhysicalDeviceProperties dava 824 bytes por coincidencia, mas limits
    comecava no offset 292 em vez de 296 (o oficial alinha a 8 por conter
    VkDeviceSize) e tinha 512 bytes em vez de 504 -- deixando
    sparseProperties 16 bytes fora de lugar, e limits sempre zerado.

    Verificacao: os _STATIC_ASSERT no fim do arquivo travam os tamanhos, e o
    vulkaninfo imprime todos os limites -- layout errado aparece como valor
    absurdo na tela.

--*/

#ifndef _BC250_VULKAN_H_
#define _BC250_VULKAN_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#define VKAPI_ATTR __declspec(dllexport)
#define VKAPI_CALL __stdcall
#define VKAPI_PTR  __stdcall

#define VK_MAKE_API_VERSION(v,ma,mi,pa) \
    ((((uint32_t)(v)) << 29) | (((uint32_t)(ma)) << 22) | (((uint32_t)(mi)) << 12) | ((uint32_t)(pa)))
#define VK_API_VERSION_1_0 VK_MAKE_API_VERSION(0,1,0,0)
#define VK_API_VERSION_1_1 VK_MAKE_API_VERSION(0,1,1,0)

#define VK_NULL_HANDLE 0

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
#define VK_TRUE 1
#define VK_FALSE 0

typedef VkFlags VkBufferUsageFlags;
typedef VkFlags VkMemoryMapFlags;
typedef VkFlags VkBufferCreateFlags;
typedef VkFlags VkDeviceCreateFlags;
typedef VkFlags VkInstanceCreateFlags;
typedef VkFlags VkCommandPoolCreateFlags;
typedef VkFlags VkCommandBufferUsageFlags;
typedef VkFlags VkFenceCreateFlags;
typedef VkFlags VkPipelineCreateFlags;
typedef VkFlags VkShaderModuleCreateFlags;
typedef VkFlags VkImageCreateFlags;
typedef uint32_t VkFormat;
typedef uint32_t VkImageType;
typedef uint32_t VkImageTiling;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkSampleCountFlags;
typedef uint32_t VkQueueFlags;
typedef uint32_t VkMemoryPropertyFlags;
typedef uint32_t VkMemoryHeapFlags;
typedef uint32_t VkFormatFeatureFlags;
#define VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT         0x00000001
#define VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT         0x00000002
#define VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT  0x00000004
#define VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT  0x00000008
#define VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT  0x00000010

typedef uint32_t VkImageUsageFlags;
typedef uint32_t VkShaderStageFlags;
#define VK_SHADER_STAGE_VERTEX_BIT                  0x00000001
#define VK_SHADER_STAGE_FRAGMENT_BIT                0x00000010
#define VK_SHADER_STAGE_COMPUTE_BIT                 0x00000020
#define VK_SHADER_STAGE_ALL_GRAPHICS                0x0000001F
#define VK_SHADER_STAGE_ALL                         0x7FFFFFFF

typedef int32_t VkResult;
#define VK_SUCCESS                        0
#define VK_NOT_READY                      1
#define VK_TIMEOUT                        2
#define VK_EVENT_SET                      3
#define VK_EVENT_RESET                    4
#define VK_INCOMPLETE                     5
#define VK_ERROR_OUT_OF_HOST_MEMORY      (-1)
#define VK_ERROR_OUT_OF_DEVICE_MEMORY    (-2)
#define VK_ERROR_INITIALIZATION_FAILED   (-3)
#define VK_ERROR_DEVICE_LOST             (-4)
#define VK_ERROR_MEMORY_MAP_FAILED       (-5)
#define VK_ERROR_LAYER_NOT_PRESENT       (-6)
#define VK_ERROR_EXTENSION_NOT_PRESENT   (-7)
#define VK_ERROR_FEATURE_NOT_PRESENT     (-8)
#define VK_ERROR_INCOMPATIBLE_DRIVER     (-9)
#define VK_ERROR_TOO_MANY_OBJECTS        (-10)
#define VK_ERROR_FORMAT_NOT_SUPPORTED    (-11)
#define VK_ERROR_FRAGMENTED_POOL         (-12)
#define VK_ERROR_UNKNOWN                 (-13)
#define VK_ERROR_OUT_OF_POOL_MEMORY      (-1000069000)
#define VK_ERROR_INVALID_SHADER_NV       (-1000012000)

/* sType. Valores conferidos contra a especificacao: SHADER_MODULE_CREATE_INFO
   e 16, nao 15 (15 e IMAGE_VIEW_CREATE_INFO) -- erro presente na versao
   anterior deste header. */
#define VK_STRUCTURE_TYPE_APPLICATION_INFO                         0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO                     1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO                 2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO                       3
#define VK_STRUCTURE_TYPE_SUBMIT_INFO                              4
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO                     5
#define VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE                      6
#define VK_STRUCTURE_TYPE_FENCE_CREATE_INFO                        8
#define VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO                    9
#define VK_STRUCTURE_TYPE_EVENT_CREATE_INFO                       10
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO                      12
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO               16
#define VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO              17
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO       18
#define VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO            29
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO             30
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO       32
#define VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO             33
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO            34
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET                    35
#define VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET                     36
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO                39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO            40
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES     49
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES   50
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES     51
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES   52
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2              1000059000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2            1000059001
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2     1000059006
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES  1000083000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES   1000177000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES 1000082000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES     1000094000
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO                       14
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO                  15
#define VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO           28
#define VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO                     31
#define VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO                 37
#define VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO                 38
#define VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2       1000146000
#define VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2                   1000146003
#define VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO                 1000157000
#define VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR               1000001000
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR                        1000001001
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT  1000212000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT 1000237000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES           1000071000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR       1000071000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES       1000196000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR   1000196000

#define VK_DRIVER_ID_AMD_PROPRIETARY                              2

#define VK_COLOR_SPACE_SRGB_NONLINEAR_KHR                         0
#define VK_PRESENT_MODE_IMMEDIATE_KHR                             0
#define VK_PRESENT_MODE_MAILBOX_KHR                               1
#define VK_PRESENT_MODE_FIFO_KHR                                  2
#define VK_PRESENT_MODE_FIFO_RELAXED_KHR                          3

#define VK_FORMAT_R8G8B8A8_UNORM                                  37
#define VK_FORMAT_R8G8B8A8_SRGB                                   43
#define VK_FORMAT_B8G8R8A8_UNORM                                  44
#define VK_FORMAT_B8G8R8A8_SRGB                                   50
#define VK_IMAGE_LAYOUT_UNDEFINED                                 0
#define VK_IMAGE_LAYOUT_GENERAL                                   1
#define VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL                  2
#define VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL          3
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL                  5
#define VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL                      6
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL                      7
#define VK_IMAGE_LAYOUT_PRESENT_SRC_KHR                           1000001002

#define VK_PHYSICAL_DEVICE_TYPE_OTHER          0
#define VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU 1
#define VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   2

#define VK_QUEUE_GRAPHICS_BIT 0x1
#define VK_QUEUE_COMPUTE_BIT  0x2
#define VK_QUEUE_TRANSFER_BIT 0x4

#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT     0x1
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT     0x2
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT    0x4
#define VK_MEMORY_PROPERTY_HOST_CACHED_BIT      0x8
#define VK_MEMORY_HEAP_DEVICE_LOCAL_BIT         0x1

#define VK_SAMPLE_COUNT_1_BIT 0x1

#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER         7
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC 9
#define VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER         6

#define VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
#define VK_MAX_EXTENSION_NAME_SIZE       256
#define VK_MAX_DESCRIPTION_SIZE          256
#define VK_UUID_SIZE                     16
#define VK_WHOLE_SIZE                    (~0ULL)

/* Handles dispatchable (VkInstance, VkPhysicalDevice, VkDevice, VkQueue,
   VkCommandBuffer) precisam ser alocacoes REAIS cujo primeiro campo do
   tamanho de um ponteiro pertence ao loader. O loader escreve a propria
   dispatch table ali e valida o valor magico. Handles falsos do tipo
   (void*)0xBC250003 -- como na versao anterior -- fazem o loader escrever
   num endereco invalido. */
#define ICD_LOADER_MAGIC 0x01CDC0DE
typedef union VK_LOADER_DATA {
    uintptr_t loaderMagic;
    void*     loaderData;
} VK_LOADER_DATA;

static __inline void set_loader_magic_value(void* pNewObject)
{
    ((VK_LOADER_DATA*)pNewObject)->loaderMagic = ICD_LOADER_MAGIC;
}

typedef struct VkInstance_T*        VkInstance;
typedef struct VkPhysicalDevice_T*  VkPhysicalDevice;
typedef struct VkDevice_T*          VkDevice;
typedef struct VkQueue_T*           VkQueue;
typedef struct VkCommandBuffer_T*   VkCommandBuffer;

/* Handles nao-dispatchable: opacos de 64 bits em qualquer plataforma. */
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkBuffer;
typedef uint64_t VkImage;
typedef uint64_t VkFence;
typedef uint64_t VkSemaphore;
typedef uint64_t VkEvent;
typedef uint64_t VkShaderModule;
typedef uint64_t VkPipeline;
typedef uint64_t VkPipelineLayout;
typedef uint64_t VkPipelineCache;
typedef uint64_t VkCommandPool;
typedef uint64_t VkDescriptorSetLayout;
typedef uint64_t VkDescriptorPool;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkSampler;
typedef uint64_t VkBufferView;
typedef uint64_t VkImageView;
typedef uint64_t VkRenderPass;
typedef uint64_t VkFramebuffer;
typedef uint64_t VkQueryPool;
typedef uint64_t VkSurfaceKHR;
typedef uint64_t VkSwapchainKHR;

typedef void (VKAPI_PTR *PFN_vkVoidFunction)(void);

typedef enum VkSystemAllocationScope {
    VK_SYSTEM_ALLOCATION_SCOPE_COMMAND = 0,
    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT = 1,
    VK_SYSTEM_ALLOCATION_SCOPE_CACHE = 2,
    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE = 3,
    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE = 4
} VkSystemAllocationScope;

typedef enum VkInternalAllocationType {
    VK_INTERNAL_ALLOCATION_TYPE_EXECUTABLE = 0
} VkInternalAllocationType;

typedef void* (VKAPI_PTR *PFN_vkAllocationFunction)(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope);
typedef void* (VKAPI_PTR *PFN_vkReallocationFunction)(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope);
typedef void (VKAPI_PTR *PFN_vkFreeFunction)(void* pUserData, void* pMemory);
typedef void (VKAPI_PTR *PFN_vkInternalAllocationNotification)(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope);
typedef void (VKAPI_PTR *PFN_vkInternalFreeNotification)(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope);

typedef struct VkAllocationCallbacks {
    void*                                   pUserData;
    PFN_vkAllocationFunction                pfnAllocation;
    PFN_vkReallocationFunction              pfnReallocation;
    PFN_vkFreeFunction                      pfnFree;
    PFN_vkInternalAllocationNotification    pfnInternalAllocation;
    PFN_vkInternalFreeNotification          pfnInternalFree;
} VkAllocationCallbacks;

typedef enum VkPipelineBindPoint {
    VK_PIPELINE_BIND_POINT_GRAPHICS = 0,
    VK_PIPELINE_BIND_POINT_COMPUTE = 1,
    VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR = 1000165000
} VkPipelineBindPoint;

typedef struct VkApplicationInfo {
    uint32_t    sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t    applicationVersion;
    const char* pEngineName;
    uint32_t    engineVersion;
    uint32_t    apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    uint32_t                  sType;
    const void*               pNext;
    VkFlags                   flags;
    const VkApplicationInfo*  pApplicationInfo;
    uint32_t                  enabledLayerCount;
    const char* const*        ppEnabledLayerNames;
    uint32_t                  enabledExtensionCount;
    const char* const*        ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkExtensionProperties {
    char     extensionName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
} VkExtensionProperties;

typedef struct VkLayerProperties {
    char     layerName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
    uint32_t implementationVersion;
    char     description[VK_MAX_DESCRIPTION_SIZE];
} VkLayerProperties;

/* Ordem e tipos exatamente como na especificacao. minMemoryMapAlignment e
   size_t (8 bytes em x64), os quatro *OffsetAlignment sao VkDeviceSize. */
typedef struct VkPhysicalDeviceLimits {
    uint32_t           maxImageDimension1D;
    uint32_t           maxImageDimension2D;
    uint32_t           maxImageDimension3D;
    uint32_t           maxImageDimensionCube;
    uint32_t           maxImageArrayLayers;
    uint32_t           maxTexelBufferElements;
    uint32_t           maxUniformBufferRange;
    uint32_t           maxStorageBufferRange;
    uint32_t           maxPushConstantsSize;
    uint32_t           maxMemoryAllocationCount;
    uint32_t           maxSamplerAllocationCount;
    VkDeviceSize       bufferImageGranularity;
    VkDeviceSize       sparseAddressSpaceSize;
    uint32_t           maxBoundDescriptorSets;
    uint32_t           maxPerStageDescriptorSamplers;
    uint32_t           maxPerStageDescriptorUniformBuffers;
    uint32_t           maxPerStageDescriptorStorageBuffers;
    uint32_t           maxPerStageDescriptorSampledImages;
    uint32_t           maxPerStageDescriptorStorageImages;
    uint32_t           maxPerStageDescriptorInputAttachments;
    uint32_t           maxPerStageResources;
    uint32_t           maxDescriptorSetSamplers;
    uint32_t           maxDescriptorSetUniformBuffers;
    uint32_t           maxDescriptorSetUniformBuffersDynamic;
    uint32_t           maxDescriptorSetStorageBuffers;
    uint32_t           maxDescriptorSetStorageBuffersDynamic;
    uint32_t           maxDescriptorSetSampledImages;
    uint32_t           maxDescriptorSetStorageImages;
    uint32_t           maxDescriptorSetInputAttachments;
    uint32_t           maxVertexInputAttributes;
    uint32_t           maxVertexInputBindings;
    uint32_t           maxVertexInputAttributeOffset;
    uint32_t           maxVertexInputBindingStride;
    uint32_t           maxVertexOutputComponents;
    uint32_t           maxTessellationGenerationLevel;
    uint32_t           maxTessellationPatchSize;
    uint32_t           maxTessellationControlPerVertexInputComponents;
    uint32_t           maxTessellationControlPerVertexOutputComponents;
    uint32_t           maxTessellationControlPerPatchOutputComponents;
    uint32_t           maxTessellationControlTotalOutputComponents;
    uint32_t           maxTessellationEvaluationInputComponents;
    uint32_t           maxTessellationEvaluationOutputComponents;
    uint32_t           maxGeometryShaderInvocations;
    uint32_t           maxGeometryInputComponents;
    uint32_t           maxGeometryOutputComponents;
    uint32_t           maxGeometryOutputVertices;
    uint32_t           maxGeometryTotalOutputComponents;
    uint32_t           maxFragmentInputComponents;
    uint32_t           maxFragmentOutputAttachments;
    uint32_t           maxFragmentDualSrcAttachments;
    uint32_t           maxFragmentCombinedOutputResources;
    uint32_t           maxComputeSharedMemorySize;
    uint32_t           maxComputeWorkGroupCount[3];
    uint32_t           maxComputeWorkGroupInvocations;
    uint32_t           maxComputeWorkGroupSize[3];
    uint32_t           subPixelPrecisionBits;
    uint32_t           subTexelPrecisionBits;
    uint32_t           mipmapPrecisionBits;
    uint32_t           maxDrawIndexedIndexValue;
    uint32_t           maxDrawIndirectCount;
    float              maxSamplerLodBias;
    float              maxSamplerAnisotropy;
    uint32_t           maxViewports;
    uint32_t           maxViewportDimensions[2];
    float              viewportBoundsRange[2];
    uint32_t           viewportSubPixelBits;
    size_t             minMemoryMapAlignment;
    VkDeviceSize       minTexelBufferOffsetAlignment;
    VkDeviceSize       minUniformBufferOffsetAlignment;
    VkDeviceSize       minStorageBufferOffsetAlignment;
    int32_t            minTexelOffset;
    uint32_t           maxTexelOffset;
    int32_t            minTexelGatherOffset;
    uint32_t           maxTexelGatherOffset;
    float              minInterpolationOffset;
    float              maxInterpolationOffset;
    uint32_t           subPixelInterpolationOffsetBits;
    uint32_t           maxFramebufferWidth;
    uint32_t           maxFramebufferHeight;
    uint32_t           maxFramebufferLayers;
    VkSampleCountFlags framebufferColorSampleCounts;
    VkSampleCountFlags framebufferDepthSampleCounts;
    VkSampleCountFlags framebufferStencilSampleCounts;
    VkSampleCountFlags framebufferNoAttachmentsSampleCounts;
    uint32_t           maxColorAttachments;
    VkSampleCountFlags sampledImageColorSampleCounts;
    VkSampleCountFlags sampledImageIntegerSampleCounts;
    VkSampleCountFlags sampledImageDepthSampleCounts;
    VkSampleCountFlags sampledImageStencilSampleCounts;
    VkSampleCountFlags storageImageSampleCounts;
    uint32_t           maxSampleMaskWords;
    VkBool32           timestampComputeAndGraphics;
    float              timestampPeriod;
    uint32_t           maxClipDistances;
    uint32_t           maxCullDistances;
    uint32_t           maxCombinedClipAndCullDistances;
    uint32_t           discreteQueuePriorities;
    float              pointSizeRange[2];
    float              lineWidthRange[2];
    float              pointSizeGranularity;
    float              lineWidthGranularity;
    VkBool32           strictLines;
    VkBool32           standardSampleLocations;
    VkDeviceSize       optimalBufferCopyOffsetAlignment;
    VkDeviceSize       optimalBufferCopyRowPitchAlignment;
    VkDeviceSize       nonCoherentAtomSize;
} VkPhysicalDeviceLimits;

typedef struct VkPhysicalDeviceSparseProperties {
    VkBool32 residencyStandard2DBlockShape;
    VkBool32 residencyStandard2DMultisampleBlockShape;
    VkBool32 residencyStandard3DBlockShape;
    VkBool32 residencyAlignedMipSize;
    VkBool32 residencyNonResidentStrict;
} VkPhysicalDeviceSparseProperties;

typedef struct VkPhysicalDeviceProperties {
    uint32_t                         apiVersion;
    uint32_t                         driverVersion;
    uint32_t                         vendorID;
    uint32_t                         deviceID;
    uint32_t                         deviceType;
    char                             deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t                          pipelineCacheUUID[VK_UUID_SIZE];
    VkPhysicalDeviceLimits           limits;
    VkPhysicalDeviceSparseProperties sparseProperties;
} VkPhysicalDeviceProperties;

/* 55 VkBool32, na ordem da especificacao. */
typedef struct VkPhysicalDeviceFeatures {
    VkBool32 robustBufferAccess;
    VkBool32 fullDrawIndexUint32;
    VkBool32 imageCubeArray;
    VkBool32 independentBlend;
    VkBool32 geometryShader;
    VkBool32 tessellationShader;
    VkBool32 sampleRateShading;
    VkBool32 dualSrcBlend;
    VkBool32 logicOp;
    VkBool32 multiDrawIndirect;
    VkBool32 drawIndirectFirstInstance;
    VkBool32 depthClamp;
    VkBool32 depthBiasClamp;
    VkBool32 fillModeNonSolid;
    VkBool32 depthBounds;
    VkBool32 wideLines;
    VkBool32 largePoints;
    VkBool32 alphaToOne;
    VkBool32 multiViewport;
    VkBool32 samplerAnisotropy;
    VkBool32 textureCompressionETC2;
    VkBool32 textureCompressionASTC_LDR;
    VkBool32 textureCompressionBC;
    VkBool32 occlusionQueryPrecise;
    VkBool32 pipelineStatisticsQuery;
    VkBool32 vertexPipelineStoresAndAtomics;
    VkBool32 fragmentStoresAndAtomics;
    VkBool32 shaderTessellationAndGeometryPointSize;
    VkBool32 shaderImageGatherExtended;
    VkBool32 shaderStorageImageExtendedFormats;
    VkBool32 shaderStorageImageMultisample;
    VkBool32 shaderStorageImageReadWithoutFormat;
    VkBool32 shaderStorageImageWriteWithoutFormat;
    VkBool32 shaderUniformBufferArrayDynamicIndexing;
    VkBool32 shaderSampledImageArrayDynamicIndexing;
    VkBool32 shaderStorageBufferArrayDynamicIndexing;
    VkBool32 shaderStorageImageArrayDynamicIndexing;
    VkBool32 shaderClipDistance;
    VkBool32 shaderCullDistance;
    VkBool32 shaderFloat64;
    VkBool32 shaderInt64;
    VkBool32 shaderInt16;
    VkBool32 shaderResourceResidency;
    VkBool32 shaderResourceMinLod;
    VkBool32 sparseBinding;
    VkBool32 sparseResidencyBuffer;
    VkBool32 sparseResidencyImage2D;
    VkBool32 sparseResidencyImage3D;
    VkBool32 sparseResidency2Samples;
    VkBool32 sparseResidency4Samples;
    VkBool32 sparseResidency8Samples;
    VkBool32 sparseResidency16Samples;
    VkBool32 sparseResidencyAliased;
    VkBool32 variableMultisampleRate;
    VkBool32 inheritedQueries;
} VkPhysicalDeviceFeatures;

typedef struct VkExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} VkExtent3D;

typedef struct VkQueueFamilyProperties {
    VkQueueFlags queueFlags;
    uint32_t     queueCount;
    uint32_t     timestampValidBits;
    VkExtent3D   minImageTransferGranularity;
} VkQueueFamilyProperties;

#define VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 1000059001
typedef struct VkQueueFamilyProperties2 {
    uint32_t                sType;
    void*                   pNext;
    VkQueueFamilyProperties queueFamilyProperties;
} VkQueueFamilyProperties2;
typedef VkQueueFamilyProperties2 VkQueueFamilyProperties2KHR;

typedef struct VkMemoryType {
    VkMemoryPropertyFlags propertyFlags;
    uint32_t              heapIndex;
} VkMemoryType;

typedef struct VkMemoryHeap {
    VkDeviceSize      size;
    VkMemoryHeapFlags flags;
} VkMemoryHeap;

typedef struct VkPhysicalDeviceMemoryProperties {
    uint32_t     memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t     memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
} VkPhysicalDeviceMemoryProperties;

typedef struct VkFormatProperties {
    VkFormatFeatureFlags linearTilingFeatures;
    VkFormatFeatureFlags optimalTilingFeatures;
    VkFormatFeatureFlags bufferFeatures;
} VkFormatProperties;

typedef struct VkImageFormatProperties {
    VkExtent3D         maxExtent;
    uint32_t           maxMipLevels;
    uint32_t           maxArrayLayers;
    VkSampleCountFlags sampleCounts;
    VkDeviceSize       maxResourceSize;
} VkImageFormatProperties;

typedef struct VkSparseImageFormatProperties {
    VkFlags    aspectMask;
    VkExtent3D imageGranularity;
    VkFlags    flags;
} VkSparseImageFormatProperties;

typedef struct VkDeviceQueueCreateInfo {
    uint32_t     sType;
    const void*  pNext;
    VkFlags      flags;
    uint32_t     queueFamilyIndex;
    uint32_t     queueCount;
    const float* pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    uint32_t                        sType;
    const void*                     pNext;
    VkFlags                         flags;
    uint32_t                        queueCreateInfoCount;
    const VkDeviceQueueCreateInfo*  pQueueCreateInfos;
    uint32_t                        enabledLayerCount;
    const char* const*              ppEnabledLayerNames;
    uint32_t                        enabledExtensionCount;
    const char* const*              ppEnabledExtensionNames;
    const VkPhysicalDeviceFeatures* pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkMemoryAllocateInfo {
    uint32_t     sType;
    const void*  pNext;
    VkDeviceSize allocationSize;
    uint32_t     memoryTypeIndex;
} VkMemoryAllocateInfo;

typedef struct VkMappedMemoryRange {
    uint32_t       sType;
    const void*    pNext;
    VkDeviceMemory memory;
    VkDeviceSize   offset;
    VkDeviceSize   size;
} VkMappedMemoryRange;

typedef struct VkBufferCreateInfo {
    uint32_t          sType;
    const void*       pNext;
    VkFlags           flags;
    VkDeviceSize      size;
    VkImageUsageFlags usage;
    uint32_t          sharingMode;
    uint32_t          queueFamilyIndexCount;
    const uint32_t*   pQueueFamilyIndices;
} VkBufferCreateInfo;

typedef struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t     memoryTypeBits;
} VkMemoryRequirements;

typedef struct VkShaderModuleCreateInfo {
    uint32_t        sType;
    const void*     pNext;
    VkFlags         flags;
    size_t          codeSize;
    const uint32_t* pCode;
} VkShaderModuleCreateInfo;

typedef struct VkSpecializationInfo {
    uint32_t    mapEntryCount;
    const void* pMapEntries;
    size_t      dataSize;
    const void* pData;
} VkSpecializationInfo;

typedef struct VkPipelineShaderStageCreateInfo {
    uint32_t                    sType;
    const void*                 pNext;
    VkFlags                     flags;
    VkShaderStageFlags          stage;
    VkShaderModule              module;
    const char*                 pName;
    const VkSpecializationInfo* pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;

typedef struct VkComputePipelineCreateInfo {
    uint32_t                        sType;
    const void*                     pNext;
    VkFlags                         flags;
    VkPipelineShaderStageCreateInfo stage;
    VkPipelineLayout                layout;
    VkPipeline                      basePipelineHandle;
    int32_t                         basePipelineIndex;
} VkComputePipelineCreateInfo;

typedef struct VkPushConstantRange {
    VkShaderStageFlags stageFlags;
    uint32_t           offset;
    uint32_t           size;
} VkPushConstantRange;

typedef struct VkPipelineLayoutCreateInfo {
    uint32_t                     sType;
    const void*                  pNext;
    VkFlags                      flags;
    uint32_t                     setLayoutCount;
    const VkDescriptorSetLayout* pSetLayouts;
    uint32_t                     pushConstantRangeCount;
    const VkPushConstantRange*   pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkPipelineCacheCreateInfo {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
    size_t      initialDataSize;
    const void* pInitialData;
} VkPipelineCacheCreateInfo;

typedef struct VkDescriptorSetLayoutBinding {
    uint32_t           binding;
    uint32_t           descriptorType;
    uint32_t           descriptorCount;
    VkShaderStageFlags stageFlags;
    const VkSampler*   pImmutableSamplers;
} VkDescriptorSetLayoutBinding;

typedef struct VkDescriptorSetLayoutCreateInfo {
    uint32_t                            sType;
    const void*                         pNext;
    VkFlags                             flags;
    uint32_t                            bindingCount;
    const VkDescriptorSetLayoutBinding* pBindings;
} VkDescriptorSetLayoutCreateInfo;

typedef struct VkDescriptorPoolSize {
    uint32_t type;
    uint32_t descriptorCount;
} VkDescriptorPoolSize;

typedef struct VkDescriptorPoolCreateInfo {
    uint32_t                    sType;
    const void*                 pNext;
    VkFlags                     flags;
    uint32_t                    maxSets;
    uint32_t                    poolSizeCount;
    const VkDescriptorPoolSize* pPoolSizes;
} VkDescriptorPoolCreateInfo;

typedef struct VkDescriptorSetAllocateInfo {
    uint32_t                     sType;
    const void*                  pNext;
    VkDescriptorPool             descriptorPool;
    uint32_t                     descriptorSetCount;
    const VkDescriptorSetLayout* pSetLayouts;
} VkDescriptorSetAllocateInfo;

typedef struct VkDescriptorBufferInfo {
    VkBuffer     buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
} VkDescriptorBufferInfo;

typedef struct VkWriteDescriptorSet {
    uint32_t                      sType;
    const void*                   pNext;
    VkDescriptorSet               dstSet;
    uint32_t                      dstBinding;
    uint32_t                      dstArrayElement;
    uint32_t                      descriptorCount;
    uint32_t                      descriptorType;
    const void*                   pImageInfo;
    const VkDescriptorBufferInfo* pBufferInfo;
    const VkBufferView*           pTexelBufferView;
} VkWriteDescriptorSet;

typedef struct VkCopyDescriptorSet {
    uint32_t        sType;
    const void*     pNext;
    VkDescriptorSet srcSet;
    uint32_t        srcBinding;
    uint32_t        srcArrayElement;
    VkDescriptorSet dstSet;
    uint32_t        dstBinding;
    uint32_t        dstArrayElement;
    uint32_t        descriptorCount;
} VkCopyDescriptorSet;

typedef struct VkCommandPoolCreateInfo {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
    uint32_t    queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    uint32_t      sType;
    const void*   pNext;
    VkCommandPool commandPool;
    uint32_t      level;
    uint32_t      commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
    const void* pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkSubmitInfo {
    uint32_t               sType;
    const void*            pNext;
    uint32_t               waitSemaphoreCount;
    const VkSemaphore*     pWaitSemaphores;
    const VkFlags*         pWaitDstStageMask;
    uint32_t               commandBufferCount;
    const VkCommandBuffer* pCommandBuffers;
    uint32_t               signalSemaphoreCount;
    const VkSemaphore*     pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkFenceCreateInfo {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
} VkFenceCreateInfo;

typedef struct VkSemaphoreCreateInfo {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
} VkSemaphoreCreateInfo;

typedef struct VkEventCreateInfo {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
} VkEventCreateInfo;

typedef struct VkBufferCopy {
    VkDeviceSize srcOffset;
    VkDeviceSize dstOffset;
    VkDeviceSize size;
} VkBufferCopy;

typedef struct VkPhysicalDeviceFeatures2 {
    uint32_t               sType;
    void*                  pNext;
    VkPhysicalDeviceFeatures features;
} VkPhysicalDeviceFeatures2;

typedef struct VkPhysicalDeviceProperties2 {
    uint32_t                 sType;
    void*                    pNext;
    VkPhysicalDeviceProperties properties;
} VkPhysicalDeviceProperties2;

typedef struct VkPhysicalDeviceMemoryProperties2 {
    uint32_t                       sType;
    void*                          pNext;
    VkPhysicalDeviceMemoryProperties memoryProperties;
} VkPhysicalDeviceMemoryProperties2;

typedef struct VkPhysicalDevice16BitStorageFeatures {
    uint32_t sType;
    void*    pNext;
    VkBool32 storageBuffer16BitAccess;
    VkBool32 uniformAndStorageBuffer16BitAccess;
    VkBool32 storagePushConstant16;
    VkBool32 storageInputOutput16;
} VkPhysicalDevice16BitStorageFeatures;

typedef struct VkPhysicalDevice8BitStorageFeatures {
    uint32_t sType;
    void*    pNext;
    VkBool32 storageBuffer8BitAccess;
    VkBool32 uniformAndStorageBuffer8BitAccess;
    VkBool32 storagePushConstant8;
} VkPhysicalDevice8BitStorageFeatures;

typedef struct VkPhysicalDeviceShaderFloat16Int8Features {
    uint32_t sType;
    void*    pNext;
    VkBool32 shaderFloat16;
    VkBool32 shaderInt8;
} VkPhysicalDeviceShaderFloat16Int8Features;

typedef struct VkPhysicalDeviceSubgroupProperties {
    uint32_t    sType;
    void*       pNext;
    uint32_t    subgroupSize;
    VkFlags     supportedStages;
    VkFlags     supportedOperations;
    VkBool32    quadOperationsInAllStages;
} VkPhysicalDeviceSubgroupProperties;

typedef struct VkPhysicalDeviceVulkan11Features {
    uint32_t sType;
    void*    pNext;
    VkBool32 storageBuffer16BitAccess;
    VkBool32 uniformAndStorageBuffer16BitAccess;
    VkBool32 storagePushConstant16;
    VkBool32 storageInputOutput16;
    VkBool32 multiview;
    VkBool32 multiviewGeometryShader;
    VkBool32 multiviewTessellationShader;
    VkBool32 variablePointersStorageBuffer;
    VkBool32 variablePointers;
    VkBool32 protectedMemory;
    VkBool32 samplerYcbcrConversion;
    VkBool32 shaderDrawParameters;
} VkPhysicalDeviceVulkan11Features;

typedef struct VkPhysicalDeviceVulkan12Features {
    uint32_t sType;
    void*    pNext;
    VkBool32 samplerMirrorClampToEdge;
    VkBool32 drawIndirectCount;
    VkBool32 storageBuffer8BitAccess;
    VkBool32 uniformAndStorageBuffer8BitAccess;
    VkBool32 storagePushConstant8;
    VkBool32 shaderBufferInt64Atomics;
    VkBool32 shaderSharedInt64Atomics;
    VkBool32 shaderFloat16;
    VkBool32 shaderInt8;
    VkBool32 descriptorIndexing;
    VkBool32 shaderInputAttachmentArrayDynamicIndexing;
    VkBool32 shaderUniformTexelBufferArrayDynamicIndexing;
    VkBool32 shaderStorageTexelBufferArrayDynamicIndexing;
    VkBool32 shaderUniformBufferArrayNonUniformIndexing;
    VkBool32 shaderSampledImageArrayNonUniformIndexing;
    VkBool32 shaderStorageBufferArrayNonUniformIndexing;
    VkBool32 shaderStorageImageArrayNonUniformIndexing;
    VkBool32 shaderInputAttachmentArrayNonUniformIndexing;
    VkBool32 shaderUniformTexelBufferArrayNonUniformIndexing;
    VkBool32 shaderStorageTexelBufferArrayNonUniformIndexing;
    VkBool32 descriptorBindingUniformBufferUpdateAfterBind;
    VkBool32 descriptorBindingSampledImageUpdateAfterBind;
    VkBool32 descriptorBindingStorageImageUpdateAfterBind;
    VkBool32 descriptorBindingStorageBufferUpdateAfterBind;
    VkBool32 descriptorBindingUniformTexelBufferUpdateAfterBind;
    VkBool32 descriptorBindingStorageTexelBufferUpdateAfterBind;
    VkBool32 descriptorBindingUpdateUnusedWhilePending;
    VkBool32 descriptorBindingPartiallyBound;
    VkBool32 descriptorBindingVariableDescriptorCount;
    VkBool32 runtimeDescriptorArray;
    VkBool32 samplerFilterMinmax;
    VkBool32 scalarBlockLayout;
    VkBool32 imagelessFramebuffer;
    VkBool32 uniformBufferStandardLayout;
    VkBool32 shaderSubgroupExtendedTypes;
    VkBool32 separateDepthStencilLayouts;
    VkBool32 hostQueryReset;
    VkBool32 timelineSemaphore;
    VkBool32 bufferDeviceAddress;
    VkBool32 bufferDeviceAddressCaptureReplay;
    VkBool32 bufferDeviceAddressMultiDevice;
    VkBool32 vulkanMemoryModel;
    VkBool32 vulkanMemoryModelDeviceScope;
    VkBool32 vulkanMemoryModelAvailabilityVisibilityChains;
    VkBool32 shaderOutputViewportIndex;
    VkBool32 shaderOutputLayer;
    VkBool32 subgroupBroadcastDynamicId;
} VkPhysicalDeviceVulkan12Features;

typedef struct VkBufferMemoryRequirementsInfo2 {
    uint32_t sType;
    const void* pNext;
    VkBuffer buffer;
} VkBufferMemoryRequirementsInfo2;

typedef struct VkMemoryRequirements2 {
    uint32_t sType;
    void* pNext;
    VkMemoryRequirements memoryRequirements;
} VkMemoryRequirements2;

typedef struct VkOffset2D { int32_t x; int32_t y; } VkOffset2D;
typedef struct VkExtent2D { uint32_t width; uint32_t height; } VkExtent2D;
typedef struct VkRect2D { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct VkOffset3D { int32_t x; int32_t y; int32_t z; } VkOffset3D;

typedef struct VkViewport {
    float x; float y; float width; float height; float minDepth; float maxDepth;
} VkViewport;

typedef struct VkImageSubresourceLayers {
    VkFlags  aspectMask;
    uint32_t mipLevel;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceLayers;

typedef struct VkImageSubresourceRange {
    VkFlags  aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceRange;

typedef struct VkBufferImageCopy {
    VkDeviceSize             bufferOffset;
    uint32_t                 bufferRowLength;
    uint32_t                 bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D               imageOffset;
    VkExtent3D               imageExtent;
} VkBufferImageCopy;

typedef struct VkWin32SurfaceCreateInfoKHR {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
    HINSTANCE   hinstance;
    HWND        hwnd;
} VkWin32SurfaceCreateInfoKHR;

typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms;
    VkFlags currentTransform;
    VkFlags supportedCompositeAlpha;
    VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR {
    uint32_t format;
    uint32_t colorSpace;
} VkSurfaceFormatKHR;

typedef struct VkSwapchainCreateInfoKHR {
    uint32_t            sType;
    const void*         pNext;
    VkFlags             flags;
    VkSurfaceKHR        surface;
    uint32_t            minImageCount;
    uint32_t            imageFormat;
    uint32_t            imageColorSpace;
    VkExtent2D          imageExtent;
    uint32_t            imageArrayLayers;
    VkFlags             imageUsage;
    uint32_t            imageSharingMode;
    uint32_t            queueFamilyIndexCount;
    const uint32_t*     pQueueFamilyIndices;
    VkFlags             preTransform;
    VkFlags             compositeAlpha;
    uint32_t            presentMode;
    VkBool32            clipped;
    VkSwapchainKHR      oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkPresentInfoKHR {
    uint32_t               sType;
    const void*            pNext;
    uint32_t               waitSemaphoreCount;
    const VkSemaphore*     pWaitSemaphores;
    uint32_t               swapchainCount;
    const VkSwapchainKHR*  pSwapchains;
    const uint32_t*        pImageIndices;
    VkResult*              pResults;
} VkPresentInfoKHR;

typedef struct VkImageCreateInfo {
    uint32_t        sType;
    const void*     pNext;
    VkFlags         flags;
    uint32_t        imageType;
    uint32_t        format;
    VkExtent3D      extent;
    uint32_t        mipLevels;
    uint32_t        arrayLayers;
    uint32_t        samples;
    uint32_t        tiling;
    VkFlags         usage;
    uint32_t        sharingMode;
    uint32_t        queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
    uint32_t        initialLayout;
} VkImageCreateInfo;

typedef struct VkImageViewCreateInfo {
    uint32_t                sType;
    const void*             pNext;
    VkFlags                 flags;
    VkImage                 image;
    uint32_t                viewType;
    uint32_t                format;
    uint32_t                components[4];
    VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;

typedef struct VkFramebufferCreateInfo {
    uint32_t            sType;
    const void*         pNext;
    VkFlags             flags;
    VkRenderPass        renderPass;
    uint32_t            attachmentCount;
    const VkImageView*  pAttachments;
    uint32_t            width;
    uint32_t            height;
    uint32_t            layers;
} VkFramebufferCreateInfo;

typedef struct VkRenderPassCreateInfo {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
    uint32_t    attachmentCount;
    const void* pAttachments;
    uint32_t    subpassCount;
    const void* pSubpasses;
    uint32_t    dependencyCount;
    const void* pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo {
    uint32_t        sType;
    const void*     pNext;
    VkFlags         flags;
    uint32_t        stageCount;
    const void*     pStages;
    const void*     pVertexInputState;
    const void*     pInputAssemblyState;
    const void*     pTessellationState;
    const void*     pViewportState;
    const void*     pRasterizationState;
    const void*     pMultisampleState;
    const void*     pDepthStencilState;
    const void*     pColorBlendState;
    const void*     pDynamicState;
    VkPipelineLayout layout;
    VkRenderPass    renderPass;
    uint32_t        subpass;
    VkPipeline      basePipelineHandle;
    int32_t         basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

typedef struct VkSamplerCreateInfo {
    uint32_t    sType;
    const void* pNext;
    VkFlags     flags;
    uint32_t    magFilter;
    uint32_t    minFilter;
    uint32_t    mipmapMode;
    uint32_t    addressModeU;
    uint32_t    addressModeV;
    uint32_t    addressModeW;
    float       mipLodBias;
    VkBool32    anisotropyEnable;
    float       maxAnisotropy;
    VkBool32    compareEnable;
    uint32_t    compareOp;
    float       minLod;
    float       maxLod;
    uint32_t    borderColor;
    VkBool32    unnormalizedCoordinates;
} VkSamplerCreateInfo;

typedef struct VkBindBufferMemoryInfo {
    uint32_t sType;
    const void* pNext;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize memoryOffset;
} VkBindBufferMemoryInfo;

typedef struct VkPhysicalDevicePCIBusInfoPropertiesEXT {
    uint32_t sType;
    void*    pNext;
    uint32_t pciDomain;
    uint32_t pciBus;
    uint32_t pciDevice;
    uint32_t pciFunction;
} VkPhysicalDevicePCIBusInfoPropertiesEXT;

typedef struct VkPhysicalDeviceMemoryBudgetPropertiesEXT {
    uint32_t     sType;
    void*        pNext;
    VkDeviceSize heapBudget[16];
    VkDeviceSize heapUsage[16];
} VkPhysicalDeviceMemoryBudgetPropertiesEXT;

typedef struct VkPhysicalDeviceIDProperties {
    uint32_t sType;
    void*    pNext;
    uint8_t  deviceUUID[16];
    uint8_t  driverUUID[16];
    uint8_t  deviceLUID[8];
    uint32_t deviceNodeMask;
    VkBool32 deviceLUIDValid;
} VkPhysicalDeviceIDProperties;
typedef VkPhysicalDeviceIDProperties VkPhysicalDeviceIDPropertiesKHR;

typedef struct VkPhysicalDeviceDriverProperties {
    uint32_t sType;
    void*    pNext;
    uint32_t driverID;
    char     driverName[256];
    char     driverInfo[256];
    uint8_t  conformanceVersionMajor;
    uint8_t  conformanceVersionMinor;
    uint8_t  conformanceVersionSubminor;
    uint8_t  conformanceVersionPatch;
} VkPhysicalDeviceDriverProperties;
typedef VkPhysicalDeviceDriverProperties VkPhysicalDeviceDriverPropertiesKHR;

typedef struct VkPhysicalDeviceVulkan11Properties {
    uint32_t sType;
    void*    pNext;
    uint8_t  deviceUUID[16];
    uint8_t  driverUUID[16];
    uint8_t  deviceLUID[8];
    uint32_t deviceNodeMask;
    VkBool32 deviceLUIDValid;
    uint32_t subgroupSize;
    VkShaderStageFlags subgroupSupportedStages;
    VkFlags  subgroupSupportedOperations;
    VkBool32 subgroupQuadOperationsInAllStages;
    uint32_t pointClippingBehavior;
    uint32_t maxMultiviewViewCount;
    uint32_t maxMultiviewInstanceIndex;
    VkBool32 protectedNoFault;
    uint32_t maxPerSetDescriptors;
    VkDeviceSize maxMemoryAllocationSize;
} VkPhysicalDeviceVulkan11Properties;

typedef struct VkPhysicalDeviceVulkan12Properties {
    uint32_t sType;
    void*    pNext;
    uint32_t driverID;
    char     driverName[256];
    char     driverInfo[256];
    uint8_t  conformanceVersionMajor;
    uint8_t  conformanceVersionMinor;
    uint8_t  conformanceVersionSubminor;
    uint8_t  conformanceVersionPatch;
    uint32_t denormBehaviorIndependence;
    uint32_t roundingModeIndependence;
    VkBool32 shaderSignedZeroInfNanPreserveFloat16;
    VkBool32 shaderSignedZeroInfNanPreserveFloat32;
    VkBool32 shaderSignedZeroInfNanPreserveFloat64;
    VkBool32 shaderDenormPreserveFloat16;
    VkBool32 shaderDenormPreserveFloat32;
    VkBool32 shaderDenormPreserveFloat64;
    VkBool32 shaderDenormFlushToZeroFloat16;
    VkBool32 shaderDenormFlushToZeroFloat32;
    VkBool32 shaderDenormFlushToZeroFloat64;
    VkBool32 shaderRoundingModeRTEFloat16;
    VkBool32 shaderRoundingModeRTEFloat32;
    VkBool32 shaderRoundingModeRTEFloat64;
    VkBool32 shaderRoundingModeRTZFloat16;
    VkBool32 shaderRoundingModeRTZFloat32;
    VkBool32 shaderRoundingModeRTZFloat64;
    uint32_t maxUpdateAfterBindDescriptorsInAllPools;
    VkBool32 shaderUniformBufferArrayNonUniformIndexingNative;
    VkBool32 shaderSampledImageArrayNonUniformIndexingNative;
    VkBool32 shaderStorageBufferArrayNonUniformIndexingNative;
    VkBool32 shaderStorageImageArrayNonUniformIndexingNative;
    VkBool32 shaderInputAttachmentArrayNonUniformIndexingNative;
    VkBool32 robustBufferAccessUpdateAfterBind;
    VkBool32 quadDivergentImplicitLod;
    uint32_t maxPerStageDescriptorUpdateAfterBindSamplers;
    uint32_t maxPerStageDescriptorUpdateAfterBindUniformBuffers;
    uint32_t maxPerStageDescriptorUpdateAfterBindStorageBuffers;
    uint32_t maxPerStageDescriptorUpdateAfterBindSampledImages;
    uint32_t maxPerStageDescriptorUpdateAfterBindStorageImages;
    uint32_t maxPerStageDescriptorUpdateAfterBindInputAttachments;
    uint32_t maxPerStageUpdateAfterBindResources;
    uint32_t maxDescriptorSetUpdateAfterBindSamplers;
    uint32_t maxDescriptorSetUpdateAfterBindUniformBuffers;
    uint32_t maxDescriptorSetUpdateAfterBindUniformBuffersDynamic;
    uint32_t maxDescriptorSetUpdateAfterBindStorageBuffers;
    uint32_t maxDescriptorSetUpdateAfterBindStorageBuffersDynamic;
    uint32_t maxDescriptorSetUpdateAfterBindSampledImages;
    uint32_t maxDescriptorSetUpdateAfterBindStorageImages;
    uint32_t maxDescriptorSetUpdateAfterBindInputAttachments;
    uint32_t supportedDepthResolveModes;
    uint32_t supportedStencilResolveModes;
    VkBool32 independentResolveNone;
    VkBool32 independentResolve;
    VkBool32 filterMinmaxSingleComponentFormats;
    VkBool32 filterMinmaxImageComponentMapping;
    uint64_t maxTimelineSemaphoreValueDifference;
    uint32_t framebufferIntegerColorSampleCounts;
} VkPhysicalDeviceVulkan12Properties;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES 1000108000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES_KHR 1000108000
typedef struct VkPhysicalDeviceImagelessFramebufferFeatures {
    uint32_t sType;
    void*    pNext;
    VkBool32 imagelessFramebuffer;
} VkPhysicalDeviceImagelessFramebufferFeatures;
typedef VkPhysicalDeviceImagelessFramebufferFeatures VkPhysicalDeviceImagelessFramebufferFeaturesKHR;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES 1000161000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT 1000161000
typedef struct VkPhysicalDeviceDescriptorIndexingFeatures {
    uint32_t sType;
    void*    pNext;
    VkBool32 shaderInputAttachmentArrayDynamicIndexing;
    VkBool32 shaderUniformTexelBufferArrayDynamicIndexing;
    VkBool32 shaderStorageTexelBufferArrayDynamicIndexing;
    VkBool32 shaderUniformBufferArrayNonUniformIndexing;
    VkBool32 shaderSampledImageArrayNonUniformIndexing;
    VkBool32 shaderStorageBufferArrayNonUniformIndexing;
    VkBool32 shaderStorageImageArrayNonUniformIndexing;
    VkBool32 shaderInputAttachmentArrayNonUniformIndexing;
    VkBool32 shaderUniformTexelBufferArrayNonUniformIndexing;
    VkBool32 shaderStorageTexelBufferArrayNonUniformIndexing;
    VkBool32 descriptorBindingUniformBufferUpdateAfterBind;
    VkBool32 descriptorBindingSampledImageUpdateAfterBind;
    VkBool32 descriptorBindingStorageImageUpdateAfterBind;
    VkBool32 descriptorBindingStorageBufferUpdateAfterBind;
    VkBool32 descriptorBindingUniformTexelBufferUpdateAfterBind;
    VkBool32 descriptorBindingStorageTexelBufferUpdateAfterBind;
    VkBool32 descriptorBindingUpdateUnusedWhilePending;
    VkBool32 descriptorBindingPartiallyBound;
    VkBool32 descriptorBindingVariableDescriptorCount;
    VkBool32 runtimeDescriptorArray;
} VkPhysicalDeviceDescriptorIndexingFeatures;
typedef VkPhysicalDeviceDescriptorIndexingFeatures VkPhysicalDeviceDescriptorIndexingFeaturesEXT;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES 1000161002
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT 1000161002
typedef struct VkPhysicalDeviceDescriptorIndexingProperties {
    uint32_t sType;
    void*    pNext;
    uint32_t maxUpdateAfterBindDescriptorsInAllPools;
    VkBool32 shaderUniformBufferArrayNonUniformIndexingNative;
    VkBool32 shaderSampledImageArrayNonUniformIndexingNative;
    VkBool32 shaderStorageBufferArrayNonUniformIndexingNative;
    VkBool32 shaderStorageImageArrayNonUniformIndexingNative;
    VkBool32 shaderInputAttachmentArrayNonUniformIndexingNative;
    VkBool32 robustBufferAccessUpdateAfterBind;
    VkBool32 quadDivergentImplicitLod;
    uint32_t maxPerStageDescriptorUpdateAfterBindSamplers;
    uint32_t maxPerStageDescriptorUpdateAfterBindUniformBuffers;
    uint32_t maxPerStageDescriptorUpdateAfterBindStorageBuffers;
    uint32_t maxPerStageDescriptorUpdateAfterBindSampledImages;
    uint32_t maxPerStageDescriptorUpdateAfterBindStorageImages;
    uint32_t maxPerStageDescriptorUpdateAfterBindInputAttachments;
    uint32_t maxPerStageUpdateAfterBindResources;
    uint32_t maxDescriptorSetUpdateAfterBindSamplers;
    uint32_t maxDescriptorSetUpdateAfterBindUniformBuffers;
    uint32_t maxDescriptorSetUpdateAfterBindUniformBuffersDynamic;
    uint32_t maxDescriptorSetUpdateAfterBindStorageBuffers;
    uint32_t maxDescriptorSetUpdateAfterBindStorageBuffersDynamic;
    uint32_t maxDescriptorSetUpdateAfterBindSampledImages;
    uint32_t maxDescriptorSetUpdateAfterBindStorageImages;
    uint32_t maxDescriptorSetUpdateAfterBindInputAttachments;
} VkPhysicalDeviceDescriptorIndexingProperties;
typedef VkPhysicalDeviceDescriptorIndexingProperties VkPhysicalDeviceDescriptorIndexingPropertiesEXT;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES 1000168000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES_KHR 1000168000
typedef struct VkPhysicalDeviceMaintenance3Properties {
    uint32_t     sType;
    void*        pNext;
    uint32_t     maxPerSetDescriptors;
    VkDeviceSize maxMemoryAllocationSize;
} VkPhysicalDeviceMaintenance3Properties;
typedef VkPhysicalDeviceMaintenance3Properties VkPhysicalDeviceMaintenance3PropertiesKHR;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES 1000207000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR 1000207000
typedef struct VkPhysicalDeviceTimelineSemaphoreFeatures {
    uint32_t sType;
    void*    pNext;
    VkBool32 timelineSemaphore;
} VkPhysicalDeviceTimelineSemaphoreFeatures;
typedef VkPhysicalDeviceTimelineSemaphoreFeatures VkPhysicalDeviceTimelineSemaphoreFeaturesKHR;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES 1000207001
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR 1000207001
typedef struct VkPhysicalDeviceTimelineSemaphoreProperties {
    uint32_t sType;
    void*    pNext;
    uint64_t maxTimelineSemaphoreValueDifference;
} VkPhysicalDeviceTimelineSemaphoreProperties;
typedef VkPhysicalDeviceTimelineSemaphoreProperties VkPhysicalDeviceTimelineSemaphorePropertiesKHR;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES 1000257000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR 1000257000
typedef struct VkPhysicalDeviceBufferDeviceAddressFeatures {
    uint32_t sType;
    void*    pNext;
    VkBool32 bufferDeviceAddress;
    VkBool32 bufferDeviceAddressCaptureReplay;
    VkBool32 bufferDeviceAddressMultiDevice;
} VkPhysicalDeviceBufferDeviceAddressFeatures;
typedef VkPhysicalDeviceBufferDeviceAddressFeatures VkPhysicalDeviceBufferDeviceAddressFeaturesKHR;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES 1000413000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES_KHR 1000413000
typedef struct VkPhysicalDeviceMaintenance4Properties {
    uint32_t     sType;
    void*        pNext;
    VkDeviceSize maxBufferSize;
} VkPhysicalDeviceMaintenance4Properties;
typedef VkPhysicalDeviceMaintenance4Properties VkPhysicalDeviceMaintenance4PropertiesKHR;

/* ============================================================================
   Protótipos das Funções da API Vulkan Exportadas pelo ICD
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

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName);
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pVersion);

/* Travas de tamanho. Se algum destes falhar na compilacao, a ABI divergiu do
   Vulkan oficial e o ICD nao pode ser usado. Valores para x64. */
#if defined(_WIN64)
_STATIC_ASSERT(sizeof(VkPhysicalDeviceLimits) == 504);
_STATIC_ASSERT(sizeof(VkPhysicalDeviceSparseProperties) == 20);
_STATIC_ASSERT(sizeof(VkPhysicalDeviceProperties) == 824);
_STATIC_ASSERT(sizeof(VkPhysicalDeviceFeatures) == 220);
_STATIC_ASSERT(sizeof(VkPhysicalDeviceMemoryProperties) == 520);
_STATIC_ASSERT(sizeof(VkQueueFamilyProperties) == 24);
_STATIC_ASSERT(sizeof(VkExtensionProperties) == 260);
_STATIC_ASSERT(sizeof(VkLayerProperties) == 520);
_STATIC_ASSERT(sizeof(VkMemoryRequirements) == 24);
_STATIC_ASSERT(sizeof(VkWriteDescriptorSet) == 64);
#endif

#endif /* _BC250_VULKAN_H_ */
