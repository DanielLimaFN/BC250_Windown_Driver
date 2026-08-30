#include "../core/bc250_compute_dispatch_contract.h"

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

static void Bc250InitDescriptor(BC250_COMPUTE_DISPATCH_DESCRIPTOR *Descriptor)
{
    memset(Descriptor, 0, sizeof(*Descriptor));
    Descriptor->ShaderGpuAddress = 0x00000000F4010000ull;
    Descriptor->ResultGpuAddress = 0x00000000F4012000ull;
    Descriptor->FenceGpuAddress = 0x00000000F4012200ull;
    Descriptor->PgmRsrc1 = 0x00200040u;
    Descriptor->PgmRsrc2 = 0x00000004u; /* USER_SGPR=2 */
    Descriptor->WaveSize = BC250_WAVE32;
    Descriptor->NumThreadX = 1u;
    Descriptor->NumThreadY = 1u;
    Descriptor->NumThreadZ = 1u;
    Descriptor->GridDimX = 1u;
    Descriptor->GridDimY = 1u;
    Descriptor->GridDimZ = 1u;
    Descriptor->ResultPoison = BC250_DISPATCH_RESULT_POISON;
    Descriptor->ResultExpected = BC250_DISPATCH_RESULT_VALUE;
    Descriptor->FencePoison = BC250_DISPATCH_FENCE_POISON;
    Descriptor->FenceExpected = BC250_DISPATCH_FENCE_VALUE;
}

static int Bc250RunComputeDispatchContractTest(void)
{
    BC250_COMPUTE_DISPATCH_DESCRIPTOR descriptor;
    BC250_COMPUTE_DISPATCH_PLAN_EVIDENCE plan;
    BC250_COMPUTE_DISPATCH_OBSERVATION observation;
    BC250_COMPUTE_DISPATCH_VERDICT verdict;

    Bc250InitDescriptor(&descriptor);
    memset(g_ring, 0xCC, sizeof(g_ring));
    CHECK(Bc250BuildObservableComputeDispatchPlan(
        g_ring, TEST_RING_DWORDS, &descriptor, &plan) ==
        BC250_COMPUTE_DISPATCH_PLAN_OK);

    CHECK(plan.PhysicalReady == 0u);
    CHECK(plan.FenceIsAfterDispatch == 1u);
    CHECK(plan.FencePacketIndex > plan.PartialFlushPacketIndex);
    CHECK(g_ring[plan.ProgramPacketIndex] == 0xC0027602u);
    CHECK(g_ring[plan.ResourcePacketIndex] == 0xC0027602u);
    CHECK(g_ring[8] == 0xC0017602u);
    CHECK(g_ring[11] == 0xC0037602u);
    CHECK(g_ring[16] == 0xC0037602u);
    CHECK(g_ring[plan.UserDataPacketIndex] == 0xC0027602u);
    CHECK(g_ring[plan.DispatchPacketIndex] == BC250_PM4_HEADER_DISPATCH_DIRECT);
    CHECK(g_ring[plan.DispatchPacketIndex + 1u] == 1u);
    CHECK(g_ring[plan.DispatchPacketIndex + 2u] == 1u);
    CHECK(g_ring[plan.DispatchPacketIndex + 3u] == 1u);
    CHECK(g_ring[plan.DispatchPacketIndex + 4u] ==
        (BC250_DISPATCH_COMPUTE_SHADER_EN |
         BC250_DISPATCH_FORCE_START_000 |
         BC250_DISPATCH_CS_W32_EN));
    CHECK(g_ring[plan.FencePacketIndex] == 0xC0064900u);
    CHECK(g_ring[plan.FencePacketIndex + 5u] == descriptor.FenceExpected);

    /* PGM_LO/HI are shaderAddress >> 8 split into low/high dwords. */
    CHECK(g_ring[plan.ProgramPacketIndex + 1u] ==
        (BC250_DISPATCH_REG_COMPUTE_PGM_LO - 0x2C00u));
    CHECK(g_ring[plan.ProgramPacketIndex + 2u] ==
        (uint32_t)(descriptor.ShaderGpuAddress >> 8));
    CHECK(g_ring[plan.ProgramPacketIndex + 3u] ==
        (uint32_t)(descriptor.ShaderGpuAddress >> 40));

    memset(&observation, 0, sizeof(observation));
    observation.SubmittedDwords = plan.SubmittedDwords;
    observation.RptrAfter = plan.SubmittedDwords;
    observation.WptrAfter = plan.SubmittedDwords;
    observation.ResultBefore = descriptor.ResultPoison;
    observation.ResultAfter = descriptor.ResultExpected;
    observation.FenceBefore = descriptor.FencePoison;
    observation.FenceAfter = descriptor.FenceExpected;
    observation.RegistersRestorePassed = 1u;
    observation.MemoryRestorePassed = 1u;
    CHECK(Bc250EvaluateObservableComputeDispatch(
        &descriptor, &observation, &verdict) == BC250_COMPUTE_DISPATCH_PLAN_OK);
    CHECK(verdict.TriadPassed == 1u);
    CHECK(verdict.PhysicalReady == 1u);

    /* Fence alone must never validate a shader result. */
    observation.ResultAfter = descriptor.ResultPoison;
    CHECK(Bc250EvaluateObservableComputeDispatch(
        &descriptor, &observation, &verdict) == BC250_COMPUTE_DISPATCH_PLAN_OK);
    CHECK(verdict.FencePassed == 1u);
    CHECK(verdict.ResultPassed == 0u);
    CHECK(verdict.TriadPassed == 0u);
    CHECK(verdict.PhysicalReady == 0u);

    /* Result + fence without strict RPTR==WPTR must fail closed. */
    observation.ResultAfter = descriptor.ResultExpected;
    observation.RptrAfter = plan.SubmittedDwords - 1u;
    CHECK(Bc250EvaluateObservableComputeDispatch(
        &descriptor, &observation, &verdict) == BC250_COMPUTE_DISPATCH_PLAN_OK);
    CHECK(verdict.RptrWptrPassed == 0u);
    CHECK(verdict.PhysicalReady == 0u);

    /* A successful triplet without verified rollback is not PhysicalReady. */
    observation.RptrAfter = plan.SubmittedDwords;
    observation.MemoryRestorePassed = 0u;
    CHECK(Bc250EvaluateObservableComputeDispatch(
        &descriptor, &observation, &verdict) == BC250_COMPUTE_DISPATCH_PLAN_OK);
    CHECK(verdict.TriadPassed == 1u);
    CHECK(verdict.RollbackPassed == 0u);
    CHECK(verdict.PhysicalReady == 0u);

    /* Invalid address/resource inputs are rejected before packet emission. */
    Bc250InitDescriptor(&descriptor);
    descriptor.ShaderGpuAddress += 4u;
    CHECK(Bc250BuildObservableComputeDispatchPlan(
        g_ring, TEST_RING_DWORDS, &descriptor, &plan) ==
        BC250_COMPUTE_DISPATCH_PLAN_BAD_ADDRESS);
    Bc250InitDescriptor(&descriptor);
    descriptor.PgmRsrc2 = 0u;
    CHECK(Bc250BuildObservableComputeDispatchPlan(
        g_ring, TEST_RING_DWORDS, &descriptor, &plan) ==
        BC250_COMPUTE_DISPATCH_PLAN_BAD_RESOURCE);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunComputeDispatchContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunComputeDispatchContractTest() ? 0 : 1;
}
#endif
