/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_vram_rw.c

Abstract:
    Teste 3 / Integridade VRAM: Leitura e Escrita Direta na GDDR6 via BAR0.
    Utiliza Escape 0xBD08 (VRAM Write) e Escape 0xBD09 (VRAM Read).

    Fase 0: convertido para o harness test_verdict.h. Prova integridade de
    acesso fisico host-to-VRAM sem assercoes triunfalistas de software.

--*/

#include "test_verdict.h"

int main(void)
{
    D3DKMT_HANDLE hAdapter;
    ULONGLONG testOffset = 0x01000000ULL; /* 16 MB offset na VRAM */
    ULONG testSizeDwords = 256;          /* 1024 bytes */
    ULONG testSizeBytes = testSizeDwords * sizeof(ULONG);
    BC250_WDDM_ESCAPE_VRAM_RW writePkt = { 0 };
    BC250_WDDM_ESCAPE_VRAM_RW readPkt = { 0 };
    ULONG i;
    int errors = 0;
    int writeOk, readOk;

    BcInicio("Teste 3: Leitura e Escrita Direta na GDDR6 (VRAM R/W)");

    hAdapter = OpenBc250Adapter();
    if (!hAdapter) {
        BcBloquear("Nao foi possivel abrir o adaptador WDDM via D3DKMT.");
        return BcVeredito();
    }

    printf("--- Preparacao e Gravacao ---\n");
    BcCriterio("Os dados gravados pelo host em VRAM devem ser lidos de volta\n"
               "            com integridade de 100%% (sem corrupcao ou perdas).");

    /* 1. Preparar Padrao: Assinatura CAFEBABE + Sequencia */
    writePkt.Header.CommandId = BC250_ESCAPE_CMD_VRAM_WRITE;
    writePkt.Header.InSize = sizeof(writePkt);
    writePkt.Header.OutSize = sizeof(writePkt);
    writePkt.VramOffset = testOffset;
    writePkt.ByteCount = testSizeBytes;

    writePkt.Data[0] = 0xCAFEBABE;
    writePkt.Data[1] = 0x12345678;
    writePkt.Data[2] = 0xDEADBEEF;
    writePkt.Data[3] = 0xFEEDFACE;
    for (i = 4; i < testSizeDwords; i++) {
        writePkt.Data[i] = (i * 0x01010101u) ^ 0xA5A5A5A5u;
    }

    writeOk = CallBc250Escape(hAdapter, &writePkt, sizeof(writePkt));
    BcMedido("Gravacao de %u bytes em offset 0x%llX: %s",
             testSizeBytes, testOffset, writeOk ? "sucesso" : "FALHOU");

    if (!writeOk) {
        BcBloquear("Falha ao gravar dados na VRAM via escape.");
        return BcVeredito();
    }

    /* 2. Ler de volta da VRAM */
    readPkt.Header.CommandId = BC250_ESCAPE_CMD_VRAM_READ;
    readPkt.Header.InSize = sizeof(readPkt);
    readPkt.Header.OutSize = sizeof(readPkt);
    readPkt.VramOffset = testOffset;
    readPkt.ByteCount = testSizeBytes;

    readOk = CallBc250Escape(hAdapter, &readPkt, sizeof(readPkt));
    BcMedido("Leitura de %u bytes em offset 0x%llX: %s",
             testSizeBytes, testOffset, readOk ? "sucesso" : "FALHOU");

    if (!readOk) {
        BcBloquear("Falha ao ler dados da VRAM via escape.");
        return BcVeredito();
    }

    /* 3. Comparar dados */
    printf("\n--- Verificacao de Integridade ---\n");
    for (i = 0; i < testSizeDwords; i++) {
        if (readPkt.Data[i] != writePkt.Data[i]) {
            if (errors < 5) {
                BcMedido("Inconsistencia DWORD [%u]: Enviado 0x%08X != Lido 0x%08X",
                         i, writePkt.Data[i], readPkt.Data[i]);
            }
            errors++;
        }
    }

    BcMedido("Header Readback : [0]=0x%08X [1]=0x%08X [2]=0x%08X [3]=0x%08X",
             readPkt.Data[0], readPkt.Data[1], readPkt.Data[2], readPkt.Data[3]);
    BcMedido("Divergencias    : %d em %u dwords", errors, testSizeDwords);

    printf("\n--- Avaliacao ---\n");
    BcProva(writeOk != 0, "Escrita direta em VRAM aceita");
    BcProva(readOk != 0, "Leitura direta de VRAM aceita");
    BcProva(errors == 0, "Conteudo lido bate byte a byte com o gravado");
    BcProva(readPkt.Data[0] == 0xCAFEBABE, "Assinatura CAFEBABE confirmada no readback");

    return BcVeredito();
}
