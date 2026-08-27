/* bc250studio.c — BC-250 Studio v0 (GUI Win32).
 *
 * Painel de controle e log para a AMD BC-250 no Windows, falando com o
 * bc250dbg.sys. Roadmap secao 7.5.
 *
 * Restricoes de projeto, e o porque de cada uma:
 *
 *  - Win32 + GDI puro, sem D3D e sem GDI+. A maquina alvo nao tem aceleracao
 *    3D: a GPU sob teste E o experimento. Uma GUI que precise dela nao serve.
 *  - Sem .NET e sem VC redist (build com /MT). O alvo e maquina de teste crua.
 *  - Nenhuma dependencia de o driver estar carregado: abre, mostra o estado
 *    "sem driver" e oferece reconectar. E comum abrir antes de instalar.
 *  - O painel de log fica visivel em TODAS as secoes. O log e o produto.
 *
 * Limitacao conhecida do v0: tudo roda na propria maquina alvo. Se ela travar,
 * o que estiver em buffer se perde — o autosave grava com flush a cada lote
 * para reduzir a perda, mas o caminho de verdade e o agente TCP do v1, que
 * manda o log para a maquina host.
 *
 * SPDX-License-Identifier: MIT
 */

#define _CRT_SECURE_NO_WARNINGS 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../lib/bc250lib.h"

/* Controles com visual moderno sem precisar de arquivo .manifest externo. */
#pragma comment(linker, "/manifestdependency:\"type='win32' "                  \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "              \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' "             \
    "language='*'\"")

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

#define STATUS_H     92
#define SIDEBAR_W    168
#define LOGBAR_H     30
#define LOG_H        250
#define PAD          10

#define SEC_DASH     0
#define SEC_REGS     1
#define SEC_EXP      2
#define SEC_THERM    3
#define SEC_COUNT    4

/* ids de controle */
#define IDC_SIDE        1000
#define IDC_LOG         1001
#define IDC_LOG_TRACE   1002
#define IDC_LOG_PAUSE   1003
#define IDC_LOG_CLEAR   1004
#define IDC_LOG_SAVE    1005
#define IDC_LOG_AUTOSV  1006

#define IDC_D_TEXT      1100
#define IDC_D_REFRESH   1101
#define IDC_D_SCAN      1102
#define IDC_D_RECONN    1103

#define IDC_R_LIST      1200
#define IDC_R_REFRESH   1201
#define IDC_R_AUTO      1202
#define IDC_R_NAME      1203
#define IDC_R_VALUE     1204
#define IDC_R_WRITE     1205

#define IDC_E_LIST      1300
#define IDC_E_RUN       1301
#define IDC_E_RESULT    1302

#define IDC_T_TEXT      1400
#define IDC_T_SRC       1401
#define IDC_T_NUM       1402
#define IDC_T_DEN       1403
#define IDC_T_OFF       1404
#define IDC_T_LIMIT     1405
#define IDC_T_APPLY     1406
#define IDC_T_ARM_RO    1407
#define IDC_T_ARM_W     1408
#define IDC_T_ARM_UNSAFE 1409

#define TIMER_LOG       1
#define TIMER_TELEM     2

#define WM_APP_TASKDONE (WM_APP + 1)

#define TASK_NONE       0
#define TASK_SCRIPT     1
#define TASK_SCAN       2

#define LOG_MAX_ITEMS   5000

/* ------------------------------------------------------------------ */
/* Estado                                                              */
/* ------------------------------------------------------------------ */

static BC250_CTX  g_Ctx;
static int        g_Connected = 0;
static HINSTANCE  g_Inst;
static HWND       g_Main, g_Side, g_Log;
static HWND       g_LogTrace, g_LogPause, g_LogAutoSave;
static HFONT      g_FontUi, g_FontMono, g_FontBig;
static int        g_Section = SEC_DASH;
static HWND       g_Sec[SEC_COUNT][24];   /* controles por secao */
static int        g_SecN[SEC_COUNT];

static BC250DBG_INFO    g_Info;
static BC250DBG_THERMAL g_Therm;
static int              g_InfoOk;

static FILE      *g_AutoSave;
static HANDLE     g_Worker;
static volatile LONG g_Task = TASK_NONE;
static char       g_TaskArg[MAX_PATH];
static char       g_TaskMsg[512];

/* ------------------------------------------------------------------ */
/* Log pane                                                            */
/* ------------------------------------------------------------------ */

static void LogAdd(const char *text, uint32_t level)
{
    int idx;

    if (!g_Log) {
        return;
    }
    idx = (int)SendMessageA(g_Log, LB_ADDSTRING, 0, (LPARAM)text);
    if (idx >= 0) {
        /* o nivel viaja no item data e vira cor no owner-draw */
        SendMessageA(g_Log, LB_SETITEMDATA, idx, (LPARAM)level);
        if (idx >= LOG_MAX_ITEMS) {
            SendMessageA(g_Log, LB_DELETESTRING, 0, 0);
            idx--;
        }
        if (!(g_LogPause && SendMessageA(g_LogPause, BM_GETCHECK, 0, 0) == BST_CHECKED)) {
            SendMessageA(g_Log, LB_SETTOPINDEX, idx, 0);
        }
    }

    if (g_AutoSave) {
        fprintf(g_AutoSave, "%s\n", text);
        fflush(g_AutoSave);   /* o ponto do autosave e sobreviver ao travamento */
    }
}

/* Mensagens da propria GUI entram no mesmo painel, marcadas com '#'. */
static void LogUi(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    LogAdd(buf, 0);
}

static void LogDrainToPane(void)
{
    static BC250DBG_LOG_ENTRY batch[256];
    char line[320];
    int n, i;

    if (!g_Connected || !g_Ctx.LogHeader) {
        return;
    }
    n = Bc250LogDrain(&g_Ctx, batch, (int)(sizeof(batch) / sizeof(batch[0])));
    for (i = 0; i < n; i++) {
        Bc250LogFormat(&g_Ctx, &batch[i], line, sizeof(line));
        LogAdd(line, batch[i].Level);
    }
}

static void LogSaveAs(void)
{
    OPENFILENAMEA ofn;
    char path[MAX_PATH] = "bc250-sessao.log";
    FILE *f = NULL;
    int i, count;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_Main;
    ofn.lpstrFilter = "Log (*.log)\0*.log\0Todos\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&ofn)) {
        return;
    }
    if (fopen_s(&f, path, "w") != 0 || !f) {
        LogUi("# nao consegui escrever %s", path);
        return;
    }
    count = (int)SendMessageA(g_Log, LB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++) {
        char buf[512];
        int len = (int)SendMessageA(g_Log, LB_GETTEXTLEN, i, 0);
        if (len > 0 && len < (int)sizeof(buf)) {
            SendMessageA(g_Log, LB_GETTEXT, i, (LPARAM)buf);
            fprintf(f, "%s\n", buf);
        }
    }
    fclose(f);
    LogUi("# %d linhas gravadas em %s", count, path);
}

/* ------------------------------------------------------------------ */
/* Conexao com o driver                                                */
/* ------------------------------------------------------------------ */

