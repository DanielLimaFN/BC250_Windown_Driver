#include "bc250_post_display_reservation.h"

#define BC250_UINT64_MAX_VALUE ((uint64_t)~(uint64_t)0)

static uint32_t Bc250DisplayBytesPerPixel(D3DDDIFORMAT Format)
{
    switch (Format) {
    case D3DDDIFMT_R5G6B5: return 2u;
    case D3DDDIFMT_A8R8G8B8:
    case D3DDDIFMT_X8R8G8B8:
    case D3DDDIFMT_A2R10G10B10:
        return 4u;
    default: return 0u;
    }
}

BC250_POST_DISPLAY_STATUS Bc250AcquirePostDisplayReservation(
    const DXGKRNL_INTERFACE *DxgkInterface,
    const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    size_t ResolvedBarCount,
    uint32_t VramApertureBarIndex,
    uint32_t SnapshotGeneration,
    BC250_INHERITED_VRAM_RESERVATION *Reservation,
    BC250_POST_DISPLAY_EVIDENCE *Evidence,
    NTSTATUS *CallbackStatus)
{
    const BC250_RESOLVED_BAR_RESOURCE *aperture = NULL;
    DXGK_DISPLAY_INFORMATION displayInfo;
    DXGK_DISPLAY_OWNERSHIP_FLAGS ownershipFlags;
    NTSTATUS status;
    uint64_t physicalStart;
    uint64_t frameBytes;
    uint64_t apertureEnd;
    uint64_t frameEnd;
    uint64_t minimumPitch;
    uint32_t bytesPerPixel;
    uint32_t usedCallback2 = 0;
    size_t apertureCount = 0;
    size_t i;
    const SIZE_T minimumInterfaceBytes =
        FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership) +
        sizeof(DxgkInterface->DxgkCbAcquirePostDisplayOwnership);
    const SIZE_T callback2InterfaceBytes =
        FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership2) +
        sizeof(DxgkInterface->DxgkCbAcquirePostDisplayOwnership2);

    if (Reservation != NULL) RtlZeroMemory(Reservation, sizeof(*Reservation));
    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    if (CallbackStatus != NULL) *CallbackStatus = STATUS_INVALID_PARAMETER;
    if (DxgkInterface == NULL || ResolvedBars == NULL ||
        ResolvedBarCount == 0 || Reservation == NULL || Evidence == NULL ||
        VramApertureBarIndex >= BC250_RESOURCE_MAX_BARS ||
        SnapshotGeneration == 0) return BC250_POST_DISPLAY_BAD_ARGUMENT;
    if (DxgkInterface->Size < minimumInterfaceBytes ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbAcquirePostDisplayOwnership == NULL) {
        return BC250_POST_DISPLAY_INTERFACE_TOO_SMALL;
    }
    for (i = 0; i < ResolvedBarCount; ++i) {
        if (ResolvedBars[i].Index != VramApertureBarIndex) continue;
        aperture = &ResolvedBars[i];
        ++apertureCount;
    }
    if (apertureCount != 1 || aperture == NULL || aperture->Length == 0) {
        return BC250_POST_DISPLAY_APERTURE_NOT_UNIQUE;
    }

    RtlZeroMemory(&displayInfo, sizeof(displayInfo));
    RtlZeroMemory(&ownershipFlags, sizeof(ownershipFlags));
    if (DxgkInterface->Size >= callback2InterfaceBytes &&
        DxgkInterface->DxgkCbAcquirePostDisplayOwnership2 != NULL) {
        usedCallback2 = 1;
        status = DxgkInterface->DxgkCbAcquirePostDisplayOwnership2(
            DxgkInterface->DeviceHandle, &displayInfo, &ownershipFlags);
    } else {
        status = DxgkInterface->DxgkCbAcquirePostDisplayOwnership(
            DxgkInterface->DeviceHandle, &displayInfo);
    }
    if (CallbackStatus != NULL) *CallbackStatus = status;
    if (!NT_SUCCESS(status)) return BC250_POST_DISPLAY_CALLBACK_FAILED;
    if (usedCallback2 != 0 &&
        ownershipFlags.FrameBufferState == FrameBufferStateUnknown) {
        return BC250_POST_DISPLAY_FRAMEBUFFER_STATE_UNKNOWN;
    }

    bytesPerPixel = Bc250DisplayBytesPerPixel(displayInfo.ColorFormat);
    if (bytesPerPixel == 0) return BC250_POST_DISPLAY_FORMAT_UNSUPPORTED;
    minimumPitch = (uint64_t)displayInfo.Width * bytesPerPixel;
    if (displayInfo.Width == 0 || displayInfo.Height == 0 ||
        displayInfo.Pitch == 0 || minimumPitch > displayInfo.Pitch) {
        return BC250_POST_DISPLAY_GEOMETRY_REJECTED;
    }
    if ((uint64_t)displayInfo.Pitch >
        BC250_UINT64_MAX_VALUE / displayInfo.Height) {
        return BC250_POST_DISPLAY_RANGE_OVERFLOW;
    }
    frameBytes = (uint64_t)displayInfo.Pitch * displayInfo.Height;
    physicalStart = (uint64_t)displayInfo.PhysicAddress.QuadPart;
    if (physicalStart > BC250_UINT64_MAX_VALUE - (frameBytes - 1u) ||
        aperture->Start >
            BC250_UINT64_MAX_VALUE - (aperture->Length - 1u)) {
        return BC250_POST_DISPLAY_RANGE_OVERFLOW;
    }
    frameEnd = physicalStart + frameBytes - 1u;
    apertureEnd = aperture->Start + aperture->Length - 1u;
    if (physicalStart < aperture->Start || frameEnd > apertureEnd) {
        return BC250_POST_DISPLAY_OUTSIDE_APERTURE;
    }

    Reservation->Offset = physicalStart - aperture->Start;
    Reservation->Length = frameBytes;
    Reservation->Alignment = 1u;
    Reservation->Role = BC250_VRAM_ROLE_SCANOUT;
    Reservation->Flags = BC250_VRAM_ALLOC_CPU_VISIBLE |
        BC250_VRAM_REGION_PERSISTENT;
    Evidence->SnapshotGeneration = SnapshotGeneration;
    Evidence->UsedOwnershipCallback2 = usedCallback2;
    Evidence->FrameBufferState = usedCallback2 != 0 ?
        ownershipFlags.FrameBufferState : FrameBufferStateUnknown;
    Evidence->TargetId = displayInfo.TargetId;
    Evidence->Width = displayInfo.Width;
    Evidence->Height = displayInfo.Height;
    Evidence->Pitch = displayInfo.Pitch;
    Evidence->BytesPerPixel = bytesPerPixel;
    Evidence->PhysicalStart = physicalStart;
    Evidence->Length = frameBytes;
    Evidence->VramOffset = Reservation->Offset;
    return BC250_POST_DISPLAY_OK;
}

const char *Bc250PostDisplayStatusName(BC250_POST_DISPLAY_STATUS Status)
{
    switch (Status) {
    case BC250_POST_DISPLAY_OK: return "ok";
    case BC250_POST_DISPLAY_BAD_ARGUMENT: return "bad_argument";
    case BC250_POST_DISPLAY_INTERFACE_TOO_SMALL:
        return "interface_too_small";
    case BC250_POST_DISPLAY_CALLBACK_FAILED: return "callback_failed";
    case BC250_POST_DISPLAY_FRAMEBUFFER_STATE_UNKNOWN:
        return "framebuffer_state_unknown";
    case BC250_POST_DISPLAY_FORMAT_UNSUPPORTED:
        return "format_unsupported";
    case BC250_POST_DISPLAY_GEOMETRY_REJECTED:
        return "geometry_rejected";
    case BC250_POST_DISPLAY_RANGE_OVERFLOW: return "range_overflow";
    case BC250_POST_DISPLAY_APERTURE_NOT_UNIQUE:
        return "aperture_not_unique";
    case BC250_POST_DISPLAY_OUTSIDE_APERTURE:
        return "outside_aperture";
    default: return "unknown";
    }
}
