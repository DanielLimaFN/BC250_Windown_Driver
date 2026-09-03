#include "bc250_boot_fuse.h"

#if defined(BC250_KERNEL_MODE) && defined(BC250_ENABLE_DXGK_REGISTRATION)
#include "bc250_display_ddi.h"

#if defined(__cplusplus)
extern "C" {
#endif
NTSYSAPI NTSTATUS NTAPI ZwFlushBuffersFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock);
#if defined(__cplusplus)
}
#endif

static KMUTEX g_Bc250BootFuseMutex;
static volatile LONG g_Bc250BootFuseMutexInit = 0;

static void Bc250FuseEnsureMutexInitialized(void)
{
    if (InterlockedCompareExchange(&g_Bc250BootFuseMutexInit, 1, 0) == 0) {
        KeInitializeMutex(&g_Bc250BootFuseMutex, 0);
    }
}
#endif /* BC250_KERNEL_MODE && BC250_ENABLE_DXGK_REGISTRATION */

_Check_return_
ULONG Bc250BootFuseComputeChecksum(
    _In_ const BC250_BOOT_FUSE_RECORD *Record)
{
    const ULONG *words;
    size_t count;
    ULONG sum;
    size_t i;

    if (Record == NULL) {
        return 0;
    }

    words = (const ULONG *)Record;
    /*
     * Faz checksum estrito dos 32 bytes (8 ULONGs) que antecedem o campo Checksum.
     * Nunca toca no Checksum nem no Reserved/padding.
     */
    count = 32u / sizeof(ULONG);
    sum = 0x5A5A5A5Au;

    for (i = 0; i < count; ++i) {
        sum = (sum << 3) | (sum >> 29);
        sum ^= words[i];
    }
    return sum;
}

_Check_return_
BOOLEAN Bc250BootFuseValidateRecord(
    _In_ const BC250_BOOT_FUSE_RECORD *Record)
{
    if (Record == NULL) {
        return FALSE;
    }
    if (Record->Magic != BC250_FUSE_MAGIC) {
        return FALSE;
    }
    if (Record->Version != BC250_FUSE_VERSION) {
        return FALSE;
    }
    if (Record->State > BC250_FUSE_STATE_TRIPPED) {
        return FALSE;
    }
    if (Record->Checksum != Bc250BootFuseComputeChecksum(Record)) {
        return FALSE;
    }
    return TRUE;
}

void Bc250BootFuseProcessTransition(
    _Inout_ BC250_BOOT_FUSE_RECORD *Record,
    _In_ BOOLEAN FileExisted,
    _In_ BOOLEAN IsStartDevice,
    _In_ ULONG Status,
    _Out_ PBOOLEAN AllowStart)
{
    if (Record == NULL || AllowStart == NULL) {
        if (AllowStart != NULL) *AllowStart = FALSE;
        return;
    }

    *AllowStart = FALSE;

    if (IsStartDevice) {
        if (!FileExisted) {
            /*
             * Primeiro carregamento ou arquivo resetado:
             * Inicializa record e arma para a primeira sessao de StartDevice.
             */
            Record->Magic = BC250_FUSE_MAGIC;
            Record->Version = BC250_FUSE_VERSION;
            Record->State = BC250_FUSE_STATE_ARMED;
            Record->BootCount = 1;
            Record->LastStatus = 0;
            Record->Flags = 0;
            Record->Reserved = 0;
            *AllowStart = TRUE;
            return;
        }

        /* Arquivo existia: verificar integridade do record lido */
        if (!Bc250BootFuseValidateRecord(Record)) {
            /*
             * Corrupcao de arquivo: fail-closed imediato.
             * Dispara o fusivel para proteger a maquina de loop.
             */
            Record->Magic = BC250_FUSE_MAGIC;
            Record->Version = BC250_FUSE_VERSION;
            Record->State = BC250_FUSE_STATE_TRIPPED;
            Record->BootCount = Record->BootCount + 1;
            Record->LastStatus = (ULONG)0xC0000001L; /* STATUS_UNSUCCESSFUL */
            Record->Flags = 0;
            Record->Reserved = 0;
            *AllowStart = FALSE;
            return;
        }

        if (Record->State == BC250_FUSE_STATE_CLEAN) {
            /*
             * Transicao normal: o boot anterior fechou limpo.
             * Arma o fusivel para este ensaio.
             */
            Record->State = BC250_FUSE_STATE_ARMED;
            Record->BootCount = Record->BootCount + 1;
            Record->LastStatus = 0;
            *AllowStart = TRUE;
            return;
        }

        if (Record->State == BC250_FUSE_STATE_ARMED) {
            /*
             * ATENCAO: A sessao anterior entrou em StartDevice mas NUNCA
             * executou StopDevice/teardown limpo (travamento, reboot, panico).
             * O fusivel DISPARA e bloqueia StartDevice!
             */
            Record->State = BC250_FUSE_STATE_TRIPPED;
            Record->BootCount = Record->BootCount + 1;
            Record->LastStatus = (Status != 0) ? Status : (ULONG)0xC0000001L;
            *AllowStart = FALSE;
            return;
        }

        if (Record->State == BC250_FUSE_STATE_TRIPPED) {
            /* Fusivel ja disparado: continua recusando ate reset explicito */
            Record->BootCount = Record->BootCount + 1;
            *AllowStart = FALSE;
            return;
        }
    } else {
        /* IsStartDevice == FALSE (chamado em StopDevice ou teardown) */
        if (!FileExisted || !Bc250BootFuseValidateRecord(Record)) {
            return;
        }

        if (Record->State == BC250_FUSE_STATE_ARMED) {
            /*
             * Desligamento limpo confirmado: transiciona de ARMED para CLEAN.
             */
            Record->State = BC250_FUSE_STATE_CLEAN;
            Record->LastStatus = Status;
        }
        /* Se ja estava TRIPPED, StopDevice nao desarma em falso */
    }
}

