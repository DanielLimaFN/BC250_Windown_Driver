#include "../core/bc250_compute_mqd_contract.h"

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

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) { printf("CHECK failed at line %d\n", __LINE__); return __LINE__; } } while (0)
#endif

static int Bc250TestStringEquals(const char *A, const char *B)
{
    if (A == NULL || B == NULL) return A == B;
    while (*A && *B) {
        if (*A != *B) return 0;
        ++A;
        ++B;
    }
    return *A == *B;
}

static BC250_GFX10_COMPUTE_MQD g_testMqd;
static BC250_COMPUTE_MQD_PROPERTIES g_testProps;
static uint32_t g_hqdPass1[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
static uint32_t g_hqdPass2[BC250_HQD_PREFLIGHT_REGISTER_COUNT];
static BC250_HQD_READONLY_SNAPSHOT g_hqdSnapshot;
static uint32_t g_selectorRegister;
static uint32_t g_selectedHqdActive;
static uint32_t g_ignoreSelectorWrites;
static uint32_t g_hqdBankRegs[2][65];
static uint32_t g_forceReadbackError;
static uint32_t g_forceAllOnes;
static uint32_t g_forceActiveOnReadback;
static uint32_t g_forceWptrOnReadback;
static uint32_t g_forceRestoreError;

static unsigned char g_testMqdBuffer[512];
static uint32_t g_testRingBuffer[16384];
static unsigned char g_testEopBuffer[4096];
static uint32_t g_testRptrBuffer[2];
static uint32_t g_testWptrBuffer[2];
static uint32_t g_testFenceBuffer[2];
static uint32_t g_testDoorbellBuffer[1024];
static uint64_t g_testPtBuffer[512];

static uint32_t g_scratchRegister = 0x12345678u;
static uint32_t g_activeRegister = 0u;
static uint32_t g_rptrRegister = 0u;
static uint32_t g_wptrRegister = 0u;
static uint32_t g_cpMecCntl = 0x50000000u;
static uint32_t g_cpMeCntl = 0x50000000u;
static uint32_t g_dbRangeLo = 0u;
static uint32_t g_dbRangeHi = 0u;
static uint32_t g_cpPqStatus = 0u;
static uint32_t g_cpPqWptrPollCntl = 0u;
static uint32_t g_rlcCpSchedulers = 0x58504840u;
static uint32_t g_gcvmL2Cntl2 = 0x12340000u;
static uint32_t g_gcvmCtx0Cntl = 0u;
static uint32_t g_gcvmCtx0PtBaseLo = 0u;
static uint32_t g_gcvmCtx0PtBaseHi = 0u;
static uint32_t g_gcvmCtx0PtStartLo = 0u;
static uint32_t g_gcvmCtx0PtEndLo = 0u;
static uint32_t g_gcvmEng0Req = 0u;
static uint32_t g_gcvmInvalidateReq = 0u;
static uint32_t g_gcvmInvalidateAck = BC250_HQD_GCVM_INVALIDATE_VMID0;
static uint32_t g_hdpInvalidate = 0u;
static uint32_t g_simulateComputeExecution = 0u;
static uint32_t g_requireInactiveForWptrRestore = 0u;
static uint32_t g_seenActiveOne = 0u;

static uint32_t Bc250TestHqdRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    uint32_t bank = ((g_selectorRegister & 0xCu) == 4u ||
                     (g_selectorRegister & 0xCu) == 8u) ? 1u : 0u;
    (void)Context;
    if (Value == NULL) return 0u;
    if (g_forceAllOnes) {
        *Value = 0xFFFFFFFFu;
        return 1u;
    }
    if (ByteOffset == 0x00030100u) {
        *Value = g_scratchRegister;
        return 1u;
    }
    if (ByteOffset == 0x00008234u) {
        *Value = g_cpMecCntl;
        return 1u;
    }
    if (ByteOffset == 0x000086D8u) {
        *Value = g_cpMeCntl;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_CP_MEC_DB_RANGE_LOWER) {
        *Value = g_dbRangeLo;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_CP_MEC_DB_RANGE_UPPER) {
        *Value = g_dbRangeHi;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_CP_PQ_STATUS) {
        *Value = g_cpPqStatus;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL) {
        *Value = g_cpPqWptrPollCntl;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_RLC_CP_SCHEDULERS) {
        *Value = g_rlcCpSchedulers;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE) {
        *Value = g_hdpInvalidate;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_L2_CNTL2) {
        *Value = g_gcvmL2Cntl2;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_CNTL) {
        *Value = g_gcvmCtx0Cntl;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_LO) {
        *Value = g_gcvmCtx0PtBaseLo;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_HI) {
        *Value = g_gcvmCtx0PtBaseHi;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_PT_START_LO) {
        *Value = g_gcvmCtx0PtStartLo;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_PT_END_LO) {
        *Value = g_gcvmCtx0PtEndLo;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_INVALIDATE_REQ) {
        *Value = g_gcvmInvalidateReq;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_INVALIDATE_ACK) {
        *Value = g_gcvmInvalidateAck;
        return 1u;
    }
    if (ByteOffset == BC250_GRBM_GFX_CNTL_BYTE_OFFSET) {
        *Value = g_selectorRegister;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_ACTIVE) {
        if (g_forceActiveOnReadback) {
            *Value = 1u;
            return 1u;
        }
        if (g_selectedHqdActive) {
            *Value = g_selectedHqdActive;
            return 1u;
        }
        *Value = g_activeRegister;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_PQ_RPTR) {
        *Value = g_rptrRegister;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_PQ_WPTR_LO || ByteOffset == BC250_HQD_MMIO_PQ_WPTR_HI) {
        if (g_forceWptrOnReadback) {
            *Value = 4u;
            return 1u;
        }
        *Value = ByteOffset == BC250_HQD_MMIO_PQ_WPTR_LO ?
            g_wptrRegister : 0u;
        return 1u;
    }
    if (ByteOffset >= 0xC800u && ByteOffset <= 0xC900u) {
        uint32_t idx = (ByteOffset - 0xC800u) >> 2;
        if (g_forceReadbackError) {
            *Value = 0x12345678u;
        } else {
            *Value = g_hqdBankRegs[bank][idx];
        }
        return 1u;
    }
    *Value = 0u;
    return 1u;
}

