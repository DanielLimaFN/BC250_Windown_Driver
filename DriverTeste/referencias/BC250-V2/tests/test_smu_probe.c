/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_smu_probe.c

Abstract:
    Diagnostic tool to probe SMN Mailbox registers on AMD BC-250 (Cyan Skillfish).
    Tests Method A (MM_INDEX 0x00 with 0x80000000), Method B (RSMU 0x38), Method C (RSMU 0x38 with bit 31).
    Sends PPSMC_MSG_TestMessage (0x01) with arg 0x1234 and checks for 0x1235 in return.

--*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMHDC {
    HDC           hDc;
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;
    UINT          VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef enum _D3DKMT_ESCAPETYPE {
    D3DKMT_ESCAPE_DRIVERPRIVATE = 0
} D3DKMT_ESCAPETYPE;

typedef struct _D3DKMT_ESCAPE {
    D3DKMT_HANDLE     hAdapter;
    D3DKMT_HANDLE     hDevice;
    D3DKMT_ESCAPETYPE Type;
    struct {
        UINT Value;
    } Flags;
    VOID*             pPrivateDriverData;
    UINT              PrivateDriverDataSize;
    D3DKMT_HANDLE     hContext;
} D3DKMT_ESCAPE;

typedef LONG (APIENTRY *PFN_D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC*);
typedef LONG (APIENTRY *PFN_D3DKMTEscape)(CONST D3DKMT_ESCAPE*);

#define BC250_ESCAPE_CMD_READ_REG           0x02
#define BC250_ESCAPE_CMD_WRITE_REG          0x03

typedef struct _BC250_ESCAPE_HEADER {
    ULONG CommandId;
    ULONG Status;
    ULONG InSize;
    ULONG OutSize;
} BC250_ESCAPE_HEADER;

typedef struct _BC250_ESCAPE_REG_ACCESS {
    BC250_ESCAPE_HEADER Header;
    ULONG Offset;
    ULONG Value;
} BC250_ESCAPE_REG_ACCESS;

static PFN_D3DKMTOpenAdapterFromHdc g_pfnOpenHdc = NULL;
static PFN_D3DKMTEscape             g_pfnEscape = NULL;
static D3DKMT_HANDLE                g_hAdapter = 0;

static BOOL InitD3dKmt(void)
{
    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) return FALSE;

    g_pfnOpenHdc = (PFN_D3DKMTOpenAdapterFromHdc)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromHdc");
    g_pfnEscape = (PFN_D3DKMTEscape)GetProcAddress(hGdi, "D3DKMTEscape");
    if (!g_pfnOpenHdc || !g_pfnEscape) return FALSE;

    HDC hdc = GetDC(NULL);
    if (!hdc) return FALSE;

    D3DKMT_OPENADAPTERFROMHDC openHdc = { 0 };
    openHdc.hDc = hdc;
    LONG status = g_pfnOpenHdc(&openHdc);
    ReleaseDC(NULL, hdc);

    if (status >= 0 && openHdc.hAdapter != 0) {
        g_hAdapter = openHdc.hAdapter;
        return TRUE;
    }
    return FALSE;
}

static ULONG RegRead(ULONG offset)
{
    BC250_ESCAPE_REG_ACCESS acc = { 0 };
    acc.Header.CommandId = BC250_ESCAPE_CMD_READ_REG;
    acc.Header.InSize = sizeof(acc);
    acc.Header.OutSize = sizeof(acc);
    acc.Offset = offset;

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = g_hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = &acc;
    esc.PrivateDriverDataSize = sizeof(acc);

    if (g_pfnEscape(&esc) >= 0 && acc.Header.Status == 0) {
        return acc.Value;
    }
    return 0xFFFFFFFF;
}

static BOOL RegWrite(ULONG offset, ULONG value)
{
    BC250_ESCAPE_REG_ACCESS acc = { 0 };
    acc.Header.CommandId = BC250_ESCAPE_CMD_WRITE_REG;
    acc.Header.InSize = sizeof(acc);
    acc.Header.OutSize = sizeof(acc);
    acc.Offset = offset;
    acc.Value = value;

    D3DKMT_ESCAPE esc = { 0 };
    esc.hAdapter = g_hAdapter;
    esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.pPrivateDriverData = &acc;
    esc.PrivateDriverDataSize = sizeof(acc);

    if (g_pfnEscape(&esc) >= 0 && acc.Header.Status == 0) {
        return TRUE;
    }
    return FALSE;
}

/* Method A: MM_INDEX (0x00) with bit 31 + MM_DATA (0x04) */
static ULONG SmnRead_MethodA(ULONG smnAddr)
{
    RegWrite(0x0000, 0x80000000u | smnAddr);
    return RegRead(0x0004);
}
static VOID SmnWrite_MethodA(ULONG smnAddr, ULONG val)
{
    RegWrite(0x0000, 0x80000000u | smnAddr);
    RegWrite(0x0004, val);
}

