#include "../wdk/bc250_rebar_reader.h"

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

static unsigned char g_config[BC250_REBAR_CONFIG_LIMIT];

static NTSTATUS APIENTRY Bc250TestReadDeviceSpace(
    HANDLE DeviceHandle, ULONG DataType, PVOID Buffer, ULONG Offset,
    ULONG Length, PULONG BytesRead)
{
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u ||
        DataType != DXGK_WHICHSPACE_CONFIG || Buffer == NULL ||
        BytesRead == NULL || Offset > sizeof(g_config) - Length) {
        return STATUS_INVALID_PARAMETER;
    }
    memcpy(Buffer, g_config + Offset, Length);
    *BytesRead = Length;
    return STATUS_SUCCESS;
}

static void Bc250ValidRebar(void)
{
    uint32_t *capability;
    memset(g_config, 0, sizeof(g_config));
    capability = (uint32_t *)(void *)(g_config + BC250_REBAR_CONFIG_START);
    capability[0] = PCI_EXPRESS_RESIZABLE_BAR_CAP_ID | (1u << 16);
    capability[1] = (1u << 1) << 4;
    capability[2] = 2u | (2u << 5) | (1u << 8);
    capability[3] = ((1u << 8) | (1u << 9)) << 4;
    capability[4] = 0u | (8u << 8);
}

static int Bc250RunRebarReaderTest(void)
{
    DXGKRNL_INTERFACE interfaceData;
    BC250_RESOLVED_BAR_RESOURCE resolved;
    BC250_REBAR_EVIDENCE evidence;
    NTSTATUS callbackStatus;
    uint32_t *capability;

    memset(&interfaceData, 0, sizeof(interfaceData));
    interfaceData.Size = sizeof(interfaceData);
    interfaceData.DeviceHandle = (HANDLE)(ULONG_PTR)0x250u;
    interfaceData.DxgkCbReadDeviceSpace = Bc250TestReadDeviceSpace;
    memset(&resolved, 0, sizeof(resolved));
    resolved.Index = 0;
    resolved.Length = 0x10000000ull;
    Bc250ValidRebar();
    CHECK(Bc250ReadRebarReadonly(
        &interfaceData, &resolved, 1, 0, &evidence, &callbackStatus) ==
        BC250_REBAR_OK);
    CHECK(evidence.SelectedSizeEncoding == 8u);
    CHECK(evidence.SelectedBytes == 0x10000000ull);
    CHECK(evidence.AssignedBytes == 0x10000000ull);
    CHECK(evidence.EntryCount == 2u && evidence.ReadCount == 3u);

    Bc250ValidRebar();
    resolved.Length = 0x20000000ull;
    CHECK(Bc250ReadRebarReadonly(
        &interfaceData, &resolved, 1, 0, &evidence, &callbackStatus) ==
        BC250_REBAR_SIZE_MISMATCH);
    resolved.Length = 0x10000000ull;
    Bc250ValidRebar();
    capability = (uint32_t *)(void *)(g_config + BC250_REBAR_CONFIG_START);
    capability[3] = (1u << 9) << 4;
    CHECK(Bc250ReadRebarReadonly(
        &interfaceData, &resolved, 1, 0, &evidence, &callbackStatus) ==
        BC250_REBAR_SELECTED_UNSUPPORTED);
    Bc250ValidRebar();
    capability[0] = 0x0001u | (1u << 16) |
        (BC250_REBAR_CONFIG_START << 20);
    CHECK(Bc250ReadRebarReadonly(
        &interfaceData, &resolved, 1, 0, &evidence, &callbackStatus) ==
        BC250_REBAR_CHAIN_LOOP);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunRebarReaderTest() ? 0ul : 1ul);
}
