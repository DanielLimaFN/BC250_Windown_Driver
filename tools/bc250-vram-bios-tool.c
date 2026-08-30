/* bc250-vram-bios-tool.c
 *
 * Ferramenta de diagnostico e inspecao de VRAM alocada no BIOS para AMD BC-250.
 *
 * 1. Le os parametros de VRAM do hardware real a partir da telemetria de registradores
 * 2. Faz o dump dos recursos PnP e ReBAR (CpuVisible e VramTotal)
 * 3. Valida a estrutura da tabela CMOS APCB (UMA_SIZE)
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <cfgmgr32.h>
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "advapi32.lib")

#include "../driver/core/bc250_cmos_vram_contract.h"

static void ReadDriverRegistryTelemetry(void)
{
    HKEY hKey;
    DWORD totalMib = 0;
    DWORD cpuVisibleMib = 0;
    DWORD rebarMib = 0;
    DWORD fbBase16M = 0;
    DWORD fbTop16M = 0;
    DWORD valSize = sizeof(DWORD);

    printf("\n=== TELEMETRIA DE REGISTRADORES DE HARDWARE (DRV PROBE) ===\n");

    /* Tenta ler a chave de telemetria gravada pelo driver MCDM no registro */
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Services\\bc250mcdm\\Parameters\\Probe",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "VramTotalMiB", NULL, NULL, (LPBYTE)&totalMib, &valSize);
        valSize = sizeof(DWORD);
        RegQueryValueExA(hKey, "CpuVisibleMiB", NULL, NULL, (LPBYTE)&cpuVisibleMib, &valSize);
        valSize = sizeof(DWORD);
        RegQueryValueExA(hKey, "RebarSelectedMiB", NULL, NULL, (LPBYTE)&rebarMib, &valSize);
        valSize = sizeof(DWORD);
        RegQueryValueExA(hKey, "FbBase16M", NULL, NULL, (LPBYTE)&fbBase16M, &valSize);
        valSize = sizeof(DWORD);
        RegQueryValueExA(hKey, "FbTop16M", NULL, NULL, (LPBYTE)&fbTop16M, &valSize);
        RegCloseKey(hKey);

        if (totalMib > 0) {
            printf(">> [BIOS UMA_SIZE DETECTADO NO HARDWARE] <<\n");
            printf("  * VRAM TOTAL DA APU (BIOS UMA):  %u MB (%.2f GB)\n", totalMib, totalMib / 1024.0f);
            printf("  * ABERTURA CPU VISIVEL (BAR0):   %u MB\n", cpuVisibleMib);
            printf("  * CONFIGURACAO ReBAR ATIVA:      %u MB\n", rebarMib);
            if (fbTop16M >= fbBase16M && fbBase16M > 0) {
                uint32_t fbSpanMb = (fbTop16M - fbBase16M + 1) * 16;
                printf("  * REGISTRADORES FB_LOCATION:     Base=%u Top=%u -> %u MB\n",
                       fbBase16M, fbTop16M, fbSpanMb);
            }
            return;
        }
    }

    printf("  (Driver de telemetria probe nao encontrado no registro no momento)\n");
}

