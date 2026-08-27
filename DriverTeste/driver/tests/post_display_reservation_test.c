#include "../wdk/bc250_post_display_reservation.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)

static DXGK_DISPLAY_INFORMATION g_displayInfo;
static DXGK_DISPLAY_OWNERSHIP_FLAGS g_ownershipFlags;
static NTSTATUS g_callbackStatus;
static uint32_t g_callback1Calls;
static uint32_t g_callback2Calls;

static NTSTATUS APIENTRY Bc250TestAcquireOwnership(
    HANDLE DeviceHandle,
    PDXGK_DISPLAY_INFORMATION DisplayInfo)
{
    ++g_callback1Calls;
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u || DisplayInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *DisplayInfo = g_displayInfo;
    return g_callbackStatus;
}

static NTSTATUS APIENTRY Bc250TestAcquireOwnership2(
    HANDLE DeviceHandle,
    PDXGK_DISPLAY_INFORMATION DisplayInfo,
    PDXGK_DISPLAY_OWNERSHIP_FLAGS Flags)
{
    ++g_callback2Calls;
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u || DisplayInfo == NULL ||
        Flags == NULL) return STATUS_INVALID_PARAMETER;
    *DisplayInfo = g_displayInfo;
    *Flags = g_ownershipFlags;
    return g_callbackStatus;
}

static void Bc250BuildValidInputs(
    DXGKRNL_INTERFACE *Interface,
    BC250_RESOLVED_BAR_RESOURCE *Aperture)
{
    memset(Interface, 0, sizeof(*Interface));
    Interface->Size = sizeof(*Interface);
    Interface->DeviceHandle = (HANDLE)(ULONG_PTR)0x250u;
    Interface->DxgkCbAcquirePostDisplayOwnership =
        Bc250TestAcquireOwnership;
    Interface->DxgkCbAcquirePostDisplayOwnership2 =
        Bc250TestAcquireOwnership2;
    memset(Aperture, 0, sizeof(*Aperture));
    Aperture->Index = 0;
    Aperture->Start = 0xC0000000ull;
    Aperture->Length = 0x10000000ull;
    memset(&g_displayInfo, 0, sizeof(g_displayInfo));
    g_displayInfo.Width = 1920;
    g_displayInfo.Height = 1080;
    g_displayInfo.Pitch = 7680;
    g_displayInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
    g_displayInfo.PhysicAddress.QuadPart = 0xC0100000ll;
    g_displayInfo.TargetId = 3;
    memset(&g_ownershipFlags, 0, sizeof(g_ownershipFlags));
    g_ownershipFlags.FrameBufferState =
        FrameBufferStateInitializedByFirmware;
    g_callbackStatus = STATUS_SUCCESS;
    g_callback1Calls = 0;
    g_callback2Calls = 0;
}

static int Bc250RunPostDisplayReservationTest(void)
{
    DXGKRNL_INTERFACE interfaceData;
    BC250_RESOLVED_BAR_RESOURCE apertures[2];
    BC250_INHERITED_VRAM_RESERVATION reservation;
    BC250_POST_DISPLAY_EVIDENCE evidence;
    NTSTATUS callbackStatus;

    Bc250BuildValidInputs(&interfaceData, &apertures[0]);
    CHECK(Bc250AcquirePostDisplayReservation(
        &interfaceData, apertures, 1, 0, 77, &reservation, &evidence,
        &callbackStatus) == BC250_POST_DISPLAY_OK);
    CHECK(callbackStatus == STATUS_SUCCESS);
    CHECK(g_callback1Calls == 0u && g_callback2Calls == 1u);
    CHECK(reservation.Offset == 0x00100000ull);
    CHECK(reservation.Length == 8294400ull);
    CHECK(reservation.Role == BC250_VRAM_ROLE_SCANOUT);
    CHECK(reservation.Flags == (BC250_VRAM_ALLOC_CPU_VISIBLE |
                                BC250_VRAM_REGION_PERSISTENT));
    CHECK(evidence.SnapshotGeneration == 77u);
    CHECK(evidence.FrameBufferState ==
        FrameBufferStateInitializedByFirmware);
    CHECK(evidence.TargetId == 3u && evidence.BytesPerPixel == 4u);

    Bc250BuildValidInputs(&interfaceData, &apertures[0]);
    g_ownershipFlags.FrameBufferState = FrameBufferStateUnknown;
    CHECK(Bc250AcquirePostDisplayReservation(
        &interfaceData, apertures, 1, 0, 77, &reservation, &evidence,
        &callbackStatus) == BC250_POST_DISPLAY_FRAMEBUFFER_STATE_UNKNOWN);
    CHECK(reservation.Length == 0 && evidence.Length == 0);

    Bc250BuildValidInputs(&interfaceData, &apertures[0]);
    g_displayInfo.Pitch = 1024;
    CHECK(Bc250AcquirePostDisplayReservation(
        &interfaceData, apertures, 1, 0, 77, &reservation, &evidence,
        &callbackStatus) == BC250_POST_DISPLAY_GEOMETRY_REJECTED);

    Bc250BuildValidInputs(&interfaceData, &apertures[0]);
    g_displayInfo.PhysicAddress.QuadPart = 0xCFF00000ll;
    CHECK(Bc250AcquirePostDisplayReservation(
        &interfaceData, apertures, 1, 0, 77, &reservation, &evidence,
        &callbackStatus) == BC250_POST_DISPLAY_OUTSIDE_APERTURE);

    Bc250BuildValidInputs(&interfaceData, &apertures[0]);
    g_displayInfo.ColorFormat = D3DDDIFMT_UNKNOWN;
    CHECK(Bc250AcquirePostDisplayReservation(
        &interfaceData, apertures, 1, 0, 77, &reservation, &evidence,
        &callbackStatus) == BC250_POST_DISPLAY_FORMAT_UNSUPPORTED);

    Bc250BuildValidInputs(&interfaceData, &apertures[0]);
    apertures[1] = apertures[0];
    CHECK(Bc250AcquirePostDisplayReservation(
        &interfaceData, apertures, 2, 0, 77, &reservation, &evidence,
        &callbackStatus) == BC250_POST_DISPLAY_APERTURE_NOT_UNIQUE);
    CHECK(g_callback1Calls == 0u && g_callback2Calls == 0u);

    Bc250BuildValidInputs(&interfaceData, &apertures[0]);
    interfaceData.Size =
        FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership2);
    CHECK(Bc250AcquirePostDisplayReservation(
        &interfaceData, apertures, 1, 0, 77, &reservation, &evidence,
        &callbackStatus) == BC250_POST_DISPLAY_OK);
    CHECK(g_callback1Calls == 1u && g_callback2Calls == 0u);
    CHECK(evidence.UsedOwnershipCallback2 == 0u);

    Bc250BuildValidInputs(&interfaceData, &apertures[0]);
    g_callbackStatus = STATUS_DEVICE_CONFIGURATION_ERROR;
    CHECK(Bc250AcquirePostDisplayReservation(
        &interfaceData, apertures, 1, 0, 77, &reservation, &evidence,
        &callbackStatus) == BC250_POST_DISPLAY_CALLBACK_FAILED);
    CHECK(callbackStatus == STATUS_DEVICE_CONFIGURATION_ERROR);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunPostDisplayReservationTest() ? 0ul : 1ul);
}
