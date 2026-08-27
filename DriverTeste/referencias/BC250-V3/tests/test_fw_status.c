/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_fw_status.c

Abstract:
    Teste 3: estado real do PSP e dos microcodigos.

    Le do registro os codigos de resposta CRUS do PSP por tipo de firmware,
    expostos pela Fase 0 (item 0.7). Antes disso o driver so' publicava um
    NTSTATUS generico, que colapsava no mesmo valor tres coisas muito
    diferentes:

      0xFFFF0008  TEE_ERROR_ITEM_NOT_FOUND   o SOS nao tem esse item de firmware
      0x00000100  PSP_ERR_UNKNOWN_COMMAND    o SOS nao implementa o comando
      timeout                                 o ring nao respondeu

    A distincao importa: as duas primeiras sao respostas do SOS por um canal que
    FUNCIONA, e nao bugs nossos. Na BC-250 o SOS aceita LOAD_IP_FW apenas para
    fw_type 18 (SMU); CP/RLC/SDMA sao carregados por bootloader/VBIOS no boot,
    exatamente como no Linux (psp_v11_0_8.c nao tem load_ip_fw).

--*/

#include "test_verdict.h"

typedef struct {
    const char *nome;
    const char *chave;
    ULONG fwType;
} FW_ENTRY;

static const FW_ENTRY g_Fw[] = {
    { "SDMA0",  "Hw_Fw_SDMA0", 9 },
    { "RLC_G",  "Hw_Fw_RLC",   8 },
    { "CP_CE",  "Hw_Fw_CE",    3 },
    { "CP_PFP", "Hw_Fw_PFP",   2 },
    { "CP_ME",  "Hw_Fw_ME",    1 },
    { "CP_MEC", "Hw_Fw_MEC",   4 },
    { "CP_MEC2","Hw_Fw_MEC2",  5 },
};

static const char *DecodeTee(ULONG resp)
{
    switch (resp) {
    case 0x00000000u: return "TEE_SUCCESS";
    case 0x00000100u: return "PSP_ERR_UNKNOWN_COMMAND";
    case 0xFFFF0008u: return "TEE_ERROR_ITEM_NOT_FOUND";
    case 0xFFFF000Cu: return "TEE_ERROR_NOT_SUPPORTED";
    default:          return "desconhecido";
    }
}

/* Le um DWORD do ramo Parameters do servico bc250v4. */
static int LerReg(const char *nome, ULONG *out)
{
    static const char *caminhos[] = {
        "SYSTEM\\CurrentControlSet\\Services\\bc250v4\\Parameters",
        "SYSTEM\\CurrentControlSet\\Services\\bc250v4",
    };
    int i;
    for (i = 0; i < 2; i++) {
        HKEY k;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, caminhos[i], 0, KEY_READ, &k) == ERROR_SUCCESS) {
            DWORD tipo = 0, val = 0, cb = sizeof(val);
            LONG r = RegQueryValueExA(k, nome, NULL, &tipo, (LPBYTE)&val, &cb);
            RegCloseKey(k);
            if (r == ERROR_SUCCESS && tipo == REG_DWORD) { *out = val; return 1; }
        }
    }
    return 0;
}

