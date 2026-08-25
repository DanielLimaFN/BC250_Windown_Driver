/*
 * stress.c — bateria de validacao da SDMA0.
 *
 * O `sdma verify` prova que a engine escreve 4 KB num lugar. Isso nao e
 * suficiente para confiar antes de partir para o bloco grafico. Esta bateria
 * cobre o que o verify nao cobre:
 *
 *   - tamanhos crescentes, ate o limite de 4 MB por pacote da ASIC
 *   - regioes distantes na janela, nao um ponto so
 *   - padrao derivado do ENDERECO, para que dado no lugar errado se denuncie
 *   - a fonte de um COPY permanece intacta
 *   - wraparound do anel de 4 KB, que o verify nunca exercita
 *   - submissoes consecutivas sem reinicializar
 *   - repeticao, para pegar instabilidade
 *
 * O padrao derivado do endereco e o ponto central. Um fill constante nao
 * distingue "escreveu no lugar certo" de "escreveu deslocado" — todo dword
 * tem o mesmo valor. Com dword(X) = semente | (X >> 2), um deslocamento
 * aparece na hora e o proprio conteudo diz de onde veio.
 */

#include "audit.h"

#define SEM_A 0x5A000000u
#define SEM_B 0xB0000000u

static int g_Ok, g_Falhas;