/* Method B: RSMU (0x38 / 0x3C) */
static ULONG SmnRead_MethodB(ULONG smnAddr)
{
    RegWrite(0x0038, smnAddr);
    return RegRead(0x003C);
}
static VOID SmnWrite_MethodB(ULONG smnAddr, ULONG val)
{
    RegWrite(0x0038, smnAddr);
    RegWrite(0x003C, val);
}

/* Method C: RSMU (0x38 with bit 31 / 0x3C) */
static ULONG SmnRead_MethodC(ULONG smnAddr)
{
    RegWrite(0x0038, 0x80000000u | smnAddr);
    return RegRead(0x003C);
}
static VOID SmnWrite_MethodC(ULONG smnAddr, ULONG val)
{
    RegWrite(0x0038, 0x80000000u | smnAddr);
    RegWrite(0x003C, val);
}

static void TestMailbox(const char* name, int method, ULONG cmdReg, ULONG argReg, ULONG rspReg)
{
    printf("\n--- Testando %s (Metodo %c: CMD=0x%08X, ARG=0x%08X, RSP=0x%08X) ---\n",
           name, 'A' + method, cmdReg, argReg, rspReg);

    ULONG (*Read)(ULONG) = (method == 0) ? SmnRead_MethodA : ((method == 1) ? SmnRead_MethodB : SmnRead_MethodC);
    VOID (*Write)(ULONG, ULONG) = (method == 0) ? SmnWrite_MethodA : ((method == 1) ? SmnWrite_MethodB : SmnWrite_MethodC);

    ULONG initialRsp = Read(rspReg);
    ULONG initialArg = Read(argReg);
    ULONG initialCmd = Read(cmdReg);
    printf("  [Leitura Inicial] RSP=0x%08X, ARG=0x%08X, CMD=0x%08X\n", initialRsp, initialArg, initialCmd);

    /* 1. Limpa resposta */
    Write(rspReg, 0);
    /* 2. Escreve argumento de teste */
    Write(argReg, 0x1234);
    /* 3. Escreve mensagem 0x01 (TestMessage) */
    Write(cmdReg, 0x01);

    /* 4. Readback imediato */
    ULONG rbCmd = Read(cmdReg);
    ULONG rbArg = Read(argReg);
    ULONG rbRsp = Read(rspReg);
    printf("  [Readback Pos-Escrita] CMD=0x%08X (esp 0x01), ARG=0x%08X (esp 0x1234), RSP=0x%08X\n",
           rbCmd, rbArg, rbRsp);

    /* 5. Polling de resposta (ate 50ms) */
    ULONG finalRsp = 0;
    ULONG finalArg = 0;
    for (int i = 0; i < 50; i++) {
        finalRsp = Read(rspReg);
        if (finalRsp != 0) break;
        Sleep(1);
    }
    finalArg = Read(argReg);

    if (finalRsp == 1 && finalArg == 0x1235) {
        printf("  >>> SUCESSO ABSOLUTO! SMU respondeu com RSP=1 e ARG=0x%08X (0x1234+1) <<<\n", finalArg);
    } else if (finalRsp != 0) {
        printf("  [!] SMU respondeu mas com status=0x%08X e arg=0x%08X\n", finalRsp, finalArg);
    } else {
        printf("  [-] Timeout: SMU nao alterou RSP (ficou 0x%08X)\n", finalRsp);
    }
}

int main(void)
{
    printf("=================================================================\n");
    printf("   Sonda de Diagnostico SMN & SMU Mailbox (AMD BC-250)           \n");
    printf("=================================================================\n");

    if (!InitD3dKmt()) {
        printf("ERRO: Nao foi possivel conectar ao driver WDDM via D3DKMT.\n");
        return 1;
    }
    printf("Conectado ao adaptador WDDM (hAdapter = 0x%08X)\n", g_hAdapter);

    /* Testa Fila 0 nos 3 metodos de aperture */
    TestMailbox("Fila 0 (C2PMSG 66/82/90)", 0, 0x03B10A08, 0x03B10A48, 0x03B10A68);
    TestMailbox("Fila 0 (C2PMSG 66/82/90)", 1, 0x03B10A08, 0x03B10A48, 0x03B10A68);
    TestMailbox("Fila 0 (C2PMSG 66/82/90)", 2, 0x03B10A08, 0x03B10A48, 0x03B10A68);

    /* Testa Fila 2 nos 3 metodos de aperture */
    TestMailbox("Fila 2 (C2PMSG 68/84/92)", 0, 0x03B10528, 0x03B10998, 0x03B10564);
    TestMailbox("Fila 2 (C2PMSG 68/84/92)", 1, 0x03B10528, 0x03B10998, 0x03B10564);
    TestMailbox("Fila 2 (C2PMSG 68/84/92)", 2, 0x03B10528, 0x03B10998, 0x03B10564);

    return 0;
}
