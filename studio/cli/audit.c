/*
 * audit.c — auditoria completa da BC-250.
 *
 * Objetivo: validar de uma vez tudo que ja foi conquistado e levantar, na
 * mesma passada, o estado de que a proxima etapa vai precisar.
 *
 * Principio de projeto: REDUNDANCIA. Cada fato importante e verificado por
 * mais de um caminho, e blocos diferentes sao cruzados entre si. Um valor
 * lido isolado nao prova nada — foi assim que "RPTR alcancou WPTR" passou por
 * prova de execucao durante horas ate o parsetest derrubar.
 *
 * As checagens de coerencia (bloco C) existem porque a falha mais cara ate
 * agora nao foi um registrador errado, e sim dois blocos configurados com
 * visoes diferentes do mesmo endereco.
 */

#include "audit.h"

/* ------------------------------------------------------------------ */
/* Contabilidade                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int pass;
    int fail;
    int warn;
    int info;
} AUDIT_TALLY;

static AUDIT_TALLY g_Tally;

#define AUD_PASS 0
#define AUD_FAIL 1
#define AUD_WARN 2
#define AUD_INFO 3

static void AuditResult(int kind, const char *fmt, ...)
{
    va_list ap;
    const char *tag;

    switch (kind) {
        case AUD_PASS: tag = "  [ ok ] "; g_Tally.pass++; break;
        case AUD_FAIL: tag = "  [FALHA] "; g_Tally.fail++; break;
        case AUD_WARN: tag = "  [aviso] "; g_Tally.warn++; break;
        default:       tag = "  [ .. ] "; g_Tally.info++; break;
    }

    fputs(tag, stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static void AuditSection(const char *title)
{
    printf("\n=== %s ===\n", title);
}

/* Leitura de registrador com valor default em caso de erro, para a auditoria
 * nunca abortar no meio. */
static uint32_t Rd(BC250_CTX *ctx, uint32_t off)
{
    uint32_t v = 0xFFFFFFFFu;
    Bc250RegRead(ctx, off, &v);
    return v;
}

static uint32_t RdVram(BC250_CTX *ctx, uint32_t off)
{
    uint32_t v = 0xFFFFFFFFu;
    Bc250VramRead(ctx, off, &v);
    return v;
}

/* ------------------------------------------------------------------ */
/* Bloco A — mapa de registradores                                     */
/*                                                                     */
/* Redundancia: dois registradores independentes com valor conhecido do */
/* Linux, mais um teste de escrita/leitura que so passa no offset certo.*/
/* ------------------------------------------------------------------ */

static void AuditRegisterMap(BC250_CTX *ctx)
{
    uint32_t v, rb, saved;
    static const uint32_t pats[] = { 0xCAFEBABEu, 0x12345678u, 0xFFFFFFFFu, 0x00000000u };
    int i, ok;

    AuditSection("A. Mapa de registradores (convencao SOC15)");

    v = Rd(ctx, 0x00000);
    if (v == 0x9FFF9700u) {
        AuditResult(AUD_PASS, "GPU_ID = 0x%08X (BC-250 confirmada)", v);
    } else {
        AuditResult(AUD_FAIL, "GPU_ID = 0x%08X, esperado 0x9FFF9700 — BAR5 nao responde", v);
        return;
    }

    /* Prova 1: registrador de harvest, valor identico ao que o Linux le. */
    v = Rd(ctx, 0x089BC);
    if (v == 0xFFF80000u) {
        AuditResult(AUD_PASS, "CC_GC_SHADER_ARRAY_CONFIG (0x089BC) = 0x%08X, bate com o Linux", v);
    } else {
        AuditResult(AUD_FAIL, "CC_GC_SHADER_ARRAY_CONFIG (0x089BC) = 0x%08X, esperado 0xFFF80000", v);
    }

    /* Prova 2: registrador independente, tambem com valor conhecido. */
    v = Rd(ctx, 0x08010);
    if (v == 0x00003028u) {
        AuditResult(AUD_PASS, "GRBM_STATUS (0x08010) = 0x%08X, bate com o Linux", v);
    } else {
        AuditResult(AUD_WARN, "GRBM_STATUS (0x08010) = 0x%08X, Linux le 0x00003028 (pode variar com carga)", v);
    }

    /* Prova 3: escrita/leitura no SCRATCH do segmento 1. O offset legado
     * (0x32D4) mascara o bit 31; o correto (0x30100) nao. */
    saved = Rd(ctx, 0x30100);
    ok = 1;
    for (i = 0; i < (int)(sizeof(pats) / sizeof(pats[0])); i++) {
        Bc250RegWrite(ctx, 0x30100, pats[i], &rb);
        if (rb != pats[i]) { ok = 0; break; }
    }
    Bc250RegWrite(ctx, 0x30100, saved, &rb);
    if (ok) {
        AuditResult(AUD_PASS, "SCRATCH_REG0 (0x30100, seg 1) guarda os 32 bits, inclusive bit 31");
    } else {
        AuditResult(AUD_FAIL, "SCRATCH_REG0 (0x30100) nao guardou 0x%08X — segmento 1 errado?", pats[i]);
    }

    /* Contraprova: o offset legado deve se comportar DIFERENTE. Se ele
     * tambem guardar tudo, nossa distincao nao esta valendo. */
    saved = Rd(ctx, 0x032D4);
    Bc250RegWrite(ctx, 0x032D4, 0xCAFEBABEu, &rb);
    Bc250RegWrite(ctx, 0x032D4, saved, &v);
    if (rb != 0xCAFEBABEu) {
        AuditResult(AUD_PASS, "contraprova: offset legado 0x32D4 mascara bits (leu 0x%08X) — nao e o SCRATCH", rb);
    } else {
        AuditResult(AUD_WARN, "contraprova inconclusiva: 0x32D4 tambem guardou o valor inteiro");
    }
}

