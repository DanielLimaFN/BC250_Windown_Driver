#ifndef BC250_UMD_PIPELINE_CACHE_H
#define BC250_UMD_PIPELINE_CACHE_H

#include "bc250_umd_vulkan_bridge.h"
#include "bc250_descriptor_binding.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _BC250_UMD_DEVICE;
struct _BC250_UMD_SHADER;
struct _BC250_UMD_BLEND_STATE;
struct _BC250_UMD_DEPTH_STENCIL_STATE;
struct _BC250_UMD_RASTERIZER_STATE;
struct _BC250_UMD_ELEMENT_LAYOUT;

/* Key representing the complete graphics pipeline state */
typedef struct _BC250_PSO_KEY {
    VkShaderModule VsModule;
    VkShaderModule PsModule;
    VkShaderModule GsModule;
    struct _BC250_UMD_SHADER* pVs;
    struct _BC250_UMD_SHADER* pPs;
    VkRenderPass RenderPass;
    VkPrimitiveTopology Topology;
    uint32_t SampleCount;

    /* Blend state */
    BOOL BlendEnable;
    VkBlendFactor SrcColorBlendFactor;
    VkBlendFactor DstColorBlendFactor;
    VkBlendOp ColorBlendOp;
    VkBlendFactor SrcAlphaBlendFactor;
    VkBlendFactor DstAlphaBlendFactor;
    VkBlendOp AlphaBlendOp;
    VkColorComponentFlags ColorWriteMask;

    /* Depth/Stencil */
    BOOL DepthTestEnable;
    BOOL DepthWriteEnable;
    VkCompareOp DepthCompareOp;

    /* Rasterizer */
    VkPolygonMode PolygonMode;
    VkCullModeFlags CullMode;
    VkFrontFace FrontFace;
    BOOL DepthBiasEnable;

    /* Input layout identifier */
    uint64_t InputLayoutHash;
    uint32_t NumInputElements;
    VkVertexInputAttributeDescription InputAttributes[16];
    VkVertexInputBindingDescription InputBindings[16];
    uint32_t NumInputBindings;
} BC250_PSO_KEY;

/* Cache entry */
typedef struct _BC250_PSO_ENTRY {
    BC250_PSO_KEY Key;
    VkPipeline Pipeline;
    VkPipelineLayout PipelineLayout;
    VkDescriptorSetLayout DescriptorSetLayout;
    uint32_t BindingCount;
    VkDescriptorSetLayoutBinding Bindings[BC250_DESCRIPTOR_MAX_BINDINGS];
    uint64_t LastUsedSequence;
    BOOL InUse;
} BC250_PSO_ENTRY;

#define BC250_PSO_CACHE_CAPACITY 64

/* Pipeline State Object Cache */
typedef struct _BC250_UMD_PIPELINE_CACHE {
    BC250_PSO_ENTRY Entries[BC250_PSO_CACHE_CAPACITY];
    uint32_t EntryCount;
    uint64_t AccessSequence;
    struct _BC250_UMD_DEVICE* pDevice;

    /* Fallback default shaders */
    VkShaderModule DefaultVsModule;
    VkShaderModule DefaultPsModule;
    VkDescriptorSetLayout DefaultDescriptorSetLayout;
    BOOL DefaultShadersCreated;
} BC250_UMD_PIPELINE_CACHE;

/* Functions */
BOOL Bc250UmdPipelineCacheInit(
    _Out_ BC250_UMD_PIPELINE_CACHE* pCache,
    _In_ struct _BC250_UMD_DEVICE* pDevice);

VOID Bc250UmdPipelineCacheDestroy(
    _Inout_ BC250_UMD_PIPELINE_CACHE* pCache);

BOOL Bc250UmdPipelineCacheGetOrCreate(
    _Inout_ BC250_UMD_PIPELINE_CACHE* pCache,
    _In_ CONST BC250_PSO_KEY* pKey,
    _Out_ VkPipeline* pOutPipeline,
    _Out_ VkPipelineLayout* pOutLayout);

BOOL Bc250UmdVkCreateShaderModule(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_SHADER* pShader);

VOID Bc250UmdVkDestroyShaderModule(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_SHADER* pShader);

#ifdef __cplusplus
}
#endif

#endif /* BC250_UMD_PIPELINE_CACHE_H */
