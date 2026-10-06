#ifndef BC250_SDMA1_BRINGUP_H
#define BC250_SDMA1_BRINGUP_H

/*
 * P4.1 (doc 620): SDMA1 vivo e ocioso, como o amdgpu deixa.
 *
 * O amdgpu inicializa as duas instancias do SDMA (sdma_v5_0_gfx_resume, laco
 * em adev->sdma.num_instances) e as duas ficam com anel ligado e provado; o
 * SDMA1 recebe trabalho do escalonador (atualizacoes de VM e copias). Nos o
 * deixavamos com o F32 parado e o anel desligado (P0, Corrida 199). Esta etapa
 * so' liga e prova o SDMA1; nao o usa.
 *
 * Proveniencia (P1):
 *   - offsets: gc_10_1_0_offset.h, mmSDMA1_* BASE_IDX 0, GC_BASE seg0 0x1260
 *     (cyan_skillfish_ip_offset.h) -> byte = (0x1260 + dw) * 4; e' o mesmo
 *     que SDMA0 + 0x600 dw (sdma_v5_0.c, SDMA1_REG_OFFSET);
 *   - sequencia: sdma_v5_0_gfx_resume (instancia 1), na forma ja provada no
 *     silicio para o SDMA0 (Bc250SdmaPresentRestartAfterTdr): mesmos campos de
 *     RB_CNTL, WPTR_POLL_CNTL 0x00403000, golden do cyan skillfish
 *     (CHICKEN_BITS, GB_ADDR_CONFIG, GB_ADDR_CONFIG_READ), UTCL1;
 *   - relogio: o Linux nao mexe em SDMA1_CLK_CTRL na Cyan Skillfish (cg_flags=0,
 *     doc 557). Forcamos os overrides so' durante a configuracao, como no
 *     SDMA0, e devolvemos o valor do POST depois da prova.
 */

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stdint.h>
#endif

#define BC250_SDMA1_IDENTITY                 0x53444D31u /* SDM1 */

#define BC250_SDMA1_REG_CLK_CTRL             0x000061ECu
#define BC250_SDMA1_REG_CNTL                 0x000061F0u
#define BC250_SDMA1_REG_CHICKEN_BITS         0x000061F4u
#define BC250_SDMA1_REG_GB_ADDR_CONFIG       0x000061F8u
#define BC250_SDMA1_REG_GB_ADDR_CONFIG_READ  0x000061FCu
#define BC250_SDMA1_REG_SEM_WAIT_FAIL_TIMER  0x00006204u
#define BC250_SDMA1_REG_STATUS               0x00006214u
#define BC250_SDMA1_REG_STATUS1              0x00006218u
#define BC250_SDMA1_REG_UCODE_CHECKSUM       0x00006224u
#define BC250_SDMA1_REG_F32_CNTL             0x00006228u
#define BC250_SDMA1_REG_UTCL1_CNTL           0x00006270u
#define BC250_SDMA1_REG_UTCL1_PAGE           0x000062A0u
#define BC250_SDMA1_REG_RB_CNTL              0x00006380u
#define BC250_SDMA1_REG_RB_BASE              0x00006384u
#define BC250_SDMA1_REG_RB_BASE_HI           0x00006388u
#define BC250_SDMA1_REG_RB_RPTR              0x0000638Cu
#define BC250_SDMA1_REG_RB_RPTR_HI           0x00006390u
#define BC250_SDMA1_REG_RB_WPTR              0x00006394u
#define BC250_SDMA1_REG_RB_WPTR_HI           0x00006398u
#define BC250_SDMA1_REG_WPTR_POLL_CNTL       0x0000639Cu
#define BC250_SDMA1_REG_RPTR_ADDR_HI         0x000063A0u
#define BC250_SDMA1_REG_RPTR_ADDR_LO         0x000063A4u
#define BC250_SDMA1_REG_IB_CNTL              0x000063A8u
#define BC250_SDMA1_REG_DOORBELL             0x000063C8u
#define BC250_SDMA1_REG_WPTR_POLL_ADDR_HI    0x00006448u
#define BC250_SDMA1_REG_WPTR_POLL_ADDR_LO    0x0000644Cu
#define BC250_SDMA1_REG_MINOR_PTR_UPDATE     0x00006454u

/* Anel de 4 KB (RB_SIZE = 10 -> 1024 dwords) e uma pagina de trabalho. */
#define BC250_SDMA1_RING_BYTES               4096u
#define BC250_SDMA1_RB_SIZE_LOG2_DWORDS      10u
#define BC250_SDMA1_WORK_WPTR_OFFSET         0x00u
#define BC250_SDMA1_WORK_RPTR_OFFSET         0x08u
#define BC250_SDMA1_WORK_FENCE_OFFSET        0x40u
/* P4.2 (doc 622): troca de sinais entre SDMA0 e SDMA1 (Go escrito pelo SDMA0,
 * Done pelo SDMA1). */
#define BC250_SDMA1_WORK_GO_OFFSET           0x80u
#define BC250_SDMA1_WORK_DONE_OFFSET         0x88u
#define BC250_SDMA1_FENCE_POISON             0x5D1A0BADu

