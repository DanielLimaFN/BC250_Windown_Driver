#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../core/bc250_cmos_vram_contract.h"

/* Mock do CMOS */
static uint8_t g_mockCmosIndex = 0;
static uint8_t g_mockCmosData[256];

static uint8_t MockInByte(void *Context, uint16_t Port)
{
    UNREFERENCED_PARAMETER(Context);
    if (Port == BC250_CMOS_DATA_PORT) {
        return g_mockCmosData[g_mockCmosIndex];
    }
    return 0xFF;
}

static void MockOutByte(void *Context, uint16_t Port, uint8_t Value)
{
    UNREFERENCED_PARAMETER(Context);
    if (Port == BC250_CMOS_INDEX_PORT) {
        g_mockCmosIndex = Value;
    }
}

static void SetupMockApcb(uint16_t umaSizeMb, uint32_t signature)
{
    memset(g_mockCmosData, 0, sizeof(g_mockCmosData));
    /* Grava assinatura em 0x90 */
    g_mockCmosData[0x90] = (uint8_t)(signature & 0xFF);
    g_mockCmosData[0x91] = (uint8_t)((signature >> 8) & 0xFF);
    g_mockCmosData[0x92] = (uint8_t)((signature >> 16) & 0xFF);
    g_mockCmosData[0x93] = (uint8_t)((signature >> 24) & 0xFF);
    /* Grava UMA_SIZE em 0xAA (0x90 + 0x1A) */
    g_mockCmosData[0xAA] = (uint8_t)(umaSizeMb & 0xFF);
    g_mockCmosData[0xAB] = (uint8_t)((umaSizeMb >> 8) & 0xFF);
}

int main(void)
{
    BC250_ISA_PORT_TRANSPORT transport;
    uint16_t umaMb = 0;
    uint64_t vramBytes = 0;
    BC250_CMOS_VRAM_STATUS status;

    transport.InByte = MockInByte;
    transport.OutByte = MockOutByte;
    transport.Context = NULL;

    printf("=== BC-250 CMOS VRAM (UMA_SIZE) Contract Test ===\n\n");

    /* Test 1: Null Arguments */
    status = Bc250ReadCmosUmaSize(NULL, &umaMb, &vramBytes);
    if (status != BC250_CMOS_VRAM_BAD_ARGUMENT) {
        printf("[FAIL] Test 1: Expected BAD_ARGUMENT, got %d (%s)\n", status, Bc250CmosVramStatusName(status));
        return 1;
    }
    printf("[PASS] Test 1: Null Transport Rejected\n");

    /* Test 2: Valid 512 MB (Default Mining / Headless BIOS) */
    SetupMockApcb(512, BC250_CMOS_APCB_SIGNATURE);
    status = Bc250ReadCmosUmaSize(&transport, &umaMb, &vramBytes);
    if (status != BC250_CMOS_VRAM_OK || umaMb != 512 || vramBytes != 512ULL * 1024ULL * 1024ULL) {
        printf("[FAIL] Test 2: Expected 512MB OK, got status=%d umaMb=%u vramBytes=%llu\n",
               status, umaMb, vramBytes);
        return 1;
    }
    printf("[PASS] Test 2: 512 MB BIOS setting detected correctly (%llu bytes)\n", vramBytes);

    /* Test 3: Valid 2048 MB (2 GB Allocation) */
    SetupMockApcb(2048, BC250_CMOS_APCB_SIGNATURE);
    status = Bc250ReadCmosUmaSize(&transport, &umaMb, &vramBytes);
    if (status != BC250_CMOS_VRAM_OK || umaMb != 2048 || vramBytes != 2048ULL * 1024ULL * 1024ULL) {
        printf("[FAIL] Test 3: Expected 2048MB OK, got status=%d umaMb=%u\n", status, umaMb);
        return 1;
    }
    printf("[PASS] Test 3: 2048 MB (2 GB) detected correctly (%llu bytes)\n", vramBytes);

    /* Test 4: Valid 8192 MB (8 GB Allocation) */
    SetupMockApcb(8192, BC250_CMOS_APCB_SIGNATURE);
    status = Bc250ReadCmosUmaSize(&transport, &umaMb, &vramBytes);
    if (status != BC250_CMOS_VRAM_OK || umaMb != 8192 || vramBytes != 8192ULL * 1024ULL * 1024ULL) {
        printf("[FAIL] Test 4: Expected 8192MB OK, got status=%d umaMb=%u\n", status, umaMb);
        return 1;
    }
    printf("[PASS] Test 4: 8192 MB (8 GB) detected correctly (%llu bytes)\n", vramBytes);

    /* Test 5: Valid 16384 MB (Full 16 GB Allocation) */
    SetupMockApcb(16384, BC250_CMOS_APCB_SIGNATURE);
    status = Bc250ReadCmosUmaSize(&transport, &umaMb, &vramBytes);
    if (status != BC250_CMOS_VRAM_OK || umaMb != 16384 || vramBytes != 16384ULL * 1024ULL * 1024ULL) {
        printf("[FAIL] Test 5: Expected 16384MB OK, got status=%d umaMb=%u\n", status, umaMb);
        return 1;
    }
    printf("[PASS] Test 5: 16384 MB (16 GB) detected correctly (%llu bytes)\n", vramBytes);

    /* Test 6: Invalid Signature (e.g. 0x00000000 or corrupted CMOS) */
    SetupMockApcb(512, 0x12345678);
    status = Bc250ReadCmosUmaSize(&transport, &umaMb, &vramBytes);
    if (status != BC250_CMOS_VRAM_BAD_SIGNATURE) {
        printf("[FAIL] Test 6: Expected BAD_SIGNATURE, got %d (%s)\n", status, Bc250CmosVramStatusName(status));
        return 1;
    }
    printf("[PASS] Test 6: Corrupted/missing APCB signature rejected\n");

    /* Test 7: Out of range (< 256MB) */
    SetupMockApcb(64, BC250_CMOS_APCB_SIGNATURE);
    status = Bc250ReadCmosUmaSize(&transport, &umaMb, &vramBytes);
    if (status != BC250_CMOS_VRAM_OUT_OF_RANGE) {
        printf("[FAIL] Test 7: Expected OUT_OF_RANGE, got %d (%s)\n", status, Bc250CmosVramStatusName(status));
        return 1;
    }
    printf("[PASS] Test 7: Under-sized allocation (<256MB) rejected\n");

    /* Test 8: Non-aligned size */
    SetupMockApcb(513, BC250_CMOS_APCB_SIGNATURE);
    status = Bc250ReadCmosUmaSize(&transport, &umaMb, &vramBytes);
    if (status != BC250_CMOS_VRAM_OUT_OF_RANGE) {
        printf("[FAIL] Test 8: Expected OUT_OF_RANGE for unaligned size, got %d\n", status);
        return 1;
    }
    printf("[PASS] Test 8: Unaligned size rejected\n");

    printf("\n>>> ALL BC-250 CMOS VRAM CONTRACT TESTS PASSED! <<<\n");
    return 0;
}
