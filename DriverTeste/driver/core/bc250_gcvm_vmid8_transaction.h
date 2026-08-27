#ifndef BC250_GCVM_VMID8_TRANSACTION_H
#define BC250_GCVM_VMID8_TRANSACTION_H

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

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GCVM_VMID8 8u
#define BC250_GCVM_VMID8_CNTL        0x0000A220u
#define BC250_GCVM_VMID8_PT_BASE_LO  0x0000A3ECu
#define BC250_GCVM_VMID8_PT_BASE_HI  0x0000A3F0u
#define BC250_GCVM_VMID8_PT_START_LO 0x0000A46Cu
#define BC250_GCVM_VMID8_PT_START_HI 0x0000A470u
#define BC250_GCVM_VMID8_PT_END_LO   0x0000A4ECu
#define BC250_GCVM_VMID8_PT_END_HI   0x0000A4F0u
#define BC250_GCVM_ENABLE_CONTEXT    0x00000001u
#define BC250_GCVM_VMID8_IDENTITY    0x564D4938u /* VMI8 */

typedef enum BC250_GCVM_VMID8_STATUS {
    BC250_GCVM_VMID8_OK = 0,
    BC250_GCVM_VMID8_BAD_ARGUMENT,
    BC250_GCVM_VMID8_CONTEXT_ENABLED,
    BC250_GCVM_VMID8_READ_FAILED,
    BC250_GCVM_VMID8_WRITE_FAILED,
    BC250_GCVM_VMID8_READBACK_FAILED,
    BC250_GCVM_VMID8_TLB_FAILED,
    BC250_GCVM_VMID8_RESTORE_FAILED
} BC250_GCVM_VMID8_STATUS;

typedef uint32_t (*BC250_GCVM_VMID8_READ32)(
    void *Context, uint32_t ByteOffset, uint32_t *Value);
typedef uint32_t (*BC250_GCVM_VMID8_WRITE32)(
    void *Context, uint32_t ByteOffset, uint32_t Value);
typedef uint32_t (*BC250_GCVM_VMID8_FLUSH)(void *Context);

typedef struct BC250_GCVM_VMID8_INTERFACE {
    BC250_GCVM_VMID8_READ32 Read32;
    BC250_GCVM_VMID8_WRITE32 Write32;
    BC250_GCVM_VMID8_FLUSH Flush;
    void *Context;
    uint32_t Identity;
} BC250_GCVM_VMID8_INTERFACE;

typedef struct BC250_GCVM_VMID8_EVIDENCE {
    uint64_t PageTablePhysical;
    uint32_t ContextControlBefore;
    uint32_t BaseLowBefore;
    uint32_t BaseHighBefore;
    uint32_t StartLowBefore;
    uint32_t StartHighBefore;
    uint32_t EndLowBefore;
    uint32_t EndHighBefore;
    uint32_t ProgramReadbackPassed;
    uint32_t TlbFlushPassed;
    uint32_t RestorePassed;
    uint32_t PhysicalReady;
} BC250_GCVM_VMID8_EVIDENCE;

BC250_GCVM_VMID8_STATUS Bc250RunGcvmVmid8InertTransaction(
    uint64_t PageTablePhysical,
    const BC250_GCVM_VMID8_INTERFACE *Interface,
    BC250_GCVM_VMID8_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif
