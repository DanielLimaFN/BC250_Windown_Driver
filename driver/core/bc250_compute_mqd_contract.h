#ifndef BC250_COMPUTE_MQD_CONTRACT_H
#define BC250_COMPUTE_MQD_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_compute_dispatch_contract.h"
#include "bc250_observable_shader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_MQD_HEADER_MAGIC            0xC0310800u
#define BC250_MQD_SIZE_BYTES              512u
#define BC250_MQD_SIZE_DWORDS             (BC250_MQD_SIZE_BYTES / 4u)

#define BC250_MQD_ALIGNMENT               256u
#define BC250_HQD_RING_ALIGNMENT          256u
#define BC250_HQD_EOP_ALIGNMENT           256u

#define BC250_HQD_MIN_QUEUE_DWORDS        64u
#define BC250_HQD_MAX_QUEUE_DWORDS        (1024u * 1024u)
#define BC250_HQD_DEFAULT_EOP_BYTES       4096u
#define BC250_HQD_MQD_GPUVA               0x0000000000000000ull
#define BC250_HQD_RPTR_GPUVA              0x0000000000000400ull
#define BC250_HQD_WPTR_GPUVA              0x0000000000000500ull
#define BC250_HQD_EOP_GPUVA               0x0000000000001000ull
#define BC250_HQD_FENCE_GPUVA             0x0000000000002200ull
#define BC250_HQD_SHADER_GPUVA            0x0000000000004000ull
#define BC250_HQD_RESULT_GPUVA            0x0000000000005000ull
#define BC250_HQD_RING_GPUVA              0x0000000000010000ull

/* GRBM indexing and selection (BAR5 byte offsets). */
#define BC250_GRBM_GFX_INDEX_BYTE_OFFSET  0x00030800u
#define BC250_GRBM_GFX_INDEX_BROADCAST    0xE0000000u
#define BC250_GRBM_GFX_CNTL_BYTE_OFFSET   0x00008088u
#define BC250_HQD_SELECTOR_MASK           0x000007FFu
#define BC250_HQD_SELECTOR_MEC1_P0_Q0_V0  0x00000004u
#define BC250_HQD_SELECTOR_MEC2_P1_Q0_V0  0x00000009u
#define BC250_HQD_QUEUE_PRIORITY_MASK      0x0000000Fu

/* GC10.1 / BASE_IDX 0 / BAR5 byte offsets, derived by SOC15. */
#define BC250_HQD_MMIO_MQD_BASE_LO        0x0000C824u
#define BC250_HQD_MMIO_MQD_BASE_HI        0x0000C828u
#define BC250_HQD_MMIO_ACTIVE             0x0000C82Cu
#define BC250_HQD_MMIO_VMID               0x0000C830u
#define BC250_HQD_MMIO_PERSISTENT_STATE   0x0000C834u
#define BC250_HQD_MMIO_PIPE_PRIORITY      0x0000C838u
#define BC250_HQD_MMIO_QUEUE_PRIORITY     0x0000C83Cu
#define BC250_HQD_MMIO_QUANTUM            0x0000C840u
#define BC250_HQD_MMIO_PQ_BASE_LO         0x0000C844u
#define BC250_HQD_MMIO_PQ_BASE_HI         0x0000C848u
#define BC250_HQD_MMIO_PQ_RPTR            0x0000C84Cu
#define BC250_HQD_MMIO_RPTR_REPORT_LO     0x0000C850u
#define BC250_HQD_MMIO_RPTR_REPORT_HI     0x0000C854u
#define BC250_HQD_MMIO_WPTR_POLL_LO       0x0000C858u
#define BC250_HQD_MMIO_WPTR_POLL_HI       0x0000C85Cu
#define BC250_HQD_MMIO_DOORBELL_CONTROL   0x0000C860u
#define BC250_HQD_MMIO_PQ_CONTROL         0x0000C868u
#define BC250_HQD_MMIO_IB_BASE_LO         0x0000C870u
#define BC250_HQD_MMIO_IB_BASE_HI         0x0000C874u
#define BC250_HQD_MMIO_IB_CONTROL         0x0000C878u
#define BC250_HQD_MMIO_IB_RPTR            0x0000C87Cu
#define BC250_HQD_MMIO_DEQUEUE_REQUEST    0x0000C884u
#define BC250_HQD_MMIO_MQD_CONTROL        0x0000C8ACu
#define BC250_HQD_MMIO_EOP_BASE_LO        0x0000C8B8u
#define BC250_HQD_MMIO_EOP_BASE_HI        0x0000C8BCu
#define BC250_HQD_MMIO_EOP_CONTROL        0x0000C8C0u
#define BC250_HQD_MMIO_EOP_RPTR           0x0000C8C4u
#define BC250_HQD_MMIO_PQ_WPTR_LO         0x0000C8FCu
#define BC250_HQD_MMIO_PQ_WPTR_HI         0x0000C900u

/* Coherence path used immediately before publishing the HQD WPTR. */
#define BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE 0x00003FC4u
#define BC250_HQD_MMIO_HDP_MISC                  0x00003FCCu
#define BC250_HQD_MMIO_HDP_FLUSH                 0x00004A80u
#define BC250_HQD_MMIO_GCVM_L2_CNTL2              0x0000A104u
#define BC250_HQD_MMIO_GCVM_CONTEXT0_CNTL         0x0000A200u
#define BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_LO   0x0000A3ACu
#define BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_HI   0x0000A3B0u
#define BC250_HQD_MMIO_GCVM_CONTEXT0_PT_START_LO  0x0000A42Cu
#define BC250_HQD_MMIO_GCVM_CONTEXT0_PT_END_LO    0x0000A4ACu
#define BC250_HQD_MMIO_GCVM_INVALIDATE_REQ        0x0000A2D0u
#define BC250_HQD_MMIO_GCVM_INVALIDATE_ACK        0x0000A318u
#define BC250_HQD_MMIO_GCVM_INVALIDATE_ENG0_REQ   0x0000A28Cu
#define BC250_HQD_MMIO_CP_MEC_DB_RANGE_LOWER      0x0000C170u
#define BC250_HQD_MMIO_CP_MEC_DB_RANGE_UPPER      0x0000C174u
#define BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL       0x0000C20Cu
/*
 * CP_PQ_WPTR_POLL_CNTL1 (mmCP_PQ_WPTR_POLL_CNTL1 = dw 0x1E24, BASE_IDX 0).
 * A base dw->BAR5 deste bloco e +0x1260 dwords, confirmada por tres
 * registradores ja catalogados: 0x1E23->0xC20C, 0x1E2D->0xC234 e
 * 0x1E55->0xC2D4.  Logo 0x1E24 -> (0x1E24 + 0x1260) * 4 = 0xC210.
 * QUEUE_MASK ocupa os 32 bits (gc_10_1_0_sh_mask.h: QUEUE_MASK_MASK).
 */
