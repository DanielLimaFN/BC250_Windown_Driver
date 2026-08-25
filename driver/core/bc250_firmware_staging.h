#ifndef BC250_FIRMWARE_STAGING_H
#define BC250_FIRMWARE_STAGING_H

#include "bc250_staging_lease.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_FIRMWARE_SHA256_BYTES 32u

#define BC250_STAGING_TRANSPORT_CPU_VISIBLE       0x00000001u
#define BC250_STAGING_TRANSPORT_COHERENT_READBACK 0x00000002u
#define BC250_STAGING_TRANSPORT_REQUIRED \
    (BC250_STAGING_TRANSPORT_CPU_VISIBLE | \
     BC250_STAGING_TRANSPORT_COHERENT_READBACK)

typedef int (*BC250_STAGING_READ)(
    void *TransportContext,
    uint64_t RelativeOffset,
    void *Buffer,
    uint32_t Length);

typedef int (*BC250_STAGING_WRITE)(
    void *TransportContext,
    uint64_t RelativeOffset,
    const void *Buffer,
    uint32_t Length);

typedef enum BC250_FIRMWARE_STAGING_STATE {
    BC250_FIRMWARE_STAGING_EMPTY = 0,
    BC250_FIRMWARE_STAGING_STAGED,
    BC250_FIRMWARE_STAGING_RESTORED,
    BC250_FIRMWARE_STAGING_CONTAMINATED
} BC250_FIRMWARE_STAGING_STATE;

typedef enum BC250_FIRMWARE_STAGING_STATUS {
    BC250_FIRMWARE_STAGING_OK = 0,
    BC250_FIRMWARE_STAGING_BAD_ARGUMENT,
    BC250_FIRMWARE_STAGING_WRONG_STATE,
    BC250_FIRMWARE_STAGING_TRANSPORT_REJECTED,
    BC250_FIRMWARE_STAGING_LEASE_REJECTED,
    BC250_FIRMWARE_STAGING_PLAN_REJECTED,
    BC250_FIRMWARE_STAGING_IDENTITY_REJECTED,
    BC250_FIRMWARE_STAGING_SCRATCH_TOO_SMALL,
    BC250_FIRMWARE_STAGING_READ_FAILED,
    BC250_FIRMWARE_STAGING_WRITE_FAILED,
    BC250_FIRMWARE_STAGING_VERIFY_FAILED,
    BC250_FIRMWARE_STAGING_ROLLBACK_FAILED
} BC250_FIRMWARE_STAGING_STATUS;

typedef struct BC250_FIRMWARE_STAGING_TRANSPORT {
    BC250_STAGING_READ Read;
    BC250_STAGING_WRITE Write;
    void *Context;
    uint64_t WindowOffset;
    uint64_t WindowBytes;
    uint32_t IdentityFlags;
    uint32_t Reserved;
} BC250_FIRMWARE_STAGING_TRANSPORT;

typedef struct BC250_FIRMWARE_STAGING_CONTEXT {
    uint32_t State;
    uint32_t FirmwareType;
    uint32_t PayloadBytes;
    uint32_t VerifyBufferBytes;
    uint64_t RelativeOffset;
    BC250_FIRMWARE_STAGING_STATUS LastFailure;
    uint32_t Reserved;
    BC250_FIRMWARE_STAGING_TRANSPORT Transport;
    unsigned char *RollbackBuffer;
    unsigned char *VerifyBuffer;
    unsigned char OriginalSha256[BC250_FIRMWARE_SHA256_BYTES];
    unsigned char StagedSha256[BC250_FIRMWARE_SHA256_BYTES];
} BC250_FIRMWARE_STAGING_CONTEXT;

BC250_FIRMWARE_STAGING_STATUS Bc250FirmwareStageVerified(
    BC250_FIRMWARE_STAGING_CONTEXT *Context,
    const BC250_FIRMWARE_STAGING_TRANSPORT *Transport,
    const BC250_CP_BOOTSTRAP *Bootstrap,
    const BC250_CP_BOOTSTRAP_REPORT *Report,
    const BC250_FIRMWARE_STAGING_LEASE *Lease,
    const BC250_PSP_LOAD_FW_PLAN *Plan,
    const void *Payload,
    uint32_t PayloadBytes,
    const unsigned char ExpectedSha256[BC250_FIRMWARE_SHA256_BYTES],
    void *RollbackBuffer,
    uint32_t RollbackBufferBytes,
    void *VerifyBuffer,
    uint32_t VerifyBufferBytes);

BC250_FIRMWARE_STAGING_STATUS Bc250FirmwareRestoreOriginal(
    BC250_FIRMWARE_STAGING_CONTEXT *Context);

BC250_FIRMWARE_STAGING_STATUS Bc250FirmwareStagingClose(
    BC250_FIRMWARE_STAGING_CONTEXT *Context);

const char *Bc250FirmwareStagingStatusName(
    BC250_FIRMWARE_STAGING_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
