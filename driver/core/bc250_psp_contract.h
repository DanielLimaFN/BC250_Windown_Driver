#ifndef BC250_PSP_CONTRACT_H
#define BC250_PSP_CONTRACT_H

#include "bc250_address_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_PSP_SETUP_TMR_DWORDS 6u
#define BC250_PSP_LOAD_FW_DWORDS   4u

typedef enum BC250_PSP_FW_TYPE {
    BC250_PSP_FW_CP_ME = 1,
    BC250_PSP_FW_CP_PFP = 2,
    BC250_PSP_FW_CP_CE = 3,
    BC250_PSP_FW_CP_MEC = 4,
    BC250_PSP_FW_CP_MEC_JT1 = 5
} BC250_PSP_FW_TYPE;

typedef enum BC250_PSP_CONTRACT_STATUS {
    BC250_PSP_CONTRACT_OK = 0,
    BC250_PSP_CONTRACT_BAD_ARGUMENT,
    BC250_PSP_CONTRACT_ADDRESS_REJECTED,
    BC250_PSP_CONTRACT_WRONG_ROLE,
    BC250_PSP_CONTRACT_NOT_CPU_VISIBLE,
    BC250_PSP_CONTRACT_SIZE_MISMATCH,
    BC250_PSP_CONTRACT_INVALID_FW_TYPE,
    BC250_PSP_CONTRACT_INVALID_PLAN
} BC250_PSP_CONTRACT_STATUS;

typedef struct BC250_PSP_SETUP_TMR_PAYLOAD {
    uint32_t Dwords[BC250_PSP_SETUP_TMR_DWORDS];
} BC250_PSP_SETUP_TMR_PAYLOAD;

typedef struct BC250_PSP_LOAD_FW_PLAN {
    BC250_VRAM_ADDRESS_BINDING Binding;
    uint32_t FirmwareType;
    uint32_t PayloadBytes;
    uint32_t Reserved;
    uint32_t Reserved2;
} BC250_PSP_LOAD_FW_PLAN;

typedef struct BC250_PSP_LOAD_FW_PAYLOAD {
    uint32_t Dwords[BC250_PSP_LOAD_FW_DWORDS];
} BC250_PSP_LOAD_FW_PAYLOAD;

BC250_PSP_CONTRACT_STATUS
Bc250PspBuildSetupTmrPayload(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    uint64_t RequiredSize,
    uint64_t RequiredAlignment,
    BC250_PSP_TMR_PLAN *Plan,
    BC250_PSP_SETUP_TMR_PAYLOAD *Payload,
    BC250_ADDRESS_STATUS *AddressStatus);

BC250_PSP_CONTRACT_STATUS
Bc250PspBuildLoadFwPayload(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    BC250_PSP_FW_TYPE FirmwareType,
    uint64_t PayloadBytes,
    BC250_PSP_LOAD_FW_PLAN *Plan,
    BC250_PSP_LOAD_FW_PAYLOAD *Payload,
    BC250_ADDRESS_STATUS *AddressStatus);

const char *Bc250PspContractStatusName(BC250_PSP_CONTRACT_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
