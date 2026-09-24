#ifndef BC250_UMD_VULKAN_BRIDGE_H
#define BC250_UMD_VULKAN_BRIDGE_H

#pragma warning(push)
#pragma warning(disable: 4201)
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#pragma warning(pop)

#define BC250_VENDOR_ID 0x1002u
#define BC250_DEVICE_ID 0x13FEu

/* Vulkan Dispatch Table for UMD Bridge */
typedef struct _BC250_VK_DISPATCH {
    /* Instance level */
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkDestroyInstance vkDestroyInstance;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
    PFN_vkCreateDevice vkCreateDevice;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;

    /* Device level */
    PFN_vkDestroyDevice vkDestroyDevice;
    PFN_vkGetDeviceQueue vkGetDeviceQueue;
    PFN_vkCreateBuffer vkCreateBuffer;
    PFN_vkDestroyBuffer vkDestroyBuffer;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
    PFN_vkCreateImage vkCreateImage;
    PFN_vkDestroyImage vkDestroyImage;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
    PFN_vkCreateImageView vkCreateImageView;
    PFN_vkDestroyImageView vkDestroyImageView;
    PFN_vkCreateSampler vkCreateSampler;
    PFN_vkDestroySampler vkDestroySampler;
    PFN_vkAllocateMemory vkAllocateMemory;
    PFN_vkFreeMemory vkFreeMemory;
    PFN_vkBindBufferMemory vkBindBufferMemory;
    PFN_vkBindImageMemory vkBindImageMemory;
    PFN_vkMapMemory vkMapMemory;
    PFN_vkUnmapMemory vkUnmapMemory;
    PFN_vkCreateRenderPass vkCreateRenderPass;
    PFN_vkDestroyRenderPass vkDestroyRenderPass;
    PFN_vkCreateFramebuffer vkCreateFramebuffer;
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
    PFN_vkResetDescriptorPool vkResetDescriptorPool;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
    PFN_vkCreateShaderModule vkCreateShaderModule;
    PFN_vkDestroyShaderModule vkDestroyShaderModule;
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
    PFN_vkDestroyPipeline vkDestroyPipeline;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
    PFN_vkCmdBindPipeline vkCmdBindPipeline;
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
    PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
    PFN_vkCmdSetViewport vkCmdSetViewport;
    PFN_vkCmdSetScissor vkCmdSetScissor;
    PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
    PFN_vkCmdPushConstants vkCmdPushConstants;
    PFN_vkCmdDraw vkCmdDraw;
    PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
    PFN_vkCmdCopyImage vkCmdCopyImage;
    PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
    PFN_vkCmdClearAttachments vkCmdClearAttachments;
    PFN_vkCmdClearColorImage vkCmdClearColorImage;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkResetFences vkResetFences;
    PFN_vkResetCommandBuffer vkResetCommandBuffer;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
    PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
    PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
} BC250_VK_DISPATCH;

typedef VkResult (VKAPI_PTR *PFN_radv_bc250_present_scanout)(
    VkDevice device, VkQueue queue, VkDeviceMemory memory, uint32_t width, uint32_t height);

typedef uint64_t (VKAPI_PTR *PFN_radv_bc250_get_memory_gpu_va)(
    VkDeviceMemory memory);

/* DX.54: capacidade do pool persistente (um lote inteiro de draws) e do cache
 * de samplers. Um draw que nao couber cai no pool proprio de antes, contado. */
#define BC250_DX54_POOL_MAX_SETS 64u
#define BC250_DX54_POOL_UNIFORM_BUFFERS (64u * 28u)
#define BC250_DX54_POOL_SAMPLED_IMAGES (64u * 32u)
#define BC250_DX54_POOL_SAMPLERS (64u * 32u)
#define BC250_DX54_SAMPLER_CACHE_CAPACITY 64u

/* UMD Vulkan Translation Bridge State */
typedef struct BC250_VK_DESCRIPTOR_BATCH {
    VkDescriptorPool Pool;
    VkImageView Views[316];
    VkSampler Samplers[316];
    VkImageMemoryBarrier RestoreImages[316];
    struct BC250_VK_DESCRIPTOR_BATCH *Next;
} BC250_VK_DESCRIPTOR_BATCH;

