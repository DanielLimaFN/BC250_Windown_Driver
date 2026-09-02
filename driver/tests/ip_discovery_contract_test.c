#include "../core/bc250_ip_discovery_contract.h"

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

#define TEST_BLOB_BYTES 512u
#define TEST_TABLE_OFFSET 0x80u
#define TEST_DIE_OFFSET 0xD0u
#define TEST_FIRST_IP_OFFSET (TEST_DIE_OFFSET + 4u)
#define TEST_OSSSYS_IP_OFFSET (TEST_FIRST_IP_OFFSET + 12u)

static void WriteLe16(unsigned char *Bytes, size_t Offset, uint16_t Value)
{
    Bytes[Offset] = (unsigned char)Value;
    Bytes[Offset + 1u] = (unsigned char)(Value >> 8);
}

static void WriteLe32(unsigned char *Bytes, size_t Offset, uint32_t Value)
{
    Bytes[Offset] = (unsigned char)Value;
    Bytes[Offset + 1u] = (unsigned char)(Value >> 8);
    Bytes[Offset + 2u] = (unsigned char)(Value >> 16);
    Bytes[Offset + 3u] = (unsigned char)(Value >> 24);
}

static uint16_t ByteSum(const unsigned char *Bytes, size_t Offset, size_t Length)
{
    uint16_t sum = 0;
    size_t index;
    for (index = 0; index < Length; ++index) {
        sum = (uint16_t)(sum + Bytes[Offset + index]);
    }
    return sum;
}

static void FinalizeChecksums(unsigned char *Blob, uint16_t TableSize,
                              uint16_t BinarySize)
{
    WriteLe16(Blob, TEST_TABLE_OFFSET + 6u, TableSize);
    WriteLe16(Blob, 16u, TableSize);
    WriteLe16(Blob, 14u,
        ByteSum(Blob, TEST_TABLE_OFFSET, TableSize));
    WriteLe16(Blob, 10u, BinarySize);
    WriteLe16(Blob, 8u, ByteSum(Blob, 10u, BinarySize - 10u));
}

static size_t AppendIp(unsigned char *Blob, size_t Offset, uint16_t HardwareId,
                       uint8_t Major, uint8_t Minor, uint8_t Revision,
                       uint8_t BaseCount, uint8_t Harvest)
{
    uint8_t index;
    WriteLe16(Blob, Offset, HardwareId);
    Blob[Offset + 2u] = 0;
    Blob[Offset + 3u] = BaseCount;
    Blob[Offset + 4u] = Major;
    Blob[Offset + 5u] = Minor;
    Blob[Offset + 6u] = Revision;
    Blob[Offset + 7u] = Harvest;
    for (index = 0; index < BaseCount; ++index) {
        WriteLe32(Blob, Offset + 8u + (size_t)index * 4u,
                  0x1000u + (uint32_t)index * 0x1000u);
    }
    return Offset + 8u + (size_t)BaseCount * 4u;
}

static uint16_t BuildBlob(unsigned char *Blob, uint8_t GcMajor,
                          uint8_t DuplicateGc, uint8_t Harvest)
{
    size_t cursor;
    uint16_t tableSize;
    uint16_t binarySize;

    memset(Blob, 0, TEST_BLOB_BYTES);
    WriteLe32(Blob, 0, BC250_DISCOVERY_BINARY_SIGNATURE);
    WriteLe16(Blob, 4, 1u);
    WriteLe16(Blob, 6, 0u);
    WriteLe16(Blob, 12, TEST_TABLE_OFFSET);
    WriteLe32(Blob, TEST_TABLE_OFFSET, BC250_DISCOVERY_IP_SIGNATURE);
    WriteLe16(Blob, TEST_TABLE_OFFSET + 4u, 2u);
    WriteLe32(Blob, TEST_TABLE_OFFSET + 8u, 0x250u);
    WriteLe16(Blob, TEST_TABLE_OFFSET + 12u, 1u);
    WriteLe16(Blob, TEST_TABLE_OFFSET + 14u, 0u);
    WriteLe16(Blob, TEST_TABLE_OFFSET + 16u, TEST_DIE_OFFSET);
    WriteLe16(Blob, TEST_DIE_OFFSET, 0u);
    WriteLe16(Blob, TEST_DIE_OFFSET + 2u,
              (uint16_t)(DuplicateGc != 0 ? 4u : 3u));
    cursor = TEST_DIE_OFFSET + 4u;
    cursor = AppendIp(Blob, cursor, 42u, 5u, 0u, 0u, 1u, 0u);
    cursor = AppendIp(Blob, cursor, BC250_DISCOVERY_OSSSYS_HWID,
                      5u, 0u, 0u, 1u, 0u);
    cursor = AppendIp(Blob, cursor, BC250_DISCOVERY_GC_HWID,
                      GcMajor, 1u, 3u, 3u, Harvest);
    if (DuplicateGc != 0) {
        cursor = AppendIp(Blob, cursor, BC250_DISCOVERY_GC_HWID,
                          10u, 1u, 3u, 1u, 0u);
    }
    tableSize = (uint16_t)(cursor - TEST_TABLE_OFFSET);
    binarySize = (uint16_t)cursor;
    FinalizeChecksums(Blob, tableSize, binarySize);
    return binarySize;
}

