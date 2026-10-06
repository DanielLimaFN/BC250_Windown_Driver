#include "bc250_sdma1_bringup.h"
#include "bc250_sdma_packet_builder.h"

/*
 * P4.1 (doc 620). Ver o cabecalho para a proveniencia. Valores de campo
 * identicos aos do SDMA0 provado (bc250_sdma_present.c, restart integral).
 */

#define BC250_SDMA1_CHICKEN_MASK    0xFFBF1F0Fu
#define BC250_SDMA1_CHICKEN_VALUE   0x03AB0107u
#define BC250_SDMA1_GB_ADDR_MASK    0x001877FFu
#define BC250_SDMA1_GB_ADDR_VALUE   0x00000044u
/* UTCL1_CNTL: RESP_MODE=3 (bits 11:9), REDO_DELAY=9 (bits 5:1), sem FORCE_INVALIDATION. */
#define BC250_SDMA1_UTCL1_CLEAR     (0x00000E00u | 0x0000003Eu | 0x00004000u)
#define BC250_SDMA1_UTCL1_SET       ((3u << 9) | (9u << 1))
#define BC250_SDMA1_UTCL1_PAGE_MASK 0x00FFF000u
#define BC250_SDMA1_UTCL1_PAGE_VAL  0x004C5C00u
/* Pacote de prova: FENCE (4 dw) + NOP ate 16 dw, como o align_mask 0xf do anel. */
#define BC250_SDMA1_PROOF_DWORDS    16u

static uint32_t Bc250Sdma1Rd(const BC250_SDMA1_INTERFACE *I, uint32_t Offset, uint32_t *Value)
{
    return I->Read32(I->Context, Offset, Value);
}

static uint32_t Bc250Sdma1Wr(const BC250_SDMA1_INTERFACE *I, uint32_t Offset, uint32_t Value)
{
    return I->Write32(I->Context, Offset, Value);
}

/* Lei 6: val = (lido & ~mask) | (valor & mask). */
static uint32_t Bc250Sdma1Rmw(const BC250_SDMA1_INTERFACE *I, uint32_t Offset,
    uint32_t Mask, uint32_t Value, uint32_t *Written)
{
    uint32_t cur = 0u;
    uint32_t next;
    if (!Bc250Sdma1Rd(I, Offset, &cur)) return 0u;
    next = (cur & ~Mask) | (Value & Mask);
    if (Written != 0) *Written = next;
    return Bc250Sdma1Wr(I, Offset, next);
}

static uint32_t Bc250Sdma1InterfaceValid(const BC250_SDMA1_INTERFACE *I)
{
    return (I != 0 && I->Read32 != 0 && I->Write32 != 0 && I->Stall != 0 &&
        I->Barrier != 0 && I->Identity == BC250_SDMA1_IDENTITY) ? 1u : 0u;
}

uint32_t Bc250Sdma1IsRegister(uint32_t ByteOffset)
{
    static const uint32_t regs[] = {
        BC250_SDMA1_REG_CLK_CTRL, BC250_SDMA1_REG_CNTL,
        BC250_SDMA1_REG_CHICKEN_BITS, BC250_SDMA1_REG_GB_ADDR_CONFIG,
        BC250_SDMA1_REG_GB_ADDR_CONFIG_READ, BC250_SDMA1_REG_SEM_WAIT_FAIL_TIMER,
        BC250_SDMA1_REG_STATUS, BC250_SDMA1_REG_STATUS1,
        BC250_SDMA1_REG_UCODE_CHECKSUM, BC250_SDMA1_REG_F32_CNTL,
        BC250_SDMA1_REG_UTCL1_CNTL, BC250_SDMA1_REG_UTCL1_PAGE,
        BC250_SDMA1_REG_RB_CNTL, BC250_SDMA1_REG_RB_BASE,
        BC250_SDMA1_REG_RB_BASE_HI, BC250_SDMA1_REG_RB_RPTR,
        BC250_SDMA1_REG_RB_RPTR_HI, BC250_SDMA1_REG_RB_WPTR,
        BC250_SDMA1_REG_RB_WPTR_HI, BC250_SDMA1_REG_WPTR_POLL_CNTL,
        BC250_SDMA1_REG_RPTR_ADDR_HI, BC250_SDMA1_REG_RPTR_ADDR_LO,
        BC250_SDMA1_REG_IB_CNTL, BC250_SDMA1_REG_DOORBELL,
        BC250_SDMA1_REG_WPTR_POLL_ADDR_HI, BC250_SDMA1_REG_WPTR_POLL_ADDR_LO,
        BC250_SDMA1_REG_MINOR_PTR_UPDATE
    };
    uint32_t i;
    for (i = 0u; i < (uint32_t)(sizeof(regs) / sizeof(regs[0])); ++i) {
        if (regs[i] == ByteOffset) return 1u;
    }
    return 0u;
}

