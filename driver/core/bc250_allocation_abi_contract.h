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
 *    - Flags: BC250_ALLOCATION_FLAG_CPU_VISIBLE (0x1) ou 0u. Flags desconhecidas
 *      resultam em rejeicao imediata (BC250_ALLOCATION_ABI_BAD_FLAGS).
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
 *    e) EVICCAO E TEARDOWN: No WDDM 2.0+, pfnDeallocate2Cb / pfnDeallocateCb
 *       realiza a eviccao atomica e o desregistro seguro no kernel. O winsys
 *       nao deve chamar pfnEvictCb imediatamente antes da desalocacao para
 *       evitar condicao de corrida no kernel (dxgmms2 / VIDMM_RECYCLE_HEAP_MGR).
 *
 * 4. SINCRONIZACAO E FILAS DE COMANDO:
 *    - Sincronizacao de paginação é garantida por monitored fences de 64 bits
 *      gerenciadas pelo dxgkrnl e lidas atomicamente via CPU/GPU VA.
 * ============================================================================
 */

#define BC250_ALLOCATION_PRIVATE_MAGIC 0x31414342u /* "BCA1" */
/*
 * Versao 1: 40 bytes legados (sem geometria DXGI, usado pelo Mesa RADV).
 * Versao 2 (F2, 13/09): 56 bytes carregando BC250_ALLOCATION_GEOMETRY (UMD D3D11).
 * O parse aceita ambas as versoes, garantindo retrocompatibilidade total.
 */
#define BC250_ALLOCATION_PRIVATE_VERSION_1 1u
#define BC250_ALLOCATION_PRIVATE_VERSION_2 2u
#define BC250_ALLOCATION_PRIVATE_VERSION   2u
#define BC250_ALLOCATION_V1_SIZE           40u
#define BC250_ALLOCATION_V2_SIZE           56u
#define BC250_ALLOCATION_MIN_ALIGNMENT 4096u
#define BC250_ALLOCATION_MAX_ALIGNMENT (2u * 1024u * 1024u)

#define BC250_ALLOCATION_TOKEN_CLASS_SHIFT 32u
#define BC250_ALLOCATION_TOKEN_CLASS_PRIMARY     0xA1100001u
#define BC250_ALLOCATION_TOKEN_CLASS_SHADOW      0xA1100002u
#define BC250_ALLOCATION_TOKEN_CLASS_STAGING     0xA1100003u
#define BC250_ALLOCATION_TOKEN_CLASS_GDI         0xA1100004u
#define BC250_ALLOCATION_TOKEN_CLASS_CPU_VISIBLE 0xA1100005u
#define BC250_ALLOCATION_TOKEN_CLASS_COMMAND     0x434D4400u /* "CMD\0" */
#define BC250_ALLOCATION_TOKEN_CLASS_PROOF       0x50524600u /* "PRF\0" */

#define BC250_ALLOCATION_FLAG_CPU_VISIBLE        0x00000001u
#define BC250_ALLOCATION_FLAG_HAS_GEOMETRY       0x00000002u
#define BC250_ALLOCATION_SUPPORTED_FLAGS \
    (BC250_ALLOCATION_FLAG_CPU_VISIBLE | BC250_ALLOCATION_FLAG_HAS_GEOMETRY)

#define BC250_ALLOCATION_TOKEN_CLASS(Token) \
    ((uint32_t)(((uint64_t)(Token)) >> BC250_ALLOCATION_TOKEN_CLASS_SHIFT))

