#include "../core/bc250_firmware_staging.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    volatile unsigned char *destination = (volatile unsigned char *)Destination;
    const volatile unsigned char *source = (const volatile unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)

typedef struct FAKE_STAGING_TRANSPORT {
    unsigned char Bytes[0x400];
    uint32_t ReadCalls;
    uint32_t WriteCalls;
    uint32_t FailReadCall;
    uint32_t FailWriteCall;
    uint32_t CorruptReadCall;
} FAKE_STAGING_TRANSPORT;

static const unsigned char g_payload[3] = { 'a', 'b', 'c' };
static const unsigned char g_payloadSha256[BC250_FIRMWARE_SHA256_BYTES] = {
    0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
    0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
    0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
    0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
};

static int FakeRead(
    void *TransportContext,
    uint64_t RelativeOffset,
    void *Buffer,
    uint32_t Length)
{
    FAKE_STAGING_TRANSPORT *fake =
        (FAKE_STAGING_TRANSPORT *)TransportContext;
    ++fake->ReadCalls;
    if (RelativeOffset > sizeof(fake->Bytes) ||
        Length > sizeof(fake->Bytes) - (size_t)RelativeOffset) return 0;
    memcpy(Buffer, fake->Bytes + (size_t)RelativeOffset, Length);
    if (fake->CorruptReadCall == fake->ReadCalls && Length != 0) {
        ((unsigned char *)Buffer)[0] ^= 0xFFu;
    }
    return fake->FailReadCall != fake->ReadCalls;
}

static int FakeWrite(
    void *TransportContext,
    uint64_t RelativeOffset,
    const void *Buffer,
    uint32_t Length)
{
    FAKE_STAGING_TRANSPORT *fake =
        (FAKE_STAGING_TRANSPORT *)TransportContext;
    ++fake->WriteCalls;
    if (RelativeOffset > sizeof(fake->Bytes) ||
        Length > sizeof(fake->Bytes) - (size_t)RelativeOffset) return 0;
    memcpy(fake->Bytes + (size_t)RelativeOffset, Buffer, Length);
    return fake->FailWriteCall != fake->WriteCalls;
}

static int BuildInputs(
    FAKE_STAGING_TRANSPORT *Fake,
    BC250_FIRMWARE_STAGING_TRANSPORT *Transport,
    BC250_CP_BOOTSTRAP *Bootstrap,
    BC250_CP_BOOTSTRAP_REPORT *Report,
    BC250_FIRMWARE_STAGING_LEASE *Lease,
    BC250_PSP_LOAD_FW_PLAN *Plan)
{
    BC250_CP_BOOTSTRAP_POLICY policy;
    BC250_CP_FIRMWARE_ENTRY manifest[BC250_CP_BOOTSTRAP_FIRMWARE_COUNT] = {
        { BC250_PSP_FW_CP_PFP, 3u },
        { BC250_PSP_FW_CP_ME, 64u },
        { BC250_PSP_FW_CP_CE, 5u },
        { BC250_PSP_FW_CP_MEC, 0x100u },
        { BC250_PSP_FW_CP_MEC_JT1, 7u }
    };
    BC250_VRAM_ADDRESS_SPACE space;
    BC250_STAGING_LEASE_EVIDENCE evidence;
    BC250_VRAM_STATUS vramStatus;
    BC250_ADDRESS_STATUS addressStatus;
    BC250_PSP_CONTRACT_STATUS pspStatus;
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;

    memset(Fake, 0, sizeof(*Fake));
    Fake->Bytes[0x20] = 0x11u;
    Fake->Bytes[0x21] = 0x22u;
    Fake->Bytes[0x22] = 0x33u;
    memset(Transport, 0, sizeof(*Transport));
    Transport->Read = FakeRead;
    Transport->Write = FakeWrite;
    Transport->Context = Fake;
    Transport->IdentityFlags = BC250_STAGING_TRANSPORT_REQUIRED;

    policy.TmrBytes = 0x1000ull;
    policy.TmrAlignment = 0x1000ull;
    policy.FirmwareAlignment = 0x10ull;
    if (Bc250CpBootstrapInit(
            Bootstrap, 0x10000ull, 0x8000ull, &vramStatus) !=
        BC250_CP_BOOTSTRAP_OK) return 0;
    memset(&space, 0, sizeof(space));
    space.TotalBytes = 0x10000ull;
    space.CpuVisibleBytes = 0x8000ull;
    space.VramMcStart = 0xF400000000ull;
    space.VramMcEnd = 0xF40000FFFFull;
    space.VramBaseOffset = 0x450000000ull;
    space.EvidenceFlags = BC250_ADDR_EVIDENCE_REQUIRED;
    space.SnapshotGeneration = 7;
    if (Bc250CpBootstrapSealInventory(
            Bootstrap, &space, &vramStatus) != BC250_CP_BOOTSTRAP_OK) return 0;
    if (Bc250CpBootstrapBuildPreflight(
            Bootstrap, &policy, manifest, BC250_CP_BOOTSTRAP_FIRMWARE_COUNT,
            Report, &vramStatus, &addressStatus, &pspStatus) !=
        BC250_CP_BOOTSTRAP_OK) return 0;
    evidence.EvidenceFlags = BC250_STAGING_EVIDENCE_REQUIRED;
    evidence.EvidenceGeneration = 7;
    if (Bc250IssueFirmwareStagingLease(
            Bootstrap, Report, &evidence, Lease,
            &bootstrapStatus, &vramStatus) != BC250_STAGING_LEASE_OK) return 0;
    Transport->WindowOffset = Lease->Offset - 0x20ull;
    Transport->WindowBytes = sizeof(Fake->Bytes);
    *Plan = Report->FirmwarePlans[0];
    return 1;
}