/* ------------------------------------------------------------------ */
/* Bloco B — abertura de VRAM pelo BAR0                                */
/*                                                                     */
/* Redundancia: padroes em tres regioes distantes, mais um teste de     */
/* aliasing (duas regioes nao podem ser a mesma memoria).               */
/* ------------------------------------------------------------------ */

static void AuditVram(BC250_CTX *ctx)
{
    static const uint32_t offs[] = { 0x00100000u, 0x08000000u, 0x0FE00000u };
    static const uint32_t pats[] = { 0xCAFEBABEu, 0x5A5A5A5Au, 0xFFFFFFFFu, 0x00000000u };
    uint32_t saved[3], v, rb;
    int i, j, ok = 1;

    AuditSection("B. Abertura de VRAM (BAR0)");

    for (i = 0; i < 3; i++) {
        saved[i] = RdVram(ctx, offs[i]);
        for (j = 0; j < (int)(sizeof(pats) / sizeof(pats[0])); j++) {
            if (Bc250VramWrite(ctx, offs[i], pats[j], &rb)) {
                AuditResult(AUD_FAIL, "escrita na VRAM 0x%08X falhou (rode 'arm write')", offs[i]);
                return;
            }
            if (rb != pats[j]) {
                AuditResult(AUD_FAIL, "VRAM 0x%08X: escrevi 0x%08X, li 0x%08X", offs[i], pats[j], rb);
                ok = 0;
            }
        }
    }
    if (ok) {
        AuditResult(AUD_PASS, "3 regioes x 4 padroes: BAR0 e memoria real e gravavel");
    }

    /* Aliasing: marcas distintas em cada regiao devem sobreviver. Se duas
     * regioes se sobrepoem, a ultima escrita apaga a anterior. */
    for (i = 0; i < 3; i++) {
        Bc250VramWrite(ctx, offs[i], 0xA0000000u + (uint32_t)i, &rb);
    }
    ok = 1;
    for (i = 0; i < 3; i++) {
        v = RdVram(ctx, offs[i]);
        if (v != 0xA0000000u + (uint32_t)i) {
            AuditResult(AUD_FAIL, "aliasing: 0x%08X devia ter 0x%08X, tem 0x%08X",
                        offs[i], 0xA0000000u + (uint32_t)i, v);
            ok = 0;
        }
    }
    if (ok) {
        AuditResult(AUD_PASS, "sem aliasing entre 1 MB, 128 MB e 254 MB");
    }

    for (i = 0; i < 3; i++) {
        Bc250VramWrite(ctx, offs[i], saved[i], &rb);
    }

    /* O conteudo original nao pode ser tudo zero nem tudo 0xFF: isso indicaria
     * janela morta em vez de VRAM. */
    if (saved[1] == 0x00000000u && saved[2] == 0x00000000u) {
        AuditResult(AUD_WARN, "conteudo original zerado nas duas regioes — pode ser janela inativa");
    } else {
        AuditResult(AUD_PASS, "conteudo original nao-trivial (0x%08X, 0x%08X): e VRAM de verdade",
                    saved[1], saved[2]);
    }
}

/* ------------------------------------------------------------------ */
/* Bloco C — coerencia do mapa de memoria da GPU                       */
/*                                                                     */
/* Este bloco existe porque a falha mais cara ate hoje nao foi um       */
/* registrador errado, e sim blocos discordando do mesmo endereco.      */
/* ------------------------------------------------------------------ */

