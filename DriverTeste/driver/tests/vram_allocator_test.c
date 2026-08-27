#include "../core/bc250_vram_allocator.h"

#include <stdio.h>

static int g_Failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #expr); \
        ++g_Failures; \
    } \
} while (0)

static void TestCapturedTopology(void)
{
    BC250_VRAM_ALLOCATOR a;
    BC250_VRAM_TOKEN scanout, pageTable, platformTop, tmr, cpuBuffer;
    BC250_VRAM_REGION region;

    CHECK(Bc250VramAllocatorInit(&a, UINT64_C(0x20000000), UINT64_C(0x10000000)) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(&a, 0, UINT64_C(0x00800000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_SCANOUT, BC250_VRAM_REGION_PERSISTENT, &scanout) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(&a, UINT64_C(0x0FF00000), UINT64_C(0x00100000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_PAGE_TABLE, BC250_VRAM_REGION_PERSISTENT, &pageTable) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(&a, UINT64_C(0x1FC00000), UINT64_C(0x00400000), UINT64_C(0x100000),
          BC250_VRAM_ROLE_PLATFORM_RESERVED, BC250_VRAM_REGION_PERSISTENT, &platformTop) == BC250_VRAM_OK);

    CHECK(Bc250VramAllocate(&a, UINT64_C(0x00400000), UINT64_C(0x100000),
          BC250_VRAM_ROLE_TMR, BC250_VRAM_ALLOC_TOP_DOWN, &tmr) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&a, tmr, &region) == BC250_VRAM_OK);
    CHECK(region.Offset == UINT64_C(0x1F800000));
    CHECK(region.Offset >= a.CpuVisibleBytes);

    CHECK(Bc250VramAllocate(&a, UINT64_C(0x00400000), UINT64_C(0x100000),
          BC250_VRAM_ROLE_TEST_BUFFER,
          BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE,
          &cpuBuffer) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&a, cpuBuffer, &region) == BC250_VRAM_OK);
    CHECK(region.Offset == UINT64_C(0x0FB00000));
    CHECK(region.Offset + region.Length <= a.CpuVisibleBytes);
    CHECK(Bc250VramValidate(&a) == BC250_VRAM_OK);

    CHECK(Bc250VramRelease(&a, scanout) == BC250_VRAM_PROTECTED);
    CHECK(Bc250VramRelease(&a, tmr) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&a, tmr, &region) == BC250_VRAM_STALE_TOKEN);
    CHECK(Bc250VramValidate(&a) == BC250_VRAM_OK);
}

static void TestFailures(void)
{
    BC250_VRAM_ALLOCATOR a;
    BC250_VRAM_TOKEN one, two;

    CHECK(Bc250VramAllocatorInit(NULL, 1, 1) == BC250_VRAM_BAD_ARGUMENT);
    CHECK(Bc250VramAllocatorInit(&a, UINT64_C(0x10000), UINT64_C(0x20000)) == BC250_VRAM_BAD_ARGUMENT);
    CHECK(Bc250VramAllocatorInit(&a, UINT64_C(0x10000), UINT64_C(0x8000)) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(&a, UINT64_C(0x1000), UINT64_C(0x2000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_RING, 0, &one) == BC250_VRAM_OK);
    CHECK(Bc250VramReserveFixed(&a, UINT64_C(0x2000), UINT64_C(0x1000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_FIRMWARE, 0, &two) == BC250_VRAM_OVERLAP);
    CHECK(Bc250VramReserveFixed(&a, UINT64_C(0x3800), UINT64_C(0x1000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_FIRMWARE, 0, &two) == BC250_VRAM_MISALIGNED);
    CHECK(Bc250VramReserveFixed(&a, UINT64_C(0xF000), UINT64_C(0x2000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_FIRMWARE, 0, &two) == BC250_VRAM_OUT_OF_RANGE);
    CHECK(two.Slot == 0 && two.Generation == 0);
    CHECK(Bc250VramReserveFixed(&a, UINT64_C(0x8000), UINT64_C(0x1000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_FIRMWARE, BC250_VRAM_ALLOC_CPU_VISIBLE, &two) == BC250_VRAM_OUT_OF_RANGE);
    CHECK(Bc250VramReserveFixed(&a, UINT64_C(0x4000), UINT64_C(0x1000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_FIRMWARE, BC250_VRAM_ALLOC_TOP_DOWN, &two) == BC250_VRAM_BAD_ARGUMENT);
    CHECK(Bc250VramReserveFixed(&a, 0, UINT64_MAX, 3,
          BC250_VRAM_ROLE_FIRMWARE, 0, &two) == BC250_VRAM_BAD_ARGUMENT);
    CHECK(Bc250VramAllocate(&a, UINT64_C(0x9000), UINT64_C(0x1000),
          BC250_VRAM_ROLE_TEST_BUFFER, BC250_VRAM_ALLOC_CPU_VISIBLE, &two) == BC250_VRAM_NO_SPACE);
    CHECK(Bc250VramRelease(&a, one) == BC250_VRAM_OK);
    CHECK(Bc250VramRelease(&a, one) == BC250_VRAM_STALE_TOKEN);
}

int main(void)
{
    TestCapturedTopology();
    TestFailures();
    if (g_Failures != 0) {
        printf("VRAM ALLOCATOR: %d FALHAS\n", g_Failures);
        return 1;
    }
    printf("VRAM ALLOCATOR: OK (reservas + CPU visibility + fail-closed)\n");
    printf("PA e MC/GPUVA continuam fora desta camada.\n");
    printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    return 0;
}
