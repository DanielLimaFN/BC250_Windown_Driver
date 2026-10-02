#ifndef BC250_APCB_MEMCONF_H
#define BC250_APCB_MEMCONF_H

/*
 * Bloco de configuracao de memoria da BC-250 na CMOS (APCB lido pela ABL).
 *
 * PROCEDENCIA:
 *   - layout e protocolo: referencias/linux/bc250_memcfg.cpp (ferramenta da
 *     comunidade, fanoush/bc250_memcfg), indice 0x72 / dado 0x73, bloco em
 *     0x90..0xAB (28 bytes), UMA_SIZE = WORD em 0xAA, em MB;
 *   - P0 (doc 549, 01/10/2026): o menu da BIOS NAO controla a UMA; este bloco
 *     controla. Com UMA_SIZE=512 o driver Windows leu 512 MiB nas Corridas
 *     194-196; gravado 2048 pela ferramenta Linux, o boot seguinte reservou
 *     exatamente 2 GiB (iomem 3f0000000-46fffffff) e a releitura mostrou a
 *     assinatura da ABL de volta: o firmware consome o pedido
 *     (LINUX_TOOL_SIGNATURE) e regrava ABL_SIGNATURE;
 *   - checksum = soma de 16 bits dos bytes 0x96..0xAB, conferida contra os
 *     dois dumps reais (0x03FA com 512 MB, 0x0400 com 2048 MB).
 *
 * Este arquivo e' logica pura (sem I/O): compila em user mode e kernel mode.
 */

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
typedef UCHAR  bc250_u8;
typedef USHORT bc250_u16;
typedef ULONG  bc250_u32;
#else
#include <stdint.h>
typedef uint8_t  bc250_u8;
typedef uint16_t bc250_u16;
typedef uint32_t bc250_u32;
#endif

#define BC250_CMOS_INDEX_PORT        0x72u
#define BC250_CMOS_DATA_PORT         0x73u
#define BC250_CMOS_BANK_BYTES        256u
#define BC250_APCB_OFFSET            0x90u
#define BC250_APCB_BYTES             28u      /* 0x90..0xAB */
#define BC250_APCB_CHECKSUM_FIRST    6u       /* soma comeca em 0x96 */

#define BC250_APCB_SIG_LINUX_TOOL    0x42435041u /* pedido pendente: a ABL aplica no proximo boot frio */
#define BC250_APCB_SIG_ABL           0x4C424124u /* "$ABL": aplicado pela ABL */
#define BC250_APCB_SIG_CMOS_BAD      0x42534D43u
#define BC250_APCB_SIG_WATCHDOG      0x46544457u
#define BC250_APCB_SIG_CHECKSUM_ERR  0x454B4843u
#define BC250_APCB_SIG_SIGNATURE_ERR 0x45474953u

/*
 * Faixa aceita para gravacao: 256..8192 MB em passos de 256 MB.
 *   - 256 MB = o BAR da placa (sem Resizable BAR na BIOS, doc 496 §5): com
 *     esse valor TODA a VRAM fica visivel a CPU. E' o piso da ferramenta
 *     Linux e a meta de projeto desde 01/10 (doc 551: funcionar com o minimo,
 *     sem VRAM grande como muleta). Nunca medido nesta placa: que a ABL o
 *     aceite e' H; a releitura depois do boot frio decide.
 *   - 512 MB e' o valor de fabrica medido; 8 GB e' o teto do menu da BIOS.
 */
#define BC250_APCB_UMA_MIN_MB        256u
#define BC250_APCB_UMA_MAX_MB        8192u
#define BC250_APCB_UMA_STEP_MB       256u

typedef struct BC250_APCB_MEMCONF {
    bc250_u32 Signature;   /* 0x90 */
    bc250_u16 Checksum;    /* 0x94 */
    bc250_u16 ClockSpeed;  /* 0x96 */
    bc250_u8  tCL;         /* 0x98 */
    bc250_u8  tRAS;
    bc250_u8  tRCDRD;
    bc250_u8  tRCDWR;
    bc250_u8  tRCAb;
    bc250_u8  tRCPb;
    bc250_u8  tRPAb;
    bc250_u8  tRPPb;
    bc250_u8  tRRDS;
    bc250_u8  tRRDL;
    bc250_u8  tRTP;
    bc250_u8  tFAW;        /* 0xA3 */
    bc250_u16 tREF;        /* 0xA4 */
    bc250_u16 RFCPb;       /* 0xA6 */
    bc250_u16 tRFC;        /* 0xA8 */
    bc250_u16 UmaSizeMb;   /* 0xAA */
} BC250_APCB_MEMCONF;

typedef enum BC250_APCB_STATUS {
    BC250_APCB_OK = 0,
    BC250_APCB_BAD_ARGUMENT,
    BC250_APCB_UNKNOWN_SIGNATURE,      /* assinatura de erro ou desconhecida */
    BC250_APCB_BAD_CHECKSUM,
    BC250_APCB_UMA_OUT_OF_RANGE,
    BC250_APCB_UMA_NOT_ALIGNED,
    BC250_APCB_UMA_UNCHANGED
} BC250_APCB_STATUS;

/* Decodifica os 28 bytes de 0x90..0xAB (little-endian) sem depender de packing. */
BC250_APCB_STATUS Bc250ApcbDecode(const bc250_u8 *Bytes, unsigned ByteCount,
                                  BC250_APCB_MEMCONF *Out);

/* Soma de 16 bits de 0x96..0xAB. */
bc250_u16 Bc250ApcbChecksum(const bc250_u8 *Bytes, unsigned ByteCount);

/* 1 = assinatura ABL ou LINUX_TOOL e checksum confere. */
int Bc250ApcbIsTrustworthy(const bc250_u8 *Bytes, unsigned ByteCount);

const char *Bc250ApcbSignatureName(bc250_u32 Signature);
const char *Bc250ApcbStatusName(BC250_APCB_STATUS Status);

/*
 * Planeja a gravacao: copia Current para Planned, troca SO' o UMA_SIZE,
 * marca LINUX_TOOL_SIGNATURE (o mesmo pedido que a ABL ja' aceitou na coleta
 * Linux) e recalcula o checksum. Recusa bloco atual nao confiavel, valor fora
 * da faixa/passo e valor igual ao atual. Os timings nunca sao tocados.
 */
BC250_APCB_STATUS Bc250ApcbPlanUmaWrite(const bc250_u8 *Current,
                                        unsigned ByteCount,
                                        bc250_u32 NewUmaMb,
                                        bc250_u8 *Planned);

#endif
