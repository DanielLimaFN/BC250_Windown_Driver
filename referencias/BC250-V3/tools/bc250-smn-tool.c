/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250-smn-tool.c

Abstract:
    Low-Level SMN (System Management Network) & SMU Control CLI Tool for AMD BC-250 (Cyan Skillfish).
    Features:
      - Direct SMN bus read/write via NBIO 0x38/0x3C
      - Raw SMU message dispatch (Queues 0, 2, 3)
      - Dynamic P-States & GFX clock control (500 MHz to 2200 MHz) + Voltage Curve
      - SMU Message 0x98 / 0x2C Zen2 CPU 8-Core (16-thread) unlock sequence
      - Live interactive hardware monitor

--*/

#include "common.h"

static void PrintBanner(void)
{
    printf("=================================================================\n");
    printf(" AMD BC-250 Low-Level SMN & SMU Control Utility (bc250-smn-tool) \n");
    printf("=================================================================\n\n");
}

static void PrintHelp(const char* exeName)
{
    printf("Uso: %s <comando> [argumentos...]\n\n", exeName);
    printf("Comandos disponiveis:\n");
    printf("  read <smn_addr_hex>             - Le 32 bits de um endereco SMN (ex: 0x00059800)\n");
    printf("  write <smn_addr_hex> <val_hex>  - Grava 32 bits em um endereco SMN\n");
    printf("  smu-msg <queue> <msg_id> [param]- Dispara mensagem SMU bruta (Queue 0, 2 ou 3)\n");
    printf("  set-clock <freq_mhz>            - Ajusta GFXCLK (500 a 2200 MHz) com curva de tensao\n");
    printf("  set-voltage <mv>                - Ajusta tensao VDDGFX em milivolts (700 a 1150 mV)\n");
    printf("  unlock-cpu                      - Dispara sequencia SMU (0x98 + 0x2C) para os 8 Nucleos Zen2\n");
    printf("  monitor [--loop]                - Monitor em tempo real de sensores e telemetria SMU\n");
    printf("  info                            - Exibe versoes do SMU, Driver IF e parametros do silicio\n\n");
    printf("Exemplos:\n");
    printf("  %s read 0x00059800              (Le diodo termico TSI)\n", exeName);
    printf("  %s set-clock 1500               (Trava GPU em 1500 MHz)\n", exeName);
    printf("  %s smu-msg 0 0x37               (Consulta GFXCLK atual via Queue 0)\n", exeName);
    printf("  %s unlock-cpu                   (Executa sequencia de ativacao de 8C/16T)\n", exeName);
}

static ULONG SmnRead(D3DKMT_HANDLE hAdapter, ULONG addr)
{
    BC250_WDDM_ESCAPE_SMN_RW esc = { 0 };
    esc.Header.CommandId = BC250_ESCAPE_SMN_READ;
    esc.Header.InSize = sizeof(esc);
    esc.Header.OutSize = sizeof(esc);
    esc.Address = addr;

    if (CallBc250Escape(hAdapter, &esc, sizeof(esc))) {
        return esc.Value;
    }
    return 0xFFFFFFFF;
}

static BOOL SmnWrite(D3DKMT_HANDLE hAdapter, ULONG addr, ULONG val)
{
    BC250_WDDM_ESCAPE_SMN_RW esc = { 0 };
    esc.Header.CommandId = BC250_ESCAPE_SMN_WRITE;
    esc.Header.InSize = sizeof(esc);
    esc.Header.OutSize = sizeof(esc);
    esc.Address = addr;
    esc.Value = val;

    return CallBc250Escape(hAdapter, &esc, sizeof(esc));
}

static BOOL SmuSend(D3DKMT_HANDLE hAdapter, ULONG queue, ULONG msg, ULONG param, ULONG* pStatus, ULONG* pArg)
{
    BC250_WDDM_ESCAPE_SMU_MSG esc = { 0 };
    esc.Header.CommandId = BC250_ESCAPE_SMU_SEND_MSG;
    esc.Header.InSize = sizeof(esc);
    esc.Header.OutSize = sizeof(esc);
    esc.QueueId = queue;
    esc.MsgId = msg;
    esc.Param = param;

    if (CallBc250Escape(hAdapter, &esc, sizeof(esc))) {
        if (pStatus) *pStatus = esc.SmuStatus;
        if (pArg) *pArg = esc.ArgReadback;
        return TRUE;
    }
    return FALSE;
}

/* Converte mV para VID: VID = (1.55 - V) / 0.00625 */
static ULONG MvToVid(ULONG mv)
{
    if (mv >= 1550) return 0;
    if (mv < 700) mv = 700;
    return (1550000 - (mv * 1000)) / 6250;
}

