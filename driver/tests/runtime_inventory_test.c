#include "../core/bc250_runtime_inventory.h"
#include "../core/bc250_staging_lease.h"

#include <string.h>

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    volatile unsigned char *destination = (volatile unsigned char *)Destination;
    const volatile unsigned char *source = (const volatile unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

static BC250_RUNTIME_INVENTORY_INPUT g_input;
static BC250_RUNTIME_INVENTORY_REPORT g_report;
static BC250_RUNTIME_INVENTORY_INPUT g_partialInput;
static BC250_RUNTIME_INVENTORY_REPORT g_partialReport;
static BC250_CP_BOOTSTRAP g_bootstrap;
static BC250_CP_BOOTSTRAP_REPORT g_preflight;

static int Bc250RunRuntimeInventoryTest(void)
{
    BC250_CP_BOOTSTRAP_POLICY policy;
    BC250_CP_FIRMWARE_ENTRY manifest[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT];
    BC250_STAGING_LEASE_EVIDENCE stagingEvidence;
    BC250_FIRMWARE_STAGING_LEASE lease;
    BC250_VRAM_ADDRESS_SPACE space;
    BC250_VRAM_TOKEN gartToken, ptToken, ringToken, mqdToken, queueToken, token;
    BC250_VRAM_REGION region;
    BC250_VRAM_STATUS vramStatus;
    BC250_ADDRESS_STATUS addressStatus;
    BC250_PSP_CONTRACT_STATUS pspStatus;
    uint32_t i;

    memset(&g_input, 0, sizeof(g_input));
    g_input.EvidenceFlags = BC250_RUNTIME_EVIDENCE_COMPLETE;
    g_input.HasScanout = 1;
    g_input.ScanoutReservation.Offset = 0x00100000ull;
    g_input.ScanoutReservation.Length = 8294400ull;
    g_input.ScanoutReservation.Alignment = 1u;
    g_input.ScanoutReservation.Role = BC250_VRAM_ROLE_SCANOUT;
    g_input.ScanoutReservation.Flags =
        BC250_VRAM_ALLOC_CPU_VISIBLE | BC250_VRAM_REGION_PERSISTENT;

    g_input.HasDiscoveryTmr = 1;
    g_input.DiscoveryTmrEvidence.Offset = 0x1FFF0000ull;
    g_input.DiscoveryTmrEvidence.Length = 0x00010000ull;
    g_input.DiscoveryTmrEvidence.SnapshotGeneration = 42;
    g_input.DiscoveryTmrEvidence.ReadCount = 6;
    g_input.DiscoveryTmrEvidence.StablePasses = 2;

    g_input.PlatformCarveoutCount = 1;
    g_input.PlatformCarveouts[0].Offset = 0x1FC00000ull;
    g_input.PlatformCarveouts[0].Length = 0x00300000ull;
    g_input.PlatformCarveouts[0].Alignment = 0x100000ull;
    g_input.PlatformCarveouts[0].Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
    g_input.PlatformCarveouts[0].Flags = BC250_VRAM_REGION_PERSISTENT;

    CHECK(Bc250BuildRuntimeVramInventory(
        &g_input, 0x20000000ull, 0x10000000ull, 42, &g_report) ==
        BC250_RUNTIME_INVENTORY_OK);
    CHECK(g_report.ReservationCount == 3u);
    CHECK(g_report.SnapshotGeneration == 42u);
    CHECK(g_report.VramTotalBytes == 0x20000000ull);
    CHECK(g_report.CpuVisibleBytes == 0x10000000ull);
    CHECK(g_report.TotalReservedBytes == 8294400ull + 0x10000ull + 0x300000ull);
    CHECK(g_report.TotalFreeBytes ==
        0x20000000ull - g_report.TotalReservedBytes);
    CHECK(g_report.CpuVisibleReservedBytes == 8294400ull);
    CHECK(g_report.CpuVisibleFreeBytes == 0x10000000ull - 8294400ull);
    CHECK(g_report.OwnershipEvidenceFlags ==
        (BC250_OWNERSHIP_EXCLUSIVE_ADAPTER |
         BC250_OWNERSHIP_INVENTORY_COMPLETE));
    CHECK(g_report.StagingEvidenceFlags == BC250_STAGING_EVIDENCE_REQUIRED);

    {
        g_partialInput = g_input;
        g_partialInput.EvidenceFlags = BC250_RUNTIME_EVIDENCE_SCANOUT_PROBED |
            BC250_RUNTIME_EVIDENCE_DISCOVERY_TMR_PROBED;
        g_partialInput.HasDiscoveryTmr = 0;
        g_partialInput.PlatformCarveoutCount = 0;
        CHECK(Bc250BuildRuntimeVramInventory(
            &g_partialInput, 0x20000000ull, 0x10000000ull, 42,
            &g_partialReport) == BC250_RUNTIME_INVENTORY_INCOMPLETE);
        CHECK(g_partialReport.ReservationCount == 1u);
        CHECK(g_partialReport.TotalReservedBytes == 8294400ull);
        CHECK(g_partialReport.TotalFreeBytes == 0u &&
            g_partialReport.CpuVisibleFreeBytes == 0u);
        CHECK(g_partialReport.OwnershipEvidenceFlags == 0u &&
            g_partialReport.StagingEvidenceFlags == 0u);
    }

    /* Test Bootstrap integration with the completed inventory */
    CHECK(Bc250CpBootstrapInit(
        &g_bootstrap, g_report.VramTotalBytes, g_report.CpuVisibleBytes,
        &vramStatus) == BC250_CP_BOOTSTRAP_OK);
    for (i = 0; i < g_report.ReservationCount; ++i) {
        const BC250_INHERITED_VRAM_RESERVATION *res = &g_report.Reservations[i];
        CHECK(Bc250CpBootstrapReserveInherited(
            &g_bootstrap, res->Offset, res->Length, res->Alignment,
            (BC250_VRAM_ROLE)res->Role, res->Flags, &token,
            &vramStatus) == BC250_CP_BOOTSTRAP_OK);
    }

    /*
     * As alocacoes gerenciadas pelo driver precisam entrar ANTES do lacre.
     * Bc250CpBootstrapBuildPreflight compara RegionCount, NextGeneration e o
     * selo do inventario contra o que foi lacrado; qualquer alocacao feita
     * depois do lacre e' rejeitada com BC250_CP_BOOTSTRAP_INVENTORY_CHANGED.
     * O teste alocava depois de lacrar e exigia OK -- contradicao com o
     * proprio contrato.  A falha ficou escondida porque o stub de memcpy
     * deste arquivo recursava infinitamente e o processo morria com codigo
     * negativo, que o portao `if errorlevel 1` do build-contract.bat deixava
     * passar.
     */
    /* Allocate future driver-managed structures */
    CHECK(Bc250VramAllocate(
        &g_bootstrap.Allocator, 0x00400000ull, 0x100000ull,
        BC250_VRAM_ROLE_GART, 0, &gartToken) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&g_bootstrap.Allocator, gartToken, &region) ==
        BC250_VRAM_OK);
    CHECK(region.Role == BC250_VRAM_ROLE_GART);

    CHECK(Bc250VramAllocate(
        &g_bootstrap.Allocator, 0x00200000ull, 0x200000ull,
        BC250_VRAM_ROLE_PAGE_TABLE, 0, &ptToken) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&g_bootstrap.Allocator, ptToken, &region) ==
        BC250_VRAM_OK);
    CHECK(region.Role == BC250_VRAM_ROLE_PAGE_TABLE);

    CHECK(Bc250VramAllocate(
        &g_bootstrap.Allocator, 0x00080000ull, 0x1000u,
        BC250_VRAM_ROLE_RING, 0, &ringToken) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&g_bootstrap.Allocator, ringToken, &region) ==
        BC250_VRAM_OK);
    CHECK(region.Role == BC250_VRAM_ROLE_RING);

    CHECK(Bc250VramAllocate(
        &g_bootstrap.Allocator, 0x00010000ull, 0x1000u,
        BC250_VRAM_ROLE_MQD, 0, &mqdToken) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&g_bootstrap.Allocator, mqdToken, &region) ==
        BC250_VRAM_OK);
    CHECK(region.Role == BC250_VRAM_ROLE_MQD);

    CHECK(Bc250VramAllocate(
        &g_bootstrap.Allocator, 0x00040000ull, 0x1000u,
        BC250_VRAM_ROLE_COMPUTE_QUEUE, 0, &queueToken) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&g_bootstrap.Allocator, queueToken, &region) ==
        BC250_VRAM_OK);
    CHECK(region.Role == BC250_VRAM_ROLE_COMPUTE_QUEUE);

    memset(&space, 0, sizeof(space));
    space.TotalBytes = g_report.VramTotalBytes;
    space.CpuVisibleBytes = g_report.CpuVisibleBytes;
    space.VramMcStart = 0xF400000000ull;
    space.VramMcEnd = space.VramMcStart + g_report.VramTotalBytes - 1ull;
    space.VramBaseOffset = 0x4500000ull;
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;
    space.SnapshotGeneration = g_report.SnapshotGeneration;

    CHECK(Bc250CpBootstrapSealInventory(
        &g_bootstrap, &space, &vramStatus) == BC250_CP_BOOTSTRAP_OK);

    /* Build preflight plans for TMR and CP firmwares */
    memset(&policy, 0, sizeof(policy));
    policy.TmrBytes = 0x00400000ull;
    policy.TmrAlignment = 0x00100000ull;
    policy.FirmwareAlignment = 0x1000ull;

    manifest[0].FirmwareType = BC250_PSP_FW_CP_PFP;
    manifest[0].PayloadBytes = 0x12000u;
    manifest[1].FirmwareType = BC250_PSP_FW_CP_ME;
    manifest[1].PayloadBytes = 0x14000u;
    manifest[2].FirmwareType = BC250_PSP_FW_CP_CE;
    manifest[2].PayloadBytes = 0x10000u;
    manifest[3].FirmwareType = BC250_PSP_FW_CP_MEC;
    manifest[3].PayloadBytes = 0x30000u;
    manifest[4].FirmwareType = BC250_PSP_FW_CP_MEC_JT1;
    manifest[4].PayloadBytes = 0x08000u;

    CHECK(Bc250CpBootstrapBuildPreflight(
        &g_bootstrap, &policy, manifest, BC250_CP_BOOTSTRAP_FIRMWARE_COUNT,
        &g_preflight, &vramStatus, &addressStatus, &pspStatus) ==
        BC250_CP_BOOTSTRAP_OK);

    /* Issue Staging Lease under complete inventory evidence */
    memset(&stagingEvidence, 0, sizeof(stagingEvidence));
    stagingEvidence.EvidenceFlags = g_report.StagingEvidenceFlags;
    stagingEvidence.EvidenceGeneration = g_report.SnapshotGeneration;

    CHECK(Bc250IssueFirmwareStagingLease(
        &g_bootstrap, &g_preflight, &stagingEvidence, &lease,
        NULL, &vramStatus) == BC250_STAGING_LEASE_OK);
    CHECK(lease.Offset < g_report.CpuVisibleBytes);
    CHECK(lease.Offset >= g_input.ScanoutReservation.Offset +
                          g_input.ScanoutReservation.Length);

    CHECK(Bc250ValidateFirmwareStagingLease(
        &g_bootstrap, &g_preflight, &lease, &g_preflight.FirmwarePlans[0],
        NULL, &vramStatus) == BC250_STAGING_LEASE_OK);

    /* Test failure modes */

    /*
     * Scanout fora da janela visivel pela CPU.  O contrato valida o scanout
     * (`Offset + Length <= CpuVisibleBytes`) ANTES de procurar sobreposicao,
     * e devolve SCANOUT_INVALID -- ordem correta: um scanout que a CPU nao
     * alcanca e' invalido independentemente de colidir com algo.
     *
     * O gabarito antigo exigia OVERLAP aqui.  0x1FC00000 esta fora dos
     * 0x10000000 visiveis, entao esse retorno nunca era possivel.  A
     * assercao ficou escondida atras da recursao infinita do stub de memcpy
     * deste arquivo e do portao `if errorlevel 1` do build-contract.bat.
     */
    g_input.ScanoutReservation.Offset = 0x1FC00000ull;
    CHECK(Bc250BuildRuntimeVramInventory(
        &g_input, 0x20000000ull, 0x10000000ull, 42, &g_report) ==
        BC250_RUNTIME_INVENTORY_SCANOUT_INVALID);

    /*
     * Sobreposicao de verdade: carveout dentro da janela visivel pela CPU
     * colidindo com o scanout em 0x00100000.  Este e' o caso que o gabarito
     * antigo pretendia cobrir e nunca cobriu.
     */
    g_input.ScanoutReservation.Offset = 0x00100000ull;
    g_input.PlatformCarveoutCount = 2;
    g_input.PlatformCarveouts[1].Offset = 0x00100000ull;
    g_input.PlatformCarveouts[1].Length = 0x00100000ull;
    g_input.PlatformCarveouts[1].Alignment = 0x100000ull;
    g_input.PlatformCarveouts[1].Role = BC250_VRAM_ROLE_PLATFORM_RESERVED;
    g_input.PlatformCarveouts[1].Flags = BC250_VRAM_REGION_PERSISTENT;
    CHECK(Bc250BuildRuntimeVramInventory(
        &g_input, 0x20000000ull, 0x10000000ull, 42, &g_report) ==
        BC250_RUNTIME_INVENTORY_OVERLAP);
    memset(&g_input.PlatformCarveouts[1], 0,
        sizeof(g_input.PlatformCarveouts[1]));
    g_input.PlatformCarveoutCount = 1;

    g_input.ScanoutReservation.Offset = 0x00100000ull;
    g_input.DiscoveryTmrEvidence.Offset = 0x20000000ull; /* out of range */
    CHECK(Bc250BuildRuntimeVramInventory(
        &g_input, 0x20000000ull, 0x10000000ull, 42, &g_report) ==
        BC250_RUNTIME_INVENTORY_DISCOVERY_TMR_INVALID);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunRuntimeInventoryTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunRuntimeInventoryTest() ? 0 : 1;
}
#endif