static int BufferZero(const unsigned char *Buffer, size_t Length)
{
    size_t i;
    for (i = 0; i < Length; ++i) if (Buffer[i] != 0) return 0;
    return 1;
}

static int RunFirmwareStagingTest(void)
{
    static FAKE_STAGING_TRANSPORT fake;
    static BC250_FIRMWARE_STAGING_TRANSPORT transport;
    static BC250_FIRMWARE_STAGING_CONTEXT context;
    static BC250_CP_BOOTSTRAP bootstrap;
    static BC250_CP_BOOTSTRAP_REPORT report;
    static BC250_FIRMWARE_STAGING_LEASE lease;
    static BC250_PSP_LOAD_FW_PLAN plan;
    static unsigned char rollback[sizeof(g_payload)];
    static unsigned char verify[2];
    static unsigned char badDigest[BC250_FIRMWARE_SHA256_BYTES];
    static unsigned char boundaryPayload[64];
    static unsigned char boundaryRollback[64];
    static unsigned char boundaryVerify[17];
    static const unsigned char boundaryDigest[BC250_FIRMWARE_SHA256_BYTES] = {
        0xFD, 0xEA, 0xB9, 0xAC, 0xF3, 0x71, 0x03, 0x62,
        0xBD, 0x26, 0x58, 0xCD, 0xC9, 0xA2, 0x9E, 0x8F,
        0x9C, 0x75, 0x7F, 0xCF, 0x98, 0x11, 0x60, 0x3A,
        0x8C, 0x44, 0x7C, 0xD1, 0xD9, 0x15, 0x11, 0x08
    };
    BC250_STAGING_LEASE_EVIDENCE evidence;
    BC250_CP_BOOTSTRAP_STATUS bootstrapStatus;
    BC250_VRAM_STATUS vramStatus;
    uint32_t i;

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    evidence.EvidenceFlags = BC250_STAGING_EVIDENCE_REQUIRED &
        ~BC250_STAGING_EVIDENCE_FREE_SPACE_RUNTIME;
    evidence.EvidenceGeneration = report.SnapshotGeneration;
    CHECK(Bc250IssueFirmwareStagingLease(
        &bootstrap, &report, &evidence, &lease,
        &bootstrapStatus, &vramStatus) ==
        BC250_STAGING_LEASE_MISSING_EVIDENCE);
    CHECK(lease.LeaseSeal == 0);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    bootstrap.AddressSpace.SnapshotGeneration++;
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload), g_payloadSha256,
        rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_LEASE_REJECTED);
    CHECK(fake.ReadCalls == 0u && fake.WriteCalls == 0u);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    transport.IdentityFlags &= ~BC250_STAGING_TRANSPORT_COHERENT_READBACK;
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload), g_payloadSha256,
        rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_TRANSPORT_REJECTED);
    CHECK(fake.ReadCalls == 0u && fake.WriteCalls == 0u);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    memset(badDigest, 0, sizeof(badDigest));
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload), badDigest,
        rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_IDENTITY_REJECTED);
    CHECK(fake.ReadCalls == 0u && fake.WriteCalls == 0u);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, rollback, sizeof(rollback) - 1u,
        verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_SCRATCH_TOO_SMALL);
    CHECK(fake.ReadCalls == 0u && fake.WriteCalls == 0u);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, (void *)g_payload, sizeof(g_payload),
        verify, sizeof(verify)) == BC250_FIRMWARE_STAGING_BAD_ARGUMENT);
    CHECK(fake.ReadCalls == 0u && fake.WriteCalls == 0u);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, rollback, sizeof(rollback),
        rollback, sizeof(rollback)) == BC250_FIRMWARE_STAGING_BAD_ARGUMENT);
    CHECK(fake.ReadCalls == 0u && fake.WriteCalls == 0u);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    for (i = 0; i < sizeof(boundaryPayload); ++i) {
        boundaryPayload[i] = (unsigned char)i;
    }
    plan = report.FirmwarePlans[1];
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        boundaryPayload, sizeof(boundaryPayload), boundaryDigest,
        boundaryRollback, sizeof(boundaryRollback),
        boundaryVerify, sizeof(boundaryVerify)) ==
        BC250_FIRMWARE_STAGING_OK);
    CHECK(Bc250FirmwareRestoreOriginal(&context) ==
        BC250_FIRMWARE_STAGING_OK);
    CHECK(Bc250FirmwareStagingClose(&context) ==
        BC250_FIRMWARE_STAGING_OK);
    CHECK(BufferZero(boundaryRollback, sizeof(boundaryRollback)));
    CHECK(BufferZero(boundaryVerify, sizeof(boundaryVerify)));

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_OK);
    CHECK(context.State == BC250_FIRMWARE_STAGING_STAGED);
    CHECK(fake.Bytes[0x20] == 'a' && fake.Bytes[0x21] == 'b' &&
          fake.Bytes[0x22] == 'c');
    CHECK(Bc250FirmwareStagingClose(&context) ==
        BC250_FIRMWARE_STAGING_WRONG_STATE);
    CHECK(Bc250FirmwareRestoreOriginal(&context) ==
        BC250_FIRMWARE_STAGING_OK);
    CHECK(fake.Bytes[0x20] == 0x11u && fake.Bytes[0x21] == 0x22u &&
          fake.Bytes[0x22] == 0x33u);
    CHECK(Bc250FirmwareStagingClose(&context) ==
        BC250_FIRMWARE_STAGING_OK);
    CHECK(BufferZero(rollback, sizeof(rollback)));
    CHECK(BufferZero(verify, sizeof(verify)));

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    fake.FailWriteCall = 1;
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_WRITE_FAILED);
    CHECK(context.State == BC250_FIRMWARE_STAGING_RESTORED);
    CHECK(fake.WriteCalls == 2u);
    CHECK(fake.Bytes[0x20] == 0x11u && fake.Bytes[0x21] == 0x22u &&
          fake.Bytes[0x22] == 0x33u);
    CHECK(Bc250FirmwareStagingClose(&context) ==
        BC250_FIRMWARE_STAGING_OK);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    fake.CorruptReadCall = 2;
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_VERIFY_FAILED);
    CHECK(context.State == BC250_FIRMWARE_STAGING_RESTORED);
    CHECK(fake.Bytes[0x20] == 0x11u && fake.Bytes[0x21] == 0x22u &&
          fake.Bytes[0x22] == 0x33u);
    CHECK(Bc250FirmwareStagingClose(&context) ==
        BC250_FIRMWARE_STAGING_OK);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_OK);
    fake.FailWriteCall = fake.WriteCalls + 1u;
    CHECK(Bc250FirmwareRestoreOriginal(&context) ==
        BC250_FIRMWARE_STAGING_ROLLBACK_FAILED);
    CHECK(context.State == BC250_FIRMWARE_STAGING_CONTAMINATED);
    CHECK(Bc250FirmwareStagingClose(&context) ==
        BC250_FIRMWARE_STAGING_WRONG_STATE);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    lease.EvidenceFlags ^= BC250_STAGING_EVIDENCE_SCANOUT_EXCLUDED_RUNTIME;
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_LEASE_REJECTED);
    CHECK(fake.ReadCalls == 0u && fake.WriteCalls == 0u);

    CHECK(BuildInputs(
        &fake, &transport, &bootstrap, &report, &lease, &plan));
    plan.Binding.Offset = 0x1400ull;
    CHECK(Bc250FirmwareStageVerified(
        &context, &transport, &bootstrap, &report, &lease, &plan,
        g_payload, sizeof(g_payload),
        g_payloadSha256, rollback, sizeof(rollback), verify, sizeof(verify)) ==
        BC250_FIRMWARE_STAGING_LEASE_REJECTED);
    CHECK(fake.ReadCalls == 0u && fake.WriteCalls == 0u);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(RunFirmwareStagingTest() ? 0ul : 1ul);
}
