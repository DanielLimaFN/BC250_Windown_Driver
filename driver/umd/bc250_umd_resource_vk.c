#include "bc250_d3d_umd.h"
#include "bc250_umd_resource_vk.h"
#include "bc250_wddm_winsys.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static void Bc250VkResTrace(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    Bc250WinsysTraceRaw(buf);
}

BOOL Bc250UmdVkCreateResource(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RESOURCE* pRes,
    _In_ CONST D3D10DDIARG_CREATERESOURCE* pCreate)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    VkResult res;

    if (pDevice == NULL || pRes == NULL || pCreate == NULL) {
        return FALSE;
    }

    if (!pDevice->VkBridgeInitialized || !pDevice->VkBridge.DeviceActive) {
        return FALSE;
    }

    pBridge = &pDevice->VkBridge;

    if (pCreate->ResourceDimension == D3D10DDIRESOURCE_BUFFER) {
        VkBufferCreateInfo bufInfo;
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo allocInfo;
        uint32_t memTypeIndex;
        VkDeviceSize size = pRes->TotalByteSize;
        if (size == 0) size = 256;

        Bc250VkResTrace("[VK_RES] Bc250UmdVkCreateResource: BUFFER size=%llu bytes (bindFlags=0x%X)",
            size, pCreate->BindFlags);

        memset(&bufInfo, 0, sizeof(bufInfo));
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = size;
        bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        res = pBridge->vk.vkCreateBuffer(pBridge->Device, &bufInfo, NULL, &pRes->VkBuf);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] ERRO CRITICO: vkCreateBuffer falhou: res=%d", res);
            return FALSE;
        }

        pBridge->vk.vkGetBufferMemoryRequirements(pBridge->Device, pRes->VkBuf, &memReqs);

        memTypeIndex = Bc250UmdVkFindMemoryType(
            pBridge, memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memTypeIndex == UINT32_MAX) {
            memTypeIndex = Bc250UmdVkFindMemoryType(
                pBridge, memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }

        Bc250VkResTrace("[VK_RES] Buffer MemoryRequirements: reqSize=%llu, memTypeIndex=%u",
            memReqs.size, memTypeIndex);

        memset(&allocInfo, 0, sizeof(allocInfo));
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = (memTypeIndex != UINT32_MAX) ? memTypeIndex : 0;

        res = pBridge->vk.vkAllocateMemory(pBridge->Device, &allocInfo, NULL, &pRes->VkMem);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] ERRO CRITICO: vkAllocateMemory falhou: res=%d (size=%llu, typeIndex=%u)",
                res, memReqs.size, allocInfo.memoryTypeIndex);
            pBridge->vk.vkDestroyBuffer(pBridge->Device, pRes->VkBuf, NULL);
            pRes->VkBuf = VK_NULL_HANDLE;
            return FALSE;
        }

        res = pBridge->vk.vkBindBufferMemory(pBridge->Device, pRes->VkBuf, pRes->VkMem, 0);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] ERRO CRITICO: vkBindBufferMemory falhou: res=%d", res);
            pBridge->vk.vkFreeMemory(pBridge->Device, pRes->VkMem, NULL);
            pBridge->vk.vkDestroyBuffer(pBridge->Device, pRes->VkBuf, NULL);
            pRes->VkMem = VK_NULL_HANDLE;
            pRes->VkBuf = VK_NULL_HANDLE;
            return FALSE;
        }

        pRes->VkMemSize = memReqs.size;
        pRes->VkResourceCreated = TRUE;
        if (pCreate->pInitialDataUP != NULL && pCreate->pInitialDataUP->pSysMem != NULL) {
            SIZE_T uploadBytes = (pRes->Width > 0 && pRes->Width < (UINT)size) ? (SIZE_T)pRes->Width : (SIZE_T)size;
            (void)Bc250UmdVkUploadBufferData(pDevice, pRes, pCreate->pInitialDataUP->pSysMem, uploadBytes);
            Bc250VkResTrace("[VK_RES] Buffer InitialDataUP carregado via Vulkan (%llu bytes)", (unsigned long long)uploadBytes);
        } else if (pRes->pCpuShadow != NULL) {
            SIZE_T uploadBytes = (pRes->Width > 0 && pRes->Width < (UINT)size) ? (SIZE_T)pRes->Width : (SIZE_T)size;
            (void)Bc250UmdVkUploadBufferData(pDevice, pRes, pRes->pCpuShadow, uploadBytes);
            Bc250VkResTrace("[VK_RES] Buffer Shadow carregado via Vulkan (%llu bytes)", (unsigned long long)uploadBytes);
        }
        Bc250VkResTrace("[VK_RES] SUCESSO: Buffer criado (buf=%p, mem=%p, size=%llu)",
            (void*)pRes->VkBuf, (void*)pRes->VkMem, size);
        return TRUE;
    }
    else if (pCreate->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D) {
        VkImageCreateInfo imgInfo;
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo allocInfo;
        uint32_t memTypeIndex;
        VkFormat vkFormat = Bc250UmdVkFormatFromDxgi(pRes->Format);
        if (vkFormat == VK_FORMAT_UNDEFINED) {
            vkFormat = VK_FORMAT_B8G8R8A8_UNORM;
        }

        Bc250VkResTrace("[VK_RES] Bc250UmdVkCreateResource: TEXTURE2D %ux%u (dxgiFmt=%u -> vkFmt=%u, bindFlags=0x%X)",
            pRes->Width, pRes->Height, pRes->Format, vkFormat, pCreate->BindFlags);

        memset(&imgInfo, 0, sizeof(imgInfo));
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = vkFormat;
        imgInfo.extent.width = (pRes->Width > 0) ? pRes->Width : 1;
        imgInfo.extent.height = (pRes->Height > 0) ? pRes->Height : 1;
        imgInfo.extent.depth = 1;
        imgInfo.mipLevels = (pRes->MipLevels > 0) ? pRes->MipLevels : 1;
        imgInfo.arrayLayers = (pRes->ArraySize > 0) ? pRes->ArraySize : 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (pCreate->BindFlags & D3D10_DDI_BIND_RENDER_TARGET) {
            imgInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if (pCreate->BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL) {
            imgInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        if (pCreate->BindFlags & D3D10_DDI_BIND_SHADER_RESOURCE) {
            imgInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        imgInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        res = pBridge->vk.vkCreateImage(pBridge->Device, &imgInfo, NULL, &pRes->VkImg);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] ERRO CRITICO: vkCreateImage falhou: res=%d", res);
            return FALSE;
        }

        pBridge->vk.vkGetImageMemoryRequirements(pBridge->Device, pRes->VkImg, &memReqs);

        memTypeIndex = Bc250UmdVkFindMemoryType(
            pBridge, memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memTypeIndex == UINT32_MAX) {
            memTypeIndex = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits, 0);
        }

        Bc250VkResTrace("[VK_RES] Image MemoryRequirements: reqSize=%llu, memTypeIndex=%u",
            memReqs.size, memTypeIndex);

        memset(&allocInfo, 0, sizeof(allocInfo));
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = (memTypeIndex != UINT32_MAX) ? memTypeIndex : 0;

        res = pBridge->vk.vkAllocateMemory(pBridge->Device, &allocInfo, NULL, &pRes->VkMem);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] ERRO CRITICO: vkAllocateMemory (Image) falhou: res=%d (size=%llu, typeIndex=%u)",
                res, memReqs.size, allocInfo.memoryTypeIndex);
            pBridge->vk.vkDestroyImage(pBridge->Device, pRes->VkImg, NULL);
            pRes->VkImg = VK_NULL_HANDLE;
            return FALSE;
        }

        res = pBridge->vk.vkBindImageMemory(pBridge->Device, pRes->VkImg, pRes->VkMem, 0);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] ERRO CRITICO: vkBindImageMemory falhou: res=%d", res);
            pBridge->vk.vkFreeMemory(pBridge->Device, pRes->VkMem, NULL);
            pBridge->vk.vkDestroyImage(pBridge->Device, pRes->VkImg, NULL);
            pRes->VkMem = VK_NULL_HANDLE;
            pRes->VkImg = VK_NULL_HANDLE;
            return FALSE;
        }

        pRes->VkMemSize = memReqs.size;
        pRes->VkLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        pRes->VkResourceCreated = TRUE;
        Bc250VkResTrace("[VK_RES] SUCESSO: Imagem criada (img=%p, mem=%p, size=%llu)",
            (void*)pRes->VkImg, (void*)pRes->VkMem, memReqs.size);
        return TRUE;
    }

    Bc250VkResTrace("[VK_RES] Dimensao de recurso nao suportada: %u", pCreate->ResourceDimension);
    return FALSE;
}

