#include "bc250_compute_dispatch_contract.h"
#include "bc250_pm4_packet_builder.h"

#include <string.h>

static int Bc250DispatchIsPowerOfTwo(uint32_t Value)
{
    return Value != 0u && (Value & (Value - 1u)) == 0u;
}

static BC250_COMPUTE_DISPATCH_PLAN_STATUS Bc250DispatchValidateDescriptor(
    const BC250_COMPUTE_DISPATCH_DESCRIPTOR *Descriptor)
{
    if (Descriptor == NULL ||
        Descriptor->NumThreadX == 0u ||
        Descriptor->NumThreadY == 0u ||
        Descriptor->NumThreadZ == 0u ||
        Descriptor->GridDimX == 0u ||
        Descriptor->GridDimY == 0u ||
        Descriptor->GridDimZ == 0u ||
        Descriptor->ResultPoison == Descriptor->ResultExpected ||
        Descriptor->FencePoison == Descriptor->FenceExpected) {
        return BC250_COMPUTE_DISPATCH_PLAN_BAD_ARGUMENT;
    }

    /* PGM address is encoded in 256-byte units on GFX10. */
    if (Descriptor->ShaderGpuAddress == 0u ||
        (Descriptor->ShaderGpuAddress & 0xFFull) != 0u ||
        Descriptor->ResultGpuAddress == 0u ||
        (Descriptor->ResultGpuAddress & 3ull) != 0u ||
        Descriptor->FenceGpuAddress == 0u ||
        (Descriptor->FenceGpuAddress & 7ull) != 0u ||
        Descriptor->ResultGpuAddress == Descriptor->FenceGpuAddress) {
        return BC250_COMPUTE_DISPATCH_PLAN_BAD_ADDRESS;
    }

    /* The first shader uses user SGPR s[0:1] for the 64-bit result address. */
    if ((Descriptor->PgmRsrc2 & 0x3Eu) < (2u << 1)) {
        return BC250_COMPUTE_DISPATCH_PLAN_BAD_RESOURCE;
    }

    if (Descriptor->WaveSize != BC250_WAVE32 &&
        Descriptor->WaveSize != BC250_WAVE64) {
        return BC250_COMPUTE_DISPATCH_PLAN_BAD_RESOURCE;
    }

    return BC250_COMPUTE_DISPATCH_PLAN_OK;
}

