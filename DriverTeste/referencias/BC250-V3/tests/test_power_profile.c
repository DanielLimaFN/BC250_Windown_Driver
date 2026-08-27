/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_power_profile.c

Abstract:
    Test 7 / Utility: Interactive Power Limit & Overclock Manager for AMD BC-250 (Cyan Skillfish).
    Applies validated Safe-Point V/F curves and TDP limits via SMU Escape 0xBD23.

--*/

#include "test_common.h"

static void PrintHeader(void)
{
    printf("=================================================================\n");
    printf(" AMD BC-250 (Cyan Skillfish) - Gerenciador de Energia & Overclock\n");
    printf("=================================================================\n\n");
}

static void ShowLiveTelemetry(D3DKMT_HANDLE hAdapter)
{
    BC250_WDDM_ESCAPE_TELEMETRY telem = { 0 };
    telem.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
    telem.Header.InSize = sizeof(telem);
    telem.Header.OutSize = sizeof(telem);

    if (CallBc250Escape(hAdapter, &telem, sizeof(telem))) {
        printf("[*] Telemetria Atual:\n");
        printf("    GPU Clock: %u MHz | Mem Clock: %u MHz | SoC Clock: %u MHz\n", telem.GfxClockMhz, telem.MemClockMhz, telem.SocClockMhz);
        printf("    GPU Temp : %u C   | SoC Temp  : %u C   | Hotspot  : %u C\n", telem.GfxTemperatureC, telem.SocTemperatureC, telem.HotspotTemperatureC);
        printf("    Tensao   : %u mV  | Consumo   : %u W   | Origem   : %s\n\n", telem.GfxVoltageMv, telem.PowerWatts,
            (telem.Flags == 1) ? "Tabela DMA SMU" : "Sonda SMN Direct");
    }
}

static bool ApplyProfile(D3DKMT_HANDLE hAdapter, ULONG freq, ULONG mv, ULONG perfProfile, ULONG maxTemp, ULONG flags, const char* name)
{
    BC250_WDDM_ESCAPE_POWER_PROFILE prof = { 0 };
    prof.Header.CommandId = BC250_ESCAPE_SET_POWER_PROFILE;
    prof.Header.InSize = sizeof(prof);
    prof.Header.OutSize = sizeof(prof);
    prof.TargetFreqMhz = freq;
    prof.TargetVoltageMv = mv;
    prof.PerfProfile = perfProfile;
    prof.MaxTemperatureC = maxTemp;
    prof.Flags = flags;

    printf("[+] Aplicando perfil '%s' (%u MHz @ %u mV, IF Prof=%u, Teto=%u C)...\n", name, freq, mv, perfProfile, maxTemp);

    if (!CallBc250Escape(hAdapter, &prof, sizeof(prof))) {
        printf("[-] ERRO: Chamada de escape falhou (Codigo de status ou acesso negado).\n");
        return false;
    }

    if (prof.ResultStatus != 0) {
        printf("[-] ERRO: Parametros rejeitados pelo driver (Status: 0x%08X).\n", prof.ResultStatus);
        return false;
    }

    printf("[+] Sucesso! Resposta do Silicio:\n");
    printf("    Frequencia Confirmada : %u MHz\n", prof.CurrentFreqMhz);
    printf("    Tensao Confirmada     : %u mV (VID: %u)\n\n", prof.CurrentVoltageMv, prof.CurrentVid);
    return true;
}