/*
 * GEOMETRIA DA SUPERFICIE (F2, 13/09) -- POR QUE ELA ENTROU NA ABI.
 *
 * `DxgkDdiDescribeAllocation` (d3dkmddi.h:4221) recebe um hAllocation e tem de
 * devolver Width, Height, Format, Pitch e RefreshRate DAQUELA superficie. Ate a
 * F2 a ABI de alocacao carregava apenas token, tamanho e alinhamento -- o KMD
 * nao tinha como responder, e a DDI devolvia STATUS_NOT_SUPPORTED para tudo.
 *
 * A alternativa seria responder com a geometria do MODO vigente. Seria certo por
 * acidente para os backbuffers do dwm.exe (todos 2560x1080) e FABRICACAO para
 * qualquer outra superficie -- Lei 4. A UMD ja conhece a geometria no momento da
 * criacao; carregá-la e' a fonte unica honesta.
 *
 * O mesmo dado alimenta `DXGK_ALLOCATIONINFO.Pitch`, que nunca era preenchido.
 *
 * `Format` trafega como D3DDDIFORMAT numerico: a traducao a partir do formato
 * DXGI e' feita na UMD, que e' quem conhece a semantica DXGI. O KMD so' carrega
 * o numero.
 */
typedef struct BC250_ALLOCATION_GEOMETRY {
    uint32_t Width;
    uint32_t Height;
    uint32_t Pitch;
    uint32_t Format;   /* D3DDDIFORMAT numerico; 0 = desconhecido */
} BC250_ALLOCATION_GEOMETRY;

typedef struct BC250_ALLOCATION_PRIVATE_V1 {
    uint32_t Magic;
    uint32_t Version;
    uint32_t StructSize;
    uint32_t Flags;
    uint64_t AllocationToken;
    uint64_t ByteSize;
    uint64_t Alignment;
    /*
     * Valida somente com BC250_ALLOCATION_FLAG_HAS_GEOMETRY. Sem a flag os
     * quatro campos sao ZERO por construcao, e quem depende deles tem de falhar
     * fechado em vez de inventar.
     */
    BC250_ALLOCATION_GEOMETRY Geometry;
} BC250_ALLOCATION_PRIVATE_V1;

typedef enum BC250_ALLOCATION_ABI_STATUS {
    BC250_ALLOCATION_ABI_OK = 0,
    BC250_ALLOCATION_ABI_BAD_ARGUMENT,
    BC250_ALLOCATION_ABI_BAD_SIZE,
    BC250_ALLOCATION_ABI_BAD_MAGIC,
    BC250_ALLOCATION_ABI_BAD_VERSION,
    BC250_ALLOCATION_ABI_BAD_FLAGS,
    BC250_ALLOCATION_ABI_BAD_ALIGNMENT,
    BC250_ALLOCATION_ABI_BAD_GEOMETRY
} BC250_ALLOCATION_ABI_STATUS;

/*
 * Geometry == NULL produz um blob sem BC250_ALLOCATION_FLAG_HAS_GEOMETRY e com
 * os quatro campos zerados -- o caso legitimo de command buffer, staging de
 * prova e scratch, que nunca sao descritos ao dxgkrnl.
 *
 * Com Geometry != NULL a coerencia e' EXIGIDA: os quatro campos nao-nulos e
 * ByteSize suficiente para Pitch * Height. Geometria inconsistente falha
 * fechado em vez de virar uma descricao inventada mais tarde.
 */
BC250_ALLOCATION_ABI_STATUS Bc250AllocationAbiFormat(
    uint64_t AllocationToken,
    uint64_t ByteSize,
    uint64_t Alignment,
    uint32_t Flags,
    const BC250_ALLOCATION_GEOMETRY *Geometry,
    BC250_ALLOCATION_PRIVATE_V1 *Output);

/* Verdadeiro somente quando a flag esta presente E os campos sao coerentes. */
int Bc250AllocationHasGeometry(
    const BC250_ALLOCATION_PRIVATE_V1 *Request);

BC250_ALLOCATION_ABI_STATUS Bc250AllocationAbiParse(
    const void *PrivateData,
    uint32_t PrivateDataSize,
    BC250_ALLOCATION_PRIVATE_V1 *Request);

int Bc250AllocationIsPrimary(
    const BC250_ALLOCATION_PRIVATE_V1 *Request);

int Bc250AllocationRequiresCpuAccess(
    const BC250_ALLOCATION_PRIVATE_V1 *Request);

#endif

