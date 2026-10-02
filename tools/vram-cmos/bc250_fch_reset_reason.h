#ifndef BC250_FCH_RESET_REASON_H
#define BC250_FCH_RESET_REASON_H

/*
 * Motivo do ultimo reset da plataforma (FCH), doc 553.
 *
 * Procedencia (P1): Linux arch/x86/kernel/cpu/amd.c, print_s5_reset_status_mmio()
 * e include/linux/platform_data/x86/amd-fch.h:
 *   FCH_PM_BASE = 0xFED80300, FCH_PM_S5_RESET_STATUS = 0xC0 (32 bits);
 *   0xFFFFFFFF = leitura invalida, ignorar;
 *   o Linux le, ESCREVE O MESMO VALOR DE VOLTA (W1C, limpa) e imprime os bits.
 * Consequencia medida no doc 552: como o Linux limpa a cada boot, um bit que
 * aparece no Windows depois de um boot Linux nasceu DEPOIS daquele boot.
 *
 * A coleta Linux de 01/10 14:52 leu 0x40080400 (bit 30 "parity error",
 * bit 19 "CF9=0x6", bit 10 sem nome no upstream).
 */

#define BC250_FCH_PM_BASE              0xFED80300u
#define BC250_FCH_PM_S5_RESET_STATUS   0x000000C0u
#define BC250_FCH_S5_RESET_STATUS_PA   (BC250_FCH_PM_BASE + BC250_FCH_PM_S5_RESET_STATUS)
#define BC250_FCH_RESET_INVALID        0xFFFFFFFFu

static __inline const char *Bc250FchResetBitName(unsigned Bit)
{
    switch (Bit) {
    case 0:  return "thermal pin BP_THERMTRIP_L was tripped";
    case 1:  return "power button was pressed for 4 seconds";
    case 2:  return "shutdown pin was tripped";
    case 4:  return "remote ASF power off command was received";
    case 9:  return "internal CPU thermal limit was tripped";
    case 16: return "system reset pin BP_SYS_RST_L was tripped";
    case 17: return "software issued PCI reset";
    case 18: return "software wrote 0x4 to reset control register 0xCF9";
    case 19: return "software wrote 0x6 to reset control register 0xCF9";
    case 20: return "software wrote 0xE to reset control register 0xCF9";
    case 21: return "ACPI power state transition occurred";
    case 22: return "keyboard reset pin KB_RST_L was tripped";
    case 23: return "internal CPU shutdown event occurred";
    case 24: return "system failed to boot before timer expired";
    case 25: return "hardware watchdog timer expired";
    case 26: return "remote ASF reset command was received";
    case 27: return "uncorrected error caused data fabric sync flood";
    case 29: return "FCH and MP1 failed warm reset handshake";
    case 30: return "a parity error occurred";
    case 31: return "a software sync flood event occurred";
    default: return 0; /* sem nome no upstream: imprimir so' o numero */
    }
}

#endif