static uint32_t Bc250TestHqdWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value)
{
    uint32_t bank = ((g_selectorRegister & 0xCu) == 4u ||
                     (g_selectorRegister & 0xCu) == 8u) ? 1u : 0u;
    (void)Context;
    if (ByteOffset == 0x00030100u) {
        g_scratchRegister = Value;
        return 1u;
    }
    if (ByteOffset == 0x00008234u) {
        g_cpMecCntl = Value;
        return 1u;
    }
    if (ByteOffset == 0x000086D8u) {
        g_cpMeCntl = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_CP_MEC_DB_RANGE_LOWER) {
        g_dbRangeLo = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_CP_MEC_DB_RANGE_UPPER) {
        g_dbRangeHi = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_CP_PQ_STATUS) {
        g_cpPqStatus = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL) {
        g_cpPqWptrPollCntl = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_RLC_CP_SCHEDULERS) {
        g_rlcCpSchedulers = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE) {
        g_hdpInvalidate = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_L2_CNTL2) {
        g_gcvmL2Cntl2 = Value & ~BC250_HQD_GCVM_L2_INVALIDATE_MASK;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_CNTL) {
        g_gcvmCtx0Cntl = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_LO) {
        g_gcvmCtx0PtBaseLo = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_PT_BASE_HI) {
        g_gcvmCtx0PtBaseHi = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_PT_START_LO) {
        g_gcvmCtx0PtStartLo = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_CONTEXT0_PT_END_LO) {
        g_gcvmCtx0PtEndLo = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_INVALIDATE_ENG0_REQ) {
        g_gcvmEng0Req = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_GCVM_INVALIDATE_REQ) {
        g_gcvmInvalidateReq = Value;
        if ((Value & BC250_HQD_GCVM_INVALIDATE_VMID0) != 0u) {
            g_gcvmInvalidateAck |= BC250_HQD_GCVM_INVALIDATE_VMID0;
        }
        return 1u;
    }
    if (ByteOffset == BC250_GRBM_GFX_CNTL_BYTE_OFFSET) {
        if (g_forceRestoreError && Value == 0u) {
            return 0u;
        }
        if (g_ignoreSelectorWrites == 0u || Value == 0u)
            g_selectorRegister = Value;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_ACTIVE) {
        g_activeRegister = Value;
        if ((Value & 1u) != 0u) g_seenActiveOne = 1u;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_DEQUEUE_REQUEST) {
        g_activeRegister = 0u;
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_PQ_WPTR_LO) {
        if (g_requireInactiveForWptrRestore != 0u &&
            g_seenActiveOne != 0u && Value == 0u &&
            (g_activeRegister & 1u) != 0u) {
            return 1u;
        }
        g_wptrRegister = Value;
        if (g_simulateComputeExecution) {
            g_rptrRegister = Value;
            g_testRptrBuffer[0] = Value;
            g_scratchRegister = 0x22222222u;
            g_testFenceBuffer[0] = 0xBC250123u;
        }
        return 1u;
    }
    if (ByteOffset == BC250_HQD_MMIO_PQ_RPTR) {
        g_rptrRegister = Value;
        return 1u;
    }
    if (ByteOffset >= 0xC800u && ByteOffset <= 0xC900u) {
        uint32_t idx = (ByteOffset - 0xC800u) >> 2;
        g_hqdBankRegs[bank][idx] = Value;
        return 1u;
    }
    return 0u;
}

static void Bc250TestHqdBarrier(void *Context)
{
    (void)Context;
    if (g_simulateComputeExecution) {
        g_rptrRegister = 16u;
        g_testRptrBuffer[0] = 16u;
        g_scratchRegister = 0x22222222u;
        g_testFenceBuffer[0] = 0xBC250123u;
    }
}

