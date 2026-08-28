#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../driver/wdk/bc250_compute_service.h"

static FILE *g_LogFile = NULL;

static void LogPrint(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);

    if (g_LogFile != NULL) {
        va_start(args, format);
        vfprintf(g_LogFile, format, args);
        va_end(args);
        fflush(g_LogFile);
    }
}

int main(int argc, char *argv[])
{
    HANDLE hDevice;
    DWORD bytesReturned = 0;
    BC250_HARDWARE_INFO hwInfo;
    BC250_PLATFORM_TELEMETRY_SNAPSHOT telemetry;
    BC250_MMIO_REG_REQ mmioReq;
    uint8_t fanIdx;
    size_t k;

    g_LogFile = fopen("tools\\bc250_hardware_diag.log", "w");
    if (g_LogFile == NULL) {
        g_LogFile = fopen("bc250_hardware_diag.log", "w");
    }

    LogPrint("================================================================================\n");
    LogPrint("   AMD BC-250 — DIAGNOSTICO SEGURO DE HARDWARE & TELEMETRIA EM SILICIO         \n");
    LogPrint("================================================================================\n\n");

    LogPrint("[PASSO 1] Abrindo conexao de kernel com \\\\.\\Bc250Compute...\n");
    hDevice = CreateFileW(
        L"\\\\.\\Bc250Compute",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        LogPrint("[!] ERRO: Nao foi possivel abrir \\\\.\\Bc250Compute (Erro %lu).\n", GetLastError());
        LogPrint("    Certifique-se de executar 'powershell -File tools\\start-bc250compute-service.ps1'.\n");
        if (g_LogFile) fclose(g_LogFile);
        return 1;
    }

    LogPrint("[+] Conexao estabelecida com sucesso!\n\n");

    /* 1. Hardware Info Geral */
    LogPrint("[PASSO 2] Consultando informacoes gerais da APU (IOCTL_BC250_GET_HARDWARE_INFO)...\n");
    ZeroMemory(&hwInfo, sizeof(hwInfo));
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_GET_HARDWARE_INFO, NULL, 0,
                        &hwInfo, sizeof(hwInfo), &bytesReturned, NULL)) {
        LogPrint("  Silicon:          %s\n", hwInfo.SiliconName);
        LogPrint("  CPU Zen 2 Cores:  %lu\n", hwInfo.CpuCoreCount);
        LogPrint("  GPU RDNA2 CUs:    %lu (%lu WGPs)\n", hwInfo.GpuCuCount, hwInfo.GpuWgpCount);
        LogPrint("  VRAM Total:       %lu MiB (Visivel: %lu MiB, ReBAR: %lu MiB)\n",
                 hwInfo.VramTotalMiB, hwInfo.CpuVisibleMiB, hwInfo.RebarSelectedMiB);
        LogPrint("  BAR5 Base MMIO:   0x%08lX (Tamanho: 0x%lX)\n\n",
                 hwInfo.MmioPhysicalBase, hwInfo.MmioLength);
    } else {
        LogPrint("[!] Falha ao consultar Hardware Info (Erro %lu)\n\n", GetLastError());
    }

    /* 2. Telemetria Consolidada (SMU + SuperIO NCT6686D) */
    LogPrint("[PASSO 3] Consultando sensores de telemetria (IOCTL_BC250_GET_TELEMETRY)...\n");
    ZeroMemory(&telemetry, sizeof(telemetry));
    if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_GET_TELEMETRY, NULL, 0,
                        &telemetry, sizeof(telemetry), &bytesReturned, NULL)) {
        LogPrint("  Timestamp QPC:    %llu\n", (unsigned long long)telemetry.TimestampQpc);

        /* Super I/O NCT6686D */
        if (telemetry.MotherboardMetrics.Valid) {
            LogPrint("\n  --- NUVOTON NCT6686D SUPER I/O (Base 0x%04X, Chip 0x%04X) ---\n",
                     telemetry.MotherboardMetrics.BaseAddress,
                     telemetry.MotherboardMetrics.ChipId);
            LogPrint("    Temp VRM MOS:     %d °C %s\n",
                     telemetry.MotherboardMetrics.TempVrmMosC,
                     telemetry.MotherboardMetrics.TempVrmMosC > 85 ? "[ALERTA: CRITICO]" : "[OK]");
            LogPrint("    Temp CPU Die:     %d °C\n", telemetry.MotherboardMetrics.TempCpuC);
            LogPrint("    Temp System/PCB:  %d °C\n", telemetry.MotherboardMetrics.TempSystemC);
            LogPrint("    Linha +12V:       %.2f V (%u mV)\n",
                     (double)telemetry.MotherboardMetrics.Voltage12VMv / 1000.0,
                     telemetry.MotherboardMetrics.Voltage12VMv);
            LogPrint("    Linha +5V:        %.2f V (%u mV)\n",
                     (double)telemetry.MotherboardMetrics.Voltage5VMv / 1000.0,
                     telemetry.MotherboardMetrics.Voltage5VMv);
            LogPrint("    Linha +3.3V:      %.2f V (%u mV)\n",
                     (double)telemetry.MotherboardMetrics.Voltage33VMv / 1000.0,
                     telemetry.MotherboardMetrics.Voltage33VMv);
            LogPrint("    Vcore / SoC:      %u mV / %u mV\n",
                     telemetry.MotherboardMetrics.VoltageVcoreMv,
                     telemetry.MotherboardMetrics.VoltageSocMv);
            LogPrint("    Ventoinhas (PWM / RPM):\n");
            for (fanIdx = 0; fanIdx < telemetry.MotherboardMetrics.FanCount; ++fanIdx) {
                if (telemetry.MotherboardMetrics.FanRpm[fanIdx] > 0 ||
                    telemetry.MotherboardMetrics.FanPwm[fanIdx] > 0) {
                    LogPrint("      Fan %u: %u RPM (PWM: %u/255 = %u%%)\n",
                             fanIdx + 1,
                             telemetry.MotherboardMetrics.FanRpm[fanIdx],
                             telemetry.MotherboardMetrics.FanPwm[fanIdx],
                             (telemetry.MotherboardMetrics.FanPwm[fanIdx] * 100u) / 255u);
                }
            }
        } else {
            LogPrint("  [Nuvoton Super I/O] Nao detectado nas portas ISA padrao (0x2E / 0x4E).\n");
        }

        /* AMD SMU v11.8 */
        if (telemetry.GpuMetrics.Valid) {
            LogPrint("\n  --- AMD SMU v11.8 (GPU & SoC) ---\n");
            LogPrint("    SMU Firmware Ver: 0x%08X (Driver IF: %u)\n",
                     telemetry.GpuMetrics.SmuFirmwareVersion,
                     telemetry.GpuMetrics.DriverIfVersion);
            LogPrint("    GPU GFX Clock:    %u MHz\n", telemetry.GpuMetrics.GfxclkFrequencyMhz);
            LogPrint("    GPU Core Voltage: %u mV\n", telemetry.GpuMetrics.GfxVoltageMv);
        } else {
            LogPrint("\n  [AMD SMU v11.8] GFXOFF / Standby ou Mailbox Inativa.\n");
        }
        LogPrint("\n");
    } else {
        LogPrint("[!] Falha ao consultar Telemetria (Erro %lu)\n\n", GetLastError());
    }

    /* 3. Ajuste de Fan se solicitado via linha de comando */
    if (argc >= 3 && strcmp(argv[1], "--set-fan") == 0) {
        ULONG fanPwm = (ULONG)atoi(argv[2]);
        BC250_SET_FAN_PWM_REQ fanReq;
        LogPrint("[PASSO 4] Solicitando ajuste de PWM do Fan 1 para %lu/255...\n", fanPwm);
        ZeroMemory(&fanReq, sizeof(fanReq));
        fanReq.FanIndex = 0; /* Fan 1 primario */
        fanReq.PwmValue = fanPwm > 255 ? 255 : fanPwm;
        if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_SET_FAN_PWM, &fanReq, sizeof(fanReq),
                            &fanReq, sizeof(fanReq), &bytesReturned, NULL) && fanReq.Success) {
            LogPrint("[+] Fan 1 PWM ajustado com sucesso para %lu/255 (%lu%%)!\n\n",
                     fanReq.PwmValue, (fanReq.PwmValue * 100u) / 255u);
        } else {
            LogPrint("[!] Falha ao ajustar PWM do Fan.\n\n");
        }
    }

    /* 4. Teste de Registradores Seguros Catalogados (docs/03_CATALOGO_DE_REGISTRADORES.md) */
    LogPrint("[PASSO 5] Testando leitura de registradores seguros catalogados...\n");
    {
        struct {
            ULONG ByteOffset;
            const char *Name;
        } safeRegs[] = {
            { 0x00008000u, "GRBM_CHIP_REV" },
            { 0x00008010u, "GRBM_STATUS" },
            { 0x00008014u, "GRBM_STATUS2" },
            { 0x00008A00u, "SCRATCH_REG0" },
            { 0x00008A04u, "SCRATCH_REG1" },
            { 0x00008A08u, "SCRATCH_REG2" },
            { 0x00008A0Cu, "SCRATCH_REG3" },
            { 0x00009100u, "CC_GC_SHADER_ARRAY_CONFIG" },
            { 0x0000C12Cu, "CP_DEVICE_ID" },
            { 0x0000C2B8u, "CP_MAX_CONTEXT" },
            { 0x0000C6DCu, "CP_SD_CNTL" },
            { 0x00034000u, "SDMA0_UCODE_CHECKSUM" },
            { 0x00034020u, "SDMA0_GFX_RB_RPTR" },
            { 0x00034028u, "SDMA0_GFX_RB_WPTR" },
            { 0x00034024u, "SDMA0_GFX_RB_WPTR_HI" },
            { 0x0003B208u, "SMU_C2PMSG_66 (Status)" }
        };

        for (k = 0; k < sizeof(safeRegs) / sizeof(safeRegs[0]); ++k) {
            LogPrint("  Lendo [%02zu/%02zu] 0x%05lX (%s)... ",
                     k + 1, sizeof(safeRegs) / sizeof(safeRegs[0]),
                     safeRegs[k].ByteOffset, safeRegs[k].Name);
            ZeroMemory(&mmioReq, sizeof(mmioReq));
            mmioReq.Offset = safeRegs[k].ByteOffset;
            if (DeviceIoControl(hDevice, (DWORD)IOCTL_BC250_READ_MMIO_REG, &mmioReq, sizeof(mmioReq),
                                &mmioReq, sizeof(mmioReq), &bytesReturned, NULL)) {
                if (mmioReq.Status == 0) {
                    LogPrint("Valor: 0x%08lX %s\n",
                             mmioReq.Value,
                             (mmioReq.Value != 0xFFFFFFFFu) ? "[OK]" : "(0xFFFFFFFF / Gated)");
                } else {
                    LogPrint("[ERRO STATUS %lu]\n", mmioReq.Status);
                }
            } else {
                LogPrint("[ERRO IOCTL %lu]\n", GetLastError());
            }
        }
    }

    CloseHandle(hDevice);
    LogPrint("\n================================================================================\n");
    LogPrint("   DIAGNOSTICO CONCLUIDO COM SUCESSO! LOG SALVO EM bc250_hardware_diag.log      \n");
    LogPrint("================================================================================\n");

    if (g_LogFile != NULL) {
        fclose(g_LogFile);
        g_LogFile = NULL;
    }

    return 0;
}
