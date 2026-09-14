#include "../core/bc250_gfxhub_tlb_transaction.h"

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
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

typedef struct BC250_TEST_TLB_TRANSPORT {
    uint32_t Request;
    uint32_t Ack;
    uint32_t RangeLow;
    uint32_t RangeHigh;
    uint32_t AckOnRequest;
    uint32_t StallCount;
} BC250_TEST_TLB_TRANSPORT;

static uint32_t Bc250TestRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    BC250_TEST_TLB_TRANSPORT *transport =
        (BC250_TEST_TLB_TRANSPORT *)Context;
    if (transport == NULL || Value == NULL) return 0u;
    switch (ByteOffset) {
    case 0xA2D0u: *Value = transport->Request; return 1u;
    case 0xA318u: *Value = transport->Ack; return 1u;
    case 0xA3A4u: *Value = transport->RangeLow; return 1u;
    case 0xA3A8u: *Value = transport->RangeHigh; return 1u;
    default: return 0u;
    }
}

static uint32_t Bc250TestWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value)
{
    BC250_TEST_TLB_TRANSPORT *transport =
        (BC250_TEST_TLB_TRANSPORT *)Context;
    if (transport == NULL) return 0u;
    switch (ByteOffset) {
    case 0xA2D0u:
        transport->Request = Value;
        if (transport->AckOnRequest != 0u) transport->Ack |= Value & 0xFFFFu;
        return 1u;
    case 0xA3A4u: transport->RangeLow = Value; return 1u;
    case 0xA3A8u: transport->RangeHigh = Value; return 1u;
    default: return 0u;
    }
}

static void Bc250TestStall(void *Context, uint32_t Microseconds)
{
    BC250_TEST_TLB_TRANSPORT *transport =
        (BC250_TEST_TLB_TRANSPORT *)Context;
    if (transport != NULL) transport->StallCount += Microseconds;
}

static int Bc250RunGfxhubTlbTransactionTest(void)
{
    BC250_PAGING_FLUSH_TLB_PARAMS params;
    BC250_GFXHUB_TLB_FLUSH_PLAN plan;
    BC250_TLB_TRANSACTION_INTERFACE interfaceData;
    BC250_TLB_TRANSACTION_EVIDENCE evidence;
    BC250_TEST_TLB_TRANSPORT transport;

    memset(&params, 0, sizeof(params));
    params.Vmid = 8u;
    CHECK(Bc250PagingBuildGfxhubTlbFlushPlan(&params, &plan) ==
        BC250_PAGING_OK);
    memset(&interfaceData, 0, sizeof(interfaceData));
    interfaceData.Read32 = Bc250TestRead32;
    interfaceData.Write32 = Bc250TestWrite32;
    interfaceData.Stall = Bc250TestStall;
    interfaceData.Identity = BC250_TLB_TRANSPORT_IDENTITY;

    memset(&transport, 0, sizeof(transport));
    transport.RangeLow = 0x12345678u;
    transport.RangeHigh = 0x9u;
    transport.AckOnRequest = 1u;
    interfaceData.Context = &transport;
    CHECK(Bc250RunGfxhubTlbTransaction(
        &plan, &interfaceData, 100u, &evidence) == BC250_TLB_TRANSACTION_OK);
    CHECK(evidence.RequestWritten == 1u);
    CHECK(evidence.DummyReadCompleted == 1u);
    CHECK(evidence.AckObserved == 1u && evidence.AckAfter == 0x100u);
    CHECK(evidence.PollCount == 1u && evidence.PhysicalReady == 1u);
    CHECK(evidence.RangeRestorePassed == 1u);
    CHECK(transport.RangeLow == 0x12345678u && transport.RangeHigh == 0x9u);

    memset(&transport, 0, sizeof(transport));
    transport.RangeLow = 0x11111111u;
    transport.RangeHigh = 0x2u;
    interfaceData.Context = &transport;
    CHECK(Bc250RunGfxhubTlbTransaction(
        &plan, &interfaceData, 3u, &evidence) ==
        BC250_TLB_TRANSACTION_TIMEOUT);
    CHECK(evidence.PollCount == 3u && evidence.PhysicalReady == 0u);
    CHECK(evidence.RangeRestorePassed == 1u && transport.StallCount == 3u);
    CHECK(transport.RangeLow == 0x11111111u && transport.RangeHigh == 0x2u);

    memset(&transport, 0, sizeof(transport));
    transport.Ack = 0xFFFFFFFFu;
    interfaceData.Context = &transport;
    CHECK(Bc250RunGfxhubTlbTransaction(
        &plan, &interfaceData, 3u, &evidence) ==
        BC250_TLB_TRANSACTION_UNDECODED);
    CHECK(evidence.RequestWritten == 0u && evidence.PhysicalReady == 0u);
    CHECK(evidence.RangeRestorePassed == 1u);
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGfxhubTlbTransactionTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGfxhubTlbTransactionTest() ? 0 : 1;
}
#endif
