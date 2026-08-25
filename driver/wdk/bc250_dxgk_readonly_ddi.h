#ifndef BC250_DXGK_READONLY_DDI_H
#define BC250_DXGK_READONLY_DDI_H

#include <ntddk.h>
#include <dispmprt.h>

#include "bc250_start_device_adapter.h"
#include "bc250_pci_bar_reader.h"
#include "bc250_rebar_reader.h"
#include "bc250_mmio_topology_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_DXGK_READONLY_SIGNATURE 0x30353242u

typedef enum BC250_DXGK_READONLY_STAGE {
    BC250_DXGK_STAGE_EMPTY = 0,
    BC250_DXGK_STAGE_INTERFACE_ACCEPTED,
    BC250_DXGK_STAGE_DEVICE_INFO_READ,
    BC250_DXGK_STAGE_RUNTIME_MISSING,
    BC250_DXGK_STAGE_RESOURCES_RESOLVED,
    BC250_DXGK_STAGE_TOPOLOGY_CAPTURED,
    BC250_DXGK_STAGE_MMIO_UNMAP_FAILED,
    BC250_DXGK_STAGE_OWNERSHIP_READY,
    BC250_DXGK_STAGE_PREFLIGHT_READY,
    BC250_DXGK_STAGE_REJECTED
} BC250_DXGK_READONLY_STAGE;

typedef struct BC250_DXGK_READONLY_INPUT {
    uint32_t ReservationCount;
    BC250_INHERITED_VRAM_RESERVATION
        Reservations[BC250_VRAM_MAX_REGIONS];
    uint32_t VramApertureBarIndex;
    uint32_t MmioBarIndex;
    uint32_t TopologyReaderIdentityFlags;
    uint32_t RuntimeOwnershipFlags;
    uint32_t SnapshotGeneration;
    uint32_t ReservedTopology;
    BC250_CP_BOOTSTRAP_POLICY Policy;
    BC250_CP_FIRMWARE_ENTRY Manifest[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT];
    uint32_t ManifestCount;
    uint32_t Reserved;
} BC250_DXGK_READONLY_INPUT;

typedef struct BC250_DXGK_READONLY_CONTEXT {
    uint32_t Signature;
    uint32_t RuntimeStaged;
    uint32_t LastStage;
    uint32_t ReadonlyGatePassed;
    NTSTATUS LastCallbackStatus;
    BC250_START_DEVICE_STATUS LastAdapterStatus;
    BC250_PCI_BAR_READ_STATUS LastPciStatus;
    BC250_REBAR_STATUS LastRebarStatus;
    BC250_ADDRESS_STATUS LastAddressStatus;
    BC250_PSP_CONTRACT_STATUS LastPspStatus;
    BC250_CM_RESOURCE_STATUS LastCmStatus;
    BC250_RESOURCE_STATUS LastResourceStatus;
    BC250_MMIO_TOPOLOGY_STATUS LastMmioStatus;
    BC250_VRAM_TOPOLOGY_READER_STATUS LastTopologyReaderStatus;
    BC250_DISCOVERY_TMR_STATUS LastDiscoveryTmrStatus;
    ULONG PciBytesRead;
    PDEVICE_OBJECT PhysicalDeviceObject;
    BC250_PCI_BAR_RESOURCE DiscoveredBars[BC250_RESOURCE_MAX_BARS];
    uint32_t DiscoveredBarCount;
    uint32_t ResolvedBarCount;
    BC250_REBAR_EVIDENCE RebarEvidence;
    BC250_VRAM_TOPOLOGY_READER_EVIDENCE TopologyEvidence;
    BC250_DISCOVERY_TMR_EVIDENCE DiscoveryTmrEvidence;
    BC250_MMIO_TOPOLOGY_CONTEXT MmioTransport;
    DXGKRNL_INTERFACE DxgkInterface;
    BC250_DXGK_READONLY_INPUT Runtime;
    BC250_START_DEVICE_CONTEXT StartDevice;
} BC250_DXGK_READONLY_CONTEXT;

NTSTATUS Bc250DxgkReadonlyInitialize(
    _Out_ BC250_DXGK_READONLY_CONTEXT *Context);

NTSTATUS Bc250DxgkReadonlyStageRuntime(
    _Inout_ BC250_DXGK_READONLY_CONTEXT *Context,
    _In_ const BC250_DXGK_READONLY_INPUT *Runtime);

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceReadonly(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ PDXGK_START_INFO DxgkStartInfo,
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _Out_ PULONG NumberOfVideoPresentSources,
    _Out_ PULONG NumberOfChildren);

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceReadonly(
    _In_ CONST PVOID MiniportDeviceContext);

#ifdef __cplusplus
}
#endif

#endif
