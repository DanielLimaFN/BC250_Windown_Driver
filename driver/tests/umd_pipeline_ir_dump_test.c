/*
 * Faixa DX.21 -- Bc250UmdVkDumpPipelineIr, offline.
 *
 * Invariantes:
 *   1. captura DESARMADA -> zero chamadas as funcoes da extensao;
 *   2. "Assembly" e' preferida a "ACO IR" e a "NIR Shader(s)";
 *   3. sem "Assembly", cai para "ACO IR"; sem nenhuma das duas, nao le texto;
 *   4. texto maior que o teto e' pedido COM dataSize == teto (truncamento
 *      declarado pela propria DDI, nunca estouro de buffer);
 *   5. teto de pipelines por processo: o (N+1)-esimo nao consulta nada.
 * NAO VALIDADO EM HARDWARE: o RADV real so' responde com o adaptador BC-250.
 */
#include "../umd/bc250_umd_vulkan_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static unsigned g_PropsCalls, g_RepCalls, g_TextReads;
static int g_Mode; /* 0 = Assembly+ACO+NIR, 1 = so' ACO IR + NIR, 2 = so' NIR, 3 = Assembly gigante */
static size_t g_LastTextRequest;
static const char kAsm[] = "s_load_dwordx8 s[8:15], s[2:3], 0x0\r\ns_buffer_load_dword s0, s[16:19], 0x0\nimage_sample v[0:3], v0, s[8:15], s[20:23]\n";
static const char kAco[] = "ACO_IR_LINE\n";
static char g_Big[200u * 1024u];

static VkResult VKAPI_CALL FakeProps(VkDevice d, const VkPipelineInfoKHR *info, uint32_t *count, VkPipelineExecutablePropertiesKHR *props)
{
    (void)d; CHECK(info->pipeline != VK_NULL_HANDLE); CHECK(*count >= 2u);
    g_PropsCalls++;
    *count = 2u;
    props[0].stages = VK_SHADER_STAGE_VERTEX_BIT; strcpy_s(props[0].name, sizeof(props[0].name), "VS");
    props[1].stages = VK_SHADER_STAGE_FRAGMENT_BIT; strcpy_s(props[1].name, sizeof(props[1].name), "FS");
    return VK_SUCCESS;
}

static void Fill(VkPipelineExecutableInternalRepresentationKHR *r, const char *name, const char *text)
{
    size_t need = strlen(text) + 1u;
    strcpy_s(r->name, sizeof(r->name), name);
    r->isText = VK_TRUE;
    if (r->pData == NULL) { r->dataSize = need; return; }
    g_TextReads++;
    g_LastTextRequest = r->dataSize;
    memcpy(r->pData, text, r->dataSize < need ? r->dataSize : need);
}

static VkResult VKAPI_CALL FakeReps(VkDevice d, const VkPipelineExecutableInfoKHR *info, uint32_t *count, VkPipelineExecutableInternalRepresentationKHR *reps)
{
    (void)d; CHECK(info->executableIndex < 2u); CHECK(*count >= 3u);
    g_RepCalls++;
    switch (g_Mode) {
    case 0: Fill(&reps[0], "NIR Shader(s)", "nir"); Fill(&reps[1], "ACO IR", kAco); Fill(&reps[2], "Assembly", kAsm); *count = 3u; break;
    case 1: Fill(&reps[0], "NIR Shader(s)", "nir"); Fill(&reps[1], "ACO IR", kAco); *count = 2u; break;
    case 2: Fill(&reps[0], "NIR Shader(s)", "nir"); *count = 1u; break;
    default: Fill(&reps[0], "Assembly", g_Big); *count = 1u; break;
    }
    return VK_SUCCESS;
}

static void Arm(BC250_UMD_VK_BRIDGE *b)
{
    memset(b, 0, sizeof(*b));
    b->Device = (VkDevice)(uintptr_t)0x10;
    b->PipelineIrCaptureEnabled = TRUE;
    b->pfnGetPipelineExecutableProperties = FakeProps;
    b->pfnGetPipelineExecutableInternalRepresentations = FakeReps;
}

int main(void)
{
    BC250_UMD_VK_BRIDGE b;
    VkPipeline p = (VkPipeline)(uintptr_t)0x77;

    /* 1. desarmada */
    Arm(&b); b.PipelineIrCaptureEnabled = FALSE;
    Bc250UmdVkDumpPipelineIr(&b, p, NULL, NULL);
    CHECK(g_PropsCalls == 0u && g_RepCalls == 0u && b.PipelineIrDumpCount == 0u);

    /* 2. Assembly preferida: 2 executaveis x (nomes + texto) = 4 chamadas, 2 leituras */
    Arm(&b); g_Mode = 0;
    Bc250UmdVkDumpPipelineIr(&b, p, NULL, NULL);
    CHECK(g_PropsCalls == 1u && g_RepCalls == 4u && g_TextReads == 2u);
    CHECK(g_LastTextRequest == sizeof(kAsm));
    CHECK(b.PipelineIrDumpCount == 1u);

    /* 3a. sem Assembly -> ACO IR */
    g_RepCalls = g_TextReads = 0u; g_Mode = 1;
    Bc250UmdVkDumpPipelineIr(&b, p, NULL, NULL);
    CHECK(g_TextReads == 2u && g_LastTextRequest == sizeof(kAco));
    /* 3b. so' NIR -> nenhuma leitura de texto */
    g_RepCalls = g_TextReads = 0u; g_Mode = 2;
    Bc250UmdVkDumpPipelineIr(&b, p, NULL, NULL);
    CHECK(g_RepCalls == 2u && g_TextReads == 0u);

    /* 4. truncamento no teto de 96 KiB */
    memset(g_Big, 'x', sizeof(g_Big) - 1u); g_Big[sizeof(g_Big) - 1u] = '\0';
    g_TextReads = 0u; g_Mode = 3;
    Bc250UmdVkDumpPipelineIr(&b, p, NULL, NULL);
    CHECK(g_TextReads == 2u && g_LastTextRequest == 96u * 1024u);

    /* 5. teto de 32 pipelines por processo */
    Arm(&b); g_Mode = 0; g_PropsCalls = 0u;
    for (unsigned i = 0; i < 40u; ++i) Bc250UmdVkDumpPipelineIr(&b, p, NULL, NULL);
    CHECK(g_PropsCalls == 32u);

    printf("UMD PIPELINE IR DUMP: PASS (desarmada=0 chamadas, Assembly>ACO IR, truncamento no teto, teto 32/processo; offline)\n");
    printf("HARDWARE VERDICT: NAO VALIDADO EM HARDWARE\n");
    return 0;
}
