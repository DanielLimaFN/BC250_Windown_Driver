#include "bc250_pci_bar_reader.h"

static BC250_PCI_BAR_READ_STATUS Bc250DecodePciBars(
    const PCI_COMMON_CONFIG *Config,
    BC250_PCI_BAR_RESOURCE *Bars,
    size_t BarCapacity,
    size_t *BarCount)
{
    size_t count = 0;
    uint32_t index;

    if (Config->VendorID != BC250_PCI_VENDOR_ID ||
        Config->DeviceID != BC250_PCI_DEVICE_ID) {
        return BC250_PCI_BAR_READ_WRONG_DEVICE;
    }
    if ((Config->HeaderType & 0x7Fu) != PCI_DEVICE_TYPE) {
        return BC250_PCI_BAR_READ_UNSUPPORTED_HEADER;
    }
    for (index = 0; index < BC250_RESOURCE_MAX_BARS; ++index) {
        uint32_t raw = Config->u.type0.BaseAddresses[index];
        uint64_t base;
        uint32_t memoryType;

        if (raw == 0) continue;
        if (raw == 0xFFFFFFFFu) return BC250_PCI_BAR_READ_FILL_VALUE;
        if ((raw & PCI_ADDRESS_IO_SPACE) != 0) {
            /* VGA-class adapters may expose a legacy I/O BAR.  This reader's
             * output contract contains memory resources only, so preserve the
             * BAR index and continue scanning the remaining memory BARs. */
            continue;
        }
        memoryType = raw & PCI_ADDRESS_MEMORY_TYPE_MASK;
        base = (uint64_t)(raw & PCI_ADDRESS_MEMORY_ADDRESS_MASK);
        if (memoryType == PCI_TYPE_64BIT) {
            uint32_t upper;
            if (index + 1u >= BC250_RESOURCE_MAX_BARS) {
                return BC250_PCI_BAR_READ_TRUNCATED_64BIT;
            }
            upper = Config->u.type0.BaseAddresses[index + 1u];
            if (upper == 0xFFFFFFFFu) return BC250_PCI_BAR_READ_FILL_VALUE;
            base |= (uint64_t)upper << 32;
            ++index;
        } else if (memoryType != PCI_TYPE_32BIT) {
            return BC250_PCI_BAR_READ_UNSUPPORTED_MEMORY_TYPE;
        }
        if (base == 0) return BC250_PCI_BAR_READ_UNASSIGNED;
        if (count >= BarCapacity) return BC250_PCI_BAR_READ_OUTPUT_TOO_SMALL;
        Bars[count].Index = index - (memoryType == PCI_TYPE_64BIT ? 1u : 0u);
        Bars[count].Present = 1;
        Bars[count].Base = base;
        ++count;
    }
    if (count == 0) return BC250_PCI_BAR_READ_UNASSIGNED;
    *BarCount = count;
    return BC250_PCI_BAR_READ_OK;
}

BC250_PCI_BAR_READ_STATUS Bc250ReadPciBarsReadonly(
    const DXGKRNL_INTERFACE *DxgkInterface,
    BC250_PCI_BAR_RESOURCE *Bars,
    size_t BarCapacity,
    size_t *BarCount,
    ULONG *BytesRead,
    NTSTATUS *CallbackStatus)
{
    PCI_COMMON_CONFIG config;
    BC250_PCI_BAR_READ_STATUS decodeStatus;
    NTSTATUS status;
    ULONG bytesRead = 0;
    const SIZE_T minimumInterfaceBytes =
        FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbReadDeviceSpace) +
        sizeof(DxgkInterface->DxgkCbReadDeviceSpace);

    if (BarCount != NULL) *BarCount = 0;
    if (BytesRead != NULL) *BytesRead = 0;
    if (CallbackStatus != NULL) *CallbackStatus = STATUS_INVALID_PARAMETER;
    if (DxgkInterface == NULL || Bars == NULL || BarCount == NULL ||
        BarCapacity == 0) return BC250_PCI_BAR_READ_BAD_ARGUMENT;
    RtlZeroMemory(Bars, BarCapacity * sizeof(*Bars));
    if (DxgkInterface->Size < minimumInterfaceBytes ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbReadDeviceSpace == NULL) {
        return BC250_PCI_BAR_READ_INTERFACE_TOO_SMALL;
    }

    RtlZeroMemory(&config, sizeof(config));
    status = DxgkInterface->DxgkCbReadDeviceSpace(
        DxgkInterface->DeviceHandle, DXGK_WHICHSPACE_CONFIG, &config, 0,
        PCI_COMMON_HDR_LENGTH, &bytesRead);
    if (BytesRead != NULL) *BytesRead = bytesRead;
    if (CallbackStatus != NULL) *CallbackStatus = status;
    if (!NT_SUCCESS(status)) return BC250_PCI_BAR_READ_CALLBACK_FAILED;
    if (bytesRead != PCI_COMMON_HDR_LENGTH) return BC250_PCI_BAR_READ_PARTIAL;
    decodeStatus = Bc250DecodePciBars(
        &config, Bars, BarCapacity, BarCount);
    if (decodeStatus != BC250_PCI_BAR_READ_OK) {
        RtlZeroMemory(Bars, BarCapacity * sizeof(*Bars));
        *BarCount = 0;
    }
    return decodeStatus;
}

const char *Bc250PciBarReadStatusName(BC250_PCI_BAR_READ_STATUS Status)
{
    switch (Status) {
    case BC250_PCI_BAR_READ_OK: return "ok";
    case BC250_PCI_BAR_READ_BAD_ARGUMENT: return "bad_argument";
    case BC250_PCI_BAR_READ_INTERFACE_TOO_SMALL: return "interface_too_small";
    case BC250_PCI_BAR_READ_CALLBACK_FAILED: return "callback_failed";
    case BC250_PCI_BAR_READ_PARTIAL: return "partial";
    case BC250_PCI_BAR_READ_WRONG_DEVICE: return "wrong_device";
    case BC250_PCI_BAR_READ_UNSUPPORTED_HEADER: return "unsupported_header";
    case BC250_PCI_BAR_READ_FILL_VALUE: return "fill_value";
    case BC250_PCI_BAR_READ_IO_BAR: return "io_bar";
    case BC250_PCI_BAR_READ_UNSUPPORTED_MEMORY_TYPE:
        return "unsupported_memory_type";
    case BC250_PCI_BAR_READ_TRUNCATED_64BIT: return "truncated_64bit";
    case BC250_PCI_BAR_READ_UNASSIGNED: return "unassigned";
    case BC250_PCI_BAR_READ_OUTPUT_TOO_SMALL: return "output_too_small";
    default: return "unknown";
    }
}
