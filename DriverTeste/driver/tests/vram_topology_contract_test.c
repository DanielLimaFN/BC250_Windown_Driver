#include "../core/bc250_vram_topology_contract.h"

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

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { \
    if (!(Expression)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #Expression); \
        return 1; \
    } \
} while (0)
#endif

static BC250_VRAM_TOPOLOGY_SNAPSHOT HealthySnapshot(void)
{
    BC250_VRAM_TOPOLOGY_SNAPSHOT snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.RccConfigMemsizeMiB = 0x200u;
    snapshot.GcmcVmFbLocationBase = 0xF400u;
    snapshot.GcmcVmFbLocationTop = 0xF41Fu;
    snapshot.GcmcVmFbOffset = 0x450u;
    snapshot.CpuVisibleBytes = 0x10000000ull;
    snapshot.EvidenceFlags = BC250_TOPOLOGY_EVIDENCE_REQUIRED;
    snapshot.SnapshotGeneration = 61u;
    return snapshot;
}

static int Bc250RunVramTopologyContractTest(void)
{
    BC250_VRAM_TOPOLOGY_SNAPSHOT snapshot = HealthySnapshot();
    BC250_VRAM_ADDRESS_SPACE space;

    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_OK);
    CHECK(space.TotalBytes == 0x20000000ull);
    CHECK(space.CpuVisibleBytes == 0x10000000ull);
    CHECK(space.VramMcStart == 0x000000F400000000ull);
    CHECK(space.VramMcEnd == 0x000000F41FFFFFFFull);
    CHECK(space.VramBaseOffset == 0x0000000450000000ull);
    CHECK(space.EvidenceFlags == BC250_ADDR_EVIDENCE_REQUIRED);
    CHECK(space.SnapshotGeneration == 61u);

    snapshot.EvidenceFlags &= ~BC250_TOPOLOGY_EVIDENCE_SAME_SNAPSHOT;
    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_MISSING_EVIDENCE);
    CHECK(space.TotalBytes == 0);

    snapshot = HealthySnapshot();
    snapshot.GcmcVmFbLocationTop--;
    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_SIZE_MISMATCH);

    snapshot = HealthySnapshot();
    snapshot.GcmcVmFbLocationTop = snapshot.GcmcVmFbLocationBase - 1u;
    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_REVERSED_RANGE);

    snapshot = HealthySnapshot();
    snapshot.GcmcVmFbOffset |= 0x80000000u;
    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_RESERVED_BITS_SET);

    snapshot = HealthySnapshot();
    snapshot.RccConfigMemsizeMiB = 0;
    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_INVALID_MEMSIZE);

    snapshot = HealthySnapshot();
    snapshot.CpuVisibleBytes = 0x30000000ull;
    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_CPU_WINDOW_MISMATCH);

    snapshot = HealthySnapshot();
    snapshot.CpuVisibleBytes++;
    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_CPU_WINDOW_MISMATCH);

    snapshot = HealthySnapshot();
    snapshot.SnapshotGeneration = 0;
    CHECK(Bc250DecodeVramTopology(&snapshot, &space) ==
          BC250_VRAM_TOPOLOGY_BAD_ARGUMENT);

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunVramTopologyContractTest() ? 1ul : 0ul);
}
#else
int main(void)
{
    int result = Bc250RunVramTopologyContractTest();
    if (result == 0) {
        printf("VRAM TOPOLOGY CONTRACT: OK (RCC + GCMC + CPU window)\n");
        printf("Nenhum MMIO foi lido ou escrito por este teste.\n");
        printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    }
    return result;
}
#endif
