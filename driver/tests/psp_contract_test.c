#include "../core/bc250_psp_contract.h"

#include <stdio.h>

#define CHECK(Expression) do { \
    if (!(Expression)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #Expression); \
        return 1; \
    } \
} while (0)

int main(void)
{
    BC250_VRAM_ALLOCATOR allocator;
    BC250_VRAM_ADDRESS_SPACE space;
    BC250_VRAM_TOKEN platform, tmr, firmware, hiddenFirmware, wrongRole;
    BC250_PSP_TMR_PLAN tmrPlan;
    BC250_PSP_SETUP_TMR_PAYLOAD tmrPayload;
    BC250_PSP_LOAD_FW_PLAN fwPlan;
    BC250_PSP_LOAD_FW_PAYLOAD fwPayload;
    BC250_ADDRESS_STATUS addressStatus;

    CHECK(Bc250VramAllocatorInit(
        &allocator, 0x20000000ull, 0x10000000ull) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(
        &allocator, 0x1FC00000ull, 0x00400000ull, 0x00100000ull,
        BC250_VRAM_ROLE_PLATFORM_RESERVED,
        BC250_VRAM_REGION_PERSISTENT, &platform) == BC250_VRAM_OK);
    CHECK(Bc250VramAllocate(
        &allocator, 0x00400000ull, 0x00100000ull, BC250_VRAM_ROLE_TMR,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_REGION_PERSISTENT,
        &tmr) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(
        &allocator, 0x00E00000ull, 0x00200000ull, 0x00001000ull,
        BC250_VRAM_ROLE_FIRMWARE, BC250_VRAM_ALLOC_CPU_VISIBLE,
        &firmware) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(
        &allocator, 0x1F000000ull, 0x00100000ull, 0x00001000ull,
        BC250_VRAM_ROLE_FIRMWARE, 0, &hiddenFirmware) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(
        &allocator, 0x00D00000ull, 0x00100000ull, 0x00001000ull,
        BC250_VRAM_ROLE_RING, BC250_VRAM_ALLOC_CPU_VISIBLE,
        &wrongRole) == BC250_VRAM_OK);

    space.TotalBytes = 0x20000000ull;
    space.CpuVisibleBytes = 0x10000000ull;
    space.VramMcStart = 0x000000F400000000ull;
    space.VramMcEnd = 0x000000F41FFFFFFFull;
    space.VramBaseOffset = 0x0000000450000000ull;
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;
    space.SnapshotGeneration = 9;

    CHECK(Bc250PspBuildSetupTmrPayload(
        &allocator, tmr, &space, 0x00400000ull, 0x00100000ull,
        &tmrPlan, &tmrPayload, &addressStatus) ==
          BC250_PSP_CONTRACT_OK);
    CHECK(addressStatus == BC250_ADDRESS_OK);
    CHECK(tmrPayload.Dwords[0] == 0x1F800000u);
    CHECK(tmrPayload.Dwords[1] == 0x000000F4u);
    CHECK(tmrPayload.Dwords[2] == 0x00400000u);
    CHECK(tmrPayload.Dwords[3] == 0x00000002u);
    CHECK(tmrPayload.Dwords[4] == 0x6F800000u);
    CHECK(tmrPayload.Dwords[5] == 0x00000004u);
    CHECK(Bc250PspBuildLoadFwPayload(
        &allocator, firmware, &space, BC250_PSP_FW_CP_MEC_JT1,
        0x00150000ull, &fwPlan, &fwPayload, &addressStatus) ==
        BC250_PSP_CONTRACT_OK);
    CHECK(addressStatus == BC250_ADDRESS_OK);
    CHECK(fwPayload.Dwords[0] == 0x00E00000u);
    CHECK(fwPayload.Dwords[1] == 0x000000F4u);
    CHECK(fwPayload.Dwords[2] == 0x00150000u);
    CHECK(fwPayload.Dwords[3] == BC250_PSP_FW_CP_MEC_JT1);

    CHECK(Bc250PspBuildLoadFwPayload(
        &allocator, hiddenFirmware, &space, BC250_PSP_FW_CP_MEC,
        0x1000, &fwPlan, &fwPayload, &addressStatus) ==
        BC250_PSP_CONTRACT_NOT_CPU_VISIBLE);
    CHECK(Bc250PspBuildLoadFwPayload(
        &allocator, wrongRole, &space, BC250_PSP_FW_CP_ME,
        0x1000, &fwPlan, &fwPayload, &addressStatus) ==
        BC250_PSP_CONTRACT_WRONG_ROLE);
    CHECK(Bc250PspBuildLoadFwPayload(
        &allocator, firmware, &space, (BC250_PSP_FW_TYPE)6,
        0x1000, &fwPlan, &fwPayload, &addressStatus) ==
        BC250_PSP_CONTRACT_INVALID_FW_TYPE);
    CHECK(Bc250PspBuildLoadFwPayload(
        &allocator, firmware, &space, BC250_PSP_FW_CP_PFP,
        0, &fwPlan, &fwPayload, &addressStatus) ==
        BC250_PSP_CONTRACT_BAD_ARGUMENT);
    CHECK(Bc250PspBuildLoadFwPayload(
        &allocator, firmware, &space, BC250_PSP_FW_CP_CE,
        0x00300000ull, &fwPlan, &fwPayload, &addressStatus) ==
        BC250_PSP_CONTRACT_SIZE_MISMATCH);

    printf("PSP CONTRACT: OK (TMR + CP firmware payloads tipados)\n");
    printf("MEC e MEC_JT1 permanecem tipos distintos; nenhum submit ocorreu.\n");
    printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    return 0;
}
