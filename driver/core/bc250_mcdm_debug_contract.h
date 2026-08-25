#ifndef BC250_MCDM_DEBUG_CONTRACT_H
#define BC250_MCDM_DEBUG_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT32 uint32_t;
#endif
#else
#include <stdint.h>
#endif

#define BC250_MCDM_DEBUG_MAGIC   0x31474244u /* "DBG1" */
#define BC250_MCDM_DEBUG_VERSION 1u

#define BC250_MCDM_DEBUG_ADAPTER_REGISTERED 0x00000001u
#define BC250_MCDM_DEBUG_ACTIVE             0x00000002u
#define BC250_MCDM_DEBUG_CONTEXT_ABI_READY  0x00000004u
#define BC250_MCDM_DEBUG_ALLOCATION_READY   0x00000008u

typedef struct BC250_MCDM_DEBUG_SNAPSHOT_V1 {
    uint32_t Magic;
    uint32_t Version;
    uint32_t StructSize;
    uint32_t Reason;
    uint32_t StateFlags;
    uint32_t FirstFailureStatus;
    uint32_t ProbeSequence;
    uint32_t LastProbeField;
    uint32_t LastProbeValue;
    uint32_t ObjectLiveCount;
    uint32_t ObjectNextSlot;
    uint32_t SubmissionsCount;
    uint32_t ResetsCount;
    uint32_t Reserved[3];
} BC250_MCDM_DEBUG_SNAPSHOT_V1;

#endif
