#include "bc250_ih_register_layout_contract.h"

#include <string.h>

static BC250_IH_REGISTER_LAYOUT_STATUS Bc250ComposeByteOffset(
    uint32_t BaseDwords,
    uint32_t RegisterDwords,
    uint32_t *ByteOffset)
{
    uint32_t sum;
    if (ByteOffset == 0) return BC250_IH_REGISTER_LAYOUT_BAD_ARGUMENT;
    if (BaseDwords > 0xFFFFFFFFu - RegisterDwords) {
        return BC250_IH_REGISTER_LAYOUT_OVERFLOW;
    }
    sum = BaseDwords + RegisterDwords;
    if (sum > 0x3FFFFFFFu) return BC250_IH_REGISTER_LAYOUT_OVERFLOW;
    *ByteOffset = sum * 4u;
    return BC250_IH_REGISTER_LAYOUT_OK;
}

BC250_IH_REGISTER_LAYOUT_STATUS Bc250ComposeOsssys5IhRegisterLayout(
    uint8_t Major,
    uint8_t Minor,
    uint32_t OsssysBaseDwords,
    BC250_IH_REGISTER_LAYOUT *Layout)
{
    BC250_IH_REGISTER_LAYOUT candidate;
    BC250_IH_REGISTER_LAYOUT_STATUS status;
#define COMPOSE(Field, Register) do { \
    status = Bc250ComposeByteOffset(OsssysBaseDwords, Register, \
                                    &candidate.Field); \
    if (status != BC250_IH_REGISTER_LAYOUT_OK) return status; \
} while (0)

    if (Layout == 0) return BC250_IH_REGISTER_LAYOUT_BAD_ARGUMENT;
    memset(Layout, 0, sizeof(*Layout));
    if (Major != 5u || Minor != 0u) {
        return BC250_IH_REGISTER_LAYOUT_UNSUPPORTED_VERSION;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.OsssysBaseDwords = OsssysBaseDwords;
    COMPOSE(IhRbCntlBytes, BC250_OSSSYS5_IH_RB_CNTL_DWORD);
    COMPOSE(IhRbBaseBytes, BC250_OSSSYS5_IH_RB_BASE_DWORD);
    COMPOSE(IhRbBaseHiBytes, BC250_OSSSYS5_IH_RB_BASE_HI_DWORD);
    COMPOSE(IhRbRptrBytes, BC250_OSSSYS5_IH_RB_RPTR_DWORD);
    COMPOSE(IhRbWptrBytes, BC250_OSSSYS5_IH_RB_WPTR_DWORD);
    COMPOSE(IhRbWptrAddrHiBytes, BC250_OSSSYS5_IH_RB_WPTR_HI_DWORD);
    COMPOSE(IhRbWptrAddrLoBytes, BC250_OSSSYS5_IH_RB_WPTR_LO_DWORD);
    COMPOSE(IhDoorbellRptrBytes, BC250_OSSSYS5_IH_DOORBELL_DWORD);
    COMPOSE(IhCntlBytes, BC250_OSSSYS5_IH_CNTL_DWORD);
    COMPOSE(IhCntl2Bytes, BC250_OSSSYS5_IH_CNTL2_DWORD);
    COMPOSE(IhStatusBytes, BC250_OSSSYS5_IH_STATUS_DWORD);
    *Layout = candidate;
    return BC250_IH_REGISTER_LAYOUT_OK;
#undef COMPOSE
}

BC250_IH_REGISTER_LAYOUT_STATUS Bc250ClassifyOsssys5IhByteLayout(
    const BC250_IH_REGISTER_LAYOUT *Candidate,
    uint32_t *InferredBaseDwords)
{
    BC250_IH_REGISTER_LAYOUT expected;
    uint32_t rbCntlDwords;
    uint32_t baseDwords;
    BC250_IH_REGISTER_LAYOUT_STATUS status;

    if (InferredBaseDwords != 0) *InferredBaseDwords = 0;
    if (Candidate == 0 || InferredBaseDwords == 0) {
        return BC250_IH_REGISTER_LAYOUT_BAD_ARGUMENT;
    }
    if ((Candidate->IhRbCntlBytes & 3u) != 0) {
        return BC250_IH_REGISTER_LAYOUT_BASE_UNALIGNED;
    }
    rbCntlDwords = Candidate->IhRbCntlBytes / 4u;
    if (rbCntlDwords < BC250_OSSSYS5_IH_RB_CNTL_DWORD) {
        return BC250_IH_REGISTER_LAYOUT_MISMATCH;
    }
    baseDwords = rbCntlDwords - BC250_OSSSYS5_IH_RB_CNTL_DWORD;
    status = Bc250ComposeOsssys5IhRegisterLayout(5u, 0u, baseDwords,
                                                 &expected);
    if (status != BC250_IH_REGISTER_LAYOUT_OK) return status;
    if (Candidate->IhRbBaseBytes != expected.IhRbBaseBytes ||
        Candidate->IhRbBaseHiBytes != expected.IhRbBaseHiBytes ||
        Candidate->IhRbRptrBytes != expected.IhRbRptrBytes ||
        Candidate->IhRbWptrBytes != expected.IhRbWptrBytes ||
        Candidate->IhRbWptrAddrHiBytes != expected.IhRbWptrAddrHiBytes ||
        Candidate->IhRbWptrAddrLoBytes != expected.IhRbWptrAddrLoBytes ||
        Candidate->IhDoorbellRptrBytes != expected.IhDoorbellRptrBytes ||
        Candidate->IhCntlBytes != expected.IhCntlBytes ||
        Candidate->IhCntl2Bytes != expected.IhCntl2Bytes ||
        Candidate->IhStatusBytes != expected.IhStatusBytes) {
        return BC250_IH_REGISTER_LAYOUT_MISMATCH;
    }
    *InferredBaseDwords = baseDwords;
    return BC250_IH_REGISTER_LAYOUT_OK;
}

const char *Bc250IhRegisterLayoutStatusName(
    BC250_IH_REGISTER_LAYOUT_STATUS Status)
{
    switch (Status) {
    case BC250_IH_REGISTER_LAYOUT_OK: return "ok";
    case BC250_IH_REGISTER_LAYOUT_BAD_ARGUMENT: return "bad_argument";
    case BC250_IH_REGISTER_LAYOUT_UNSUPPORTED_VERSION: return "unsupported_version";
    case BC250_IH_REGISTER_LAYOUT_BASE_UNALIGNED: return "base_unaligned";
    case BC250_IH_REGISTER_LAYOUT_OVERFLOW: return "overflow";
    case BC250_IH_REGISTER_LAYOUT_MISMATCH: return "layout_mismatch";
    default: return "unknown";
    }
}
