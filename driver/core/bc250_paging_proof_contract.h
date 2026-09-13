#ifndef BC250_PAGING_PROOF_CONTRACT_H
#define BC250_PAGING_PROOF_CONTRACT_H
#include "bc250_sdma_packet_builder.h"

/* Optional diagnostic metadata, never permission to execute another IB.
 * Decode only complete, contiguous DWORD CONST_FILL + padding NOP streams.
 * Provenance: the same packet layout emitted by bc250_sdma_packet_builder.c. */
#define BC250_PAGING_PROOF_OP_NONE  0u
#define BC250_PAGING_PROOF_OP_FILL  1u
#define BC250_PAGING_PROOF_OP_COPY  2u

typedef struct BC250_PAGING_PROOF {
    uint64_t CpuPhysical;
    uint64_t Destination;
    uint64_t Source;
    uint32_t PublishedBytes;
    uint32_t Bytes;
    uint32_t Pattern;
    uint32_t Opcode;
} BC250_PAGING_PROOF;

static inline int Bc250PagingDecodeProof(const uint32_t *Words,
    uint32_t Count, uint64_t Physical, BC250_PAGING_PROOF *Proof)
{
    uint32_t i = 0u, bytes = 0u, pattern = 0u, opcode = BC250_PAGING_PROOF_OP_NONE;
    uint64_t dstStart = 0u, srcStart = 0u;
    if (!Words || !Proof || !Physical || !Count || Count > 0x3fffffffu) return 0;

    /* Scan leading NOPs to find the first packet */
    while (i < Count && Words[i] == BC250_SDMA_OP_NOP) {
        ++i;
    }
    if (i >= Count) return 0;

    if (Words[i] == (BC250_SDMA_OP_CONST_FILL | (2u << 30))) {
        opcode = BC250_PAGING_PROOF_OP_FILL;
        while (i < Count) {
            uint64_t address;
            uint32_t chunk;
            if (Words[i] == BC250_SDMA_OP_NOP) { ++i; continue; }
            if (Count - i < 5u || Words[i] != (BC250_SDMA_OP_CONST_FILL | (2u << 30)) ||
                Words[i + 4u] > 0x3fffffu) return 0;
            address = Words[i + 1u] | ((uint64_t)Words[i + 2u] << 32);
            chunk = Words[i + 4u] + 1u;
            if ((address & 3u) || (chunk & 3u) || address + chunk < address ||
                bytes > 0xffffffffu - chunk) return 0;
            if (!bytes) { dstStart = address; pattern = Words[i + 3u]; }
            else if (address != dstStart + bytes || pattern != Words[i + 3u]) return 0;
            bytes += chunk;
            i += 5u;
        }
        if (!bytes || Count * 4u > 4096u - (uint32_t)(Physical & 4095u)) return 0;
    } else if (Words[i] == (BC250_SDMA_OP_COPY | (BC250_SDMA_SUBOP_COPY_LINEAR << 8))) {
        opcode = BC250_PAGING_PROOF_OP_COPY;
        while (i < Count) {
            uint64_t srcAddr, dstAddr;
            uint32_t chunk;
            if (Words[i] == BC250_SDMA_OP_NOP) { ++i; continue; }
            if (Count - i < 7u || Words[i] != (BC250_SDMA_OP_COPY | (BC250_SDMA_SUBOP_COPY_LINEAR << 8)) ||
                Words[i + 1u] > 0x3fffffu) return 0;
            chunk = Words[i + 1u] + 1u;
            srcAddr = Words[i + 3u] | ((uint64_t)Words[i + 4u] << 32);
            dstAddr = Words[i + 5u] | ((uint64_t)Words[i + 6u] << 32);
            if ((srcAddr & 3u) || (dstAddr & 3u) || (chunk & 3u) ||
                srcAddr + chunk < srcAddr || dstAddr + chunk < dstAddr ||
                bytes > 0xffffffffu - chunk) return 0;
            if (!bytes) {
                srcStart = srcAddr;
                dstStart = dstAddr;
            } else if (srcAddr != srcStart + bytes || dstAddr != dstStart + bytes) {
                return 0;
            }
            bytes += chunk;
            i += 7u;
        }
        if (!bytes || Count * 4u > 4096u - (uint32_t)(Physical & 4095u)) return 0;
    } else {
        return 0;
    }

    Proof->CpuPhysical = Physical;
    Proof->Destination = dstStart;
    Proof->Source = srcStart;
    Proof->PublishedBytes = Count * 4u;
    Proof->Bytes = bytes;
    Proof->Pattern = pattern;
    Proof->Opcode = opcode;
    return 1;
}

static inline int Bc250PagingProofMatches(const BC250_PAGING_PROOF *Proof,
    uint64_t Physical, uint32_t Bytes)
{
    return Proof && Physical && Proof->Bytes &&
        Proof->CpuPhysical == Physical && Proof->PublishedBytes == Bytes;
}
#endif
