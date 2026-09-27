/*
 * Faixa DX.23 -- inspecao do buffer linear de Present por GRADE, offline.
 *
 * Reproduz a Corrida 68: conteudo SO nas 40 linhas inferiores (barra de
 * tarefas, scissor 0,1040..2560,1080). As 5 amostras antigas (4 pixels do
 * canto superior esquerdo + centro) dao 0/5 nesse quadro; a grade tem de
 * enxergar. Contraponto: superficie toda preta devolve 0.
 * NAO VALIDADO EM HARDWARE.
 */
#include "../umd/bc250_umd_vulkan_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

#define W 2560u
#define H 1080u
static uint32_t *g_Pixels;

static VkResult VKAPI_CALL FakeMap(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **pp)
{ (void)d; (void)m; (void)o; (void)s; (void)f; *pp = g_Pixels; return VK_SUCCESS; }
static void VKAPI_CALL FakeUnmap(VkDevice d, VkDeviceMemory m) { (void)d; (void)m; }

int main(void)
{
    BC250_UMD_VK_BRIDGE b;
    uint32_t n;
    g_Pixels = (uint32_t *)calloc((size_t)W * H, 4u);
    CHECK(g_Pixels != NULL);
    memset(&b, 0, sizeof(b));
    b.DeviceActive = TRUE;
    b.Device = (VkDevice)(uintptr_t)0x10;
    b.LinearPresentMem = (VkDeviceMemory)(uintptr_t)0x20;
    b.LinearPresentSize = (VkDeviceSize)W * H * 4u;
    b.vk.vkMapMemory = FakeMap;
    b.vk.vkUnmapMemory = FakeUnmap;

    /* Contraponto: tudo preto. */
    n = Bc250UmdVkInspectLinearPresentBuf(&b, W, H);
    CHECK(n == 0u);

    /* Corrida 68: so' a barra de tarefas tem conteudo. */
    for (uint32_t y = H - 40u; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x)
            g_Pixels[(size_t)y * W + x] = 0xFF202020u;
    CHECK(g_Pixels[0] == 0u && g_Pixels[(H / 2) * W + W / 2] == 0u); /* o metodo antigo leria 0/5 */
    n = Bc250UmdVkInspectLinearPresentBuf(&b, W, H);
    CHECK(n == 10u * (W / 8u)); /* 10 linhas da grade (1 em 4) x 320 colunas */

    free(g_Pixels);
    printf("UMD PRESENT GRID: PASS (preto=0; barra de 40 linhas vista pela grade e invisivel as 5 amostras; offline)\n");
    printf("HARDWARE VERDICT: NAO VALIDADO EM HARDWARE\n");
    return 0;
}