static void Connect(void)
{
    int rc;

    if (g_Connected) {
        Bc250Close(&g_Ctx);
        g_Connected = 0;
    }
    rc = Bc250Open(&g_Ctx);
    if (rc != 0) {
        LogUi("# driver nao encontrado (erro %d). Rode dbg\\install.bat como admin.", rc);
        g_Connected = 0;
        g_InfoOk = 0;
        return;
    }
    g_Connected = 1;
    LogUi("# conectado a %s", BC250DBG_USER_DEVICE_PATH);

    if (Bc250LogAttach(&g_Ctx) == 0) {
        Bc250LogSeekEnd(&g_Ctx);
        LogUi("# log ring mapeado (%u entradas)", g_Ctx.LogCount);
    } else {
        LogUi("# nao consegui mapear o log ring");
    }
}

/* ------------------------------------------------------------------ */
/* Telemetria e faixa de status                                        */
/* ------------------------------------------------------------------ */

static const char *ArmName(uint32_t lvl)
{
    switch (lvl) {
    case BC250DBG_ARM_READONLY: return "readonly";
    case BC250DBG_ARM_WRITE:    return "write";
    case BC250DBG_ARM_UNSAFE:   return "UNSAFE";
    default:                    return "?";
    }
}

/* Veredito da tarefa 0.6 (convencao de offset do BAR5).
 * 1 = convencao B, -1 = convencao A, 0 = indefinido. */
static int OffsetVerdict(void)
{
    if (!g_InfoOk) {
        return 0;
    }
    if (g_Info.CcShaderArrayB == 0xFFF80000u) {
        return 1;
    }
    if (g_Info.CcShaderArray == 0xFFF80000u) {
        return -1;
    }
    return 0;
}

static void RefreshTelemetry(void)
{
    if (!g_Connected) {
        g_InfoOk = 0;
        return;
    }
    g_InfoOk = (Bc250Info(&g_Ctx, &g_Info) == 0);
    Bc250ThermalGet(&g_Ctx, &g_Therm);
}

static void DrawStatusStrip(HDC dc, RECT *rc)
{
    HBRUSH bg = CreateSolidBrush(RGB(28, 30, 36));
    HGDIOBJ oldFont;
    char buf[128];
    int x = PAD + 4;
    int yTop = rc->top + 12, yBot = rc->top + 52;
    int i;

    struct { const char *label; char value[48]; COLORREF color; } cell[5];

    FillRect(dc, rc, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);

    if (!g_Connected) {
        SetTextColor(dc, RGB(230, 120, 110));
        oldFont = SelectObject(dc, g_FontBig);
        {
            const char *l1 = "driver nao carregado";
            const char *l2 = "instale com dbg\\install.bat (admin) e clique "
                             "em Reconectar";
            TextOutA(dc, x, yTop, l1, (int)strlen(l1));
            SelectObject(dc, g_FontUi);
            SetTextColor(dc, RGB(170, 175, 185));
            TextOutA(dc, x, yBot + 6, l2, (int)strlen(l2));
        }
        SelectObject(dc, oldFont);
        return;
    }

    strcpy_s(cell[0].value, sizeof(cell[0].value), "-");
    if (g_InfoOk) {
        sprintf_s(cell[0].value, sizeof(cell[0].value), "%u MHz", g_Info.GfxFreqMhz);
    }
    cell[0].label = "GFXCLK";
    cell[0].color = RGB(120, 200, 255);

    sprintf_s(cell[1].value, sizeof(cell[1].value), "0x%02X", g_InfoOk ? g_Info.GfxVid : 0);
    cell[1].label = "GFX VID";
    cell[1].color = RGB(200, 200, 210);

    if (g_Therm.Trusted) {
        sprintf_s(cell[2].value, sizeof(cell[2].value), "%.1f C", g_Therm.MilliC / 1000.0);
        cell[2].color = (g_Therm.MilliC > (int)g_Therm.LimitC * 1000 - 5000)
                      ? RGB(240, 140, 100) : RGB(140, 220, 150);
    } else {
        strcpy_s(cell[2].value, sizeof(cell[2].value), "nao calib.");
        cell[2].color = RGB(190, 170, 110);
    }
    cell[2].label = "TEMP";

    sprintf_s(cell[3].value, sizeof(cell[3].value), "%u", g_InfoOk ? g_Info.ActiveWgp : 0);
    cell[3].label = "WGP ATIVOS";
    cell[3].color = (g_InfoOk && g_Info.ActiveWgp == 0) ? RGB(190, 170, 110)
                                                        : RGB(140, 220, 150);

    strcpy_s(cell[4].value, sizeof(cell[4].value), ArmName(g_InfoOk ? g_Info.ArmLevel : 0));
    cell[4].label = "ARMAMENTO";
    cell[4].color = (g_InfoOk && g_Info.ArmLevel == BC250DBG_ARM_UNSAFE)
                  ? RGB(240, 120, 110) : RGB(200, 200, 210);

    for (i = 0; i < 5; i++) {
        SelectObject(dc, g_FontUi);
        SetTextColor(dc, RGB(130, 136, 150));
        TextOutA(dc, x, yTop, cell[i].label, (int)strlen(cell[i].label));
        SelectObject(dc, g_FontBig);
        SetTextColor(dc, cell[i].color);
        TextOutA(dc, x, yTop + 18, cell[i].value, (int)strlen(cell[i].value));
        x += 165;
    }

    /* linha do veredito de offset: e a pergunta aberta mais cara do projeto */
    SelectObject(dc, g_FontUi);
    if (!g_InfoOk) {
        SetTextColor(dc, RGB(190, 170, 110));
        strcpy_s(buf, sizeof(buf), "GET_INFO falhou");
    } else if (OffsetVerdict() > 0) {
        SetTextColor(dc, RGB(140, 220, 150));
        strcpy_s(buf, sizeof(buf),
                 "offset: convencao B confirmada — byte = (base_ip + reg) * 4");
    } else if (OffsetVerdict() < 0) {
        SetTextColor(dc, RGB(240, 180, 110));
        strcpy_s(buf, sizeof(buf),
                 "offset: convencao A — inesperado, contraria o amdgpu");
    } else {
        SetTextColor(dc, RGB(190, 170, 110));
        strcpy_s(buf, sizeof(buf),
                 "offset: indefinido — nenhum lado traz 0xFFF80000 (rode a varredura)");
    }
    TextOutA(dc, PAD + 4, rc->top + 66, buf, (int)strlen(buf));
}

/* ------------------------------------------------------------------ */
/* Secao: Dashboard                                                    */
/* ------------------------------------------------------------------ */

