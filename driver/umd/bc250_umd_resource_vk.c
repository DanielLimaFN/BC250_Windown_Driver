#include "bc250_d3d_umd.h"
#include "bc250_umd_resource_vk.h"
#include "bc250_wddm_winsys.h"
#include "bc250_radv_import.h"
#include "bc250_umd_caps_probe.h"
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

/* Bytes por texel somente para formatos nao comprimidos que a bridge publica.
 * Desconhecido retorna zero: nunca inferir RGBA8 a partir do pitch. */
static UINT Bc250UmdVkUploadBytesPerTexel(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT: return 16u;
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT: return 12u;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT: return 8u;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return 4u;
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM: return 2u;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM: return 1u;
    default: return 0u;
    }
}

BOOL Bc250UmdVkComputeImageUploadLayout(
    DXGI_FORMAT Format,
    UINT Width,
    UINT Height,
    UINT RowPitch,
    SIZE_T ByteSize,
    UINT TotalByteSize,
    _Out_ UINT* pBufferRowLength,
    _Out_ SIZE_T* pRequiredBytes)
{
    const UINT bpp = Bc250UmdVkUploadBytesPerTexel(Format);
    SIZE_T lineBytes;
    SIZE_T required;

    if (pBufferRowLength != NULL) *pBufferRowLength = 0u;
    if (pRequiredBytes != NULL) *pRequiredBytes = 0u;
    if (pBufferRowLength == NULL || pRequiredBytes == NULL ||
        Width == 0u || Height == 0u || RowPitch == 0u || bpp == 0u ||
        (SIZE_T)Width > ((SIZE_T)-1) / bpp) return FALSE;
    lineBytes = (SIZE_T)Width * bpp;
    if ((SIZE_T)RowPitch < lineBytes || (RowPitch % bpp) != 0u ||
        (SIZE_T)Height > ((SIZE_T)-1) / RowPitch) return FALSE;
    required = (SIZE_T)RowPitch * Height;
    if (ByteSize < required ||
        (TotalByteSize != 0u && required > (SIZE_T)TotalByteSize)) return FALSE;
    *pBufferRowLength = RowPitch / bpp;
    *pRequiredBytes = required;
    return *pBufferRowLength >= Width;
}

/*
 * FRENTE B (doc 547) -- a VkImage passa a USAR a alocacao WDDM do recurso.
 *
 * MEDIDO na Corrida 189: o dwm.exe pediu 12 MB para a VkImage de uma superficie
 * aberta, o MakeResident da RADV voltou 0xC0000017 (segmento de ~257 MB cheio)
 * e o DWM destruiu e recriou o device inteiro, 3 s antes da trava. Cada textura
 * ocupava DUAS vezes a memoria: a alocacao WDDM desta UMD (linear, pitch
 * RowPitch) e a memoria propria da RADV para a imagem otimizada.
 *
 * Aqui a imagem nasce LINEAR, com initialLayout PREINITIALIZED (o conteudo da
 * alocacao e' preservado), e a memoria Vulkan e' importada pela GPUVA que esta
 * UMD ja' mapeou e tornou residente (Bc250RadvImportGpuVaInfo). So' e' aceito
 * quando a RADV confirma, pela propria descricao do layout, que a imagem cabe
 * exatamente na alocacao: offset 0, rowPitch == RowPitch, tamanho e alinhamento.
 * Qualquer divergencia devolve FALSE com o motivo nomeado e o chamador segue o
 * caminho com memoria propria -- que e' o caminho de GPU de hoje, nao fallback
 * de software.
 */
#define BC250_FRENTE_B_LOG_FIRST 32
static volatile LONG g_Bc250FrenteBLog = 0;

static void Bc250FrenteBTrace(const char *outcome, const BC250_UMD_RESOURCE *pRes, const char *detail,
                              unsigned long long a, unsigned long long b)
{
    LONG n = InterlockedIncrement(&g_Bc250FrenteBLog);
    if (n <= BC250_FRENTE_B_LOG_FIRST || (n % 256) == 0) {
        Bc250VkResTrace("[VK_RES] FRENTE_B %s #%ld: %ux%u fmt=%u pitch=%u va=0x%llX alloc=%llu -- %s (%llu/%llu)",
            outcome, n, pRes->Width, pRes->Height, (unsigned)pRes->Format, pRes->RowPitch,
            (unsigned long long)pRes->Allocation.GpuVirtualAddress,
            (unsigned long long)pRes->Allocation.ByteSize, detail, a, b);
    }
}

