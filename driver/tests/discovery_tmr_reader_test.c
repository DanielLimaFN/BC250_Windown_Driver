#include "../core/bc250_discovery_tmr_reader.h"

#include <string.h>

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

typedef struct BC250_TEST_TMR_READER {
    uint32_t Values[3];
    uint32_t Calls;
    uint32_t FailCall;
    uint32_t MutateSecondPass;
} BC250_TEST_TMR_READER;

static uint32_t Bc250TestRead32(
    void *Context,
    uint32_t ByteOffset,
    uint32_t *Value)
{
    static const uint32_t offsets[3] = {
        BC250_DRIVER_SCRATCH_0_BYTE_OFFSET,
        BC250_DRIVER_SCRATCH_1_BYTE_OFFSET,
        BC250_DRIVER_SCRATCH_2_BYTE_OFFSET
    };
    BC250_TEST_TMR_READER *reader = (BC250_TEST_TMR_READER *)Context;
    uint32_t i;

    if (reader == NULL || Value == NULL) return 0;
    ++reader->Calls;
    if (reader->FailCall != 0 && reader->Calls == reader->FailCall) return 0;
    for (i = 0; i < 3u; ++i) {
        if (ByteOffset != offsets[i]) continue;
        *Value = reader->Values[i];
        if (reader->MutateSecondPass != 0 && reader->Calls > 3u && i == 2u) {
            ++*Value;
        }
        return 1;
    }
    return 0;
}

static void Bc250HealthyReader(
    BC250_TEST_TMR_READER *Reader,
    BC250_VRAM_TOPOLOGY_READER_INTERFACE *Interface)
{
    memset(Reader, 0, sizeof(*Reader));
    Reader->Values[0] = 0x1FFF0000u;
    Reader->Values[1] = 0u;
    Reader->Values[2] = 0x10000u;
    memset(Interface, 0, sizeof(*Interface));
    Interface->Read32 = Bc250TestRead32;
    Interface->Context = Reader;
    Interface->IdentityFlags = BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
}

static int Bc250RunDiscoveryTmrReaderTest(void)
{
    BC250_TEST_TMR_READER reader;
    BC250_VRAM_TOPOLOGY_READER_INTERFACE interfaceData;
    BC250_DISCOVERY_TMR_EVIDENCE evidence;

    Bc250HealthyReader(&reader, &interfaceData);
    CHECK(Bc250CaptureDiscoveryTmrReadonly(
        &interfaceData, 0x20000000ull, 91, &evidence) ==
        BC250_DISCOVERY_TMR_OK);
    CHECK(reader.Calls == 6u && evidence.ReadCount == 6u);
    CHECK(evidence.StablePasses == 2u);
    CHECK(evidence.Offset == 0x1FFF0000ull);
    CHECK(evidence.Length == 0x10000ull);
    CHECK(evidence.SnapshotGeneration == 91u);

    Bc250HealthyReader(&reader, &interfaceData);
    reader.Values[2] = 0;
    CHECK(Bc250CaptureDiscoveryTmrReadonly(
        &interfaceData, 0x20000000ull, 92, &evidence) ==
        BC250_DISCOVERY_TMR_NOT_REPORTED);
    CHECK(evidence.Length == 0);

    Bc250HealthyReader(&reader, &interfaceData);
    reader.MutateSecondPass = 1;
    CHECK(Bc250CaptureDiscoveryTmrReadonly(
        &interfaceData, 0x20000000ull, 93, &evidence) ==
        BC250_DISCOVERY_TMR_UNSTABLE_SNAPSHOT);

    Bc250HealthyReader(&reader, &interfaceData);
    reader.FailCall = 2;
    CHECK(Bc250CaptureDiscoveryTmrReadonly(
        &interfaceData, 0x20000000ull, 94, &evidence) ==
        BC250_DISCOVERY_TMR_CALLBACK_FAILED);
    CHECK(reader.Calls == 2u);

    Bc250HealthyReader(&reader, &interfaceData);
    reader.Values[0] = 0x1FFFF000u;
    CHECK(Bc250CaptureDiscoveryTmrReadonly(
        &interfaceData, 0x20000000ull, 95, &evidence) ==
        BC250_DISCOVERY_TMR_OUT_OF_RANGE);

    Bc250HealthyReader(&reader, &interfaceData);
    interfaceData.IdentityFlags &=
        ~BC250_TOPOLOGY_READER_IDENTITY_READONLY;
    CHECK(Bc250CaptureDiscoveryTmrReadonly(
        &interfaceData, 0x20000000ull, 96, &evidence) ==
        BC250_DISCOVERY_TMR_MISSING_IDENTITY);
    CHECK(reader.Calls == 0u);
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunDiscoveryTmrReaderTest() ? 1ul : 0ul);
}
#else
int main(void)
{
    return Bc250RunDiscoveryTmrReaderTest();
}
#endif
