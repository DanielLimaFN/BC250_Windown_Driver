/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_hw_info.c

Abstract:
    Teste 1: estado de silicio e procedencia da configuracao.

    Fase 0: reescrito. A versao anterior terminava com
    "HARDWARE 100% VALIDADO EM SILICIO REAL (24 CUs / 1536 Shaders)!" sem
    verificar nada, e apresentava 24 CUs / 1536 shaders como se tivessem sido
    medidos quando na verdade eram o baseline assumido (SPI_PG lia 0).

    Este teste nao declara aceleracao. Ele reporta estado e a PROCEDENCIA de
    cada numero, e falha se algum valor apresentado como detectado for na
    verdade assumido.

--*/

#include "test_verdict.h"

int main(void)
{
    D3DKMT_HANDLE h;
    BC250_WDDM_ESCAPE_INFO info = { 0 };
    BC250_WDDM_ESCAPE_COMPUTE_CAPS caps = { 0 };
    int infoOk, capsOk;
    ULONG grbm, cpStat;

    BcInicio("Teste 1: Estado de Silicio e Procedencia da Configuracao");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }

    info.Header.CommandId = BC250_ESCAPE_CMD_QUERY_INFO;
    info.Header.InSize  = sizeof(info);
    info.Header.OutSize = sizeof(info);
    infoOk = CallBc250Escape(h, &info, sizeof(info));

    caps.Header.CommandId = BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS;
    caps.Header.InSize  = sizeof(caps);
    caps.Header.OutSize = sizeof(caps);
    capsOk = CallBc250Escape(h, &caps, sizeof(caps));

    if (!infoOk || !capsOk) {
        BcBloquear("Consulta de estado ao KMD falhou.");
        return BcVeredito();
    }

    /*
      Version skew: um KMD anterior a' Fase 0 nao preenche os campos de
      procedencia, e eles chegam zerados — indistinguiveis de "DETECTADO".
      Interpretar isso como deteccao seria exatamente o falso positivo que a
      Fase 0 existe para eliminar.
    */
    if (caps.SchemaVersion == 0) {
        BcBloquear("O driver carregado e' anterior a' Fase 0 (SchemaVersion=0):\n"
                   "            ele nao reporta a procedencia dos valores. Recompile e\n"
                   "            reinstale o KMD antes de confiar nestes numeros.");
        printf("\n            Valores brutos do driver antigo (SEM procedencia):\n");
        printf("              NumCu=%u  NumWgp=%u  Shaders=%u  VRAM=%llu MB\n",
               caps.NumCu, caps.NumWgp, caps.NumTotalShaders,
               (unsigned long long)(caps.VramTotalBytes / (1024ULL * 1024ULL)));
        return BcVeredito();
    }

    printf("--- Identificacao ---\n");
    BcMedido("Device ID          : 0x%04X", info.DeviceId);
    BcMedido("Hardware init      : %s", info.HardwareInitialized ? "sim" : "nao");

    printf("\n--- Configuracao: valor + procedencia ---\n");
    BcCriterio("Todo numero de configuracao deve vir rotulado como DETECTADO\n"
               "            ou ASSUMIDO. A regra BC-250 proibe fixar esses valores.");
    BcMedido("Compute Units      : %u        [%s]", caps.NumCu,
             caps.CuCountSource == 0 ? "DETECTADO" : "ASSUMIDO");
    BcMedido("WGPs               : %u        [%s]", caps.NumWgp,
             caps.CuCountSource == 0 ? "DETECTADO" : "ASSUMIDO");
    BcMedido("Stream Processors  : %u      [%s]", caps.NumTotalShaders,
             caps.CuCountSource == 0 ? "DETECTADO" : "ASSUMIDO");
    BcMedido("SPI_PG cru (0x935C): 0x%08X", caps.WgpMaskRaw);
    BcMedido("VRAM mapeada       : %llu MB   [%s]",
             (unsigned long long)(caps.VramTotalBytes / (1024ULL * 1024ULL)),
             caps.VramSizeSource == 0 ? "DETECTADO pela BAR" : "ASSUMIDO");
    BcMedido("Clock GFX          : %u MHz    [telemetria SMU]", caps.GpuClockMhz);

    if (caps.SchemaVersion >= 2) {
        BcMedido("CPU Cores ativos   : %u        [%s]", caps.CpuCoresActive,
                 caps.CpuCoresActive ? "DETECTADO pelo SO" : "ASSUMIDO");
        BcMedido("ReBAR              : %s        [DETECTADO pela BAR0]",
                 caps.ReBarActive ? "ATIVO (16 GB)" : "INATIVO (256 MB abertura)");
        BcMedido("Memoria GTT        : %llu MB      [%s]",
                 (unsigned long long)(caps.GttSizeBytes / (1024ULL * 1024ULL)),
                 caps.GttSizeSource == 0 ? "DETECTADO" : "ASSUMIDO");
        /* O KMD marca GfxOffDisabled a partir de um readback do SMN. Enquanto
           a leitura do registrador nao for plausivel (ver test_gfxoff), isso e'
           leitura de registrador, nao prova. Nao rotular como PROVADO aqui. */
        BcMedido("GFXOFF             : %s   [ver test_gfxoff]",
                 caps.GfxOffApplicable ? "aplicavel" : "NAO APLICAVEL a este ASIC");
    }

    printf("\n--- Estado das engines ---\n");
    grbm   = BcRegLer(h, T_GRBM_STATUS);
    cpStat = BcRegLer(h, T_CP_STAT);
    BcMedido("GRBM_STATUS        : 0x%08X%s", grbm,
             BcEhFillPattern(grbm) ? "  <<< FILL / OFFSET INVALIDO" : "");
    BcMedido("CP_STAT            : 0x%08X%s", cpStat,
             BcEhFillPattern(cpStat) ? "  <<< FILL / OFFSET INVALIDO" : "");
    BcMedido("CP_ME_CNTL         : 0x%08X", BcRegLer(h, T_CP_ME_CNTL));
    BcMedido("CP_MEC_CNTL        : 0x%08X", BcRegLer(h, T_CP_MEC_CNTL));
    BcMedido("CP Ring WPTR/RPTR  : 0x%08X / 0x%08X", info.GfxWptr, info.GfxRptr);
    BcMedido("Firmware do CP     : %s",
             caps.FirmwareReady ? "ACEITO" : "NAO ACEITO pelo PSP");
    BcMedido("SDMA0 copias       : %u", info.SdmaCopyCount);

    printf("\n--- Avaliacao ---\n");
    BcProva(!BcEhFillPattern(grbm) && !BcEhFillPattern(cpStat),
            "Offsets de GRBM_STATUS e CP_STAT decodificam (nao sao fill)");
    BcProva(caps.VramSizeSource == 0,
            "Tamanho de VRAM foi detectado pela BAR, nao assumido");
    BcProva(caps.CuCountSource == 0,
            "Contagem de CU foi detectada no silicio, nao assumida");
    if (caps.SchemaVersion >= 2) {
        BcProva(caps.CpuCoresActive > 0,
                "Nucleos fisicos/ativos da CPU foram detectados no SO");
    }
    BcProva(caps.FirmwareReady != 0,
            "Prova 1: microcodigo aceito e engine inicializada");

    printf("\n            Nota: este teste NAO declara aceleracao. Um adaptador\n");
    printf("            enumerado e registradores legiveis nao provam execucao.\n");

    return BcVeredito();
}