static BOOL Bc250UmdVkTryCreateAliasedImage(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RESOURCE* pRes,
    _In_ CONST D3D10DDIARG_CREATERESOURCE* pCreate,
    VkFormat vkFormat,
    VkImageUsageFlags usage)
{
    BC250_UMD_VK_BRIDGE *pBridge = &pDevice->VkBridge;
    VkImageCreateInfo imgInfo;
    VkImageSubresource sub;
    VkSubresourceLayout layout;
    VkMemoryRequirements memReqs;
    VkMemoryAllocateInfo allocInfo;
    Bc250RadvImportGpuVaInfo importInfo;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t memTypeIndex;
    uint64_t allocBytes;
    const char *reason;
    VkResult res;

    if (pBridge->vk.vkGetImageSubresourceLayout == NULL) {
        Bc250FrenteBTrace("NAO", pRes, "vkGetImageSubresourceLayout ausente", 0, 0); return FALSE;
    }
    reason = Bc250UmdFrenteBPreCheck(pRes->Allocation.Created, pRes->Allocation.GpuVirtualAddress,
        pRes->Allocation.ByteSize, pRes->MipLevels, pRes->ArraySize, pRes->SampleCount,
        (pCreate->BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL) != 0u ? TRUE : FALSE,
        (pCreate->BindFlags & D3D10_DDI_BIND_RENDER_TARGET) != 0u ? TRUE : FALSE,
        pRes->OpenedShared,
        pRes->Width, pRes->Height, pRes->RowPitch);
    if (reason != NULL) {
        Bc250FrenteBTrace("NAO", pRes, reason, pRes->MipLevels, pRes->ArraySize);
        return FALSE;
    }

    /* Criar imagem LINEAR com uso que o formato nao suporta em linear e'
     * comportamento indefinido: a capacidade e' perguntada antes. */
    {
        VkFormatProperties fp;
        VkFormatFeatureFlags need = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) need |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) need |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if (pBridge->vk.vkGetPhysicalDeviceFormatProperties == NULL) {
            Bc250FrenteBTrace("NAO", pRes, "vkGetPhysicalDeviceFormatProperties ausente", 0, 0); return FALSE;
        }
        memset(&fp, 0, sizeof(fp));
        pBridge->vk.vkGetPhysicalDeviceFormatProperties(pBridge->PhysicalDevice, vkFormat, &fp);
        if ((fp.linearTilingFeatures & need) != need) {
            Bc250FrenteBTrace("NAO", pRes, "formato sem suporte linear para o uso (tem/precisa)",
                (unsigned long long)fp.linearTilingFeatures, (unsigned long long)need);
            return FALSE;
        }
    }

    memset(&imgInfo, 0, sizeof(imgInfo));
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = vkFormat;
    imgInfo.extent.width = pRes->Width;
    imgInfo.extent.height = pRes->Height;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imgInfo.usage = usage;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    res = pBridge->vk.vkCreateImage(pBridge->Device, &imgInfo, NULL, &image);
    if (res != VK_SUCCESS) {
        Bc250FrenteBTrace("NAO", pRes, "vkCreateImage LINEAR recusado", (unsigned long long)(LONG)res, 0); return FALSE;
    }

    memset(&sub, 0, sizeof(sub));
    sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    memset(&layout, 0, sizeof(layout));
    pBridge->vk.vkGetImageSubresourceLayout(pBridge->Device, image, &sub, &layout);
    pBridge->vk.vkGetImageMemoryRequirements(pBridge->Device, image, &memReqs);
    allocBytes = pRes->Allocation.ByteSize & ~0xFFFull;

    reason = Bc250UmdFrenteBLayoutCheck(pRes->Allocation.GpuVirtualAddress, pRes->Allocation.ByteSize,
        pRes->RowPitch, layout.offset, layout.rowPitch, memReqs.size, memReqs.alignment);
    if (reason != NULL) {
        /* Os dois numeros que decidem: pitch da RADV e tamanho exigido. */
        Bc250FrenteBTrace("NAO", pRes, reason,
            (unsigned long long)layout.rowPitch, (unsigned long long)memReqs.size);
        goto refuse;
    }
    memTypeIndex = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memTypeIndex == UINT32_MAX)
        memTypeIndex = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits, 0);
    if (memTypeIndex == UINT32_MAX) {
        Bc250FrenteBTrace("NAO", pRes, "nenhum tipo de memoria compativel", memReqs.memoryTypeBits, 0);
        goto refuse;
    }

    memset(&importInfo, 0, sizeof(importInfo));
    importInfo.sType = BC250_RADV_STRUCTURE_TYPE_IMPORT_GPU_VA;
    importInfo.magic = BC250_RADV_IMPORT_GPU_VA_MAGIC;
    importInfo.gpu_va = pRes->Allocation.GpuVirtualAddress;
    importInfo.size = allocBytes;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &importInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    res = pBridge->vk.vkAllocateMemory(pBridge->Device, &allocInfo, NULL, &memory);
    if (res != VK_SUCCESS) {
        Bc250FrenteBTrace("NAO", pRes, "RADV recusou a importacao por GPUVA", (unsigned long long)(LONG)res, 0);
        goto refuse;
    }
    res = pBridge->vk.vkBindImageMemory(pBridge->Device, image, memory, 0);
    if (res != VK_SUCCESS) {
        Bc250FrenteBTrace("NAO", pRes, "vkBindImageMemory recusou", (unsigned long long)(LONG)res, 0);
        pBridge->vk.vkFreeMemory(pBridge->Device, memory, NULL);
        goto refuse;
    }

    pRes->VkImg = image;
    pRes->VkMem = memory;
    pRes->VkMemSize = memReqs.size;
    /* Recurso proprio sem escrita conhecida: segue "sem conteudo" como antes.
     * Quem adota o conteudo existente (superficie aberta) faz a transicao
     * PREINITIALIZED -> SHADER_READ_ONLY explicitamente. */
    pRes->VkLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    pRes->VkMemAliased = TRUE;
    pRes->VkResourceCreated = TRUE;
    Bc250FrenteBTrace("SIM", pRes, "VkImage LINEAR sobre a propria alocacao (req/alloc)",
        (unsigned long long)memReqs.size, (unsigned long long)allocBytes);
    return TRUE;

refuse:
    pBridge->vk.vkDestroyImage(pBridge->Device, image, NULL);
    return FALSE;
}

/*
 * Frente B: a superficie aberta JA' tem o conteudo do outro processo na propria
 * memoria. A imagem nasceu PREINITIALIZED (unico layout que o Vulkan garante
 * preservar) e passa a SHADER_READ_ONLY sem copia nenhuma.
 */