static int Bc250RunComputeMqdContractTest(void)
{
    uint32_t available = 0;
    uint32_t wptr = 0;
    volatile uint32_t registerOffset = 0;
    BC250_HQD_SELECTOR_INTERFACE selectorInterface;
    BC250_HQD_SELECTOR_EVIDENCE selectorEvidence;
    BC250_HQD_BANK_PROBE_EVIDENCE bankEvidence;
    BC250_HQD_REGISTER_WRITE inactivePlan[BC250_HQD_INACTIVE_PLAN_MAX_WRITES];
    uint32_t inactivePlanCount = 0u;

    memset(&g_testMqd, 0, sizeof(g_testMqd));
    memset(&g_testProps, 0, sizeof(g_testProps));

    /* 0. Mapa SOC15 GC10.1: impedir regressao para offsets de outra geracao. */
    registerOffset = BC250_HQD_MMIO_MQD_BASE_LO; CHECK(registerOffset == 0x0000C824u);
    registerOffset = BC250_HQD_MMIO_MQD_BASE_HI; CHECK(registerOffset == 0x0000C828u);
    registerOffset = BC250_HQD_MMIO_ACTIVE; CHECK(registerOffset == 0x0000C82Cu);
    registerOffset = BC250_HQD_MMIO_PQ_RPTR; CHECK(registerOffset == 0x0000C84Cu);
    registerOffset = BC250_HQD_MMIO_DOORBELL_CONTROL; CHECK(registerOffset == 0x0000C860u);
    registerOffset = BC250_HQD_MMIO_PQ_WPTR_LO; CHECK(registerOffset == 0x0000C8FCu);
    registerOffset = BC250_HQD_MMIO_PQ_WPTR_HI; CHECK(registerOffset == 0x0000C900u);
    registerOffset = BC250_HQD_MMIO_MQD_BASE_LO; CHECK(registerOffset != 0x00009104u);
    registerOffset = BC250_HQD_MMIO_ACTIVE; CHECK(registerOffset != 0x0000910Cu);
    registerOffset = BC250_HQD_MMIO_PQ_WPTR_LO; CHECK(registerOffset != 0x00009130u);

    /* 1. Setup valid properties */
    g_testProps.MqdGpuAddress = 0x0000080000000000ull;
    g_testProps.HqdBaseGpuAddress = 0x0000080000001000ull;
    g_testProps.QueueSizeDwords = 16384u; /* 64 KiB */
    g_testProps.EopGpuAddress = 0x0000080000002000ull;
    g_testProps.EopSizeBytes = 4096u;
    g_testProps.RptrGpuAddress = 0x0000080000003000ull;
    g_testProps.WptrGpuAddress = 0x0000080000003008ull;
    g_testProps.UseDoorbell = 1u;
    g_testProps.DoorbellIndex = 0x20u;
    g_testProps.PipePriority = 0u;
    g_testProps.QueuePriority = 0u;
    g_testProps.HqdActive = 1u;

    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_OK);
    CHECK(g_testMqd.Header == BC250_MQD_HEADER_MAGIC);
    CHECK(g_testMqd.ComputePipelinestatEnable == 0x00000001u);
    CHECK(g_testMqd.ComputeStaticThreadMgmtSe0 == 0xFFFFFFFFu);
    CHECK(g_testMqd.ComputeMiscReserved == 0x00000003u);
    CHECK(g_testMqd.CpHqdEopBaseAddrLo == (uint32_t)((0x0000080000002000ull >> 8) & 0xFFFFFFFFull));
    CHECK(g_testMqd.CpHqdPqBaseLo == (uint32_t)((0x0000080000001000ull >> 8) & 0xFFFFFFFFull));
    CHECK((g_testMqd.CpHqdPqControl & 0x3Fu) == 13u); /* 16384 -> order 14, 14-1 = 13 */
    CHECK(g_testMqd.CpHqdPqControl == 0xD030090Du);
    CHECK(g_testMqd.CpHqdPqDoorbellControl == 0x40000080u);
    CHECK(g_testMqd.CpHqdPersistentState == 0x00005300u);
    CHECK(g_testMqd.CpHqdIbControl == 0x00300000u);
    CHECK(g_testMqd.CpHqdQuantum == 0x00000111u);
    CHECK(g_testMqd.CpHqdActive == 1u);

    /* 2. Validate MQD */
    CHECK(Bc250ComputeMqdValidate(&g_testMqd, &g_testProps) == BC250_MQD_OK);

    /* Plano offline: apenas estado inativo, sem doorbell ou publicacao WPTR. */
    g_testProps.HqdActive = 0u;
    g_testProps.UseDoorbell = 0u;
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_OK);
    CHECK(Bc250ComputeBuildInactiveHqdPlan(&g_testMqd, &g_testProps,
        inactivePlan, BC250_HQD_INACTIVE_PLAN_MAX_WRITES,
        &inactivePlanCount) == BC250_HQD_INACTIVE_PLAN_OK);
    CHECK(inactivePlanCount == BC250_HQD_INACTIVE_PLAN_MAX_WRITES);
    CHECK(inactivePlan[15].ByteOffset == BC250_HQD_MMIO_DOORBELL_CONTROL &&
          inactivePlan[15].Value == 0u);
    g_testMqd.CpHqdActive = 1u;
    CHECK(Bc250ComputeBuildInactiveHqdPlan(&g_testMqd, &g_testProps,
        inactivePlan, BC250_HQD_INACTIVE_PLAN_MAX_WRITES,
        &inactivePlanCount) == BC250_HQD_INACTIVE_PLAN_MQD_INVALID);
    g_testMqd.CpHqdActive = 0u;
    g_testProps.HqdActive = 1u;
    CHECK(Bc250ComputeBuildInactiveHqdPlan(&g_testMqd, &g_testProps,
        inactivePlan, BC250_HQD_INACTIVE_PLAN_MAX_WRITES,
        &inactivePlanCount) == BC250_HQD_INACTIVE_PLAN_MQD_INVALID);
    g_testProps.HqdActive = 1u;
    g_testProps.UseDoorbell = 1u;
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_OK);

    /* 3. Test queue available space and wptr advancement */
    CHECK(Bc250ComputeQueueComputeAvailableSpace(
        g_testProps.QueueSizeDwords, 0, 0, &available) == BC250_MQD_OK);
    CHECK(available == g_testProps.QueueSizeDwords - 1u);

    CHECK(Bc250ComputeQueueAdvanceWptr(
        g_testProps.QueueSizeDwords, 0, 12u, &wptr) == BC250_MQD_OK);
    CHECK(wptr == 12u);

    /* Test wraparound */
    CHECK(Bc250ComputeQueueAdvanceWptr(
        g_testProps.QueueSizeDwords, g_testProps.QueueSizeDwords - 4u, 8u, &wptr) == BC250_MQD_OK);
    CHECK(wptr == 4u);

    /* 4. Test error cases */
    /* Null pointers */
    CHECK(Bc250ComputeMqdInit(NULL, &g_testMqd) == BC250_MQD_BAD_ARGUMENT);
    CHECK(Bc250ComputeMqdInit(&g_testProps, NULL) == BC250_MQD_BAD_ARGUMENT);

    /* Invalid queue sizes */
    g_testProps.QueueSizeDwords = 100u; /* Not power of 2 */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_INVALID_QUEUE_SIZE);

    g_testProps.QueueSizeDwords = 32u; /* Below minimum 64 */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_INVALID_QUEUE_SIZE);
    g_testProps.QueueSizeDwords = 16384u; /* Restore */

    /* Invalid EOP size */
    g_testProps.EopSizeBytes = 128u; /* Below minimum 256 */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_INVALID_EOP_SIZE);
    g_testProps.EopSizeBytes = 4096u; /* Restore */

    /* Misaligned GPU address */
    g_testProps.MqdGpuAddress = 0x0000080000000004ull; /* Not 256-byte aligned */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_MISALIGNED_ADDRESS);
    g_testProps.MqdGpuAddress = 0x0000080000000000ull; /* Restore */

    g_testProps.HqdBaseGpuAddress = 0x0000080000001080ull; /* Not 256-byte aligned */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_MISALIGNED_ADDRESS);
    g_testProps.HqdBaseGpuAddress = 0x0000080000001000ull; /* Restore */

    g_testProps.WptrGpuAddress = 0x0000080000003004ull; /* precisa de 8-byte alignment */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_MISALIGNED_ADDRESS);
    g_testProps.WptrGpuAddress = 0x0000080000003008ull; /* Restore */

    g_testProps.DoorbellIndex = 0x04000000u; /* nao cabe no campo GC10.1 */
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_INVALID_FIELD);
    g_testProps.DoorbellIndex = 0x20u; /* Restore */

    g_testProps.PipePriority = 4u;
    CHECK(Bc250ComputeMqdInit(&g_testProps, &g_testMqd) == BC250_MQD_INVALID_FIELD);
    g_testProps.PipePriority = 0u;

    /* 5. Preflight HQD readonly: duas passagens, all-ones e inherited active. */
    memset(g_hqdPass1, 0, sizeof(g_hqdPass1));
    memset(g_hqdPass2, 0, sizeof(g_hqdPass2));
    g_hqdPass1[BC250_HQD_PREFLIGHT_SELECTOR_BEFORE] =
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0;
    g_hqdPass1[BC250_HQD_PREFLIGHT_SELECTOR_AFTER] =
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0;
    g_hqdPass2[BC250_HQD_PREFLIGHT_SELECTOR_BEFORE] =
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0;
    g_hqdPass2[BC250_HQD_PREFLIGHT_SELECTOR_AFTER] =
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0;
    g_hqdPass1[BC250_HQD_PREFLIGHT_PQ_CONTROL] = 0xD000090Du;
    g_hqdPass2[BC250_HQD_PREFLIGHT_PQ_CONTROL] = 0xD000090Du;
    CHECK(Bc250ComputeHqdReadonlyPreflight(
        g_hqdPass1, g_hqdPass2, &g_hqdSnapshot) == BC250_HQD_PREFLIGHT_OK);
    CHECK(g_hqdSnapshot.StablePasses == 2u);
    CHECK(g_hqdSnapshot.HqdWasInactive == 1u);

    g_hqdPass2[BC250_HQD_PREFLIGHT_PQ_RPTR] = 1u;
    CHECK(Bc250ComputeHqdReadonlyPreflight(
        g_hqdPass1, g_hqdPass2, &g_hqdSnapshot) == BC250_HQD_PREFLIGHT_UNSTABLE);
    g_hqdPass2[BC250_HQD_PREFLIGHT_PQ_RPTR] = 0u;

    g_hqdPass1[BC250_HQD_PREFLIGHT_MQD_BASE_LO] = 0xFFFFFFFFu;
    g_hqdPass2[BC250_HQD_PREFLIGHT_MQD_BASE_LO] = 0xFFFFFFFFu;
    CHECK(Bc250ComputeHqdReadonlyPreflight(
        g_hqdPass1, g_hqdPass2, &g_hqdSnapshot) == BC250_HQD_PREFLIGHT_UNMAPPED);
    g_hqdPass1[BC250_HQD_PREFLIGHT_MQD_BASE_LO] = 0u;
    g_hqdPass2[BC250_HQD_PREFLIGHT_MQD_BASE_LO] = 0u;

    g_hqdPass1[BC250_HQD_PREFLIGHT_ACTIVE] = 1u;
    g_hqdPass2[BC250_HQD_PREFLIGHT_ACTIVE] = 1u;
    CHECK(Bc250ComputeHqdReadonlyPreflight(
        g_hqdPass1, g_hqdPass2, &g_hqdSnapshot) == BC250_HQD_PREFLIGHT_INHERITED_ACTIVE);
    CHECK(g_hqdSnapshot.HqdWasInactive == 0u);
    g_hqdPass1[BC250_HQD_PREFLIGHT_ACTIVE] = 0u;
    g_hqdPass2[BC250_HQD_PREFLIGHT_ACTIVE] = 0u;

    g_hqdPass1[BC250_HQD_PREFLIGHT_SELECTOR_BEFORE] = 0u;
    g_hqdPass1[BC250_HQD_PREFLIGHT_SELECTOR_AFTER] = 0u;
    g_hqdPass2[BC250_HQD_PREFLIGHT_SELECTOR_BEFORE] = 0u;
    g_hqdPass2[BC250_HQD_PREFLIGHT_SELECTOR_AFTER] = 0u;
    CHECK(Bc250ComputeHqdReadonlyPreflight(
        g_hqdPass1, g_hqdPass2, &g_hqdSnapshot) ==
        BC250_HQD_PREFLIGHT_SELECTOR_NOT_COMPUTE);
    CHECK(g_hqdSnapshot.StablePasses == 2u);
    CHECK(g_hqdSnapshot.HqdWasInactive == 1u);

    /* 6. Selecao MEC transacional: RMW, captura readonly e rollback. */
    memset(&selectorInterface, 0, sizeof(selectorInterface));
    selectorInterface.Read32 = Bc250TestHqdRead32;
    selectorInterface.Write32 = Bc250TestHqdWrite32;
    selectorInterface.Barrier = Bc250TestHqdBarrier;
    g_selectorRegister = 0u;
    g_selectedHqdActive = 0u;
    g_ignoreSelectorWrites = 0u;
    CHECK(Bc250ComputeProbeMecHqdSelected(
        &selectorInterface, &selectorEvidence) == BC250_HQD_SELECTOR_OK);
    CHECK(selectorEvidence.OriginalSelector == 0u);
    CHECK(selectorEvidence.AppliedSelector ==
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0);
    CHECK(selectorEvidence.RestoredSelector == 0u);
    CHECK(selectorEvidence.ReadCalls == 31u);
    CHECK(selectorEvidence.WriteCalls == 2u);
    CHECK(selectorEvidence.ApplyReadbackPassed == 1u);
    CHECK(selectorEvidence.AppliedSelectorReadbackFirst ==
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0);
    CHECK(selectorEvidence.AppliedSelectorReadbackLast ==
        BC250_HQD_SELECTOR_MEC1_P0_Q0_V0);
    CHECK(selectorEvidence.RestorePassed == 1u);
    CHECK(selectorEvidence.PhysicalReady == 1u);
    CHECK(selectorEvidence.HqdStatus == BC250_HQD_PREFLIGHT_OK);
    CHECK(g_selectorRegister == 0u);

    /* 7. Prova de independencia dos bancos com prioridade inerte. */
    memset(g_hqdBankRegs, 0, sizeof(g_hqdBankRegs));
    g_selectorRegister = 0u;
    g_selectedHqdActive = 0u;
    g_hqdBankRegs[0][15] = 3u;
    g_hqdBankRegs[1][15] = 7u;
    g_ignoreSelectorWrites = 0u;
    CHECK(Bc250ComputeProbeMecHqdBankIsolation(
        &selectorInterface, &bankEvidence) == BC250_HQD_BANK_PROBE_OK);
    CHECK(bankEvidence.Bank0Priority == 3u);
    CHECK(bankEvidence.CandidatePriority == 7u);
    CHECK(bankEvidence.PoisonPriority != 3u &&
          bankEvidence.PoisonPriority != 7u);
    CHECK(bankEvidence.PoisonReadbackPassed == 1u);
    CHECK(bankEvidence.BankIsolationPassed == 1u);
    CHECK(bankEvidence.RestorePassed == 1u);
    CHECK(bankEvidence.HqdCapturePassed == 1u);
    CHECK(bankEvidence.CandidateHqdSnapshot.StablePasses == 2u);
    CHECK(bankEvidence.CandidateHqdSnapshot.HqdWasInactive == 1u);
    CHECK(bankEvidence.PhysicalReady == 1u);
    CHECK(bankEvidence.ReadCalls == 47u);
    CHECK(bankEvidence.WriteCalls == 7u);
    CHECK(g_selectorRegister == 0u);
    CHECK(g_hqdBankRegs[0][15] == 3u && g_hqdBankRegs[1][15] == 7u);

    g_ignoreSelectorWrites = 1u;
    CHECK(Bc250ComputeProbeMecHqdBankIsolation(
        &selectorInterface, &bankEvidence) ==
        BC250_HQD_BANK_PROBE_NOT_ISOLATED);
    CHECK(bankEvidence.BankIsolationPassed == 0u);
    CHECK(bankEvidence.RestorePassed == 1u);
    CHECK(bankEvidence.HqdCapturePassed == 0u);
    CHECK(bankEvidence.ReadCalls == 21u);
    CHECK(bankEvidence.PhysicalReady == 0u);
    CHECK(g_selectorRegister == 0u);
    CHECK(g_hqdBankRegs[0][15] == 3u && g_hqdBankRegs[1][15] == 7u);
    g_ignoreSelectorWrites = 0u;

    g_selectorRegister = 1u;
    CHECK(Bc250ComputeProbeMecHqdSelected(
        &selectorInterface, &selectorEvidence) ==
        BC250_HQD_SELECTOR_PREFLIGHT_FAILED);
    CHECK(selectorEvidence.WriteCalls == 0u);
    CHECK(g_selectorRegister == 1u);

    g_selectorRegister = 0u;
    g_selectedHqdActive = 1u;
    CHECK(Bc250ComputeProbeMecHqdSelected(
        &selectorInterface, &selectorEvidence) ==
        BC250_HQD_SELECTOR_HQD_FAILED);
    CHECK(selectorEvidence.HqdStatus ==
        BC250_HQD_PREFLIGHT_INHERITED_ACTIVE);
    CHECK(selectorEvidence.RestorePassed == 1u);
    CHECK(selectorEvidence.PhysicalReady == 0u);
    CHECK(g_selectorRegister == 0u);
    g_selectedHqdActive = 0u;

    /* 8. Transacao reversivel de programacao HQD inativa (Gate 1.68) */
    {
        BC250_HQD_MC_BUFFERS buffers;
        BC250_HQD_PROGRAM_EVIDENCE progEvidence;

        memset(&buffers, 0, sizeof(buffers));
        buffers.MqdVirtual = g_testMqdBuffer;
        buffers.MqdGpuAddress = 0x0000080000000000ull;
        buffers.MqdSizeBytes = sizeof(g_testMqdBuffer);

        buffers.RingVirtual = g_testRingBuffer;
        buffers.RingGpuAddress = 0x0000080000001000ull;
        buffers.RingSizeBytes = sizeof(g_testRingBuffer);

        buffers.EopVirtual = g_testEopBuffer;
        buffers.EopGpuAddress = 0x0000080000002000ull;
        buffers.EopSizeBytes = sizeof(g_testEopBuffer);

        buffers.RptrVirtual = g_testRptrBuffer;
        buffers.RptrGpuAddress = 0x0000080000003000ull;
        buffers.RptrSizeBytes = sizeof(g_testRptrBuffer);

        buffers.WptrVirtual = g_testWptrBuffer;
        buffers.WptrGpuAddress = 0x0000080000003008ull;
        buffers.WptrSizeBytes = sizeof(g_testWptrBuffer);

        buffers.FenceVirtual = g_testFenceBuffer;
        buffers.FenceGpuAddress = 0x0000080000003010ull;
        buffers.FenceSizeBytes = sizeof(g_testFenceBuffer);

        g_testProps.MqdGpuAddress = buffers.MqdGpuAddress;
        g_testProps.HqdBaseGpuAddress = buffers.RingGpuAddress;
        g_testProps.QueueSizeDwords = 16384u;
        g_testProps.EopGpuAddress = buffers.EopGpuAddress;
        g_testProps.EopSizeBytes = 4096u;
        g_testProps.RptrGpuAddress = buffers.RptrGpuAddress;
        g_testProps.WptrGpuAddress = buffers.WptrGpuAddress;
        g_testProps.UseDoorbell = 0u;
        g_testProps.DoorbellIndex = 0u;
        g_testProps.PipePriority = 0u;
        g_testProps.QueuePriority = 0u;
        g_testProps.HqdActive = 0u;

        memset(g_hqdBankRegs, 0, sizeof(g_hqdBankRegs));
        g_selectorRegister = 0u;
        g_forceReadbackError = 0u;
        g_forceAllOnes = 0u;
        g_forceActiveOnReadback = 0u;
        g_forceWptrOnReadback = 0u;
        g_forceRestoreError = 0u;

        /* 8.1 Caso Nominal: Sucesso e Rollback 100% integro */
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd,
            &progEvidence) == BC250_HQD_PROGRAM_OK);
        CHECK(progEvidence.MemoryVerificationPassed == 1u);
        CHECK(progEvidence.ApplyReadbackPassed == 1u);
        CHECK(progEvidence.ActiveRemainedZero == 1u);
        CHECK(progEvidence.RestorePassed == 1u);
        CHECK(progEvidence.MemoryRestorePassed == 1u);
        CHECK(progEvidence.PhysicalReady == 1u);
        CHECK(progEvidence.OriginalSelector == 0u);
        CHECK(progEvidence.SelectedSelector == BC250_HQD_SELECTOR_MEC1_P0_Q0_V0);
        CHECK(progEvidence.RestoredSelector == 0u);
        CHECK(g_selectorRegister == 0u);
        CHECK(g_testFenceBuffer[0] == 0u);
        CHECK(g_testMqdBuffer[0] == 0u);

        /* 8.2 Controles Negativos: Parametros Nulos e Desalinhamento */
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            NULL, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_BAD_ARGUMENT);
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, NULL, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_BAD_ARGUMENT);

        buffers.MqdGpuAddress = 0x0000080000000004ull; /* desalinhado */
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_BAD_ARGUMENT);
        buffers.MqdGpuAddress = 0x0000080000000000ull;

        /* 8.3 Controles Negativos: Seguranca contra ACTIVE ou Doorbell */
        g_testProps.HqdActive = 1u;
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_UNSAFE_STATE);
        g_testProps.HqdActive = 0u;

        g_testProps.UseDoorbell = 1u;
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_UNSAFE_STATE);
        g_testProps.UseDoorbell = 0u;

        /* 8.4 Controles Negativos: Erros de Readback e Falhas de Hardware */
        g_forceReadbackError = 1u;
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_READBACK_FAILED);
        CHECK(progEvidence.MismatchMask != 0u);
        CHECK(progEvidence.FirstMismatchOffset != 0u);
        CHECK(progEvidence.ApplyReadbackPassed == 0u);
        CHECK(progEvidence.PhysicalReady == 0u);
        CHECK(progEvidence.RestorePassed == 1u);
        g_forceReadbackError = 0u;

        g_forceAllOnes = 1u;
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_PREFLIGHT_FAILED);
        CHECK(progEvidence.PhysicalReady == 0u);
        g_forceAllOnes = 0u;

        g_forceActiveOnReadback = 1u;
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_PREFLIGHT_FAILED);
        CHECK(progEvidence.PhysicalReady == 0u);
        CHECK(progEvidence.RestorePassed == 1u);
        g_forceActiveOnReadback = 0u;

        g_forceWptrOnReadback = 1u;
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_UNSAFE_STATE);
        CHECK(progEvidence.PhysicalReady == 0u);
        CHECK(progEvidence.RestorePassed == 1u);
        g_forceWptrOnReadback = 0u;

        g_forceRestoreError = 1u;
        CHECK(Bc250ComputeRunInactiveHqdProgramTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd, &progEvidence) ==
            BC250_HQD_PROGRAM_RESTORE_FAILED);
        CHECK(progEvidence.PhysicalReady == 0u);
        g_forceRestoreError = 0u;
    }

    /* 9. Transacao de Ativacao do HQD e Submissao PM4 (Gate 1.70) */
    {
        BC250_HQD_MC_BUFFERS buffers;
        BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE actEvidence;

        memset(&buffers, 0, sizeof(buffers));
        buffers.MqdVirtual = g_testMqdBuffer;
        buffers.MqdGpuAddress = 0x0000080000000000ull;
        buffers.MqdSizeBytes = sizeof(g_testMqdBuffer);

        buffers.RingVirtual = g_testRingBuffer;
        buffers.RingGpuAddress = 0x0000080000001000ull;
        buffers.RingSizeBytes = sizeof(g_testRingBuffer);

        buffers.EopVirtual = g_testEopBuffer;
        buffers.EopGpuAddress = 0x0000080000002000ull;
        buffers.EopSizeBytes = sizeof(g_testEopBuffer);

        buffers.RptrVirtual = g_testRptrBuffer;
        buffers.RptrGpuAddress = 0x0000080000003000ull;
        buffers.RptrSizeBytes = sizeof(g_testRptrBuffer);

        buffers.WptrVirtual = g_testWptrBuffer;
        buffers.WptrGpuAddress = 0x0000080000003008ull;
        buffers.WptrSizeBytes = sizeof(g_testWptrBuffer);

        buffers.FenceVirtual = g_testFenceBuffer;
        buffers.FenceGpuAddress = 0x0000080000003200ull;
        buffers.FenceSizeBytes = sizeof(g_testFenceBuffer);

        buffers.DoorbellVirtual = g_testDoorbellBuffer;
        buffers.DoorbellSizeBytes = sizeof(g_testDoorbellBuffer);

        buffers.PtVirtual = g_testPtBuffer;
        buffers.PtGpuAddress = 0x0000080000004000ull;
        buffers.PtSizeBytes = sizeof(g_testPtBuffer);
        memset(g_testPtBuffer, 0, sizeof(g_testPtBuffer));
        g_testPtBuffer[2] = 0x0000080000003000ull | 0xE1ull;

        g_testProps.MqdGpuAddress = buffers.MqdGpuAddress;
        g_testProps.HqdBaseGpuAddress = buffers.RingGpuAddress;
        g_testProps.QueueSizeDwords = 16384u;
        g_testProps.EopGpuAddress = buffers.EopGpuAddress;
        g_testProps.EopSizeBytes = 4096u;
        g_testProps.RptrGpuAddress = buffers.RptrGpuAddress;
        g_testProps.WptrGpuAddress = buffers.WptrGpuAddress;
        g_testProps.UseDoorbell = 1u;
        g_testProps.DoorbellIndex = 0u; /* derivado da faixa MEC no preflight */
        g_testProps.PipePriority = 0u;
        g_testProps.QueuePriority = 0u;
        g_testProps.HqdActive = 1u;

        memset(g_hqdBankRegs, 0, sizeof(g_hqdBankRegs));
        memset(g_testDoorbellBuffer, 0, sizeof(g_testDoorbellBuffer));
        g_selectorRegister = 0u;
        g_activeRegister = 0u;
        g_rptrRegister = 0u;
        /* Cursor residual do ensaio anterior: deve ser normalizado e restaurado. */
        g_wptrRegister = 16u;
        g_scratchRegister = 0x12345678u;
        g_gcvmL2Cntl2 = 0x12340000u;
        g_gcvmInvalidateReq = 0u;
        g_gcvmInvalidateAck = BC250_HQD_GCVM_INVALIDATE_VMID0;
        g_hdpInvalidate = 0u;
        g_dbRangeLo = 0x00000110u;
        g_dbRangeHi = 0x0FFFFFFCu;
        g_cpPqStatus = 0u;
        g_cpPqWptrPollCntl = 0x80000004u;
        g_rlcCpSchedulers = 0x58504840u;
        g_simulateComputeExecution = 0u;

        CHECK(buffers.PtSizeBytes >= 4096u);
        CHECK((buffers.PtGpuAddress & 0xFFFull) == 0u);
        CHECK((g_testPtBuffer[2] & 0x0000FFFFFFFFF000ull) ==
              0x0000080000003000ull);
        CHECK((g_testPtBuffer[2] & ~0x0000FFFFFFFFF000ull) == 0xE1ull);
        CHECK(((g_testPtBuffer[2] & 0x0000FFFFFFFFF000ull) | 0x200ull) ==
              buffers.FenceGpuAddress);

        /* 9.1 Caso Offline Sem Simulacao: PM4 nao consumido (fail-closed seguro) */
        CHECK(Bc250ComputeRunActiveHqdSubmissionTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd,
            &actEvidence) == BC250_HQD_ACTIVE_PM4_NOT_CONSUMED);
        CHECK(actEvidence.MemoryVerificationPassed == 1u);
        CHECK(actEvidence.ActiveConfirmed == 1u);
        CHECK(actEvidence.WptrApplied == 16u);
        CHECK(actEvidence.DoorbellRung == 1u);
        CHECK(actEvidence.DoorbellIndex == 0x44u);
        CHECK(actEvidence.DoorbellOffset == 0x110u);
        CHECK(actEvidence.DoorbellValueApplied == 16u);
        CHECK(actEvidence.DoorbellRangeLowerApplied == 0x00000110u);
        CHECK(actEvidence.DoorbellRangeLowerReadback == 0x00000110u);
        CHECK(actEvidence.DoorbellRangeUpperApplied == 0x0FFFFFFCu);
        CHECK(actEvidence.DoorbellRangeUpperReadback == 0x0FFFFFFCu);
        CHECK(actEvidence.CpPqStatusReadback == 0x00000002u);
        CHECK(actEvidence.WptrPollCntlBefore == 0x80000004u);
        CHECK(actEvidence.WptrPollCntlApplied == 0x00000004u);
        CHECK(actEvidence.WptrPollCntlReadback == 0x00000004u);
        CHECK(actEvidence.SelectorReleasedBeforeKick == 1u);
        CHECK(actEvidence.InitialWptrLo == 16u);
        CHECK(actEvidence.InitialWptrHi == 0u);
        CHECK(actEvidence.BaselineWptrApplied == 1u);
        CHECK(actEvidence.BaselineWptrReadback == 0u);
        CHECK(actEvidence.BaselineWptrRestored == 16u);
        CHECK(actEvidence.BaselineWptrRestorePolls == 1u);
        CHECK(actEvidence.BaselineWptrRestorePassed == 1u);
        CHECK(actEvidence.DirectWptrKickApplied == 1u);
        CHECK(actEvidence.DirectWptrBefore == 0u);
        CHECK(actEvidence.DirectWptrReadback == 16u);
        CHECK(actEvidence.DirectWptrRestored == 16u);
        CHECK(actEvidence.DirectWptrRestorePolls == 1u);
        CHECK(actEvidence.DirectWptrRestorePassed == 1u);
        CHECK(actEvidence.RptrMmioLast == 0u);
        CHECK(actEvidence.WptrMmioLast == 16u);
        CHECK(actEvidence.HqStatus0PollLast == 0u);
        CHECK(actEvidence.HqdErrorPollLast == 0u);
        CHECK(actEvidence.SelectedSelector == 0x00000008u);
        CHECK(actEvidence.RlcCpSchedulersBefore == 0x58504840u);
        CHECK(actEvidence.RlcCpSchedulersApplied == 0x585048C0u);
        CHECK(actEvidence.RlcCpSchedulersReadback == 0x585048C0u);
        CHECK(actEvidence.RlcCpSchedulersAfterKick == 0x585048C0u);
        CHECK(actEvidence.RlcCpSchedulersRestored == 0x58504840u);
        CHECK(actEvidence.RlcCpSchedulersRestorePassed == 1u);
        CHECK(g_rlcCpSchedulers == 0x58504840u);
        CHECK(actEvidence.HdpInvalidateApplied == 1u);
        CHECK(actEvidence.GcvmL2Cntl2Before == 0x12340000u);
        CHECK(actEvidence.GcvmL2Cntl2Applied == 0x12340003u);
        CHECK(actEvidence.GcvmL2Cntl2Readback == 0x12340000u);
        CHECK(actEvidence.GcvmInvalidateRequestApplied ==
              BC250_HQD_GCVM_INVALIDATE_REQ_VMID0);
        CHECK(actEvidence.GcvmInvalidateRequestReadback ==
              BC250_HQD_GCVM_INVALIDATE_REQ_VMID0);
        CHECK(g_gcvmInvalidateReq == BC250_HQD_GCVM_INVALIDATE_REQ_VMID0);
        CHECK(actEvidence.GcvmInvalidateAckAfter == 1u);
        CHECK(actEvidence.GcvmContext0PtBaseLoApplied == (uint32_t)(buffers.PtGpuAddress | 1u));
        CHECK(actEvidence.GcvmContext0CntlApplied == 0x007FFE01u);
        CHECK(actEvidence.GcvmContext0RestorePassed == 1u);
        CHECK(actEvidence.CoherencyPassed == 1u);
        CHECK(g_gcvmL2Cntl2 == 0x12340000u);
        CHECK(actEvidence.TriadPassed == 0u);
        CHECK(actEvidence.RestorePassed == 1u);
        CHECK(actEvidence.MemoryRestorePassed == 1u);
        CHECK(actEvidence.PhysicalReady == 0u);
        CHECK(g_wptrRegister == 16u);

        /*
         * 9.2 O silicio medido em 3.19 recusou reduzir WPTR com ACTIVE=1.
         * O rollback deve aguardar o DEQUEUE e restaurar o cursor exatamente
         * com a fila inativa, conforme gfx_v10_0_kiq_init_register.
         */
        g_wptrRegister = 0u;
        g_activeRegister = 0u;
        g_seenActiveOne = 0u;
        g_requireInactiveForWptrRestore = 1u;
        CHECK(Bc250ComputeRunActiveHqdSubmissionTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd,
            &actEvidence) == BC250_HQD_ACTIVE_PM4_NOT_CONSUMED);
        CHECK(actEvidence.InitialWptrLo == 0u);
        CHECK(actEvidence.BaselineWptrRestored == 0u);
        CHECK(actEvidence.BaselineWptrRestorePassed == 1u);
        CHECK(actEvidence.RestorePassed == 1u);
        CHECK(g_wptrRegister == 0u);
        CHECK(g_rptrRegister == 0u);
        g_requireInactiveForWptrRestore = 0u;

        /* 9.3 Caso com Simulacao: Triade aprovada com sucesso */
        g_wptrRegister = 16u;
        g_simulateComputeExecution = 1u;
        CHECK(Bc250ComputeRunActiveHqdSubmissionTransaction(
            &selectorInterface, &buffers, &g_testProps, &g_testMqd,
            &actEvidence) == BC250_HQD_ACTIVE_OK);
        CHECK(actEvidence.TriadPassed == 1u);
        CHECK(actEvidence.RptrAfter == 16u);
        CHECK(actEvidence.RptrMmioLast == 16u);
        CHECK(actEvidence.WptrMmioLast == 16u);
        CHECK(actEvidence.ScratchAfter == 0x22222222u);
        CHECK(actEvidence.FenceAfter == 0xBC250123u);
        CHECK(actEvidence.DoorbellRung == 1u);
        CHECK(actEvidence.RestorePassed == 1u);
        CHECK(actEvidence.MemoryRestorePassed == 1u);
        CHECK(actEvidence.PhysicalReady == 1u);
        CHECK(actEvidence.BaselineWptrReadback == 0u);
        CHECK(actEvidence.BaselineWptrRestored == 16u);
        CHECK(actEvidence.BaselineWptrRestorePolls == 1u);
        CHECK(actEvidence.BaselineWptrRestorePassed == 1u);
        CHECK(g_wptrRegister == 16u);
        g_simulateComputeExecution = 0u;
    }

    /* 10. Nomes de status */
    CHECK(Bc250TestStringEquals(Bc250HqdProgramStatusName(BC250_HQD_PROGRAM_OK), "ok"));
    CHECK(Bc250TestStringEquals(Bc250HqdProgramStatusName(BC250_HQD_PROGRAM_UNSAFE_STATE), "unsafe_state"));
    CHECK(Bc250TestStringEquals(Bc250HqdProgramStatusName(BC250_HQD_PROGRAM_RESTORE_FAILED), "restore_failed"));
    CHECK(Bc250TestStringEquals(Bc250HqdActiveStatusName(BC250_HQD_ACTIVE_OK), "ok"));
    CHECK(Bc250TestStringEquals(Bc250HqdActiveStatusName(BC250_HQD_ACTIVE_PM4_NOT_CONSUMED), "pm4_not_consumed"));

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunComputeMqdContractTest());
}
#else
int main(void)
{
    return Bc250RunComputeMqdContractTest();
}
#endif
