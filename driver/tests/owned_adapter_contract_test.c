#include "../core/bc250_owned_adapter_contract.h"

#include <stdio.h>

#define CHECK(Expression) do { \
    if (!(Expression)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #Expression); \
        return 1; \
    } \
} while (0)

static const BC250_RESOLVED_BAR_RESOURCE g_bars[] = {
    { 5u, 1u, 0u, 0u, 0xFE800000ull, 0x00080000ull },
    { 0u, 1u, 1u, 0u, 0xC0000000ull, 0x10000000ull },
    { 2u, 2u, 0u, 0u, 0xD0000000ull, 0x00200000ull }
};

static const BC250_INHERITED_VRAM_RESERVATION g_reservations[] = {
    { 0x1FC00000ull, 0x00400000ull, 0x00100000ull,
      BC250_VRAM_ROLE_PLATFORM_RESERVED, BC250_VRAM_REGION_PERSISTENT },
    { 0x0FC00000ull, 0x00400000ull, 0x00001000ull,
      BC250_VRAM_ROLE_PAGE_TABLE,
      BC250_VRAM_ALLOC_CPU_VISIBLE | BC250_VRAM_REGION_PERSISTENT }
};

static BC250_OWNED_ADAPTER_INPUT ValidInput(void)
{
    BC250_OWNED_ADAPTER_INPUT input;
    input.ResolvedBars = g_bars;
    input.ResolvedBarCount = sizeof(g_bars) / sizeof(g_bars[0]);
    input.VramApertureBarIndex = 0;
    input.OwnershipEvidenceFlags = BC250_OWNERSHIP_REQUIRED;
    input.VramTotalBytes = 0x20000000ull;
    input.VramMcStart = 0x000000F400000000ull;
    input.VramMcEnd = 0x000000F41FFFFFFFull;
    input.VramBaseOffset = 0x0000000450000000ull;
    input.SnapshotGeneration = 31;
    input.Reserved = 0;
    return input;
}

int main(void)
{
    BC250_OWNED_ADAPTER_INPUT input;
    BC250_OWNED_ADAPTER_BOOTSTRAP owned;
    BC250_CP_BOOTSTRAP_POLICY policy;
    BC250_CP_BOOTSTRAP_REPORT report;
    BC250_CP_FIRMWARE_ENTRY manifest[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT] = {
        { BC250_PSP_FW_CP_PFP, 0x12000u },
        { BC250_PSP_FW_CP_ME, 0x14000u },
        { BC250_PSP_FW_CP_CE, 0x10000u },
        { BC250_PSP_FW_CP_MEC, 0x30000u },
        { BC250_PSP_FW_CP_MEC_JT1, 0x08000u }
    };
    BC250_INHERITED_VRAM_RESERVATION badReservation;
    BC250_RESOLVED_BAR_RESOURCE duplicateBars[2];
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;
    BC250_VRAM_STATUS vramStatus;
    BC250_ADDRESS_STATUS addressStatus;
    BC250_PSP_CONTRACT_STATUS pspStatus;

    input = ValidInput();
    CHECK(Bc250BuildOwnedAdapterBootstrap(
        &input, g_reservations,
        sizeof(g_reservations) / sizeof(g_reservations[0]), &owned,
        &bootstrapStatus, &vramStatus) == BC250_OWNED_ADAPTER_OK);
    CHECK(owned.CpuAperturePhysicalStart == 0xC0000000ull);
    CHECK(owned.CpuApertureBytes == 0x10000000ull);
    CHECK(owned.InheritedReservationCount == 2u);
    CHECK(owned.Bootstrap.State == BC250_CP_BOOTSTRAP_INVENTORY_SEALED);

    policy.TmrBytes = 0x00400000ull;
    policy.TmrAlignment = 0x00100000ull;
    policy.FirmwareAlignment = 0x00001000ull;
    CHECK(Bc250CpBootstrapBuildPreflight(
        &owned.Bootstrap, &policy, manifest,
        BC250_CP_BOOTSTRAP_FIRMWARE_COUNT, &report, &vramStatus,
        &addressStatus, &pspStatus) == BC250_CP_BOOTSTRAP_OK);
    CHECK(report.TmrPlan.Binding.Offset == 0x1F800000ull);
    CHECK(report.TmrPlan.Binding.McAddress == 0x000000F41F800000ull);
    CHECK(report.TmrPlan.Binding.SystemPhysicalAddress ==
        0x000000046F800000ull);

    input = ValidInput();
    input.OwnershipEvidenceFlags &= ~BC250_OWNERSHIP_EXCLUSIVE_ADAPTER;
    CHECK(Bc250BuildOwnedAdapterBootstrap(
        &input, g_reservations, 2, &owned, &bootstrapStatus, &vramStatus) ==
        BC250_OWNED_ADAPTER_MISSING_EVIDENCE);

    input = ValidInput();
    input.VramMcEnd -= 1u;
    CHECK(Bc250BuildOwnedAdapterBootstrap(
        &input, g_reservations, 2, &owned, &bootstrapStatus, &vramStatus) ==
        BC250_OWNED_ADAPTER_TOPOLOGY_REJECTED);

    input = ValidInput();
    input.VramBaseOffset = UINT64_MAX - 0x1000u;
    CHECK(Bc250BuildOwnedAdapterBootstrap(
        &input, g_reservations, 2, &owned, &bootstrapStatus, &vramStatus) ==
        BC250_OWNED_ADAPTER_TOPOLOGY_REJECTED);

    duplicateBars[0] = g_bars[1];
    duplicateBars[1] = g_bars[1];
    input = ValidInput();
    input.ResolvedBars = duplicateBars;
    input.ResolvedBarCount = 2;
    CHECK(Bc250BuildOwnedAdapterBootstrap(
        &input, g_reservations, 2, &owned, &bootstrapStatus, &vramStatus) ==
        BC250_OWNED_ADAPTER_APERTURE_NOT_UNIQUE);

    badReservation = g_reservations[0];
    badReservation.Flags &= ~BC250_VRAM_REGION_PERSISTENT;
    input = ValidInput();
    CHECK(Bc250BuildOwnedAdapterBootstrap(
        &input, &badReservation, 1, &owned, &bootstrapStatus, &vramStatus) ==
        BC250_OWNED_ADAPTER_RESERVATION_REJECTED);
    CHECK(owned.Bootstrap.State == BC250_CP_BOOTSTRAP_EMPTY);

    printf("OWNED ADAPTER CONTRACT: OK (PnP BAR + runtime + inventory)\n");
    printf("Ownership sintetico; zero mapa, zero submit, zero hardware.\n");
    printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    return 0;
}
