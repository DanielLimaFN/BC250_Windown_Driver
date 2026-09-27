#include "bc250_d3d_umd.h"
#include "bc250_umd_pipeline_cache.h"
#include "bc250_wddm_winsys.h"
#include "default_vs_spv.inl"
#include "default_ps_spv.inl"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static void Bc250PsoTrace(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    Bc250WinsysTraceRaw(buf);
}

#define BC250_MAX_SPIRV_IDS 4096

typedef struct _BC250_SPIRV_REFLECT_CTX {
    uint8_t idType[BC250_MAX_SPIRV_IDS];
    uint32_t varType[BC250_MAX_SPIRV_IDS];
    uint32_t varStorage[BC250_MAX_SPIRV_IDS];
    int32_t idBinding[BC250_MAX_SPIRV_IDS];
    int32_t idSet[BC250_MAX_SPIRV_IDS];
} BC250_SPIRV_REFLECT_CTX;

static uint32_t Bc250ReflectSpirvBindings(
    const uint32_t* words,
    uint32_t numWords,
    VkShaderStageFlags stageFlag,
    VkDescriptorSetLayoutBinding* outBindings,
    uint32_t maxBindings)
{
    BC250_SPIRV_REFLECT_CTX* ctx;
    uint32_t numBindings = 0;
    uint32_t i;

    if (!words || numWords < 5 || words[0] != 0x07230203 || words[3] > BC250_MAX_SPIRV_IDS || !outBindings || maxBindings == 0) return UINT32_MAX;

    ctx = (BC250_SPIRV_REFLECT_CTX*)calloc(1, sizeof(BC250_SPIRV_REFLECT_CTX));
    if (!ctx) return UINT32_MAX;

    for (i = 0; i < BC250_MAX_SPIRV_IDS; ++i) {
        ctx->idBinding[i] = -1;
        ctx->idSet[i] = -1;
    }

    i = 5;
    while (i < numWords) {
        uint32_t header = words[i];
        uint32_t op = header & 0xFFFF;
        uint32_t len = (header >> 16) & 0xFFFF;
        if (len == 0 || len > numWords - i) { free(ctx); return UINT32_MAX; }

        if (op == 71 && len >= 4) { /* OpDecorate */
            uint32_t target = words[i + 1];
            uint32_t dec = words[i + 2];
            if (target < BC250_MAX_SPIRV_IDS) {
                if (dec == 33) ctx->idBinding[target] = (int32_t)words[i + 3];
                else if (dec == 34) ctx->idSet[target] = (int32_t)words[i + 3];
            }
        } else if (op == 59 && len >= 4) { /* OpVariable */
            uint32_t typeId = words[i + 1];
            uint32_t varId = words[i + 2];
            uint32_t storage = words[i + 3];
            if (varId < BC250_MAX_SPIRV_IDS) {
                ctx->varType[varId] = typeId;
                ctx->varStorage[varId] = storage;
            }
        } else if (op == 30 && len >= 2) { /* OpTypeStruct */
            uint32_t resId = words[i + 1];
            if (resId < BC250_MAX_SPIRV_IDS) ctx->idType[resId] = 1;
        } else if (op == 26 && len >= 2) { /* OpTypeSampler */
            uint32_t resId = words[i + 1];
            if (resId < BC250_MAX_SPIRV_IDS) ctx->idType[resId] = 2;
        } else if (op == 25 && len >= 9) { /* OpTypeImage: sampled, not buffer/storage */
            uint32_t resId = words[i + 1];
            if (resId < BC250_MAX_SPIRV_IDS && words[i + 3] != 5u && words[i + 7] == 1u) ctx->idType[resId] = 3;
        } else if (op == 27 && len >= 2) { /* OpTypeSampledImage */
            uint32_t resId = words[i + 1];
            if (resId < BC250_MAX_SPIRV_IDS) ctx->idType[resId] = 4;
        } else if (op == 32 && len >= 4) { /* OpTypePointer */
            uint32_t resId = words[i + 1];
            uint32_t targetType = words[i + 3];
            if (resId < BC250_MAX_SPIRV_IDS && targetType < BC250_MAX_SPIRV_IDS) {
                ctx->idType[resId] = ctx->idType[targetType];
            }
        }
        i += len;
    }

    for (uint32_t varId = 0; varId < BC250_MAX_SPIRV_IDS; ++varId) {
        if (ctx->idBinding[varId] >= 0) {
            uint32_t bindingNum = (uint32_t)ctx->idBinding[varId];
            uint32_t type = ctx->varType[varId];
            uint32_t kind = (type < BC250_MAX_SPIRV_IDS) ? ctx->idType[type] : 0;
            uint32_t descType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

            if (ctx->idSet[varId] != 0 || kind == 0 || kind == 4 || numBindings >= maxBindings) {
                free(ctx); return UINT32_MAX; /* no silent truncation or guessed descriptor type */
            }

            if (kind == 1 || ctx->varStorage[varId] == 2) {
                descType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            } else if (kind == 2) {
                descType = VK_DESCRIPTOR_TYPE_SAMPLER;
            } else if (kind == 3) {
                descType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            } else if (kind == 4) {
                descType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }

            if (numBindings < maxBindings) {
                outBindings[numBindings].binding = bindingNum;
                outBindings[numBindings].descriptorType = descType;
                outBindings[numBindings].descriptorCount = 1;
                outBindings[numBindings].stageFlags = stageFlag;
                outBindings[numBindings].pImmutableSamplers = NULL;
                numBindings++;
            }
        }
    }

    free(ctx);
    return numBindings;
}

