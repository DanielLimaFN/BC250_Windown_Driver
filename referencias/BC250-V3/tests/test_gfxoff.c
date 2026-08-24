/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_gfxoff.c

Abstract:
    Prova 9 da regra BC-250: "GFXOFF comprovadamente desativado durante a
    execucao, nao apenas solicitado por mensagem."

    RESULTADO: a prova 9 e' NAO APLICAVEL a este ASIC, e a tentativa anterior
    de satisfaze-la estava ATIVAMENTE DANIFICANDO o silicio.

    ---------------------------------------------------------------------------
    O que a versao anterior fazia

    O KMD chamava Bc250HwDisableGfxOff(), que lia e escrevia em:

        SMN_GFXOFF_FEATURE_LOW   0x03B10528
        SMN_GFXOFF_FEATURE_HIGH  0x03B10998

    Esses enderecos, no nosso proprio wddm_hw.c, sao:

        SMU_Q2_CMD  0x03B10528
        SMU_Q2_ARG  0x03B10998

    O bring-up lia o comando/argumento da fila 2 do mailbox do SMU e depois
    GRAVAVA um valor arbitrario no registrador de COMANDO do SMU. Todo boot
    disparava um comando invalido no mailbox.

    A evidencia estava a' vista nas medicoes: "FEATURE_HIGH" lia 0x00001235,
    que e' o eco do token de teste do SMU (SMU_TEST_ARG 0x1234, resposta
    0x1235). E "FEATURE_LOW" lia 0x00000001 — a resposta "ok" da fila. Nunca
    foi mascara de features.

    E' candidato direto a causa da degradacao da SDMA0, que caiu de 1-3 fences
    para 0 logo depois que essa funcao entrou no bring-up.

    ---------------------------------------------------------------------------
    Por que GFXOFF nao se aplica a Cyan Skillfish

      1. smu_v11_8_ppsmc.h vai ate' PPSMC_MSG_GetEnabledSmuFeatures (0x3D) e
         NAO tem AllowGfxOff nem DisallowGfxOff.
      2. cyan_skillfish_ppt.c do Linux nao tem referencia a gfx_off.
      3. O Linux configura cg_flags = 0 e pg_flags = 0 para cyan skillfish:
         sem clock gating e sem power gating.

    Um mecanismo que nao existe nao pode ser desativado, e tambem nao pode
    interferir na execucao. A prova 9 fica NAO APLICAVEL — justificada por
    essas evidencias, nao satisfeita por um bit lido de um registrador que nao
    decodifica.

--*/

#include "test_verdict.h"

int main(void)
{
    D3DKMT_HANDLE h;
    BC250_WDDM_ESCAPE_COMPUTE_CAPS caps = { 0 };
    int capsOk;

    BcInicio("Prova 9: GFXOFF (nao aplicavel a Cyan Skillfish)");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }

    caps.Header.CommandId = BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS;
    caps.Header.InSize  = sizeof(caps);
    caps.Header.OutSize = sizeof(caps);
    capsOk = CallBc250Escape(h, &caps, sizeof(caps));

    if (!capsOk || caps.SchemaVersion < 2) {
        BcBloquear("KMD nao reporta GfxOffApplicable (SchemaVersion < 2).\n"
                   "            Recompile e reinstale o driver.");
        return BcVeredito();
    }

    printf("--- Fundamentacao documental ---\n");
    BcInfo("smu_v11_8_ppsmc.h    : sem AllowGfxOff / DisallowGfxOff (para em 0x3D)");
    BcInfo("cyan_skillfish_ppt.c : sem nenhuma referencia a gfx_off");
    BcInfo("Linux amdgpu         : cg_flags = 0, pg_flags = 0 para este ASIC");

    printf("\n--- Colisao de enderecos que causou o falso positivo ---\n");
    BcInfo("0x03B10528 foi chamado de GFXOFF_FEATURE_LOW  -> e' SMU_Q2_CMD");
    BcInfo("0x03B10998 foi chamado de GFXOFF_FEATURE_HIGH -> e' SMU_Q2_ARG");
    BcInfo("Leituras medidas: 0x00000001 e 0x00001235 (eco do token 0x1234).");
    BcInfo("Sao respostas de mailbox, nao mascara de features.");

    printf("\n--- Avaliacao ---\n");
    BcCriterio("O KMD deve declarar GFXOFF nao aplicavel e NAO deve afirmar ter\n"
               "            desativado um mecanismo que nao existe neste silicio.");
    BcMedido("GfxOffApplicable relatado pelo KMD: %u", caps.GfxOffApplicable);
    BcMedido("GfxOffDisabled  relatado pelo KMD : %u", caps.GfxOffDisabled);

    BcProva(caps.GfxOffApplicable == 0,
            "KMD declara GFXOFF como nao aplicavel a este ASIC");
    BcProva(caps.GfxOffDisabled == 0,
            "KMD nao afirma ter desativado um mecanismo inexistente");

    printf("\n            Prova 9 da regra BC-250: NAO APLICAVEL, justificada.\n");
    printf("            Isto NAO dispensa as demais provas — apenas esta.\n");

    return BcVeredito();
}