/* Campos (gc_10_1_0_sh_mask.h, SDMA0_* = SDMA1_*). */
#define BC250_SDMA1_RB_ENABLE                0x00000001u
#define BC250_SDMA1_RB_SIZE_MASK             0x0000003Eu
#define BC250_SDMA1_RPTR_WRITEBACK_ENABLE    0x00001000u
#define BC250_SDMA1_IB_ENABLE                0x00000001u
#define BC250_SDMA1_F32_HALT                 0x00000001u
#define BC250_SDMA1_CNTL_UTC_L1_ENABLE       0x00000002u
#define BC250_SDMA1_DOORBELL_ENABLE          0x10000000u
#define BC250_SDMA1_POLL_CNTL_GOLDEN         0x00403000u
#define BC250_SDMA1_POLL_ENABLE              0x00000004u
#define BC250_SDMA1_CLK_OVERRIDES            0xFF000000u

typedef uint32_t (*BC250_SDMA1_READ32)(void *Context, uint32_t ByteOffset, uint32_t *Value);
typedef uint32_t (*BC250_SDMA1_WRITE32)(void *Context, uint32_t ByteOffset, uint32_t Value);
typedef void (*BC250_SDMA1_STALL)(void *Context, uint32_t Microseconds);
typedef void (*BC250_SDMA1_BARRIER)(void *Context);

typedef struct BC250_SDMA1_INTERFACE {
    BC250_SDMA1_READ32 Read32;
    BC250_SDMA1_WRITE32 Write32;
    BC250_SDMA1_STALL Stall;
    BC250_SDMA1_BARRIER Barrier;
    void *Context;
    uint32_t Identity;
} BC250_SDMA1_INTERFACE;

typedef struct BC250_SDMA1_CONFIG {
    volatile uint32_t *RingCpu;      /* BC250_SDMA1_RING_BYTES */
    uint64_t RingGpu;                /* VA da VMID 0 (GART), alinhado a 4 KB */
    volatile uint32_t *WorkCpu;      /* uma pagina */
    uint64_t WorkGpu;
    uint32_t FenceValue;             /* != 0 e != poison */
    uint32_t MaxPolls;               /* passos de 10 us */
} BC250_SDMA1_CONFIG;

typedef enum BC250_SDMA1_STATUS {
    BC250_SDMA1_OK = 0,
    BC250_SDMA1_BAD_ARGUMENT,
    BC250_SDMA1_READ_FAILED,
    BC250_SDMA1_WRITE_FAILED,
    BC250_SDMA1_NO_FIRMWARE,          /* UCODE_CHECKSUM 0 ou 0xFFFFFFFF */
    BC250_SDMA1_INHERITED_ACTIVE,     /* anel ja ligado antes de nos */
    BC250_SDMA1_READBACK_FAILED,
    BC250_SDMA1_EMIT_FAILED,
    BC250_SDMA1_PROOF_TIMEOUT,
    BC250_SDMA1_SHUTDOWN_UNSAFE
} BC250_SDMA1_STATUS;

typedef struct BC250_SDMA1_EVIDENCE {
    uint32_t UcodeChecksum;
    uint32_t ClkBefore;
    uint32_t CntlBefore;
    uint32_t F32Before;
    uint32_t RbCntlBefore;
    uint32_t IbCntlBefore;
    uint32_t StatusBefore;
    uint32_t RbCntlApplied;
    uint32_t F32Applied;
    uint32_t CntlApplied;
    uint32_t ReadbackPassed;
    uint32_t WptrBytes;
    uint32_t RptrObserved;
    uint32_t RptrWriteback;
    uint32_t FenceObserved;
    uint32_t FenceExpected;
    uint32_t ProofPolls;
    uint32_t ProofPassed;
    uint32_t ClkRestored;
    uint32_t ShutdownRbCntl;
    uint32_t ShutdownPollCntl;
    uint32_t ShutdownF32;
    uint32_t SafeToFree;
    uint32_t PhysicalReady;
} BC250_SDMA1_EVIDENCE;

/* 1 se o registrador e' um dos que este contrato le ou escreve. */
uint32_t Bc250Sdma1IsRegister(uint32_t ByteOffset);

/*
 * PREFLIGHT -> SNAPSHOT -> APPLY -> READBACK -> PROVA (FENCE sobre poison e
 * RPTR == WPTR) -> relogio de volta ao POST. Falha depois do APPLY desliga a
 * engine (Bc250Sdma1Shutdown) antes de voltar; Evidence->SafeToFree diz se a
 * memoria do anel pode ser devolvida.
 */
BC250_SDMA1_STATUS Bc250Sdma1BringUp(
    const BC250_SDMA1_CONFIG *Config,
    const BC250_SDMA1_INTERFACE *Interface,
    BC250_SDMA1_EVIDENCE *Evidence);

/*
 * Desliga como sdma_v5_0_gfx_stop + sdma_v5_0_enable(false): anel, IB e poll
 * desligados, writeback desligado, F32 parado, com readback. Devolve OK so'
 * se a engine provadamente nao alcanca mais a memoria (SafeToFree = 1).
 */
BC250_SDMA1_STATUS Bc250Sdma1Shutdown(
    const BC250_SDMA1_INTERFACE *Interface,
    BC250_SDMA1_EVIDENCE *Evidence);

const char *Bc250Sdma1StatusName(BC250_SDMA1_STATUS Status);

#endif