BOOL Bc250UmdPipelineCacheInit(
    _Out_ BC250_UMD_PIPELINE_CACHE* pCache,
    _In_ BC250_UMD_DEVICE* pDevice)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    VkShaderModuleCreateInfo vsInfo;
    VkShaderModuleCreateInfo psInfo;
    VkResult res;

    if (pCache == NULL || pDevice == NULL) {
        return FALSE;
    }

    memset(pCache, 0, sizeof(*pCache));
    pCache->pDevice = pDevice;

    if (!pDevice->VkBridgeInitialized || !pDevice->VkBridge.DeviceActive) {
        return FALSE;
    }

    pBridge = &pDevice->VkBridge;
    Bc250PsoTrace("[VK_PSO] Bc250UmdPipelineCacheInit: criando shaders SPIR-V e layout padrao...");

    /* Create fallback default VS */
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(s_DefaultVsSpirv);
    vsInfo.pCode = (const uint32_t*)(const void*)s_DefaultVsSpirv;
    res = pBridge->vk.vkCreateShaderModule(pBridge->Device, &vsInfo, NULL, &pCache->DefaultVsModule);
    if (res != VK_SUCCESS) {
        Bc250PsoTrace("[VK_PSO] ERRO: falha ao criar Default VS Module: res=%d", res);
        pCache->DefaultVsModule = VK_NULL_HANDLE;
    }

    /* Create fallback default PS */
    memset(&psInfo, 0, sizeof(psInfo));
    psInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    psInfo.codeSize = sizeof(s_DefaultPsSpirv);
    psInfo.pCode = (const uint32_t*)(const void*)s_DefaultPsSpirv;
    res = pBridge->vk.vkCreateShaderModule(pBridge->Device, &psInfo, NULL, &pCache->DefaultPsModule);
    if (res != VK_SUCCESS) {
        Bc250PsoTrace("[VK_PSO] ERRO: falha ao criar Default PS Module: res=%d", res);
        pCache->DefaultPsModule = VK_NULL_HANDLE;
    }

    /* Default fallback shaders do not require descriptor set layouts */
    pCache->DefaultDescriptorSetLayout = VK_NULL_HANDLE;

    pCache->DefaultShadersCreated = (pCache->DefaultVsModule != VK_NULL_HANDLE &&
                                     pCache->DefaultPsModule != VK_NULL_HANDLE);
    Bc250PsoTrace("[VK_PSO] Shaders padrao: VS=%p, PS=%p (Criados=%d)",
        (void*)pCache->DefaultVsModule, (void*)pCache->DefaultPsModule, (int)pCache->DefaultShadersCreated);
    return TRUE;
}

