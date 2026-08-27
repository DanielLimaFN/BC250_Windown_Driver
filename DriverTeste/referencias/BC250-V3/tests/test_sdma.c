/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_sdma.c

Abstract:
    Teste 4: engine de copia fisica SDMA0.

    Fase 0 e Fase 1: reescrito para declarar criterio e derivar veredito de provas.

    A estrutura do caminho SDMA em wddm_hw.c e' correta: fence pre-envenenada
    com ~valor, pacote COPY+FENCE num ring em VRAM, polling do RPTR e da fence.
    O destino recebe um POISON (0x5C) e o conteudo e' verificado byte a byte.

    Resultado medido nesta placa (2026-08-22, KMD Fase 0/1):
      - Nas primeiras submissoes (~1 a 3), a GPU escreve a fence e consome o ring,
        mas a copia de dados e' intermitente/incompleta;
      - A partir da 4a submissao, a engine entra em estado de falha latchado:
        o RPTR pode alcancar o WPTR, mas a GPU nao escreve mais a fence (0/20 fences)
        e nenhum byte e' transferido (poison intacto).

    Este teste executa o diagnostico CONST_FILL antes do estresse para avaliar a
    engine em estado fresco e separa movimentacao de dados de execucao de fence.

--*/

#include "test_verdict.h"

#define SRC_OFFSET   0x01000000u   /* 16 MB */
#define DST_OFFSET   0x02000000u   /* 32 MB */
#define COPY_BYTES   4096u   /* igual ao bloco verificado byte a byte */
#define PATTERN_SEED 0x9E3779B9u
#define TENTATIVAS_COPIA 20u

/* Usa a estrutura canonica de test_common.h: VramOffset e' ULONGLONG e
   Data e' ULONG[1024]. Uma copia local com layout diferente faz o KMD
   rejeitar o escape por tamanho/offset invalido. */
typedef BC250_WDDM_ESCAPE_VRAM_RW BC_ESC_VRAM;

static int VramGravar(D3DKMT_HANDLE h, ULONG offset, const UCHAR *src, ULONG n)
{
    BC_ESC_VRAM rw = { 0 };
    rw.Header.CommandId = BC250_ESCAPE_CMD_VRAM_WRITE;
    rw.Header.InSize  = sizeof(rw);
    rw.Header.OutSize = sizeof(rw);
    rw.VramOffset = offset;
    rw.ByteCount  = n;
    memcpy(rw.Data, src, n);
    return CallBc250Escape(h, &rw, sizeof(rw));
}

static int VramLer(D3DKMT_HANDLE h, ULONG offset, UCHAR *dst, ULONG n)
{
    BC_ESC_VRAM rw = { 0 };
    rw.Header.CommandId = BC250_ESCAPE_CMD_VRAM_READ;
    rw.Header.InSize  = sizeof(rw);
    rw.Header.OutSize = sizeof(rw);
    rw.VramOffset = offset;
    rw.ByteCount  = n;
    if (!CallBc250Escape(h, &rw, sizeof(rw))) return 0;
    memcpy(dst, rw.Data, n);
    return 1;
}

