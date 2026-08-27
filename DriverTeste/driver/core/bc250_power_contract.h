#ifndef BC250_POWER_CONTRACT_H
#define BC250_POWER_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BC250_DEVICE_POWER_STATE {
    BC250_POWER_STATE_UNSPECIFIED = 0,
    BC250_POWER_STATE_D0 = 1,
    BC250_POWER_STATE_D1 = 2,
    BC250_POWER_STATE_D2 = 3,
    BC250_POWER_STATE_D3 = 4
} BC250_DEVICE_POWER_STATE;

typedef enum BC250_POWER_STATUS {
    BC250_POWER_OK = 0,
    BC250_POWER_BAD_ARGUMENT,
    BC250_POWER_INVALID_STATE,
    BC250_POWER_DEVICE_BUSY,
    BC250_POWER_HARDWARE_ERROR
} BC250_POWER_STATUS;

typedef struct BC250_POWER_CONTEXT {
    uint32_t CurrentPowerState;
    uint32_t PreviousPowerState;
    uint32_t StablePowerStateEnabled;
    uint32_t GfxoffPermitted;
    uint32_t TransitionCount;
} BC250_POWER_CONTEXT;

BC250_POWER_STATUS Bc250PowerInitialize(
    BC250_POWER_CONTEXT *Context);

BC250_POWER_STATUS Bc250PowerSetState(
    BC250_POWER_CONTEXT *Context,
    uint32_t TargetState,
    uint32_t ActionType);

BC250_POWER_STATUS Bc250PowerSetStableState(
    BC250_POWER_CONTEXT *Context,
    uint32_t EnableStablePower);

const char *Bc250PowerStatusName(BC250_POWER_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