static void AuditMemoryMap(BC250_CTX *ctx, uint64_t *outFbBase)
{
    uint32_t fbBase, fbTop, apLow, apHigh, ctxCntl, ptBase, l2Cntl, agpBase, agpTop, agpBot;
    uint64_t fbStart, fbEnd, apStart, apEnd;

    AuditSection("C. Coerencia do mapa de memoria (GCVM / GCMC)");

    fbBase  = Rd(ctx, 0x0A600);
    fbTop   = Rd(ctx, 0x0A604);
    apLow   = Rd(ctx, 0x0A614);
    apHigh  = Rd(ctx, 0x0A618);
    agpBase = Rd(ctx, 0x0A610);
    agpTop  = Rd(ctx, 0x0A608);
    agpBot  = Rd(ctx, 0x0A60C);
    ctxCntl = Rd(ctx, 0x0A200);
    ptBase  = Rd(ctx, 0x0A3AC);
    l2Cntl  = Rd(ctx, 0x0A100);

    fbStart = (uint64_t)fbBase << 24;
    fbEnd   = ((uint64_t)fbTop + 1) << 24;
    apStart = (uint64_t)apLow  << 18;
    apEnd   = (uint64_t)apHigh << 18;

    if (outFbBase) *outFbBase = fbStart;

    AuditResult(AUD_INFO, "FB_LOCATION  : 0x%04X..0x%04X -> VRAM em [0x%llX, 0x%llX)  %llu MB",
                fbBase, fbTop, (unsigned long long)fbStart, (unsigned long long)fbEnd,
                (unsigned long long)((fbEnd - fbStart) >> 20));
    AuditResult(AUD_INFO, "SYSTEM_APERT.: 0x%06X..0x%06X -> [0x%llX, 0x%llX)",
                apLow, apHigh, (unsigned long long)apStart, (unsigned long long)apEnd);
    AuditResult(AUD_INFO, "AGP          : base 0x%08X top 0x%08X bot 0x%08X", agpBase, agpTop, agpBot);

    /* Cruzamento 1: a abertura precisa CONTER o frame buffer. E assim que o
     * gmc_v10_0_init_system_aperture programa (min/max das faixas). */
    if (fbEnd <= fbStart) {
        AuditResult(AUD_FAIL, "FB_LOCATION invalido (top <= base)");
    } else if (apStart <= fbStart && apEnd >= fbEnd) {
        AuditResult(AUD_PASS, "a abertura de sistema contem o frame buffer");
    } else {
        AuditResult(AUD_FAIL, "a abertura NAO contem o frame buffer — blocos discordam do "
                              "endereco da VRAM; acesso da GPU a VRAM vai falhar tradução");
    }

    /* Cruzamento 2: a base que o DRIVER usa precisa ser a mesma do hardware. */
    if ((uint64_t)BC250_VRAM_GPU_BASE == fbStart) {
        AuditResult(AUD_PASS, "BC250_VRAM_GPU_BASE (0x%llX) == FB_LOCATION do hardware",
                    (unsigned long long)BC250_VRAM_GPU_BASE);
    } else {
        AuditResult(AUD_FAIL, "BC250_VRAM_GPU_BASE (0x%llX) != FB_LOCATION (0x%llX) — "
                              "o driver e o hardware discordam de onde fica a VRAM",
                    (unsigned long long)BC250_VRAM_GPU_BASE, (unsigned long long)fbStart);
    }

    /* Cruzamento 3: a abertura nao deveria cobrir enderecos baixos de RAM do
     * sistema sem GART montado — e como uma escrita da GPU vai parar na
     * memoria do host (bugcheck 0x1A). */
    if (apStart == 0 && apEnd > 0x100000000ull && ptBase == 0) {
        AuditResult(AUD_WARN, "abertura cobre [0, %llu GB) com PT_BASE=0: escrita da GPU em "
                              "endereco baixo pode atingir RAM do sistema",
                    (unsigned long long)(apEnd >> 30));
    }

    AuditResult(AUD_INFO, "GCVM_CONTEXT0_CNTL = 0x%08X  ENABLE_CONTEXT=%u  PAGE_TABLE_DEPTH=%u",
                ctxCntl, ctxCntl & 1u, (ctxCntl >> 1) & 3u);
    AuditResult(AUD_INFO, "GCVM_CONTEXT0_PT_BASE_LO = 0x%08X", ptBase);
    AuditResult(AUD_INFO, "GCVM_L2_CNTL = 0x%08X", l2Cntl);

    if ((ctxCntl & 1u) && ptBase == 0) {
        AuditResult(AUD_FAIL, "ENABLE_CONTEXT=1 com PT_BASE=0: operandos de endereco vindos "
                              "de PACOTE falham a traducao em silencio. Medido nesta placa: "
                              "com o contexto ligado o WRITE_LINEAR nao grava; desligado, grava. "
                              "Desligue o contexto ate haver GART com PT_BASE valido.");
    } else if ((ctxCntl & 1u) == 0) {
        AuditResult(AUD_PASS, "contexto 0 desligado: a abertura de sistema resolve os "
                              "operandos de pacote (estado correto sem GART)");
    }
}

