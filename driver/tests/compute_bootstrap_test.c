#include "../core/bc250_compute_bootstrap.h"

#include <stdio.h>

#define CHECK(Expression) do { \
    if (!(Expression)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #Expression); \
        return 1; \
    } \
} while (0)

static const BC250_CP_FIRMWARE_ENTRY g_manifest[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT] = {
    { BC250_PSP_FW_CP_PFP, 0x00012000u },
    { BC250_PSP_FW_CP_ME, 0x00014000u },
    { BC250_PSP_FW_CP_CE, 0x00010000u },
    { BC250_PSP_FW_CP_MEC, 0x00030000u },
    { BC250_PSP_FW_CP_MEC_JT1, 0x00008000u }
};

static BC250_CP_BOOTSTRAP_STATUS PrepareSealed(
    BC250_CP_BOOTSTRAP *Bootstrap,
    BC250_VRAM_STATUS *VramStatus)
{
    BC250_VRAM_ADDRESS_SPACE space;
    BC250_VRAM_TOKEN platform;
    BC250_CP_BOOTSTRAP_STATUS status;

    status = Bc250CpBootstrapInit(
        Bootstrap, 0x20000000ull, 0x10000000ull, VramStatus);
    if (status != BC250_CP_BOOTSTRAP_OK) return status;
    status = Bc250CpBootstrapReserveInherited(
        Bootstrap, 0x1FC00000ull, 0x00400000ull, 0x00100000ull,
        BC250_VRAM_ROLE_PLATFORM_RESERVED, BC250_VRAM_REGION_PERSISTENT,
        &platform, VramStatus);
    if (status != BC250_CP_BOOTSTRAP_OK) return status;
    space.TotalBytes = 0x20000000ull;
    space.CpuVisibleBytes = 0x10000000ull;
    space.VramMcStart = 0x000000F400000000ull;
    space.VramMcEnd = 0x000000F41FFFFFFFull;
    space.VramBaseOffset = 0x0000000450000000ull;
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;
    space.SnapshotGeneration = 17;
    return Bc250CpBootstrapSealInventory(Bootstrap, &space, VramStatus);
}

