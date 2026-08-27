#ifndef BC250_RESOURCE_CONTRACT_H
#define BC250_RESOURCE_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_RESOURCE_MAX_BARS 6u

typedef enum BC250_RESOURCE_STATUS {
    BC250_RESOURCE_OK = 0,
    BC250_RESOURCE_BAD_ARGUMENT,
    BC250_RESOURCE_RANGE_OVERFLOW,
    BC250_RESOURCE_DUPLICATE_BAR,
    BC250_RESOURCE_AMBIGUOUS_RANGE,
    BC250_RESOURCE_BAR_UNASSIGNED,
    BC250_RESOURCE_OUTPUT_TOO_SMALL
} BC250_RESOURCE_STATUS;

typedef struct BC250_MEMORY_RESOURCE {
    uint64_t Start;
    uint64_t Length;
} BC250_MEMORY_RESOURCE;

typedef struct BC250_PCI_BAR_RESOURCE {
    uint32_t Index;
    uint32_t Present;
    uint64_t Base;
} BC250_PCI_BAR_RESOURCE;

typedef struct BC250_RESOLVED_BAR_RESOURCE {
    uint32_t Index;
    uint32_t DescriptorDepth;
    uint32_t CappedByNextBar;
    uint32_t Reserved;
    uint64_t Start;
    uint64_t Length;
} BC250_RESOLVED_BAR_RESOURCE;

/*
 * Resolve BARs implementados contra recursos de memoria traduzidos.
 *
 * O chamador deve obter Bars por leitura PCI sancionada e MemoryResources da
 * lista traduzida do PnP/Dxgkrnl. A funcao nao adivinha identidade por tamanho.
 * Descritores agregados sao limitados pelo menor descritor que contem a base e
 * pela base do proximo BAR implementado.
 */
BC250_RESOURCE_STATUS
Bc250ResolveTranslatedBars(
    const BC250_MEMORY_RESOURCE *MemoryResources,
    size_t MemoryResourceCount,
    const BC250_PCI_BAR_RESOURCE *Bars,
    size_t BarCount,
    BC250_RESOLVED_BAR_RESOURCE *Resolved,
    size_t ResolvedCapacity,
    size_t *ResolvedCount);

const char *Bc250ResourceStatusName(BC250_RESOURCE_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