/* ------------------------------------------------------------------ */
/* Bloco D — PSP                                                       */
/* ------------------------------------------------------------------ */

static void AuditPsp(BC250_CTX *ctx)
{
    BC250DBG_PSP_STATUS_IO st;

    AuditSection("D. Mailbox GPCOM do PSP");

    if (Bc250PspStatus(ctx, &st)) {
        AuditResult(AUD_FAIL, "IOCTL de status do PSP falhou — driver antigo instalado?");
        return;
    }

    if (st.C2pmsg64 & BC250_PSP_TOS_READY) {
        AuditResult(AUD_PASS, "C2PMSG_64 = 0x%08X — TOS pronto (bit 31)", st.C2pmsg64);
    } else {
        AuditResult(AUD_FAIL, "C2PMSG_64 = 0x%08X — TOS nao esta pronto", st.C2pmsg64);
    }

    if ((st.C2pmsg64 & 0xFFFFu) != 0) {
        AuditResult(AUD_WARN, "16 bits baixos de C2PMSG_64 = 0x%04X (comando pendente ou nao-ACK)",
                    st.C2pmsg64 & 0xFFFFu);
    }

    AuditResult(AUD_INFO, "C2PMSG_81 (status/heartbeat do SOS) = 0x%08X", st.C2pmsg81);

    if (st.RingCreated) {
        uint64_t regRing = ((uint64_t)st.RingHi << 32) | st.RingLo;
        AuditResult(AUD_INFO, "anel criado por nos em PA 0x%llX", (unsigned long long)st.RingPhysical);
        /* Redundancia: o que o PSP tem registrado precisa ser o nosso anel. */
        if (regRing == st.RingPhysical) {
            AuditResult(AUD_PASS, "o anel registrado no PSP e o nosso (0x%llX)",
                        (unsigned long long)regRing);
        } else {
            AuditResult(AUD_FAIL, "PSP aponta para 0x%llX mas nosso anel esta em 0x%llX — "
                                  "anel de sessao anterior ainda registrado",
                        (unsigned long long)regRing, (unsigned long long)st.RingPhysical);
        }
    } else {
        AuditResult(AUD_INFO, "anel GPCOM ainda nao criado nesta sessao ('psp init')");
    }
}

/* ------------------------------------------------------------------ */
/* Bloco E — microcodigo e microcontrolador F32                        */
/*                                                                     */
/* Redundancia: checksum, bits de controle, e o PC amostrado em duas    */
/* janelas separadas por uma pausa. Um PC parado pode ser coincidencia  */
/* numa amostra curta; em duas janelas, nao.                            */
/* ------------------------------------------------------------------ */

static void AuditF32(BC250_CTX *ctx)
{
    uint32_t checksum, f32cntl, st2, pc, lo, hi, distintos;
    uint32_t amostras[24];
    int i, j, novo;

    AuditSection("E. Microcodigo e microcontrolador F32 da SDMA0");

    checksum = Rd(ctx, 0x04A24);
    f32cntl  = Rd(ctx, 0x04A28);

    if (checksum != 0 && checksum != 0xFFFFFFFFu) {
        AuditResult(AUD_PASS, "UCODE_CHECKSUM = 0x%08X (nao-zero: bytes entraram na instr. RAM)", checksum);
    } else {
        AuditResult(AUD_FAIL, "UCODE_CHECKSUM = 0x%08X — microcodigo nao foi carregado", checksum);
    }

    AuditResult(AUD_INFO, "F32_CNTL = 0x%08X  HALT=%u  RESET=%u", f32cntl, f32cntl & 1u, (f32cntl >> 9) & 1u);
    if (f32cntl & 1u) {
        AuditResult(AUD_FAIL, "F32 esta em HALT — nada vai executar");
    }

    /* Faixa e enderecos distintos, nao transicoes. Um laco de 3 instrucoes no
     * ponto de entrada gera transicoes e parece "em movimento", mas e init
     * travado. Medido: firmware sadio cobre 0x0140..0x03E0; travado oscila
     * entre 0x000E e 0x0010. */
    lo = 0x3FFF; hi = 0; distintos = 0;
    for (i = 0; i < 24; i++) {
        st2 = Rd(ctx, 0x04A60);
        pc = (st2 >> 2) & 0x3FFFu;
        amostras[i] = pc;
        if (pc < lo) lo = pc;
        if (pc > hi) hi = pc;
        for (j = 0, novo = 1; j < i; j++) if (amostras[j] == pc) { novo = 0; break; }
        if (novo) distintos++;
    }

    AuditResult(AUD_INFO, "PC: faixa 0x%04X..0x%04X, %u enderecos distintos em 24 amostras",
                lo, hi, distintos);

    if (distintos <= 1) {
        AuditResult(AUD_FAIL, "PC congelado em 0x%04X: nao ha microcodigo executando", lo);
    } else if (hi < 0x0080 && (hi - lo) < 0x0020) {
        AuditResult(AUD_FAIL, "PC preso no inicio (0x%04X..0x%04X): init do firmware travado, "
                              "nao e o laco principal", lo, hi);
    } else if ((hi - lo) < 0x0080) {
        AuditResult(AUD_WARN, "PC em faixa estreita (0x%04X..0x%04X): pode ser laco de espera", lo, hi);
    } else {
        AuditResult(AUD_PASS, "PC cobre 0x%04X..0x%04X com %u enderecos: laco principal rodando",
                    lo, hi, distintos);
    }
}

