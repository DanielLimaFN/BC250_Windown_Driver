#include "bc250_umd_state_translate.h"
#include "bc250_umd_vulkan_bridge.h"
#include <string.h>

BOOL Bc250UmdMapBlendFactor(UINT d3dBlend, _Out_ VkBlendFactor *out, _Out_ const char **why)
{
    *why = NULL;
    switch (d3dBlend) {
    case 1:  *out = VK_BLEND_FACTOR_ZERO; return TRUE;
    case 2:  *out = VK_BLEND_FACTOR_ONE; return TRUE;
    case 3:  *out = VK_BLEND_FACTOR_SRC_COLOR; return TRUE;
    case 4:  *out = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; return TRUE;
    case 5:  *out = VK_BLEND_FACTOR_SRC_ALPHA; return TRUE;
    case 6:  *out = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; return TRUE;
    case 7:  *out = VK_BLEND_FACTOR_DST_ALPHA; return TRUE;
    case 8:  *out = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; return TRUE;
    case 9:  *out = VK_BLEND_FACTOR_DST_COLOR; return TRUE;
    case 10: *out = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR; return TRUE;
    case 11: *out = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE; return TRUE;
    case 14: *out = VK_BLEND_FACTOR_CONSTANT_COLOR; return TRUE;          /* BLEND_FACTOR */
    case 15: *out = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR; return TRUE;
    case 16: case 17: case 18: case 19:
        /* Dual-source exige a feature dualSrcBlend, que o device nao habilita. */
        *out = VK_BLEND_FACTOR_ZERO; *why = "BLEND_DUAL_SOURCE_NAO_HABILITADO"; return FALSE;
    default:
        *out = VK_BLEND_FACTOR_ZERO; *why = "BLEND_FATOR_DESCONHECIDO"; return FALSE;
    }
}

BOOL Bc250UmdMapBlendOp(UINT d3dBlendOp, _Out_ VkBlendOp *out, _Out_ const char **why)
{
    *why = NULL;
    switch (d3dBlendOp) {
    case 1: *out = VK_BLEND_OP_ADD; return TRUE;
    case 2: *out = VK_BLEND_OP_SUBTRACT; return TRUE;
    case 3: *out = VK_BLEND_OP_REVERSE_SUBTRACT; return TRUE;
    case 4: *out = VK_BLEND_OP_MIN; return TRUE;
    case 5: *out = VK_BLEND_OP_MAX; return TRUE;
    default: *out = VK_BLEND_OP_ADD; *why = "BLEND_OP_DESCONHECIDA"; return FALSE;
    }
}

const char *Bc250UmdTranslateInputLayout(
    _In_reads_(n) const D3D10DDIARG_INPUT_ELEMENT_DESC *els,
    UINT n,
    _In_reads_(16) const UINT *strides,
    uint32_t vbPresentMask,
    _Out_writes_(16) VkVertexInputAttributeDescription *attrs,
    _Out_ uint32_t *nAttrs,
    _Out_writes_(16) VkVertexInputBindingDescription *binds,
    _Out_ uint32_t *nBinds,
    _Out_ UINT *failedElement)
{
    uint32_t usedSlots = 0, instanceSlots = 0, vertexSlots = 0, registers = 0;
    UINT e;

    *nAttrs = 0;
    *nBinds = 0;
    *failedElement = 0;
    memset(attrs, 0, 16 * sizeof(*attrs));
    memset(binds, 0, 16 * sizeof(*binds));
    if (n > 16u) {
        *failedElement = 16u;
        return "IA_MAIS_DE_16_ELEMENTOS";   /* antes: truncado em silencio */
    }

    for (e = 0; e < n; ++e) {
        const D3D10DDIARG_INPUT_ELEMENT_DESC *el = &els[e];
        VkFormat fmt = Bc250UmdVkFormatFromDxgiStrict((uint32_t)el->Format);
        *failedElement = e;
        if (el->InputSlot >= 16u) return "IA_SLOT_FORA_DE_FAIXA";
        if (el->InputRegister >= 32u) return "IA_REGISTRADOR_FORA_DE_FAIXA";
        if (registers & (1u << el->InputRegister)) return "IA_REGISTRADOR_DUPLICADO";
        /* antes: formato desconhecido virava B8G8R8A8 pelo default da tabela */
        if (fmt == VK_FORMAT_UNDEFINED) return "IA_FORMATO_DE_VERTICE_NAO_MAPEADO";
        if (el->AlignedByteOffset == 0xFFFFFFFFu) return "IA_OFFSET_APPEND_NAO_RESOLVIDO";
        if (el->InputSlotClass == D3D10_DDI_INPUT_PER_INSTANCE_DATA) {
            /* 0 ou >1 exigem VK_EXT_vertex_attribute_divisor, nao habilitada. */
            if (el->InstanceDataStepRate != 1u) return "IA_STEP_RATE_DIFERENTE_DE_1";
            instanceSlots |= (1u << el->InputSlot);
        } else {
            vertexSlots |= (1u << el->InputSlot);
        }
        registers |= (1u << el->InputRegister);
        usedSlots |= (1u << el->InputSlot);
        /*
         * A location e' o REGISTRADOR vN do VS, que o runtime ja casou por
         * semantica (InputRegister). O vkd3d-shader traduz vN em Location N.
         * Antes era a posicao do elemento na lista.
         */
        attrs[e].location = el->InputRegister;
        attrs[e].binding = el->InputSlot;
        attrs[e].format = fmt;
        attrs[e].offset = el->AlignedByteOffset;
    }
    *nAttrs = n;
    *failedElement = n;

    if (instanceSlots & vertexSlots) return "IA_SLOT_COM_DADOS_POR_VERTICE_E_POR_INSTANCIA";
    /* antes: slot sem buffer era vinculado ao buffer do slot 0 -- dado inventado */
    if (usedSlots & ~vbPresentMask) return "IA_SLOT_USADO_SEM_VERTEX_BUFFER";

    for (uint32_t s = 0; s < 16u; ++s) {
        if (!(usedSlots & (1u << s))) continue;
        /*
         * Stride 0 e' LEGITIMO (todos os vertices leem o mesmo dado) e vai como
         * esta; antes era trocado por um stride inventado. O RADV do fork trata
         * stride 0 no descritor e protege a divisao do compilador com
         * if (stride) (radv_pipeline_graphics.c:745, P1).
         */
        binds[*nBinds].binding = s;
        binds[*nBinds].stride = strides[s];
        binds[*nBinds].inputRate = (instanceSlots & (1u << s)) ?
            VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        (*nBinds)++;
    }
    return NULL;
}
