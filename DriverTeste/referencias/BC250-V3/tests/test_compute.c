/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_compute.c

Abstract:
    Teste 6: GPGPU Compute Dispatch.

    Fase 0: reescrito. A versao anterior imprimia
    "OS 1536 SHADERS DISPARAM E COMPUTAM COM SUCESSO!" com:
      - GRBM_STATUS identico antes e depois,
      - duracao de 0 microssegundos,
      - "fence" que era um contador incrementado pela CPU,
      - e o resultado numerico produzido por um loop de soma em CPU dentro do
        proprio driver (removido em wddm_hw.c na Fase 0).

    Agora o veredito vem da evidencia devolvida pelo KMD: fence pre-envenenada
    em VRAM, valor observado, e mudanca de GRBM.

--*/

#include "test_verdict.h"

int main(void)
{
    D3DKMT_HANDLE h;
    BC250_WDDM_ESCAPE_COMPUTE_CAPS caps = { 0 };
    BC250_WDDM_ESCAPE_COMPUTE_DISPATCH disp = { 0 };
    int capsOk;

    BcInicio("Teste 6: GPGPU Compute Dispatch");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }

    /*-----------------------------------------------------------------------
      Configuracao detectada, com procedencia explicita
    -----------------------------------------------------------------------*/
    caps.Header.CommandId = BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS;
    caps.Header.InSize  = sizeof(caps);
    caps.Header.OutSize = sizeof(caps);
    capsOk = CallBc250Escape(h, &caps, sizeof(caps));

    if (capsOk && caps.SchemaVersion == 0) {
        BcBloquear("Driver anterior a' Fase 0 (SchemaVersion=0): nao expoe fence\n"
                   "            observada nem procedencia. Recompile e reinstale o KMD.");
        return BcVeredito();
    }

    printf("--- Configuracao detectada ---\n");
    if (capsOk) {
        BcMedido("Compute Units    : %u   [%s]", caps.NumCu,
                 caps.CuCountSource == 0 ? "DETECTADO no silicio" : "ASSUMIDO (baseline)");
        BcMedido("WGPs             : %u", caps.NumWgp);
        BcMedido("Stream Processors: %u   [%s]", caps.NumTotalShaders,
                 caps.CuCountSource == 0 ? "DETECTADO" : "ASSUMIDO");
        BcMedido("SPI_PG cru       : 0x%08X", caps.WgpMaskRaw);
        BcMedido("Clock GFX        : %u MHz (telemetria SMU)", caps.GpuClockMhz);
        BcMedido("VRAM mapeada     : %llu MB   [%s]",
                 (unsigned long long)(caps.VramTotalBytes / (1024ULL * 1024ULL)),
                 caps.VramSizeSource == 0 ? "DETECTADO pela BAR" : "ASSUMIDO");
        BcMedido("Firmware do CP   : %s",
                 caps.FirmwareReady ? "ACEITO" : "NAO ACEITO pelo PSP");
    } else {
        BcBloquear("Consulta de capacidades falhou.");
        return BcVeredito();
    }

    if (!caps.FirmwareReady) {
        BcBloquear("Microcodigo do CP nao foi aceito pelo PSP. Nenhuma engine de\n"
                   "            shader esta inicializada, logo nenhum dispatch pode executar.\n"
                   "            Esta e' a condicao atual desta placa: o SOS da BC-250 responde\n"
                   "            TEE_ERROR_ITEM_NOT_FOUND para CP/RLC/SDMA (so' fw_type 18 e' aceito).");
        return BcVeredito();
    }

    /*-----------------------------------------------------------------------
      Dispatch
    -----------------------------------------------------------------------*/
    printf("\n--- Submissao do dispatch ---\n");
    BcCriterio("A GPU deve escrever a fence esperada em VRAM e o GRBM_STATUS\n"
               "            deve mudar durante a carga.");

    disp.Header.CommandId = BC250_ESCAPE_CMD_COMPUTE_DISPATCH;
    disp.Header.InSize  = sizeof(disp);
    disp.Header.OutSize = sizeof(disp);
    disp.ShaderGpuAddress = caps.ComputeVramBase;
    disp.ThreadsPerGroupX = 64;
    disp.ThreadsPerGroupY = 1;
    disp.ThreadsPerGroupZ = 1;
    disp.DimX = 64;
    disp.DimY = 1;
    disp.DimZ = 1;
    disp.UserDataCount = 0;

    /* O retorno do escape e' apenas o transporte. A prova esta em
       ExecutionProven / FenceObserved, abaixo. */
    (void)CallBc250Escape(h, &disp, sizeof(disp));

    printf("\n--- Evidencia fisica ---\n");
    BcMedido("Status da engine   : 0x%08X", disp.Header.Status);
    BcMedido("Duracao            : %u us", disp.DurationUs);
    BcMedido("Fence GPU addr     : 0x%llX",
             (unsigned long long)disp.FenceGpuAddress);
    BcMedido("Fence esperada     : 0x%08X", disp.FenceValue);
    BcMedido("Fence observada    : 0x%08X", disp.FenceObserved);
    BcMedido("GRBM antes         : 0x%08X", disp.GrbmStatusBefore);
    BcMedido("GRBM depois        : 0x%08X", disp.GrbmStatusAfter);

    BcProva(disp.ExecutionProven == 1,
            "Prova 5: a propria GPU escreveu a fence em memoria");
    BcProva(disp.GrbmChanged == 1,
            "Prova 7: GRBM_STATUS mudou durante a carga");
    BcProva(disp.DurationUs > 0,
            "Duracao mensuravel (0 us indica que nada executou)");

    if (disp.FenceObserved == (ULONG)~disp.FenceValue) {
        printf("\n            Diagnostico: a fence continua com o valor de poison\n");
        printf("            (~esperado). A GPU nao a tocou. O dispatch foi escrito\n");
        printf("            nos registradores COMPUTE_* mas nenhuma onda executou.\n");
    }

    return BcVeredito();
}