static void DashRefresh(void)
{
    char t[4096];
    int n = 0;
    int v;

    if (!g_Connected) {
        SetWindowTextA(g_Sec[SEC_DASH][0],
            "Driver nao carregado.\r\n\r\n"
            "1) dbg\\build.bat        (maquina de desenvolvimento)\r\n"
            "2) dbg\\install.bat      (na placa, como Administrador)\r\n"
            "3) Reconectar\r\n\r\n"
            "Pre-requisitos da maquina alvo:\r\n"
            "  bcdedit /set testsigning on\r\n"
            "  bcdedit /set hypervisorlaunchtype off\r\n");
        return;
    }
    if (!g_InfoOk) {
        SetWindowTextA(g_Sec[SEC_DASH][0], "GET_INFO falhou.\r\n");
        return;
    }

/* o clamp evita que sizeof(t)-n vire underflow se o texto crescer */
#define P(...) do { if (n < (int)sizeof(t) - 1) {                             \
        n += _snprintf_s(t + n, sizeof(t) - n, _TRUNCATE, __VA_ARGS__); } } while (0)

    P("driver      bc250dbg v%u.%u   ABI %u   armamento %s\r\n",
      (g_Info.DriverVersion >> 16) & 0xFFFF, (g_Info.DriverVersion >> 8) & 0xFF,
      g_Info.AbiVersion, ArmName(g_Info.ArmLevel));

    if (g_Info.PciFound) {
        P("pci         %02X:%02X.%u   %04X:%04X rev %02X\r\n",
          g_Info.PciBus, g_Info.PciDev, g_Info.PciFn,
          g_Info.VendorId, g_Info.DeviceId, g_Info.RevisionId);
    } else {
        P("pci         NAO ENCONTRADO (BAR5 pelo fallback)\r\n");
    }
    P("mmio        0x%llX  +%u KB  %s\r\n\r\n",
      (unsigned long long)g_Info.MmioPhysical, g_Info.MmioLength / 1024,
      g_Info.MmioMapped ? "mapeado" : "NAO MAPEADO");

    P("GPU_ID      0x%08X %s\r\n\r\n", g_Info.GpuId,
      g_Info.GpuId == 0x9FFF9700 ? "(confere com a BC-250)" : "(inesperado)");

    P("--- tarefa 0.6: convencao de offset do BAR5 ---------------------\r\n");
    P("                     A (base+reg)   B ((base+reg)*4)\r\n");
    P("SCRATCH_REG0         0x%08X     0x%08X\r\n",
      g_Info.ScratchReg0, g_Info.ScratchReg0B);
    P("GRBM_STATUS          0x%08X     0x%08X\r\n",
      g_Info.GrbmStatus, g_Info.GrbmStatusB);
    P("CC_GC_SHADER_ARRAY   0x%08X     0x%08X   <== desempate\r\n",
      g_Info.CcShaderArray, g_Info.CcShaderArrayB);
    v = OffsetVerdict();
    if (v > 0) {
        P("\r\n>>> CONVENCAO B. O Linux le 0xFFF80000 nesse registrador (24 de\r\n"
          "    40 CU) e o lado B bate. O mapa do projeto anterior esta\r\n"
          "    deslocado por 4x — e a conclusao de CUs fusiveladas cai junto.\r\n");
    } else if (v < 0) {
        P("\r\n>>> CONVENCAO A. Inesperado: contraria o amdgpu, que faz\r\n"
          "    writel(v, rmmio + reg*4). Reveja a analise antes de seguir.\r\n");
    } else {
        P("\r\n>>> INDEFINIDO. Nenhum dos dois traz 0xFFF80000. Use a varredura\r\n"
          "    por conteudo (botao abaixo) para achar o bloco GC sem hipotese.\r\n");
    }

    P("\r\n--- SMU --------------------------------------------------------\r\n");
    if (g_Info.SmuAlive) {
        P("versao      %u.%u.%u   driver_if %u\r\n",
          (g_Info.SmuVersion >> 16) & 0xFF, (g_Info.SmuVersion >> 8) & 0xFF,
          g_Info.SmuVersion & 0xFF, g_Info.SmuDriverIf);
        P("features    0x%08X  %s%s\r\n", g_Info.SmuFeatures,
          (g_Info.SmuFeatures & (1u << 0)) ? "GFXCLK-DPM " : "",
          (g_Info.SmuFeatures & (1u << 2)) ? "GFXOFF" : "");
        P("gfxclk      %u MHz    VID 0x%02X    WGP ativos %u\r\n",
          g_Info.GfxFreqMhz, g_Info.GfxVid, g_Info.ActiveWgp);
        if (g_Info.SmuFeatures & (1u << 2)) {
            P("\r\n    GFXOFF ligado: o bloco GFX esta em deep sleep. Registrador\r\n"
              "    de CP lido nesse estado e sombra sem clock. Tarefa 0.2.\r\n");
        }
    } else {
        P("SMU NAO RESPONDE\r\n");
    }

    P("\r\n--- termico ----------------------------------------------------\r\n");
    P("estado      %s   limite %u C%s\r\n",
      g_Therm.Trusted ? "calibrado" : "NAO CALIBRADO",
      g_Therm.LimitC, g_Therm.Tripped ? "   *** TRIP ATIVO ***" : "");
    P("sondas      edge 0x%08X   junc 0x%08X   mem 0x%08X\r\n",
      g_Therm.RawEdge, g_Therm.RawJunction, g_Therm.RawMem);
    P("ventoinha   %u rpm   pwm %u\r\n", g_Therm.FanRpm, g_Therm.FanPwm);

    P("\r\nlog ring    %u entradas, head %lld, perdas %lld\r\n",
      g_Info.LogEntryCount, (long long)g_Info.LogHead,
      (long long)g_Ctx.LogDropped);
#undef P

    SetWindowTextA(g_Sec[SEC_DASH][0], t);
}

/* ------------------------------------------------------------------ */
/* Secao: Registradores                                                */
/* ------------------------------------------------------------------ */

static void RegsFill(void)
{
    HWND lv = g_Sec[SEC_REGS][0];
    int i;

    SendMessageA(lv, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < g_Bc250RegCount; i++) {
        LVITEMA it;
        char buf[64];

        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        it.pszText = (LPSTR)g_Bc250Regs[i].Name;
        it.lParam = i;
        SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);

        it.mask = LVIF_TEXT;
        it.iSubItem = 1;
        it.pszText = (LPSTR)(g_Bc250Regs[i].Space == BC250DBG_SPACE_SMN ? "smn" : "bar");
        SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);

        sprintf_s(buf, sizeof(buf), "0x%08X", g_Bc250Regs[i].Offset);
        it.iSubItem = 2;
        it.pszText = buf;
        SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);

        it.iSubItem = 4;
        it.pszText = (LPSTR)(g_Bc250Regs[i].Note ? g_Bc250Regs[i].Note : "");
        SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);
    }
}

static int IsRegSafeToRead(int idx)
{
    uint32_t off;
    if (idx < 0 || idx >= g_Bc250RegCount) return 0;
    if (g_Bc250Regs[idx].Space == BC250DBG_SPACE_SMN) return 1;

    off = g_Bc250Regs[idx].Offset;
    if (off == 0x00000 || off == 0x00038 || off == 0x0003C) return 1;
    if (off >= 0x03000 && off <= 0x034FC) return 1;
    if (off == 0x089E0 || off == 0x0E060 || off == 0x0B460 || off == 0x0B360 || off == 0x06C8C) return 1;

    return 0;
}

