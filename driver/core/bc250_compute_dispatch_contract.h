#ifndef BC250_COMPUTE_DISPATCH_CONTRACT_H
#define BC250_COMPUTE_DISPATCH_CONTRACT_H

#include "bc250_isa_validator.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Packet register coordinates, not BAR byte offsets.  They are derived from
 * the catalogued GC10.1 BAR5 offsets in docs/03_CATALOGO_DE_REGISTRADORES.md:
 * packet_reg = BAR5_byte_offset / 4, then SET_SH_REG subtracts 0x2c00.
 */
#define BC250_DISPATCH_REG_COMPUTE_START_X       0x2E04u
#define BC250_DISPATCH_REG_COMPUTE_NUM_THREAD_X  0x2E07u
#define BC250_DISPATCH_REG_COMPUTE_PGM_LO        0x2E0Cu
#define BC250_DISPATCH_REG_COMPUTE_PGM_RSRC1     0x2E12u
#define BC250_DISPATCH_REG_COMPUTE_RESOURCE_LIMITS 0x2E15u
#define BC250_DISPATCH_REG_COMPUTE_USER_DATA_0   0x2E40u

#define BC250_DISPATCH_RESULT_POISON 0x43DAFFFEu
#define BC250_DISPATCH_FENCE_POISON  0xA55A0FF0u
#define BC250_DISPATCH_FENCE_VALUE   0xBC250D15u
#define BC250_DISPATCH_RESULT_VALUE  0xBC250C01u
#define BC250_DISPATCH_RING_MIN_DWORDS 64u

typedef enum BC250_COMPUTE_DISPATCH_PLAN_STATUS {
    BC250_COMPUTE_DISPATCH_PLAN_OK = 0,
    BC250_COMPUTE_DISPATCH_PLAN_BAD_ARGUMENT,
    BC250_COMPUTE_DISPATCH_PLAN_BAD_ADDRESS,
    BC250_COMPUTE_DISPATCH_PLAN_BAD_RESOURCE,
    BC250_COMPUTE_DISPATCH_PLAN_RING_TOO_SMALL,
    BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED
} BC250_COMPUTE_DISPATCH_PLAN_STATUS;

typedef struct BC250_COMPUTE_DISPATCH_DESCRIPTOR {
    uint64_t ShaderGpuAddress;
    uint64_t ResultGpuAddress;
    uint64_t FenceGpuAddress;

    uint32_t PgmRsrc1;
    uint32_t PgmRsrc2;
    BC250_WAVEFRONT_SIZE WaveSize;

    uint32_t NumThreadX;
    uint32_t NumThreadY;
    uint32_t NumThreadZ;
    uint32_t GridDimX;
    uint32_t GridDimY;
    uint32_t GridDimZ;

    uint32_t ResultPoison;
    uint32_t ResultExpected;
    uint32_t FencePoison;
    uint32_t FenceExpected;
} BC250_COMPUTE_DISPATCH_DESCRIPTOR;

typedef struct BC250_COMPUTE_DISPATCH_PLAN_EVIDENCE {
    uint32_t ProgramPacketIndex;
    uint32_t ResourcePacketIndex;
    uint32_t UserDataPacketIndex;
    uint32_t DispatchPacketIndex;
    uint32_t PartialFlushPacketIndex;
    uint32_t FencePacketIndex;
    uint32_t SubmittedDwords;
    uint32_t DispatchInitiator;
    uint32_t FenceIsAfterDispatch;
    uint32_t PhysicalReady;
} BC250_COMPUTE_DISPATCH_PLAN_EVIDENCE;

typedef struct BC250_COMPUTE_DISPATCH_OBSERVATION {
    uint32_t SubmittedDwords;
    uint32_t RptrBefore;
    uint32_t RptrAfter;
    uint32_t WptrBefore;
    uint32_t WptrAfter;
    uint32_t ResultBefore;
    uint32_t ResultAfter;
    uint32_t FenceBefore;
    uint32_t FenceAfter;
    uint32_t ResultMismatchCount;
    uint32_t RegistersRestorePassed;
    uint32_t MemoryRestorePassed;
} BC250_COMPUTE_DISPATCH_OBSERVATION;

typedef struct BC250_COMPUTE_DISPATCH_VERDICT {
    uint32_t RptrWptrPassed;
    uint32_t FencePassed;
    uint32_t ResultPassed;
    uint32_t RollbackPassed;
    uint32_t TriadPassed;
    uint32_t PhysicalReady;
} BC250_COMPUTE_DISPATCH_VERDICT;

BC250_COMPUTE_DISPATCH_PLAN_STATUS Bc250BuildObservableComputeDispatchPlan(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    const BC250_COMPUTE_DISPATCH_DESCRIPTOR *Descriptor,
    BC250_COMPUTE_DISPATCH_PLAN_EVIDENCE *Evidence);

BC250_COMPUTE_DISPATCH_PLAN_STATUS Bc250EvaluateObservableComputeDispatch(
    const BC250_COMPUTE_DISPATCH_DESCRIPTOR *Descriptor,
    const BC250_COMPUTE_DISPATCH_OBSERVATION *Observation,
    BC250_COMPUTE_DISPATCH_VERDICT *Verdict);

const char *Bc250ComputeDispatchPlanStatusName(
    BC250_COMPUTE_DISPATCH_PLAN_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
