#include "../core/bc250_sdma_packet_builder.h"

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
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

#define TEST_RING_DWORDS 128u

static uint32_t g_ring[TEST_RING_DWORDS];

static int Bc250RunSdmaPacketBuilderTest(void)
{
    uint32_t cursor = 0;
    uint32_t rptr = 0;
    uint32_t wptr = 0;
    uint32_t available = 0;
    uint32_t i;

    memset(g_ring, 0, sizeof(g_ring));

    /* 1. Test COPY_LINEAR emission */
    CHECK(Bc250SdmaEmitCopyLinear(
        g_ring, TEST_RING_DWORDS, &cursor,
        0x0000000001000000ull, 0x0000000002000000ull, 4096u) == BC250_SDMA_OK);
    CHECK(cursor == 7u);
    CHECK(g_ring[0] == 0x00000001u); /* Opcode 1, Subop 0 */
    CHECK(g_ring[1] == 4095u);       /* ByteCount - 1 */
    CHECK(g_ring[2] == 0x00000000u); /* Endian/reserved */
    CHECK(g_ring[3] == 0x01000000u); /* Src Low */
    CHECK(g_ring[4] == 0x00000000u); /* Src High */
    CHECK(g_ring[5] == 0x02000000u); /* Dst Low */
    CHECK(g_ring[6] == 0x00000000u); /* Dst High */

    /* 2. Test FENCE emission */
    CHECK(Bc250SdmaEmitFence(
        g_ring, TEST_RING_DWORDS, &cursor,
        0x000000000FF00000ull, 0xBC250001u) == BC250_SDMA_OK);
    CHECK(cursor == 11u);
    CHECK(g_ring[7] == 0x00030005u); /* Opcode 5, MTYPE UC */
    CHECK(g_ring[8] == 0x0FF00000u); /* Fence Low */
    CHECK(g_ring[9] == 0x00000000u); /* Fence High */
    CHECK(g_ring[10] == 0xBC250001u); /* Sequence */
    CHECK(Bc250SdmaEmitGcr(
        g_ring, TEST_RING_DWORDS, &cursor) == BC250_SDMA_OK);
    CHECK(cursor == 16u);
    CHECK(g_ring[11] == 17u);
    CHECK(g_ring[12] == 0u);
    CHECK(g_ring[13] == 0xC3A10000u);
    CHECK(g_ring[14] == 0u && g_ring[15] == 0u);

    cursor = 0u;
    memset(g_ring, 0, sizeof(g_ring));
    {
        const uint32_t data[4] = {0x11223344u,0x55667788u,0xA5A5A5A5u,0xBC250125u};
        CHECK(Bc250SdmaEmitWriteLinear(g_ring, TEST_RING_DWORDS, &cursor,
            0x000000F40FF04000ull, data, 4u) == BC250_SDMA_OK);
        CHECK(cursor == 8u);
        CHECK(g_ring[0] == 2u && g_ring[1] == 0x0FF04000u &&
            g_ring[2] == 0x000000F4u && g_ring[3] == 3u);
        CHECK(g_ring[4] == data[0] && g_ring[7] == data[3]);
    }

    /* 3. Test CONST_FILL emission */
    cursor = 0;
    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250SdmaEmitConstFill(
        g_ring, TEST_RING_DWORDS, &cursor,
        0x0000000003000000ull, 0x5A5A5A5Au, 4096u) == BC250_SDMA_OK);
    CHECK(cursor == 5u);
    CHECK(g_ring[0] == 0x0000020Bu); /* Opcode 11, Size 2 (DWORD) */
    CHECK(g_ring[1] == 0x03000000u); /* Dst Low */
    CHECK(g_ring[2] == 0x00000000u); /* Dst High */
    CHECK(g_ring[3] == 0x5A5A5A5Au); /* Fill value */
    CHECK(g_ring[4] == 4095u);       /* ByteCount - 1 */

    /* 4. Test NOP, TRAP, POLL_REGMEM emission */
    cursor = 0;
    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250SdmaEmitNop(g_ring, TEST_RING_DWORDS, &cursor, 2u) == BC250_SDMA_OK);
    CHECK(cursor == 3u);
    CHECK(g_ring[0] == 0x00020000u); /* NOP with 2 extra dwords */
    CHECK(g_ring[1] == 0);
    CHECK(g_ring[2] == 0);

    CHECK(Bc250SdmaEmitTrap(g_ring, TEST_RING_DWORDS, &cursor, 0x42u) == BC250_SDMA_OK);
    CHECK(cursor == 5u);
    CHECK(g_ring[3] == 0x00000006u); /* Opcode 6 */
    CHECK(g_ring[4] == 0x42u);

    CHECK(Bc250SdmaEmitPollRegMem(
        g_ring, TEST_RING_DWORDS, &cursor,
        0x000000000FF04000ull, 0x1u, 0x1u, 0u, 1u, 10u, 100u) == BC250_SDMA_OK);
    CHECK(cursor == 11u);

    /* 5. Test Ring space calculation and wraparound */
    rptr = 0;
    wptr = 0;
    CHECK(Bc250SdmaRingComputeAvailableSpace(
        TEST_RING_DWORDS, rptr, wptr, &available) == BC250_SDMA_OK);
    CHECK(available == TEST_RING_DWORDS - 1u);

    /* Advance WPTR by 11 dwords */
    CHECK(Bc250SdmaRingAdvanceWptr(
        TEST_RING_DWORDS, wptr, 11u, &wptr) == BC250_SDMA_OK);
    CHECK(wptr == 11u);
    CHECK(Bc250SdmaRingComputeAvailableSpace(
        TEST_RING_DWORDS, rptr, wptr, &available) == BC250_SDMA_OK);
    CHECK(available == TEST_RING_DWORDS - 1u - 11u);

    /* Simulate 12 sequential submits with wraparound */
    cursor = 0;
    rptr = 0;
    wptr = 0;
    for (i = 0; i < 12; ++i) {
        uint32_t startCursor = cursor;
        CHECK(Bc250SdmaRingComputeAvailableSpace(
            TEST_RING_DWORDS, rptr, wptr, &available) == BC250_SDMA_OK);
        CHECK(available >= 11u);

        CHECK(Bc250SdmaEmitCopyLinear(
            g_ring, TEST_RING_DWORDS, &cursor,
            0x01000000ull, 0x02000000ull + (uint64_t)(i * 4096u), 4096u) == BC250_SDMA_OK);
        CHECK(Bc250SdmaEmitFence(
            g_ring, TEST_RING_DWORDS, &cursor,
            0x0FF00000ull, 0xBC250000u + i) == BC250_SDMA_OK);
        CHECK(cursor - startCursor == 11u);

        CHECK(Bc250SdmaRingAdvanceWptr(
            TEST_RING_DWORDS, wptr, 11u, &wptr) == BC250_SDMA_OK);
        /* Simulate GPU consuming ring immediately (RPTR = WPTR) */
        rptr = wptr;
    }

    /* 6. Test Error cases */
    CHECK(Bc250SdmaEmitCopyLinear(
        NULL, TEST_RING_DWORDS, &cursor, 0x1000ull, 0x2000ull, 4096u) == BC250_SDMA_BAD_ARGUMENT);
    CHECK(Bc250SdmaEmitCopyLinear(
        g_ring, 100u /* not power of 2 */, &cursor, 0x1000ull, 0x2000ull, 4096u) == BC250_SDMA_BAD_ARGUMENT);
    CHECK(Bc250SdmaEmitCopyLinear(
        g_ring, TEST_RING_DWORDS, &cursor, 0x1000ull, 0x2000ull, 0u) == BC250_SDMA_PAYLOAD_TOO_LARGE);
    CHECK(Bc250SdmaEmitCopyLinear(
        g_ring, TEST_RING_DWORDS, &cursor, 0x1000ull, 0x2000ull, 0x00400001u) == BC250_SDMA_PAYLOAD_TOO_LARGE);

    CHECK(Bc250SdmaEmitConstFill(
        g_ring, TEST_RING_DWORDS, &cursor, 0x1001ull /* misaligned */, 0xAAu, 4096u) == BC250_SDMA_MISALIGNED);
    CHECK(Bc250SdmaEmitFence(
        g_ring, TEST_RING_DWORDS, &cursor, 0x1002ull /* misaligned */, 0x1u) == BC250_SDMA_MISALIGNED);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunSdmaPacketBuilderTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunSdmaPacketBuilderTest() ? 0 : 1;
}
#endif
