#ifndef BC250_UMD_PRIVATE_CONTRACT_H
#define BC250_UMD_PRIVATE_CONTRACT_H

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

/*
 * BC250_UMD_PRIVATE -- o blob de DXGKQAITYPE_UMDRIVERPRIVATE (Type=0).
 *
 * POR QUE ISTO EXISTE, medido e nao suposto:
 *
 * Na corrida 2026-09-07 08:25 (docs/evidence/2026-09-07_08-25-09-.../ANALISE.md) as tres
 * entradas do adapter table da UMD foram instrumentadas e sairam TODAS com ZERO chamadas --
 * CalcPrivateDeviceSize, CloseAdapter, CreateDevice, GetCaps e GetSupportedVersions. O
 * runtime aceitava o S_OK do OpenAdapter e descartava a DLL sem nem fechar o adapter.
 *
 * No mesmo log, do lado do kernel:
 *
 *     [McdmQAI] Type=0, OutSize=2400, CapsStatus=0x00000000
 *     [McdmQAI] Type=0 NOT SUPPORTED          <- 80 vezes
 *
 * Type=0 e' DXGKQAITYPE_UMDRIVERPRIVATE (shared/d3dkmddi.h), o canal de private driver data
 * entre o NOSSO KMD e o NOSSO UMD -- e ele nao tinha case no switch, caindo no default. Era
 * o tipo mais pedido da corrida (160 chamadas) e o unico recusado em massa.
 *
 * ESTE BLOB E' NOSSO NOS DOIS LADOS. Nao imitamos a ABI da AMD -- que e' exatamente a
 * fragilidade que a Collabora aponta no proprio port de RADV para Windows. Isso e' o que
 * torna a rota B (DXBC -> SPIR-V -> RADV/ACO) viavel: quando o winsys da branch wddm2 for
 * adaptado, ele passa a falar este formato versionado.
 *
 * REGRAS DE CONTRATO
 *
 *  1. Versionado e auto-descrito: Magic, Version e SizeBytes vem primeiro, para que uma UMD
 *     antiga contra um KMD novo (ou o inverso) FALHE FECHADO em vez de ler lixo.
 *  2. Somente FATOS. Nada aqui promete funcionalidade. Os campos descrevem o que o KMD ja'
 *     provou -- segmentos, nodes, geometria de GPUVA -- e nada mais.
 *  3. GateFlags declara explicitamente o que NAO existe (pipeline 3D, submissao Node 1),
 *     para a UMD nao inferir capacidade a partir de silencio.
 *  4. Zero ponteiros. O blob atravessa a fronteira kernel/usuario; ponteiro aqui seria
 *     defeito de seguranca.
 */

#define BC250_UMD_PRIVATE_MAGIC     0x424332355FUMDull /* placeholder, ver abaixo */

/* 'BC25UMD1' em little-endian, sem depender de trigraphs nem de char signedness. */
#undef BC250_UMD_PRIVATE_MAGIC
#define BC250_UMD_PRIVATE_MAGIC     0x31444D5535324342ull

#define BC250_UMD_PRIVATE_VERSION   1u

/* Bits de GateFlags: o que ESTA' fechado. Ausencia de bit nao promete nada. */
#define BC250_UMD_GATE_NO_3D_PIPELINE     0x00000001u /* sem rasterizador/blend/depth */
#define BC250_UMD_GATE_NO_NODE1_SUBMIT    0x00000002u /* submissao Node 1 nao provada */
#define BC250_UMD_GATE_NO_IB_UNDER_VMID8  0x00000004u /* IB de paginacao nao executa (B1) */
#define BC250_UMD_GATE_NO_SHADER_COMPILER 0x00000008u /* sem DXBC -> ISA nem -> SPIR-V */
#define BC250_UMD_GATE_CAPS_PROBE_ONLY    0x00000010u /* UMD deve falhar fechado em device */

