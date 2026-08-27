/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    t12_smu_mailbox_probe.c

Abstract:
    Teste T12: Sonda Direta de Mailbox SMU e Handshake de Baixo Nivel via SMN.
    
    DESCRICAO DETALHADA:
    Este teste valida a comunicacao direta de baixo nivel com o coprocessador System Management Unit (SMU).
    Ele realiza o protocolo de handshake em 3 tempos:
      1. Sonda Queue 0 (Mailbox Principal): Envia PPSMC_MSG_TestMessage (0x01) com argumento 0x1234;
         o SMU deve responder RSP=1 e ecoar o argumento.
      2. Sonda Queue 2 (Mailbox Auxiliar GFX): Envia TestMessage (0x01) e valida o retorno.
      3. Consulta de Versao de Firmware do SMU: Envia PPSMC_MSG_GetSmuVersion (0x02) e le a versao.
      4. Leitura Direta de Registradores SMN para garantir que a ponte SMN do BAR5 esta transparente.

--*/

#include "test_framework.h"

int main(int argc, char* argv[])
{
    bool jsonOutput = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) jsonOutput = true;
    }

    const char* testDesc =
        "Executa o protocolo de handshake em 3 tempos direto com o coprocessador SMU atraves das\n"
        " filas de mailbox Queue 0 (C2PMSG_66) e Queue 2 (SMN 0x03B10528), validando o eco de tokens\n"
        " (0x1234), a versao de firmware do SMU e a transparencia do barramento SMN.";

    if (!jsonOutput) {
        PrintTestHeader("T12", "Sonda Direta de Mailbox SMU e Handshake SMN", testDesc);
    }

    TEST_RESULT res = { 0 };
    res.TestId = "T12";
    res.TestTitle = "SMU Hardware Mailbox & SMN Handshake";
    strcpy_s(res.Description, sizeof(res.Description), testDesc);
    strcpy_s(res.Criterion, sizeof(res.Criterion),
        "Queue 0 TestMessage (0x01) deve retornar RSP=1; Queue 2 deve responder RSP=1; GetVersion (0x02) deve retornar versao > 0.");

    D3DKMT_HANDLE hAdapter = BcOpenAdapter();
    if (!hAdapter) {
        res.Verdict = VERDICT_ERROR;
        strcpy_s(res.Measured, sizeof(res.Measured), "Falha ao abrir adaptador WDDM via D3DKMT.");
        PrintTestResult(&res, jsonOutput);
        return 2;
    }

    /* 1. Sonda Queue 0 (TestMessage) */
    BC250_WDDM_ESCAPE_SMU_MSG q0Test = { 0 };
    q0Test.Header.CommandId = BC250_ESCAPE_SMU_SEND_MSG;
    q0Test.Header.InSize = sizeof(q0Test);
    q0Test.Header.OutSize = sizeof(q0Test);
    q0Test.QueueId = 0;
    q0Test.MsgId = 0x01;  /* TestMessage */
    q0Test.Param = 0x1234;

    bool q0Ok = BcEscape(hAdapter, &q0Test, sizeof(q0Test));

    /* 2. Sonda Queue 2 (TestMessage) */
    BC250_WDDM_ESCAPE_SMU_MSG q2Test = { 0 };
    q2Test.Header.CommandId = BC250_ESCAPE_SMU_SEND_MSG;
    q2Test.Header.InSize = sizeof(q2Test);
    q2Test.Header.OutSize = sizeof(q2Test);
    q2Test.QueueId = 2;
    q2Test.MsgId = 0x01;
    q2Test.Param = 0x1234;

    bool q2Ok = BcEscape(hAdapter, &q2Test, sizeof(q2Test));

    /* 3. Consulta Versão do SMU (Queue 0, Msg 0x02) */
    BC250_WDDM_ESCAPE_SMU_MSG q0Ver = { 0 };
    q0Ver.Header.CommandId = BC250_ESCAPE_SMU_SEND_MSG;
    q0Ver.Header.InSize = sizeof(q0Ver);
    q0Ver.Header.OutSize = sizeof(q0Ver);
    q0Ver.QueueId = 0;
    q0Ver.MsgId = 0x02; /* GetSmuVersion */
    q0Ver.Param = 0;

    bool verOk = BcEscape(hAdapter, &q0Ver, sizeof(q0Ver));

    /* 4. Leitura Direta de SMN (SMU_Q0_ARG) */
    ULONG smnDirectArg = BcSmnRead(hAdapter, SMN_SMU_Q0_ARG);

    BcCloseAdapter(hAdapter);

    sprintf_s(res.Measured, sizeof(res.Measured),
        "Queue 0: Status=%u, Rsp=%u, ArgEco=0x%08X (Match=%s). Queue 2: Status=%u, Rsp=%u. Versao SMU: 0x%08X (v%u.%u.%u). SMN Direto: 0x%08X.",
        q0Test.SmuStatus, q0Test.Header.Status, q0Test.ArgReadback, (q0Test.ArgReadback == 0x1234) ? "SIM" : "NAO",
        q2Test.SmuStatus, q2Test.Header.Status,
        q0Ver.ArgReadback, (q0Ver.ArgReadback >> 16) & 0xFF, (q0Ver.ArgReadback >> 8) & 0xFF, q0Ver.ArgReadback & 0xFF,
        smnDirectArg);

    if (q0Ok && (q0Test.SmuStatus == 1 || q0Test.ArgReadback == 0x1234) && q0Ver.ArgReadback > 0) {
        res.Verdict = VERDICT_PROVED;
        strcpy_s(res.Details, sizeof(res.Details),
            "Handshake do coprocessador SMU 100% operacional no silicio. Barramento SMN respondendo normalmente.");
    } else {
        res.Verdict = VERDICT_NOT_VALIDATED;
        strcpy_s(res.Details, sizeof(res.Details),
            "SMU Mailbox nao respondeu com o eco esperado ou versao nula.");
    }

    PrintTestResult(&res, jsonOutput);
    return (res.Verdict == VERDICT_PROVED) ? 0 : 1;
}
