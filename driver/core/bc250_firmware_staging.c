#include "bc250_firmware_staging.h"

#include <string.h>

#ifdef BC250_KERNEL_MODE
typedef ULONG_PTR BC250_POINTER_VALUE;
#else
typedef uintptr_t BC250_POINTER_VALUE;
#endif

typedef struct BC250_SHA256_CONTEXT {
    uint32_t State[8];
    uint64_t TotalBytes;
    uint32_t BufferBytes;
    unsigned char Buffer[64];
} BC250_SHA256_CONTEXT;

static const uint32_t g_Bc250Sha256K[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static uint32_t Bc250RotateRight(uint32_t Value, uint32_t Bits)
{
    return (Value >> Bits) | (Value << (32u - Bits));
}

static uint32_t Bc250LoadBigEndian32(const unsigned char *Bytes)
{
    return ((uint32_t)Bytes[0] << 24) |
           ((uint32_t)Bytes[1] << 16) |
           ((uint32_t)Bytes[2] << 8) |
           (uint32_t)Bytes[3];
}

static void Bc250StoreBigEndian32(unsigned char *Bytes, uint32_t Value)
{
    Bytes[0] = (unsigned char)(Value >> 24);
    Bytes[1] = (unsigned char)(Value >> 16);
    Bytes[2] = (unsigned char)(Value >> 8);
    Bytes[3] = (unsigned char)Value;
}

static void Bc250Sha256Transform(
    BC250_SHA256_CONTEXT *Context,
    const unsigned char Block[64])
{
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t i;

    for (i = 0; i < 16u; ++i) {
        words[i] = Bc250LoadBigEndian32(Block + i * 4u);
    }
    for (i = 16u; i < 64u; ++i) {
        uint32_t s0 = Bc250RotateRight(words[i - 15u], 7u) ^
            Bc250RotateRight(words[i - 15u], 18u) ^
            (words[i - 15u] >> 3);
        uint32_t s1 = Bc250RotateRight(words[i - 2u], 17u) ^
            Bc250RotateRight(words[i - 2u], 19u) ^
            (words[i - 2u] >> 10);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }

    a = Context->State[0]; b = Context->State[1];
    c = Context->State[2]; d = Context->State[3];
    e = Context->State[4]; f = Context->State[5];
    g = Context->State[6]; h = Context->State[7];
    for (i = 0; i < 64u; ++i) {
        uint32_t sum1 = Bc250RotateRight(e, 6u) ^
            Bc250RotateRight(e, 11u) ^ Bc250RotateRight(e, 25u);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + sum1 + choice +
            g_Bc250Sha256K[i] + words[i];
        uint32_t sum0 = Bc250RotateRight(a, 2u) ^
            Bc250RotateRight(a, 13u) ^ Bc250RotateRight(a, 22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    Context->State[0] += a; Context->State[1] += b;
    Context->State[2] += c; Context->State[3] += d;
    Context->State[4] += e; Context->State[5] += f;
    Context->State[6] += g; Context->State[7] += h;
}

static void Bc250Sha256Init(BC250_SHA256_CONTEXT *Context)
{
    memset(Context, 0, sizeof(*Context));
    Context->State[0] = 0x6A09E667u; Context->State[1] = 0xBB67AE85u;
    Context->State[2] = 0x3C6EF372u; Context->State[3] = 0xA54FF53Au;
    Context->State[4] = 0x510E527Fu; Context->State[5] = 0x9B05688Cu;
    Context->State[6] = 0x1F83D9ABu; Context->State[7] = 0x5BE0CD19u;
}

static void Bc250Sha256Update(
    BC250_SHA256_CONTEXT *Context,
    const unsigned char *Bytes,
    uint32_t Length)
{
    uint32_t take;

    Context->TotalBytes += Length;
    while (Length != 0) {
        take = 64u - Context->BufferBytes;
        if (take > Length) take = Length;
        memcpy(Context->Buffer + Context->BufferBytes, Bytes, take);
        Context->BufferBytes += take;
        Bytes += take;
        Length -= take;
        if (Context->BufferBytes == 64u) {
            Bc250Sha256Transform(Context, Context->Buffer);
            Context->BufferBytes = 0;
        }
    }
}

static void Bc250Sha256Final(
    BC250_SHA256_CONTEXT *Context,
    unsigned char Digest[BC250_FIRMWARE_SHA256_BYTES])
{
    uint64_t bitLength = Context->TotalBytes * 8u;
    uint32_t i;

    Context->Buffer[Context->BufferBytes++] = 0x80u;
    if (Context->BufferBytes > 56u) {
        while (Context->BufferBytes < 64u) {
            Context->Buffer[Context->BufferBytes++] = 0;
        }
        Bc250Sha256Transform(Context, Context->Buffer);
        Context->BufferBytes = 0;
    }
    while (Context->BufferBytes < 56u) {
        Context->Buffer[Context->BufferBytes++] = 0;
    }
    for (i = 0; i < 8u; ++i) {
        Context->Buffer[63u - i] = (unsigned char)(bitLength >> (i * 8u));
    }
    Bc250Sha256Transform(Context, Context->Buffer);
    for (i = 0; i < 8u; ++i) {
        Bc250StoreBigEndian32(Digest + i * 4u, Context->State[i]);
    }
    memset(Context, 0, sizeof(*Context));
}

void Bc250FirmwareSha256(
    const void *Data,
    uint32_t Length,
    unsigned char Digest[BC250_FIRMWARE_SHA256_BYTES])
{
    BC250_SHA256_CONTEXT context;
    Bc250Sha256Init(&context);
    Bc250Sha256Update(&context, (const unsigned char *)Data, Length);
    Bc250Sha256Final(&context, Digest);
}

int Bc250FirmwareDigestEqual(
    const unsigned char *Left,
    const unsigned char *Right)
{
    unsigned char different = 0;
    uint32_t i;
    for (i = 0; i < BC250_FIRMWARE_SHA256_BYTES; ++i) {
        different = (unsigned char)(different | (Left[i] ^ Right[i]));
    }
    return different == 0;
}

static int Bc250RangesOverlap(
    const void *Left,
    uint32_t LeftBytes,
    const void *Right,
    uint32_t RightBytes)
{
    BC250_POINTER_VALUE leftStart = (BC250_POINTER_VALUE)Left;
    BC250_POINTER_VALUE rightStart = (BC250_POINTER_VALUE)Right;
    BC250_POINTER_VALUE leftEnd = leftStart + LeftBytes;
    BC250_POINTER_VALUE rightEnd = rightStart + RightBytes;

    if (leftEnd < leftStart || rightEnd < rightStart) return 1;
    return leftStart < rightEnd && rightStart < leftEnd;
}

static int Bc250HashTransport(
    BC250_FIRMWARE_STAGING_CONTEXT *Context,
    unsigned char Digest[BC250_FIRMWARE_SHA256_BYTES])
{
    BC250_SHA256_CONTEXT hash;
    uint32_t completed = 0;
    uint32_t take;

    Bc250Sha256Init(&hash);
    while (completed < Context->PayloadBytes) {
        take = Context->PayloadBytes - completed;
        if (take > Context->VerifyBufferBytes) {
            take = Context->VerifyBufferBytes;
        }
        if (!Context->Transport.Read(
                Context->Transport.Context,
                Context->RelativeOffset + completed,
                Context->VerifyBuffer,
                take)) {
            memset(&hash, 0, sizeof(hash));
            return 0;
        }
        Bc250Sha256Update(&hash, Context->VerifyBuffer, take);
        completed += take;
    }
    Bc250Sha256Final(&hash, Digest);
    return 1;
}

static int Bc250RestoreInternal(BC250_FIRMWARE_STAGING_CONTEXT *Context)
{
    unsigned char digest[BC250_FIRMWARE_SHA256_BYTES];

    if (!Context->Transport.Write(
            Context->Transport.Context,
            Context->RelativeOffset,
            Context->RollbackBuffer,
            Context->PayloadBytes) ||
        !Bc250HashTransport(Context, digest) ||
        !Bc250FirmwareDigestEqual(digest, Context->OriginalSha256)) {
        memset(digest, 0, sizeof(digest));
        Context->State = BC250_FIRMWARE_STAGING_CONTAMINATED;
        return 0;
    }
    memset(digest, 0, sizeof(digest));
    Context->State = BC250_FIRMWARE_STAGING_RESTORED;
    return 1;
}

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
    uint32_t VerifyBufferBytes)
{
    unsigned char sourceDigest[BC250_FIRMWARE_SHA256_BYTES];
    unsigned char readbackDigest[BC250_FIRMWARE_SHA256_BYTES];
    uint64_t relativeOffset;
    BC250_FIRMWARE_STAGING_STATUS failure;
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;
    BC250_VRAM_STATUS vramStatus;

    if (Context == NULL) return BC250_FIRMWARE_STAGING_BAD_ARGUMENT;
    memset(Context, 0, sizeof(*Context));
    if (Transport == NULL || Bootstrap == NULL || Report == NULL ||
        Lease == NULL || Plan == NULL || Payload == NULL ||
        ExpectedSha256 == NULL || RollbackBuffer == NULL ||
        VerifyBuffer == NULL || PayloadBytes == 0) {
        return BC250_FIRMWARE_STAGING_BAD_ARGUMENT;
    }
    if (Transport->Read == NULL || Transport->Write == NULL ||
        Transport->WindowBytes == 0 ||
        Transport->IdentityFlags != BC250_STAGING_TRANSPORT_REQUIRED) {
        return BC250_FIRMWARE_STAGING_TRANSPORT_REJECTED;
    }
    if (Bc250ValidateFirmwareStagingLease(
            Bootstrap, Report, Lease, Plan,
            &bootstrapStatus, &vramStatus) != BC250_STAGING_LEASE_OK) {
        return BC250_FIRMWARE_STAGING_LEASE_REJECTED;
    }
    if (Plan->FirmwareType < BC250_PSP_FW_CP_ME ||
        Plan->FirmwareType > BC250_PSP_FW_CP_MEC_JT1 ||
        Plan->PayloadBytes != PayloadBytes ||
        Plan->Binding.Role != BC250_VRAM_ROLE_FIRMWARE ||
        Plan->Binding.CpuVisible == 0 ||
        Plan->Binding.NaturallyAligned == 0 ||
        Plan->Binding.SnapshotGeneration == 0 ||
        (Plan->Binding.RegionFlags & BC250_VRAM_REGION_PERSISTENT) == 0 ||
        Plan->Binding.Length < PayloadBytes ||
        Plan->Binding.Offset < Transport->WindowOffset) {
        return BC250_FIRMWARE_STAGING_PLAN_REJECTED;
    }
    relativeOffset = Plan->Binding.Offset - Transport->WindowOffset;
    if (relativeOffset > Transport->WindowBytes ||
        PayloadBytes > Transport->WindowBytes - relativeOffset) {
        return BC250_FIRMWARE_STAGING_PLAN_REJECTED;
    }
    if (RollbackBufferBytes < PayloadBytes || VerifyBufferBytes == 0) {
        return BC250_FIRMWARE_STAGING_SCRATCH_TOO_SMALL;
    }
    if (Bc250RangesOverlap(Payload, PayloadBytes,
                           RollbackBuffer, RollbackBufferBytes) ||
        Bc250RangesOverlap(Payload, PayloadBytes,
                           VerifyBuffer, VerifyBufferBytes) ||
        Bc250RangesOverlap(RollbackBuffer, RollbackBufferBytes,
                           VerifyBuffer, VerifyBufferBytes)) {
        return BC250_FIRMWARE_STAGING_BAD_ARGUMENT;
    }

    Bc250FirmwareSha256(Payload, PayloadBytes, sourceDigest);
    if (!Bc250FirmwareDigestEqual(sourceDigest, ExpectedSha256)) {
        memset(sourceDigest, 0, sizeof(sourceDigest));
        return BC250_FIRMWARE_STAGING_IDENTITY_REJECTED;
    }

    Context->State = BC250_FIRMWARE_STAGING_EMPTY;
    Context->FirmwareType = Plan->FirmwareType;
    Context->PayloadBytes = PayloadBytes;
    Context->VerifyBufferBytes = VerifyBufferBytes;
    Context->RelativeOffset = relativeOffset;
    Context->Transport = *Transport;
    Context->RollbackBuffer = (unsigned char *)RollbackBuffer;
    Context->VerifyBuffer = (unsigned char *)VerifyBuffer;
    memcpy(Context->StagedSha256, sourceDigest, sizeof(sourceDigest));
    memset(sourceDigest, 0, sizeof(sourceDigest));

    if (!Transport->Read(
            Transport->Context, relativeOffset,
            RollbackBuffer, PayloadBytes)) {
        Context->LastFailure = BC250_FIRMWARE_STAGING_READ_FAILED;
        return BC250_FIRMWARE_STAGING_READ_FAILED;
    }
    Bc250FirmwareSha256(RollbackBuffer, PayloadBytes, Context->OriginalSha256);

    if (!Transport->Write(
            Transport->Context, relativeOffset, Payload, PayloadBytes)) {
        failure = BC250_FIRMWARE_STAGING_WRITE_FAILED;
        Context->LastFailure = failure;
        return Bc250RestoreInternal(Context) ? failure :
            BC250_FIRMWARE_STAGING_ROLLBACK_FAILED;
    }
    if (!Bc250HashTransport(Context, readbackDigest)) {
        failure = BC250_FIRMWARE_STAGING_READ_FAILED;
        Context->LastFailure = failure;
        memset(readbackDigest, 0, sizeof(readbackDigest));
        return Bc250RestoreInternal(Context) ? failure :
            BC250_FIRMWARE_STAGING_ROLLBACK_FAILED;
    }
    if (!Bc250FirmwareDigestEqual(readbackDigest, Context->StagedSha256)) {
        failure = BC250_FIRMWARE_STAGING_VERIFY_FAILED;
        Context->LastFailure = failure;
        memset(readbackDigest, 0, sizeof(readbackDigest));
        return Bc250RestoreInternal(Context) ? failure :
            BC250_FIRMWARE_STAGING_ROLLBACK_FAILED;
    }
    memset(readbackDigest, 0, sizeof(readbackDigest));
    Context->State = BC250_FIRMWARE_STAGING_STAGED;
    Context->LastFailure = BC250_FIRMWARE_STAGING_OK;
    return BC250_FIRMWARE_STAGING_OK;
}

BC250_FIRMWARE_STAGING_STATUS Bc250FirmwareRestoreOriginal(
    BC250_FIRMWARE_STAGING_CONTEXT *Context)
{
    if (Context == NULL) return BC250_FIRMWARE_STAGING_BAD_ARGUMENT;
    if (Context->State != BC250_FIRMWARE_STAGING_STAGED) {
        return BC250_FIRMWARE_STAGING_WRONG_STATE;
    }
    if (!Bc250RestoreInternal(Context)) {
        Context->LastFailure = BC250_FIRMWARE_STAGING_ROLLBACK_FAILED;
        return BC250_FIRMWARE_STAGING_ROLLBACK_FAILED;
    }
    Context->LastFailure = BC250_FIRMWARE_STAGING_OK;
    return BC250_FIRMWARE_STAGING_OK;
}

BC250_FIRMWARE_STAGING_STATUS Bc250FirmwareStagingClose(
    BC250_FIRMWARE_STAGING_CONTEXT *Context)
{
    if (Context == NULL) return BC250_FIRMWARE_STAGING_BAD_ARGUMENT;
    if (Context->State == BC250_FIRMWARE_STAGING_STAGED ||
        Context->State == BC250_FIRMWARE_STAGING_CONTAMINATED) {
        return BC250_FIRMWARE_STAGING_WRONG_STATE;
    }
    if (Context->RollbackBuffer != NULL) {
        memset(Context->RollbackBuffer, 0, Context->PayloadBytes);
    }
    if (Context->VerifyBuffer != NULL) {
        memset(Context->VerifyBuffer, 0, Context->VerifyBufferBytes);
    }
    memset(Context, 0, sizeof(*Context));
    return BC250_FIRMWARE_STAGING_OK;
}

const char *Bc250FirmwareStagingStatusName(
    BC250_FIRMWARE_STAGING_STATUS Status)
{
    switch (Status) {
    case BC250_FIRMWARE_STAGING_OK: return "ok";
    case BC250_FIRMWARE_STAGING_BAD_ARGUMENT: return "bad_argument";
    case BC250_FIRMWARE_STAGING_WRONG_STATE: return "wrong_state";
    case BC250_FIRMWARE_STAGING_TRANSPORT_REJECTED:
        return "transport_rejected";
    case BC250_FIRMWARE_STAGING_LEASE_REJECTED: return "lease_rejected";
    case BC250_FIRMWARE_STAGING_PLAN_REJECTED: return "plan_rejected";
    case BC250_FIRMWARE_STAGING_IDENTITY_REJECTED:
        return "identity_rejected";
    case BC250_FIRMWARE_STAGING_SCRATCH_TOO_SMALL:
        return "scratch_too_small";
    case BC250_FIRMWARE_STAGING_READ_FAILED: return "read_failed";
    case BC250_FIRMWARE_STAGING_WRITE_FAILED: return "write_failed";
    case BC250_FIRMWARE_STAGING_VERIFY_FAILED: return "verify_failed";
    case BC250_FIRMWARE_STAGING_ROLLBACK_FAILED: return "rollback_failed";
    default: return "unknown";
    }
}
