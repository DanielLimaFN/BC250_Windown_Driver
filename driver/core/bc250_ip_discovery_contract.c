#include "bc250_ip_discovery_contract.h"

#include <string.h>

#define BC250_BINARY_V1_HEADER_BYTES 60u
#define BC250_BINARY_V2_HEADER_BYTES 16u
#define BC250_TABLE_INFO_BYTES 8u
#define BC250_IP_HEADER_BYTES 80u
#define BC250_DIE_HEADER_BYTES 4u
#define BC250_IP_RECORD_BYTES 8u

static uint16_t Bc250ReadLe16(const uint8_t *Bytes)
{
    return (uint16_t)((uint16_t)Bytes[0] | ((uint16_t)Bytes[1] << 8));
}

static uint32_t Bc250ReadLe32(const uint8_t *Bytes)
{
    return (uint32_t)Bytes[0] | ((uint32_t)Bytes[1] << 8) |
        ((uint32_t)Bytes[2] << 16) | ((uint32_t)Bytes[3] << 24);
}

static uint64_t Bc250ReadLe64(const uint8_t *Bytes)
{
    return (uint64_t)Bc250ReadLe32(Bytes) |
        ((uint64_t)Bc250ReadLe32(Bytes + 4) << 32);
}

static uint16_t Bc250ByteSum(const uint8_t *Bytes, size_t Length)
{
    uint16_t sum = 0;
    size_t index;
    for (index = 0; index < Length; ++index) sum = (uint16_t)(sum + Bytes[index]);
    return sum;
}

static uint32_t Bc250RangeInside(size_t Offset, size_t Length, size_t Limit)
{
    return Offset <= Limit && Length <= Limit - Offset;
}

