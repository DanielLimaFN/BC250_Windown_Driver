/*
 * Faixa DX.24 -- traducao de estado D3D11 -> Vulkan, offline.
 *
 * Casos tirados da Corrida 69: o layout de 8 elementos do DWM (slot 1 por
 * instancia, Class=1), blend com ZERO (que vale 0 no Vulkan e era confundido
 * com "nao preenchido"), formatos X8/sRGB com comentarios trocados. Cada recusa
 * tem nome proprio; nada e' aproximado. NAO VALIDADO EM HARDWARE.
 */
#include "../umd/bc250_umd_state_translate.c" /* unity, como a UMD: os headers D3D definem constantes nao-static */
#include "../umd/bc250_umd_vulkan_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static D3D10DDIARG_INPUT_ELEMENT_DESC El(UINT slot, UINT fmt, UINT off, UINT cls, UINT step, UINT reg)
{
    D3D10DDIARG_INPUT_ELEMENT_DESC e;
    memset(&e, 0, sizeof(e));
    e.InputSlot = slot;
    e.Format = (DXGI_FORMAT)fmt;
    e.AlignedByteOffset = off;
    e.InputSlotClass = (D3D10_DDI_INPUT_CLASSIFICATION)cls;
    e.InstanceDataStepRate = step;
    e.InputRegister = reg;
    return e;
}

