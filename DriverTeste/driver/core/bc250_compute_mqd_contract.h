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

typedef enum BC250_MQD_STATUS {
    BC250_MQD_OK = 0,
    BC250_MQD_BAD_ARGUMENT,
    BC250_MQD_MISALIGNED_ADDRESS,
    BC250_MQD_INVALID_QUEUE_SIZE,
    BC250_MQD_INVALID_EOP_SIZE
} BC250_MQD_STATUS;

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
    uint32_t PipePriority;
    uint32_t QueuePriority;
    uint32_t HqdActive;
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

const char *Bc250MqdStatusName(BC250_MQD_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
