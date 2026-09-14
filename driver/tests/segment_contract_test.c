#include "../core/bc250_segment_contract.h"

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
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

static BC250_SEGMENT_CONFIG g_testSegConfig;
static BC250_ALLOCATION_REQUEST g_testAllocReq;
static BC250_ALLOCATION_DESCRIPTOR g_testAllocDesc;

static int Bc250RunSegmentContractTest(void)
{
    memset(&g_testSegConfig, 0, sizeof(g_testSegConfig));
    memset(&g_testAllocReq, 0, sizeof(g_testAllocReq));
    memset(&g_testAllocDesc, 0, sizeof(g_testAllocDesc));

    /* 1. Test Segment Configuration (16GB VRAM, 15.5GB Usable, 256MB Aperture, 512MB GART) */
    CHECK(Bc250BuildSegmentConfig(
        0x0000000400000000ull, 0x00000003E0000000ull,
        0x0000000010000000ull, 0x0000000020000000ull,
        &g_testSegConfig) == BC250_SEGMENT_OK);

    CHECK(g_testSegConfig.SegmentCount == 2u);
    CHECK(g_testSegConfig.Segments[0].SegmentId == BC250_SEGMENT_ID_VRAM);
    CHECK(g_testSegConfig.Segments[0].SizeBytes == 0x00000003E0000000ull);
    CHECK(g_testSegConfig.Segments[0].CpuVisibleSizeBytes == 0x0000000010000000ull);
    CHECK((g_testSegConfig.Segments[0].Flags & BC250_SEGMENT_FLAG_VRAM) != 0);
    CHECK((g_testSegConfig.Segments[0].Flags & BC250_SEGMENT_FLAG_CPU_VISIBLE) != 0);

    CHECK(g_testSegConfig.Segments[1].SegmentId == BC250_SEGMENT_ID_GART);
    CHECK(g_testSegConfig.Segments[1].SizeBytes == 0x0000000020000000ull);
    CHECK((g_testSegConfig.Segments[1].Flags & BC250_SEGMENT_FLAG_APERTURE) != 0);

    /* 2. Test 4K-aligned Allocation Request */
    g_testAllocReq.AllocationId = 1u;
    g_testAllocReq.SizeBytes = 10000u;
    g_testAllocReq.AlignmentBytes = 4096u;
    g_testAllocReq.PreferredSegmentId = BC250_SEGMENT_ID_VRAM;
    g_testAllocReq.SupportedSegmentMask = BC250_SEGMENT_MASK_ALL;

    CHECK(Bc250ValidateAllocationRequest(&g_testSegConfig, &g_testAllocReq) == BC250_SEGMENT_OK);

    CHECK(Bc250DescribeAllocation(
        &g_testSegConfig, &g_testAllocReq, 0x00100000ull, &g_testAllocDesc) == BC250_SEGMENT_OK);
    CHECK(g_testAllocDesc.AllocationId == 1u);
    CHECK(g_testAllocDesc.AlignedSizeBytes == 12288u); /* 3 * 4096 */
    CHECK(g_testAllocDesc.AlignmentBytes == 4096u);
    CHECK(g_testAllocDesc.AssignedSegmentId == BC250_SEGMENT_ID_VRAM);
    CHECK(g_testAllocDesc.SegmentOffset == 0x00100000ull);
    CHECK(g_testAllocDesc.IsValid == 1u);

    /* 3. Test 64K-aligned Allocation Request */
    g_testAllocReq.AllocationId = 2u;
    g_testAllocReq.SizeBytes = 70000u;
    g_testAllocReq.AlignmentBytes = 65536u;
    g_testAllocReq.PreferredSegmentId = BC250_SEGMENT_ID_GART;

    CHECK(Bc250DescribeAllocation(
        &g_testSegConfig, &g_testAllocReq, 0x00200000ull, &g_testAllocDesc) == BC250_SEGMENT_OK);
    CHECK(g_testAllocDesc.AlignedSizeBytes == 131072u); /* 2 * 65536 */
    CHECK(g_testAllocDesc.AssignedSegmentId == BC250_SEGMENT_ID_GART);

    /* 4. Test Error Handling */
    /* Out of memory in VRAM */
    g_testAllocReq.SizeBytes = 0x0000000400000000ull; /* 16GB > 15.5GB usable */
    g_testAllocReq.PreferredSegmentId = BC250_SEGMENT_ID_VRAM;
    CHECK(Bc250ValidateAllocationRequest(&g_testSegConfig, &g_testAllocReq) == BC250_SEGMENT_OUT_OF_MEMORY);
    g_testAllocReq.SizeBytes = 10000u;

    /* Invalid Alignment */
    g_testAllocReq.AlignmentBytes = 1000u; /* Not power of 2 or < 4K */
    CHECK(Bc250ValidateAllocationRequest(&g_testSegConfig, &g_testAllocReq) == BC250_SEGMENT_INVALID_ALIGNMENT);
    g_testAllocReq.AlignmentBytes = 4096u;

    /* Misaligned offset */
    CHECK(Bc250DescribeAllocation(
        &g_testSegConfig, &g_testAllocReq, 0x00100004ull, &g_testAllocDesc) == BC250_SEGMENT_INVALID_ALIGNMENT);

    /* Null Arguments */
    CHECK(Bc250BuildSegmentConfig(0, 0, 0, 0, NULL) == BC250_SEGMENT_BAD_ARGUMENT);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunSegmentContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunSegmentContractTest() ? 0 : 1;
}
#endif