#define BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1      0x0000C210u
#define BC250_HQD_MMIO_CP_PQ_STATUS                0x0000C2E0u
/* Read-only scheduler/fault diagnostics.  RLC uses GC BASE_IDX 1. */
#define BC250_HQD_MMIO_RLC_CP_SCHEDULERS           0x0003B2A8u
#define BC250_HQD_MMIO_CC_GC_SHADER_ARRAY_CONFIG   0x000089BCu
#define BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK      0x0000935Cu
#define BC250_HQD_MMIO_RLC_PG_CNTL                 0x0003B10Cu
#define BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK   0x0003B14Cu
#define BC250_HQD_MMIO_SMUIO_GFX_MISC_CNTL         0x0005A314u
#define BC250_HQD_CC_INACTIVE_WGPS_MASK             0xFFFF0000u
#define BC250_HQD_CC_INACTIVE_WGPS_SHIFT            16u
/*
 * Diagnostico de stall do CP/CPC - candidato 3.34, somente leitura.
 *
 * Derivados de gc_10_1_0_offset.h com a base +0x1260 dwords, a mesma que ja
 * fecha em cinco registradores independentes, dois deles escritos com sucesso
 * no silicio: CP_MEC_CNTL (dw 0x0E2D -> 0x8234) e CP_ME_CNTL
 * (dw 0x0F56 -> 0x86D8). CP_MEC_ME1_HEADER_DUMP e adjacente ao CP_MEC_CNTL.
 *
 * Estes registradores NUNCA entram na allowlist de escrita.
 */
#define BC250_HQD_MMIO_CP_CPC_STATUS               0x00008210u
#define BC250_HQD_MMIO_CP_CPC_BUSY_STAT            0x00008214u
#define BC250_HQD_MMIO_CP_CPC_STALLED_STAT1        0x00008218u
#define BC250_HQD_MMIO_CP_CPF_STATUS               0x0000821Cu
#define BC250_HQD_MMIO_CP_CPF_BUSY_STAT            0x00008220u
#define BC250_HQD_MMIO_CP_CPF_STALLED_STAT1        0x00008224u
#define BC250_HQD_MMIO_CP_MEC_ME1_HEADER_DUMP      0x00008238u
#define BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP      0x0000823Cu
#define BC250_HQD_MMIO_CP_STALLED_STAT1            0x00008674u
#define BC250_HQD_MMIO_CP_STALLED_STAT2            0x00008678u
#define BC250_HQD_MMIO_CP_BUSY_STAT                0x0000867Cu
#define BC250_HQD_MMIO_CP_STAT                     0x00008680u
#define BC250_HQD_MMIO_CP_MEC1_INSTR_PNTR          0x000086A0u
#define BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR          0x000086A4u
#define BC250_HQD_MEC_HEADER_DUMP_DEPTH            4u
#define BC250_HQD_MMIO_CPC_UTCL1_ERROR             0x0000C17Cu
#define BC250_HQD_MMIO_CP_ME1_PIPE0_INT_STATUS     0x0000C234u
#define BC250_HQD_MMIO_CP_ME2_PIPE1_INT_STATUS     0x0000C248u
#define BC250_HQD_MMIO_CP_ME2_PIPE0_INT_STATUS     0x0000C244u
#define BC250_HQD_MMIO_CPC_INT_STATUS              0x0000C2D4u
#define BC250_HQD_MMIO_CPC_UTCL1_STATUS            0x0000C6D4u
#define BC250_HQD_MMIO_CPF_UTCL1_STATUS            0x0000C6D8u
#define BC250_HQD_MMIO_HQ_STATUS0                  0x0000C8A4u
#define BC250_HQD_MMIO_HQ_CONTROL0                 0x0000C8A8u
#define BC250_HQD_MMIO_HQD_ERROR                   0x0000C8F0u
#define BC250_HQD_GCVM_INVALIDATE_VMID0            0x00000001u
#define BC250_HQD_GCVM_INVALIDATE_REQ_VMID0         0x00F80001u
#define BC250_HQD_GCVM_L2_INVALIDATE_MASK          0x00000003u
#define BC250_HQD_RLC_SCHEDULER0_MASK               0x000000FFu
#define BC250_HQD_RLC_SCHEDULER_ENABLE              0x00000080u
#define BC250_HQD_WPTR_POLL_ENABLE_MASK            0x80000000u
/* CP_PQ_WPTR_POLL_CNTL: PERIOD[7:0], DISABLE_PEND_REQ_ONE_SHOT[29],
 * POLL_ACTIVE[30] (status read-only) e EN[31].  O literal historico
 * 0x00403004 pertence ao SDMA (SdmaRingPollCntl) e NAO ao CP; ele fica
 * proibido neste caminho.
 */
#define BC250_HQD_WPTR_POLL_PERIOD_MASK            0x000000FFu
#define BC250_HQD_WPTR_POLL_ACTIVE_MASK            0x40000000u
/* PERIOD de sonda: valor arbitrario dentro de PERIOD[7:0], restaurado depois. */
#define BC250_HQD_WPTR_POLL_PROBE_PERIOD           0x00000004u
/* CP_PQ_WPTR_POLL_CNTL1: um bit por fila; MEC1 usa 8 filas por pipe. */
#define BC250_HQD_QUEUES_PER_PIPE                  8u
#define BC250_HQD_QUEUE_MASK_MAX_BIT               31u
#define BC250_HQD_CP_PQ_DOORBELL_UPDATED_MASK       0x00000001u
#define BC250_HQD_CP_PQ_DOORBELL_ENABLE_MASK        0x00000002u
#define BC250_HQD_DOORBELL_BIF_DROP_MASK            0x00000002u
#define BC250_HQD_DOORBELL_HIT_MASK                 0x80000000u

typedef enum BC250_MQD_STATUS {
    BC250_MQD_OK = 0,
    BC250_MQD_BAD_ARGUMENT,
    BC250_MQD_MISALIGNED_ADDRESS,
    BC250_MQD_INVALID_QUEUE_SIZE,
    BC250_MQD_INVALID_EOP_SIZE,
    BC250_MQD_INVALID_FIELD
} BC250_MQD_STATUS;

typedef enum BC250_HQD_PREFLIGHT_STATUS {
    BC250_HQD_PREFLIGHT_OK = 0,
    BC250_HQD_PREFLIGHT_BAD_ARGUMENT,
    BC250_HQD_PREFLIGHT_UNSTABLE,
    BC250_HQD_PREFLIGHT_UNMAPPED,
    BC250_HQD_PREFLIGHT_INHERITED_ACTIVE,
    BC250_HQD_PREFLIGHT_TRANSPORT_FAILED,
    BC250_HQD_PREFLIGHT_WRITE_DETECTED,
    BC250_HQD_PREFLIGHT_SELECTOR_NOT_COMPUTE
} BC250_HQD_PREFLIGHT_STATUS;

