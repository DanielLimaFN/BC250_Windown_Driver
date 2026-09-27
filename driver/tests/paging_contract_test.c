#include "../core/bc250_paging_contract.h"

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

#define TEST_PAGING_BUFFER_DWORDS 256u

static uint32_t g_pagingBuffer[TEST_PAGING_BUFFER_DWORDS];
static uint64_t g_pteBuffer[64];

static int Bc250RunPagingContractTest(void)
{
    uint32_t cursor = 0;
    uint32_t writtenPtes = 0;
    BC250_PAGING_TRANSFER_PARAMS transferParams;
    BC250_PAGING_FILL_PARAMS fillParams;
    BC250_PAGING_UPDATE_PAGE_TABLE_PARAMS updateParams;
    BC250_PAGING_FLUSH_TLB_PARAMS flushParams;
    BC250_GFXHUB_TLB_FLUSH_PLAN flushPlan;
    BC250_PAGING_REQUEST request;

    memset(g_pagingBuffer, 0, sizeof(g_pagingBuffer));
    memset(g_pteBuffer, 0, sizeof(g_pteBuffer));

    /* 1. Test SDMA Linear Transfer */
    cursor = 0;
    memset(&transferParams, 0, sizeof(transferParams));
    transferParams.SourceGpuAddress = 0x0000000001000000ull;
    transferParams.DestinationGpuAddress = 0x0000000002000000ull;
    transferParams.TransferSizeBytes = 8192u;

    CHECK(Bc250PagingBuildTransfer(
        &transferParams, g_pagingBuffer, TEST_PAGING_BUFFER_DWORDS, &cursor) == BC250_PAGING_OK);
    CHECK(cursor == 7u);
    CHECK(g_pagingBuffer[0] == 0x00000001u); /* SDMA_OP_COPY_LINEAR */
    CHECK(g_pagingBuffer[1] == 8191u);       /* count - 1 */
    CHECK(g_pagingBuffer[2] == 0u);          /* endian/reserved */
    CHECK(g_pagingBuffer[3] == 0x01000000u); /* src lo */
    CHECK(g_pagingBuffer[5] == 0x02000000u); /* dst lo */

    /* 2. Test SDMA Const Fill */
    memset(&fillParams, 0, sizeof(fillParams));
    fillParams.DestinationGpuAddress = 0x0000000003000000ull;
    fillParams.FillValue = 0x00000000u;
    fillParams.FillSizeBytes = 4096u;

    CHECK(Bc250PagingBuildFill(
        &fillParams, g_pagingBuffer, TEST_PAGING_BUFFER_DWORDS, &cursor) == BC250_PAGING_OK);
    CHECK(cursor == 12u); /* 7 + 5 dwords */
    CHECK((g_pagingBuffer[7] & 0xFFu) == BC250_SDMA_OP_CONST_FILL); /* SDMA_OP_CONST_FILL */
    CHECK(g_pagingBuffer[8] == 0x03000000u); /* dst */
    CHECK(g_pagingBuffer[10] == 0x00000000u); /* value */
    CHECK(g_pagingBuffer[11] == 4095u);      /* count - 1 */

    uint64_t physAddrOut = 0;
    uint32_t flagsOut = 0;

    /* 3. Test Update Page Table PTE Generation */
    memset(&updateParams, 0, sizeof(updateParams));
    updateParams.PageTableGpuAddress = 0x0000080000000000ull;
    updateParams.StartIndex = 0u;
    updateParams.EntryCount = 4u;
    updateParams.PhysicalAddressBase = 0x00000000C0000000ull;
    updateParams.PteFlags = BC250_PTE_VALID | BC250_PTE_SYSTEM |
                            BC250_PTE_READABLE | BC250_PTE_WRITEABLE;

    CHECK(Bc250PagingBuildUpdatePageTable(
        &updateParams, g_pteBuffer, 64u, &writtenPtes) == BC250_PAGING_OK);
    CHECK(writtenPtes == 4u);
    CHECK(Bc250GcvmDecodePte(g_pteBuffer[0], &physAddrOut, &flagsOut) == BC250_GCVM_OK);
    CHECK(physAddrOut == 0x00000000C0000000ull);
    CHECK(flagsOut == updateParams.PteFlags);
    CHECK(Bc250GcvmDecodePte(g_pteBuffer[1], &physAddrOut, &flagsOut) == BC250_GCVM_OK);
    CHECK(physAddrOut == 0x00000000C0001000ull);
    CHECK(Bc250GcvmDecodePte(g_pteBuffer[2], &physAddrOut, &flagsOut) == BC250_GCVM_OK);
    CHECK(physAddrOut == 0x00000000C0002000ull);
    CHECK(Bc250GcvmDecodePte(g_pteBuffer[3], &physAddrOut, &flagsOut) == BC250_GCVM_OK);
    CHECK(physAddrOut == 0x00000000C0003000ull);

    /* 4. Build the GC 10.1.3 GFXHUB invalidation plan without MMIO. */
    memset(&flushParams, 0, sizeof(flushParams));
    memset(&flushPlan, 0, sizeof(flushPlan));
    flushParams.Vmid = 8u;
    flushParams.HubIndex = 0u;
    flushParams.FlushType = 0u;

    CHECK(Bc250PagingBuildGfxhubTlbFlushPlan(
        &flushParams, &flushPlan) == BC250_PAGING_OK);
    CHECK(flushPlan.EngineId == 17u);
    CHECK(flushPlan.RequestRegisterOffset == 0xA2D0u);
    CHECK(flushPlan.AckRegisterOffset == 0xA318u);
    CHECK(flushPlan.RangeLowRegisterOffset == 0xA3A4u);
    CHECK(flushPlan.RangeHighRegisterOffset == 0xA3A8u);
    CHECK(flushPlan.RequestValue == 0x00F80100u);
    CHECK(flushPlan.AckMask == 0x00000100u);
    CHECK(flushPlan.ExpectedRangeLow == 0xFFFFFFFFu);
    CHECK(flushPlan.ExpectedRangeHigh == 0x0000001Fu);
    CHECK(flushPlan.RequiresDummyRequestRead == 1u);
    CHECK(flushPlan.UsesSemaphore == 0u);
    flushParams.Vmid = 9u;
    CHECK(Bc250PagingBuildGfxhubTlbFlushPlan(
        &flushParams, &flushPlan) == BC250_PAGING_OK);
    CHECK(flushPlan.RequestValue == 0x00F80200u);
    CHECK(flushPlan.AckMask == 0x00000200u);

    /*
     * DX.32: engine 0 para o anel grafico. Numeros independentes do codigo:
     * gc_10_1_0_offset.h (ENG0_REQ 0x1643, ENG0_ACK 0x1655, ENG0_ADDR_RANGE_LO32
     * 0x1667) com GC_BASE[0]=0x1260, vezes 4. Engine sem faixa programada recusa.
     */
    CHECK(Bc250PagingBuildGfxhubTlbFlushPlanForEngine(
        &flushParams, BC250_GFX_RING_INVALIDATE_ENGINE, &flushPlan) == BC250_PAGING_OK);
    CHECK(flushPlan.EngineId == 0u);
    CHECK(flushPlan.RequestRegisterOffset == 0xA28Cu);
    CHECK(flushPlan.AckRegisterOffset == 0xA2D4u);
    CHECK(flushPlan.RangeLowRegisterOffset == 0xA31Cu);
    CHECK(flushPlan.RangeHighRegisterOffset == 0xA320u);
    CHECK(flushPlan.RequestValue == 0x00F80200u && flushPlan.AckMask == 0x00000200u);
    CHECK(Bc250PagingBuildGfxhubTlbFlushPlanForEngine(
        &flushParams, 17u, &flushPlan) == BC250_PAGING_OK);
    CHECK(flushPlan.RequestRegisterOffset == 0xA2D0u && flushPlan.AckRegisterOffset == 0xA318u);
    CHECK(flushPlan.RangeLowRegisterOffset == 0xA3A4u && flushPlan.RangeHighRegisterOffset == 0xA3A8u);
    CHECK(Bc250PagingBuildGfxhubTlbFlushPlanForEngine(
        &flushParams, 5u, &flushPlan) == BC250_PAGING_BAD_ARGUMENT);
    flushParams.Vmid = 8u;

    CHECK(Bc250PagingBuildFlushTlb(
        &flushParams, g_pagingBuffer, TEST_PAGING_BUFFER_DWORDS, &cursor) ==
        BC250_PAGING_UNSUPPORTED_OPERATION);
    CHECK(cursor == 12u);
    flushParams.Vmid = 0u;
    CHECK(Bc250PagingBuildGfxhubTlbFlushPlan(
        &flushParams, &flushPlan) == BC250_PAGING_BAD_ARGUMENT);
    flushParams.Vmid = 8u;
    flushParams.HubIndex = 1u;
    CHECK(Bc250PagingBuildGfxhubTlbFlushPlan(
        &flushParams, &flushPlan) == BC250_PAGING_BAD_ARGUMENT);
    flushParams.HubIndex = 0u;
    flushParams.FlushType = 8u;
    CHECK(Bc250PagingBuildGfxhubTlbFlushPlan(
        &flushParams, &flushPlan) == BC250_PAGING_BAD_ARGUMENT);

    /* 5. Test Unified Request Dispatcher */
    memset(&request, 0, sizeof(request));
    request.Operation = BC250_PAGING_OP_TRANSFER;
    request.Params.Transfer = transferParams;
    cursor = 0;
    CHECK(Bc250PagingBuildRequest(
        &request, g_pagingBuffer, TEST_PAGING_BUFFER_DWORDS, &cursor) == BC250_PAGING_OK);
    CHECK(cursor == 7u);

    /* 6. Test Error Handling */
    CHECK(Bc250PagingBuildTransfer(NULL, g_pagingBuffer, 256u, &cursor) == BC250_PAGING_BAD_ARGUMENT);
    updateParams.PhysicalAddressBase = 0x00000000C0000004ull; /* Not 4K aligned */
    CHECK(Bc250PagingBuildUpdatePageTable(&updateParams, g_pteBuffer, 64u, &writtenPtes) == BC250_PAGING_MISALIGNED_ADDRESS);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunPagingContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunPagingContractTest() ? 0 : 1;
}
#endif
