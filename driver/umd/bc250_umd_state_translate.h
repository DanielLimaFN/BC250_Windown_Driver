#ifndef BC250_UMD_STATE_TRANSLATE_H
#define BC250_UMD_STATE_TRANSLATE_H

/*
 * DX.24 -- traducao PURA de estado D3D11 para Vulkan (sem device, testavel).
 *
 * Corrida 69 (desfecho B): origem com conteudo, draws no alvo apresentado e
 * superficie preta. A leitura achou estado pedido pelo DWM que nao chegava ao
 * Vulkan: fatores de blend nunca mapeados, dados por instancia lidos por
 * vertice, stride 0 trocado por stride inventado, location pela ordem do
 * elemento em vez do InputRegister, e slot sem buffer preenchido com o slot 0.
 * Regra: traduz exatamente ou devolve o MOTIVO da recusa -- nunca aproxima.
 */

#include "bc250_d3d_umd.h"

BOOL Bc250UmdMapBlendFactor(UINT d3dBlend, _Out_ VkBlendFactor *out, _Out_ const char **why);
BOOL Bc250UmdMapBlendOp(UINT d3dBlendOp, _Out_ VkBlendOp *out, _Out_ const char **why);

/*
 * Devolve NULL em sucesso; senao, o motivo nomeado da recusa e, em
 * *failedElement, o indice do elemento (ou n quando a falha e' de slot).
 * strides e vbPresentMask descrevem os 16 slots de IA vinculados.
 */
const char *Bc250UmdTranslateInputLayout(
    _In_reads_(n) const D3D10DDIARG_INPUT_ELEMENT_DESC *els,
    UINT n,
    _In_reads_(16) const UINT *strides,
    uint32_t vbPresentMask,
    _Out_writes_(16) VkVertexInputAttributeDescription *attrs,
    _Out_ uint32_t *nAttrs,
    _Out_writes_(16) VkVertexInputBindingDescription *binds,
    _Out_ uint32_t *nBinds,
    _Out_ UINT *failedElement);

#endif
