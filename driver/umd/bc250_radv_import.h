/*
 * Frente B (doc 547) -- espelho de mesa-radv-wddm/src/amd/vulkan/radv_bc250_import.h.
 *
 * Estrutura PRIVADA encadeada em VkMemoryAllocateInfo::pNext para a RADV da
 * BC-250 criar memoria Vulkan sobre uma GPUVA que ESTA UMD ja' mapeou e tornou
 * residente. O espaco de GPUVA e' por processo e adaptador (a RADV libera VA com
 * hAdapter, radv_wddm2_bo.c), entao a VA vale nos contextos da RADV do mesmo
 * processo. Os dois arquivos tem de ficar identicos em sType, magic e layout --
 * o static assert abaixo trava o tamanho.
 */
#ifndef BC250_RADV_IMPORT_H
#define BC250_RADV_IMPORT_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#define BC250_RADV_STRUCTURE_TYPE_IMPORT_GPU_VA ((VkStructureType)0x42C25001)
#define BC250_RADV_IMPORT_GPU_VA_MAGIC 0xBC25FB01u

typedef struct Bc250RadvImportGpuVaInfo {
    VkStructureType sType;
    const void *pNext;
    uint32_t magic;
    uint32_t reserved;
    uint64_t gpu_va;
    uint64_t size;
} Bc250RadvImportGpuVaInfo;

typedef char Bc250RadvImportGpuVaInfoLayout[(sizeof(Bc250RadvImportGpuVaInfo) == 40u) ? 1 : -1];

#endif /* BC250_RADV_IMPORT_H */
