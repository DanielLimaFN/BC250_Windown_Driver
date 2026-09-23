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

VkFormat Bc250UmdVkFormatFromDxgi(uint32_t dxgiFormat);
/* DX.24: igual, mas devolve VK_FORMAT_UNDEFINED para formato nao mapeado explicitamente. */
VkFormat Bc250UmdVkFormatFromDxgiStrict(uint32_t dxgiFormat);
/* DX.24: swizzle que uma view de AMOSTRAGEM precisa para imitar o D3D (X8 -> alpha 1, A8 -> so' alpha). */
VkComponentMapping Bc250UmdVkSrvComponentMapping(uint32_t dxgiFormat);
BOOL Bc250UmdVkEnsureLinearPresentBuf(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, uint32_t width, uint32_t height);
uint32_t Bc250UmdVkInspectLinearPresentBuf(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, uint32_t width, uint32_t height);

#endif /* BC250_UMD_VULKAN_BRIDGE_H */

