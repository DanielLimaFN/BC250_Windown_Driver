#ifndef BC250_IH_CONTRACT_H
#define BC250_IH_CONTRACT_H

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

#define BC250_IH_ENTRY_SIZE_BYTES         16u
#define BC250_IH_ENTRY_SIZE_DWORDS        4u
#define BC250_IH_RING_ALIGNMENT           256u

#define BC250_IH_CLIENTID_GRBM_CP         0x01u
#define BC250_IH_CLIENTID_SDMA0           0x0Au
#define BC250_IH_CLIENTID_SDMA1           0x0Bu
#define BC250_IH_CLIENTID_IH              0x14u

#define BC250_IH_SRCID_CP_EOP             0xB5u
#define BC250_IH_SRCID_SDMA_TRAP          0xE0u
#define BC250_IH_SRCID_SDMA_SRBM_WRITE    0xE1u

typedef enum BC250_IH_STATUS {
    BC250_IH_OK = 0,
    BC250_IH_BAD_ARGUMENT,
    BC250_IH_MISALIGNED_ADDRESS,
    BC250_IH_INVALID_RING_SIZE,
    BC250_IH_BUFFER_OVERFLOW
} BC250_IH_STATUS;

typedef struct BC250_IH_VECTOR_ENTRY {
    uint32_t ClientId;
    uint32_t SrcId;
    uint32_t RingId;
    uint32_t Vmid;
    uint64_t Timestamp;
    uint32_t Pasid;
    uint32_t ContextData;
} BC250_IH_VECTOR_ENTRY;

typedef struct BC250_IH_RING_PROPERTIES {
    uint64_t RingGpuAddress;
    uint32_t RingSizeDwords;
    uint64_t WptrGpuAddress;
    uint32_t UseDoorbell;
    uint32_t DoorbellIndex;
} BC250_IH_RING_PROPERTIES;

typedef struct BC250_IH_STATE {
    uint32_t RptrDwords;
    uint32_t WptrDwords;
    uint32_t RingSizeDwords;
    uint32_t OverflowCount;
    uint32_t ProcessedCount;
    uint32_t SdmaFenceEvents;
    uint32_t ComputeFenceEvents;
} BC250_IH_STATE;

BC250_IH_STATUS Bc250IhInit(
    BC250_IH_STATE *State,
    const BC250_IH_RING_PROPERTIES *Props);

BC250_IH_STATUS Bc250IhDecodeEntry(
    const uint32_t *RawDwords,
    BC250_IH_VECTOR_ENTRY *Entry);

BC250_IH_STATUS Bc250IhEncodeEntry(
    const BC250_IH_VECTOR_ENTRY *Entry,
    uint32_t *RawDwords);

int Bc250IhCheckInterruptNeeded(
    const BC250_IH_STATE *State,
    uint32_t CurrentWptr);

BC250_IH_STATUS Bc250IhProcessRing(
    BC250_IH_STATE *State,
    const uint32_t *RingBuffer,
    uint32_t CurrentWptr,
    BC250_IH_VECTOR_ENTRY *OutEntries,
    uint32_t MaxEntries,
    uint32_t *OutCount);

const char *Bc250IhStatusName(BC250_IH_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