BC250_IP_DISCOVERY_STATUS Bc250ParseIpDiscoveryIdentity(
    const void *Binary,
    size_t BinaryBufferBytes,
    uint16_t HardwareId,
    uint8_t Instance,
    BC250_IP_DISCOVERY_IDENTITY *Identity)
{
    const uint8_t *bytes = (const uint8_t *)Binary;
    BC250_IP_DISCOVERY_IDENTITY candidate;
    uint16_t binaryMajor;
    uint16_t binaryMinor;
    uint16_t binaryChecksum;
    uint16_t binarySize;
    size_t tableListOffset;
    uint16_t tableCount;
    size_t minimumBinaryHeader;
    uint16_t tableOffset;
    uint16_t tableChecksum;
    uint16_t tableSize;
    uint16_t tableVersion;
    uint16_t dieCount;
    uint32_t targetCount = 0;
    uint32_t targetHarvested = 0;
    uint16_t dieIndex;

    if (Identity != 0) memset(Identity, 0, sizeof(*Identity));
    if (Binary == 0 || Identity == 0) return BC250_IP_DISCOVERY_BAD_ARGUMENT;
    if (BinaryBufferBytes < 12u) return BC250_IP_DISCOVERY_BINARY_TOO_SMALL;
    if (Bc250ReadLe32(bytes) != BC250_DISCOVERY_BINARY_SIGNATURE) {
        return BC250_IP_DISCOVERY_BAD_BINARY_SIGNATURE;
    }
    binaryMajor = Bc250ReadLe16(bytes + 4);
    binaryMinor = Bc250ReadLe16(bytes + 6);
    binaryChecksum = Bc250ReadLe16(bytes + 8);
    binarySize = Bc250ReadLe16(bytes + 10);
    if (binaryMajor <= 1u) {
        tableListOffset = 12u;
        tableCount = 6u;
        minimumBinaryHeader = BC250_BINARY_V1_HEADER_BYTES;
        if (BinaryBufferBytes < BC250_BINARY_V1_HEADER_BYTES) {
            return BC250_IP_DISCOVERY_BINARY_TOO_SMALL;
        }
    } else if (binaryMajor == 2u) {
        if (BinaryBufferBytes < BC250_BINARY_V2_HEADER_BYTES) {
            return BC250_IP_DISCOVERY_BINARY_TOO_SMALL;
        }
        tableCount = Bc250ReadLe16(bytes + 12);
        tableListOffset = 16u;
        minimumBinaryHeader = tableListOffset +
            (size_t)tableCount * BC250_TABLE_INFO_BYTES;
        if (tableCount == 0 ||
            !Bc250RangeInside(tableListOffset,
                (size_t)tableCount * BC250_TABLE_INFO_BYTES,
                BinaryBufferBytes)) {
            return BC250_IP_DISCOVERY_BINARY_BOUNDS;
        }
    } else {
        return BC250_IP_DISCOVERY_UNSUPPORTED_BINARY_VERSION;
    }
    if (binarySize < minimumBinaryHeader ||
        binarySize > BinaryBufferBytes) {
        return BC250_IP_DISCOVERY_BINARY_BOUNDS;
    }
    if (Bc250ByteSum(bytes + 10, binarySize - 10u) != binaryChecksum) {
        return BC250_IP_DISCOVERY_BAD_BINARY_CHECKSUM;
    }
    if (tableCount < 1u) return BC250_IP_DISCOVERY_TABLE_MISSING;
    tableOffset = Bc250ReadLe16(bytes + tableListOffset);
    tableChecksum = Bc250ReadLe16(bytes + tableListOffset + 2u);
    if (tableOffset == 0) return BC250_IP_DISCOVERY_TABLE_MISSING;
    if (!Bc250RangeInside(tableOffset, BC250_IP_HEADER_BYTES, binarySize)) {
        return BC250_IP_DISCOVERY_TABLE_BOUNDS;
    }
    if (Bc250ReadLe32(bytes + tableOffset) != BC250_DISCOVERY_IP_SIGNATURE) {
        return BC250_IP_DISCOVERY_BAD_TABLE_SIGNATURE;
    }
    tableVersion = Bc250ReadLe16(bytes + tableOffset + 4u);
    tableSize = Bc250ReadLe16(bytes + tableOffset + 6u);
    if (tableVersion > 4u) {
        return BC250_IP_DISCOVERY_UNSUPPORTED_TABLE_VERSION;
    }
    if (tableSize < BC250_IP_HEADER_BYTES ||
        !Bc250RangeInside(tableOffset, tableSize, binarySize)) {
        return BC250_IP_DISCOVERY_TABLE_BOUNDS;
    }
    if (Bc250ByteSum(bytes + tableOffset, tableSize) != tableChecksum) {
        return BC250_IP_DISCOVERY_BAD_TABLE_CHECKSUM;
    }
    dieCount = Bc250ReadLe16(bytes + tableOffset + 12u);
    if (dieCount == 0 || dieCount > BC250_DISCOVERY_MAX_DIES) {
        return BC250_IP_DISCOVERY_DIE_BOUNDS;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.BinaryVersionMajor = binaryMajor;
    candidate.BinaryVersionMinor = binaryMinor;
    candidate.TableVersion = tableVersion;
    candidate.DieCount = dieCount;
    candidate.BinaryBytes = binarySize;
    candidate.TableBytes = tableSize;
    for (dieIndex = 0; dieIndex < dieCount; ++dieIndex) {
        size_t dieInfo = (size_t)tableOffset + 14u +
            (size_t)dieIndex * 4u;
        uint16_t dieId = Bc250ReadLe16(bytes + dieInfo);
        uint16_t dieOffset = Bc250ReadLe16(bytes + dieInfo + 2u);
        uint16_t ipCount;
        size_t ipOffset;
        uint16_t ipIndex;

        if (dieOffset < (size_t)tableOffset + BC250_IP_HEADER_BYTES ||
            !Bc250RangeInside(dieOffset, BC250_DIE_HEADER_BYTES,
                              (size_t)tableOffset + tableSize)) {
            return BC250_IP_DISCOVERY_DIE_BOUNDS;
        }
        ipCount = Bc250ReadLe16(bytes + dieOffset + 2u);
        ipOffset = (size_t)dieOffset + BC250_DIE_HEADER_BYTES;
        for (ipIndex = 0; ipIndex < ipCount; ++ipIndex) {
            uint16_t hwId;
            uint8_t baseCount;
            size_t baseWidth = (tableVersion == 4u &&
                (bytes[tableOffset + 78u] & 1u) != 0) ? 8u : 4u;
            size_t recordBytes;
            uint8_t baseIndex;

            if (!Bc250RangeInside(ipOffset, BC250_IP_RECORD_BYTES,
                                  (size_t)tableOffset + tableSize)) {
                return BC250_IP_DISCOVERY_IP_BOUNDS;
            }
            hwId = Bc250ReadLe16(bytes + ipOffset);
            baseCount = bytes[ipOffset + 3u];
            if (baseCount >
                ((size_t)-1 - BC250_IP_RECORD_BYTES) / baseWidth) {
                return BC250_IP_DISCOVERY_IP_BOUNDS;
            }
            recordBytes = BC250_IP_RECORD_BYTES +
                (size_t)baseCount * baseWidth;
            if (!Bc250RangeInside(ipOffset, recordBytes,
                                  (size_t)tableOffset + tableSize)) {
                return BC250_IP_DISCOVERY_IP_BOUNDS;
            }
            if (hwId == HardwareId && bytes[ipOffset + 2u] == Instance) {
                ++targetCount;
                if (tableVersion <= 2u &&
                    (bytes[ipOffset + 7u] & 0x0Fu) != 0) {
                    targetHarvested = 1;
                }
                candidate.DieId = dieId;
                candidate.HardwareId = hwId;
                candidate.Instance = bytes[ipOffset + 2u];
                candidate.Major = bytes[ipOffset + 4u];
                candidate.Minor = bytes[ipOffset + 5u];
                candidate.Revision = bytes[ipOffset + 6u];
                candidate.BaseAddressCount = baseCount;
                candidate.BaseAddressWidth = (uint8_t)baseWidth;
                for (baseIndex = 0;
                     baseIndex < baseCount &&
                     baseIndex < BC250_DISCOVERY_MAX_BASES; ++baseIndex) {
                    const uint8_t *base = bytes + ipOffset +
                        BC250_IP_RECORD_BYTES + (size_t)baseIndex * baseWidth;
                    candidate.BaseAddresses[baseIndex] = baseWidth == 8u ?
                        Bc250ReadLe64(base) : Bc250ReadLe32(base);
                }
            }
            ipOffset += recordBytes;
        }
    }
    if (targetCount == 0) return BC250_IP_DISCOVERY_TARGET_NOT_FOUND;
    if (targetCount != 1) return BC250_IP_DISCOVERY_TARGET_DUPLICATE;
    if (targetHarvested != 0) return BC250_IP_DISCOVERY_TARGET_HARVESTED;
    *Identity = candidate;
    return BC250_IP_DISCOVERY_OK;
}

BC250_IP_DISCOVERY_STATUS Bc250ParseIpDiscoveryGcIdentity(
    const void *Binary,
    size_t BinaryBufferBytes,
    BC250_GC_DISCOVERY_IDENTITY *Identity)
{
    BC250_IP_DISCOVERY_STATUS status = Bc250ParseIpDiscoveryIdentity(
        Binary, BinaryBufferBytes, BC250_DISCOVERY_GC_HWID, 0u, Identity);

    if (status == BC250_IP_DISCOVERY_TARGET_NOT_FOUND) {
        return BC250_IP_DISCOVERY_GC_NOT_FOUND;
    }
    if (status == BC250_IP_DISCOVERY_TARGET_DUPLICATE) {
        return BC250_IP_DISCOVERY_GC_DUPLICATE;
    }
    if (status == BC250_IP_DISCOVERY_TARGET_HARVESTED) {
        return BC250_IP_DISCOVERY_GC_HARVESTED;
    }
    if (status != BC250_IP_DISCOVERY_OK) return status;
    if (Identity->Major != 10u || Identity->Minor != 1u ||
        Identity->Revision != 3u) {
        return BC250_IP_DISCOVERY_GC_VERSION_MISMATCH;
    }
    return BC250_IP_DISCOVERY_OK;
}

const char *Bc250IpDiscoveryStatusName(BC250_IP_DISCOVERY_STATUS Status)
{
    switch (Status) {
    case BC250_IP_DISCOVERY_OK: return "ok";
    case BC250_IP_DISCOVERY_BAD_ARGUMENT: return "bad_argument";
    case BC250_IP_DISCOVERY_BINARY_TOO_SMALL: return "binary_too_small";
    case BC250_IP_DISCOVERY_BAD_BINARY_SIGNATURE: return "bad_binary_signature";
    case BC250_IP_DISCOVERY_UNSUPPORTED_BINARY_VERSION: return "unsupported_binary_version";
    case BC250_IP_DISCOVERY_BINARY_BOUNDS: return "binary_bounds";
    case BC250_IP_DISCOVERY_BAD_BINARY_CHECKSUM: return "bad_binary_checksum";
    case BC250_IP_DISCOVERY_TABLE_MISSING: return "table_missing";
    case BC250_IP_DISCOVERY_TABLE_BOUNDS: return "table_bounds";
    case BC250_IP_DISCOVERY_BAD_TABLE_SIGNATURE: return "bad_table_signature";
    case BC250_IP_DISCOVERY_UNSUPPORTED_TABLE_VERSION: return "unsupported_table_version";
    case BC250_IP_DISCOVERY_BAD_TABLE_CHECKSUM: return "bad_table_checksum";
    case BC250_IP_DISCOVERY_DIE_BOUNDS: return "die_bounds";
    case BC250_IP_DISCOVERY_IP_BOUNDS: return "ip_bounds";
    case BC250_IP_DISCOVERY_GC_NOT_FOUND: return "gc_not_found";
    case BC250_IP_DISCOVERY_GC_DUPLICATE: return "gc_duplicate";
    case BC250_IP_DISCOVERY_GC_HARVESTED: return "gc_harvested";
    case BC250_IP_DISCOVERY_GC_VERSION_MISMATCH: return "gc_version_mismatch";
    case BC250_IP_DISCOVERY_TARGET_NOT_FOUND: return "target_not_found";
    case BC250_IP_DISCOVERY_TARGET_DUPLICATE: return "target_duplicate";
    case BC250_IP_DISCOVERY_TARGET_HARVESTED: return "target_harvested";
    default: return "unknown";
    }
}