typedef struct BC250_UMD_PRIVATE {
    /* --- cabecalho de identidade: sempre os tres primeiros campos --- */
    uint64_t Magic;         /* BC250_UMD_PRIVATE_MAGIC */
    uint32_t Version;       /* BC250_UMD_PRIVATE_VERSION */
    uint32_t SizeBytes;     /* sizeof(BC250_UMD_PRIVATE) no produtor */

    /* --- identidade do adaptador (fatos) --- */
    uint32_t VendorId;      /* 0x1002 */
    uint32_t DeviceId;      /* 0x13FE */
    uint32_t RevisionId;
    uint32_t Reserved0;

    /* --- topologia de memoria, espelhando o QUERYSEGMENT4 ja aceito --- */
    uint32_t SegmentCount;
    uint32_t VramSegmentId;      /* segmento de VRAM local */
    uint32_t ApertureSegmentId;  /* segmento de aperture/GART */
    uint32_t Reserved1;
    uint64_t VramSizeBytes;
    uint64_t ApertureSizeBytes;

    /* --- geometria de GPUVA, espelhando GPUMMUCAPS/PAGETABLELEVELDESC --- */
    uint32_t GpuVaBits;         /* 34: 12 offset + 9 folha + 13 raiz (docs/03) */
    uint32_t PageSizeBytes;     /* 4096 */
    uint32_t PageTableLevels;
    uint32_t Reserved2;

    /* --- engines --- */
    uint32_t NodeCount;
    uint32_t SdmaNodeOrdinal;    /* Node 0 */
    uint32_t ComputeNodeOrdinal; /* Node 1 */
    uint32_t Reserved3;

    /* --- o que NAO existe: ver os bits BC250_UMD_GATE_* --- */
    uint32_t GateFlags;
    uint32_t Reserved4;
} BC250_UMD_PRIVATE;

typedef enum BC250_UMD_PRIVATE_STATUS {
    BC250_UMD_PRIVATE_OK = 0,
    BC250_UMD_PRIVATE_BAD_ARGUMENT,
    BC250_UMD_PRIVATE_BUFFER_TOO_SMALL,
    BC250_UMD_PRIVATE_BAD_MAGIC,
    BC250_UMD_PRIVATE_BAD_VERSION,
    BC250_UMD_PRIVATE_BAD_SIZE,
    BC250_UMD_PRIVATE_INCOHERENT
} BC250_UMD_PRIVATE_STATUS;

/* Fatos que o KMD injeta no blob. Mantido separado da struct de fronteira para o
 * produtor nao poder esquecer um campo por engano -- o compilador cobra. */
typedef struct BC250_UMD_PRIVATE_FACTS {
    uint32_t VendorId;
    uint32_t DeviceId;
    uint32_t RevisionId;
    uint32_t SegmentCount;
    uint32_t VramSegmentId;
    uint32_t ApertureSegmentId;
    uint64_t VramSizeBytes;
    uint64_t ApertureSizeBytes;
    uint32_t GpuVaBits;
    uint32_t PageSizeBytes;
    uint32_t PageTableLevels;
    uint32_t NodeCount;
    uint32_t SdmaNodeOrdinal;
    uint32_t ComputeNodeOrdinal;
    uint32_t GateFlags;
} BC250_UMD_PRIVATE_FACTS;

/*
 * Lado KMD: escreve o blob em Buffer. Falha fechado se o buffer for pequeno --
 * NUNCA escreve parcial, para a UMD nao receber meio blob valido.
 */
BC250_UMD_PRIVATE_STATUS Bc250UmdPrivateWrite(
    const BC250_UMD_PRIVATE_FACTS *Facts,
    void *Buffer,
    uint32_t BufferBytes,
    uint32_t *BytesWritten);

/*
 * Lado UMD: valida o blob recebido. Recusa magic errado, versao desconhecida,
 * SizeBytes incoerente e valores impossiveis (ex.: NodeCount 0).
 */
BC250_UMD_PRIVATE_STATUS Bc250UmdPrivateValidate(
    const void *Buffer,
    uint32_t BufferBytes);

/* Tamanho que o KMD exige do buffer. Util para o log e para o teste. */
uint32_t Bc250UmdPrivateRequiredBytes(void);

#ifdef __cplusplus
}
#endif

#endif /* BC250_UMD_PRIVATE_CONTRACT_H */