static void Res(int ok, const char *fmt, ...)
{
    va_list ap;
    if (ok) { g_Ok++; fputs("  [ ok ] ", stdout); }
    else    { g_Falhas++; fputs("  [FALHA] ", stdout); }
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

/* Preenche uma regiao com o padrao derivado do endereco, pela CPU. */
static int PreencheCpu(BC250_CTX *ctx, uint32_t off, uint32_t len, uint32_t sem)
{
    BC250DBG_VRAM_PATTERN_IO io;
    uint32_t feito = 0;

    while (feito < len) {
        uint32_t pedaco = len - feito;
        if (pedaco > BC250DBG_VRAM_OP_MAX_BYTES) pedaco = BC250DBG_VRAM_OP_MAX_BYTES;
        memset(&io, 0, sizeof(io));
        io.Pattern = BC250DBG_PAT_ADDR;
        io.Value   = sem;
        io.Start   = off + feito;
        io.Length  = pedaco;
        if (Bc250VramFillPattern(ctx, &io)) return -1;
        feito += pedaco;
    }
    return 0;
}

/* Confere uma regiao contra um padrao, devolvendo o numero de divergencias.
 *
 * Faz uma segunda tentativa apos invalidar o HDP e esperar. Se a primeira
 * falha e a segunda passa, o dado da SDMA chegou na memoria mas ainda nao
 * estava visivel pela abertura do BAR0 — e isso e informacao, nao ruido:
 * significa que um teste que le imediatamente depois da submissao pode dar
 * falso negativo. O relatorio diz quando a segunda tentativa foi necessaria. */
static long Confere(BC250_CTX *ctx, uint32_t off, uint32_t len,
                    uint32_t pat, uint32_t valor, int mostrar)
{
    BC250DBG_VRAM_PATTERN_IO io;
    uint32_t feito;
    long ruins;
    int tentativa, mostrados;

    for (tentativa = 0; tentativa < 2; tentativa++) {
        if (tentativa == 1) {
            Bc250HdpInvalidate(ctx);
            Sleep(20);
        }

        feito = 0; ruins = 0; mostrados = 0;
        while (feito < len) {
            uint32_t pedaco = len - feito;
            if (pedaco > BC250DBG_VRAM_OP_MAX_BYTES) pedaco = BC250DBG_VRAM_OP_MAX_BYTES;
            memset(&io, 0, sizeof(io));
            io.Pattern = pat;
            io.Value   = valor;
            io.Start   = off + feito;
            io.Length  = pedaco;
            if (Bc250VramVerifyPattern(ctx, &io)) return -1;
            ruins += (long)io.BadCount;
            if (mostrar && tentativa == 1 && io.BadCount && mostrados < 3) {
                uint32_t k, n = io.BadCount;
                if (n > BC250DBG_VRAM_BAD_SAMPLES) n = BC250DBG_VRAM_BAD_SAMPLES;
                for (k = 0; k < n && mostrados < 3; k++, mostrados++) {
                    printf("         0x%08X: lido 0x%08X, esperado 0x%08X\n",
                           io.BadOffset[k], io.BadGot[k], io.BadWant[k]);
                }
            }
            feito += pedaco;
        }

        if (ruins == 0) {
            if (tentativa == 1) {
                printf("         (passou apenas na 2a leitura, depois de invalidar o HDP\n"
                       "          e esperar 20 ms — o dado chega com atraso)\n");
            }
            return 0;
        }
    }

    return ruins;
}

/* ------------------------------------------------------------------ */
/* 1. CONST_FILL em tamanhos crescentes, regioes distantes             */
/* ------------------------------------------------------------------ */

static void TesteFill(BC250_CTX *ctx)
{
    struct { uint32_t off; uint32_t len; const char *nome; } casos[] = {
        { 0x01000000u, 0x1000u,    "4 KB  em 16 MB"  },
        { 0x04000000u, 0x10000u,   "64 KB em 64 MB"  },
        { 0x08000000u, 0x100000u,  "1 MB  em 128 MB" },
        { 0x0B000000u, 0x400000u,  "4 MB  em 176 MB (limite da ASIC)" },
    };
    int n = (int)(sizeof(casos) / sizeof(casos[0])), i;

    printf("\n=== 1. CONST_FILL crescente ===\n");

    for (i = 0; i < n; i++) {
        BC250DBG_SDMA_OP_IO io;
        uint32_t magico = 0xF0000000u + (uint32_t)i;
        long ruins;

        /* Suja com o padrao de endereco: se o fill nao cobrir tudo, sobra
         * padrao antigo e a contagem de divergencias mostra quanto. */
        if (PreencheCpu(ctx, casos[i].off, casos[i].len, SEM_A)) {
            Res(0, "%s: falha ao sujar a regiao", casos[i].nome);
            continue;
        }

        memset(&io, 0, sizeof(io));
        io.Op = BC250DBG_SDMA_OP_FILL;
        io.DstOffset = casos[i].off;
        io.Value = magico;
        io.ByteCount = casos[i].len;
        io.FillSize = 2;

        {
            int rc = Bc250SdmaOp(ctx, &io);
            if (rc) {
                Res(0, "%s: IOCTL falhou (erro %d) — anel armado?", casos[i].nome, rc);
                continue;
            }
            if (!io.Completed) {
                Res(0, "%s: submetido mas a engine nao consumiu (RPTR 0x%X, WPTR 0x%X)",
                    casos[i].nome, io.RptrAfter, io.Wptr);
                continue;
            }
        }

        ruins = Confere(ctx, casos[i].off, casos[i].len, BC250DBG_PAT_CONST, magico, 1);
        if (ruins < 0)       Res(0, "%s: falha na verificacao", casos[i].nome);
        else if (ruins == 0) Res(1, "%s: %u dwords todos com 0x%08X",
                                 casos[i].nome, casos[i].len / 4, magico);
        else                 Res(0, "%s: %ld de %u dwords divergiram",
                                 casos[i].nome, ruins, casos[i].len / 4);
    }
}

/* ------------------------------------------------------------------ */
/* 2. COPY com padrao de endereco, e a fonte tem de sobreviver         */
/* ------------------------------------------------------------------ */

static void TesteCopy(BC250_CTX *ctx)
{
    const uint32_t src = 0x02000000u, dst = 0x06000000u, len = 0x40000u; /* 256 KB */
    BC250DBG_SDMA_OP_IO io;
    long ruins;

    printf("\n=== 2. COPY_LINEAR com padrao derivado do endereco ===\n");

    if (PreencheCpu(ctx, src, len, SEM_A)) { Res(0, "falha ao preencher a fonte"); return; }
    if (PreencheCpu(ctx, dst, len, SEM_B)) { Res(0, "falha ao preencher o destino"); return; }

    memset(&io, 0, sizeof(io));
    io.Op = BC250DBG_SDMA_OP_COPY;
    io.SrcOffset = src;
    io.DstOffset = dst;
    io.ByteCount = len;

    if (Bc250SdmaOp(ctx, &io) || !io.Completed) {
        Res(0, "COPY nao concluiu (completed=%u)", io.Completed);
        return;
    }

    /* O destino tem de conter o padrao da FONTE, ou seja, com os valores
     * derivados dos enderecos de origem — nao dos de destino. Isso detecta
     * copia deslocada, que um padrao constante nao detectaria. */
    {
        BC250DBG_VRAM_PATTERN_IO v;
        uint32_t feito = 0;
        long bad = 0;
        while (feito < len) {
            uint32_t pedaco = len - feito;
            uint32_t k, nb;
            if (pedaco > BC250DBG_VRAM_OP_MAX_BYTES) pedaco = BC250DBG_VRAM_OP_MAX_BYTES;
            memset(&v, 0, sizeof(v));
            v.Pattern = BC250DBG_PAT_ADDR;
            /* Deslocamos a semente para que o valor esperado no destino seja
             * o mesmo que a fonte tinha no offset correspondente.
             * cast assinado obrigatorio: (src - dst) e negativo e >> unsigned deslocaria com zeros */
            v.Value   = SEM_A + (uint32_t)(((int32_t)src - (int32_t)dst) / 4);
            v.Start   = dst + feito;
            v.Length  = pedaco;
            if (Bc250VramVerifyPattern(ctx, &v)) { bad = -1; break; }
            bad += (long)v.BadCount;
            nb = v.BadCount; if (nb > 3) nb = 3;
            for (k = 0; k < nb; k++) {
                printf("         0x%08X: lido 0x%08X, esperado 0x%08X\n",
                       v.BadOffset[k], v.BadGot[k], v.BadWant[k]);
            }
            feito += pedaco;
        }
        if (bad < 0)       Res(0, "falha na verificacao do destino");
        else if (bad == 0) Res(1, "destino com %u dwords: cada um com o valor do endereco de origem", len / 4);
        else               Res(0, "destino: %ld de %u dwords divergiram", bad, len / 4);
    }

    /* A fonte nao pode ter sido tocada. */
    ruins = Confere(ctx, src, len, BC250DBG_PAT_ADDR, SEM_A, 1);
    if (ruins == 0) Res(1, "fonte intacta apos a copia");
    else            Res(0, "fonte foi alterada: %ld dwords divergiram", ruins);
}

/* ------------------------------------------------------------------ */
/* 3. Wraparound do anel                                              */
/*                                                                    */
/* O anel tem 4 KB. Cada CONST_FILL sao 5 dwords mais 5 do rinse GCR = */
/* 40 bytes. Cem submissoes dao 4000 bytes: passa do fim do anel e     */
/* forca o wrap, que o verify nunca exercita.                          */
/* ------------------------------------------------------------------ */

static void TesteWrap(BC250_CTX *ctx)
{
    const uint32_t alvo = 0x0D000000u;
    int i, falhou = 0;
    uint32_t v = 0;

    printf("\n=== 3. Wraparound do anel de 4 KB (150 submissoes) ===\n");

    for (i = 0; i < 150; i++) {
        BC250DBG_SDMA_OP_IO io;
        uint32_t magico = 0xC0DE0000u + (uint32_t)i;

        memset(&io, 0, sizeof(io));
        io.Op = BC250DBG_SDMA_OP_FILL;
        io.DstOffset = alvo;
        io.Value = magico;
        io.ByteCount = 0x1000u;
        io.FillSize = 2;

        if (Bc250SdmaOp(ctx, &io) || !io.Completed) {
            Res(0, "submissao %d nao concluiu (completed=%u)", i, io.Completed);
            falhou = 1;
            break;
        }

        /* Confere so um dword por iteracao: o objetivo aqui e o wrap, nao o
         * volume. Cada iteracao usa um valor diferente, entao um pacote
         * perdido no wrap apareceria como valor antigo. */
        Bc250HdpInvalidate(ctx);
        if (Bc250VramRead(ctx, alvo, &v)) { Res(0, "falha na leitura"); falhou = 1; break; }
        if (v != magico) {
            Res(0, "submissao %d: esperado 0x%08X, lido 0x%08X (pacote perdido no wrap?)",
                i, magico, v);
            falhou = 1;
            break;
        }
        if ((i % 50) == 49) { printf("  ... %d submissoes\n", i + 1); }
    }

    if (!falhou) Res(1, "150 submissoes atravessando o fim do anel, todas aplicadas");
}

/* ------------------------------------------------------------------ */
/* 4. Pacotes consecutivos sem reinicializar                          */
/* ------------------------------------------------------------------ */

static void TesteSequencia(BC250_CTX *ctx)
{
    const uint32_t base = 0x03000000u;
    int i, falhou = 0;

    printf("\n=== 4. Fill, copy e fill encadeados na mesma sessao ===\n");

    if (PreencheCpu(ctx, base, 0x10000u, SEM_A)) { Res(0, "falha ao preparar"); return; }

    for (i = 0; i < 3 && !falhou; i++) {
        BC250DBG_SDMA_OP_IO io;
        uint32_t magico = 0xAB000000u + (uint32_t)i;
        long ruins;

        memset(&io, 0, sizeof(io));
        io.Op = BC250DBG_SDMA_OP_FILL;
        io.DstOffset = base;
        io.Value = magico;
        io.ByteCount = 0x10000u;
        io.FillSize = 2;
        if (Bc250SdmaOp(ctx, &io) || !io.Completed) { Res(0, "fill %d falhou", i); falhou = 1; break; }

        memset(&io, 0, sizeof(io));
        io.Op = BC250DBG_SDMA_OP_COPY;
        io.SrcOffset = base;
        io.DstOffset = base + 0x10000u;
        io.ByteCount = 0x10000u;
        if (Bc250SdmaOp(ctx, &io) || !io.Completed) { Res(0, "copy %d falhou", i); falhou = 1; break; }

        ruins = Confere(ctx, base + 0x10000u, 0x10000u, BC250DBG_PAT_CONST, magico, 1);
        if (ruins != 0) { Res(0, "rodada %d: %ld dwords divergiram no destino", i, ruins); falhou = 1; }
    }

    if (!falhou) Res(1, "3 rodadas de fill+copy encadeadas, todas conferindo");
}

/* ------------------------------------------------------------------ */
/* 5. Saude da engine antes e depois                                  */
/* ------------------------------------------------------------------ */

static void TesteSaude(BC250_CTX *ctx, const char *quando)
{
    uint32_t st2, pc, lo = 0x3FFF, hi = 0, ctxCntl = 0, rbCntl = 0, rbBase = 0;
    uint32_t wbAntes = 0, wbDepois = 0;
    BC250DBG_SDMA_RAW_IO io;
    int i;

    printf("\n=== 5. Saude da engine (%s) ===\n", quando);

    for (i = 0; i < 24; i++) {
        st2 = 0;
        Bc250RegRead(ctx, 0x04A60, &st2);
        pc = (st2 >> 2) & 0x3FFFu;
        if (pc < lo) lo = pc;
        if (pc > hi) hi = pc;
    }

    if ((hi - lo) >= 0x0080 && hi >= 0x0080) {
        Res(1, "F32 no laco principal (PC 0x%04X..0x%04X)", lo, hi);
    } else {
        Res(0, "F32 fora do laco principal (PC 0x%04X..0x%04X)", lo, hi);
    }

    Bc250RegRead(ctx, 0x0A200, &ctxCntl);
    if ((ctxCntl & 1u) == 0) {
        Res(1, "contexto GCVM desligado (0x%08X) — operandos de pacote traduzem", ctxCntl);
    } else {
        Res(0, "contexto GCVM LIGADO (0x%08X) sem page table: escritas de pacote vao falhar", ctxCntl);
    }

    /* O anel precisa estar armado NESTA instancia do driver. Um driver
     * recarregado zera o RB_CNTL e o BcSdmaFree solta o anel; sem essa
     * checagem toda submissao falha e o relatorio culpa a engine. */
    Bc250RegRead(ctx, 0x04B80, &rbCntl);
    Bc250RegRead(ctx, 0x04B84, &rbBase);
    if ((rbCntl & 1u) && rbBase != 0) {
        Res(1, "anel armado (RB_CNTL 0x%08X, RB_BASE 0x%08X)", rbCntl, rbBase);
    } else {
        Res(0, "anel NAO armado (RB_CNTL 0x%08X, RB_BASE 0x%08X) — rode 'sdma init'",
            rbCntl, rbBase);
        return;
    }

    /* Sonda VIVA do writeback. Ler o buffer e conferir se e diferente de zero
     * nao serve: logo depois de um 'sdma init' ele e legitimamente zero, e um
     * valor residual de sessao anterior passa como "ativo". O que importa e se
     * ele AVANCA quando submetemos algo. */
    Bc250VramRead(ctx, BC250_SDMA_RPTR_WB_OFFSET, &wbAntes);

    memset(&io, 0, sizeof(io));
    io.DwordCount = 4;
    for (i = 0; i < 4; i++) io.Data[i] = 0;   /* NOPs */
    if (Bc250SdmaRaw(ctx, &io)) {
        Res(0, "sonda: submissao de NOPs falhou no IOCTL");
        return;
    }

    Bc250HdpInvalidate(ctx);
    Bc250VramRead(ctx, BC250_SDMA_RPTR_WB_OFFSET, &wbDepois);

    if (wbDepois != wbAntes) {
        Res(1, "sonda viva: writeback avancou 0x%08X -> 0x%08X", wbAntes, wbDepois);
    } else {
        Res(0, "sonda viva: writeback parado em 0x%08X apos submeter NOPs — "
               "a engine nao esta consumindo o anel", wbAntes);
    }
}

/* ------------------------------------------------------------------ */

int Bc250RunStress(BC250_CTX *ctx)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    g_Ok = 0; g_Falhas = 0;

    printf("========================================================\n");
    printf("  BC-250 — bateria de validacao da SDMA0\n");
    printf("========================================================\n");
    printf("O padrao de dados e derivado do endereco: dado no lugar\n");
    printf("errado se denuncia pelo proprio conteudo.\n");

    TesteSaude(ctx, "antes");
    if (g_Falhas) {
        printf("\nA engine nao esta sadia. Rode 'ciclo.cmd' antes da bateria.\n");
        return 1;
    }

    TesteFill(ctx);
    TesteCopy(ctx);
    TesteWrap(ctx);
    TesteSequencia(ctx);
    TesteSaude(ctx, "depois");

    printf("\n========================================================\n");
    printf("  %d ok, %d falhas\n", g_Ok, g_Falhas);
    printf("========================================================\n");

    if (g_Falhas == 0) {
        printf("\nA SDMA0 aguenta tamanho, regioes distantes, wraparound do\n"
               "anel e sequencias encadeadas. Base solida para o bloco grafico.\n");
    } else {
        printf("\nHa falhas. Nao avance para o GFXOFF antes de entender cada uma:\n"
               "o anel do CP usa o mesmo caminho de memoria.\n");
    }

    return g_Falhas ? 1 : 0;
}