int main(int argc, char* argv[])
{
    PrintHeader();

    D3DKMT_HANDLE hAdapter = OpenBc250Adapter();
    if (!hAdapter) {
        printf("[-] ERRO: Nao foi possivel abrir o adaptador WDDM via D3DKMT.\n");
        printf("[-] Verifique se o driver bc250wddm.sys esta instalado e ativo.\n");
        return 1;
    }
    printf("[+] Adaptador WDDM conectado (Handle: 0x%08X)\n\n", hAdapter);

    ShowLiveTelemetry(hAdapter);

    /*
      Modo somente-leitura para a suite automatizada.

      Este utilitario e' interativo: sem argumento ele imprime um menu e espera
      scanf. Rodando dentro de run_suite.cmd isso travava a suite e, quando
      respondido, APLICAVA um overclock (1850 MHz @ 900 mV) no meio da bateria —
      alterando o estado do silicio para os testes seguintes e usando o caminho
      de forcar frequencia sem tabelas DPM, que a referencia marca como
      perigoso. Nenhuma das duas coisas pode acontecer sem alguem pedir.
    */
    if (argc > 1 && (_stricmp(argv[1], "--readonly") == 0 || _stricmp(argv[1], "-r") == 0)) {
        printf("\n[modo somente-leitura] Nenhum perfil aplicado.\n");
        printf("Para alterar frequencia/tensao rode este utilitario sem --readonly.\n");
        return 0;
    }

    if (argc > 1) {
        if (_stricmp(argv[1], "--eco") == 0 || _stricmp(argv[1], "-1") == 0) {
            ApplyProfile(hAdapter, 400, 700, 1, 80, 0, "Eco / Idle (400 MHz @ 700 mV ~10W)");
        } else if (_stricmp(argv[1], "--stock") == 0 || _stricmp(argv[1], "-2") == 0) {
            ApplyProfile(hAdapter, 1600, 800, 3, 85, 0, "Stock Seguro (1600 MHz @ 800 mV ~35W)");
        } else if (_stricmp(argv[1], "--boost") == 0 || _stricmp(argv[1], "-3") == 0) {
            ApplyProfile(hAdapter, 1850, 900, 3, 85, 0, "Balanced Boost (1850 MHz @ 900 mV ~52W)");
        } else if (_stricmp(argv[1], "--sweetspot") == 0 || _stricmp(argv[1], "-4") == 0) {
            ApplyProfile(hAdapter, 2000, 950, 3, 85, 0, "Sweet Spot Gaming (2000 MHz @ 950 mV ~65W)");
        } else if (_stricmp(argv[1], "--high") == 0 || _stricmp(argv[1], "-5") == 0) {
            ApplyProfile(hAdapter, 2100, 1000, 3, 85, 0, "High Performance (2100 MHz @ 1000 mV ~72W)");
        } else if (_stricmp(argv[1], "--extreme") == 0 || _stricmp(argv[1], "-6") == 0) {
            ApplyProfile(hAdapter, 2200, 1045, 3, 85, 0, "Extreme Overclock (2200 MHz @ 1045 mV ~80W)");
        } else if (_stricmp(argv[1], "--unforce") == 0 || _stricmp(argv[1], "-7") == 0) {
            ApplyProfile(hAdapter, 0, 0, 3, 85, 1, "Restaurar Padrao SMU (Unforce Auto DPM)");
        } else if (_stricmp(argv[1], "--custom") == 0 && argc >= 4) {
            ULONG freq = (ULONG)atoi(argv[2]);
            ULONG mv = (ULONG)atoi(argv[3]);
            ApplyProfile(hAdapter, freq, mv, 3, 85, 0, "Custom");
        } else {
            printf("Uso: %s [--eco | --stock | --boost | --sweetspot | --high | --extreme | --unforce | --custom <freq> <mv>]\n", argv[0]);
        }
        ShowLiveTelemetry(hAdapter);
        CloseBc250Adapter(hAdapter);
        return 0;
    }

    printf("Selecione o perfil desejado para a AMD BC-250:\n\n");
    printf("  [1] Eco / Idle        :  400 MHz @  700 mV (~10W) - Economia maxima\n");
    printf("  [2] Stock Seguro      : 1600 MHz @  800 mV (~35W) - Frio e silencioso\n");
    printf("  [3] Balanced Boost    : 1850 MHz @  900 mV (~52W) - +23%% desempenho\n");
    printf("  [4] Sweet Spot Gaming : 2000 MHz @  950 mV (~65W) - +33%% desempenho (Recomendado)\n");
    printf("  [5] High Performance  : 2100 MHz @ 1000 mV (~72W) - +40%% desempenho\n");
    printf("  [6] Extreme Overclock : 2200 MHz @ 1045 mV (~80W) - +46%% desempenho\n");
    printf("  [7] Custom Manual     : Frequencia (400-2350 MHz) e Tensao (700-1125 mV)\n");
    printf("  [8] Restaurar Padrao  : Desbloqueia travas e retorna ao Governor padrao\n");
    printf("  [0] Sair\n\n");
    printf("Opcao: ");

    int choice = 0;
    if (scanf_s("%d", &choice) != 1) {
        CloseBc250Adapter(hAdapter);
        return 0;
    }

    switch (choice) {
    case 1:
        ApplyProfile(hAdapter, 400, 700, 1, 80, 0, "Eco / Idle (400 MHz @ 700 mV ~10W)");
        break;
    case 2:
        ApplyProfile(hAdapter, 1600, 800, 3, 85, 0, "Stock Seguro (1600 MHz @ 800 mV ~35W)");
        break;
    case 3:
        ApplyProfile(hAdapter, 1850, 900, 3, 85, 0, "Balanced Boost (1850 MHz @ 900 mV ~52W)");
        break;
    case 4:
        ApplyProfile(hAdapter, 2000, 950, 3, 85, 0, "Sweet Spot Gaming (2000 MHz @ 950 mV ~65W)");
        break;
    case 5:
        ApplyProfile(hAdapter, 2100, 1000, 3, 85, 0, "High Performance (2100 MHz @ 1000 mV ~72W)");
        break;
    case 6:
        ApplyProfile(hAdapter, 2200, 1045, 3, 85, 0, "Extreme Overclock (2200 MHz @ 1045 mV ~80W)");
        break;
    case 7: {
        ULONG freq = 0, mv = 0;
        printf("Digite a frequencia desejada em MHz (400 a 2350): ");
        scanf_s("%u", &freq);
        printf("Digite a tensao desejada em mV (700 a 1125): ");
        scanf_s("%u", &mv);
        ApplyProfile(hAdapter, freq, mv, 3, 85, 0, "Personalizado");
        break;
    }
    case 8:
        ApplyProfile(hAdapter, 0, 0, 3, 85, 1, "Restaurar Padrao SMU (Unforce)");
        break;
    default:
        printf("Nenhuma alteracao realizada.\n");
        break;
    }

    Sleep(500);
    ShowLiveTelemetry(hAdapter);
    CloseBc250Adapter(hAdapter);
    return 0;
}
