#include "bc250_compute_service.h"
#include "../core/bc250_smu_telemetry_contract.h"
#include "../core/bc250_superio_nct6686_contract.h"
#include <ntddk.h>

#define BC250_PCI_VENDOR_ID 0x1002u
#define BC250_PCI_DEVICE_ID 0x13FEu

static PVOID g_MmioBase = NULL;
static PHYSICAL_ADDRESS g_MmioPhysBase;
static ULONG g_MmioLength = 0x80000u; /* 512 KiB */
int _fltused = 0x9875;

static DRIVER_UNLOAD Bc250ComputeUnload;
static DRIVER_DISPATCH Bc250ComputeCreateClose;
static DRIVER_DISPATCH Bc250ComputeDeviceControl;

static uint8_t KernelIsaInByte(void *Context, uint16_t Port)
{
    UNREFERENCED_PARAMETER(Context);
    return READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port);
}

static void KernelIsaOutByte(void *Context, uint16_t Port, uint8_t Value)
{
    UNREFERENCED_PARAMETER(Context);
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port, Value);
}

static uint32_t KernelSmuRead32(void *Context, uint32_t ByteOffset)
{
    UNREFERENCED_PARAMETER(Context);
    if (g_MmioBase == NULL || ByteOffset >= g_MmioLength) return 0xFFFFFFFFu;
    return *(volatile ULONG *)((ULONG_PTR)g_MmioBase + ByteOffset);
}

static void KernelSmuWrite32(void *Context, uint32_t ByteOffset, uint32_t Value)
{
    UNREFERENCED_PARAMETER(Context);
    if (g_MmioBase == NULL || ByteOffset >= g_MmioLength) return;
    *(volatile ULONG *)((ULONG_PTR)g_MmioBase + ByteOffset) = Value;
}

