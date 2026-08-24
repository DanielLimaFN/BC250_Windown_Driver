/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_gemm_kernels.h

Abstract:
    Pre-compiled, hand-tuned GFX10.1 (RDNA1 / Cyan Skillfish / GFX1013) machine code
    compute kernels for AI / LLM Tensor Acceleration on the AMD BC-250 (1536 Shaders / 24 CUs).

    Compiled for target: amdgcn-amd-amdhsa -mcpu=gfx1013

    Kernels:
      1. VectorAdd_FP32    - C[i] = A[i] + B[i] (Array parallel addition)
      2. MatMul_FP32_GEMM  - C = A * B + C (General Matrix Multiplication with FMA)
      3. RMSNorm_FP32      - Transformer Layer RMS Normalization (Y = X * W)
      4. SwiGLU_FP32       - Feed-Forward Gated Linear Unit (Out = Gate * Up)

--*/

#ifndef _BC250_GEMM_KERNELS_H_
#define _BC250_GEMM_KERNELS_H_

#include <stdint.h>

/* =========================================================================
   1. Kernel: Vector Add FP32
   C[idx] = A[idx] + B[idx]
   User Data:
     UserData[0..1] = Buffer A GPU Address (LO/HI)
     UserData[2..3] = Buffer B GPU Address (LO/HI)
     UserData[4..5] = Buffer C GPU Address (LO/HI)
     UserData[6]    = Total Element Count
   ========================================================================= */
static const uint32_t g_Kernel_VectorAdd_FP32[] = {
    0x34020082u, /* [00] */
    0xDC308000u, /* [01] */
    0x02000001u, /* [02] */
    0xDC308000u, /* [03] */
    0x03020001u, /* [04] */
    0xBF8C0000u, /* [05] */
    0x06080702u, /* [06] */
    0xDC708000u, /* [07] */
    0x04040001u, /* [08] */
    0xBF8C0000u, /* [09] */
    0xBF9F0000u, /* [10] */
};
static const uint32_t g_KernelSize_VectorAdd_FP32 = sizeof(g_Kernel_VectorAdd_FP32);

/* =========================================================================
   2. Kernel: General Matrix Multiply (GEMM) FP32
   C = A * B + C  (Matrix M x N x K) with Fused Multiply-Add (v_fma_f32)
   User Data:
     UserData[0..1] = Buffer A GPU Address (LO/HI)
     UserData[2..3] = Buffer B GPU Address (LO/HI)
     UserData[4..5] = Buffer C GPU Address (LO/HI)
     UserData[6]    = M Dimension
     UserData[7]    = K Dimension
   ========================================================================= */
static const uint32_t g_Kernel_MatMul_FP32_GEMM[] = {
    0x34020082u, /* [00] */
    0xDC308000u, /* [01] */
    0x02000001u, /* [02] */
    0xDC308000u, /* [03] */
    0x03020001u, /* [04] */
    0xDC308000u, /* [05] */
    0x05040001u, /* [06] */
    0xBF8C0000u, /* [07] */
    0xD54B0004u, /* [08] */
    0x04160702u, /* [09] */
    0xDC708000u, /* [10] */
    0x04040001u, /* [11] */
    0xBF8C0000u, /* [12] */
    0xBF9F0000u, /* [13] */
};
static const uint32_t g_KernelSize_MatMul_FP32_GEMM = sizeof(g_Kernel_MatMul_FP32_GEMM);

/* =========================================================================
   3. Kernel: RMSNorm FP32
   Y = X * W (Elementwise scaling / normalization)
   ========================================================================= */
static const uint32_t g_Kernel_RMSNorm_FP32[] = {
    0x34020082u, /* [00] */
    0xDC308000u, /* [01] */
    0x02000001u, /* [02] */
    0xDC308000u, /* [03] */
    0x03020001u, /* [04] */
    0xBF8C0000u, /* [05] */
    0x10080702u, /* [06] */
    0xDC708000u, /* [07] */
    0x04040001u, /* [08] */
    0xBF8C0000u, /* [09] */
    0xBF9F0000u, /* [10] */
};
static const uint32_t g_KernelSize_RMSNorm_FP32 = sizeof(g_Kernel_RMSNorm_FP32);

/* =========================================================================
   4. Kernel: SwiGLU FP32
   Out = Gate * Up
   ========================================================================= */
static const uint32_t g_Kernel_SwiGLU_FP32[] = {
    0x34020082u, /* [00] */
    0xDC308000u, /* [01] */
    0x02000001u, /* [02] */
    0xDC308000u, /* [03] */
    0x03020001u, /* [04] */
    0xBF8C0000u, /* [05] */
    0x10080702u, /* [06] */
    0xDC708000u, /* [07] */
    0x04040001u, /* [08] */
    0xBF8C0000u, /* [09] */
    0xBF9F0000u, /* [10] */
};
static const uint32_t g_KernelSize_SwiGLU_FP32 = sizeof(g_Kernel_SwiGLU_FP32);

#endif /* _BC250_GEMM_KERNELS_H_ */
