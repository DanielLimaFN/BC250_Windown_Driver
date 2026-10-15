/*
 * bc250_vram_stress.c -- carga de VRAM cheia para a L1b (doc 663).
 *
 * Imita a assinatura das travas do Windows: VRAM quase cheia e alocacoes
 * novas de tela cheia (2560x1080x4 = 11.059.200 bytes) zeradas pelo kernel.
 * Rode com RADV_DEBUG=zerovram para o amdgpu zerar cada alocacao nova
 * (AMDGPU_GEM_CREATE_VRAM_CLEARED, feito pelo SDMA), e RADV_PERFTEST=transfer_queue
 * para expor a fila de copia (SDMA), se a versao da RADV tiver.
 *
 * Uso: bc250_vram_stress ALVO_MB DURACAO_S [TROCAS_POR_CICLO]
 * Saida: uma linha por segundo em stdout (o script redireciona para arquivo).
 * Nao confere dados: e' carga, nao prova (Lei 2 vale para o nosso driver).
 *
 * Compilar: gcc -O2 -o bc250_vram_stress bc250_vram_stress.c -lvulkan
 */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TELA_BYTES 11059200ull
#define MAX_BUF 64

typedef struct { VkBuffer buf; VkDeviceMemory mem; int vivo; } Slot;

static VkDevice g_dev;
static uint32_t g_tipo;
static Slot g_slot[MAX_BUF];

static double agora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int alocar(Slot *s)
{
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements req;
    bi.size = TELA_BYTES;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_dev, &bi, NULL, &s->buf) != VK_SUCCESS) return 0;
    vkGetBufferMemoryRequirements(g_dev, s->buf, &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = g_tipo;
    if (vkAllocateMemory(g_dev, &ai, NULL, &s->mem) != VK_SUCCESS) {
        vkDestroyBuffer(g_dev, s->buf, NULL);
        return 0;
    }
    vkBindBufferMemory(g_dev, s->buf, s->mem, 0);
    s->vivo = 1;
    return 1;
}

static void liberar(Slot *s)
{
    if (!s->vivo) return;
    vkDestroyBuffer(g_dev, s->buf, NULL);
    vkFreeMemory(g_dev, s->mem, NULL);
    s->vivo = 0;
}

