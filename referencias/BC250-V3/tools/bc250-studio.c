/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250-studio.c

Abstract:
    Real-Time CLI / TUI Control Center & Dashboard for AMD BC-250 (Cyan Skillfish).
    Features:
      - Live Ring Engine Monitor (CP PM4 Ring 0, SDMA0 Copy Ring, PSP GPCOM Ring)
      - GPU Memory Map Visualizer (16 GB GDDR6 UMA Breakdown)
      - Live Event Stream & Telemetry (GFXCLK, Voltage, TSI Temp, VSyncs, Escapes)

--*/

#include "common.h"
#include <conio.h>

static void RenderBar(char* buf, size_t bufSz, ULONG used, ULONG total, int width)
{
    if (total == 0) total = 1;
    if (used > total) used = total;
    int filled = (int)(((ULONGLONG)used * width) / total);
    int i;
    int pos = 0;
    buf[pos++] = '[';
    for (i = 0; i < width; i++) {
        if (i < filled) {
            buf[pos++] = '#';
        } else {
            buf[pos++] = '.';
        }
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
}

static void ClearScreen(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD coord = { 0, 0 };
    DWORD count;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    FillConsoleOutputCharacterA(hOut, ' ', csbi.dwSize.X * csbi.dwSize.Y, coord, &count);
    SetConsoleCursorPosition(hOut, coord);
}

int main(int argc, char* argv[])
{
    D3DKMT_HANDLE hAdapter = OpenBc250Adapter();
    if (!hAdapter) {
        printf("[-] ERRO: Adaptador AMD BC-250 (WDDM 2.0+) nao encontrado ou driver nao carregado.\n");
        return 1;
    }

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hConsole, &mode);
    SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    printf("Iniciando BC250 Studio Real-Time Control Center...\n");

    int refreshMs = 250; /* 4 Hz polling */
    if (argc >= 2) {
        int r = atoi(argv[1]);
        if (r >= 50 && r <= 5000) refreshMs = r;
    }

    while (1) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) {
                break;
            }
        }

        BC250_WDDM_ESCAPE_RING_STATUS rings = { 0 };
        rings.Header.CommandId = BC250_ESCAPE_QUERY_RING_STATUS;
        rings.Header.InSize = sizeof(rings);
        rings.Header.OutSize = sizeof(rings);
        CallBc250Escape(hAdapter, &rings, sizeof(rings));

        BC250_WDDM_ESCAPE_MEMORY_MAP map = { 0 };
        map.Header.CommandId = BC250_ESCAPE_QUERY_MEMORY_MAP;
        map.Header.InSize = sizeof(map);
        map.Header.OutSize = sizeof(map);
        CallBc250Escape(hAdapter, &map, sizeof(map));

        BC250_WDDM_ESCAPE_EVENT_LOG events = { 0 };
        events.Header.CommandId = BC250_ESCAPE_QUERY_EVENT_LOG;
        events.Header.InSize = sizeof(events);
        events.Header.OutSize = sizeof(events);
        CallBc250Escape(hAdapter, &events, sizeof(events));

        BC250_WDDM_ESCAPE_TELEMETRY telem = { 0 };
        telem.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
        telem.Header.InSize = sizeof(telem);
        telem.Header.OutSize = sizeof(telem);
        CallBc250Escape(hAdapter, &telem, sizeof(telem));

        ClearScreen();

        /* Top Header */
        printf("========================================================================================================\n");
        printf("  AMD BC-250 CONTROL CENTER & STUDIO (CYAN SKILLFISH / OBERON 16GB GDDR6)  [Pressione 'Q' para sair]  \n");
        printf("========================================================================================================\n\n");

        /* Panel 1: Telemetry & Hardware State */
        printf("+--- [ TELEMETRIA SILICIO EM TEMPO REAL ] --------------------------------------------------------------+\n");
        printf("| GPU Core Temp: %3u C (TSI: %3u C) | VDDGFX: %4u mV | GFXCLK: %4u MHz | PPT Power: %3u W | VSyncs: %-7u |\n",
               telem.GfxTemperatureC, telem.GfxTemperatureC, telem.GfxVoltageMv, telem.GfxClockMhz, telem.PowerWatts, events.VsyncCount);
        printf("| SoC Temp     : %3u C              | SoC VDD: %4u mV | SOCCLK: %4u MHz | Mem CLK  : %4u MHz | Frames: %-7u |\n",
               telem.SocTemperatureC, telem.SocVoltageMv, telem.SocClockMhz, telem.MemClockMhz, events.PresentCount);
        printf("| Origem Dados : %-32s | GRBM: 0x%08X (GUI=%-4s, SPI=%-4s)             |\n",
               (telem.Flags == 1) ? "Tabela SMU DMA (6)" : "Sonda Direta SMN (Diodo TSI)",
               rings.GrbmStatus,
               (rings.GrbmStatus & 0x80000000) ? "BUSY" : "IDLE",
               (rings.GrbmStatus & 0x00400000) ? "BUSY" : "IDLE");
        printf("+-------------------------------------------------------------------------------------------------------+\n\n");

        /* Panel 2: Live Ring Engine Monitor */
        char barCp[64], barSdma[64], barPsp[64];
        RenderBar(barCp, sizeof(barCp), rings.CpRingOccupancyBytes, rings.CpRingSizeBytes, 20);
        RenderBar(barSdma, sizeof(barSdma), rings.SdmaRingOccupancyBytes, rings.SdmaRingSizeBytes, 20);
        RenderBar(barPsp, sizeof(barPsp), (rings.PspRingWptr * 64) % (rings.PspRingSizeBytes ? rings.PspRingSizeBytes : 4096), rings.PspRingSizeBytes ? rings.PspRingSizeBytes : 4096, 20);

        printf("+--- [ LIVE RING ENGINE MONITOR ] ----------------------------------------------------------------------+\n");
        printf("| ENGINE          | OCUPACAO            | BASE GPU           | WPTR       | RPTR       | SUBMITS / STAT     |\n");
        printf("+-----------------+---------------------+--------------------+------------+------------+--------------------+\n");
        printf("| CP PM4 Ring 0   | %s %4u B | 0x%016llX | 0x%08X | 0x%08X | Submits: %-9u |\n",
               barCp, rings.CpRingOccupancyBytes, rings.CpRingGpuBase, rings.CpRingWptr, rings.CpRingRptr, rings.CpRingSubmitCount);
        printf("| SDMA0 Copy Ring | %s %4u B | 0x%016llX | 0x%08X | 0x%08X | Copias : %-9u |\n",
               barSdma, rings.SdmaRingOccupancyBytes, rings.SdmaRingGpuBase, rings.SdmaRingWptr, rings.SdmaRingRptr, rings.SdmaCopyCount);
        printf("| PSP GPCOM Ring  | %s %4u B | 0x%016llX | 0x%08X | Fence: #%-2u | Comandos: %-8u |\n",
               barPsp, (rings.PspRingWptr * 64) % 4096, rings.PspRingGpuBase, rings.PspRingWptr, rings.PspFenceValue, rings.PspCmdCount);
        printf("+-------------------------------------------------------------------------------------------------------+\n\n");

        /* Panel 3: GPU Memory Map Visualizer */
        printf("+--- [ GPU MEMORY MAP VISUALIZER (16 GB GDDR6 UMA ALLOCATION) ] ----------------------------------------+\n");
        printf("| [0x00000000] GOP FB (Scanout) : %4u x %4u @ %u Bpp (~%u MB)                                        |\n",
               map.ScreenWidth, map.ScreenHeight, (map.ScreenPitch / (map.ScreenWidth ? map.ScreenWidth : 1)), map.GopFbSizeBytes / (1024 * 1024));
        printf("| [0x01000000] Staging Compute  : %u MB (Buffers de Entrada/Saida Shaders & GPGPU)                     |\n",
               map.StagingComputeBytes / (1024 * 1024));
        printf("| [0x0F800000] TMR Carveout     : %u MB (Memoria Segura do PSP / Microcodes Autenticados)              |\n",
               map.TmrSizeBytes / (1024 * 1024));
        printf("| [0x0FD00000] SDMA0 Ring Buffer: %u KB (Anel Circular DMA de Alta Velocidade)                         |\n",
               map.SdmaRingSizeBytes / 1024);
        printf("| [0x0FE00000] CP PM4 Ring 0    : %u KB (Anel Principal do Command Processor 3D/Compute)              |\n",
               map.CpRingSizeBytes / 1024);
        printf("| [0x10000000] Dynamic GDDR6    : %.2f GB Livres (Total Fisico: 16.00 GB GDDR6)                          |\n",
               (double)map.DynamicPoolBytes / (1024.0 * 1024.0 * 1024.0));
        printf("+-------------------------------------------------------------------------------------------------------+\n\n");

        /* Panel 4: Live Event Stream */
        printf("+--- [ LIVE EVENT STREAM (ULTIMAS CHAMADAS DE KERNEL) ] ------------------------------------------------+\n");
        printf("| ID  | TIPO EVENTO         | PARAMETRO 1        | PARAMETRO 2        | TIMESTAMP QPC                  |\n");
        printf("+-----+---------------------+--------------------+--------------------+--------------------------------+\n");

        int shown = 0;
        for (int e = (int)events.EntryCount - 1; e >= 0 && shown < 4; e--, shown++) {
            BC250_EVENT_ENTRY* ent = &events.Entries[e];
            char evtName[32];
            if (ent->EventId == 0x534D4E57u) strcpy(evtName, "SMN_WRITE");
            else if (ent->EventId == 0x534D5501u) strcpy(evtName, "SMU_MSG");
            else if (ent->EventId == 0x50535001u) strcpy(evtName, "PSP_CMD");
            else sprintf(evtName, "0x%08X", ent->EventId);

            printf("| #%02d | %-19s | 0x%016llX | 0x%016llX | %-30lld |\n",
                   shown + 1, evtName, (ULONGLONG)ent->Param1, (ULONGLONG)ent->Param2, ent->Timestamp.QuadPart);
        }
        if (shown == 0) {
            printf("|     | (Aguardando primeiras chamadas de hardware/escape...)                                      |\n");
        }
        printf("+-------------------------------------------------------------------------------------------------------+\n");

        Sleep(refreshMs);
    }

    return 0;
}