/* ------------------------------------------------------------------ */
/* Bloco F — anel da SDMA0                                             */
/*                                                                     */
/* Quatro fontes para os ponteiros: registrador e memoria, para cada    */
/* ponteiro. Elas precisam concordar entre si.                          */
/* ------------------------------------------------------------------ */

static void AuditSdmaRing(BC250_CTX *ctx, uint64_t fbBase)
{
    BC250DBG_SDMA_STATUS_IO st;
    uint32_t rbCntl, rbBaseReg, rbSize;
    uint64_t rbBaseGpu, esperado;

    AuditSection("F. Anel da SDMA0");

    if (Bc250SdmaStatus(ctx, &st)) {
        AuditResult(AUD_FAIL, "IOCTL de status da SDMA falhou");
        return;
    }

    rbCntl = st.RbCntl;
    rbSize = (rbCntl >> 1) & 0x1Fu;

    AuditResult(AUD_INFO, "RB_CNTL = 0x%08X  ENABLE=%u  RB_SIZE=%u (%u dwords)  RPTR_WB=%u",
                rbCntl, rbCntl & 1u, rbSize, 1u << rbSize, (rbCntl >> 12) & 1u);

    if ((rbCntl & 1u) == 0) {
        AuditResult(AUD_FAIL, "anel desabilitado — rode 'sdma init'");
        return;
    }
    if ((1u << rbSize) * 4u != 4096u) {
        AuditResult(AUD_WARN, "RB_SIZE=%u corresponde a %u bytes; o anel alocado tem 4096",
                    rbSize, (1u << rbSize) * 4u);
    } else {
        AuditResult(AUD_PASS, "RB_SIZE bate com o anel de 4 KB");
    }

    if (((rbCntl >> 12) & 1u) == 0) {
        AuditResult(AUD_WARN, "RPTR_WRITEBACK desabilitado: o progresso real nao fica visivel");
    }

    /* Redundancia: RB_BASE lido do registrador contra o esperado. */
    rbBaseReg = Rd(ctx, 0x04B84);
    rbBaseGpu = (uint64_t)rbBaseReg << 8;
    esperado  = fbBase + BC250_SDMA_RING_VRAM_DEFAULT;

    AuditResult(AUD_INFO, "RB_BASE = 0x%08X -> GPU 0x%llX", rbBaseReg, (unsigned long long)rbBaseGpu);

    if (rbBaseGpu == esperado) {
        AuditResult(AUD_PASS, "RB_BASE aponta para VRAM offset 0x%X segundo o FB_LOCATION do hardware",
                    BC250_SDMA_RING_VRAM_DEFAULT);
    } else {
        AuditResult(AUD_FAIL, "RB_BASE = 0x%llX mas pelo FB_LOCATION deveria ser 0x%llX",
                    (unsigned long long)rbBaseGpu, (unsigned long long)esperado);
    }

    AuditResult(AUD_INFO, "WPTR reg 0x%08X | WPTR shadow 0x%08X", st.Wptr, st.WptrShadow);
    AuditResult(AUD_INFO, "RPTR reg 0x%08X | RPTR writeback 0x%08X", st.Rptr, st.RptrWriteback);

    if (st.Wptr == st.WptrShadow) {
        AuditResult(AUD_PASS, "WPTR do registrador e do shadow concordam");
    } else {
        AuditResult(AUD_FAIL, "WPTR do registrador (0x%08X) difere do shadow (0x%08X)",
                    st.Wptr, st.WptrShadow);
    }

    if (st.RptrWriteback == 0 && st.Wptr != 0) {
        AuditResult(AUD_WARN, "writeback do RPTR em 0 com %u bytes submetidos: "
                              "a engine ainda nao reportou progresso nenhum",
                    st.Wptr - st.RptrWriteback);
    }
}