static void KernelSmuStallUs(void *Context, uint32_t Microseconds)
{
    UNREFERENCED_PARAMETER(Context);
    KeStallExecutionProcessor(Microseconds);
}

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
        /* Do not manufacture a topology or clocks.  The active MCDM miniport
         * owns PCI resource discovery and is the only supported live backend. */
        hwInfo->MmioLength = g_MmioLength;
        RtlCopyMemory(hwInfo->SiliconName,
                      "BC250 physical backend unavailable",
                      sizeof("BC250 physical backend unavailable"));
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

    case IOCTL_BC250_ALLOC_VRAM: {
        PBC250_ALLOC_VRAM_REQ allocReq;
        if (buffer == NULL || inLength < sizeof(BC250_ALLOC_VRAM_REQ) ||
            outLength < sizeof(BC250_ALLOC_VRAM_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        allocReq = (PBC250_ALLOC_VRAM_REQ)buffer;
        UNREFERENCED_PARAMETER(allocReq);
        /* A CPU pool must never be presented as GPU VRAM. */
        allocReq->AllocatedGpuVa = 0u;
        allocReq->Success = 0u;
        info = sizeof(BC250_ALLOC_VRAM_REQ);
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    case IOCTL_BC250_FREE_VRAM: {
        PBC250_FREE_VRAM_REQ freeReq;
        if (buffer == NULL || inLength < sizeof(BC250_FREE_VRAM_REQ) ||
            outLength < sizeof(BC250_FREE_VRAM_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        freeReq = (PBC250_FREE_VRAM_REQ)buffer;
        freeReq->Success = 0u;
        info = sizeof(BC250_FREE_VRAM_REQ);
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    case IOCTL_BC250_SDMA_COPY: {
        PBC250_SDMA_COPY_REQ copyReq;
        if (buffer == NULL || inLength < sizeof(BC250_SDMA_COPY_REQ) ||
            outLength < sizeof(BC250_SDMA_COPY_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        copyReq = (PBC250_SDMA_COPY_REQ)buffer;
        RtlZeroMemory(copyReq, sizeof(*copyReq));
        info = sizeof(BC250_SDMA_COPY_REQ);
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    case IOCTL_BC250_DISPATCH_COMPUTE: {
        PBC250_COMPUTE_DISPATCH_REQ dispReq;
        if (buffer == NULL || inLength < sizeof(BC250_COMPUTE_DISPATCH_REQ) ||
            outLength < sizeof(BC250_COMPUTE_DISPATCH_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        dispReq = (PBC250_COMPUTE_DISPATCH_REQ)buffer;
        RtlZeroMemory(dispReq, sizeof(*dispReq));
        info = sizeof(BC250_COMPUTE_DISPATCH_REQ);
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    case IOCTL_BC250_GET_TELEMETRY: {
        BC250_PLATFORM_TELEMETRY_SNAPSHOT *snapshot;
        BC250_SMU_TRANSPORT smuTrans;
        BC250_ISA_PORT_TRANSPORT isaTrans;
        LARGE_INTEGER qpc;
        uint16_t chipId = 0, baseAddr = 0;

        if (buffer == NULL || outLength < sizeof(BC250_PLATFORM_TELEMETRY_SNAPSHOT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        snapshot = (BC250_PLATFORM_TELEMETRY_SNAPSHOT *)buffer;
        RtlZeroMemory(snapshot, sizeof(*snapshot));

        qpc = KeQueryPerformanceCounter(NULL);
        snapshot->TimestampQpc = (uint64_t)qpc.QuadPart;

        /* 1. Coleta SMU */
        smuTrans.Read32 = KernelSmuRead32;
        smuTrans.Write32 = KernelSmuWrite32;
        smuTrans.StallUs = KernelSmuStallUs;
        smuTrans.Context = NULL;

        if (g_MmioBase != NULL) {
            uint32_t smuVer = 0, drvIfVer = 0, gfxFreq = 0, gfxVid = 0;
            if (Bc250SmuGetVersion(&smuTrans, &smuVer) == BC250_SMU_STATUS_OK) {
                snapshot->GpuMetrics.SmuFirmwareVersion = smuVer;
                snapshot->GpuMetrics.Valid = 1u;
            }
            if (Bc250SmuGetDriverIfVersion(&smuTrans, &drvIfVer) == BC250_SMU_STATUS_OK) {
                snapshot->GpuMetrics.DriverIfVersion = drvIfVer;
            }
            if (Bc250SmuGetGfxFrequency(&smuTrans, &gfxFreq) == BC250_SMU_STATUS_OK) {
                snapshot->GpuMetrics.GfxclkFrequencyMhz = gfxFreq;
            }
            if (Bc250SmuGetGfxVid(&smuTrans, &gfxVid) == BC250_SMU_STATUS_OK) {
                snapshot->GpuMetrics.GfxVoltageMv = gfxVid;
            }
        }

        /* 2. Coleta Super I/O NCT6686D */
        isaTrans.InByte = KernelIsaInByte;
        isaTrans.OutByte = KernelIsaOutByte;
        isaTrans.Context = NULL;

        if (Bc250Nct6686Detect(&isaTrans, &chipId, &baseAddr) == BC250_SUPERIO_STATUS_OK) {
            (void)Bc250Nct6686ReadSensors(&isaTrans, baseAddr, &snapshot->MotherboardMetrics);
            snapshot->MotherboardMetrics.ChipId = chipId;
            snapshot->MotherboardMetrics.BaseAddress = baseAddr;
        }

        snapshot->TelemetryHealthy = 1u;
        info = sizeof(BC250_PLATFORM_TELEMETRY_SNAPSHOT);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250_SET_FAN_PWM: {
        PBC250_SET_FAN_PWM_REQ fanReq;
        BC250_ISA_PORT_TRANSPORT isaTrans;
        uint16_t chipId = 0, baseAddr = 0;

        if (buffer == NULL || inLength < sizeof(BC250_SET_FAN_PWM_REQ) ||
            outLength < sizeof(BC250_SET_FAN_PWM_REQ)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        fanReq = (PBC250_SET_FAN_PWM_REQ)buffer;
        fanReq->Success = 0u;

        isaTrans.InByte = KernelIsaInByte;
        isaTrans.OutByte = KernelIsaOutByte;
        isaTrans.Context = NULL;

        if (Bc250Nct6686Detect(&isaTrans, &chipId, &baseAddr) == BC250_SUPERIO_STATUS_OK) {
            if (Bc250Nct6686SetFanPwm(&isaTrans, baseAddr, (uint8_t)fanReq->FanIndex, (uint8_t)fanReq->PwmValue) == BC250_SUPERIO_STATUS_OK) {
                fanReq->Success = 1u;
            }
        }

        info = sizeof(BC250_SET_FAN_PWM_REQ);
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
