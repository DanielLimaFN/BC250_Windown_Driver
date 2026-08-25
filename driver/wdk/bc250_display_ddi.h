#ifndef BC250_DISPLAY_DDI_H
#define BC250_DISPLAY_DDI_H

#include <ntddk.h>
#include <dispmprt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BC250_DISPLAY_STATE {
    DXGK_DISPLAY_INFORMATION PostDisplayInfo;
    ULONG DisplayAcquired;
    ULONG SourceVisible;
    ULONGLONG ScanoutBaseAddress;
} BC250_DISPLAY_STATE;

DXGKDDI_QUERY_CHILD_RELATIONS           Bc250DxgkDdiQueryChildRelations;
DXGKDDI_QUERY_CHILD_STATUS              Bc250DxgkDdiQueryChildStatus;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR         Bc250DxgkDdiQueryDeviceDescriptor;
DXGKDDI_ISSUPPORTEDVIDPN                Bc250DxgkDdiIsSupportedVidPn;
DXGKDDI_RECOMMENDFUNCTIONALVIDPN        Bc250DxgkDdiRecommendFunctionalVidPn;
DXGKDDI_ENUMVIDPNCOFUNCMODALITY         Bc250DxgkDdiEnumVidPnCofuncModality;
DXGKDDI_SETVIDPNSOURCEADDRESS           Bc250DxgkDdiSetVidPnSourceAddress;
DXGKDDI_SETVIDPNSOURCEVISIBILITY        Bc250DxgkDdiSetVidPnSourceVisibility;
DXGKDDI_COMMITVIDPN                    Bc250DxgkDdiCommitVidPn;
DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH   Bc250DxgkDdiUpdateActiveVidPnPresentPath;
DXGKDDI_RECOMMENDMONITORMODES          Bc250DxgkDdiRecommendMonitorModes;
DXGKDDI_RECOMMENDVIDPNTOPOLOGY         Bc250DxgkDdiRecommendVidPnTopology;
DXGKDDI_PRESENT                        Bc250DxgkDdiPresent;
DXGKDDI_CONTROLINTERRUPT               Bc250DxgkDdiControlInterrupt;

NTSTATUS Bc250DisplayPopulateCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData);

#ifdef __cplusplus
}
#endif

#endif