int main(void)
{
    D3DKMT_HANDLE h;
    BC250_WDDM_ESCAPE_COMPUTE_CAPS caps = { 0 };
    ULONG pspRing = 0, tmr = 0, c2p64, c2p81, c2p35;
    int i, n, aceitos = 0, comResposta = 0, semSchema = 0;

    BcInicio("Teste 3: PSP e Microcodigos (codigos TEE crus)");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }

    printf("--- Mailbox do PSP (leitura direta de MMIO) ---\n");
    c2p64 = BcRegLer(h, T_PSP_C2PMSG_64);
    c2p81 = BcRegLer(h, T_PSP_C2PMSG_81);
    c2p35 = BcRegLer(h, 0x5818Cu);
    BcMedido("C2PMSG_64 = 0x%08X  (bit31 = TOS pronto, 0x0002 = eco do ring KM)", c2p64);
    BcMedido("C2PMSG_81 = 0x%08X  (nao-zero = SOS vivo)", c2p81);
    BcMedido("C2PMSG_35 = 0x%08X  (0xFFFFFFFF = bootloader concluiu)", c2p35);

    BcCriterio("O anel GPCOM do PSP precisa estar criado e o SOS vivo.");
    BcProva((c2p64 & 0x80000000u) != 0, "TOS pronto (C2PMSG_64 bit31)");
    BcProva(c2p81 != 0 && !BcEhFillPattern(c2p81), "SOS vivo (C2PMSG_81 nao-zero)");

    printf("\n--- Estado do anel e do TMR (registro) ---\n");
    if (LerReg("Hw_PspRing", &pspRing)) BcMedido("Hw_PspRing = 0x%08X %s", pspRing, pspRing == 0 ? "(sucesso)" : "");
    if (LerReg("Hw_Tmr", &tmr))         BcMedido("Hw_Tmr     = 0x%08X %s", tmr, tmr == 0 ? "(sucesso)" : "");

    printf("\n--- LOAD_IP_FW por tipo de firmware ---\n");
    BcCriterio("Todo microcodigo do CP precisa ser aceito para que exista\n"
               "            qualquer caminho de execucao de shader.");

    n = (int)(sizeof(g_Fw) / sizeof(g_Fw[0]));
    for (i = 0; i < n; i++) {
        char chaveResp[64], chaveSeen[64];
        ULONG nts = 0, resp = 0, seen = 0;
        int temNts, temResp, temSeen;

        sprintf_s(chaveResp, sizeof(chaveResp), "%s_TeeResp", g_Fw[i].chave);
        sprintf_s(chaveSeen, sizeof(chaveSeen), "%s_Seen", g_Fw[i].chave);

        temNts  = LerReg(g_Fw[i].chave, &nts);
        temResp = LerReg(chaveResp, &resp);
        temSeen = LerReg(chaveSeen, &seen);

        /*
          Chave ausente NAO e' o mesmo que valor zero. Um driver anterior a'
          Fase 0 nao publica *_TeeResp; tratar a ausencia como 0x00000000
          faria o teste imprimir TEE_SUCCESS para firmware que nunca carregou.
        */
        {
            char sNts[24], sTee[24];
            if (temNts)  sprintf_s(sNts, sizeof(sNts), "0x%08X", nts);
            else         strcpy_s(sNts, sizeof(sNts), "(ausente)");
            if (temResp) sprintf_s(sTee, sizeof(sTee), "0x%08X", resp);
            else         strcpy_s(sTee, sizeof(sTee), "(ausente)");

            printf("            %-7s (fw_type %2u)  NTSTATUS=%-12s TEE=%-12s %-26s %s\n",
                   g_Fw[i].nome, g_Fw[i].fwType, sNts, sTee,
                   temResp ? DecodeTee(resp) : "codigo TEE nao publicado",
                   (temSeen && seen) ? "" : "(sem resposta registrada)");
        }

        if (!temResp) semSchema++;
        if (temSeen && seen) comResposta++;
        if (temNts && nts == 0) aceitos++;
    }

    if (semSchema == n) {
        printf("\n");
        BcBloquear("O driver carregado nao publica os codigos TEE (item 0.7 da\n"
                   "            Fase 0). Recompile e reinstale o KMD para obter o\n"
                   "            diagnostico real do PSP.");
        return BcVeredito();
    }

    caps.Header.CommandId = BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS;
    caps.Header.InSize  = sizeof(caps);
    caps.Header.OutSize = sizeof(caps);
    if (CallBc250Escape(h, &caps, sizeof(caps))) {
        printf("\n");
        BcMedido("FirmwareReady relatado pelo KMD: %s",
                 caps.FirmwareReady ? "sim" : "nao");
    }

    printf("\n--- Avaliacao ---\n");
    BcMedido("Microcodigos aceitos: %d de %d   (com resposta do PSP: %d)",
             aceitos, n, comResposta);
    BcProva(comResposta > 0,
            "O anel GPCOM entrega comandos e o PSP responde");
    BcProva(aceitos == n,
            "Prova 1: TODOS os microcodigos do CP foram aceitos");

    if (aceitos < n) {
        printf("\n            Diagnostico: LOAD_IP_FW via anel do PSP nao e' o caminho\n");
        printf("            para CP/RLC/SDMA nesta placa. O SOS aceita apenas\n");
        printf("            fw_type 18 (SMU). Caminhos alternativos a investigar:\n");
        printf("              1. mailbox direto C2PMSG_35/36/37 (base 0x58000)\n");
        printf("              2. IC_BASE DMA - com o CP OBRIGATORIAMENTE halted\n");
        printf("            Ver Fase 3 em ANALISE_REFERENCIAS_E_PLANO.md.\n");
    }

    return BcVeredito();
}
