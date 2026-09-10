#ifndef BC250_ALLOCATION_ABI_CONTRACT_H
#define BC250_ALLOCATION_ABI_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stdint.h>
#endif

/*
 * ============================================================================
 * CONTRATO DE ABI DE ALOCACAO BC-250 (V1)
 * ============================================================================
 *
 * 1. INVARIANCIA ESTRUTURAL E VERSIONAMENTO:
 *    - Tamanho fixo: exatamente 40 bytes (sizeof(BC250_ALLOCATION_PRIVATE_V1)).
 *    - Magic: 0x31414342u ("BCA1").
 *    - Versao: 1u (BC250_ALLOCATION_PRIVATE_VERSION).
 *    - Flags: reservado para futuras extensoes; DEVE ser 0u na V1.
 *    - Qualquer divergencia em Magic, Version, StructSize ou Flags resulta em
 *      rejeicao imediata (fail-closed) com status especifico.
 *
 * 2. MODELO DE OWNERSHIP:
 *    - UMD (ou driver cliente como Mesa RADV) aloca e preenche o buffer privado
 *      passado em D3DDDI_ALLOCATIONINFO2::pPrivateDriverData / PrivateDriverDataSize.
 *    - O KMD valida a estrutura em DxgkDdiCreateAllocation e DxgkDdiOpenAllocation
 *      atraves de Bc250AllocationAbiParse, registrando o objeto sob a tabela
 *      de objetos do adaptador / processo com seu AllocationToken unico.
 *    - O token identifica de forma imutavel a alocacao durante toda a sua vida.
 *
 * 3. CICLO DE VIDA DA ALOCACAO (LIFECYCLE STATE MACHINE):
 *    a) CRIACAO: D3DKMTCreateAllocation2 invocado pela UMD. O KMD cria o
 *       handle da alocacao na VRAM (Segmento 1) ou Aperture (Segmento 2).
 *    b) MAPEAMENTO GPUVA: MapGpuVirtualAddress atribui uma faixa de endereco
 *       virtual de GPU dentro do teto de 34 bits (BC250_ADAPTER_MAX_GPUVA_ADDRESS).
 *    c) RESIDENCIA: D3DKMTMakeResident torna a alocacao acessivel pela GPU.
 *       Nenhum comando deve referenciar uma alocacao nao residente.
 *    d) SUBMISSAO: Comandos SDMA (Node 0) ou PM4 Compute (Node 1) referenciam
 *       o GPUVA correspondente.
 *    e) EVICCAO E TEARDOWN: Antes de desalocar, a alocacao deve ser evictada
 *       (D3DKMTEvict) e seu GPUVA liberado. Se D3DKMTDeallocate2 for invocado
 *       enquanto a alocacao estiver marcada residente, o winsys deve evictar
 *       automaticamente antes de chamar a destruicao no kernel.
 *
 * 4. SINCRONIZACAO E FILAS DE COMANDO:
 *    - Sincronizacao de paginação é garantida por monitored fences de 64 bits
 *      gerenciadas pelo dxgkrnl e lidas atomicamente via CPU/GPU VA.
 * ============================================================================
 */

#define BC250_ALLOCATION_PRIVATE_MAGIC 0x31414342u /* "BCA1" */
#define BC250_ALLOCATION_PRIVATE_VERSION 1u
#define BC250_ALLOCATION_MIN_ALIGNMENT 4096u
#define BC250_ALLOCATION_MAX_ALIGNMENT (2u * 1024u * 1024u)

#define BC250_ALLOCATION_TOKEN_CLASS_SHIFT 32u
#define BC250_ALLOCATION_TOKEN_CLASS_PRIMARY 0xA1100001u
#define BC250_ALLOCATION_TOKEN_CLASS(Token) \
    ((uint32_t)(((uint64_t)(Token)) >> BC250_ALLOCATION_TOKEN_CLASS_SHIFT))

typedef struct BC250_ALLOCATION_PRIVATE_V1 {
    uint32_t Magic;
    uint32_t Version;
    uint32_t StructSize;
    uint32_t Flags;
    uint64_t AllocationToken;
    uint64_t ByteSize;
    uint64_t Alignment;
} BC250_ALLOCATION_PRIVATE_V1;

typedef enum BC250_ALLOCATION_ABI_STATUS {
    BC250_ALLOCATION_ABI_OK = 0,
    BC250_ALLOCATION_ABI_BAD_ARGUMENT,
    BC250_ALLOCATION_ABI_BAD_SIZE,
    BC250_ALLOCATION_ABI_BAD_MAGIC,
    BC250_ALLOCATION_ABI_BAD_VERSION,
    BC250_ALLOCATION_ABI_BAD_FLAGS,
    BC250_ALLOCATION_ABI_BAD_ALIGNMENT
} BC250_ALLOCATION_ABI_STATUS;

BC250_ALLOCATION_ABI_STATUS Bc250AllocationAbiFormat(
    uint64_t AllocationToken,
    uint64_t ByteSize,
    uint64_t Alignment,
    uint32_t Flags,
    BC250_ALLOCATION_PRIVATE_V1 *Output);

BC250_ALLOCATION_ABI_STATUS Bc250AllocationAbiParse(
    const void *PrivateData,
    uint32_t PrivateDataSize,
    BC250_ALLOCATION_PRIVATE_V1 *Request);

int Bc250AllocationIsPrimary(
    const BC250_ALLOCATION_PRIVATE_V1 *Request);

#endif