VOID Bc250UmdVkDestroyResource(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RESOURCE* pRes)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    if (pDevice == NULL || pRes == NULL || !pDevice->VkBridgeInitialized) return;
    pBridge = &pDevice->VkBridge;

    Bc250VkResTrace("[VK_RES] Bc250UmdVkDestroyResource: buf=%p, img=%p, mem=%p",
        (void*)pRes->VkBuf, (void*)pRes->VkImg, (void*)pRes->VkMem);

    if (pRes->VkBuf != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyBuffer(pBridge->Device, pRes->VkBuf, NULL);
        pRes->VkBuf = VK_NULL_HANDLE;
    }
    if (pRes->VkImg != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyImage(pBridge->Device, pRes->VkImg, NULL);
        pRes->VkImg = VK_NULL_HANDLE;
    }
    if (pRes->VkMem != VK_NULL_HANDLE) {
        pBridge->vk.vkFreeMemory(pBridge->Device, pRes->VkMem, NULL);
        pRes->VkMem = VK_NULL_HANDLE;
    }
    pRes->VkResourceCreated = FALSE;
}

BOOL Bc250UmdVkCreateRtv(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RTV* pRtv,
    _In_ CONST D3D10DDIARG_CREATERENDERTARGETVIEW* pCreate)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    BC250_UMD_RESOURCE* pRes;
    VkImageViewCreateInfo viewInfo;
    VkAttachmentDescription attachment;
    VkAttachmentReference colorRef;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo rpInfo;
    VkFramebufferCreateInfo fbInfo;
    VkFormat vkFormat;
    VkResult res;

    UNREFERENCED_PARAMETER(pCreate);

    if (pDevice == NULL || pRtv == NULL || !pDevice->VkBridgeInitialized) {
        return FALSE;
    }
    pBridge = &pDevice->VkBridge;
    pRes = pRtv->pResource;
    if (pRes == NULL || pRes->VkImg == VK_NULL_HANDLE) {
        Bc250VkResTrace("[VK_RES] Bc250UmdVkCreateRtv RECUSADO: recurso ausente ou VkImg nulo (pRes=%p)", (void*)pRes);
        return FALSE;
    }

    vkFormat = Bc250UmdVkFormatFromDxgi(pRtv->Format ? pRtv->Format : pRes->Format);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        vkFormat = VK_FORMAT_B8G8R8A8_UNORM;
    }

    Bc250VkResTrace("[VK_RES] Bc250UmdVkCreateRtv: res=%p, img=%p, format=%u, dim=%ux%u",
        (void*)pRes, (void*)pRes->VkImg, vkFormat, pRes->Width, pRes->Height);

    /* 1. Create Image View */
    memset(&viewInfo, 0, sizeof(viewInfo));
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = pRes->VkImg;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vkFormat;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    uint32_t resLayers = (pRes->ArraySize > 0) ? pRes->ArraySize : 1;
    uint32_t baseLayer = pRtv->FirstArraySlice;
    uint32_t layerCount = (pRtv->ArraySize > 0 && pRtv->ArraySize != (UINT)-1) ? pRtv->ArraySize : 1;
    if (baseLayer >= resLayers) baseLayer = 0;
    if (baseLayer + layerCount > resLayers) {
        layerCount = (resLayers > baseLayer) ? (resLayers - baseLayer) : 1;
    }

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = pRtv->MipSlice;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = baseLayer;
    viewInfo.subresourceRange.layerCount = layerCount;

    res = pBridge->vk.vkCreateImageView(pBridge->Device, &viewInfo, NULL, &pRtv->VkView);
    if (res != VK_SUCCESS) {
        Bc250VkResTrace("[VK_RES] ERRO: vkCreateImageView (RTV) falhou: res=%d", res);
        return FALSE;
    }

    /* 2. Create RenderPass */
    memset(&attachment, 0, sizeof(attachment));
    attachment.format = vkFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&colorRef, 0, sizeof(colorRef));
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency;
    memset(&dependency, 0, sizeof(dependency));
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    memset(&rpInfo, 0, sizeof(rpInfo));
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &attachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    res = pBridge->vk.vkCreateRenderPass(pBridge->Device, &rpInfo, NULL, &pRtv->VkPass);
    if (res != VK_SUCCESS) {
        Bc250VkResTrace("[VK_RES] ERRO: vkCreateRenderPass (RTV Clear) falhou: res=%d", res);
        pBridge->vk.vkDestroyImageView(pBridge->Device, pRtv->VkView, NULL);
        pRtv->VkView = VK_NULL_HANDLE;
        return FALSE;
    }

    /* 2b. Create RenderPass with LOAD (para Draw subsequente preservando conteudo) */
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    res = pBridge->vk.vkCreateRenderPass(pBridge->Device, &rpInfo, NULL, &pRtv->VkPassLoad);
    if (res != VK_SUCCESS || pRtv->VkPassLoad == VK_NULL_HANDLE) {
        Bc250VkResTrace("[VK_RES] ERRO: vkCreateRenderPass (RTV Load) falhou: res=%d (D1.3 fail-closed)", res);
        pBridge->vk.vkDestroyRenderPass(pBridge->Device, pRtv->VkPass, NULL);
        pBridge->vk.vkDestroyImageView(pBridge->Device, pRtv->VkView, NULL);
        pRtv->VkPass = VK_NULL_HANDLE;
        pRtv->VkPassLoad = VK_NULL_HANDLE;
        pRtv->VkView = VK_NULL_HANDLE;
        return FALSE;
    }

    /* 3. Create Framebuffer */
    memset(&fbInfo, 0, sizeof(fbInfo));
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = pRtv->VkPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &pRtv->VkView;
    fbInfo.width = (pRes->Width > 0) ? pRes->Width : 1;
    fbInfo.height = (pRes->Height > 0) ? pRes->Height : 1;
    fbInfo.layers = 1;

    res = pBridge->vk.vkCreateFramebuffer(pBridge->Device, &fbInfo, NULL, &pRtv->VkFb);
    if (res != VK_SUCCESS) {
        Bc250VkResTrace("[VK_RES] ERRO: vkCreateFramebuffer (RTV) falhou: res=%d", res);
        if (pRtv->VkPassLoad != VK_NULL_HANDLE) {
            pBridge->vk.vkDestroyRenderPass(pBridge->Device, pRtv->VkPassLoad, NULL);
            pRtv->VkPassLoad = VK_NULL_HANDLE;
        }
        pBridge->vk.vkDestroyRenderPass(pBridge->Device, pRtv->VkPass, NULL);
        pBridge->vk.vkDestroyImageView(pBridge->Device, pRtv->VkView, NULL);
        pRtv->VkPass = VK_NULL_HANDLE;
        pRtv->VkView = VK_NULL_HANDLE;
        return FALSE;
    }

    pRtv->VkRtvCreated = TRUE;
    Bc250VkResTrace("[VK_RES] SUCESSO: RTV criado (view=%p, passClear=%p, passLoad=%p, fb=%p)",
        (void*)pRtv->VkView, (void*)pRtv->VkPass, (void*)pRtv->VkPassLoad, (void*)pRtv->VkFb);
    return TRUE;
}

