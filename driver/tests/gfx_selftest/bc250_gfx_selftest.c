/*
 * bc250-gfx-selftest -- controle positivo do caminho grafico, em Vulkan puro.
 *
 * Faixa DX.26 / Fase B do docs/337 (22/09/2026). As auditorias externas e a
 * Fase A (docs/338) mostraram que o instrumento de leitura nunca teve controle
 * positivo: em 688 inspecoes desde 17/09 nunca apareceu um valor nao-nulo. Este
 * programa isola cada elo, SEM passar pela traducao D3D11 e SEM apresentar nada
 * na tela, carregando o ICD diretamente (como a UMD faz):
 *
 *   S1 CPU grava sentinela; GPU vkCmdFillBuffer(S1_FILL)   -> map/coerencia/fence
 *   S2 vkCmdClearColorImage(magenta) + copia imagem->buffer -> imagem/copia/tiling
 *   S3 render pass loadOp=CLEAR (verde) + copia            -> CB escreve?
 *   S4 CLEAR verde + triangulo de tela cheia, PS branco    -> rasterizacao
 *   S5 igual a S4 com contador atomico por fragmento       -> ha fragmentos?
 *
 * O buffer de leitura e' reenvenenado PELA GPU antes de cada copia. Falha de
 * map e timeout de fence sao estados PROPRIOS, nunca viram zero. A variante do
 * RADV (RADV_DEBUG) vem do ambiente e e' lida no vkCreateInstance; o runner roda
 * este executavel uma vez por variante, em processos separados.
 *
 * NAO e' prova de desktop. Nada deste programa vai ao scanout.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include "selftest_spv.inl"

#define W 256u
#define H 256u
#define POISON 0xA5A5A5A5u
/*
 * DX.27: o S1 da Corrida 72 preenchia com POISON e esperava POISON, e a contagem
 * classificava "veneno" antes de "igual" -- saia FAIL com 65536/65536 certos.
 * Agora a CPU grava uma sentinela, a GPU preenche com um valor DIFERENTE do
 * veneno e da sentinela, e so' esse valor conta como PASS.
 */
#define CPU_SENTINEL 0x5E5E5E5Eu
#define S1_FILL 0x3C3C3C3Cu
#define C_MAGENTA 0xFFFF00FFu /* B8G8R8A8: B=FF G=00 R=FF A=FF */
#define C_GREEN   0xFF00FF00u /* B=00 G=FF R=00 A=FF */
#define C_WHITE   0xFFFFFFFFu

typedef VkResult (VKAPI_PTR *PFN_Negotiate)(uint32_t *);

static PFN_vkGetInstanceProcAddr g_gipa;
static PFN_vkGetDeviceProcAddr g_gdpa;
static VkInstance g_inst;
static VkPhysicalDevice g_phys;
static VkDevice g_dev;
static VkQueue g_queue;
static uint32_t g_qfam = UINT32_MAX;
static VkPhysicalDeviceMemoryProperties g_mem;

#define IFN(n) static PFN_##n p_##n
#define DFN(n) static PFN_##n p_##n
IFN(vkCreateInstance); IFN(vkEnumeratePhysicalDevices); IFN(vkGetPhysicalDeviceProperties);
IFN(vkGetPhysicalDeviceQueueFamilyProperties); IFN(vkGetPhysicalDeviceMemoryProperties); IFN(vkCreateDevice);
DFN(vkGetDeviceQueue); DFN(vkCreateBuffer); DFN(vkGetBufferMemoryRequirements); DFN(vkAllocateMemory);
DFN(vkBindBufferMemory); DFN(vkCreateImage); DFN(vkGetImageMemoryRequirements); DFN(vkBindImageMemory);
DFN(vkCreateImageView); DFN(vkMapMemory); DFN(vkUnmapMemory); DFN(vkCreateRenderPass); DFN(vkCreateFramebuffer);
DFN(vkCreateShaderModule); DFN(vkCreateDescriptorSetLayout); DFN(vkCreatePipelineLayout);
DFN(vkCreateGraphicsPipelines); DFN(vkCreateDescriptorPool); DFN(vkAllocateDescriptorSets);
DFN(vkUpdateDescriptorSets); DFN(vkCreateCommandPool); DFN(vkAllocateCommandBuffers); DFN(vkBeginCommandBuffer);
DFN(vkEndCommandBuffer); DFN(vkResetCommandBuffer); DFN(vkQueueSubmit); DFN(vkCreateFence); DFN(vkWaitForFences);
DFN(vkResetFences); DFN(vkCmdFillBuffer); DFN(vkCmdPipelineBarrier); DFN(vkCmdClearColorImage);
DFN(vkCmdCopyImageToBuffer); DFN(vkCmdBeginRenderPass); DFN(vkCmdEndRenderPass); DFN(vkCmdBindPipeline);
DFN(vkCmdBindDescriptorSets); DFN(vkCmdDraw);