VOID Bc250UmdPipelineCacheDestroy(
    _Inout_ BC250_UMD_PIPELINE_CACHE* pCache)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    uint32_t i;

    if (pCache == NULL || pCache->pDevice == NULL || !pCache->pDevice->VkBridgeInitialized) {
        return;
    }
    pBridge = &pCache->pDevice->VkBridge;
    (void)Bc250UmdVkDrainPending(pBridge, "destruir cache de pipelines");

    for (i = 0; i < BC250_PSO_CACHE_CAPACITY; ++i) {
        if (pCache->Entries[i].InUse) {
            if (pCache->Entries[i].Pipeline != VK_NULL_HANDLE) {
                pBridge->vk.vkDestroyPipeline(pBridge->Device, pCache->Entries[i].Pipeline, NULL);
                pCache->Entries[i].Pipeline = VK_NULL_HANDLE;
            }
            if (pCache->Entries[i].PipelineLayout != VK_NULL_HANDLE) {
                pBridge->vk.vkDestroyPipelineLayout(pBridge->Device, pCache->Entries[i].PipelineLayout, NULL);
                pCache->Entries[i].PipelineLayout = VK_NULL_HANDLE;
            }
            if (pCache->Entries[i].DescriptorSetLayout != VK_NULL_HANDLE &&
                pCache->Entries[i].DescriptorSetLayout != pCache->DefaultDescriptorSetLayout) {
                pBridge->vk.vkDestroyDescriptorSetLayout(pBridge->Device, pCache->Entries[i].DescriptorSetLayout, NULL);
                pCache->Entries[i].DescriptorSetLayout = VK_NULL_HANDLE;
            }
            pCache->Entries[i].InUse = FALSE;
        }
    }

    if (pCache->DefaultDescriptorSetLayout != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyDescriptorSetLayout(pBridge->Device, pCache->DefaultDescriptorSetLayout, NULL);
        pCache->DefaultDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (pCache->DefaultVsModule != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyShaderModule(pBridge->Device, pCache->DefaultVsModule, NULL);
        pCache->DefaultVsModule = VK_NULL_HANDLE;
    }
    if (pCache->DefaultPsModule != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyShaderModule(pBridge->Device, pCache->DefaultPsModule, NULL);
        pCache->DefaultPsModule = VK_NULL_HANDLE;
    }
    pCache->DefaultShadersCreated = FALSE;
    pCache->EntryCount = 0;
}

BOOL Bc250UmdVkCreateShaderModule(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_SHADER* pShader)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    VkShaderModuleCreateInfo smInfo;
    VkResult res;

    if (pDevice == NULL || pShader == NULL || !pDevice->VkBridgeInitialized) {
        return FALSE;
    }
    pBridge = &pDevice->VkBridge;

    if (!pShader->SpirvValid || pShader->Spirv.SizeBytes == 0 ||
        pShader->Spirv.Words == NULL) {
        Bc250PsoTrace("[VK_PSO] Bc250UmdVkCreateShaderModule RECUSADO: SPIR-V invalido ou tamanho zero");
        return FALSE;
    }

    if (pShader->VkModule != VK_NULL_HANDLE) {
        return TRUE;
    }

    Bc250PsoTrace("[VK_PSO] Bc250UmdVkCreateShaderModule: criando shader module a partir de SPIR-V (%u bytes)...",
        pShader->Spirv.SizeBytes);

    memset(&smInfo, 0, sizeof(smInfo));
    smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = pShader->Spirv.SizeBytes;
    smInfo.pCode = pShader->Spirv.Words;

    {
        VkShaderStageFlags stage = (pShader->Type == BC250_DXBC_SHADER_PIXEL) ?
            VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        pShader->NumBindings = Bc250ReflectSpirvBindings(smInfo.pCode,
            pShader->Spirv.SizeBytes / 4, stage, pShader->Bindings, 158);
        if (pShader->NumBindings == UINT32_MAX) {
            pShader->NumBindings = 0;
            Bc250PsoTrace("[VK_DESCRIPTOR] RECUSADO: reflection unsupported or malformed");
            return FALSE;
        }
    }
    res = pBridge->vk.vkCreateShaderModule(pBridge->Device, &smInfo, NULL, &pShader->VkModule);
    if (res == VK_SUCCESS) {
        VkShaderStageFlags stage = (pShader->Type == BC250_DXBC_SHADER_PIXEL) ?
            VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        pShader->VkModuleCreated = TRUE;
        Bc250PsoTrace("[VK_PSO] SUCESSO: Shader Module criado: module=%p (stage=0x%X, bindings=%u)",
            (void*)pShader->VkModule, stage, pShader->NumBindings);
        return TRUE;
    }

    Bc250PsoTrace("[VK_PSO] ERRO CRITICO: vkCreateShaderModule falhou: res=%d", res);
    return FALSE;
}