static void RegsRefreshValues(void)
{
    HWND lv = g_Sec[SEC_REGS][0];
    int i;

    if (!g_Connected) {
        return;
    }
    for (i = 0; i < g_Bc250RegCount; i++) {
        uint32_t v = 0;
        char buf[64];
        LVITEMA it;
        int rc;

        if (!IsRegSafeToRead(i)) {
            strcpy_s(buf, sizeof(buf), "[GFXOFF / pendente]");
        } else if (g_Bc250Regs[i].Space == BC250DBG_SPACE_SMN) {
            rc = Bc250SmnRead(&g_Ctx, g_Bc250Regs[i].Offset, &v);
            if (rc == 0) {
                sprintf_s(buf, sizeof(buf), "0x%08X", v);
            } else {
                sprintf_s(buf, sizeof(buf), "erro %d", rc);
            }
        } else {
            rc = Bc250RegRead(&g_Ctx, g_Bc250Regs[i].Offset, &v);
            if (rc == 0) {
                sprintf_s(buf, sizeof(buf), "0x%08X", v);
            } else {
                sprintf_s(buf, sizeof(buf), "erro %d", rc);
            }
        }

        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.iSubItem = 3;
        it.pszText = buf;
        SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);
    }
}

static void RegsWrite(void)
{
    char name[64], val[64];
    uint32_t space, off, value, readback = 0;
    int rc;

    if (!g_Connected) {
        return;
    }
    GetWindowTextA(g_Sec[SEC_REGS][3], name, sizeof(name));
    GetWindowTextA(g_Sec[SEC_REGS][4], val, sizeof(val));

    if (!Bc250ResolveReg(name, &space, &off)) {
        LogUi("# registrador desconhecido: %s", name);
        return;
    }
    value = (uint32_t)strtoul(val, NULL, 0);

    rc = (space == BC250DBG_SPACE_SMN)
       ? Bc250SmnWrite(&g_Ctx, off, value, &readback)
       : Bc250RegWrite(&g_Ctx, off, value, &readback);

    if (rc == ERROR_ACCESS_DENIED) {
        LogUi("# NEGADO: driver em readonly. Va em Termico e arme 'write'.");
    } else if (rc != 0) {
        LogUi("# escrita falhou: erro %d", rc);
    } else {
        LogUi("# %s <- 0x%08X   readback 0x%08X   %s", name, value, readback,
              readback == value ? "guardou" : "*** NAO GUARDOU ***");
    }
    RegsRefreshValues();
}

/* ------------------------------------------------------------------ */
/* Secao: Experimentos                                                 */
/* ------------------------------------------------------------------ */

static char g_ExpDir[MAX_PATH];

/* Procura a pasta experiments/ a partir do executavel: aceita tanto rodar de
 * studio\output quanto da raiz do repositorio. */
static void ExpFindDir(void)
{
    char base[MAX_PATH], test[MAX_PATH];
    const char *cand[] = { "experiments", "..\\experiments",
                           "..\\..\\experiments", "..\\..\\..\\experiments" };
    size_t i;
    char *slash;

    GetModuleFileNameA(NULL, base, sizeof(base));
    slash = strrchr(base, '\\');
    if (slash) {
        *slash = '\0';
    }

    for (i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        _snprintf_s(test, sizeof(test), _TRUNCATE, "%s\\%s", base, cand[i]);
        if (GetFileAttributesA(test) != INVALID_FILE_ATTRIBUTES) {
            strcpy_s(g_ExpDir, sizeof(g_ExpDir), test);
            return;
        }
    }
    strcpy_s(g_ExpDir, sizeof(g_ExpDir), "experiments");
}

static void ExpFill(void)
{
    HWND lb = g_Sec[SEC_EXP][0];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[MAX_PATH];

    SendMessageA(lb, LB_RESETCONTENT, 0, 0);
    _snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s\\*.bcs", g_ExpDir);

    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        LogUi("# nenhum .bcs encontrado em %s", g_ExpDir);
        return;
    }
    do {
        SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* preenchidos pelo worker, consumidos na thread da UI */
static BC250_SCRIPT        g_RunScript;
static BC250DBG_SCRIPT_OUT g_RunOut;
static BC250DBG_OP_RESULT *g_RunResults;
static int                 g_RunOk;

static void ExpShowResults(void)
{
    HWND lv = g_Sec[SEC_EXP][2];
    uint32_t i;

    SendMessageA(lv, LVM_DELETEALLITEMS, 0, 0);
    if (!g_RunOk || !g_RunResults) {
        return;
    }

    for (i = 0; i < g_RunOut.OpCount; i++) {
        const BC250DBG_OP *op = &g_RunScript.Ops[i];
        LVITEMA it;
        char buf[96];

        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        sprintf_s(buf, sizeof(buf), "%u",
                  g_RunScript.Lines ? g_RunScript.Lines[i] : 0);
        it.pszText = buf;
        SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);

        it.iSubItem = 1;
        it.pszText = (LPSTR)Bc250OpName(op->Op);
        SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);

        it.iSubItem = 2;
        it.pszText = (LPSTR)Bc250OpStatusName(g_RunResults[i].Status);
        SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);

        sprintf_s(buf, sizeof(buf), "0x%012llX",
                  (unsigned long long)g_RunResults[i].Value);
        it.iSubItem = 3;
        it.pszText = buf;
        SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);

        if (op->Op == BC250DBG_OP_SMU) {
            sprintf_s(buf, sizeof(buf), "q%u msg 0x%02X -> smu 0x%02X",
                      (uint32_t)op->A, (uint32_t)op->B,
                      (uint32_t)(g_RunResults[i].Value >> 32));
        } else {
            const char *nm = Bc250RegName(
                op->Op == BC250DBG_OP_SMN_R || op->Op == BC250DBG_OP_SMN_W
                    ? BC250DBG_SPACE_SMN : BC250DBG_SPACE_BAR,
                (uint32_t)op->A);
            sprintf_s(buf, sizeof(buf), "%s0x%05X", nm ? nm : "",
                      (uint32_t)op->A);
        }
        it.iSubItem = 4;
        it.pszText = buf;
        SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);
    }
}

/* ------------------------------------------------------------------ */
/* Secao: Termico e armamento                                          */
/* ------------------------------------------------------------------ */

static void ThermRefresh(void)
{
    char t[2048];
    int n = 0;

    if (!g_Connected) {
        SetWindowTextA(g_Sec[SEC_THERM][0], "Driver nao carregado.\r\n");
        return;
    }

#define P(...) do { if (n < (int)sizeof(t) - 1) {                             \
        n += _snprintf_s(t + n, sizeof(t) - n, _TRUNCATE, __VA_ARGS__); } } while (0)
    P("estado      %s\r\n", g_Therm.Trusted ? "CALIBRADO" : "NAO CALIBRADO");
    P("temperatura %.2f C   (so vale se calibrado)\r\n", g_Therm.MilliC / 1000.0);
    P("limite      %u C%s\r\n", g_Therm.LimitC,
      g_Therm.Tripped ? "    *** TRIP ATIVO — driver desarmado ***" : "");
    P("escala      %u/%u   offset %d mC   fonte %s\r\n\r\n",
      g_Therm.ScaleNum, g_Therm.ScaleDen, g_Therm.OffsetMilliC,
      g_Therm.Source == 0 ? "edge" : g_Therm.Source == 2 ? "mem" : "junction");
    P("sondas cruas (SMN)\r\n");
    P("  edge      0x%08X\r\n", g_Therm.RawEdge);
    P("  junction  0x%08X\r\n", g_Therm.RawJunction);
    P("  mem       0x%08X\r\n", g_Therm.RawMem);
    P("  fan       %u rpm   pwm %u\r\n\r\n", g_Therm.FanRpm, g_Therm.FanPwm);
    P("A escala das sondas NAO e conhecida. Enquanto nao for declarada aqui,\r\n"
      "o driver recusa armar 'unsafe' — que e o nivel das mensagens de SMU\r\n"
      "capazes de mexer em clock e tensao. Procedimento completo em\r\n"
      "docs\\CALIBRACAO-TERMICA.md.\r\n\r\n"
      "Ponto de referencia ja coletado no Linux (18/08): 63 C com SCLK 500 MHz\r\n"
      "em repouso, VDDGFX 699 mV, 34,7 W.\r\n");