int main(int argc, char **argv)
{
    uint32_t alvo_mb = argc > 1 ? (uint32_t)atoi(argv[1]) : 200;
    double dur = argc > 2 ? atof(argv[2]) : 600.0;
    uint32_t trocas = argc > 3 ? (uint32_t)atoi(argv[3]) : 2;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkInstance inst;
    VkPhysicalDevice fis[8], pd = VK_NULL_HANDLE;
    uint32_t n = 8, i, nf = 16, fam_g = UINT32_MAX, fam_t = UINT32_MAX;
    VkQueueFamilyProperties fp[16];
    VkPhysicalDeviceMemoryProperties mp;
    VkDeviceQueueCreateInfo qci[2];
    float prio = 1.0f;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkQueue qg, qt = VK_NULL_HANDLE;
    VkCommandPool pool_g, pool_t = VK_NULL_HANDLE;
    VkCommandBuffer cb_g, cb_t = VK_NULL_HANDLE;
    VkFence fen_g, fen_t = VK_NULL_HANDLE;
    uint32_t vivos = 0, falhas = 0, alocs = 0, libs = 0, subm = 0, timeouts = 0;
    double t0, ult, max_ms = 0.0;

    app.apiVersion = VK_API_VERSION_1_1;
    app.pApplicationName = "bc250_vram_stress";
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) { puts("ERRO vkCreateInstance"); return 1; }
    vkEnumeratePhysicalDevices(inst, &n, fis);
    for (i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(fis[i], &p);
        if (p.vendorID == 0x1002) { pd = fis[i]; printf("GPU %s\n", p.deviceName); break; }
    }
    if (pd == VK_NULL_HANDLE) { puts("ERRO sem GPU AMD"); return 1; }

    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nf, fp);
    for (i = 0; i < nf; ++i) {
        if ((fp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && fam_g == UINT32_MAX) fam_g = i;
        else if ((fp[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                 !(fp[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) && fam_t == UINT32_MAX) fam_t = i;
    }
    printf("FILAS grafica=%u copia=%s\n", fam_g, fam_t == UINT32_MAX ? "nenhuma" : "sim");

    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    g_tipo = UINT32_MAX;
    for (i = 0; i < mp.memoryTypeCount; ++i) {  /* VRAM pura primeiro */
        VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
        if ((f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { g_tipo = i; break; }
    }
    if (g_tipo == UINT32_MAX)
        for (i = 0; i < mp.memoryTypeCount; ++i)
            if (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { g_tipo = i; break; }
    if (g_tipo == UINT32_MAX) { puts("ERRO sem memoria DEVICE_LOCAL"); return 1; }
    printf("MEMORIA tipo=%u heap=%u heap_mb=%llu\n", g_tipo, mp.memoryTypes[g_tipo].heapIndex,
           (unsigned long long)(mp.memoryHeaps[mp.memoryTypes[g_tipo].heapIndex].size >> 20));

    memset(qci, 0, sizeof(qci));
    qci[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci[0].queueFamilyIndex = fam_g; qci[0].queueCount = 1; qci[0].pQueuePriorities = &prio;
    qci[1] = qci[0]; qci[1].queueFamilyIndex = fam_t;
    dci.queueCreateInfoCount = fam_t == UINT32_MAX ? 1 : 2;
    dci.pQueueCreateInfos = qci;
    if (vkCreateDevice(pd, &dci, NULL, &g_dev) != VK_SUCCESS) { puts("ERRO vkCreateDevice"); return 1; }
    vkGetDeviceQueue(g_dev, fam_g, 0, &qg);
    {
        VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = fam_g;
        vkCreateCommandPool(g_dev, &pci, NULL, &pool_g);
        cai.commandPool = pool_g; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
        vkAllocateCommandBuffers(g_dev, &cai, &cb_g);
        vkCreateFence(g_dev, &fci, NULL, &fen_g);
        if (fam_t != UINT32_MAX) {
            vkGetDeviceQueue(g_dev, fam_t, 0, &qt);
            pci.queueFamilyIndex = fam_t;
            vkCreateCommandPool(g_dev, &pci, NULL, &pool_t);
            cai.commandPool = pool_t;
            vkAllocateCommandBuffers(g_dev, &cai, &cb_t);
            vkCreateFence(g_dev, &fci, NULL, &fen_t);
        }
    }

    /* Enche ate o alvo (ou ate o kernel recusar). */
    for (i = 0; i < MAX_BUF && (uint64_t)vivos * TELA_BYTES < (uint64_t)alvo_mb << 20; ++i) {
        if (alocar(&g_slot[i])) { ++vivos; ++alocs; } else { ++falhas; break; }
    }
    printf("CHEIO buffers=%u mb=%llu falhas=%u\n", vivos, (unsigned long long)((vivos * TELA_BYTES) >> 20), falhas);
    fflush(stdout);

    srand(250);
    t0 = ult = agora();
    while (agora() - t0 < dur) {
        uint32_t k;
        VkCommandBufferBeginInfo bb = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkBufferCopy reg = { 0, 0, TELA_BYTES };
        int a = -1, b = -1;
        double ts;

        /* Troca: libera e realoca TROCAS buffers (zeramento de alocacao nova). */
        for (k = 0; k < trocas; ++k) {
            uint32_t j = (uint32_t)rand() % MAX_BUF;
            if (g_slot[j].vivo) { liberar(&g_slot[j]); --vivos; ++libs; }
            if (alocar(&g_slot[j])) { ++vivos; ++alocs; } else ++falhas;
        }
        for (k = 0; k < MAX_BUF && (a < 0 || b < 0); ++k) {
            uint32_t j = (uint32_t)rand() % MAX_BUF;
            if (!g_slot[j].vivo) continue;
            if (a < 0) a = (int)j; else if ((int)j != a) b = (int)j;
        }
        if (a < 0 || b < 0) continue;

        /* Fila grafica: fill + copia de tela cheia. */
        vkResetCommandBuffer(cb_g, 0);
        vkBeginCommandBuffer(cb_g, &bb);
        vkCmdFillBuffer(cb_g, g_slot[a].buf, 0, VK_WHOLE_SIZE, 0x250250u + subm);
        vkCmdCopyBuffer(cb_g, g_slot[a].buf, g_slot[b].buf, 1, &reg);
        vkEndCommandBuffer(cb_g);
        si.commandBufferCount = 1; si.pCommandBuffers = &cb_g;
        ts = agora();
        vkResetFences(g_dev, 1, &fen_g);
        if (vkQueueSubmit(qg, 1, &si, fen_g) == VK_SUCCESS) {
            ++subm;
            if (vkWaitForFences(g_dev, 1, &fen_g, VK_TRUE, 5000000000ull) != VK_SUCCESS) {
                ++timeouts; printf("TIMEOUT fila grafica submissao=%u\n", subm); fflush(stdout);
            }
        }
        /* Fila de copia (SDMA), se exposta: copia de volta. */
        if (qt != VK_NULL_HANDLE) {
            vkResetCommandBuffer(cb_t, 0);
            vkBeginCommandBuffer(cb_t, &bb);
            vkCmdCopyBuffer(cb_t, g_slot[b].buf, g_slot[a].buf, 1, &reg);
            vkEndCommandBuffer(cb_t);
            si.pCommandBuffers = &cb_t;
            vkResetFences(g_dev, 1, &fen_t);
            if (vkQueueSubmit(qt, 1, &si, fen_t) == VK_SUCCESS) {
                ++subm;
                if (vkWaitForFences(g_dev, 1, &fen_t, VK_TRUE, 5000000000ull) != VK_SUCCESS) {
                    ++timeouts; printf("TIMEOUT fila de copia submissao=%u\n", subm); fflush(stdout);
                }
            }
        }
        if ((agora() - ts) * 1000.0 > max_ms) max_ms = (agora() - ts) * 1000.0;

        if (agora() - ult >= 1.0) {
            printf("t=%.0f vivos=%u mb=%llu alocs=%u libs=%u falhas=%u subm=%u timeouts=%u max_ms=%.1f\n",
                   agora() - t0, vivos, (unsigned long long)((vivos * TELA_BYTES) >> 20),
                   alocs, libs, falhas, subm, timeouts, max_ms);
            fflush(stdout);
            ult = agora();
            max_ms = 0.0;
        }
    }
    vkDeviceWaitIdle(g_dev);
    for (i = 0; i < MAX_BUF; ++i) liberar(&g_slot[i]);
    printf("FIM alocs=%u libs=%u falhas=%u subm=%u timeouts=%u\n", alocs, libs, falhas, subm, timeouts);
    return timeouts ? 3 : 0;
}
