/*
 * bc250_escape_contract.h
 *
 * Contrato de comunicação privada WDDM Escape (D3DKMTEscape / DxgkDdiEscape)
 * para o adaptador AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2).
 *
 * Permite que componentes em user-mode (Mesa RADV, harnesses de teste)
 * requisitem apresentação direta no scanout do UEFI GOP via aceleração de
 * hardware SDMA do KMD (Bc250SdmaPresentBlt) ou injeção segura de buffer de pixel.
 */

#ifndef BC250_ESCAPE_CONTRACT_H
#define BC250_ESCAPE_CONTRACT_H

/*
 * O discriminante e' _KERNEL_MODE apenas. NTDDI_VERSION tambem e' definido em
 * user-mode (a UMD define), e incluir <ntdef.h> depois de <windows.h> redefine
 * _PROCESSOR_NUMBER, _GROUP_AFFINITY, _FLOAT128 e PROBE_ALIGNMENT -- o build
 * da UMD quebrou exatamente assim ao passar a usar este contrato.
 */
#if defined(_KERNEL_MODE)
/* Kernel Mode WDK */
#include <ntdef.h>
#else
/* User Mode Win32 / POSIX */
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <stdint.h>
#endif
#endif

#ifndef UINT32
typedef unsigned int UINT32;
#endif

#ifndef UINT64
typedef unsigned long long UINT64;
#endif

#define BC250_ESCAPE_SIGNATURE              0xBC250000u
#define BC250_ESCAPE_OP_PRESENT_BLT         (BC250_ESCAPE_SIGNATURE | 0x01u)
/*
 * COMPOSITE_BLT -- copia linear origem->destino executada PELO KMD.
 *
 * Por que existe, medido em 13/09 08:12: naquela corrida o KMD executou 958
 * IBs. 953 eram buffers de paginacao do proprio dxgkrnl e nao produziram UM
 * TDR. Os outros 5 eram submissoes de CONTEXTO vindas da UMD no Node 0 -- o
 * no de paginacao (PagingNodeIndex=0) -- e cada uma foi seguida de um TDR, com
 * a engine OCIOSA e o anel DRENADO. Cinco TDRs, BugCheck 0x116.
 *
 * O caminho de blt do KMD (Bc250SdmaPresentBlt) ja fez 584 blts consecutivos
 * sem um unico TDR em 12/09 13:01. Ele nao cria pacote de contexto, nao passa
 * pelo escalonador e portanto nao tem caminho de TDR. Este opcode reusa esse
 * caminho para a composicao do DWM em vez de submeter contexto no no de
 * paginacao.
 *
 * ESCOPO DECLARADO: copia 1:1 de retangulo RGBA/BGRA de 32 bits. Pitch de
 * origem e destino e deslocamento no destino sao explicitos; nao ha scaling,
 * blend nem rasterizacao. O KMD usa COPY_LINEAR no caso contiguo ja provado e
 * COPY_LINEAR_SUB_WIND no caso retangular. A segunda codificacao ainda e'
 * HIPOTESE (H), NAO VALIDADA EM HARDWARE.
 */
#define BC250_ESCAPE_OP_COMPOSITE_BLT       (BC250_ESCAPE_SIGNATURE | 0x02u)

#define BC250_PRESENT_BLT_FLAG_NONE         0x00000000u
#define BC250_PRESENT_BLT_FLAG_SDMA_ONLY    0x00000001u
#define BC250_PRESENT_BLT_FLAG_HOST_COPY    0x00000002u

#define BC250_PRESENT_BLT_STATUS_SUCCESS    0x00000000u
#define BC250_PRESENT_BLT_STATUS_INVALID    0xC000000Du /* STATUS_INVALID_PARAMETER */
#define BC250_PRESENT_BLT_STATUS_UNSUPPORTED 0xC00000BBu /* STATUS_NOT_SUPPORTED */
#define BC250_PRESENT_BLT_STATUS_FAILED     0xC0000001u /* STATUS_UNSUCCESSFUL */

#pragma pack(push, 8)
typedef struct _BC250_ESCAPE_PRESENT_BLT {
    UINT32 Opcode;             /* BC250_ESCAPE_OP_PRESENT_BLT */
    UINT32 Width;              /* Largura em pixels (ex: 2560) */
    UINT32 Height;             /* Altura em pixels (ex: 1080) */
    UINT32 Pitch;              /* Pitch em bytes (ex: 2560 * 4 = 10240) */
    UINT32 SizeBytes;          /* Total de bytes (ex: 11059200) */
    UINT32 SourceAllocation;   /* Handle da alocação (D3DKMT_HANDLE) ou 0 */
    UINT64 SourceGpuVa;        /* Endereço Virtual GPU (GPUVA) ou 0 */
    UINT64 SourcePhysAddress;  /* Endereço Físico VRAM (offset local) ou 0 */
    UINT32 Flags;              /* BC250_PRESENT_BLT_FLAG_* */
    UINT32 ResultCode;         /* Código de retorno preenchido pelo KMD */
    void  *pUserPixelBuffer;   /* Ponteiro de memória host-visible mapeada em user-mode */
    UINT64 Reserved;           /* Alinhamento / expansão futura */
} BC250_ESCAPE_PRESENT_BLT;

typedef struct _BC250_ESCAPE_COMPOSITE_BLT {
    UINT32 Opcode;             /* BC250_ESCAPE_OP_COMPOSITE_BLT */
    UINT32 Flags;              /* Reservado; deve ser zero */
    UINT64 SourceGpuVa;        /* GPUVA da origem */
    UINT64 DestGpuVa;          /* GPUVA do destino */
    UINT32 Width;              /* Largura 1:1, pixels de 32 bits */
    UINT32 Height;
    UINT32 SourcePitch;        /* Bytes por linha da origem */
    UINT32 DestPitch;          /* Bytes por linha do destino */
    UINT32 DestX;              /* Offset do retangulo no destino, pixels */
    UINT32 DestY;
    UINT32 ResultCode;         /* Preenchido pelo KMD */
    UINT32 Reserved;
} BC250_ESCAPE_COMPOSITE_BLT;
#pragma pack(pop)

#endif /* BC250_ESCAPE_CONTRACT_H */
