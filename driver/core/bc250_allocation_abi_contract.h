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
#define BC250_ALLOCATION_TOKEN_CLASS_RESOURCE    0x52455300u /* "RES\0" */

#define BC250_ALLOCATION_FLAG_CPU_VISIBLE        0x00000001u
#define BC250_ALLOCATION_FLAG_HAS_GEOMETRY       0x00000002u
/*
 * M1 (doc 491, 28/09): a UMD declara que NENHUM caminho do KMD endereca esta
 * alocacao por endereco fisico -- ela so' e' tocada pela GPU via GPUVA (e pela
 * CPU via Lock). So' entao ela pode morar no segmento de memoria do sistema
 * (abertura, segmento 2), que na BC-250 e' a mesma GDDR6. Valida SOMENTE na
 * classe RESOURCE; qualquer outra classe com a flag e' RECUSADA (fail-closed),
 * porque primaria, scanout e cópias do KMD enderecam o segmento 1 por MC.
 */
#define BC250_ALLOCATION_FLAG_GPU_VA_ONLY        0x00000004u
/*
 * M1b.1 (doc 554, 01/10): a RADV declara o DOMINIO que pediu e marca os
 * buffers de comando. Ate aqui o blob BCA1 so' carregava CPU_VISIBLE, e o KMD
 * punha tudo no segmento 1 (doc 496 secao 1). Validas SOMENTE em token da RADV
 * (fora das classes da UMD); em classe da UMD sao RECUSADAS (fail-closed).
 *   RADV_DOMAIN_GTT: a RADV pediu RADEON_DOMAIN_GTT (memoria do sistema).
 *   RADV_COMMAND:    BO de comando (prioridade CS). O KMD le o DW0 do IB pela
 *                    VRAM (DX.43, IB de aquecimento); por isso fica no 1.
 */
#define BC250_ALLOCATION_FLAG_RADV_DOMAIN_GTT    0x00000008u
#define BC250_ALLOCATION_FLAG_RADV_COMMAND       0x00000010u
#define BC250_ALLOCATION_RADV_ONLY_FLAGS \
    (BC250_ALLOCATION_FLAG_RADV_DOMAIN_GTT | BC250_ALLOCATION_FLAG_RADV_COMMAND)
#define BC250_ALLOCATION_SUPPORTED_FLAGS \
    (BC250_ALLOCATION_FLAG_CPU_VISIBLE | BC250_ALLOCATION_FLAG_HAS_GEOMETRY | \
     BC250_ALLOCATION_FLAG_GPU_VA_ONLY | BC250_ALLOCATION_RADV_ONLY_FLAGS)

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

/* M1: verdadeiro so' para RESOURCE com BC250_ALLOCATION_FLAG_GPU_VA_ONLY. */
int Bc250AllocationMayUseSystemSegment(
    const BC250_ALLOCATION_PRIVATE_V1 *Request);

/*
 * M2 (doc 548): alocacao que pode morar no segmento 3 (VRAM sem CPU). So' as
 * da RADV (token que nao e' de nenhuma classe da UMD) SEM a flag de CPU: as
 * imagens otimizadas que estouravam o segmento 1 (Corridas 189 e 193). Nenhuma
 * alocacao da UMD vai para la' -- o KMD copia essas por endereco MC, e o
 * conversor so' resolve o segmento 1 (fail-closed para o 3).
 */
int Bc250AllocationMayUseHiddenSegment(
    const BC250_ALLOCATION_PRIVATE_V1 *Request);

/*
 * M2.1 (Corrida 196, doc 548): com um segmento local SEM CPU declarado, o
 * VidMm deixa de tratar a VRAM como inteiramente visivel e passa a exigir,
 * para Lock2, que a alocacao acessivel pela CPU tenha o segmento de abertura
 * no conjunto suportado (allocation-usage-tracking.md). Verdadeiro so' para
 * alocacao da RADV (token fora das classes da UMD) que o KMD publicou como
 * CpuVisible -- a UMD D3D11 nao chama Lock e as classes dela sao copiadas
 * pelo KMD por endereco MC no segmento 1, entao nao podem ir para a abertura.
 */
int Bc250AllocationLockNeedsApertureFallback(
    const BC250_ALLOCATION_PRIVATE_V1 *Request,
    int CpuVisible);

/*
 * M1b.1 (doc 554): para onde vai uma alocacao da RADV quando a memoria da
 * carga real passa a morar no segmento 2 (abertura, a mesma GDDR6). Fonte
 * unica; o KMD so' aplica com BC250_M1B_RADV_SYSTEM_SEGMENT_ENABLED e sem
 * segmento 3 publicado. Devolve 1 e preenche Out quando a alocacao PODE ir
 * ao segmento 2; 0 = fica como esta' (segmento 1).
 *   classe da UMD                -> 0 (o KMD as enderecam por MC)
 *   RADV_COMMAND                 -> 0 (DX.43 le o DW0 do IB pela VRAM)
 *   dominio VRAM com CPU         -> 0 (buffer linear do Present, doc 496 item 3)
 *   dominio GTT                  -> SegSet 0x3, preferido 2 depois 1
 *   dominio VRAM sem CPU         -> SegSet 0x3, preferido 1 depois 2 (transborda)
 */
typedef struct BC250_ALLOCATION_SEGMENT_POLICY {
    uint32_t SegmentSet;
    uint32_t Preferred0;
    uint32_t Preferred1;
} BC250_ALLOCATION_SEGMENT_POLICY;

int Bc250AllocationRadvSystemSegmentPolicy(
    const BC250_ALLOCATION_PRIVATE_V1 *Request,
    int CpuVisible,
    BC250_ALLOCATION_SEGMENT_POLICY *Out);

#endif