int main(void)
{
    D3DKMT_HANDLE h;
    BC250_WDDM_ESCAPE_SDMA_COPY cp = { 0 };
    UCHAR padrao[4096], lido[4096], zeros[4096];
    ULONG i;
    int conteudoOk, origemOk, fillOk = 0;
    ULONG fillBytes = 0;
    ULONG t, copiasOk = 0, fencesOk = 0;
    int falhasDetalhadas = 0, escOk = 0, escFalhas = 0;

    BcInicio("Teste 4: Engine de Copia Fisica SDMA0");

    h = BcAbrirAdaptadorVivo();
    if (h == 0) {
        BcBloquear("Adaptador WDDM indisponivel ou nao responde. Se um teste\n"
                   "            anterior estourou o TDR, o handle esta morto: reinicie\n"
                   "            a bateria. Nao ha' medicao valida neste estado.");
        return BcVeredito();
    }

    /* Padrao pseudo-aleatorio deterministico: um memset nao distinguiria
       uma copia real de um destino que ja estivesse zerado. */
    for (i = 0; i < sizeof(padrao); i++) {
        padrao[i] = (UCHAR)(((i * PATTERN_SEED) >> 13) ^ (i * 31u) ^ 0xA5u);
    }
    /* T5 - poison: o destino recebe um marcador distinto, nao zeros. Assim
       "a DMA escreveu zeros" fica distinguivel de "a DMA nao escreveu nada". */
    memset(zeros, 0x5C, sizeof(zeros));

    printf("--- Preparacao ---\n");
    BcCriterio("Origem recebe padrao conhecido; destino recebe poison 0x5C.");
    if (!VramGravar(h, SRC_OFFSET, padrao, sizeof(padrao)) ||
        !VramGravar(h, DST_OFFSET, zeros, sizeof(zeros))) {
        BcBloquear("Escrita direta em VRAM falhou.");
        return BcVeredito();
    }
    if (!VramLer(h, DST_OFFSET, lido, sizeof(lido))) {
        BcBloquear("Leitura de VRAM falhou.");
        return BcVeredito();
    }
    BcMedido("Destino antes da copia (poison): 0x%02X 0x%02X 0x%02X 0x%02X (esperado 0x5C)",
             lido[0], lido[1], lido[2], lido[3]);

    /*
      Verificar a ORIGEM antes de copiar separa duas falhas muito diferentes:
      "a escrita do host nao chegou na VRAM" e "a SDMA nao moveu os bytes".
      Sem esta checagem, um destino zerado seria atribuido erradamente a' DMA.
    */
    if (!VramLer(h, SRC_OFFSET, lido, sizeof(lido))) {
        BcBloquear("Leitura da origem falhou.");
        return BcVeredito();
    }
    origemOk = (memcmp(lido, padrao, sizeof(padrao)) == 0);
    BcMedido("Origem apos escrita   : 0x%02X 0x%02X 0x%02X 0x%02X  (padrao 0x%02X 0x%02X 0x%02X 0x%02X)  %s",
             lido[0], lido[1], lido[2], lido[3],
             padrao[0], padrao[1], padrao[2], padrao[3],
             origemOk ? "[OK]" : "[DIVERGE]");
    if (!origemOk) {
        BcBloquear("O padrao nao esta presente na origem: a escrita do host em\n"
                   "            VRAM nao persistiu. Nao da' para julgar a SDMA assim.");
        return BcVeredito();
    }

    /*-----------------------------------------------------------------------
      Diagnostico: CONST_FILL separa duas hipoteses

      O FILL faz a GPU escrever um padrao gerado por ela mesma — nao depende de
      ler nenhum dado que o host tenha posto na VRAM. Executado antes das 20
      copias para testar a engine em estado fresco.
    -----------------------------------------------------------------------*/
    printf("\n--- Diagnostico: CONST_FILL ---\n");
    BcCriterio("Se o motor executa movimentacao de dados, o FILL deve trocar o\n"
               "            poison 0x5C por 0x77 no destino.");
    {
        BC250_WDDM_ESCAPE_SDMA_FILL fl = { 0 };
        UCHAR esperadoFill[4096];

        memset(zeros, 0x5C, sizeof(zeros));
        (void)VramGravar(h, DST_OFFSET, zeros, sizeof(zeros));

        fl.Header.CommandId = BC250_ESCAPE_SDMA_FILL;
        fl.Header.InSize  = sizeof(fl);
        fl.Header.OutSize = sizeof(fl);
        fl.DstGpuAddress = DST_OFFSET;
        fl.SizeBytes     = COPY_BYTES;
        fl.Pattern       = 0x77777777u;

        (void)CallBc250Escape(h, &fl, sizeof(fl));
        BcMedido("Escape SDMA_FILL: status da engine = 0x%08X", fl.Header.Status);
        {
            memset(esperadoFill, 0x77, sizeof(esperadoFill));
            /*
              Contar bytes com o padrao em vez de exigir memcmp exato.

              A corrida anterior imprimiu "Nem o FILL escreve" e, tres linhas
              abaixo, a primeira tentativa de copia leu 'destino 0x77' — o
              padrao do FILL. Ou seja: o FILL TINHA escrito, e a verificacao
              por memcmp-tudo-ou-nada leu cedo demais e concluiu o oposto.

              Um FILL parcial e' informacao valiosa (aponta para o campo de
              contagem), e um FILL completo prova que o motor executa
              movimentacao de dados. Zero é o unico caso que prova o contrario.
            */
            fillOk = 0;
            fillBytes = 0;
            for (i = 0; i < 100; i++) {
                ULONG k, n77 = 0;
                if (!VramLer(h, DST_OFFSET, lido, sizeof(lido))) break;
                for (k = 0; k < sizeof(lido); k++) if (lido[k] == 0x77u) n77++;
                if (n77 > fillBytes) fillBytes = n77;
                if (memcmp(lido, esperadoFill, sizeof(esperadoFill)) == 0) { fillOk = 1; break; }
                Sleep(5);
            }
            BcMedido("Destino apos FILL: 0x%02X 0x%02X 0x%02X 0x%02X  (esperado 0x77)",
                     lido[0], lido[1], lido[2], lido[3]);
            BcMedido("Bytes com o padrao 0x77: %u de %u",
                     fillBytes, (ULONG)sizeof(lido));

            if (fillOk) {
                BcInfo("FILL COMPLETO. O motor SDMA EXECUTA movimentacao de dados;");
                BcInfo("o COPY falha especificamente na leitura da origem.");
            } else if (fillBytes > 0) {
                BcInfo("FILL PARCIAL (%u bytes). O motor executa o pacote mas nao", fillBytes);
                BcInfo("completa a transferencia — suspeita no campo de contagem.");
            } else {
                BcInfo("FILL nao escreveu nenhum byte. Ou o motor so' processa a");
                BcInfo("FENCE, ou ja' estava latchado antes deste teste comecar.");
            }
        }
    }

    printf("\n--- Copia por DMA: %u tentativas ---\n", TENTATIVAS_COPIA);
    BcCriterio("A SDMA0 deve completar a copia com fence escrita pela GPU e o\n"
               "            conteudo do destino deve bater byte a byte com a\n"
               "            origem, em TODAS as tentativas. Intermitencia nao serve.");

    /*
      Uma unica amostra nao caracteriza esta engine: a mesma copia foi observada
      passando e falhando em execucoes consecutivas, com os caminhos de coerencia
      do host corretos nos dois casos (barreira + HDP flush na escrita, HDP
      invalidate na leitura). Repetir e reportar a taxa e' a leitura honesta.
    */
    for (t = 0; t < TENTATIVAS_COPIA; t++) {
        int okAgora = 0;

        memset(zeros, 0x5C, sizeof(zeros));
        if (!VramGravar(h, DST_OFFSET, zeros, sizeof(zeros))) continue;
        if (!VramLer(h, DST_OFFSET, lido, sizeof(lido)) || memcmp(lido, zeros, sizeof(zeros)) != 0) {
            BcMedido("[%02u] poison nao assentou no destino; tentativa descartada", t + 1);
            continue;
        }

        memset(&cp, 0, sizeof(cp));
        cp.Header.CommandId = BC250_ESCAPE_SDMA_COPY;
        cp.Header.InSize  = sizeof(cp);
        cp.Header.OutSize = sizeof(cp);
        cp.SrcGpuAddress = SRC_OFFSET;
        cp.DstGpuAddress = DST_OFFSET;
        cp.SizeBytes     = COPY_BYTES;
        /* O veredito vem do PAYLOAD, nao do retorno do escape: o escape so'
           transporta a medicao. Header.Status carrega o status real da engine. */
        escOk = CallBc250Escape(h, &cp, sizeof(cp));
        if (!escOk) escFalhas++;
        if (t == 0 || escFalhas <= 2) {
            BcMedido("[%02u] transporte=%s engine=0x%08X fenceWritten=%u rptrOnly=%u",
                     t + 1, escOk ? "ok" : "FALHOU", cp.Header.Status,
                     cp.FenceWritten, cp.CompletedByRptrOnly);
        }

        if (cp.FenceWritten) fencesOk++;

        for (i = 0; i < 25; i++) {
            if (!VramLer(h, DST_OFFSET, lido, sizeof(lido))) break;
            if (memcmp(lido, padrao, sizeof(padrao)) == 0) { okAgora = 1; break; }
            Sleep(2);
        }

        if (okAgora) {
            copiasOk++;
        } else if (falhasDetalhadas < 3) {
            ULONG k, iguais = 0, dif = 0xFFFFFFFFu;
            for (k = 0; k < sizeof(padrao); k++) {
                if (lido[k] == padrao[k]) iguais++;
                else if (dif == 0xFFFFFFFFu) dif = k;
            }
            if (dif == 0xFFFFFFFFu) dif = 0;
            BcMedido("[%02u] FALHOU: %u/%u bytes corretos, 1a divergencia no offset %u "
                     "(destino 0x%02X, esperado 0x%02X)",
                     t + 1, iguais, (ULONG)sizeof(padrao), dif, lido[dif], padrao[dif]);
            falhasDetalhadas++;
        }
    }

    conteudoOk = (copiasOk == TENTATIVAS_COPIA);

    BcMedido("Copias corretas : %u de %u", copiasOk, TENTATIVAS_COPIA);
    BcMedido("Fences escritas : %u de %u", fencesOk, TENTATIVAS_COPIA);
    BcMedido("Ultima duracao  : %u us  (banda relatada %u MB/s)",
             cp.DurationUs, cp.BandwidthMBs);
    BcMedido("RPTR ultimo     : antes=0x%08X depois=0x%08X (alvo 0x%08X)",
             cp.RptrBefore, cp.RptrAfter, cp.WptrTarget);
    BcMedido("Fence ultima    : addr=0x%llX esperada=0x%08X observada=0x%08X",
             (unsigned long long)cp.FenceGpuAddress, cp.FenceValue, cp.FenceObserved);

    if (copiasOk > 0 && copiasOk < TENTATIVAS_COPIA) {
        BcInfo("A copia e' INTERMITENTE. Os caminhos de coerencia do host estao");
        BcInfo("corretos nos dois casos, entao a instabilidade esta na execucao");
        BcInfo("da engine. Uma DMA que acerta parte das vezes nao e' validada.");
    } else if (copiasOk == 0) {
        BcInfo("Nenhuma copia moveu dados, apesar da fence ser escrita pela GPU:");
        BcInfo("o motor processa a FENCE mas nao o SDMA_OP_COPY.");
    }

    /*-----------------------------------------------------------------------
      Estado interno da engine — separa "nao executou" de "nao pode executar"
    -----------------------------------------------------------------------*/
    printf("\n--- Estado interno da SDMA0 ---\n");
    {
        ULONG f32  = BcRegLer(h, T_SDMA0_F32_CNTL);
        ULONG rbc  = BcRegLer(h, T_SDMA0_GFX_RB_CNTL);
        ULONG base = BcRegLer(h, T_SDMA0_GFX_RB_BASE);
        ULONG rptr = BcRegLer(h, T_SDMA0_GFX_RB_RPTR);
        ULONG wptr = BcRegLer(h, T_SDMA0_GFX_RB_WPTR);

        BcMedido("F32_CNTL     = 0x%08X  (bit0 HALT = %u)", f32, f32 & 1u);
        BcMedido("GFX_RB_CNTL  = 0x%08X  (bit0 RB_ENABLE = %u)", rbc, rbc & 1u);
        BcMedido("GFX_RB_BASE  = 0x%08X", base);
        BcMedido("GFX_RB_RPTR  = 0x%08X", rptr);
        BcMedido("GFX_RB_WPTR  = 0x%08X", wptr);
        BcMedido("FREEZE       = 0x%08X", BcRegLer(h, T_SDMA0_FREEZE));
        BcMedido("STATUS_REG   = 0x%08X", BcRegLer(h, T_SDMA0_STATUS_REG));
        BcMedido("UTCL1_RD/WR  = 0x%08X / 0x%08X",
                 BcRegLer(h, T_SDMA0_UTCL1_RD_STAT),
                 BcRegLer(h, T_SDMA0_UTCL1_WR_STAT));

        if (((f32 & 1u) == 0) && ((rbc & 1u) != 0) && base != 0 &&
            wptr != rptr && rptr == 0) {
            BcInfo("Motor NAO halted, ring habilitado e programado, WPTR");
            BcInfo("avancando, RPTR congelado em zero.");
            BcInfo("");
            BcInfo("Essa combinacao e' engine SEM MICROCODIGO: o bit de halt");
            BcInfo("limpo nao significa que exista programa para executar.");
            BcInfo("Coerente com Hw_Fw_SDMA0 = TEE_ERROR_ITEM_NOT_FOUND.");
            BcInfo("");
            BcInfo("Mesmo bloqueio do CP. Nao ha' o que corrigir no pacote nem");
            BcInfo("no anel — e' a Fase 3 (carga de microcodigo).");
        }
    }

    printf("\n--- Avaliacao ---\n");
    BcProva(cp.RptrAfter == cp.WptrTarget,
            "Prova 4: RPTR alcancou o WPTR (frame consumido pelo motor)");
    BcProva(fencesOk == TENTATIVAS_COPIA,
            "Prova 5: a GPU escreveu a fence em TODAS as tentativas");
    BcProva(conteudoOk,
            "Prova 6: conteudo correto em TODAS as tentativas (sem intermitencia)");
    BcProva(fillOk,
            "Diagnostico: SDMA CONST_FILL escreve o padrao no destino");
    BcProva(cp.DurationUs > 0,
            "Duracao mensuravel");

    return BcVeredito();
}