typedef enum BC250_HQD_PREFLIGHT_REGISTER {
    BC250_HQD_PREFLIGHT_SELECTOR_BEFORE = 0,
    BC250_HQD_PREFLIGHT_MQD_BASE_LO,
    BC250_HQD_PREFLIGHT_MQD_BASE_HI,
    BC250_HQD_PREFLIGHT_ACTIVE,
    BC250_HQD_PREFLIGHT_VMID,
    BC250_HQD_PREFLIGHT_PQ_BASE_LO,
    BC250_HQD_PREFLIGHT_PQ_BASE_HI,
    BC250_HQD_PREFLIGHT_PQ_RPTR,
    BC250_HQD_PREFLIGHT_DOORBELL_CONTROL,
    BC250_HQD_PREFLIGHT_PQ_CONTROL,
    BC250_HQD_PREFLIGHT_PQ_WPTR_LO,
    BC250_HQD_PREFLIGHT_PQ_WPTR_HI,
    BC250_HQD_PREFLIGHT_SELECTOR_AFTER,
    BC250_HQD_PREFLIGHT_REGISTER_COUNT
} BC250_HQD_PREFLIGHT_REGISTER;

typedef struct BC250_HQD_READONLY_SNAPSHOT {
    uint32_t Values[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
    uint32_t StablePasses;
    uint32_t HqdWasInactive;
} BC250_HQD_READONLY_SNAPSHOT;

typedef uint32_t (*BC250_HQD_READ32_FN)(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
typedef uint32_t (*BC250_HQD_WRITE32_FN)(
    void *Context, uint32_t ByteOffset, uint32_t Value);
typedef void (*BC250_HQD_BARRIER_FN)(void *Context);

typedef struct BC250_HQD_SELECTOR_INTERFACE {
    BC250_HQD_READ32_FN Read32;
    BC250_HQD_WRITE32_FN Write32;
    BC250_HQD_BARRIER_FN Barrier;
    void *Context;
} BC250_HQD_SELECTOR_INTERFACE;

typedef enum BC250_HQD_SELECTOR_STATUS {
    BC250_HQD_SELECTOR_OK = 0,
    BC250_HQD_SELECTOR_BAD_ARGUMENT,
    BC250_HQD_SELECTOR_PREFLIGHT_FAILED,
    BC250_HQD_SELECTOR_WRITE_FAILED,
    BC250_HQD_SELECTOR_READBACK_FAILED,
    BC250_HQD_SELECTOR_HQD_FAILED,
    BC250_HQD_SELECTOR_RESTORE_FAILED
} BC250_HQD_SELECTOR_STATUS;

typedef struct BC250_HQD_SELECTOR_EVIDENCE {
    uint32_t OriginalSelector;
    uint32_t AppliedSelector;
    uint32_t AppliedSelectorReadbackFirst;
    uint32_t AppliedSelectorReadbackLast;
    uint32_t RestoredSelector;
    uint32_t ReadCalls;
    uint32_t WriteCalls;
    uint32_t ApplyReadbackPassed;
    uint32_t RestorePassed;
    uint32_t PhysicalReady;
    BC250_HQD_PREFLIGHT_STATUS HqdStatus;
    BC250_HQD_READONLY_SNAPSHOT HqdSnapshot;
} BC250_HQD_SELECTOR_EVIDENCE;

typedef enum BC250_HQD_BANK_PROBE_STATUS {
    BC250_HQD_BANK_PROBE_OK = 0,
    BC250_HQD_BANK_PROBE_BAD_ARGUMENT,
    BC250_HQD_BANK_PROBE_PREFLIGHT_FAILED,
    BC250_HQD_BANK_PROBE_ACTIVE,
    BC250_HQD_BANK_PROBE_READ_FAILED,
    BC250_HQD_BANK_PROBE_WRITE_FAILED,
    BC250_HQD_BANK_PROBE_POISON_FAILED,
    BC250_HQD_BANK_PROBE_NOT_ISOLATED,
    BC250_HQD_BANK_PROBE_RESTORE_FAILED,
    BC250_HQD_BANK_PROBE_HQD_CAPTURE_FAILED
} BC250_HQD_BANK_PROBE_STATUS;

typedef struct BC250_HQD_BANK_PROBE_EVIDENCE {
    uint32_t OriginalSelector;
    uint32_t Bank0Active;
    uint32_t Bank0Priority;
    uint32_t CandidateActive;
    uint32_t CandidatePriority;
    uint32_t PoisonPriority;
    uint32_t PoisonReadback;
    uint32_t Bank0CrossReadback;
    uint32_t CandidateCrossReadback;
    uint32_t CandidateRestoreReadback;
    uint32_t Bank0FinalReadback;
    uint32_t FinalSelector;
    uint32_t ReadCalls;
    uint32_t WriteCalls;
    uint32_t PoisonReadbackPassed;
    uint32_t BankIsolationPassed;
    uint32_t RestorePassed;
    uint32_t HqdCapturePassed;
    uint32_t PhysicalReady;
    BC250_HQD_READONLY_SNAPSHOT CandidateHqdSnapshot;
} BC250_HQD_BANK_PROBE_EVIDENCE;

typedef enum BC250_HQD_WPTR_DIAGNOSTIC_STATUS {
    BC250_HQD_WPTR_DIAG_OK = 0,
    BC250_HQD_WPTR_DIAG_BAD_ARGUMENT,
    BC250_HQD_WPTR_DIAG_PREFLIGHT_FAILED,
    BC250_HQD_WPTR_DIAG_SELECTOR_WRITE_FAILED,
    BC250_HQD_WPTR_DIAG_PASS1_READ_FAILED,
    BC250_HQD_WPTR_DIAG_PASS2_READ_FAILED,
    BC250_HQD_WPTR_DIAG_SNAPSHOT_UNSTABLE,
    BC250_HQD_WPTR_DIAG_BANK_ACTIVE,
    BC250_HQD_WPTR_DIAG_WRITE_FAILED,
    BC250_HQD_WPTR_DIAG_RESTORE_FAILED
} BC250_HQD_WPTR_DIAGNOSTIC_STATUS;

#define BC250_HQD_WPTR_DIAG_SERIES_COUNT 16u

typedef struct BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE {
    uint32_t OriginalSelector;
    uint32_t TargetSelector;
    uint32_t TargetSelectorReadback;

    uint32_t ActiveBeforePass1;
    uint32_t ActiveBeforePass2;
    uint32_t RptrBeforePass1;
    uint32_t RptrBeforePass2;
    uint32_t WptrLoBeforePass1;
    uint32_t WptrLoBeforePass2;
    uint32_t WptrHiBeforePass1;
    uint32_t WptrHiBeforePass2;
    uint32_t PqControlBeforePass1;
    uint32_t PqControlBeforePass2;
    uint32_t PqBaseLoBeforePass1;
    uint32_t PqBaseLoBeforePass2;
    uint32_t PqBaseHiBeforePass1;
    uint32_t PqBaseHiBeforePass2;
    uint32_t DoorbellControlBeforePass1;
    uint32_t DoorbellControlBeforePass2;
    uint32_t StablePreflightPassed;

    uint32_t TestValueWritten;
    uint32_t FirstReadback;
    uint32_t SeriesReadbacks[BC250_HQD_WPTR_DIAG_SERIES_COUNT];
    uint32_t SeriesSelectors[BC250_HQD_WPTR_DIAG_SERIES_COUNT];
    uint32_t SeriesMatchIndex;
    uint32_t SelectorRemainedUnmodified;

    uint32_t RestoredValueWritten;
    uint32_t RestoredValueObserved;
    uint32_t RestorePolls;
    uint32_t RestorePassed;

    uint32_t ActiveAfterPass1;
    uint32_t ActiveAfterPass2;
    uint32_t WptrLoAfterPass1;
    uint32_t WptrLoAfterPass2;
    uint32_t WptrHiAfterPass1;
    uint32_t WptrHiAfterPass2;

    uint32_t RestoredSelector;
    uint32_t SelectorRestorePassed;

    uint32_t ReadCalls;
    uint32_t WriteCalls;
    uint32_t PhysicalReady;
} BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE;

typedef struct BC250_COMPUTE_MQD_PROPERTIES {
    uint64_t MqdGpuAddress;
    uint64_t HqdBaseGpuAddress;
    uint32_t QueueSizeDwords;
    uint64_t EopGpuAddress;
    uint32_t EopSizeBytes;
    uint64_t RptrGpuAddress;
    uint64_t WptrGpuAddress;
    uint32_t UseDoorbell;
    uint32_t DoorbellIndex;
    /* Fila MEC explicitamente possuida pelo miniport; nao herdar do boot. */
    uint32_t SchedulerMe;
    uint32_t SchedulerPipe;
    uint32_t SchedulerQueue;
    uint32_t PipePriority;
    uint32_t QueuePriority;
    uint32_t HqdActive;
    uint32_t InitialWptrDwords;
} BC250_COMPUTE_MQD_PROPERTIES;

typedef struct BC250_GFX10_COMPUTE_MQD {
    uint32_t Header;                                /* 0x00: 0xC0310800 */
    uint32_t ComputePipelinestatEnable;             /* 0x04: 0x00000001 */
    uint32_t ComputeStaticThreadMgmtSe0;            /* 0x08: 0xFFFFFFFF */
    uint32_t ComputeStaticThreadMgmtSe1;            /* 0x0C: 0xFFFFFFFF */
    uint32_t ComputeStaticThreadMgmtSe2;            /* 0x10: 0xFFFFFFFF */
    uint32_t ComputeStaticThreadMgmtSe3;            /* 0x14: 0xFFFFFFFF */
    uint32_t ComputeMiscReserved;                   /* 0x18: 0x00000003 */
    uint32_t Reserved0[9];                          /* 0x1C - 0x3C */
    uint32_t CpHqdEopBaseAddrLo;                    /* 0x40: EOP Base >> 8 */
    uint32_t CpHqdEopBaseAddrHi;                    /* 0x44: EOP Base Hi */
    uint32_t CpHqdEopControl;                       /* 0x48: EOP size config */
    uint32_t CpHqdPqDoorbellControl;                /* 0x4C: Doorbell config */
    uint32_t CpHqdDequeueRequest;                   /* 0x50: 0 */
    uint32_t CpHqdPqRptr;                           /* 0x54: RPTR */
    uint32_t CpHqdPqWptrLo;                         /* 0x58: WPTR Lo */
    uint32_t CpHqdPqWptrHi;                         /* 0x5C: WPTR Hi */
    uint32_t CpMqdBaseAddrLo;                       /* 0x60: MQD Base Lo */
    uint32_t CpMqdBaseAddrHi;                       /* 0x64: MQD Base Hi */
    uint32_t CpMqdControl;                          /* 0x68: VMID config */
    uint32_t CpHqdPqBaseLo;                         /* 0x6C: Ring Base >> 8 */
    uint32_t CpHqdPqBaseHi;                         /* 0x70: Ring Base Hi */
    uint32_t CpHqdPqControl;                        /* 0x74: Queue size, page size, unord */
    uint32_t CpHqdPqRptrReportAddrLo;               /* 0x78: RPTR report Lo */
    uint32_t CpHqdPqRptrReportAddrHi;               /* 0x7C: RPTR report Hi */
    uint32_t CpHqdPqWptrPollAddrLo;                 /* 0x80: WPTR poll Lo */
    uint32_t CpHqdPqWptrPollAddrHi;                 /* 0x84: WPTR poll Hi */
    uint32_t CpHqdVmid;                             /* 0x88: VMID 0 */
    uint32_t CpHqdPersistentState;                  /* 0x8C: Preload 0x53 */
    uint32_t CpHqdIbControl;                        /* 0x90: Min IB 3 */
    uint32_t CpHqdQuantum;                          /* 0x94: Quantum config */
    uint32_t CpHqdPipePriority;                     /* 0x98: Pipe priority */
    uint32_t CpHqdQueuePriority;                    /* 0x9C: Queue priority */
    uint32_t CpHqdActive;                           /* 0xA0: Active state */
    uint32_t Reserved1[23];                         /* 0xA4 - 0xFC */
    uint32_t Pad[64];                               /* 0x100 - 0x1FC (total 512 bytes) */
} BC250_GFX10_COMPUTE_MQD;

#define BC250_HQD_INACTIVE_PLAN_MAX_WRITES 16u

typedef struct BC250_HQD_REGISTER_WRITE {
    uint32_t ByteOffset;
    uint32_t Value;
} BC250_HQD_REGISTER_WRITE;

typedef enum BC250_HQD_INACTIVE_PLAN_STATUS {
    BC250_HQD_INACTIVE_PLAN_OK = 0,
    BC250_HQD_INACTIVE_PLAN_BAD_ARGUMENT,
    BC250_HQD_INACTIVE_PLAN_MQD_INVALID,
    BC250_HQD_INACTIVE_PLAN_UNSAFE_STATE,
    BC250_HQD_INACTIVE_PLAN_CAPACITY
} BC250_HQD_INACTIVE_PLAN_STATUS;

BC250_MQD_STATUS Bc250ComputeMqdInit(
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd);

BC250_MQD_STATUS Bc250ComputeMqdValidate(
    const BC250_GFX10_COMPUTE_MQD *Mqd,
    const BC250_COMPUTE_MQD_PROPERTIES *Props);

BC250_MQD_STATUS Bc250ComputeQueueComputeAvailableSpace(
    uint32_t QueueSizeDwords,
    uint32_t RptrDwords,
    uint32_t WptrDwords,
    uint32_t *AvailableDwords);

BC250_MQD_STATUS Bc250ComputeQueueAdvanceWptr(
    uint32_t QueueSizeDwords,
    uint32_t CurrentWptrDwords,
    uint32_t EmittedDwords,
    uint32_t *NewWptrDwords);

BC250_HQD_PREFLIGHT_STATUS Bc250ComputeHqdReadonlyPreflight(
    const uint32_t Pass1[BC250_HQD_PREFLIGHT_REGISTER_COUNT],
    const uint32_t Pass2[BC250_HQD_PREFLIGHT_REGISTER_COUNT],
    BC250_HQD_READONLY_SNAPSHOT *Snapshot);

BC250_HQD_SELECTOR_STATUS Bc250ComputeProbeMecHqdSelected(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_SELECTOR_EVIDENCE *Evidence);

BC250_HQD_BANK_PROBE_STATUS Bc250ComputeProbeMecHqdBankIsolation(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_BANK_PROBE_EVIDENCE *Evidence);

BC250_HQD_WPTR_DIAGNOSTIC_STATUS Bc250ComputeProbeMecHqdWptrBehavior(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE *Evidence);

/* Offline only: emits an inactive, no-doorbell register plan; performs no IO. */
BC250_HQD_INACTIVE_PLAN_STATUS Bc250ComputeBuildInactiveHqdPlan(
    const BC250_GFX10_COMPUTE_MQD *Mqd,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_HQD_REGISTER_WRITE *Writes,
    uint32_t WriteCapacity,
    uint32_t *WriteCount);

#define BC250_HQD_FENCE_POISON_VALUE       0x43DAFFFEu

typedef enum BC250_HQD_PROGRAM_STATUS {
    BC250_HQD_PROGRAM_OK = 0,
    BC250_HQD_PROGRAM_BAD_ARGUMENT,
    BC250_HQD_PROGRAM_PREFLIGHT_FAILED,
    BC250_HQD_PROGRAM_MEMORY_FAILED,
    BC250_HQD_PROGRAM_PLAN_FAILED,
    BC250_HQD_PROGRAM_WRITE_FAILED,
    BC250_HQD_PROGRAM_READBACK_FAILED,
    BC250_HQD_PROGRAM_UNSAFE_STATE,
    BC250_HQD_PROGRAM_RESTORE_FAILED
} BC250_HQD_PROGRAM_STATUS;

typedef struct BC250_HQD_MC_BUFFERS {
    volatile void *MqdVirtual;
    uint64_t MqdGpuAddress;
    uint32_t MqdSizeBytes;

    volatile void *RingVirtual;
    uint64_t RingGpuAddress;
    uint32_t RingSizeBytes;

    volatile void *EopVirtual;
    uint64_t EopGpuAddress;
    uint32_t EopSizeBytes;

    volatile void *RptrVirtual;
    uint64_t RptrGpuAddress;
    uint32_t RptrSizeBytes;

    volatile void *WptrVirtual;
    uint64_t WptrGpuAddress;
    uint32_t WptrSizeBytes;

    volatile void *FenceVirtual;
    uint64_t FenceGpuAddress;
    uint32_t FenceSizeBytes;

    /* Optional, required only by the observable CU shader transaction. */
    volatile void *ShaderVirtual;
    uint64_t ShaderGpuAddress;
    uint32_t ShaderSizeBytes;

    volatile void *ResultVirtual;
    uint64_t ResultGpuAddress;
    uint32_t ResultSizeBytes;

    volatile void *DoorbellVirtual;
    uint32_t DoorbellSizeBytes;

    volatile void *PtVirtual;
    uint64_t PtGpuAddress;
    uint32_t PtSizeBytes;
} BC250_HQD_MC_BUFFERS;

typedef struct BC250_HQD_PROGRAM_EVIDENCE {
    uint32_t OriginalSelector;
    uint32_t SelectedSelector;
    uint32_t RestoredSelector;

    uint32_t SnapshotValues[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t AppliedValues[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t ReadbackPass1[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t ReadbackPass2[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t RestoredValues[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];

    uint32_t ActiveReadbackPass1;
    uint32_t ActiveReadbackPass2;
    uint32_t WptrLoReadbackPass1;
    uint32_t WptrLoReadbackPass2;
    uint32_t WptrHiReadbackPass1;
    uint32_t WptrHiReadbackPass2;

    uint32_t ReadCalls;
    uint32_t WriteCalls;

    uint32_t MismatchMask;
    uint32_t FirstMismatchOffset;
    uint32_t FirstMismatchWritten;
    uint32_t FirstMismatchRead1;
    uint32_t FirstMismatchRead2;

    uint32_t MemoryVerificationPassed;
    uint32_t ApplyReadbackPassed;
    uint32_t ActiveRemainedZero;
    uint32_t RestorePassed;
    uint32_t MemoryRestorePassed;
    uint32_t PhysicalReady;
} BC250_HQD_PROGRAM_EVIDENCE;

typedef enum BC250_HQD_ACTIVE_STATUS {
    BC250_HQD_ACTIVE_OK = 0,
    BC250_HQD_ACTIVE_BAD_ARGUMENT,
    BC250_HQD_ACTIVE_PREFLIGHT_FAILED,
    BC250_HQD_ACTIVE_MEMORY_FAILED,
    BC250_HQD_ACTIVE_PLAN_FAILED,
    BC250_HQD_ACTIVE_WRITE_FAILED,
    BC250_HQD_ACTIVE_READBACK_FAILED,
    BC250_HQD_ACTIVE_PM4_NOT_CONSUMED,
    BC250_HQD_ACTIVE_FENCE_FAILED,
    BC250_HQD_ACTIVE_SCRATCH_FAILED,
    BC250_HQD_ACTIVE_SHADER_FAILED,
    BC250_HQD_ACTIVE_RESULT_FAILED,
    BC250_HQD_ACTIVE_RESTORE_FAILED
} BC250_HQD_ACTIVE_STATUS;

/*
 * Estagios do rollback interno.  O agregado RestorePassed nao permitia
 * localizar a falha observada no ensaio 3.29; cada sitio passa agora a
 * registrar o PRIMEIRO estagio que reprovou, com offset, esperado e lido.
 */
typedef enum BC250_HQD_RESTORE_STAGE {
    BC250_HQD_RESTORE_STAGE_NONE = 0,
    BC250_HQD_RESTORE_STAGE_ACTIVE_DEACTIVATE,
    BC250_HQD_RESTORE_STAGE_ACTIVE_READBACK,
    BC250_HQD_RESTORE_STAGE_DEQUEUE_REQUEST,
    BC250_HQD_RESTORE_STAGE_POINTER_WRITE,
    BC250_HQD_RESTORE_STAGE_POINTER_CONVERGE,
    BC250_HQD_RESTORE_STAGE_IB_CONTROL,
    BC250_HQD_RESTORE_STAGE_EOP_RPTR,
    BC250_HQD_RESTORE_STAGE_QUEUE_ADDRESS,
    BC250_HQD_RESTORE_STAGE_HQD_PLAN,
    BC250_HQD_RESTORE_STAGE_FINAL_BANK,
    BC250_HQD_RESTORE_STAGE_CP_PQ_STATUS,
    BC250_HQD_RESTORE_STAGE_WPTR_POLL_CNTL,
    BC250_HQD_RESTORE_STAGE_WPTR_POLL_CNTL1,
    BC250_HQD_RESTORE_STAGE_GCVM_L2_CNTL2,
    BC250_HQD_RESTORE_STAGE_RLC_CP_SCHEDULERS,
    BC250_HQD_RESTORE_STAGE_GCVM_CONTEXT0,
    BC250_HQD_RESTORE_STAGE_CP_MEC_CNTL,
    BC250_HQD_RESTORE_STAGE_CP_ME_CNTL,
    BC250_HQD_RESTORE_STAGE_COUNT
} BC250_HQD_RESTORE_STAGE;

typedef struct BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE {
    uint32_t OriginalSelector;
    uint32_t SelectedSelector;
    uint32_t RestoredSelector;

    uint32_t SnapshotValues[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t AppliedValues[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t ReadbackValues[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t RestoredValues[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];

    uint32_t ActiveBefore;
    uint32_t ActiveApplied;
    uint32_t ActiveAfter;

    uint32_t RptrBefore;
    uint32_t RptrAfter;
    uint32_t WptrApplied;

    uint32_t FenceBefore;
    uint32_t FenceAfter;
    uint32_t ScratchBefore;
    uint32_t ScratchAfter;

    uint32_t CpMecCntlBefore;
    uint32_t CpMecCntlApplied;
    uint32_t CpMecCntlAfter;
    uint32_t CpMeCntlBefore;
    uint32_t CpMeCntlApplied;
    uint32_t CpMeCntlAfter;

    uint32_t PqBaseLoApplied;
    uint32_t PqBaseHiApplied;
    uint32_t WptrPollAddrLoApplied;
    uint32_t WptrPollAddrHiApplied;
    uint32_t WptrShadowApplied;
    uint32_t CpMecCntlReadback;
    uint32_t MecPcFirst;
    uint32_t MecPcLast;
    uint32_t MecPcTransitions;
    uint32_t MecPcSamples;
    uint32_t PqControlObserved;
    uint32_t EopRptrBefore;
    uint32_t EopRptrApplied;
    uint32_t EopRptrReadback;
    uint32_t EopRptrRestored;
    uint32_t EopRptrRestorePassed;

    uint32_t DoorbellRung;
    uint32_t DoorbellIndex;
    uint32_t DoorbellOffset;
    uint32_t DoorbellValueApplied;
    uint32_t DoorbellRangeLowerBefore;
    uint32_t DoorbellRangeLowerApplied;
    uint32_t DoorbellRangeLowerReadback;
    uint32_t DoorbellRangeUpperBefore;
    uint32_t DoorbellRangeUpperApplied;
    uint32_t DoorbellRangeUpperReadback;
    uint32_t CpPqStatusBefore;
    uint32_t CpPqStatusApplied;
    uint32_t CpPqStatusReadback;
    uint32_t WptrPollCntlBefore;
    uint32_t WptrPollCntlApplied;
    uint32_t WptrPollCntlReadback;
    /*
     * Candidato 3.31 - sonda de gravabilidade do CP_PQ_WPTR_POLL_CNTL.
     *
     * O ensaio 3.30 escreveu EN=1 e releu 0x00000000: o bit nao e retido.
     * Ate 3.29 isso passava despercebido porque o caminho KIQ escrevia
     * `orig & ~EN` = 0 sobre um registrador que ja valia 0 - uma escrita de
     * zero sobre zero passa em qualquer readback.  A sonda escreve PERIOD
     * (campo comum, sem efeito de habilitacao) e rele, separando duas
     * hipoteses: registrador read-only neste contexto (nada gruda) contra
     * bit EN especificamente bloqueado (PERIOD gruda, EN nao).
     */
    uint32_t WptrPollCntlProbeApplied;
    uint32_t WptrPollCntlProbeReadback;
    uint32_t WptrPollCntlWritable;
    uint32_t WptrPollCntlEnLatched;
    /* CP_PQ_WPTR_POLL_CNTL1 (queue mask) - candidato 3.30. */
    uint32_t WptrPollCntl1Before;
    uint32_t WptrPollCntl1Applied;
    uint32_t WptrPollCntl1Readback;
    uint32_t WptrPollCntl1Restored;
    uint32_t WptrPollCntl1RestorePassed;
    uint32_t QueueMaskBit;
    /* Publicacao one-shot do WPTR na ordem KFD, antes de ACTIVE. */
    uint32_t PreActiveWptrApplied;
    uint32_t PreActiveWptrReadback;
    uint32_t PreActiveWptrPublished;
    /*
     * Candidato 3.32.
     *
     * PostActiveWptrObserved: WPTR lido logo apos ACTIVE=1 e ANTES de
     * qualquer doorbell ou escrita direta. Responde sozinho a pergunta que
     * sobrou do 3.31: com a fila dentro da QUEUE_MASK, o CP busca o cursor
     * do shadow por conta propria?
     *
     * WptrPollCntl1DirtyEntry: o 3.31 provou que a QUEUE_MASK nao volta a
     * zero. Num boot morno o snapshot entraria com o bit ja aceso e o
     * rollback "passaria" comparando 1 com 1 - um falso verde. Este campo
     * denuncia essa condicao.
     */
    uint32_t PostActiveWptrObserved;
    uint32_t WptrPollCntl1DirtyEntry;
    uint32_t WptrPollCntl1ClearAttempts;
    /*
     * Candidato 3.33 - observacao do consumo do ring.
     *
     * O 3.32 deixou a fila nao-vazia (PQ_EMPTY=0), nao-ociosa (QUEUE_IDLE=0),
     * com o cursor publicado pelo hardware e o MEC executando - e ainda assim
     * RPTR=0 depois de 50000 iteracoes. Estes campos separam as hipoteses sem
     * mudar nenhuma escrita: sao todos leitura.
     *
     * Primeira amostra x ultima permite ver se o estado evoluiu durante o
     * polling ou ficou congelado desde o inicio.
     */
    uint32_t PqControlFirstSample;
    uint32_t HqStatus0FirstSample;
    uint32_t DequeueRequestPollLast;
    uint32_t VmidPollLast;
    uint32_t PersistentStatePollLast;
    uint32_t RptrReportMemFirst;
    uint32_t RptrReportMemLast;
    uint32_t RptrNonZeroSamples;
    /*
     * Candidato 3.34 - por que o CPC nao consome.
     *
     * O 3.33 eliminou o writeback de RPTR (registrador e memoria ambos em
     * zero) e o VMID (0, igual ao Contexto 0). Sobrou o CP travado antes de
     * retirar o pacote, com o MEC em laco (PC volta sempre a 0x17FB) e o
     * proprio CP tendo alterado CP_HQD_PERSISTENT_STATE.
     *
     * CP_CPC_STATUS / BUSY_STAT / STALLED_STAT1 dizem em que o CPC esta
     * parado. Os dumps ME1/ME2 entregam os cabecalhos PM4 processados; ME2 e
     * o alvo observado no silicio e ME1 permanece como controle.
     *
     * O header dump e uma FIFO de depuracao: cada leitura avanca o ponteiro
     * interno. Por isso ele e lido UMA vez, em rajada curta, depois do laco
     * de polling - nunca a cada amostra.
     */
    uint32_t CpcStatusFirst;
    uint32_t CpcStatusLast;
    uint32_t CpcBusyStatLast;
    uint32_t CpfBusyStatBeforeActive;
    uint32_t CpfBusyStatAfterActive;
    uint32_t CpfBusyStatBeforeWptr;
    uint32_t CpfBusyStatAfterWptr;
    uint32_t CpfBusyStatLast;
    uint32_t CpfStalledStat1BeforeActive;
    uint32_t CpfStalledStat1AfterActive;
    uint32_t CpfStalledStat1BeforeKick;
    uint32_t CpfStalledStat1AfterKick;
    uint32_t CpfStalledStat1Last;
    uint32_t CpfStatusAfterActive;
    uint32_t CpfStatusAfterKick;
    uint32_t DequeueRequestApplied;
    uint32_t DequeueRequestReadback;
    uint32_t DequeueRequestRestored;
    uint32_t DequeueRequestRestorePassed;
    uint32_t CpcStalledStat1Last;
    uint32_t CpStatFirst;
    uint32_t CpStatLast;
    uint32_t CpStalledStat1After;
    uint32_t MecMe1HeaderDump[BC250_HQD_MEC_HEADER_DUMP_DEPTH];
    uint32_t MecMe2HeaderDumpBeforeActive[BC250_HQD_MEC_HEADER_DUMP_DEPTH];
    uint32_t MecMe2HeaderDump[BC250_HQD_MEC_HEADER_DUMP_DEPTH];
    uint32_t CpcStatusBeforeKick;
    uint32_t CpcBusyStatBeforeKick;
    uint32_t CpfBusyStatBeforeKick;
    uint32_t CpcStalledStat1BeforeKick;
    uint32_t CpStatBeforeKick;
    uint32_t CpStalledStat1BeforeKick;
    uint32_t CpcStatusAfterKick;
    uint32_t CpcBusyStatAfterKick;
    uint32_t CpfBusyStatAfterKick;
    uint32_t CpcStalledStat1AfterKick;
    uint32_t CpStatAfterKick;
    uint32_t CpStalledStat1AfterKick;
    uint32_t SelectorReleasedBeforeKick;
    uint32_t CpPqStatusAfterKick;
    uint32_t DoorbellControlAfterKick;
    uint32_t WptrAfterKick;
    uint32_t RptrAfterKick;
    uint32_t InitialWptrLo;
    uint32_t InitialWptrHi;
    uint32_t BaselineWptrApplied;
    uint32_t BaselineWptrReadback;
    uint32_t BaselineWptrRestored;
    uint32_t BaselineWptrRestorePolls;
    uint32_t BaselineWptrRestorePassed;
    uint32_t DirectWptrKickApplied;
    uint32_t DirectWptrBefore;
    uint32_t DirectWptrReadback;
    uint32_t DirectRptrAfterKick;
    uint32_t DirectWptrRestored;
    uint32_t DirectWptrRestorePolls;
    uint32_t DirectWptrRestorePassed;
    uint32_t RptrMmioLast;
    uint32_t WptrMmioLast;
    uint32_t HqStatus0PollLast;
    uint32_t HqdErrorPollLast;
    uint32_t RlcCpSchedulersBefore;
    uint32_t RlcCpSchedulersApplied;
    uint32_t RlcCpSchedulersReadback;
    uint32_t RlcCpSchedulersAfterKick;
    uint32_t RlcCpSchedulersRestored;
    uint32_t RlcCpSchedulersRestorePassed;
    uint32_t HqStatus0AfterKick;
    uint32_t HqControl0AfterKick;
    uint32_t HqdErrorAfterKick;
    uint32_t CpcUtcl1ErrorAfterKick;
    uint32_t CpcUtcl1StatusAfterKick;
    uint32_t CpfUtcl1StatusAfterKick;
    uint32_t CpcIntStatusAfterKick;
    uint32_t Me1Pipe0IntStatusAfterKick;
    uint32_t Me2Pipe1IntStatusAfterKick;
    uint32_t Me2Pipe0IntStatusAfterKick;

    uint32_t HdpInvalidateApplied;
    uint32_t GcvmL2Cntl2Before;
    uint32_t GcvmL2Cntl2Applied;
    uint32_t GcvmL2Cntl2Readback;
    uint32_t GcvmInvalidateRequestApplied;
    uint32_t GcvmInvalidateRequestReadback;
    uint32_t GcvmInvalidateAckBefore;
    uint32_t GcvmInvalidateAckAfter;
    uint32_t GcvmInvalidatePolls;
    uint32_t GcvmContext0CntlBefore;
    uint32_t GcvmContext0CntlApplied;
    uint32_t GcvmContext0PtBaseLoBefore;
    uint32_t GcvmContext0PtBaseHiBefore;
    uint32_t GcvmContext0PtStartLoBefore;
    uint32_t GcvmContext0PtEndLoBefore;
    uint32_t GcvmContext0PtBaseLoApplied;
    uint32_t GcvmContext0RestorePassed;
    uint32_t CoherencyPassed;

    uint32_t GfxOffApplicable;
    uint32_t GfxOffStateBefore;
    uint32_t GfxOffStateGuarded;
    uint32_t GfxOffStateAfter;
    uint32_t GfxOffDisallowResponse;
    uint32_t GfxOffAllowResponse;
    uint32_t GfxOffGuardPassed;
    uint32_t GfxOffRestorePassed;

    uint32_t PollIterations;
    uint32_t ReadCalls;
    uint32_t WriteCalls;

    uint32_t MemoryVerificationPassed;
    uint32_t ActiveConfirmed;
    uint32_t TriadPassed;
    uint32_t RestorePassed;
    /*
     * Localizacao da PRIMEIRA reprovacao de rollback.  Stage usa
     * BC250_HQD_RESTORE_STAGE; Offset e o byte offset BAR5 quando
     * aplicavel, ou 0 para estagios sem registrador unico.
     */
    uint32_t FirstRestoreFailureStage;
    uint32_t FirstRestoreFailureOffset;
    uint32_t FirstRestoreFailureExpected;
    uint32_t FirstRestoreFailureObserved;
    /* 1 = a propria chamada MMIO falhou; 0 = valor lido divergiu. */
    uint32_t FirstRestoreFailureTransport;
    uint32_t RestoreFailureCount;
    uint32_t MemoryRestorePassed;
    uint32_t PhysicalReady;
} BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE;

typedef struct BC250_HQD_SHADER_DISPATCH_EVIDENCE {
    BC250_COMPUTE_DISPATCH_PLAN_EVIDENCE Plan;
    BC250_COMPUTE_DISPATCH_OBSERVATION Observation;
    BC250_COMPUTE_DISPATCH_VERDICT Verdict;
    uint32_t CcShaderArrayPass1;
    uint32_t CcShaderArrayPass2;
    uint32_t SpiStaticWgpMaskPass1;
    uint32_t SpiStaticWgpMaskPass2;
    uint32_t RlcAlwaysOnWgpMaskPass1;
    uint32_t RlcAlwaysOnWgpMaskPass2;
    uint32_t NonHarvestedWgpMask;
    uint32_t AlwaysOnNonHarvestedWgpMask;
    uint32_t WgpPreflightPassed;
    uint32_t RingGpuVaLow;
    uint32_t RingGpuVaHigh;
    uint32_t RingPhysicalLow;
    uint32_t RingPhysicalHigh;
    uint32_t RingPteLow;
    uint32_t RingPteHigh;
    uint32_t RingTranslationPassed;
    uint32_t RingReadbackMismatchCount;
    uint32_t RingReadbackPassed;
    uint32_t RingDword[8];
    uint32_t ShaderReadbackMismatchCount;
    uint32_t ShaderReadbackPassed;
    uint32_t ShaderRestorePassed;
    uint32_t ResultRestorePassed;
    uint32_t FenceRestorePassed;
    uint32_t RingRestorePassed;
    uint32_t PhysicalReady;
} BC250_HQD_SHADER_DISPATCH_EVIDENCE;

/*
 * Estado de teardown de uma fila de computacao ATIVA (Fase P1).
 *
 * Ate o candidato 3.43 toda transacao era de tiro unico: ativava, submetia e
 * revertia dentro da mesma chamada, com os valores originais vivos apenas em
 * variaveis locais.  O caminho de producao precisa manter a fila ativa entre
 * submissoes, portanto esses mesmos valores tem de sobreviver a chamada.  Esta
 * estrutura e' exatamente aquele conjunto, movido de locais para um bloco que
 * o chamador possui e guarda ate o StopDevice.
 *
 * O caminho nao persistente continua usando a mesma estrutura como local, com
 * a mesma ordem de rollback LIFO: nao existe segunda implementacao de restore.
 */
typedef struct BC250_HQD_PERSISTENT_STATE {
    uint32_t QueueIsActive;
    uint32_t TransactionMode;

    BC250_HQD_REGISTER_WRITE PlanWrites[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t PlanCount;

    uint32_t OriginalScratch;
    uint32_t OrigCpMecCntl;
    uint32_t OrigCpMeCntl;
    uint32_t OrigCpPqStatus;
    uint32_t OrigCpPqWptrPollCntl;
    uint32_t OrigCpPqWptrPollCntl1;
    uint32_t OrigGcvmL2Cntl2;
    uint32_t OrigGcvmCtx0Cntl;
    uint32_t OrigGcvmCtx0PtBaseLo;
    uint32_t OrigGcvmCtx0PtBaseHi;
    uint32_t OrigGcvmCtx0PtStartLo;
    uint32_t OrigGcvmCtx0PtEndLo;
    uint32_t OrigIbControl;
    uint32_t OrigEopRptr;

    uint32_t QueueAddressSnapshots[4];
    uint32_t QueueAddressWriteCount;
    uint32_t DequeueRequestSnapshot;
    uint32_t WptrLoSnapshot;
    uint32_t WptrHiSnapshot;

    uint32_t HqdWritten;
    uint32_t CpPqStatusWritten;
    uint32_t CpPqWptrPollCntlWritten;
    uint32_t CpPqWptrPollCntl1Written;
    uint32_t GcvmL2Cntl2Written;
    uint32_t GcvmCtx0Written;
    uint32_t RlcCpSchedulersWritten;
    uint32_t DequeueRequestWritten;
    uint32_t WptrBaselineWritten;
    uint32_t DirectWptrWritten;
    uint32_t IbControlWritten;
    uint32_t EopRptrWritten;
    uint32_t CpMecCntlWritten;
    uint32_t CpMeCntlWritten;
    uint32_t ScratchWritten;
    uint32_t SelectorCaptured;
    uint32_t ObservableDispatch;
    uint32_t ObservableMemoryStaged;
    uint32_t CoreMemoryRestorePassed;

    uint32_t MqdSnapshot[BC250_MQD_SIZE_DWORDS];
    uint32_t RingSnapshot[BC250_DISPATCH_RING_MIN_DWORDS];
    uint32_t ShaderSnapshot[BC250_OBSERVABLE_SHADER_DWORDS];
    uint32_t ResultSnapshot;
    uint32_t FenceSnapshot[2];
    uint32_t RptrSnapshot[2];
    uint32_t WptrSnapshot[2];

    BC250_COMPUTE_DISPATCH_DESCRIPTOR DispatchDescriptor;
} BC250_HQD_PERSISTENT_STATE;

/*
 * Modo da transacao.  DIAGNOSTIC e OBSERVABLE_SHADER sao os dois caminhos
 * provados em silicio e permanecem inalterados.  PERSISTENT_ACTIVATE deixa a
 * fila ATIVA e vazia (WPTR == RPTR == 0), sem lote e sem rollback: quem chama
 * fica dono do teardown via Bc250ComputeDeactivatePersistentHqd.
 */
typedef enum BC250_HQD_TRANSACTION_MODE {
    BC250_HQD_TRANSACTION_DIAGNOSTIC = 0,
    BC250_HQD_TRANSACTION_OBSERVABLE_SHADER = 1,
    BC250_HQD_TRANSACTION_PERSISTENT_ACTIVATE = 2
} BC250_HQD_TRANSACTION_MODE;

/*
 * Ativa a fila e a deixa viva.  Em caso de falha, o rollback e' executado
 * dentro da propria chamada e State->QueueIsActive volta zerado: nunca se
 * devolve fila meio ativada.
 */
BC250_HQD_ACTIVE_STATUS Bc250ComputeActivatePersistentHqd(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence,
    BC250_HQD_PERSISTENT_STATE *State);

/*
 * Desativa a fila e executa exatamente o mesmo rollback LIFO do caminho de
 * tiro unico, a partir do estado guardado na ativacao.
 */
BC250_HQD_ACTIVE_STATUS Bc250ComputeDeactivatePersistentHqd(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence,
    BC250_HQD_PERSISTENT_STATE *State);

BC250_HQD_PROGRAM_STATUS Bc250ComputeRunInactiveHqdProgramTransaction(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd,
    BC250_HQD_PROGRAM_EVIDENCE *Evidence);

BC250_HQD_ACTIVE_STATUS Bc250ComputeRunActiveHqdSubmissionTransaction(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *Evidence);

/*
 * Physical path for the single allowlisted gfx1010 shader.  PhysicalReady is
 * impossible unless CU result, post-dispatch fence, RPTR/WPTR and both
 * register/memory rollback all pass in the same transaction.
 */
BC250_HQD_ACTIVE_STATUS Bc250ComputeRunObservableShaderDispatchTransaction(
    const BC250_HQD_SELECTOR_INTERFACE *Interface,
    const BC250_HQD_MC_BUFFERS *Buffers,
    const BC250_COMPUTE_MQD_PROPERTIES *Props,
    BC250_GFX10_COMPUTE_MQD *Mqd,
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE *ActiveEvidence,
    BC250_HQD_SHADER_DISPATCH_EVIDENCE *DispatchEvidence);

const char *Bc250MqdStatusName(BC250_MQD_STATUS Status);
const char *Bc250HqdProgramStatusName(BC250_HQD_PROGRAM_STATUS Status);
const char *Bc250HqdActiveStatusName(BC250_HQD_ACTIVE_STATUS Status);
const char *Bc250HqdWptrDiagStatusName(BC250_HQD_WPTR_DIAGNOSTIC_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