BC250_SDMA1_STATUS Bc250Sdma1Shutdown(
    const BC250_SDMA1_INTERFACE *Interface,
    BC250_SDMA1_EVIDENCE *Evidence)
{
    uint32_t ok = 1u;
    if (!Bc250Sdma1InterfaceValid(Interface) || Evidence == 0) {
        return BC250_SDMA1_BAD_ARGUMENT;
    }
    Evidence->SafeToFree = 0u;
    /* sdma_v5_0_gfx_stop: anel e IB desligados; depois o poll e o writeback. */
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_RB_CNTL,
        BC250_SDMA1_RB_ENABLE | BC250_SDMA1_RPTR_WRITEBACK_ENABLE, 0u, 0);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_IB_CNTL, BC250_SDMA1_IB_ENABLE, 0u, 0);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_WPTR_POLL_CNTL,
        BC250_SDMA1_POLL_ENABLE, 0u, 0);
    /* sdma_v5_0_enable(false): F32 parado. */
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_F32_CNTL,
        BC250_SDMA1_F32_HALT, BC250_SDMA1_F32_HALT, 0);
    Interface->Stall(Interface->Context, 100u);
    if (!ok ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_RB_CNTL, &Evidence->ShutdownRbCntl) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_WPTR_POLL_CNTL, &Evidence->ShutdownPollCntl) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_F32_CNTL, &Evidence->ShutdownF32)) {
        return BC250_SDMA1_SHUTDOWN_UNSAFE;
    }
    if ((Evidence->ShutdownRbCntl &
            (BC250_SDMA1_RB_ENABLE | BC250_SDMA1_RPTR_WRITEBACK_ENABLE)) != 0u ||
        (Evidence->ShutdownPollCntl & BC250_SDMA1_POLL_ENABLE) != 0u ||
        (Evidence->ShutdownF32 & BC250_SDMA1_F32_HALT) == 0u) {
        return BC250_SDMA1_SHUTDOWN_UNSAFE;
    }
    Evidence->SafeToFree = 1u;
    Evidence->PhysicalReady = 0u;
    return BC250_SDMA1_OK;
}

static BC250_SDMA1_STATUS Bc250Sdma1Fail(
    const BC250_SDMA1_INTERFACE *Interface,
    BC250_SDMA1_EVIDENCE *Evidence,
    BC250_SDMA1_STATUS Status)
{
    BC250_SDMA1_STATUS shut = Bc250Sdma1Shutdown(Interface, Evidence);
    /* O relogio volta ao POST mesmo na falha. */
    (void)Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_CLK_CTRL, Evidence->ClkBefore);
    (void)Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_CLK_CTRL, &Evidence->ClkRestored);
    return shut == BC250_SDMA1_OK ? Status : BC250_SDMA1_SHUTDOWN_UNSAFE;
}

