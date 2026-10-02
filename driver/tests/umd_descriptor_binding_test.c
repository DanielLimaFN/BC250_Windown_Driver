#include "../umd/bc250_d3d_umd.h"
#include "../umd/bc250_umd_resource_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static unsigned creates, allocations, updates, binds, destroys, uploads;
static int failAllocation;
static VkBuffer expectedVs, expectedPs;
static int imageMode, texelMode;
static unsigned bufferViewsCreated, bufferViewsDestroyed;
static unsigned barriers, viewsDestroyed, samplersDestroyed;
static void Bc250UmdTrace(const char *fmt, ...) { (void)fmt; }
BOOL Bc250UmdVkUploadBufferData(BC250_UMD_DEVICE *d, BC250_UMD_RESOURCE *r, const void *p, SIZE_T n)
{ (void)d; (void)r; (void)p; assert(n == 64); uploads++; return TRUE; }
BOOL Bc250UmdVkCreateResource(BC250_UMD_DEVICE *pDevice, BC250_UMD_RESOURCE *pRes, CONST D3D10DDIARG_CREATERESOURCE *pCreate)
{ (void)pDevice; (void)pRes; (void)pCreate; return TRUE; }
BOOL Bc250UmdVkUploadImageData(BC250_UMD_DEVICE *pDevice, BC250_UMD_RESOURCE *pRes, CONST VOID *pData, UINT RowPitch, SIZE_T ByteSize)
{ (void)pDevice; (void)pRes; (void)pData; (void)RowPitch; (void)ByteSize; return TRUE; }
HRESULT Bc250UmdBridgeGpuBlt(BC250_UMD_DEVICE *d, uint64_t src, UINT sp, uint64_t dst, UINT dp, UINT w, UINT h)
{ (void)d; (void)src; (void)sp; (void)dst; (void)dp; (void)w; (void)h; return S_OK; }
BOOL Bc250UmdVkCopyStagingBufferToImage(BC250_UMD_DEVICE *d, BC250_UMD_RESOURCE *r, VkDeviceSize o, UINT p, SIZE_T n)
{ (void)d; (void)r; (void)o; (void)p; (void)n; return TRUE; }
#include "../umd/bc250_umd_pipeline_cache.c"
/* DX.54: variavel (nao const) para a suite exercitar os dois caminhos. */
static BOOL s_Bc250Dx54PersistentDescriptors = FALSE;
static unsigned persistentCreates, samplerCreates, resets;
#include "../umd/bc250_umd_descriptors.c"
/* SHADER.2: o gravador declara o helper de bytes por texel de bc250_d3d_umd.c. */
static UINT Bc250DxgiFormatBytesPerPixel(DXGI_FORMAT Format)
{ return Format == DXGI_FORMAT_R8_UINT ? 1u : (Format == DXGI_FORMAT_R32_UINT ? 4u : 0u); }
static VkResult VKAPI_CALL CreatePool(VkDevice d, const VkDescriptorPoolCreateInfo *p, const VkAllocationCallbacks *a, VkDescriptorPool *out)
{ (void)d; (void)a;
  if (p->maxSets == BC250_DX54_POOL_MAX_SETS) { assert(s_Bc250Dx54PersistentDescriptors && p->poolSizeCount == 4u); persistentCreates++; *out = (VkDescriptorPool)(uintptr_t)0x5000; return VK_SUCCESS; }
  assert(p->maxSets == 1); assert(p->poolSizeCount == (imageMode ? 2u : 1u)); *out = (VkDescriptorPool)(uintptr_t)++creates; return VK_SUCCESS; }
