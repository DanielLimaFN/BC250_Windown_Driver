/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_llm_kernels.h

Abstract:
    Suíte de Kernels de Computação Acelerados em GPU (GFX10) para Modelos de
    Linguagem de Grande Escala (LLM / Transformers) na APU AMD BC-250.
    Cobre operações de atenção, normalização, embeddings rotacionais e GEMV/GEMM.

--*/

#ifndef BC250_LLM_KERNELS_H
#define BC250_LLM_KERNELS_H

#include "bc250_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Estruturas de Quantização GGML (Q4_0 e Q8_0)
 * ============================================================================
 */

#define BC250_QK4_0 32
#define BC250_QK8_0 32

/* Bloco de 32 pesos quantizados em 4 bits (16 bytes de nibbles + 2 bytes FP16 delta) */
typedef struct {
    uint16_t d;                     /* Escala em FP16 (meia precisão) */
    uint8_t  qs[BC250_QK4_0 / 2];   /* 16 bytes = 32 nibbles de 4 bits */
} bc250_block_q4_0_t;

/* Bloco de 32 pesos quantizados em 8 bits (32 bytes INT8 + 2 bytes FP16 delta) */
typedef struct {
    uint16_t d;                     /* Escala em FP16 (meia precisão) */
    int8_t   qs[BC250_QK8_0];       /* 32 bytes de inteiros signed de 8 bits */
} bc250_block_q8_0_t;

/*
 * ============================================================================
 * Operadores de Normalização e Ativação de Transformers
 * ============================================================================
 */

/* Root Mean Square Normalization: Out[i] = (In[i] / sqrt(mean(In^2) + eps)) * Weight[i] */
BC250_API bc250_status_t bc250_llm_rmsnorm(
    bc250_device_t *device,
    uint64_t dst_gpu_va,
    uint64_t src_gpu_va,
    uint64_t weight_gpu_va,
    uint32_t size,
    float eps);

/* Rotary Position Embedding (RoPE) nos tensores Q e K */
BC250_API bc250_status_t bc250_llm_rope(
    bc250_device_t *device,
    uint64_t q_gpu_va,
    uint64_t k_gpu_va,
    uint32_t n_dims,
    uint32_t n_heads,
    uint32_t pos,
    float freq_base,
    float freq_scale);

/* Softmax: Out[i] = exp(In[i] - max(In)) / sum(exp(In - max(In))) */
BC250_API bc250_status_t bc250_llm_softmax(
    bc250_device_t *device,
    uint64_t dst_gpu_va,
    uint64_t src_gpu_va,
    uint32_t n_elements);

/* Sigmoid Linear Unit (SiLU / Swish): Out[i] = In[i] / (1.0 + exp(-In[i])) */
BC250_API bc250_status_t bc250_llm_silu(
    bc250_device_t *device,
    uint64_t dst_gpu_va,
    uint64_t src_gpu_va,
    uint32_t n_elements);

/*
 * ============================================================================
 * Multiplicação de Matriz por Vetor / Matriz (GEMV / GEMM)
 * ============================================================================
 */

/* GEMV Quantizado Q4_0: Out[M] = Weight_Q4_0[M x K] * In_Vec[K] */
BC250_API bc250_status_t bc250_llm_gemv_q4_0(
    bc250_device_t *device,
    uint64_t dst_vec_gpu_va,
    uint64_t weight_q4_gpu_va,
    uint64_t src_vec_gpu_va,
    uint32_t M,
    uint32_t K);

/* GEMM Ponto Flutuante FP32: C[M x N] = A[M x K] * B[K x N] */
BC250_API bc250_status_t bc250_llm_gemm_f32(
    bc250_device_t *device,
    uint64_t c_gpu_va,
    uint64_t a_gpu_va,
    uint64_t b_gpu_va,
    uint32_t M,
    uint32_t N,
    uint32_t K);

#ifdef __cplusplus
}
#endif

#endif /* BC250_LLM_KERNELS_H */
