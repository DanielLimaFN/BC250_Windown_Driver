#ifndef BC250CMOS_ABI_H
#define BC250CMOS_ABI_H

/*
 * ABI entre bc250-vram.exe e bc250cmos.sys (driver minimo, sem PnP, que so'
 * fala com as portas de CMOS 0x72/0x73). Ver bc250_apcb_memconf.h para a
 * procedencia do bloco e docs/550 para o desenho.
 */

#include "bc250_apcb_memconf.h"

#define BC250CMOS_DEVICE_NAME   L"\\Device\\Bc250Cmos"
#define BC250CMOS_SYMLINK_NAME  L"\\DosDevices\\Bc250Cmos"
#define BC250CMOS_USER_PATH     "\\\\.\\Bc250Cmos"

#define BC250CMOS_IOCTL_BASE    0x925u
#define BC250CMOS_IOCTL(n) CTL_CODE(FILE_DEVICE_UNKNOWN, BC250CMOS_IOCTL_BASE + (n), METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

/* Saida: BC250CMOS_BANK (256 bytes do banco 0x72/0x73). */
#define IOCTL_BC250CMOS_READ_BANK   BC250CMOS_IOCTL(0x00)
/* Entrada: BC250CMOS_WRITE_REQUEST. Saida: BC250CMOS_WRITE_RESULT. */
#define IOCTL_BC250CMOS_WRITE_UMA   BC250CMOS_IOCTL(0x01)

/*
 * Entrada: BC250CMOS_RESET_REQUEST. Saida: BC250CMOS_RESET_RESULT.
 * Le FCH_PM_S5_RESET_STATUS (bc250_fch_reset_reason.h) e, com Clear=1, faz o
 * mesmo W1C do Linux e rele. O valor ANTES da limpeza volta na resposta.
 */
#define IOCTL_BC250CMOS_RESET_REASON BC250CMOS_IOCTL(0x02)

#define BC250CMOS_ABI_MAGIC     0x534F4D43u /* "CMOS" */
#define BC250CMOS_ABI_VERSION   1u

typedef struct BC250CMOS_BANK {
    bc250_u8 Bytes[BC250_CMOS_BANK_BYTES];
} BC250CMOS_BANK;

typedef struct BC250CMOS_WRITE_REQUEST {
    bc250_u32 Magic;
    bc250_u32 Version;
    bc250_u32 NewUmaMb;
    /* Bloco 0x90..0xAB que a ferramenta leu. O kernel so' grava se o atual for IDENTICO. */
    bc250_u8  Expected[BC250_APCB_BYTES];
    /* Bloco que a ferramenta planejou. O kernel refaz o plano e exige igualdade. */
    bc250_u8  Planned[BC250_APCB_BYTES];
} BC250CMOS_WRITE_REQUEST;

typedef enum BC250CMOS_WRITE_STATUS {
    BC250CMOS_WRITE_OK = 0,
    BC250CMOS_WRITE_BAD_REQUEST,
    BC250CMOS_WRITE_STALE,              /* bloco atual != Expected: nada gravado */
    BC250CMOS_WRITE_PLAN_REFUSED,       /* contrato recusou (PlanStatus diz por que) */
    BC250CMOS_WRITE_PLAN_DIVERGED,      /* plano do kernel != plano da ferramenta */
    BC250CMOS_WRITE_READBACK_MISMATCH_RESTORED,   /* gravou, releu diferente, restaurou Expected */
    BC250CMOS_WRITE_READBACK_MISMATCH_UNRESTORED  /* idem, e a restauracao tambem nao conferiu */
} BC250CMOS_WRITE_STATUS;

typedef struct BC250CMOS_WRITE_RESULT {
    bc250_u32 Status;       /* BC250CMOS_WRITE_STATUS */
    bc250_u32 PlanStatus;   /* BC250_APCB_STATUS */
    bc250_u8  Before[BC250_APCB_BYTES];
    bc250_u8  Readback[BC250_APCB_BYTES];
} BC250CMOS_WRITE_RESULT;

typedef struct BC250CMOS_RESET_REQUEST {
    bc250_u32 Magic;
    bc250_u32 Version;
    bc250_u32 Clear;        /* 1 = escrever o valor lido de volta (W1C) */
} BC250CMOS_RESET_REQUEST;

typedef enum BC250CMOS_RESET_STATUS {
    BC250CMOS_RESET_OK = 0,
    BC250CMOS_RESET_BAD_REQUEST,
    BC250CMOS_RESET_MAP_FAILED,
    BC250CMOS_RESET_INVALID_READ      /* 0xFFFFFFFF: nada limpo, como o Linux */
} BC250CMOS_RESET_STATUS;

typedef struct BC250CMOS_RESET_RESULT {
    bc250_u32 Status;       /* BC250CMOS_RESET_STATUS */
    bc250_u32 Raw;          /* valor lido ANTES de qualquer escrita */
    bc250_u32 Cleared;      /* 1 = houve escrita W1C */
    bc250_u32 AfterClear;   /* releitura depois da limpeza (= Raw se nao limpou) */
} BC250CMOS_RESET_RESULT;

#endif
