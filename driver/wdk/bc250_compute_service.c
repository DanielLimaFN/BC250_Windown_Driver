#include "bc250_compute_service.h"
#include <ntddk.h>

#define BC250_PCI_VENDOR_ID 0x1002u
#define BC250_PCI_DEVICE_ID 0x13FEu

static PVOID g_MmioBase = NULL;
static PHYSICAL_ADDRESS g_MmioPhysBase;
static ULONG g_MmioLength = 0x80000u; /* 512 KiB */

static DRIVER_UNLOAD Bc250ComputeUnload;
static DRIVER_DISPATCH Bc250ComputeCreateClose;
static DRIVER_DISPATCH Bc250ComputeDeviceControl;

static ULONG Bc250ReadSmn(ULONG SmnAddress)
{
    if (g_MmioBase == NULL) return 0xFFFFFFFFu;
    /* 0x0000 = mmPCIE_INDEX, 0x0004 = mmPCIE_DATA */
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000u) = SmnAddress;
    return *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0004u);
}

static void Bc250WriteSmn(ULONG SmnAddress, ULONG Value)
{
    if (g_MmioBase == NULL) return;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0000u) = SmnAddress;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + 0x0004u) = Value;
}

static NTSTATUS Bc250ComputeCreateClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250ComputeDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    ULONG controlCode;
    PVOID buffer;
    ULONG inLength;
    ULONG outLength;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR info = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    stack = IoGetCurrentIrpStackLocation(Irp);
    controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    buffer = Irp->AssociatedIrp.SystemBuffer;
    inLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLength = stack->Parameters.DeviceIoControl.OutputBufferLength;

    switch (controlCode) {
    case IOCTL_BC250_GET_HARDWARE_INFO: {
        PBC250_HARDWARE_INFO hwInfo;
        if (buffer == NULL || outLength < sizeof(BC250_HARDWARE_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        hwInfo = (PBC250_HARDWARE_INFO)buffer;
        RtlZeroMemory(hwInfo, sizeof(BC250_HARDWARE_INFO));
        hwInfo->CpuCoreCount = 8;
        hwInfo->GpuCuCount = 24;
        hwInfo->GpuWgpCount = 12;
        hwInfo->VramTotalMiB = 512;
        hwInfo->CpuVisibleMiB = 256;
        hwInfo->RebarSupportedMask = 0x700;
        hwInfo->RebarSelectedMiB = 256;
        hwInfo->MmioPhysicalBase = 0xFE800000u;
        hwInfo->MmioLength = g_MmioLength;
        hwInfo->GfxoffState = 0;
        hwInfo->GpuClockMhz = 2000;
        hwInfo->MemoryClockMhz = 1750;
        RtlCopyMemory(hwInfo->SiliconName, "AMD BC-250 (Oberon RDNA2)", 26);
        info = sizeof(BC250_HARDWARE_INFO);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_READ_MMIO_REG: {
        PBC250_MMIO_REG_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_MMIO_REG_REQ) ||
            outLength < sizeof(BC250_MMIO_REG_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_MMIO_REG_REQ)buffer;
        if (g_MmioBase == NULL) {
            req->Status = 2;
            req->Value = 0xFFFFFFFFu;
        } else if (req->Offset >= g_MmioLength || (req->Offset & 3u) != 0) {
            req->Status = 1;
            req->Value = 0xFFFFFFFFu;
        } else {
            req->Value = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + req->Offset);
            req->Status = 0;
        }
        info = sizeof(BC250_MMIO_REG_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_WRITE_MMIO_REG: {
        PBC250_MMIO_REG_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_MMIO_REG_REQ) ||
            outLength < sizeof(BC250_MMIO_REG_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_MMIO_REG_REQ)buffer;
        if (g_MmioBase == NULL) {
            req->Status = 2;
        } else if (req->Offset >= g_MmioLength || (req->Offset & 3u) != 0) {
            req->Status = 1;
        } else {
            *(volatile ULONG *)((ULONG_PTR)g_MmioBase + req->Offset) = req->Value;
            req->Value = *(volatile ULONG *)((ULONG_PTR)g_MmioBase + req->Offset);
            req->Status = 0;
        }
        info = sizeof(BC250_MMIO_REG_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_READ_SMN_REG: {
        PBC250_SMN_REG_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_SMN_REG_REQ) ||
            outLength < sizeof(BC250_SMN_REG_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_SMN_REG_REQ)buffer;
        req->Value = Bc250ReadSmn(req->SmnAddress);
        req->Status = 0;
        info = sizeof(BC250_SMN_REG_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_WRITE_SMN_REG: {
        PBC250_SMN_REG_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_SMN_REG_REQ) ||
            outLength < sizeof(BC250_SMN_REG_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_SMN_REG_REQ)buffer;
        Bc250WriteSmn(req->SmnAddress, req->Value);
        req->Value = Bc250ReadSmn(req->SmnAddress);
        req->Status = 0;
        info = sizeof(BC250_SMN_REG_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_WAKEUP_GFX_CORE: {
        PBC250_WAKEUP_REQ req;
        if (buffer == NULL || inLength < sizeof(BC250_WAKEUP_REQ) ||
            outLength < sizeof(BC250_WAKEUP_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        req = (PBC250_WAKEUP_REQ)buffer;
        
        /* 1. Lê estado inicial do SMN */
        req->InitialGfxoffState = Bc250ReadSmn(0x03B10524u); /* GFXOFF_STATUS */

        /* 2. Envia mensagem para SMU / RLC desativar GFXOFF */
        Bc250WriteSmn(0x03B10A20u, 0x15u); /* PPSMC_MSG_DisallowGfxOff */
        
        /* 3. Aguarda resposta */
        req->SmuResponse = Bc250ReadSmn(0x03B10A24u); /* SMU response */
        req->FinalGfxoffState = Bc250ReadSmn(0x03B10524u);
        req->Success = 1;

        info = sizeof(BC250_WAKEUP_REQ);
        status = STATUS_SUCCESS;
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static VOID Bc250ComputeUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING dosDeviceName;

    if (g_MmioBase != NULL) {
        MmUnmapIoSpace(g_MmioBase, g_MmioLength);
        g_MmioBase = NULL;
    }

    RtlInitUnicodeString(&dosDeviceName, BC250_COMPUTE_DOS_DEVICE_NAME);
    IoDeleteSymbolicLink(&dosDeviceName);

    if (DriverObject->DeviceObject != NULL) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING deviceName;
    UNICODE_STRING dosDeviceName;
    PDEVICE_OBJECT deviceObject = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    /* Habilita barramento PCI */
    {
        USHORT pciCmd = 0;
        ULONG bytes = HalGetBusDataByOffset(
            PCIConfiguration,
            1,
            0,
            &pciCmd,
            4,
            sizeof(pciCmd));

        if (bytes == sizeof(pciCmd)) {
            pciCmd |= 0x0006u;
            (void)HalSetBusDataByOffset(
                PCIConfiguration,
                1,
                0,
                &pciCmd,
                4,
                sizeof(pciCmd));
        }
    }

    RtlInitUnicodeString(&deviceName, BC250_COMPUTE_DEVICE_NAME);
    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&dosDeviceName, BC250_COMPUTE_DOS_DEVICE_NAME);
    status = IoDeleteSymbolicLink(&dosDeviceName);
    status = IoCreateSymbolicLink(&dosDeviceName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = Bc250ComputeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Bc250ComputeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Bc250ComputeDeviceControl;
    DriverObject->DriverUnload = Bc250ComputeUnload;

    g_MmioPhysBase.QuadPart = 0xFE800000ll;
    g_MmioBase = MmMapIoSpace(g_MmioPhysBase, g_MmioLength, MmNonCached);

    deviceObject->Flags |= DO_BUFFERED_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}
