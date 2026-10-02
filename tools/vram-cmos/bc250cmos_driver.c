/*
 * bc250cmos.sys -- driver minimo de laboratorio para ler e gravar o bloco de
 * memoria (APCB) da CMOS da BC-250 pelas portas 0x72 (indice) / 0x73 (dado).
 *
 * E' o equivalente Windows de referencias/linux/bc250_memcfg.cpp. Nao toca a
 * GPU, nao e' PnP e so' fica carregado enquanto bc250-vram.ps1 roda.
 *
 * Ciclo de vida da gravacao (Lei 5):
 *   PREFLIGHT  pedido com magic/versao/tamanho certos; plano refeito aqui com
 *              Bc250ApcbPlanUmaWrite e exigido IDENTICO ao da ferramenta;
 *   SNAPSHOT   rele 0x90..0xAB e exige IDENTICO ao que a ferramenta viu;
 *   APPLY      grava os 28 bytes;
 *   READBACK   rele os 28 bytes;
 *   ROLLBACK   se o readback divergir, regrava o snapshot e rele de novo.
 * A prova final (a ABL aplicou) so' existe depois de um boot frio: o KMD do
 * BC-250 loga VramTotalMiB lido da placa.
 *
 * Acesso: so' SYSTEM e Administradores (SDDL). Cada par indice/dado roda com
 * spinlock para nao intercalar com outro chamador deste driver.
 */

#include <ntddk.h>
#include <wdmsec.h>
#include "bc250cmos_abi.h"
#include "bc250_fch_reset_reason.h"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD Bc250CmosUnload;
_Dispatch_type_(IRP_MJ_CREATE) _Dispatch_type_(IRP_MJ_CLOSE) DRIVER_DISPATCH Bc250CmosCreateClose;
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH Bc250CmosDeviceControl;

/* {6C1B2F0E-5A3D-4E7B-9C25-0B250C0A0B90} */
static const GUID kBc250CmosClassGuid =
    { 0x6c1b2f0e, 0x5a3d, 0x4e7b, { 0x9c, 0x25, 0x0b, 0x25, 0x0c, 0x0a, 0x0b, 0x90 } };

static KSPIN_LOCK g_Bc250CmosLock;

static UCHAR Bc250CmosReadByteLocked(UCHAR Index)
{
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)BC250_CMOS_INDEX_PORT, Index);
    return READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)BC250_CMOS_DATA_PORT);
}

static VOID Bc250CmosWriteByteLocked(UCHAR Index, UCHAR Value)
{
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)BC250_CMOS_INDEX_PORT, Index);
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)BC250_CMOS_DATA_PORT, Value);
}

static VOID Bc250CmosReadRange(UCHAR First, ULONG Count, UCHAR *Out)
{
    KIRQL irql;
    ULONG i;
    KeAcquireSpinLock(&g_Bc250CmosLock, &irql);
    for (i = 0; i < Count; ++i) {
        Out[i] = Bc250CmosReadByteLocked((UCHAR)(First + i));
    }
    KeReleaseSpinLock(&g_Bc250CmosLock, irql);
}

static VOID Bc250CmosWriteRange(UCHAR First, ULONG Count, const UCHAR *In)
{
    KIRQL irql;
    ULONG i;
    KeAcquireSpinLock(&g_Bc250CmosLock, &irql);
    for (i = 0; i < Count; ++i) {
        Bc250CmosWriteByteLocked((UCHAR)(First + i), In[i]);
    }
    KeReleaseSpinLock(&g_Bc250CmosLock, irql);
}

