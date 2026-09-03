#ifndef BC250_BOOT_FUSE_H
#define BC250_BOOT_FUSE_H

#if defined(BC250_KERNEL_MODE)
#include <ntddk.h>
#else
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_FUSE_MAGIC   0x45535546u /* 'FUSE' em little-endian ASCII */
#define BC250_FUSE_VERSION 1u

/*
 * Caminho NT do arquivo do fusivel persistente.
 * Vive na raiz da unidade do sistema (fora do registro e fora da chave do servico),
 * de modo que sobrevive a rollbacks de driver, desinstalacoes e reboots.
 */
#define BC250_FUSE_FILE_PATH_NT  L"\\DosDevices\\C:\\BC250_BOOT_FUSE.dat"
#define BC250_FUSE_FILE_PATH_DOS "C:\\BC250_BOOT_FUSE.dat"

typedef enum _BC250_FUSE_STATE {
    BC250_FUSE_STATE_CLEAN   = 0, /* Desligamento anterior limpo / pronto para ensaio */
    BC250_FUSE_STATE_ARMED   = 1, /* Em execucao: StartDevice entrou e ainda nao fechou limpo */
    BC250_FUSE_STATE_TRIPPED = 2  /* FUSIVEL DISPARADO: boot anterior morreu sujo / loop bloqueado */
} BC250_FUSE_STATE;

typedef struct _BC250_BOOT_FUSE_RECORD {
    ULONG Magic;             /* offset 0 */
    ULONG Version;           /* offset 4 */
    ULONG State;             /* offset 8 */
    ULONG BootCount;         /* offset 12 */
    ULONG LastStatus;        /* offset 16 */
    ULONG Flags;             /* offset 20 */
    LARGE_INTEGER Timestamp; /* offset 24 (8 bytes, termina em 32) */
    ULONG Checksum;          /* offset 32 */
    ULONG Reserved;          /* offset 36 (padding explicito; total 40 bytes) */
} BC250_BOOT_FUSE_RECORD;

/*
 * Calcula o checksum aditivo de um record sobre todos os campos antes de Checksum.
 */
_Check_return_
ULONG Bc250BootFuseComputeChecksum(
    _In_ const BC250_BOOT_FUSE_RECORD *Record);

/*
 * Valida a integridade basica de um record lido do disco.
 */
_Check_return_
BOOLEAN Bc250BootFuseValidateRecord(
    _In_ const BC250_BOOT_FUSE_RECORD *Record);

/*
 * Executa a transicao pura da maquina de estados.
 * Permite teste unitario offline 100% deterministico sem dependencia de I/O de kernel.
 */
void Bc250BootFuseProcessTransition(
    _Inout_ BC250_BOOT_FUSE_RECORD *Record,
    _In_ BOOLEAN FileExisted,
    _In_ BOOLEAN IsStartDevice,
    _In_ ULONG Status,
    _Out_ PBOOLEAN AllowStart);

#if defined(BC250_KERNEL_MODE)

/*
 * Avalia o fusivel no inicio de DxgkDdiStartDevice (executa em PASSIVE_LEVEL):
 * - Se o arquivo nao existe: cria como ARMED, permite inicio (*AllowStartDevice = TRUE).
 * - Se arquivo existe e State == CLEAN: transiciona para ARMED, permite inicio (*AllowStartDevice = TRUE).
 * - Se arquivo existe e State == ARMED: a sessao anterior morreu suja! Transiciona para TRIPPED,
 *   recusa inicio (*AllowStartDevice = FALSE).
 * - Se arquivo existe e State == TRIPPED: mantem TRIPPED, recusa inicio (*AllowStartDevice = FALSE).
 */
_Check_return_
NTSTATUS Bc250BootFuseEvaluateAndArm(
    _Out_ PBOOLEAN AllowStartDevice,
    _Out_opt_ PULONG PreviousState);

/*
 * Chamado em DxgkDdiStopDeviceActive ou no teardown limpo:
 * Transiciona de ARMED para CLEAN com o status informado.
 */
_Check_return_
NTSTATUS Bc250BootFuseDisarmClean(
    _In_ NTSTATUS CompletionStatus);

/*
 * Forca o disparo do fusivel (por exemplo, ao detectar condicao de panico irrecuperavel).
 */
_Check_return_
NTSTATUS Bc250BootFuseForceTrip(
    _In_ NTSTATUS ReasonStatus);

/*
 * Reseta o fusivel para estado CLEAN (usado apos inspecao / recuperacao).
 */
_Check_return_
NTSTATUS Bc250BootFuseReset(void);

#endif /* BC250_KERNEL_MODE */

#ifdef __cplusplus
}
#endif

#endif /* BC250_BOOT_FUSE_H */
