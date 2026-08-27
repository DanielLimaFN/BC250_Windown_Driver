#include "../core/bc250_ih_contract.h"

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

#define TEST_IH_RING_SIZE 256u

static uint32_t g_ihRing[TEST_IH_RING_SIZE];
static BC250_IH_STATE g_ihState;
static BC250_IH_RING_PROPERTIES g_ihProps;
static BC250_IH_VECTOR_ENTRY g_outEntries[16];

static int Bc250RunIhContractTest(void)
{
    uint32_t rawDwords[4];
    BC250_IH_VECTOR_ENTRY inEntry;
    BC250_IH_VECTOR_ENTRY decodedEntry;
    uint32_t outCount = 0;

    memset(g_ihRing, 0, sizeof(g_ihRing));
    memset(&g_ihState, 0, sizeof(g_ihState));
    memset(&g_ihProps, 0, sizeof(g_ihProps));
    memset(g_outEntries, 0, sizeof(g_outEntries));

    /* 1. Test Initialization */
    g_ihProps.RingGpuAddress = 0x0000080000000000ull;
    g_ihProps.RingSizeDwords = TEST_IH_RING_SIZE;
    g_ihProps.WptrGpuAddress = 0x0000080000001000ull;
    g_ihProps.UseDoorbell = 1u;
    g_ihProps.DoorbellIndex = 0x10u;

    CHECK(Bc250IhInit(&g_ihState, &g_ihProps) == BC250_IH_OK);
    CHECK(g_ihState.RingSizeDwords == TEST_IH_RING_SIZE);
    CHECK(g_ihState.RptrDwords == 0);
    CHECK(g_ihState.WptrDwords == 0);

    /* 2. Test Encode / Decode SDMA Trap Vector */
    memset(&inEntry, 0, sizeof(inEntry));
    inEntry.ClientId = BC250_IH_CLIENTID_SDMA0;
    inEntry.SrcId = BC250_IH_SRCID_SDMA_TRAP;
    inEntry.RingId = 0u;
    inEntry.Vmid = 0u;
    inEntry.Timestamp = 0x123456789ABCDEF0ull;
    inEntry.Pasid = 0x0042u;
    inEntry.ContextData = 0xCAFEu;

    memset(rawDwords, 0, sizeof(rawDwords));
    CHECK(Bc250IhEncodeEntry(&inEntry, rawDwords) == BC250_IH_OK);

    memset(&decodedEntry, 0, sizeof(decodedEntry));
    CHECK(Bc250IhDecodeEntry(rawDwords, &decodedEntry) == BC250_IH_OK);

    CHECK(decodedEntry.ClientId == BC250_IH_CLIENTID_SDMA0);
    CHECK(decodedEntry.SrcId == BC250_IH_SRCID_SDMA_TRAP);
    CHECK(decodedEntry.Timestamp == 0x123456789ABCDEF0ull);
    CHECK(decodedEntry.Pasid == 0x0042u);
    CHECK(decodedEntry.ContextData == 0xCAFEu);

    /* 3. Test Encode / Decode CP EOP Vector */
    memset(&inEntry, 0, sizeof(inEntry));
    inEntry.ClientId = BC250_IH_CLIENTID_GRBM_CP;
    inEntry.SrcId = BC250_IH_SRCID_CP_EOP;
    inEntry.RingId = 1u;
    inEntry.Vmid = 2u;
    inEntry.Timestamp = 0xFEDCBA9876543210ull;
    inEntry.Pasid = 0x0084u;
    inEntry.ContextData = 0xBEEFu;

    CHECK(Bc250IhEncodeEntry(&inEntry, rawDwords) == BC250_IH_OK);
    CHECK(Bc250IhDecodeEntry(rawDwords, &decodedEntry) == BC250_IH_OK);

    CHECK(decodedEntry.ClientId == BC250_IH_CLIENTID_GRBM_CP);
    CHECK(decodedEntry.SrcId == BC250_IH_SRCID_CP_EOP);
    CHECK(decodedEntry.RingId == 1u);
    CHECK(decodedEntry.Vmid == 2u);
    CHECK(decodedEntry.Timestamp == 0xFEDCBA9876543210ull);
    CHECK(decodedEntry.Pasid == 0x0084u);
    CHECK(decodedEntry.ContextData == 0xBEEFu);

    /* 4. Test Ring Processing and Event Counting */
    /* Write 1 SDMA Trap and 1 CP EOP entry directly to ring */
    inEntry.ClientId = BC250_IH_CLIENTID_SDMA0;
    inEntry.SrcId = BC250_IH_SRCID_SDMA_TRAP;
    CHECK(Bc250IhEncodeEntry(&inEntry, &g_ihRing[0]) == BC250_IH_OK);

    inEntry.ClientId = BC250_IH_CLIENTID_GRBM_CP;
    inEntry.SrcId = BC250_IH_SRCID_CP_EOP;
    CHECK(Bc250IhEncodeEntry(&inEntry, &g_ihRing[4]) == BC250_IH_OK);

    CHECK(Bc250IhCheckInterruptNeeded(&g_ihState, 8u) == 1);

    CHECK(Bc250IhProcessRing(
        &g_ihState, g_ihRing, 8u, g_outEntries, 16u, &outCount) == BC250_IH_OK);
    CHECK(outCount == 2u);
    CHECK(g_ihState.SdmaFenceEvents == 1u);
    CHECK(g_ihState.ComputeFenceEvents == 1u);
    CHECK(g_ihState.ProcessedCount == 2u);
    CHECK(g_ihState.RptrDwords == 8u);
    CHECK(Bc250IhCheckInterruptNeeded(&g_ihState, 8u) == 0);

    /* 5. Test Ring Wraparound */
    g_ihState.RptrDwords = TEST_IH_RING_SIZE - 4u;
    CHECK(Bc250IhEncodeEntry(&inEntry, &g_ihRing[TEST_IH_RING_SIZE - 4u]) == BC250_IH_OK);
    CHECK(Bc250IhProcessRing(
        &g_ihState, g_ihRing, 0u, g_outEntries, 16u, &outCount) == BC250_IH_OK);
    CHECK(outCount == 1u);
    CHECK(g_ihState.RptrDwords == 0u);

    /* 6. Test Error Handling */
    CHECK(Bc250IhInit(NULL, &g_ihProps) == BC250_IH_BAD_ARGUMENT);
    g_ihProps.RingSizeDwords = 30u; /* Not power of 2 */
    CHECK(Bc250IhInit(&g_ihState, &g_ihProps) == BC250_IH_INVALID_RING_SIZE);
    g_ihProps.RingSizeDwords = TEST_IH_RING_SIZE;

    g_ihProps.RingGpuAddress = 0x0000080000000004ull; /* Not 256 aligned */
    CHECK(Bc250IhInit(&g_ihState, &g_ihProps) == BC250_IH_MISALIGNED_ADDRESS);
    g_ihProps.RingGpuAddress = 0x0000080000000000ull;

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunIhContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunIhContractTest() ? 0 : 1;
}
#endif
