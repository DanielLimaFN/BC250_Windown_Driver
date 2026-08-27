#include "../wdk/bc250_pci_bar_reader.h"

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
    unsigned char *destination = (unsigned char *)Destination;
    const unsigned char *source = (const unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)

static PCI_COMMON_CONFIG g_config;
static ULONG g_partialBytes;

static NTSTATUS APIENTRY Bc250TestReadDeviceSpace(
    HANDLE DeviceHandle, ULONG DataType, PVOID Buffer, ULONG Offset,
    ULONG Length, PULONG BytesRead)
{
    ULONG bytes;
    if (DeviceHandle != (HANDLE)(ULONG_PTR)0x250u ||
        DataType != DXGK_WHICHSPACE_CONFIG || Offset != 0 ||
        Length != PCI_COMMON_HDR_LENGTH || Buffer == NULL || BytesRead == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    bytes = g_partialBytes != 0 ? g_partialBytes : PCI_COMMON_HDR_LENGTH;
    memcpy(Buffer, &g_config, bytes);
    *BytesRead = bytes;
    return STATUS_SUCCESS;
}

static void Bc250ValidConfig(void)
{
    memset(&g_config, 0, sizeof(g_config));
    g_config.VendorID = BC250_PCI_VENDOR_ID;
    g_config.DeviceID = BC250_PCI_DEVICE_ID;
    g_config.HeaderType = PCI_DEVICE_TYPE;
    g_config.u.type0.BaseAddresses[0] = 0xC000000Cu;
    g_config.u.type0.BaseAddresses[1] = 0;
    g_config.u.type0.BaseAddresses[2] = 0xD0000000u;
    g_config.u.type0.BaseAddresses[4] = 0x0000E001u;
    g_config.u.type0.BaseAddresses[5] = 0xFE800000u;
    g_partialBytes = 0;
}

static int Bc250RunPciBarReaderTest(void)
{
    DXGKRNL_INTERFACE interfaceData;
    BC250_PCI_BAR_RESOURCE bars[BC250_RESOURCE_MAX_BARS];
    size_t count;
    ULONG bytesRead;
    NTSTATUS callbackStatus;

    memset(&interfaceData, 0, sizeof(interfaceData));
    interfaceData.Size = sizeof(interfaceData);
    interfaceData.DeviceHandle = (HANDLE)(ULONG_PTR)0x250u;
    interfaceData.DxgkCbReadDeviceSpace = Bc250TestReadDeviceSpace;
    Bc250ValidConfig();
    CHECK(Bc250ReadPciBarsReadonly(
        &interfaceData, bars, RTL_NUMBER_OF(bars), &count, &bytesRead,
        &callbackStatus) == BC250_PCI_BAR_READ_OK);
    CHECK(count == 3 && bytesRead == PCI_COMMON_HDR_LENGTH);
    CHECK(bars[0].Index == 0 && bars[0].Base == 0xC0000000ull);
    CHECK(bars[1].Index == 2 && bars[1].Base == 0xD0000000ull);
    CHECK(bars[2].Index == 5 && bars[2].Base == 0xFE800000ull);

    Bc250ValidConfig();
    g_partialBytes = PCI_COMMON_HDR_LENGTH - 1u;
    CHECK(Bc250ReadPciBarsReadonly(
        &interfaceData, bars, RTL_NUMBER_OF(bars), &count, &bytesRead,
        &callbackStatus) == BC250_PCI_BAR_READ_PARTIAL);
    Bc250ValidConfig();
    g_config.DeviceID = 0xFFFFu;
    CHECK(Bc250ReadPciBarsReadonly(
        &interfaceData, bars, RTL_NUMBER_OF(bars), &count, &bytesRead,
        &callbackStatus) == BC250_PCI_BAR_READ_WRONG_DEVICE);
    Bc250ValidConfig();
    g_config.u.type0.BaseAddresses[2] = 0xD0000001u;
    CHECK(Bc250ReadPciBarsReadonly(
        &interfaceData, bars, RTL_NUMBER_OF(bars), &count, &bytesRead,
        &callbackStatus) == BC250_PCI_BAR_READ_OK);
    CHECK(count == 2 && bars[0].Index == 0 && bars[1].Index == 5);
    Bc250ValidConfig();
    g_config.u.type0.BaseAddresses[5] = 0xFE800004u;
    CHECK(Bc250ReadPciBarsReadonly(
        &interfaceData, bars, RTL_NUMBER_OF(bars), &count, &bytesRead,
        &callbackStatus) == BC250_PCI_BAR_READ_TRUNCATED_64BIT);
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunPciBarReaderTest() ? 0ul : 1ul);
}