#undef P

    SetWindowTextA(g_Sec[SEC_THERM][0], t);
}

static void ThermApply(void)
{
    BC250DBG_THERMAL_SET set;
    char buf[32];
    int rc;

    if (!g_Connected) {
        return;
    }
    ZeroMemory(&set, sizeof(set));

    GetWindowTextA(g_Sec[SEC_THERM][1], buf, sizeof(buf));
    set.Source = (uint32_t)strtoul(buf, NULL, 0);
    GetWindowTextA(g_Sec[SEC_THERM][2], buf, sizeof(buf));
    set.ScaleNum = (uint32_t)strtoul(buf, NULL, 0);
    GetWindowTextA(g_Sec[SEC_THERM][3], buf, sizeof(buf));
    set.ScaleDen = (uint32_t)strtoul(buf, NULL, 0);
    GetWindowTextA(g_Sec[SEC_THERM][4], buf, sizeof(buf));
    set.OffsetMilliC = (int32_t)strtol(buf, NULL, 0);
    GetWindowTextA(g_Sec[SEC_THERM][5], buf, sizeof(buf));
    set.LimitC = (uint32_t)strtoul(buf, NULL, 0);
    set.Trusted = 1;

    rc = Bc250ThermalSet(&g_Ctx, &set);
    if (rc != 0) {
        LogUi("# calibracao recusada: erro %d (limite valido: 40..105 C)", rc);
    } else {
        LogUi("# calibracao aceita: fonte %u, %u/%u, offset %d mC, limite %u C",
              set.Source, set.ScaleNum, set.ScaleDen, set.OffsetMilliC, set.LimitC);
    }
    RefreshTelemetry();
    ThermRefresh();
}

