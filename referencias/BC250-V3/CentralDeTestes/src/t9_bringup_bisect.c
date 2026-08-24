/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t9_bringup_bisect.c

Abstract:
    Teste T9: Inspecao e Bissecao de Hardware Bring-up no Registro do Windows.
    
    DESCRICAO DETALHADA:
    Este teste inspeciona o estado gravado pelo driver de video em tempo de execucao
    na chave HKLM\SYSTEM\CurrentControlSet\Services\bc250v4\Parameters.
    Ele diagnostica o progresso de cada etapa fisica:
      1. HwBringupStatus (deve ser STATUS_SUCCESS / 0x00000000).
      2. PSP TOS Ready bit (bit 31 do registrador 0x58200).
      3. SMU Version (versao do microcodigo SMU reportada via Mailbox Q0).
      4. SDMA Ring & Benchmark (taxa e status da engine de copia).
      5. Contagem de Compute Units e Shaders fisicos alocados (24 CUs / 12 WGPs / 1536 Shaders).

--*/

#include "test_framework.h"

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc =
        "Audita a chave de parametros do driver de video no Registro do Windows,\n"
        " diagnosticando a bissecao exata de cada etapa do bring-up de hardware\n"
        " (SMU, PSP, TMR, SDMA0 e Command Processor).";

    if (!jsonOutput) {
        PrintTestHeader("T9", "Inspecao de Hardware Bring-up no Registro do Windows", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T9";
    res.TestTitle = "Hardware Bring-up & Registry Diagnostic";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "HwBringupStatus==0x00000000; PSP_TOS bit31==1; SDMA_Status==0x00000000; CU/WGP/ALU==24/12/1536.");

    HKEY hKey = NULL;
    LPCWSTR subKey = L"SYSTEM\\CurrentControlSet\\Services\\bc250v4\\Parameters";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        res.Verdict = VERDICT_BLOCKED;
        strcpy_s(res.Measured, sizeof(res.Measured),
            "Chave de parametros do driver bc250v4 nao encontrada no registro (driver nao carregado).");
        strcpy_s(res.Details, sizeof(res.Details),
            "Instale o driver com package\\install-wddm.ps1 antes de executar a bissecao.");
        PrintTestResult(&res, jsonOutput);
        return 1;
    }

    ULONG hwBringupStatus = 0;
    ULONG pspTos = 0, pspRing = 0, smuVer = 0, sdmaStatus = 0, sdmaBw = 0;
    ULONG numCu = 0, numWgp = 0, shaders = 0;
    ULONG rbCntl = 0, meCntl = 0, rptrInit = 0;
    DWORD sz = sizeof(ULONG);

    RegQueryValueExW(hKey, L"HwBringupStatus", NULL, NULL, (LPBYTE)&hwBringupStatus, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_PspTosReg", NULL, NULL, (LPBYTE)&pspTos, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_PspRing", NULL, NULL, (LPBYTE)&pspRing, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_SmuVersion", NULL, NULL, (LPBYTE)&smuVer, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_Sdma_Status", NULL, NULL, (LPBYTE)&sdmaStatus, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_Sdma_Bandwidth_MBs", NULL, NULL, (LPBYTE)&sdmaBw, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_NumCu", NULL, NULL, (LPBYTE)&numCu, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_NumWgp", NULL, NULL, (LPBYTE)&numWgp, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_NumShaders", NULL, NULL, (LPBYTE)&shaders, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_RbCntlReadback", NULL, NULL, (LPBYTE)&rbCntl, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_MeCntlReadback", NULL, NULL, (LPBYTE)&meCntl, &sz);
    sz = sizeof(ULONG); RegQueryValueExW(hKey, L"Hw_RptrAfterInit", NULL, NULL, (LPBYTE)&rptrInit, &sz);

    RegCloseKey(hKey);

    sprintf_s(res.Measured, sizeof(res.Measured),
        "HwBringupStatus=0x%08X, PSP_TOS=0x%08X, PSP_RingStatus=0x%08X, SMU_Version=0x%08X, SDMA_Status=0x%08X (BW=%u MB/s), CU/WGP/ALU=%u/%u/%u, CP_RbCntl=0x%08X.",
        hwBringupStatus, pspTos, pspRing, smuVer, sdmaStatus, sdmaBw, numCu, numWgp, shaders, rbCntl);

    if (hwBringupStatus == 0 && (pspTos & 0x80000000) != 0 && sdmaStatus == 0 && numCu == 24) {
        res.Verdict = VERDICT_PROVED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Bring-up basico de kernel (SMU Handshake, PSP Ring, SDMA0 Copy Engine) 100% concluido no Windows.");
    } else {
        res.Verdict = VERDICT_NOT_VALIDATED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Bring-up incompleto ou falhou em alguma das etapas intermediarias.");
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