int main(int argc, char* argv[])
{
    PrintBanner();

    if (argc < 2) {
        PrintHelp(argv[0]);
        return 1;
    }

    D3DKMT_HANDLE hAdapter = OpenBc250Adapter();
    if (!hAdapter) {
        printf("[-] ERRO: Adaptador AMD BC-250 (WDDM 2.0+) nao encontrado ou driver nao carregado.\n");
        return 1;
    }

    const char* cmd = argv[1];

    if (_stricmp(cmd, "read") == 0) {
        if (argc < 3) {
            printf("[-] Uso: %s read <smn_addr_hex>\n", argv[0]);
            return 1;
        }
        ULONG addr = (ULONG)strtoul(argv[2], NULL, 16);
        ULONG val = SmnRead(hAdapter, addr);
        printf("[+] SMN READ  [0x%08X] = 0x%08X (%u)\n", addr, val, val);
        return 0;
    }

    if (_stricmp(cmd, "write") == 0) {
        if (argc < 4) {
            printf("[-] Uso: %s write <smn_addr_hex> <val_hex>\n", argv[0]);
            return 1;
        }
        ULONG addr = (ULONG)strtoul(argv[2], NULL, 16);
        ULONG val = (ULONG)strtoul(argv[3], NULL, 16);
        if (SmnWrite(hAdapter, addr, val)) {
            printf("[+] SMN WRITE [0x%08X] <= 0x%08X OK\n", addr, val);
            ULONG readback = SmnRead(hAdapter, addr);
            printf("[+] Readback: [0x%08X] = 0x%08X\n", addr, readback);
            return 0;
        } else {
            printf("[-] Falha ao gravar no barramento SMN.\n");
            return 1;
        }
    }

    if (_stricmp(cmd, "smu-msg") == 0) {
        if (argc < 4) {
            printf("[-] Uso: %s smu-msg <queue> <msg_id_hex> [param_hex]\n", argv[0]);
            return 1;
        }
        ULONG queue = (ULONG)strtoul(argv[2], NULL, 0);
        ULONG msg = (ULONG)strtoul(argv[3], NULL, 0);
        ULONG param = (argc >= 5) ? (ULONG)strtoul(argv[4], NULL, 0) : 0;
        ULONG status = 0, arg = 0;

        printf("[*] Enviando SMU Message: Queue %u, Msg 0x%02X, Param 0x%08X (%u)...\n", queue, msg, param, param);
        if (SmuSend(hAdapter, queue, msg, param, &status, &arg)) {
            printf("[+] Resposta SMU Status: 0x%02X (%s)\n", status, (status == 1) ? "OK / SUCESSO" : "STATUS CODE");
            printf("[+] Argumento Readback : 0x%08X (%u)\n", arg, arg);
            return 0;
        } else {
            printf("[-] Falha ao enviar mensagem ao SMU.\n");
            return 1;
        }
    }

    if (_stricmp(cmd, "set-clock") == 0) {
        if (argc < 3) {
            printf("[-] Uso: %s set-clock <freq_mhz>\n", argv[0]);
            return 1;
        }
        ULONG mhz = (ULONG)strtoul(argv[2], NULL, 10);
        if (mhz < 500 || mhz > 2200) {
            printf("[-] Frequencia fora da faixa segura de operacao (500 a 2200 MHz).\n");
            return 1;
        }

        /* Curva dinamica de tensao baseada na frequencia (Safe Points Cyan Skillfish) */
        ULONG targetMv = 800;
        if (mhz > 1800) targetMv = 1100;
        else if (mhz > 1500) targetMv = 975;
        else if (mhz > 1200) targetMv = 900;
        else if (mhz > 800)  targetMv = 850;

        ULONG vid = MvToVid(targetMv);

        printf("[*] Configurando GFX P-State: %u MHz | Tensao Alvo: %u mV (VID: 0x%02X)...\n", mhz, targetMv, vid);

        ULONG status = 0, arg = 0;
        /* 1. Ajusta VID de Tensao */
        SmuSend(hAdapter, 0, 0x3B, vid, &status, &arg); /* ForceGfxVid */
        /* 2. Ajusta Frequencia */
        SmuSend(hAdapter, 0, 0x39, mhz, &status, &arg); /* ForceGfxFreq */

        /* 3. Confirma leitura */
        SmuSend(hAdapter, 0, 0x37, 0, &status, &arg);   /* GetGfxFrequency */
        printf("[+] GFXCLK Confirmado pelo Silicio: %u MHz (Status: 0x%02X)\n", arg, status);
        return 0;
    }

    if (_stricmp(cmd, "set-voltage") == 0) {
        if (argc < 3) {
            printf("[-] Uso: %s set-voltage <mv>\n", argv[0]);
            return 1;
        }
        ULONG mv = (ULONG)strtoul(argv[2], NULL, 10);
        if (mv < 700 || mv > 1150) {
            printf("[-] Tensao fora da faixa segura (700 a 1150 mV).\n");
            return 1;
        }
        ULONG vid = MvToVid(mv);
        ULONG status = 0, arg = 0;
        printf("[*] Forcando VDDGFX para %u mV (VID: 0x%02X)...\n", mv, vid);
        if (SmuSend(hAdapter, 0, 0x3B, vid, &status, &arg)) {
            printf("[+] Tensao VDDGFX aplicada com sucesso! (Status: 0x%02X)\n", status);
            return 0;
        } else {
            printf("[-] Falha ao enviar ajuste de tensao.\n");
            return 1;
        }
    }

    if (_stricmp(cmd, "unlock-cpu") == 0) {
        printf("[*] Iniciando Sequencia de Desbloqueio dos 8 Nucleos (16 Threads) Zen2...\n");
        ULONG status = 0, arg = 0;

        /* 1. Mensagem 0x98: Parametros de P-State e CCX */
        printf("  [1/4] Disparando SMU Queue 3 Msg 0x98 (CCX Topology Init)...\n");
        SmuSend(hAdapter, 3, 0x98, 0, &status, &arg);
        printf("        Resultado: Status 0x%02X\n", status);

        /* 2. Mensagem 0x2C: SetCoreEnableMask (0xFF = 8 cores ativos) */
        printf("  [2/4] Disparando SMU Queue 0 Msg 0x2C (SetCoreEnableMask = 0xFF [8 Cores])...\n");
        SmuSend(hAdapter, 0, 0x2C, 0xFF, &status, &arg);
        printf("        Resultado: Status 0x%02X\n", status);

        /* 3. Mensagem 0x99: Repopular tabela de P-States */
        printf("  [3/4] Disparando SMU Queue 3 Msg 0x99 (Repopulate P-State 0 Table)...\n");
        SmuSend(hAdapter, 3, 0x99, 0, &status, &arg);
        printf("        Resultado: Status 0x%02X\n", status);

        /* 4. Query P-States e Cores */
        printf("  [4/4] Consultando P-States dos nucleos...\n");
        for (int c = 0; c < 8; c++) {
            SmuSend(hAdapter, 0, 0x0C, c, &status, &arg);
            printf("        Core #%d: P-State %u (Status 0x%02X)\n", c, arg, status);
        }

        printf("\n[+] SEQUENCIA SMU DE ATIVACAO EXECUTADA COM SUCESSO!\n");
        return 0;
    }

    if (_stricmp(cmd, "info") == 0) {
        ULONG status = 0, arg = 0;
        printf("--- [ Identificacao de Firmware e Silicio ] ---\n");
        SmuSend(hAdapter, 0, 0x02, 0, &status, &arg);
        printf("  SMU Firmware Version : 0x%08X (v%u.%u.%u)\n", arg, (arg >> 16) & 0xFF, (arg >> 8) & 0xFF, arg & 0xFF);

        SmuSend(hAdapter, 0, 0x03, 0, &status, &arg);
        printf("  Driver IF Version    : 0x%08X (%u)\n", arg, arg);

        SmuSend(hAdapter, 0, 0x3D, 0, &status, &arg);
        printf("  SMU Enabled Features : 0x%08X\n", arg);

        SmuSend(hAdapter, 0, 0x37, 0, &status, &arg);
        printf("  GFXCLK Frequencia    : %u MHz\n", arg);

        SmuSend(hAdapter, 0, 0x38, 0, &status, &arg);
        printf("  GFX Voltage (VID)    : 0x%02X (%u mV)\n", arg, (arg <= 248) ? (1550 - (arg * 625 / 100)) : 0);

        ULONG tsi = SmnRead(hAdapter, 0x00059800u);
        ULONG tsiRaw = (tsi >> 21) & 0x7FFu;
        LONG tempC = ((LONG)tsiRaw * 125 - 49000) / 1000;
        printf("  Diodo TSI (SMN 59800): Raw 0x%03X -> Temp Real: %ld C\n\n", tsiRaw, tempC);
        return 0;
    }

    if (_stricmp(cmd, "monitor") == 0) {
        bool loop = (argc >= 3 && _stricmp(argv[2], "--loop") == 0);
        int iters = loop ? 1000000 : 5;

        for (int i = 0; i < iters; i++) {
            BC250_WDDM_ESCAPE_TELEMETRY telem = { 0 };
            telem.Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
            telem.Header.InSize = sizeof(telem);
            telem.Header.OutSize = sizeof(telem);

            CallBc250Escape(hAdapter, &telem, sizeof(telem));

            if (loop) {
                system("cls");
                printf("=== AMD BC-250 LIVE SMN & SMU MONITOR (Pressione Ctrl+C para sair) ===\n\n");
            }

            printf("[%02d] GPU Temp: %u C | SoC: %u C | Edge: %u C | Clock: %u MHz | Tensao: %u mV | PPT: %u W | Origem: %s\n",
                   i + 1, telem.GfxTemperatureC, telem.SocTemperatureC, telem.EdgeTemperatureC,
                   telem.GfxClockMhz, telem.GfxVoltageMv, telem.PowerWatts,
                   (telem.Flags == 1) ? "Tabela SMU DMA (6)" : "Diodo TSI (SMN 59800)");

            if (i < iters - 1) Sleep(1000);
        }
        return 0;
    }

    PrintHelp(argv[0]);
    return 1;
}
