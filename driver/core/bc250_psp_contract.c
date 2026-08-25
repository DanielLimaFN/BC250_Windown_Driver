#include "bc250_psp_contract.h"

#include <string.h>

#define BC250_UINT32_MAX_VALUE ((uint32_t)~(uint32_t)0)

static int Bc250PspFirmwareTypeValid(uint32_t FirmwareType)
{
    return FirmwareType >= BC250_PSP_FW_CP_ME &&
           FirmwareType <= BC250_PSP_FW_CP_MEC_JT1;
}

BC250_PSP_CONTRACT_STATUS
Bc250PspBuildSetupTmrPayload(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    uint64_t RequiredSize,
    uint64_t RequiredAlignment,
    BC250_PSP_TMR_PLAN *Plan,
    BC250_PSP_SETUP_TMR_PAYLOAD *Payload,
    BC250_ADDRESS_STATUS *AddressStatus)
{
    BC250_ADDRESS_STATUS addressStatus;

    if (Plan != NULL) memset(Plan, 0, sizeof(*Plan));
    if (Payload != NULL) memset(Payload, 0, sizeof(*Payload));
    if (AddressStatus != NULL) *AddressStatus = BC250_ADDRESS_BAD_ARGUMENT;
    if (Plan == NULL || Payload == NULL) {
        return BC250_PSP_CONTRACT_BAD_ARGUMENT;
    }

    addressStatus = Bc250BuildPspTmrPlan(
        Allocator, Token, AddressSpace, RequiredSize, RequiredAlignment, Plan);
    if (AddressStatus != NULL) *AddressStatus = addressStatus;
    if (addressStatus != BC250_ADDRESS_OK) {
        return BC250_PSP_CONTRACT_ADDRESS_REJECTED;
    }

    Payload->Dwords[0] = (uint32_t)Plan->Binding.McAddress;
    Payload->Dwords[1] = (uint32_t)(Plan->Binding.McAddress >> 32);
    Payload->Dwords[2] = Plan->BufferSize;
    Payload->Dwords[3] = Plan->TmrFlags;
    Payload->Dwords[4] = (uint32_t)Plan->Binding.SystemPhysicalAddress;
    Payload->Dwords[5] =
        (uint32_t)(Plan->Binding.SystemPhysicalAddress >> 32);
    return BC250_PSP_CONTRACT_OK;
}

BC250_PSP_CONTRACT_STATUS
Bc250PspBuildLoadFwPayload(
    const BC250_VRAM_ALLOCATOR *Allocator,
    BC250_VRAM_TOKEN Token,
    const BC250_VRAM_ADDRESS_SPACE *AddressSpace,
    BC250_PSP_FW_TYPE FirmwareType,
    uint64_t PayloadBytes,
    BC250_PSP_LOAD_FW_PLAN *Plan,
    BC250_PSP_LOAD_FW_PAYLOAD *Payload,
    BC250_ADDRESS_STATUS *AddressStatus)
{
    BC250_ADDRESS_STATUS addressStatus;

    if (Plan != NULL) memset(Plan, 0, sizeof(*Plan));
    if (Payload != NULL) memset(Payload, 0, sizeof(*Payload));
    if (AddressStatus != NULL) *AddressStatus = BC250_ADDRESS_BAD_ARGUMENT;
    if (Plan == NULL || Payload == NULL || PayloadBytes == 0 ||
        PayloadBytes > BC250_UINT32_MAX_VALUE) {
        return BC250_PSP_CONTRACT_BAD_ARGUMENT;
    }
    if (!Bc250PspFirmwareTypeValid((uint32_t)FirmwareType)) {
        return BC250_PSP_CONTRACT_INVALID_FW_TYPE;
    }

    addressStatus = Bc250BindVramRegion(
        Allocator, Token, AddressSpace, &Plan->Binding);
    if (AddressStatus != NULL) *AddressStatus = addressStatus;
    if (addressStatus != BC250_ADDRESS_OK) {
        return BC250_PSP_CONTRACT_ADDRESS_REJECTED;
    }
    if (Plan->Binding.Role != BC250_VRAM_ROLE_FIRMWARE) {
        memset(Plan, 0, sizeof(*Plan));
        return BC250_PSP_CONTRACT_WRONG_ROLE;
    }
    if (!Plan->Binding.CpuVisible) {
        memset(Plan, 0, sizeof(*Plan));
        return BC250_PSP_CONTRACT_NOT_CPU_VISIBLE;
    }
    if (PayloadBytes > Plan->Binding.Length) {
        memset(Plan, 0, sizeof(*Plan));
        return BC250_PSP_CONTRACT_SIZE_MISMATCH;
    }

    Plan->FirmwareType = (uint32_t)FirmwareType;
    Plan->PayloadBytes = (uint32_t)PayloadBytes;
    Payload->Dwords[0] = (uint32_t)Plan->Binding.McAddress;
    Payload->Dwords[1] = (uint32_t)(Plan->Binding.McAddress >> 32);
    Payload->Dwords[2] = Plan->PayloadBytes;
    Payload->Dwords[3] = Plan->FirmwareType;
    if (AddressStatus != NULL) *AddressStatus = BC250_ADDRESS_OK;
    return BC250_PSP_CONTRACT_OK;
}

const char *Bc250PspContractStatusName(BC250_PSP_CONTRACT_STATUS Status)
{
    switch (Status) {
    case BC250_PSP_CONTRACT_OK: return "ok";
    case BC250_PSP_CONTRACT_BAD_ARGUMENT: return "bad_argument";
    case BC250_PSP_CONTRACT_ADDRESS_REJECTED: return "address_rejected";
    case BC250_PSP_CONTRACT_WRONG_ROLE: return "wrong_role";
    case BC250_PSP_CONTRACT_NOT_CPU_VISIBLE: return "not_cpu_visible";
    case BC250_PSP_CONTRACT_SIZE_MISMATCH: return "size_mismatch";
    case BC250_PSP_CONTRACT_INVALID_FW_TYPE: return "invalid_fw_type";
    case BC250_PSP_CONTRACT_INVALID_PLAN: return "invalid_plan";
    default: return "unknown";
    }
}
