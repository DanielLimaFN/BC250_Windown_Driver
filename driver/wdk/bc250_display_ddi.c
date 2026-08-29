#include "bc250_display_ddi.h"
#include "bc250_probe_telemetry.h"

BC250_DISPLAY_STATE g_Bc250DisplayState = { 0 };

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static VOID Bc250VsyncDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    DXGKARGCB_NOTIFY_INTERRUPT_DATA data;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!g_Bc250DisplayState.VsyncEnabled ||
        g_Bc250DisplayState.DxgkInterface.DxgkCbNotifyInterrupt == NULL ||
        g_Bc250DisplayState.DxgkInterface.DxgkCbNotifyDpc == NULL) {
        return;
    }

    RtlZeroMemory(&data, sizeof(data));
    data.InterruptType = DXGK_INTERRUPT_DISPLAYONLY_VSYNC;
    data.DisplayOnlyVsync.VidPnTargetId = 0;

    g_Bc250DisplayState.DxgkInterface.DxgkCbNotifyInterrupt(
        g_Bc250DisplayState.DxgkInterface.DeviceHandle, &data);
    g_Bc250DisplayState.DxgkInterface.DxgkCbNotifyDpc(
        g_Bc250DisplayState.DxgkInterface.DeviceHandle);

    g_Bc250DisplayState.VsyncCount++;
}

static VOID Bc250SetVsyncState(BOOLEAN Enable)
{
    if (!g_Bc250DisplayState.VsyncTimerInitialized) {
        return;
    }

    if (Enable) {
        LARGE_INTEGER due;
        due.QuadPart = -166667LL; /* 16.66 ms = 60 Hz */
        g_Bc250DisplayState.VsyncEnabled = TRUE;
        KeSetTimerEx(&g_Bc250DisplayState.VsyncTimer, due, 17, &g_Bc250DisplayState.VsyncDpc);
    } else {
        g_Bc250DisplayState.VsyncEnabled = FALSE;
        KeCancelTimer(&g_Bc250DisplayState.VsyncTimer);
    }
}
#else
static VOID Bc250SetVsyncState(BOOLEAN Enable) { UNREFERENCED_PARAMETER(Enable); }
#endif