static void Log(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    fflush(stdout);
}

static uint32_t FindMem(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < g_mem.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (g_mem.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static VkResult MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *b, VkDeviceMemory *m)
{
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryRequirements r;
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkResult res;
    bi.size = size; bi.usage = usage;
    if ((res = p_vkCreateBuffer(g_dev, &bi, NULL, b)) != VK_SUCCESS) return res;
    p_vkGetBufferMemoryRequirements(g_dev, *b, &r);
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = FindMem(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return VK_ERROR_FEATURE_NOT_PRESENT;
    if ((res = p_vkAllocateMemory(g_dev, &ai, NULL, m)) != VK_SUCCESS) return res;
    return p_vkBindBufferMemory(g_dev, *b, *m, 0);
}

static VkCommandBuffer g_cmd;
static VkFence g_fence;

static void Begin(void)
{
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    p_vkResetCommandBuffer(g_cmd, 0);
    p_vkBeginCommandBuffer(g_cmd, &bi);
}

/* Envia e espera ate 3 s. VK_TIMEOUT e perda de device sao devolvidos, nunca engolidos. */
static VkResult Submit(void)
{
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkResult res;
    p_vkEndCommandBuffer(g_cmd);
    si.commandBufferCount = 1; si.pCommandBuffers = &g_cmd;
    p_vkResetFences(g_dev, 1, &g_fence);
    if ((res = p_vkQueueSubmit(g_queue, 1, &si, g_fence)) != VK_SUCCESS) return res;
    return p_vkWaitForFences(g_dev, 1, &g_fence, VK_TRUE, 3000000000ull);
}

static void BufBarrier(VkBuffer b, VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags ss, VkPipelineStageFlags ds)
{
    VkBufferMemoryBarrier m = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    m.srcAccessMask = src; m.dstAccessMask = dst;
    m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    m.buffer = b; m.size = VK_WHOLE_SIZE;
    p_vkCmdPipelineBarrier(g_cmd, ss, ds, 0, 0, NULL, 1, &m, 0, NULL);
}

static void ImgBarrier(VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst,
                       VkPipelineStageFlags ss, VkPipelineStageFlags ds)
{
    VkImageMemoryBarrier m = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    m.srcAccessMask = src; m.dstAccessMask = dst; m.oldLayout = from; m.newLayout = to;
    m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    m.image = img; m.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    m.subresourceRange.levelCount = 1; m.subresourceRange.layerCount = 1;
    p_vkCmdPipelineBarrier(g_cmd, ss, ds, 0, 0, NULL, 0, NULL, 1, &m);
}

static VkBuffer g_rb; static VkDeviceMemory g_rbm;

/* Reenvenena o buffer de leitura PELA GPU e copia a imagem (em TRANSFER_SRC) para ele. */
static void PoisonAndCopy(VkImage img)
{
    VkBufferImageCopy c;
    p_vkCmdFillBuffer(g_cmd, g_rb, 0, VK_WHOLE_SIZE, POISON);
    BufBarrier(g_rb, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    memset(&c, 0, sizeof(c));
    c.bufferRowLength = W; c.bufferImageHeight = H;
    c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.layerCount = 1;
    c.imageExtent.width = W; c.imageExtent.height = H; c.imageExtent.depth = 1;
    p_vkCmdCopyImageToBuffer(g_cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_rb, 1, &c);
    BufBarrier(g_rb, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
}

/* Conta o buffer inteiro: veneno, cor esperada, zeros, outros. Devolve 1 = PASS. */
static int Inspect(const char *step, VkResult sub, uint32_t expected)
{
    void *p = NULL;
    VkResult mr;
    uint32_t poison = 0, exp = 0, zero = 0, other = 0, firstIdx = UINT32_MAX, firstVal = 0;
    if (sub != VK_SUCCESS) {
        Log("[SELFTEST] %s: SUBMIT/FENCE res=%d%s -> NAO_MEDIDO\n", step, (int)sub,
            sub == VK_TIMEOUT ? " (TIMEOUT: provavel TDR)" : "");
        return -1;
    }
    mr = p_vkMapMemory(g_dev, g_rbm, 0, VK_WHOLE_SIZE, 0, &p);
    if (mr != VK_SUCCESS || p == NULL) {
        Log("[SELFTEST] %s: MAP_FALHOU res=%d -> NAO_MEDIDO (falha de medicao, nao imagem preta)\n", step, (int)mr);
        return -1;
    }
    for (uint32_t i = 0; i < W * H; ++i) {
        uint32_t v = ((const uint32_t *)p)[i];
        if (v == expected) ++exp;          /* o esperado primeiro: nunca confundir com veneno */
        else if (v == POISON) ++poison;
        else if (v == 0u) ++zero;
        else ++other;
        if (v != expected && firstIdx == UINT32_MAX) { firstIdx = i; firstVal = v; }
    }
    p_vkUnmapMemory(g_dev, g_rbm);
    Log("[SELFTEST] %s: esperado=0x%08X -> igual=%u veneno=%u zeros=%u outros=%u de %u; primeiro_divergente=%d(0x%08X) -> %s\n",
        step, expected, exp, poison, zero, other, W * H, firstIdx == UINT32_MAX ? -1 : (int)firstIdx, firstVal,
        exp == W * H ? "PASS" : "FAIL");
    return exp == W * H ? 1 : 0;
}

#define LOADI(n) p_##n = (PFN_##n)g_gipa(g_inst, #n)
#define LOADD(n) do { p_##n = (PFN_##n)g_gdpa(g_dev, #n); if (!p_##n) { Log("[SELFTEST] ERRO: %s ausente\n", #n); return 2; } } while (0)

int main(void)
{
    char dbg[128] = "(ausente)";
    HMODULE icd;
    PFN_Negotiate neg;
    uint32_t ver = 5, n = 1, nq = 0;
    VkQueueFamilyProperties qp[16];
    VkPhysicalDeviceProperties props;
    VkResult res;
    int r1, r2, r3 = -2, r4 = -2, r5 = -2;
    uint32_t counter = 0;

    GetEnvironmentVariableA("RADV_DEBUG", dbg, sizeof(dbg));
    Log("[SELFTEST] INICIO: RADV_DEBUG=%s alvo=%ux%u B8G8R8A8_UNORM\n", dbg, W, H);

    icd = LoadLibraryA("C:\\BC250_LIB\\vulkan_radeon.dll");
    if (!icd) { Log("[SELFTEST] ERRO: ICD nao carregado (%lu)\n", GetLastError()); return 2; }
    neg = (PFN_Negotiate)(void *)GetProcAddress(icd, "vk_icdNegotiateLoaderICDInterfaceVersion");
    g_gipa = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(icd, "vk_icdGetInstanceProcAddr");
    if (!neg || !g_gipa || neg(&ver) != VK_SUCCESS) { Log("[SELFTEST] ERRO: negociacao do ICD\n"); return 2; }

    {
        VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        VkInstanceCreateInfo ii = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        app.pApplicationName = "bc250-gfx-selftest"; app.apiVersion = VK_API_VERSION_1_1;
        ii.pApplicationInfo = &app;
        p_vkCreateInstance = (PFN_vkCreateInstance)g_gipa(NULL, "vkCreateInstance");
        if (!p_vkCreateInstance || (res = p_vkCreateInstance(&ii, NULL, &g_inst)) != VK_SUCCESS) {
            Log("[SELFTEST] ERRO: vkCreateInstance\n"); return 2;
        }
    }
    LOADI(vkEnumeratePhysicalDevices); LOADI(vkGetPhysicalDeviceProperties);
    LOADI(vkGetPhysicalDeviceQueueFamilyProperties); LOADI(vkGetPhysicalDeviceMemoryProperties); LOADI(vkCreateDevice);
    g_gdpa = (PFN_vkGetDeviceProcAddr)g_gipa(g_inst, "vkGetDeviceProcAddr");
    if (p_vkEnumeratePhysicalDevices(g_inst, &n, &g_phys) < 0 || n == 0) { Log("[SELFTEST] ERRO: sem GPU\n"); return 2; }
    p_vkGetPhysicalDeviceProperties(g_phys, &props);
    p_vkGetPhysicalDeviceMemoryProperties(g_phys, &g_mem);
    nq = 16; p_vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &nq, qp);
    for (uint32_t i = 0; i < nq; ++i) if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_qfam = i; break; }
    Log("[SELFTEST] GPU: '%s' vendor=0x%04X device=0x%04X fila_grafica=%u\n", props.deviceName, props.vendorID, props.deviceID, g_qfam);
    if (g_qfam == UINT32_MAX) return 2;
    {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        VkPhysicalDeviceFeatures f;
        VkDeviceCreateInfo di = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        memset(&f, 0, sizeof(f)); f.fragmentStoresAndAtomics = VK_TRUE;
        q.queueFamilyIndex = g_qfam; q.queueCount = 1; q.pQueuePriorities = &prio;
        di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &q; di.pEnabledFeatures = &f;
        if ((res = p_vkCreateDevice(g_phys, &di, NULL, &g_dev)) != VK_SUCCESS) { Log("[SELFTEST] ERRO: vkCreateDevice res=%d\n", (int)res); return 2; }
    }
    LOADD(vkGetDeviceQueue); LOADD(vkCreateBuffer); LOADD(vkGetBufferMemoryRequirements); LOADD(vkAllocateMemory);
    LOADD(vkBindBufferMemory); LOADD(vkCreateImage); LOADD(vkGetImageMemoryRequirements); LOADD(vkBindImageMemory);
    LOADD(vkCreateImageView); LOADD(vkMapMemory); LOADD(vkUnmapMemory); LOADD(vkCreateRenderPass); LOADD(vkCreateFramebuffer);
    LOADD(vkCreateShaderModule); LOADD(vkCreateDescriptorSetLayout); LOADD(vkCreatePipelineLayout);
    LOADD(vkCreateGraphicsPipelines); LOADD(vkCreateDescriptorPool); LOADD(vkAllocateDescriptorSets);
    LOADD(vkUpdateDescriptorSets); LOADD(vkCreateCommandPool); LOADD(vkAllocateCommandBuffers); LOADD(vkBeginCommandBuffer);
    LOADD(vkEndCommandBuffer); LOADD(vkResetCommandBuffer); LOADD(vkQueueSubmit); LOADD(vkCreateFence); LOADD(vkWaitForFences);
    LOADD(vkResetFences); LOADD(vkCmdFillBuffer); LOADD(vkCmdPipelineBarrier); LOADD(vkCmdClearColorImage);
    LOADD(vkCmdCopyImageToBuffer); LOADD(vkCmdBeginRenderPass); LOADD(vkCmdEndRenderPass); LOADD(vkCmdBindPipeline);
    LOADD(vkCmdBindDescriptorSets); LOADD(vkCmdDraw);
    p_vkGetDeviceQueue(g_dev, g_qfam, 0, &g_queue);

    /* recursos */
    VkImage img; VkDeviceMemory imgm; VkImageView view; VkRenderPass rp; VkFramebuffer fb;
    VkBuffer cnt; VkDeviceMemory cntm;
    {
        VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkMemoryRequirements r; VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_B8G8R8A8_UNORM;
        ic.extent.width = W; ic.extent.height = H; ic.extent.depth = 1; ic.mipLevels = 1; ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (p_vkCreateImage(g_dev, &ic, NULL, &img) != VK_SUCCESS) { Log("[SELFTEST] ERRO: vkCreateImage\n"); return 2; }
        p_vkGetImageMemoryRequirements(g_dev, img, &r);
        ai.allocationSize = r.size; ai.memoryTypeIndex = FindMem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = FindMem(r.memoryTypeBits, 0);
        if (p_vkAllocateMemory(g_dev, &ai, NULL, &imgm) != VK_SUCCESS || p_vkBindImageMemory(g_dev, img, imgm, 0) != VK_SUCCESS) {
            Log("[SELFTEST] ERRO: memoria da imagem\n"); return 2;
        }
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ic.format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        if (p_vkCreateImageView(g_dev, &vi, NULL, &view) != VK_SUCCESS) { Log("[SELFTEST] ERRO: view\n"); return 2; }
    }
    if (MakeBuffer((VkDeviceSize)W * H * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &g_rb, &g_rbm) != VK_SUCCESS ||
        MakeBuffer(256u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &cnt, &cntm) != VK_SUCCESS) {
        Log("[SELFTEST] ERRO: buffers\n"); return 2;
    }
    {
        VkAttachmentDescription a; VkAttachmentReference ar = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp; VkRenderPassCreateInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        memset(&a, 0, sizeof(a)); memset(&sp, 0, sizeof(sp));
        a.format = VK_FORMAT_B8G8R8A8_UNORM; a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &ar;
        rpi.attachmentCount = 1; rpi.pAttachments = &a; rpi.subpassCount = 1; rpi.pSubpasses = &sp;
        if (p_vkCreateRenderPass(g_dev, &rpi, NULL, &rp) != VK_SUCCESS) { Log("[SELFTEST] ERRO: render pass\n"); return 2; }
        fi.renderPass = rp; fi.attachmentCount = 1; fi.pAttachments = &view; fi.width = W; fi.height = H; fi.layers = 1;
        if (p_vkCreateFramebuffer(g_dev, &fi, NULL, &fb) != VK_SUCCESS) { Log("[SELFTEST] ERRO: framebuffer\n"); return 2; }
    }
    VkDescriptorSetLayout dsl; VkPipelineLayout pl; VkPipeline pipeWhite, pipeCount; VkDescriptorSet ds;
    {
        VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        VkDescriptorSetLayoutCreateInfo dli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
        VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        VkDescriptorSetAllocateInfo dai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        VkDescriptorBufferInfo dbi = {0, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet wd = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        VkDescriptorPool dp;
        dli.bindingCount = 1; dli.pBindings = &b;
        p_vkCreateDescriptorSetLayout(g_dev, &dli, NULL, &dsl);
        pli.setLayoutCount = 1; pli.pSetLayouts = &dsl;
        p_vkCreatePipelineLayout(g_dev, &pli, NULL, &pl);
        dpi.maxSets = 1; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps;
        p_vkCreateDescriptorPool(g_dev, &dpi, NULL, &dp);
        dai.descriptorPool = dp; dai.descriptorSetCount = 1; dai.pSetLayouts = &dsl;
        p_vkAllocateDescriptorSets(g_dev, &dai, &ds);
        dbi.buffer = cnt;
        wd.dstSet = ds; wd.descriptorCount = 1; wd.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; wd.pBufferInfo = &dbi;
        p_vkUpdateDescriptorSets(g_dev, 1, &wd, 0, NULL);
    }
    {
        VkShaderModuleCreateInfo si = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        VkShaderModule vs, psw, psc;
        VkPipelineShaderStageCreateInfo st[2];
        VkPipelineVertexInputStateCreateInfo vin = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        VkViewport vp = {0.0f, 0.0f, (float)W, (float)H, 0.0f, 1.0f};
        VkRect2D sc = {{0, 0}, {W, H}};
        VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState cba;
        VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkGraphicsPipelineCreateInfo gp = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        si.codeSize = sizeof(g_SelftestVs); si.pCode = g_SelftestVs; p_vkCreateShaderModule(g_dev, &si, NULL, &vs);
        si.codeSize = sizeof(g_SelftestPsWhite); si.pCode = g_SelftestPsWhite; p_vkCreateShaderModule(g_dev, &si, NULL, &psw);
        si.codeSize = sizeof(g_SelftestPsCount); si.pCode = g_SelftestPsCount; p_vkCreateShaderModule(g_dev, &si, NULL, &psc);
        memset(st, 0, sizeof(st));
        st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main";
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].pName = "main";
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_CLOCKWISE; rs.lineWidth = 1.0f;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        memset(&cba, 0, sizeof(cba)); cba.colorWriteMask = 0xF;
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        gp.stageCount = 2; gp.pStages = st; gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
        gp.layout = pl; gp.renderPass = rp;
        st[1].module = psw;
        res = p_vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gp, NULL, &pipeWhite);
        st[1].module = psc;
        if (res != VK_SUCCESS || p_vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gp, NULL, &pipeCount) != VK_SUCCESS) {
            Log("[SELFTEST] ERRO: pipelines\n"); return 2;
        }
    }
    {
        VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkCommandPool pool;
        cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex = g_qfam;
        p_vkCreateCommandPool(g_dev, &cpi, NULL, &pool);
        cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
        p_vkAllocateCommandBuffers(g_dev, &cai, &g_cmd);
        p_vkCreateFence(g_dev, &fci, NULL, &g_fence);
    }
    Log("[SELFTEST] RECURSOS OK: imagem, buffers, render pass, 2 pipelines\n");

    /* S1: sentinela da CPU, depois fill da GPU com valor proprio */
    {
        void *sp = NULL;
        if (p_vkMapMemory(g_dev, g_rbm, 0, VK_WHOLE_SIZE, 0, &sp) != VK_SUCCESS || sp == NULL) {
            Log("[SELFTEST] S1 FILL_BUFFER_GPU: MAP_FALHOU ao gravar a sentinela -> NAO_MEDIDO\n");
            r1 = -1;
        } else {
            for (uint32_t i = 0; i < W * H; ++i) ((volatile uint32_t *)sp)[i] = CPU_SENTINEL;
            p_vkUnmapMemory(g_dev, g_rbm);
            r1 = 0;
        }
    }
    if (r1 == 0) {
        Begin();
        p_vkCmdFillBuffer(g_cmd, g_rb, 0, VK_WHOLE_SIZE, S1_FILL);
        BufBarrier(g_rb, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
        r1 = Inspect("S1 FILL_BUFFER_GPU (sentinela CPU 0x5E5E5E5E -> fill GPU)", Submit(), S1_FILL);
    }

    /* S2 */
    Begin();
    ImgBarrier(img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    {
        VkClearColorValue cc; VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        cc.float32[0] = 1.0f; cc.float32[1] = 0.0f; cc.float32[2] = 1.0f; cc.float32[3] = 1.0f;
        p_vkCmdClearColorImage(g_cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &rr);
    }
    ImgBarrier(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    PoisonAndCopy(img);
    r2 = (r1 < 0) ? -1 : Inspect("S2 CLEAR_TRANSFERENCIA_MAGENTA", Submit(), C_MAGENTA);

    /* S3, S4, S5 compartilham o render pass com CLEAR verde */
    for (int step = 3; step <= 5; ++step) {
        VkRenderPassBeginInfo rb = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        VkClearValue cv;
        int prev = (step == 3) ? r2 : (step == 4 ? r3 : r4);
        if (prev < 0) {
            Log("[SELFTEST] S%d: PULADO -- passo anterior NAO_MEDIDO\n", step);
            if (step == 3) r3 = -1; else if (step == 4) r4 = -1; else r5 = -1;
            continue;
        }
        Begin();
        if (step == 5) {
            p_vkCmdFillBuffer(g_cmd, cnt, 0, VK_WHOLE_SIZE, 0u);
            BufBarrier(cnt, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        cv.color.float32[0] = 0.0f; cv.color.float32[1] = 1.0f; cv.color.float32[2] = 0.0f; cv.color.float32[3] = 1.0f;
        rb.renderPass = rp; rb.framebuffer = fb; rb.renderArea.extent.width = W; rb.renderArea.extent.height = H;
        rb.clearValueCount = 1; rb.pClearValues = &cv;
        p_vkCmdBeginRenderPass(g_cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
        if (step >= 4) {
            p_vkCmdBindPipeline(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, step == 4 ? pipeWhite : pipeCount);
            if (step == 5) p_vkCmdBindDescriptorSets(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, NULL);
            p_vkCmdDraw(g_cmd, 3, 1, 0, 0);
        }
        p_vkCmdEndRenderPass(g_cmd);
        if (step == 5) {
            BufBarrier(cnt, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
        }
        ImgBarrier(img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        PoisonAndCopy(img);
        res = Submit();
        if (step == 3) r3 = Inspect("S3 CLEAR_RENDER_PASS_VERDE", res, C_GREEN);
        else if (step == 4) r4 = Inspect("S4 DRAW_TELA_CHEIA_BRANCO", res, C_WHITE);
        else {
            r5 = Inspect("S5 DRAW_COM_CONTADOR", res, C_WHITE);
            if (res == VK_SUCCESS) {
                void *p = NULL;
                if (p_vkMapMemory(g_dev, cntm, 0, VK_WHOLE_SIZE, 0, &p) == VK_SUCCESS && p) {
                    counter = *(volatile uint32_t *)p;
                    p_vkUnmapMemory(g_dev, cntm);
                    Log("[SELFTEST] S5 CONTADOR_DE_FRAGMENTOS=%u (esperado %u) -> %s\n", counter, W * H,
                        counter == 0u ? "ZERO FRAGMENTOS" : (counter == W * H ? "PASS" : "PARCIAL"));
                } else {
                    Log("[SELFTEST] S5 CONTADOR: MAP_FALHOU -> NAO_MEDIDO\n");
                }
            }
        }
    }

    Log("[SELFTEST] VEREDITO RADV_DEBUG=%s S1=%d S2=%d S3=%d S4=%d S5=%d contador=%u (1=PASS 0=FAIL -1=NAO_MEDIDO)\n",
        dbg, r1, r2, r3, r4, r5, counter);
    return 0;
}