static NTSTATUS Bc250CmosWriteUma(const BC250CMOS_WRITE_REQUEST *Req, BC250CMOS_WRITE_RESULT *Res)
{
    UCHAR plan[BC250_APCB_BYTES];
    BC250_APCB_STATUS planStatus;

    RtlZeroMemory(Res, sizeof(*Res));
    if (Req->Magic != BC250CMOS_ABI_MAGIC || Req->Version != BC250CMOS_ABI_VERSION) {
        Res->Status = BC250CMOS_WRITE_BAD_REQUEST;
        return STATUS_SUCCESS;
    }

    /* PREFLIGHT: o plano sai do contrato, aqui, e tem de bater com o da ferramenta. */
    planStatus = Bc250ApcbPlanUmaWrite(Req->Expected, BC250_APCB_BYTES, Req->NewUmaMb, plan);
    Res->PlanStatus = (ULONG)planStatus;
    if (planStatus != BC250_APCB_OK) {
        Res->Status = BC250CMOS_WRITE_PLAN_REFUSED;
        return STATUS_SUCCESS;
    }
    if (RtlCompareMemory(plan, Req->Planned, BC250_APCB_BYTES) != BC250_APCB_BYTES) {
        Res->Status = BC250CMOS_WRITE_PLAN_DIVERGED;
        return STATUS_SUCCESS;
    }

    /* SNAPSHOT: o bloco atual tem de ser exatamente o que a ferramenta viu. */
    Bc250CmosReadRange((UCHAR)BC250_APCB_OFFSET, BC250_APCB_BYTES, Res->Before);
    if (RtlCompareMemory(Res->Before, Req->Expected, BC250_APCB_BYTES) != BC250_APCB_BYTES) {
        Res->Status = BC250CMOS_WRITE_STALE;
        return STATUS_SUCCESS;
    }

    /* APPLY + READBACK */
    Bc250CmosWriteRange((UCHAR)BC250_APCB_OFFSET, BC250_APCB_BYTES, plan);
    Bc250CmosReadRange((UCHAR)BC250_APCB_OFFSET, BC250_APCB_BYTES, Res->Readback);
    if (RtlCompareMemory(Res->Readback, plan, BC250_APCB_BYTES) == BC250_APCB_BYTES) {
        Res->Status = BC250CMOS_WRITE_OK;
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "[bc250cmos] UMA_SIZE pedido=%lu MB gravado e relido (aplica no proximo boot frio)\n",
            Req->NewUmaMb);
        return STATUS_SUCCESS;
    }

    /* ROLLBACK */
    {
        UCHAR again[BC250_APCB_BYTES];
        Bc250CmosWriteRange((UCHAR)BC250_APCB_OFFSET, BC250_APCB_BYTES, Res->Before);
        Bc250CmosReadRange((UCHAR)BC250_APCB_OFFSET, BC250_APCB_BYTES, again);
        Res->Status = (RtlCompareMemory(again, Res->Before, BC250_APCB_BYTES) == BC250_APCB_BYTES) ?
            BC250CMOS_WRITE_READBACK_MISMATCH_RESTORED :
            BC250CMOS_WRITE_READBACK_MISMATCH_UNRESTORED;
    }
    return STATUS_SUCCESS;
}

/*
 * Motivo do ultimo reset (doc 553): mesmo acesso do Linux (amd.c), 32 bits,
 * mapeamento sem cache so' durante a chamada. Unica escrita: o proprio valor
 * lido, e so' com Clear=1 e leitura valida.
 */
