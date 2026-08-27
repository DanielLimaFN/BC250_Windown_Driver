#ifndef BC250_SDMA_F32_TRANSACTION_H
#define BC250_SDMA_F32_TRANSACTION_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stdint.h>
#endif

#define BC250_SDMA_F32_IDENTITY          0x53463332u /* SF32 */
#define BC250_SDMA0_CLK_CTRL             0x000049ECu
#define BC250_SDMA0_CNTL                 0x000049F0u
#define BC250_SDMA0_F32_CNTL             0x00004A28u
#define BC250_SDMA0_STATUS2_REG          0x00004A60u
#define BC250_SDMA_CLK_FORCE_MASK        0xFF000000u
#define BC250_SDMA_CNTL_CTXEMPTY_INT_EN  0x00040000u
#define BC250_SDMA_F32_HALT              0x00000001u
#define BC250_SDMA_F32_RESET             0x00000200u
#define BC250_SDMA_F32_PC_MASK           0x0000FFFCu
#define BC250_SDMA_F32_PC_SHIFT          2u
#define BC250_SDMA_F32_SAMPLE_COUNT      16u

typedef uint32_t (*BC250_SDMA_F32_READ32)(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
typedef uint32_t (*BC250_SDMA_F32_WRITE32)(
    void *Context, uint32_t ByteOffset, uint32_t Value);
typedef void (*BC250_SDMA_F32_STALL)(void *Context, uint32_t Microseconds);

typedef struct BC250_SDMA_F32_INTERFACE {
    BC250_SDMA_F32_READ32 Read32;
    BC250_SDMA_F32_WRITE32 Write32;
    BC250_SDMA_F32_STALL Stall;
    void *Context;
    uint32_t Identity;
} BC250_SDMA_F32_INTERFACE;

typedef enum BC250_SDMA_F32_STATUS {
    BC250_SDMA_F32_OK = 0,
    BC250_SDMA_F32_BAD_ARGUMENT,
    BC250_SDMA_F32_PREFLIGHT_FAILED,
    BC250_SDMA_F32_WRITE_FAILED,
    BC250_SDMA_F32_READBACK_FAILED,
    BC250_SDMA_F32_PC_STATIC,
    BC250_SDMA_F32_RESTORE_FAILED
} BC250_SDMA_F32_STATUS;

typedef struct BC250_SDMA_F32_EVIDENCE {
    uint32_t ClockBefore;
    uint32_t ControlBefore;
    uint32_t F32Before;
    uint32_t Status2Before;
    uint32_t ClockApplied;
    uint32_t ControlApplied;
    uint32_t F32Applied;
    uint32_t PcFirst;
    uint32_t PcLast;
    uint32_t PcTransitions;
    uint32_t ClockRestored;
    uint32_t ControlRestored;
    uint32_t F32Restored;
    uint32_t ApplyReadbackPassed;
    uint32_t RestorePassed;
    uint32_t PhysicalReady;
} BC250_SDMA_F32_EVIDENCE;

BC250_SDMA_F32_STATUS Bc250RunSdmaF32Transaction(
    const BC250_SDMA_F32_INTERFACE *Interface,
    BC250_SDMA_F32_EVIDENCE *Evidence);

#endif