static void Bc250BuildEdid(void)
{
    UCHAR *e = g_Bc250DisplayState.Edid;
    ULONG hActive = g_Bc250DisplayState.CurrentMode.Width;
    ULONG vActive = g_Bc250DisplayState.CurrentMode.Height;
    ULONG hTotal, vTotal, clock10kHz, hSizeMm, vSizeMm;
    ULONG sum = 0, i;
    UCHAR *d;
    static CONST UCHAR stdTimings[16] = {
        0xD1, 0xC0,   /* 1920x1080 @60, 16:9  */
        0xA9, 0x40,   /* 1600x900  @60, 16:9  */
        0x95, 0x00,   /* 1440x900  @60, 16:10 */
        0x90, 0x40,   /* 1400x1050 @60, 4:3   */
        0x81, 0x80,   /* 1280x1024 @60, 5:4   */
        0x81, 0x40,   /* 1280x960  @60, 4:3   */
        0x81, 0x00,   /* 1280x800  @60, 16:10 */
        0x81, 0xC0    /* 1280x720  @60, 16:9  */
    };

    g_Bc250DisplayState.EdidValid = FALSE;
    RtlZeroMemory(e, 128);

    if (hActive == 0 || vActive == 0) return;

    hTotal = hActive + 160;
    vTotal = vActive + 45;
    clock10kHz = (ULONG)(((ULONGLONG)hTotal * vTotal * 60ULL + 5000ULL) / 10000ULL);
    if (clock10kHz > 0xFFFF) clock10kHz = 0xFFFF;

    hSizeMm = (hActive * 254u) / 960u;
    vSizeMm = (vActive * 254u) / 960u;
    if (hSizeMm > 4095u) hSizeMm = 4095u;
    if (vSizeMm > 4095u) vSizeMm = 4095u;

    /* Header */
    e[0] = 0x00;
    for (i = 1; i <= 6; i++) e[i] = 0xFF;
    e[7] = 0x00;

    /* Vendor "BCX" */
    e[8] = 0x08; e[9] = 0x78;
    /* Product 0x13FE */
    e[10] = 0xFE; e[11] = 0x13;
    /* Serial 1 */
    e[12] = 0x01; e[13] = 0x00; e[14] = 0x00; e[15] = 0x00;
    e[16] = 1; e[17] = 36; e[18] = 1; e[19] = 3;

    /* Basic params */
    e[20] = 0x80;
    e[21] = (UCHAR)(hSizeMm / 10u);
    e[22] = (UCHAR)(vSizeMm / 10u);
    e[23] = 120;
    e[24] = 0x02;

    /* Chromaticity */
    e[25] = 0xEE; e[26] = 0x91; e[27] = 0xA3; e[28] = 0x54; e[29] = 0x4C;
    e[30] = 0x99; e[31] = 0x26; e[32] = 0x0F; e[33] = 0x50; e[34] = 0x54;

    /* Established timings */
    e[35] = 0x21; e[36] = 0x08; e[37] = 0x00;

    /* Standard timings */
    RtlCopyMemory(&e[38], stdTimings, sizeof(stdTimings));

    /* Detailed timing descriptor 1: native mode */
    d = &e[54];
    d[0] = (UCHAR)(clock10kHz & 0xFF);
    d[1] = (UCHAR)((clock10kHz >> 8) & 0xFF);
    d[2] = (UCHAR)(hActive & 0xFF);
    d[3] = (UCHAR)(160 & 0xFF);
    d[4] = (UCHAR)((((hActive >> 8) & 0x0F) << 4) | ((160 >> 8) & 0x0F));
    d[5] = (UCHAR)(vActive & 0xFF);
    d[6] = (UCHAR)(45 & 0xFF);
    d[7] = (UCHAR)((((vActive >> 8) & 0x0F) << 4) | ((45 >> 8) & 0x0F));
    d[8] = 48; d[9] = 32; d[10] = (UCHAR)((3u << 4) | 5u); d[11] = 0;
    d[12] = (UCHAR)(hSizeMm & 0xFF);
    d[13] = (UCHAR)(vSizeMm & 0xFF);
    d[14] = (UCHAR)((((hSizeMm >> 8) & 0x0F) << 4) | ((vSizeMm >> 8) & 0x0F));
    d[15] = 0; d[16] = 0; d[17] = 0x1E;

    /* Detailed descriptor 2: range limits */
    d = &e[72];
    d[0] = 0; d[1] = 0; d[2] = 0; d[3] = 0xFD; d[4] = 0;
    d[5] = 50; d[6] = 61; d[7] = 20; d[8] = 90; d[9] = 19; d[10] = 0; d[11] = 0x0A;
    for (i = 12; i < 18; i++) d[i] = 0x20;

    /* Detailed descriptor 3: monitor name */
    d = &e[90];
    d[0] = 0; d[1] = 0; d[2] = 0; d[3] = 0xFC; d[4] = 0;
    d[5] = 'B'; d[6] = 'C'; d[7] = '-'; d[8] = '2'; d[9] = '5'; d[10] = '0';
    d[11] = ' '; d[12] = 'G'; d[13] = 'O'; d[14] = 'P'; d[15] = 0x0A; d[16] = 0x20; d[17] = 0x20;

    /* Detailed descriptor 4: dummy */
    d = &e[108];
    d[0] = 0; d[1] = 0; d[2] = 0; d[3] = 0x10; d[4] = 0;

    /* Checksum */
    e[126] = 0;
    for (i = 0; i < 127; i++) sum += e[i];
    e[127] = (UCHAR)((0x100u - (sum & 0xFFu)) & 0xFFu);

    g_Bc250DisplayState.EdidValid = TRUE;
}

void Bc250DisplayInit(
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _In_ const DXGK_DISPLAY_INFORMATION *PostDisplayInfo)
{
    RtlZeroMemory(&g_Bc250DisplayState, sizeof(g_Bc250DisplayState));
    if (DxgkInterface != NULL) {
        RtlCopyMemory(&g_Bc250DisplayState.DxgkInterface, DxgkInterface, sizeof(DXGKRNL_INTERFACE));
    }
    if (PostDisplayInfo != NULL) {
        RtlCopyMemory(&g_Bc250DisplayState.PostDisplayInfo, PostDisplayInfo, sizeof(DXGK_DISPLAY_INFORMATION));
        g_Bc250DisplayState.DisplayAcquired = 1;
        g_Bc250DisplayState.FrameBufferPhysical = PostDisplayInfo->PhysicAddress;
        g_Bc250DisplayState.CurrentMode.Width = PostDisplayInfo->Width ? PostDisplayInfo->Width : 1920;
        g_Bc250DisplayState.CurrentMode.Height = PostDisplayInfo->Height ? PostDisplayInfo->Height : 1080;
        g_Bc250DisplayState.CurrentMode.Pitch = PostDisplayInfo->Pitch ? PostDisplayInfo->Pitch : (g_Bc250DisplayState.CurrentMode.Width * 4);
    } else {
        g_Bc250DisplayState.CurrentMode.Width = 1920;
        g_Bc250DisplayState.CurrentMode.Height = 1080;
        g_Bc250DisplayState.CurrentMode.Pitch = 1920 * 4;
    }
    g_Bc250DisplayState.CurrentMode.Format = D3DDDIFMT_A8R8G8B8;
    g_Bc250DisplayState.CurrentMode.RefreshRateNumerator = 60000;
    g_Bc250DisplayState.CurrentMode.RefreshRateDenominator = 1000;
    g_Bc250DisplayState.ModeCount = 1;
    g_Bc250DisplayState.SourceVisible = 1;

    Bc250BuildEdid();

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeInitializeTimer(&g_Bc250DisplayState.VsyncTimer);
    KeInitializeDpc(&g_Bc250DisplayState.VsyncDpc, Bc250VsyncDpcRoutine, NULL);
    g_Bc250DisplayState.VsyncTimerInitialized = TRUE;

    if (g_Bc250DisplayState.FrameBufferPhysical.QuadPart != 0) {
        ULONG fbBytes = g_Bc250DisplayState.CurrentMode.Pitch * g_Bc250DisplayState.CurrentMode.Height;
        g_Bc250DisplayState.FrameBufferSize = fbBytes;
        g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
            g_Bc250DisplayState.FrameBufferPhysical,
            fbBytes,
            PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
                g_Bc250DisplayState.FrameBufferPhysical,
                fbBytes,
                PAGE_READWRITE | PAGE_NOCACHE);
        }
        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpace(
                g_Bc250DisplayState.FrameBufferPhysical,
                fbBytes,
                MmWriteCombined);
        }
    }
    Bc250ProbeRecord(
        BC250_PROBE_DMM_DISPLAY_MAPPED,
        (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1 : 0);
#endif
}