static void DoArm(uint32_t level)
{
    int rc;

    if (!g_Connected) {
        return;
    }
    if (level == BC250DBG_ARM_UNSAFE) {
        if (MessageBoxA(g_Main,
                "O nivel 'unsafe' libera mensagens de SMU que mexem em clock e\n"
                "tensao. Ja houve trava de maquina com ForceGfxFreq sem tabelas\n"
                "DPM. O sensor termico precisa estar calibrado.\n\n"
                "Continuar?",
                "BC-250 Studio — armar unsafe",
                MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }
    rc = Bc250Arm(&g_Ctx, level);
    if (rc != 0) {
        LogUi("# arm falhou: erro %d%s", rc,
              level == BC250DBG_ARM_UNSAFE
                ? " (unsafe exige sensor termico calibrado)" : "");
    } else {
        LogUi("# armamento agora: %s", ArmName(level));
    }
    RefreshTelemetry();
}

/* ------------------------------------------------------------------ */
/* Worker: operacoes longas fora da thread da UI                       */
/* ------------------------------------------------------------------ */

static DWORD WINAPI WorkerProc(LPVOID param)
{
    LONG task = (LONG)(LONG_PTR)param;

    if (task == TASK_SCRIPT) {
        char err[256], path[MAX_PATH];

        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%s", g_ExpDir, g_TaskArg);
        g_RunOk = 0;
        free(g_RunResults);
        g_RunResults = NULL;

        if (!Bc250ScriptLoadFile(path, &g_RunScript, err, sizeof(err))) {
            _snprintf_s(g_TaskMsg, sizeof(g_TaskMsg), _TRUNCATE,
                        "# erro de montagem: %s", err);
        } else if (Bc250ScriptRun(&g_Ctx, &g_RunScript, &g_RunOut,
                                  &g_RunResults) != 0) {
            _snprintf_s(g_TaskMsg, sizeof(g_TaskMsg), _TRUNCATE,
                        "# execucao falhou (driver recusou o IOCTL)");
            Bc250ScriptFree(&g_RunScript);
        } else {
            g_RunOk = 1;
            _snprintf_s(g_TaskMsg, sizeof(g_TaskMsg), _TRUNCATE,
                        "# script '%s': %u ops, %u falha(s), %llu us, final %s",
                        g_RunScript.Name, g_RunOut.OpCount, g_RunOut.Failures,
                        (unsigned long long)g_RunOut.ElapsedUs,
                        Bc250OpStatusName(g_RunOut.Status));
        }
    } else if (task == TASK_SCAN) {
        /* Varredura por conteudo: acha o bloco GC sem depender de convencao.
         * 0xFFF80000 = CC_GC_SHADER_ARRAY_CONFIG lido no Linux. */
        const uint32_t total = 0x80000u / 4u;
        uint32_t *buf = (uint32_t *)malloc(total * sizeof(uint32_t));
        uint32_t off = 0, i, hits = 0;
        int n = 0;

        if (!buf) {
            strcpy_s(g_TaskMsg, sizeof(g_TaskMsg), "# sem memoria");
        } else {
            int failed = 0;
            while (off < total) {
                uint32_t chunk = total - off;
                if (chunk > BC250DBG_SNAP_MAX) {
                    chunk = BC250DBG_SNAP_MAX;
                }
                if (Bc250Snapshot(&g_Ctx, BC250DBG_SPACE_BAR, off * 4, chunk,
                                  buf + off) != 0) {
                    failed = 1;
                    break;
                }
                off += chunk;
            }
            if (failed) {
                _snprintf_s(g_TaskMsg, sizeof(g_TaskMsg), _TRUNCATE,
                            "# varredura falhou em 0x%05X", off * 4);
            } else {
                n = _snprintf_s(g_TaskMsg, sizeof(g_TaskMsg), _TRUNCATE,
                                "# 0xFFF80000 aparece em:");
                for (i = 0; i < total && hits < 8; i++) {
                    if (buf[i] == 0xFFF80000u) {
                        uint32_t baseB = i - 0x2004u;   /* dword de CC_GC */
                        n += _snprintf_s(g_TaskMsg + n, sizeof(g_TaskMsg) - n,
                                         _TRUNCATE, "  0x%05X%s", i * 4,
                                         baseB == 0x1260u ? " (=GC base B!)" : "");
                        hits++;
                    }
                }
                if (hits == 0) {
                    strcpy_s(g_TaskMsg, sizeof(g_TaskMsg),
                             "# 0xFFF80000 nao aparece em lugar nenhum do BAR5");
                }
            }
            free(buf);
        }
    }

    PostMessageA(g_Main, WM_APP_TASKDONE, (WPARAM)task, 0);
    return 0;
}

static void StartTask(LONG task, const char *arg)
{
    if (!g_Connected) {
        LogUi("# sem driver");
        return;
    }
    if (InterlockedCompareExchange(&g_Task, task, TASK_NONE) != TASK_NONE) {
        LogUi("# ja existe uma operacao em andamento");
        return;
    }
    if (arg) {
        strcpy_s(g_TaskArg, sizeof(g_TaskArg), arg);
    }
    if (g_Worker) {
        CloseHandle(g_Worker);
    }
    g_Worker = CreateThread(NULL, 0, WorkerProc, (LPVOID)(LONG_PTR)task, 0, NULL);
    if (!g_Worker) {
        InterlockedExchange(&g_Task, TASK_NONE);
        LogUi("# nao consegui criar a thread de trabalho");
    }
}

/* ------------------------------------------------------------------ */
/* Criacao dos controles                                               */
/* ------------------------------------------------------------------ */

static HWND Mk(int sec, const char *cls, const char *text, DWORD style, int id)
{
    HWND h = CreateWindowExA(
        (strcmp(cls, "EDIT") == 0 || strcmp(cls, "LISTBOX") == 0 ||
         strcmp(cls, WC_LISTVIEWA) == 0) ? WS_EX_CLIENTEDGE : 0,
        cls, text, WS_CHILD | style, 0, 0, 10, 10,
        g_Main, (HMENU)(INT_PTR)id, g_Inst, NULL);

    SendMessageA(h, WM_SETFONT, (WPARAM)g_FontUi, TRUE);
    if (sec >= 0 && g_SecN[sec] < 24) {
        g_Sec[sec][g_SecN[sec]++] = h;
    }
    return h;
}

static void LvColumn(HWND lv, int i, const char *title, int width)
{
    LVCOLUMNA col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.iSubItem = i;
    col.pszText = (LPSTR)title;
    col.cx = width;
    SendMessageA(lv, LVM_INSERTCOLUMNA, i, (LPARAM)&col);
}

static void CreateAll(void)
{
    HWND lv;
    int i;

    /* --- barra lateral --- */
    g_Side = CreateWindowExA(0, "LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_HASSTRINGS,
        0, 0, 10, 10, g_Main, (HMENU)IDC_SIDE, g_Inst, NULL);
    SendMessageA(g_Side, WM_SETFONT, (WPARAM)g_FontUi, TRUE);
    SendMessageA(g_Side, LB_ADDSTRING, 0, (LPARAM)"  Dashboard");
    SendMessageA(g_Side, LB_ADDSTRING, 0, (LPARAM)"  Registradores");
    SendMessageA(g_Side, LB_ADDSTRING, 0, (LPARAM)"  Experimentos");
    SendMessageA(g_Side, LB_ADDSTRING, 0, (LPARAM)"  Termico / armar");
    SendMessageA(g_Side, LB_SETCURSEL, 0, 0);

    /* --- painel de log (sempre visivel) --- */
    g_Log = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", NULL,
        /* LBS_HASSTRINGS e obrigatorio: sem ele, um listbox owner-draw guarda
         * o ponteiro passado no LB_ADDSTRING em vez de copiar o texto — e a
         * linha viraria lixo assim que o buffer local saisse de escopo. */
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOSEL,
        0, 0, 10, 10, g_Main, (HMENU)IDC_LOG, g_Inst, NULL);
    SendMessageA(g_Log, WM_SETFONT, (WPARAM)g_FontMono, TRUE);
    SendMessageA(g_Log, LB_SETHORIZONTALEXTENT, 1600, 0);

    g_LogTrace = Mk(-1, "BUTTON", "trace de registrador",
                    WS_VISIBLE | BS_AUTOCHECKBOX, IDC_LOG_TRACE);
    g_LogPause = Mk(-1, "BUTTON", "pausar rolagem",
                    WS_VISIBLE | BS_AUTOCHECKBOX, IDC_LOG_PAUSE);
    g_LogAutoSave = Mk(-1, "BUTTON", "gravar em disco",
                       WS_VISIBLE | BS_AUTOCHECKBOX, IDC_LOG_AUTOSV);
    Mk(-1, "BUTTON", "limpar", WS_VISIBLE | BS_PUSHBUTTON, IDC_LOG_CLEAR);
    Mk(-1, "BUTTON", "salvar...", WS_VISIBLE | BS_PUSHBUTTON, IDC_LOG_SAVE);

    /* --- secao: dashboard --- */
    Mk(SEC_DASH, "EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY,
       IDC_D_TEXT);
    SendMessageA(g_Sec[SEC_DASH][0], WM_SETFONT, (WPARAM)g_FontMono, TRUE);
    Mk(SEC_DASH, "BUTTON", "Atualizar", BS_PUSHBUTTON, IDC_D_REFRESH);
    Mk(SEC_DASH, "BUTTON", "Varredura de offset (le 512 KB)", BS_PUSHBUTTON,
       IDC_D_SCAN);
    Mk(SEC_DASH, "BUTTON", "Reconectar", BS_PUSHBUTTON, IDC_D_RECONN);

    /* --- secao: registradores --- */
    lv = Mk(SEC_REGS, WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL, IDC_R_LIST);
    SendMessageA(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    LvColumn(lv, 0, "NOME", 210);
    LvColumn(lv, 1, "ESP", 50);
    LvColumn(lv, 2, "OFFSET", 100);
    LvColumn(lv, 3, "VALOR", 100);
    LvColumn(lv, 4, "NOTA", 320);
    Mk(SEC_REGS, "BUTTON", "Atualizar valores", BS_PUSHBUTTON, IDC_R_REFRESH);
    Mk(SEC_REGS, "BUTTON", "auto 1s", BS_AUTOCHECKBOX, IDC_R_AUTO);
    Mk(SEC_REGS, "EDIT", "SCRATCH_REG0", ES_AUTOHSCROLL, IDC_R_NAME);
    Mk(SEC_REGS, "EDIT", "0x00000000", ES_AUTOHSCROLL, IDC_R_VALUE);
    Mk(SEC_REGS, "BUTTON", "Escrever", BS_PUSHBUTTON, IDC_R_WRITE);

    /* --- secao: experimentos --- */
    Mk(SEC_EXP, "LISTBOX", "", WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
       IDC_E_LIST);
    Mk(SEC_EXP, "BUTTON", "Executar", BS_PUSHBUTTON, IDC_E_RUN);
    lv = Mk(SEC_EXP, WC_LISTVIEWA, "", LVS_REPORT, IDC_E_RESULT);
    SendMessageA(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    LvColumn(lv, 0, "LINHA", 60);
    LvColumn(lv, 1, "OP", 80);
    LvColumn(lv, 2, "STATUS", 120);
    LvColumn(lv, 3, "VALOR", 130);
    LvColumn(lv, 4, "DETALHE", 260);

    /* --- secao: termico --- */
    Mk(SEC_THERM, "EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY,
       IDC_T_TEXT);
    SendMessageA(g_Sec[SEC_THERM][0], WM_SETFONT, (WPARAM)g_FontMono, TRUE);
    Mk(SEC_THERM, "EDIT", "1", ES_AUTOHSCROLL, IDC_T_SRC);
    Mk(SEC_THERM, "EDIT", "1000", ES_AUTOHSCROLL, IDC_T_NUM);
    Mk(SEC_THERM, "EDIT", "1", ES_AUTOHSCROLL, IDC_T_DEN);
    Mk(SEC_THERM, "EDIT", "0", ES_AUTOHSCROLL, IDC_T_OFF);
    Mk(SEC_THERM, "EDIT", "85", ES_AUTOHSCROLL, IDC_T_LIMIT);
    Mk(SEC_THERM, "BUTTON", "Aplicar calibracao", BS_PUSHBUTTON, IDC_T_APPLY);
    Mk(SEC_THERM, "BUTTON", "arm readonly", BS_PUSHBUTTON, IDC_T_ARM_RO);
    Mk(SEC_THERM, "BUTTON", "arm write", BS_PUSHBUTTON, IDC_T_ARM_W);
    Mk(SEC_THERM, "BUTTON", "arm UNSAFE", BS_PUSHBUTTON, IDC_T_ARM_UNSAFE);
    Mk(SEC_THERM, "STATIC", "fonte (0=edge 1=junc 2=mem) / num / den / offset mC / limite C",
       SS_LEFT, -1);

    /* rotulos da secao de registradores, criados por ultimo */
    Mk(SEC_REGS, "STATIC", "registrador:", SS_RIGHT, -1);
    Mk(SEC_REGS, "STATIC", "valor:", SS_RIGHT, -1);

    for (i = 0; i < SEC_COUNT; i++) {
        int j;
        for (j = 0; j < g_SecN[i]; j++) {
            ShowWindow(g_Sec[i][j], i == g_Section ? SW_SHOW : SW_HIDE);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

static void MoveCtl(HWND h, int x, int y, int w, int hh)
{
    if (h) {
        MoveWindow(h, x, y, w, hh, TRUE);
    }
}

static void LayoutAll(void)
{
    RECT rc;
    int W, H, cx, cy, cw, ch, y, x;

    GetClientRect(g_Main, &rc);
    W = rc.right;
    H = rc.bottom;

    MoveCtl(g_Side, 0, STATUS_H, SIDEBAR_W, H - STATUS_H);

    cx = SIDEBAR_W + PAD;
    cy = STATUS_H + PAD;
    cw = W - cx - PAD;
    ch = H - cy - LOG_H - LOGBAR_H - PAD;
    if (ch < 80) {
        ch = 80;
    }

    /* barra do log + lista, coladas no rodape */
    y = H - LOG_H - LOGBAR_H;
    x = cx;
    MoveCtl(g_LogTrace, x, y + 4, 150, 20);          x += 158;
    MoveCtl(g_LogPause, x, y + 4, 120, 20);          x += 128;
    MoveCtl(g_LogAutoSave, x, y + 4, 130, 20);       x += 138;
    MoveCtl(GetDlgItem(g_Main, IDC_LOG_CLEAR), x, y + 2, 70, 24);   x += 78;
    MoveCtl(GetDlgItem(g_Main, IDC_LOG_SAVE), x, y + 2, 80, 24);
    MoveCtl(g_Log, cx, y + LOGBAR_H, cw, LOG_H - PAD);

    switch (g_Section) {
    case SEC_DASH:
        MoveCtl(g_Sec[SEC_DASH][0], cx, cy, cw, ch - 38);
        MoveCtl(g_Sec[SEC_DASH][1], cx, cy + ch - 30, 110, 26);
        MoveCtl(g_Sec[SEC_DASH][2], cx + 118, cy + ch - 30, 230, 26);
        MoveCtl(g_Sec[SEC_DASH][3], cx + 356, cy + ch - 30, 110, 26);
        break;

    case SEC_REGS:
        MoveCtl(g_Sec[SEC_REGS][0], cx, cy, cw, ch - 38);
        MoveCtl(g_Sec[SEC_REGS][1], cx, cy + ch - 30, 140, 26);
        MoveCtl(g_Sec[SEC_REGS][2], cx + 148, cy + ch - 27, 80, 20);
        MoveCtl(g_Sec[SEC_REGS][6], cx + 236, cy + ch - 26, 80, 18);   /* rotulo */
        MoveCtl(g_Sec[SEC_REGS][3], cx + 320, cy + ch - 30, 180, 24);
        MoveCtl(g_Sec[SEC_REGS][7], cx + 506, cy + ch - 26, 45, 18);   /* rotulo */
        MoveCtl(g_Sec[SEC_REGS][4], cx + 555, cy + ch - 30, 120, 24);
        MoveCtl(g_Sec[SEC_REGS][5], cx + 683, cy + ch - 30, 90, 26);
        break;

    case SEC_EXP:
        MoveCtl(g_Sec[SEC_EXP][0], cx, cy, 260, ch - 38);
        MoveCtl(g_Sec[SEC_EXP][1], cx, cy + ch - 30, 260, 26);
        MoveCtl(g_Sec[SEC_EXP][2], cx + 270, cy, cw - 270, ch);
        break;

    case SEC_THERM:
        MoveCtl(g_Sec[SEC_THERM][0], cx, cy, cw, ch - 76);
        MoveCtl(g_Sec[SEC_THERM][10], cx, cy + ch - 68, 520, 18);  /* rotulo */
        x = cx;
        MoveCtl(g_Sec[SEC_THERM][1], x, cy + ch - 46, 50, 24);  x += 58;
        MoveCtl(g_Sec[SEC_THERM][2], x, cy + ch - 46, 70, 24);  x += 78;
        MoveCtl(g_Sec[SEC_THERM][3], x, cy + ch - 46, 60, 24);  x += 68;
        MoveCtl(g_Sec[SEC_THERM][4], x, cy + ch - 46, 70, 24);  x += 78;
        MoveCtl(g_Sec[SEC_THERM][5], x, cy + ch - 46, 60, 24);  x += 68;
        MoveCtl(g_Sec[SEC_THERM][6], x, cy + ch - 48, 160, 26);
        x = cx;
        MoveCtl(g_Sec[SEC_THERM][7], x, cy + ch - 14, 120, 26);  x += 128;
        MoveCtl(g_Sec[SEC_THERM][8], x, cy + ch - 14, 120, 26);  x += 128;
        MoveCtl(g_Sec[SEC_THERM][9], x, cy + ch - 14, 120, 26);
        break;
    }
}

static void SwitchSection(int sec)
{
    int i, j;

    if (sec < 0 || sec >= SEC_COUNT) {
        return;
    }
    g_Section = sec;
    for (i = 0; i < SEC_COUNT; i++) {
        for (j = 0; j < g_SecN[i]; j++) {
            ShowWindow(g_Sec[i][j], i == sec ? SW_SHOW : SW_HIDE);
        }
    }
    LayoutAll();

    if (sec == SEC_DASH)  DashRefresh();
    if (sec == SEC_REGS)  RegsRefreshValues();
    if (sec == SEC_THERM) ThermRefresh();
    InvalidateRect(g_Main, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* Janela principal                                                    */
/* ------------------------------------------------------------------ */

static void DrawLogItem(LPDRAWITEMSTRUCT di)
{
    char buf[512];
    COLORREF fg;
    int len;

    if ((int)di->itemID < 0) {
        return;
    }
    len = (int)SendMessageA(di->hwndItem, LB_GETTEXTLEN, di->itemID, 0);
    if (len <= 0 || len >= (int)sizeof(buf)) {
        return;
    }
    SendMessageA(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)buf);

    switch ((uint32_t)di->itemData) {
    case BC250DBG_LVL_ERROR: fg = RGB(200, 40, 40);   break;
    case BC250DBG_LVL_WARN:  fg = RGB(170, 110, 0);   break;
    case BC250DBG_LVL_TRACE: fg = RGB(120, 120, 130); break;
    case BC250DBG_LVL_INFO:  fg = RGB(20, 20, 20);    break;
    default:                 fg = RGB(0, 90, 170);    break;  /* mensagem da GUI */
    }

    FillRect(di->hDC, &di->rcItem, (HBRUSH)(COLOR_WINDOW + 1));
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, fg);
    TextOutA(di->hDC, di->rcItem.left + 3, di->rcItem.top, buf, (int)strlen(buf));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_CREATE:
        g_Main = hwnd;
        CreateAll();
        return 0;

    case WM_SIZE:
        LayoutAll();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(hwnd, &ps);

        GetClientRect(hwnd, &rc);
        rc.bottom = STATUS_H;
        DrawStatusStrip(dc, &rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT mi = (LPMEASUREITEMSTRUCT)lp;
        if (mi->CtlID == IDC_LOG) {
            mi->itemHeight = 15;
        }
        return TRUE;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lp;
        if (di->CtlID == IDC_LOG) {
            DrawLogItem(di);
            return TRUE;
        }
        break;
    }

    case WM_TIMER:
        if (wp == TIMER_LOG) {
            LogDrainToPane();
        } else if (wp == TIMER_TELEM) {
            RefreshTelemetry();
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_Section == SEC_DASH) {
                DashRefresh();
            } else if (g_Section == SEC_THERM) {
                ThermRefresh();
            } else if (g_Section == SEC_REGS &&
                       SendMessageA(g_Sec[SEC_REGS][2], BM_GETCHECK, 0, 0)
                           == BST_CHECKED) {
                RegsRefreshValues();
            }
        }
        return 0;

    case WM_APP_TASKDONE:
        LogUi("%s", g_TaskMsg);
        if (wp == TASK_SCRIPT) {
            ExpShowResults();
            if (g_RunOk) {
                Bc250ScriptFree(&g_RunScript);
            }
        }
        InterlockedExchange(&g_Task, TASK_NONE);
        RefreshTelemetry();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);

        if (id == IDC_SIDE && code == LBN_SELCHANGE) {
            SwitchSection((int)SendMessageA(g_Side, LB_GETCURSEL, 0, 0));
            return 0;
        }

        switch (id) {
        case IDC_D_REFRESH:
            RefreshTelemetry();
            DashRefresh();
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case IDC_D_SCAN:
            LogUi("# varrendo os 512 KB do BAR5 atras de 0xFFF80000...");
            LogUi("# (isso toca registrador com efeito de leitura; nao rode "
                  "com carga real)");
            StartTask(TASK_SCAN, NULL);
            break;

        case IDC_D_RECONN:
            Connect();
            RefreshTelemetry();
            DashRefresh();
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case IDC_R_REFRESH:
            RegsRefreshValues();
            break;

        case IDC_R_WRITE:
            RegsWrite();
            break;

        case IDC_E_RUN: {
            char name[MAX_PATH];
            int sel = (int)SendMessageA(g_Sec[SEC_EXP][0], LB_GETCURSEL, 0, 0);
            if (sel < 0) {
                LogUi("# selecione um experimento");
                break;
            }
            SendMessageA(g_Sec[SEC_EXP][0], LB_GETTEXT, sel, (LPARAM)name);
            LogUi("# executando %s", name);
            StartTask(TASK_SCRIPT, name);
            break;
        }

        case IDC_T_APPLY:
            ThermApply();
            break;

        case IDC_T_ARM_RO:
            DoArm(BC250DBG_ARM_READONLY);
            break;
        case IDC_T_ARM_W:
            DoArm(BC250DBG_ARM_WRITE);
            break;
        case IDC_T_ARM_UNSAFE:
            DoArm(BC250DBG_ARM_UNSAFE);
            break;

        case IDC_LOG_TRACE:
            if (g_Connected) {
                int on = SendMessageA(g_LogTrace, BM_GETCHECK, 0, 0) == BST_CHECKED;
                Bc250LogSetMask(&g_Ctx,
                    on ? BC250DBG_LVL_ALL
                       : (BC250DBG_LVL_ERROR | BC250DBG_LVL_WARN | BC250DBG_LVL_INFO),
                    BC250DBG_SUB_ALL);
                LogUi("# trace de registrador %s", on ? "ligado" : "desligado");
            }
            break;

        case IDC_LOG_CLEAR:
            SendMessageA(g_Log, LB_RESETCONTENT, 0, 0);
            break;

        case IDC_LOG_SAVE:
            LogSaveAs();
            break;

        case IDC_LOG_AUTOSV: {
            int on = SendMessageA(g_LogAutoSave, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (on && !g_AutoSave) {
                if (fopen_s(&g_AutoSave, "bc250-studio.log", "a") != 0) {
                    g_AutoSave = NULL;
                    LogUi("# nao consegui abrir bc250-studio.log");
                } else {
                    LogUi("# gravando em bc250-studio.log (flush por linha)");
                }
            } else if (!on && g_AutoSave) {
                fclose(g_AutoSave);
                g_AutoSave = NULL;
                LogUi("# gravacao em disco parada");
            }
            break;
        }
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_LOG);
        KillTimer(hwnd, TIMER_TELEM);
        if (g_AutoSave) {
            fclose(g_AutoSave);
            g_AutoSave = NULL;
        }
        if (g_Connected) {
            Bc250Close(&g_Ctx);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSEXA wc;
    INITCOMMONCONTROLSEX icc;
    HWND hwnd;
    MSG msg;

    UNREFERENCED_PARAMETER(prev);
    UNREFERENCED_PARAMETER(cmd);

    g_Inst = inst;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    g_FontUi   = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_FontBig  = CreateFontA(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                             0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_FontMono = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "BC250StudioWnd";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    hwnd = CreateWindowExA(0, "BC250StudioWnd",
        "BC-250 Studio v0 — instrumentacao (bc250dbg.sys)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 800,
        NULL, NULL, inst, NULL);
    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, show);

    LogUi("# BC-250 Studio v0");
    LogUi("# leia docs\\ORACULO-LINUX-2026-08-18.md antes de concluir qualquer "
          "coisa sobre registrador de GC");
    Connect();
    ExpFindDir();
    ExpFill();
    LogUi("# experimentos em %s", g_ExpDir);
    RefreshTelemetry();
    DashRefresh();
    LayoutAll();

    SetTimer(hwnd, TIMER_LOG, 200, NULL);
    SetTimer(hwnd, TIMER_TELEM, 1000, NULL);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    DeleteObject(g_FontUi);
    DeleteObject(g_FontBig);
    DeleteObject(g_FontMono);
    return (int)msg.wParam;
}
