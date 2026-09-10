#include "../wdk/bc250_display_ddi.h"
#include "../wdk/bc250_active_miniport.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    volatile unsigned char *destination = (volatile unsigned char *)Destination;
    const volatile unsigned char *source = (const volatile unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)

void Bc250ProbeRecord(int Field, unsigned long Value)
{
    (void)Field;
    (void)Value;
}

/* Fase V5: rastro de DDI. Instrumentacao pura; fora do kernel e' no-op. */
void Bc250ProbeMarkDdi(unsigned long DdiId)
{
    (void)DdiId;
}

/* Schema 130: marco pos-StartDevice. Tambem instrumentacao pura. */
void Bc250ProbeMarkPostStartStage(unsigned long Stage)
{
    (void)Stage;
}

/*
 * Schema 130: captura integral das interfaces pedidas por
 * DxgkDdiQueryInterface. O tipo real e' BC250_V5_QI_RECORD, mas este arquivo
 * nao inclui bc250_probe_telemetry.h; void* basta para o stub e mantem o
 * teste de VidPn independente do modulo de telemetria.
 */
void Bc250ProbeRecordQueryInterface(const void *Record)
{
    (void)Record;
}

void Bc250ProbeRecordFirstFailure(long Status)
{
    (void)Status;
}

static BOOLEAN Bc250FakeResolveGpuVa(
    HANDLE Context,
    UINT64 Va,
    UINT64 *PhysicalAddress,
    UINT32 *SegmentId)
{
    if (Context != (HANDLE)0x1234 || Va != 0xAC0000ull ||
        PhysicalAddress == NULL || SegmentId == NULL) {
        return FALSE;
    }
    *PhysicalAddress = 0xC10000ull;
    *SegmentId = 1u;
    return TRUE;
}

typedef struct BC250_FAKE_VIDPN {
    SIZE_T PathCount;
    NTSTATUS SourcePinnedStatus;
    NTSTATUS TargetPinnedStatus;
    D3DKMDT_VIDPN_PRESENT_PATH Path;
    D3DKMDT_VIDPN_SOURCE_MODE SourceMode;
    D3DKMDT_VIDPN_TARGET_MODE TargetMode;
    D3DKMDT_VIDPN_SOURCE_MODE CreatedSourceMode;
    D3DKMDT_VIDPN_TARGET_MODE CreatedTargetMode;
    ULONG PathReleases;
    ULONG SourceModeReleases;
    ULONG TargetModeReleases;
    ULONG SourceSetReleases;
    ULONG TargetSetReleases;
    NTSTATUS UpdatePathStatus;
    ULONG PathSupportUpdates;
    D3DKMDT_VIDPN_PRESENT_PATH LastSupportedPath;
    NTSTATUS AssignSourceStatus;
    NTSTATUS AssignTargetStatus;
    ULONG SourceSetCreates;
    ULONG TargetSetCreates;
    ULONG SourceModeAdds;
    ULONG TargetModeAdds;
    ULONG SourceSetAssignments;
    ULONG TargetSetAssignments;
} BC250_FAKE_VIDPN;

static BC250_FAKE_VIDPN g_FakeVidPn;
static DXGK_VIDPN_INTERFACE g_FakeVidPnInterface;
static DXGK_VIDPNTOPOLOGY_INTERFACE g_FakeTopologyInterface;
static DXGK_VIDPNSOURCEMODESET_INTERFACE g_FakeSourceInterface;
static DXGK_VIDPNTARGETMODESET_INTERFACE g_FakeTargetInterface;

#define BC250_FAKE_VIDPN_HANDLE ((D3DKMDT_HVIDPN)(ULONG_PTR)1)
#define BC250_FAKE_TOPOLOGY_HANDLE ((D3DKMDT_HVIDPNTOPOLOGY)(ULONG_PTR)2)
#define BC250_FAKE_SOURCE_SET_HANDLE ((D3DKMDT_HVIDPNSOURCEMODESET)(ULONG_PTR)3)
#define BC250_FAKE_TARGET_SET_HANDLE ((D3DKMDT_HVIDPNTARGETMODESET)(ULONG_PTR)4)

