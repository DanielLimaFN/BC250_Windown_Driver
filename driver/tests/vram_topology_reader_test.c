#include "../core/bc250_vram_topology_reader.h"

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
#define CHECK(Expression) do { \
    if (!(Expression)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #Expression); \
        return 1; \
    } \
} while (0)
#endif

typedef struct BC250_TEST_READER {
    uint32_t Values[4];
    uint32_t Calls;
    uint32_t FailCall;
    uint32_t MutateSecondPass;
} BC250_TEST_READER;

static uint32_t Bc250TestRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    BC250_TEST_READER *reader = (BC250_TEST_READER *)Context;
    uint32_t index;
    static const uint32_t offsets[4] = {
        BC250_RCC_CONFIG_MEMSIZE_BYTE_OFFSET,
        BC250_GCMC_VM_FB_LOCATION_BASE_BYTE_OFFSET,
        BC250_GCMC_VM_FB_LOCATION_TOP_BYTE_OFFSET,
        BC250_GCMC_VM_FB_OFFSET_BYTE_OFFSET
    };

    if (reader == 0 || Value == 0) return 0;
    ++reader->Calls;
    if (reader->FailCall != 0 && reader->Calls == reader->FailCall) return 0;
    for (index = 0; index < 4; ++index) {
        if (ByteOffset == offsets[index]) {
            *Value = reader->Values[index];
            if (reader->MutateSecondPass != 0 && reader->Calls > 4u &&
                index == 2u) {
                ++*Value;
            }
            return 1;
        }
    }
    return 0;
}

static void Bc250HealthyReader(
    BC250_TEST_READER *Reader,
    BC250_VRAM_TOPOLOGY_READER_INTERFACE *Interface)
{
    memset(Reader, 0, sizeof(*Reader));
    Reader->Values[0] = 0x200u;
    Reader->Values[1] = 0xF400u;
    Reader->Values[2] = 0xF41Fu;
    Reader->Values[3] = 0x450u;
    memset(Interface, 0, sizeof(*Interface));
    Interface->Read32 = Bc250TestRead32;
    Interface->Context = Reader;
    Interface->IdentityFlags = BC250_TOPOLOGY_READER_IDENTITY_REQUIRED;
}

static int Bc250RunVramTopologyReaderTest(void)
{
    BC250_TEST_READER reader;
    BC250_VRAM_TOPOLOGY_READER_INTERFACE interfaceData;
    BC250_VRAM_TOPOLOGY_READER_EVIDENCE evidence;

    Bc250HealthyReader(&reader, &interfaceData);
    CHECK(Bc250CaptureVramTopologyReadonly(
        &interfaceData, 0x10000000ull, 71u, &evidence) ==
        BC250_VRAM_TOPOLOGY_READER_OK);
    CHECK(reader.Calls == 8u && evidence.ReadCount == 8u);
    CHECK(evidence.StablePasses == 2u);
    CHECK(evidence.AddressSpace.TotalBytes == 0x20000000ull);
    CHECK(evidence.AddressSpace.VramMcStart == 0xF400000000ull);
    CHECK(evidence.AddressSpace.VramMcEnd == 0xF41FFFFFFFull);
    CHECK(evidence.AddressSpace.VramBaseOffset == 0x450000000ull);
    CHECK(evidence.AddressSpace.SnapshotGeneration == 71u);

    Bc250HealthyReader(&reader, &interfaceData);
    interfaceData.IdentityFlags &=
        ~BC250_TOPOLOGY_READER_IDENTITY_MMIO_OWNED;
    CHECK(Bc250CaptureVramTopologyReadonly(
        &interfaceData, 0x10000000ull, 72u, &evidence) ==
        BC250_VRAM_TOPOLOGY_READER_MISSING_IDENTITY);
    CHECK(reader.Calls == 0u && evidence.ReadCount == 0u);

    Bc250HealthyReader(&reader, &interfaceData);
    reader.FailCall = 3u;
    CHECK(Bc250CaptureVramTopologyReadonly(
        &interfaceData, 0x10000000ull, 73u, &evidence) ==
        BC250_VRAM_TOPOLOGY_READER_CALLBACK_FAILED);
    CHECK(reader.Calls == 3u && evidence.ReadCount == 0u);

    Bc250HealthyReader(&reader, &interfaceData);
    reader.MutateSecondPass = 1u;
    CHECK(Bc250CaptureVramTopologyReadonly(
        &interfaceData, 0x10000000ull, 74u, &evidence) ==
        BC250_VRAM_TOPOLOGY_READER_UNSTABLE_SNAPSHOT);
    CHECK(reader.Calls == 8u && evidence.ReadCount == 0u);

    Bc250HealthyReader(&reader, &interfaceData);
    reader.Values[2]--;
    CHECK(Bc250CaptureVramTopologyReadonly(
        &interfaceData, 0x10000000ull, 75u, &evidence) ==
        BC250_VRAM_TOPOLOGY_READER_TOPOLOGY_REJECTED);
    CHECK(reader.Calls == 8u && evidence.ReadCount == 0u);
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunVramTopologyReaderTest() ? 1ul : 0ul);
}
#else
int main(void)
{
    int result = Bc250RunVramTopologyReaderTest();
    if (result == 0) {
        printf("VRAM TOPOLOGY READER: OK (2 passes + fixed offsets)\n");
        printf("Callback sintetico; nenhum MMIO real foi acessado.\n");
        printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    }
    return result;
}
#endif