void Bc250DisplayTeardown(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    Bc250SetVsyncState(FALSE);
    if (g_Bc250DisplayState.VsyncTimerInitialized) {
        KeCancelTimer(&g_Bc250DisplayState.VsyncTimer);
        KeFlushQueuedDpcs();
    }
    if (g_Bc250DisplayState.FrameBufferVirtual != NULL) {
        MmUnmapIoSpace(g_Bc250DisplayState.FrameBufferVirtual, g_Bc250DisplayState.FrameBufferSize);
        g_Bc250DisplayState.FrameBufferVirtual = NULL;
        g_Bc250DisplayState.FrameBufferSize = 0;
    }
#endif
}

static NTSTATUS Bc250AddSingleSourceMode(
    _In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pSourceModeSetInterface,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    D3DKMDT_VIDPN_SOURCE_MODE *pSourceModeInfo = NULL;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(SourceId);

    if (pSourceModeSetInterface == NULL || hSourceModeSet == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    status = pSourceModeSetInterface->pfnCreateNewModeInfo(
        hSourceModeSet, &pSourceModeInfo);
    if (!NT_SUCCESS(status) || pSourceModeInfo == NULL) {
        return status;
    }

    pSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
    pSourceModeInfo->Format.Graphics.VisibleRegionSize.cx = g_Bc250DisplayState.CurrentMode.Width;
    pSourceModeInfo->Format.Graphics.VisibleRegionSize.cy = g_Bc250DisplayState.CurrentMode.Height;
    pSourceModeInfo->Format.Graphics.PrimSurfSize = pSourceModeInfo->Format.Graphics.VisibleRegionSize;
    pSourceModeInfo->Format.Graphics.Stride = g_Bc250DisplayState.CurrentMode.Pitch;
    pSourceModeInfo->Format.Graphics.PixelFormat = g_Bc250DisplayState.CurrentMode.Format;
    pSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
    pSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

    status = pSourceModeSetInterface->pfnAddMode(hSourceModeSet, pSourceModeInfo);
    if (!NT_SUCCESS(status)) {
        (void)pSourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, pSourceModeInfo);
        return status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS Bc250AddSingleTargetMode(
    _In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pTargetModeSetInterface,
    _In_ D3DKMDT_HVIDPNTARGETMODESET hTargetModeSet,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    D3DKMDT_VIDPN_TARGET_MODE *pTargetModeInfo = NULL;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(TargetId);

    if (pTargetModeSetInterface == NULL || hTargetModeSet == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    status = pTargetModeSetInterface->pfnCreateNewModeInfo(
        hTargetModeSet, &pTargetModeInfo);
    if (!NT_SUCCESS(status) || pTargetModeInfo == NULL) {
        return status;
    }

    pTargetModeInfo->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    pTargetModeInfo->VideoSignalInfo.TotalSize.cx = g_Bc250DisplayState.CurrentMode.Width;
    pTargetModeInfo->VideoSignalInfo.TotalSize.cy = g_Bc250DisplayState.CurrentMode.Height;
    pTargetModeInfo->VideoSignalInfo.ActiveSize.cx = g_Bc250DisplayState.CurrentMode.Width;
    pTargetModeInfo->VideoSignalInfo.ActiveSize.cy = g_Bc250DisplayState.CurrentMode.Height;
    pTargetModeInfo->VideoSignalInfo.VSyncFreq.Numerator = g_Bc250DisplayState.CurrentMode.RefreshRateNumerator;
    pTargetModeInfo->VideoSignalInfo.VSyncFreq.Denominator = g_Bc250DisplayState.CurrentMode.RefreshRateDenominator;
    pTargetModeInfo->VideoSignalInfo.HSyncFreq.Numerator = 67500;
    pTargetModeInfo->VideoSignalInfo.HSyncFreq.Denominator = 1000;
    pTargetModeInfo->VideoSignalInfo.PixelRate = 148500000;
    pTargetModeInfo->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    pTargetModeInfo->Preference = D3DKMDT_MP_PREFERRED;

    status = pTargetModeSetInterface->pfnAddMode(hTargetModeSet, pTargetModeInfo);
    if (!NT_SUCCESS(status)) {
        (void)pTargetModeSetInterface->pfnReleaseModeInfo(hTargetModeSet, pTargetModeInfo);
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryChildRelations(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    Bc250ProbeRecord(BC250_PROBE_DMM_CHILD_RELATIONS_CALLED, ChildRelationsSize);

    if (ChildRelations == NULL || ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR)) {
        Bc250ProbeRecordFirstFailure(STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(ChildRelations, ChildRelationsSize);

    ChildRelations[0].ChildDeviceType = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_OTHER;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
    ChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    ChildRelations[0].AcpiUid = 0;
    ChildRelations[0].ChildUid = 0;

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryChildStatus(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_STATUS ChildStatus,
    BOOLEAN NonDestructiveOnly)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (ChildStatus == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250ProbeRecord(BC250_PROBE_DMM_CHILD_STATUS_CALLED, (ULONG)ChildStatus->Type);

    if (ChildStatus->Type == StatusConnection) {
        ChildStatus->HotPlug.Connected = TRUE;
        return STATUS_SUCCESS;
    }

    if (ChildStatus->Type == StatusRotation) {
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryDeviceDescriptor(
    CONST PVOID MiniportDeviceContext,
    ULONG ChildUid,
    PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    ULONG available, copy;
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    Bc250ProbeRecord(BC250_PROBE_DMM_DESCRIPTOR_CALLED, ChildUid);

    if (DeviceDescriptor == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (ChildUid != 0 || !g_Bc250DisplayState.EdidValid) {
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }

    if (DeviceDescriptor->DescriptorOffset >= 128 ||
        DeviceDescriptor->DescriptorBuffer == NULL ||
        DeviceDescriptor->DescriptorLength == 0) {
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }

    available = 128 - DeviceDescriptor->DescriptorOffset;
    copy = (DeviceDescriptor->DescriptorLength < available)
         ? DeviceDescriptor->DescriptorLength : available;

    RtlCopyMemory(DeviceDescriptor->DescriptorBuffer,
                  &g_Bc250DisplayState.Edid[DeviceDescriptor->DescriptorOffset],
                  copy);
    DeviceDescriptor->DescriptorLength = copy;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiGetChildContainerId(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_CHILD_CONTAINER_ID ContainerId)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(ContainerId);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryInterface(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(QueryInterface);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryAdapterInfoDisplay(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pQueryAdapterInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250ProbeRecord(BC250_PROBE_DMM_QUERY_ADAPTER_TYPE, (ULONG)pQueryAdapterInfo->Type);
    Bc250ProbeRecord(BC250_PROBE_DMM_QAI_OUTSIZE, pQueryAdapterInfo->OutputDataSize);

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_DRIVERCAPS) {
        DXGK_DRIVERCAPS *caps;
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < 32) {
            Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_BUFFER_TOO_SMALL);
            return STATUS_BUFFER_TOO_SMALL;
        }
        caps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, pQueryAdapterInfo->OutputDataSize);

        caps->HighestAcceptableAddress.QuadPart = -1;
        caps->WDDMVersion = DXGKDDI_WDDMv1_2;
        caps->SupportNonVGA = TRUE;
        caps->SupportSmoothRotation = FALSE;
        Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, 0);
        return STATUS_SUCCESS;
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_PHYSICALADAPTERCAPS) {
        DXGK_PHYSICALADAPTERCAPS *physCaps;
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < 16) {
            Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_BUFFER_TOO_SMALL);
            return STATUS_BUFFER_TOO_SMALL;
        }
        physCaps = (DXGK_PHYSICALADAPTERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(physCaps, pQueryAdapterInfo->OutputDataSize);
        physCaps->NumExecutionNodes = 0;
        physCaps->PagingNodeIndex = 0;
        physCaps->Flags.Value = 0;
        physCaps->DxgkPhysicalAdapterHandle = (HANDLE)hAdapter;
        Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, 0);
        return STATUS_SUCCESS;
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_NUMPOWERCOMPONENTS) {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_BUFFER_TOO_SMALL);
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0;
        Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, 0);
        return STATUS_SUCCESS;
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_PREFERREDGPUNODE) {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_BUFFER_TOO_SMALL);
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0;
        Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, 0);
        return STATUS_SUCCESS;
    }

    Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetPowerState(
    CONST PVOID MiniportDeviceContext,
    ULONG HardwareUid,
    DEVICE_POWER_STATE DevicePowerState,
    POWER_ACTION ActionType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(HardwareUid);
    UNREFERENCED_PARAMETER(ActionType);

    if (DevicePowerState == PowerDeviceD0 ||
        DevicePowerState == PowerDeviceD1 ||
        DevicePowerState == PowerDeviceD2 ||
        DevicePowerState == PowerDeviceD3) {
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS APIENTRY Bc250DxgkDdiNotifyAcpiEvent(
    CONST PVOID MiniportDeviceContext,
    DXGK_EVENT_TYPE EventType,
    ULONG Event,
    PVOID Argument,
    PULONG AcpiFlags)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(Argument);
    if (AcpiFlags != NULL) {
        *AcpiFlags = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetPalette(
    CONST PVOID MiniportDeviceContext,
    CONST DXGKARG_SETPALETTE *pSetPalette)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pSetPalette);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiGetScanLine(
    CONST PVOID MiniportDeviceContext,
    DXGKARG_GETSCANLINE *pGetScanLine)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    if (pGetScanLine == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    pGetScanLine->InVerticalBlank = TRUE;
    pGetScanLine->ScanLine = 0;
    return STATUS_SUCCESS;
}

VOID APIENTRY Bc250DxgkDdiControlEtwLogging(
    BOOLEAN Enable,
    ULONG Flags,
    UCHAR Level)
{
    UNREFERENCED_PARAMETER(Enable);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Level);
}

NTSTATUS APIENTRY Bc250DxgkDdiCollectDbgInfo(
    CONST PVOID MiniportDeviceContext,
    CONST DXGKARG_COLLECTDBGINFO *pCollectDbgInfo)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pCollectDbgInfo);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiNotifySurpriseRemoval(
    PVOID MiniportDeviceContext,
    DXGK_SURPRISE_REMOVAL_TYPE RemovalType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(RemovalType);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiIsSupportedVidPn(
    CONST HANDLE hAdapter,
    DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    Bc250ProbeRecord(BC250_PROBE_DMM_VIDPN_QUERY_CALLED, 1);
    if (pIsSupportedVidPn == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (pIsSupportedVidPn->hDesiredVidPn == 0) {
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    pIsSupportedVidPn->IsVidPnSupported = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiRecommendFunctionalVidPn(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendFunctionalVidPn);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS APIENTRY Bc250DxgkDdiEnumVidPnCofuncModality(
    CONST HANDLE hAdapter,
    CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *pEnumCofuncModality)
{
    CONST DXGK_VIDPN_INTERFACE *pVidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *pVidPnTopologyInterface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface = NULL;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology = 0;
    D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet = 0;
    D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet = 0;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPath = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *pPinnedSourceMode = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *pPinnedTargetMode = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(hAdapter);

    if (pEnumCofuncModality == NULL || g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface(
        pEnumCofuncModality->hConstrainingVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1,
        &pVidPnInterface);
    if (!NT_SUCCESS(status) || pVidPnInterface == NULL) {
        return status;
    }

    status = pVidPnInterface->pfnGetTopology(
        pEnumCofuncModality->hConstrainingVidPn,
        &hVidPnTopology,
        &pVidPnTopologyInterface);
    if (!NT_SUCCESS(status) || pVidPnTopologyInterface == NULL) {
        return status;
    }

    status = pVidPnTopologyInterface->pfnAcquireFirstPathInfo(
        hVidPnTopology, &pVidPnPresentPath);
    if (!NT_SUCCESS(status) || pVidPnPresentPath == NULL) {
        return status;
    }

    while (status != STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET && pVidPnPresentPath != NULL) {
        /* Populate Source Mode Set if not pinned */
        status = pVidPnInterface->pfnAcquireSourceModeSet(
            pEnumCofuncModality->hConstrainingVidPn,
            pVidPnPresentPath->VidPnSourceId,
            &hVidPnSourceModeSet,
            &pVidPnSourceModeSetInterface);
        if (NT_SUCCESS(status) && pVidPnSourceModeSetInterface != NULL) {
            pPinnedSourceMode = NULL;
            (void)pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(
                hVidPnSourceModeSet, &pPinnedSourceMode);
            if (pPinnedSourceMode == NULL) {
                (void)pVidPnInterface->pfnReleaseSourceModeSet(
                    pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet);
                hVidPnSourceModeSet = 0;

                status = pVidPnInterface->pfnCreateNewSourceModeSet(
                    pEnumCofuncModality->hConstrainingVidPn,
                    pVidPnPresentPath->VidPnSourceId,
                    &hVidPnSourceModeSet,
                    &pVidPnSourceModeSetInterface);
                if (NT_SUCCESS(status) && pVidPnSourceModeSetInterface != NULL) {
                    (void)Bc250AddSingleSourceMode(
                        pVidPnSourceModeSetInterface,
                        hVidPnSourceModeSet,
                        pVidPnPresentPath->VidPnSourceId);
                    (void)pVidPnInterface->pfnAssignSourceModeSet(
                        pEnumCofuncModality->hConstrainingVidPn,
                        pVidPnPresentPath->VidPnSourceId,
                        hVidPnSourceModeSet);
                }
            } else {
                (void)pVidPnSourceModeSetInterface->pfnReleaseModeInfo(
                    hVidPnSourceModeSet, pPinnedSourceMode);
                (void)pVidPnInterface->pfnReleaseSourceModeSet(
                    pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet);
            }
        }

        /* Populate Target Mode Set if not pinned */
        status = pVidPnInterface->pfnAcquireTargetModeSet(
            pEnumCofuncModality->hConstrainingVidPn,
            pVidPnPresentPath->VidPnTargetId,
            &hVidPnTargetModeSet,
            &pVidPnTargetModeSetInterface);
        if (NT_SUCCESS(status) && pVidPnTargetModeSetInterface != NULL) {
            pPinnedTargetMode = NULL;
            (void)pVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(
                hVidPnTargetModeSet, &pPinnedTargetMode);
            if (pPinnedTargetMode == NULL) {
                (void)pVidPnInterface->pfnReleaseTargetModeSet(
                    pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet);
                hVidPnTargetModeSet = 0;

                status = pVidPnInterface->pfnCreateNewTargetModeSet(
                    pEnumCofuncModality->hConstrainingVidPn,
                    pVidPnPresentPath->VidPnTargetId,
                    &hVidPnTargetModeSet,
                    &pVidPnTargetModeSetInterface);
                if (NT_SUCCESS(status) && pVidPnTargetModeSetInterface != NULL) {
                    (void)Bc250AddSingleTargetMode(
                        pVidPnTargetModeSetInterface,
                        hVidPnTargetModeSet,
                        pVidPnPresentPath->VidPnTargetId);
                    (void)pVidPnInterface->pfnAssignTargetModeSet(
                        pEnumCofuncModality->hConstrainingVidPn,
                        pVidPnPresentPath->VidPnTargetId,
                        hVidPnTargetModeSet);
                }
            } else {
                (void)pVidPnTargetModeSetInterface->pfnReleaseModeInfo(
                    hVidPnTargetModeSet, pPinnedTargetMode);
                (void)pVidPnInterface->pfnReleaseTargetModeSet(
                    pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet);
            }
        }

        /* Acquire next path */
        {
            CONST D3DKMDT_VIDPN_PRESENT_PATH *pNextPath = NULL;
            status = pVidPnTopologyInterface->pfnAcquireNextPathInfo(
                hVidPnTopology, pVidPnPresentPath, &pNextPath);
            (void)pVidPnTopologyInterface->pfnReleasePathInfo(
                hVidPnTopology, pVidPnPresentPath);
            pVidPnPresentPath = pNextPath;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceAddress(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddress);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceVisibility(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetVidPnSourceVisibility != NULL) {
        g_Bc250DisplayState.SourceVisible = pSetVidPnSourceVisibility->Visible ? 1 : 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiCommitVidPn(
    CONST HANDLE hAdapter,
    CONST DXGKARG_COMMITVIDPN *pCommitVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pCommitVidPn == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pCommitVidPn->Flags.PathPoweredOff) {
        return STATUS_SUCCESS;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiUpdateActiveVidPnPresentPath(
    CONST HANDLE hAdapter,
    CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pUpdateActiveVidPnPresentPath);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiRecommendMonitorModes(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes)
{
    D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSourceMode = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(hAdapter);

    if (pRecommendMonitorModes == NULL ||
        pRecommendMonitorModes->pMonitorSourceModeSetInterface == NULL ||
        pRecommendMonitorModes->hMonitorSourceModeSet == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(
        pRecommendMonitorModes->hMonitorSourceModeSet,
        &pMonitorSourceMode);
    if (!NT_SUCCESS(status) || pMonitorSourceMode == NULL) {
        return status;
    }

    pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = g_Bc250DisplayState.CurrentMode.Width;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = g_Bc250DisplayState.CurrentMode.Height;
    pMonitorSourceMode->VideoSignalInfo.ActiveSize.cx = g_Bc250DisplayState.CurrentMode.Width;
    pMonitorSourceMode->VideoSignalInfo.ActiveSize.cy = g_Bc250DisplayState.CurrentMode.Height;
    pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Numerator = g_Bc250DisplayState.CurrentMode.RefreshRateNumerator;
    pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Denominator = g_Bc250DisplayState.CurrentMode.RefreshRateDenominator;
    pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Numerator = 67500;
    pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Denominator = 1000;
    pMonitorSourceMode->VideoSignalInfo.PixelRate = 148500000;
    pMonitorSourceMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = D3DKMDT_MP_PREFERRED;

    status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(
        pRecommendMonitorModes->hMonitorSourceModeSet,
        pMonitorSourceMode);
    if (!NT_SUCCESS(status)) {
        (void)pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(
            pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiRecommendVidPnTopology(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *pRecommendVidPnTopology)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendVidPnTopology);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS APIENTRY Bc250DxgkDdiPresent(
    CONST HANDLE hContext,
    DXGKARG_PRESENT *pPresent)
{
    UNREFERENCED_PARAMETER(hContext);
    if (pPresent == NULL) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiControlInterrupt(
    CONST HANDLE hAdapter,
    CONST DXGK_INTERRUPT_TYPE InterruptType,
    BOOLEAN EnableInterrupt)
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (InterruptType == DXGK_INTERRUPT_DISPLAYONLY_VSYNC) {
        Bc250SetVsyncState(EnableInterrupt);
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiPresentDisplayOnly(
    CONST PVOID MiniportDeviceContext,
    CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly)
{
    ULONG i;
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (pPresentDisplayOnly == NULL || pPresentDisplayOnly->pSource == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
        return STATUS_SUCCESS;
    }

    if (pPresentDisplayOnly->NumDirtyRects > 0 && pPresentDisplayOnly->pDirtyRect != NULL) {
        for (i = 0; i < pPresentDisplayOnly->NumDirtyRects; i++) {
            CONST RECT *r = &pPresentDisplayOnly->pDirtyRect[i];
            LONG left = (r->left < 0) ? 0 : r->left;
            LONG top = (r->top < 0) ? 0 : r->top;
            LONG right = (r->right > (LONG)g_Bc250DisplayState.CurrentMode.Width) ? (LONG)g_Bc250DisplayState.CurrentMode.Width : r->right;
            LONG bottom = (r->bottom > (LONG)g_Bc250DisplayState.CurrentMode.Height) ? (LONG)g_Bc250DisplayState.CurrentMode.Height : r->bottom;
            LONG y;

            if (right <= left || bottom <= top) continue;

            for (y = top; y < bottom; y++) {
                PUCHAR dst = (PUCHAR)g_Bc250DisplayState.FrameBufferVirtual +
                    ((SIZE_T)y * g_Bc250DisplayState.CurrentMode.Pitch) +
                    ((SIZE_T)left * 4);
                CONST UCHAR *src = (CONST UCHAR *)pPresentDisplayOnly->pSource +
                    ((SIZE_T)y * pPresentDisplayOnly->Pitch) +
                    ((SIZE_T)left * 4);
                RtlCopyMemory(dst, src, (SIZE_T)(right - left) * 4);
            }
        }
    } else {
        LONG y;
        for (y = 0; y < (LONG)g_Bc250DisplayState.CurrentMode.Height; y++) {
            PUCHAR dst = (PUCHAR)g_Bc250DisplayState.FrameBufferVirtual +
                ((SIZE_T)y * g_Bc250DisplayState.CurrentMode.Pitch);
            CONST UCHAR *src = (CONST UCHAR *)pPresentDisplayOnly->pSource +
                ((SIZE_T)y * pPresentDisplayOnly->Pitch);
            RtlCopyMemory(dst, src, (SIZE_T)g_Bc250DisplayState.CurrentMode.Width * 4);
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceAndReleasePostDisplayOwnership(
    CONST PVOID MiniportDeviceContext,
    CONST D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGK_DISPLAY_INFORMATION DisplayInfo)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);

    if (DisplayInfo == NULL) return STATUS_INVALID_PARAMETER;

    if (g_Bc250DisplayState.DisplayAcquired != 0u) {
        RtlCopyMemory(DisplayInfo, &g_Bc250DisplayState.PostDisplayInfo, sizeof(DXGK_DISPLAY_INFORMATION));
        DisplayInfo->TargetId = 0;
    } else {
        DisplayInfo->Width = g_Bc250DisplayState.CurrentMode.Width;
        DisplayInfo->Height = g_Bc250DisplayState.CurrentMode.Height;
        DisplayInfo->Pitch = g_Bc250DisplayState.CurrentMode.Pitch;
        DisplayInfo->ColorFormat = D3DDDIFMT_A8R8G8B8;
        DisplayInfo->PhysicAddress = g_Bc250DisplayState.FrameBufferPhysical;
        DisplayInfo->TargetId = 0;
        DisplayInfo->AcpiId = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSystemDisplayEnable(
    CONST PVOID MiniportDeviceContext,
    CONST D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    UINT *Width,
    UINT *Height,
    D3DDDIFORMAT *ColorFormat)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);

    if (Width == NULL || Height == NULL || ColorFormat == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *Width = g_Bc250DisplayState.CurrentMode.Width;
    *Height = g_Bc250DisplayState.CurrentMode.Height;
    *ColorFormat = D3DDDIFMT_A8R8G8B8;
    return STATUS_SUCCESS;
}

VOID APIENTRY Bc250DxgkDdiSystemDisplayWrite(
    CONST PVOID MiniportDeviceContext,
    PVOID Source,
    UINT SourceWidth,
    UINT SourceHeight,
    UINT SourceStride,
    UINT PositionX,
    UINT PositionY)
{
    UINT y;
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (Source == NULL || g_Bc250DisplayState.FrameBufferVirtual == NULL) return;

    for (y = 0; y < SourceHeight; y++) {
        if ((PositionY + y) >= g_Bc250DisplayState.CurrentMode.Height) break;
        {
            PUCHAR dst = (PUCHAR)g_Bc250DisplayState.FrameBufferVirtual +
                ((SIZE_T)(PositionY + y) * g_Bc250DisplayState.CurrentMode.Pitch) +
                ((SIZE_T)PositionX * 4);
            CONST UCHAR *src = (CONST UCHAR *)Source + ((SIZE_T)y * SourceStride);
            UINT copyWidth = SourceWidth;
            if ((PositionX + copyWidth) > g_Bc250DisplayState.CurrentMode.Width) {
                copyWidth = g_Bc250DisplayState.CurrentMode.Width - PositionX;
            }
            RtlCopyMemory(dst, src, (SIZE_T)copyWidth * 4);
        }
    }
}

NTSTATUS APIENTRY Bc250DxgkDdiSetPointerPosition(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPointerPosition);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetPointerShape(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPointerShape);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryVidPnHWCapability(
    CONST HANDLE hAdapter,
    DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pVidPnHWCaps == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&pVidPnHWCaps->VidPnHWCaps, sizeof(pVidPnHWCaps->VidPnHWCaps));
    return STATUS_SUCCESS;
}

NTSTATUS Bc250DisplayPopulateCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;

    InitializationData->DxgkDdiQueryChildRelations = Bc250DxgkDdiQueryChildRelations;
    InitializationData->DxgkDdiQueryChildStatus = Bc250DxgkDdiQueryChildStatus;
    InitializationData->DxgkDdiQueryDeviceDescriptor = Bc250DxgkDdiQueryDeviceDescriptor;
    InitializationData->DxgkDdiIsSupportedVidPn = Bc250DxgkDdiIsSupportedVidPn;
    InitializationData->DxgkDdiRecommendFunctionalVidPn = Bc250DxgkDdiRecommendFunctionalVidPn;
    InitializationData->DxgkDdiEnumVidPnCofuncModality = Bc250DxgkDdiEnumVidPnCofuncModality;
    InitializationData->DxgkDdiSetVidPnSourceAddress = Bc250DxgkDdiSetVidPnSourceAddress;
    InitializationData->DxgkDdiSetVidPnSourceVisibility = Bc250DxgkDdiSetVidPnSourceVisibility;
    InitializationData->DxgkDdiCommitVidPn = Bc250DxgkDdiCommitVidPn;
    InitializationData->DxgkDdiUpdateActiveVidPnPresentPath = Bc250DxgkDdiUpdateActiveVidPnPresentPath;
    InitializationData->DxgkDdiRecommendMonitorModes = Bc250DxgkDdiRecommendMonitorModes;
    InitializationData->DxgkDdiRecommendVidPnTopology = Bc250DxgkDdiRecommendVidPnTopology;
    InitializationData->DxgkDdiPresent = Bc250DxgkDdiPresent;
    InitializationData->DxgkDdiControlInterrupt = Bc250DxgkDdiControlInterrupt;
    InitializationData->DxgkDdiStopDeviceAndReleasePostDisplayOwnership = Bc250DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    InitializationData->DxgkDdiSystemDisplayEnable = Bc250DxgkDdiSystemDisplayEnable;
    InitializationData->DxgkDdiSystemDisplayWrite = Bc250DxgkDdiSystemDisplayWrite;
    InitializationData->DxgkDdiSetPointerPosition = Bc250DxgkDdiSetPointerPosition;
    InitializationData->DxgkDdiSetPointerShape = Bc250DxgkDdiSetPointerShape;
    InitializationData->DxgkDdiQueryVidPnHWCapability = Bc250DxgkDdiQueryVidPnHWCapability;
    InitializationData->DxgkDdiSetPowerState = Bc250DxgkDdiSetPowerState;

    return STATUS_SUCCESS;
}
