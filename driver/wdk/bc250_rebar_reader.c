#include "bc250_rebar_reader.h"

static BC250_REBAR_STATUS Bc250ReadConfigExact(
    const DXGKRNL_INTERFACE *Interface,
    ULONG Offset,
    PVOID Buffer,
    ULONG Length,
    uint32_t *ReadCount,
    NTSTATUS *CallbackStatus)
{
    ULONG bytesRead = 0;
    NTSTATUS status = Interface->DxgkCbReadDeviceSpace(
        Interface->DeviceHandle, DXGK_WHICHSPACE_CONFIG, Buffer, Offset,
        Length, &bytesRead);
    ++*ReadCount;
    if (CallbackStatus != NULL) *CallbackStatus = status;
    if (!NT_SUCCESS(status)) return BC250_REBAR_CALLBACK_FAILED;
    return bytesRead == Length ? BC250_REBAR_OK : BC250_REBAR_PARTIAL_READ;
}

static BC250_REBAR_STATUS Bc250FindCapability(
    const DXGKRNL_INTERFACE *Interface,
    uint32_t *CapabilityOffset,
    uint32_t *ReadCount,
    NTSTATUS *CallbackStatus)
{
    uint32_t visited[BC250_REBAR_MAX_CHAIN];
    uint32_t visitedCount = 0;
    uint32_t offset = BC250_REBAR_CONFIG_START;

    while (offset != 0 && visitedCount < BC250_REBAR_MAX_CHAIN) {
        uint32_t header = 0;
        uint32_t next;
        uint32_t i;
        BC250_REBAR_STATUS status;

        if (offset < BC250_REBAR_CONFIG_START ||
            offset > BC250_REBAR_CONFIG_LIMIT - sizeof(header) ||
            (offset & 3u) != 0) return BC250_REBAR_CHAIN_MALFORMED;
        for (i = 0; i < visitedCount; ++i) {
            if (visited[i] == offset) return BC250_REBAR_CHAIN_LOOP;
        }
        visited[visitedCount++] = offset;
        status = Bc250ReadConfigExact(
            Interface, offset, &header, sizeof(header), ReadCount,
            CallbackStatus);
        if (status != BC250_REBAR_OK) return status;
        if ((header & 0xFFFFu) == PCI_EXPRESS_RESIZABLE_BAR_CAP_ID) {
            *CapabilityOffset = offset;
            return BC250_REBAR_OK;
        }
        next = (header >> 20) & 0xFFFu;
        if (next != 0 && (next < BC250_REBAR_CONFIG_START ||
                          next >= BC250_REBAR_CONFIG_LIMIT ||
                          (next & 3u) != 0)) {
            return BC250_REBAR_CHAIN_MALFORMED;
        }
        offset = next;
    }
    return offset == 0 ? BC250_REBAR_NOT_FOUND : BC250_REBAR_CHAIN_LOOP;
}