VOID Bc250UmdVkDestroyShaderModule(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_SHADER* pShader)
{
    if (pDevice == NULL || pShader == NULL || !pDevice->VkBridgeInitialized) return;
    if (pShader->DescriptorSetLayout != VK_NULL_HANDLE || pShader->VkModule != VK_NULL_HANDLE) {
        (void)Bc250UmdVkDrainPending(&pDevice->VkBridge, "destruir shader");
    }
    if (pShader->DescriptorSetLayout != VK_NULL_HANDLE) {
        pDevice->VkBridge.vk.vkDestroyDescriptorSetLayout(pDevice->VkBridge.Device, pShader->DescriptorSetLayout, NULL);
        pShader->DescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (pShader->VkModule != VK_NULL_HANDLE) {
        pDevice->VkBridge.vk.vkDestroyShaderModule(pDevice->VkBridge.Device, pShader->VkModule, NULL);
        pShader->VkModule = VK_NULL_HANDLE;
    }
    pShader->VkModuleCreated = FALSE;
}

BOOL Bc250UmdPipelineCacheGetOrCreate(
    _Inout_ BC250_UMD_PIPELINE_CACHE* pCache,
    _In_ CONST BC250_PSO_KEY* pKey,
    _Out_ VkPipeline* pOutPipeline,
    _Out_ VkPipelineLayout* pOutLayout)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    BC250_PSO_KEY effectiveKey;
    uint32_t i;
    uint32_t freeSlot = UINT32_MAX;
    uint32_t oldestSlot = 0;
    uint64_t oldestSeq = UINT64_MAX;

    VkPipelineShaderStageCreateInfo stages[2];
    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkPipelineViewportStateCreateInfo viewportState;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineColorBlendStateCreateInfo colorBlending;
    VkDynamicState dynamicStates[3] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS /* DX.24: D3D11 BLEND_FACTOR vem do OMSetBlendState */
    };
    VkPipelineDynamicStateCreateInfo dynamicState;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo;
    VkGraphicsPipelineCreateInfo pipelineInfo;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult res;

    if (pCache == NULL || pKey == NULL || pOutPipeline == NULL || pOutLayout == NULL) {
        return FALSE;
    }
    if (pCache->pDevice == NULL || !pCache->pDevice->VkBridgeInitialized) {
        return FALSE;
    }
    pBridge = &pCache->pDevice->VkBridge;

    effectiveKey = *pKey;
    if (effectiveKey.VsModule == VK_NULL_HANDLE || effectiveKey.PsModule == VK_NULL_HANDLE) {
        Bc250PsoTrace("[VK_PSO] RECUSA: VsModule (%p) ou PsModule (%p) ausente",
            (void*)effectiveKey.VsModule, (void*)effectiveKey.PsModule);
        return FALSE;
    }
    if (effectiveKey.RenderPass == VK_NULL_HANDLE) {
        Bc250PsoTrace("[VK_PSO] RECUSA: RenderPass nulo");
        return FALSE;
    }

    /* 1. Lookup in Cache */
    for (i = 0; i < BC250_PSO_CACHE_CAPACITY; ++i) {
        if (pCache->Entries[i].InUse) {
            if (memcmp(&pCache->Entries[i].Key, &effectiveKey, sizeof(BC250_PSO_KEY)) == 0) {
                pCache->Entries[i].LastUsedSequence = ++pCache->AccessSequence;
                *pOutPipeline = pCache->Entries[i].Pipeline;
                *pOutLayout = pCache->Entries[i].PipelineLayout;
                Bc250PsoTrace("[VK_PSO] Cache HIT no slot %u: pipeline=%p, layout=%p",
                    i, (void*)*pOutPipeline, (void*)*pOutLayout);
                return TRUE;
            }
            if (pCache->Entries[i].LastUsedSequence < oldestSeq) {
                oldestSeq = pCache->Entries[i].LastUsedSequence;
                oldestSlot = i;
            }
        } else if (freeSlot == UINT32_MAX) {
            freeSlot = i;
        }
    }

    Bc250PsoTrace("[VK_PSO] Cache MISS. Compilando novo VkGraphicsPipeline (VS=%p, PS=%p, RP=%p, Topo=%u)...",
        (void*)effectiveKey.VsModule, (void*)effectiveKey.PsModule,
        (void*)effectiveKey.RenderPass, effectiveKey.Topology);

    /* 2. Build New Graphics Pipeline */
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = effectiveKey.VsModule;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = effectiveKey.PsModule;
    stages[1].pName = "main";

    memset(&vertexInputInfo, 0, sizeof(vertexInputInfo));
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = effectiveKey.NumInputBindings;
    vertexInputInfo.pVertexBindingDescriptions = (effectiveKey.NumInputBindings > 0) ? effectiveKey.InputBindings : NULL;
    vertexInputInfo.vertexAttributeDescriptionCount = effectiveKey.NumInputElements;
    vertexInputInfo.pVertexAttributeDescriptions = (effectiveKey.NumInputElements > 0) ? effectiveKey.InputAttributes : NULL;

    memset(&inputAssembly, 0, sizeof(inputAssembly));
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = effectiveKey.Topology ? effectiveKey.Topology : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    memset(&viewportState, 0, sizeof(viewportState));
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    memset(&rasterizer, 0, sizeof(rasterizer));
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = effectiveKey.PolygonMode ? effectiveKey.PolygonMode : VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = effectiveKey.CullMode;
    rasterizer.frontFace = effectiveKey.FrontFace ? effectiveKey.FrontFace : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = effectiveKey.DepthBiasEnable ? VK_TRUE : VK_FALSE;

    memset(&multisampling, 0, sizeof(multisampling));
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = effectiveKey.SampleCount ? (VkSampleCountFlagBits)effectiveKey.SampleCount : VK_SAMPLE_COUNT_1_BIT;
    multisampling.alphaToCoverageEnable = effectiveKey.AlphaToCoverageEnable ? VK_TRUE : VK_FALSE;

    memset(&depthStencil, 0, sizeof(depthStencil));
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = effectiveKey.DepthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = effectiveKey.DepthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = effectiveKey.DepthCompareOp ? effectiveKey.DepthCompareOp : VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    memset(&colorBlendAttachment, 0, sizeof(colorBlendAttachment));
    /* DX.24: com blend state vinculado, mascara 0 e' "nao escrever" (D3D11), nao "padrao". */
    colorBlendAttachment.colorWriteMask = effectiveKey.BlendFactorsExplicit ? effectiveKey.ColorWriteMask :
        (effectiveKey.ColorWriteMask ? effectiveKey.ColorWriteMask : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT));
    colorBlendAttachment.blendEnable = effectiveKey.BlendEnable ? VK_TRUE : VK_FALSE;
    if (effectiveKey.BlendFactorsExplicit) {
        /* DX.24: valor pedido pelo D3D11, inclusive ZERO/ADD (que valem 0). */
        colorBlendAttachment.srcColorBlendFactor = effectiveKey.SrcColorBlendFactor;
        colorBlendAttachment.dstColorBlendFactor = effectiveKey.DstColorBlendFactor;
        colorBlendAttachment.colorBlendOp = effectiveKey.ColorBlendOp;
        colorBlendAttachment.srcAlphaBlendFactor = effectiveKey.SrcAlphaBlendFactor;
        colorBlendAttachment.dstAlphaBlendFactor = effectiveKey.DstAlphaBlendFactor;
        colorBlendAttachment.alphaBlendOp = effectiveKey.AlphaBlendOp;
    } else {
        /* Padrao D3D11 (sem blend state): ONE/ZERO/ADD. */
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    memset(&colorBlending, 0, sizeof(colorBlending));
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    memset(&dynamicState, 0, sizeof(dynamicState));
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 3;
    dynamicState.pDynamicStates = dynamicStates;

    /* 2b. Build Descriptor Set Layout & Pipeline Layout */
    VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;
    VkPushConstantRange pcRange;
    VkDescriptorSetLayoutBinding combinedBindings[BC250_DESCRIPTOR_MAX_BINDINGS];
    uint32_t combinedCount = 0;

    memset(combinedBindings, 0, sizeof(combinedBindings));
    combinedCount = 0;

    if (effectiveKey.pVs != NULL && effectiveKey.pVs->NumBindings > 0) {
        for (uint32_t b = 0; b < effectiveKey.pVs->NumBindings && combinedCount < BC250_DESCRIPTOR_MAX_BINDINGS; ++b) {
            combinedBindings[combinedCount++] = effectiveKey.pVs->Bindings[b];
        }
    }

    if (effectiveKey.pPs != NULL && effectiveKey.pPs->NumBindings > 0) {
        for (uint32_t b = 0; b < effectiveKey.pPs->NumBindings && combinedCount < BC250_DESCRIPTOR_MAX_BINDINGS; ++b) {
            uint32_t psBind = effectiveKey.pPs->Bindings[b].binding;
            BOOL found = FALSE;
            for (uint32_t c = 0; c < combinedCount; ++c) {
                if (combinedBindings[c].binding == psBind) {
                    combinedBindings[c].stageFlags |= effectiveKey.pPs->Bindings[b].stageFlags;
                    found = TRUE;
                    break;
                }
            }
            if (!found) {
                combinedBindings[combinedCount++] = effectiveKey.pPs->Bindings[b];
            }
        }
    } else if (effectiveKey.PsModule == pCache->DefaultPsModule && pCache->DefaultDescriptorSetLayout != VK_NULL_HANDLE) {
        if (combinedCount == 0) {
            dsLayout = pCache->DefaultDescriptorSetLayout;
        } else {
            BOOL found = FALSE;
            for (uint32_t c = 0; c < combinedCount; ++c) {
                if (combinedBindings[c].binding == 0) {
                    found = TRUE;
                    break;
                }
            }
            if (!found && combinedCount < BC250_DESCRIPTOR_MAX_BINDINGS) {
                combinedBindings[combinedCount].binding = 0;
                combinedBindings[combinedCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                combinedBindings[combinedCount].descriptorCount = 1;
                combinedBindings[combinedCount].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                combinedBindings[combinedCount].pImmutableSamplers = NULL;
                combinedCount++;
            }
        }
    }

    if (dsLayout == VK_NULL_HANDLE) {
        if (combinedCount > 0) {
            VkDescriptorSetLayoutCreateInfo dsInfo;
            memset(&dsInfo, 0, sizeof(dsInfo));
            dsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            dsInfo.bindingCount = combinedCount;
            dsInfo.pBindings = combinedBindings;

            res = pBridge->vk.vkCreateDescriptorSetLayout(pBridge->Device, &dsInfo, NULL, &dsLayout);
            if (res != VK_SUCCESS) {
                Bc250PsoTrace("[VK_PSO] ERRO: vkCreateDescriptorSetLayout falhou: res=%d", res);
                dsLayout = pCache->DefaultDescriptorSetLayout;
            } else {
                Bc250PsoTrace("[VK_PSO] Novo VkDescriptorSetLayout criado com %u bindings: layout=%p",
                    combinedCount, (void*)dsLayout);
            }
        } else {
            dsLayout = pCache->DefaultDescriptorSetLayout;
        }
    }

    if (combinedCount > 0 && dsLayout == VK_NULL_HANDLE) {
        Bc250PsoTrace("[VK_PSO] ERRO CRITICO (D1.3): Shaders requerem descritores (count=%u) mas VkDescriptorSetLayout e nulo -> recusando criacao",
            combinedCount);
        return FALSE;
    }

    memset(&pcRange, 0, sizeof(pcRange));
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = 128;

    memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (dsLayout != VK_NULL_HANDLE) {
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &dsLayout;
    }
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;

    res = pBridge->vk.vkCreatePipelineLayout(pBridge->Device, &pipelineLayoutInfo, NULL, &pipelineLayout);
    if (res != VK_SUCCESS) {
        Bc250PsoTrace("[VK_PSO] ERRO CRITICO: vkCreatePipelineLayout falhou: res=%d", res);
        if (dsLayout != VK_NULL_HANDLE && dsLayout != pCache->DefaultDescriptorSetLayout) {
            pBridge->vk.vkDestroyDescriptorSetLayout(pBridge->Device, dsLayout, NULL);
        }
        return FALSE;
    }

    memset(&pipelineInfo, 0, sizeof(pipelineInfo));
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = effectiveKey.RenderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    /* DX.21: captura somente de leitura do programa final; nao altera o codigo gerado. */
    if (pBridge->PipelineIrCaptureEnabled) {
        pipelineInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
    }

    BOOL psoSuccess = FALSE;
    __try {
        res = pBridge->vk.vkCreateGraphicsPipelines(pBridge->Device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
        if (res == VK_SUCCESS && pipeline != VK_NULL_HANDLE) {
            psoSuccess = TRUE;
        } else {
            Bc250PsoTrace("[VK_PSO] AVISO: vkCreateGraphicsPipelines retornou res=%d (VS=%p, PS=%p, Layout=%p)",
                res, (void*)effectiveKey.VsModule, (void*)effectiveKey.PsModule, (void*)pipelineLayout);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Bc250PsoTrace("[VK_PSO] EXCECAO INTERCEPTADA em vkCreateGraphicsPipelines: code=0x%08X (VS=%p, PS=%p)",
            GetExceptionCode(), (void*)effectiveKey.VsModule, (void*)effectiveKey.PsModule);
        psoSuccess = FALSE;
        pipeline = VK_NULL_HANDLE;
    }

    if (!psoSuccess || pipeline == VK_NULL_HANDLE) {
        if (pipelineLayout != VK_NULL_HANDLE) {
            pBridge->vk.vkDestroyPipelineLayout(pBridge->Device, pipelineLayout, NULL);
        }
        if (dsLayout != VK_NULL_HANDLE && dsLayout != pCache->DefaultDescriptorSetLayout) {
            pBridge->vk.vkDestroyDescriptorSetLayout(pBridge->Device, dsLayout, NULL);
        }
        return FALSE;
    }

    Bc250UmdVkDumpPipelineIr(pBridge, pipeline, (const void *)effectiveKey.VsModule, (const void *)effectiveKey.PsModule);

    /* 3. Insert into Cache */
    uint32_t targetSlot = (freeSlot != UINT32_MAX) ? freeSlot : oldestSlot;
    if (pCache->Entries[targetSlot].InUse) {
        /* DX.49: um draw do lote pode estar usando a pipeline despejada. */
        (void)Bc250UmdVkDrainPending(pBridge, "despejar pipeline do cache");
        if (pCache->Entries[targetSlot].Pipeline != VK_NULL_HANDLE) {
            pBridge->vk.vkDestroyPipeline(pBridge->Device, pCache->Entries[targetSlot].Pipeline, NULL);
        }
        if (pCache->Entries[targetSlot].PipelineLayout != VK_NULL_HANDLE) {
            pBridge->vk.vkDestroyPipelineLayout(pBridge->Device, pCache->Entries[targetSlot].PipelineLayout, NULL);
        }
        if (pCache->Entries[targetSlot].DescriptorSetLayout != VK_NULL_HANDLE &&
            pCache->Entries[targetSlot].DescriptorSetLayout != pCache->DefaultDescriptorSetLayout) {
            pBridge->vk.vkDestroyDescriptorSetLayout(pBridge->Device, pCache->Entries[targetSlot].DescriptorSetLayout, NULL);
        }
    } else {
        pCache->EntryCount++;
    }

    pCache->Entries[targetSlot].Key = effectiveKey;
    pCache->Entries[targetSlot].Pipeline = pipeline;
    pCache->Entries[targetSlot].PipelineLayout = pipelineLayout;
    pCache->Entries[targetSlot].DescriptorSetLayout = dsLayout;
    pCache->Entries[targetSlot].BindingCount = combinedCount;
    memcpy(pCache->Entries[targetSlot].Bindings, combinedBindings, combinedCount * sizeof(combinedBindings[0]));
    pCache->Entries[targetSlot].LastUsedSequence = ++pCache->AccessSequence;
    pCache->Entries[targetSlot].InUse = TRUE;

    Bc250PsoTrace("[VK_PSO] SUCESSO: Graphics Pipeline compilado e armazenado no slot %u (pipeline=%p, layout=%p, dsLayout=%p)",
        targetSlot, (void*)pipeline, (void*)pipelineLayout, (void*)dsLayout);

    *pOutPipeline = pipeline;
    *pOutLayout = pipelineLayout;
    return TRUE;
}
