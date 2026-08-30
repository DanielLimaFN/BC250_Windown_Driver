#ifndef BC250_CMOS_VRAM_CONTRACT_H
#define BC250_CMOS_VRAM_CONTRACT_H

#include <stdint.h>
#include "bc250_superio_nct6686_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Contrato de Leitura de VRAM via CMOS APCB (UMA_SIZE)
 * 
 * Na plataforma AMD BC-250 (Zen2 + Ariel/Cyan Skillfish), o BIOS/UEFI grava
 * a configuracao de memoria particionada (UMA VRAM) na tabela APCB no CMOS.
 * 
 * Portas I/O:
 *   - Indice CMOS: Porta 0x72
 *   - Dados CMOS:  Porta 0x73
 * 
 * Estrutura no CMOS (offset 0x90 a 0xAF = 32 bytes):
 *   - Offset 0x00 (0x90): Assinatura 'APCB' = 0x42435041 (Little Endian)
 *   - Offset 0x1A (0xAA): UINT16 UMA_SIZE em Megabytes (ex: 512, 1024, 2048, 4096, 8192, 16384)
 */

#define BC250_CMOS_INDEX_PORT            0x72u
#define BC250_CMOS_DATA_PORT             0x73u
#define BC250_CMOS_APCB_BASE_OFFSET      0x90u
#define BC250_CMOS_APCB_SIZE_BYTES       32u
#define BC250_CMOS_APCB_SIGNATURE        0x42435041u /* 'APCB' */
#define BC250_CMOS_APCB_UMA_SIZE_OFFSET  0x1Au      /* 0xAA - 0x90 = 0x1A */

#define BC250_CMOS_UMA_MIN_MB            256u
#define BC250_CMOS_UMA_MAX_MB            16384u

typedef enum BC250_CMOS_VRAM_STATUS {
    BC250_CMOS_VRAM_OK = 0,
    BC250_CMOS_VRAM_BAD_ARGUMENT,
    BC250_CMOS_VRAM_BAD_SIGNATURE,
    BC250_CMOS_VRAM_OUT_OF_RANGE
} BC250_CMOS_VRAM_STATUS;

/*
 * Executa a leitura transacional da tabela APCB no CMOS e retorna o tamanho
 * de VRAM configurado no BIOS em Megabytes e Bytes.
 */
BC250_CMOS_VRAM_STATUS Bc250ReadCmosUmaSize(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t *OutUmaSizeMb,
    uint64_t *OutVramBytes);

/* Retorna o nome textual do status para logs e diagnosticos */
const char *Bc250CmosVramStatusName(BC250_CMOS_VRAM_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_CMOS_VRAM_CONTRACT_H */