/* ------------------------------------------------------------------ */
/* Bloco G — a engine decodifica pacotes?                              */
/*                                                                     */
/* Distingue TRES casos, nao dois. A versao anterior deste teste        */
/* confundia "parou esperando" com "nunca comecou".                     */
/* ------------------------------------------------------------------ */

static void AuditPacketDecode(BC250_CTX *ctx)
{
    BC250DBG_SDMA_RAW_IO io;
    uint32_t rptrAntes, wptrDepois, rptrDepois;
    int i;

    AuditSection("G. A engine decodifica os pacotes?");

    /* Passo 1: NOPs bem formados. Se nem isso avanca, a engine nao esta
     * processando o anel e nao adianta testar pacote malformado. */
    memset(&io, 0, sizeof(io));
    io.DwordCount = 4;
    for (i = 0; i < 4; i++) io.Data[i] = 0;
    if (Bc250SdmaRaw(ctx, &io)) {
        AuditResult(AUD_FAIL, "submissao crua falhou (anel inicializado? 'arm write'?)");
        return;
    }
    rptrAntes  = io.RptrBefore;
    wptrDepois = io.Wptr;
    rptrDepois = io.RptrAfter;

    AuditResult(AUD_INFO, "4 NOPs: RPTR 0x%X -> 0x%X, WPTR 0x%X", rptrAntes, rptrDepois, wptrDepois);

    if (rptrDepois == rptrAntes) {
        AuditResult(AUD_FAIL, "o RPTR nao saiu do lugar nem com NOPs validos: "
                              "a engine nao esta consumindo o anel");
        AuditResult(AUD_INFO, "provaveis causas: leitura do anel falhando traducao (ver bloco H), "
                              "ou o F32 nao enxerga o WPTR");
        return;
    }

    /* Passo 2: pacote deliberadamente incompleto.
     *
     * ATENCAO — este criterio e DEFEITUOSO e nao reprova mais nada.
     * A premissa era que uma engine que decodifica ficaria esperando os dwords
     * que faltam. Ela consome o que esta disponivel, entao "consumiu o pacote
     * incompleto" nao distingue nada. Provamos que a engine DECODIFICA por
     * outro caminho: um pacote SRBM_WRITE de 3 dwords em 0xE0 deixou o RPTR
     * parado em 0xE8, ou seja, no meio dele.
     *
     * Fica como informacao, nunca como veredito. O veredito de execucao e o
     * bloco I (escrita chegando na memoria). */
    memset(&io, 0, sizeof(io));
    io.DwordCount = 1;
    io.Data[0] = 0u | (8u << 16);   /* SDMA_OP_NOP anunciando 8 dwords extras */
    if (Bc250SdmaRaw(ctx, &io)) {
        AuditResult(AUD_WARN, "submissao do pacote incompleto falhou");
        return;
    }

    AuditResult(AUD_INFO, "NOP incompleto: RPTR 0x%X -> 0x%X, WPTR 0x%X (informativo; "
                          "este criterio nao vale como veredito — ver docs secao 7)",
                io.RptrBefore, io.RptrAfter, io.Wptr);

    if (io.RptrAfter != io.Wptr && io.RptrAfter == io.RptrBefore) {
        /* Se ela parar, completamos para nao deixar a fila travada. */
        memset(&io, 0, sizeof(io));
        io.DwordCount = 8;
        for (i = 0; i < 8; i++) io.Data[i] = 0;
        Bc250SdmaRaw(ctx, &io);
        AuditResult(AUD_INFO, "o RPTR parou no pacote; completado para liberar a fila");
    }
}

/* ------------------------------------------------------------------ */
/* Bloco H — falhas latcheadas                                         */
/* ------------------------------------------------------------------ */

