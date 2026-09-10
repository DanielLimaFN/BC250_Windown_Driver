#ifndef BC250_PAGING_PROOF_CONTRACT_H
#define BC250_PAGING_PROOF_CONTRACT_H
#include "bc250_sdma_packet_builder.h"

/* Optional diagnostic metadata, never permission to execute another IB.
 * Decode only complete, contiguous DWORD CONST_FILL + padding NOP streams.
 * Provenance: the same packet layout emitted by bc250_sdma_packet_builder.c. */
typedef struct BC250_PAGING_PROOF {
    uint64_t CpuPhysical;
    uint64_t Destination;
    uint32_t PublishedBytes;
    uint32_t Bytes;
    uint32_t Pattern;
} BC250_PAGING_PROOF;

static inline int Bc250PagingDecodeProof(const uint32_t *Words,
    uint32_t Count, uint64_t Physical, BC250_PAGING_PROOF *Proof)
{
    uint32_t i = 0u, bytes = 0u, pattern = 0u;
    uint64_t start = 0u;
    if (!Words || !Proof || !Physical || !Count || Count > 0x3fffffffu) return 0;
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
        if (!bytes) { start = address; pattern = Words[i + 3u]; }
        else if (address != start + bytes || pattern != Words[i + 3u]) return 0;
        bytes += chunk;
        i += 5u;
    }
    /* A single physical page keeps the canary mapping contiguous by construction. */
    if (!bytes || bytes > 4096u - (uint32_t)(start & 4095u) ||
        Count * 4u > 4096u - (uint32_t)(Physical & 4095u)) return 0;
    Proof->CpuPhysical = Physical;
    Proof->Destination = start;
    Proof->PublishedBytes = Count * 4u;
    Proof->Bytes = bytes;
    Proof->Pattern = pattern;
    return 1;
}

static inline int Bc250PagingProofMatches(const BC250_PAGING_PROOF *Proof,
    uint64_t Physical, uint32_t Bytes)
{
    return Proof && Physical && Proof->Bytes &&
        Proof->CpuPhysical == Physical && Proof->PublishedBytes == Bytes;
}
#endif
