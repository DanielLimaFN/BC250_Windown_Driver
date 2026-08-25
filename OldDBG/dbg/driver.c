/* driver.c — DriverEntry e despacho de IOCTL do bc250dbg.sys.
 *
 * Driver legacy (nao-PnP), instalado por `sc create`. Ele nao assume o
 * dispositivo PCI: apenas localiza a BC-250 no config space, mapeia o BAR5
 * para observacao e expoe a superficie de instrumentacao. E assim que a
 * ferramenta continua funcionando quando o driver grafico esta quebrado,
 * desinstalado ou em loop de TDR.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bc250dbg.h"

BC250DBG_DEVICE g_Dev;

static UNICODE_STRING g_SymLink;
static IO_REMOVE_LOCK g_RemoveLock;
static BOOLEAN        g_SymLinkCreated = FALSE;

DRIVER_INITIALIZE DriverEntry;
static DRIVER_UNLOAD BcUnload;
static DRIVER_DISPATCH BcCreate;
static DRIVER_DISPATCH BcCleanup;
static DRIVER_DISPATCH BcClose;
static DRIVER_DISPATCH BcDeviceControl;

/* ------------------------------------------------------------------------ */
/* Helpers de validacao de buffer (METHOD_BUFFERED)                          */
/* ------------------------------------------------------------------------ */

#define IN_OK(type)   (inLen  >= sizeof(type))
#define OUT_OK(type)  (outLen >= sizeof(type))

/* ------------------------------------------------------------------------ */
/* IRP_MJ_CREATE / CLEANUP / CLOSE                                           */
/* ------------------------------------------------------------------------ */

static NTSTATUS BcCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION sl = IoGetCurrentIrpStackLocation(Irp);
    PBC250DBG_FILE_CONTEXT ctx;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceObject);

    /* O lock adquirido aqui e segurado por TODA a vida do handle e so e
     * solto no IRP_MJ_CLOSE. E o que impede o driver de descarregar com
     * um consumidor de log ainda com o ring mapeado. Tag NULL nos dois
     * lados porque o par acquire/release cruza IRPs diferentes. */
    status = IoAcquireRemoveLock(&g_RemoveLock, NULL);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    ctx = (PBC250DBG_FILE_CONTEXT)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                 sizeof(*ctx),
                                                 BC250DBG_POOL_TAG);
    if (ctx == NULL) {
        IoReleaseRemoveLock(&g_RemoveLock, NULL);
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    sl->FileObject->FsContext = ctx;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS BcCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION sl = IoGetCurrentIrpStackLocation(Irp);
    PBC250DBG_FILE_CONTEXT ctx = (PBC250DBG_FILE_CONTEXT)sl->FileObject->FsContext;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceObject);

    status = IoAcquireRemoveLock(&g_RemoveLock, Irp);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    /* O CLEANUP chega no contexto do processo dono do handle — que e
     * exatamente onde o MmUnmapLockedPages precisa acontecer. */
    if (ctx != NULL) {
        BcLogUnmapFromUser(ctx);
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoReleaseRemoveLock(&g_RemoveLock, Irp);
    return STATUS_SUCCESS;
}