BC250_COMPUTE_DISPATCH_PLAN_STATUS Bc250BuildObservableComputeDispatchPlan(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    const BC250_COMPUTE_DISPATCH_DESCRIPTOR *Descriptor,
    BC250_COMPUTE_DISPATCH_PLAN_EVIDENCE *Evidence)
{
    BC250_COMPUTE_DISPATCH_PLAN_STATUS status;
    uint32_t cursor = 0u;
    uint32_t values[3];
    uint32_t initiator;
    uint32_t packetIndex;

    if (Evidence != NULL) memset(Evidence, 0, sizeof(*Evidence));
    if (RingBuffer == NULL || Evidence == NULL ||
        !Bc250DispatchIsPowerOfTwo(RingDwords) ||
        RingDwords < BC250_DISPATCH_RING_MIN_DWORDS) {
        return BC250_COMPUTE_DISPATCH_PLAN_RING_TOO_SMALL;
    }

    status = Bc250DispatchValidateDescriptor(Descriptor);
    if (status != BC250_COMPUTE_DISPATCH_PLAN_OK) return status;

    /* Only the transaction-owned prefix is touched; callers can snapshot it. */
    memset(RingBuffer, 0,
        BC250_DISPATCH_RING_MIN_DWORDS * sizeof(uint32_t));

    Evidence->ProgramPacketIndex = cursor;
    values[0] = (uint32_t)(Descriptor->ShaderGpuAddress >> 8);
    values[1] = (uint32_t)(Descriptor->ShaderGpuAddress >> 40);
    if (Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, &cursor,
            BC250_DISPATCH_REG_COMPUTE_PGM_LO, values, 2u) != BC250_PM4_OK) {
        return BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
    }
    /* Every SET_SH_REG in a compute queue must carry SHADER_TYPE=COMPUTE. */
    RingBuffer[Evidence->ProgramPacketIndex] |= BC250_PM4_SHADER_TYPE_COMPUTE;

    Evidence->ResourcePacketIndex = cursor;
    values[0] = Descriptor->PgmRsrc1;
    values[1] = Descriptor->PgmRsrc2;
    if (Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, &cursor,
            BC250_DISPATCH_REG_COMPUTE_PGM_RSRC1, values, 2u) != BC250_PM4_OK) {
        return BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
    }
    RingBuffer[Evidence->ResourcePacketIndex] |= BC250_PM4_SHADER_TYPE_COMPUTE;

    packetIndex = cursor;
    values[0] = 0u;
    if (Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, &cursor,
            BC250_DISPATCH_REG_COMPUTE_RESOURCE_LIMITS, values, 1u) != BC250_PM4_OK) {
        return BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
    }
    RingBuffer[packetIndex] |= BC250_PM4_SHADER_TYPE_COMPUTE;

    packetIndex = cursor;
    values[0] = 0u;
    values[1] = 0u;
    values[2] = 0u;
    if (Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, &cursor,
            BC250_DISPATCH_REG_COMPUTE_START_X, values, 3u) != BC250_PM4_OK) {
        return BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
    }
    RingBuffer[packetIndex] |= BC250_PM4_SHADER_TYPE_COMPUTE;

    packetIndex = cursor;
    values[0] = Descriptor->NumThreadX;
    values[1] = Descriptor->NumThreadY;
    values[2] = Descriptor->NumThreadZ;
    if (Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, &cursor,
            BC250_DISPATCH_REG_COMPUTE_NUM_THREAD_X, values, 3u) != BC250_PM4_OK) {
        return BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
    }
    RingBuffer[packetIndex] |= BC250_PM4_SHADER_TYPE_COMPUTE;

    Evidence->UserDataPacketIndex = cursor;
    values[0] = (uint32_t)(Descriptor->ResultGpuAddress & 0xFFFFFFFFull);
    values[1] = (uint32_t)(Descriptor->ResultGpuAddress >> 32);
    if (Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, &cursor,
            BC250_DISPATCH_REG_COMPUTE_USER_DATA_0, values, 2u) != BC250_PM4_OK) {
        return BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
    }
    RingBuffer[Evidence->UserDataPacketIndex] |= BC250_PM4_SHADER_TYPE_COMPUTE;

    initiator = BC250_DISPATCH_COMPUTE_SHADER_EN |
                BC250_DISPATCH_FORCE_START_000;
    if (Descriptor->WaveSize == BC250_WAVE32) {
        initiator |= BC250_DISPATCH_CS_W32_EN;
    }

    Evidence->DispatchPacketIndex = cursor;
    if (Bc250BuildComputeDispatchPacketEx(
            RingBuffer, RingDwords, &cursor,
            Descriptor->GridDimX,
            Descriptor->GridDimY,
            Descriptor->GridDimZ,
            initiator) != BC250_ISA_OK) {
        return BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
    }

    /* The flush is after DISPATCH_DIRECT and before the physical fence. */
    Evidence->PartialFlushPacketIndex = cursor;
    if (Bc250Pm4EmitEventWrite(
            RingBuffer, RingDwords, &cursor,
            BC250_PM4_EVENT_CS_PARTIAL_FLUSH) != BC250_PM4_OK) {
        return BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
    }

    /* Exact 8-dword RELEASE_MEM form already proven by the active HQD gate. */
    Evidence->FencePacketIndex = cursor;
    if (cursor + 8u + 5u > RingDwords) {
        return BC250_COMPUTE_DISPATCH_PLAN_RING_TOO_SMALL;
    }
    RingBuffer[cursor++] = 0xC0064900u;
    RingBuffer[cursor++] = 0x06603514u;
    RingBuffer[cursor++] = 0x20000000u;
    RingBuffer[cursor++] = (uint32_t)(Descriptor->FenceGpuAddress & 0xFFFFFFFFull);
    RingBuffer[cursor++] = (uint32_t)(Descriptor->FenceGpuAddress >> 32);
    RingBuffer[cursor++] = Descriptor->FenceExpected;
    RingBuffer[cursor++] = 0u;
    RingBuffer[cursor++] = 0u;

    /* WRITE_DATA direto na memória física comprovado no Direct Load */
    RingBuffer[cursor++] = 0xC0033700u;
    RingBuffer[cursor++] = 0x00100500u;
    RingBuffer[cursor++] = (uint32_t)(Descriptor->FenceGpuAddress & 0xFFFFFFFFull);
    RingBuffer[cursor++] = (uint32_t)(Descriptor->FenceGpuAddress >> 32);
    RingBuffer[cursor++] = Descriptor->FenceExpected;

    Evidence->SubmittedDwords = cursor;
    Evidence->DispatchInitiator = initiator;
    Evidence->FenceIsAfterDispatch =
        (Evidence->FencePacketIndex > Evidence->DispatchPacketIndex) ? 1u : 0u;
    /* A packet plan is never physical proof. */
    Evidence->PhysicalReady = 0u;

    return Evidence->FenceIsAfterDispatch != 0u ?
        BC250_COMPUTE_DISPATCH_PLAN_OK :
        BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED;
}