VOID Bc250UmdVkDestroyRtv(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RTV* pRtv)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    if (pDevice == NULL || pRtv == NULL || !pDevice->VkBridgeInitialized) return;
    pBridge = &pDevice->VkBridge;

    Bc250VkResTrace("[VK_RES] Bc250UmdVkDestroyRtv: view=%p, passClear=%p, passLoad=%p, fb=%p",
        (void*)pRtv->VkView, (void*)pRtv->VkPass, (void*)pRtv->VkPassLoad, (void*)pRtv->VkFb);

    if (pRtv->VkFb != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyFramebuffer(pBridge->Device, pRtv->VkFb, NULL);
        pRtv->VkFb = VK_NULL_HANDLE;
    }
    if (pRtv->VkPassLoad != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyRenderPass(pBridge->Device, pRtv->VkPassLoad, NULL);
        pRtv->VkPassLoad = VK_NULL_HANDLE;
    }
    if (pRtv->VkPass != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyRenderPass(pBridge->Device, pRtv->VkPass, NULL);
        pRtv->VkPass = VK_NULL_HANDLE;
    }
    if (pRtv->VkView != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyImageView(pBridge->Device, pRtv->VkView, NULL);
        pRtv->VkView = VK_NULL_HANDLE;
    }
    pRtv->VkRtvCreated = FALSE;
}