static void AuditFaults(BC250_CTX *ctx)
{
    uint32_t rd0, rd1, wr0, wr1, l2st, l2lo, l2hi, stat;
    uint64_t addr;

    AuditSection("H. Falhas de traducao e protecao");

    rd0  = Rd(ctx, 0x04A8C);
    rd1  = Rd(ctx, 0x04A90);
    wr0  = Rd(ctx, 0x04A94);
    wr1  = Rd(ctx, 0x04A98);
    l2st = Rd(ctx, 0x0A130);
    l2lo = Rd(ctx, 0x0A134);
    l2hi = Rd(ctx, 0x0A138);
    stat = Rd(ctx, 0x04A14);

    if (rd0 || rd1) {
        addr = (((uint64_t)(rd1 & 0xFu)) << 32 | rd0) << 12;
        AuditResult(AUD_WARN, "UTCL1 LEITURA: endereco 0x%llX, VMID %u, ativo=%u",
                    (unsigned long long)addr, (rd1 >> 4) & 0xFu, (rd1 >> 26) & 3u);
    } else {
        AuditResult(AUD_PASS, "sem falha de traducao em leitura");
    }

    if (wr0 || wr1) {
        addr = (((uint64_t)(wr1 & 0xFu)) << 32 | wr0) << 12;
        AuditResult(AUD_WARN, "UTCL1 ESCRITA: endereco 0x%llX, VMID %u, ativo=%u",
                    (unsigned long long)addr, (wr1 >> 4) & 0xFu, (wr1 >> 26) & 3u);
    } else {
        AuditResult(AUD_PASS, "sem falha de traducao em escrita");
    }

    if (l2st) {
        AuditResult(AUD_WARN, "GCVM_L2_PROTECTION_FAULT: status 0x%08X, addr 0x%08X%08X",
                    l2st, l2hi, l2lo);
    } else {
        AuditResult(AUD_PASS, "sem falha de protecao na L2");
    }

    AuditResult(AUD_INFO, "SDMA0_STATUS_REG = 0x%08X  IDLE=%u RB_EMPTY=%u PACKET_READY=%u "
                          "RB_MC_RREQ_IDLE=%u",
                stat, stat & 1u, (stat >> 2) & 1u, (stat >> 12) & 1u, (stat >> 17) & 1u);

    if (((stat >> 17) & 1u) == 0) {
        AuditResult(AUD_WARN, "RB_MC_RREQ_IDLE=0: leitura do anel pendurada, nao retornou");
    }
}

/* ------------------------------------------------------------------ */
/* Bloco I — escrita em memoria pela SDMA                              */
/*                                                                     */
/* Redundancia: tres enderecos, leitura direta, invalidacao do HDP e    */
/* releitura, e varredura da janela caso o dado tenha ido para outro    */
/* lugar.                                                              */
/* ------------------------------------------------------------------ */

static void AuditSdmaWrite(BC250_CTX *ctx)
{
    static const uint32_t offs[] = { 0x02000000u, 0x0A000000u, 0x0E800000u };
    BC250DBG_SDMA_OP_IO io;
    BC250DBG_VRAM_SCAN_IO scan;
    uint32_t magic, v, orig;
    int i, gravou = 0;

    AuditSection("I. Escrita em memoria pela SDMA");

    for (i = 0; i < 3; i++) {
        magic = 0x5EED0000u + (uint32_t)i;
        orig  = RdVram(ctx, offs[i]);

        memset(&io, 0, sizeof(io));
        io.Op = BC250DBG_SDMA_OP_WRITE;
        io.DstOffset = offs[i];
        io.DwordCount = 1;
        io.Data[0] = magic;

        if (Bc250SdmaOp(ctx, &io)) {
            AuditResult(AUD_FAIL, "IOCTL de operacao da SDMA falhou");
            return;
        }

        v = RdVram(ctx, offs[i]);
        if (v == magic) {
            AuditResult(AUD_PASS, "WRITE_LINEAR em 0x%08X gravou (era 0x%08X)", offs[i], orig);
            gravou = 1;
            continue;
        }

        Bc250HdpInvalidate(ctx);
        v = RdVram(ctx, offs[i]);
        if (v == magic) {
            AuditResult(AUD_PASS, "WRITE_LINEAR em 0x%08X gravou, visivel apos invalidar o HDP", offs[i]);
            gravou = 1;
            continue;
        }

        /* Foi parar em outro lugar? Varremos uma janela de 32 MB em volta do
         * alvo, nao os 256 MB: a varredura e por MMIO e o custo por dword e
         * alto. Para a busca completa existe 'vram scan'. */
        memset(&scan, 0, sizeof(scan));
        scan.Value  = magic;
        scan.Start  = (offs[i] > 0x01000000u) ? (offs[i] - 0x01000000u) : 0u;
        scan.Length = 0x02000000u;
        if (Bc250VramScan(ctx, &scan) == 0) {
            if (scan.HitCount == 0) {
                AuditResult(AUD_INFO, "0x%08X nao aparece na janela de 32 MB em volta do alvo", magic);
            } else {
                uint32_t k, n = scan.HitCount;
                if (n > BC250DBG_VRAM_SCAN_MAX_HITS) n = BC250DBG_VRAM_SCAN_MAX_HITS;
                printf("  [ .. ] 0x%08X aparece %u vez(es):", magic, scan.HitCount);
                for (k = 0; k < n; k++) printf(" 0x%08X", scan.Hits[k]);
                printf("  (dentro do anel = o proprio pacote)\n");
                g_Tally.info++;
            }
        }
    }

    if (!gravou) {
        AuditResult(AUD_FAIL, "nenhum dos 3 enderecos recebeu a escrita. ESTE e o veredito "
                              "de execucao — sem ele nada da SDMA funciona. Ver blocos C e H.");
    }
}

