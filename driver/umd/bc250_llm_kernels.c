/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_llm_kernels.c

Abstract:
    Operadores de computacao para LLM/Transformers na APU AMD BC-250.

    ESTADO ATUAL: FAIL-CLOSED. Nenhum destes operadores possui shader GFX10
    real, portanto todos recusam a chamada com BC250_ERROR_NOT_SUPPORTED.

    Historico do porque disto (Lei 3 do CLAUDE.md - Zero Fakes):

    Ate 2026-08-31 este arquivo declarava seis "shaders canonicos GFX10", e
    todos os seis eram literalmente quatro dwords:

        0xBF810000  (s_endpgm)  seguido de tres 0xBF800000  (s_nop)

    ou seja, sair do programa e tres no-ops. Nenhum lia entrada, nenhum
    escrevia saida - os 22 parametros de ponteiro eram todos
    UNREFERENCED_PARAMETER. Cada funcao despachava esse shader vazio e
    devolvia BC250_SUCCESS, e tools/bc250-llm-benchmark.c estampava
    "PROVADO NO SILICIO" no resultado.

    Isso e' um gerador de falso-positivo: uma aplicacao que chamasse
    bc250_llm_rmsnorm receberia sucesso, leria o buffer de destino intocado e
    trataria lixo como resultado - exatamente o mecanismo que produziu tokens
    corrompidos no caminho Vulkan/RADV, so que dentro do nosso proprio codigo.

    Recusar e' a unica resposta honesta enquanto o shader nao existir. Cada
    operador volta a ficar disponivel individualmente quando (a) tiver ISA
    GFX10 real, (b) vincular os buffers de entrada e saida no despacho, e
    (c) tiver o tripe de validacao fisica: RPTR == WPTR, fence gravada pela
    GPU em VRAM e conferencia byte a byte do destino contra gabarito de CPU.

--*/

#define BC250_RUNTIME_EXPORTS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "bc250_llm_kernels.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Validacao de argumentos mantida deliberadamente.
 *
 * Um chamador precisa distinguir "voce me passou lixo" de "este operador nao
 * existe ainda". Por isso BC250_ERROR_INVALID_ARGUMENT continua tendo
 * precedencia sobre BC250_ERROR_NOT_SUPPORTED: assim o contrato da API pode
 * ser exercitado por testes offline antes de existir qualquer shader.
 */

BC250_API bc250_status_t bc250_llm_rmsnorm(
    bc250_device_t *device,
    uint64_t dst_gpu_va,
    uint64_t src_gpu_va,
    uint64_t weight_gpu_va,
    uint32_t size,
    float eps)
{
    UNREFERENCED_PARAMETER(eps);

    if (device == NULL || dst_gpu_va == 0 || src_gpu_va == 0 ||
        weight_gpu_va == 0 || size == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_llm_rope(
    bc250_device_t *device,
    uint64_t q_gpu_va,
    uint64_t k_gpu_va,
    uint32_t n_dims,
    uint32_t n_heads,
    uint32_t pos,
    float freq_base,
    float freq_scale)
{
    UNREFERENCED_PARAMETER(pos);
    UNREFERENCED_PARAMETER(freq_base);
    UNREFERENCED_PARAMETER(freq_scale);

    if (device == NULL || q_gpu_va == 0 || k_gpu_va == 0 ||
        n_dims == 0 || n_heads == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_llm_softmax(
    bc250_device_t *device,
    uint64_t dst_gpu_va,
    uint64_t src_gpu_va,
    uint32_t n_elements)
{
    if (device == NULL || dst_gpu_va == 0 || src_gpu_va == 0 ||
        n_elements == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_llm_silu(
    bc250_device_t *device,
    uint64_t dst_gpu_va,
    uint64_t src_gpu_va,
    uint32_t n_elements)
{
    if (device == NULL || dst_gpu_va == 0 || src_gpu_va == 0 ||
        n_elements == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_llm_gemv_q4_0(
    bc250_device_t *device,
    uint64_t dst_vec_gpu_va,
    uint64_t weight_q4_gpu_va,
    uint64_t src_vec_gpu_va,
    uint32_t M,
    uint32_t K)
{
    if (device == NULL || dst_vec_gpu_va == 0 || weight_q4_gpu_va == 0 ||
        src_vec_gpu_va == 0 || M == 0 || K == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    return BC250_ERROR_NOT_SUPPORTED;
}

BC250_API bc250_status_t bc250_llm_gemm_f32(
    bc250_device_t *device,
    uint64_t c_gpu_va,
    uint64_t a_gpu_va,
    uint64_t b_gpu_va,
    uint32_t M,
    uint32_t N,
    uint32_t K)
{
    if (device == NULL || c_gpu_va == 0 || a_gpu_va == 0 || b_gpu_va == 0 ||
        M == 0 || N == 0 || K == 0) {
        return BC250_ERROR_INVALID_ARGUMENT;
    }
    return BC250_ERROR_NOT_SUPPORTED;
}
