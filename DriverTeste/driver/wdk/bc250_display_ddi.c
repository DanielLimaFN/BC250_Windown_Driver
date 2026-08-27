#include "bc250_display_ddi.h"

NTSTATUS APIENTRY Bc250DxgkDdiQueryChildRelations(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize)
{
    ULONG childCount;
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (ChildRelations == NULL || ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR)) {
        return STATUS_INVALID_PARAMETER;
    }

    childCount = ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR);
    RtlZeroMemory(ChildRelations, ChildRelationsSize);

    ChildRelations[0].ChildDeviceType = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_OTHER;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
    ChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    ChildRelations[0].AcpiUid = 0;
    ChildRelations[0].ChildUid = 1;

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

    if (ChildStatus->Type == StatusConnection) {
        ChildStatus->HotPlug.Connected = TRUE;
        return STATUS_SUCCESS;
    }

    if (ChildStatus->Type == StatusRotation) {
        ChildStatus->Rotation.Angle = 0;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryDeviceDescriptor(
    CONST PVOID MiniportDeviceContext,
    ULONG ChildUid,
    PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiIsSupportedVidPn(
    CONST HANDLE hAdapter,
    DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pIsSupportedVidPn == NULL) {
        return STATUS_INVALID_PARAMETER;
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
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pEnumCofuncModality);
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
    UNREFERENCED_PARAMETER(pSetVidPnSourceVisibility);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiCommitVidPn(
    CONST HANDLE hAdapter,
    CONST DXGKARG_COMMITVIDPN *pCommitVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCommitVidPn);
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
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendMonitorModes);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiRecommendVidPnTopology(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *pRecommendVidPnTopology)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendVidPnTopology);
    return STATUS_SUCCESS;
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
    UNREFERENCED_PARAMETER(InterruptType);
    UNREFERENCED_PARAMETER(EnableInterrupt);
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

    return STATUS_SUCCESS;
}
