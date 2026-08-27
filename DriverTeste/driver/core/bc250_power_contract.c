#include "bc250_power_contract.h"

#include <string.h>

BC250_POWER_STATUS Bc250PowerInitialize(
    BC250_POWER_CONTEXT *Context)
{
    if (Context == NULL) {
        return BC250_POWER_BAD_ARGUMENT;
    }

    memset(Context, 0, sizeof(BC250_POWER_CONTEXT));
    Context->CurrentPowerState = BC250_POWER_STATE_D0;
    Context->PreviousPowerState = BC250_POWER_STATE_UNSPECIFIED;
    Context->StablePowerStateEnabled = 0u;
    Context->GfxoffPermitted = 1u;
    Context->TransitionCount = 0u;

    return BC250_POWER_OK;
}

BC250_POWER_STATUS Bc250PowerSetState(
    BC250_POWER_CONTEXT *Context,
    uint32_t TargetState,
    uint32_t ActionType)
{
    if (Context == NULL) {
        return BC250_POWER_BAD_ARGUMENT;
    }

    if (TargetState != BC250_POWER_STATE_D0 &&
        TargetState != BC250_POWER_STATE_D1 &&
        TargetState != BC250_POWER_STATE_D2 &&
        TargetState != BC250_POWER_STATE_D3) {
        return BC250_POWER_INVALID_STATE;
    }

#ifdef BC250_KERNEL_MODE
    UNREFERENCED_PARAMETER(ActionType);
#else
    (void)ActionType;
#endif

    if (Context->CurrentPowerState == TargetState) {
        return BC250_POWER_OK;
    }

    Context->PreviousPowerState = Context->CurrentPowerState;
    Context->CurrentPowerState = TargetState;
    Context->TransitionCount++;

    if (TargetState == BC250_POWER_STATE_D3) {
        Context->GfxoffPermitted = 0u;
    } else if (TargetState == BC250_POWER_STATE_D0) {
        Context->GfxoffPermitted = (Context->StablePowerStateEnabled != 0u) ? 0u : 1u;
    }

    return BC250_POWER_OK;
}

BC250_POWER_STATUS Bc250PowerSetStableState(
    BC250_POWER_CONTEXT *Context,
    uint32_t EnableStablePower)
{
    if (Context == NULL) {
        return BC250_POWER_BAD_ARGUMENT;
    }

    Context->StablePowerStateEnabled = (EnableStablePower != 0u) ? 1u : 0u;
    if (Context->CurrentPowerState == BC250_POWER_STATE_D0) {
        Context->GfxoffPermitted = (EnableStablePower != 0u) ? 0u : 1u;
    }

    return BC250_POWER_OK;
}

const char *Bc250PowerStatusName(BC250_POWER_STATUS Status)
{
    switch (Status) {
    case BC250_POWER_OK: return "ok";
    case BC250_POWER_BAD_ARGUMENT: return "bad_argument";
    case BC250_POWER_INVALID_STATE: return "invalid_state";
    case BC250_POWER_DEVICE_BUSY: return "device_busy";
    case BC250_POWER_HARDWARE_ERROR: return "hardware_error";
    default: return "unknown";
    }
}