int main(void)
{
    VkVertexInputAttributeDescription a[16];
    VkVertexInputBindingDescription bnd[16];
    uint32_t na, nb;
    UINT failed;
    UINT strides[32];
    const char *why;
    VkBlendFactor f;
    VkBlendOp op;
    memset(strides, 0, sizeof(strides));

    /* --- blend --- */
    CHECK(Bc250UmdMapBlendFactor(1, &f, &why) && f == VK_BLEND_FACTOR_ZERO && why == NULL);
    CHECK(Bc250UmdMapBlendFactor(2, &f, &why) && f == VK_BLEND_FACTOR_ONE);
    CHECK(Bc250UmdMapBlendFactor(6, &f, &why) && f == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    CHECK(Bc250UmdMapBlendFactor(11, &f, &why) && f == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE);
    CHECK(Bc250UmdMapBlendFactor(14, &f, &why) && f == VK_BLEND_FACTOR_CONSTANT_COLOR);
    CHECK(!Bc250UmdMapBlendFactor(16, &f, &why) && why && strcmp(why, "BLEND_DUAL_SOURCE_NAO_HABILITADO") == 0);
    CHECK(!Bc250UmdMapBlendFactor(12, &f, &why) && why && strcmp(why, "BLEND_FATOR_DESCONHECIDO") == 0);
    CHECK(Bc250UmdMapBlendOp(3, &op, &why) && op == VK_BLEND_OP_REVERSE_SUBTRACT);
    CHECK(!Bc250UmdMapBlendOp(9, &op, &why) && why != NULL);

    /* --- layout do DWM (Corrida 69, layout 0x6416448), registradores embaralhados --- */
    {
        D3D10DDIARG_INPUT_ELEMENT_DESC els[8];
        els[0] = El(0, 6, 0, 0, 0, 3);    /* R32G32B32_FLOAT */
        els[1] = El(0, 28, 12, 0, 0, 0);  /* R8G8B8A8_UNORM */
        els[2] = El(0, 16, 16, 0, 0, 1);
        els[3] = El(0, 16, 24, 0, 0, 2);
        els[4] = El(1, 2, 0, 1, 1, 4);    /* por instancia */
        els[5] = El(1, 2, 16, 1, 1, 5);
        els[6] = El(1, 28, 32, 1, 1, 6);
        els[7] = El(1, 16, 36, 1, 1, 7);
        strides[0] = 32; strides[1] = 0;  /* slot 1 com stride 0, como no log */
        why = Bc250UmdTranslateInputLayout(els, 8, strides, 0x3u, a, &na, bnd, &nb, &failed);
        CHECK(why == NULL && na == 8 && nb == 2);
        CHECK(a[0].location == 3 && a[1].location == 0);      /* InputRegister, nao a ordem */
        CHECK(a[1].format == VK_FORMAT_R8G8B8A8_UNORM && a[1].offset == 12);
        CHECK(bnd[0].binding == 0 && bnd[0].stride == 32 && bnd[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX);
        CHECK(bnd[1].binding == 1 && bnd[1].stride == 0 && bnd[1].inputRate == VK_VERTEX_INPUT_RATE_INSTANCE);

        /* slot usado sem buffer: recusa, nunca o slot 0 no lugar */
        why = Bc250UmdTranslateInputLayout(els, 8, strides, 0x1u, a, &na, bnd, &nb, &failed);
        CHECK(why && strcmp(why, "IA_SLOT_USADO_SEM_VERTEX_BUFFER") == 0);

        /* step rate 0 e 2: recusa (exigiria divisor) */
        els[4].InstanceDataStepRate = 2;
        why = Bc250UmdTranslateInputLayout(els, 8, strides, 0x3u, a, &na, bnd, &nb, &failed);
        CHECK(why && strcmp(why, "IA_STEP_RATE_DIFERENTE_DE_1") == 0 && failed == 4);
        els[4].InstanceDataStepRate = 1;

        /* mesmo slot por vertice e por instancia */
        els[5].InputSlotClass = D3D10_DDI_INPUT_PER_VERTEX_DATA;
        why = Bc250UmdTranslateInputLayout(els, 8, strides, 0x3u, a, &na, bnd, &nb, &failed);
        CHECK(why && strcmp(why, "IA_SLOT_COM_DADOS_POR_VERTICE_E_POR_INSTANCIA") == 0);
        els[5].InputSlotClass = D3D10_DDI_INPUT_PER_INSTANCE_DATA;

        /* registrador duplicado */
        els[2].InputRegister = 3;
        why = Bc250UmdTranslateInputLayout(els, 8, strides, 0x3u, a, &na, bnd, &nb, &failed);
        CHECK(why && strcmp(why, "IA_REGISTRADOR_DUPLICADO") == 0 && failed == 2);
        els[2].InputRegister = 1;

        /* formato de vertice desconhecido: recusa, nao B8G8R8A8 */
        els[3].Format = (DXGI_FORMAT)24; /* R10G10B10A2_UNORM, nao mapeado */
        why = Bc250UmdTranslateInputLayout(els, 8, strides, 0x3u, a, &na, bnd, &nb, &failed);
        CHECK(why && strcmp(why, "IA_FORMATO_DE_VERTICE_NAO_MAPEADO") == 0 && failed == 3);
    }
    {
        D3D10DDIARG_INPUT_ELEMENT_DESC many[17];
        for (UINT i = 0; i < 17; ++i) many[i] = El(0, 16, i * 8, 0, 0, i);
        strides[0] = 136;
        why = Bc250UmdTranslateInputLayout(many, 17, strides, 0x1u, a, &na, bnd, &nb, &failed);
        CHECK(why && strcmp(why, "IA_MAIS_DE_16_ELEMENTOS") == 0);
    }

    /* --- formatos --- */
    CHECK(Bc250UmdVkFormatFromDxgiStrict(87) == VK_FORMAT_B8G8R8A8_UNORM);
    CHECK(Bc250UmdVkFormatFromDxgiStrict(88) == VK_FORMAT_B8G8R8A8_UNORM);
    CHECK(Bc250UmdVkFormatFromDxgiStrict(91) == VK_FORMAT_B8G8R8A8_SRGB);
    CHECK(Bc250UmdVkFormatFromDxgiStrict(93) == VK_FORMAT_B8G8R8A8_SRGB);
    CHECK(Bc250UmdVkFormatFromDxgiStrict(29) == VK_FORMAT_R8G8B8A8_SRGB);
    CHECK(Bc250UmdVkFormatFromDxgiStrict(24) == VK_FORMAT_UNDEFINED);
    CHECK(Bc250UmdVkFormatFromDxgi(24) == VK_FORMAT_B8G8R8A8_UNORM); /* recurso mantem fallback historico */
    {
        VkComponentMapping m = Bc250UmdVkSrvComponentMapping(88);
        CHECK(m.a == VK_COMPONENT_SWIZZLE_ONE && m.r == VK_COMPONENT_SWIZZLE_IDENTITY);
        m = Bc250UmdVkSrvComponentMapping(65);
        CHECK(m.r == VK_COMPONENT_SWIZZLE_ZERO && m.a == VK_COMPONENT_SWIZZLE_R);
        m = Bc250UmdVkSrvComponentMapping(87);
        CHECK(m.a == VK_COMPONENT_SWIZZLE_IDENTITY);
    }

    printf("UMD STATE TRANSLATE: PASS (blend mapeado com recusas nomeadas; layout do DWM por InputRegister, "
           "slot por instancia, stride 0 preservado, slot sem buffer recusado; X8/sRGB/A8 corrigidos; offline)\n");
    printf("HARDWARE VERDICT: NAO VALIDADO EM HARDWARE\n");
    return 0;
}