BOOL Bc250UmdVkCreateDsv(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_DSV* pDsv,
    _In_ CONST D3D10DDIARG_CREATEDEPTHSTENCILVIEW* pCreate)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    BC250_UMD_RESOURCE* pRes;
    VkImageViewCreateInfo viewInfo;
    VkFormat vkFormat;
    VkResult res;

    UNREFERENCED_PARAMETER(pCreate);

    if (pDevice == NULL || pDsv == NULL || !pDevice->VkBridgeInitialized) {
        return FALSE;
    }
    pBridge = &pDevice->VkBridge;
    pRes = pDsv->pResource;
    if (pRes == NULL || pRes->VkImg == VK_NULL_HANDLE) {
        Bc250VkResTrace("[VK_RES] Bc250UmdVkCreateDsv RECUSADO: recurso ausente ou VkImg nulo");
        return FALSE;
    }

    vkFormat = Bc250UmdVkFormatFromDxgi(pDsv->Format ? pDsv->Format : pRes->Format);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        vkFormat = VK_FORMAT_D32_SFLOAT;
    }

    memset(&viewInfo, 0, sizeof(viewInfo));
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = pRes->VkImg;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vkFormat;
    uint32_t resLayers = (pRes->ArraySize > 0) ? pRes->ArraySize : 1;
    uint32_t baseLayer = pDsv->FirstArraySlice;
    uint32_t layerCount = (pDsv->ArraySize > 0 && pDsv->ArraySize != (UINT)-1) ? pDsv->ArraySize : 1;
    if (baseLayer >= resLayers) baseLayer = 0;
    if (baseLayer + layerCount > resLayers) {
        layerCount = (resLayers > baseLayer) ? (resLayers - baseLayer) : 1;
    }

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = pDsv->MipSlice;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = baseLayer;
    viewInfo.subresourceRange.layerCount = layerCount;

    res = pBridge->vk.vkCreateImageView(pBridge->Device, &viewInfo, NULL, &pDsv->VkView);
    if (res != VK_SUCCESS) {
        Bc250VkResTrace("[VK_RES] ERRO: vkCreateImageView (DSV) falhou: res=%d", res);
        return FALSE;
    }
    pDsv->VkDsvCreated = TRUE;
    Bc250VkResTrace("[VK_RES] SUCESSO: DSV criado (view=%p, fmt=%u)", (void*)pDsv->VkView, vkFormat);
    return TRUE;
}