BOOL Bc250UmdVkAdoptAliasedContent(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RESOURCE* pRes)
{
    BC250_UMD_VK_BRIDGE *pBridge = &pDevice->VkBridge;
    VkImageMemoryBarrier barrier;
    VkCommandBuffer cmd;

    if (!pRes->VkMemAliased || pRes->VkImg == VK_NULL_HANDLE) return FALSE;
    cmd = Bc250UmdVkGetCommandBuffer(pBridge);
    if (cmd == VK_NULL_HANDLE) {
        Bc250FrenteBTrace("ADOCAO_RECUSADA", pRes, "sem command buffer", 0, 0);
        return FALSE;
    }
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pRes->VkImg;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    pBridge->vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    pRes->VkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Bc250FrenteBTrace("ADOTADA", pRes, "superficie aberta lida na propria memoria, sem copia", 0, 0);
    return TRUE;
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

    if (!pDevice->DeviceActive || !pDevice->VkBridgeInitialized || !pDevice->VkBridge.DeviceActive || pDevice->VkBridge.Device == VK_NULL_HANDLE) {
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
                        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | /* SHADER.2 */
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
        imgInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
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

        pRes->VkMemAliased = FALSE;
        if (Bc250UmdVkTryCreateAliasedImage(pDevice, pRes, pCreate, vkFormat, imgInfo.usage)) {
            goto image_ready;
        }

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

        Bc250VkResTrace("[VK_RES] Image MemoryRequirements: reqSize=%llu, memTypeIndex=%u, alignment=%llu, typeBits=0x%X",
            memReqs.size, memTypeIndex, (unsigned long long)memReqs.alignment, (unsigned)memReqs.memoryTypeBits);

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

image_ready:

        /*
         * CARGA DO SUBRECURSO 0, quando o D3D entregou dado inicial.
         *
         * Ate 19/09 nada nesta UMD chamava vkCmdCopyBufferToImage: a VkImage
         * nascia vazia e o descritor recusava por VkLayout==UNDEFINED. Era a
         * causa do "binding index=2" medido na Corrida 38.
         *
         * Sem dado inicial a imagem permanece SEM conteudo de proposito --
         * VkImageHasContent fica 0 e o descritor recusa com motivo nomeado.
         * Render target preenchido por desenho tem caminho proprio e nao
         * depende disto.
         */
        if (pCreate->pInitialDataUP != NULL && pCreate->pInitialDataUP->pSysMem != NULL) {
            /*
             * O TAMANHO DA ORIGEM SAI DO CONTRATO, NUNCA DE ESTIMATIVA.
             *
             * A primeira versao deste bloco calculava
             *   bytes = (SysMemPitch ? SysMemPitch : Width*4) * Height
             * e passava isso a um memcpy. Quando SysMemPitch vinha 0 o
             * Width*4 era um CHUTE sobre a largura da linha da aplicacao, e o
             * memcpy lia alem do buffer de origem. A suite offline segfaultou
             * na hora -- leitura fora dos limites, defeito meu.
             *
             * SysMemPitch e' o unico numero que a aplicacao nos deu sobre o
             * layout dela. Sem ele nao se sabe quantos bytes existem, e
             * adivinhar e' exatamente o que produziu o crash. Entao: pitch
             * zero RECUSA, e a textura fica sem conteudo, declaradamente.
             */
            const UINT rowPitch = pCreate->pInitialDataUP->SysMemPitch;
            if (rowPitch == 0u) {
                Bc250VkResTrace("[VK_RES] Textura SEM conteudo: SysMemPitch=0 e o tamanho da origem "
                    "seria ADIVINHADO; recusado (fail-closed)");
            } else {
                const SIZE_T bytes = (SIZE_T)rowPitch * (SIZE_T)pRes->Height;
                if (!Bc250UmdVkUploadImageData(pDevice, pRes, pCreate->pInitialDataUP->pSysMem, rowPitch, bytes)) {
                    Bc250VkResTrace("[VK_RES] Textura SEM conteudo: o upload do subrecurso 0 falhou; "
                        "a imagem NAO sera aceita como SRV (fail-closed)");
                }
            }
        } else {
            Bc250VkResTrace("[VK_RES] Textura criada SEM dado inicial: VkImageHasContent=0 ate que "
                "alguem escreva nela; amostra-la agora seria ler memoria nao inicializada");
        }
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
    if (!pDevice->DeviceActive || !pBridge->DeviceActive || pBridge->Device == VK_NULL_HANDLE) {
        pRes->VkBuf = VK_NULL_HANDLE;
        pRes->VkImg = VK_NULL_HANDLE;
        pRes->VkMem = VK_NULL_HANDLE;
        pRes->VkResourceCreated = FALSE;
        pRes->VkMemAliased = FALSE;
        return;
    }

    Bc250VkResTrace("[VK_RES] Bc250UmdVkDestroyResource: buf=%p, img=%p, mem=%p",
        (void*)pRes->VkBuf, (void*)pRes->VkImg, (void*)pRes->VkMem);
    /* DX.49: um draw gravado no lote pode referenciar este recurso. */
    (void)Bc250UmdVkDrainPending(pBridge, "destruir recurso");

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
    pRes->VkMemAliased = FALSE;
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
    pRtv->VkImageAtCreation = pRes->VkImg;
    pRtv->FbCache[0].Image = pRes->VkImg;
    pRtv->FbCache[0].View = pRtv->VkView;
    pRtv->FbCache[0].Fb = pRtv->VkFb;
    pRtv->FbCacheCount = 1u;
    pRtv->FollowHitCount = 0u;
    pRtv->FollowMissCount = 0u;
    Bc250VkResTrace("[VK_RES] SUCESSO: RTV criado (view=%p, passClear=%p, passLoad=%p, fb=%p)",
        (void*)pRtv->VkView, (void*)pRtv->VkPass, (void*)pRtv->VkPassLoad, (void*)pRtv->VkFb);
    return TRUE;
}

VOID Bc250UmdVkDestroyRtv(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RTV* pRtv)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    uint32_t i;
    if (pDevice == NULL || pRtv == NULL || !pDevice->VkBridgeInitialized) return;
    pBridge = &pDevice->VkBridge;
    if (!pDevice->DeviceActive || !pBridge->DeviceActive || pBridge->Device == VK_NULL_HANDLE) {
        pRtv->FbCacheCount = 0;
        pRtv->VkFb = VK_NULL_HANDLE;
        pRtv->VkView = VK_NULL_HANDLE;
        pRtv->VkPassLoad = VK_NULL_HANDLE;
        pRtv->VkPass = VK_NULL_HANDLE;
        pRtv->VkRtvCreated = FALSE;
        return;
    }

    Bc250VkResTrace("[VK_RES] Bc250UmdVkDestroyRtv: view=%p, passClear=%p, passLoad=%p, fb=%p (CachedFbs=%u)",
        (void*)pRtv->VkView, (void*)pRtv->VkPass, (void*)pRtv->VkPassLoad, (void*)pRtv->VkFb, pRtv->FbCacheCount);
    (void)Bc250UmdVkDrainPending(pBridge, "destruir RTV");

    for (i = 0; i < pRtv->FbCacheCount; ++i) {
        if (pRtv->FbCache[i].Fb != VK_NULL_HANDLE) {
            uint32_t j;
            if (pRtv->VkFb == pRtv->FbCache[i].Fb) {
                pRtv->VkFb = VK_NULL_HANDLE;
            }
            for (j = i + 1; j < pRtv->FbCacheCount; ++j) {
                if (pRtv->FbCache[j].Fb == pRtv->FbCache[i].Fb) {
                    pRtv->FbCache[j].Fb = VK_NULL_HANDLE;
                }
            }
            pBridge->vk.vkDestroyFramebuffer(pBridge->Device, pRtv->FbCache[i].Fb, NULL);
            pRtv->FbCache[i].Fb = VK_NULL_HANDLE;
        }
        if (pRtv->FbCache[i].View != VK_NULL_HANDLE) {
            uint32_t j;
            if (pRtv->VkView == pRtv->FbCache[i].View) {
                pRtv->VkView = VK_NULL_HANDLE;
            }
            for (j = i + 1; j < pRtv->FbCacheCount; ++j) {
                if (pRtv->FbCache[j].View == pRtv->FbCache[i].View) {
                    pRtv->FbCache[j].View = VK_NULL_HANDLE;
                }
            }
            pBridge->vk.vkDestroyImageView(pBridge->Device, pRtv->FbCache[i].View, NULL);
            pRtv->FbCache[i].View = VK_NULL_HANDLE;
        }
    }
    pRtv->FbCacheCount = 0;
    pRtv->VkFb = VK_NULL_HANDLE;
    pRtv->VkView = VK_NULL_HANDLE;

    if (pRtv->VkPassLoad != VK_NULL_HANDLE) {
        if (pRtv->VkPassLoad == pRtv->VkPass) {
            pRtv->VkPass = VK_NULL_HANDLE;
        }
        pBridge->vk.vkDestroyRenderPass(pBridge->Device, pRtv->VkPassLoad, NULL);
        pRtv->VkPassLoad = VK_NULL_HANDLE;
    }
    if (pRtv->VkPass != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyRenderPass(pBridge->Device, pRtv->VkPass, NULL);
        pRtv->VkPass = VK_NULL_HANDLE;
    }
    pRtv->VkRtvCreated = FALSE;
}