static NTSTATUS BcClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION sl = IoGetCurrentIrpStackLocation(Irp);
    PBC250DBG_FILE_CONTEXT ctx = (PBC250DBG_FILE_CONTEXT)sl->FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (ctx != NULL) {
        ExFreePoolWithTag(ctx, BC250DBG_POOL_TAG);
        sl->FileObject->FsContext = NULL;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    /* Fecha o par aberto no IRP_MJ_CREATE. */
    IoReleaseRemoveLock(&g_RemoveLock, NULL);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* IRP_MJ_DEVICE_CONTROL                                                     */
/* ------------------------------------------------------------------------ */

static NTSTATUS BcDoScript(PVOID buffer, ULONG inLen, ULONG outLen,
                           ULONG_PTR *info)
{
    BC250DBG_SCRIPT_IN header;
    BC250DBG_OP *ops = NULL;
    BC250DBG_OP_RESULT *results = NULL;
    BC250DBG_SCRIPT_OUT out;
    ULONG opBytes, resBytes;
    NTSTATUS status;

    if (inLen < sizeof(BC250DBG_SCRIPT_IN)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(&header, buffer, sizeof(header));
    header.Name[sizeof(header.Name) - 1] = '\0';

    if (header.OpCount == 0 || header.OpCount > BC250DBG_SCRIPT_MAX_OPS) {
        return STATUS_INVALID_PARAMETER;
    }

    opBytes  = header.OpCount * (ULONG)sizeof(BC250DBG_OP);
    resBytes = header.OpCount * (ULONG)sizeof(BC250DBG_OP_RESULT);

    if (inLen < sizeof(BC250DBG_SCRIPT_IN) + opBytes) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (outLen < sizeof(BC250DBG_SCRIPT_OUT) + resBytes) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* METHOD_BUFFERED usa o MESMO buffer para entrada e saida: as ops
     * precisam ser copiadas antes de escrever qualquer resultado. */
    ops = (BC250DBG_OP *)ExAllocatePool2(POOL_FLAG_NON_PAGED, opBytes,
                                         BC250DBG_POOL_TAG);
    results = (BC250DBG_OP_RESULT *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                    resBytes,
                                                    BC250DBG_POOL_TAG);
    if (ops == NULL || results == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    RtlCopyMemory(ops, (PUCHAR)buffer + sizeof(BC250DBG_SCRIPT_IN), opBytes);

    status = BcScriptRun(&header, ops, &out, results, header.OpCount);
    if (!NT_SUCCESS(status)) {
        goto done;
    }

    RtlCopyMemory(buffer, &out, sizeof(out));
    RtlCopyMemory((PUCHAR)buffer + sizeof(BC250DBG_SCRIPT_OUT), results,
                  out.OpCount * sizeof(BC250DBG_OP_RESULT));
    *info = sizeof(BC250DBG_SCRIPT_OUT) +
            out.OpCount * sizeof(BC250DBG_OP_RESULT);

done:
    if (ops != NULL) {
        ExFreePoolWithTag(ops, BC250DBG_POOL_TAG);
    }
    if (results != NULL) {
        ExFreePoolWithTag(results, BC250DBG_POOL_TAG);
    }
    return status;
}

static NTSTATUS BcDoSnapshot(PVOID buffer, ULONG inLen, ULONG outLen,
                             ULONG_PTR *info)
{
    BC250DBG_SNAP_IN in;
    BC250DBG_SNAP_OUT out;
    ULONG *dst;
    ULONG i;

    if (inLen < sizeof(in)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlCopyMemory(&in, buffer, sizeof(in));

    if (in.Count == 0 || in.Count > BC250DBG_SNAP_MAX) {
        return STATUS_INVALID_PARAMETER;
    }
    if (outLen < sizeof(BC250DBG_SNAP_OUT) + in.Count * sizeof(ULONG)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if ((in.Start & 3u) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    out.Size = sizeof(out);
    out.Count = in.Count;
    dst = (ULONG *)((PUCHAR)buffer + sizeof(BC250DBG_SNAP_OUT));

    /* Snapshot nao emite TRACE por dword: um dump de 4096 registradores
     * afogaria o ring e mudaria o timing do que se quer observar. */
    if (in.Space == BC250DBG_SPACE_VRAM) {
        NTSTATUS vst = BcVramMap();
        if (!NT_SUCCESS(vst)) {
            return vst;
        }
    }

    for (i = 0; i < in.Count; i++) {
        if (in.Space == BC250DBG_SPACE_SMN) {
            dst[i] = BcSmnReadRaw(in.Start + i * 4);
        } else if (in.Space == BC250DBG_SPACE_VRAM) {
            dst[i] = BcVramReadRaw(in.Start + i * 4);
        } else {
            dst[i] = BcRegReadRaw(in.Start + i * 4);
        }
    }

    RtlCopyMemory(buffer, &out, sizeof(out));
    *info = sizeof(BC250DBG_SNAP_OUT) + in.Count * sizeof(ULONG);

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "snapshot %s 0x%x +%u dw",
              in.Space == BC250DBG_SPACE_SMN ? "smn" :
              in.Space == BC250DBG_SPACE_VRAM ? "vram" : "bar",
              in.Start, in.Count);
    return STATUS_SUCCESS;
}

static NTSTATUS BcDoArm(PVOID buffer, ULONG inLen, ULONG outLen,
                        ULONG_PTR *info)
{
    BC250DBG_ARM_IO io;
    LONG previous;

    if (inLen < sizeof(io) || outLen < sizeof(io)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlCopyMemory(&io, buffer, sizeof(io));

    if (io.Level > BC250DBG_ARM_UNSAFE) {
        return STATUS_INVALID_PARAMETER;
    }
    if (io.Level == BC250DBG_ARM_UNSAFE) {
        if (io.Magic != BC250DBG_ARM_MAGIC) {
            return STATUS_INVALID_PARAMETER;
        }
        /* Sem sensor termico calibrado nao ha como armar operacao capaz de
         * mexer em clock e tensao. Ver thermal.c. */
        if (!g_Dev.ThermalTrusted) {
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                      "arm UNSAFE recusado: sensor nao calibrado");
            return STATUS_DEVICE_POWER_FAILURE;
        }
    }

    previous = InterlockedExchange(&g_Dev.ArmLevel, (LONG)io.Level);
    BcLogEvent(BC250DBG_LVL_WARN, BC250DBG_SUB_CORE, BC250DBG_EV_ARM_CHANGE,
               (ULONG)previous, io.Level, 0, 0);

    io.Status = 0;
    RtlCopyMemory(buffer, &io, sizeof(io));
    *info = sizeof(io);
    return STATUS_SUCCESS;
}

static NTSTATUS BcDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION sl = IoGetCurrentIrpStackLocation(Irp);
    PVOID  buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG  inLen  = sl->Parameters.DeviceIoControl.InputBufferLength;
    ULONG  outLen = sl->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG  code   = sl->Parameters.DeviceIoControl.IoControlCode;
    PBC250DBG_FILE_CONTEXT ctx = (PBC250DBG_FILE_CONTEXT)sl->FileObject->FsContext;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    status = IoAcquireRemoveLock(&g_RemoveLock, Irp);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    switch (code) {

    case IOCTL_BC250DBG_GET_INFO:
        if (!OUT_OK(BC250DBG_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        BcHwCollectInfo((BC250DBG_INFO *)buffer);
        info = sizeof(BC250DBG_INFO);
        status = STATUS_SUCCESS;
        break;

    case IOCTL_BC250DBG_REG_READ:
    case IOCTL_BC250DBG_SMN_READ: {
        BC250DBG_REG_IO io;
        ULONG st;

        if (!IN_OK(BC250DBG_REG_IO) || !OUT_OK(BC250DBG_REG_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        io.Value = (code == IOCTL_BC250DBG_REG_READ)
                 ? BcRegRead(io.Offset, &st)
                 : BcSmnRead(io.Offset, &st);
        io.Status = st;
        io.Size = sizeof(io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        status = (st == BC250DBG_OPS_OK) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
        break;
    }

    case IOCTL_BC250DBG_SDMA_RAW: {
        BC250DBG_SDMA_RAW_IO io;

        if (!IN_OK(BC250DBG_SDMA_RAW_IO) || !OUT_OK(BC250DBG_SDMA_RAW_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        io.Size = sizeof(io);
        status = BcSdmaRaw(&io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_VRAM_FILL:
    case IOCTL_BC250DBG_VRAM_VERIFY: {
        BC250DBG_VRAM_PATTERN_IO io;

        if (!IN_OK(BC250DBG_VRAM_PATTERN_IO) || !OUT_OK(BC250DBG_VRAM_PATTERN_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        io.Size = sizeof(io);
        status = (code == IOCTL_BC250DBG_VRAM_VERIFY)
               ? BcVramVerifyPattern(&io)
               : BcVramFillPattern(&io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_VRAM_SCAN: {
        BC250DBG_VRAM_SCAN_IO io;

        if (!IN_OK(BC250DBG_VRAM_SCAN_IO) || !OUT_OK(BC250DBG_VRAM_SCAN_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        io.Size = sizeof(io);
        status = BcVramScan(&io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_HDP_INVALIDATE: {
        if (g_Dev.Mmio == NULL) {
            status = STATUS_DEVICE_NOT_READY;
            break;
        }
        if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
            status = STATUS_ACCESS_DENIED;
            break;
        }
        BcHdpReadCacheInvalidate();
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250DBG_SDMA_OP: {
        BC250DBG_SDMA_OP_IO io;

        if (!IN_OK(BC250DBG_SDMA_OP_IO) || !OUT_OK(BC250DBG_SDMA_OP_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        io.Size = sizeof(io);
        status = BcSdmaExecOp(&io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_SDMA_F32_RESET: {
        BC250DBG_SDMA_F32_RESET_IO io;
        ULONG before = 0, after = 0, moved = 0;

        if (!IN_OK(BC250DBG_SDMA_F32_RESET_IO) ||
            !OUT_OK(BC250DBG_SDMA_F32_RESET_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlZeroMemory(&io, sizeof(io));
        io.Size = sizeof(io);

        status = BcSdmaF32Reset(&before, &after, &moved);

        io.PcBefore = before;
        io.PcAfter  = after;
        io.Moved    = moved;
        io.Status   = NT_SUCCESS(status) ? BC250DBG_OPS_OK
                    : (status == STATUS_ACCESS_DENIED) ? BC250DBG_OPS_DENIED
                                                       : BC250DBG_OPS_TIMEOUT;
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        /* PC parado nao e erro de IOCTL: o chamador quer ver os numeros. */
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250DBG_PSP_STATUS: {
        BC250DBG_PSP_STATUS_IO io;

        if (!IN_OK(BC250DBG_PSP_STATUS_IO) || !OUT_OK(BC250DBG_PSP_STATUS_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        if (g_Dev.Mmio == NULL) {
            status = STATUS_DEVICE_NOT_READY;
            break;
        }
        RtlZeroMemory(&io, sizeof(io));
        io.Size = sizeof(io);
        BcPspReadStatus(&io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250DBG_PSP_RING_INIT: {
        BC250DBG_PSP_RING_INIT_IO io;
        ULONGLONG ringPa = 0;
        ULONG before = 0, after = 0;

        if (!IN_OK(BC250DBG_PSP_RING_INIT_IO) || !OUT_OK(BC250DBG_PSP_RING_INIT_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlZeroMemory(&io, sizeof(io));
        io.Size = sizeof(io);

        status = BcPspRingCreate(&ringPa, &before, &after);

        io.RingPhysical   = ringPa;
        io.C2pmsg64Before = before;
        io.C2pmsg64After  = after;
        io.Status = NT_SUCCESS(status) ? BC250DBG_OPS_OK
                  : (status == STATUS_ACCESS_DENIED) ? BC250DBG_OPS_DENIED
                                                     : BC250DBG_OPS_TIMEOUT;
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_PSP_LOAD_FW: {
        BC250DBG_PSP_LOAD_FW_IO header;
        const ULONG *dwords;

        if (inLen < sizeof(header) || outLen < sizeof(header)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&header, buffer, sizeof(header));
        if (header.DwordCount == 0 ||
            header.DwordCount > BC250DBG_PSP_MAX_FW_DWORDS) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (inLen < sizeof(header) + header.DwordCount * sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        dwords = (const ULONG *)((const UCHAR *)buffer + sizeof(header));
        header.FenceReached = 0;
        header.RespStatus   = 0;
        header.RespFwAddrLo = 0;
        header.RespFwAddrHi = 0;
        header.RespTmrSize  = 0;

        status = BcPspLoadIpFw(header.FwType, dwords, header.DwordCount, &header);

        /* Microcodigo de SDMA so entra em execucao apos um pulso de RESET no
         * F32 — tirar o HALT nao basta (ver BcSdmaF32Reset). Fazemos aqui para
         * que 'psp loadfw sdma0' deixe a engine pronta de fato. */
        if (NT_SUCCESS(status) && header.RespStatus == 0 &&
            (header.FwType == BC250_PSP_FW_SDMA0 ||
             header.FwType == BC250_PSP_FW_SDMA1)) {
            ULONG before = 0, after = 0, moved = 0;
            BcSdmaF32Reset(&before, &after, &moved);
        }

        header.Size = sizeof(header);
        RtlCopyMemory(buffer, &header, sizeof(header));
        info = sizeof(header);
        break;
    }

    case IOCTL_BC250DBG_PSP_SETUP_TMR: {
        BC250DBG_PSP_SETUP_TMR_IO io;

        if (!IN_OK(BC250DBG_PSP_SETUP_TMR_IO) || !OUT_OK(BC250DBG_PSP_SETUP_TMR_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));

        status = BcPspSetupTmr(io.TmrSizeBytes, io.VramOffset, &io);

        io.Size = sizeof(io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_GFX_INIT: {
        BC250DBG_GFX_INIT_IO io;

        if (!IN_OK(BC250DBG_GFX_INIT_IO) || !OUT_OK(BC250DBG_GFX_INIT_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));

        status = BcGfxInit(&io);

        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_GFX_SUBMIT: {
        BC250DBG_GFX_SUBMIT_IO io;

        if (!IN_OK(BC250DBG_GFX_SUBMIT_IO) || !OUT_OK(BC250DBG_GFX_SUBMIT_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));

        status = BcGfxSubmit(&io);

        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_GFX_STATUS: {
        BC250DBG_GFX_STATUS_IO io;

        if (!OUT_OK(BC250DBG_GFX_STATUS_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        BcGfxStatus(&io);

        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250DBG_VRAM_READ:
    case IOCTL_BC250DBG_VRAM_WRITE: {
        BC250DBG_REG_IO io;

        if (!IN_OK(BC250DBG_REG_IO) || !OUT_OK(BC250DBG_REG_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));

        if ((io.Offset & 3u) != 0) {
            io.Status = BC250DBG_OPS_RANGE;
            status = STATUS_INVALID_PARAMETER;
            RtlCopyMemory(buffer, &io, sizeof(io));
            info = sizeof(io);
            break;
        }

        status = BcVramMap();
        if (!NT_SUCCESS(status)) {
            io.Status = BC250DBG_OPS_BADOP;
            RtlCopyMemory(buffer, &io, sizeof(io));
            info = sizeof(io);
            break;
        }

        if (code == IOCTL_BC250DBG_VRAM_WRITE) {
            if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
                io.Status = BC250DBG_OPS_DENIED;
                status = STATUS_ACCESS_DENIED;
                RtlCopyMemory(buffer, &io, sizeof(io));
                info = sizeof(io);
                break;
            }
            BcVramWriteRaw(io.Offset, io.Value);
        }

        io.Value  = BcVramReadRaw(io.Offset);   /* sempre devolve o readback */
        io.Status = BC250DBG_OPS_OK;
        io.Size   = sizeof(io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250DBG_REG_WRITE:
    case IOCTL_BC250DBG_SMN_WRITE: {
        BC250DBG_REG_IO io;
        ULONG st, readback;

        if (!IN_OK(BC250DBG_REG_IO) || !OUT_OK(BC250DBG_REG_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        readback = (code == IOCTL_BC250DBG_REG_WRITE)
                 ? BcRegWrite(io.Offset, io.Value, &st)
                 : BcSmnWrite(io.Offset, io.Value, &st);
        io.Value = readback;      /* devolve o readback, nao o valor pedido */
        io.Status = st;
        io.Size = sizeof(io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        if (st == BC250DBG_OPS_DENIED) {
            status = STATUS_ACCESS_DENIED;
        } else if (st != BC250DBG_OPS_OK) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            status = STATUS_SUCCESS;
        }
        break;
    }

    case IOCTL_BC250DBG_SMU_MSG: {
        BC250DBG_SMU_IO io;
        ULONG smuStatus = 0, result = 0;

        if (!IN_OK(BC250DBG_SMU_IO) || !OUT_OK(BC250DBG_SMU_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        status = BcSmuMsg(io.Queue, io.Msg, io.Arg, io.ArgHi, io.TimeoutMs,
                          &smuStatus, &result);
        io.Status = smuStatus;
        io.Result = result;
        io.Size = sizeof(io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    case IOCTL_BC250DBG_LOG_MAP:
        if (!OUT_OK(BC250DBG_LOG_MAP_OUT) || ctx == NULL) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = BcLogMapToUser(ctx, (BC250DBG_LOG_MAP_OUT *)buffer);
        if (NT_SUCCESS(status)) {
            info = sizeof(BC250DBG_LOG_MAP_OUT);
        }
        break;

    case IOCTL_BC250DBG_LOG_UNMAP:
        if (ctx != NULL) {
            BcLogUnmapFromUser(ctx);
        }
        status = STATUS_SUCCESS;
        break;

    case IOCTL_BC250DBG_LOG_MASK: {
        BC250DBG_LOG_MASK_IN in;

        if (!IN_OK(BC250DBG_LOG_MASK_IN)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&in, buffer, sizeof(in));
        InterlockedExchange(&g_Dev.LogLevelMask,
                            (LONG)(in.LevelMask & BC250DBG_LVL_ALL));
        InterlockedExchange(&g_Dev.LogSubsysMask,
                            (LONG)(in.SubsysMask & BC250DBG_SUB_ALL));
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250DBG_SCRIPT_RUN:
        status = BcDoScript(buffer, inLen, outLen, &info);
        break;

    case IOCTL_BC250DBG_SCRIPT_ABORT:
        InterlockedExchange(&g_Dev.ScriptAbort, 1);
        BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_SCRIPT, "abort pedido");
        status = STATUS_SUCCESS;
        break;

    case IOCTL_BC250DBG_SNAPSHOT:
        status = BcDoSnapshot(buffer, inLen, outLen, &info);
        break;

    case IOCTL_BC250DBG_THERMAL_GET:
        if (!OUT_OK(BC250DBG_THERMAL)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        BcThermalGet((BC250DBG_THERMAL *)buffer);
        info = sizeof(BC250DBG_THERMAL);
        status = STATUS_SUCCESS;
        break;

    case IOCTL_BC250DBG_THERMAL_SET: {
        BC250DBG_THERMAL_SET in;

        if (!IN_OK(BC250DBG_THERMAL_SET)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&in, buffer, sizeof(in));
        status = BcThermalSet(&in);
        break;
    }

    case IOCTL_BC250DBG_ARM:
        status = BcDoArm(buffer, inLen, outLen, &info);
        break;

    case IOCTL_BC250DBG_PCICFG_READ: {
        BC250DBG_PCICFG_IO io;

        if (!IN_OK(BC250DBG_PCICFG_IO) || !OUT_OK(BC250DBG_PCICFG_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        io.Value = BcPciConfigRead(io.Bus, io.Dev, io.Fn, io.Offset);
        io.Status = BC250DBG_OPS_OK;
        io.Size = sizeof(io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250DBG_SDMA_INIT: {
        BC250DBG_SDMA_INIT_IO io;
        ULONGLONG gpuAddr = 0;

        if (!IN_OK(BC250DBG_SDMA_INIT_IO) || !OUT_OK(BC250DBG_SDMA_INIT_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        status = BcSdmaInit(io.RingSizeDwords * sizeof(ULONG),
                            io.Placement, io.VramOffset, &gpuAddr);
        if (NT_SUCCESS(status)) {
            io.PhysicalAddress = gpuAddr;
            io.RbCntl = BcRegReadRaw(0x04B80);   /* SDMA0_GFX_RB_CNTL */
            io.Status = BC250DBG_OPS_OK;
            RtlCopyMemory(buffer, &io, sizeof(io));
            info = sizeof(io);
        }
        break;
    }

    case IOCTL_BC250DBG_SDMA_SUBMIT: {
        BC250DBG_SDMA_SUBMIT_IO io;

        if (!IN_OK(BC250DBG_SDMA_SUBMIT_IO) || !OUT_OK(BC250DBG_SDMA_SUBMIT_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        status = BcSdmaSubmit((const ULONG *)io.Dwords, io.DwordCount,
                              (ULONG *)&io.RptrBefore, (ULONG *)&io.WptrAfter, (ULONG *)&io.RptrAfter);
        if (NT_SUCCESS(status)) {
            io.Status = (io.RptrAfter == io.WptrAfter) ? BC250DBG_OPS_OK : BC250DBG_OPS_TIMEOUT;
            RtlCopyMemory(buffer, &io, sizeof(io));
            info = sizeof(io);
        }
        break;
    }

    case IOCTL_BC250DBG_SDMA_STATUS: {
        BC250DBG_SDMA_STATUS_IO io;
        PHYSICAL_ADDRESS pa;

        if (!OUT_OK(BC250DBG_SDMA_STATUS_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = BcSdmaStatus((ULONG *)&io.Rptr, (ULONG *)&io.Wptr,
                              (ULONG *)&io.RbCntl, (ULONG *)&io.StatusReg, &pa,
                              (ULONG *)&io.RptrWriteback, (ULONG *)&io.WptrShadow);
        if (NT_SUCCESS(status)) {
            io.RbBase = (UINT64)pa.QuadPart;
            io.Status = BC250DBG_OPS_OK;
            RtlCopyMemory(buffer, &io, sizeof(io));
            info = sizeof(io);
        }
        break;
    }

    case IOCTL_BC250DBG_SDMA_LOAD_FW: {
        BC250DBG_SDMA_LOAD_FW_IO header;
        const ULONG *dwords;

        if (inLen < sizeof(header) || outLen < sizeof(header)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&header, buffer, sizeof(header));
        if (header.DwordCount == 0 || header.DwordCount > 16384) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (inLen < sizeof(header) + header.DwordCount * sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        dwords = (const ULONG *)((const UCHAR *)buffer + sizeof(header));
        status = BcSdmaLoadFirmware(dwords, header.DwordCount,
                                    (ULONG *)&header.Version, (ULONG *)&header.Checksum);
        if (NT_SUCCESS(status)) {
            header.Status = BC250DBG_OPS_OK;
            RtlCopyMemory(buffer, &header, sizeof(header));
            info = sizeof(header);
        }
        break;
    }

    case IOCTL_BC250DBG_GCVM_STATUS: {
        BC250DBG_GCVM_STATUS_IO io;

        if (!OUT_OK(BC250DBG_GCVM_STATUS_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        BcGcvmStatus(&io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_BC250DBG_GCVM_INIT: {
        BC250DBG_GCVM_INIT_IO io;

        if (!IN_OK(BC250DBG_GCVM_INIT_IO) || !OUT_OK(BC250DBG_GCVM_INIT_IO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(&io, buffer, sizeof(io));
        status = BcGcvmInit(&io);
        RtlCopyMemory(buffer, &io, sizeof(io));
        info = sizeof(io);
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoReleaseRemoveLock(&g_RemoveLock, Irp);
    return status;
}

/* ------------------------------------------------------------------------ */
/* Entrada e saida do driver                                                 */
/* ------------------------------------------------------------------------ */

static VOID BcUnload(PDRIVER_OBJECT DriverObject)
{
    BcSdmaFree();
    BcPspFree();
    BcThermalStop();

    IoAcquireRemoveLock(&g_RemoveLock, NULL);
    IoReleaseRemoveLockAndWait(&g_RemoveLock, NULL);

    if (g_SymLinkCreated) {
        IoDeleteSymbolicLink(&g_SymLink);
        g_SymLinkCreated = FALSE;
    }
    if (DriverObject->DeviceObject != NULL) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }

    BcVramUnmap();
    BcMmioUnmap();
    BcLogFree();

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "bc250dbg: descarregado\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING ntName;
    PDEVICE_OBJECT devObj = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlZeroMemory(&g_Dev, sizeof(g_Dev));
    KeInitializeSpinLock(&g_Dev.SmnLock);
    KeInitializeMutex(&g_Dev.SmuMutex, 0);
    KeInitializeMutex(&g_Dev.ScriptMutex, 0);
    InterlockedExchange(&g_Dev.ArmLevel, BC250DBG_ARM_READONLY);
    IoInitializeRemoveLock(&g_RemoveLock, BC250DBG_POOL_TAG, 0, 0);

    status = BcLogInit(BC250DBG_LOG_DEFAULT_COUNT);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "bc250dbg v%u.%u ABI %u",
              (BC250DBG_DRIVER_VERSION >> 16) & 0xFFFFu,
              (BC250DBG_DRIVER_VERSION >> 8) & 0xFFu,
              BC250DBG_ABI_VERSION);

    /* Falhas aqui nao sao fatais: o driver ainda serve para inspecionar o
     * config space e para o operador diagnosticar o proprio ambiente. */
    (VOID)BcPciScan();
    (VOID)BcMmioMap();

    RtlInitUnicodeString(&ntName, BC250DBG_NT_DEVICE_NAME);
    status = IoCreateDevice(DriverObject, 0, &ntName, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &devObj);
    if (!NT_SUCCESS(status)) {
        BcVramUnmap();
        BcMmioUnmap();
        BcLogFree();
        return status;
    }

    RtlInitUnicodeString(&g_SymLink, BC250DBG_DOS_DEVICE_NAME);
    status = IoCreateSymbolicLink(&g_SymLink, &ntName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(devObj);
        BcVramUnmap();
        BcMmioUnmap();
        BcLogFree();
        return status;
    }
    g_SymLinkCreated = TRUE;

    devObj->Flags |= DO_BUFFERED_IO;
    devObj->Flags &= ~DO_DEVICE_INITIALIZING;
    g_Dev.DeviceObject = devObj;

    /* Nao zerar MajorFunction[]: o I/O manager ja preencheu com um handler
     * que rejeita o que nao implementamos. Trocar por NULL derruba o sistema
     * no primeiro IRP inesperado. */
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = BcCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]        = BcCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = BcClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = BcDeviceControl;
    DriverObject->DriverUnload = BcUnload;

    (VOID)BcThermalStart();

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE, "pronto (arm=readonly)");
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "bc250dbg: carregado, dispositivo \\\\.\\bc250dbg\n");
    return STATUS_SUCCESS;
}
