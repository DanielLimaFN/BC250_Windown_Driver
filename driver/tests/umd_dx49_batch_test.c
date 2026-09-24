/*
 * DX.49 -- contrato OFFLINE do lote de draws, do anel de dados por draw e do
 * staging persistente de upload, com dispatch Vulkan espiao. Nao toca hardware.
 *
 * O que trava:
 *  - o anel entrega faixas alinhadas e disjuntas, e recusa (sem avancar) o que
 *    nao cabe -- o chamador drena ANTES de gravar, nunca no meio do draw;
 *  - o dreno sem gravacao nao submete nada;
 *  - o flush fecha o render pass aberto ANTES de vkEndCommandBuffer;
 *  - o anel so' volta ao inicio com a fence concluida; com timeout ele fica;
 *  - o staging e' criado uma vez, reusado, e so' cresce drenando antes.
 */
#include "../umd/bc250_umd_vulkan_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned g_submits, g_ends, g_endRenderPass, g_frees, g_allocs, g_buffers, g_destroyBuffers;
static int g_endRenderPassBeforeEnd;
static VkResult g_waitResult = VK_SUCCESS;
static unsigned char g_mem[3][17u * 1024u * 1024u];
static unsigned g_memNext;

static VkResult VKAPI_CALL CreateBuffer(VkDevice d, const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *b)
{ (void)d; (void)a; (void)i; *b = (VkBuffer)(uintptr_t)(100u + ++g_buffers); return VK_SUCCESS; }
static void VKAPI_CALL DestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a) { (void)d; (void)b; (void)a; g_destroyBuffers++; }
static void VKAPI_CALL Reqs(VkDevice d, VkBuffer b, VkMemoryRequirements *r)
{ (void)d; (void)b; r->size = 0; r->alignment = 256; r->memoryTypeBits = 1u; }
static VkResult VKAPI_CALL Alloc(VkDevice d, const VkMemoryAllocateInfo *i, const VkAllocationCallbacks *a, VkDeviceMemory *m)
{ (void)d; (void)i; (void)a; assert(g_memNext < 3u); *m = (VkDeviceMemory)(uintptr_t)(1u + g_memNext++); g_allocs++; return VK_SUCCESS; }
static void VKAPI_CALL Free(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; g_frees++; }
static VkResult VKAPI_CALL Bind(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize o) { (void)d; (void)b; (void)m; (void)o; return VK_SUCCESS; }
static VkResult VKAPI_CALL Map(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **p)
{ (void)d; (void)o; (void)s; (void)f; *p = g_mem[(uintptr_t)m - 1u]; return VK_SUCCESS; }
static VkResult VKAPI_CALL ResetCb(VkCommandBuffer c, VkCommandBufferResetFlags f) { (void)c; (void)f; return VK_SUCCESS; }
static VkResult VKAPI_CALL BeginCb(VkCommandBuffer c, const VkCommandBufferBeginInfo *i) { (void)c; (void)i; return VK_SUCCESS; }
static void VKAPI_CALL EndRp(VkCommandBuffer c) { (void)c; g_endRenderPass++; }
static VkResult VKAPI_CALL EndCb(VkCommandBuffer c) { (void)c; g_endRenderPassBeforeEnd = (g_endRenderPass > 0u); g_ends++; return VK_SUCCESS; }
static VkResult VKAPI_CALL ResetF(VkDevice d, uint32_t n, const VkFence *f) { (void)d; (void)n; (void)f; return VK_SUCCESS; }
static VkResult VKAPI_CALL Submit(VkQueue q, uint32_t n, const VkSubmitInfo *s, VkFence f) { (void)q; (void)n; (void)s; (void)f; g_submits++; return VK_SUCCESS; }
static VkResult VKAPI_CALL Wait(VkDevice d, uint32_t n, const VkFence *f, VkBool32 all, uint64_t t)
{ (void)d; (void)n; (void)f; (void)all; (void)t; return g_waitResult; }