static NTSTATUS APIENTRY CALLBACK Bc250FakeQueryVidPnInterface(
    D3DKMDT_HVIDPN VidPn,
    DXGK_VIDPN_INTERFACE_VERSION Version,
    CONST DXGK_VIDPN_INTERFACE **Interface)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE ||
        Version != DXGK_VIDPN_INTERFACE_VERSION_V1 || Interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Interface = &g_FakeVidPnInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeGetTopology(
    D3DKMDT_HVIDPN VidPn,
    D3DKMDT_HVIDPNTOPOLOGY *Topology,
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE **Interface)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || Topology == NULL || Interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Topology = BC250_FAKE_TOPOLOGY_HANDLE;
    *Interface = &g_FakeTopologyInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeGetNumPaths(
    D3DKMDT_HVIDPNTOPOLOGY Topology,
    SIZE_T *PathCount)
{
    if (Topology != BC250_FAKE_TOPOLOGY_HANDLE || PathCount == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *PathCount = g_FakeVidPn.PathCount;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeAcquireFirstPath(
    D3DKMDT_HVIDPNTOPOLOGY Topology,
    CONST D3DKMDT_VIDPN_PRESENT_PATH **Path)
{
    if (Topology != BC250_FAKE_TOPOLOGY_HANDLE || Path == NULL ||
        g_FakeVidPn.PathCount == 0) {
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    *Path = &g_FakeVidPn.Path;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeReleasePath(
    D3DKMDT_HVIDPNTOPOLOGY Topology,
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path)
{
    if (Topology != BC250_FAKE_TOPOLOGY_HANDLE || Path != &g_FakeVidPn.Path) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.PathReleases;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeUpdatePathSupport(
    D3DKMDT_HVIDPNTOPOLOGY Topology,
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path)
{
    if (Topology != BC250_FAKE_TOPOLOGY_HANDLE || Path == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.PathSupportUpdates;
    g_FakeVidPn.LastSupportedPath = *Path;
    return g_FakeVidPn.UpdatePathStatus;
}

static NTSTATUS APIENTRY Bc250FakeAcquireSourceSet(
    D3DKMDT_HVIDPN VidPn,
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    D3DKMDT_HVIDPNSOURCEMODESET *ModeSet,
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE **Interface)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || SourceId != 0 ||
        ModeSet == NULL || Interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *ModeSet = BC250_FAKE_SOURCE_SET_HANDLE;
    *Interface = &g_FakeSourceInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeReleaseSourceSet(
    D3DKMDT_HVIDPN VidPn,
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || ModeSet != BC250_FAKE_SOURCE_SET_HANDLE) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.SourceSetReleases;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeCreateSourceSet(
    D3DKMDT_HVIDPN VidPn,
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    D3DKMDT_HVIDPNSOURCEMODESET *ModeSet,
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE **Interface)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || SourceId != 0 ||
        ModeSet == NULL || Interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.SourceSetCreates;
    *ModeSet = BC250_FAKE_SOURCE_SET_HANDLE;
    *Interface = &g_FakeSourceInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeAssignSourceSet(
    D3DKMDT_HVIDPN VidPn,
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || SourceId != 0 ||
        ModeSet != BC250_FAKE_SOURCE_SET_HANDLE) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.SourceSetAssignments;
    return g_FakeVidPn.AssignSourceStatus;
}

static NTSTATUS APIENTRY Bc250FakeAcquirePinnedSourceMode(
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet,
    CONST D3DKMDT_VIDPN_SOURCE_MODE **Mode)
{
    if (ModeSet != BC250_FAKE_SOURCE_SET_HANDLE || Mode == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Mode = (g_FakeVidPn.SourcePinnedStatus == STATUS_SUCCESS) ?
        &g_FakeVidPn.SourceMode : NULL;
    return g_FakeVidPn.SourcePinnedStatus;
}

static NTSTATUS APIENTRY Bc250FakeAcquireFirstSourceMode(
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet,
    CONST D3DKMDT_VIDPN_SOURCE_MODE **Mode)
{
    if (ModeSet != BC250_FAKE_SOURCE_SET_HANDLE || Mode == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Mode = NULL;
    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

/*
 * pfnGetNumModes existe no contrato do dxgkrnl e e' usado pela instrumentacao
 * de topologia vazia.  Sem estes dois fakes o ramo chamava por ponteiro nulo.
 */
static NTSTATUS APIENTRY Bc250FakeGetNumSourceModes(
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet,
    SIZE_T *NumModes)
{
    if (ModeSet != BC250_FAKE_SOURCE_SET_HANDLE || NumModes == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *NumModes = g_FakeVidPn.SourceModeAdds;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeGetNumTargetModes(
    D3DKMDT_HVIDPNTARGETMODESET ModeSet,
    SIZE_T *NumModes)
{
    if (ModeSet != BC250_FAKE_TARGET_SET_HANDLE || NumModes == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *NumModes = g_FakeVidPn.TargetModeAdds;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeReleaseSourceMode(
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet,
    CONST D3DKMDT_VIDPN_SOURCE_MODE *Mode)
{
    if (ModeSet != BC250_FAKE_SOURCE_SET_HANDLE ||
        (Mode != &g_FakeVidPn.SourceMode && Mode != &g_FakeVidPn.CreatedSourceMode)) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.SourceModeReleases;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeCreateSourceMode(
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet,
    D3DKMDT_VIDPN_SOURCE_MODE **Mode)
{
    if (ModeSet != BC250_FAKE_SOURCE_SET_HANDLE || Mode == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    memset(&g_FakeVidPn.CreatedSourceMode, 0,
        sizeof(g_FakeVidPn.CreatedSourceMode));
    *Mode = &g_FakeVidPn.CreatedSourceMode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeAddSourceMode(
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet,
    D3DKMDT_VIDPN_SOURCE_MODE *Mode)
{
    if (ModeSet != BC250_FAKE_SOURCE_SET_HANDLE ||
        Mode != &g_FakeVidPn.CreatedSourceMode) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.SourceModeAdds;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeAcquireTargetSet(
    D3DKMDT_HVIDPN VidPn,
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    D3DKMDT_HVIDPNTARGETMODESET *ModeSet,
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE **Interface)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || (TargetId != 0 && TargetId != 1) ||
        ModeSet == NULL || Interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *ModeSet = BC250_FAKE_TARGET_SET_HANDLE;
    *Interface = &g_FakeTargetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeReleaseTargetSet(
    D3DKMDT_HVIDPN VidPn,
    D3DKMDT_HVIDPNTARGETMODESET ModeSet)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || ModeSet != BC250_FAKE_TARGET_SET_HANDLE) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.TargetSetReleases;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeCreateTargetSet(
    D3DKMDT_HVIDPN VidPn,
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    D3DKMDT_HVIDPNTARGETMODESET *ModeSet,
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE **Interface)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || (TargetId != 0 && TargetId != 1) ||
        ModeSet == NULL || Interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.TargetSetCreates;
    *ModeSet = BC250_FAKE_TARGET_SET_HANDLE;
    *Interface = &g_FakeTargetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeAssignTargetSet(
    D3DKMDT_HVIDPN VidPn,
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    D3DKMDT_HVIDPNTARGETMODESET ModeSet)
{
    if (VidPn != BC250_FAKE_VIDPN_HANDLE || (TargetId != 0 && TargetId != 1) ||
        ModeSet != BC250_FAKE_TARGET_SET_HANDLE) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.TargetSetAssignments;
    return g_FakeVidPn.AssignTargetStatus;
}

static NTSTATUS APIENTRY Bc250FakeAcquirePinnedTargetMode(
    D3DKMDT_HVIDPNTARGETMODESET ModeSet,
    CONST D3DKMDT_VIDPN_TARGET_MODE **Mode)
{
    if (ModeSet != BC250_FAKE_TARGET_SET_HANDLE || Mode == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Mode = (g_FakeVidPn.TargetPinnedStatus == STATUS_SUCCESS) ?
        &g_FakeVidPn.TargetMode : NULL;
    return g_FakeVidPn.TargetPinnedStatus;
}

static NTSTATUS APIENTRY Bc250FakeAcquireFirstTargetMode(
    D3DKMDT_HVIDPNTARGETMODESET ModeSet,
    CONST D3DKMDT_VIDPN_TARGET_MODE **Mode)
{
    if (ModeSet != BC250_FAKE_TARGET_SET_HANDLE || Mode == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Mode = NULL;
    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY Bc250FakeReleaseTargetMode(
    D3DKMDT_HVIDPNTARGETMODESET ModeSet,
    CONST D3DKMDT_VIDPN_TARGET_MODE *Mode)
{
    if (ModeSet != BC250_FAKE_TARGET_SET_HANDLE ||
        (Mode != &g_FakeVidPn.TargetMode && Mode != &g_FakeVidPn.CreatedTargetMode)) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.TargetModeReleases;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeCreateTargetMode(
    D3DKMDT_HVIDPNTARGETMODESET ModeSet,
    D3DKMDT_VIDPN_TARGET_MODE **Mode)
{
    if (ModeSet != BC250_FAKE_TARGET_SET_HANDLE || Mode == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    memset(&g_FakeVidPn.CreatedTargetMode, 0,
        sizeof(g_FakeVidPn.CreatedTargetMode));
    *Mode = &g_FakeVidPn.CreatedTargetMode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Bc250FakeAddTargetMode(
    D3DKMDT_HVIDPNTARGETMODESET ModeSet,
    D3DKMDT_VIDPN_TARGET_MODE *Mode)
{
    if (ModeSet != BC250_FAKE_TARGET_SET_HANDLE ||
        Mode != &g_FakeVidPn.CreatedTargetMode) {
        return STATUS_INVALID_PARAMETER;
    }
    ++g_FakeVidPn.TargetModeAdds;
    return STATUS_SUCCESS;
}

static void Bc250InitFakeVidPn(void)
{
    memset(&g_FakeVidPn, 0, sizeof(g_FakeVidPn));
    memset(&g_FakeVidPnInterface, 0, sizeof(g_FakeVidPnInterface));
    memset(&g_FakeTopologyInterface, 0, sizeof(g_FakeTopologyInterface));
    memset(&g_FakeSourceInterface, 0, sizeof(g_FakeSourceInterface));
    memset(&g_FakeTargetInterface, 0, sizeof(g_FakeTargetInterface));

    g_FakeVidPn.PathCount = 1;
    g_FakeVidPn.SourcePinnedStatus = STATUS_SUCCESS;
    g_FakeVidPn.TargetPinnedStatus = STATUS_SUCCESS;
    g_FakeVidPn.UpdatePathStatus = STATUS_SUCCESS;
    g_FakeVidPn.AssignSourceStatus = STATUS_SUCCESS;
    g_FakeVidPn.AssignTargetStatus = STATUS_SUCCESS;
    g_FakeVidPn.Path.VidPnSourceId = 0;
    g_FakeVidPn.Path.VidPnTargetId = 0;
    g_FakeVidPn.Path.ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
    g_FakeVidPn.Path.ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
    g_FakeVidPn.SourceMode.Type = D3DKMDT_RMT_GRAPHICS;
    g_FakeVidPn.SourceMode.Format.Graphics.PrimSurfSize.cx = 2560;
    g_FakeVidPn.SourceMode.Format.Graphics.PrimSurfSize.cy = 1080;
    g_FakeVidPn.SourceMode.Format.Graphics.VisibleRegionSize.cx = 2560;
    g_FakeVidPn.SourceMode.Format.Graphics.VisibleRegionSize.cy = 1080;
    g_FakeVidPn.SourceMode.Format.Graphics.Stride = 10240;
    g_FakeVidPn.SourceMode.Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
    g_FakeVidPn.TargetMode.VideoSignalInfo.ActiveSize.cx = 2560;
    g_FakeVidPn.TargetMode.VideoSignalInfo.ActiveSize.cy = 1080;
    g_FakeVidPn.TargetMode.VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

    g_FakeVidPnInterface.Version = DXGK_VIDPN_INTERFACE_VERSION_V1;
    g_FakeVidPnInterface.pfnGetTopology = Bc250FakeGetTopology;
    g_FakeVidPnInterface.pfnAcquireSourceModeSet = Bc250FakeAcquireSourceSet;
    g_FakeVidPnInterface.pfnReleaseSourceModeSet = Bc250FakeReleaseSourceSet;
    g_FakeVidPnInterface.pfnCreateNewSourceModeSet = Bc250FakeCreateSourceSet;
    g_FakeVidPnInterface.pfnAssignSourceModeSet = Bc250FakeAssignSourceSet;
    g_FakeVidPnInterface.pfnAcquireTargetModeSet = Bc250FakeAcquireTargetSet;
    g_FakeVidPnInterface.pfnReleaseTargetModeSet = Bc250FakeReleaseTargetSet;
    g_FakeVidPnInterface.pfnCreateNewTargetModeSet = Bc250FakeCreateTargetSet;
    g_FakeVidPnInterface.pfnAssignTargetModeSet = Bc250FakeAssignTargetSet;
    g_FakeTopologyInterface.pfnGetNumPaths = Bc250FakeGetNumPaths;
    g_FakeTopologyInterface.pfnAcquireFirstPathInfo = Bc250FakeAcquireFirstPath;
    g_FakeTopologyInterface.pfnUpdatePathSupportInfo = Bc250FakeUpdatePathSupport;
    g_FakeTopologyInterface.pfnReleasePathInfo = Bc250FakeReleasePath;
    g_FakeSourceInterface.pfnAcquirePinnedModeInfo = Bc250FakeAcquirePinnedSourceMode;
    g_FakeSourceInterface.pfnAcquireFirstModeInfo = Bc250FakeAcquireFirstSourceMode;
    g_FakeSourceInterface.pfnGetNumModes = Bc250FakeGetNumSourceModes;
    g_FakeTargetInterface.pfnGetNumModes = Bc250FakeGetNumTargetModes;
    g_FakeSourceInterface.pfnReleaseModeInfo = Bc250FakeReleaseSourceMode;
    g_FakeSourceInterface.pfnCreateNewModeInfo = Bc250FakeCreateSourceMode;
    g_FakeSourceInterface.pfnAddMode = Bc250FakeAddSourceMode;
    g_FakeTargetInterface.pfnAcquirePinnedModeInfo = Bc250FakeAcquirePinnedTargetMode;
    g_FakeTargetInterface.pfnAcquireFirstModeInfo = Bc250FakeAcquireFirstTargetMode;
    g_FakeTargetInterface.pfnReleaseModeInfo = Bc250FakeReleaseTargetMode;
    g_FakeTargetInterface.pfnCreateNewModeInfo = Bc250FakeCreateTargetMode;
    g_FakeTargetInterface.pfnAddMode = Bc250FakeAddTargetMode;
}

static int Bc250RunDisplayVidPnTest(void)
{
    DRIVER_INITIALIZATION_DATA initData;
    DXGK_CHILD_DESCRIPTOR childDescriptor;
    DXGK_CHILD_STATUS childStatus;
    DXGKARG_ISSUPPORTEDVIDPN isSupported;
    DXGKARG_ENUMVIDPNCOFUNCMODALITY enumCofunc;
    DXGKARG_COMMITVIDPN commitVidPn;
    DXGKARG_PRESENT presentArg;
    DXGK_PRESENTALLOCATIONINFO presentAllocations[3];
    DXGKRNL_INTERFACE dxgkInterface;
    DXGK_DISPLAY_INFORMATION postDisplayInfo;
    DXGKARG_PRESENT_DISPLAYONLY displayOnly;
    D3DKMT_MOVE_RECT moveRect;
    RECT dirtyRect;
    unsigned long sourcePixels[16];
    unsigned long frameBufferPixels[16];
    unsigned long index;
    ULONG releaseBaseline;
    UINT64 resolvedPhysical;
    UINT64 firstMismatch;
    UINT32 resolvedSegment;

    memset(&initData, 0, sizeof(initData));
    memset(&childDescriptor, 0, sizeof(childDescriptor));
    memset(&childStatus, 0, sizeof(childStatus));
    memset(&isSupported, 0, sizeof(isSupported));
    memset(&enumCofunc, 0, sizeof(enumCofunc));
    memset(&commitVidPn, 0, sizeof(commitVidPn));
    memset(&presentArg, 0, sizeof(presentArg));
    memset(presentAllocations, 0, sizeof(presentAllocations));
    memset(&dxgkInterface, 0, sizeof(dxgkInterface));
    memset(&postDisplayInfo, 0, sizeof(postDisplayInfo));
    memset(&displayOnly, 0, sizeof(displayOnly));
    memset(&moveRect, 0, sizeof(moveRect));
    memset(&dirtyRect, 0, sizeof(dirtyRect));
    memset(sourcePixels, 0, sizeof(sourcePixels));
    memset(frameBufferPixels, 0, sizeof(frameBufferPixels));

    /* 1. Test DDI Population */
    CHECK(Bc250DisplayPopulateCallbacks(&initData) == STATUS_SUCCESS);
    CHECK(initData.DxgkDdiQueryChildRelations == Bc250DxgkDdiQueryChildRelations);
    CHECK(initData.DxgkDdiQueryChildStatus == Bc250DxgkDdiQueryChildStatus);
    CHECK(initData.DxgkDdiIsSupportedVidPn == Bc250DxgkDdiIsSupportedVidPn);
    CHECK(initData.DxgkDdiCommitVidPn == Bc250DxgkDdiCommitVidPn);
    CHECK(initData.DxgkDdiPresent == Bc250DxgkDdiPresent);

    /* 2. Test QueryChildRelations */
    CHECK(Bc250DxgkDdiQueryChildRelations(NULL, &childDescriptor, sizeof(childDescriptor)) == STATUS_SUCCESS);
    CHECK(childDescriptor.ChildDeviceType == TypeVideoOutput);
    CHECK(childDescriptor.ChildUid == 1);
    CHECK(childDescriptor.ChildCapabilities.HpdAwareness == HpdAwarenessAlwaysConnected);

    /* 3. Test QueryChildStatus */
    childStatus.Type = StatusConnection;
    CHECK(Bc250DxgkDdiQueryChildStatus(NULL, &childStatus, FALSE) == STATUS_SUCCESS);
    CHECK(childStatus.HotPlug.Connected == TRUE);
    childStatus.Type = StatusRotation;
    childStatus.Rotation.Angle = 90;
    CHECK(Bc250DxgkDdiQueryChildStatus(NULL, &childStatus, FALSE) == STATUS_SUCCESS);
    CHECK(childStatus.Rotation.Angle == 0);

    /* 4. Test IsSupportedVidPn */
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == TRUE);

    /* 5. Test Present */
    CHECK(Bc250DxgkDdiPresent(NULL, &presentArg) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DxgkDdiPresent((HANDLE)0x1234, NULL) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DxgkDdiPresent((HANDLE)0x1234, &presentArg) == STATUS_NOT_SUPPORTED);

    /* 5b. Contrato da origem sobre DXGK_PRESENTALLOCATIONINFO (WDDM 2.0 GpuMmu).
     *
     * O dxgkrnl preenche pAllocationInfo, nao pAllocationList: sao membros da
     * mesma uniao com layouts diferentes. A corrida 04/09 18:10 provou o efeito
     * de ler o membro errado (SegmentId=16, hDev=NULL, ponteiro de kernel no
     * slot 2). Ver o comentario em Bc250DisplayResolvePresentSource.
     *
     * Autoridade: PhysicalAddress + SegmentId==1 entregues pelo VidMm primeiro;
     * GPUVA resolvida no snapshot comitado depois. hDeviceSpecificAllocation
     * NAO e exigido -- o DWM entrega NULL nesse campo. */
    presentArg.Flags.Blt = 1u;
    presentArg.pAllocationInfo = presentAllocations;
    CHECK(!Bc250DisplayResolvePresentSource((HANDLE)0x1234, &presentArg,
        &resolvedPhysical, &resolvedSegment));
    presentAllocations[DXGK_PRESENT_SOURCE_INDEX].SegmentId = 12u;
    presentAllocations[DXGK_PRESENT_SOURCE_INDEX].PhysicalAddress.QuadPart =
        0xAC0000ll;
    CHECK(!Bc250DisplayResolvePresentSource((HANDLE)0x1234, &presentArg,
        &resolvedPhysical, &resolvedSegment));
    /* Segmento 1 com fisico valido resolve mesmo com hDeviceSpecificAllocation
     * NULL, que e exatamente o formato entregue pelo DWM. */
    presentAllocations[DXGK_PRESENT_SOURCE_INDEX].SegmentId = 1u;
    presentAllocations[DXGK_PRESENT_SOURCE_INDEX].PhysicalAddress.QuadPart =
        0xAD0000ll;
    CHECK(presentAllocations[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation
        == NULL);
    CHECK(Bc250DisplayResolvePresentSource((HANDLE)0x1234, &presentArg,
        &resolvedPhysical, &resolvedSegment));
    CHECK(resolvedPhysical == 0xAD0000ull && resolvedSegment == 1u);
    /* Sem fisico utilizavel, cai para a GPUVA resolvida no snapshot. */
    presentAllocations[DXGK_PRESENT_SOURCE_INDEX].SegmentId = 0u;
    presentAllocations[DXGK_PRESENT_SOURCE_INDEX].PhysicalAddress.QuadPart = 0ll;
    presentAllocations[DXGK_PRESENT_SOURCE_INDEX].AllocationVirtualAddress =
        0xAC0000ull;
    Bc250DisplayInstallGpuVaResolver(Bc250FakeResolveGpuVa);
    CHECK(Bc250DisplayResolvePresentSource((HANDLE)0x1234, &presentArg,
        &resolvedPhysical, &resolvedSegment));
    CHECK(resolvedPhysical == 0xC10000ull && resolvedSegment == 1u);
    Bc250DisplayInstallGpuVaResolver(NULL);
    CHECK(!Bc250DisplayResolvePresentSource((HANDLE)0x1234, &presentArg,
        &resolvedPhysical, &resolvedSegment));

    /* O tracker paralelo pode ser consultado para diagnostico, mas nao tem
     * autoridade para liberar Present antes do commit do journal/FlushTlb. */
    Bc250DisplayRecordGpuVaMapping((HANDLE)0x9980, 0xAC0000ull, 0xC10000ull, 11075584ull, 1u);
    CHECK(Bc250DisplayLookupPhysicalAddress((HANDLE)0x9980, 0xAC0000ull, &resolvedPhysical, &resolvedSegment));
    CHECK(resolvedPhysical == 0xC10000ull && resolvedSegment == 1u);
    /* Process isolation: different process fails lookup */
    CHECK(!Bc250DisplayLookupPhysicalAddress((HANDLE)0x9740, 0xAC0000ull, &resolvedPhysical, &resolvedSegment));
    /* Present continua fail-closed sem o resolver do snapshot comitado. */
    Bc250DisplayRegisterContextProcess((HANDLE)0x1234, (HANDLE)0x9980);
    CHECK(!Bc250DisplayResolvePresentSource((HANDLE)0x1234, &presentArg,
        &resolvedPhysical, &resolvedSegment));
    Bc250DisplayUnregisterContextProcess((HANDLE)0x1234);
    Bc250DisplayClearGpuVaMappings();
    CHECK(!Bc250DisplayLookupPhysicalAddress((HANDLE)0x9980, 0xAC0000ull, &resolvedPhysical, &resolvedSegment));

    /* 6. GOP ownership data must be complete and arithmetically safe. */
    CHECK(Bc250DisplayInit(NULL, &postDisplayInfo) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DisplayInit(&dxgkInterface, &postDisplayInfo) == STATUS_INVALID_PARAMETER);
    postDisplayInfo.PhysicAddress.QuadPart = 0xC0000000LL;
    postDisplayInfo.Width = 2560;
    postDisplayInfo.Height = 1080;
    postDisplayInfo.Pitch = 10240;
    CHECK(Bc250DisplayInit(&dxgkInterface, &postDisplayInfo) == STATUS_SUCCESS);
    CHECK(g_Bc250DisplayState.CurrentMode.Width == 2560);
    CHECK(g_Bc250DisplayState.CurrentMode.Height == 1080);
    CHECK(g_Bc250DisplayState.CurrentMode.Pitch == 10240);

    /* 7. Strict VidPN validation accepts only source 0 -> target 0 at GOP geometry. */
    Bc250InitFakeVidPn();
    g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface =
        Bc250FakeQueryVidPnInterface;
    isSupported.hDesiredVidPn = BC250_FAKE_VIDPN_HANDLE;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == TRUE);
    CHECK(g_FakeVidPn.PathReleases == 1);
    CHECK(g_FakeVidPn.SourceModeReleases == 1);
    CHECK(g_FakeVidPn.TargetModeReleases == 1);
    CHECK(g_FakeVidPn.SourceSetReleases == 1);
    CHECK(g_FakeVidPn.TargetSetReleases == 1);

    releaseBaseline = g_FakeVidPn.PathReleases;
    g_FakeVidPn.Path.VidPnTargetId = 1;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == TRUE);
    g_FakeVidPn.Path.VidPnTargetId = 2;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == FALSE);
    CHECK(g_FakeVidPn.PathReleases >= releaseBaseline + 1);
    g_FakeVidPn.Path.VidPnTargetId = 0;

    g_FakeVidPn.Path.ContentTransformation.Rotation = D3DKMDT_VPPR_ROTATE90;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == FALSE);
    g_FakeVidPn.Path.ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;

    g_FakeVidPn.PathCount = 2;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == FALSE);
    g_FakeVidPn.PathCount = 1;

    /* Source allocation stride is independent from the physical GOP pitch. */
    g_FakeVidPn.SourceMode.Format.Graphics.Stride = 12288;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == TRUE);
    g_FakeVidPn.SourceMode.Format.Graphics.PixelFormat = D3DDDIFMT_X8R8G8B8;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == TRUE);
    g_FakeVidPn.SourceMode.Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
    g_FakeVidPn.SourceMode.Format.Graphics.Stride = 4096;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == FALSE);
    g_FakeVidPn.SourceMode.Format.Graphics.Stride = 10240;

    g_FakeVidPn.TargetMode.VideoSignalInfo.ActiveSize.cy = 720;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == FALSE);
    g_FakeVidPn.TargetMode.VideoSignalInfo.ActiveSize.cy = 1080;

    g_FakeVidPn.SourcePinnedStatus = STATUS_GRAPHICS_MODE_NOT_PINNED;
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == TRUE);
    g_FakeVidPn.SourcePinnedStatus = STATUS_SUCCESS;

    /* 8. Cofunctional enumeration is single-path, pivot-aware and fail-closed. */
    enumCofunc.hConstrainingVidPn = BC250_FAKE_VIDPN_HANDLE;
    enumCofunc.EnumPivotType = D3DKMDT_EPT_NOPIVOT;
    releaseBaseline = g_FakeVidPn.PathReleases;
    CHECK(Bc250DxgkDdiEnumVidPnCofuncModality(NULL, &enumCofunc) == STATUS_SUCCESS);
    CHECK(g_FakeVidPn.PathSupportUpdates == 1);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.ScalingSupport.Identity == 1);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.RotationSupport.Identity == 1);
    CHECK(g_FakeVidPn.PathReleases == releaseBaseline + 1);

    /* The DMM contract forbids changing support for the active scaling or
     * rotation pivot.  The other (non-pivot) field must still be sanitized
     * to keep the 0x113/0x1C protection. */
    RtlZeroMemory(&g_FakeVidPn.Path.ContentTransformation.ScalingSupport,
        sizeof(g_FakeVidPn.Path.ContentTransformation.ScalingSupport));
    g_FakeVidPn.Path.ContentTransformation.ScalingSupport.Centered = 1;
    RtlZeroMemory(&g_FakeVidPn.Path.ContentTransformation.RotationSupport,
        sizeof(g_FakeVidPn.Path.ContentTransformation.RotationSupport));
    g_FakeVidPn.Path.ContentTransformation.RotationSupport.Rotate90 = 1;
    g_FakeVidPn.Path.ContentTransformation.RotationSupport.Offset90 = 1;

    enumCofunc.EnumPivotType = D3DKMDT_EPT_SCALING;
    enumCofunc.EnumPivot.VidPnSourceId = 0;
    enumCofunc.EnumPivot.VidPnTargetId = 0;
    CHECK(Bc250DxgkDdiEnumVidPnCofuncModality(NULL, &enumCofunc) == STATUS_SUCCESS);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.ScalingSupport.Centered == 1);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.ScalingSupport.Identity == 0);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.RotationSupport.Identity == 1);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.RotationSupport.Offset0 == 1);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.RotationSupport.Rotate90 == 0);

    enumCofunc.EnumPivotType = D3DKMDT_EPT_ROTATION;
    CHECK(Bc250DxgkDdiEnumVidPnCofuncModality(NULL, &enumCofunc) == STATUS_SUCCESS);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.RotationSupport.Rotate90 == 1);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.RotationSupport.Offset90 == 1);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.RotationSupport.Identity == 0);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.ScalingSupport.Identity == 1);
    CHECK(g_FakeVidPn.LastSupportedPath.ContentTransformation.ScalingSupport.Centered == 0);

    RtlZeroMemory(&g_FakeVidPn.Path.ContentTransformation.ScalingSupport,
        sizeof(g_FakeVidPn.Path.ContentTransformation.ScalingSupport));
    RtlZeroMemory(&g_FakeVidPn.Path.ContentTransformation.RotationSupport,
        sizeof(g_FakeVidPn.Path.ContentTransformation.RotationSupport));

    enumCofunc.EnumPivotType = D3DKMDT_EPT_VIDPNSOURCE;
    enumCofunc.EnumPivot.VidPnSourceId = 0;
    releaseBaseline = g_FakeVidPn.SourceSetReleases;
    CHECK(Bc250DxgkDdiEnumVidPnCofuncModality(NULL, &enumCofunc) == STATUS_SUCCESS);
    CHECK(g_FakeVidPn.SourceSetReleases == releaseBaseline);

    enumCofunc.EnumPivotType = D3DKMDT_EPT_NOPIVOT;
    g_FakeVidPn.PathCount = 2;
    CHECK(Bc250DxgkDdiEnumVidPnCofuncModality(NULL, &enumCofunc) ==
        STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
    g_FakeVidPn.PathCount = 1;

    g_FakeVidPn.UpdatePathStatus = STATUS_UNSUCCESSFUL;
    CHECK(Bc250DxgkDdiEnumVidPnCofuncModality(NULL, &enumCofunc) ==
        STATUS_UNSUCCESSFUL);
    g_FakeVidPn.UpdatePathStatus = STATUS_SUCCESS;

    g_FakeVidPn.SourcePinnedStatus = STATUS_GRAPHICS_MODE_NOT_PINNED;
    g_FakeVidPn.TargetPinnedStatus = STATUS_GRAPHICS_MODE_NOT_PINNED;
    CHECK(Bc250DxgkDdiEnumVidPnCofuncModality(NULL, &enumCofunc) == STATUS_SUCCESS);
    CHECK(g_FakeVidPn.SourceSetCreates == 1);
    CHECK(g_FakeVidPn.TargetSetCreates == 1);
    CHECK(g_FakeVidPn.SourceModeAdds == 1);
    CHECK(g_FakeVidPn.TargetModeAdds == 1);
    CHECK(g_FakeVidPn.SourceSetAssignments == 1);
    CHECK(g_FakeVidPn.TargetSetAssignments == 1);
    CHECK(g_FakeVidPn.CreatedSourceMode.Format.Graphics.Stride == 10240);
    CHECK(g_FakeVidPn.CreatedTargetMode.VideoSignalInfo.ActiveSize.cx == 2560);
    /*
     * Timings ancorados na medicao fisica do OTG
     * (docs/evidence/2026-09-02_21-40-32-.../dcn_inventory.json):
     * H_TOTAL=2783(+1)=2784, V_TOTAL=1110(+1)=1111, 56.13 Hz,
     * PixelRate = 2784 * 1111 * 56.13 = 173611437 Hz.
     * O gabarito antigo (2720x1125@60) era CVT sintetico e nunca existiu
     * no silicio; ficou obsoleto no commit 55cbcb8 e mantinha este teste
     * vermelho desde entao.
     */
    CHECK(g_FakeVidPn.CreatedTargetMode.VideoSignalInfo.TotalSize.cx == 2784);
    CHECK(g_FakeVidPn.CreatedTargetMode.VideoSignalInfo.TotalSize.cy == 1111);
    CHECK(g_FakeVidPn.CreatedTargetMode.VideoSignalInfo.HSyncFreq.Numerator == 6236043);
    CHECK(g_FakeVidPn.CreatedTargetMode.VideoSignalInfo.HSyncFreq.Denominator == 100);
    CHECK(g_FakeVidPn.CreatedTargetMode.VideoSignalInfo.VSyncFreq.Numerator == 5613);
    CHECK(g_FakeVidPn.CreatedTargetMode.VideoSignalInfo.VSyncFreq.Denominator == 100);
    CHECK(g_FakeVidPn.CreatedTargetMode.VideoSignalInfo.PixelRate == 173611437);

    g_FakeVidPn.AssignSourceStatus = STATUS_UNSUCCESSFUL;
    releaseBaseline = g_FakeVidPn.SourceSetReleases;
    CHECK(Bc250DxgkDdiEnumVidPnCofuncModality(NULL, &enumCofunc) ==
        STATUS_UNSUCCESSFUL);
    CHECK(g_FakeVidPn.SourceSetReleases == releaseBaseline + 2);
    g_FakeVidPn.AssignSourceStatus = STATUS_SUCCESS;
    g_FakeVidPn.SourcePinnedStatus = STATUS_SUCCESS;
    g_FakeVidPn.TargetPinnedStatus = STATUS_SUCCESS;

    /* 9. Commit accepts an aligned source stride larger than the GOP pitch. */
    g_FakeVidPn.SourceMode.Format.Graphics.Stride = 12288;
    g_Bc250DisplayState.DisplayStarted = 1;
    commitVidPn.AffectedVidPnSourceId = 0;
    commitVidPn.hFunctionalVidPn = BC250_FAKE_VIDPN_HANDLE;
    CHECK(Bc250DxgkDdiCommitVidPn(NULL, &commitVidPn) == STATUS_SUCCESS);
    CHECK(g_Bc250DisplayState.VidPnCommitted == 1);
    CHECK(g_Bc250DisplayState.ForceFullPresent == 1);
    g_FakeVidPn.SourceMode.Format.Graphics.Stride = 10240;

    /*
     * 9b. Commit de topologia vazia: aceitar durante transicao de energia
     * (Flags.PathPoweredOff ou Flags.PathPowerTransition) e durante transicao
     * transiente de cofuncionalidade (Flags==0, pathCount==0) para permitir que
     * o DMM avance para GetStandardAllocationDriverData sem travar em cofunc loop.
     * Em ambos os casos, VidPnCommitted permanece 0 e o scanout GOP e preservado.
     */
    g_Bc250DisplayState.SourceVisible = 0;
    g_Bc250DisplayState.ForceFullPresent = 0;
    g_Bc250DisplayState.VidPnCommitted = 0;
    g_FakeVidPn.PathCount = 0;
    commitVidPn.Flags.PathPoweredOff = 1;
    CHECK(Bc250DxgkDdiCommitVidPn(NULL, &commitVidPn) == STATUS_SUCCESS);
    CHECK(g_Bc250DisplayState.SourceVisible == 0);
    CHECK(g_Bc250DisplayState.ForceFullPresent == 0);
    CHECK(g_Bc250DisplayState.VidPnCommitted == 0);

    /* Transicao transiente sem transicao de energia (pathCount==0, Flags==0) tambem deve retornar SUCCESS */
    commitVidPn.Flags.PathPoweredOff = 0;
    CHECK(Bc250DxgkDdiCommitVidPn(NULL, &commitVidPn) == STATUS_SUCCESS);
    CHECK(g_Bc250DisplayState.VidPnCommitted == 0);
    g_FakeVidPn.PathCount = 1;

    /*
     * 9c. Origem de scanout com autoridade: PrimarySegment/PrimaryAddress do
     * SetVidPnSourceAddress.  Somente o segmento 1 (VRAM local) e' aceito; o
     * segmento 2 (GART) e qualquer valor fora do contrato falham fechado.
     */
    g_Bc250DisplayState.PrimaryAddressValid = FALSE;
    g_Bc250DisplayState.PrimarySegment = 0;
    g_Bc250DisplayState.PrimaryAddress.QuadPart = 0;
    CHECK(!Bc250DisplayResolvePrimarySource(&resolvedPhysical, &resolvedSegment));
    g_Bc250DisplayState.PrimaryAddressValid = TRUE;
    g_Bc250DisplayState.PrimaryAddress.QuadPart = 0xAD0000ll;
    g_Bc250DisplayState.PrimarySegment = 2u;
    CHECK(!Bc250DisplayResolvePrimarySource(&resolvedPhysical, &resolvedSegment));
    g_Bc250DisplayState.PrimarySegment = 12u;
    CHECK(!Bc250DisplayResolvePrimarySource(&resolvedPhysical, &resolvedSegment));
    g_Bc250DisplayState.PrimarySegment = 1u;
    CHECK(Bc250DisplayResolvePrimarySource(&resolvedPhysical, &resolvedSegment));
    CHECK(resolvedPhysical == 0xAD0000ull && resolvedSegment == 1u);
    g_Bc250DisplayState.PrimaryAddress.QuadPart = 0;
    CHECK(!Bc250DisplayResolvePrimarySource(&resolvedPhysical, &resolvedSegment));
    g_Bc250DisplayState.PrimaryAddressValid = FALSE;
    g_Bc250DisplayState.PrimarySegment = 0;

    /* 10. PresentDisplayOnly validates format/pitch and copies bounded pixels. */
    g_Bc250DisplayState.CurrentMode.Width = 4;
    g_Bc250DisplayState.CurrentMode.Height = 4;
    g_Bc250DisplayState.CurrentMode.Pitch = 16;
    g_Bc250DisplayState.FrameBufferVirtual = frameBufferPixels;
    g_Bc250DisplayState.DisplayStarted = 1;
    g_Bc250DisplayState.PowerStateD0 = 1;
    g_Bc250DisplayState.SourceVisible = 1;
    g_Bc250DisplayState.VidPnCommitted = 1;
    for (index = 0; index < 16; ++index) sourcePixels[index] = 0x25000000u + index;
    displayOnly.pSource = sourcePixels;
    displayOnly.BytesPerPixel = 4;
    displayOnly.Pitch = 16;
    CHECK(Bc250DxgkDdiPresentDisplayOnly(NULL, &displayOnly) == STATUS_SUCCESS);
    for (index = 0; index < 16; ++index) CHECK(frameBufferPixels[index] == sourcePixels[index]);
    CHECK(g_Bc250DisplayState.VsyncProofRecorded == 0);
    g_Bc250DisplayState.VsyncCount = 7;
    CHECK(Bc250DxgkDdiPresentDisplayOnly(NULL, &displayOnly) == STATUS_SUCCESS);
    CHECK(g_Bc250DisplayState.VsyncProofRecorded == 1);
    g_Bc250DisplayState.VsyncCount = 8;
    CHECK(Bc250DxgkDdiPresentDisplayOnly(NULL, &displayOnly) == STATUS_SUCCESS);
    CHECK(g_Bc250DisplayState.VsyncProofRecorded == 1);
    displayOnly.BytesPerPixel = 3;
    CHECK(Bc250DxgkDdiPresentDisplayOnly(NULL, &displayOnly) == STATUS_INVALID_PARAMETER);
    displayOnly.BytesPerPixel = 4;
    displayOnly.Pitch = 8;
    CHECK(Bc250DxgkDdiPresentDisplayOnly(NULL, &displayOnly) == STATUS_INVALID_PARAMETER);

    /* 11. Dirty and move destination rectangles remain bounded. */
    displayOnly.Pitch = 16;
    dirtyRect.left = 1; dirtyRect.top = 1;
    dirtyRect.right = 3; dirtyRect.bottom = 3;
    displayOnly.NumDirtyRects = 1;
    displayOnly.pDirtyRect = &dirtyRect;
    memset(frameBufferPixels, 0, sizeof(frameBufferPixels));
    CHECK(Bc250DxgkDdiPresentDisplayOnly(NULL, &displayOnly) == STATUS_SUCCESS);
    CHECK(frameBufferPixels[5] == sourcePixels[5]);
    CHECK(frameBufferPixels[10] == sourcePixels[10]);
    CHECK(frameBufferPixels[0] == 0);

    displayOnly.NumDirtyRects = 0;
    displayOnly.pDirtyRect = NULL;
    displayOnly.NumMoves = 1;
    displayOnly.pMoves = &moveRect;
    moveRect.SourcePoint.x = 0; moveRect.SourcePoint.y = 0;
    moveRect.DestRect.left = 1; moveRect.DestRect.top = 1;
    moveRect.DestRect.right = 3; moveRect.DestRect.bottom = 3;
    for (index = 0; index < 16; ++index) frameBufferPixels[index] = index;
    CHECK(Bc250DxgkDdiPresentDisplayOnly(NULL, &displayOnly) == STATUS_SUCCESS);
    CHECK(frameBufferPixels[5] == sourcePixels[5]);
    CHECK(frameBufferPixels[6] == sourcePixels[6]);
    CHECK(frameBufferPixels[9] == sourcePixels[9]);
    CHECK(frameBufferPixels[10] == sourcePixels[10]);

    /*
     * 11. Amostragem distribuida.  Este e' o teste que separa "superficie
     * zerada" de "primeiros pixels pretos" -- a distincao que a corrida 09:04
     * nao conseguiu fazer porque olhava apenas 4 DWORDs no inicio do buffer.
     */
    {
        ULONG nonZero = 0xFFFFFFFFu;
        ULONG xorHash = 0xFFFFFFFFu;

        g_Bc250DisplayState.FrameBufferVirtual = frameBufferPixels;
        g_Bc250DisplayState.FrameBufferSize = sizeof(frameBufferPixels);

        /* Buffer inteiramente zerado: nenhum ponto amostrado e' diferente de zero. */
        memset(frameBufferPixels, 0, sizeof(frameBufferPixels));
        CHECK(Bc250DisplaySampleSurface(0ull, sizeof(frameBufferPixels), 16u,
            &nonZero, &xorHash));
        CHECK(nonZero == 0 && xorHash == 0);

        /*
         * Um unico pixel diferente de zero no FIM do buffer, com os primeiros
         * 4 DWORDs em zero: a leitura antiga diria "tudo zero", a amostragem
         * distribuida acusa.
         */
        frameBufferPixels[15] = 0xDEADBEEFu;
        CHECK(Bc250DisplaySampleSurface(0ull, sizeof(frameBufferPixels), 16u,
            &nonZero, &xorHash));
        CHECK(nonZero == 1 && xorHash == 0xDEADBEEFu);
        CHECK(frameBufferPixels[0] == 0 && frameBufferPixels[1] == 0 &&
              frameBufferPixels[2] == 0 && frameBufferPixels[3] == 0);

        /* Fail-closed nos limites: fora da abertura nao amostra. */
        CHECK(!Bc250DisplaySampleSurface(sizeof(frameBufferPixels) + 4u,
            sizeof(frameBufferPixels), 16u, &nonZero, &xorHash));
        CHECK(!Bc250DisplaySampleSurface(0ull, sizeof(frameBufferPixels) + 4u,
            16u, &nonZero, &xorHash));
        CHECK(!Bc250DisplaySampleSurface(0ull, sizeof(frameBufferPixels), 0u,
            &nonZero, &xorHash));
        g_Bc250DisplayState.FrameBufferVirtual = NULL;
        CHECK(!Bc250DisplaySampleSurface(0ull, sizeof(frameBufferPixels), 16u,
            &nonZero, &xorHash));

        /* Prova integral: igualdade, primeiro byte divergente e bounds. */
        for (index = 0; index < 8; ++index) {
            frameBufferPixels[index] = 0xA5000000u + index;
            frameBufferPixels[index + 8] = frameBufferPixels[index];
        }
        g_Bc250DisplayState.FrameBufferVirtual = frameBufferPixels;
        CHECK(Bc250DisplayCompareSurfaceBytes(0ull, 32ull, 32u, &firstMismatch));
        CHECK(firstMismatch == ~(UINT64)0);
        ((unsigned char *)&frameBufferPixels[8])[13] ^= 1u;
        CHECK(!Bc250DisplayCompareSurfaceBytes(0ull, 32ull, 32u, &firstMismatch));
        CHECK(firstMismatch == 13u);
        CHECK(!Bc250DisplayCompareSurfaceBytes(0ull, 60ull, 8u, &firstMismatch));
        CHECK(!Bc250DisplayCompareSurfaceBytes(0ull, 32ull, 32u, NULL));
    }

    return 1;
}

void __stdcall mainCRTStartup(void)
{
    int result = Bc250RunDisplayVidPnTest();
    ExitProcess((result == 1) ? 0ul : (unsigned long)result);
}
