#ifndef BC250_PCI_BAR_READER_H
#define BC250_PCI_BAR_READER_H

#include <ntddk.h>
#include <dispmprt.h>

#include "../core/bc250_resource_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_PCI_VENDOR_ID 0x1002u
#define BC250_PCI_DEVICE_ID 0x13FEu
#define BC250_PCI_VRAM_APERTURE_BAR_INDEX 0u
#define BC250_PCI_MMIO_BAR_INDEX          5u

typedef enum BC250_PCI_BAR_READ_STATUS {
    BC250_PCI_BAR_READ_OK = 0,
    BC250_PCI_BAR_READ_BAD_ARGUMENT,
    BC250_PCI_BAR_READ_INTERFACE_TOO_SMALL,
    BC250_PCI_BAR_READ_CALLBACK_FAILED,
    BC250_PCI_BAR_READ_PARTIAL,
    BC250_PCI_BAR_READ_WRONG_DEVICE,
    BC250_PCI_BAR_READ_UNSUPPORTED_HEADER,
    BC250_PCI_BAR_READ_FILL_VALUE,
    BC250_PCI_BAR_READ_IO_BAR,
    BC250_PCI_BAR_READ_UNSUPPORTED_MEMORY_TYPE,
    BC250_PCI_BAR_READ_TRUNCATED_64BIT,
    BC250_PCI_BAR_READ_UNASSIGNED,
    BC250_PCI_BAR_READ_OUTPUT_TOO_SMALL
} BC250_PCI_BAR_READ_STATUS;

BC250_PCI_BAR_READ_STATUS Bc250ReadPciBarsReadonly(
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _Out_writes_to_(BarCapacity, *BarCount) BC250_PCI_BAR_RESOURCE *Bars,
    _In_ size_t BarCapacity,
    _Out_ size_t *BarCount,
    _Out_opt_ ULONG *BytesRead,
    _Out_opt_ NTSTATUS *CallbackStatus);

const char *Bc250PciBarReadStatusName(BC250_PCI_BAR_READ_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