/*
 * DX.28 / DX.80 -- A RTV SEGUE O RECURSO DEPOIS DA ROTACAO DE IDENTIDADES.
 *
 * DX.28: a rotacao de identidades leva o grupo Vulkan junto; esta funcao
 * sincroniza VkView/VkFb quando a imagem do recurso nao e' mais a da criacao.
 *
 * DX.80: cache de framebuffers por imagem. Na Corrida 124 (DX.79), a destruicao
 * e recriacao continua de VkFb/VkView a cada troca de backbuffer provocou 369
 * flushes sincronos incondicionais (~3,7 s de CPU parada). Com o cache, imagens
 * ja vistas na swapchain reutilizam seus framebuffers/views sem emitir nenhum
 * flush e sem travar a CPU.
 */
BOOL Bc250UmdVkRtvFollowResource(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_RTV* pRtv)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    BC250_UMD_RESOURCE* pRes;
    VkFramebufferCreateInfo fbInfo;
    VkImageViewCreateInfo viewInfo;
    VkImageView newView = VK_NULL_HANDLE;
    VkFramebuffer newFb = VK_NULL_HANDLE;
    VkFormat vkFormat;
    VkResult res;
    uint32_t resLayers, baseLayer, layerCount;
    uint32_t i;

    if (pDevice == NULL || pRtv == NULL || !pDevice->VkBridgeInitialized) return FALSE;
    if (!pRtv->VkRtvCreated) return FALSE;
    pRes = pRtv->pResource;
    if (pRes == NULL || pRes->VkImg == VK_NULL_HANDLE) return FALSE;
    if (pRes->VkImg == pRtv->VkImageAtCreation) return TRUE;

    pBridge = &pDevice->VkBridge;

    /* DX.80 -- 1. Busca no cache de framebuffers por imagem */
    for (i = 0; i < pRtv->FbCacheCount; ++i) {
        if (pRtv->FbCache[i].Image == pRes->VkImg &&
            pRtv->FbCache[i].Fb != VK_NULL_HANDLE &&
            pRtv->FbCache[i].View != VK_NULL_HANDLE) {

            /* Se ha um render pass gravando no framebuffer antigo, fecha-o no CB */
            if (pDevice->InRenderPass && pDevice->ActiveFramebuffer != pRtv->FbCache[i].Fb) {
                if (pBridge->CommandBuffer != VK_NULL_HANDLE && pBridge->vk.vkCmdEndRenderPass != NULL) {
                    pBridge->vk.vkCmdEndRenderPass(pBridge->CommandBuffer);
                }
                pDevice->InRenderPass = FALSE;
            }

            pRtv->VkView = pRtv->FbCache[i].View;
            pRtv->VkFb = pRtv->FbCache[i].Fb;
            pRtv->VkImageAtCreation = pRes->VkImg;
            pRtv->FollowHitCount++;

            Bc250VkResTrace("[VK_RES] RTV_SEGUE_RECURSO (CACHE HIT): rtv=%p res=%p imagem=%p fb=%p (Hits=%u Misses=%u)",
                (void*)pRtv, (void*)pRes, (void*)pRes->VkImg, (void*)pRtv->VkFb,
                pRtv->FollowHitCount, pRtv->FollowMissCount);
            return TRUE;
        }
    }

    /* DX.80 -- 2. Cache Miss: criar nova View e Framebuffer para esta imagem */
    pRtv->FollowMissCount++;

    if (pDevice->InRenderPass) {
        if (pBridge->CommandBuffer != VK_NULL_HANDLE && pBridge->vk.vkCmdEndRenderPass != NULL) {
            pBridge->vk.vkCmdEndRenderPass(pBridge->CommandBuffer);
        }
        pDevice->InRenderPass = FALSE;
    }
    pDevice->ActiveFramebuffer = VK_NULL_HANDLE;

    vkFormat = Bc250UmdVkFormatFromDxgi(pRtv->Format ? pRtv->Format : pRes->Format);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        vkFormat = VK_FORMAT_B8G8R8A8_UNORM;
    }
    resLayers = (pRes->ArraySize > 0) ? pRes->ArraySize : 1;
    baseLayer = pRtv->FirstArraySlice;
    layerCount = (pRtv->ArraySize > 0 && pRtv->ArraySize != (UINT)-1) ? pRtv->ArraySize : 1;
    if (baseLayer >= resLayers) baseLayer = 0;
    if (baseLayer + layerCount > resLayers) {
        layerCount = (resLayers > baseLayer) ? (resLayers - baseLayer) : 1;
    }

    memset(&viewInfo, 0, sizeof(viewInfo));
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = pRes->VkImg;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vkFormat;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = pRtv->MipSlice;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = baseLayer;
    viewInfo.subresourceRange.layerCount = layerCount;
    res = pBridge->vk.vkCreateImageView(pBridge->Device, &viewInfo, NULL, &newView);
    if (res != VK_SUCCESS) {
        Bc250VkResTrace("[VK_RES] RTV_SEGUE_RECURSO RECUSADO: vkCreateImageView falhou (res=%d)", res);
        return FALSE;
    }

    memset(&fbInfo, 0, sizeof(fbInfo));
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = pRtv->VkPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &newView;
    fbInfo.width = (pRes->Width > 0) ? pRes->Width : 1;
    fbInfo.height = (pRes->Height > 0) ? pRes->Height : 1;
    fbInfo.layers = 1;
    res = pBridge->vk.vkCreateFramebuffer(pBridge->Device, &fbInfo, NULL, &newFb);
    if (res != VK_SUCCESS) {
        pBridge->vk.vkDestroyImageView(pBridge->Device, newView, NULL);
        Bc250VkResTrace("[VK_RES] RTV_SEGUE_RECURSO RECUSADO: vkCreateFramebuffer falhou (res=%d)", res);
        return FALSE;
    }

    /* Se o cache estiver cheio, drena o lote e descarta a entrada mais antiga */
    if (pRtv->FbCacheCount >= BC250_RTV_MAX_CACHED_FBS) {
        Bc250VkResTrace("[VK_RES] RTV_SEGUE_RECURSO: rtv=%p res=%p reciclagem de slot", (void*)pRtv, (void*)pRes);
        (void)Bc250UmdVkDrainPending(pBridge, "RTV seguir recurso rotacionado");
        if (pRtv->FbCache[0].Fb != VK_NULL_HANDLE) {
            pRtv->VkFb = pRtv->FbCache[0].Fb;
            pBridge->vk.vkDestroyFramebuffer(pBridge->Device, pRtv->VkFb, NULL);
        }
        if (pRtv->FbCache[0].View != VK_NULL_HANDLE) {
            pBridge->vk.vkDestroyImageView(pBridge->Device, pRtv->FbCache[0].View, NULL);
        }
        for (i = 0; i + 1 < BC250_RTV_MAX_CACHED_FBS; ++i) {
            pRtv->FbCache[i] = pRtv->FbCache[i + 1];
        }
        pRtv->FbCacheCount = BC250_RTV_MAX_CACHED_FBS - 1;
    }

    pRtv->FbCache[pRtv->FbCacheCount].Image = pRes->VkImg;
    pRtv->FbCache[pRtv->FbCacheCount].View = newView;
    pRtv->FbCache[pRtv->FbCacheCount].Fb = newFb;
    pRtv->FbCacheCount++;

    pRtv->VkView = newView;
    pRtv->VkFb = newFb;
    pRtv->VkImageAtCreation = pRes->VkImg;

    Bc250VkResTrace("[VK_RES] RTV_SEGUE_RECURSO: rtv=%p res=%p imagem %p -> %p (view %p -> %p, fb %p -> %p) [DX80_CACHE] Count=%u Hits=%u Misses=%u",
        (void*)pRtv, (void*)pRes, (void*)pRtv->VkImageAtCreation, (void*)pRes->VkImg,
        (void*)pRtv->VkView, (void*)newView, (void*)pRtv->VkFb, (void*)newFb,
        pRtv->FbCacheCount, pRtv->FollowHitCount, pRtv->FollowMissCount);
    return TRUE;
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
    if (!pDevice->DeviceActive || !pBridge->DeviceActive || pBridge->Device == VK_NULL_HANDLE) {
        pDsv->VkView = VK_NULL_HANDLE;
        pDsv->VkDsvCreated = FALSE;
        return;
    }

    Bc250VkResTrace("[VK_RES] Bc250UmdVkDestroyDsv: view=%p", (void*)pDsv->VkView);
    (void)Bc250UmdVkDrainPending(pBridge, "destruir DSV");

    if (pDsv->VkView != VK_NULL_HANDLE) {
        pBridge->vk.vkDestroyImageView(pBridge->Device, pDsv->VkView, NULL);
        pDsv->VkView = VK_NULL_HANDLE;
    }
    pDsv->VkDsvCreated = FALSE;
}

