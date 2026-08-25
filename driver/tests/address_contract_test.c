#include "../core/bc250_address_contract.h"

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
    BC250_VRAM_ADDRESS_BINDING binding;
    BC250_PSP_TMR_PLAN plan;
    BC250_VRAM_TOKEN platform, tmr, weakAlignment, temporary, stale;
    BC250_ADDRESS_STATUS status;

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

    space.TotalBytes = 0x20000000ull;
    space.CpuVisibleBytes = 0x10000000ull;
    space.VramMcStart = 0x000000F400000000ull;
    space.VramMcEnd = 0x000000F41FFFFFFFull;
    space.VramBaseOffset = 0x0000000450000000ull;
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;
    space.SnapshotGeneration = 7;

    status = Bc250BuildPspTmrPlan(
        &allocator, tmr, &space, 0x00400000ull, 0x00100000ull, &plan);
    CHECK(status == BC250_ADDRESS_OK);
    CHECK(plan.Binding.Offset == 0x1F800000ull);
    CHECK(plan.Binding.McAddress == 0x000000F41F800000ull);
    CHECK(plan.Binding.SystemPhysicalAddress == 0x000000046F800000ull);
    CHECK(plan.Binding.Alignment == 0x00100000ull);
    CHECK(plan.Binding.CpuVisible == 0);
    CHECK(plan.Binding.NaturallyAligned == 1);
    CHECK(plan.Binding.SnapshotGeneration == 7);
    CHECK(plan.BufferSize == 0x00400000u);
    CHECK(plan.TmrFlags == BC250_PSP_TMR_VIRT_PHY_ADDR);

    space.EvidenceFlags &= ~BC250_ADDR_EVIDENCE_FB_OFFSET_RUNTIME;
    CHECK(Bc250BuildPspTmrPlan(
        &allocator, tmr, &space, 0x00400000ull, 0x00100000ull, &plan) ==
        BC250_ADDRESS_MISSING_EVIDENCE);
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;

    space.VramMcEnd--;
    CHECK(Bc250BindVramRegion(&allocator, tmr, &space, &binding) ==
          BC250_ADDRESS_TOPOLOGY_MISMATCH);
    space.VramMcEnd++;

    CHECK(Bc250BuildPspTmrPlan(
        &allocator, tmr, &space, 0x00800000ull, 0x00100000ull, &plan) ==
        BC250_ADDRESS_SIZE_MISMATCH);
    CHECK(Bc250BuildPspTmrPlan(
        &allocator, tmr, &space, 0x00400000ull, 0x00300000ull, &plan) ==
        BC250_ADDRESS_BAD_ARGUMENT);

    CHECK(Bc250VramReserveFixed(
        &allocator, 0x01000000ull, 0x00100000ull, 0x00001000ull,
        BC250_VRAM_ROLE_TMR, BC250_VRAM_REGION_PERSISTENT,
        &weakAlignment) == BC250_VRAM_OK);
    CHECK(Bc250BuildPspTmrPlan(
        &allocator, weakAlignment, &space, 0x00100000ull, 0x00100000ull,
        &plan) == BC250_ADDRESS_MISALIGNED);

    CHECK(Bc250VramAllocate(
        &allocator, 0x00100000ull, 0x00100000ull,
        BC250_VRAM_ROLE_TEST_BUFFER, 0, &temporary) == BC250_VRAM_OK);
    CHECK(Bc250BuildPspTmrPlan(
        &allocator, temporary, &space, 0x00100000ull, 0x00100000ull, &plan) ==
        BC250_ADDRESS_WRONG_ROLE);
    stale = temporary;
    CHECK(Bc250VramRelease(&allocator, temporary) == BC250_VRAM_OK);
    CHECK(Bc250BindVramRegion(&allocator, stale, &space, &binding) ==
          BC250_ADDRESS_STALE_TOKEN);

    CHECK(Bc250VramAllocate(
        &allocator, 0x00100000ull, 0x00100000ull,
        BC250_VRAM_ROLE_TMR, 0, &temporary) == BC250_VRAM_OK);
    CHECK(Bc250BuildPspTmrPlan(
        &allocator, temporary, &space, 0x00100000ull, 0x00100000ull, &plan) ==
        BC250_ADDRESS_NOT_PERSISTENT);

    printf("ADDRESS CONTRACT: OK (token -> MC/PA + TMR fail-closed)\n");
    printf("MC e PA vieram do mesmo offset e snapshot sintetico.\n");
    printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    return 0;
}