#if defined(BC250_KERNEL_MODE)

#if defined(BC250_ENABLE_DXGK_REGISTRATION)

static NTSTATUS Bc250FuseReadRecordLocked(
    _Out_ BC250_BOOT_FUSE_RECORD *Record,
    _Out_ PBOOLEAN FileExisted)
{
    UNICODE_STRING ntPath = RTL_CONSTANT_STRING(BC250_FUSE_FILE_PATH_NT);
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    HANDLE fileHandle = NULL;
    NTSTATUS status;

    *FileExisted = FALSE;
    RtlZeroMemory(Record, sizeof(*Record));

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    InitializeObjectAttributes(
        &objAttr, &ntPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(
        &fileHandle,
        GENERIC_READ | SYNCHRONIZE,
        &objAttr,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0);

    if (!NT_SUCCESS(status)) {
        if (status == STATUS_OBJECT_NAME_NOT_FOUND ||
            status == STATUS_OBJECT_PATH_NOT_FOUND ||
            status == STATUS_NO_SUCH_FILE) {
            return STATUS_SUCCESS; /* Arquivo nao existe ainda */
        }
        return status;
    }

    *FileExisted = TRUE;

    status = ZwReadFile(
        fileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        Record,
        sizeof(*Record),
        NULL,
        NULL);

    ZwClose(fileHandle);

    if (!NT_SUCCESS(status) || ioStatus.Information < sizeof(*Record)) {
        return STATUS_DATATYPE_MISALIGNMENT_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS Bc250FuseWriteRecordLocked(
    _In_ const BC250_BOOT_FUSE_RECORD *Record)
{
    UNICODE_STRING ntPath = RTL_CONSTANT_STRING(BC250_FUSE_FILE_PATH_NT);
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    HANDLE fileHandle = NULL;
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    InitializeObjectAttributes(
        &objAttr, &ntPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(
        &fileHandle,
        GENERIC_WRITE | SYNCHRONIZE,
        &objAttr,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ,
        FILE_OVERWRITE_IF,
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = ZwWriteFile(
        fileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        (PVOID)Record,
        sizeof(*Record),
        NULL,
        NULL);

    if (NT_SUCCESS(status)) {
        (void)ZwFlushBuffersFile(fileHandle, &ioStatus);
    }

    ZwClose(fileHandle);
    return status;
}

_Check_return_
NTSTATUS Bc250BootFuseEvaluateAndArm(
    _Out_ PBOOLEAN AllowStartDevice,
    _Out_opt_ PULONG PreviousState)
{
    BC250_BOOT_FUSE_RECORD record;
    BOOLEAN fileExisted = FALSE;
    NTSTATUS status;

    if (AllowStartDevice == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *AllowStartDevice = FALSE;
    if (PreviousState != NULL) {
        *PreviousState = (ULONG)BC250_FUSE_STATE_TRIPPED;
    }

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Bc250FuseEnsureMutexInitialized();
    (void)KeWaitForSingleObject(
        &g_Bc250BootFuseMutex, Executive, KernelMode, FALSE, NULL);

    status = Bc250FuseReadRecordLocked(&record, &fileExisted);
    if (!NT_SUCCESS(status)) {
        /*
         * Erro de I/O em arquivo existente (ex: truncado).
         * Fail-closed: grava record TRIPPED defensivo.
         */
        Bc250DisplayLog("[FUSE] Arquivo de fusivel corrompido/ilegivel (%08x) -> TRIP defensivo!\n", status);
        RtlZeroMemory(&record, sizeof(record));
        record.Magic = BC250_FUSE_MAGIC;
        record.Version = BC250_FUSE_VERSION;
        record.State = BC250_FUSE_STATE_TRIPPED;
        record.BootCount = 1;
        record.LastStatus = (ULONG)status;
        KeQuerySystemTime(&record.Timestamp);
        record.Checksum = Bc250BootFuseComputeChecksum(&record);
        (void)Bc250FuseWriteRecordLocked(&record);

        *AllowStartDevice = FALSE;
        if (PreviousState != NULL) *PreviousState = (ULONG)BC250_FUSE_STATE_TRIPPED;
        KeReleaseMutex(&g_Bc250BootFuseMutex, FALSE);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (fileExisted && PreviousState != NULL) {
        *PreviousState = record.State;
    } else if (!fileExisted && PreviousState != NULL) {
        *PreviousState = (ULONG)BC250_FUSE_STATE_CLEAN;
    }

    Bc250BootFuseProcessTransition(
        &record,
        fileExisted,
        TRUE, /* IsStartDevice */
        0,
        AllowStartDevice);

    KeQuerySystemTime(&record.Timestamp);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);

    status = Bc250FuseWriteRecordLocked(&record);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[FUSE] Falha de escrita no arquivo do fusivel (%08x) -> fail-closed!\n", status);
        *AllowStartDevice = FALSE;
        KeReleaseMutex(&g_Bc250BootFuseMutex, FALSE);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!(*AllowStartDevice)) {
        Bc250DisplayLog(
            "[FUSE] STARTDEVICE RECUSADO: Fusivel disparado (State=%u, Boots=%u, LastStatus=%08x)\n",
            record.State, record.BootCount, record.LastStatus);
    } else {
        Bc250DisplayLog(
            "[FUSE] STARTDEVICE AUTORIZADO: Fusivel armado para ensaio (Boots=%u)\n",
            record.BootCount);
    }

    KeReleaseMutex(&g_Bc250BootFuseMutex, FALSE);
    return STATUS_SUCCESS;
}

_Check_return_
NTSTATUS Bc250BootFuseDisarmClean(
    _In_ NTSTATUS CompletionStatus)
{
    BC250_BOOT_FUSE_RECORD record;
    BOOLEAN fileExisted = FALSE;
    BOOLEAN dummyAllow = FALSE;
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Bc250FuseEnsureMutexInitialized();
    (void)KeWaitForSingleObject(
        &g_Bc250BootFuseMutex, Executive, KernelMode, FALSE, NULL);

    status = Bc250FuseReadRecordLocked(&record, &fileExisted);
    if (!NT_SUCCESS(status) || !fileExisted) {
        KeReleaseMutex(&g_Bc250BootFuseMutex, FALSE);
        return status;
    }

    if (!Bc250BootFuseValidateRecord(&record)) {
        KeReleaseMutex(&g_Bc250BootFuseMutex, FALSE);
        return STATUS_DATATYPE_MISALIGNMENT_ERROR;
    }

    Bc250BootFuseProcessTransition(
        &record,
        TRUE,
        FALSE, /* IsStartDevice = FALSE -> StopDevice */
        (ULONG)CompletionStatus,
        &dummyAllow);

    KeQuerySystemTime(&record.Timestamp);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);

    status = Bc250FuseWriteRecordLocked(&record);
    if (NT_SUCCESS(status)) {
        Bc250DisplayLog(
            "[FUSE] Desligamento limpo registrado: fusivel em CLEAN (Status=%08x)\n",
            (ULONG)CompletionStatus);
    }

    KeReleaseMutex(&g_Bc250BootFuseMutex, FALSE);
    return status;
}

_Check_return_
NTSTATUS Bc250BootFuseForceTrip(
    _In_ NTSTATUS ReasonStatus)
{
    BC250_BOOT_FUSE_RECORD record;
    BOOLEAN fileExisted = FALSE;
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Bc250FuseEnsureMutexInitialized();
    (void)KeWaitForSingleObject(
        &g_Bc250BootFuseMutex, Executive, KernelMode, FALSE, NULL);

    status = Bc250FuseReadRecordLocked(&record, &fileExisted);
    if (!fileExisted || !NT_SUCCESS(status) || !Bc250BootFuseValidateRecord(&record)) {
        RtlZeroMemory(&record, sizeof(record));
        record.Magic = BC250_FUSE_MAGIC;
        record.Version = BC250_FUSE_VERSION;
        record.BootCount = 1;
    }

    record.State = BC250_FUSE_STATE_TRIPPED;
    record.LastStatus = (ULONG)ReasonStatus;
    KeQuerySystemTime(&record.Timestamp);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);

    status = Bc250FuseWriteRecordLocked(&record);
    Bc250DisplayLog("[FUSE] Fusivel forcado para TRIPPED (Reason=%08x)\n", (ULONG)ReasonStatus);

    KeReleaseMutex(&g_Bc250BootFuseMutex, FALSE);
    return status;
}

_Check_return_
NTSTATUS Bc250BootFuseReset(void)
{
    BC250_BOOT_FUSE_RECORD record;
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Bc250FuseEnsureMutexInitialized();
    (void)KeWaitForSingleObject(
        &g_Bc250BootFuseMutex, Executive, KernelMode, FALSE, NULL);

    RtlZeroMemory(&record, sizeof(record));
    record.Magic = BC250_FUSE_MAGIC;
    record.Version = BC250_FUSE_VERSION;
    record.State = BC250_FUSE_STATE_CLEAN;
    record.BootCount = 0;
    record.LastStatus = STATUS_SUCCESS;
    record.Flags = 0;
    KeQuerySystemTime(&record.Timestamp);
    record.Checksum = Bc250BootFuseComputeChecksum(&record);

    status = Bc250FuseWriteRecordLocked(&record);
    Bc250DisplayLog("[FUSE] Fusivel resetado para CLEAN\n");

    KeReleaseMutex(&g_Bc250BootFuseMutex, FALSE);
    return status;
}

#else /* !BC250_ENABLE_DXGK_REGISTRATION (offline test build) */

_Check_return_
NTSTATUS Bc250BootFuseEvaluateAndArm(
    _Out_ PBOOLEAN AllowStartDevice,
    _Out_opt_ PULONG PreviousState)
{
    if (AllowStartDevice == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *AllowStartDevice = TRUE;
    if (PreviousState != NULL) {
        *PreviousState = (ULONG)BC250_FUSE_STATE_CLEAN;
    }
    return STATUS_SUCCESS;
}

_Check_return_
NTSTATUS Bc250BootFuseDisarmClean(
    _In_ NTSTATUS CompletionStatus)
{
    UNREFERENCED_PARAMETER(CompletionStatus);
    return STATUS_SUCCESS;
}

_Check_return_
NTSTATUS Bc250BootFuseForceTrip(
    _In_ NTSTATUS ReasonStatus)
{
    UNREFERENCED_PARAMETER(ReasonStatus);
    return STATUS_SUCCESS;
}

_Check_return_
NTSTATUS Bc250BootFuseReset(void)
{
    return STATUS_SUCCESS;
}

#endif /* BC250_ENABLE_DXGK_REGISTRATION */

#endif /* BC250_KERNEL_MODE */