BC250_REBAR_STATUS Bc250ReadRebarReadonly(
    const DXGKRNL_INTERFACE *DxgkInterface,
    const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    size_t ResolvedBarCount,
    uint32_t TargetBarIndex,
    BC250_REBAR_EVIDENCE *Evidence,
    NTSTATUS *CallbackStatus)
{
    uint32_t capability[1u + 2u * BC250_RESOURCE_MAX_BARS];
    uint32_t capabilityOffset = 0;
    uint32_t readCount = 0;
    uint32_t entryCount;
    uint32_t targetCount = 0;
    uint32_t selectedEncoding = 0;
    uint32_t supportedMask = 0;
    uint64_t assignedBytes = 0;
    uint64_t selectedBytes;
    size_t assignedCount = 0;
    size_t i;
    BC250_REBAR_STATUS status;
    const SIZE_T minimumInterfaceBytes =
        FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbReadDeviceSpace) +
        sizeof(DxgkInterface->DxgkCbReadDeviceSpace);

    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    if (CallbackStatus != NULL) *CallbackStatus = STATUS_INVALID_PARAMETER;
    if (DxgkInterface == NULL || ResolvedBars == NULL ||
        ResolvedBarCount == 0 || Evidence == NULL ||
        TargetBarIndex >= BC250_RESOURCE_MAX_BARS) return BC250_REBAR_BAD_ARGUMENT;
    if (DxgkInterface->Size < minimumInterfaceBytes ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbReadDeviceSpace == NULL) {
        return BC250_REBAR_INTERFACE_TOO_SMALL;
    }

    status = Bc250FindCapability(
        DxgkInterface, &capabilityOffset, &readCount, CallbackStatus);
    if (status != BC250_REBAR_OK) return status;
    if (capabilityOffset > BC250_REBAR_CONFIG_LIMIT - 3u * sizeof(uint32_t)) {
        return BC250_REBAR_CAPABILITY_MALFORMED;
    }
    RtlZeroMemory(capability, sizeof(capability));
    status = Bc250ReadConfigExact(
        DxgkInterface, capabilityOffset, capability, 3u * sizeof(uint32_t),
        &readCount, CallbackStatus);
    if (status != BC250_REBAR_OK) return status;
    entryCount = (capability[2] >> 5) & 7u;
    if (entryCount == 0 || entryCount > BC250_RESOURCE_MAX_BARS) {
        return BC250_REBAR_CAPABILITY_MALFORMED;
    }
    if (entryCount > 1u) {
        ULONG capabilityBytes =
            (1u + 2u * entryCount) * sizeof(uint32_t);
        if (capabilityOffset > BC250_REBAR_CONFIG_LIMIT - capabilityBytes) {
            return BC250_REBAR_CAPABILITY_MALFORMED;
        }
        status = Bc250ReadConfigExact(
            DxgkInterface, capabilityOffset, capability,
            capabilityBytes, &readCount,
            CallbackStatus);
        if (status != BC250_REBAR_OK) return status;
    }
    for (i = 0; i < entryCount; ++i) {
        uint32_t cap = capability[1u + 2u * i];
        uint32_t control = capability[2u + 2u * i];
        uint32_t barIndex = control & 7u;
        if (barIndex >= BC250_RESOURCE_MAX_BARS) {
            return BC250_REBAR_CAPABILITY_MALFORMED;
        }
        if (barIndex == TargetBarIndex) {
            ++targetCount;
            supportedMask = (cap >> 4) & 0xFFFFFu;
            selectedEncoding = (control >> 8) & 0x3Fu;
        }
    }
    if (targetCount == 0) return BC250_REBAR_TARGET_NOT_FOUND;
    if (targetCount != 1) return BC250_REBAR_DUPLICATE_TARGET;
    if (selectedEncoding >= 20u ||
        (supportedMask & (1u << selectedEncoding)) == 0) {
        return BC250_REBAR_SELECTED_UNSUPPORTED;
    }
    selectedBytes = 1ull << (20u + selectedEncoding);
    for (i = 0; i < ResolvedBarCount; ++i) {
        if (ResolvedBars[i].Index == TargetBarIndex) {
            ++assignedCount;
            assignedBytes = ResolvedBars[i].Length;
        }
    }
    if (assignedCount != 1) return BC250_REBAR_ASSIGNED_NOT_UNIQUE;
    if (assignedBytes != selectedBytes) return BC250_REBAR_SIZE_MISMATCH;

    Evidence->CapabilityOffset = capabilityOffset;
    Evidence->BarIndex = TargetBarIndex;
    Evidence->SizesSupportedMask = supportedMask;
    Evidence->SelectedSizeEncoding = selectedEncoding;
    Evidence->SelectedBytes = selectedBytes;
    Evidence->AssignedBytes = assignedBytes;
    Evidence->EntryCount = entryCount;
    Evidence->ReadCount = readCount;
    return BC250_REBAR_OK;
}

const char *Bc250RebarStatusName(BC250_REBAR_STATUS Status)
{
    switch (Status) {
    case BC250_REBAR_OK: return "ok";
    case BC250_REBAR_BAD_ARGUMENT: return "bad_argument";
    case BC250_REBAR_INTERFACE_TOO_SMALL: return "interface_too_small";
    case BC250_REBAR_CALLBACK_FAILED: return "callback_failed";
    case BC250_REBAR_PARTIAL_READ: return "partial_read";
    case BC250_REBAR_CHAIN_MALFORMED: return "chain_malformed";
    case BC250_REBAR_CHAIN_LOOP: return "chain_loop";
    case BC250_REBAR_NOT_FOUND: return "not_found";
    case BC250_REBAR_CAPABILITY_MALFORMED: return "capability_malformed";
    case BC250_REBAR_TARGET_NOT_FOUND: return "target_not_found";
    case BC250_REBAR_DUPLICATE_TARGET: return "duplicate_target";
    case BC250_REBAR_SELECTED_UNSUPPORTED: return "selected_unsupported";
    case BC250_REBAR_ASSIGNED_NOT_UNIQUE: return "assigned_not_unique";
    case BC250_REBAR_SIZE_MISMATCH: return "size_mismatch";
    default: return "unknown";
    }
}