static VOID Bc250CmosResetReason(const BC250CMOS_RESET_REQUEST *Req, BC250CMOS_RESET_RESULT *Res)
{
    PHYSICAL_ADDRESS pa;
    volatile ULONG *reg;

    RtlZeroMemory(Res, sizeof(*Res));
    if (Req->Magic != BC250CMOS_ABI_MAGIC || Req->Version != BC250CMOS_ABI_VERSION ||
        Req->Clear > 1u) {
        Res->Status = BC250CMOS_RESET_BAD_REQUEST;
        return;
    }
    pa.QuadPart = BC250_FCH_S5_RESET_STATUS_PA;
    reg = (volatile ULONG *)MmMapIoSpaceEx(pa, sizeof(ULONG), PAGE_READWRITE | PAGE_NOCACHE);
    if (reg == NULL) {
        Res->Status = BC250CMOS_RESET_MAP_FAILED;
        return;
    }
    Res->Raw = READ_REGISTER_ULONG(reg);
    Res->AfterClear = Res->Raw;
    if (Res->Raw == BC250_FCH_RESET_INVALID) {
        Res->Status = BC250CMOS_RESET_INVALID_READ;
    } else {
        if (Req->Clear == 1u && Res->Raw != 0u) {
            WRITE_REGISTER_ULONG(reg, Res->Raw);
            Res->Cleared = 1u;
            Res->AfterClear = READ_REGISTER_ULONG(reg);
        }
        Res->Status = BC250CMOS_RESET_OK;
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "[bc250cmos] S5_RESET_STATUS=0x%08lX limpo=%lu depois=0x%08lX\n",
            Res->Raw, Res->Cleared, Res->AfterClear);
    }
    MmUnmapIoSpace((PVOID)reg, sizeof(ULONG));
}

NTSTATUS Bc250CmosDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = sp->Parameters.DeviceIoControl.IoControlCode;
    ULONG inLen = sp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = sp->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID buf = Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (code) {
    case IOCTL_BC250CMOS_READ_BANK:
        if (buf == NULL || outLen < sizeof(BC250CMOS_BANK)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        Bc250CmosReadRange(0u, BC250_CMOS_BANK_BYTES, ((BC250CMOS_BANK *)buf)->Bytes);
        info = sizeof(BC250CMOS_BANK);
        status = STATUS_SUCCESS;
        break;

    case IOCTL_BC250CMOS_WRITE_UMA: {
        BC250CMOS_WRITE_REQUEST req;
        if (buf == NULL || inLen < sizeof(BC250CMOS_WRITE_REQUEST) ||
            outLen < sizeof(BC250CMOS_WRITE_RESULT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        /* METHOD_BUFFERED: entrada e saida dividem o buffer; copia a entrada antes. */
        RtlCopyMemory(&req, buf, sizeof(req));
        status = Bc250CmosWriteUma(&req, (BC250CMOS_WRITE_RESULT *)buf);
        info = sizeof(BC250CMOS_WRITE_RESULT);
        break;
    }
    case IOCTL_BC250CMOS_RESET_REASON: {
        BC250CMOS_RESET_REQUEST req;
        if (buf == NULL || inLen < sizeof(BC250CMOS_RESET_REQUEST) ||
            outLen < sizeof(BC250CMOS_RESET_RESULT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&req, buf, sizeof(req));
        Bc250CmosResetReason(&req, (BC250CMOS_RESET_RESULT *)buf);
        info = sizeof(BC250CMOS_RESET_RESULT);
        status = STATUS_SUCCESS;
        break;
    }
    default:
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS Bc250CmosCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

VOID Bc250CmosUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING link;
    RtlInitUnicodeString(&link, BC250CMOS_SYMLINK_NAME);
    IoDeleteSymbolicLink(&link);
    if (DriverObject->DeviceObject != NULL) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING name;
    UNICODE_STRING link;
    UNICODE_STRING sddl;
    PDEVICE_OBJECT device = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);
    KeInitializeSpinLock(&g_Bc250CmosLock);

    RtlInitUnicodeString(&name, BC250CMOS_DEVICE_NAME);
    RtlInitUnicodeString(&link, BC250CMOS_SYMLINK_NAME);
    RtlInitUnicodeString(&sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

    status = IoCreateDeviceSecure(DriverObject, 0, &name, FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN, FALSE, &sddl, &kBc250CmosClassGuid, &device);
    if (!NT_SUCCESS(status)) return status;

    status = IoCreateSymbolicLink(&link, &name);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = Bc250CmosCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Bc250CmosCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Bc250CmosDeviceControl;
    DriverObject->DriverUnload = Bc250CmosUnload;
    return STATUS_SUCCESS;
}