int main(void)
{
    static BC250_UMD_VK_BRIDGE b;
    BOOL renderPassOpen = FALSE;
    VkBuffer buf;
    VkDeviceSize off, off2;
    unsigned char payload[300];
    BOOL grew;
    uint64_t us;

    memset(&b, 0, sizeof(b));
    b.DeviceActive = TRUE;
    b.Device = (VkDevice)(uintptr_t)1;
    b.CommandBuffer = (VkCommandBuffer)(uintptr_t)2;
    b.pRenderPassOpen = &renderPassOpen;
    b.MemProperties.memoryTypeCount = 1;
    b.MemProperties.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    b.vk.vkCreateBuffer = CreateBuffer; b.vk.vkDestroyBuffer = DestroyBuffer;
    b.vk.vkGetBufferMemoryRequirements = Reqs; b.vk.vkAllocateMemory = Alloc;
    b.vk.vkFreeMemory = Free; b.vk.vkBindBufferMemory = Bind; b.vk.vkMapMemory = Map;
    b.vk.vkResetCommandBuffer = ResetCb; b.vk.vkBeginCommandBuffer = BeginCb;
    b.vk.vkCmdEndRenderPass = EndRp; b.vk.vkEndCommandBuffer = EndCb;
    b.vk.vkResetFences = ResetF; b.vk.vkQueueSubmit = Submit; b.vk.vkWaitForFences = Wait;

    /* Anel: faixas alinhadas, disjuntas, dados copiados. */
    memset(payload, 0xA5, sizeof(payload));
    assert(Bc250UmdVkDrawRingFree(&b) == BC250_DX49_DRAW_RING_BYTES);
    assert(Bc250UmdVkDrawRingPut(&b, payload, 100, &buf, &off) && off == 0 && buf != VK_NULL_HANDLE);
    assert(Bc250UmdVkDrawRingPut(&b, payload, 300, &buf, &off2) && off2 == BC250_DX49_DRAW_RING_ALIGN);
    assert(b.DrawRingMapped[off2] == 0xA5 && b.DrawRingMapped[off2 + 299] == 0xA5);
    /* Nao cabe: recusa SEM avancar a cabeca. */
    {
        VkDeviceSize head = b.DrawRingHead;
        assert(!Bc250UmdVkDrawRingPut(&b, payload, BC250_DX49_DRAW_RING_BYTES, &buf, &off));
        assert(b.DrawRingHead == head && buf == VK_NULL_HANDLE);
    }

    /* Dreno sem gravacao: nada e' submetido. */
    assert(Bc250UmdVkDrainPending(&b, "teste") == VK_SUCCESS && g_submits == 0);

    /* Lote com render pass aberto: o flush fecha antes de End e zera o anel. */
    assert(Bc250UmdVkGetCommandBuffer(&b) != VK_NULL_HANDLE);
    b.PendingDraws = 3;
    renderPassOpen = TRUE;
    assert(Bc250UmdVkDrainPending(&b, "teste") == VK_SUCCESS);
    assert(g_submits == 1 && g_endRenderPass == 1 && g_endRenderPassBeforeEnd && !renderPassOpen);
    assert(b.DrawRingHead == 0 && b.PendingDraws == 0 && b.BatchDrawsFlushed == 3 && b.BatchFlushCount == 1);

    /* Fence que nao volta: o anel NAO volta ao inicio. */
    assert(Bc250UmdVkDrawRingPut(&b, payload, 100, &buf, &off));
    assert(Bc250UmdVkGetCommandBuffer(&b) != VK_NULL_HANDLE);
    g_waitResult = VK_TIMEOUT;
    assert(Bc250UmdVkFlushCommands(&b) == VK_TIMEOUT);
    assert(b.SubmissionFailed && b.DrawRingHead != 0 && b.BatchFlushCount == 1);
    g_waitResult = VK_SUCCESS;
    b.SubmissionFailed = FALSE;
    b.DrawRingHead = 0;

    /* Staging: criado uma vez, reusado, cresce drenando antes. */
    assert(Bc250UmdVkEnsureUploadStaging(&b, 1000, &grew, &us) && grew && b.UploadStagingSize >= 1000);
    assert(Bc250UmdVkEnsureUploadStaging(&b, 2u * 1024u * 1024u, &grew, &us) && !grew && b.UploadStagingGrowCount == 1);
    {
        unsigned submitsBefore = g_submits, freesBefore = g_frees;
        assert(Bc250UmdVkGetCommandBuffer(&b) != VK_NULL_HANDLE);
        assert(Bc250UmdVkEnsureUploadStaging(&b, 6u * 1024u * 1024u, &grew, &us) && grew);
        assert(g_submits == submitsBefore + 1 && g_frees == freesBefore + 1 && b.UploadStagingGrowCount == 2);
        assert(b.UploadStagingSize >= 6u * 1024u * 1024u);
    }

    puts("UMD DX49 BATCH CONTRACT: PASS (anel alinhado/disjunto, dreno, render pass fechado, "
         "anel retido sem fence, staging reusado; offline)");
    return 0;
}