int main(void)
{
    BC250_CP_BOOTSTRAP bootstrap, changed, changedSnapshot, badOrder, noSpace,
        badSnapshot;
    BC250_CP_BOOTSTRAP_POLICY policy;
    BC250_CP_BOOTSTRAP_REPORT report;
    BC250_CP_FIRMWARE_ENTRY manifest[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT];
    BC250_VRAM_ADDRESS_SPACE space;
    BC250_VRAM_TOKEN token;
    BC250_VRAM_REGION region;
    BC250_VRAM_STATUS vramStatus;
    BC250_ADDRESS_STATUS addressStatus;
    BC250_PSP_CONTRACT_STATUS pspStatus;
    uint32_t i;

    policy.TmrBytes = 0x00400000ull;
    policy.TmrAlignment = 0x00100000ull;
    policy.FirmwareAlignment = 0x00001000ull;
    CHECK(PrepareSealed(&bootstrap, &vramStatus) == BC250_CP_BOOTSTRAP_OK);
    CHECK(Bc250CpBootstrapReserveInherited(
        &bootstrap, 0, 0x1000, 0x1000, BC250_VRAM_ROLE_RING, 0,
        &token, &vramStatus) == BC250_CP_BOOTSTRAP_WRONG_STATE);
    CHECK(Bc250CpBootstrapBuildPreflight(
        &bootstrap, &policy, g_manifest, BC250_CP_BOOTSTRAP_FIRMWARE_COUNT,
        &report, &vramStatus, &addressStatus, &pspStatus) ==
        BC250_CP_BOOTSTRAP_OK);
    CHECK(bootstrap.State == BC250_CP_BOOTSTRAP_PREFLIGHT_READY);
    CHECK(report.SnapshotGeneration == 17u);
    CHECK(report.FirmwareCount == BC250_CP_BOOTSTRAP_FIRMWARE_COUNT);
    CHECK(report.TmrPayload.Dwords[0] == 0x1F800000u);
    CHECK(report.TmrPayload.Dwords[1] == 0x000000F4u);
    CHECK(report.TmrPayload.Dwords[2] == 0x00400000u);
    CHECK(Bc250VramGetRegion(
        &bootstrap.Allocator, report.FirmwareStagingToken, &region) ==
        BC250_VRAM_OK);
    CHECK(region.Role == BC250_VRAM_ROLE_FIRMWARE);
    CHECK(region.Length == 0x00030000ull);
    CHECK((region.Flags & (BC250_VRAM_ALLOC_CPU_VISIBLE |
                           BC250_VRAM_REGION_PERSISTENT)) ==
          (BC250_VRAM_ALLOC_CPU_VISIBLE | BC250_VRAM_REGION_PERSISTENT));
    for (i = 0; i < BC250_CP_BOOTSTRAP_FIRMWARE_COUNT; ++i) {
        CHECK(report.FirmwarePayloads[i].Dwords[0] == region.Offset);
        CHECK(report.FirmwarePayloads[i].Dwords[1] == 0x000000F4u);
        CHECK(report.FirmwarePayloads[i].Dwords[2] == g_manifest[i].PayloadBytes);
        CHECK(report.FirmwarePayloads[i].Dwords[3] == g_manifest[i].FirmwareType);
    }
    CHECK(Bc250CpBootstrapBuildPreflight(
        &bootstrap, &policy, g_manifest, BC250_CP_BOOTSTRAP_FIRMWARE_COUNT,
        &report, &vramStatus, &addressStatus, &pspStatus) ==
        BC250_CP_BOOTSTRAP_WRONG_STATE);

    CHECK(PrepareSealed(&changed, &vramStatus) == BC250_CP_BOOTSTRAP_OK);
    CHECK(Bc250VramReserveFixed(
        &changed.Allocator, 0, 0x1000, 0x1000,
        BC250_VRAM_ROLE_TEST_BUFFER, 0, &token) == BC250_VRAM_OK);
    CHECK(Bc250CpBootstrapBuildPreflight(
        &changed, &policy, g_manifest, BC250_CP_BOOTSTRAP_FIRMWARE_COUNT,
        &report, &vramStatus, &addressStatus, &pspStatus) ==
        BC250_CP_BOOTSTRAP_INVENTORY_CHANGED);

    CHECK(PrepareSealed(&changedSnapshot, &vramStatus) ==
        BC250_CP_BOOTSTRAP_OK);
    changedSnapshot.AddressSpace.VramBaseOffset += 0x1000ull;
    CHECK(Bc250CpBootstrapBuildPreflight(
        &changedSnapshot, &policy, g_manifest,
        BC250_CP_BOOTSTRAP_FIRMWARE_COUNT, &report, &vramStatus,
        &addressStatus, &pspStatus) == BC250_CP_BOOTSTRAP_SNAPSHOT_CHANGED);

    for (i = 0; i < BC250_CP_BOOTSTRAP_FIRMWARE_COUNT; ++i) {
        manifest[i] = g_manifest[i];
    }
    manifest[3] = g_manifest[4];
    manifest[4] = g_manifest[3];
    CHECK(PrepareSealed(&badOrder, &vramStatus) == BC250_CP_BOOTSTRAP_OK);
    CHECK(Bc250CpBootstrapBuildPreflight(
        &badOrder, &policy, manifest, BC250_CP_BOOTSTRAP_FIRMWARE_COUNT,
        &report, &vramStatus, &addressStatus, &pspStatus) ==
        BC250_CP_BOOTSTRAP_MANIFEST_REJECTED);
    CHECK(badOrder.Allocator.RegionCount == 1u);

    for (i = 0; i < BC250_CP_BOOTSTRAP_FIRMWARE_COUNT; ++i) {
        manifest[i] = g_manifest[i];
    }
    manifest[3].PayloadBytes = 0x10000001u;
    CHECK(PrepareSealed(&noSpace, &vramStatus) == BC250_CP_BOOTSTRAP_OK);
    CHECK(Bc250CpBootstrapBuildPreflight(
        &noSpace, &policy, manifest, BC250_CP_BOOTSTRAP_FIRMWARE_COUNT,
        &report, &vramStatus, &addressStatus, &pspStatus) ==
        BC250_CP_BOOTSTRAP_ALLOCATION_REJECTED);
    CHECK(vramStatus == BC250_VRAM_NO_SPACE);
    CHECK(noSpace.State == BC250_CP_BOOTSTRAP_INVENTORY_SEALED);
    CHECK(noSpace.Allocator.RegionCount == 1u);
    CHECK(noSpace.Allocator.NextGeneration == noSpace.SealedNextGeneration);

    CHECK(Bc250CpBootstrapInit(
        &badSnapshot, 0x20000000ull, 0x10000000ull, &vramStatus) ==
        BC250_CP_BOOTSTRAP_OK);
    space.TotalBytes = 0x20000000ull;
    space.CpuVisibleBytes = 0x10000000ull;
    space.VramMcStart = 0x000000F400000000ull;
    space.VramMcEnd = 0x000000F41FFFFFFFull;
    space.VramBaseOffset = 0x0000000450000000ull;
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED &
        ~BC250_ADDR_EVIDENCE_FB_OFFSET_RUNTIME;
    space.SnapshotGeneration = 18;
    CHECK(Bc250CpBootstrapSealInventory(
        &badSnapshot, &space, &vramStatus) ==
        BC250_CP_BOOTSTRAP_SNAPSHOT_REJECTED);
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;
    space.VramBaseOffset = UINT64_MAX - 0x1000u;
    CHECK(Bc250CpBootstrapSealInventory(
        &badSnapshot, &space, &vramStatus) ==
        BC250_CP_BOOTSTRAP_SNAPSHOT_REJECTED);

    printf("COMPUTE BOOTSTRAP: OK (inventario selado + TMR + 5 payloads CP)\n");
    printf("Preflight atomico; zero transporte PSP; zero escrita em hardware.\n");
    printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    return 0;
}