typedef struct _BC250_UMD_VK_BRIDGE {
    HMODULE hIcdModule;
    BOOL Initialized;
    BOOL DeviceActive;

    VkInstance Instance;
    VkPhysicalDevice PhysicalDevice;
    VkDevice Device;
    VkQueue GraphicsQueue;
    uint32_t GraphicsQueueFamily;
    VkPhysicalDeviceMemoryProperties MemProperties;

    VkCommandPool CommandPool;
    VkCommandBuffer CommandBuffer;
    VkFence Fence;
    BOOL RecordingActive;
    BOOL SubmissionFailed;
    BC250_VK_DESCRIPTOR_BATCH *DescriptorBatches;

    /*
     * DX.49 -- LOTE DE DRAWS (Corrida 93: 425 flushes/min, cada draw com
     * submit + espera antes E depois). O draw agora so' grava; o lote vai para
     * a GPU no proximo flush (Present, D3D10 Flush, upload de textura, copia,
     * dreno antes de destruir objeto, ou teto BC250_DX49_BATCH_MAX_DRAWS).
     *
     * Os dados de VB/IB/CB de cada draw vao para o ANEL abaixo, em faixa
     * propria: o draw seguinte nao reescreve o que o anterior ainda vai ler.
     * O anel so' volta ao inicio depois de um flush com fence concluida, e o
     * flush daqui SEMPRE espera a fence.
     *
     * pRenderPassOpen aponta para BC250_UMD_DEVICE.InRenderPass: o flush fecha
     * o render pass aberto por um clear antes de vkEndCommandBuffer, senao o
     * dreno chamado de dentro de uma destruicao mataria o device.
     */
    BOOL *pRenderPassOpen;
    uint32_t PendingDraws;
    uint64_t BatchFlushCount;
    uint64_t BatchDrawsFlushed;
    VkBuffer DrawRingBuf;
    VkDeviceMemory DrawRingMem;
    uint8_t *DrawRingMapped;
    VkDeviceSize DrawRingSize;
    VkDeviceSize DrawRingHead;
    BOOL DrawRingFailed;

    /*
     * DX.54 -- POOL DE DESCRITORES PERSISTENTE (Corrida 98R, P0 de tempo: no
     * dwm.exe o intervalo entre o upload do CB e "[VK_DESCRIPTOR] BOUND" foi de
     * ~14-24 ms por draw, 2062 vezes; ali so' ha criacao de view, sampler e de
     * um VkDescriptorPool NOVO por draw -- no RADV o pool aloca um BO, isto e',
     * ida ao kernel). Um pool por device, criado no primeiro draw com
     * descritor, zerado com vkResetDescriptorPool SO' depois de fence
     * concluida; samplers ficam num cache ate o shutdown.
     */
    VkDescriptorPool PersistentPool;
    BOOL PersistentPoolFailed;
    uint32_t PersistentPoolSets;
    uint64_t PersistentPoolResets;
    uint64_t DescriptorFallbackPools;
    uint32_t SamplerCacheCount;
    struct {
        VkSamplerCreateInfo Info;
        VkSampler Sampler;
    } SamplerCache[BC250_DX54_SAMPLER_CACHE_CAPACITY];
    /* DX.54: tempos do ultimo flush (QPC), para o log dizer onde o flush gasta. */
    uint64_t DescriptorMicrosInBatch;

    /*
     * DX.49 -- STAGING PERSISTENTE do upload de textura (Corrida 93: ~1,1 s
     * depois de cada upload de 3,6 MB, entre o trace SUCESSO e o retorno, onde
     * so' ha vkFreeMemory/vkDestroyBuffer do staging criado a cada chamada).
     * Cresce quando precisa; so' e' liberado no shutdown. O upload continua
     * esperando a fence da copia, entao o reuso nunca pisa numa copia em voo.
     */
    VkBuffer UploadStagingBuf;
    VkDeviceMemory UploadStagingMem;
    void *UploadStagingMapped;
    VkDeviceSize UploadStagingSize;
    uint32_t UploadStagingGrowCount;

    /* Destiled linear presentation buffer for artifact-free scanout */
    VkBuffer LinearPresentBuf;
    VkDeviceMemory LinearPresentMem;
    VkDeviceSize LinearPresentSize;
    BOOL LinearPresentCreated;

    PFN_radv_bc250_present_scanout pfnPresentScanout;
    PFN_radv_bc250_get_memory_gpu_va pfnGetMemoryGpuVa;

    /*
     * Faixa DX.21: despejo do assembly ACO de cada pipeline no log da UMD.
     * Somente leitura (VK_KHR_pipeline_executable_properties). Ausente =
     * declarado no log, nunca fatal: instrumentacao nao pode derrubar o device.
     */
    BOOL PipelineIrCaptureEnabled;
    uint32_t PipelineIrDumpCount;
    PFN_vkGetPipelineExecutablePropertiesKHR pfnGetPipelineExecutableProperties;
    PFN_vkGetPipelineExecutableInternalRepresentationsKHR pfnGetPipelineExecutableInternalRepresentations;

    BC250_VK_DISPATCH vk;
} BC250_UMD_VK_BRIDGE;

