#ifndef BC250_CM_RESOURCE_ADAPTER_H
#define BC250_CM_RESOURCE_ADAPTER_H

#include <ntddk.h>

#include "../core/bc250_resource_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_CM_MAX_FULL_DESCRIPTORS    64u
#define BC250_CM_MAX_PARTIAL_DESCRIPTORS 256u
#define BC250_CM_MAX_MEMORY_RESOURCES    64u

typedef enum BC250_CM_RESOURCE_STATUS {
    BC250_CM_RESOURCE_OK = 0,
    BC250_CM_RESOURCE_BAD_ARGUMENT,
    BC250_CM_RESOURCE_BAD_COUNT,
    BC250_CM_RESOURCE_BAD_MEMORY_DESCRIPTOR,
    BC250_CM_RESOURCE_UNSUPPORTED_LAYOUT,
    BC250_CM_RESOURCE_OUTPUT_TOO_SMALL,
    BC250_CM_RESOURCE_POINTER_OVERFLOW,
    BC250_CM_RESOURCE_CONTRACT_REJECTED
} BC250_CM_RESOURCE_STATUS;

/*
 * Extrai todos os descritores de memoria de todas as full resource lists.
 * Nao mapeia, nao acessa PCI/MMIO e nao atribui identidade de BAR por tamanho.
 */
BC250_CM_RESOURCE_STATUS
Bc250CollectTranslatedMemoryResources(
    _In_ const CM_RESOURCE_LIST *TranslatedResourceList,
    _Out_writes_to_(MemoryResourceCapacity, *MemoryResourceCount)
        BC250_MEMORY_RESOURCE *MemoryResources,
    _In_ size_t MemoryResourceCapacity,
    _Out_ size_t *MemoryResourceCount);

/*
 * Adaptador completo para StartDevice: coleta a lista traduzida e delega a
 * identidade/tamanho dos BARs ao contrato portatil.
 */
BC250_CM_RESOURCE_STATUS
Bc250ResolveCmTranslatedBars(
    _In_ const CM_RESOURCE_LIST *TranslatedResourceList,
    _In_reads_(BarCount) const BC250_PCI_BAR_RESOURCE *Bars,
    _In_ size_t BarCount,
    _Out_writes_to_(ResolvedCapacity, *ResolvedCount)
        BC250_RESOLVED_BAR_RESOURCE *Resolved,
    _In_ size_t ResolvedCapacity,
    _Out_ size_t *ResolvedCount,
    _Out_opt_ BC250_RESOURCE_STATUS *ContractStatus);

const char *Bc250CmResourceStatusName(BC250_CM_RESOURCE_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