BC250_COMPUTE_DISPATCH_PLAN_STATUS Bc250EvaluateObservableComputeDispatch(
    const BC250_COMPUTE_DISPATCH_DESCRIPTOR *Descriptor,
    const BC250_COMPUTE_DISPATCH_OBSERVATION *Observation,
    BC250_COMPUTE_DISPATCH_VERDICT *Verdict)
{
    BC250_COMPUTE_DISPATCH_PLAN_STATUS status;

    if (Verdict != NULL) memset(Verdict, 0, sizeof(*Verdict));
    if (Observation == NULL || Verdict == NULL) {
        return BC250_COMPUTE_DISPATCH_PLAN_BAD_ARGUMENT;
    }
    status = Bc250DispatchValidateDescriptor(Descriptor);
    if (status != BC250_COMPUTE_DISPATCH_PLAN_OK) return status;

    Verdict->RptrWptrPassed =
        (Observation->SubmittedDwords != 0u &&
         Observation->RptrAfter == Observation->SubmittedDwords &&
         Observation->WptrAfter == Observation->SubmittedDwords) ? 1u : 0u;
    Verdict->FencePassed =
        (Observation->FenceBefore == Descriptor->FencePoison &&
         Observation->FenceAfter == Descriptor->FenceExpected) ? 1u : 0u;
    Verdict->ResultPassed =
        (Observation->ResultBefore == Descriptor->ResultPoison &&
         Observation->ResultAfter == Descriptor->ResultExpected &&
         Observation->ResultMismatchCount == 0u) ? 1u : 0u;
    Verdict->RollbackPassed =
        (Observation->RegistersRestorePassed != 0u &&
         Observation->MemoryRestorePassed != 0u) ? 1u : 0u;
    Verdict->TriadPassed =
        (Verdict->RptrWptrPassed != 0u &&
         Verdict->FencePassed != 0u &&
         Verdict->ResultPassed != 0u) ? 1u : 0u;
    Verdict->PhysicalReady =
        (Verdict->TriadPassed != 0u &&
         Verdict->RollbackPassed != 0u) ? 1u : 0u;

    return BC250_COMPUTE_DISPATCH_PLAN_OK;
}

const char *Bc250ComputeDispatchPlanStatusName(
    BC250_COMPUTE_DISPATCH_PLAN_STATUS Status)
{
    switch (Status) {
    case BC250_COMPUTE_DISPATCH_PLAN_OK: return "ok";
    case BC250_COMPUTE_DISPATCH_PLAN_BAD_ARGUMENT: return "bad_argument";
    case BC250_COMPUTE_DISPATCH_PLAN_BAD_ADDRESS: return "bad_address";
    case BC250_COMPUTE_DISPATCH_PLAN_BAD_RESOURCE: return "bad_resource";
    case BC250_COMPUTE_DISPATCH_PLAN_RING_TOO_SMALL: return "ring_too_small";
    case BC250_COMPUTE_DISPATCH_PLAN_PACKET_FAILED: return "packet_failed";
    default: return "unknown";
    }
}