/* ------------------------------------------------------------------ */
/* Bloco J — levantamento para a proxima etapa (bloco grafico)         */
/* ------------------------------------------------------------------ */

static void AuditGfxReadiness(BC250_CTX *ctx)
{
    BC250DBG_INFO in;
    uint32_t grbm, cpMeCntl, cpRb0Base, wgpMask, ccArray;

    AuditSection("J. Preparo para a proxima etapa (bloco grafico / PM4)");

    grbm      = Rd(ctx, 0x08010);
    cpMeCntl  = Rd(ctx, 0x086D8);
    cpRb0Base = Rd(ctx, 0x0C100);
    wgpMask   = Rd(ctx, 0x0935C);
    ccArray   = Rd(ctx, 0x089BC);

    AuditResult(AUD_INFO, "GRBM_STATUS = 0x%08X  GUI_ACTIVE=%u", grbm, (grbm >> 31) & 1u);
    AuditResult(AUD_INFO, "CP_ME_CNTL (0x086D8) = 0x%08X", cpMeCntl);
    AuditResult(AUD_INFO, "CP_RB0_BASE (0x0C100) = 0x%08X", cpRb0Base);
    AuditResult(AUD_INFO, "SPI_PG_ENABLE_STATIC_WGP_MASK (0x0935C) = 0x%08X", wgpMask);
    AuditResult(AUD_INFO, "CC_GC_SHADER_ARRAY_CONFIG (0x089BC) = 0x%08X", ccArray);

    if (cpMeCntl == 0xFFFFFFFFu) {
        AuditResult(AUD_WARN, "CP_ME_CNTL le 0xFFFFFFFF: bloco do CP nao responde neste estado");
    }

    if (Bc250Info(ctx, &in) == 0) {
        AuditResult(AUD_INFO, "SMU: features 0x%08X, GfxFreq %u MHz, WGP ativos %u",
                    in.SmuFeatures, in.GfxFreqMhz, in.ActiveWgp);
        if (in.ActiveWgp == 0) {
            AuditResult(AUD_WARN, "0 WGP ativo (GFXOFF): o dominio grafico esta em deep sleep "
                                  "e precisa ser acordado antes de qualquer PM4");
        }
    }
}

/* ------------------------------------------------------------------ */

int Bc250RunAudit(BC250_CTX *ctx)
{
    uint64_t fbBase = 0;

    /* Sem buffer: uma auditoria longa precisa mostrar progresso ao vivo,
     * inclusive quando a saida esta sendo canalizada. */
    setvbuf(stdout, NULL, _IONBF, 0);

    memset(&g_Tally, 0, sizeof(g_Tally));

    printf("========================================================\n");
    printf("  BC-250 — auditoria completa\n");
    printf("========================================================\n");
    printf("Cada fato e conferido por mais de um caminho. Blocos sao\n");
    printf("cruzados entre si: um valor isolado nao prova nada.\n");

    AuditRegisterMap(ctx);
    AuditVram(ctx);
    AuditMemoryMap(ctx, &fbBase);
    AuditPsp(ctx);
    AuditF32(ctx);
    AuditSdmaRing(ctx, fbBase);
    AuditPacketDecode(ctx);
    AuditFaults(ctx);
    AuditSdmaWrite(ctx);
    AuditGfxReadiness(ctx);

    printf("\n========================================================\n");
    printf("  Resumo: %d ok, %d falhas, %d avisos\n",
           g_Tally.pass, g_Tally.fail, g_Tally.warn);
    printf("========================================================\n");

    if (g_Tally.fail == 0) {
        printf("\nNenhuma falha. Se ainda assim algo nao funciona, o teste que\n"
               "falta e o do bloco G — ele e o unico que distingue execucao de\n"
               "ponteiro arrastado.\n");
    } else {
        printf("\nComece pela primeira FALHA da lista: os blocos dependem uns dos\n"
               "outros de cima para baixo, e uma falha em C explica quase sempre\n"
               "as falhas em G e I.\n");
    }

    return g_Tally.fail ? 1 : 0;
}
