#ifndef BC250_POST_DISPLAY_RESERVATION_H
#define BC250_POST_DISPLAY_RESERVATION_H

#include <ntddk.h>
#include <dispmprt.h>

#include "../core/bc250_owned_adapter_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BC250_POST_DISPLAY_STATUS {
    BC250_POST_DISPLAY_OK = 0,
    BC250_POST_DISPLAY_BAD_ARGUMENT,
    BC250_POST_DISPLAY_INTERFACE_TOO_SMALL,
    BC250_POST_DISPLAY_CALLBACK_FAILED,
    BC250_POST_DISPLAY_FRAMEBUFFER_STATE_UNKNOWN,
    BC250_POST_DISPLAY_FORMAT_UNSUPPORTED,
    BC250_POST_DISPLAY_GEOMETRY_REJECTED,
    BC250_POST_DISPLAY_RANGE_OVERFLOW,
    BC250_POST_DISPLAY_APERTURE_NOT_UNIQUE,
    BC250_POST_DISPLAY_OUTSIDE_APERTURE
} BC250_POST_DISPLAY_STATUS;

typedef struct BC250_POST_DISPLAY_EVIDENCE {
    uint32_t SnapshotGeneration;
    uint32_t UsedOwnershipCallback2;
    uint32_t FrameBufferState;
    uint32_t TargetId;
    uint32_t Width;
    uint32_t Height;
    uint32_t Pitch;
    uint32_t BytesPerPixel;
    uint64_t PhysicalStart;
    uint64_t Length;
    uint64_t VramOffset;
} BC250_POST_DISPLAY_EVIDENCE;

/*
 * This callback transfers post-display ownership. It is deliberately not
 * called by the readonly DDI. The function is prepared and tested offline for
 * a future, explicitly mutating activation phase.
 */
BC250_POST_DISPLAY_STATUS Bc250AcquirePostDisplayReservation(
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _In_reads_(ResolvedBarCount)
        const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    _In_ size_t ResolvedBarCount,
    _In_ uint32_t VramApertureBarIndex,
    _In_ uint32_t SnapshotGeneration,
    _Out_ BC250_INHERITED_VRAM_RESERVATION *Reservation,
    _Out_ BC250_POST_DISPLAY_EVIDENCE *Evidence,
    _Out_opt_ NTSTATUS *CallbackStatus);

const char *Bc250PostDisplayStatusName(BC250_POST_DISPLAY_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
