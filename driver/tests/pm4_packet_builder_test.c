#include "../core/bc250_pm4_packet_builder.h"

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

#define TEST_PM4_RING_DWORDS 128u

static uint32_t g_pm4Ring[TEST_PM4_RING_DWORDS];

static int Bc250RunPm4PacketBuilderTest(void)
{
    uint32_t cursor = 0;
    uint32_t rptr = 0;
    uint32_t wptr = 0;
    uint32_t available = 0;
    uint32_t writeDataPayload[2] = { 0xDEADBEEFu, 0xCAFEBABEu };
    uint32_t shRegPayload[2] = { 0x00000001u, 0x00000002u };
    uint32_t i;

    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));

    /* 1. Test NOP emission (ExtraCount = 3 -> 4 total dwords) */
    CHECK(Bc250Pm4EmitNop(g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor, 3u) == BC250_PM4_OK);
    CHECK(cursor == 4u);
    CHECK(g_pm4Ring[0] == 0xC0021000u); /* Type 3, Count 2, 3-dword body */
    CHECK(g_pm4Ring[1] == 0);
    CHECK(g_pm4Ring[2] == 0);
    CHECK(g_pm4Ring[3] == 0);
    CHECK(Bc250Pm4EmitNop(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor, 0u) ==
        BC250_PM4_PAYLOAD_TOO_LARGE);

    /* 2. Test WRITE_DATA emission */
    cursor = 0;
    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));
    CHECK(Bc250Pm4EmitWriteData(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        BC250_PM4_WD_ENGINE_ME | BC250_PM4_WD_DST_MEM | BC250_PM4_WD_CONFIRM,
        0x0000080000001000ull, writeDataPayload, 2u) == BC250_PM4_OK);
    CHECK(cursor == 6u);
    CHECK(g_pm4Ring[0] == 0xC0043700u); /* Type 3, Count 4 (5 dwords body), Op 0x37 */
    CHECK(g_pm4Ring[1] == (BC250_PM4_WD_ENGINE_ME | BC250_PM4_WD_DST_MEM | BC250_PM4_WD_CONFIRM));
    CHECK(g_pm4Ring[2] == 0x00001000u); /* Dst Low */
    CHECK(g_pm4Ring[3] == 0x00000800u); /* Dst High */
    CHECK(g_pm4Ring[4] == 0xDEADBEEFu);
    CHECK(g_pm4Ring[5] == 0xCAFEBABEu);

    /* 2b. Test DRAW_INDEX_AUTO emission */
    cursor = 0;
    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));
    CHECK(Bc250Pm4EmitDrawIndexAuto(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        3u, BC250_PM4_DI_PT_TRIANGLELIST) == BC250_PM4_OK);
    CHECK(cursor == 3u);
    CHECK(g_pm4Ring[0] == 0xC0012D00u); /* Type 3, Count 1 (2 dwords payload), Op 0x2D */
    CHECK(g_pm4Ring[1] == 3u);          /* IndexCount = 3 */
    CHECK(g_pm4Ring[2] == BC250_PM4_DI_PT_TRIANGLELIST);

    /* 2b-2. Test PACKET2 NOP emission */
    cursor = 0;
    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));
    CHECK(Bc250Pm4EmitPacket2Nop(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor) == BC250_PM4_OK);
    CHECK(cursor == 1u);
    CHECK(g_pm4Ring[0] == BC250_PM4_PACKET2_NOP);

    /* 2c. Test COLOR_TARGET_0 emission */
    cursor = 0;
    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));
    CHECK(Bc250Pm4EmitColorTarget0(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        0x0000000010000000ull, 1920u, 1080u, 7680u, 0x16u, 1u) == BC250_PM4_OK);
    CHECK(cursor == 8u);
    CHECK(g_pm4Ring[0] == 0xC0066900u); /* Type 3, Count 6 (7 dwords payload: regOffset + 6 values), Op 0x69 */
    CHECK(g_pm4Ring[1] == 0x318u);       /* Reg Offset relative to 0xA000 */
    CHECK(g_pm4Ring[2] == (uint32_t)(0x0000000010000000ull >> 8)); /* CB_COLOR0_BASE */
    CHECK(g_pm4Ring[6] == (((0x16u & 0x1Fu) << 2) | (1u << 11) | (1u << 16))); /* CB_COLOR0_INFO */
    /* Test misalignment rejection */
    CHECK(Bc250Pm4EmitColorTarget0(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        0x0000000010000004ull, 1920u, 1080u, 7680u, 0x16u, 1u) == BC250_PM4_MISALIGNED);

    /*
     * FAIL-CLOSED DE LARGURA DE CAMPO (docs/03, bloco "GFX10 3D").
     * CB_COLOR0_PITCH.TILE_MAX e' [10:0] e CB_COLOR0_SLICE.TILE_MAX e' [21:0].
     * Antes desta faixa o emissor MASCARAVA em silencio, produzindo um render
     * target com geometria errada que o hardware aceitaria -- falso sucesso.
     * O CONTRAPONTO no limite exato existe para a recusa nao virar peneira.
     */
    cursor = 0;
    /* Contraponto: pitch de 16384 px cai EXATAMENTE em TILE_MAX = 2047 -> aceito */
    CHECK(Bc250Pm4EmitColorTarget0(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        0x0000000010000000ull, 16384u, 8u, 65536u, 0x16u, 1u) == BC250_PM4_OK);
    CHECK(g_pm4Ring[3] == 0x7FFu); /* CB_COLOR0_PITCH.TILE_MAX no teto, sem truncar */
    /* Um tile alem do teto de PITCH -> RECUSA, nunca mascara */
    cursor = 0;
    CHECK(Bc250Pm4EmitColorTarget0(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        0x0000000010000000ull, 16392u, 8u, 65568u, 0x16u, 1u) == BC250_PM4_BAD_ARGUMENT);
    /* Altura que estoura SLICE.TILE_MAX com PITCH ainda dentro -> RECUSA isolada */
    cursor = 0;
    CHECK(Bc250Pm4EmitColorTarget0(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        0x0000000010000000ull, 16384u, 16392u, 65536u, 0x16u, 1u) == BC250_PM4_BAD_ARGUMENT);

    /* 2d. Test WINDOW_SCISSOR emission */
    cursor = 0;
    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));
    CHECK(Bc250Pm4EmitWindowScissor(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        0u, 0u, 1920u, 1080u) == BC250_PM4_OK);
    CHECK(cursor == 4u);
    CHECK(g_pm4Ring[0] == 0xC0026900u); /* Type 3, Count 2 (3 dwords payload: regOffset + 2 values), Op 0x69 */
    CHECK(g_pm4Ring[1] == 0x81u);        /* Reg Offset relative to 0xA000 */
    CHECK(g_pm4Ring[2] == 0x80000000u);  /* TL: (0,0) with OffsetDisable bit 31 */
    CHECK(g_pm4Ring[3] == (1920u | (1080u << 16))); /* BR: (1920, 1080) */

    /* 3. Test SET_SH_REG emission */
    cursor = 0;
    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));
    CHECK(Bc250Pm4EmitSetShReg(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        BC250_PM4_SH_REG_BASE + 0x14u, shRegPayload, 2u) == BC250_PM4_OK);
    CHECK(cursor == 4u);
    CHECK(g_pm4Ring[0] == 0xC0027600u); /* Type 3, Count 2, Op 0x76 */
    CHECK(g_pm4Ring[1] == 0x14u);       /* Reg Offset relative to base */
    CHECK(g_pm4Ring[2] == 0x00000001u);
    CHECK(g_pm4Ring[3] == 0x00000002u);

    /* 4. Test SET_CONTEXT_REG & SET_UCONFIG_REG emission */
    cursor = 0;
    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));
    CHECK(Bc250Pm4EmitSetContextReg(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        BC250_PM4_CONTEXT_REG_BASE + 0x08u, shRegPayload, 1u) == BC250_PM4_OK);
    CHECK(cursor == 3u);
    CHECK(g_pm4Ring[0] == 0xC0016900u); /* Type 3, Count 1, Op 0x69 */
    CHECK(g_pm4Ring[1] == 0x08u);
    CHECK(g_pm4Ring[2] == 0x00000001u);

    CHECK(Bc250Pm4EmitSetUconfigReg(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        BC250_PM4_UCONFIG_REG_BASE + 0x20u, shRegPayload, 1u) == BC250_PM4_OK);
    CHECK(cursor == 6u);
    CHECK(g_pm4Ring[3] == 0xC0017900u); /* Type 3, Count 1, Op 0x79 */
    CHECK(g_pm4Ring[4] == 0x20u);
    CHECK(g_pm4Ring[5] == 0x00000001u);

    /* 5. Test WAIT_REG_MEM, RELEASE_MEM, EVENT_WRITE */
    cursor = 0;
    memset(g_pm4Ring, 0, sizeof(g_pm4Ring));
    CHECK(Bc250Pm4EmitWaitRegMem(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        BC250_PM4_WAIT_EQ, 0x0000080000002000ull, 0x1u, 0xFFFFFFFFu, 10u) == BC250_PM4_OK);
    CHECK(cursor == 7u);
    CHECK(g_pm4Ring[0] == 0xC0053C00u); /* Type 3, Count 5, Op 0x3C */

    CHECK(Bc250Pm4EmitReleaseMem(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        BC250_PM4_EVENT_EOP, 0, 0x0000080000003000ull, 0xBC250001ull) == BC250_PM4_OK);
    CHECK(cursor == 15u);
    CHECK(g_pm4Ring[7] == 0xC0064900u); /* Type 3, Count 6, Op 0x49 */
    CHECK(g_pm4Ring[9] == 0x40000000u); /* DATA_SEL=2: 64-bit data */
    CHECK(g_pm4Ring[14] == 0u);         /* ctxid/reserved */

    CHECK(Bc250Pm4EmitEventWrite(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
        BC250_PM4_EVENT_CS_PARTIAL_FLUSH) == BC250_PM4_OK);
    CHECK(cursor == 17u);
    CHECK(g_pm4Ring[15] == 0xC0004600u); /* Type 3, Count 0, Op 0x46 */

    /* 6. Test Ring space calculation and wraparound */
    rptr = 0;
    wptr = 0;
    CHECK(Bc250Pm4RingComputeAvailableSpace(
        TEST_PM4_RING_DWORDS, rptr, wptr, &available) == BC250_PM4_OK);
    CHECK(available == TEST_PM4_RING_DWORDS - 1u);

    /* Simulate 12 sequential submits with wraparound */
    cursor = 0;
    rptr = 0;
    wptr = 0;
    for (i = 0; i < 12; ++i) {
        uint32_t startCursor = cursor;
        CHECK(Bc250Pm4RingComputeAvailableSpace(
            TEST_PM4_RING_DWORDS, rptr, wptr, &available) == BC250_PM4_OK);
        CHECK(available >= 10u);

        CHECK(Bc250Pm4EmitWriteData(
            g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor,
            BC250_PM4_WD_ENGINE_ME | BC250_PM4_WD_DST_MEM,
            0x0000080000001000ull, writeDataPayload, 2u) == BC250_PM4_OK);
        CHECK(Bc250Pm4EmitNop(
            g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor, 3u) == BC250_PM4_OK);
        CHECK(cursor - startCursor == 10u);

        CHECK(Bc250Pm4RingAdvanceWptr(
            TEST_PM4_RING_DWORDS, wptr, 10u, &wptr) == BC250_PM4_OK);
        /* Simulate GPU consuming ring */
        rptr = wptr;
    }

    /* 7. Test Error cases */
    CHECK(Bc250Pm4EmitWriteData(
        NULL, TEST_PM4_RING_DWORDS, &cursor, 0, 0x1000ull, writeDataPayload, 2u) == BC250_PM4_BAD_ARGUMENT);
    CHECK(Bc250Pm4EmitWriteData(
        g_pm4Ring, 100u /* not power of 2 */, &cursor, 0, 0x1000ull, writeDataPayload, 2u) == BC250_PM4_BAD_ARGUMENT);
    CHECK(Bc250Pm4EmitWriteData(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor, 0, 0x1000ull, writeDataPayload, 0u) == BC250_PM4_PAYLOAD_TOO_LARGE);
    CHECK(Bc250Pm4EmitSetShReg(
        g_pm4Ring, TEST_PM4_RING_DWORDS, &cursor, 0, NULL, 2u) == BC250_PM4_BAD_ARGUMENT);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunPm4PacketBuilderTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunPm4PacketBuilderTest() ? 0 : 1;
}
#endif