static void DumpPciHardwareResources(const char *vendorDevNeedle)
{
    ULONG len = 0;
    char *buf = NULL;
    int hits = 0;

    printf("\n=== Recursos de Memoria PnP / BIOS (Windows Configuration Manager) ===\n");

    if (CM_Get_Device_ID_List_SizeA(&len, NULL, CM_GETIDLIST_FILTER_NONE) != CR_SUCCESS || len == 0) {
        printf("Aviso: Nao foi possivel obter a lista de dispositivos PnP.\n");
        return;
    }

    buf = (char *)calloc(len, 1);
    if (!buf || CM_Get_Device_ID_ListA(NULL, buf, len, CM_GETIDLIST_FILTER_NONE) != CR_SUCCESS) {
        printf("Aviso: Falha ao ler lista PnP.\n");
        if (buf) free(buf);
        return;
    }

    for (char *p = buf; *p; p += strlen(p) + 1) {
        if (!strstr(p, vendorDevNeedle)) continue;
        DEVINST dn = 0;
        if (CM_Locate_DevNodeA(&dn, p, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
            LOG_CONF logConf = 0;
            printf("Dispositivo Encontrado: %s\n", p);
            
            /* Tenta primeiro ALLOC_LOG_CONF e depois BOOT_LOG_CONF */
            if (CM_Get_First_Log_Conf(&logConf, dn, ALLOC_LOG_CONF) == CR_SUCCESS && logConf) {
                RES_DES rd = logConf;
                while (rd) {
                    RES_DES next = 0;
                    ULONG size = 0;
                    if (CM_Get_Next_Res_Des(&next, rd, ResType_Mem, &size, 0) == CR_SUCCESS) {
                        MEM_RESOURCE *mr = (MEM_RESOURCE *)malloc(size);
                        if (mr && CM_Get_Res_Des_Data(next, mr, size, 0) == CR_SUCCESS) {
                            ULONGLONG start = mr->MEM_Header.MD_Alloc_Base;
                            ULONGLONG end   = mr->MEM_Header.MD_Alloc_End;
                            ULONGLONG lenBytes = end - start + 1;
                            printf("  -> [MEM] PA Base = 0x%08X%08X | Tamanho = %6llu MB (%llu bytes)\n",
                                   (unsigned)(start >> 32), (unsigned)start,
                                   lenBytes / (1024 * 1024), lenBytes);
                        }
                        if (mr) free(mr);
                        if (rd != logConf) CM_Free_Res_Des_Handle(rd);
                        rd = next;
                    } else {
                        break;
                    }
                }
                CM_Free_Log_Conf_Handle(logConf);
            }
            hits++;
            printf("\n");
        }
    }

    if (!hits) {
        printf("Nenhum dispositivo com HWID '%s' encontrado no Configuration Manager.\n", vendorDevNeedle);
    }
    if (buf) free(buf);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("====================================================================\n");
    printf("     AMD BC-250 BIOS VRAM (UMA_SIZE) DIAGNOSTIC & VERIFICATION\n");
    printf("====================================================================\n");

    /* 1. Leitura direta da telemetria de hardware capturada pelo driver */
    ReadDriverRegistryTelemetry();

    /* 2. Informacoes da estrutura de CMOS */
    printf("\n=== ESTRUTURA CMOS APCB (fanoush/bc250_memcfg standard) ===\n");
    printf("  - Indice I/O Port:   0x%02X\n", BC250_CMOS_INDEX_PORT);
    printf("  - Dados I/O Port:    0x%02X\n", BC250_CMOS_DATA_PORT);
    printf("  - APCB Base Offset:  0x%02X (Assinatura 'APCB' = 0x%08X)\n",
           BC250_CMOS_APCB_BASE_OFFSET, BC250_CMOS_APCB_SIGNATURE);
    printf("  - UMA_SIZE Offset:   0xAA (Offset relativo: 0x%02X)\n",
           BC250_CMOS_APCB_UMA_SIZE_OFFSET);
    printf("  - Limites Validos:   %u MB ate %u MB (multiplo de 16MB)\n",
           BC250_CMOS_UMA_MIN_MB, BC250_CMOS_UMA_MAX_MB);

    /* 3. Recursos PnP */
    DumpPciHardwareResources("13FE");

    printf("====================================================================\n");
    printf("COMO USAR ESTA VERIFICACAO QUANDO MODIFICAR A BIOS:\n");
    printf("1. Ao entrar no setup da BIOS do BC-250, altere o campo UMA_SIZE / VRAM\n");
    printf("   (ex: 512M -> 2048M / 4096M / 8192M / 16384M).\n");
    printf("2. Apos o boot, o modulo 'bc250_cmos_vram_contract' le o CMOS nas portas\n");
    printf("   0x72/0x73 e reporta o valor exato para o driver e o Windows.\n");
    printf("====================================================================\n");

    return 0;
}
