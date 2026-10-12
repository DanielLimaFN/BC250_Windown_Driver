#ifndef BC250_PA_VALIDATOR_H
#define BC250_PA_VALIDATOR_H

/*
 * DBG.1 (doc 704): classificador puro de endereco fisico para o validador do
 * modo debug. Responde se uma faixa [Pa, Pa+Bytes) cai inteira numa faixa de
 * RAM do Windows (MmGetPhysicalMemoryRanges no StartDevice), se toca uma faixa
 * proibida (carve-out da VRAM) ou se sai da RAM. So' aritmetica: sem acesso a
 * memoria nem a registrador, usado tanto no driver quanto nas suites offline.
 */

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define BC250_PA_MAX_RAM_RANGES    64u
#define BC250_PA_MAX_FORBID_RANGES 4u
#define BC250_PA_LOW_LEGACY_LIMIT  0x100000ull /* 1 MiB */

typedef struct BC250_PA_RANGE {
    uint64_t Base;
    uint64_t Bytes;
} BC250_PA_RANGE;

enum {
    BC250_PA_CLASS_IN_RAM      = 0u,
    BC250_PA_CLASS_OUTSIDE_RAM = 1u, /* nenhuma faixa de RAM contem o inicio */
    BC250_PA_CLASS_STRADDLE    = 2u, /* comeca na RAM e passa do fim da faixa */
    BC250_PA_CLASS_FORBIDDEN   = 3u, /* toca uma faixa proibida (carve-out) */
    BC250_PA_CLASS_LOW_LEGACY  = 4u, /* abaixo de 1 MiB */
    BC250_PA_CLASS_NO_MAP      = 5u  /* sem tabela de RAM: nao classificado */
};

static __inline int Bc250PaRangeOverlaps(const BC250_PA_RANGE *R, uint64_t Pa, uint64_t Bytes)
{
    return R->Bytes != 0u && Pa < R->Base + R->Bytes && R->Base < Pa + Bytes;
}

static __inline uint32_t Bc250PaClassify(
    const BC250_PA_RANGE *Ram, uint32_t RamCount,
    const BC250_PA_RANGE *Forbid, uint32_t ForbidCount,
    uint64_t Pa, uint64_t Bytes)
{
    uint32_t i;
    if (Bytes == 0u) Bytes = 1u;
    if (Pa > ~(uint64_t)0 - Bytes) return BC250_PA_CLASS_OUTSIDE_RAM;
    for (i = 0u; i < ForbidCount && Forbid != NULL; ++i) {
        if (Bc250PaRangeOverlaps(&Forbid[i], Pa, Bytes)) return BC250_PA_CLASS_FORBIDDEN;
    }
    if (Pa < BC250_PA_LOW_LEGACY_LIMIT) return BC250_PA_CLASS_LOW_LEGACY;
    if (Ram == NULL || RamCount == 0u) return BC250_PA_CLASS_NO_MAP;
    for (i = 0u; i < RamCount; ++i) {
        const BC250_PA_RANGE *r = &Ram[i];
        if (r->Bytes == 0u) continue;
        if (Pa >= r->Base && Pa < r->Base + r->Bytes) {
            return (Pa + Bytes <= r->Base + r->Bytes) ?
                BC250_PA_CLASS_IN_RAM : BC250_PA_CLASS_STRADDLE;
        }
    }
    return BC250_PA_CLASS_OUTSIDE_RAM;
}

#endif /* BC250_PA_VALIDATOR_H */