/*
 * =========================================================================
 * UPLOAD DE PIXEL PARA VkImage. Escopo DECLARADO: mip 0, camada 0.
 * =========================================================================
 * MEDIDO na Corrida 38 (19/09 15:35:31): os draws do dwm.exe foram recusados
 * no binding index=2, e a causa nao era o descritor -- era que
 * vkCmdCopyBufferToImage NUNCA era chamado em lugar nenhum da UMD. A VkImage
 * era criada e ficava VAZIA, com VkLayout preso em UNDEFINED desde a criacao,
 * porque os unicos sitios que mexiam no layout eram os de render target.
 *
 * Corrigir so o layout teria sido pior que nao corrigir: o descritor passaria
 * a aceitar a imagem e o shader amostraria memoria nao inicializada. Por isso
 * o conteudo vem primeiro, e VkImageHasContent so vai a 1 quando o pixel
 * chegou de fato.
 *
 * LIMITE DECLARADO: copia o subrecurso 0 (mip 0, camada 0). Mip chain e array
 * NAO sao carregados, e o descritor RECUSA a view que pedir alem disso, com
 * motivo nomeado. Preencher parcialmente e deixar o resto indefinido seria a
 * mesma falha de origem com outra roupa.
 */
/*
 * DX.23: conteudo da ORIGEM do upload, medido na memoria da CPU (barato).
 * Corrida 68: todos os Presents sairam pretos e nao havia como saber se as
 * texturas que o DWM nos entregou ja eram pretas. Amostra ate 65536 dwords
 * espalhados pelo buffer inteiro; zero aqui significa que o preto veio de cima.
 */
