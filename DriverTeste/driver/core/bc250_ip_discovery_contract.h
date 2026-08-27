#ifndef BC250_IP_DISCOVERY_CONTRACT_H
#define BC250_IP_DISCOVERY_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
typedef UINT16 uint16_t;
typedef UINT8 uint8_t;
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_DISCOVERY_BINARY_SIGNATURE 0x28211407u
#define BC250_DISCOVERY_IP_SIGNATURE     0x53445049u
#define BC250_DISCOVERY_GC_HWID          11u
#define BC250_DISCOVERY_MAX_DIES         16u
#define BC250_DISCOVERY_MAX_BASES        8u

typedef enum BC250_IP_DISCOVERY_STATUS {
    BC250_IP_DISCOVERY_OK = 0,
    BC250_IP_DISCOVERY_BAD_ARGUMENT,
    BC250_IP_DISCOVERY_BINARY_TOO_SMALL,
    BC250_IP_DISCOVERY_BAD_BINARY_SIGNATURE,
    BC250_IP_DISCOVERY_UNSUPPORTED_BINARY_VERSION,
    BC250_IP_DISCOVERY_BINARY_BOUNDS,
    BC250_IP_DISCOVERY_BAD_BINARY_CHECKSUM,
    BC250_IP_DISCOVERY_TABLE_MISSING,
    BC250_IP_DISCOVERY_TABLE_BOUNDS,
    BC250_IP_DISCOVERY_BAD_TABLE_SIGNATURE,
    BC250_IP_DISCOVERY_UNSUPPORTED_TABLE_VERSION,
    BC250_IP_DISCOVERY_BAD_TABLE_CHECKSUM,
    BC250_IP_DISCOVERY_DIE_BOUNDS,
    BC250_IP_DISCOVERY_IP_BOUNDS,
    BC250_IP_DISCOVERY_GC_NOT_FOUND,
    BC250_IP_DISCOVERY_GC_DUPLICATE,
    BC250_IP_DISCOVERY_GC_HARVESTED,
    BC250_IP_DISCOVERY_GC_VERSION_MISMATCH
} BC250_IP_DISCOVERY_STATUS;

typedef struct BC250_GC_DISCOVERY_IDENTITY {
    uint16_t BinaryVersionMajor;
    uint16_t BinaryVersionMinor;
    uint16_t TableVersion;
    uint16_t DieCount;
    uint16_t DieId;
    uint16_t HardwareId;
    uint8_t Instance;
    uint8_t Major;
    uint8_t Minor;
    uint8_t Revision;
    uint8_t BaseAddressCount;
    uint8_t BaseAddressWidth;
    uint16_t Reserved;
    uint64_t BaseAddresses[BC250_DISCOVERY_MAX_BASES];
    uint32_t BinaryBytes;
    uint32_t TableBytes;
} BC250_GC_DISCOVERY_IDENTITY;

BC250_IP_DISCOVERY_STATUS Bc250ParseIpDiscoveryGcIdentity(
    const void *Binary,
    size_t BinaryBufferBytes,
    BC250_GC_DISCOVERY_IDENTITY *Identity);

const char *Bc250IpDiscoveryStatusName(BC250_IP_DISCOVERY_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
