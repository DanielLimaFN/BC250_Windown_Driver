/*
 * bc250-vram.exe -- le e grava o tamanho da VRAM (UMA_SIZE) da BC-250 na CMOS,
 * o equivalente Windows de "bc250-linux-referencia.sh --ler-vram/--set-vram".
 *
 *   bc250-vram.exe --ler
 *   bc250-vram.exe --set-uma 2048 --confirmo
 *   bc250-vram.exe --ler --motivo-reset [--limpar-reset]   (doc 553)
 *
 * Exige o bc250cmos.sys carregado (bc250-vram.ps1 faz isso e descarrega no fim).
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bc250cmos_abi.h"
#include "bc250_fch_reset_reason.h"

static void DumpBank(const bc250_u8 *b)
{
    unsigned n;
    printf("     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
    printf("----------------------------------------------------\n");
    for (n = 0; n < BC250_CMOS_BANK_BYTES; ++n) {
        if ((n % 16u) == 0u) printf("%s%04X ", n ? "\n" : "", n / 16u);
        printf("%02X ", b[n]);
    }
    printf("\n");
}

static void DumpConf(const bc250_u8 *apcb)
{
    BC250_APCB_MEMCONF c;
    (void)Bc250ApcbDecode(apcb, BC250_APCB_BYTES, &c);
    printf("\nSignature    : 0x%08X (%s)\n", (unsigned)c.Signature, Bc250ApcbSignatureName(c.Signature));
    printf("Checksum     : 0x%04X (calculado 0x%04X -> %s)\n", c.Checksum,
        Bc250ApcbChecksum(apcb, BC250_APCB_BYTES),
        c.Checksum == Bc250ApcbChecksum(apcb, BC250_APCB_BYTES) ? "CONFERE" : "NAO CONFERE");
    printf("ClockSpeed=%u\ntCL=%02u\ntRAS=%02u\ntRCDRD=%02u\ntRCDWR=%02u\ntRCAb=%02u\ntRCPb=%02u\n",
        c.ClockSpeed, c.tCL, c.tRAS, c.tRCDRD, c.tRCDWR, c.tRCAb, c.tRCPb);
    printf("tRPAb=%02u\ntRPPb=%02u\ntRRDS=%02u\ntRRDL=%02u\ntRTP=%02u\ntFAW=%02u\n",
        c.tRPAb, c.tRPPb, c.tRRDS, c.tRRDL, c.tRTP, c.tFAW);
    printf("tREF=%04u\nRFCPb=%04u\ntRFC=%04u\n", c.tREF, c.RFCPb, c.tRFC);
    printf("UMA_SIZE=%04u MB\n", c.UmaSizeMb);
    if (c.Signature == BC250_APCB_SIG_LINUX_TOOL) {
        printf("ESTADO: pedido de %u MB PENDENTE -- so' vale depois de desligar da tomada e ligar (boot frio).\n", c.UmaSizeMb);
    } else if (c.Signature == BC250_APCB_SIG_ABL) {
        printf("ESTADO: valor APLICADO pelo firmware no ultimo boot.\n");
    } else {
        printf("ESTADO: assinatura de ERRO/desconhecida -- a ferramenta nao grava sobre este bloco.\n");
    }
}

static int ReadBank(HANDLE h, BC250CMOS_BANK *bank)
{
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_BC250CMOS_READ_BANK, NULL, 0, bank, sizeof(*bank), &got, NULL) ||
        got != sizeof(*bank)) {
        printf("ERRO: leitura da CMOS falhou (GetLastError=%lu)\n", GetLastError());
        return 0;
    }
    return 1;
}

static const char *WriteStatusName(bc250_u32 s)
{
    switch (s) {
    case BC250CMOS_WRITE_OK: return "OK (gravado e relido igual)";
    case BC250CMOS_WRITE_BAD_REQUEST: return "pedido invalido";
    case BC250CMOS_WRITE_STALE: return "bloco mudou entre a leitura e a gravacao -- NADA gravado";
    case BC250CMOS_WRITE_PLAN_REFUSED: return "contrato recusou -- NADA gravado";
    case BC250CMOS_WRITE_PLAN_DIVERGED: return "plano do kernel diferente do da ferramenta -- NADA gravado";
    case BC250CMOS_WRITE_READBACK_MISMATCH_RESTORED: return "releitura divergiu; bloco ORIGINAL restaurado";
    case BC250CMOS_WRITE_READBACK_MISMATCH_UNRESTORED: return "releitura divergiu e a restauracao NAO conferiu -- NAO desligue sem reler";
    default: return "desconhecido";
    }
}

static int ResetReason(HANDLE h, int clear)
{
    BC250CMOS_RESET_REQUEST req;
    BC250CMOS_RESET_RESULT res;
    BYTE io[sizeof(BC250CMOS_RESET_RESULT) > sizeof(BC250CMOS_RESET_REQUEST) ?
            sizeof(BC250CMOS_RESET_RESULT) : sizeof(BC250CMOS_RESET_REQUEST)];
    DWORD got = 0;
    unsigned bit;

    printf("\n=== MOTIVO DO ULTIMO RESET DA PLATAFORMA (FCH S5_RESET_STATUS 0x%08X) ===\n",
        (unsigned)BC250_FCH_S5_RESET_STATUS_PA);
    memset(&req, 0, sizeof(req));
    req.Magic = BC250CMOS_ABI_MAGIC;
    req.Version = BC250CMOS_ABI_VERSION;
    req.Clear = clear ? 1u : 0u;
    memcpy(io, &req, sizeof(req));
    if (!DeviceIoControl(h, IOCTL_BC250CMOS_RESET_REASON, io, sizeof(req), io, sizeof(res), &got, NULL) ||
        got != sizeof(res)) {
        printf("ERRO: IOCTL de motivo de reset falhou (GetLastError=%lu)\n", GetLastError());
        return 0;
    }
    memcpy(&res, io, sizeof(res));
    if (res.Status == BC250CMOS_RESET_MAP_FAILED) {
        printf("RESET_REASON: mapeamento falhou -- sem leitura.\n");
        return 0;
    }
    if (res.Status == BC250CMOS_RESET_INVALID_READ) {
        printf("RESET_REASON raw=0xFFFFFFFF -- leitura invalida (o Linux tambem ignora). Nada limpo.\n");
        return 1;
    }
    if (res.Status != BC250CMOS_RESET_OK) {
        printf("RESET_REASON: pedido recusado (status=%u)\n", (unsigned)res.Status);
        return 0;
    }
    printf("RESET_REASON raw=0x%08X\n", (unsigned)res.Raw);
    if (res.Raw == 0u) printf("RESET_REASON nenhum bit ligado desde a ultima limpeza\n");
    for (bit = 0; bit < 32u; ++bit) {
        if (res.Raw & (1u << bit)) {
            const char *name = Bc250FchResetBitName(bit);
            printf("RESET_REASON bit %2u: %s\n", bit, name ? name : "(sem nome no upstream)");
        }
    }
    if (res.Cleared) {
        printf("RESET_REASON limpo (W1C, como o Linux); releitura=0x%08X\n", (unsigned)res.AfterClear);
    } else {
        printf("RESET_REASON NAO limpo (%s)\n", clear ? "nada a limpar" : "--limpar-reset ausente");
    }
    return 1;
}

int main(int argc, char **argv)
{
    HANDLE h;
    BC250CMOS_BANK bank;
    const bc250_u8 *apcb;
    int doSet = 0, confirmed = 0, doReset = 0, clearReset = 0, i;
    unsigned long uma = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--ler") == 0) {
        } else if (strcmp(argv[i], "--set-uma") == 0 && i + 1 < argc) {
            doSet = 1;
            uma = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--confirmo") == 0) {
            confirmed = 1;
        } else if (strcmp(argv[i], "--motivo-reset") == 0) {
            doReset = 1;
        } else if (strcmp(argv[i], "--limpar-reset") == 0) {
            clearReset = 1;
        } else {
            printf("uso: bc250-vram.exe --ler [--motivo-reset [--limpar-reset]] | --set-uma <MB> --confirmo\n");
            return 2;
        }
    }

    h = CreateFileA(BC250CMOS_USER_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERRO: %s nao abriu (GetLastError=%lu). Rode pelo bc250-vram.ps1, como Administrador.\n",
            BC250CMOS_USER_PATH, GetLastError());
        return 1;
    }

    printf("=== LEITURA DE CMOS APCB (VRAM / UMA_SIZE) ===\n");
    if (!ReadBank(h, &bank)) { CloseHandle(h); return 1; }
    DumpBank(bank.Bytes);
    apcb = bank.Bytes + BC250_APCB_OFFSET;
    DumpConf(apcb);
    if (doReset && !ResetReason(h, clearReset)) { CloseHandle(h); return 5; }

    if (doSet) {
        BC250CMOS_WRITE_REQUEST req;
        BC250CMOS_WRITE_RESULT res;
        BC250_APCB_STATUS plan;
        DWORD got = 0;
        BYTE io[sizeof(BC250CMOS_WRITE_REQUEST) > sizeof(BC250CMOS_WRITE_RESULT) ?
                sizeof(BC250CMOS_WRITE_REQUEST) : sizeof(BC250CMOS_WRITE_RESULT)];

        printf("\n=== AJUSTE DE VRAM NA CMOS APCB (UMA_SIZE = %lu MB) ===\n", uma);
        memset(&req, 0, sizeof(req));
        req.Magic = BC250CMOS_ABI_MAGIC;
        req.Version = BC250CMOS_ABI_VERSION;
        req.NewUmaMb = (bc250_u32)uma;
        memcpy(req.Expected, apcb, BC250_APCB_BYTES);
        plan = Bc250ApcbPlanUmaWrite(req.Expected, BC250_APCB_BYTES, req.NewUmaMb, req.Planned);
        if (plan != BC250_APCB_OK) {
            printf("RECUSADO antes de tocar a CMOS: %s (faixa aceita %u..%u MB, passo %u)\n",
                Bc250ApcbStatusName(plan), BC250_APCB_UMA_MIN_MB, BC250_APCB_UMA_MAX_MB, BC250_APCB_UMA_STEP_MB);
            CloseHandle(h);
            return 3;
        }
        printf("Plano: so' UMA_SIZE e assinatura/checksum mudam; timings intactos.\n");
        DumpConf(req.Planned);
        if (!confirmed) {
            printf("\nSIMULACAO: nada gravado. Repita com --confirmo para gravar.\n");
            CloseHandle(h);
            return 0;
        }
        memcpy(io, &req, sizeof(req));
        if (!DeviceIoControl(h, IOCTL_BC250CMOS_WRITE_UMA, io, sizeof(req), io, sizeof(res), &got, NULL) ||
            got != sizeof(res)) {
            printf("ERRO: IOCTL de gravacao falhou (GetLastError=%lu)\n", GetLastError());
            CloseHandle(h);
            return 1;
        }
        memcpy(&res, io, sizeof(res));
        printf("\nResultado do kernel: %s (plano=%s)\n", WriteStatusName(res.Status),
            Bc250ApcbStatusName((BC250_APCB_STATUS)res.PlanStatus));
        if (res.Status == BC250CMOS_WRITE_OK) {
            printf("Releitura:\n");
            DumpConf(res.Readback);
            printf("\n------------------------------------------------------------------\n");
            printf("VALOR GRAVADO NA CMOS. Desligue a maquina DA TOMADA (boot frio)\n");
            printf("para a ABL aplicar. Depois rode --ler: a assinatura volta a ABL.\n");
            printf("------------------------------------------------------------------\n");
        }
        CloseHandle(h);
        return res.Status == BC250CMOS_WRITE_OK ? 0 : 4;
    }

    CloseHandle(h);
    return 0;
}