VOID Bc250UmdVkDumpPipelineIr(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, VkPipeline pipeline,
    const void *vsModuleTag, const void *psModuleTag);

/* Functions exported by the Vulkan Bridge */
BOOL Bc250UmdVkBridgeInit(_Out_ BC250_UMD_VK_BRIDGE *pBridge);
VOID Bc250UmdVkBridgeShutdown(_Inout_ BC250_UMD_VK_BRIDGE *pBridge);

uint32_t Bc250UmdVkFindMemoryType(
    _In_ const BC250_UMD_VK_BRIDGE *pBridge,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties);

VkCommandBuffer Bc250UmdVkGetCommandBuffer(_Inout_ BC250_UMD_VK_BRIDGE *pBridge);
VkResult Bc250UmdVkFlushCommands(_Inout_ BC250_UMD_VK_BRIDGE *pBridge);
VOID Bc250UmdVkReleaseDescriptorBatches(BC250_UMD_VK_BRIDGE *pBridge);

/* DX.49: teto do lote e tamanho do anel de dados por draw.
 * DX.54: 16 MB -> 4 MB. Na 96 cada device criava 16 MB mapeados, inclusive os
 * do Explorer (~36 devices/min), e a ponte perdeu residencia 438 vezes; o
 * dwm.exe usa ~180 KB por draw, entao 4 MB guardam ~22 draws. */
#define BC250_DX49_BATCH_MAX_DRAWS 64u
#define BC250_DX49_DRAW_RING_BYTES (4u * 1024u * 1024u)
#define BC250_DX49_DRAW_RING_ALIGN 256u
/* DX.54: o anel so' nasce num device que ja fez este numero de draws pelo
 * caminho direto -- os devices de vida curta do laco do Explorer (1 draw
 * cada) nunca o criam. */
#define BC250_DX54_RING_MIN_DIRECT_DRAWS 32u

/* DX.54: um conjunto do pool persistente, ou VK_NULL_HANDLE se o pool nao
 * existe/esgotou (o chamador cai no pool por draw). */
VkDescriptorSet Bc250UmdVkAllocPersistentSet(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, VkDescriptorSetLayout layout);
/* DX.54: sampler do cache (vive ate o shutdown), ou VK_NULL_HANDLE se o cache
 * estiver cheio ou a criacao falhar. */
VkSampler Bc250UmdVkCachedSampler(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, _In_ const VkSamplerCreateInfo *pInfo);

/* Submete e espera o que estiver gravado, antes de destruir ou reescrever um
 * objeto que um draw pendente pode referenciar. Sem gravacao: nao faz nada. */
VkResult Bc250UmdVkDrainPending(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, _In_z_ const char *reason);
/* Bytes livres no anel (0 se o anel nao existe e nao pode ser criado). */
VkDeviceSize Bc250UmdVkDrawRingFree(_Inout_ BC250_UMD_VK_BRIDGE *pBridge);
/* Copia Bytes para uma faixa nova do anel; FALSE se nao couber (o chamador
 * decide drenar antes de gravar o draw, nunca no meio dele). */
BOOL Bc250UmdVkDrawRingPut(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, _In_reads_bytes_(Bytes) const void *pData,
    VkDeviceSize Bytes, _Out_ VkBuffer *pBuffer, _Out_ VkDeviceSize *pOffset);
/* Staging persistente de upload: garante >= Bytes, devolve o mapa. */
BOOL Bc250UmdVkEnsureUploadStaging(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, VkDeviceSize Bytes,
    _Out_ BOOL *pGrew, _Out_ uint64_t *pAllocMicros);

VkFormat Bc250UmdVkFormatFromDxgi(uint32_t dxgiFormat);
/* DX.24: igual, mas devolve VK_FORMAT_UNDEFINED para formato nao mapeado explicitamente. */
VkFormat Bc250UmdVkFormatFromDxgiStrict(uint32_t dxgiFormat);
/* DX.24: swizzle que uma view de AMOSTRAGEM precisa para imitar o D3D (X8 -> alpha 1, A8 -> so' alpha). */
VkComponentMapping Bc250UmdVkSrvComponentMapping(uint32_t dxgiFormat);
BOOL Bc250UmdVkEnsureLinearPresentBuf(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, uint32_t width, uint32_t height);
uint32_t Bc250UmdVkInspectLinearPresentBuf(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, uint32_t width, uint32_t height);

#endif /* BC250_UMD_VULKAN_BRIDGE_H */