BC250_SDMA1_STATUS Bc250Sdma1BringUp(
    const BC250_SDMA1_CONFIG *Config,
    const BC250_SDMA1_INTERFACE *Interface,
    BC250_SDMA1_EVIDENCE *Evidence)
{
    uint32_t proof[BC250_SDMA1_PROOF_DWORDS];
    uint32_t cursor = 0u;
    uint32_t ok = 1u;
    uint32_t i;
    uint32_t v = 0u;
    uint32_t rbBase = 0u, rbBaseHi = 0u, pollCntl = 0u, pollLo = 0u, pollHi = 0u;
    uint32_t rptrLo = 0u, rptrHi = 0u, ibCntl = 0u, chicken = 0u, gb = 0u, gbRead = 0u;
    uint64_t wptrPoll, rptrWb, fenceGpu;

    if (Evidence != 0) {
        unsigned char *p = (unsigned char *)Evidence;
        for (i = 0u; i < (uint32_t)sizeof(*Evidence); ++i) p[i] = 0u;
    }
    /* PREFLIGHT */
    if (Config == 0 || Evidence == 0 || !Bc250Sdma1InterfaceValid(Interface) ||
        Config->RingCpu == 0 || Config->WorkCpu == 0 ||
        (Config->RingGpu & 0xFFFull) != 0ull || (Config->WorkGpu & 0xFFFull) != 0ull ||
        Config->RingGpu == 0ull || Config->WorkGpu == 0ull ||
        Config->RingGpu >= (1ull << 48) || Config->WorkGpu >= (1ull << 48) ||
        Config->FenceValue == 0u || Config->FenceValue == BC250_SDMA1_FENCE_POISON ||
        Config->MaxPolls == 0u) {
        return BC250_SDMA1_BAD_ARGUMENT;
    }
    wptrPoll = Config->WorkGpu + BC250_SDMA1_WORK_WPTR_OFFSET;
    rptrWb = Config->WorkGpu + BC250_SDMA1_WORK_RPTR_OFFSET;
    fenceGpu = Config->WorkGpu + BC250_SDMA1_WORK_FENCE_OFFSET;

    /* SNAPSHOT */
    if (!Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_UCODE_CHECKSUM, &Evidence->UcodeChecksum) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_CLK_CTRL, &Evidence->ClkBefore) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_CNTL, &Evidence->CntlBefore) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_F32_CNTL, &Evidence->F32Before) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_RB_CNTL, &Evidence->RbCntlBefore) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_IB_CNTL, &Evidence->IbCntlBefore) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_STATUS, &Evidence->StatusBefore)) {
        return BC250_SDMA1_READ_FAILED;
    }
    if (Evidence->UcodeChecksum == 0u || Evidence->UcodeChecksum == 0xFFFFFFFFu) {
        return BC250_SDMA1_NO_FIRMWARE;
    }
    if ((Evidence->RbCntlBefore & BC250_SDMA1_RB_ENABLE) != 0u) {
        return BC250_SDMA1_INHERITED_ACTIVE;
    }

    /* APPLY, na ordem de sdma_v5_0_gfx_resume, com o F32 parado. */
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_F32_CNTL,
        BC250_SDMA1_F32_HALT, BC250_SDMA1_F32_HALT, 0);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_IB_CNTL, BC250_SDMA1_IB_ENABLE, 0u, 0);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_WPTR_POLL_CNTL, 0u);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_CLK_CTRL,
        BC250_SDMA1_CLK_OVERRIDES, BC250_SDMA1_CLK_OVERRIDES, 0);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_SEM_WAIT_FAIL_TIMER, 0u);

    for (i = 0u; i < BC250_SDMA1_RING_BYTES / 4u; ++i) Config->RingCpu[i] = 0u;
    Config->WorkCpu[BC250_SDMA1_WORK_WPTR_OFFSET / 4u] = 0u;
    Config->WorkCpu[BC250_SDMA1_WORK_WPTR_OFFSET / 4u + 1u] = 0u;
    Config->WorkCpu[BC250_SDMA1_WORK_RPTR_OFFSET / 4u] = 0u;
    Config->WorkCpu[BC250_SDMA1_WORK_RPTR_OFFSET / 4u + 1u] = 0u;
    Config->WorkCpu[BC250_SDMA1_WORK_FENCE_OFFSET / 4u] = BC250_SDMA1_FENCE_POISON;
    Interface->Barrier(Interface->Context);

    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_RB_CNTL,
        BC250_SDMA1_RB_SIZE_MASK | BC250_SDMA1_RB_ENABLE | BC250_SDMA1_RPTR_WRITEBACK_ENABLE,
        (BC250_SDMA1_RB_SIZE_LOG2_DWORDS << 1) | BC250_SDMA1_RPTR_WRITEBACK_ENABLE,
        &Evidence->RbCntlApplied);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_RPTR, 0u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_RPTR_HI, 0u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_WPTR, 0u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_WPTR_HI, 0u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_WPTR_POLL_ADDR_LO, (uint32_t)wptrPoll);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_WPTR_POLL_ADDR_HI, (uint32_t)(wptrPoll >> 32));
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_WPTR_POLL_CNTL,
        BC250_SDMA1_POLL_CNTL_GOLDEN | BC250_SDMA1_POLL_ENABLE);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RPTR_ADDR_HI, (uint32_t)(rptrWb >> 32));
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RPTR_ADDR_LO, (uint32_t)rptrWb & 0xFFFFFFFCu);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_CHICKEN_BITS,
        BC250_SDMA1_CHICKEN_MASK, BC250_SDMA1_CHICKEN_VALUE, 0);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_GB_ADDR_CONFIG,
        BC250_SDMA1_GB_ADDR_MASK, BC250_SDMA1_GB_ADDR_VALUE, 0);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_GB_ADDR_CONFIG_READ,
        BC250_SDMA1_GB_ADDR_MASK, BC250_SDMA1_GB_ADDR_VALUE, 0);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_BASE, (uint32_t)(Config->RingGpu >> 8));
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_BASE_HI, (uint32_t)(Config->RingGpu >> 40));
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_MINOR_PTR_UPDATE, 1u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_WPTR, 0u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_WPTR_HI, 0u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_MINOR_PTR_UPDATE, 0u);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_DOORBELL, BC250_SDMA1_DOORBELL_ENABLE, 0u, 0);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_CNTL,
        BC250_SDMA1_CNTL_UTC_L1_ENABLE, BC250_SDMA1_CNTL_UTC_L1_ENABLE, &Evidence->CntlApplied);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_UTCL1_CNTL,
        BC250_SDMA1_UTCL1_CLEAR, BC250_SDMA1_UTCL1_SET, 0);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_UTCL1_PAGE,
        BC250_SDMA1_UTCL1_PAGE_MASK, BC250_SDMA1_UTCL1_PAGE_VAL, 0);
    /* unhalt, anel e IB ligados */
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_F32_CNTL, BC250_SDMA1_F32_HALT, 0u,
        &Evidence->F32Applied);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_RB_CNTL,
        BC250_SDMA1_RB_ENABLE, BC250_SDMA1_RB_ENABLE, &Evidence->RbCntlApplied);
    ok &= Bc250Sdma1Rmw(Interface, BC250_SDMA1_REG_IB_CNTL,
        BC250_SDMA1_IB_ENABLE, BC250_SDMA1_IB_ENABLE, 0);
    if (!ok) return Bc250Sdma1Fail(Interface, Evidence, BC250_SDMA1_WRITE_FAILED);

    /* READBACK antes da prova: a fence nao pode esconder configuracao perdida. */
    if (!Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_RB_CNTL, &v) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_RB_BASE, &rbBase) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_RB_BASE_HI, &rbBaseHi) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_WPTR_POLL_CNTL, &pollCntl) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_WPTR_POLL_ADDR_LO, &pollLo) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_WPTR_POLL_ADDR_HI, &pollHi) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_RPTR_ADDR_LO, &rptrLo) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_RPTR_ADDR_HI, &rptrHi) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_IB_CNTL, &ibCntl) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_CHICKEN_BITS, &chicken) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_GB_ADDR_CONFIG, &gb) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_GB_ADDR_CONFIG_READ, &gbRead) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_F32_CNTL, &Evidence->F32Applied) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_CNTL, &Evidence->CntlApplied)) {
        return Bc250Sdma1Fail(Interface, Evidence, BC250_SDMA1_READ_FAILED);
    }
    Evidence->RbCntlApplied = v;
    if ((v & (BC250_SDMA1_RB_ENABLE | BC250_SDMA1_RB_SIZE_MASK | BC250_SDMA1_RPTR_WRITEBACK_ENABLE)) !=
            (BC250_SDMA1_RB_ENABLE | (BC250_SDMA1_RB_SIZE_LOG2_DWORDS << 1) |
             BC250_SDMA1_RPTR_WRITEBACK_ENABLE) ||
        rbBase != (uint32_t)(Config->RingGpu >> 8) ||
        rbBaseHi != (uint32_t)(Config->RingGpu >> 40) ||
        pollCntl != (BC250_SDMA1_POLL_CNTL_GOLDEN | BC250_SDMA1_POLL_ENABLE) ||
        pollLo != (uint32_t)wptrPoll || pollHi != (uint32_t)(wptrPoll >> 32) ||
        rptrLo != ((uint32_t)rptrWb & 0xFFFFFFFCu) || rptrHi != (uint32_t)(rptrWb >> 32) ||
        (ibCntl & BC250_SDMA1_IB_ENABLE) == 0u ||
        (chicken & BC250_SDMA1_CHICKEN_MASK) != (BC250_SDMA1_CHICKEN_VALUE & BC250_SDMA1_CHICKEN_MASK) ||
        (gb & BC250_SDMA1_GB_ADDR_MASK) != BC250_SDMA1_GB_ADDR_VALUE ||
        (gbRead & BC250_SDMA1_GB_ADDR_MASK) != BC250_SDMA1_GB_ADDR_VALUE ||
        (Evidence->F32Applied & BC250_SDMA1_F32_HALT) != 0u ||
        (Evidence->CntlApplied & BC250_SDMA1_CNTL_UTC_L1_ENABLE) == 0u) {
        return Bc250Sdma1Fail(Interface, Evidence, BC250_SDMA1_READBACK_FAILED);
    }
    Evidence->ReadbackPassed = 1u;

    /* PROVA: FENCE sobre poison (MTYPE_UC no pacote) + RPTR == WPTR. */
    for (i = 0u; i < BC250_SDMA1_PROOF_DWORDS; ++i) proof[i] = 0u; /* 0 = NOP */
    if (Bc250SdmaEmitFence(proof, BC250_SDMA1_PROOF_DWORDS, &cursor, fenceGpu,
            Config->FenceValue) != BC250_SDMA_OK) {
        return Bc250Sdma1Fail(Interface, Evidence, BC250_SDMA1_EMIT_FAILED);
    }
    for (i = 0u; i < BC250_SDMA1_PROOF_DWORDS; ++i) Config->RingCpu[i] = proof[i];
    Evidence->WptrBytes = BC250_SDMA1_PROOF_DWORDS * 4u;
    Evidence->FenceExpected = Config->FenceValue;
    Config->WorkCpu[BC250_SDMA1_WORK_WPTR_OFFSET / 4u] = Evidence->WptrBytes;
    Interface->Barrier(Interface->Context);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_MINOR_PTR_UPDATE, 1u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_WPTR, Evidence->WptrBytes);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_RB_WPTR_HI, 0u);
    ok &= Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_MINOR_PTR_UPDATE, 0u);
    if (!ok) return Bc250Sdma1Fail(Interface, Evidence, BC250_SDMA1_WRITE_FAILED);
    for (i = 1u; i <= Config->MaxPolls; ++i) {
        Interface->Stall(Interface->Context, 10u);
        Interface->Barrier(Interface->Context);
        Evidence->ProofPolls = i;
        Evidence->FenceObserved = Config->WorkCpu[BC250_SDMA1_WORK_FENCE_OFFSET / 4u];
        if (!Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_RB_RPTR, &Evidence->RptrObserved)) {
            return Bc250Sdma1Fail(Interface, Evidence, BC250_SDMA1_READ_FAILED);
        }
        if (Evidence->FenceObserved == Evidence->FenceExpected &&
            Evidence->RptrObserved == Evidence->WptrBytes) {
            break;
        }
    }
    Evidence->RptrWriteback = Config->WorkCpu[BC250_SDMA1_WORK_RPTR_OFFSET / 4u];
    if (Evidence->FenceObserved != Evidence->FenceExpected ||
        Evidence->RptrObserved != Evidence->WptrBytes) {
        return Bc250Sdma1Fail(Interface, Evidence, BC250_SDMA1_PROOF_TIMEOUT);
    }
    Evidence->ProofPassed = 1u;

    /* Relogio de volta ao POST: o Linux nao o toca na Cyan Skillfish (doc 557). */
    if (!Bc250Sdma1Wr(Interface, BC250_SDMA1_REG_CLK_CTRL, Evidence->ClkBefore) ||
        !Bc250Sdma1Rd(Interface, BC250_SDMA1_REG_CLK_CTRL, &Evidence->ClkRestored) ||
        Evidence->ClkRestored != Evidence->ClkBefore) {
        return Bc250Sdma1Fail(Interface, Evidence, BC250_SDMA1_READBACK_FAILED);
    }
    Evidence->PhysicalReady = 1u;
    return BC250_SDMA1_OK;
}

const char *Bc250Sdma1StatusName(BC250_SDMA1_STATUS Status)
{
    switch (Status) {
    case BC250_SDMA1_OK: return "OK";
    case BC250_SDMA1_BAD_ARGUMENT: return "BAD_ARGUMENT";
    case BC250_SDMA1_READ_FAILED: return "READ_FAILED";
    case BC250_SDMA1_WRITE_FAILED: return "WRITE_FAILED";
    case BC250_SDMA1_NO_FIRMWARE: return "NO_FIRMWARE";
    case BC250_SDMA1_INHERITED_ACTIVE: return "INHERITED_ACTIVE";
    case BC250_SDMA1_READBACK_FAILED: return "READBACK_FAILED";
    case BC250_SDMA1_EMIT_FAILED: return "EMIT_FAILED";
    case BC250_SDMA1_PROOF_TIMEOUT: return "PROOF_TIMEOUT";
    case BC250_SDMA1_SHUTDOWN_UNSAFE: return "SHUTDOWN_UNSAFE";
    default: return "DESCONHECIDO";
    }
}