static uint32_t Bc250VkCountNonZeroSource(const VOID *pData, SIZE_T ByteSize, uint32_t *pSamples)
{
    const uint32_t *words = (const uint32_t *)pData;
    SIZE_T count = ByteSize / 4u;
    SIZE_T step = (count > 65536u) ? (count / 65536u) : 1u;
    uint32_t nonZero = 0u, samples = 0u;
    for (SIZE_T i = 0; i < count; i += step) {
        samples++;
        if (words[i] != 0u) nonZero++;
    }
    if (pSamples != NULL) *pSamples = samples;
    return nonZero;
}

static uint64_t Bc250VkUploadNowMicros(void)
{
    LARGE_INTEGER now, freq;
    if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) return 0u;
    return (uint64_t)(now.QuadPart / freq.QuadPart) * 1000000ull +
           (uint64_t)((now.QuadPart % freq.QuadPart) * 1000000ll / freq.QuadPart);
}

BOOL Bc250UmdVkUploadImageData(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_RESOURCE* pRes,
    _In_reads_bytes_(ByteSize) CONST VOID* pData,
    UINT RowPitch,
    SIZE_T ByteSize)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    VkCommandBuffer cmd;
    VkImageMemoryBarrier barrier;
    VkBufferImageCopy region;
    VkResult res;
    BOOL grew = FALSE;
    uint64_t allocMicros = 0u;
    uint64_t t0;
    UINT bufferRowLength = 0u;

    if (pDevice == NULL || pRes == NULL || pData == NULL || ByteSize == 0 ||
        !pDevice->DeviceActive || !pDevice->VkBridgeInitialized || !pDevice->VkBridge.DeviceActive ||
        pRes->VkImg == VK_NULL_HANDLE ||
        pRes->Width == 0 || pRes->Height == 0) {
        return FALSE;
    }
    pBridge = &pDevice->VkBridge;
    if (pBridge->SubmissionFailed) return FALSE;

    /*
     * Ponteiro de funcao ausente e o caso NORMAL na suite offline, que monta um
     * bridge parcial de proposito. Sem esta guarda o teste segfaulta -- foi o
     * que aconteceu na primeira versao desta funcao. Fail-closed: sem a
     * entrada completa, nao ha upload e a imagem continua sem conteudo.
     */
    if (pBridge->Device == VK_NULL_HANDLE ||
        pBridge->vk.vkCreateBuffer == NULL || pBridge->vk.vkDestroyBuffer == NULL ||
        pBridge->vk.vkGetBufferMemoryRequirements == NULL ||
        pBridge->vk.vkAllocateMemory == NULL || pBridge->vk.vkFreeMemory == NULL ||
        pBridge->vk.vkBindBufferMemory == NULL ||
        pBridge->vk.vkMapMemory == NULL || pBridge->vk.vkUnmapMemory == NULL ||
        pBridge->vk.vkCmdPipelineBarrier == NULL ||
        pBridge->vk.vkCmdCopyBufferToImage == NULL) {
        Bc250VkResTrace("[VK_RES] UploadImageData RECUSADO: entrada Vulkan incompleta "
            "(bridge parcial); a imagem NAO recebe conteudo");
        return FALSE;
    }

    /*
     * ==================================================================
     * VALIDACAO ANTES DA COPIA. Corrigido apos crash proprio.
     * ==================================================================
     * A primeira versao validava RowPitch DEPOIS do memcpy para o staging.
     * A suite offline segfaultou dentro de memcpy: o tamanho vinha de
     * RowPitch*Height e nada garantia que a origem tivesse esses bytes. Ler
     * antes de conferir e' a ordem errada, e nenhuma quantidade de checagem
     * posterior conserta uma leitura que ja aconteceu.
     *
     * Regras, todas verificaveis com o que a aplicacao nos deu:
     *  - pitch tem de conter width*bpp e ser multiplo exato do texel;
     *    padding e representado em bufferRowLength, em TEXELS;
     *  - o byte count declarado tem de cobrir pitch*altura;
     *  - o total tem de caber no que o proprio recurso diz ter.
     * Qualquer duvida RECUSA, e a textura fica sem conteudo declaradamente.
     */
    {
        SIZE_T required = 0u;
        if (!Bc250UmdVkComputeImageUploadLayout(pRes->Format, pRes->Width,
                pRes->Height, RowPitch, ByteSize, pRes->TotalByteSize,
                &bufferRowLength, &required)) {
            Bc250VkResTrace("[VK_RES] UploadImageData RECUSADO: geometria invalida "
                "Format=%u WxH=%ux%u RowPitch=%u ByteSize=%zu Total=%u",
                (UINT)pRes->Format, pRes->Width, pRes->Height, RowPitch,
                ByteSize, pRes->TotalByteSize);
            return FALSE;
        }
        /* So' copiamos o que foi justificado, nunca o ByteSize bruto. */
        ByteSize = required;
    }

    /*
     * DX.49 -- STAGING PERSISTENTE. Corrida 93 (P0): ~1,1 s entre o trace
     * SUCESSO e o retorno de cada upload de 3,6 MB; ali so' existia a
     * liberacao do staging criado nesta chamada (atribuicao P2). O staging agora
     * e' do bridge, cresce quando precisa e so' morre no shutdown. A fence da
     * copia continua sendo esperada antes de retornar: o proximo upload nunca
     * reescreve um staging que a GPU ainda le.
     */
    VkDeviceSize uploadOffset = 0;
    BOOL wrapNeeded = FALSE;
    VkDeviceSize alignedHead = 0;

    /*
     * DX.79 -- COALESCENCIA DE UPLOADS NO STAGING PERSISTENTE.
     * Na Corrida 123 (DX.78) o DWM realizou 504 uploads com flush sincrono a cada chamada,
     * somando ~3,5 s de espera em fence. Com o anel de staging, uploads sucessivos
     * gravam em faixas disjuntas alinhadas a 256 B sem necessidade de flush sincrono.
     * Se o espaco restante no staging for insuficiente ou o lote atingir 64 uploads
     * (T3.7: evita acumulo excessivo de barreiras no menu Iniciar/Explorer),
     * executamos o dreno dos comandos pendentes (wrap) antes de posicionar o dado no inicio.
     */
