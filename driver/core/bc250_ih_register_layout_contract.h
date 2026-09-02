#ifndef BC250_IH_REGISTER_LAYOUT_CONTRACT_H
#define BC250_IH_REGISTER_LAYOUT_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
typedef UINT16 uint16_t;
typedef UINT8 uint8_t;
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Official OSSSYS 5.0.0 register indices, expressed in DWORDs. */
#define BC250_OSSSYS5_IH_RB_CNTL_DWORD       0x0080u
#define BC250_OSSSYS5_IH_RB_BASE_DWORD       0x0081u
#define BC250_OSSSYS5_IH_RB_BASE_HI_DWORD    0x0082u
#define BC250_OSSSYS5_IH_RB_RPTR_DWORD       0x0083u
#define BC250_OSSSYS5_IH_RB_WPTR_DWORD       0x0084u
#define BC250_OSSSYS5_IH_RB_WPTR_HI_DWORD    0x0085u
#define BC250_OSSSYS5_IH_RB_WPTR_LO_DWORD    0x0086u
#define BC250_OSSSYS5_IH_DOORBELL_DWORD      0x0087u
#define BC250_OSSSYS5_IH_CNTL_DWORD          0x00C0u
#define BC250_OSSSYS5_IH_CNTL2_DWORD         0x00C1u
#define BC250_OSSSYS5_IH_STATUS_DWORD        0x00C2u

typedef enum BC250_IH_REGISTER_LAYOUT_STATUS {
    BC250_IH_REGISTER_LAYOUT_OK = 0,
    BC250_IH_REGISTER_LAYOUT_BAD_ARGUMENT,
    BC250_IH_REGISTER_LAYOUT_UNSUPPORTED_VERSION,
    BC250_IH_REGISTER_LAYOUT_BASE_UNALIGNED,
    BC250_IH_REGISTER_LAYOUT_OVERFLOW,
    BC250_IH_REGISTER_LAYOUT_MISMATCH
} BC250_IH_REGISTER_LAYOUT_STATUS;

typedef struct BC250_IH_REGISTER_LAYOUT {
    uint32_t OsssysBaseDwords;
    uint32_t IhRbCntlBytes;
    uint32_t IhRbBaseBytes;
    uint32_t IhRbBaseHiBytes;
    uint32_t IhRbRptrBytes;
    uint32_t IhRbWptrBytes;
    uint32_t IhRbWptrAddrHiBytes;
    uint32_t IhRbWptrAddrLoBytes;
    uint32_t IhDoorbellRptrBytes;
    uint32_t IhCntlBytes;
    uint32_t IhCntl2Bytes;
    uint32_t IhStatusBytes;
} BC250_IH_REGISTER_LAYOUT;

BC250_IH_REGISTER_LAYOUT_STATUS Bc250ComposeOsssys5IhRegisterLayout(
    uint8_t Major,
    uint8_t Minor,
    uint32_t OsssysBaseDwords,
    BC250_IH_REGISTER_LAYOUT *Layout);

BC250_IH_REGISTER_LAYOUT_STATUS Bc250ClassifyOsssys5IhByteLayout(
    const BC250_IH_REGISTER_LAYOUT *Candidate,
    uint32_t *InferredBaseDwords);

const char *Bc250IhRegisterLayoutStatusName(
    BC250_IH_REGISTER_LAYOUT_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