static int Bc250RunIpDiscoveryContractTest(void)
{
    unsigned char blob[TEST_BLOB_BYTES];
    BC250_GC_DISCOVERY_IDENTITY identity;
    BC250_IP_DISCOVERY_IDENTITY osssys;
    uint16_t binarySize;
    uint16_t tableSize;

    binarySize = BuildBlob(blob, 10u, 0u, 0u);
    CHECK(Bc250ParseIpDiscoveryGcIdentity(blob, binarySize, &identity) ==
          BC250_IP_DISCOVERY_OK);
    CHECK(identity.HardwareId == BC250_DISCOVERY_GC_HWID);
    CHECK(identity.Major == 10u && identity.Minor == 1u &&
          identity.Revision == 3u);
    CHECK(identity.Instance == 0u && identity.DieCount == 1u);
    CHECK(identity.BaseAddressCount == 3u && identity.BaseAddressWidth == 4u);
    CHECK(identity.BaseAddresses[0] == 0x1000u &&
          identity.BaseAddresses[2] == 0x3000u);
    CHECK(Bc250ParseIpDiscoveryIdentity(blob, binarySize,
          BC250_DISCOVERY_OSSSYS_HWID, 0u, &osssys) ==
          BC250_IP_DISCOVERY_OK);
    CHECK(osssys.HardwareId == BC250_DISCOVERY_OSSSYS_HWID);
    CHECK(osssys.Major == 5u && osssys.Minor == 0u &&
          osssys.Revision == 0u);
    CHECK(osssys.BaseAddressCount == 1u &&
          osssys.BaseAddresses[0] == 0x1000u);

    WriteLe16(blob, TEST_OSSSYS_IP_OFFSET, 42u);
    FinalizeChecksums(blob, (uint16_t)(binarySize - TEST_TABLE_OFFSET),
                      binarySize);
    CHECK(Bc250ParseIpDiscoveryIdentity(blob, binarySize,
          BC250_DISCOVERY_OSSSYS_HWID, 0u, &osssys) ==
          BC250_IP_DISCOVERY_TARGET_NOT_FOUND);

    binarySize = BuildBlob(blob, 10u, 0u, 0u);
    WriteLe16(blob, TEST_FIRST_IP_OFFSET, BC250_DISCOVERY_OSSSYS_HWID);
    FinalizeChecksums(blob, (uint16_t)(binarySize - TEST_TABLE_OFFSET),
                      binarySize);
    CHECK(Bc250ParseIpDiscoveryIdentity(blob, binarySize,
          BC250_DISCOVERY_OSSSYS_HWID, 0u, &osssys) ==
          BC250_IP_DISCOVERY_TARGET_DUPLICATE);

    binarySize = BuildBlob(blob, 10u, 0u, 0u);
    blob[TEST_OSSSYS_IP_OFFSET + 7u] = 1u;
    FinalizeChecksums(blob, (uint16_t)(binarySize - TEST_TABLE_OFFSET),
                      binarySize);
    CHECK(Bc250ParseIpDiscoveryIdentity(blob, binarySize,
          BC250_DISCOVERY_OSSSYS_HWID, 0u, &osssys) ==
          BC250_IP_DISCOVERY_TARGET_HARVESTED);

    binarySize = BuildBlob(blob, 11u, 0u, 0u);
    CHECK(Bc250ParseIpDiscoveryGcIdentity(blob, binarySize, &identity) ==
          BC250_IP_DISCOVERY_GC_VERSION_MISMATCH);

    binarySize = BuildBlob(blob, 10u, 1u, 0u);
    CHECK(Bc250ParseIpDiscoveryGcIdentity(blob, binarySize, &identity) ==
          BC250_IP_DISCOVERY_GC_DUPLICATE);

    binarySize = BuildBlob(blob, 10u, 0u, 1u);
    CHECK(Bc250ParseIpDiscoveryGcIdentity(blob, binarySize, &identity) ==
          BC250_IP_DISCOVERY_GC_HARVESTED);

    binarySize = BuildBlob(blob, 10u, 0u, 0u);
    blob[TEST_DIE_OFFSET + 5u] ^= 1u;
    WriteLe16(blob, 8u, ByteSum(blob, 10u, binarySize - 10u));
    CHECK(Bc250ParseIpDiscoveryGcIdentity(blob, binarySize, &identity) ==
          BC250_IP_DISCOVERY_BAD_TABLE_CHECKSUM);

    binarySize = BuildBlob(blob, 10u, 0u, 0u);
    tableSize = (uint16_t)(binarySize - TEST_TABLE_OFFSET);
    WriteLe16(blob, TEST_TABLE_OFFSET + 16u, binarySize - 2u);
    FinalizeChecksums(blob, tableSize, binarySize);
    CHECK(Bc250ParseIpDiscoveryGcIdentity(blob, binarySize, &identity) ==
          BC250_IP_DISCOVERY_DIE_BOUNDS);

    binarySize = BuildBlob(blob, 10u, 0u, 0u);
    blob[0] = 0;
    CHECK(Bc250ParseIpDiscoveryGcIdentity(blob, binarySize, &identity) ==
          BC250_IP_DISCOVERY_BAD_BINARY_SIGNATURE);
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunIpDiscoveryContractTest() ? 1ul : 0ul);
}
#else
int main(void)
{
    int result = Bc250RunIpDiscoveryContractTest();
    if (result == 0) {
        printf("IP DISCOVERY CONTRACT: OK (GC 10.1.3 fail-closed)\n");
        printf("Blob sintetico; nenhuma VRAM ou MMIO foi acessada.\n");
        printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    }
    return result;
}
#endif