#define BC250_DX79_UPLOAD_COALESCE_MAX_BATCH 64u
    t0 = Bc250VkUploadNowMicros();
    if (pBridge->UploadStagingMapped != NULL && pBridge->UploadStagingSize > 0) {
        alignedHead = (pBridge->UploadStagingHead + (BC250_DX79_UPLOAD_ALIGN - 1u)) &
                      ~(VkDeviceSize)(BC250_DX79_UPLOAD_ALIGN - 1u);
        if (alignedHead > pBridge->UploadStagingSize ||
            (VkDeviceSize)ByteSize > (pBridge->UploadStagingSize - alignedHead) ||
            pBridge->UploadCoalescedInBatch >= BC250_DX79_UPLOAD_COALESCE_MAX_BATCH) {
            wrapNeeded = TRUE;
        }
    }

    if (wrapNeeded && pBridge->RecordingActive) {
        res = Bc250UmdVkFlushCommands(pBridge);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] UploadImageData RECUSADO: dreno de wrapping falhou res=%d", res);
            return FALSE;
        }
        alignedHead = 0;
    }

    if (!Bc250UmdVkEnsureUploadStaging(pBridge, (VkDeviceSize)ByteSize, &grew, &allocMicros)) {
        Bc250VkResTrace("[VK_RES] UploadImageData RECUSADO: staging persistente indisponivel para %zu bytes", ByteSize);
        return FALSE;
    }
    if (grew) {
        alignedHead = 0;
    }

    uploadOffset = alignedHead;
    memcpy((uint8_t *)pBridge->UploadStagingMapped + uploadOffset, pData, ByteSize);
    pBridge->UploadStagingHead = uploadOffset + (VkDeviceSize)ByteSize;

    cmd = Bc250UmdVkGetCommandBuffer(pBridge);
    if (cmd == VK_NULL_HANDLE) {
        Bc250VkResTrace("[VK_RES] UploadImageData RECUSADO: sem command buffer");
        return FALSE;
    }

    /* UNDEFINED -> TRANSFER_DST. Descartar o conteudo anterior e correto aqui:
     * a imagem ou acabou de nascer, ou vai ser integralmente reescrita no
     * subrecurso 0, que e o unico que esta funcao declara carregar.
     *
     * DX.49: com o lote de draws, draws ANTERIORES gravados no mesmo command
     * buffer podem amostrar esta imagem. A origem da barreira passa a ser
     * ALL_COMMANDS com MEMORY_WRITE: a copia espera quem veio antes (WAR/WAW).
     * TOP_OF_PIPE com acesso 0 so' era correto porque nada ficava pendente. */
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pRes->VkImg;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = pRes->MipLevels ? pRes->MipLevels : 1u;
    barrier.subresourceRange.layerCount = pRes->ArraySize ? pRes->ArraySize : 1u;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pBridge->vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    memset(&region, 0, sizeof(region));
    region.bufferOffset = uploadOffset;
    /* Vulkan recebe o pitch real em texels; isto preserva padding de 256 B. */
    region.bufferRowLength = bufferRowLength;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = pRes->Width;
    region.imageExtent.height = pRes->Height;
    region.imageExtent.depth = 1;
    pBridge->vk.vkCmdCopyBufferToImage(cmd, pBridge->UploadStagingBuf, pRes->VkImg,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    pBridge->vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* DX.79: Coalescencia de upload de textura.
     * A copia para a VkImage e a barreira para SHADER_READ_ONLY foram
     * gravadas no command buffer em faixa exclusiva do staging persistente.
     * O flush sincrono com espera de fence SO' ocorre se o staging precisou
     * de wrap. Nos demais casos, a execucao e' coalescida no proximo flush natural. */
    if (wrapNeeded) {
        res = Bc250UmdVkFlushCommands(pBridge);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] UploadImageData RECUSADO: flush/fence devolveu res=%d; "
                "a imagem NAO recebeu conteudo e continua sem VkImageHasContent", res);
            return FALSE;
        }
        pBridge->UploadFlushCount++;
    } else {
        res = VK_SUCCESS;
        pBridge->UploadCoalescedCount++;
        pBridge->UploadCoalescedInBatch++;
    }

    /* Unica fonte de verdade: o layout. SHADER_READ_ONLY_OPTIMAL so' chega
     * aqui, protegido pela barreira de pipeline da GPU. */
    pRes->VkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    {
        uint32_t srcSamples = 0u;
        uint32_t srcNonZero = Bc250VkCountNonZeroSource(pData, ByteSize, &srcSamples);
        Bc250VkResTrace("[VK_RES] UploadImageData SUCESSO: %ux%u, %zu bytes, RowPitch=%u "
            "bufferRowLength=%u texels em mip0/camada0 (img=%p res=%p) NonZeroSrc=%u/%u -- mip chain e array "
            "NAO carregados por escopo declarado [DX49_STAGING] Persistente=1 Cresceu=%d AlocUs=%llu TotalUs=%llu "
            "[DX79_UPLOAD] Offset=%llu Head=%llu FlushSincrono=%d Coalescidos=%llu FlushesUpload=%llu",
            pRes->Width, pRes->Height, ByteSize, RowPitch, bufferRowLength,
            (void*)pRes->VkImg, (void*)pRes, srcNonZero, srcSamples,
            (int)grew, (unsigned long long)allocMicros,
            (unsigned long long)(Bc250VkUploadNowMicros() - t0),
            (unsigned long long)uploadOffset, (unsigned long long)pBridge->UploadStagingHead,
            wrapNeeded ? 1 : 0,
            (unsigned long long)pBridge->UploadCoalescedCount,
            (unsigned long long)pBridge->UploadFlushCount);
    }
    return TRUE;
}