static VkResult VKAPI_CALL ResetPool(VkDevice d, VkDescriptorPool p, VkDescriptorPoolResetFlags f)
{ (void)d; (void)f; assert(p == (VkDescriptorPool)(uintptr_t)0x5000); resets++; return VK_SUCCESS; }
static VkResult VKAPI_CALL Allocate(VkDevice d, const VkDescriptorSetAllocateInfo *p, VkDescriptorSet *out)
{ (void)d; assert(p->descriptorSetCount == 1); allocations++; if (failAllocation) return VK_ERROR_OUT_OF_POOL_MEMORY; *out = (VkDescriptorSet)(uintptr_t)allocations; return VK_SUCCESS; }
static void VKAPI_CALL Update(VkDevice d, uint32_t n, const VkWriteDescriptorSet *w, uint32_t c, const VkCopyDescriptorSet *copy)
{ (void)d; (void)c; (void)copy;
  if (texelMode) { assert(n == 1 && w[0].dstBinding == 288 && w[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    assert(w[0].pTexelBufferView && *w[0].pTexelBufferView == (VkBufferView)(uintptr_t)0x31); updates++; return; }
  assert(n == 2);
  if (imageMode) { assert(w[0].dstBinding == 291 && w[1].dstBinding == 418); assert(w[0].pImageInfo->imageView && w[0].pImageInfo->imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); assert(w[1].pImageInfo->sampler); }
  else { assert(w[0].dstBinding == 0 && w[1].dstBinding == 256); assert(w[0].pBufferInfo->buffer == expectedVs && w[1].pBufferInfo->buffer == expectedPs); assert(w[0].pBufferInfo->range == 64 && w[1].pBufferInfo->range == 64); } updates++; }
static void VKAPI_CALL Bind(VkCommandBuffer c, VkPipelineBindPoint p, VkPipelineLayout l, uint32_t first, uint32_t n, const VkDescriptorSet *s, uint32_t offsets, const uint32_t *o)
{ (void)c; (void)p; (void)l; (void)s; (void)o; assert(first == 0 && n == 1 && offsets == 0 && updates > binds); binds++; }
static void VKAPI_CALL Destroy(VkDevice d, VkDescriptorPool p, const VkAllocationCallbacks *a)
{ (void)d; (void)p; (void)a; destroys++; }
static VkResult VKAPI_CALL View(VkDevice d, const VkImageViewCreateInfo *p, const VkAllocationCallbacks *a, VkImageView *out)
{ (void)d; (void)a; assert(p->image == (VkImage)(uintptr_t)20 && p->subresourceRange.levelCount == 1); *out = (VkImageView)(uintptr_t)21; return VK_SUCCESS; }
static VkResult VKAPI_CALL Sampler(VkDevice d, const VkSamplerCreateInfo *p, const VkAllocationCallbacks *a, VkSampler *out)
{ (void)d; (void)a; assert(p->minFilter == VK_FILTER_LINEAR && p->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE); samplerCreates++; *out = (VkSampler)(uintptr_t)22; return VK_SUCCESS; }
static VkResult VKAPI_CALL BufferView(VkDevice d, const VkBufferViewCreateInfo *p, const VkAllocationCallbacks *a, VkBufferView *out)
{ (void)d; (void)a; assert(p->buffer == (VkBuffer)(uintptr_t)30 && p->format == VK_FORMAT_R8_UINT && p->offset == 16 && p->range == 32);
  bufferViewsCreated++; *out = (VkBufferView)(uintptr_t)0x31; return VK_SUCCESS; }
static void VKAPI_CALL DestroyBufferView(VkDevice d, VkBufferView v, const VkAllocationCallbacks *a)
{ (void)d; (void)a; assert(v == (VkBufferView)(uintptr_t)0x31); bufferViewsDestroyed++; }
static void VKAPI_CALL DestroyView(VkDevice d, VkImageView v, const VkAllocationCallbacks *a)
{ (void)d; (void)v; (void)a; viewsDestroyed++; }
static void VKAPI_CALL DestroySampler(VkDevice d, VkSampler v, const VkAllocationCallbacks *a)
{ (void)d; (void)v; (void)a; samplersDestroyed++; }
static void VKAPI_CALL Barrier(VkCommandBuffer c, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags dep,
    uint32_t mc, const VkMemoryBarrier *m, uint32_t bc, const VkBufferMemoryBarrier *b, uint32_t ic, const VkImageMemoryBarrier *im)
{ (void)c; (void)src; (void)dst; (void)dep; (void)mc; (void)m; (void)bc; (void)b;
  assert(ic == 1 && im->image == (VkImage)(uintptr_t)20);
  assert(im->newLayout == ((barriers % 2u) == 0 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)); barriers++; }
int main(void)
{
    BC250_UMD_DEVICE *d = calloc(1, sizeof(*d));
    BC250_UMD_RESOURCE vs = {0}, ps = {0};
    uint32_t shadow[16] = {1};
    BOOL has;
    VkPipelineLayout layout = (VkPipelineLayout)(uintptr_t)1;
    BC250_PSO_ENTRY *entry = &d->PipelineCache.Entries[0];
    assert(d);
    entry->InUse = TRUE; entry->PipelineLayout = layout;
    d->VkBridge.vk.vkCreateDescriptorPool = CreatePool;
    d->VkBridge.vk.vkAllocateDescriptorSets = Allocate;
    d->VkBridge.vk.vkUpdateDescriptorSets = Update;
    d->VkBridge.vk.vkCmdBindDescriptorSets = Bind;
    d->VkBridge.vk.vkDestroyDescriptorPool = Destroy;
    assert(Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has) && !has && creates == 0);
    entry->BindingCount = 2;
    entry->Bindings[0] = (VkDescriptorSetLayoutBinding){0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL};
    entry->Bindings[1] = (VkDescriptorSetLayoutBinding){256, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    vs.VkBuf = expectedVs = (VkBuffer)(uintptr_t)7; ps.VkBuf = expectedPs = (VkBuffer)(uintptr_t)8;
    vs.Width = ps.Width = vs.TotalByteSize = ps.TotalByteSize = 64;
    vs.pCpuShadow = ps.pCpuShadow = shadow;
    d->PipelineState.VsConstantBuffers[0].pDrvPrivate = &vs;
    d->PipelineState.PsConstantBuffers[0].pDrvPrivate = &ps;
    assert(Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has) && has);
    assert(binds == 1 && uploads == 2 && destroys == 0);
    Bc250UmdRestoreDescriptorImages(&d->VkBridge, VK_NULL_HANDLE);
    /* Changed PS b0 must use a new set, never overwrite the earlier one. */
    ps.VkBuf = expectedPs = (VkBuffer)(uintptr_t)9;
    assert(Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has));
    assert(creates == 2 && binds == 2 && destroys == 0);
    Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge);
    assert(destroys == 2);
    failAllocation = 1;
    assert(!Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has));
    assert(binds == 2 && d->VkBridge.SubmissionFailed && destroys == 2);
    Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge);
    assert(destroys == 3);
    d->VkBridge.SubmissionFailed = FALSE;
    d->PipelineState.PsConstantBuffers[0].pDrvPrivate = NULL;
    assert(!Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has));
    assert(binds == 2);
    Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge);
    {
        BC250_UMD_RESOURCE texture = {0};
        BC250_UMD_SRV srv = {0};
        BC250_UMD_SAMPLER sampler = {0};
        imageMode = 1; failAllocation = 0; d->VkBridge.SubmissionFailed = FALSE;
        d->VkBridge.vk.vkCreateImageView = View; d->VkBridge.vk.vkDestroyImageView = DestroyView;
        d->VkBridge.vk.vkCreateSampler = Sampler; d->VkBridge.vk.vkDestroySampler = DestroySampler;
        d->VkBridge.vk.vkCmdPipelineBarrier = Barrier;
        texture.VkImg = (VkImage)(uintptr_t)20; texture.MipLevels = texture.ArraySize = texture.SampleCount = 1;
        texture.VkLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        srv.Created = TRUE; srv.pResource = &texture; srv.Desc.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
        srv.Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; srv.Desc.Tex2D.MipLevels = srv.Desc.Tex2D.ArraySize = 1;
        sampler.Created = TRUE; sampler.Desc.Filter = (D3D10_DDI_FILTER)0x15;
        sampler.Desc.AddressU = sampler.Desc.AddressV = sampler.Desc.AddressW = (D3D10_DDI_TEXTURE_ADDRESS_MODE)3;
        sampler.Desc.ComparisonFunc = (D3D10_DDI_COMPARISON_FUNC)1;
        d->PipelineState.PsShaderResources[3].pDrvPrivate = &srv;
        d->PipelineState.PsSamplers[2].pDrvPrivate = &sampler;
        entry->Bindings[0] = (VkDescriptorSetLayoutBinding){291, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        entry->Bindings[1] = (VkDescriptorSetLayoutBinding){418, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        assert(Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has));
        assert(barriers == 1 && viewsDestroyed == 0 && samplersDestroyed == 0);
        Bc250UmdRestoreDescriptorImages(&d->VkBridge, VK_NULL_HANDLE);
        assert(barriers == 2);
        Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge);
        assert(viewsDestroyed == 1 && samplersDestroyed == 1);
        texture.VkLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        assert(!Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has));
        Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge);

        /*
         * DX.54: pool persistente e cache de samplers. Um pool so' para varios
         * draws, sampler criado uma vez e NUNCA destruido pelo lote, e o 65o
         * conjunto sem reset cai no pool por draw -- contado.
         */
        {
            unsigned createsBefore, samplersBefore, destroysBefore, draw;
            s_Bc250Dx54PersistentDescriptors = TRUE;
            d->VkBridge.vk.vkResetDescriptorPool = ResetPool;
            entry->DescriptorSetLayout = (VkDescriptorSetLayout)(uintptr_t)0x77;
            d->VkBridge.SubmissionFailed = FALSE;
            texture.VkLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            createsBefore = creates; samplersBefore = samplersDestroyed; destroysBefore = destroys;
            samplerCreates = 0;
            for (draw = 0; draw < BC250_DX54_POOL_MAX_SETS; ++draw) {
                assert(Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has) && has);
                Bc250UmdRestoreDescriptorImages(&d->VkBridge, VK_NULL_HANDLE);
            }
            assert(persistentCreates == 1u && creates == createsBefore);
            assert(samplerCreates == 1u && d->VkBridge.SamplerCacheCount == 1u);
            assert(d->VkBridge.PersistentPoolSets == BC250_DX54_POOL_MAX_SETS);
            assert(d->VkBridge.DescriptorFallbackPools == 0u);
            /* Esgotado sem fence: o proximo draw usa pool proprio, contado. */
            assert(Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has) && has);
            Bc250UmdRestoreDescriptorImages(&d->VkBridge, VK_NULL_HANDLE);
            assert(creates == createsBefore + 1u && d->VkBridge.DescriptorFallbackPools == 1u);
            Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge);
            /* O lote destroi so' o pool por draw; sampler do cache e pool persistente ficam. */
            assert(destroys == destroysBefore + 1u && samplersDestroyed == samplersBefore);
            assert(d->VkBridge.PersistentPool == (VkDescriptorPool)(uintptr_t)0x5000);
            s_Bc250Dx54PersistentDescriptors = FALSE;
        }
    }
    /*
     * SHADER.2 (Corrida 161): Buffer<uint> em t0 do PS -- o shader do menu
     * Iniciar. Vinculo 288 = PS + SRV_BASE + 0, UNIFORM_TEXEL_BUFFER, view com
     * o formato e a faixa da SRV; cada recusa sai com o proprio nome.
     */
    {
        BC250_UMD_RESOURCE buf = {0};
        BC250_UMD_SRV srv = {0};
        uint8_t bytes[64] = {7};
        imageMode = 0; texelMode = 1; failAllocation = 0; d->VkBridge.SubmissionFailed = FALSE;
        d->VkBridge.vk.vkCreateBufferView = BufferView; d->VkBridge.vk.vkDestroyBufferView = DestroyBufferView;
        buf.VkBuf = (VkBuffer)(uintptr_t)30; buf.Width = buf.TotalByteSize = 64; buf.pCpuShadow = bytes;
        srv.Created = TRUE; srv.pResource = &buf; srv.Desc.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
        srv.Desc.Format = DXGI_FORMAT_R8_UINT; srv.Desc.Buffer.FirstElement = 16; srv.Desc.Buffer.NumElements = 32;
        d->PipelineState.PsShaderResources[0].pDrvPrivate = &srv;
        entry->BindingCount = 1;
        entry->Bindings[0] = (VkDescriptorSetLayoutBinding){288, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        { unsigned u = uploads, b = binds;
          assert(Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has) && has);
          assert(uploads == u + 1 && binds == b + 1 && bufferViewsCreated == 1); }
        Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge);
        assert(bufferViewsDestroyed == 1);
        srv.Desc.Buffer.NumElements = 64; /* 16 + 64 > 64 bytes */
        assert(!Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has));
        assert(strcmp(d->LastVkDrawFailureReason, "SRV_BUFFER_FAIXA_FORA_DO_RECURSO") == 0);
        Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge); d->VkBridge.SubmissionFailed = FALSE;
        srv.Desc.Buffer.FirstElement = 3; srv.Desc.Buffer.NumElements = 8;
        assert(!Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has));
        assert(strcmp(d->LastVkDrawFailureReason, "SRV_BUFFER_DESLOCAMENTO_DESALINHADO") == 0);
        Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge); d->VkBridge.SubmissionFailed = FALSE;
        srv.Desc.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
        assert(!Bc250UmdBindDrawDescriptors(d, layout, VK_NULL_HANDLE, &has));
        assert(strcmp(d->LastVkDrawFailureReason, "SRV_TEXEL_DIMENSAO_NAO_E_BUFFER") == 0);
        Bc250UmdVkReleaseDescriptorBatches(&d->VkBridge); d->VkBridge.SubmissionFailed = FALSE;
        assert(bufferViewsCreated == 1 && bufferViewsDestroyed == 1);
        texelMode = 0; d->PipelineState.PsShaderResources[0].pDrvPrivate = NULL;
    }
    /* Reflexao: OpTypeImage Dim=Buffer Sampled=1 (o que o vkd3d 1.17 emitiu
     * para o shader reproduzido) -> UNIFORM_TEXEL_BUFFER no vinculo 288;
     * Sampled=2 (storage) segue sem tipo e recusa o shader inteiro. */
    { uint32_t spv[] = {0x07230203, 0x10000, 0, 20, 0,
          (4u << 16) | 71u, 12, 34, 0, (4u << 16) | 71u, 12, 33, 288,
          (9u << 16) | 25u, 10, 9, 5, 2, 0, 0, 1, 0,
          (4u << 16) | 32u, 11, 0, 10, (4u << 16) | 59u, 11, 12, 0};
      VkDescriptorSetLayoutBinding b[2];
      uint32_t n = (uint32_t)(sizeof(spv) / sizeof(spv[0]));
      assert(Bc250ReflectSpirvBindings(spv, n, VK_SHADER_STAGE_FRAGMENT_BIT, b, 2) == 1u);
      assert(b[0].binding == 288 && b[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
      spv[20] = 2; /* Sampled=2: storage texel buffer, fora do escopo */
      assert(Bc250ReflectSpirvBindings(spv, n, VK_SHADER_STAGE_FRAGMENT_BIT, b, 2) == UINT32_MAX); }
    /* Malformed reflection is an error, never an empty descriptor layout. */
    { uint32_t invalid[] = {0x07230203, 0x10000, 0, 4, 0, 0};
      VkDescriptorSetLayoutBinding b[1];
      assert(Bc250ReflectSpirvBindings(invalid, 6, VK_SHADER_STAGE_VERTEX_BIT, b, 1) == UINT32_MAX); }
    free(d);
    puts("UMD_DESCRIPTOR_BINDING: PASS (offline dispatch spy; no hardware)");
    return 0;
}
