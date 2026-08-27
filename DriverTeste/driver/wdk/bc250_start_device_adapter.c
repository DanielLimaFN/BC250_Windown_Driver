#include "bc250_start_device_adapter.h"

static void Bc250StartDeviceClear(BC250_START_DEVICE_CONTEXT *Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
}

BC250_START_DEVICE_STATUS Bc250StartDeviceInitialize(
    BC250_START_DEVICE_CONTEXT *Context)
{
    if (Context == NULL) return BC250_START_DEVICE_BAD_ARGUMENT;
    Bc250StartDeviceClear(Context);
    return BC250_START_DEVICE_OK;
}

BC250_START_DEVICE_STATUS Bc250StartDevicePrepareOwnership(
    BC250_START_DEVICE_CONTEXT *Context,
    const CM_RESOURCE_LIST *TranslatedResourceList,
    const BC250_PCI_BAR_RESOURCE *Bars,
    size_t BarCount,
    const BC250_START_DEVICE_TOPOLOGY *Topology,
    const BC250_INHERITED_VRAM_RESERVATION *Reservations,
    size_t ReservationCount,
    BC250_CM_RESOURCE_STATUS *CmStatus,
    BC250_RESOURCE_STATUS *ResourceStatus,
    BC250_OWNED_ADAPTER_STATUS *OwnedStatus,
    BC250_CP_BOOTSTRAP_STATUS *BootstrapStatus,
    BC250_VRAM_STATUS *VramStatus)
{
    BC250_OWNED_ADAPTER_INPUT input;
    BC250_CM_RESOURCE_STATUS cmStatus;
    BC250_OWNED_ADAPTER_STATUS ownedStatus;
    size_t resolvedCount = 0;

    if (CmStatus != NULL) *CmStatus = BC250_CM_RESOURCE_BAD_ARGUMENT;
    if (ResourceStatus != NULL) *ResourceStatus = BC250_RESOURCE_BAD_ARGUMENT;
    if (OwnedStatus != NULL) *OwnedStatus = BC250_OWNED_ADAPTER_BAD_ARGUMENT;
    if (BootstrapStatus != NULL) {
        *BootstrapStatus = BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_BAD_ARGUMENT;
    if (Context == NULL || TranslatedResourceList == NULL || Bars == NULL ||
        BarCount == 0 || Topology == NULL ||
        (ReservationCount != 0 && Reservations == NULL)) {
        return BC250_START_DEVICE_BAD_ARGUMENT;
    }
    if (Context->State != BC250_START_DEVICE_EMPTY) {
        return BC250_START_DEVICE_WRONG_STATE;
    }
    if (Topology->RuntimeEvidenceFlags !=
        BC250_START_RUNTIME_EVIDENCE_REQUIRED) {
        return BC250_START_DEVICE_MISSING_RUNTIME_EVIDENCE;
    }

    cmStatus = Bc250ResolveCmTranslatedBars(
        TranslatedResourceList, Bars, BarCount, Context->ResolvedBars,
        BC250_RESOURCE_MAX_BARS, &resolvedCount, ResourceStatus);
    if (CmStatus != NULL) *CmStatus = cmStatus;
    if (cmStatus != BC250_CM_RESOURCE_OK) {
        Bc250StartDeviceClear(Context);
        return BC250_START_DEVICE_RESOURCES_REJECTED;
    }
    Context->ResolvedBarCount = (uint32_t)resolvedCount;

    RtlZeroMemory(&input, sizeof(input));
    input.ResolvedBars = Context->ResolvedBars;
    input.ResolvedBarCount = resolvedCount;
    input.VramApertureBarIndex = Topology->VramApertureBarIndex;
    input.OwnershipEvidenceFlags = Topology->RuntimeEvidenceFlags |
        BC250_OWNERSHIP_TRANSLATED_RESOURCES |
        BC250_OWNERSHIP_PCI_BAR_IDENTITY;
    input.VramTotalBytes = Topology->VramTotalBytes;
    input.VramMcStart = Topology->VramMcStart;
    input.VramMcEnd = Topology->VramMcEnd;
    input.VramBaseOffset = Topology->VramBaseOffset;
    input.SnapshotGeneration = Topology->SnapshotGeneration;
    ownedStatus = Bc250BuildOwnedAdapterBootstrap(
        &input, Reservations, ReservationCount, &Context->Owned,
        BootstrapStatus, VramStatus);
    if (OwnedStatus != NULL) *OwnedStatus = ownedStatus;
    if (ownedStatus != BC250_OWNED_ADAPTER_OK) {
        Bc250StartDeviceClear(Context);
        return BC250_START_DEVICE_OWNERSHIP_REJECTED;
    }

    Context->State = BC250_START_DEVICE_OWNED_SEALED;
    if (CmStatus != NULL) *CmStatus = BC250_CM_RESOURCE_OK;
    if (ResourceStatus != NULL) *ResourceStatus = BC250_RESOURCE_OK;
    if (OwnedStatus != NULL) *OwnedStatus = BC250_OWNED_ADAPTER_OK;
    if (BootstrapStatus != NULL) *BootstrapStatus = BC250_CP_BOOTSTRAP_OK;
    if (VramStatus != NULL) *VramStatus = BC250_VRAM_OK;
    return BC250_START_DEVICE_OK;
}

BC250_START_DEVICE_STATUS Bc250StartDeviceBuildReadonlyPreflight(
    BC250_START_DEVICE_CONTEXT *Context,
    const BC250_CP_BOOTSTRAP_POLICY *Policy,
    const BC250_CP_FIRMWARE_ENTRY *Manifest,
    uint32_t ManifestCount,
    BC250_VRAM_STATUS *VramStatus,
    BC250_ADDRESS_STATUS *AddressStatus,
    BC250_PSP_CONTRACT_STATUS *PspStatus,
    BC250_CP_BOOTSTRAP_STATUS *BootstrapStatus)
{
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;

    if (BootstrapStatus != NULL) {
        *BootstrapStatus = BC250_CP_BOOTSTRAP_BAD_ARGUMENT;
    }
    if (Context == NULL || Policy == NULL || Manifest == NULL) {
        return BC250_START_DEVICE_BAD_ARGUMENT;
    }
    if (Context->State != BC250_START_DEVICE_OWNED_SEALED) {
        return BC250_START_DEVICE_WRONG_STATE;
    }
    bootstrapStatus = Bc250CpBootstrapBuildPreflight(
        &Context->Owned.Bootstrap, Policy, Manifest, ManifestCount,
        &Context->Preflight, VramStatus, AddressStatus, PspStatus);
    if (BootstrapStatus != NULL) *BootstrapStatus = bootstrapStatus;
    if (bootstrapStatus != BC250_CP_BOOTSTRAP_OK) {
        RtlZeroMemory(&Context->Preflight, sizeof(Context->Preflight));
        return BC250_START_DEVICE_PREFLIGHT_REJECTED;
    }
    Context->State = BC250_START_DEVICE_PREFLIGHT_READY;
    return BC250_START_DEVICE_OK;
}

BC250_START_DEVICE_STATUS Bc250StartDeviceTeardown(
    BC250_START_DEVICE_CONTEXT *Context)
{
    if (Context == NULL) return BC250_START_DEVICE_BAD_ARGUMENT;
    Bc250StartDeviceClear(Context);
    return BC250_START_DEVICE_OK;
}

const char *Bc250StartDeviceStatusName(BC250_START_DEVICE_STATUS Status)
{
    switch (Status) {
    case BC250_START_DEVICE_OK: return "ok";
    case BC250_START_DEVICE_BAD_ARGUMENT: return "bad_argument";
    case BC250_START_DEVICE_WRONG_STATE: return "wrong_state";
    case BC250_START_DEVICE_MISSING_RUNTIME_EVIDENCE:
        return "missing_runtime_evidence";
    case BC250_START_DEVICE_RESOURCES_REJECTED: return "resources_rejected";
    case BC250_START_DEVICE_OWNERSHIP_REJECTED: return "ownership_rejected";
    case BC250_START_DEVICE_PREFLIGHT_REJECTED: return "preflight_rejected";
    default: return "unknown";
    }
}