BOOL Bc250UmdVkCopyStagingBufferToImage(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_RESOURCE* pRes,
    VkDeviceSize BufferOffset,
    UINT RowPitch,
    SIZE_T ByteSize)
{
    BC250_UMD_VK_BRIDGE* pBridge;
    VkCommandBuffer cmd;
    VkImageMemoryBarrier barrier;
    VkBufferImageCopy region;
    VkResult res;
    UINT bufferRowLength = 0u;
    SIZE_T required = 0u;

    if (pDevice == NULL || pRes == NULL || ByteSize == 0 ||
        !pDevice->DeviceActive || !pDevice->VkBridgeInitialized || !pDevice->VkBridge.DeviceActive ||
        pRes->VkImg == VK_NULL_HANDLE ||
        pRes->Width == 0 || pRes->Height == 0) {
        return FALSE;
    }
    pBridge = &pDevice->VkBridge;
    if (pBridge->SubmissionFailed) return FALSE;

    if (pBridge->Device == VK_NULL_HANDLE ||
        pBridge->UploadStagingBuf == VK_NULL_HANDLE ||
        pBridge->vk.vkCmdPipelineBarrier == NULL ||
        pBridge->vk.vkCmdCopyBufferToImage == NULL) {
        Bc250VkResTrace("[VK_RES] CopyStagingBufferToImage RECUSADO: bridge ou staging incompleto");
        return FALSE;
    }

    if (!Bc250UmdVkComputeImageUploadLayout(pRes->Format, pRes->Width,
            pRes->Height, RowPitch, ByteSize, pRes->TotalByteSize,
            &bufferRowLength, &required)) {
        Bc250VkResTrace("[VK_RES] CopyStagingBufferToImage RECUSADO: layout invalido Format=%u WxH=%ux%u RowPitch=%u ByteSize=%zu",
            (UINT)pRes->Format, pRes->Width, pRes->Height, RowPitch, ByteSize);
        return FALSE;
    }

    if (BufferOffset + (VkDeviceSize)required > pBridge->UploadStagingSize) {
        Bc250VkResTrace("[VK_RES] CopyStagingBufferToImage RECUSADO: fora do staging (offset=%llu req=%zu size=%llu)",
            (unsigned long long)BufferOffset, required, (unsigned long long)pBridge->UploadStagingSize);
        return FALSE;
    }

    cmd = Bc250UmdVkGetCommandBuffer(pBridge);
    if (cmd == VK_NULL_HANDLE) {
        Bc250VkResTrace("[VK_RES] CopyStagingBufferToImage RECUSADO: sem command buffer");
        return FALSE;
    }

    /* Transicao: UNDEFINED -> TRANSFER_DST */
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pRes->VkImg;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = pRes->MipLevels ? pRes->MipLevels : 1u;
    barrier.subresourceRange.layerCount = pRes->ArraySize ? pRes->ArraySize : 1u;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pBridge->vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    memset(&region, 0, sizeof(region));
    region.bufferOffset = BufferOffset;
    region.bufferRowLength = bufferRowLength;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = pRes->Width;
    region.imageExtent.height = pRes->Height;
    region.imageExtent.depth = 1;
    pBridge->vk.vkCmdCopyBufferToImage(cmd, pBridge->UploadStagingBuf, pRes->VkImg,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    /* Transicao: TRANSFER_DST -> SHADER_READ_ONLY_OPTIMAL */
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    pBridge->vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    res = Bc250UmdVkFlushCommands(pBridge);
    if (res != VK_SUCCESS) {
        Bc250VkResTrace("[VK_RES] CopyStagingBufferToImage RECUSADO: flush falhou res=%d", res);
        return FALSE;
    }
    pBridge->UploadFlushCount++;

    pRes->VkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Bc250VkResTrace("[VK_RES] CopyStagingBufferToImage SUCESSO: %ux%u pitch=%u offset=%llu bytes=%zu (img=%p)",
        pRes->Width, pRes->Height, RowPitch, (unsigned long long)BufferOffset, required, (void*)pRes->VkImg);
    return TRUE;
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
        !pDevice->DeviceActive || !pDevice->VkBridgeInitialized || !pDevice->VkBridge.DeviceActive ||
        pRes->VkMem == VK_NULL_HANDLE || ByteSize > pRes->VkMemSize) {
        return FALSE;
    }
    pBridge = &pDevice->VkBridge;

    res = pBridge->vk.vkMapMemory(pBridge->Device, pRes->VkMem, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (res == VK_SUCCESS && mapped != NULL) {
        memcpy(mapped, pData, ByteSize);
        if (pBridge->vk.vkFlushMappedMemoryRanges != NULL) {
            VkMappedMemoryRange range;
            memset(&range, 0, sizeof(range));
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = pRes->VkMem;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            res = pBridge->vk.vkFlushMappedMemoryRanges(pBridge->Device, 1, &range);
        }
        pBridge->vk.vkUnmapMemory(pBridge->Device, pRes->VkMem);
        if (res != VK_SUCCESS) {
            Bc250VkResTrace("[VK_RES] UploadBufferData RECUSADO: flush res=%d", res);
            return FALSE;
        }
        Bc250VkResTrace("[VK_RES] UploadBufferData SUCESSO: %zu bytes copiados para VkMem=%p (res=%p)",
            ByteSize, (void*)pRes->VkMem, (void*)pRes);
        return TRUE;
    } else {
        Bc250VkResTrace("[VK_RES] UploadBufferData ERRO: vkMapMemory falhou (res=%d) para VkMem=%p",
            res, (void*)pRes->VkMem);
    }
    return FALSE;
}