VOID Bc250UmdVkDestroyDsv(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_DSV* pDsv)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    if (pDevice == NULL || pDsv == NULL || !pDevice->VkBridgeInitialized) return;
    pBridge = &pDevice->VkBridge;

    Bc250VkResTrace("[VK_RES] Bc250UmdVkDestroyDsv: view=%p", (void*)pDsv->VkView);

    if (pDsv->VkView != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyImageView(pBridge->Device, pDsv->VkView, NULL);
        pDsv->VkView = VK_NULL_HANDLE;
    }
    pDsv->VkDsvCreated = FALSE;
}

BOOL Bc250UmdVkUploadBufferData(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RESOURCE* pRes,
    _In_reads_bytes_(ByteSize) CONST VOID* pData,
    SIZE_T ByteSize)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    void* mapped = NULL;
    VkResult res;

    if (pDevice == NULL || pRes == NULL || pData == NULL || ByteSize == 0 ||
        !pDevice->VkBridgeInitialized || pRes->VkMem == VK_NULL_HANDLE) {
        return FALSE;
    }
    pBridge = &pDevice->VkBridge;

    res = pBridge->vk.vkMapMemory(pBridge->Device, pRes->VkMem, 0, ByteSize, 0, &mapped);
    if (res == VK_SUCCESS && mapped != NULL) {
        memcpy(mapped, pData, ByteSize);
        if (pBridge->vk.vkFlushMappedMemoryRanges != NULL) {
            VkMappedMemoryRange range;
            memset(&range, 0, sizeof(range));
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = pRes->VkMem;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            pBridge->vk.vkFlushMappedMemoryRanges(pBridge->Device, 1, &range);
        }
        pBridge->vk.vkUnmapMemory(pBridge->Device, pRes->VkMem);
        Bc250VkResTrace("[VK_RES] UploadBufferData SUCESSO: %zu bytes copiados para VkMem=%p (res=%p)",
            ByteSize, (void*)pRes->VkMem, (void*)pRes);
        return TRUE;
    } else {
        Bc250VkResTrace("[VK_RES] UploadBufferData ERRO: vkMapMemory falhou (res=%d) para VkMem=%p",
            res, (void*)pRes->VkMem);
    }
    return FALSE;
}
